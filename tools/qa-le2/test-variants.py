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


if __name__ == "__main__":
    unittest.main()
