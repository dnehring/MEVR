# SPDX-License-Identifier: MIT
"""Check the in-memory camera sample and settings-write signature in completed candidate captures."""
import argparse
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('batch', type=Path)
a = p.parse_args()
rows, errors = [], []
for folder in sorted(a.batch.glob('cafeteria-*')):
    window = json.loads((folder / 'capture-window.json').read_text(encoding='utf-8-sig'))
    snapshots = [json.loads(path.read_text(encoding='utf-8-sig')) for path in folder.glob('????-??-??_??-??-??-???.json')]
    samples = [s.get('cameraDiagnostic', {}) for s in snapshots]
    analysis = json.loads((folder / 'analysis.json').read_text(encoding='utf-8-sig'))
    writes = window.get('iniWrites', [])
    live = any(s.get('valid') and s.get('sequence', 0) > 0 for s in samples)
    host_log = (folder / 'host.log').read_text(encoding='utf-8-sig', errors='replace')
    if writes:
        errors.append(f'{folder.name}: settings writes during the clean capture')
    if not live or 'camera diagnostics: in-memory publication' not in host_log:
        errors.append(f'{folder.name}: no verified live in-memory sample')
    rows.append({'run': folder.name, 'iniWrites': len(writes), 'liveCamera': live, 'samples': samples,
                 'ticks': [r['tickSummary'] for r in analysis]})
if not rows:
    errors.append('No completed runs')
report = {'passed': not errors, 'errors': errors, 'runs': rows,
          'scope': 'Camera publication and automatic settings writes only; other hitches and manual alignment acceptance are separate.'}
(a.batch / 'camera-diagnostic-check.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
raise SystemExit(1 if errors else 0)
