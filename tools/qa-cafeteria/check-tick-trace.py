# SPDX-License-Identifier: MIT
"""Validate buffered full-hook/queued-call timing relationships in completed probe files."""
import argparse
import csv
import json
from pathlib import Path


def check(directory):
    rows, errors = [], []
    for path in sorted(directory.rglob('*-probe.json')):
        meta = json.loads(path.read_text(encoding='utf-8-sig'))
        trace = meta.get('tickInstrumentation', {})
        if trace.get('version') != 2:
            errors.append(f'{path.name}: expected tick instrumentation v2'); continue
        if trace.get('ticksDropped') or trace.get('callsDropped'):
            errors.append(f'{path.name}: trace buffer overflow')
        with (path.parent / meta['files']['ticks']).open(encoding='utf-8-sig', newline='') as f:
            ticks = {int(t['tick']): {k: float(v) for k, v in t.items() if v != ''} for t in csv.DictReader(f)}
        with (path.parent / meta['files']['calls']).open(encoding='utf-8-sig', newline='') as f:
            calls = [{k: float(v) for k, v in c.items()} for c in csv.DictReader(f)]
        if not ticks or len(ticks) != meta['ticks'] or len(calls) != trace.get('queuedCalls'):
            errors.append(f'{path.name}: missing/inconsistent trace rows')
        per_tick = {}
        for call in calls:
            t = ticks.get(int(call['tick']))
            if t is None or not (t['t_drain_begin']-.01 <= call['t_begin'] <= call['t_end'] <= t['t_begin']+.01):
                errors.append(f'{path.name}: call outside its drain'); continue
            if abs(call['wall_ms'] - (call['t_end']-call['t_begin'])) > .01 or call['outcome'] not in (0,1,2):
                errors.append(f'{path.name}: invalid call duration/outcome')
            count, wall = per_tick.get(int(call['tick']), (0,0))
            per_tick[int(call['tick'])] = count+1, wall+call['wall_ms']
        for index, t in ticks.items():
            count, wall = per_tick.get(index, (0,0))
            valid = (t['t_hook_begin'] <= t['t_drain_begin'] <= t['t_begin'] and t['drain_ms'] >= 0 and
                     abs(t['drain_ms']-(t['t_begin']-t['t_drain_begin'])) < .01 and
                     abs(t['hook_ms']-(t['t_begin']-t['t_hook_begin']+t['engine_ms']+t['host_ms'])) < .01 and
                     count == t['queued_calls'] and wall <= t['drain_ms']+.01)
            if not valid: errors.append(f'{path.name}: inconsistent hook/drain/call rows at tick {index}')
        rows.append({'probe':path.name,'ticks':len(ticks),'queuedCalls':len(calls),
                     'maxHookMs':max((t['hook_ms'] for t in ticks.values()),default=None),
                     'maxDrainMs':max((t['drain_ms'] for t in ticks.values()),default=None),
                     'maxCallMs':max((c['wall_ms'] for c in calls),default=None)})
    if not rows: errors.append('No v2 probes found')
    return {'passed':not errors,'errors':errors,'probes':rows,
            'scope':'Trace completeness and timing relationships; no performance or root-cause acceptance implied.'}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    a = p.parse_args()
    result = check(a.directory)
    (a.directory/'tick-trace-check.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))
    raise SystemExit(0 if result['passed'] else 1)
