# SPDX-License-Identifier: MIT
"""Export one correlated diagnostic observation; no live processes or requests."""
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
WEAPON_FIELDS = dict(magazineCapacity=('MagSize.Value', 'float32'),
                    magazineUsed=('AmmoUsedCount', 'float32'),
                    reserveStored=('CurrentSpareAmmo', 'int32'),
                    infiniteAmmo=('bInfiniteAmmo', 'bool'),
                    weaponCanBeReloaded=('bWeaponCanBeReloaded', 'bool'),
                    earlyReload=('bEarlyReload', 'bool'), drawingWeapon=('bDrawingWeapon', 'bool'),
                    holsteringWeapon=('bHolsteringWeapon', 'bool'), playerUsable=('bPlayerUsable', 'bool'))
LOADED_SOURCE = 'final SFXWeapon.GetAmmoCountInMagazine: int(MagSize.Value-AmmoUsedCount)'


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


def normalize_resource_fields(resources, stable):
    """Shared scalar/bit/source checks for fields sampled by either observer."""
    require(isinstance(resources, dict), 'Resource fields must be an object')
    state, unavailable = {'resources': {}}, {}
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
    return state['resources'], unavailable


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
    state['resources'], unavailable = normalize_resource_fields(resources, stable)
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


def weapon_identity(value, selected=False):
    require(isinstance(value, dict), 'Weapon context identity must be an object')
    keys = {'class', 'index'} | ({'name', 'nameNumber', 'heavyWeaponClass'} if selected else set())
    require(set(value) == keys and isinstance(value.get('class'), str) and 0 < len(value['class']) <= 256
            and '\0' not in value['class'] and type(value.get('index')) is int and 0 <= value['index'] <= 0x7fffffff,
            'Invalid process-local weapon identity')
    if selected:
        require(isinstance(value.get('name'), str) and 0 < len(value['name']) < 128 and '\0' not in value['name']
                and type(value.get('nameNumber')) is int and -0x80000000 <= value['nameNumber'] <= 0x7fffffff
                and type(value.get('heavyWeaponClass')) is bool, 'Invalid selected weapon identity')
    return dict(value)


def float32(value):
    require(type(value) in (int, float), 'Expected float32 scalar, not bool/string')
    try:
        converted = struct.unpack('<f', struct.pack('<f', value))[0]
    except (OverflowError, struct.error):
        raise ValueError('Scalar outside float32 range') from None
    require(math.isfinite(converted) and converted == value, 'Scalar is not an exact finite float32 value')
    return converted


RESERVE_SOURCE = 'SFXGame.SFXWeapon.GetCurrentSpareAmmo: float(CurrentSpareAmmo)'
RESERVE_POLICY = 'le2-audited-base-reserve-v1'
RESERVE_SDK = '6bd05809cccb1a099a520371a95779d4a56bb246'
RESERVE_PACKAGE = '681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77'
RESERVE_CLASSES = module('weapon-policy').RESERVE_CLASSES


