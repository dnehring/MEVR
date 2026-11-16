# SPDX-License-Identifier: MIT
"""What a game's slow frames are made of (research/sequel-render-levers.md "Performance research", the worst frames).

Takes a frame-timing capture (plugin/PerfProbe's -probe.json and -frames.csv) and render-thread stack samples taken AT
THE SAME TIME (mevr-stack-sampler with its timing CSV: tools/perf-spike-stacks.ps1 runs both), puts each sample in the
frame it fell in (the sampler's QPC against the probe's originQpc), splits the frames into slow ones (the render
thread's work above --threshold ms, default the headset period, or --percentile) and the rest, and prints for each
game function (tools/stack-functions.py's names and .pdata bounds) its time per frame in both groups (samples x the
sampling interval / frames) and the difference: what grows in a slow frame. Also the share of slow frames and how
their work is spread over the frame's renders when the probe has a first-eye mark.

    python -I tools/perf-spike-stacks.py <probe.json> <raw-stacks.txt> <sample-times.csv> --game LE2 [--threshold 11.1]
        [--percentile 90] [--top 30] [--interval-us 1000]
"""
import argparse
import collections
import csv
import importlib.util
import bisect
import json
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load_module(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('probe')
    ap.add_argument('raw')
    ap.add_argument('times')
    ap.add_argument('--game', default='LE1', choices=['LE1', 'LE2', 'LE3'])
    ap.add_argument('--threshold', type=float, default=0.0, help='slow frame: render work above this (ms); 0 = the period')
    ap.add_argument('--percentile', type=float, default=0.0, help='slow frame: render work above this percentile')
    ap.add_argument('--top', type=int, default=30)
    ap.add_argument('--interval-us', type=float, default=1000.0)
    ap.add_argument('--exe')
    args = ap.parse_args(argv)
    probe = load_module('perf_probe', 'perf-probe.py')
    sf = load_module('stack_functions', 'stack-functions.py')

    meta = json.load(open(args.probe, encoding='utf-8'))
    folder = os.path.dirname(args.probe)
    frames = probe.load_rows(os.path.join(folder, meta['files']['frames']))
    frames = [r for r in frames[1:] if r.get('t_start') is not None and r.get('t_end') is not None]
    origin, freq = meta['clocks']['originQpc'], meta['clocks']['qpcFrequency']
    work = [probe.frame_metrics(r).get('render_work') for r in frames]
    periods = [r.get('display_period_ms') for r in frames if r.get('display_period_ms')]
    period = min(periods) if periods else 1000.0 / 90.0
    valid = sorted(w for w in work if w is not None)
    if args.percentile:
        threshold = valid[min(len(valid) - 1, int(round(args.percentile / 100.0 * (len(valid) - 1))))]
    else:
        threshold = args.threshold or period
    starts = [r['t_start'] for r in frames]

    samples = []
    with open(args.raw, encoding='utf-8') as f:
        for line in f:
            items = []
            for item in line.strip().split(';'):
                if item:
                    module, _, offset = item.rpartition('+0x')
                    items.append((module, int(offset, 16)))
            samples.append(items)
    times = []
    with open(args.times, encoding='utf-8') as f:
        for row in csv.DictReader(f):
            times.append((int(row['sample']), (int(row['qpc']) - origin) * 1000.0 / freq))

    pdata_starts, funcs = sf.load_pdata(args.exe or sf.default_exe(args.game))
    names = sf.load_names(args.game)
    module_name = f'MassEffect{args.game[2:]}'

    def function_of(rva):
        i = bisect.bisect_right(pdata_starts, rva) - 1
        if i >= 0 and funcs[i][0] <= rva < funcs[i][1]:
            return funcs[i][0]
        return None

    def label(start):
        return f'0x{start:X} {names.get(start, "")}'.rstrip()

    groups = {'slow': collections.Counter(), 'rest': collections.Counter()}
    outside = {'slow': collections.Counter(), 'rest': collections.Counter()}
    frames_in = {'slow': set(), 'rest': set()}
    placed = 0
    for index, ms in times:
        if index >= len(samples):
            continue
        i = bisect.bisect_right(starts, ms) - 1
        if i < 0 or ms >= frames[i]['t_end'] or work[i] is None:
            continue
        placed += 1
        kind = 'slow' if work[i] > threshold else 'rest'
        frames_in[kind].add(i)
        stack = samples[index]
        game = [function_of(off) for module, off in stack if module == module_name]
        game = [g for g in game if g is not None]
        for g in set(game):
            groups[kind][g] += 1
        if stack:
            top = stack[0][0] if stack[0][0] != module_name else 'game'
            outside[kind][top] += 1
    n_slow = sum(1 for w in work if w is not None and w > threshold)
    n_rest = sum(1 for w in work if w is not None and w <= threshold)
    print(f'{args.probe}')
    print(f'  {len(frames)} frames, {len(times)} samples, {placed} placed in a frame; slow = render work > {threshold:.2f} ms '
          f'(period {period:.2f}): {n_slow} slow ({100.0 * n_slow / max(1, n_slow + n_rest):.1f}%), {n_rest} rest')
    sw = [w for w in work if w is not None and w > threshold]
    rw = [w for w in work if w is not None and w <= threshold]
    if sw and rw:
        print(f'  render work: slow median {statistics.median(sw):.2f} ms, rest median {statistics.median(rw):.2f} ms')
    # A sample is interval-us of the thread's time; per frame = samples x interval / frames sampled in that group.
    per = {k: (args.interval_us / 1000.0) / max(1, len(frames_in[k])) for k in groups}
    print(f'  frames with samples: slow {len(frames_in["slow"])}, rest {len(frames_in["rest"])}')
    keys = set(groups['slow']) | set(groups['rest'])
    rows = sorted(keys, key=lambda g: groups['slow'][g] * per['slow'] - groups['rest'][g] * per['rest'], reverse=True)
    print(f'\n  {"slow ms/fr":>10} {"rest ms/fr":>10} {"grows by":>9}  function (inclusive)')
    for g in rows[:args.top]:
        a, b = groups['slow'][g] * per['slow'], groups['rest'][g] * per['rest']
        print(f'  {a:10.2f} {b:10.2f} {a - b:9.2f}  {label(g)}')
    print(f'\n  {"slow ms/fr":>10} {"rest ms/fr":>10}  innermost module')
    for m in sorted(set(outside['slow']) | set(outside['rest']), key=lambda m: -outside['slow'][m])[:12]:
        print(f'  {outside["slow"][m] * per["slow"]:10.2f} {outside["rest"][m] * per["rest"]:10.2f}  {m}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
