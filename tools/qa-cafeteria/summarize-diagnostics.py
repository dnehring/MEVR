# SPDX-License-Identifier: MIT
"""Reprocess intrusive captures and map sampled game addresses through the executable's unwind table."""
import argparse
import bisect
import csv
import hashlib
import importlib.util
import json
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--exe', type=Path, help='Matching installed MassEffect1.exe; otherwise use captured GameRoot')
    args = parser.parse_args()
    context = json.loads((args.directory / 'context.json').read_text(encoding='utf-8-sig'))
    exe = args.exe or Path(context['GameRoot']) / 'Game/ME1/Binaries/Win64/MassEffect1.exe'
    mapping = module('stack_functions', HERE.parent / 'stack-functions.py')
    investigate = module('investigate', HERE / 'investigate.py')
    starts, functions = mapping.load_pdata(str(exe))
    names = mapping.load_names()

    def label(rva):
        index = bisect.bisect_right(starts, rva) - 1
        if index >= 0 and functions[index][0] <= rva < functions[index][1]:
            start = functions[index][0]
            return f'MassEffect1+0x{start:X} {names.get(start, "(unnamed)")}'
        return f'MassEffect1+0x{rva:X} (no unwind entry)'

    def mapped(stack):
        return ';'.join(label(int(frame.split('!')[1]))
                        if frame.startswith('MassEffect1!') and frame.split('!')[1].isdigit()
                        else frame for frame in stack.split(';'))

    report = {'intrusive': True, 'mappingExeSha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
              'mappingCaveat': 'Use the same executable as the capture. Unwind ranges may be function fragments. '
                               'Export-only labels such as SpiSupportDecl or GetDenuvoTimeTicketRequest are '
                               'nearest-symbol fallbacks, not reliable function identities.', 'captures': []}
    for directory in sorted(args.directory.glob('diagnostic-*')):
        if not directory.is_dir():
            continue
        investigate.inspect(directory)
        data = json.loads((directory / 'investigation.json').read_text())
        costs = sorted(float(row['suspendedUs']) / 1000 for row in csv.DictReader((directory / 'sample-times.csv').open()))
        ticks = []
        for probe in data:
            for tick in probe['longTickSamples']:
                for key in ('engineSamples', 'hostSamples'):
                    counts = Counter()
                    for stack, count in tick[key]['stacks'].items():
                        counts[mapped(stack)] += count
                    tick[key]['mappedStacks'] = dict(counts)
                ticks.append(tick)
        report['captures'].append({'directory': directory.name, 'sampleCount': len(costs),
                                   'observerMs': {'p50': costs[len(costs)//2], 'p99': costs[int((len(costs)-1)*.99)],
                                                  'max': max(costs), 'total': sum(costs)}, 'longTicks': ticks})
    destination = args.directory / 'diagnostic-summary.json'
    destination.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(destination)


if __name__ == '__main__':
    main()
