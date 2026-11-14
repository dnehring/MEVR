# SPDX-License-Identifier: MIT
"""Export a correlated archived resource observation; no live processes or requests."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import struct
import sys


def module(name):
    spec = importlib.util.spec_from_file_location('le2_export_' + name, Path(__file__).with_name(name + '.py'))
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


control, scenario = module('control'), module('scenario')
session = control.session
FIELDS = dict(credits='Credits', medigel='Medigel', eezo='Eezo', iridium='Iridium',
              palladium='Palladium', platinum='Platinum', probes='Probes', fuel='CurrentFuel')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def raw(path, limit=16 * 1024 * 1024):
    path = session.safe_path(path)
    require(path.is_file() and path.stat().st_size <= limit, f'Missing or oversized input: {path.name}')
    value = path.read_bytes()
    require(len(value) <= limit, 'Input grew beyond bound')
    return value


def pairs(items):
    result = {}
    for key, value in items:
        require(key not in result, f'Duplicate JSON key: {key}')
        result[key] = value
    return result


def parse(data):
    def bad_number(value):
        raise ValueError(f'Nonfinite JSON number: {value}')
    def finite_float(value):
        number = float(value)
        require(math.isfinite(number), f'JSON number exceeds finite range: {value}')
        return number
    value = json.loads(data.decode('utf-8-sig'), object_pairs_hook=pairs,
                       parse_constant=bad_number, parse_float=finite_float)
    require(isinstance(value, dict), 'JSON object required')
    return value


def read(path):
    return parse(raw(path))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def normalize(payload):
    capabilities = payload.get('capabilities')
    require(payload.get('source') == 'diagnostic-host' and payload.get('game') == 'LE2'
            and payload.get('readOnly') is True and payload.get('ready') is False
            and isinstance(capabilities, dict) and capabilities.get('observeResources') is True,
            'Not a completed read-only diagnostic resource producer payload')
    observation = payload.get('resourceObservation')
    require(isinstance(observation, dict) and type(observation.get('schemaVersion')) is int
            and observation['schemaVersion'] == 1 and observation.get('game') == 'LE2'
            and observation.get('validationScope') == 'direct-live-inventory-POD-sample'
            and observation.get('dispatchCapability') is False and observation.get('saveCacheUsed') is False
            and observation.get('persistenceProven') is False, 'Unexpected resource observation contract')
    context, resources = observation.get('context'), observation.get('resources')
    require(isinstance(context, dict) and isinstance(resources, dict), 'Resource context/fields required')
    require(type(context.get('available')) is bool and type(observation.get('allResourcesAvailable')) is bool,
            'Availability flags must be boolean')
    state, unavailable = {'resources': {}}, {}
    stable = context['available']
    for key, field in FIELDS.items():
        sample = resources.get(key)
        reason = 'field-missing'
        if isinstance(sample, dict):
            reason = sample.get('reason', 'reason-unavailable')
            require(type(sample.get('available')) is bool, f'{key}: availability flag must be boolean')
            if sample['available']:
                value = sample.get('value')
                scalar = 'float32' if key == 'fuel' else 'int32'
                valid = type(value) in (int, float) and math.isfinite(value) and value >= 0 if key == 'fuel' else type(value) is int and 0 <= value <= 0x7fffffff
                require(sample.get('sourceField') == 'SFXGame.SFXInventoryManager.' + field
                        and sample.get('scalarType') == scalar, f'{key}: wrong source or scalar type')
                require(valid, f'{key}: invalid available scalar value')
                bits = sample.get('sampleBits')
                require(isinstance(bits, str) and re.fullmatch(r'0x[0-9A-F]{8}', bits), f'{key}: invalid sample bits')
                decoded = struct.unpack('<f' if key == 'fuel' else '<i', int(bits[2:], 16).to_bytes(4, 'little'))[0]
                require(math.isfinite(decoded) and decoded == value, f'{key}: value differs from sampled bits')
                if stable:
                    state['resources'][key] = value
                    continue
        unavailable['resources.' + key] = {'reason': reason if stable else 'context-unavailable', 'sample': sample}
    for key in ('map', 'position'):
        sample = context.get(key)
        if stable and isinstance(sample, dict) and sample.get('available') is True:
            if key == 'map' and isinstance(sample.get('value'), str) and sample['value'] and '\0' not in sample['value']:
                state['map'] = sample['value']
                continue
            if key == 'position' and all(type(sample.get(c)) in (int, float) and math.isfinite(sample[c]) for c in ('x', 'y', 'z')):
                state['position'] = {c: sample[c] for c in ('x', 'y', 'z')}
                continue
        unavailable[key] = {'reason': 'context-unavailable' if not stable else 'missing-or-invalid-observation', 'sample': sample}
    if not observation['allResourcesAvailable'] and len(state['resources']) == 8:
        unavailable['resourceSummary'] = {'reason': 'producer-reported-incomplete-resources'}
    return state, unavailable


def export(run_path, request_id, case_id):
    require(control.valid_uuid(request_id), 'Canonical nonzero request UUID required')
    run = session.safe_path(Path(run_path).absolute())
    # Strict JSON first; lifecycle helper remains the shared source of session/root rules.
    context, plan = read(run / 'context.json'), read(run / 'plan.json')
    checked_run, checked_context = session.load_context(run, require_lock=False)
    require(checked_run == run and checked_context == context and context.get('backupComplete') is True,
            'Protected context is incomplete or changed')
    output = session.safe_path(run / 'artifacts' / ('state-' + request_id + '.json'))
    report_path = session.safe_path(run / 'artifacts' / ('state-' + request_id + '.check.json'))
    require(not output.exists() and not report_path.exists(), 'Export output already exists')
    descriptor = read(run / 'control' / 'identity.json')
    wire_identity = descriptor.get('identity')
    require(type(descriptor.get('schemaVersion')) is int and descriptor['schemaVersion'] == 1 and descriptor.get('source') == 'diagnostic-host'
            and descriptor.get('ready') is False and isinstance(wire_identity, dict)
            and set(wire_identity) == set(control.IDENTITY), 'Invalid frozen runtime descriptor')
    request_path = run / 'control' / 'history' / (request_id + '.request')
    response_path = run / 'control' / 'history' / (request_id + '.result')
    request_bytes, response_bytes = raw(request_path, control.MAX_FRAME), raw(response_path, control.MAX_FRAME)
    request, response = control.decode(request_bytes), control.decode(response_bytes)
    require(request['kind'] == 'request' and request['id'] == request_id and request['verb'] == 'observe-resources'
            and not request['payload'] and response['kind'] == 'result' and response['status'] == 'completed',
            'Requires archived empty observe-resources request and completed result')
    require(all(request[k] == response[k] for k in (*control.IDENTITY, 'id', 'sequence', 'deadline', 'verb'))
            and all(request[k] == wire_identity[k] for k in control.IDENTITY), 'Uncorrelated archived response')
    # consumed-N counts attempts, not client sequence. Match exact bytes within a bounded scan.
    consumed = list((run / 'control').glob('consumed-*.request'))
    require(len(consumed) <= 4096, 'Consumed request archive exceeds bound')
    matches = [p for p in consumed if raw(p, control.MAX_FRAME) == request_bytes]
    require(len(matches) == 1, 'Missing or ambiguous consumed request evidence')
    process = context.get('gameProcess', {})
    require(isinstance(process, dict) and type(process.get('pid')) is int and process.get('name') == 'MassEffect2'
            and request['pid'] == str(process['pid']) and request['start'] == control.filetime(process['started'])
            and request['run'] == context['runId'] and request['exe'] == context['exeSha256']
            and session.safe_path(Path(process['path'])) == session.safe_path(Path(context['exePath']))
            == session.safe_path(Path(plan['gameRoot']) / 'Game/ME2/Binaries/Win64/MassEffect2.exe')
            and plan.get('exeSha256') == request['exe'], 'Archived process identity differs from session')
    deployment = read(run / 'deployment.json')
    commit = deployment.get('candidateCommit')
    require(isinstance(commit, str) and re.fullmatch(r'[0-9a-f]{40}', commit)
            and deployment.get('asiSha256') == request['host'] == request['mod'], 'Deployment attestation differs from wire modules')
    for name, key in (('scenario.json', 'scenarioSha256'), ('recipe.json', 'recipeSha256'),
                      ('preparation-receipt.json', 'preparationReceiptSha256')):
        contents = raw(run / name)
        parse(contents)
        require(sha(contents) == context.get(key), 'Frozen artifact changed: ' + name)
    fixture_path = session.safe_path(run / 'fixture.pcsav')
    require(fixture_path.is_file() and fixture_path.stat().st_size <= 512 * 1024 * 1024
            and session.digest(fixture_path) == context.get('fixtureSha256'), 'Frozen fixture changed')
    frozen = scenario.load(run / 'scenario.json', for_run=True, recipe_path=run / 'recipe.json')
    require(case_id in frozen['variants'], 'Case is not a declared scenario variant')
    identity = dict(runId=context['runId'], caseId=case_id, scenarioSha256=context['scenarioSha256'],
                    baseSaveSha256=frozen['fixture']['baseSaveSha256'], recipeSha256=context['recipeSha256'],
                    preparedSaveSha256=context['fixtureSha256'], candidateCommit=commit,
                    gameVersion='exe-sha256:' + request['exe'], gameExeSha256=request['exe'], loadedModSha256=request['mod'])
    # Reuse the evidence auditor's complete frozen artifact/receipt binding.
    binding = scenario.bind_session(identity, run)
    require(binding['processBound'] and isinstance(plan.get('fixture'), dict) and isinstance(plan.get('scenario'), dict)
            and plan['fixture'].get('sha256') == context['fixtureSha256']
            and plan['scenario'].get('sha256') == context['scenarioSha256'], 'Frozen plan inputs differ')
    payload = parse(response['payload'])
    state, unavailable = normalize(payload)
    require(control.number(payload.get('gameTick')) > 0 and control.number(payload.get('gameThreadId'), (1 << 32) - 1) > 0,
            'Missing sample tick/thread')
    control.number(payload.get('sampleMonotonicMs'))
    artifact = dict(schemaVersion=1, source='runtime-observation', identity=identity, state=state, unavailable=unavailable,
                    provenance=dict(requestId=request_id, wireIdentity=wire_identity, requestSha256=sha(request_bytes),
                        responseSha256=sha(response_bytes), consumedRequest=matches[0].name,
                        gameTick=payload['gameTick'], gameThreadId=payload['gameThreadId'], sampleMonotonicMs=payload['sampleMonotonicMs'],
                        deadline=request['deadline'], deploymentSha256=session.digest(run / 'deployment.json'),
                        candidateCommitEvidence='reviewed-deployment-receipt-attestation-not-binary-commit-proof',
                        session=binding, runtimeObservation=payload['resourceObservation']),
                    claims=dict(visualVerified=False, loadedFixtureProven=False, persistenceProven=False, vrVerified=False))
    failures = scenario.check_start(frozen, artifact)
    failures.extend('unavailable observation: ' + key for key in unavailable)
    report = dict(schemaVersion=1, identity=identity, sourceRequestId=request_id,
                  status='matched-observed-start-only' if not failures else 'starting-state-check-failed',
                  failures=failures, scope='No scene, fixture-load, persistence, input or VR acceptance')
    state_bytes = (json.dumps(artifact, indent=2, allow_nan=False) + '\n').encode('utf-8')
    report['stateSha256'] = sha(state_bytes)
    report_bytes = (json.dumps(report, indent=2, allow_nan=False) + '\n').encode('utf-8')
    output.parent.mkdir(exist_ok=True)
    # Nonreplacing publication: a crash/partial pair is preserved, never overwritten.
    # Exported JSON includes full bounded observation and is not a wire frame.
    for target, data in ((output, state_bytes), (report_path, report_bytes)):
        with session.safe_path(target).open('xb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    return (2 if failures else 0), output, report_path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--request', required=True)
    parser.add_argument('--case', required=True)
    args = parser.parse_args(argv)
    try:
        status, state, report = export(args.run, args.request, args.case)
        print(json.dumps({'state': str(state), 'check': str(report), 'exitCode': status}))
        return status
    except (ValueError, OSError, KeyError, TypeError, OverflowError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
