# SPDX-License-Identifier: MIT
"""Summarize a batch of comparable clean runs. Correctness checks are mandatory; performance limits are explicit."""
import argparse
import json
import statistics
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('batch', type=Path)
p.add_argument('--max-p99-ms', type=float)
p.add_argument('--max-interval-ms', type=float)
p.add_argument('--max-tick-ms', type=float)
p.add_argument('--output', type=Path, help='report path (default: batch/comparison.json)')
a = p.parse_args()
rows, fingerprints, errors = [], [], []
for path in sorted(a.batch.glob('cafeteria-*/analysis.json')):
    folder = path.parent
    if (folder / 'diagnostic.json').exists():
        continue
    ctx = json.loads((folder / 'context.json').read_text(encoding='utf-8-sig'))
    if not (folder / 'restored.json').exists() or not list(folder.glob('route-cafeteria-*.json')):
        errors.append(f'{folder.name}: missing route or restore receipt')
    state = json.loads((folder / 'final-state.json').read_text(encoding='utf-8-sig')) if (folder / 'final-state.json').exists() else {}
    if not state or any(state.get(k) for k in ('mismatches','requests','processes')):
        errors.append(f'{folder.name}: final state not clean')
    for r in json.loads(path.read_text(encoding='utf-8-sig')):
        levers = r['renderLevers']
        if not r['halcyon'].get('sfr2') or levers.get('sharePass') != 5:
            errors.append(f'{folder.name}: expected One Draw/sharePass=5 workload is not active')
        if r['accessViolations'] or r['gpuFramesDropped'] or levers.get('shareFailed') or levers.get('replayFailed') or levers.get('drawReplay',{}).get('replayFailures'):
            errors.append(f'{folder.name}: probe correctness/measurement failure')
        fingerprints.append(json.dumps([ctx['ExpectedAsiSha256'],ctx['Preset'],r['resolution'],r['halcyon'],{k:levers.get(k) for k in ('sharePass','umbraQueryWait','ue3QueryWait','perEyeViewState','perEyeOcclusion','pCoresOnly')}],sort_keys=True))
        s = r['all']
        tick = r.get('tickSummary', {})
        if not tick:
            errors.append(f'{folder.name}: rerun analyze.py to include game-thread tick metrics')
        rows.append({'run':folder.name,'probe':r['source'],'frames':s['frames'],'p99Ms':s['interval']['p99'],
                     'maxMs':s['interval']['max'],'over11ms':s['over11ms'],'over22ms':s['over22ms'],
                     'renderWorkP99Ms':s['render_work'].get('p99'),'gpuWorkP99Ms':s['gpu_work'].get('p99'),
                     'tickMaxMs':tick.get('totalMs',{}).get('max'),'engineMaxMs':tick.get('engineMs',{}).get('max'),
                     'hostMaxMs':tick.get('hostMs',{}).get('max'),'ticksOver11ms':tick.get('over11ms')})
if not rows:
    errors.append('No completed clean measurements found')
if (a.batch / 'batch.json').exists():
    planned = json.loads((a.batch / 'batch.json').read_text(encoding='utf-8-sig'))['count']
    if len(rows) != planned:
        errors.append(f'Expected {planned} clean captures, found {len(rows)}')
if len(set(fingerprints)) > 1:
    errors.append('Build, preset, resolution or renderer settings differ across samples')
measurement_errors = list(errors)
for r in rows:
    if a.max_p99_ms is not None and r['p99Ms'] > a.max_p99_ms:
        errors.append(f"{r['run']}: p99 exceeds {a.max_p99_ms} ms")
    if a.max_interval_ms is not None and r['maxMs'] > a.max_interval_ms:
        errors.append(f"{r['run']}: maximum interval exceeds {a.max_interval_ms} ms")
    if a.max_tick_ms is not None and r['tickMaxMs'] is not None and r['tickMaxMs'] > a.max_tick_ms:
        errors.append(f"{r['run']}: game-thread tick exceeds {a.max_tick_ms} ms")
summary={'runs':rows,'errors':errors,'comparable':not measurement_errors,'passed':not errors,
         'performanceLimitsApplied':any(x is not None for x in (a.max_p99_ms,a.max_interval_ms,a.max_tick_ms)),
         'medianP99Ms':statistics.median(r['p99Ms'] for r in rows) if rows else None,
         'worstIntervalMs':max((r['maxMs'] for r in rows),default=None)}
(a.output or a.batch/'comparison.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps(summary,indent=2))
raise SystemExit(1 if errors else 0)
