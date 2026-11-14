# SPDX-License-Identifier: MIT
"""Bound a reviewed New Save dispatch to a private disk candidate; never request a save."""
from __future__ import annotations
import argparse
import datetime as dt
import importlib.util
import json
import os
from pathlib import Path
import re
import stat
import sys


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


e = load('save_evidence_common', Path(__file__).with_name('export-state.py'))
c, s, scenario = e.control, e.session, e.scenario
adapter = load('save_evidence_adapter', Path(__file__).parents[1] / 'le2-save.py')
require, raw, read, sha = e.require, e.raw, e.read, e.sha
MAX_FILE, MAX_TOTAL, MAX_ENTRIES = 32 * 1024 * 1024, 256 * 1024 * 1024, 4096
BASELINE = 'artifacts/save-baseline.json'


def utc():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def leaf(value):
    require(isinstance(value, str) and 0 < len(value.encode('utf-16-le')) <= 510
            and not any(ord(ch) < 32 or ch in '<>:"/\\|?*' for ch in value)
            and value not in ('.', '..') and not value.endswith((' ', '.'))
            and not re.fullmatch(r'(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?', value, re.I),
            'Expected a bounded ordinary Windows leaf name')
    return value


def reviewer(value):
    require(isinstance(value, str) and value.strip() and '\0' not in value
            and len(value.encode('utf-8')) <= 256, 'A bounded reviewer attestation is required')
    return value


def quiet(directory):
    # client.lock may belong to control.submit, which calls verify_baseline here.
    for name in ('outstanding.json', 'request.control', 'result.control'):
        require(not s.safe_path(directory / name).exists(), 'Pending or unarchived control traffic: ' + name)
    history = s.safe_path(directory / 'history')
    require(history.is_dir(), 'Control history is missing')
    entries = []
    with os.scandir(history) as scan:
        for entry in scan:
            require(len(entries) < MAX_ENTRIES, 'Control history exceeds bound')
            path = s.safe_path(Path(entry.path))
            require(path.is_file(), 'Unexpected control history entry')
            require(not path.name.endswith('.error.result'), 'Retained uncertain/error result')
            entries.append(path)
    return entries


def sequence(directory):
    value = raw(directory / 'sequence.txt', 32).decode('ascii').strip()
    return c.number(value)


def stable_bytes(path):
    path = s.safe_path(path)
    def stamp():
        value = path.stat()
        require(stat.S_ISREG(value.st_mode) and value.st_size <= MAX_FILE, 'Nonregular or oversized career file')
        return (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns)
    first = stamp()
    one = raw(path, MAX_FILE)
    middle = stamp()
    two = raw(path, MAX_FILE)
    last = stamp()
    require(first == middle == last and one == two and len(one) == first[2], 'Career file changed during stable read')
    return one, dict(size=len(one), sha256=sha(one), mtimeNs=last[3])


def inventory(career):
    files, directories, seen = {}, [], set()
    count = total = 0
    pending = [career]
    while pending:
        folder = s.safe_path(pending.pop())
        require(folder.is_dir(), 'Career directory disappeared')
        with os.scandir(folder) as scan:
            for entry in scan:
                count += 1
                require(count <= MAX_ENTRIES, 'Career inventory exceeds entry bound')
                path = s.safe_path(Path(entry.path))
                relative = path.relative_to(career).as_posix()
                require(relative.casefold() not in seen, 'Ambiguous case-insensitive career path')
                seen.add(relative.casefold())
                if path.is_dir():
                    directories.append(relative)
                    pending.append(path)
                else:
                    data, record = stable_bytes(path)
                    total += len(data)
                    require(total <= MAX_TOTAL, 'Career inventory exceeds byte bound')
                    files[relative] = record
    return dict(files=dict(sorted(files.items())), directories=sorted(directories))


