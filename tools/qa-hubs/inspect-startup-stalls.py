# SPDX-License-Identifier: MIT
"""Correlate saved startup CPU peaks with nearby logs; never attach to a running game."""
import argparse
import datetime as dt
import importlib.util
import json
import re
from pathlib import Path

spec = importlib.util.spec_from_file_location('probe', Path(__file__).resolve().parents[1] / 'perf-probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def inspect(folder, count=5):
    analysis = json.loads((folder / 'startup-analysis.json').read_text(encoding='utf-8-sig'))
    meta = json.loads((folder / analysis['probe']).read_text(encoding='utf-8-sig'))
    origin = dt.datetime.fromisoformat(analysis['logOrigin'])
    logs = []
    for line in (folder / 'host.log').read_text(encoding='utf-8-sig', errors='replace').splitlines():
        match = re.match(r'\[(\d\d:\d\d:\d\d\.\d+)\]', line)
        if match:
            when = dt.datetime.combine(origin.date(), dt.time.fromisoformat(match[1]), origin.tzinfo)
            seconds = (when - origin).total_seconds()
            if seconds < -43200:
                seconds += 86400
            logs.append((seconds * 1000, line))

    def nearby(start, end):
        return [line for when, line in logs if start - 250 <= when <= end + 250]

    frames = []
    for row in probe.load_rows(folder / meta['files']['frames']):
        metrics = probe.frame_metrics(row)
        if metrics['render_work'] is not None and row['t_start'] >= 1000:
            frames.append({'frame': row['frame'], 'startMs': row['t_start'], 'endMs': row['t_end'],
                           'renderBusyMs': metrics['render_work'], 'intervalMs': metrics['interval'],
                           'gameBusyMs': metrics['game_busy']})
    frames = sorted(frames, key=lambda r: r['renderBusyMs'], reverse=True)[:count]
    for row in frames:
        row['nearbyHostLog'] = nearby(row['startMs'], row['endMs'])
    ticks = sorted(probe.load_rows(folder / meta['files']['ticks']), key=lambda r: r['hook_ms'], reverse=True)[:count]
    tick_results = []
    for tick in ticks:
        start, end = probe.tick_bounds(tick)
        tick_results.append({**{key: tick.get(key) for key in ('tick', 't_begin', 'hook_ms', 'engine_ms', 'engine_busy_ms', 'host_ms')},
                             'nearbyHostLog': nearby(start, end)})
    result = {'scope': 'Saved-data correlation only. Host-log wall clocks are approximate; nearby messages do not prove the cost of their named routines.',
              'run': folder.name, 'renderCpuPeaks': frames, 'hookPeaks': tick_results}
    (folder / 'startup-stalls.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--count', type=int, default=5)
    args = parser.parse_args()
    if args.count < 1:
        parser.error('--count must be positive')
    result = inspect(args.directory, args.count)
    print(json.dumps(result, indent=2))
