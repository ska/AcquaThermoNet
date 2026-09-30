#!/usr/bin/env python3
"""Minimal MQTT 3.1.1 broker for local tests, no dependencies.

Supports CONNECT, PUBLISH qos 0/1, retain, SUBSCRIBE with + and #, PINGREQ,
DISCONNECT and the Will. Every PUBLISH is logged as
"<epoch> <topic> <payload>[ [R]]". Not for production: no auth, no QoS 2,
no persistence.

Usage:
  minibroker.py PORT LOGFILE [TLS options]         run the broker (127.0.0.1)
  minibroker.py pub PORT TOPIC PAYLOAD [retain]    publish one message (plain)

TLS options (PORT becomes a TLS listener):
  --tls-cert FILE --tls-key FILE   server certificate and key (PEM)
  --tls-client-ca FILE             require a client certificate signed by this CA
  --plain-port N                   extra plain listener, e.g. for "pub"
"""
import socket, ssl, struct, sys, threading, time

def enc_len(n):
    out = b''
    while True:
        b = n % 128; n //= 128
        out += bytes([b | (0x80 if n else 0)])
        if not n: return out

def read_pkt(sock):
    h = sock.recv(1)
    if not h: return None, None
    mult, n = 1, 0
    while True:
        b = sock.recv(1)[0]
        n += (b & 127) * mult; mult *= 128
        if not b & 128: break
    body = b''
    while len(body) < n:
        chunk = sock.recv(n - len(body))
        if not chunk: return None, None
        body += chunk
    return h[0], body

def mstr(s):
    s = s.encode() if isinstance(s, str) else s
    return struct.pack('!H', len(s)) + s

def match(flt, topic):
    f, t = flt.split('/'), topic.split('/')
    for i, p in enumerate(f):
        if p == '#': return True
        if i >= len(t) or (p != '+' and p != t[i]): return False
    return len(f) == len(t)

def publish_pkt(topic, payload, retain=False):
    body = mstr(topic) + payload
    return bytes([0x30 | (1 if retain else 0)]) + enc_len(len(body)) + body

def listener(port):
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('127.0.0.1', port)); s.listen()
    return s

class Broker:
    def __init__(self, port, log, tls=None, plain_port=None):
        self.clients = {}   # sock -> list of filters
        self.retained = {}
        self.lock = threading.Lock()
        self.log = open(log, 'w', buffering=1)
        self.listeners = [(listener(port), tls)]
        if plain_port:
            self.listeners.append((listener(plain_port), None))

    def route(self, topic, payload, retain):
        self.log.write('%.3f %s %s%s\n' % (time.time(), topic, payload.decode(errors='replace'), ' [R]' if retain else ''))
        with self.lock:
            if retain:
                if payload: self.retained[topic] = payload
                else: self.retained.pop(topic, None)
            for s, flts in list(self.clients.items()):
                if any(match(f, topic) for f in flts):
                    try: s.sendall(publish_pkt(topic, payload))
                    except OSError: pass

    def handle(self, s):
        will = None
        try:
            while True:
                h, body = read_pkt(s)
                if h is None: break
                t = h >> 4
                if t == 1:      # CONNECT
                    i = 2 + struct.unpack('!H', body[:2])[0]
                    flags = body[i + 1]; i += 4
                    ln = struct.unpack('!H', body[i:i+2])[0]; cid = body[i+2:i+2+ln].decode(); i += 2 + ln
                    if flags & 0x04:
                        ln = struct.unpack('!H', body[i:i+2])[0]; wt = body[i+2:i+2+ln].decode(); i += 2 + ln
                        ln = struct.unpack('!H', body[i:i+2])[0]; wm = body[i+2:i+2+ln]; i += 2 + ln
                        will = (wt, wm, bool(flags & 0x20))
                    self.log.write('%.3f CONNECT %s\n' % (time.time(), cid))
                    with self.lock: self.clients[s] = []
                    s.sendall(b'\x20\x02\x00\x00')
                elif t == 3:    # PUBLISH
                    qos = (h >> 1) & 3; retain = bool(h & 1)
                    ln = struct.unpack('!H', body[:2])[0]; topic = body[2:2+ln].decode(); i = 2 + ln
                    if qos:
                        pid = body[i:i+2]; i += 2
                        s.sendall(b'\x40\x02' + pid)
                    self.route(topic, body[i:], retain)
                elif t == 8:    # SUBSCRIBE
                    pid = body[:2]; i = 2; flts = []
                    while i < len(body):
                        ln = struct.unpack('!H', body[i:i+2])[0]; flts.append(body[i+2:i+2+ln].decode()); i += 3 + ln
                    with self.lock: self.clients[s] += flts
                    s.sendall(bytes([0x90]) + enc_len(2 + len(flts)) + pid + b'\x00' * len(flts))
                    for topic, payload in list(self.retained.items()):
                        if any(match(f, topic) for f in flts):
                            s.sendall(publish_pkt(topic, payload, True))
                elif t == 12:   # PINGREQ
                    s.sendall(b'\xd0\x00')
                elif t == 14:   # DISCONNECT
                    will = None; break
        except OSError:
            pass
        with self.lock: self.clients.pop(s, None)
        s.close()
        if will: self.route(*will)

    def serve(self, srv, tls):
        while True:
            c, _ = srv.accept()
            threading.Thread(target=self.accept, args=(c, tls), daemon=True).start()

    def accept(self, c, tls):
        if tls:
            try:
                c = tls.wrap_socket(c, server_side=True)
            except (ssl.SSLError, OSError) as e:
                self.log.write('%.3f TLS handshake failed: %s\n' % (time.time(), e))
                c.close()
                return
            peer = c.getpeercert()
            self.log.write('%.3f TLS ok %s client cert %s\n' % (time.time(), c.version(),
                           dict(x[0] for x in peer['subject'])['commonName'] if peer else 'none'))
        self.handle(c)

    def run(self):
        for srv, tls in self.listeners[1:]:
            threading.Thread(target=self.serve, args=(srv, tls), daemon=True).start()
        self.serve(*self.listeners[0])

def pub(port, topic, payload, retain=False):
    s = socket.create_connection(('127.0.0.1', port))
    body = mstr('MQTT') + b'\x04\x02\x00\x3c' + mstr('pub%d' % time.time_ns())
    s.sendall(b'\x10' + enc_len(len(body)) + body); s.recv(4)
    s.sendall(publish_pkt(topic, payload.encode(), retain))
    s.sendall(b'\xe0\x00'); s.close()

def main():
    if sys.argv[1] == 'pub':
        pub(int(sys.argv[2]), sys.argv[3], sys.argv[4], len(sys.argv) > 5)
        return
    port, log, opts = int(sys.argv[1]), sys.argv[2], sys.argv[3:]
    opt = dict(zip(opts[::2], opts[1::2]))
    tls = None
    if '--tls-cert' in opt:
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(opt['--tls-cert'], opt['--tls-key'])
        if '--tls-client-ca' in opt:
            tls.verify_mode = ssl.CERT_REQUIRED
            tls.load_verify_locations(opt['--tls-client-ca'])
    plain = int(opt['--plain-port']) if '--plain-port' in opt else None
    Broker(port, log, tls, plain).run()

if __name__ == '__main__':
    main()
