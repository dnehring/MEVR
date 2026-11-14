# SPDX-License-Identifier: MIT
"""Offline boundary batches: temp files only; no game/session/profile access.

Set LE2_TEST_SAVE to a path-separated private corpus for optional real-save
preparation. Source files are read-only; batches use temporary directories.
"""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


generator = load("le2_variants_tests", HERE / "prepare-variants.py")
fixtures = load("le2_variants_fixture", ROOT / "tests/Le2SaveTests.py")
save_tool = fixtures.le2


def spec(resource="player.credits", cost=4, quantum=1, surplus=8):
    return dict(schemaVersion=1, id="test-boundary", resource=resource,
                cost=cost, quantum=quantum, surplus=surplus, nativeValidation="pending",
                provenance=dict(kind="source-inspected", reference="synthetic test cost",
                                sha256="a" * 64, description="Test-only synthetic evidence; never a gameplay claim"))


class VariantTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.base = self.root / "base.pcsav"
        self.raw = fixtures.fixture()
        self.base.write_bytes(self.raw)
        self.base_hash = generator.sha(self.raw)
        self.spec_path = self.root / "spec.json"
        self.output = self.root / "batch"
        self.write_spec(spec())

    def write_spec(self, value):
        self.spec_path.write_bytes(generator.json_bytes(value))

    def prepare(self):
        return generator.prepare(self.base, self.output, self.base_hash, self.spec_path)

    def test_four_cases_replay_through_adapter_and_preserve_unrelated_state(self):
        index = self.prepare()
        original = save_tool.Save(self.raw)
        self.assertEqual(index["status"], "offline-prepared")
        for key in ("scenarioReadiness", "nativeValidation", "vrValidation"):
            self.assertEqual(index[key], "pending")
        for label, balance in zip(generator.LABELS, (0, 3, 4, 8)):
            case = index["cases"][label]
            output = save_tool.Save.load(self.output / case["fixture"])
            self.assertEqual(case["expectedValue"], balance)
            self.assertEqual(output.value("player.credits"), balance)
            self.assertEqual([c["path"] for c in save_tool.diff(original, output)], ["player.credits"])
            recipe_raw = (self.output / case["recipe"]).read_bytes()
            recipe = json.loads(recipe_raw)
            self.assertEqual(recipe["expected_input_sha256"], self.base_hash)
            self.assertEqual(recipe["expect"], {"player.credits": 100})
            replayed, _ = original.prepare(recipe)
            self.assertEqual(replayed.raw, output.raw)
            receipt = json.loads((self.output / case["receipt"]).read_bytes())
            self.assertEqual(receipt["input_sha256"], self.base_hash)
            self.assertEqual(receipt["output_sha256"], generator.sha(output.raw))
            self.assertEqual(receipt["recipe_sha256"], generator.sha(recipe_raw))
            self.assertEqual(receipt["operation"], "prepare")
        for name, record in index["files"].items():
            raw = (self.output / name).read_bytes()
            self.assertEqual(record, {"sha256": generator.sha(raw), "bytes": len(raw)})
        self.assertEqual((self.output / "base.pcsav").read_bytes(), self.raw)
        self.assertEqual(self.base.read_bytes(), self.raw)
        self.assertEqual(json.loads((self.output / "index.json").read_text()), index)

    def test_one_probe_marks_zero_and_insufficient_equivalent(self):
        self.spec_path.write_bytes((HERE / "variant-specs/one-probe.json").read_bytes())
        index = self.prepare()
        zero, low = index["cases"]["zero"], index["cases"]["insufficient"]
        self.assertEqual(low["equivalentTo"], "zero")
        self.assertEqual(low["fixture"], zero["fixture"])
        self.assertEqual(low["recipe"], zero["recipe"])
        self.assertEqual(low["receipt"], zero["receipt"])
        self.assertFalse((self.output / "insufficient.pcsav").exists())
        self.assertEqual(len(list(self.output.glob("*.pcsav"))), 4)  # Frozen base + 3 unique cases.
        self.assertEqual(index["provenance"]["kind"], "source-inspected")

    def test_batch_receipts_match_single_cli_including_noop_and_float32_rounding(self):
        for number, value in enumerate((spec(surplus=100), spec("player.current_fuel", 0.1, 0.01, 0.2))):
            with self.subTest(resource=value["resource"]):
                self.output = self.root / f"batch-equivalence-{number}"
                self.write_spec(value)
                index = self.prepare()
                for label, case in index["cases"].items():
                    single = self.root / f"single-{number}-{label}.pcsav"
                    receipt = single.with_suffix(".receipt.json")
                    result = subprocess.run([sys.executable, str(generator.ADAPTER_PATH), "prepare",
                                             str(self.base), str(single), "--recipe",
                                             str(self.output / case["recipe"]), "--receipt", str(receipt)],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(single.read_bytes(), (self.output / case["fixture"]).read_bytes())
                    self.assertEqual(receipt.read_bytes(), (self.output / case["receipt"]).read_bytes())
                    self.assertEqual(json.loads(result.stdout), json.loads(receipt.read_bytes()))
                exact = json.loads((self.output / "exact.receipt.json").read_bytes())
                self.assertEqual(set(exact), {"schema_version", "operation", "game", "save_version", "evidence",
                                             "upstream_pin", "input_sha256", "output_sha256", "recipe_sha256",
                                             "recipe_id", "changes", "byte_identical"})
                if number == 0:
                    self.assertTrue(json.loads((self.output / "surplus.receipt.json").read_bytes())["byte_identical"])
                else:
                    self.assertNotEqual(exact["changes"][0]["requested"], exact["changes"][0]["after"])
                self.assertEqual(self.base.read_bytes(), self.raw)

    def test_all_allowlisted_resources_and_noop_case(self):
        for i, resource in enumerate(sorted(generator.RESOURCE_PATHS)):
            with self.subTest(resource=resource):
                self.output = self.root / f"batch-{i}"
                old = save_tool.Save(self.raw).value(resource)
                self.write_spec(spec(resource, 1, 1, max(old, 2)))
                index = self.prepare()
                self.assertEqual(index["resource"], resource)
                for case in index["cases"].values():
                    self.assertEqual(save_tool.Save.load(self.output / case["fixture"]).value(resource), case["expectedValue"])
                if old >= 2:
                    receipt = json.loads((self.output / "surplus.receipt.json").read_text())
                    self.assertTrue(receipt["byte_identical"])

    def test_fuel_exposes_float32_rounding_without_collapsing_boundaries(self):
        self.write_spec(spec("player.current_fuel", 0.1, 0.01, 0.2))
        index = self.prepare()
        case = index["cases"]["exact"]
        self.assertEqual(case["requestedValue"], 0.1)
        self.assertEqual(case["expectedValue"], struct.unpack("<f", struct.pack("<f", 0.1))[0])
        self.assertLess(index["cases"]["insufficient"]["expectedValue"], case["expectedValue"])
        self.assertLess(case["expectedValue"], index["cases"]["surplus"]["expectedValue"])

    def test_invalid_specs_fail_before_creating_output(self):
        changes = [dict(resource="plot.booleans.1"), dict(resource="player.weapons.0.ammo_total"),
                   dict(resource=[]), dict(cost=True), dict(cost=1.5), dict(cost=0), dict(cost=-1),
                   dict(quantum=0), dict(quantum=5), dict(surplus=4), dict(surplus=2147483648),
                   dict(nativeValidation="passed"), dict(schemaVersion=True), dict(id="../escape"),
                   dict(unexpected=1), dict(provenance={}), dict(provenance=None)]
        for change in changes:
            value = spec(); value.update(change)
            with self.subTest(change=change):
                self.write_spec(value)
                with self.assertRaises(generator.VariantError):
                    self.prepare()
                self.assertFalse(self.output.exists())

    def test_nonfinite_overflow_and_collapsed_fuel_boundaries_rejected(self):
        cases = [(float("nan"), 1, 3), (1, float("inf"), 3), (1, 1, float("inf")),
                 (1e40, 1, 2e40), (1, 1, 10 ** 400),
                 (1, 1e-12, 2), (1, 0.5, 1.000000001),
                 (1e-46, 1e-46, 1e-45), (1e-45, 5e-46, 1e-44)]
        for cost, quantum, surplus in cases:
            with self.subTest(cost=cost, quantum=quantum, surplus=surplus), self.assertRaises(generator.VariantError):
                generator.boundaries(spec("player.current_fuel", cost, quantum, surplus))

    def test_unsubstantiated_or_malformed_provenance_rejected(self):
        for change in (dict(kind="guessed"), dict(sha256=None), dict(sha256="bad"),
                       dict(reference=""), dict(description=""), dict(extra="unknown")):
            value = spec(); value["provenance"].update(change)
            with self.subTest(change=change), self.assertRaises(generator.VariantError):
                generator.boundaries(value)

    def test_duplicate_spec_key_and_hash_mismatch_rejected(self):
        self.spec_path.write_text('{"cost":1,"cost":2}')
        with self.assertRaisesRegex(generator.VariantError, "Duplicate spec key"):
            self.prepare()
        self.write_spec(spec())
        with self.assertRaisesRegex(generator.VariantError, "Base save hash"):
            generator.prepare(self.base, self.output, "0" * 64, self.spec_path)
        self.assertFalse(self.output.exists())

    def test_existing_batch_untouched_and_partial_write_failure_cleaned(self):
        self.output.mkdir()
        marker = self.output / "keep"
        marker.write_text("existing user file")
        with self.assertRaisesRegex(generator.VariantError, "NEW directory"):
            self.prepare()
        self.assertEqual(marker.read_text(), "existing user file")
        self.output = self.root / "new-failure"
        real_write = generator.write_new
        calls = []
        def fail(path, raw):
            calls.append(path)
            if len(calls) == 5:
                raise OSError("injected write failure")
            real_write(path, raw)
        with mock.patch.object(generator, "write_new", side_effect=fail), self.assertRaisesRegex(OSError, "injected"):
            self.prepare()
        self.assertGreater(len(calls), 1)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.base.read_bytes(), self.raw)

    def test_interrupt_cleanup_and_changed_input_refusal(self):
        real_write = generator.write_new
        def interrupt(path, raw):
            real_write(path, raw)
            raise KeyboardInterrupt()
        with mock.patch.object(generator, "write_new", side_effect=interrupt), self.assertRaises(KeyboardInterrupt):
            self.prepare()
        self.assertFalse(self.output.exists())
        def change_source(path, raw):
            real_write(path, raw)
            if Path(path).name == "spec.json":
                self.spec_path.write_bytes(generator.json_bytes(spec(cost=2)))
        with mock.patch.object(generator, "write_new", side_effect=change_source), self.assertRaisesRegex(generator.VariantError, "Input changed"):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_index_write_failure_does_not_leave_completed_batch(self):
        real_write = generator.write_new
        def fail(path, raw):
            if Path(path).name == "index.json":
                raise OSError("index unavailable")
            real_write(path, raw)
        with mock.patch.object(generator, "write_new", side_effect=fail), self.assertRaisesRegex(OSError, "index unavailable"):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_reparse_path_refused(self):
        original = Path.is_symlink
        def linked(path):
            return path == self.root or original(path)
        with mock.patch.object(Path, "is_symlink", linked), self.assertRaisesRegex(generator.VariantError, "reparse"):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_cli_json_and_exit_codes(self):
        command = [sys.executable, str(HERE / "prepare-variants.py"), str(self.base), str(self.output),
                   "--base-sha256", self.base_hash, "--spec", str(self.spec_path)]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["baseSaveSha256"], self.base_hash)
        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 2)

    @unittest.skipUnless(os.environ.get("LE2_TEST_SAVE"), "Optional private LE2 corpus")
    def test_real_corpus_one_probe_preparation(self):
        template = HERE / "variant-specs/one-probe.json"
        for i, name in enumerate(os.environ["LE2_TEST_SAVE"].split(os.pathsep)):
            with self.subTest(corpus_index=i):
                base = Path(name).resolve(strict=True)
                expected = generator.sha(base.read_bytes())
                folder = self.root / f"real-{i}"
                index = generator.prepare(base, folder, expected, template)
                for case in index["cases"].values():
                    save = save_tool.Save.load(folder / case["fixture"])
                    self.assertEqual(save.value("player.probes"), case["expectedValue"])
                self.assertEqual(generator.sha(base.read_bytes()), expected)


