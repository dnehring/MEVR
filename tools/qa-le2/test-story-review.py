# SPDX-License-Identifier: MIT
"""Offline diagnostics tests; synthetic edits never represent a native event run."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


review = load('story_review', HERE / 'story-review.py')
fixtures = load('story_wire_fixture', HERE.parents[1] / 'tests' / 'Le2SaveTests.py')


def element(kind='Bool', index=0, value=True, parameter=False, increment=False):
    target = {'Bool': 'GlobalBool', 'Int': 'GlobalInt', 'Float': 'GlobalFloat'}[kind]
    data = {target: index, 'NewState' if kind == 'Bool' else 'NewValue': value,
            'UseParam': parameter, 'InstanceVersion': 0, 'Type': {'Bool': 0, 'Float': 2, 'Int': 4}[kind]}
    if kind != 'Bool':
        data['Increment'] = increment
    return {'type': 'BioStateEventElement' + kind, 'data': data}


class StoryReviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.before, self.after, self.source, self.plan = (self.root / name for name in ('before.pcsav', 'after.pcsav', 'source.json', 'plan.json'))
        self.before.write_bytes(fixtures.fixture())
        self.after.write_bytes(self.before.read_bytes())
        self.choice = {'id': 7, 'packageSha256': review.sha(b'SYNTHETIC PACKAGE'), 'mapPath': 'Test.StateMap', 'mapExportIndex': 3}
        self.report = {'schemaVersion': 1, 'game': 'LE2', 'scope': {'mountedCompleteness': False,
                       'sourceDirectory': 'UNIT-TEST ONLY', 'packageSelector': 'Test'},
                       'requestedIds': [7], 'packages': [{'game': 'LE2', 'packageFile': 'UNIT-TEST.pcc',
                       'packageSha256': self.choice['packageSha256'], 'maps': [{'path': 'Test.StateMap', 'exportIndex': 3}]}],
                       'events': [{**self.choice, 'packageFile': 'UNIT-TEST.pcc', 'elements': [element()]}],
                       'missingIds': [], 'duplicateIds': [], 'errors': [], 'scopedInspectionComplete': True}
        self.choices = [self.choice]

    def bind(self):
        self.source.write_text(json.dumps(self.report), encoding='utf-8')
        self.selection = {'schemaVersion': 1, 'events': self.choices,
                          'beforeSha256': review.sha(self.before.read_bytes()), 'afterSha256': review.sha(self.after.read_bytes()),
                          'sourceReportSha256': review.sha(self.source.read_bytes())}
        self.plan.write_text(json.dumps(self.selection), encoding='utf-8')

    def run_review(self):
        self.bind()
        return review.review(self.before, self.after, self.source, self.plan)

    def edit(self, edits):
        save = review.SAVE.Save.load(self.before)
        changed, _ = save.prepare(fixtures.recipe(save, edits))
        self.after.write_bytes(changed.raw)

    def test_direct_bool_changed_is_not_event_execution_proof(self):
        self.edit({'plot.booleans.0': True})
        result = self.run_review()
        self.assertEqual(result['diagnostic'], 'direct-values-consistent')
        self.assertEqual(result['directEffects'][0]['comparison'], 'changed-to-expected-value')
        self.assertFalse(result['eventFiredEstablished'])
        self.assertFalse(result['nativeAcceptance'])
        self.assertFalse(result['storyValidityEstablished'])
        self.assertEqual(len(result['changeGroups']['selectedDirectFields']), 1)

    def test_idempotent_before_value_distinguished(self):
        self.report['events'][0]['elements'] = [element(index=1)]  # Synthetic base already has bit 1.
        result = self.run_review()
        self.assertEqual(result['directEffects'][0]['comparison'], 'already-at-expected-value')
        self.assertTrue(result['byteIdenticalSaves'])
        self.assertFalse(result['eventFiredEstablished'])

    def test_missing_effects_reported(self):
        result = self.run_review()
        self.assertEqual(result['diagnostic'], 'direct-effect-mismatch')
        self.assertEqual(result['missingEffects'][0]['path'], 'plot.booleans.0')

    def test_numeric_assignment_increment_and_float32(self):
        self.report['events'][0]['elements'] = [element('Int', 0, 5, increment=True), element('Float', 0, 0.1)]
        self.edit({'plot.integers.0': 3, 'plot.floats.0': 0.1})  # Synthetic int starts at -2.
        result = self.run_review()
        self.assertEqual(result['diagnostic'], 'direct-values-consistent')
        self.assertEqual(result['directEffects'][1]['expected'], struct.unpack('<f', struct.pack('<f', 0.1))[0])
        self.report['events'][0]['elements'] = [element('Int', 0, -3), element('Float', 0, 0.25, increment=True)]
        self.edit({'plot.integers.0': -3, 'plot.floats.0': 1.5})
        self.assertEqual(self.run_review()['diagnostic'], 'direct-values-consistent')

    def test_parameter_required_and_never_implicitly_coerced(self):
        self.report['events'][0]['elements'] = [element(parameter=True)]
        self.assertEqual(self.run_review()['diagnostic'], 'unresolved')
        self.choice['parameter'] = 1
        self.assertEqual(self.run_review()['diagnostic'], 'unresolved')
        self.choice['parameter'] = True
        self.edit({'plot.booleans.0': True})
        self.assertEqual(self.run_review()['diagnostic'], 'direct-values-consistent')
        self.report['events'][0]['elements'] = [element('Int', 0, 100, parameter=True)]
        self.choice['parameter'] = 9
        self.edit({'plot.integers.0': 9})
        self.assertEqual(self.run_review()['directEffects'][0]['expected'], 9)

    def test_unused_parameter_and_unknown_effects_unresolved(self):
        self.choice['parameter'] = True
        self.assertEqual(self.run_review()['diagnostic'], 'unresolved')
        del self.choice['parameter']
        for name in ('Function', 'Consequence', 'Substate', 'LocalBool', 'Conditional', 'Unknown'):
            with self.subTest(name=name):
                self.report['events'][0]['elements'] = [{'type': 'BioStateEventElement' + name, 'data': {'nested': {'keep': 1}}}]
                result = self.run_review()
                self.assertEqual(result['diagnostic'], 'unresolved')
                self.assertEqual(result['unresolved'][0]['element']['data']['nested']['keep'], 1)

    def test_bad_element_shapes_and_types_do_not_pass(self):
        mutations = [lambda d: d.update(UseParam='false'), lambda d: d.update(NewState=1),
                     lambda d: d.update(Type=False), lambda d: d.update(InstanceVersion=1),
                     lambda d: d.update(FutureField=1), lambda d: d.pop('NewState'), lambda d: d.update(GlobalBool=-1)]
        for mutation in mutations:
            bad = element()
            mutation(bad['data'])
            self.report['events'][0]['elements'] = [bad]
            self.assertEqual(self.run_review()['diagnostic'], 'unresolved')
        for bad in (element('Int', 0, True), element('Int', 0, 2147483647, increment=True),
                    element('Float', 0, 1e100), element(index=9999)):
            if bad['type'].endswith('Int') and bad['data']['Increment']:
                bad['data']['NewValue'] = -2147483648  # -2 plus this underflows int32.
            self.report['events'][0]['elements'] = [bad]
            self.assertEqual(self.run_review()['diagnostic'], 'unresolved')

    def test_multiple_writes_are_not_ordered_or_silently_collapsed(self):
        self.report['events'][0]['elements'] = [element(), element(value=False)]
        result = self.run_review()
        self.assertEqual(result['diagnostic'], 'unresolved')
        self.assertEqual(len(result['directEffects']), 2)
        self.assertTrue(all(e['comparison'] == 'unresolved-multiple-writes' for e in result['directEffects']))

    def test_distinct_selected_events_and_qualified_duplicate_ids(self):
        second = {**self.choice, 'mapPath': 'Test.SecondMap', 'mapExportIndex': 4}
        self.report['packages'][0]['maps'].append({'path': second['mapPath'], 'exportIndex': second['mapExportIndex']})
        self.report['events'].append({**second, 'packageFile': 'UNIT-TEST.pcc', 'elements': [element(index=2)]})
        self.report['duplicateIds'] = [7]
        self.choices.append(second)
        self.edit({'plot.booleans.0': True, 'plot.booleans.2': True})
        result = self.run_review()
        self.assertEqual(result['diagnostic'], 'direct-values-consistent')
        self.assertEqual(len(result['selectedSources']), 2)
        self.assertEqual(len(result['directEffects']), 2)
        self.assertEqual(result['inputPaths']['before'], str(self.before.resolve()))

    def test_other_plot_journal_world_changes_retained(self):
        self.edit({'plot.booleans.0': True, 'plot.booleans.2': True, 'location.x': 9, 'player.credits': 333})
        save = review.SAVE.Save.load(self.after)
        raw = bytearray(save.raw[:-4])
        offset = save.spans['journal.quest_progress_counter'][0]
        struct.pack_into('<i', raw, offset, 2)
        self.after.write_bytes(fixtures.with_crc(raw))
        result = self.run_review()
        self.assertEqual(result['semanticChangeCount'], 5)
        self.assertEqual([len(result['changeGroups'][key]) for key in ('selectedDirectFields', 'otherPlot', 'journal', 'world', 'other')], [1] * 5)
        self.assertFalse(result['storyValidityEstablished'])

    def test_hash_binding_bad_crc_and_duplicate_keys_rejected(self):
        self.bind()
        for key in ('beforeSha256', 'afterSha256', 'sourceReportSha256'):
            selection = copy.deepcopy(self.selection)
            selection[key] = 'f' * 64
            self.plan.write_text(json.dumps(selection))
            with self.assertRaisesRegex(ValueError, key):
                review.review(self.before, self.after, self.source, self.plan)
        self.plan.write_text('{"schemaVersion":1,"schemaVersion":1}')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            review.review(self.before, self.after, self.source, self.plan)
        self.bind()
        self.after.write_bytes(self.after.read_bytes()[:-1] + b'X')
        with self.assertRaisesRegex(ValueError, 'CRC'):
            review.review(self.before, self.after, self.source, self.plan)

    def test_missing_ambiguous_source_or_inventory_is_unresolved(self):
        original = copy.deepcopy(self.report)
        for mutate in (lambda s: s['events'].clear(), lambda s: s['events'].append(copy.deepcopy(s['events'][0])),
                       lambda s: s['packages'].clear(), lambda s: s.update(scopedInspectionComplete=False),
                       lambda s: s.update(errors=[{'error': 'partial-read'}]), lambda s: s.update(requestedIds=[]),
                       lambda s: s['scope'].update(mountedCompleteness=True), lambda s: s['events'][0].update(id=True)):
            self.report = copy.deepcopy(original)
            mutate(self.report)
            self.assertEqual(self.run_review()['diagnostic'], 'unresolved')

    def test_cli_never_overwrites_inputs_or_existing_report(self):
        self.bind()
        original = self.before.read_bytes()
        args = [sys.executable, str(HERE / 'story-review.py'), str(self.before), str(self.after),
                '--source-report', str(self.source), '--selection', str(self.plan), '--output']
        result = subprocess.run(args + [str(self.before)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.before.read_bytes(), original)
        output = self.root / 'result.json'
        result = subprocess.run(args + [str(output)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)  # Desired true bit is absent in unchanged synthetic save.
        first = output.read_bytes()
        result = subprocess.run(args + [str(output)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(output.read_bytes(), first)

    @unittest.skipUnless(os.getenv('LE2_STORY_SAVE') and os.getenv('LE2_STORY_SOURCE_REPORT'),
                         'optional private save and actual scoped event report not supplied')
    def test_optional_real_source_unchanged_save_is_not_native_proof(self):
        path = Path(os.environ['LE2_STORY_SAVE'])
        source = Path(os.environ['LE2_STORY_SOURCE_REPORT'])
        raw = path.read_bytes()
        self.before.write_bytes(raw)
        self.after.write_bytes(raw)
        self.report = json.loads(source.read_text(encoding='utf-8-sig'))
        event = next(e for e in self.report['events'] if e['id'] == 4551)
        self.choices = [{key: event[key] for key in ('id', 'packageSha256', 'mapPath', 'mapExportIndex')}]
        self.bind()
        # Preserve the real scanner report's exact bytes rather than reserializing its JSON.
        self.source.write_bytes(source.read_bytes())
        self.selection['sourceReportSha256'] = review.sha(self.source.read_bytes())
        self.plan.write_text(json.dumps(self.selection), encoding='utf-8')
        result = review.review(self.before, self.after, self.source, self.plan)
        self.assertEqual({e['path'] for e in result['directEffects']}, {'plot.booleans.4419', 'plot.booleans.5679'})
        self.assertTrue(all(e['comparison'] in ('missing-or-different-effect', 'already-at-expected-value') for e in result['directEffects']))
        self.assertTrue(result['byteIdenticalSaves'])
        self.assertFalse(result['eventFiredEstablished'])
        self.assertEqual(path.read_bytes(), raw)


if __name__ == '__main__':
    unittest.main()
