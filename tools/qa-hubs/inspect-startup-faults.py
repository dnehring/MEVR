# SPDX-License-Identifier: MIT
"""Locate first-chance faults within a completed probe; does not reclassify or waive them."""
import argparse
import csv
import json
from collections import Counter
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('directory', type=Path)
folder = p.parse_args().directory
meta = json.loads(next(folder.glob('*-probe.json')).read_text(encoding='utf-8-sig'))
with (folder / meta['files']['frames']).open(encoding='utf-8', newline='') as stream:
    frames = list(csv.DictReader(stream))
faults = [r for r in frames if int(r['access_violations'])]
bins = Counter()
for row in faults:
    bins[int(float(row['t_start']) / 1000)] += int(row['access_violations'])
result = {'total': meta['accessViolations'], 'frameTotal': sum(int(r['access_violations']) for r in faults),
          'bySecond': dict(sorted(bins.items())),
          'first': [{k: r[k] for k in ('frame', 't_start', 't_end', 'access_violations', 'render_thread')} for r in faults[:10]],
          'last': [{k: r[k] for k in ('frame', 't_start', 't_end', 'access_violations', 'render_thread')} for r in faults[-10:]]}
(folder / 'fault-distribution.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
