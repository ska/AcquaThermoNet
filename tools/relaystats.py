#!/usr/bin/env python3
"""Relay ON time per zone and per day from the AcquaThermoNet relay log.

Reads the monthly CSV files written by the application ([RELAY_LOG],
e.g. log/relays-2026-09.csv), rebuilds the ON intervals of every relay,
splits them at midnight and sums them per day.

Usage:
  relaystats.py FILE_OR_DIR... [--from YYYY-MM-DD] [--to YYYY-MM-DD]
                [--reason regulation,frost,...] [--csv OUT.csv] [--plot OUT.png]

  --reason   count only ON periods started for these reasons
             (regulation, frost, exercise); default: all
  --csv      write the daily table (hours, one column per zone)
  --plot     stacked bar chart of the daily hours (needs matplotlib)

An ON not followed by an OFF (e.g. power loss) is closed at the next line
of that relay (usually the "startup" OFF) and reported as "unclosed";
the last ON still running is counted until now.
"""
import argparse, csv, glob, os, sys, time
from collections import defaultdict
from datetime import datetime, date, timedelta


def read_events(paths):
    files = []
    for p in paths:
        files += sorted(glob.glob(os.path.join(p, '*.csv'))) if os.path.isdir(p) else [p]
    events = []
    for f in files:
        with open(f, newline='') as fh:
            reader = csv.DictReader(fh)
            if not {'epoch', 'relay', 'zones', 'state', 'reason', 'on_s'} <= set(reader.fieldnames or []):
                print('not a relay log, skipped: %s' % f, file=sys.stderr)
                continue
            for row in reader:
                try:
                    events.append((int(row['epoch']), row['relay'], row['zones'], row['state'],
                                   row['reason'], row['on_s']))
                except (KeyError, ValueError):
                    print('skipped bad line in %s: %s' % (f, row), file=sys.stderr)
    events.sort(key=lambda e: e[0])
    return events


def intervals(events, reasons):
    """(zones, start_epoch, end_epoch) of every ON period"""
    out, open_on, unclosed = [], {}, 0
    for epoch, relay, zones, state, reason, on_s in events:
        if state == 'ON':
            if relay not in open_on:
                open_on[relay] = (epoch, zones, reason)
            continue
        if relay not in open_on:
            continue
        start, zones_on, reason_on = open_on.pop(relay)
        if on_s:
            start = epoch - int(on_s)       # monotonic duration: exact length
        else:
            unclosed += 1                   # OFF without duration: power loss/restart
        if reasons is None or reason_on in reasons:
            out.append((zones_on, start, epoch))
    now = int(time.time())
    for relay, (start, zones_on, reason_on) in open_on.items():
        if reasons is None or reason_on in reasons:
            out.append((zones_on, start, now))
    return out, unclosed, len(open_on)


def per_day(ivals):
    days = defaultdict(lambda: defaultdict(float))
    for zones, start, end in ivals:
        t = datetime.fromtimestamp(start)
        end_dt = datetime.fromtimestamp(end)
        while t < end_dt:
            midnight = datetime.combine(t.date() + timedelta(days=1), datetime.min.time())
            chunk_end = min(midnight, end_dt)
            days[t.date()][zones] += (chunk_end - t).total_seconds()
            t = chunk_end
    return days


def hm(seconds):
    m = int(round(seconds / 60))
    return '%d:%02d' % (m // 60, m % 60)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('paths', nargs='+')
    ap.add_argument('--from', dest='date_from')
    ap.add_argument('--to', dest='date_to')
    ap.add_argument('--reason')
    ap.add_argument('--csv')
    ap.add_argument('--plot')
    args = ap.parse_args()

    reasons = set(args.reason.split(',')) if args.reason else None
    ivals, unclosed, running = intervals(read_events(args.paths), reasons)
    days = per_day(ivals)

    d_from = date.fromisoformat(args.date_from) if args.date_from else None
    d_to = date.fromisoformat(args.date_to) if args.date_to else None
    sel = sorted(d for d in days if (not d_from or d >= d_from) and (not d_to or d <= d_to))
    zones = sorted({z for d in sel for z in days[d]})
    if not sel:
        print('no ON time in the selected period')
        return

    width = max(10, max(len(z) for z in zones) + 2)
    print('date        ' + ''.join(z.rjust(width) for z in zones))
    totals = defaultdict(float)
    for d in sel:
        print(d.isoformat() + '  ' + ''.join(hm(days[d].get(z, 0)).rjust(width) for z in zones))
        for z in zones:
            totals[z] += days[d].get(z, 0)
    print('total       ' + ''.join(hm(totals[z]).rjust(width) for z in zones))
    if unclosed:
        print('%d ON period(s) without OFF duration (restart/power loss): closed at the next line' % unclosed)
    if running:
        print('%d relay(s) still ON: counted until now' % running)

    if args.csv:
        with open(args.csv, 'w', newline='') as fh:
            w = csv.writer(fh)
            w.writerow(['date'] + ['%s_hours' % z for z in zones])
            for d in sel:
                w.writerow([d.isoformat()] + ['%.3f' % (days[d].get(z, 0) / 3600) for z in zones])
        print('daily table written to', args.csv)

    if args.plot:
        try:
            import matplotlib
            matplotlib.use('Agg')
            import matplotlib.pyplot as plt
        except ImportError:
            print('matplotlib not available: no plot', file=sys.stderr)
            return
        fig, ax = plt.subplots(figsize=(max(6, len(sel) * 0.5), 4))
        bottom = [0.0] * len(sel)
        for z in zones:
            vals = [days[d].get(z, 0) / 3600 for d in sel]
            ax.bar([d.isoformat() for d in sel], vals, bottom=bottom, label=z)
            bottom = [b + v for b, v in zip(bottom, vals)]
        ax.set_ylabel('hours ON')
        ax.set_title('Relay ON time per day')
        ax.legend()
        plt.xticks(rotation=45, ha='right')
        fig.tight_layout()
        fig.savefig(args.plot)
        print('plot written to', args.plot)


if __name__ == '__main__':
    main()
