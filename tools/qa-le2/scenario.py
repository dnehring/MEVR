# SPDX-License-Identifier: MIT
"""Validate LE2 scenarios and audit artifact-backed observations; never launch a game.

`ready` means an attempt can be made, not that the scenario passed. Audit results
check artifact integrity, run identity and explicit predicates. The observation
producer and human reviewer remain responsible for truthful runtime evidence.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import importlib.util
import json
import math
import re
from pathlib import Path

GATES = ('native', 'vr', 'visual', 'headset', 'performance', 'campaign')
KINDS = ('runtime-state', 'input-trace', 'save-inspection', 'image', 'video', 'timing', 'log')
RELOAD_CASES = ('empty', 'partial', 'insufficient-reserve', 'exact-reserve',
                'surplus-reserve', 'no-reserve', 'full-magazine')
PROBE_CASES = ('one-probe', 'zero-probes', 'scan-hold', 'anomaly')
SAVED_RESOURCES = {key: key for key in ('credits', 'medigel', 'eezo', 'iridium', 'palladium', 'platinum', 'probes')}
SAVED_RESOURCES['fuel'] = 'current_fuel'
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
    slots = s.get('artifacts')
    need(isinstance(slots, dict) and bool(slots), 'artifact slots required')
    if not isinstance(slots, dict):
        slots = {}
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
                if isinstance(check, dict):
                    slot = check.get('artifact', 'start')
                    need(_text(slot) and slots.get(slot) == 'runtime-state',
                         'starting-state check must reference a runtime-state artifact')
    need(_strings(s.get('loadProcedure'), True), 'loadProcedure required')
    need(_strings(s.get('resetProcedure'), True), 'resetProcedure required')
    actions = s.get('actions')
    need(isinstance(actions, list) and bool(actions), 'actions required')
    if isinstance(actions, list):
        for action in actions:
            need(isinstance(action, dict) and _text(action.get('id')) and _text(action.get('description'))
                 and action.get('inputPath') in ('native', 'vr', 'either'), 'invalid action')
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
            need(kind in ('compare', 'delta', 'sum-equal', 'finite-reload-transfer', 'orbital-probe-samples',
                          'dialogue-prompt-sample', 'saved-reload-resources', 'review'),
                 f'{aid}: invalid check kind')
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
            elif kind in ('finite-reload-transfer', 'orbital-probe-samples'):
                refs = [check.get('before'), check.get('after')]
                cases = RELOAD_CASES if kind == 'finite-reload-transfer' else PROBE_CASES
                need(isinstance(variants, list) and all(x in cases for x in variants),
                     f'{aid}: check requires a supported variant')
                need(all(_reference(ref) and slots.get(ref['artifact']) == 'runtime-state' for ref in refs),
                     f'{aid}: check requires runtime observations')
            elif kind == 'dialogue-prompt-sample':
                refs = [check.get('history')]
                need(_reference(refs[0]) and slots.get(refs[0]['artifact']) == 'runtime-state',
                     f'{aid}: dialogue prompt requires a runtime history artifact')
                prompts = check.get('prompts')
                polarity = check.get('polarityByVariant')
                need(_text(check.get('conversation')) and valid_dialogue_prompts(prompts)
                     and isinstance(polarity, dict) and _strings(variants, True)
                     and all(key in variants and any(row['polarity'] == value for row in prompts)
                             for key, value in polarity.items()),
                     f'{aid}: expected conversation and valid prompt identities are required')
            elif kind == 'saved-reload-resources':
                need(isinstance(required, list) and 'after-reload' in required
                     and slots.get('after-reload') == 'runtime-state',
                     f'{aid}: saved resource comparison requires after-reload runtime evidence')
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


def valid_dialogue_prompts(entries):
    return (isinstance(entries, list) and bool(entries)
            and all(isinstance(row, dict) and set(row) == {'polarity', 'entryExportId', 'replyExportId'}
                    and row['polarity'] in ('paragon', 'renegade')
                    and all(type(row[key]) is int and row[key] > 0 for key in ('entryExportId', 'replyExportId'))
                    for row in entries))


def check_dialogue_prompt_sample(artifact, history, check, variant):
    provenance = artifact['provenance']
    if (provenance['observationVerb'] not in ('observe-dialogue-history', 'stop-dialogue-history')
            or provenance['historyOrigin']['available'] is not True):
        raise ValueError('dialogue prompt requires an export with archived recorder-start provenance')
    if (history['started'] is not True or history['active'] is not False
            or history['reason'] not in ('stopped', 'expired') or history['dictionaryOmitted'] is not False):
        raise ValueError('dialogue prompt requires a finished capture with its identity dictionary')
    epochs = {epoch['epoch'] for epoch in history['epochs']
              if epoch['conversation']['name'] == check['conversation']}
    required_polarity = check['polarityByVariant'].get(variant)
    prompts = [row for row in check['prompts'] if required_polarity in (None, row['polarity'])]
    for row in history['rows']:
        if row['status'] != 'stable-sample' or row['epoch'] not in epochs:
            continue
        for prompt in prompts:
            polarity = prompt['polarity']
            if (row['currentEntry']['exportId'] == prompt['entryExportId']
                    and row['interruptReply']['exportId'] == prompt['replyExportId']
                    and row['interrupt']['type'] == (1 if polarity == 'paragon' else 0)
                    and row.get('gui', {}).get(polarity + 'Displayed') is True):
                return
    raise ValueError('expected conversation and authored interrupt prompt were not sampled together')


def check_reload_transfer(before, after, variant):
    """Check sampled ammo accounting for one isolated ordinary native reload."""
    identities = [sample['identity'] for sample in (before, after)]
    for identity in identities:
        if (not isinstance(identity, dict) or not _text(identity.get('class')) or not _text(identity.get('name'))
                or type(identity.get('index')) is not int or identity['index'] < 0
                or type(identity.get('nameNumber')) is not int or identity.get('heavyWeaponClass') is not False):
            raise ValueError('ordinary weapon identity required at both reload samples')
    if identities[0] != identities[1]:
        raise ValueError('selected weapon identity differs across reload samples')

    def counts(sample):
        if sample.get('infiniteAmmo') is not False:
            raise ValueError('explicit finite weapon ammo flag required')
        values = tuple(sample[key] for key in ('magazineCapacity', 'magazineUsed', 'loaded', 'effectiveReserve'))
        if not all(_number(value) and 0 <= value <= 2**24 and int(value) == value for value in values):
            raise ValueError('reload requires integral counts in the exact float32 range')
        capacity, used, loaded, reserve = map(int, values)
        if capacity == 0 or used + loaded != capacity:
            raise ValueError('loaded and used counts do not reconcile to magazine capacity')
        return capacity, used, loaded, reserve

    capacity, used, loaded, reserve = counts(before)
    if not {
        'empty': loaded == 0 and reserve > 0,
        'partial': 0 < loaded < capacity and reserve > 0,
        'insufficient-reserve': 0 < reserve < used,
        'exact-reserve': reserve == used and used > 0,
        'surplus-reserve': reserve > used > 0,
        'no-reserve': reserve == 0 and used > 0,
        'full-magazine': used == 0 and reserve > 0,
    }[variant]:
        raise ValueError('starting counts do not establish the selected reload variant')
    transfer = min(used, reserve)
    if counts(after) != (capacity, used - transfer, loaded + transfer, reserve - transfer):
        raise ValueError(f'observed ammo does not match exact finite reload transfer of {transfer}')


def check_orbital_probe_samples(before, after, variant):
    """Compare endpoints around one action on a sampled orbital target."""
    first, last = before['orbital'], after['orbital']
    for sample in (first, last):
        if type(sample['currentMode']) is not int or sample['currentMode'] != 10:
            raise ValueError('probe samples require orbital mode')
        if type(sample['activeWorld']) is not int or sample['activeWorld'] < 0:
            raise ValueError('probe samples require a valid active world')
        planet = sample['planet']
        identity = planet['identity']
        if (not _text(identity['class']) or type(identity['index']) is not int or identity['index'] < 0
                or type(planet['tableId']) is not int):
            raise ValueError('probe samples require a valid planet identity')
        for key in ('probeActive', 'impactActive', 'rescaling', 'exiting'):
            if sample[key] is not False:
                raise ValueError(f'probe endpoints require inactive {key}')
    if (first['activeWorld'] != last['activeWorld']
            or first['planet']['identity'] != last['planet']['identity']
            or first['planet']['tableId'] != last['planet']['tableId']):
        raise ValueError('probe endpoint planet identity differs')
    scanning = variant != 'scan-hold'
    if first['scanning'] is not scanning or (variant == 'scan-hold' and last['scanning'] is not True):
        raise ValueError('scan state does not establish the selected probe variant')
    start, end = before['resources']['probes'], after['resources']['probes']
    if any(type(value) is not int or value < 0 for value in (start, end)):
        raise ValueError('probe balances must be nonnegative integers')
    if ((variant == 'one-probe' and start != 1) or (variant == 'zero-probes' and start != 0)
            or (variant in ('scan-hold', 'anomaly') and start == 0)):
        raise ValueError('starting probe balance does not establish the selected variant')
    cost = int(variant in ('one-probe', 'anomaly'))
    if start - end != cost:
        raise ValueError(f'observed probe cost differs from expected {cost}')


def check_start(scenario, runtime_state, observations=None):
    """Check independent starting observations without merging samples."""
    failures = []
    if not isinstance(runtime_state, dict) or not isinstance(runtime_state.get('state'), dict):
        return ['starting state must be an object with observed state']
    if runtime_state.get('source') != 'runtime-observation':
        failures.append('starting state must come from runtime observation')
    state = runtime_state.get('state', {})
    if state.get('map') != scenario['expectedStart']['map']:
        failures.append('starting map differs')
    for check in scenario['expectedStart']['checks']:
        slot = check.get('artifact', 'start')
        observed = state
        if slot != 'start':
            sample = (observations or {}).get(slot)
            if (not isinstance(sample, dict) or sample.get('source') != 'runtime-observation'
                    or not isinstance(sample.get('state'), dict)
                    or not isinstance(runtime_state.get('identity'), dict)
                    or sample.get('identity') != runtime_state['identity']):
                failures.append(f'missing/invalid starting observation: {slot}')
                continue
            observed = sample['state']
        try:
            if not compare(field(observed, check['field']), check['op'], check['value']):
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


def bind_reload(primary, primary_path, reload_path, link, observation):
    """Bind a different run's observation to an exact archived outcome save."""
    if primary_path is None or not isinstance(link, dict) or not _hash(link.get('saveReportSha256')):
        raise ValueError('reload link requires a source session and pinned save report')
    source = Path(primary_path).resolve()
    target = Path(reload_path).resolve()
    name = link.get('saveReport')
    if not isinstance(name, str) or not re.fullmatch(r'artifacts/save-[0-9a-f-]{36}\.json', name):
        raise ValueError('reload link requires a run-relative New Save report')
    report_path = source / name
    if report_path.resolve() != report_path or sha(report_path) != link['saveReportSha256']:
        raise ValueError('reload link source report path or hash differs')
    report = read_json(report_path)
    if (report.get('source') != 'save-new-disk-candidate' or report.get('identity') != primary
            or report.get('status') != 'new-candidate-archived-and-adapter-parseable'
            or report.get('failures') != []):
        raise ValueError('reload link requires a successful New Save report from the primary run')
    saved = report['artifact']
    save_path = report_path.with_suffix('.pcsav')
    save_sha = saved['sha256']
    if (saved['path'] != save_path.relative_to(source).as_posix() or save_path.resolve() != save_path
            or not _hash(save_sha) or sha(save_path) != save_sha or save_path.stat().st_size != saved['size']):
        raise ValueError('reload link archived outcome identity differs')
    spec = importlib.util.spec_from_file_location('le2_reload_save', Path(__file__).parents[1] / 'le2-save.py')
    adapter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(adapter)
    save = adapter.Save.load(save_path)
    if hashlib.sha256(save.raw).hexdigest() != save_sha:
        raise ValueError('reload link save changed while parsing')
    identity = observation['identity']
    if (identity.get('runId') == primary['runId'] or identity.get('caseId') != primary['caseId']
            or identity.get('gameExeSha256') != primary['gameExeSha256']
            or identity.get('candidateCommit') != primary.get('candidateCommit')
            or identity.get('loadedModSha256') != primary.get('loadedModSha256')
            or identity.get('baseSaveSha256') != save_sha or identity.get('preparedSaveSha256') != save_sha):
        raise ValueError('reload run must stage the exact saved outcome in a separate matching case and build')
    binding = bind_session(identity, target)
    if not binding['processBound'] or not binding['restorationVerified']:
        raise ValueError('reload run requires process binding and verified restoration')
    source_process = read_json(source / 'context.json')['gameProcess']
    target_process = read_json(target / 'context.json')['gameProcess']
    if target_process['name'] != 'MassEffect2' or identity.get('scenarioSha256') != sha(target / 'scenario.json'):
        raise ValueError('reload run must bind the ME2 process and frozen scenario')
    reload_scenario = read_json(target / 'scenario.json')
    if identity['caseId'] not in reload_scenario['variants']:
        raise ValueError('reload case is missing from the frozen reload scenario')
    if datetime.fromisoformat(target_process['started']) <= datetime.fromisoformat(source_process['started']):
        raise ValueError('reload run must use a newer game process')
    wire = observation['provenance']['wireIdentity']
    descriptor = read_json(target / 'control' / 'identity.json')
    if (wire.get('run') != identity['runId'] or wire.get('exe') != identity['gameExeSha256']
            or wire.get('mod') != identity['loadedModSha256'] or wire.get('pid') != str(target_process['pid'])
            or wire != descriptor.get('identity')):
        raise ValueError('reload observation wire identity differs')
    resources = {key: save.data['player'][field] for key, field in SAVED_RESOURCES.items()}
    return dict(binding, runId=identity['runId'], savedOutcomeSha256=save_sha,
                saveReportSha256=link['saveReportSha256'], savedResources=resources, normalLoadVerified=False)


