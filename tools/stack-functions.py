"""Where a sampled thread's time goes, by the game's own functions (research/22).

Reads the raw stacks mevr-stack-sampler writes (one sample per line, innermost frame first,
"module+0xoffset"; tools/sample-render-thread.ps1 makes them), maps MassEffect1 frames to whole
functions through the exe's .pdata unwind table (plain on disk, unlike .text), names them from
tools/ghidra/symbols-le1.csv where we know them, and prints:
  - inclusive: the share of samples with the function anywhere on the stack;
  - self: the share where it is the innermost game frame (time in it or in what it calls outside
    the exe: D3D, the driver, the C runtime);
  - with --under RVA: for the samples inside that function, which of its direct game callees
    they are in (a drill-down; repeat on a child to go deeper).

    python -I tools/stack-functions.py <raw.txt> [--under 0x76EE20] [--top 30] [--exe PATH]
"""
import argparse
import bisect
import collections
import csv
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_EXE = (r'C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition'
               r'\Game\ME1\Binaries\Win64\MassEffect1.exe')


def load_pdata(exe):
    spec = importlib.util.spec_from_file_location('verify_rvas', os.path.join(HERE, 'verify_rvas.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    _, funcs = module.load(exe)
    return [f[0] for f in funcs], funcs


def load_names():
    names = {}
    path = os.path.join(HERE, 'ghidra', 'symbols-le1.csv')
    with open(path, encoding='utf-8') as f:
        for row in csv.reader(f):
            if len(row) >= 2 and not row[0].startswith('#'):
                try:
                    names[int(row[0], 16)] = row[1]
                except ValueError:
                    pass
    return names


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('raw')
    parser.add_argument('--under', help='RVA of a game function: break its samples down by direct callee')
    parser.add_argument('--top', type=int, default=30)
    parser.add_argument('--exe', default=DEFAULT_EXE)
    args = parser.parse_args()

    starts, funcs = load_pdata(args.exe)
    names = load_names()

    def function_of(rva):
        i = bisect.bisect_right(starts, rva) - 1
        if i >= 0 and funcs[i][0] <= rva < funcs[i][1]:
            return funcs[i][0]
        return None

    def label(start):
        return f'0x{start:X} {names.get(start, "")}'.rstrip()

    samples = []
    with open(args.raw, encoding='utf-8') as f:
        for line in f:
            frames = []
            for item in line.strip().split(';'):
                if not item:
                    continue
                module, _, offset = item.rpartition('+0x')
                frames.append((module, int(offset, 16)))
            if frames:
                samples.append(frames)
    total = len(samples)
    if total == 0:
        sys.exit('no samples')

    inclusive = collections.Counter()
    self_time = collections.Counter()
    outside = collections.Counter()   # what the innermost game function was calling (module)
    children = collections.Counter()
    under = int(args.under, 16) if args.under else None
    under_total = 0
    for frames in samples:
        game = [function_of(off) for module, off in frames if module == 'MassEffect1']
        game = [g for g in game if g is not None]
        for g in set(game):
            inclusive[g] += 1
        if game:
            self_time[game[0]] += 1
            if frames[0][0] != 'MassEffect1':
                outside[(game[0], frames[0][0])] += 1
        if under is not None and under in game:
            under_total += 1
            i = game.index(under)   # innermost occurrence: its direct game callee is the frame before
            children[game[i - 1] if i > 0 else None] += 1

    print(f'{total} samples ({args.raw})')
    print(f'\n  inclusive  function')
    for g, n in inclusive.most_common(args.top):
        print(f'  {100.0 * n / total:8.1f}%  {label(g)}')
    print(f'\n  self       function (innermost game frame; its calls into D3D, the driver, the CRT count here)')
    for g, n in self_time.most_common(args.top):
        print(f'  {100.0 * n / total:8.1f}%  {label(g)}')
    print(f'\n  outside    innermost game function -> the module it was in')
    for (g, module), n in outside.most_common(args.top):
        print(f'  {100.0 * n / total:8.1f}%  {label(g)} -> {module}')
    if under is not None:
        print(f'\n  under {label(under)}: {100.0 * under_total / total:.1f}% of all samples; by direct game callee')
        for g, n in children.most_common(args.top):
            print(f'  {100.0 * n / total:8.1f}%  {label(g) if g is not None else "(itself, or outside the exe)"}')


if __name__ == '__main__':
    main()