def inputs(run, case, wire, *, staged=True):
    _, context = s.load_context(run)
    require(context == read(run / 'context.json'), 'Context changed while reading')
    plan = read(run / 'plan.json')
    require(context.get('phase') == 'game-bound' and context.get('backupComplete') is True,
            'An owned game-bound protected session is required')
    descriptor = read(run / 'control/identity.json')
    process = context['gameProcess']
    require(descriptor.get('source') == 'diagnostic-host' and descriptor.get('ready') is False
            and descriptor.get('identity') == wire and set(wire) == set(c.IDENTITY)
            and wire['game'] == 'LE2' and wire['run'] == context['runId']
            and wire['pid'] == str(process['pid']) and process['name'] == 'MassEffect2'
            and wire['start'] == c.filetime(process['started'])
            and wire['exe'] == context['exeSha256'] == plan['exeSha256'], 'Process/lease identity differs from frozen session')
    deployment = read(run / 'deployment.json')
    commit = deployment.get('candidateCommit')
    require(isinstance(commit, str) and re.fullmatch('[0-9a-f]{40}', commit)
            and deployment.get('asiSha256') == wire['host'] == wire['mod'], 'Deployment receipt differs from runtime identity')
    for name in ('scenario.json', 'recipe.json', 'preparation-receipt.json'):
        read(run / name)  # Bound size and reject duplicate/nonfinite JSON before shared readers.
    require(sha(raw(run / 'fixture.pcsav', MAX_FILE)) == context['fixtureSha256'], 'Frozen fixture changed')
    frozen = scenario.load(run / 'scenario.json', for_run=True, recipe_path=run / 'recipe.json')
    require(case in frozen['variants'], 'Case is not a frozen scenario variant')
    identity = dict(runId=wire['run'], caseId=case, scenarioSha256=context['scenarioSha256'],
                    baseSaveSha256=frozen['fixture']['baseSaveSha256'], recipeSha256=context['recipeSha256'],
                    preparedSaveSha256=context['fixtureSha256'], candidateCommit=commit,
                    gameVersion='exe-sha256:' + wire['exe'], gameExeSha256=wire['exe'], loadedModSha256=wire['mod'])
    require(scenario.bind_session(identity, run)['processBound'], 'Frozen artifacts are not process bound')
    selection = plan['fixtureSelection']
    selected = selection['descriptor']
    career_name, save_name = leaf(selected['career']), leaf(selected['saveName'])
    require(type(selected['index']) is int and selected['index'] >= 0 and type(selected['type']) is int
            and selected['type'] == 0 and type(selection.get('schemaVersion')) is int
            and selection['schemaVersion'] == 1, 'Expected an explicit reviewed manual fixture descriptor')
    target_relative = 'Save/ME2/' + career_name + '/' + save_name + '.pcsav'
    require(selection['targetRelativePath'] == target_relative == plan['fixture']['targetRelativePath']
            and selection['fixtureSha256'] == plan['fixture']['sha256'] == context['fixtureSha256']
            and selection['exeSha256'] == wire['exe'], 'Frozen reviewed fixture mapping differs')
    proof = selection['evidence']
    reviewer(proof['reviewedBy'])
    require(c.valid_uuid(proof['runId']) and all(isinstance(proof.get(k), str) and re.fullmatch('[0-9a-f]{64}', proof[k])
            for k in ('observationSha256', 'captureSha256')), 'Missing reviewed fixture mapping evidence')
    target = s.relative_file(Path(context['profileRoot']), target_relative)
    require(target == s.safe_path(Path(context['fixtureTarget'])), 'Staged fixture path differs from plan')
    if staged:
        require(sha(raw(target, MAX_FILE)) == context['fixtureSha256'], 'Staged fixture changed')
    return context, identity, target.parent, dict(planSha256=context['planSha256'],
        deploymentSha256=sha(raw(run / 'deployment.json')), fixtureTargetRelativePath=target_relative,
        fixtureSha256=context['fixtureSha256'], careerRelativePath='Save/ME2/' + career_name,
        fixtureDescriptor=selected)


def archive(run, token, wire, verb, status):
    require(c.valid_uuid(token), 'Canonical request UUID required')
    directory = run / 'control'
    request_bytes = raw(directory / 'history' / (token + '.request'), c.MAX_FRAME)
    response_bytes = raw(directory / 'history' / (token + '.result'), c.MAX_FRAME)
    request, response = c.decode(request_bytes), c.decode(response_bytes)
    require(request['kind'] == 'request' and request['id'] == token and request['verb'] == verb
            and not request['payload'] and response['kind'] == 'result' and response['status'] == status
            and all(request[k] == response[k] for k in (*c.IDENTITY, 'id', 'sequence', 'deadline', 'verb'))
            and all(request[k] == wire[k] for k in c.IDENTITY), 'Uncorrelated or wrong archived action')
    matches, count = [], 0
    with os.scandir(directory) as scan:
        for entry in scan:
            count += 1
            require(count <= MAX_ENTRIES, 'Control directory exceeds bound')
            if re.fullmatch(r'consumed-[0-9]+\.request', entry.name):
                path = s.safe_path(Path(entry.path))
                if raw(path, c.MAX_FRAME) == request_bytes:
                    matches.append(path.name)
    require(len(matches) == 1, 'Missing or ambiguous consumed request')
    payload = e.parse(response['payload'])
    require(payload.get('source') == 'diagnostic-host' and payload.get('game') == 'LE2'
            and payload.get('ready') is False and not payload.get('error'), 'Unexpected diagnostic producer payload')
    return payload, dict(requestId=token, sequence=c.number(request['sequence']),
        requestSha256=sha(request_bytes), responseSha256=sha(response_bytes), consumedRequest=matches[0])