def check_saved_reload_resources(saved, observed):
    for key in SAVED_RESOURCES:
        value = observed[key]
        valid = _number(value) if key == 'fuel' else type(value) is int
        if not valid or value < 0 or not compare(saved[key], 'eq', value):
            raise ValueError(f'reloaded {key} differs from saved outcome')


def audit(scenario_path, evidence_path, recipe_path=None, session_path=None, reload_session=None):
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
    if 'reloadLink' in e and reload_session is None:
        raise ValueError('reloadLink requires --reload-session')
    if reload_session is not None and ('after-reload' not in artifacts or 'reloadLink' not in e
                                       or s['artifacts'].get('after-reload') != 'runtime-state'):
        raise ValueError('--reload-session requires after-reload runtime evidence and a reloadLink')
    problems = {}
    reload_binding = None
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
                if not isinstance(value, dict):
                    raise ValueError('structured artifact must be an object')
                if value.get('identity') != identity and (name != 'after-reload' or reload_session is None):
                    raise ValueError('artifact belongs to another run/build/fixture')
                if record['kind'] == 'runtime-state' and value.get('source') != 'runtime-observation':
                    raise ValueError('native state must be observed in the game')
                if record['kind'] == 'input-trace' and value.get('inputPath') not in ('native', 'vr-controller'):
                    raise ValueError('input trace is not a player input path')
                if record['kind'] == 'input-trace' and (not isinstance(value.get('events'), list)
                        or not value['events'] or not all(isinstance(x, dict) for x in value['events'])):
                    raise ValueError('input trace has no events')
                if name == 'after-reload' and reload_session is not None:
                    reload_binding = bind_reload(identity, session_path, reload_session, e.get('reloadLink'), value)
                values[name] = value
        except (OSError, KeyError, ValueError, TypeError) as ex:
            problems[name] = str(ex)
    reviews = e.get('reviews', {})
    if not isinstance(reviews, dict):
        raise ValueError('reviews must be an object')
    results = []
    start_problems = (check_start(s, values['start'], values) if 'start' in values and 'start' not in problems
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
        elif assertion['check']['kind'] == 'saved-reload-resources' and reload_binding is None:
            status, reason = 'pending', 'fresh reload linked to the archived outcome save required'
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
                    elif check['kind'] == 'finite-reload-transfer':
                        check_reload_transfer(observed(check['before']), observed(check['after']), e['caseId'])
                        passed = True
                    elif check['kind'] == 'orbital-probe-samples':
                        check_orbital_probe_samples(observed(check['before']), observed(check['after']), e['caseId'])
                        passed = True
                    elif check['kind'] == 'dialogue-prompt-sample':
                        ref = check['history']
                        check_dialogue_prompt_sample(values[ref['artifact']], observed(ref), check, e['caseId'])
                        passed = True
                    elif check['kind'] == 'saved-reload-resources':
                        check_saved_reload_resources(reload_binding['savedResources'], values['after-reload']['state']['resources'])
                        passed = True
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
            'sessionBinding': session_binding, 'reloadSessionBinding': reload_binding,
            'startProblems': start_problems, 'artifactProblems': problems,
            'assertions': results, 'gates': gates, 'fullPortAccepted': False,
            'scope': 'Artifact integrity and declared observations; not independent proof of producer truth or campaign coverage.'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    v = sub.add_parser('validate'); v.add_argument('scenario'); v.add_argument('--for-run', action='store_true'); v.add_argument('--recipe')
    a = sub.add_parser('audit'); a.add_argument('scenario'); a.add_argument('evidence'); a.add_argument('--recipe'); a.add_argument('--session', required=True)
    a.add_argument('--reload-session', help='Fresh protected run for linked after-reload evidence')
    c = sub.add_parser('check-start'); c.add_argument('scenario'); c.add_argument('state'); c.add_argument('--recipe')
    c.add_argument('--observation', action='append', nargs=2, default=[], metavar=('SLOT', 'STATE_JSON'),
                   help='Add a separate runtime-state sample for a starting-check artifact slot')
    args = p.parse_args()
    try:
        if args.command == 'validate':
            s = load(args.scenario, args.for_run, args.recipe)
            result = {'valid': True, 'id': s['id'], 'status': s['status'], 'sha256': sha(args.scenario),
                      'readyToAttempt': s['status'] == 'ready', 'acceptance': 'not-evaluated'}
        elif args.command == 'check-start':
            s = load(args.scenario, True, args.recipe)
            observations = {}
            for slot, path in args.observation:
                if slot == 'start' or slot in observations or s['artifacts'].get(slot) != 'runtime-state':
                    raise ValueError(f'invalid or repeated supplemental starting-state slot: {slot}')
                observations[slot] = read_json(path)
            failures = check_start(s, read_json(args.state), observations)
            result = {'startingStateMatches': not failures, 'failures': failures,
                      'renderedSceneVerified': False, 'acceptance': 'not-evaluated'}
        else:
            result = audit(args.scenario, args.evidence, args.recipe, args.session, args.reload_session)
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
