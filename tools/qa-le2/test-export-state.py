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