def observation(run, token, wire, career, selected):
    payload, provenance = archive(run, token, wire, 'observe-save-flow', 'completed')
    flow = payload.get('saveFlow', {})
    save = flow.get('save', {})
    records = save.get('records')
    require(payload.get('readOnly') is True and payload.get('capabilities', {}).get('observeSaveFlow') is True
            and type(flow.get('schemaVersion')) is int and flow['schemaVersion'] == 1 and flow.get('game') == 'LE2'
            and flow.get('available') is True
            and flow.get('saveNewEntrySpent') is False and save.get('sameOpenedSaveFlow') is True
            and save.get('newSaveObservationBound') is True and save.get('structuralNewSaveSupported') is True
            and save.get('recordsComplete') is True and isinstance(records, list) and len(records) <= 512
            and type(save.get('observedRowCount')) is int and save['observedRowCount'] == len(records),
            'A complete same-flow New Save observation is required')
    tuples, names, manual_indices = set(), set(), set()
    for row in records:
        require(isinstance(row, dict) and leaf(row['career']) == career.name and leaf(row['saveName'])
                and type(row['index']) is int and -1 <= row['index'] < 0x7fffffff
                and type(row['type']) is int and 0 <= row['type'] < 5
                and (row['type'] != 0 or row['index'] >= 0), 'Invalid or different-career descriptor')
        value = (row['career'], row['saveName'], row['index'], row['type'])
        require(value not in tuples and row['saveName'] not in names
                and (row['type'] != 0 or row['index'] not in manual_indices), 'Ambiguous duplicate descriptor')
        tuples.add(value)
        names.add(row['saveName'])
        if row['type'] == 0:
            manual_indices.add(row['index'])
    require((selected['career'], selected['saveName'], selected['index'], selected['type']) in tuples,
            'Reviewed frozen fixture descriptor is missing from complete Save list')
    for key in ('gameTick', 'gameThreadId', 'sampleMonotonicMs'):
        c.number(payload.get(key))
        provenance[key] = payload[key]
    return provenance


def capture_proof(run, relative, context, wire):
    require(isinstance(relative, str) and relative.startswith('captures/') and relative.endswith('/capture.json'),
            'Capture must be an existing run-relative capture receipt')
    path = s.relative_file(run, relative)
    capture = read(path)
    expected = dict(runDirectory=str(run), controlDirectory=str(run / 'control'), identity=wire,
                    process=context['gameProcess'], planSha256=context['planSha256'])
    require(capture.get('schemaVersion') == 1 and capture.get('source') == 'le2-window-capture'
            and capture.get('status') == 'captured' and capture.get('bindingBefore') == expected
            and capture.get('bindingAfter') == expected and capture.get('binding') == expected,
            'Capture does not bind the same owned run/process/plan')
    image = s.safe_path(path.parent / 'frame.png')
    require(capture['image']['path'] == str(image) and sha(raw(image, MAX_FILE)) == capture['image']['sha256'],
            'Capture image changed or differs from receipt')
    return dict(path=relative, sha256=sha(raw(path)), imageSha256=capture['image']['sha256'])


def publish_json(path, value):
    c.publish(path, (json.dumps(value, indent=2, allow_nan=False) + '\n').encode('utf-8'))


