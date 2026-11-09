# SPDX-License-Identifier: MIT
"""Compare repeated captures within each scenario; never pool different hub workloads."""
import argparse
import json
from pathlib import Path
import statistics


def summarize(root, p99_limit=None, interval_limit=None, tick_limit=None, min_runs=1):
    groups, errors = {}, []
    for folder in sorted(x for x in root.glob('hub-*') if x.is_dir()):
        try:
            def read(name): return json.loads((folder / name).read_text(encoding='utf-8-sig'))
            ctx, route, state = read('context.json'), read('route.json'), read('final-state.json')
            machine = read('machine.json')
            machine_identity = {k: machine[k] for k in ('cpu', 'gpu', 'os', 'powerScheme')}
            if ctx.get('SkipCapture'):
                errors.append(f'{folder.name}: route-only run has no performance acceptance'); continue
            if ctx.get('ScenarioStatus') not in ('ready', 'validated') or not route.get('routePassed'):
                raise ValueError('draft or incomplete route')
            if not (folder / 'restored.json').exists() or any(state.get(k) for k in ('mismatches', 'requests', 'processes')):
                raise ValueError('missing restoration or unclean final state')
            if route.get('scenarioSha256') != ctx.get('ScenarioSha256'):
                raise ValueError('route/scenario identity differs')
            if not ctx.get('FixtureSelectedSha256') or not ctx.get('FixtureInventorySha256'):
                raise ValueError('selected starting-save identity unverified; map/pose alone is not a story-state fixture')
            analyses = read('analysis.json')
            if len(analyses) != 1: raise ValueError('expected one clean capture')
            r = analyses[0]
            raw_meta = read(r['source'])
            levers = r['renderLevers']
            if not r['halcyon'].get('sfr2') or levers.get('sharePass') != 5:
                raise ValueError('One Draw/sharePass=5 required')
            if r['accessViolations'] or r['gpuFramesDropped'] or levers.get('shareFailed') or levers.get('replayFailed') or levers.get('drawReplay', {}).get('replayFailures'):
                raise ValueError('probe correctness/measurement failure')
            instrumentation = r.get('tickInstrumentation', raw_meta.get('tickInstrumentation', {})) or {}
            if any(r.get(k, 0) or raw_meta.get(k, 0) or instrumentation.get(k, 0) for k in ('ticksDropped', 'callsDropped')):
                raise ValueError('tick/call trace dropped samples')
            tick_scope = r.get('tickSummary', {}).get('totalScope', 'engineHostOnly')
            if tick_scope not in ('wholeHook', 'engineHostOnly'):
                raise ValueError('mixed or empty tick timing scope')
            if instrumentation.get('version', 0) >= 2 and tick_scope != 'wholeHook':
                raise ValueError('v2 tick instrumentation requires wholeHook timing scope')
            fingerprint = json.dumps([ctx['ScenarioSha256'], ctx['ExpectedAsiSha256'], ctx['Preset'], ctx['FixtureSelectedSha256'],
                                      ctx['FixtureInventorySha256'], machine_identity, r['resolution'], r['halcyon'],
                                      instrumentation.get('version'), tick_scope,
                                      {k: levers.get(k) for k in ('sharePass', 'umbraQueryWait', 'ue3QueryWait', 'perEyeViewState', 'perEyeOcclusion', 'pCoresOnly')}], sort_keys=True)
            s, tick = r['all'], r['tickSummary']
            row = dict(run=folder.name, p99Ms=s['interval']['p99'], maxMs=s['interval']['max'],
                       engineMaxMs=tick['engineMs']['max'], hostMaxMs=tick['hostMs']['max'], tickMaxMs=tick['totalMs']['max'],
                       intervalsOver11ms=s['over11ms'], intervalsOver22ms=s['over22ms'],
                       tickScope=tick_scope, tickInstrumentation=instrumentation,
                       fixtureSelectedSha256=ctx['FixtureSelectedSha256'], fixtureInventorySha256=ctx['FixtureInventorySha256'],
                       fixtureSelectionEvidence=ctx.get('FixtureSelectionEvidence'),
                       drainMs=tick.get('drainMs'), hookMs=tick.get('hookMs'), queuedCallSummary=r.get('queuedCallSummary'))
            group = groups.setdefault(ctx['ScenarioId'], dict(runs=[], fingerprints=[]))
            group['runs'].append(row); group['fingerprints'].append(fingerprint)
        except (OSError, KeyError, ValueError, TypeError) as e:
            errors.append(f'{folder.name}: {e}')
    for name, group in groups.items():
        group['comparable'] = len(set(group.pop('fingerprints'))) == 1
        if len(group['runs']) < min_runs: errors.append(f'{name}: fewer than {min_runs} complete runs')
        if not group['comparable']: errors.append(f'{name}: different build/settings/scenario fingerprints')
        group['medianP99Ms'] = statistics.median(r['p99Ms'] for r in group['runs'])
        for r in group['runs']:
            for key, limit in [('p99Ms', p99_limit), ('maxMs', interval_limit), ('tickMaxMs', tick_limit)]:
                if limit is not None and r[key] > limit: errors.append(f"{r['run']}: {key} exceeds {limit} ms")
    if not groups: errors.append('No completed hub captures')
    applied = any(x is not None for x in (p99_limit, interval_limit, tick_limit))
    return dict(passed=not errors, performanceLimitsApplied=applied, performanceAccepted=applied and not errors,
                scope='Per-scenario null-headset regression only; route validity and physical headset acceptance are separate.',
                scenarios=groups, errors=errors)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--max-p99-ms', type=float)
    p.add_argument('--max-interval-ms', type=float)
    p.add_argument('--max-tick-ms', type=float)
    p.add_argument('--output', type=Path)
    p.add_argument('--min-runs', type=int, default=1)
    a = p.parse_args()
    if a.min_runs < 1: p.error('--min-runs must be positive')
    result = summarize(a.directory, a.max_p99_ms, a.max_interval_ms, a.max_tick_ms, a.min_runs)
    (a.output or a.directory / 'hub-comparison.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)
