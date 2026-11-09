# SPDX-License-Identifier: MIT
"""Validate Citadel measurement sessions; report budget failures separately from capture validity."""
import argparse
import configparser
import importlib.util
import json
import statistics
from pathlib import Path

spec = importlib.util.spec_from_file_location('probe', Path(__file__).resolve().parents[1] / 'perf-probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def compare(directory, partial=False):
    plan = read(directory / 'plan.json')
    runs = read(directory / 'runs.json')
    if isinstance(runs, dict):
        runs = [runs]
    errors, rows, builds, hardware, invariants = [], [], [], [], []
    if not partial and [r['workload'] for r in runs] != plan['workloads']:
        errors.append('Completed workload order/count differs from the fixed plan')
    if not runs:
        errors.append('No captures')
    for r in runs:
        d = Path(r['directory'])
        label = d.name
        try:
            receipt, restored = read(d / 'session-result.json'), read(d / 'restored.json')
            expected, analyses = read(d / 'workload.json'), read(d / 'analysis.json')
            if len(analyses) != 1:
                raise ValueError('expected one clean probe')
            a = analyses[0]
            meta = read(d / a['source'])
            trace = read(d / 'tick-trace-check.json')
            if not receipt['completed'] or not receipt['restored'] or not receipt['nativeLoadValidated'] or receipt['pawnMovementUU'] > 60:
                raise ValueError('session/load/stationary/restoration check failed')
            if restored['verifiedFiles'] != len(read(d / 'before.json')['files']) or not trace['passed'] or (d / 'diagnostic.json').exists():
                raise ValueError('missing restoration/trace validation or intrusive capture')
            if a['resolution']['render'] != expected['source'] or a['resolution']['eye'] != expected['eye'] or a['resolution']['recommendedEye'] != expected['eye']:
                raise ValueError('observed resolution differs from workload')
            upscaled = expected['source'] != expected['eye']
            if meta['upscale']['method'] != 'fsr' or bool(meta['upscale']['active']) != upscaled:
                raise ValueError('FSR activation differs from workload')
            if upscaled and (meta['upscale']['source'] != expected['source'] or meta['upscale']['target'] != expected['eye']):
                raise ValueError('FSR source/target differs from workload')
            levers = a['renderLevers']
            if not a['halcyon']['sfr2'] or levers['sharePass'] != 5 or levers['minScreenRadiusPx'] != expected['cullPx']:
                raise ValueError('One Draw/culling workload mismatch')
            if a['accessViolations'] or a['gpuFramesDropped'] or levers['shareFailed'] or levers['replayFailed'] or levers['drawReplay']['replayFailures']:
                raise ValueError('capture recorded render fault or dropped GPU timings')
            if not levers['motionBlurGuard']['protectsGameplay']:
                raise ValueError('motion blur guard lost coverage')
            if meta['seconds'] < plan['seconds'] - 1 or not meta['gpuTimestamps']:
                raise ValueError('capture duration or GPU clock calibration invalid')
            log = (d / 'host.log').read_text(encoding='utf-8-sig', errors='replace')
            settings = [line for line in log.splitlines() if 'system settings: scale set ' in line][-1]
            for text in ('DynamicShadows False', 'MotionBlur False', 'AmbientOcclusion ' + str(expected['ambientOcclusion']),
                         'FoliageDrawRadiusMultiplier ' + expected['foliage'].rstrip('0').rstrip('.')):
                if text not in settings:
                    raise ValueError('last live settings application differs from workload: ' + text)
            config = configparser.ConfigParser(strict=False)
            config.read(d / 'config/LE1MEVR.ini', encoding='utf-8-sig')
            builds.append(sorted(read(d / 'build-files.json'), key=lambda x: x['name']))
            machine = read(d / 'machine.json')
            hardware.append({k: machine[k] for k in ('cpu', 'gpu', 'powerScheme')})
            invariants.append([a['halcyon'], {k: levers[k] for k in ('pCoresOnly','umbraQueryWait','ue3QueryWait','perEyeViewState','perEyeOcclusion','sharePass')},
                               config.get('SystemSettings', 'AntiAliasing'), read(d / 'staged.json')['saveSha256']])
            frames = probe.load_rows(d / meta['files']['frames'])
            intervals = [probe.frame_metrics(f)['interval'] for f in frames
                         if f['xr_frame'] and f['should_render'] and 1000 <= f['t_start'] < (meta['seconds']-1)*1000]
            s, t = a['all'], a['tickSummary']
            rows.append({'run': label, 'directory': str(d), 'workload': r['workload'], 'source': a['resolution']['render'],
                         'frames': s['frames'], 'intervalMs': s['interval'], 'renderBusyMs': s['render_work'],
                         'gpuSpanMs': s['gpu_work'], 'engineMs': t['engineMs'], 'hostMs': t['hostMs'],
                         'hookMs': t['hookMs'], 'endFrameMs': s['end_frame'], 'draws': s['draws'],
                         'iniWrites': len(read(d / 'capture-window.json')['iniWrites']),
                         'budgets': {str(hz): {'budgetMs': 1000/hz, 'exceeded': sum(v > 1000/hz for v in intervals),
                                              'p99Pass': s['interval']['p99'] <= 1000/hz} for hz in (72,90,120)},
                         'over22ms': s['over22ms'],
                         'severeHitchPass': s['interval']['max'] <= plan['severeIntervalMs'] and t['hookMs']['max'] <= plan['severeHookMs']})
        except (ValueError, KeyError, IndexError, OSError) as e:
            errors.append(f'{label}: {e}')
    for name, values in [('build', builds), ('hardware/power', hardware), ('non-workload settings/save', invariants)]:
        if len({json.dumps(v, sort_keys=True) for v in values}) > 1:
            errors.append(name + ' changed across captures')
    summary = {}
    for workload in dict.fromkeys(plan['workloads']):
        selected = [r for r in rows if r['workload'] == workload]
        if selected:
            summary[workload] = {'captures': len(selected), 'medianP99Ms': statistics.median(r['intervalMs']['p99'] for r in selected),
                                 'worstIntervalMs': max(r['intervalMs']['max'] for r in selected),
                                 'worstHookMs': max(r['hookMs']['max'] for r in selected),
                                 'allP99At90HzPass': all(r['budgets']['90']['p99Pass'] for r in selected),
                                 'allSevereHitchPass': all(r['severeHitchPass'] for r in selected)}
    result = {'valid': not errors, 'partial': partial, 'errors': errors, 'runs': rows, 'summary': summary,
              'scope': 'Stationary null-headset throughput. Budget checks are not physical display/drop measurements. Workloads intentionally differ.'}
    (directory / 'comparison.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('valid','partial','errors','summary')}, indent=2))
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--partial', action='store_true')
    args = p.parse_args()
    raise SystemExit(0 if compare(args.directory, args.partial)['valid'] else 1)