def normalize_weapon(payload):
    capabilities = payload.get('capabilities')
    require(payload.get('source') == 'diagnostic-host' and payload.get('game') == 'LE2'
            and payload.get('readOnly') is True and payload.get('ready') is False
            and isinstance(capabilities, dict) and capabilities.get('observeWeapon') is True,
            'Not a completed read-only diagnostic weapon producer payload')
    observation = payload.get('weaponObservation')
    require(isinstance(observation, dict) and type(observation.get('schemaVersion')) is int
            and observation['schemaVersion'] == 1 and observation.get('game') == 'LE2'
            and type(observation.get('available')) is bool
            and all(observation.get(key) is False for key in ('dispatchCapability', 'reloadEligibilityVerified',
                    'reloadCompletionVerified', 'persistenceVerified', 'durableObjectIdentity', 'coherentSimulationSnapshot')),
            'Unexpected weapon observation contract')
    require(isinstance(observation.get('reason'), str) and observation['reason'], 'Weapon observation reason required')
    stable = observation['available']
    state, unavailable = {'weapon': {}}, {}
    if stable:
        state['weapon']['identity'] = weapon_identity(observation.get('selectedWeapon'), selected=True)
        context = observation.get('context')
        require(isinstance(context, dict) and type(context.get('pendingWeaponPresent')) is bool
                and context.get('source') == 'current pawn.Weapon; reciprocal Instigator/InvManager/WorldInfo',
                'Unexpected weapon context contract')
        for key in ('world', 'controller', 'pawn', 'inventory'):
            weapon_identity(context.get(key))
    else:
        unavailable['weapon.identity'] = {'reason': observation['reason'], 'sample': observation.get('selectedWeapon')}
    fields = observation.get('fields', {})
    require(isinstance(fields, dict), 'Weapon fields must be an object')
    for key, (source, scalar) in WEAPON_FIELDS.items():
        sample = fields.get(key)
        reason = 'field-missing'
        if isinstance(sample, dict):
            require(type(sample.get('available')) is bool and isinstance(sample.get('reason'), str)
                    and sample['reason'] and sample.get('source') == source, f'{key}: wrong availability/reason/source')
            reason = sample['reason']
            if sample['available']:
                value = sample.get('value')
                if scalar == 'float32':
                    float32(value)
                elif scalar == 'int32':
                    require(type(value) is int and -0x80000000 <= value <= 0x7fffffff, f'{key}: invalid signed raw int32')
                else:
                    require(type(value) is bool, f'{key}: invalid boolean flag')
                if stable:
                    state['weapon'][key] = value
                    continue
        else:
            require(sample is None, f'{key}: field must be an object')
        unavailable['weapon.' + key] = {'reason': reason if stable else observation['reason'], 'sample': sample}
    loaded = fields.get('loadedCount')
    if isinstance(loaded, dict):
        require(type(loaded.get('available')) is bool and isinstance(loaded.get('reason'), str) and loaded['reason']
                and loaded.get('source') == LOADED_SOURCE, 'loadedCount: wrong availability/reason/source')
        if loaded['available']:
            value = loaded.get('value')
            require(type(value) is int and 0 <= value <= 0x7fffffff, 'loadedCount: invalid integer')
            capacity, used = fields.get('magazineCapacity', {}), fields.get('magazineUsed', {})
            require(isinstance(capacity, dict) and isinstance(used, dict)
                    and capacity.get('available') is True and used.get('available') is True,
                    'loadedCount requires available capacity and used values')
            cap, used_value = float32(capacity.get('value')), float32(used.get('value'))
            require(0 <= used_value <= cap, 'loadedCount: invalid source range')
            # Match the source float subtraction before its int truncation;
            # Python's double subtraction alone can differ at float32 edges.
            remainder = struct.unpack('<f', struct.pack('<f', cap - used_value))[0]
            require(math.isfinite(remainder) and remainder < 2147483648 and int(remainder) == value,
                    'loadedCount differs from source float32 subtraction/truncation')
            require(loaded['reason'] == 'source-derived-truncation-toward-zero', 'loadedCount: missing derivation semantics')
            if stable:
                state['weapon']['loaded'] = value
        if not loaded['available'] or not stable:
            unavailable['weapon.loaded'] = {'reason': loaded['reason'] if stable else observation['reason'], 'sample': loaded}
    else:
        require(loaded is None, 'loadedCount must be an object')
        unavailable['weapon.loaded'] = {'reason': 'field-missing' if stable else observation['reason'], 'sample': loaded}
    effective = fields.get('effectiveReserve')
    if effective is not None:
        require(isinstance(effective, dict) and type(effective.get('available')) is bool
                and isinstance(effective.get('reason'), str) and effective['reason'],
                'effectiveReserve: invalid availability/reason')
        if effective['available']:
            require(effective.get('source') == RESERVE_SOURCE
                    and effective['reason'] == 'source-derived-int32-to-float32', 'effectiveReserve: wrong derivation')
            raw = fields.get('reserveStored', {})
            require(isinstance(raw, dict) and raw.get('available') is True
                    and type(raw.get('value')) is int and 0 <= raw['value'] <= 0x7fffffff,
                    'effectiveReserve requires nonnegative available int32 reserveStored')
            value = float32(effective.get('value'))
            expected = struct.unpack('<f', struct.pack('<f', raw['value']))[0]
            require(value == expected, 'effectiveReserve differs from float32 conversion of reserveStored')
            proof, selected = effective.get('provenance'), observation.get('selectedWeapon', {})
            require(isinstance(proof, dict) and isinstance(selected, dict)
                    and selected.get('class') in RESERVE_CLASSES and selected.get('heavyWeaponClass') is False
                    and proof.get('selectedClass') == selected['class']
                    and proof.get('policy') == RESERVE_POLICY
                    and proof.get('declaration') == 'SFXGame.SFXWeapon.GetCurrentSpareAmmo'
                    and proof.get('sdkPin') == RESERVE_SDK and proof.get('basePackageSha256') == RESERVE_PACKAGE
                    and proof.get('nativeGetterExecuted') is False and proof.get('mountedScriptBodyProven') is False
                    and all(type(proof.get(key)) is int and 0 <= proof[key] < 64 * 1024 * 1024
                            for key in ('functionIndex', 'returnPropertyIndex'))
                    and proof['functionIndex'] != proof['returnPropertyIndex'],
                    'effectiveReserve: missing or contradictory audited declaration provenance')
            if stable:
                state['weapon']['effectiveReserve'] = value
        if not effective['available'] or not stable:
            unavailable['weapon.effectiveReserve'] = {'reason': effective['reason'] if stable else observation['reason'], 'sample': effective}
    else:
        unavailable['weapon.effectiveReserve'] = {'reason': 'field-missing', 'sample': None}
    return state, unavailable