def reload_spec(capacity=12, partial=4, surplus=24):
    value = dict(schemaVersion=1, kind='reload-ammo', id='test-reload', capacity=capacity,
                 partialUsed=partial, surplusReserve=surplus, ammoPolicy='ordinary-finite', nativeValidation='pending',
                 weapon=dict(index=0, className='SFXWeapon_AssaultRifle', runtimeClass='SFXGameContent_Inventory.SFXWeapon_AssaultRifle'),
                 provenance=dict(kind='native-measured', reference='synthetic-test-only', sha256='b'*64,
                                 description='Synthetic attestation for offline tests; no native evidence'))
    return value


def ammo_fixture(records):
    # Replace the independently authored fixture's weapon-list wire bytes;
    # never use the adapter parser schema to construct this test input.
    def record(name, used, reserve, current):
        return fixtures.string(name)+fixtures.integer(used)+fixtures.integer(reserve)+fixtures.integer(int(current))+fixtures.integer()+fixtures.string()
    original = fixtures.integer(1)+record('Pistol', 3, 24, True)
    replacement = fixtures.integer(len(records))+b''.join(record(*row) for row in records)
    raw = fixtures.fixture()[:-4]
    assert raw.count(original) == 1
    return fixtures.with_crc(raw.replace(original, replacement, 1))