def before(run, case, token, capture, reviewed_by, *, bind=c.binding):
    run = s.safe_path(Path(run).absolute())
    directory, wire = bind(run)
    require(directory == run / 'control', 'Unexpected control directory')
    quiet(directory)
    output = s.safe_path(run / BASELINE)
    require(not output.exists(), 'One exclusive baseline per run; it already exists')
    context, identity, career, frozen = inputs(run, case, wire)
    observed = observation(run, token, wire, career, frozen['fixtureDescriptor'])
    proof = capture_proof(run, capture, context, wire)
    reviewed_by = reviewer(reviewed_by)
    cutoff = sequence(directory)
    require(cutoff >= observed['sequence'], 'Observation is newer than client sequence')
    files = inventory(career)
    require(bind(run) == (directory, wire) and inventory(career) == files, 'Binding or inventory changed during baseline')
    quiet(directory)
    require(sequence(directory) == cutoff, 'Control traffic changed during baseline')
    receipt = dict(schemaVersion=1, source='protected-career-file-baseline', identity=identity,
        wireIdentity=wire, frozen=frozen, sourceObservation=observed, capture=proof,
        reviewedBy=reviewed_by, reviewScope='reviewer-attests-initialized-enabled-New-Save-UI-image-contents-not-independently-verified',
        controlSequence=cutoff, createdUtc=utc(), inventory=files,
        saveCompleted=False, persistenceVerified=False)
    output.parent.mkdir(exist_ok=True)
    publish_json(output, receipt)
    return receipt


def baseline_context(run, directory, wire):
    quiet(directory)
    baseline = read(run / BASELINE)
    require(type(baseline.get('schemaVersion')) is int and baseline['schemaVersion'] == 1
            and baseline.get('source') == 'protected-career-file-baseline' and baseline['wireIdentity'] == wire,
            'Invalid or different-lease baseline')
    context, identity, career, frozen = inputs(run, baseline['identity']['caseId'], wire, staged=False)
    require(identity == baseline['identity'] and frozen == baseline['frozen'], 'Frozen baseline binding changed')
    staged_name = frozen['fixtureDescriptor']['saveName'] + '.pcsav'
    require(baseline['inventory']['files'].get(staged_name, {}).get('sha256') == frozen['fixtureSha256'],
            'Baseline inventory does not contain the unchanged staged fixture')
    require(observation(run, baseline['sourceObservation']['requestId'], wire, career, frozen['fixtureDescriptor']) == baseline['sourceObservation']
            and capture_proof(run, baseline['capture']['path'], context, wire) == baseline['capture'],
            'Baseline observation/capture changed')
    reviewer(baseline['reviewedBy'])
    cutoff = baseline['controlSequence']
    require(type(cutoff) is int and cutoff >= baseline['sourceObservation']['sequence']
            and sequence(directory) >= cutoff, 'Invalid or rolled-back baseline sequence')
    return baseline, career


def prior_actions(directory, cutoff):
    actions = []
    for path in quiet(directory):
        if path.suffix == '.request':
            frame = c.decode(raw(path, c.MAX_FRAME))
            if c.number(frame['sequence']) > cutoff and frame['verb'] == 'save-new':
                actions.append(frame['id'])
    return actions


def verify_baseline(run, *, bind=c.binding):
    """Read-only client preflight; caller may hold client.lock. Never dispatches."""
    run = s.safe_path(Path(run).absolute())
    directory, wire = bind(run)
    require(directory == run / 'control', 'Unexpected control directory')
    baseline, career = baseline_context(run, directory, wire)
    require(not prior_actions(directory, baseline['controlSequence']), 'A SaveNew request already followed the baseline')
    require(inventory(career) == baseline['inventory'], 'Career inventory changed since baseline')
    require(bind(run) == (directory, wire), 'Runtime binding changed during preflight')
    return baseline


