#!/usr/bin/env python3
"""Modbus RTU relay board simulator on a pseudo terminal, no dependencies.

Emulates the 8 relay board used by AcquaThermoNet (device id 1):
  FC6  write register N (1..8): 0x0100 = relay ON, 0x0200 = relay OFF
  FC3  read registers 1..8: 0x0001 = ON, 0x0000 = OFF
Prints the pty path on stdout: use it as [SERIAL] port in setting.ini.
Every request is logged as "#<n> write relay <r> ON|OFF" or "#<n> read -> 01000000".

Usage: mbsim.py LOGFILE [--stuck-on N] [--drop N] [--init-on]
  --stuck-on N   relay N always reads ON (relay fault)
  --drop N       no answer to every N-th request (answer timeout)
  --init-on      all relays ON at start
"""
import argparse, os, select, sys, time, tty

def crc16(b):
    c = 0xFFFF
    for x in b:
        c ^= x
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return bytes([c & 0xFF, c >> 8])

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log')
    ap.add_argument('--stuck-on', type=int, default=0)
    ap.add_argument('--drop', type=int, default=0)
    ap.add_argument('--init-on', action='store_true')
    args = ap.parse_args()

    master, slave = os.openpty()
    tty.setraw(slave)
    print(os.ttyname(slave), flush=True)

    regs = [1 if args.init_on else 0] * 9
    n = 0
    buf = b''
    log = open(args.log, 'w', buffering=1)
    while True:
        r, _, _ = select.select([master], [], [], 0.5)
        if not r:
            continue
        buf += os.read(master, 256)
        while len(buf) >= 8:            # both requests are 8 bytes
            req, buf = buf[:8], buf[8:]
            n += 1
            if crc16(req[:6]) != req[6:]:
                log.write('#%d bad crc %s\n' % (n, req.hex()))
                continue
            fc = req[1]
            addr = (req[2] << 8) | req[3]
            val = (req[4] << 8) | req[5]
            if args.drop and n % args.drop == 0:
                log.write('#%d fc%d DROPPED\n' % (n, fc))
                continue
            if fc == 6:
                if 1 <= addr <= 8:
                    regs[addr] = 1 if val == 0x0100 else 0
                log.write('#%d write relay %d %s\n' % (n, addr, 'ON' if val == 0x0100 else 'OFF'))
                ans = req[:6]
            elif fc == 3:
                if args.stuck_on:
                    regs[args.stuck_on] = 1
                ans = bytes([1, 3, 16]) + b''.join(bytes([0, regs[i]]) for i in range(1, 9))
                log.write('#%d read -> %s\n' % (n, ''.join(str(regs[i]) for i in range(1, 9))))
            else:
                continue
            time.sleep(0.02)
            os.write(master, ans + crc16(ans))

if __name__ == '__main__':
    main()
