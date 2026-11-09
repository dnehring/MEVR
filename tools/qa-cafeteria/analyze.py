# SPDX-License-Identifier: MIT
"""Summarize fake-headset probes, excluding one second at each drain boundary."""
import argparse
import importlib.util
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path)
HERE = parser.parse_args().directory.resolve()
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('probe', ROOT / 'tools/perf-probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)

def dist(values):
    v = sorted(x for x in values if x is not None)
    return {k: round(v[min(len(v)-1, round(q*(len(v)-1)))], 4)
            for k, q in [('p50', .5), ('p95', .95), ('p99', .99), ('max', 1)]} if v else {}

keys = ['interval','game_wall','game_busy','render_work','draws','end_frame','wait_wall','gpu_work','lag_present','overlays','copies']
out = []
for path in sorted(HERE.glob('*-probe.json')):
    meta = json.loads(path.read_text(encoding='utf-8-sig'))
    raw = probe.load_rows(HERE / meta['files']['frames'])
    ticks = probe.load_rows(HERE / meta['files']['ticks'])
    calls = probe.load_rows(HERE / meta['files']['calls']) if meta['files'].get('calls') else []
    rows = []
    for r in raw:
        if not r['xr_frame'] or not r['should_render'] or not (1000 <= r['t_start'] < (meta['seconds']-1)*1000):
            continue
        m = probe.frame_metrics(r)
        m.update(at_s=r['t_start']/1000, frame=int(r['frame']))
        rows.append(m)
    def worst_detail(m):
        r = next(r for r in raw if int(r['frame']) == m['frame'])
        overlaps = [t for t in ticks if probe.tick_bounds(t)[0] < r['t_end']
                    and probe.tick_bounds(t)[1] > r['t_start']]
        return {**{k:m.get(k) for k in ['at_s','frame']+keys},
                'overlapping_ticks': [{k:t.get(k) for k in ['t_begin','engine_ms','engine_busy_ms','host_ms','host_busy_ms',
                                                           't_hook_begin','hook_ms','drain_ms','queued_calls']}
                                      for t in overlaps],
                'queued_calls': [c for c in calls if c['t_begin'] < r['t_end'] and c['t_end'] > r['t_start']]}
    def summary(rs):
        return {'frames':len(rs),'over11ms':sum(r['interval']>1000/90 for r in rs),
                'over22ms':sum(r['interval']>2000/90 for r in rs),
                **{k:dist(r.get(k) for r in rs) for k in keys}}
    result = {'source':path.name,'resolution':meta['resolution'],'halcyon':meta['halcyon'],
              'renderLevers':meta['renderLevers'],'accessViolations':meta['accessViolations'],
              'tickInstrumentation':meta.get('tickInstrumentation', {}),
              'gpuFramesDropped':meta['gpuFramesDropped'],'all':summary(rows),
              'thirds':[summary([r for r in rows if a <= r['at_s'] < b])
                        for a,b in [(1,meta['seconds']/3),(meta['seconds']/3,meta['seconds']*2/3),
                                    (meta['seconds']*2/3,meta['seconds']-1)]] if meta['seconds']>3 else [],
              'worst':[worst_detail(r)
                       for r in sorted(rows,key=lambda r:r['interval'],reverse=True)[:12]]}
    measured_ticks = [t for t in ticks if 1000 <= t['t_begin'] < (meta['seconds']-1)*1000]
    tick_scopes = {'wholeHook' if t.get('hook_ms') is not None and t.get('t_hook_begin') is not None
                   else 'engineHostOnly' for t in measured_ticks}
    result['tickSummary'] = {
        'engineMs': dist(t['engine_ms'] for t in measured_ticks),
        'engineBusyMs': dist(t['engine_busy_ms'] for t in measured_ticks),
        'hostMs': dist(t['host_ms'] for t in measured_ticks),
        'engineHostMs': dist(t['engine_ms']+t['host_ms'] for t in measured_ticks),
        'drainMs': dist(t.get('drain_ms') for t in measured_ticks),
        'hookMs': dist(t.get('hook_ms') for t in measured_ticks),
        'totalScope': next(iter(tick_scopes)) if len(tick_scopes) == 1 else 'mixed' if tick_scopes else 'empty',
        'totalMs': dist(probe.tick_total(t) for t in measured_ticks),
        'over11ms': sum(probe.tick_total(t) > 1000/90 for t in measured_ticks),
        'over22ms': sum(probe.tick_total(t) > 2000/90 for t in measured_ticks)}
    result['queuedCallSummary'] = {'count':len(calls), 'wallMs':dist(c['wall_ms'] for c in calls),
                                   'worst':sorted(calls,key=lambda c:c['wall_ms'],reverse=True)[:12]}
    out.append(result)
    print(json.dumps({'source':path.name,'all':result['all'],'worst':result['worst'][:3]}))
(HERE/'analysis.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
