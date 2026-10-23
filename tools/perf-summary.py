# SPDX-License-Identifier: MIT
"""Frame rate and GPU time for the latest LE1 session, split by what the headset showed.

Reads Halcyon's log (%LOCALAPPDATA%\\MELEVR\\MELEVR_Log.txt): [PRESENTHZ] fps (presents per second,
each one a full stereo frame), [GPU] avgMs and [VRSTATE] (VR / flat panel) transitions.
Note: [GPU] avgMs is the GPU timestamp span from one present to the next, i.e. the frame
interval, not GPU busy time (11.1 ms at a steady 90 fps); tools/perf-monitor.ps1 samples real
GPU and CPU load. Prints one row per stretch of the same state, plus the overall median.

Usage: python tools/perf-summary.py [--log PATH] [--session N]   (N: 1 = latest, 2 = the one before)
"""

import argparse
import os
import re
import statistics
import sys

LINE = re.compile(r'^(\d{4}-\d\d-\d\d) (\d\d:\d\d:\d\d)\.\d+ \| (.*)$')
SESSION_START = 'MELEVR1 (Halcyon)'


def sessions(lines):
    starts = [i for i, line in enumerate(lines) if SESSION_START in line]
    return [lines[a:b] for a, b in zip(starts, starts[1:] + [len(lines)])]


def summarize(session):
    rows = []
    current = {'state': 'startup', 'since': None, 'fps': [], 'gpu': []}
    for line in session:
        m = LINE.match(line)
        if not m:
            continue
        time, text = m.group(2), m.group(3)
        if current['since'] is None:
            current['since'] = time
        if text.startswith('[VRSTATE]'):
            state = text.split('->')[-1].strip() if '->' in text else text
            if state != current['state']:
                rows.append(current)
                current = {'state': state, 'since': time, 'fps': [], 'gpu': []}
        elif text.startswith('[PRESENTHZ]'):
            f = re.search(r'fps=([\d.]+)', text)
            if f:
                current['fps'].append(float(f.group(1)))
        elif text.startswith('[GPU]'):
            g = re.search(r'avgMs=([\d.]+)', text)
            if g:
                current['gpu'].append(float(g.group(1)))
    rows.append(current)
    return [r for r in rows if r['fps'] or r['gpu']]


def median(values):
    return f'{statistics.median(values):6.1f}' if values else '     -'


def main():
    parser = argparse.ArgumentParser()
    default = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'MELEVR', 'MELEVR_Log.txt')
    parser.add_argument('--log', default=default)
    parser.add_argument('--session', type=int, default=1)
    args = parser.parse_args()
    with open(args.log, encoding='utf-8', errors='replace') as f:
        lines = f.read().splitlines()
    all_sessions = sessions(lines)
    if not all_sessions or args.session > len(all_sessions):
        sys.exit(f'no session {args.session} in {args.log}')
    session = all_sessions[-args.session]
    first = LINE.match(session[1]) if len(session) > 1 else None
    print(f'session {args.session} of {len(all_sessions)}' + (f', started {first.group(1)} {first.group(2)}' if first else ''))
    print(f'{"from":8}  {"state":28}  {"fps (median)":>12}  {"GPU ms (median)":>15}  samples')
    fps_all, gpu_all = [], []
    for r in summarize(session):
        fps_all += r['fps']
        gpu_all += r['gpu']
        print(f'{r["since"]:8}  {r["state"][:28]:28}  {median(r["fps"]):>12}  {median(r["gpu"]):>15}  {len(r["fps"])}/{len(r["gpu"])}')
    print(f'{"overall":8}  {"":28}  {median(fps_all):>12}  {median(gpu_all):>15}')


if __name__ == '__main__':
    main()
