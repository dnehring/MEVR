# SPDX-License-Identifier: MIT
"""Compare restored startup repetitions, keeping transition gaps separate from settings-reset hitches."""
import argparse
import json
import statistics
from pathlib import Path


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def compare(baseline, candidate, output):
    errors, runs, fingerprints, hosts, directories = [], [], [], [], []
    variant_modules = {}
    for label, batch in [('baseline', baseline), ('candidate', candidate)]:
        entries = read(batch / 'runs.json')
        if isinstance(entries, dict):
            entries = [entries]
        if len(entries) < 2:
            errors.append(f'{label}: fewer than two repetitions')
        modules = []
        for entry in entries:
            folder = Path(entry['directory'])
            directories.append(str(folder.resolve()))
            analysis = read(folder / 'startup-analysis.json')
            if not analysis['measurementValidity']['valid']:
                errors.append(f'{folder.name}: measurement invalid')
            builds = {f['name']: f['sha256'] for f in read(folder / 'build-files.json')}
            hosts.append(builds['LE1MEVR.asi'])
            modules.append(builds['LE1MEVR_mod.dll'])
            machine, context = read(folder / 'machine.json'), read(folder / 'context.json')
            fingerprints.append([read(folder / 'workload.json'), analysis['resolution'], context['ActiveRuntime'],
                                 {k: machine[k] for k in ('cpu', 'gpu', 'powerScheme')},
                                 read(folder / 'staged.json')['saveSha256'],
                                 [(folder / 'config' / name).read_text(encoding='utf-8-sig')
                                  for name in ('LE1MEVR.ini', 'MELEVR.ini')]])
            settling = [a for a in analysis['settingsApplies'] if 'the level has rendered' in a['log']]
            reset_ticks = [t for a in settling for t in a['nearbyTicks']]
            runs.append({'variant': label, 'run': folder.name, 'directory': str(folder),
                         'threadSwitches': analysis['renderInstrumentation']['threadSwitches'],
                         'handoffs': analysis['threadHandoffs'], 'settlingResets': len(settling),
                         'resetHostMaxMs': max((t['host_ms'] for t in reset_ticks), default=None),
                         'wholeHookMaxMs': analysis['regressionChecks']['wholeHookMaxMs'],
                         'phases': [{k: p[k] for k in ('name', 'frames', 'interval', 'render_work', 'wait_wall', 'engine_ms', 'host_ms')}
                                    for p in analysis['phases']]})
        if len(set(modules)) != 1:
            errors.append(f'{label}: module changed between repetitions')
        variant_modules[label] = set(modules)
    if variant_modules['baseline'] & variant_modules['candidate']:
        errors.append('Baseline and candidate used the same module; no implementation comparison occurred')
    if len(set(hosts)) != 1:
        errors.append('Host changed across baseline and candidate')
    if len(set(directories)) != len(directories):
        errors.append('A capture was reused as an independent repetition')
    if len({json.dumps(f, sort_keys=True) for f in fingerprints}) != 1:
        errors.append('Workload, source/eye size, runtime, hardware, settings or save changed')
    summary = {}
    for label in ('baseline', 'candidate'):
        selected = [r for r in runs if r['variant'] == label]
        summary[label] = {'runs': len(selected), 'settlingResets': sum(r['settlingResets'] for r in selected),
                          'threadSwitches': sum(r['threadSwitches'] for r in selected),
                          'maxWholeHookMs': max(r['wholeHookMaxMs'] for r in selected),
                          'medianWholeHookMaxMs': statistics.median(r['wholeHookMaxMs'] for r in selected)}
    result = {'valid': not errors, 'errors': errors, 'summary': summary, 'runs': runs,
              'settlingResetRemoved': summary['baseline']['settlingResets'] > 0 and summary['candidate']['settlingResets'] == 0,
              'scope': 'Null-headset startup comparison. Wall gaps across threads remain; CPU on handoff rows is unknown. No physical-runtime pacing acceptance.'}
    output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('valid', 'errors', 'summary', 'settlingResetRemoved')}, indent=2))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    raise SystemExit(0 if compare(args.baseline, args.candidate, args.output)['valid'] else 1)
