# SPDX-License-Identifier: MIT
"""Summarises a hair trace (`mevr hair trace <s>`, %LOCALAPPDATA%\\MEVR\\hair-trace-*.csv; docs/trilogy-hair-plan.md 4.4).

Per hair component and chain: how far its tip got from the animated pose (peak), when, how long until it settled back
within 1 cm (and stayed), how much the segments stretched against the animation (must stay near 0), the steps' times
and the collision pushes. Usage: python tools/hair-trace.py [csv] (default: the newest trace).
"""
import csv
import glob
import math
import os
import sys
from collections import defaultdict


def newest():
    folder = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'MEVR')
    files = sorted(glob.glob(os.path.join(folder, 'hair-trace-*.csv')), key=os.path.getmtime)
    return files[-1] if files else None


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else newest()
    if not path or not os.path.exists(path):
        print('no hair trace found')
        return 1
    # (component, chain) -> t -> point -> (sim, anim); plus dt and collisions per (component, chain, t)
    frames = defaultdict(lambda: defaultdict(dict))
    meta = {}
    with open(path, newline='') as f:
        for row in csv.DictReader(f):
            key = (row['component'], int(row['chain']))
            t = float(row['t'])
            sim = (float(row['simX']), float(row['simY']), float(row['simZ']))
            anim = (float(row['animX']), float(row['animY']), float(row['animZ']))
            frames[key][t][int(row['point'])] = (sim, anim)
            meta[(key, t)] = (float(row['dt']), int(row['collisions']))
    print(f'{path}')
    for key in sorted(frames):
        series = frames[key]
        times = sorted(series)
        if not times:
            continue
        tip_index = max(series[times[0]])
        offsets = []
        stretch = 0.0
        for t in times:
            pts = series[t]
            if tip_index not in pts:
                continue
            sim, anim = pts[tip_index]
            offsets.append((t, dist(sim, anim)))
            for i in range(1, tip_index + 1):
                if i in pts and i - 1 in pts:
                    ls = dist(pts[i][0], pts[i - 1][0])
                    la = dist(pts[i][1], pts[i - 1][1])
                    stretch = max(stretch, abs(ls - la))
        if not offsets:
            continue
        peak_t, peak = max(offsets, key=lambda o: o[1])
        settled = None
        for t, off in offsets:
            if t < peak_t:
                continue
            if off < 0.01:
                if settled is None:
                    settled = t
            else:
                settled = None
        dts = [meta[(key, t)][0] for t in times if (key, t) in meta]
        pushes = sum(meta[(key, t)][1] for t in times if (key, t) in meta)
        mean_dt = sum(dts) / len(dts) if dts else 0.0
        print(f'  component {key[0]} chain {key[1]}: {len(times)} steps over {times[-1] - times[0]:.1f} s '
              f'(step {mean_dt * 1000:.1f} ms mean), tip peak {peak * 100:.1f} cm at {peak_t:.2f} s, '
              + (f'settled within 1 cm {settled - peak_t:.2f} s after it' if settled is not None else 'never settled within 1 cm')
              + f', worst stretch {stretch * 1000:.1f} mm, {pushes} collision pushes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
