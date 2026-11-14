# SPDX-License-Identifier: MIT
"""Offline adversarial checks. Synthetic observations are never native evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('scenario', HERE / 'scenario.py')
scenario = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(scenario)


class ScenarioTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / 'scenario.json'
        self.evidence_path = self.root / 'evidence.json'
        self.recipe_path = self.root / 'recipe.json'
        self.base = self.root / 'synthetic-save.bin'
        self.base.write_bytes(b'UNIT TEST ONLY: not a game save')
        self.recipe = {'schema_version': 1, 'id': 'test-noop',
                       'expected_input_sha256': scenario.sha(self.base), 'expect': {}, 'set': {}}
        self.write(self.recipe_path, self.recipe)
        self.s = scenario.load(HERE / 'scenarios' / 'reload.json')
        self.s.update(status='ready', recipe={'path': 'recipe.json', 'sha256': scenario.sha(self.recipe_path)},
                      expectedStart={'map': 'SYNTHETIC_TEST', 'checks': [
                          {'field': 'resources.probes', 'op': 'eq', 'value': 1}]})
        self.s['fixture']['baseSaveSha256'] = scenario.sha(self.base)
        self.s['variants'] = ['empty', 'no-reserve']
        self.s['assertions'] = [
            {'id': 'cost', 'gate': 'native', 'description': 'One probe consumed',
             'requires': ['before', 'after'], 'check': {'kind': 'delta',
                 'before': {'artifact': 'before', 'field': 'state.resources.probes'},
                 'after': {'artifact': 'after', 'field': 'state.resources.probes'}, 'expected': -1}},
            {'id': 'controller', 'gate': 'vr', 'description': 'Observed player controls',
             'requires': ['input', 'scene'], 'check': {'kind': 'review'}},
            {'id': 'comfort', 'gate': 'headset', 'description': 'Physical comfort',
             'requires': ['input', 'scene'], 'check': {'kind': 'review'}}]
        self.write(self.path, self.s)
        self.identity = {'runId': 'UNIT-TEST', 'caseId': 'empty',
                         'scenarioSha256': scenario.sha(self.path),
                         'baseSaveSha256': scenario.sha(self.base),
                         'recipeSha256': scenario.sha(self.recipe_path),
                         'preparedSaveSha256': scenario.sha(self.base),
                         'candidateCommit': 'UNIT-TEST', 'gameVersion': 'UNIT-TEST',
                         'gameExeSha256': 'a' * 64, 'loadedModSha256': 'b' * 64}
        self.e = {'schemaVersion': 1, 'identity': self.identity, 'caseId': 'empty',
                  'mode': 'vr', 'headset': 'physical', 'artifacts': {}, 'reviews': {}}
        for name, probes in [('start', 1), ('before', 1), ('after', 0)]:
            self.artifact(name, {'identity': self.identity, 'source': 'runtime-observation',
                                'state': {'map': 'SYNTHETIC_TEST', 'resources': {'probes': probes}}})
        self.artifact('input', {'identity': self.identity, 'inputPath': 'vr-controller',
                                'events': [{'action': 'probe', 'timeSeconds': 1}]})
        self.artifact('scene', b'UNIT TEST ONLY: not video evidence')
        for aid in ('controller', 'comfort'):
            self.e['reviews'][aid] = self.review()

    def write(self, path, value):
        path.write_text(json.dumps(value), encoding='utf-8')

    def artifact(self, slot, value):
        path = self.root / (slot + '.json' if isinstance(value, dict) else slot + '.bin')
        if isinstance(value, dict):
            self.write(path, value)
        else:
            path.write_bytes(value)
        self.e['artifacts'][slot] = {'path': path.name, 'sha256': scenario.sha(path),
                                     'kind': self.s['artifacts'][slot]}

    def review(self):
        return {'reviewer': 'UNIT-TEST ONLY', 'observedAtUtc': '2026-01-01T00:00:00Z',
                'notes': 'Synthetic validator test, no real acceptance.', 'verdict': 'supported',
                'artifactHashes': {x: self.e['artifacts'][x]['sha256'] for x in ('input', 'scene')}}

    def audit(self):
        self.write(self.evidence_path, self.e)
        return scenario.audit(self.path, self.evidence_path)

    def test_catalog_is_draft_and_cannot_execute(self):
        paths = list((HERE / 'scenarios').glob('*.json'))
        self.assertEqual(len(paths), 3)
        for path in paths:
            s = scenario.load(path)
            self.assertEqual(s['status'], 'draft')
            self.assertIsNone(s['fixture']['baseSaveSha256'])
            with self.assertRaisesRegex(ValueError, 'draft'):
                scenario.load(path, for_run=True)

    def test_dialogue_prompt_requires_matching_entry_reply_and_capture_origin(self):
        config = scenario.load(HERE / 'scenarios' / 'dialogue-interrupt.json')
        check = next(a['check'] for a in config['assertions'] if a['id'] == 'veetor-prompt-sampled')
        artifact = {'provenance': {'observationVerb': 'stop-dialogue-history', 'historyOrigin': {'available': True}}}
        history = {'started': True, 'active': False, 'reason': 'stopped', 'dictionaryOmitted': False,
                   'epochs': [{'epoch': 1, 'conversation': {'name': check['conversation']}}],
                   'rows': [{'status': 'stable-sample', 'epoch': 1, 'currentEntry': {'exportId': 210141},
                             'interruptReply': {'exportId': 210192}, 'interrupt': {'type': 1},
                             'gui': {'paragonDisplayed': True}}]}
        scenario.check_dialogue_prompt_sample(artifact, history, check, 'paragon-window')
        scenario.check_dialogue_prompt_sample(artifact, history, check, 'missed-window')
        with self.assertRaisesRegex(ValueError, 'not sampled together'):
            scenario.check_dialogue_prompt_sample(artifact, history, check, 'renegade-window')
        for field, value in (('currentEntry', {'exportId': 210145}), ('interruptReply', {'exportId': 210197}),
                             ('gui', {'wasTriggered': True}), ('epoch', 2)):
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'not sampled together'):
                row = dict(history['rows'][0], **{field: value})
                scenario.check_dialogue_prompt_sample(artifact, dict(history, rows=[row]), check, 'paragon-window')
        with self.assertRaisesRegex(ValueError, 'not sampled together'):
            wrong = dict(history, epochs=[{'epoch': 1, 'conversation': {'name': 'another_conversation'}}])
            scenario.check_dialogue_prompt_sample(artifact, wrong, check, 'paragon-window')
        with self.assertRaisesRegex(ValueError, 'finished capture'):
            scenario.check_dialogue_prompt_sample(artifact, dict(history, active=True), check, 'paragon-window')
        artifact['provenance']['historyOrigin']['available'] = False
        with self.assertRaisesRegex(ValueError, 'archived recorder-start'):
            scenario.check_dialogue_prompt_sample(artifact, history, check, 'paragon-window')

    def test_orbital_probe_requires_same_planet_and_settled_endpoints(self):
        orbital = {'currentMode': 10, 'activeWorld': 42, 'scanning': True,
                   'planet': {'identity': {'class': 'Synthetic.Planet', 'index': 12}, 'tableId': 99},
                   'probeActive': False, 'impactActive': False, 'rescaling': False, 'exiting': False}
        before = {'orbital': orbital, 'resources': {'probes': 1}}
        after = {'orbital': dict(orbital, scanning=False), 'resources': {'probes': 0}}
        scenario.check_orbital_probe_samples(before, after, 'one-probe')
        for field, value in (('impactActive', True), ('probeActive', True), ('activeWorld', 43),
                             ('planet', dict(orbital['planet'], identity={'class': 'Synthetic.Planet', 'index': 13}))):
            with self.subTest(field=field), self.assertRaises(ValueError):
                scenario.check_orbital_probe_samples(before, dict(after, orbital=dict(after['orbital'], **{field: value})), 'one-probe')
        empty = dict(before, resources={'probes': 0})
        scenario.check_orbital_probe_samples(empty, after, 'zero-probes')
        idle = dict(before, orbital=dict(orbital, scanning=False))
        scenario.check_orbital_probe_samples(idle, before, 'scan-hold')
        with self.assertRaisesRegex(ValueError, 'probe cost'):
            scenario.check_orbital_probe_samples(before, before, 'one-probe')

    def test_reload_checks_exact_transfer_and_weapon_identity(self):
        check = next(a for a in scenario.load(HERE / 'scenarios' / 'reload.json')['assertions']
                     if a['id'] == 'finite-reload-transfer')
        check['variants'] = ['empty', 'no-reserve']
        self.s['assertions'] = [check]
        self.write(self.path, self.s)
        self.identity['scenarioSha256'] = scenario.sha(self.path)
        for slot in ('start', 'input'):
            artifact = scenario.read_json(self.root / (slot + '.json'))
            self.artifact(slot, dict(artifact, identity=self.identity))
        before = {'identity': {'class': 'Synthetic.Pistol', 'index': 7, 'name': 'Pistol',
                               'nameNumber': 1, 'heavyWeaponClass': False},
                  'magazineCapacity': 12, 'magazineUsed': 12, 'loaded': 0,
                  'effectiveReserve': 5, 'infiniteAmmo': False}
        def sample(slot, weapon):
            self.artifact(slot, {'identity': self.identity, 'source': 'runtime-observation',
                                 'state': {'weapon': weapon}})
        sample('before', before)
        after = dict(before, magazineUsed=7, loaded=5, effectiveReserve=0)
        sample('after', after)
        self.assertEqual(self.audit()['gates']['native'], 'supported')
        for invalid in (
                dict(after, magazineUsed=8, loaded=4, effectiveReserve=1),
                dict(after, identity=dict(after['identity'], nameNumber=2)),
                dict(after, infiniteAmmo=True),
                dict(after, magazineUsed=7.5, loaded=4.5),
        ):
            sample('after', invalid)
            self.assertEqual(self.audit()['gates']['native'], 'failed')
        with self.assertRaisesRegex(ValueError, 'starting counts'):
            scenario.check_reload_transfer(before, after, 'exact-reserve')
        no_reserve = dict(before, effectiveReserve=0)
        scenario.check_reload_transfer(no_reserve, no_reserve, 'no-reserve')

    def test_ready_is_binding_not_acceptance(self):
        self.assertEqual(scenario.validate(self.s, True), [])
        result = self.audit()
        self.assertEqual(result['gates']['native'], 'supported')
        self.assertEqual(result['gates']['headset'], 'supported')
        self.assertEqual(result['gates']['campaign'], 'not-covered')
        self.assertFalse(result['fullPortAccepted'])

    def test_recipe_tamper_and_wrong_base_fail(self):
        self.recipe['expect'] = {'player.probes': 3}
        self.write(self.recipe_path, self.recipe)
        with self.assertRaisesRegex(ValueError, 'recipe hash'):
            self.audit()
        self.s['recipe']['sha256'] = scenario.sha(self.recipe_path)
        self.s['fixture']['baseSaveSha256'] = 'c' * 64
        self.write(self.path, self.s)
        with self.assertRaisesRegex(ValueError, 'input hash'):
            scenario.load(self.path, True)

    def test_frozen_recipe_override(self):
        copied = self.root / 'frozen'
        copied.mkdir()
        (copied / 'scenario.json').write_bytes(self.path.read_bytes())
        with self.assertRaises(OSError):
            scenario.load(copied / 'scenario.json', True)
        self.assertEqual(scenario.load(copied / 'scenario.json', True, self.recipe_path)['status'], 'ready')

    def test_wrong_run_identity_refused(self):
        value = scenario.read_json(self.root / 'after.json')
        value['identity']['runId'] = 'STALE-RUN'
        self.artifact('after', value)
        result = self.audit()
        self.assertIn('another run', result['artifactProblems']['after'])
        self.assertEqual(result['gates']['native'], 'pending')

    def test_reload_start_can_use_separate_run_bound_weapon_observation(self):
        self.s['artifacts']['start-weapon'] = 'runtime-state'
        self.s['expectedStart']['checks'].append({
            'artifact': 'start-weapon', 'field': 'weapon.loaded', 'op': 'eq', 'value': 0})
        self.write(self.path, self.s)
        self.identity['scenarioSha256'] = scenario.sha(self.path)
        for name in ('start', 'before', 'after', 'input'):
            sample = scenario.read_json(self.root / (name + '.json'))
            sample['identity'] = self.identity
            self.artifact(name, sample)
        weapon = {'identity': self.identity, 'source': 'runtime-observation',
                  'state': {'weapon': {'loaded': 0}}}
        self.assertTrue(self.audit()['startProblems'])
        self.artifact('start-weapon', weapon)
        self.assertEqual(self.audit()['gates']['native'], 'supported')
        cli = [sys.executable, str(HERE / 'scenario.py'), 'check-start', str(self.path),
               str(self.root / 'start.json'), '--observation', 'start-weapon', str(self.root / 'start-weapon.json')]
        checked = subprocess.run(cli, capture_output=True, text=True, check=True)
        self.assertTrue(json.loads(checked.stdout)['startingStateMatches'])
        weapon['state']['weapon']['loaded'] = 1
        self.artifact('start-weapon', weapon)
        self.assertEqual(self.audit()['startProblems'], ['starting check failed: weapon.loaded'])
        weapon['state']['weapon']['loaded'] = 0
        weapon['identity'] = dict(self.identity, runId='STALE-RUN')
        self.artifact('start-weapon', weapon)
        self.assertIn('another run', self.audit()['artifactProblems']['start-weapon'])
        self.assertTrue(self.audit()['startProblems'])
        self.s['expectedStart']['checks'][-1]['artifact'] = 'input'
        self.assertIn('starting-state check must reference a runtime-state artifact', scenario.validate(self.s))

    def test_wrong_scenario_or_fixture_identity_refused(self):
        for key in ('scenarioSha256', 'baseSaveSha256', 'recipeSha256'):
            original = self.identity[key]
            self.identity[key] = 'f' * 64
            with self.assertRaises(ValueError):
                self.audit()
            self.identity[key] = original

    def test_path_escape_and_mutated_bytes_refused(self):
        self.e['artifacts']['after']['path'] = '../outside.json'
        result = self.audit()
        self.assertIn('escapes', result['artifactProblems']['after'])
        self.e['artifacts']['after']['path'] = 'after.json'
        (self.root / 'after.json').write_text('{}')
        self.assertIn('bytes changed', self.audit()['artifactProblems']['after'])

    def test_actual_values_drive_result_not_pass_boolean(self):
        value = scenario.read_json(self.root / 'after.json')
        value['state']['resources']['probes'] = 5
        value['passed'] = True
        self.artifact('after', value)
        self.assertEqual(self.audit()['gates']['native'], 'failed')

    def test_boolean_cannot_replace_numeric_state(self):
        value = scenario.read_json(self.root / 'after.json')
        value['state']['resources']['probes'] = False
        self.artifact('after', value)
        self.assertEqual(self.audit()['gates']['native'], 'failed')
        self.assertFalse(scenario.compare(True, 'eq', 1))

    def test_native_input_cannot_pass_vr(self):
        self.e['mode'] = 'native-baseline'
        result = self.audit()
        self.assertEqual(result['gates']['native'], 'supported')
        self.assertEqual(result['gates']['vr'], 'pending')
        self.e['mode'] = 'vr'
        value = scenario.read_json(self.root / 'input.json')
        value['inputPath'] = 'native'
        self.artifact('input', value)
        self.assertEqual(self.audit()['gates']['vr'], 'failed')

    def test_synthetic_headset_cannot_pass_comfort(self):
        self.e['headset'] = 'synthetic'
        result = self.audit()
        self.assertEqual(result['gates']['vr'], 'supported')
        self.assertEqual(result['gates']['headset'], 'pending')

    def test_bare_pass_and_unbound_review_are_pending(self):
        self.e['reviews']['controller'] = True
        self.assertEqual(self.audit()['gates']['vr'], 'pending')
        review = self.review()
        review['artifactHashes']['scene'] = 'f' * 64
        self.e['reviews']['controller'] = review
        self.assertEqual(self.audit()['gates']['vr'], 'pending')

    def test_missing_artifact_or_start_never_passes(self):
        del self.e['artifacts']['scene']
        self.assertEqual(self.audit()['gates']['vr'], 'pending')
        del self.e['artifacts']['start']
        self.assertEqual(self.audit()['gates']['native'], 'pending')

    def test_start_requires_live_source_map_and_values(self):
        state = {'source': 'save-inspection', 'state': {'map': 'WRONG', 'resources': {'probes': 0}}}
        self.assertEqual(len(scenario.check_start(self.s, state)), 3)
        self.artifact('start', state)
        self.assertEqual(self.audit()['gates']['native'], 'pending')
        self.assertTrue(scenario.check_start(self.s, {'state': []}))

    def test_variant_scopes_assertions_and_binds_identity(self):
        self.s['assertions'][0]['variants'] = ['no-reserve']
        self.write(self.path, self.s)
        self.identity['scenarioSha256'] = scenario.sha(self.path)
        # Rewrite identity on all structured observations after changing scenario.
        for slot in ('start', 'before', 'after', 'input'):
            value = scenario.read_json(self.root / (slot + '.json'))
            value['identity'] = self.identity
            self.artifact(slot, value)
        for aid in ('controller', 'comfort'):
            self.e['reviews'][aid] = self.review()
        result = self.audit()
        self.assertEqual(result['gates']['native'], 'not-covered')
        self.e['caseId'] = 'no-reserve'
        with self.assertRaisesRegex(ValueError, 'caseId'):
            self.audit()

    def test_schema_refuses_invalid_references_and_claims(self):
        for mutation in (
                lambda s: s.update(status='validated'),
                lambda s: s.update(schemaVersion=True),
                lambda s: s.update(variants=None),
                lambda s: s['assertions'][0]['check']['before'].update(artifact='missing'),
                lambda s: s['artifacts'].update(start='save-inspection'),
                lambda s: s['assertions'][1].update(requires=['scene'])):
            s = copy.deepcopy(self.s)
            mutation(s)
            self.assertTrue(scenario.validate(s))

    def test_duplicate_keys_and_nonfinite_json_refused(self):
        for value in ('{"a": 1, "a": 2}', '{"a": NaN}', '{"a": Infinity}', '{"a": 1e999}'):
            self.evidence_path.write_text(value)
            with self.assertRaises(ValueError):
                scenario.read_json(self.evidence_path)


if __name__ == '__main__':
    unittest.main()
