#!/usr/bin/env python3
"""Minimal Telegram Bot API server for local tests, no dependencies.

Implements what AcquaThermoNet uses: getUpdates (long polling) and
sendMessage, for one bot token (a wrong token gets 401 like the real API).
Every sent message is logged as one JSON line {"time","chat_id","text"}.
Set [TELEGRAM] api_url=http://127.0.0.1:PORT in setting.ini.

Usage:
  minitelegram.py PORT TOKEN LOGFILE           run the server
  minitelegram.py inject PORT CHAT_ID TEXT     a user writes TEXT from CHAT_ID
"""
import json, sys, threading, time, urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Bot:
    def __init__(self, token, log):
        self.token = token
        self.log = open(log, 'w', buffering=1)
        self.updates = []
        self.next_id = 1
        self.cond = threading.Condition()

    def inject(self, chat_id, text, username='tester'):
        with self.cond:
            self.updates.append({'update_id': self.next_id, 'message': {
                'message_id': self.next_id, 'date': int(time.time()), 'text': text,
                'chat': {'id': chat_id, 'type': 'private'},
                'from': {'id': chat_id, 'is_bot': False, 'first_name': 'Test', 'username': username}}})
            self.next_id += 1
            self.cond.notify_all()

    def get_updates(self, offset, timeout):
        deadline = time.time() + timeout
        with self.cond:
            while True:
                ready = [u for u in self.updates if u['update_id'] >= offset]
                self.updates = ready                  # confirmed ones are gone
                if ready or time.time() >= deadline:
                    return ready
                self.cond.wait(deadline - time.time())


def handler_for(bot):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def reply(self, code, obj):
            data = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_POST(self):
            body = self.rfile.read(int(self.headers.get('Content-Length', 0)) or 0)
            req = json.loads(body or b'{}')
            if self.path == '/inject':
                bot.inject(int(req['chat_id']), req['text'], req.get('username', 'tester'))
                return self.reply(200, {'ok': True})
            prefix = '/bot%s/' % bot.token
            if not self.path.startswith(prefix):
                return self.reply(401, {'ok': False, 'error_code': 401, 'description': 'Unauthorized'})
            method = self.path[len(prefix):]
            if method == 'getUpdates':
                res = bot.get_updates(int(req.get('offset', 0)), min(int(req.get('timeout', 0)), 50))
                return self.reply(200, {'ok': True, 'result': res})
            if method == 'sendMessage':
                bot.log.write(json.dumps({'time': time.time(), 'chat_id': req['chat_id'], 'text': req['text']}) + '\n')
                return self.reply(200, {'ok': True, 'result': {'message_id': 1, 'text': req['text']}})
            return self.reply(404, {'ok': False, 'error_code': 404, 'description': 'Not Found'})
    return Handler


def main():
    if sys.argv[1] == 'inject':
        port, chat, text = int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
        req = urllib.request.Request('http://127.0.0.1:%d/inject' % port,
                                     json.dumps({'chat_id': chat, 'text': text}).encode(),
                                     {'Content-Type': 'application/json'})
        urllib.request.urlopen(req).read()
        return
    port, token, log = int(sys.argv[1]), sys.argv[2], sys.argv[3]
    ThreadingHTTPServer(('127.0.0.1', port), handler_for(Bot(token, log))).serve_forever()


if __name__ == '__main__':
    main()