def after(run, token, candidate, reviewed_by, *, bind=c.binding):
    run = s.safe_path(Path(run).absolute())
    directory, wire = bind(run)
    require(directory == run / 'control', 'Unexpected control directory')
    baseline, career = baseline_context(run, directory, wire)
    candidate = leaf(candidate)
    require(candidate.lower().endswith('.pcsav'), 'Observed candidate must be a pcsav leaf, never an inferred index')
    reviewed_by = reviewer(reviewed_by)
    output = s.safe_path(run / 'artifacts' / ('save-' + token + '.pcsav'))
    report = s.safe_path(run / 'artifacts' / ('save-' + token + '.json'))
    require(c.valid_uuid(token) and not output.exists() and not report.exists(), 'Invalid request or existing output')
    payload, action = archive(run, token, wire, 'save-new', 'dispatched')
    value = payload.get('menuAction', {})
    require(payload.get('readOnly') is False and payload.get('capabilities', {}).get('nativeSaveNew') is True
            and value.get('action') == 'save-new'
            and all(value.get(k) is True for k in ('entered', 'returnedNormally', 'parameterFrameIntact'))
            and value.get('saveCompleted') is False and value.get('diskInventoryVerifiedByEngine') is False
            and not value.get('error') and not value.get('persistenceVerified'), 'SaveNew dispatch is uncertain or not the bounded route')
    require(action['sequence'] > baseline['controlSequence'] and prior_actions(directory, baseline['controlSequence']) == [token],
            'Action must be the sole SaveNew after baseline')
    baseline_sha = sha(raw(run / BASELINE))
    sidecar_path = directory / 'history' / (token + '.save-baseline.json')
    sidecar = read(sidecar_path)
    require(sidecar.get('schemaVersion') == 1 and sidecar.get('requestId') == token
            and type(sidecar.get('sequence')) is int and sidecar['sequence'] == action['sequence']
            and sidecar.get('wireIdentity') == wire and sidecar.get('saveBaselineSha256') == baseline_sha,
            'Client action is not sealed to this baseline')
    current = inventory(career)
    old, now = baseline['inventory']['files'], current['files']
    require(candidate.casefold() not in {name.casefold() for name in old}, 'Candidate existed in baseline')
    changed = {name: dict(before=record, after=now.get(name)) for name, record in old.items() if now.get(name) != record}
    added = {name: record for name, record in now.items() if name not in old}
    unexpected = sorted(name for name in added if name != candidate)
    failures = []
    if changed: failures.append('previous-career-files-changed-or-removed')
    if unexpected: failures.append('unexpected-additional-files')
    if current['directories'] != baseline['inventory']['directories']: failures.append('career-directories-changed')
    if candidate not in added: failures.append('reviewed-candidate-not-present')
    data = None
    if not failures:
        data, stamp = stable_bytes(s.relative_file(career, candidate))
        require(stamp == added[candidate], 'Candidate changed after inventory')
        adapter.Save(data)  # Version 30, CRC32/BZIP2, bounded schema and full EOF.
    require(bind(run) == (directory, wire) and inventory(career) == current, 'Binding or career changed during collection')
    quiet(directory)
    require(sha(raw(run / BASELINE)) == baseline_sha, 'Baseline changed during collection')
    result = dict(schemaVersion=1, source='save-new-disk-candidate', identity=baseline['identity'], wireIdentity=wire,
        saveBaselineSha256=baseline_sha, action=action, actionSidecarSha256=sha(raw(sidecar_path)),
        candidateLeaf=candidate, reviewedBy=reviewed_by, createdUtc=utc(), previousFileChanges=changed,
        newFiles=added, directories=current['directories'], failures=failures,
        status='inventory-check-failed' if failures else 'new-candidate-archived-and-adapter-parseable',
        claims=dict(callbackCompletionProven=False, persistenceVerified=False, independentOracleVerified=False,
                    normalReloadVerified=False, visualVerified=False, vrVerified=False))
    if data is not None:
        c.publish(output, data)
        require(sha(raw(output, MAX_FILE)) == sha(data), 'Candidate archive readback differs')
        result['artifact'] = dict(path=output.relative_to(run).as_posix(), sha256=sha(data), size=len(data),
                                  parser='tools/le2-save.py LE2 version30 CRC32/BZIP2/fullEOF')
    publish_json(report, result)
    return (2 if failures else 0), result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    pre = sub.add_parser('before')
    pre.add_argument('run', type=Path)
    pre.add_argument('--case', required=True)
    pre.add_argument('--observation', required=True)
    pre.add_argument('--capture', required=True)
    pre.add_argument('--reviewed-by', required=True)
    post = sub.add_parser('after')
    post.add_argument('run', type=Path)
    post.add_argument('--request', required=True)
    post.add_argument('--candidate', required=True)
    post.add_argument('--reviewed-by', required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'before':
            result = before(args.run, args.case, args.observation, args.capture, args.reviewed_by)
            code = 0
        else:
            code, result = after(args.run, args.request, args.candidate, args.reviewed_by)
        print(json.dumps(result, indent=2, allow_nan=False))
        return code
    except (ValueError, OSError, KeyError, TypeError, UnicodeError) as error:
        print('Save evidence refused: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
