# SPDX-License-Identifier: MIT
"""Split a protected startup probe by recorded launch actions, keeping waits separate from CPU work."""
import argparse
import datetime as dt
import importlib.util
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('probe', ROOT / 'tools/perf-probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def dist(values):
    values = sorted(v for v in values if v is not None)
    return {key: round(values[round(q * (len(values) - 1))], 4)
            for key, q in [('p50', .5), ('p95', .95), ('p99', .99), ('max', 1)]} if values else {}


def analyze(folder):
    investigation = json.loads((folder / 'investigation.json').read_text(encoding='utf-8-sig'))
    if len(investigation) != 1:
        raise ValueError('Expected one startup probe')
    origin = dt.datetime.fromisoformat(investigation[0]['logOrigin'])
    meta = json.loads((folder / investigation[0]['probe']).read_text(encoding='utf-8-sig'))
    raw = probe.load_rows(folder / meta['files']['frames'])
    ticks = probe.load_rows(folder / meta['files']['ticks'])
    phase = json.loads((folder / 'context.json').read_text(encoding='utf-8-sig')).get('StartupPhase', 'Splash')
    transition = phase == 'Transition'
    rows = [{**probe.frame_metrics(r), 'at_s': r['t_start'] / 1000, 'frame': int(r['frame'])}
            for r in raw if (transition or (r['xr_frame'] and r['should_render'])) and
            1000 <= r['t_start'] < (meta['seconds'] - 1) * 1000]
    events = [json.loads(line) for line in (folder / 'launch-events.jsonl').read_text(encoding='utf-8-sig').splitlines()]
    for event in events:
        event['at_s'] = ((event['qpc'] - meta['clocks']['originQpc']) / meta['clocks']['qpcFrequency']
                         if meta['clocks'].get('originQpc') and 'qpc' in event else
                         (dt.datetime.fromisoformat(event['utc']) - origin).total_seconds())
    enters = [e['at_s'] for e in events if e['kind'] == 'enter-posted']
    ready = next(e['at_s'] for e in events if e['kind'] == 'gameplay-ready')
    if len(enters) != 2:
        raise ValueError('Phase naming requires exactly two Enter presses; inspect launch events before labeling')
    phases = [('held_splash', 1, enters[0] - 1), ('title_flight_and_menu', enters[0], enters[1]),
              ('resume_loading', enters[1], ready), ('gameplay_settling', ready, ready + 17),
              ('settled_gameplay', ready + 17, meta['seconds'] - 1)]
    focus = [e['at_s'] for e in events if e['kind'] == 'foreground-activate' and e.get('foreground')
             and e.get('requested') and e['at_s'] < meta['seconds'] - 3]
    if focus:
        segment_end = enters[0] - 1 if phase == 'Splash' else enters[1] - 1
        phases.append(('foreground_title_segment', max(1, focus[-1] + 2), min(segment_end, meta['seconds'] - 1)))
    keys = ['interval', 'render_work', 'game_busy', 'wait_wall', 'wait_busy', 'end_frame', 'gpu_work', 'frame_pace']
    result = {'probe': investigation[0]['probe'], 'logOrigin': origin.isoformat(),
              'boundary': 'One second trimmed at capture edges. Transition includes non-XR frames and wall gaps across thread handoffs. Launch events use QPC when available; host log alignment is approximate.',
              'renderInstrumentation': meta.get('renderInstrumentation'),
              'threadHandoffs': [{k: r[k] for k in ('frame','t_start','t_end','render_thread','previous_render_thread','cpu_valid')}
                                 for r in raw if r.get('cpu_valid') == 0],
              'events': events, 'phases': [], 'notCaptured': [], 'settingsApplies': [],
              'accessViolations': meta['accessViolations'], 'gpuFramesDropped': meta['gpuFramesDropped'],
              'tickInstrumentation': meta.get('tickInstrumentation'), 'renderLevers': meta['renderLevers'],
              'resolution': meta['resolution']}
    receipt = json.loads((folder / 'session-result.json').read_text(encoding='utf-8-sig'))
    final = json.loads((folder / 'verified-final-state.json').read_text(encoding='utf-8-sig'))
    levers = meta['renderLevers']
    render = meta.get('renderInstrumentation', {})
    boundaries = [r for r in raw if r.get('cpu_valid') == 0]
    checks = {
        'sessionCompletedAndRestored': receipt.get('completed') and receipt.get('restored') and final.get('passed'),
        'captureDurationComplete': meta['seconds'] >= meta['secondsRequested'] - .1,
        'rawFrameCountMatches': len(raw) == meta['frames'],
        'rawTickCountMatches': len(ticks) == meta['ticks'],
        'noAccessViolations': meta['accessViolations'] == 0,
        'gpuTimesComplete': meta['gpuTimestamps'] and meta['gpuFramesDropped'] == 0,
        'traceComplete': meta.get('tickInstrumentation', {}).get('version') == 2 and
                         meta['tickInstrumentation']['ticksDropped'] == 0 and meta['tickInstrumentation']['callsDropped'] == 0,
        'oneDrawHealthy': levers['sharePass'] == 5 and not levers['shareFailed'] and not levers['replayFailed'],
        'blurGuardHealthy': levers.get('motionBlurGuard', {}).get('protectsGameplay') is True,
        'requestedPhasesCaptured': (meta['seconds'] > ready + 17 and meta.get('renderInstrumentation', {}).get('version') == 3)
                                  if transition else meta['seconds'] < enters[1],
        'noOverlappingPresents': meta.get('renderInstrumentation', {}).get('overlappingPresents', 0) == 0,
        'sameRenderContext': meta.get('renderInstrumentation', {}).get('contextChanges', 0) == 0,
        'handoffRowsConsistent': (len(boundaries) == render.get('cpuBoundaryFrames') == render.get('threadSwitches') and
                                 all(r['render_thread'] != r['previous_render_thread'] and r['t_end'] >= r['t_start'] and
                                     all(v is None for k, v in r.items() if k.startswith('busy_')) for r in boundaries))
                                if render.get('version') == 3 else True,
        'transitionCoverage': 1 < enters[0] < enters[1] <= ready < meta['seconds'] - 17 if transition else True,
    }
    result['measurementValidity'] = {'valid': all(checks.values()), 'checks': checks}
    for name, start, end in phases:
        start, end = max(1, start), min(end, meta['seconds'] - 1)
        if start >= end:
            result['notCaptured'].append(name)
            continue
        chosen = [r for r in rows if start <= r['at_s'] < end]
        chosen_ticks = [t for t in ticks if start * 1000 <= t['t_begin'] < end * 1000]
        result['phases'].append({'name': name, 'start_s': start, 'end_s': end, 'frames': len(chosen),
                                **{key: dist(r.get(key) for r in chosen) for key in keys},
                                **{key: dist(t.get(key) for t in chosen_ticks)
                                   for key in ['engine_ms', 'engine_busy_ms', 'host_ms', 'hook_ms']},
                                'over22ms': sum(r['interval'] > 2000 / 90 for r in chosen),
                                'worst': sorted(chosen, key=lambda r: r['interval'], reverse=True)[:3]})
    for line in (folder / 'host.log').read_text(encoding='utf-8-sig', errors='replace').splitlines():
        match = re.match(r'\[(\d\d:\d\d:\d\d\.\d+)\].*system settings: scale set', line)
        if not match:
            continue
        when = dt.datetime.combine(origin.date(), dt.time.fromisoformat(match[1]), origin.tzinfo)
        offset = (when - origin).total_seconds() * 1000
        if not 0 <= offset < meta['seconds'] * 1000:
            continue
        overlaps = [t for t in ticks if probe.tick_bounds(t)[0] - 3 <= offset <= probe.tick_bounds(t)[1] + 3]
        result['settingsApplies'].append({'at_s': offset / 1000, 'log': line, 'nearbyTicks': overlaps})
    measured = [t for t in ticks if 1000 <= t['t_begin'] < (meta['seconds'] - 1) * 1000]
    hook_max = max((t['hook_ms'] for t in measured), default=None)
    result['regressionChecks'] = {'wholeHookLimitMs': 2000 / 90, 'wholeHookMaxMs': hook_max,
                                  'wholeHookPass': hook_max is not None and hook_max <= 2000 / 90,
                                  'physicalHeadsetPacingValidated': False}
    (folder / 'startup-analysis.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({'directory': str(folder), 'phases': [{k: v for k, v in p.items() if k != 'worst'} for p in result['phases']],
                      'settingsApplyCount': len(result['settingsApplies']), 'notCaptured': result['notCaptured'],
                      'measurementValidity': result['measurementValidity'], 'regressionChecks': result['regressionChecks']}))
    if not result['measurementValidity']['valid']:
        raise ValueError('Startup measurement validity failed; see startup-analysis.json')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    analyze(parser.parse_args().directory.resolve())
