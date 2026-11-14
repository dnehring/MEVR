# SPDX-License-Identifier: MIT
"""Temporary synthetic archives only; never launch or read a live game."""
import contextlib
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('export_state', Path(__file__).with_name('export-state.py'))
e = importlib.util.module_from_spec(spec)
spec.loader.exec_module(e)


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.run = self.root / 'run'
        self.run.mkdir()
        self.token = '33333333-3333-4333-8333-333333333333'
        self.wire = dict(run='11111111-1111-4111-8111-111111111111', lease='22222222-2222-4222-8222-222222222222',
                         pid='1234', start=e.control.filetime('2026-11-14T07:00:00.1234567Z'),
                         exe='a' * 64, host='b' * 64, mod='b' * 64, game='LE2')
        profile, game = self.root / 'profile', self.root / 'game'
        exe = game / 'Game/ME2/Binaries/Win64/MassEffect2.exe'
        recipe = dict(schema_version=1, id='synthetic-export', expected_input_sha256='c' * 64,
                      expect={'player.probes': 1}, set={'player.probes': 2})
        self.put('recipe.json', recipe)
        scenario = e.scenario.read_json(Path(__file__).with_name('scenarios') / 'planet-scanning.json')
        scenario.update(status='ready', recipe=dict(path='recipe.json', sha256=e.session.digest(self.run / 'recipe.json')),
                        expectedStart=dict(map='BioP_Nor', checks=[dict(field='resources.probes', op='eq', value=2)]))
        scenario['fixture']['baseSaveSha256'] = 'c' * 64
        self.case = scenario['variants'][0]
        self.put('scenario.json', scenario)
        (self.run / 'fixture.pcsav').write_bytes(b'FAKE SAVE NEVER LOADED')
        self.put('preparation-receipt.json', dict(input_sha256='c' * 64, output_sha256=e.session.digest(self.run / 'fixture.pcsav'),
                 recipe_sha256=e.session.digest(self.run / 'recipe.json')))
        self.plan = dict(profileRoot=str(profile), gameRoot=str(game), exeSha256=self.wire['exe'],
                         fixture=dict(sha256=e.session.digest(self.run / 'fixture.pcsav')),
                         scenario=dict(sha256=e.session.digest(self.run / 'scenario.json')))
        self.put('plan.json', self.plan)
        self.context = dict(schemaVersion=1, runDirectory=str(self.run), runId=self.wire['run'], backupComplete=True,
                            profileRoot=str(profile), gameRoot=str(game), exePath=str(exe), exeSha256=self.wire['exe'],
                            lockPath=str(self.root / 'locks/qa-cafeteria.lock'), phase='restored', restorationVerified=True,
                            gameProcess=dict(pid=1234, name='MassEffect2', path=str(exe), started='2026-11-14T07:00:00.1234567Z'),
                            protected=[dict(path=str(p)) for p in (profile, game / 'Game/ME2/BioGame/Config', game / 'Game/ME2/Binaries/Win64')])
        for filename, key in [('plan.json', 'planSha256'), ('scenario.json', 'scenarioSha256'),
                              ('recipe.json', 'recipeSha256'), ('fixture.pcsav', 'fixtureSha256'),
                              ('preparation-receipt.json', 'preparationReceiptSha256')]:
            self.context[key] = e.session.digest(self.run / filename)
        self.put('context.json', self.context)
        self.put('deployment.json', dict(candidateCommit='d' * 40, asiSha256='b' * 64))
        self.put('control/identity.json', dict(schemaVersion=1, source='diagnostic-host', ready=False, identity=self.wire))
        resources = {}
        for key, field in e.FIELDS.items():
            value = 50.25 if key == 'fuel' else 2
            bits = struct.unpack('<I', struct.pack('<f' if key == 'fuel' else '<i', value))[0]
            resources[key] = dict(available=True, reason='stable-direct-live-inventory-field', value=value,
                                  sourceField='SFXGame.SFXInventoryManager.' + field,
                                  scalarType='float32' if key == 'fuel' else 'int32', sampleBits=f'0x{bits:08X}')
        self.payload = dict(source='diagnostic-host', game='LE2', readOnly=True, ready=False,
                            capabilities=dict(observeResources=True), gameTick='123', gameThreadId='88', sampleMonotonicMs='456',
                            resourceObservation=dict(schemaVersion=1, game='LE2', validationScope='direct-live-inventory-POD-sample',
                            dispatchCapability=False, saveCacheUsed=False, persistenceProven=False, durableObjectIdentity=False,
                            allResourcesAvailable=True, context=dict(available=True, map=dict(available=True, value='BioP_Nor'),
                            position=dict(available=True, x=1.0, y=2.0, z=3.0)), resources=resources))
        self.request = dict(self.wire, kind='request', id=self.token, sequence='1', deadline='999999',
                            verb='observe-resources', status='pending', bytes='0', payload=b'')
        self.archive()

    def put(self, name, value):
        path = self.run / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding='utf-8')

    def archive(self, **changes):
        data = json.dumps(self.payload, allow_nan=True).encode()
        self.result = dict(self.request, kind='result', status='completed', payload=data, bytes=str(len(data)))
        self.result.update(changes)
        for name, frame in [(f'control/history/{self.token}.request', self.request),
                            (f'control/history/{self.token}.result', self.result), ('control/consumed-1.request', self.request)]:
            path = self.run / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(e.control.encode(frame))

    def invoke(self):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return e.main([str(self.run), '--request', self.token, '--case', self.case])

    def state(self):
        return e.read(self.run / f'artifacts/state-{self.token}.json')

    def use_dialogue(self):
        self.request['verb'] = 'observe-dialogue'
        self.payload.pop('resourceObservation', None)
        self.payload['capabilities'] = {'observeDialogue': True}
        def identity(index):
            return dict(available=True, reason='sdk-structural-checks-passed', index=index,
                        address=f'0x{0x1000+index:X}', classAddress='0x2000', **{'class': 'Synthetic.DialogueObject'})
        absent = dict(available=False, index=-1, reason='no-node-minus-one')
        node = dict(available=True, index=0, reason='bounded-selected-node-fields', exportId=-9,
                    stateTransition=21, stateTransitionParameter=-3)
        context = {key: identity(index) for index, key in enumerate(e.DIALOGUE_IDENTITIES)}
        context['mapContainmentProven'] = False
        conversation = dict(identity(7), name='synthetic_conversation', package=dict(identity(11), name='SyntheticPackage'),
                            entryCount=2, replyCount=1, currentEntry=node,
                            currentReply=absent, selectedReply=absent, nodePlayTimer=-0.25)
        gui = dict(available=True, reason='reachable-cached-handler-display-state-only',
                   manager=identity(8), panel=identity(9), handler=identity(10),
                   **{key: False for key in e.DIALOGUE_GUI_FLAGS})
        observation = dict(schemaVersion=1, game='LE2', available=True,
            reason='stable-current-conversation-fields-only', context=context, conversation=conversation,
            interrupt=dict(replyListIndex=0, reply=copy.deepcopy(node), startTimeRemaining=-1.5, windowTimeRemaining=0.25,
                           enabled=True, activated=True, type=1, typeName='paragon'), gui=gui,
            **{key: False for key in e.DIALOGUE_LIMITS})
        self.payload['dialogueObservation'] = observation
        self.archive()
        return observation

    def test_dialogue_export_retains_one_sample_without_acceptance_or_map(self):
        observation = self.use_dialogue()
        self.assertEqual(self.invoke(), 2)
        artifact = self.state()
        dialogue = artifact['state']['dialogue']
        self.assertNotIn('map', artifact['state'])
        self.assertNotIn('position', artifact['state'])
        self.assertNotIn('resources', artifact['state'])
        self.assertEqual(dialogue['context']['world'], {'class': 'Synthetic.DialogueObject', 'index': 0})
        self.assertEqual(dialogue['conversation']['currentReply'], observation['conversation']['currentReply'])
        self.assertEqual(dialogue['conversation']['nodePlayTimer'], -0.25)
        self.assertEqual(dialogue['conversation']['name'], 'synthetic_conversation')
        self.assertEqual(dialogue['conversation']['package']['name'], 'SyntheticPackage')
        self.assertNotIn('address', dialogue['conversation']['package']['identity'])
        self.assertTrue(dialogue['interrupt']['activated'])
        self.assertFalse(dialogue['gui']['visible'])  # Reporting is not interactive admission.
        self.assertEqual(artifact['provenance']['runtimeObservation'], observation)
        self.assertEqual(artifact['provenance']['observationVerb'], 'observe-dialogue')
        self.assertFalse(artifact['provenance']['combinedObservation'])
        self.assertTrue(all(value is False for value in artifact['claims'].values()))
        self.assertFalse(any(key in dialogue for key in ('accepted', 'legal', 'selected', 'history')))
        self.assertEqual(self.invoke(), 1)  # Existing state/report never replaced.

    def test_dialogue_optional_pawn_gui_and_absent_nodes_preserved(self):
        observation = self.use_dialogue()
        observation['context']['pawn'] = dict(available=False, reason='null-object')
        observation['gui'] = dict(available=False, reason='dialogue-cached-handler-unavailable')
        absent = dict(available=False, index=-1, reason='no-node-minus-one')
        observation['conversation'].update(entryCount=0, replyCount=0, currentEntry=absent,
                                           currentReply=absent, selectedReply=absent)
        observation['interrupt'].update(replyListIndex=-1, reply=absent, type=255, typeName='unknown')
        state, unavailable = e.normalize_dialogue(self.payload)
        self.assertNotIn('pawn', state['dialogue']['context'])
        self.assertNotIn('gui', state['dialogue'])
        self.assertIn('dialogue.context.pawn', unavailable)
        self.assertIn('dialogue.gui', unavailable)
        self.assertEqual(state['dialogue']['interrupt']['type'], 255)
        self.assertEqual(state['dialogue']['conversation']['currentEntry'], absent)
        self.assertNotIn('exportId', state['dialogue']['conversation']['currentEntry'])
        for target in (observation['context']['pawn'], observation['gui']):
            target['index'] = 0
            with self.assertRaises(ValueError): e.normalize_dialogue(self.payload)
            del target['index']

    def test_dialogue_unavailable_never_exports_partial_fields(self):
        self.use_dialogue()
        observation = dict(schemaVersion=1, game='LE2', available=False, reason='no-current-conversation',
                           **{key: False for key in e.DIALOGUE_LIMITS})
        self.payload['dialogueObservation'] = observation
        self.archive()
        self.assertEqual(self.invoke(), 2)
        self.assertEqual(self.state()['state'], {'dialogue': {}})
        self.assertEqual(self.state()['unavailable']['dialogue']['sample'], observation)
        for key in ('context', 'conversation', 'interrupt', 'gui'):
            observation[key] = {}
            with self.assertRaises(ValueError): e.normalize_dialogue(self.payload)
            del observation[key]

    def test_dialogue_rejects_type_bounds_identity_and_inferred_claims(self):
        baseline = copy.deepcopy(self.use_dialogue())
        cases = [
            (('schemaVersion',), True), (('available',), 1), (('reason',), 'accepted-interrupt'),
            (('context', 'mapContainmentProven'), True), (('context', 'world', 'index'), True),
            (('context', 'engine', 'available'), False), (('context', 'worldInfo', 'address'), '0x0'),
            (('conversation', 'replyCount'), 4097), (('conversation', 'entryCount'), -1),
            (('conversation', 'nodePlayTimer'), float('inf')), (('conversation', 'nodePlayTimer'), True),
            (('conversation', 'nodePlayTimer'), 0.1), (('interrupt', 'enabled'), 1),
            (('interrupt', 'activated'), 'true'), (('interrupt', 'type'), True),
            (('interrupt', 'type'), 256), (('interrupt', 'typeName'), 'renegade'),
            (('interrupt', 'replyListIndex'), -1), (('interrupt', 'startTimeRemaining'), float('nan')),
            (('gui', 'reason'), 'interactive'), (('gui', 'wasTriggered'), 0),
            (('gui', 'panel', 'classAddress'), '0x0'),
        ] + [((key,), True) for key in e.DIALOGUE_LIMITS]
        for path, value in cases:
            observation = copy.deepcopy(baseline)
            target = observation
            for key in path[:-1]: target = target[key]
            target[path[-1]] = value
            self.payload['dialogueObservation'] = observation
            with self.subTest(path=path, value=value), self.assertRaises(ValueError): e.normalize_dialogue(self.payload)

    def test_dialogue_node_sentinels_selected_bounds_and_raw_metadata(self):
        baseline = copy.deepcopy(self.use_dialogue())
        for node_name, key, value in (
                ('currentReply', 'index', 0), ('currentReply', 'exportId', 1),
                ('currentReply', 'available', True), ('currentEntry', 'index', -1),
                ('currentEntry', 'index', 2), ('currentEntry', 'index', True),
                ('currentEntry', 'exportId', 2**31), ('currentEntry', 'stateTransition', False),
                ('currentEntry', 'stateTransitionParameter', -2**31-1), ('currentEntry', 'reason', 'accepted')):
            observation = copy.deepcopy(baseline)
            observation['conversation'][node_name][key] = value
            self.payload['dialogueObservation'] = observation
            with self.subTest(node=node_name, key=key), self.assertRaises(ValueError): e.normalize_dialogue(self.payload)
        self.payload['dialogueObservation'] = baseline
        baseline['conversation']['entryCount'] = 4096
        baseline['conversation']['currentEntry']['index'] = 4095
        state, _ = e.normalize_dialogue(self.payload)
        self.assertEqual(state['dialogue']['conversation']['currentEntry']['index'], 4095)
        self.assertEqual(state['dialogue']['conversation']['currentEntry']['exportId'], -9)

    def test_dialogue_names_and_optional_direct_package(self):
        observation = self.use_dialogue()
        conversation = observation['conversation']
        for target in (conversation, conversation['package']):
            saved = target['name']
            for value in ('', 'a'*128, 'bad\0name', 'naïve', 'bad\nname', 1):
                target['name'] = value
                with self.subTest(value=value), self.assertRaises(ValueError): e.normalize_dialogue(self.payload)
            target['name'] = 'A'*127
            e.normalize_dialogue(self.payload)
            target['name'] = saved
        conversation['package'] = dict(available=False, reason='not-observed')
        state, unavailable = e.normalize_dialogue(self.payload)
        self.assertNotIn('package', state['dialogue']['conversation'])
        self.assertEqual(unavailable['dialogue.conversation.package']['sample'], conversation['package'])
        conversation['package']['name'] = 'invented'
        with self.assertRaises(ValueError): e.normalize_dialogue(self.payload)

    def test_dialogue_archive_reuses_contract_and_identity_rejections(self):
        self.use_dialogue()
        for changes in ({'status': 'dispatched'}, {'verb': 'observe-weapon'}, {'host': 'c'*64}):
            self.archive(**changes)
            self.assertEqual(self.invoke(), 1)
        self.archive()
        self.payload['readOnly'] = False
        self.archive()
        self.assertEqual(self.invoke(), 1)
        self.payload['readOnly'] = True
        self.archive()
        consumed = self.run / 'control/consumed-1.request'
        saved = consumed.read_bytes()
        consumed.unlink()
        self.assertEqual(self.invoke(), 1)
        consumed.write_bytes(saved)
        (self.run / 'fixture.pcsav').write_bytes(b'changed')
        self.assertEqual(self.invoke(), 1)
        self.assertFalse((self.run / 'artifacts').exists())

    @unittest.skipUnless(os.environ.get('LE2_CONTROL_CPP'), 'set LE2_CONTROL_CPP for actual C++ codec conformance')
    def test_dialogue_actual_cpp_codec_roundtrip(self):
        self.use_dialogue()
        for suffix in ('request', 'result'):
            path = self.run / f'control/history/{self.token}.{suffix}'
            result = subprocess.run([os.environ['LE2_CONTROL_CPP'], str(path)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, path.read_bytes())

    def use_orbital(self):
        resources = copy.deepcopy(self.payload['resourceObservation']['resources'])
        self.request['verb'] = 'observe-orbital'
        self.payload.pop('resourceObservation')
        self.payload['capabilities'] = {'observeOrbital': True}
        def identity(index):
            return dict(available=True, reason='sdk-structural-checks-passed', index=index,
                        address=f'0x{0x1000+index:X}', classAddress='0x2000', **{'class': 'Synthetic.OrbitalObject'})
        context = dict(currentMode=10, activeWorld=200100, cameraSource='GameModeManager2.GameModes[9].GalaxyCam')
        context.update({key: identity(index) for index, key in enumerate(e.ORBITAL_IDENTITIES)})
        feature = dict(index=0, tableId=37, type=1, mineralType=2, scaledAmount=-7,
                       visibleConditional=104, visibleParameter=0, usableConditional=105,
                       usableParameter=1, transition=4551, transitionParameter=0)
        observation = dict(schemaVersion=1, game='LE2', available=True,
            reason='stable-current-orbital-structural-sample', context=context,
            planet=dict(object=identity(9), mapName='BioP_N7Crsh', tableId=500, displayNameRef=123,
                        levelType=0, landConditional=106, source=e.ORBITAL_SOURCE,
                        featuresComplete=True, features=[feature]),
            state=dict(source='current SFXGameModeOrbital fields', scanning=True, probeActive=False,
                       impactActive=False, rescaling=False, exiting=False,
                       probeLaunchTime=1.5, impactTime=-1.0, scanDist=0.25), resources=resources,
            **{key: False for key in e.ORBITAL_LIMITS})
        self.payload['orbitalObservation'] = observation
        self.archive()
        return observation

    def test_orbital_export_preserves_raw_metadata_without_world_or_discovery_claim(self):
        observation = self.use_orbital()
        self.assertEqual(self.invoke(), 2)  # Current-world map/position were never sampled.
        artifact = self.state()
        orbital = artifact['state']['orbital']
        self.assertEqual(orbital['planet']['mapName'], 'BioP_N7Crsh')
        self.assertEqual(orbital['planet']['features'][0]['scaledAmount'], -7)
        self.assertEqual(orbital['planet']['features'][0]['transition'], 4551)
        self.assertEqual(orbital['planet']['identity'], {'class': 'Synthetic.OrbitalObject', 'index': 9})
        self.assertNotIn('address', orbital['context']['world'])
        self.assertEqual(artifact['state']['resources']['probes'], 2)
        self.assertTrue(orbital['scanning'])
        for key in ('map', 'position', 'discovery', 'savedPlanetId'):
            self.assertNotIn(key, artifact['state'])
        self.assertEqual(artifact['provenance']['runtimeObservation'], observation)
        self.assertEqual(artifact['provenance']['observationVerb'], 'observe-orbital')
        self.assertFalse(artifact['provenance']['combinedObservation'])
        self.assertTrue(all(value is False for value in artifact['claims'].values()))
        self.assertEqual(self.invoke(), 1)  # Existing private output never replaced.

    def test_orbital_landing_map_case_empty_and_raw_float_values(self):
        observation = self.use_orbital()
        for name in ('Biop_n7CRsh', '', 'Synthetic_' + '\U0001F680'):
            observation['planet']['mapName'] = name
            state, unavailable = e.normalize_orbital(self.payload)
            self.assertEqual(state['orbital']['planet']['mapName'], name)
            self.assertNotIn('map', state)
            self.assertIn('map', unavailable)
            self.assertEqual(state['orbital']['impactTime'], -1)
        for name in ('Bad\0Map', 'x' * 256, '\ud800'):
            observation['planet']['mapName'] = name
            with self.assertRaises(ValueError): e.normalize_orbital(self.payload)

    def test_orbital_resources_reuse_bits_validation_and_keep_unavailable(self):
        observation = self.use_orbital()
        observation['resources']['probes'].update(available=False, reason='unreadable-changing-or-invalid-resource')
        state, unavailable = e.normalize_orbital(self.payload)
        self.assertNotIn('probes', state['resources'])
        self.assertEqual(unavailable['resources.probes']['sample'], observation['resources']['probes'])
        for key, value in [('sampleBits', '0x00000000'), ('sourceField', 'CachedSave.Fuel'), ('value', True)]:
            baseline = copy.deepcopy(observation['resources']['fuel'])
            observation['resources']['fuel'][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): e.normalize_orbital(self.payload)
            observation['resources']['fuel'] = baseline

    def test_orbital_unavailable_and_exact_budget_shape_do_not_manufacture_state(self):
        self.use_orbital()
        base = dict(schemaVersion=1, game='LE2', available=False, reason='orbital-context-or-state-changed',
                    **{key: False for key in e.ORBITAL_LIMITS})
        self.payload['orbitalObservation'] = base
        state, unavailable = e.normalize_orbital(self.payload)
        self.assertEqual(state, {'resources': {}, 'orbital': {}})
        self.assertEqual(unavailable['orbital']['reason'], base['reason'])
        budget = dict(schemaVersion=1, game='LE2', available=False, dispatchCapability=False,
                      reason='orbital-payload-budget-exceeded')
        self.payload['orbitalObservation'] = budget
        self.archive()
        self.assertEqual(self.invoke(), 2)
        self.assertEqual(self.state()['state'], {'resources': {}, 'orbital': {}})
        budget['resources'] = {}
        with self.assertRaises(ValueError): e.normalize_orbital(self.payload)
        self.payload['orbitalObservation'] = {**base, 'planet': {}}
        with self.assertRaises(ValueError): e.normalize_orbital(self.payload)

    def test_orbital_rejects_wrong_types_sources_identity_and_positive_claims(self):
        original = copy.deepcopy(self.use_orbital())
        mutations = [
            (('context', 'currentMode'), True), (('context', 'currentMode'), 9),
            (('context', 'activeWorld'), -1), (('context', 'cameraSource'), 'arbitrary-camera'),
            (('context', 'world', 'address'), '0x0'), (('context', 'pawn', 'available'), False),
            (('context', 'pawn', 'index'), True), (('context', 'world', 'index'), 64*1024*1024),
            (('planet', 'source'), 'saved-planet'), (('planet', 'tableId'), True),
            (('planet', 'levelType'), 256), (('planet', 'featuresComplete'), False),
            (('state', 'source'), 'cached-orbital'), (('state', 'scanning'), 1),
            (('state', 'scanDist'), float('inf')), (('state', 'probeLaunchTime'), 0.1),
            (('savedPlanetIdVerified',), True), (('discoveryConditionalEvaluated',), True),
            (('persistenceVerified',), True), (('durableObjectIdentity',), True),
        ]
        for path, value in mutations:
            observation = copy.deepcopy(original)
            target = observation
            for key in path[:-1]: target = target[key]
            target[path[-1]] = value
            self.payload['orbitalObservation'] = observation
            with self.subTest(path=path), self.assertRaises(ValueError): e.normalize_orbital(self.payload)

    def test_orbital_feature_bounds_order_types_and_no_discovery_inference(self):
        observation = self.use_orbital()
        feature = observation['planet']['features'][0]
        for rows in ([feature] * 65, [feature, feature], [{**feature, 'index': True}],
                     [{**feature, 'mineralType': -1}], [{**feature, 'transition': 2147483648}],
                     [{**feature, 'discovered': True}]):
            observation['planet']['features'] = rows
            with self.subTest(rows=len(rows)), self.assertRaises(ValueError): e.normalize_orbital(self.payload)
        observation['planet']['features'] = [{**feature, 'index': i} for i in range(64)]
        self.assertEqual(len(e.normalize_orbital(self.payload)[0]['orbital']['planet']['features']), 64)
        observation['planet']['features'] = []
        self.assertEqual(e.normalize_orbital(self.payload)[0]['orbital']['planet']['features'], [])

    def test_orbital_archive_reuses_correlation_frozen_and_consumption_guards(self):
        self.use_orbital()
        self.archive(status='dispatched')
        self.assertEqual(self.invoke(), 1)
        self.archive(verb='observe-resources')
        self.assertEqual(self.invoke(), 1)
        self.archive()
        consumed = self.run / 'control/consumed-1.request'
        saved = consumed.read_bytes()
        consumed.unlink()
        self.assertEqual(self.invoke(), 1)
        consumed.write_bytes(saved)
        fixture = self.run / 'fixture.pcsav'
        fixture.write_bytes(b'changed')
        self.assertEqual(self.invoke(), 1)
        self.assertFalse((self.run / 'artifacts').exists())

    @unittest.skipUnless(os.environ.get('LE2_CONTROL_CPP'), 'set LE2_CONTROL_CPP for actual C++ codec conformance')
    def test_orbital_actual_cpp_codec_roundtrip(self):
        self.use_orbital()
        for suffix in ('request', 'result'):
            path = self.run / f'control/history/{self.token}.{suffix}'
            result = subprocess.run([os.environ['LE2_CONTROL_CPP'], str(path)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, path.read_bytes())

    def use_weapon(self):
        self.request['verb'] = 'observe-weapon'
        self.payload.pop('resourceObservation', None)
        self.payload['capabilities'] = {'observeWeapon': True}
        fields = {}
        for name, (source, scalar) in e.WEAPON_FIELDS.items():
            value = {'magazineCapacity': 12.75, 'magazineUsed': 4.5, 'reserveStored': -7}.get(name, False)
            fields[name] = dict(available=True, reason='stable-raw-field', source=source, value=value)
        fields['loadedCount'] = dict(available=True, reason='source-derived-truncation-toward-zero', source=e.LOADED_SOURCE, value=8)
        fields['effectiveReserve'] = dict(available=False, reason='effective-getter-not-resolved')
        observation = dict(schemaVersion=1, game='LE2', available=True, reason='validated-pawn-selected-weapon',
                           dispatchCapability=False, reloadEligibilityVerified=False, reloadCompletionVerified=False,
                           persistenceVerified=False, durableObjectIdentity=False, coherentSimulationSnapshot=False,
                           selectedWeapon=dict(name='SyntheticRifle', nameNumber=12, index=93,
                                               **{'class': 'SyntheticPackage.Rifle'}, heavyWeaponClass=False),
                           context=dict(source='current pawn.Weapon; reciprocal Instigator/InvManager/WorldInfo',
                                        pendingWeaponPresent=False), fields=fields)
        for i, key in enumerate(('world', 'controller', 'pawn', 'inventory')):
            observation['context'][key] = {'class': 'SyntheticPackage.' + key, 'index': i + 1}
        self.payload['weaponObservation'] = observation
        self.archive()
        return observation

    def test_weapon_export_preserves_raw_semantics_and_exact_identity(self):
        observation = self.use_weapon()
        self.assertEqual(self.invoke(), 2)  # No map/resources or effective reserve fabricated.
        artifact = self.state()
        weapon = artifact['state']['weapon']
        self.assertEqual(weapon['identity'], observation['selectedWeapon'])
        self.assertEqual(weapon['loaded'], 8)
        self.assertEqual(weapon['magazineUsed'], 4.5)
        self.assertEqual(weapon['reserveStored'], -7)
        self.assertNotIn('reserve', weapon)
        self.assertNotIn('effectiveReserve', weapon)
        self.assertNotIn('resources', artifact['state'])
        self.assertNotIn('map', artifact['state'])
        self.assertEqual(artifact['provenance']['runtimeObservation'], observation)
        self.assertEqual(artifact['provenance']['normalizedSemantics']['weapon.loaded'], e.LOADED_SOURCE)
        self.assertEqual(artifact['provenance']['observationVerb'], 'observe-weapon')
        self.assertFalse(artifact['provenance']['combinedObservation'])
        self.assertFalse(any(artifact['claims'].values()))
        self.assertEqual(artifact['unavailable']['weapon.effectiveReserve']['sample'], observation['fields']['effectiveReserve'])

    def use_effective_reserve(self, raw=23):
        observation = self.use_weapon()
        observation['selectedWeapon']['class'] = 'SFXGameContent_Inventory.SFXWeapon_AssaultRifle'
        fields = observation['fields']
        fields['reserveStored']['value'] = raw
        fields['effectiveReserve'] = dict(available=True, reason='source-derived-int32-to-float32',
            source=e.RESERVE_SOURCE, value=struct.unpack('<f', struct.pack('<f', raw))[0],
            provenance=dict(policy=e.RESERVE_POLICY, declaration='SFXGame.SFXWeapon.GetCurrentSpareAmmo',
                selectedClass=observation['selectedWeapon']['class'], functionIndex=98, returnPropertyIndex=99,
                sdkPin=e.RESERVE_SDK, basePackageSha256=e.RESERVE_PACKAGE,
                nativeGetterExecuted=False, mountedScriptBodyProven=False))
        return observation

    def test_effective_reserve_float32_rounding_and_archive(self):
        for raw in (0, 23, 16777217, 2147483647):
            observation = self.use_effective_reserve(raw)
            state, unavailable = e.normalize_weapon(self.payload)
            self.assertEqual(state['weapon']['effectiveReserve'], struct.unpack('<f', struct.pack('<f', raw))[0])
            self.assertEqual(state['weapon']['reserveStored'], raw)
            self.assertNotIn('reserve', state['weapon'])
            self.assertNotIn('weapon.effectiveReserve', unavailable)
        self.archive()
        self.assertEqual(self.invoke(), 2)  # Fixture map/resources are still absent.
        self.assertEqual(self.state()['state']['weapon']['effectiveReserve'], 2147483648)
        self.assertEqual(self.state()['provenance']['runtimeObservation'], observation)

    def test_effective_reserve_rejects_invalid_dependency_and_provenance(self):
        baseline = copy.deepcopy(self.use_effective_reserve())
        mutations = [
            ('value', 22), ('value', True), ('value', float('nan')), ('value', 23.1),
            ('source', 'CurrentSpareAmmo'), ('reason', 'native-getter-returned'),
        ]
        for key, value in mutations:
            observation = copy.deepcopy(baseline)
            observation['fields']['effectiveReserve'][key] = value
            self.payload['weaponObservation'] = observation
            with self.subTest(key=key, value=value), self.assertRaises(ValueError): e.normalize_weapon(self.payload)
        for key, value in [('policy', 'unknown'), ('sdkPin', 'a'*40), ('basePackageSha256', 'a'*64),
                           ('selectedClass', 'Other'), ('functionIndex', True), ('returnPropertyIndex', 98),
                           ('nativeGetterExecuted', True), ('mountedScriptBodyProven', True),
                           ('declaration', 'SFXHeavyWeapon.GetCurrentSpareAmmo')]:
            observation = copy.deepcopy(baseline)
            observation['fields']['effectiveReserve']['provenance'][key] = value
            self.payload['weaponObservation'] = observation
            with self.subTest(key=key), self.assertRaises(ValueError): e.normalize_weapon(self.payload)
        for raw in (-1, 2147483648, True, 1.5):
            observation = copy.deepcopy(baseline)
            observation['fields']['reserveStored']['value'] = raw
            self.payload['weaponObservation'] = observation
            with self.subTest(raw=raw), self.assertRaises(ValueError): e.normalize_weapon(self.payload)
        observation = copy.deepcopy(baseline)
        observation['fields']['reserveStored']['available'] = False
        self.payload['weaponObservation'] = observation
        with self.assertRaises(ValueError): e.normalize_weapon(self.payload)
        observation = copy.deepcopy(baseline)
        observation['selectedWeapon']['class'] = 'Unknown.Rifle'
        observation['fields']['effectiveReserve']['provenance']['selectedClass'] = 'Unknown.Rifle'
        self.payload['weaponObservation'] = observation
        with self.assertRaises(ValueError): e.normalize_weapon(self.payload)

    def test_weapon_unavailable_context_preserved_without_zero_values(self):
        observation = self.use_weapon()
        observation.update(available=False, reason='weapon-context-changed')
        for key in ('context', 'selectedWeapon', 'fields'):
            del observation[key]
        self.archive()
        self.assertEqual(self.invoke(), 2)
        self.assertEqual(self.state()['state'], {'weapon': {}})
        self.assertEqual(self.state()['unavailable']['weapon.identity']['reason'], 'weapon-context-changed')

    def test_weapon_unavailable_field_preserves_reason_and_sample(self):
        observation = self.use_weapon()
        observation['fields']['magazineUsed'] = dict(available=False, source='AmmoUsedCount', reason='weapon-field-changed')
        observation['fields']['loadedCount'] = dict(available=False, source=e.LOADED_SOURCE, reason='capacity-or-used-unavailable-or-out-of-range')
        self.archive()
        self.assertEqual(self.invoke(), 2)
        self.assertNotIn('loaded', self.state()['state']['weapon'])
        self.assertEqual(self.state()['unavailable']['weapon.magazineUsed']['sample'], observation['fields']['magazineUsed'])
        self.assertEqual(self.state()['state']['weapon']['reserveStored'], -7)

    def test_weapon_rejects_wrong_source_type_range_and_derived_value(self):
        original = copy.deepcopy(self.use_weapon())
        cases = [('reserveStored', 'value', True), ('reserveStored', 'value', 1 << 31),
                 ('reserveStored', 'value', -0x80000001), ('reserveStored', 'source', 'GetCurrentSpareAmmo'),
                 ('magazineCapacity', 'value', 0.1), ('magazineCapacity', 'value', 1e100),
                 ('magazineUsed', 'available', 1), ('infiniteAmmo', 'value', 0),
                 ('loadedCount', 'value', 9), ('loadedCount', 'value', True),
                 ('loadedCount', 'source', 'rounded'), ('loadedCount', 'reason', 'rounded'),
                 ('effectiveReserve', 'available', True)]
        for name, key, value in cases:
            with self.subTest(name=name, key=key, value=value):
                observation = copy.deepcopy(original)
                observation['fields'][name][key] = value
                self.payload['weaponObservation'] = observation
                self.archive()
                self.assertEqual(self.invoke(), 1)
                self.assertFalse((self.run / 'artifacts').exists())

    def test_weapon_loaded_uses_float32_subtraction_before_truncation(self):
        observation = self.use_weapon()
        observation['fields']['magazineCapacity']['value'] = 16777216
        observation['fields']['magazineUsed']['value'] = 0.5
        observation['fields']['loadedCount']['value'] = 16777216
        state, _ = e.normalize_weapon(self.payload)
        self.assertEqual(state['weapon']['loaded'], 16777216)
        observation['fields']['loadedCount']['value'] = 16777215  # Python double then int would do this.
        with self.assertRaises(ValueError):
            e.normalize_weapon(self.payload)

    def test_weapon_loaded_requires_valid_available_source_values(self):
        original = copy.deepcopy(self.use_weapon())
        for changes in ({'available': False}, {'value': -1}, {'value': 13}):
            self.payload['weaponObservation'] = copy.deepcopy(original)
            self.payload['weaponObservation']['fields']['magazineUsed'].update(changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                e.normalize_weapon(self.payload)
        self.payload['weaponObservation'] = copy.deepcopy(original)
        self.payload['weaponObservation']['fields']['magazineCapacity'] = None
        self.archive()
        self.assertEqual(self.invoke(), 1)
        self.assertFalse((self.run / 'artifacts').exists())

    def test_weapon_identity_and_claim_contracts_strict(self):
        original = copy.deepcopy(self.use_weapon())
        for key, value in [('index', True), ('index', -1), ('nameNumber', 1.5),
                           ('heavyWeaponClass', 0), ('address', '0x123'), ('name', '')]:
            self.payload['weaponObservation'] = copy.deepcopy(original)
            self.payload['weaponObservation']['selectedWeapon'][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                e.normalize_weapon(self.payload)
        for key in ('durableObjectIdentity', 'coherentSimulationSnapshot', 'reloadCompletionVerified'):
            self.payload['weaponObservation'] = copy.deepcopy(original)
            self.payload['weaponObservation'][key] = True
            with self.subTest(key=key), self.assertRaises(ValueError):
                e.normalize_weapon(self.payload)

    def test_weapon_archive_uses_same_correlation_and_frozen_guards(self):
        self.use_weapon()
        for changes in ({'status': 'error'}, {'lease': '44444444-4444-4444-8444-444444444444'}, {'verb': 'observe-resources'}):
            self.archive(**changes)
            self.assertEqual(self.invoke(), 1)
        self.archive()
        (self.run / 'recipe.json').write_bytes(b'{}')
        self.assertEqual(self.invoke(), 1)
        self.assertFalse((self.run / 'artifacts').exists())

    def test_weapon_cannot_normalize_resource_payload_or_bypass_consumption(self):
        self.request['verb'] = 'observe-weapon'
        self.archive()
        self.assertEqual(self.invoke(), 1)
        self.use_weapon()
        (self.run / 'control/consumed-1.request').unlink()
        self.assertEqual(self.invoke(), 1)

    @unittest.skipUnless(os.environ.get('LE2_CONTROL_CPP'), 'set LE2_CONTROL_CPP for actual C++ codec conformance')
    def test_weapon_actual_cpp_archive_conformance(self):
        for effective in (False, True):
            self.use_effective_reserve(16777217) if effective else self.use_weapon()
            self.archive()
            for suffix in ('request', 'result'):
                path = self.run / f'control/history/{self.token}.{suffix}'
                result = subprocess.run([os.environ['LE2_CONTROL_CPP'], str(path)], capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, path.read_bytes())

    def test_success_and_exact_provenance(self):
        self.assertEqual(self.invoke(), 0)
        state = self.state()
        self.assertEqual(state['state']['resources']['probes'], 2)
        self.assertEqual(state['state']['resources']['fuel'], 50.25)
        self.assertEqual(state['identity']['gameVersion'], 'exe-sha256:' + 'a' * 64)
        self.assertEqual(state['provenance']['wireIdentity']['start'], self.wire['start'])
        self.assertEqual(state['provenance']['responseSha256'], e.sha((self.run / f'control/history/{self.token}.result').read_bytes()))
        self.assertFalse(any(state['claims'].values()))

    def test_missing_resource_exports_partial_and_fails(self):
        del self.payload['resourceObservation']['resources']['probes']
        self.archive()
        self.assertEqual(self.invoke(), 2)
        self.assertNotIn('probes', self.state()['state']['resources'])
        self.assertIn('resources.probes', self.state()['unavailable'])

    def test_unavailable_never_zero_filled(self):
        sample = self.payload['resourceObservation']['resources']['probes']
        sample.update(available=False, reason='engine-field-unavailable', value=0)
        self.payload['resourceObservation']['allResourcesAvailable'] = False
        self.archive()
        self.assertEqual(self.invoke(), 2)
        self.assertNotIn('probes', self.state()['state']['resources'])
        self.assertEqual(self.state()['unavailable']['resources.probes']['sample']['reason'], 'engine-field-unavailable')

    def test_boolean_is_not_integer(self):
        self.payload['resourceObservation']['resources']['probes']['value'] = True
        self.archive()
        self.assertEqual(self.invoke(), 1)
        self.assertFalse((self.run / 'artifacts').exists())

    def test_nan_contract_refused(self):
        self.payload['resourceObservation']['resources']['fuel']['value'] = float('nan')
        self.archive()
        self.assertEqual(self.invoke(), 1)
        self.assertFalse((self.run / 'artifacts').exists())

    def test_exponent_overflow_refused_even_in_discarded_payload_field(self):
        # parse_constant handles NaN/Infinity tokens, not valid number syntax
        # overflowing Python's float. Ignored outer fields must not evade this.
        for number in ('1e400', '-1e400'):
            data = (json.dumps(self.payload)[:-1] + ',"ignoredOverflow":' + number + '}').encode()
            result = dict(self.result, payload=data, bytes=str(len(data)))
            (self.run / f'control/history/{self.token}.result').write_bytes(e.control.encode(result))
            self.assertEqual(self.invoke(), 1)
            self.assertFalse((self.run / 'artifacts').exists())

    def test_bits_must_match_value(self):
        self.payload['resourceObservation']['resources']['probes']['sampleBits'] = '0x00000003'
        self.archive()
        self.assertEqual(self.invoke(), 1)

    def test_changed_frozen_artifacts_refused(self):
        for name in ('plan.json', 'scenario.json', 'recipe.json', 'preparation-receipt.json', 'fixture.pcsav'):
            with self.subTest(name=name):
                path = self.run / name
                before = path.read_bytes()
                path.write_bytes(before + b' ')
                self.assertEqual(self.invoke(), 1)
                path.write_bytes(before)

    def test_mismatched_process_or_descriptor(self):
        self.context['gameProcess']['started'] = '2026-11-14T07:00:00.1234568Z'
        self.put('context.json', self.context)
        self.assertEqual(self.invoke(), 1)

    def test_mismatched_request_response(self):
        for changes in ({'lease': '44444444-4444-4444-8444-444444444444'}, {'sequence': '2'}, {'deadline': '999998'}, {'host': 'e' * 64}):
            with self.subTest(changes=changes):
                self.archive(**changes)
                self.assertEqual(self.invoke(), 1)

    def test_corrupt_frame(self):
        path = self.run / f'control/history/{self.token}.result'
        path.write_bytes(path.read_bytes()[:-1])
        self.assertEqual(self.invoke(), 1)

    def test_wrong_verb_and_status(self):
        for status in ('error', 'dispatched', 'rejected'):
            self.archive(status=status)
            self.assertEqual(self.invoke(), 1)
        self.request['verb'] = 'observe'
        self.archive()
        self.assertEqual(self.invoke(), 1)

    def test_deployment_hash_mismatch(self):
        self.put('deployment.json', dict(candidateCommit='d' * 40, asiSha256='e' * 64))
        self.assertEqual(self.invoke(), 1)

    def test_missing_runtime_capability(self):
        self.payload['capabilities']['observeResources'] = False
        self.archive()
        self.assertEqual(self.invoke(), 1)

    def test_wrong_source_type_and_cache_claims(self):
        sample = self.payload['resourceObservation']['resources']['probes']
        for key, value in (('sourceField', 'CachedSave.PlayerRecord.Probes'), ('scalarType', 'float32')):
            before = sample[key]
            sample[key] = value
            self.archive()
            self.assertEqual(self.invoke(), 1)
            sample[key] = before
        self.payload['resourceObservation']['saveCacheUsed'] = True
        self.archive()
        self.assertEqual(self.invoke(), 1)

    def test_wrong_case_and_partial_existing_output(self):
        case = self.case
        self.case = 'not-a-declared-case'
        self.assertEqual(self.invoke(), 1)
        self.case = case
        self.put(f'artifacts/state-{self.token}.check.json', {'existing': 'must preserve'})
        self.assertEqual(self.invoke(), 1)
        self.assertFalse((self.run / f'artifacts/state-{self.token}.json').exists())

    def test_context_unavailable_omits_all_values(self):
        self.payload['resourceObservation']['context']['available'] = False
        self.payload['resourceObservation']['allResourcesAvailable'] = False
        self.archive()
        self.assertEqual(self.invoke(), 2)
        self.assertEqual(self.state()['state'], {'resources': {}})

    def test_expected_start_failure_still_exports(self):
        self.payload['resourceObservation']['context']['map']['value'] = 'WrongMap'
        self.archive()
        self.assertEqual(self.invoke(), 2)
        report = e.read(self.run / f'artifacts/state-{self.token}.check.json')
        self.assertIn('starting map differs', report['failures'])

    def test_overwrite_refused(self):
        self.assertEqual(self.invoke(), 0)
        before = (self.run / f'artifacts/state-{self.token}.json').read_bytes()
        self.assertEqual(self.invoke(), 1)
        self.assertEqual((self.run / f'artifacts/state-{self.token}.json').read_bytes(), before)

    def test_missing_or_duplicate_consumption_refused(self):
        consumed = self.run / 'control/consumed-1.request'
        saved = consumed.read_bytes()
        consumed.unlink()
        self.assertEqual(self.invoke(), 1)
        consumed.write_bytes(saved)
        (self.run / 'control/consumed-2.request').write_bytes(saved)
        self.assertEqual(self.invoke(), 1)

    @unittest.skipUnless(os.environ.get('LE2_CONTROL_CPP'), 'set LE2_CONTROL_CPP for actual C++ codec conformance')
    def test_actual_cpp_archive_conformance(self):
        for suffix in ('request', 'result'):
            path = self.run / f'control/history/{self.token}.{suffix}'
            result = subprocess.run([os.environ['LE2_CONTROL_CPP'], str(path)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, path.read_bytes())


if __name__ == '__main__':
    unittest.main()
