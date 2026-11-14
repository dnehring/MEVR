# SPDX-License-Identifier: MIT
"""Offline LE2 before/after diagnostics for selected direct state-event effects.

Never edits saves, executes transitions, or establishes that an event fired.
The selected source-report bytes and both save inputs must be hash-pinned.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import struct

HERE = Path(__file__).resolve().parent


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


SAVE = module('le2_story_save', HERE.parent / 'le2-save.py')
JSON = module('le2_story_json', HERE / 'scenario.py')
FORMATS = {
    'BioStateEventElementBool': ('booleans', 'GlobalBool', 'NewState', 0),
    'BioStateEventElementInt': ('integers', 'GlobalInt', 'NewValue', 4),
    'BioStateEventElementFloat': ('floats', 'GlobalFloat', 'NewValue', 2),
}
WORLD = ('base_level_name', 'location', 'rotation', 'levels', 'streaming_states',
         'kismet_records', 'doors', 'pawns', 'galaxy_map')


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def text(value):
    return isinstance(value, str) and bool(value.strip())


def integer(value):
    return type(value) is int


def hash_value(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-fA-F]{64}', value) is not None


def read(path):
    if Path(path).stat().st_size > 32 * 1024 * 1024:
        raise ValueError('JSON input exceeds 32 MiB limit')
    raw = Path(path).read_bytes()
    result = JSON.read_json(path)
    if Path(path).read_bytes() != raw:
        raise ValueError('JSON input changed while being read')
    return result, raw


def scalar(value, kind):
    """Only explicit typed operands; do not invent native parameter coercions."""
    if kind == 'booleans':
        if type(value) is not bool:
            raise ValueError('boolean operand required; integer-to-bool conversion is unverified')
        return value
    if kind == 'integers':
        if not integer(value) or not -(2 ** 31) <= value < 2 ** 31:
            raise ValueError('signed 32-bit integer operand required')
        return value
    if type(value) not in (int, float):
        raise ValueError('finite numeric float operand required')
    try:
        encoded = struct.unpack('<f', struct.pack('<f', value))[0]
    except (OverflowError, struct.error):
        raise ValueError('float32 operand out of range') from None
    if not math.isfinite(encoded):
        raise ValueError('finite float32 operand required')
    return encoded


def selection_key(event):
    return (event['id'], event['packageSha256'].lower(), event['mapPath'], event['mapExportIndex'])


def validate_selection(selection, before_hash, after_hash, report_hash):
    if not isinstance(selection, dict) or type(selection.get('schemaVersion')) is not int or selection['schemaVersion'] != 1:
        raise ValueError('selection schemaVersion must be integer 1')
    required = {'schemaVersion', 'beforeSha256', 'afterSha256', 'sourceReportSha256', 'events'}
    if set(selection) != required:
        raise ValueError('selection keys must exactly identify the three input hashes and events')
    for key, actual in (('beforeSha256', before_hash), ('afterSha256', after_hash), ('sourceReportSha256', report_hash)):
        if not hash_value(selection[key]) or selection[key].lower() != actual:
            raise ValueError(f'{key} does not match input bytes')
    if not isinstance(selection['events'], list) or not selection['events']:
        raise ValueError('select at least one event')
    seen = set()
    for event in selection['events']:
        keys = {'id', 'packageSha256', 'mapPath', 'mapExportIndex'}
        if not isinstance(event, dict) or not keys <= set(event) or set(event) - keys - {'parameter'}:
            raise ValueError('invalid selected event fields')
        if (not integer(event['id']) or event['id'] < 0 or not hash_value(event['packageSha256'])
                or not text(event['mapPath']) or not integer(event['mapExportIndex']) or event['mapExportIndex'] <= 0):
            raise ValueError('selected event requires nonnegative ID, package hash and exact map identity')
        key = selection_key(event)
        if key in seen:
            raise ValueError('duplicate selected event; repeated execution is not modeled')
        seen.add(key)


def review(before_path, after_path, source_path, selection_path):
    before, after = SAVE.Save.load(before_path), SAVE.Save.load(after_path)
    source, source_raw = read(source_path)
    selection, selection_raw = read(selection_path)
    validate_selection(selection, sha(before.raw), sha(after.raw), sha(source_raw))
    if (not isinstance(source, dict) or type(source.get('schemaVersion')) is not int or source['schemaVersion'] != 1
            or source.get('game') != 'LE2' or not isinstance(source.get('events'), list)
            or not isinstance(source.get('packages'), list) or not isinstance(source.get('scope'), dict)):
        raise ValueError('expected a scoped LE2 state-events report')
    unresolved, effects, selected_sources = [], [], []
    valid_events = []
    for event in source['events']:
        if (not isinstance(event, dict) or not integer(event.get('id')) or event['id'] < 0
                or not hash_value(event.get('packageSha256')) or not text(event.get('packageFile'))
                or not text(event.get('mapPath')) or not integer(event.get('mapExportIndex'))
                or event['mapExportIndex'] <= 0):
            unresolved.append({'reason': 'malformed-source-event', 'record': event})
        else:
            valid_events.append(event)
    if (source.get('scopedInspectionComplete') is not True or source.get('errors') != []
            or source.get('missingIds') != [] or source['scope'].get('mountedCompleteness') is not False):
        unresolved.append({'reason': 'source-report-incomplete-or-scope-invalid',
                           'details': 'Source diagnostics cannot be ignored; direct comparisons below remain provisional.'})
    for choice in selection['events']:
        key = selection_key(choice)
        candidates = [event for event in valid_events if selection_key(event) == key]
        if (not isinstance(source.get('requestedIds'), list)
                or not any(type(value) is int and value == choice['id'] for value in source['requestedIds'])):
            unresolved.append({'event': choice, 'reason': 'event-not-in-source-requested-ids'})
        if len(candidates) != 1:
            unresolved.append({'event': choice, 'reason': 'selected-event-missing-or-ambiguous', 'matches': len(candidates)})
            continue
        event = candidates[0]
        # Require agreement between the report's package inventory and the selected map.
        inventories = [p for p in source['packages'] if isinstance(p, dict)
                       and p.get('packageFile') == event.get('packageFile')
                       and hash_value(p.get('packageSha256')) and p['packageSha256'].lower() == key[1] and p.get('game') == 'LE2'
                       and isinstance(p.get('maps'), list)
                       and any(isinstance(m, dict) and m.get('path') == key[2]
                               and integer(m.get('exportIndex')) and m['exportIndex'] == key[3]
                               for m in p['maps'])]
        if not text(event.get('packageFile')) or len(inventories) != 1:
            unresolved.append({'event': choice, 'reason': 'source-package-map-inventory-mismatch'})
            continue
        selected_sources.append({**choice, 'packageFile': event['packageFile']})
        if not isinstance(event.get('elements'), list) or not event['elements']:
            unresolved.append({'event': choice, 'reason': 'missing-or-empty-element-list'})
            continue
        parameter_used = False
        for index, element in enumerate(event['elements']):
            identity = {'event': choice, 'elementIndex': index}
            try:
                if not isinstance(element, dict) or set(element) != {'type', 'data'}:
                    raise ValueError('invalid element wrapper')
                element_type, data = element['type'], element['data']
                if element_type not in FORMATS:
                    raise ValueError('unsupported effect; function/consequence/local/substate/conditional effects are unresolved')
                kind, target, operand, discriminator = FORMATS[element_type]
                keys = {target, operand, 'UseParam', 'InstanceVersion', 'Type'}
                if kind != 'booleans':
                    keys.add('Increment')
                if not isinstance(data, dict) or set(data) != keys:
                    raise ValueError('unsupported element field shape; missing and unknown fields are not ignored')
                if (type(data['Type']) is not int or data['Type'] != discriminator
                        or type(data['InstanceVersion']) is not int or data['InstanceVersion'] != 0):
                    raise ValueError('unsupported element discriminator or instance version')
                if not integer(data[target]) or data[target] < 0 or type(data['UseParam']) is not bool:
                    raise ValueError('invalid plot index or UseParam flag')
                increment = data.get('Increment', False)
                if type(increment) is not bool:
                    raise ValueError('Increment must be a boolean')
                declared = scalar(data[operand], kind)  # Validate even when replaced by an explicit parameter.
                if data['UseParam']:
                    parameter_used = True
                    if 'parameter' not in choice:
                        raise ValueError('UseParam requires an explicit typed parameter in the selected event')
                    applied = scalar(choice['parameter'], kind)
                else:
                    applied = declared
                path = f'plot.{kind}.{data[target]}'
                old, current = before.value(path), after.value(path)
                expected = scalar(old + applied, kind) if increment else applied
                effects.append({**identity, 'type': element_type, 'path': path,
                                'operation': 'increment' if increment else 'assign', 'sourceData': data,
                                'operand': applied, 'before': old, 'after': current, 'expected': expected})
            except (ValueError, TypeError, OverflowError, SAVE.SaveError) as exc:
                unresolved.append({**identity, 'reason': str(exc), 'element': element})
        if 'parameter' in choice and not parameter_used:
            unresolved.append({'event': choice, 'reason': 'supplied-parameter-not-used-by-any-supported-effect'})

    # Do not assume event execution order, repeated firings, or intermediate state from two endpoint saves.
    counts = Counter(effect['path'] for effect in effects)
    for effect in effects:
        if counts[effect['path']] > 1:
            effect['comparison'] = 'unresolved-multiple-writes'
            unresolved.append({'event': effect['event'], 'elementIndex': effect['elementIndex'],
                               'path': effect['path'], 'reason': 'multiple-direct-writes; execution order is unproven'})
        elif not SAVE.scalar_equal(effect['after'], effect['expected']):
            effect['comparison'] = 'missing-or-different-effect'
        elif SAVE.scalar_equal(effect['before'], effect['expected']):
            effect['comparison'] = 'already-at-expected-value'
        else:
            effect['comparison'] = 'changed-to-expected-value'
        effect['eventFiredEstablished'] = False

    changes = SAVE.diff(before, after)
    paths = set(counts)
    groups = {'selectedDirectFields': [], 'otherPlot': [], 'journal': [], 'world': [], 'other': []}
    for change in changes:
        path = change['path']
        if path in paths:
            group = 'selectedDirectFields'
        elif path.startswith(('plot.', 'me1_plot.')):
            group = 'otherPlot'
        elif path == 'journal' or path.startswith('journal.'):
            group = 'journal'
        elif path.split('.')[0] in WORLD:
            group = 'world'
        else:
            group = 'other'
        groups[group].append(change)
    mismatches = [e for e in effects if e['comparison'] == 'missing-or-different-effect']
    diagnostic = ('unresolved' if unresolved else 'direct-effect-mismatch' if mismatches
                  else 'direct-values-consistent')
    return {'schemaVersion': 1, 'purpose': 'offline-source-review-only', 'diagnostic': diagnostic,
            'inputPaths': {key: str(Path(path).resolve()) for key, path in
                           (('before', before_path), ('after', after_path), ('sourceReport', source_path), ('selection', selection_path))},
            'identity': {'beforeSha256': sha(before.raw), 'afterSha256': sha(after.raw),
                         'sourceReportSha256': sha(source_raw), 'selectionSha256': sha(selection_raw),
                         'saveAdapterSha256': sha((HERE.parent / 'le2-save.py').read_bytes())},
            'sourceScope': source['scope'], 'selectedSources': selected_sources,
            'packageBytesReverified': False, 'byteIdenticalSaves': before.raw == after.raw,
            'directEffects': effects, 'missingEffects': mismatches, 'unresolved': unresolved,
            'changeGroups': groups, 'semanticChangeCount': len(changes),
            'eventFiredEstablished': False, 'nativeAcceptance': False, 'storyValidityEstablished': False,
            'limitations': ['Endpoint agreement cannot establish that the selected event fired or caused a change.',
                            'Already-at-value is idempotent agreement, not evidence of execution.',
                            'Other deltas may be legitimate indirect effects or unrelated activity; causality is unresolved.',
                            'Package identities are bound to the pinned report, not re-read from the installation.',
                            'No mounted completeness, implicit parameter conversion, repeated execution or transitive effects are modeled.']}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('before'); p.add_argument('after')
    p.add_argument('--source-report', required=True); p.add_argument('--selection', required=True)
    p.add_argument('--output', required=True)
    args = p.parse_args()
    try:
        output = Path(args.output)
        inputs = {Path(x).resolve() for x in (args.before, args.after, args.source_report, args.selection)}
        if output.resolve() in inputs or output.exists():
            raise ValueError('output must be a new file distinct from all inputs')
        result = review(args.before, args.after, args.source_report, args.selection)
        with output.open('x', encoding='utf-8') as stream:
            stream.write(json.dumps(result, indent=2, allow_nan=False) + '\n')
        print(json.dumps({'diagnostic': result['diagnostic'], 'directEffects': len(result['directEffects']),
                          'missingEffects': len(result['missingEffects']), 'unresolved': len(result['unresolved']),
                          'semanticChangeCount': result['semanticChangeCount'], 'acceptance': 'not-evaluated'}))
        return 0 if result['diagnostic'] == 'direct-values-consistent' else 2
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(json.dumps({'error': str(exc), 'acceptance': 'not-evaluated'}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
