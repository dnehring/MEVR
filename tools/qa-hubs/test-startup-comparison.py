# SPDX-License-Identifier: MIT
"""Admission regressions for startup A/B evidence; no game or runtime is launched."""
import contextlib
import copy
import importlib.util
import io
import json
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location('startup_compare', Path(__file__).with_name('compare-startup.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def write(path, value):
    path.write_text(json.dumps(value), encoding='utf-8')


with tempfile.TemporaryDirectory(prefix='mevr-startup-compare-') as temporary:
    root = Path(temporary)
    batches = [root / label for label in ('baseline', 'candidate')]
    folders = []
    for variant, batch in enumerate(batches):
        batch.mkdir()
        entries = []
        for repeat in range(2):
            folder = batch / str(repeat)
            (folder / 'config').mkdir(parents=True)
            folders.append(folder)
            entries.append({'directory': str(folder), 'repeat': repeat + 1})
            write(folder / 'startup-analysis.json', {
                'measurementValidity': {'valid': True}, 'resolution': {'eye': [2284, 2448]},
                'renderInstrumentation': {'threadSwitches': 1}, 'threadHandoffs': [],
                'settingsApplies': [] if variant else [{'log': 'the level has rendered 15 s',
                                                       'nearbyTicks': [{'host_ms': 150.0}]}],
                'regressionChecks': {'wholeHookMaxMs': 1400.0}, 'phases': []})
            write(folder / 'build-files.json', [{'name': 'LE1MEVR.asi', 'sha256': 'same-host'},
                                                {'name': 'LE1MEVR_mod.dll', 'sha256': f'module-{variant}'}])
            write(folder / 'machine.json', {'cpu': 'cpu', 'gpu': 'gpu', 'powerScheme': 'power'})
            write(folder / 'context.json', {'ActiveRuntime': 'steamvr'})
            write(folder / 'workload.json', {'cullPx': 4, 'workload': 'Quality'})
            write(folder / 'staged.json', {'saveSha256': 'same-save'})
            for name in ('LE1MEVR.ini', 'MELEVR.ini'):
                (folder / 'config' / name).write_text('[fixed]\nvalue=1\n', encoding='utf-8')
        write(batch / 'runs.json', entries)

    def compare():
        with contextlib.redirect_stdout(io.StringIO()):
            return module.compare(*batches, root / 'comparison.json')

    accepted = compare()
    assert accepted['valid'] and accepted['settlingResetRemoved']
    assert accepted['summary']['candidate']['maxWholeHookMs'] == 1400.0  # load stall stays visible

    # The actual failed native attempt: two source variants silently produced identical binaries.
    for folder in folders[2:]:
        path = folder / 'build-files.json'
        data = json.loads(path.read_text())
        data[1]['sha256'] = 'module-0'
        write(path, data)
    result = compare()
    assert not result['valid'] and any('same module' in e for e in result['errors'])
    for folder in folders[2:]:
        path = folder / 'build-files.json'
        data = json.loads(path.read_text())
        data[1]['sha256'] = 'module-1'
        write(path, data)

    path = batches[1] / 'runs.json'
    original = json.loads(path.read_text())
    duplicate = copy.deepcopy(original)
    duplicate[1]['directory'] = duplicate[0]['directory']
    write(path, duplicate)
    assert not compare()['valid']
    write(path, original)

    (folders[-1] / 'config' / 'LE1MEVR.ini').write_text('[fixed]\nvalue=2\n', encoding='utf-8')
    assert not compare()['valid']
    (folders[-1] / 'config' / 'LE1MEVR.ini').write_text('[fixed]\nvalue=1\n', encoding='utf-8')
    path = folders[-1] / 'startup-analysis.json'
    data = json.loads(path.read_text())
    data['measurementValidity']['valid'] = False
    write(path, data)
    assert not compare()['valid']
print('Startup comparison admission passed: distinct binaries/repeats, fixed settings, valid measurements.')