class ReloadVariantTests(unittest.TestCase):
    write_spec = VariantTests.write_spec
    prepare = VariantTests.prepare

    def setUp(self):
        VariantTests.setUp(self)
        self.base_records([('SFXWeapon_AssaultRifle', 3, 24, True)])
        self.write_spec(reload_spec())

    def base_records(self, rows):
        self.raw = ammo_fixture(rows)
        self.base.write_bytes(self.raw)
        self.base_hash = generator.sha(self.raw)

    def test_cases_edit_only_exact_weapon_ammo_and_replay_shared_receipts(self):
        index = self.prepare()
        self.assertEqual(set(index['cases']), {'full-magazine', 'partial', 'empty', 'no-reserve',
                         'insufficient-reserve', 'exact-reserve', 'surplus-reserve'})
        self.assertEqual(index['boundaryUsed'], 4)
        self.assertTrue(index['savedSelection']['targetIsSavedCurrent'])
        self.assertFalse(index['savedSelection']['actualSelectedWeaponVerified'])
        original = save_tool.Save(self.raw)
        expected = {'full-magazine': (0,24), 'partial': (4,24), 'empty': (12,24),
                    'no-reserve': (4,0), 'insufficient-reserve': (4,3), 'exact-reserve': (4,4), 'surplus-reserve': (4,24)}
        for label, (used, reserve) in expected.items():
            case = index['cases'][label]
            result = save_tool.Save.load(self.output/case['fixture'])
            self.assertEqual(result.value('player.weapons.0.ammo_used_count'), used)
            self.assertEqual(result.value('player.weapons.0.ammo_total'), reserve)
            self.assertEqual(case['expectedLoadedAtAttestedCapacity'], 12-used)
            self.assertLessEqual({d['path'] for d in save_tool.diff(original,result)},
                                {'player.weapons.0.ammo_used_count','player.weapons.0.ammo_total'})
            recipe_raw = (self.output/case['recipe']).read_bytes()
            recipe = json.loads(recipe_raw)
            self.assertEqual(recipe['expect']['player.weapons.0.class_name'], 'SFXWeapon_AssaultRifle')
            self.assertTrue(recipe['expect']['player.weapons.0.current_weapon'])
            replay, changes = original.prepare(recipe)
            self.assertEqual(replay.raw, result.raw)
            self.assertEqual(json.loads((self.output/case['receipt']).read_bytes()),
                             save_tool.make_receipt('prepare', original, replay, changes, recipe=recipe, recipe_raw=recipe_raw))
        self.assertEqual(index['cases']['surplus-reserve']['equivalentTo'], 'partial')
        self.assertFalse((self.output/'surplus-reserve.pcsav').exists())
        for name, record in index['files'].items():
            self.assertEqual(generator.sha((self.output/name).read_bytes()), record['sha256'])
        self.assertEqual(self.base.read_bytes(), self.raw)
        for gate in ('nativeValidation','vrValidation','scenarioReadiness'): self.assertEqual(index[gate], 'pending')

    def test_capacity_one_and_two_report_degenerate_cases(self):
        self.base_records([('SFXWeapon_AssaultRifle',0,24,True)])
        for capacity, partial in ((1,None),(2,1)):
            self.output = self.root/f'capacity-{capacity}'
            self.write_spec(reload_spec(capacity,partial))
            index = self.prepare()
            self.assertEqual(index['boundaryUsed'], 1)
            self.assertEqual(index['cases']['insufficient-reserve']['equivalentTo'], 'no-reserve')
            self.assertEqual(index['autoReloadAmbiguous'], capacity==1)
            if capacity==1:
                self.assertNotIn('partial',index['cases'])
                self.assertIn('partial',index['omittedCases'])
                self.assertEqual(index['cases']['surplus-reserve']['equivalentTo'],'empty')
            else: self.assertIn('partial',index['cases'])

    def test_invalid_ammo_schema_policy_values_and_capacity_evidence(self):
        changes = [dict(capacity=0),dict(capacity=True),dict(capacity=12.0),dict(capacity=2**24),
                   dict(partialUsed=1),dict(partialUsed=True),dict(partialUsed=12),dict(partialUsed=None),
                   dict(surplusReserve=12),dict(surplusReserve=True),dict(surplusReserve=2**24+1),
                   dict(ammoPolicy='heat'),dict(nativeValidation='passed'),dict(extra=1),dict(kind='guessed')]
        for change in changes:
            value=reload_spec();value.update(change);self.write_spec(value)
            with self.subTest(change=change),self.assertRaises(ValueError): self.prepare()
            self.assertFalse(self.output.exists())
        value=reload_spec();value['provenance']['kind']='source-inspected';self.write_spec(value)
        with self.assertRaisesRegex(ValueError,'native-measured'):self.prepare()
        value=reload_spec(1,1);self.write_spec(value)
        with self.assertRaisesRegex(ValueError,'partialUsed must be null'):self.prepare()

    def test_class_index_duplicates_and_current_selection_ambiguity_rejected(self):
        for change in (dict(index=-1),dict(index=True),dict(index=1),dict(className='Other'),
                       dict(runtimeClass='SFXGame.SFXHeavyWeapon'),dict(runtimeClass='Unknown.Rifle')):
            value=reload_spec();value['weapon'].update(change);self.write_spec(value)
            with self.subTest(change=change),self.assertRaises(ValueError):self.prepare()
            self.assertFalse(self.output.exists())
        self.write_spec(reload_spec())
        for rows in ([('SFXWeapon_AssaultRifle',3,24,True),('SFXWeapon_AssaultRifle',0,24,False)],
                     [('SFXWeapon_AssaultRifle',3,24,True),('SFXWeapon_HeavyPistol',0,24,True)],
                     [('SFXWeapon_AssaultRifle',13,24,True)], [('SFXWeapon_AssaultRifle',-1,24,True)],
                     [('SFXWeapon_AssaultRifle',3,-1,True)], [('SFXWeapon_AssaultRifle',3,2**24+1,True)]):
            self.base_records(rows)
            with self.subTest(rows=rows),self.assertRaises(ValueError):self.prepare()
            self.assertFalse(self.output.exists())

    def test_nonzero_index_and_unselected_target_remain_explicit(self):
        self.base_records([('SFXWeapon_HeavyPistol',0,24,True),('SFXWeapon_AssaultRifle',3,24,False)])
        value=reload_spec();value['weapon']['index']=1;self.write_spec(value)
        index=self.prepare()
        self.assertFalse(index['savedSelection']['targetIsSavedCurrent'])
        self.assertEqual(index['savedSelection']['currentRecordIndex'],0)
        case=index['cases']['partial'];recipe=json.loads((self.output/case['recipe']).read_bytes())
        self.assertFalse(recipe['expect']['player.weapons.1.current_weapon'])
        self.assertEqual(set(recipe['set']),{'player.weapons.1.ammo_used_count','player.weapons.1.ammo_total'})
        self.output=self.root/'no-selected'
        self.base_records([('SFXWeapon_HeavyPistol',0,24,False),('SFXWeapon_AssaultRifle',3,24,False)])
        self.assertIsNone(self.prepare()['savedSelection']['currentRecordIndex'])

    def test_reload_batch_matches_standalone_cli_and_rolls_back_failure(self):
        index=self.prepare();case=index['cases']['exact-reserve']
        output,receipt=self.root/'single.pcsav',self.root/'single-receipt.json'
        command=[sys.executable,str(ROOT/'tools/le2-save.py'),'prepare',str(self.base),str(output),'--recipe',str(self.output/case['recipe']),
                 '--receipt',str(receipt)]
        run=subprocess.run(command,capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stderr)
        self.assertEqual(output.read_bytes(),(self.output/case['fixture']).read_bytes())
        self.assertEqual(receipt.read_bytes(),(self.output/case['receipt']).read_bytes())
        self.output=self.root/'fail-reload';real_write=generator.write_new
        def fail(path,raw):
            real_write(path,raw)
            if Path(path).name=='partial.pcsav':raise OSError('injected ammo failure')
        with mock.patch.object(generator,'write_new',side_effect=fail),self.assertRaisesRegex(OSError,'injected'):self.prepare()
        self.assertFalse(self.output.exists())
        self.assertEqual(self.base.read_bytes(),self.raw)



if __name__ == "__main__":
    unittest.main()
