# SPDX-License-Identifier: MIT
"""Replay admission checks against a private copy of one real capture; never touch the game."""
import argparse
import contextlib
import importlib.util
import io
import json
import shutil
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location('matrix', Path(__file__).with_name('compare-matrix.py'))
matrix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('reference', type=Path, help='One completed named-workload session')
    args = p.parse_args()
    root = Path(__file__).resolve().parents[2] / 'scratch/qa-hubs'
    destination = Path(tempfile.mkdtemp(prefix='matrix-validation-', dir=root))
    run = destination / 'capture'
    run.mkdir()
    (run / 'config').mkdir()
    for name in ('session-result.json','restored.json','before.json','workload.json','analysis.json','tick-trace-check.json',
                 'host.log','build-files.json','machine.json','staged.json','capture-window.json','config/LE1MEVR.ini'):
        shutil.copy2(args.reference / name, run / name)
    analysis = matrix.read(run / 'analysis.json')
    probe_name = analysis[0]['source']
    shutil.copy2(args.reference / probe_name, run / probe_name)
    meta = matrix.read(run / probe_name)
    shutil.copy2(args.reference / meta['files']['frames'], run / meta['files']['frames'])
    workload = matrix.read(run / 'workload.json')['workload']
    plan = {'workloads': [workload], 'seconds': 90, 'severeIntervalMs': 2000/90, 'severeHookMs': 2000/90}
    (destination / 'plan.json').write_text(json.dumps(plan))
    (destination / 'runs.json').write_text(json.dumps([{'workload': workload, 'directory': str(run)}]))

    def check(valid):
        with contextlib.redirect_stdout(io.StringIO()):
            result = matrix.compare(destination)
        assert result['valid'] == valid, result['errors']
        return result

    baseline = check(True)
    # A slow but otherwise valid capture remains useful evidence; changing a limit cannot invalidate its capture.
    plan['severeIntervalMs'] = 0.01
    (destination / 'plan.json').write_text(json.dumps(plan))
    assert check(True)['runs'][0]['severeHitchPass'] is False
    for field, value in [('sharePass', 0), ('shareFailed', True)]:
        altered = json.loads(json.dumps(analysis))
        altered[0]['renderLevers'][field] = value
        (run / 'analysis.json').write_text(json.dumps(altered))
        check(False)
    (run / 'analysis.json').write_text(json.dumps(analysis))
    for field in ('overlappingPresents', 'contextChanges'):
        altered_meta = json.loads(json.dumps(meta))
        altered_meta['renderInstrumentation'] = {'version': 3, field: 1}
        (run / probe_name).write_text(json.dumps(altered_meta))
        check(False)
    (run / probe_name).write_text(json.dumps(meta))
    meta['resolution']['render'][0] += 2
    altered = json.loads(json.dumps(analysis))
    altered[0]['resolution'] = meta['resolution']
    (run / 'analysis.json').write_text(json.dumps(altered))
    check(False)
    (run / 'analysis.json').write_text(json.dumps(analysis))
    plan['workloads'].append(workload)
    (destination / 'plan.json').write_text(json.dumps(plan))
    check(False)
    print(f'Valid/slow capture admission, One Draw/fault/resolution rejection, and incomplete-plan rejection passed: {destination}')


if __name__ == '__main__':
    main()