ORBITAL_SOURCE = 'unique pawn.LatentActions BioSeqAct_OrbitalGame.CurrentPlanet; camera hierarchy cross-check'
ORBITAL_FLAGS = ('scanning', 'probeActive', 'impactActive', 'rescaling', 'exiting')
ORBITAL_FLOATS = ('probeLaunchTime', 'impactTime', 'scanDist')
ORBITAL_IDENTITIES = ('controller', 'pawn', 'world', 'worldInfo', 'inventory', 'mode', 'camera', 'latentAction')
ORBITAL_LIMITS = ('dispatchCapability', 'savedPlanetIdVerified', 'discoveryConditionalEvaluated',
                  'persistenceVerified', 'durableObjectIdentity')


def orbital_integer(value, low=-0x80000000, high=0x7fffffff):
    require(type(value) is int and low <= value <= high, 'Orbital integer outside declared SDK range')
    return value


def orbital_identity(value):
    require(isinstance(value, dict) and set(value) == {'available', 'reason', 'index', 'class', 'address', 'classAddress'}
            and value['available'] is True and value['reason'] == 'sdk-structural-checks-passed',
            'Orbital identity lacks validated object sample')
    orbital_integer(value['index'], 0, 64 * 1024 * 1024 - 1)
    require(isinstance(value['class'], str) and 0 < len(value['class']) <= 256
            and all(32 <= ord(c) <= 126 for c in value['class']), 'Invalid orbital class label')
    for key in ('address', 'classAddress'):
        require(isinstance(value[key], str) and re.fullmatch(r'0x[1-9A-F][0-9A-F]{0,15}', value[key]),
                'Invalid process-local orbital address encoding')
    # Pointer strings stay in the original provenance; not reusable handles.
    return {'class': value['class'], 'index': value['index']}


