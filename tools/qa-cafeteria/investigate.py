# SPDX-License-Identifier: MIT
"""Explain outliers using frame/tick overlap, settings-write times, and optional sampled stacks. No causal verdicts."""
import argparse
import bisect
import csv
import datetime as dt
import importlib.util
import json
import re
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('probe', HERE.parent / 'perf-probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def inspect(directory):
    window = json.loads((directory / 'capture-window.json').read_text(encoding='utf-8-sig'))
    requested = dt.datetime.fromisoformat(window['requested'])
    log = (directory / 'host.log').read_text(encoding='utf-8-sig', errors='replace')
    starts = []
    for m in re.finditer(r'\[(\d\d:\d\d:\d\d\.\d+)\].*perf probe: measuring 90 s', log):
        start = dt.datetime.combine(requested.date(), dt.time.fromisoformat(m[1]), requested.tzinfo)
        if abs((start-requested).total_seconds()) < 10:
            starts.append(start)
    if len(starts) != 1:
        raise ValueError(f'{directory}: cannot uniquely align probe with host log')
    origin = starts[0]
    writes = [(dt.datetime.fromisoformat(v['fileWriteUtc'].replace('Z','+00:00'))-origin).total_seconds()*1000 for v in window.get('iniWrites', [])]
    samples = []
    if (directory / 'sample-times.csv').exists():
        names = (directory / 'sample-times.csv.symbols.txt').read_text(errors='replace').splitlines()
        for row in csv.DictReader((directory / 'sample-times.csv').open()):
            utc = dt.datetime(1601,1,1,tzinfo=dt.timezone.utc) + dt.timedelta(microseconds=int(row['utcFileTime'])/10)
            samples.append({'ms': (utc-origin).total_seconds()*1000, 'suspendedMs': float(row['suspendedUs'])/1000,
                            'threadCycles': int(row.get('threadCycles',0)),
                            'stack': names[int(row['sample'])]})
    result = []
    def sampled_span(start, end):
        chosen = [s for s in samples if start <= s['ms'] <= end]
        return {'samples': len(chosen), 'observerSuspensionMs': sum(s['suspendedMs'] for s in chosen),
                'stacks': dict(Counter(s['stack'] for s in chosen))}
    for path in sorted(directory.glob('*-probe.json')):
        meta = json.loads(path.read_text(encoding='utf-8-sig'))
        if meta['seconds'] < 80:
            continue
        frames = probe.load_rows(directory / meta['files']['frames'])
        ticks = probe.load_rows(directory / meta['files']['ticks'])
        begins = [t['t_begin'] for t in ticks]
        episodes = []
        for row in frames:
            if not row['xr_frame'] or not row['should_render'] or not 1000 <= row['t_start'] < (meta['seconds']-1)*1000:
                continue
            metrics = probe.frame_metrics(row)
            if metrics['interval'] <= 1000/90:
                continue
            start, end = row['t_start'], row['t_end']
            nearby = ticks[max(0, bisect.bisect_right(begins,start)-2):bisect.bisect_right(begins,end)+2]
            overlaps = [t for t in nearby if t['t_begin'] < end and t['t_begin']+t['engine_ms']+t['host_ms'] > start]
            gaps = [{'fromMs': a['t_begin']+a['engine_ms']+a['host_ms'], 'toMs': b['t_begin'],
                     'unmeasuredMs': b['t_begin']-a['t_begin']-a['engine_ms']-a['host_ms']}
                    for a,b in zip(nearby,nearby[1:]) if a['t_begin']+a['engine_ms']+a['host_ms'] < end and b['t_begin'] > start]
            sampled = [s for s in samples if start <= s['ms'] <= end]
            cycles = sampled[-1]['threadCycles']-sampled[0]['threadCycles'] if len(sampled)>1 else None
            episodes.append({'atMs': start, 'utc': (origin+dt.timedelta(milliseconds=start)).astimezone(dt.timezone.utc).isoformat(),
                             'metrics': metrics, 'overlappingTicks': overlaps, 'tickGaps': gaps,
                             'closestIniWriteDeltaMs': min((w-end for w in writes),key=abs) if writes else None,
                             'sampleCount': len(sampled), 'observerSuspensionMs': sum(s['suspendedMs'] for s in sampled),
                             'sampledThreadCpuMs': cycles*1000/meta['clocks']['cycleFrequency'] if cycles is not None else None,
                             'sampledStacks': dict(Counter(s['stack'] for s in sampled))})
        long_ticks = [t for t in ticks if t['engine_ms'] > 10 or t['host_ms'] > 10]
        result.append({'probe': path.name, 'logOrigin': origin.isoformat(), 'iniWriteAtMs': writes,
                       'intrusive': bool(samples), 'episodes': sorted(episodes,key=lambda e:e['metrics']['interval'],reverse=True),
                       'longTicks': long_ticks,
                       'longTickSamples': [{**t,
                                            'engineSamples': sampled_span(t['t_begin'],t['t_begin']+t['engine_ms']),
                                            'hostSamples': sampled_span(t['t_begin']+t['engine_ms'],t['t_begin']+t['engine_ms']+t['host_ms'])}
                                           for t in long_ticks] if samples else []})
    (directory / 'investigation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    for r in result:
        print(json.dumps({'run': directory.name, 'probe': r['probe'], 'writesAtMs': writes,
                          'longTicks': r['longTicks'], 'worst': r['episodes'][:3]}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    args = p.parse_args()
    inspect(args.directory)
