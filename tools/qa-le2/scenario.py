# SPDX-License-Identifier: MIT
"""Validate LE2 scenarios and audit artifact-backed observations; never launch a game.

`ready` means an attempt can be made, not that the scenario passed. Audit results
check artifact integrity, run identity and explicit predicates. The observation
producer and human reviewer remain responsible for truthful runtime evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import re
from pathlib import Path

GATES = ('native', 'vr', 'visual', 'headset', 'performance', 'campaign')
KINDS = ('runtime-state', 'input-trace', 'save-inspection', 'image', 'video', 'timing', 'log')
IDENTITY = ('runId', 'caseId', 'scenarioSha256', 'baseSaveSha256', 'recipeSha256',
            'preparedSaveSha256', 'candidateCommit', 'gameVersion')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def _pairs(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise ValueError(f'duplicate JSON key: {key}')
        out[key] = value
    return out


def read_json(path):
    def nonfinite(value):
        raise ValueError(f'nonfinite JSON number: {value}')
    def finite_float(value):
        result = float(value)
        if not math.isfinite(result):
            nonfinite(value)
        return result
    return json.loads(Path(path).read_text(encoding='utf-8-sig'), object_pairs_hook=_pairs,
                      parse_constant=nonfinite, parse_float=finite_float)


def _text(value):
    return isinstance(value, str) and bool(value.strip())


def _hash(value):
    return isinstance(value, str) and re.fullmatch(r'[a-fA-F0-9]{64}', value) is not None


def _number(value):
    return type(value) is int or (isinstance(value, float) and math.isfinite(value))


def _strings(value, nonempty=False):
    return isinstance(value, list) and (bool(value) or not nonempty) and all(_text(x) for x in value)


def _scalar(value):
    return value is None or isinstance(value, (str, bool)) or _number(value)


def _reference(value):
    return (isinstance(value, dict) and set(value) == {'artifact', 'field'}
            and _text(value['artifact']) and _text(value['field']))


def validate(s, for_run=False):
    errors = []
    def need(ok, message):
        if not ok:
            errors.append(message)
    if not isinstance(s, dict):
        return ['scenario must be an object']
    need(type(s.get('schemaVersion')) is int and s['schemaVersion'] == 1, 'schemaVersion must be 1')
    need(s.get('spdxLicenseIdentifier') == 'MIT', 'spdxLicenseIdentifier must be MIT')
    need(s.get('game') == 'LE2', 'game must be LE2')
    need(isinstance(s.get('id'), str) and re.fullmatch(r'[a-z0-9]+(?:-[a-z0-9]+)*', s['id']), 'invalid id')
    need(_text(s.get('label')), 'label required')
    need(s.get('status') in ('draft', 'ready'), 'status must be draft or ready, never a pass claim')
    need(_strings(s.get('workPackages'), True), 'workPackages required')
    need(_strings(s.get('prerequisites'), True), 'prerequisites required')
    need(_strings(s.get('acquisitionTasks')), 'acquisitionTasks must be a string list')
    need(_strings(s.get('variants'), True), 'variants required')
    ready = s.get('status') == 'ready'
    need(not for_run or ready, 'draft scenario cannot execute')
    fixture = s.get('fixture')
    need(isinstance(fixture, dict), 'fixture object required')
    if isinstance(fixture, dict):
        need(_text(fixture.get('id')), 'fixture.id required')
        need((not ready and fixture.get('baseSaveSha256') is None) or _hash(fixture.get('baseSaveSha256')),
             'fixture.baseSaveSha256 must pin a base save when ready')
    recipe = s.get('recipe')
    need((not ready and recipe is None) or (isinstance(recipe, dict) and _text(recipe.get('path'))
         and _hash(recipe.get('sha256'))), 'recipe path/hash required when ready')
    start = s.get('expectedStart')
    need(isinstance(start, dict), 'expectedStart required')
    if isinstance(start, dict):
        need((not ready and start.get('map') is None) or _text(start.get('map')), 'expectedStart.map required when ready')
        checks = start.get('checks')
        need(isinstance(checks, list), 'expectedStart.checks must be a list')
        if isinstance(checks, list):
            need(not ready or bool(checks), 'ready scenario needs explicit starting-state checks')
            for check in checks:
                need(isinstance(check, dict) and _text(check.get('field')) and check.get('op') in ('eq', 'ge', 'le')
                     and 'value' in check and _scalar(check['value']), 'invalid starting-state check')
    need(_strings(s.get('loadProcedure'), True), 'loadProcedure required')
    need(_strings(s.get('resetProcedure'), True), 'resetProcedure required')
    actions = s.get('actions')
    need(isinstance(actions, list) and bool(actions), 'actions required')
    if isinstance(actions, list):
        for action in actions:
            need(isinstance(action, dict) and _text(action.get('id')) and _text(action.get('description'))
                 and action.get('inputPath') in ('native', 'vr', 'either'), 'invalid action')
    slots = s.get('artifacts')
    need(isinstance(slots, dict) and bool(slots), 'artifact slots required')
    if not isinstance(slots, dict):
        slots = {}
    for key, kind in slots.items():
        need(_text(key) and kind in KINDS, 'invalid artifact slot/kind')
    need(slots.get('start') == 'runtime-state', 'start runtime-state artifact required')
    assertions = s.get('assertions')
    need(isinstance(assertions, list) and bool(assertions), 'assertions required')
    ids = set()
    if isinstance(assertions, list):
        for assertion in assertions:
            if not isinstance(assertion, dict):
                errors.append('assertion must be an object'); continue
            aid = assertion.get('id')
            need(_text(aid) and aid not in ids, 'assertion ids must be nonempty and unique')
            if isinstance(aid, str):
                ids.add(aid)
            need(assertion.get('gate') in GATES and _text(assertion.get('description')), f'{aid}: gate/description required')
            variants = assertion.get('variants', s.get('variants'))
            need(_strings(variants, True) and isinstance(s.get('variants'), list)
                 and all(x in s['variants'] for x in variants),
                 f'{aid}: assertion variants must belong to scenario')
            required = assertion.get('requires')
            need(_strings(required, True) and all(x in slots for x in required), f'{aid}: unknown/missing artifact slot')
            check = assertion.get('check')
            need(isinstance(check, dict), f'{aid}: check required')
            if not isinstance(check, dict):
                continue
            kind = check.get('kind')
            need(kind in ('compare', 'delta', 'sum-equal', 'review'), f'{aid}: invalid check kind')
            refs = []
            if kind == 'compare':
                refs = [check.get('left')]
                need(check.get('op') in ('eq', 'ne', 'gt', 'ge', 'lt', 'le'), f'{aid}: invalid comparison')
                right = check.get('right')
                if isinstance(right, dict):
                    refs.append(right)
                else:
                    need('right' in check and _scalar(right), f'{aid}: invalid comparison target')
            elif kind == 'delta':
                refs = [check.get('before'), check.get('after')]
                need(_number(check.get('expected')), f'{aid}: finite expected delta required')
            elif kind == 'sum-equal':
                left, right = check.get('left'), check.get('right')
                need(isinstance(left, list) and bool(left) and isinstance(right, list) and bool(right), f'{aid}: nonempty sum terms required')
                refs = (left if isinstance(left, list) else []) + (right if isinstance(right, list) else [])
            for ref in refs:
                need(_reference(ref) and ref.get('artifact') in slots, f'{aid}: invalid state reference')
                if _reference(ref):
                    need(isinstance(required, list) and ref['artifact'] in required, f'{aid}: reference must be required')
                    need(slots.get(ref['artifact']) in ('runtime-state', 'save-inspection', 'input-trace', 'timing'), f'{aid}: reference requires structured artifact')
            if assertion.get('gate') in ('vr', 'visual', 'headset'):
                need(isinstance(required, list) and any(slots.get(x) == 'input-trace' for x in required), f'{aid}: VR evidence needs input trace')
    return errors


def load(path, for_run=False, recipe_path=None):
    scenario = read_json(path)
    errors = validate(scenario, for_run)
    if errors:
        raise ValueError('; '.join(errors))
    if for_run:
        recipe = Path(recipe_path) if recipe_path else Path(path).parent / scenario['recipe']['path']
        if sha(recipe) != scenario['recipe']['sha256'].lower():
            raise ValueError('recipe hash mismatch')
        body = read_json(recipe)
        if (not isinstance(body, dict) or body.get('schema_version') != 1
                or not _text(body.get('id')) or not isinstance(body.get('expect'), dict)
                or not isinstance(body.get('set'), dict)):
            raise ValueError('invalid save preparation recipe')
        if body.get('expected_input_sha256', '').lower() != scenario['fixture']['baseSaveSha256'].lower():
            raise ValueError('recipe input hash differs from scenario base save')
    return scenario


def field(data, path):
    for part in path.split('.'):
        if isinstance(data, list) and part.isdigit():
            data = data[int(part)]
        elif isinstance(data, dict):
            data = data[part]
        else:
            raise ValueError(f'not a traversable field: {path}')
    return data


def compare(left, op, right):
    if op in ('eq', 'ne'):
        equal = type(left) is type(right) and left == right
        if _number(left) and _number(right):
            equal = left == right
        return equal if op == 'eq' else not equal
    if not _number(left) or not _number(right):
        raise ValueError('ordered comparison requires finite numbers')
    return {'gt': left > right, 'ge': left >= right, 'lt': left < right, 'le': left <= right}[op]


def check_start(scenario, runtime_state):
    """Accept normalized observed state, never a save inspection as a live observation."""
    failures = []
    if not isinstance(runtime_state, dict) or not isinstance(runtime_state.get('state'), dict):
        return ['starting state must be an object with observed state']
    if runtime_state.get('source') != 'runtime-observation':
        failures.append('starting state must come from runtime observation')
    state = runtime_state.get('state', {})
    if state.get('map') != scenario['expectedStart']['map']:
        failures.append('starting map differs')
    for check in scenario['expectedStart']['checks']:
        try:
            if not compare(field(state, check['field']), check['op'], check['value']):
                failures.append(f"starting check failed: {check['field']}")
        except (ValueError, KeyError, IndexError, TypeError):
            failures.append(f"missing/invalid starting field: {check['field']}")
    return failures


def bind_session(identity, run_path):
    """Bind observation identity to the protected run, not self-reported hashes."""
    if run_path is None:
        return {'status': 'not-supplied', 'restorationVerified': False}
    spec = importlib.util.spec_from_file_location('le2_audit_session', Path(__file__).with_name('session.py'))
    session = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(session)
    run, context = session.load_context(Path(run_path).resolve(), require_lock=False)
    for name, context_key in (('runId', 'runId'), ('scenarioSha256', 'scenarioSha256'),
                              ('preparedSaveSha256', 'fixtureSha256'),
                              ('gameExeSha256', 'exeSha256'), ('recipeSha256', 'recipeSha256')):
        if identity.get(name) != context.get(context_key):
            raise ValueError(f'evidence {name} differs from protected session')
    for name, context_key in (('scenario.json', 'scenarioSha256'),
                              ('fixture.pcsav', 'fixtureSha256'),
                              ('recipe.json', 'recipeSha256'),
                              ('preparation-receipt.json', 'preparationReceiptSha256')):
        if sha(run / name) != context.get(context_key):
            raise ValueError(f'frozen session artifact changed: {name}')
    receipt = read_json(run / 'preparation-receipt.json')
    if (receipt.get('input_sha256') != identity['baseSaveSha256']
            or receipt.get('output_sha256') != identity['preparedSaveSha256']
            or receipt.get('recipe_sha256') != identity['recipeSha256']):
        raise ValueError('evidence differs from preparation receipt')
    restored = context.get('phase') == 'restored' and context.get('restorationVerified') is True
    return {'status': 'bound', 'contextSha256': sha(run / 'context.json'),
            'processBound': isinstance(context.get('gameProcess'), dict),
            'restorationVerified': restored}


def audit(scenario_path, evidence_path, recipe_path=None, session_path=None):
    """Audit all claims; pending/failed gates do not become successful by omission."""
    s = load(scenario_path, for_run=True, recipe_path=recipe_path)
    e = read_json(evidence_path)
    if not isinstance(e, dict) or e.get('schemaVersion') != 1:
        raise ValueError('unsupported evidence document')
    identity = e.get('identity', {})
    if not isinstance(identity, dict) or not all(_text(identity.get(k)) for k in IDENTITY):
        raise ValueError('complete evidence identity required')
    if identity['scenarioSha256'].lower() != sha(scenario_path):
        raise ValueError('evidence is for a different scenario revision')
    if identity['baseSaveSha256'].lower() != s['fixture']['baseSaveSha256'].lower():
        raise ValueError('evidence base save differs from fixture binding')
    if identity['recipeSha256'].lower() != s['recipe']['sha256'].lower():
        raise ValueError('evidence recipe differs from fixture binding')
    if not _hash(identity['preparedSaveSha256']) or not _hash(identity.get('gameExeSha256')):
        raise ValueError('prepared save and game executable hashes required')
    if e.get('caseId') not in s['variants'] or identity['caseId'] != e.get('caseId'):
        raise ValueError('caseId must identify one declared scenario variant')
    if e.get('mode') not in ('native-baseline', 'vr') or e.get('headset') not in ('none', 'synthetic', 'physical'):
        raise ValueError('invalid execution mode/headset')
    if e['mode'] == 'vr' and not _hash(identity.get('loadedModSha256')):
        raise ValueError('VR evidence must identify the loaded mod')
    root = Path(evidence_path).resolve().parent
    artifacts, values = e.get('artifacts'), {}
    if not isinstance(artifacts, dict):
        raise ValueError('artifact manifest required')
    problems = {}
    for name, record in artifacts.items():
        try:
            if name not in s['artifacts'] or not isinstance(record, dict):
                raise ValueError('unknown artifact slot')
            if record.get('kind') != s['artifacts'][name] or not _hash(record.get('sha256')):
                raise ValueError('artifact kind/hash mismatch')
            path = (root / record['path']).resolve()
            if not path.is_relative_to(root) or path == root:
                raise ValueError('artifact path escapes evidence directory')
            if sha(path) != record['sha256'].lower():
                raise ValueError('artifact bytes changed')
            if record['kind'] in ('runtime-state', 'save-inspection', 'input-trace', 'timing'):
                value = read_json(path)
                if not isinstance(value, dict) or value.get('identity') != identity:
                    raise ValueError('artifact belongs to another run/build/fixture')
                if record['kind'] == 'runtime-state' and value.get('source') != 'runtime-observation':
                    raise ValueError('native state must be observed in the game')
                if record['kind'] == 'input-trace' and value.get('inputPath') not in ('native', 'vr-controller'):
                    raise ValueError('input trace is not a player input path')
                if record['kind'] == 'input-trace' and (not isinstance(value.get('events'), list)
                        or not value['events'] or not all(isinstance(x, dict) for x in value['events'])):
                    raise ValueError('input trace has no events')
                values[name] = value
        except (OSError, KeyError, ValueError, TypeError) as ex:
            problems[name] = str(ex)
    reviews = e.get('reviews', {})
    if not isinstance(reviews, dict):
        raise ValueError('reviews must be an object')
    results = []
    start_problems = (check_start(s, values['start']) if 'start' in values and 'start' not in problems
                      else ['missing/invalid starting runtime observation'])
    def observed(ref):
        return field(values[ref['artifact']], ref['field'])
    for assertion in s['assertions']:
        if e['caseId'] not in assertion.get('variants', s['variants']):
            continue
        aid, gate = assertion['id'], assertion['gate']
        reason, status = '', 'supported'
        missing = [x for x in assertion['requires'] if x not in artifacts or x in problems]
        if start_problems:
            status, reason = 'pending', '; '.join(start_problems)
        elif missing:
            status, reason = 'pending', 'missing/invalid artifacts: ' + ', '.join(missing)
        elif gate in ('vr', 'visual', 'headset') and e['mode'] != 'vr':
            status, reason = 'pending', 'native baseline cannot establish VR behavior'
        elif gate == 'headset' and e['headset'] != 'physical':
            status, reason = 'pending', 'physical headset required'
        else:
            try:
                if gate in ('vr', 'visual', 'headset'):
                    traces = [values[x] for x in assertion['requires'] if s['artifacts'][x] == 'input-trace']
                    if not traces or any(x['inputPath'] != 'vr-controller' for x in traces):
                        raise ValueError('actual VR controller input required')
                check = assertion['check']
                if check['kind'] == 'review':
                    review = reviews.get(aid, {})
                    expected_hashes = {x: artifacts[x]['sha256'].lower() for x in assertion['requires']}
                    if (not isinstance(review, dict) or not _text(review.get('reviewer'))
                            or not _text(review.get('observedAtUtc')) or not _text(review.get('notes'))
                            or review.get('artifactHashes') != expected_hashes
                            or review.get('verdict') not in ('supported', 'failed')):
                        status, reason = 'pending', 'explicit attributed review of exact artifacts required'
                    elif review['verdict'] == 'failed':
                        status, reason = 'failed', review['notes']
                else:
                    if check['kind'] == 'compare':
                        right = observed(check['right']) if isinstance(check['right'], dict) else check['right']
                        passed = compare(observed(check['left']), check['op'], right)
                    elif check['kind'] == 'delta':
                        before, after = observed(check['before']), observed(check['after'])
                        if not _number(before) or not _number(after):
                            raise ValueError('delta requires finite observed numbers')
                        passed = after - before == check['expected']
                    else:
                        left, right = [observed(x) for x in check['left']], [observed(x) for x in check['right']]
                        if not all(_number(x) for x in left + right):
                            raise ValueError('sum requires finite observed numbers')
                        passed = sum(left) == sum(right)
                    if not passed:
                        status, reason = 'failed', 'observed predicate is false'
            except (KeyError, IndexError, ValueError, TypeError) as ex:
                status, reason = 'failed', str(ex)
        results.append({'id': aid, 'gate': gate, 'status': status, 'reason': reason})
    gates = {}
    for gate in GATES:
        rows = [x for x in results if x['gate'] == gate]
        gates[gate] = ('not-covered' if not rows else 'failed' if any(x['status'] == 'failed' for x in rows)
                       else 'pending' if any(x['status'] == 'pending' for x in rows) else 'supported')
    session_binding = bind_session(identity, session_path)
    return {'scenario': s['id'], 'caseId': e['caseId'], 'runId': identity['runId'],
            'sessionBinding': session_binding,
            'startProblems': start_problems, 'artifactProblems': problems,
            'assertions': results, 'gates': gates, 'fullPortAccepted': False,
            'scope': 'Artifact integrity and declared observations; not independent proof of producer truth or campaign coverage.'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    v = sub.add_parser('validate'); v.add_argument('scenario'); v.add_argument('--for-run', action='store_true'); v.add_argument('--recipe')
    a = sub.add_parser('audit'); a.add_argument('scenario'); a.add_argument('evidence'); a.add_argument('--recipe'); a.add_argument('--session', required=True)
    c = sub.add_parser('check-start'); c.add_argument('scenario'); c.add_argument('state'); c.add_argument('--recipe')
    args = p.parse_args()
    try:
        if args.command == 'validate':
            s = load(args.scenario, args.for_run, args.recipe)
            result = {'valid': True, 'id': s['id'], 'status': s['status'], 'sha256': sha(args.scenario),
                      'readyToAttempt': s['status'] == 'ready', 'acceptance': 'not-evaluated'}
        elif args.command == 'check-start':
            s = load(args.scenario, True, args.recipe)
            failures = check_start(s, read_json(args.state))
            result = {'startingStateMatches': not failures, 'failures': failures,
                      'renderedSceneVerified': False, 'acceptance': 'not-evaluated'}
        else:
            result = audit(args.scenario, args.evidence, args.recipe, args.session)
        print(json.dumps(result, indent=2))
        if args.command == 'check-start':
            return 2 if failures else 0
        if args.command == 'validate':
            return 0
        binding = result['sessionBinding']
        return 0 if (all(x['status'] == 'supported' for x in result['assertions'])
                     and binding.get('status') == 'bound' and binding.get('processBound') is True
                     and binding.get('restorationVerified') is True) else 2
    except (OSError, ValueError, KeyError, TypeError, IndexError) as ex:
        print(json.dumps({'valid': False, 'error': str(ex)}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