def normalize_orbital(payload):
    capabilities = payload.get('capabilities')
    require(payload.get('source') == 'diagnostic-host' and payload.get('game') == 'LE2'
            and payload.get('readOnly') is True and payload.get('ready') is False
            and isinstance(capabilities, dict) and capabilities.get('observeOrbital') is True,
            'Not a completed read-only diagnostic orbital producer payload')
    observation = payload.get('orbitalObservation')
    require(isinstance(observation, dict) and type(observation.get('schemaVersion')) is int
            and observation['schemaVersion'] == 1 and observation.get('game') == 'LE2'
            and type(observation.get('available')) is bool and observation.get('dispatchCapability') is False
            and isinstance(observation.get('reason'), str) and observation['reason'],
            'Unexpected orbital observation contract')
    # The producer intentionally emits this minimal unavailable object at its
    # payload budget. Missing claims are not interpreted as positive evidence.
    budget = (observation['available'] is False and observation['reason'] == 'orbital-payload-budget-exceeded'
              and set(observation) == {'schemaVersion', 'game', 'available', 'reason', 'dispatchCapability'})
    require(budget or all(observation.get(key) is False for key in ORBITAL_LIMITS),
            'Orbital observation must not claim saved identity, discovery or persistence')
    state = {'resources': {}, 'orbital': {}}
    unavailable = {
        'map': {'reason': 'orbital-planet-mapName-is-not-current-world-map', 'sample': None},
        'position': {'reason': 'not-sampled-by-orbital-observer', 'sample': None},
    }
    if not observation['available']:
        require(not any(key in observation for key in ('context', 'planet', 'state', 'resources')),
                'Unavailable orbital observation cannot contain available sample data')
        unavailable['orbital'] = {'reason': observation['reason'], 'sample': observation}
        for key in FIELDS:
            unavailable['resources.' + key] = {'reason': observation['reason'], 'sample': None}
        return state, unavailable
    require(observation['reason'] == 'stable-current-orbital-structural-sample', 'Orbital stable-sample reason required')
    context, planet, sampled = observation.get('context'), observation.get('planet'), observation.get('state')
    require(isinstance(context, dict) and context.get('cameraSource') == 'GameModeManager2.GameModes[9].GalaxyCam'
            and type(context.get('currentMode')) is int and context['currentMode'] == 10,
            'Orbital context must declare the verified current mode and camera path')
    active = orbital_integer(context.get('activeWorld'), 0)
    identities = {key: orbital_identity(context.get(key)) for key in ORBITAL_IDENTITIES}
    require(isinstance(planet, dict) and planet.get('source') == ORBITAL_SOURCE
            and planet.get('featuresComplete') is True, 'Orbital planet source/complete list required')
    name = planet.get('mapName')
    require(isinstance(name, str) and '\0' not in name and len(name.encode('utf-8')) <= 1024
            and len(name.encode('utf-16-le')) // 2 <= 255, 'Invalid bounded orbital landing map name')
    normalized_planet = dict(identity=orbital_identity(planet.get('object')), mapName=name,
                             featuresComplete=True, features=[])
    for key in ('tableId', 'displayNameRef', 'landConditional'):
        normalized_planet[key] = orbital_integer(planet.get(key))
    normalized_planet['levelType'] = orbital_integer(planet.get('levelType'), 0, 255)
    features = planet.get('features')
    require(isinstance(features, list) and len(features) <= 64, 'Orbital feature list exceeds producer bound')
    feature_ints = ('tableId', 'scaledAmount', 'visibleConditional', 'visibleParameter',
                    'usableConditional', 'usableParameter', 'transition', 'transitionParameter')
    for index, row in enumerate(features):
        require(isinstance(row, dict) and set(row) == {'index', 'type', 'mineralType', *feature_ints}
                and type(row.get('index')) is int and row['index'] == index, 'Invalid complete orbital feature row')
        checked = {key: orbital_integer(row[key]) for key in feature_ints}
        checked.update(index=index, type=orbital_integer(row['type'], 0, 255),
                       mineralType=orbital_integer(row['mineralType'], 0, 255))
        normalized_planet['features'].append(checked)
    require(isinstance(sampled, dict) and sampled.get('source') == 'current SFXGameModeOrbital fields'
            and all(type(sampled.get(key)) is bool for key in ORBITAL_FLAGS), 'Orbital flag source/type mismatch')
    normalized = dict(currentMode=10, activeWorld=active, context=identities, planet=normalized_planet)
    normalized.update({key: sampled[key] for key in ORBITAL_FLAGS})
    normalized.update({key: float32(sampled.get(key)) for key in ORBITAL_FLOATS})
    state['orbital'] = normalized
    state['resources'], missing = normalize_resource_fields(observation.get('resources'), True)
    unavailable.update(missing)
    return state, unavailable


DIALOGUE_LIMITS = ('dispatchCapability', 'legalityEvaluated', 'acceptedInterruptProven',
                   'historyComplete', 'durableObjectIdentity')
DIALOGUE_IDENTITIES = ('world', 'worldInfo', 'controller', 'localPlayer', 'engine', 'pawn')
DIALOGUE_GUI_FLAGS = ('visible', 'focused', 'panelInitialized', 'removing', 'inputDisabled',
                      'eventsDisabled', 'wheelDisplayed', 'paragonDisplayed', 'renegadeDisplayed',
                      'screenInitialized', 'wasTriggered', 'usingGamepad')


def dialogue_name(value):
    require(isinstance(value, str) and 1 <= len(value) <= 127
            and all(32 <= ord(c) <= 126 for c in value), 'Invalid bounded dialogue name')
    return value


def dialogue_node(value, count):
    require(isinstance(value, dict) and type(value.get('available')) is bool,
            'Dialogue node requires explicit availability')
    if not value['available']:
        require(set(value) == {'available', 'index', 'reason'} and type(value.get('index')) is int
                and value['index'] == -1 and value.get('reason') == 'no-node-minus-one',
                'Absent dialogue node must be the explicit minus-one sentinel')
        return dict(value)
    require(set(value) == {'available', 'index', 'reason', 'exportId', 'stateTransition', 'stateTransitionParameter'}
            and value.get('reason') == 'bounded-selected-node-fields', 'Invalid selected dialogue node fields')
    require(type(value.get('index')) is int and 0 <= value['index'] < count,
            'Selected dialogue node index outside sampled array')
    for key in ('exportId', 'stateTransition', 'stateTransitionParameter'):
        require(type(value[key]) is int and -0x80000000 <= value[key] <= 0x7fffffff,
                'Dialogue node metadata must be signed int32')
    return dict(value)


def normalize_dialogue(payload):
    capabilities = payload.get('capabilities')
    require(payload.get('source') == 'diagnostic-host' and payload.get('game') == 'LE2'
            and payload.get('readOnly') is True and payload.get('ready') is False
            and isinstance(capabilities, dict) and capabilities.get('observeDialogue') is True,
            'Not a completed read-only diagnostic dialogue producer payload')
    observation = payload.get('dialogueObservation')
    require(isinstance(observation, dict) and type(observation.get('schemaVersion')) is int
            and observation['schemaVersion'] == 1 and observation.get('game') == 'LE2'
            and type(observation.get('available')) is bool
            and isinstance(observation.get('reason'), str) and observation['reason']
            and all(observation.get(key) is False for key in DIALOGUE_LIMITS),
            'Unexpected dialogue observation contract or positive acceptance claim')
    state = {'dialogue': {}}
    unavailable = {key: {'reason': 'not-sampled-by-dialogue-observer', 'sample': None}
                   for key in ('map', 'position')}
    if not observation['available']:
        require(not any(key in observation for key in ('context', 'conversation', 'interrupt', 'gui')),
                'Unavailable dialogue observation cannot contain sample data')
        unavailable['dialogue'] = {'reason': observation['reason'], 'sample': observation}
        return state, unavailable
    require(observation['reason'] == 'stable-current-conversation-fields-only', 'Dialogue stable-sample reason required')
    context = observation.get('context')
    require(isinstance(context, dict) and context.get('mapContainmentProven') is False,
            'Dialogue context must not claim map containment')
    identities = {}
    for key in DIALOGUE_IDENTITIES:
        sample = context.get(key)
        if key == 'pawn' and isinstance(sample, dict) and sample.get('available') is False:
            require(set(sample) == {'available', 'reason'} and isinstance(sample.get('reason'), str)
                    and sample['reason'], 'Invalid unavailable dialogue pawn sample')
            unavailable['dialogue.context.pawn'] = {'reason': sample['reason'], 'sample': sample}
        else:
            # Dialogue and orbital producers reuse the same ObjectJson format.
            identities[key] = orbital_identity(sample)
    conv, interrupt, gui = (observation.get(key) for key in ('conversation', 'interrupt', 'gui'))
    object_keys = {'available', 'reason', 'index', 'class', 'address', 'classAddress'}
    require(isinstance(conv, dict) and object_keys <= conv.keys(), 'Dialogue conversation identity required')
    conversation = {'identity': orbital_identity({key: conv[key] for key in object_keys})}
    conversation['name'] = dialogue_name(conv.get('name'))
    require(type(conv.get('nameNumber')) is int and -0x80000000 <= conv['nameNumber'] <= 0x7fffffff,
            'Dialogue nameNumber must be a raw signed int32')
    conversation['nameNumber'] = conv['nameNumber']
    package = conv.get('package')
    require(isinstance(package, dict) and type(package.get('available')) is bool,
            'Dialogue direct package availability required')
    if package['available']:
        require(set(package) == object_keys | {'name'}, 'Invalid dialogue package sample shape')
        conversation['package'] = dict(identity=orbital_identity({key: package[key] for key in object_keys}),
                                       name=dialogue_name(package['name']))
    else:
        require(set(package) == {'available', 'reason'} and isinstance(package.get('reason'), str)
                and package['reason'], 'Invalid unavailable dialogue package sample')
        unavailable['dialogue.conversation.package'] = {'reason': package['reason'], 'sample': package}
    for key in ('entryCount', 'replyCount'):
        require(type(conv.get(key)) is int and 0 <= conv[key] <= 4096, 'Dialogue node count outside producer bound')
        conversation[key] = conv[key]
    for key in ('currentEntry', 'currentReply', 'selectedReply'):
        conversation[key] = dialogue_node(conv.get(key), conv['entryCount' if key == 'currentEntry' else 'replyCount'])
    conversation['nodePlayTimer'] = float32(conv.get('nodePlayTimer'))
    require(isinstance(interrupt, dict) and all(type(interrupt.get(key)) is bool for key in ('enabled', 'activated')),
            'Dialogue interrupt requires raw boolean flags')
    reply = dialogue_node(interrupt.get('reply'), conv['replyCount'])
    require(type(interrupt.get('replyListIndex')) is int and interrupt['replyListIndex'] == reply['index'],
            'Dialogue interrupt reply index disagrees with its node')
    kind = interrupt.get('type')
    require(type(kind) is int and 0 <= kind <= 255
            and interrupt.get('typeName') == ('renegade' if kind == 0 else 'paragon' if kind == 1 else 'unknown'),
            'Dialogue interrupt enum/name mismatch')
    sampled = dict(replyListIndex=reply['index'], reply=reply, type=kind, typeName=interrupt['typeName'],
                   enabled=interrupt['enabled'], activated=interrupt['activated'],
                   startTimeRemaining=float32(interrupt.get('startTimeRemaining')),
                   windowTimeRemaining=float32(interrupt.get('windowTimeRemaining')))
    state['dialogue'] = dict(context=identities, conversation=conversation, interrupt=sampled)
    require(isinstance(gui, dict) and type(gui.get('available')) is bool
            and isinstance(gui.get('reason'), str) and gui['reason'], 'Dialogue GUI availability/reason required')
    if gui['available']:
        require(gui['reason'] == 'reachable-cached-handler-display-state-only'
                and all(type(gui.get(key)) is bool for key in DIALOGUE_GUI_FLAGS),
                'Dialogue GUI must contain raw display flags')
        normalized_gui = {key: orbital_identity(gui.get(key)) for key in ('manager', 'panel', 'handler')}
        normalized_gui.update({key: gui[key] for key in DIALOGUE_GUI_FLAGS})
        state['dialogue']['gui'] = normalized_gui
    else:
        require(set(gui) == {'available', 'reason'}, 'Unavailable dialogue GUI cannot contain sample data')
        unavailable['dialogue.gui'] = {'reason': gui['reason'], 'sample': gui}
    return state, unavailable


HISTORY_COLUMNS = ('tickOffset', 'msOffset', 'status', 'epoch', 'currentEntryIndex', 'currentEntryExportId',
                   'currentReplyIndex', 'currentReplyExportId', 'selectedReplyIndex', 'selectedReplyExportId',
                   'interruptReplyIndex', 'interruptReplyExportId', 'interruptType', 'interruptFlags', 'guiFlags',
                   'nodeTimerBits', 'startTimeRemainingBits', 'windowTimeRemainingBits')
HISTORY_COUNTERS = ('sampledRows', 'unavailableRows', 'cadenceSkippedTicks', 'bufferDroppedRows',
                    'serializationOmittedRows', 'missedCadenceSlots', 'gapCount')
HISTORY_STATUSES = ('stable-sample', 'no-current-conversation', 'guarded-read-fault',
                    'invalid-or-unavailable-context', 'changed-between-reads')
HISTORY_GUI_FLAGS = ('available', *DIALOGUE_GUI_FLAGS)
HISTORY_VERBS = ('observe-dialogue-history', 'stop-dialogue-history')


def history_integer(value, low=0, high=0xffffffff):
    require(type(value) is int and low <= value <= high, 'History integer outside declared range')
    return value


def history_identity(value):
    identity = orbital_identity(value)
    require(len(identity['class']) <= 128, 'History dictionary class label exceeds recorder cap')
    return identity


def normalize_dialogue_history(payload, verb):
    require(verb in HISTORY_VERBS, 'Unsupported history export verb')
    capability = payload.get('capabilities')
    require(payload.get('source') == 'diagnostic-host' and payload.get('game') == 'LE2'
            and payload.get('ready') is False and payload.get('readOnly') is (verb == 'observe-dialogue-history')
            and isinstance(capability, dict) and capability.get('observeDialogueHistory') is True,
            'Invalid dialogue history host envelope')
    history = payload.get('dialogueHistory')
    require(isinstance(history, dict) and type(history.get('schemaVersion')) is int and history['schemaVersion'] == 1
            and history.get('game') == 'LE2' and type(history.get('started')) is bool and type(history.get('active')) is bool
            and isinstance(history.get('reason'), str) and history['reason']
            and all(history.get(key) is False for key in DIALOGUE_LIMITS if key != 'durableObjectIdentity'),
            'Invalid history recorder contract or acceptance claim')
    for key, expected in (('durationMs', 10000), ('cadenceMs', 125), ('capacity', 81), ('maxEpochs', 4)):
        require(type(history.get(key)) is int and history[key] == expected, 'Unexpected bounded recorder configuration')
    require(history.get('columns') == list(HISTORY_COLUMNS), 'History column schema mismatch')
    require(history.get('endTimeSource') == 'last-valid-tick-call-or-start', 'History end clock semantics required')
    clocks = {key: control.number(history.get(key)) for key in ('startTick', 'startMonoMs', 'endTick', 'endMonoMs')}
    counters = {key: history_integer(history.get(key)) for key in HISTORY_COUNTERS}
    require(isinstance(history.get('lastUnavailableReason'), str), 'History unavailable reason required')
    epochs, rows = history.get('epochs'), history.get('rows')
    require(isinstance(epochs, list) and len(epochs) <= 4 and isinstance(rows, list) and len(rows) <= 81,
            'History epochs/rows exceed recorder bound')
    identities = {}
    object_keys = {'available', 'reason', 'index', 'class', 'address', 'classAddress'}
    for epoch in epochs:
        require(isinstance(epoch, dict) and set(epoch) == {'epoch', 'controller', 'worldInfo', 'conversation'},
                'Unexpected history epoch shape')
        number = history_integer(epoch['epoch'], 1, 4)
        require(number not in identities, 'Duplicate history epoch')
        conv = epoch['conversation']
        require(isinstance(conv, dict) and set(conv) == object_keys | {'name', 'nameNumber'},
                'Invalid history conversation identity')
        identities[number] = dict(epoch=number, controller=history_identity(epoch['controller']),
            worldInfo=history_identity(epoch['worldInfo']),
            conversation=dict(identity=history_identity({key: conv[key] for key in object_keys}),
                              name=dialogue_name(conv['name']),
                              nameNumber=history_integer(conv['nameNumber'], -0x80000000, 0x7fffffff)))
    expanded, adjacent = [], []
    previous = None
    last_epoch = 0
    for row in rows:
        require(isinstance(row, list) and len(row) == len(HISTORY_COLUMNS), 'Invalid compact history row width')
        tick, ms, status, epoch = (history_integer(row[i], 0, bound) for i, bound in
                                  enumerate((0xffffffff, 9999, 4, 4)))
        if previous is not None:
            require(tick > previous[0] and ms - previous[1] >= 125,
                    'History ticks must increase and rows must respect recorder cadence')
            same_identity = status == previous[2] == 0 and epoch == previous[3]
            adjacent.append(dict(fromRow=len(expanded)-1, toRow=len(expanded), tickDelta=tick-previous[0],
                msDelta=ms-previous[1], elapsedExceedsCadence=ms-previous[1] > 125,
                sameSampledIdentity=same_identity, unavailableEndpoint=status != 0 or previous[2] != 0,
                changedColumns=([name for name, left, right in zip(HISTORY_COLUMNS[4:], previous[4:], row[4:])
                                 if left != right] if same_identity else None)))
        item = dict(tickOffset=tick, msOffset=ms, status=HISTORY_STATUSES[status], epoch=epoch)
        if status:
            require(epoch == 0 and all(value is None for value in row[4:]),
                    'Unavailable history row cannot contain state or identity')
        else:
            require(epoch in identities, 'History row refers to missing epoch')
            require(epoch >= last_epoch, 'History identity epoch moves backwards')
            last_epoch = epoch
            for index, label in ((4, 'currentEntry'), (6, 'currentReply'), (8, 'selectedReply'), (10, 'interruptReply')):
                node = history_integer(row[index], -1, 4095)
                export_id = row[index+1]
                if node == -1:
                    require(export_id is None, 'Absent history node must not carry an export ID')
                else:
                    history_integer(export_id, -0x80000000, 0x7fffffff)
                item[label] = dict(index=node, exportId=export_id)
            kind = history_integer(row[12], 0, 255)
            flags = history_integer(row[13], 0, 3)
            gui = history_integer(row[14], 0, 8191)
            require(bool(gui & 1) or gui == 0, 'Unavailable history GUI must not carry display flags')
            item['interrupt'] = dict(type=kind, enabled=bool(flags & 1), activated=bool(flags & 2))
            # Other bits are uninterpreted unless the producer sampled the GUI.
            item['guiFlags'] = gui
            if gui & 1:
                item['gui'] = {name: bool(gui & (1 << bit)) for bit, name in enumerate(HISTORY_GUI_FLAGS)}
            for index, label in ((15, 'nodePlayTimer'), (16, 'startTimeRemaining'), (17, 'windowTimeRemaining')):
                bits = history_integer(row[index])
                value = struct.unpack('<f', struct.pack('<I', bits))[0]
                require(math.isfinite(value), 'History float bits must represent finite float32')
                item[label] = value
        expanded.append(item)
        previous = row
    omitted = counters['bufferDroppedRows'] + counters['serializationOmittedRows']
    shown_unavailable = sum(row[2] != 0 for row in rows)
    require(counters['sampledRows'] == len(rows) + omitted
            and shown_unavailable <= counters['unavailableRows'] <= shown_unavailable + omitted
            and counters['gapCount'] <= counters['missedCadenceSlots'],
            'History row counters contradict serialized data')
    require(sorted(identities) == list(range(1, len(identities)+1)), 'History epoch sequence is not contiguous')
    if not history['started']:
        require(not history['active'] and not rows and not epochs and not any(clocks.values())
                and not any(counters.values()) and history['reason'] == 'never-started'
                and 'dialogueHistoryStartRequest' not in payload,
                'Never-started history cannot contain capture data')
    else:
        require(control.valid_uuid(payload.get('dialogueHistoryStartRequest')), 'Started history lacks host origin UUID')
        if history['active']:
            require(clocks['endTick'] == clocks['endMonoMs'] == 0 and history['reason'] == 'recording',
                    'Active history has terminal clocks/reason')
        else:
            require(clocks['endTick'] >= clocks['startTick'] and clocks['endMonoMs'] >= clocks['startMonoMs'],
                    'Finished history end precedes start clocks')
            require(not rows or (clocks['startTick'] + rows[-1][0] <= clocks['endTick']
                                and clocks['startMonoMs'] + rows[-1][1] <= clocks['endMonoMs']),
                    'Finished history contains a row after its end clocks')
            require(history['reason'] in ('stopped', 'expired', 'clock-invalid', 'context-limit',
                                          'read-allocation-failed', 'history-label-limit', 'counter-limit'),
                    'Unknown history terminal reason')
    require(verb != 'stop-dialogue-history' or not history['active'], 'Stop result must not describe an active recorder')
    state = {'dialogueHistory': dict(started=history['started'], active=history['active'], reason=history['reason'],
             durationMs=10000, cadenceMs=125, startTick=history['startTick'], startMonoMs=history['startMonoMs'],
             endTick=history['endTick'], endMonoMs=history['endMonoMs'], endTimeSource=history['endTimeSource'], reportedCounters=counters,
             lastUnavailableReason=history['lastUnavailableReason'], epochs=list(identities.values()), rows=expanded,
             adjacentSamples=adjacent, historyComplete=False, acceptedInterruptProven=False)}
    unavailable = {key: {'reason': 'not-sampled-by-dialogue-history-recorder', 'sample': None}
                   for key in ('map', 'position')}
    return state, unavailable


def dialogue_history_start(run, request, payload, consumed):
    """Cross-check the host's capture-origin attribution; no live binding or input."""
    start_id = payload.get('dialogueHistoryStartRequest')
    if 'dialogueHistoryStartRequest' not in payload:
        return {'available': False, 'reason': 'host-has-no-successful-history-start'}
    require(control.valid_uuid(start_id), 'History start request must be a canonical UUID')
    request_path = run / 'control/history' / (start_id + '.request')
    response_path = run / 'control/history' / (start_id + '.result')
    request_bytes, response_bytes = raw(request_path, control.MAX_FRAME), raw(response_path, control.MAX_FRAME)
    start, result = control.decode(request_bytes), control.decode(response_bytes)
    require(start['kind'] == 'request' and start['id'] == start_id and start['verb'] == 'start-dialogue-history'
            and not start['payload'] and result['kind'] == 'result' and result['status'] == 'completed'
            and all(start[key] == result[key] for key in (*control.IDENTITY, 'id', 'sequence', 'deadline', 'verb'))
            and all(start[key] == request[key] for key in control.IDENTITY)
            and control.number(start['sequence']) < control.number(request['sequence']),
            'History origin is not an earlier correlated completed start in this lease')
    matches = [p for p in consumed if raw(p, control.MAX_FRAME) == request_bytes]
    require(len(matches) == 1, 'History start lacks unique consumed request evidence')
    started_payload = parse(result['payload'])
    require(isinstance(started_payload, dict) and started_payload.get('source') == 'diagnostic-host' and started_payload.get('game') == 'LE2'
            and started_payload.get('readOnly') is False and started_payload.get('ready') is False
            and isinstance(started_payload.get('capabilities'), dict)
            and started_payload['capabilities'].get('observeDialogueHistory') is True
            and started_payload.get('dialogueHistoryStartRequest') == start_id,
            'History start host envelope does not match capture attribution')
    origin = started_payload.get('dialogueHistory')
    history = payload['dialogueHistory']
    require(isinstance(origin, dict) and type(origin.get('schemaVersion')) is int and origin['schemaVersion'] == 1
            and origin.get('game') == 'LE2' and origin.get('started') is True and origin.get('active') is True
            and origin.get('reason') == 'recording' and origin.get('rows') == [] and origin.get('epochs') == []
            and origin.get('endTick') == origin.get('endMonoMs') == '0'
            and origin.get('endTimeSource') == 'last-valid-tick-call-or-start'
            and origin.get('columns') == list(HISTORY_COLUMNS)
            and all(type(origin.get(key)) is int and origin[key] == 0 for key in HISTORY_COUNTERS)
            and all(origin.get(key) is False for key in DIALOGUE_LIMITS if key != 'durableObjectIdentity')
            and all(origin.get(key) == history[key] for key in ('startTick', 'startMonoMs', 'durationMs', 'cadenceMs', 'capacity', 'maxEpochs')),
            'History start snapshot differs from exported capture origin')
    return dict(available=True, kind='host-correlated-recorder-start-not-gameplay-input', requestId=start_id,
                sequence=start['sequence'], requestSha256=sha(request_bytes), responseSha256=sha(response_bytes),
                consumedRequest=matches[0].name)


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
    require(request['kind'] == 'request' and request['id'] == request_id and request['verb'] in ('observe-resources', 'observe-weapon', 'observe-orbital', 'observe-dialogue', *HISTORY_VERBS)
            and not request['payload'] and response['kind'] == 'result' and response['status'] == 'completed',
            'Requires archived empty diagnostic observation/stop-history request and completed result')
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
    weapon = request['verb'] == 'observe-weapon'
    orbital = request['verb'] == 'observe-orbital'
    dialogue = request['verb'] == 'observe-dialogue'
    history = request['verb'] in HISTORY_VERBS
    observation_key = 'dialogueHistory' if history else 'weaponObservation' if weapon else 'orbitalObservation' if orbital else 'dialogueObservation' if dialogue else 'resourceObservation'
    normalizer = normalize_weapon if weapon else normalize_orbital if orbital else normalize_dialogue if dialogue else normalize
    state, unavailable = normalize_dialogue_history(payload, request['verb']) if history else normalizer(payload)
    history_origin = dialogue_history_start(run, request, payload, consumed) if history else None
    require(control.number(payload.get('gameTick')) > 0 and control.number(payload.get('gameThreadId'), (1 << 32) - 1) > 0,
            'Missing sample tick/thread')
    control.number(payload.get('sampleMonotonicMs'))
    artifact = dict(schemaVersion=1, source='runtime-observation', identity=identity, state=state, unavailable=unavailable,
                    provenance=dict(requestId=request_id, wireIdentity=wire_identity, requestSha256=sha(request_bytes),
                        responseSha256=sha(response_bytes), consumedRequest=matches[0].name,
                        gameTick=payload['gameTick'], gameThreadId=payload['gameThreadId'], sampleMonotonicMs=payload['sampleMonotonicMs'],
                        deadline=request['deadline'], deploymentSha256=session.digest(run / 'deployment.json'),
                        candidateCommitEvidence='reviewed-deployment-receipt-attestation-not-binary-commit-proof',
                        session=binding, observationVerb=request['verb'], combinedObservation=False,
                        normalizedSemantics=({'weapon.loaded': LOADED_SOURCE, 'weapon.reserveStored': 'raw signed CurrentSpareAmmo; not effective/legal reserve', 'weapon.effectiveReserve': RESERVE_SOURCE + '; audited declaration, no getter call or live bytecode proof'} if weapon else {'orbital.planet.mapName': 'BioPlanet.MapName landing destination; not current world map', 'orbital.planet.features': 'raw conditional/transition metadata; no evaluation or discovery proof'} if orbital else {'dialogue': 'stable sampled fields only; no timing history, legality, accepted interrupt or selection acceptance inference'} if dialogue else {}),
                        runtimeObservation=payload[observation_key]),
                    claims=dict(visualVerified=False, loadedFixtureProven=False, persistenceProven=False, vrVerified=False))
    if history:
        artifact['provenance']['historyOrigin'] = history_origin
        artifact['provenance']['normalizedSemantics'] = {
            'dialogueHistory': 'bounded sampled history; raw counters and adjacent intervals, not accepted input or complete event history',
            'dialogueHistory.adjacentSamples': 'differences between serialized rows only; omitted rows may lie between them; no interpolation'}
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
