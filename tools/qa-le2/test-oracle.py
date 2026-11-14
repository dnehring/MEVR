# SPDX-License-Identifier: MIT
"""Test oracle provenance and rejection gates, not another save-format parser.

Default tests mock the oracle boundary deliberately: they prove rejection and
evidence binding, not independent decoding. Set LE2_TSE_ORACLE to a built oracle
and LE2_TEST_SAVE to a path-separated private corpus for the real integration.
"""
from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ADAPTER = ROOT / "tools/le2-save.py"


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


builder = module("oracle_builder_tests", HERE / "build-tse-oracle.py")
checker = module("oracle_checker_tests", HERE / "check-save-oracle.py")
fixtures = module("oracle_wire_fixture", ROOT / "tests/Le2SaveTests.py")
le2 = fixtures.le2


class StagingTests(unittest.TestCase):
    def test_stages_pinned_blobs_instead_of_working_tree(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "upstream"
            (source / "src/save_data").mkdir(parents=True)
            (source / "src/save_data/mod.rs").write_text("modified working tree")
            (source / "src/save_data/ignored.rs").write_text("must not be staged")
            payload = b"// immutable blob\n\x00\xff"
            oid = "1" * 40
            calls = []

            def git(args, **kwargs):
                calls.append(args)
                operation = args[3]
                if operation == "rev-parse":
                    return builder.PIN + "\n"
                if operation == "ls-tree":
                    self.assertEqual(args[5], builder.PIN)
                    return f"100644 blob {oid}\tsrc/save_data/mod.rs\n"
                if operation == "cat-file":
                    self.assertEqual(args[4:], ["blob", oid])
                    return payload
                self.fail(f"Unexpected git invocation: {args}")

            output = root / "staged"
            with mock.patch.object(builder.subprocess, "check_output", side_effect=git):
                manifest = builder.stage(source, output)
            self.assertEqual((output / "src/save_data/mod.rs").read_bytes(), payload)
            self.assertFalse((output / "src/save_data/ignored.rs").exists())
            self.assertEqual(manifest["upstream_git_blobs"], {"src/save_data/mod.rs": oid})
            for name, expected in manifest["files"].items():
                self.assertEqual(hashlib.sha256((output / name).read_bytes()).hexdigest(), expected)
            self.assertEqual((output / "Cargo.lock").read_bytes(), (HERE / "tse-oracle.Cargo.lock").read_bytes())
            self.assertEqual(json.loads((output / "source-manifest.json").read_text()), manifest)
            self.assertEqual(len(calls), 3)

    def test_wrong_revision_and_nested_destination_rejected_before_creation(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "source"
            source.mkdir()
            wrong = Path(temp) / "wrong"
            with mock.patch.object(builder.subprocess, "check_output", return_value="0" * 40), self.assertRaisesRegex(ValueError, "revision"):
                builder.stage(source, wrong)
            self.assertFalse(wrong.exists())
            nested = source / "stage"
            with mock.patch.object(builder.subprocess, "check_output", return_value=builder.PIN), self.assertRaisesRegex(ValueError, "outside"):
                builder.stage(source, nested)
            self.assertFalse(nested.exists())

    def test_unexpected_git_entry_cannot_escape_staging(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "source"
            source.mkdir()
            for i, line in enumerate(("120000 blob " + "1" * 40 + "\tsrc/link.rs\n",
                                      "100644 blob " + "1" * 40 + "\t../escape.rs\n")):
                with mock.patch.object(builder.subprocess, "check_output", side_effect=[builder.PIN, line]) as git:
                    with self.assertRaisesRegex(ValueError, "Unexpected upstream entry"):
                        builder.stage(source, Path(temp) / f"stage-{i}")
                    self.assertEqual(git.call_count, 2)
            self.assertFalse((Path(temp) / "escape.rs").exists())


class CrosscheckTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / "oracle-project"
        self.oracle = self.project / "target/debug/mevr-tse-oracle.exe"
        self.oracle.parent.mkdir(parents=True)
        self.oracle.write_bytes(b"mock oracle executable: not independent evidence")
        (self.project / "Cargo.lock").write_bytes(b"mock dependency lock")
        (self.project / "codec.rs").write_bytes(b"reviewed immutable codec")
        self.manifest = {"schema_version": 1, "upstream_revision": le2.UPSTREAM_PIN,
                         "files": {name: checker.digest(self.project / name) for name in ("Cargo.lock", "codec.rs")}}
        self.manifest_path = self.project / "source-manifest.json"
        self.manifest_path.write_text(json.dumps(self.manifest))
        self.save = self.root / "source.pcsav"
        self.original = fixtures.fixture()
        self.save.write_bytes(self.original)
        self.output = self.root / "result"
        self.real_run = subprocess.run

    def oracle_boundary(self, mutate=None, reject=False):
        def run(args, **kwargs):
            if str(args[0]) != str(self.oracle):
                return self.real_run(args, **kwargs)  # Real adapter CLI and receipt.
            if reject:
                return subprocess.CompletedProcess(args, 1, "", "independent decoder rejected input")
            path = Path(args[1])
            data = copy.deepcopy(le2.Save.load(path).data)
            # Mimic upstream JSON's packed plot words only. This stub is not a
            # substitute for the real Rust integration test below.
            for group in ("plot", "me1_plot"):
                bits = data[group]["booleans"]
                data[group]["booleans"] = [sum(int(bit) << j for j, bit in enumerate(bits[i:i + 32]))
                                           for i in range(0, len(bits), 32)]
            report = {"schema_version": 1, "upstream_revision": le2.UPSTREAM_PIN,
                      "crc_valid": True, "reserialized_identically": True, "data": data}
            if mutate:
                mutate(report, path.stem)
            Path(args[2]).write_text(json.dumps(report))
            return subprocess.CompletedProcess(args, 0, "mock oracle response", "")
        return run

    def verify(self, **kwargs):
        with mock.patch.object(checker.subprocess, "run", side_effect=self.oracle_boundary(**kwargs)):
            return checker.verify(self.save, self.oracle, self.output, ADAPTER)

    def test_success_records_bound_artifacts_without_native_claim(self):
        result = self.verify()
        self.assertTrue(result["passed"])
        self.assertEqual(result["native_load"], "untested")
        self.assertEqual(result["gui_open"], "untested")
        for key, path in (("input_sha256", self.save), ("prepared_sha256", self.output / "prepared.pcsav"),
                          ("source_manifest_sha256", self.manifest_path), ("oracle_sha256", self.oracle),
                          ("receipt_sha256", self.output / "receipt.json"), ("recipe_sha256", self.output / "recipe.json")):
            self.assertEqual(result[key], checker.digest(path))
        self.assertIn("plot.booleans.31", result["actual_changed_paths"])
        self.assertEqual(self.save.read_bytes(), self.original)

    def test_extra_semantic_edit_rejected(self):
        def mutate(report, name):
            if name == "prepared":
                report["data"]["disc"] += 1
        with self.assertRaisesRegex(AssertionError, "semantic diff mismatch"):
            self.verify(mutate=mutate)
        self.assertFalse((self.output / "result.json").exists())
        self.assertEqual(self.save.read_bytes(), self.original)

    def test_wrong_declared_scalar_value_rejected(self):
        def mutate(report, name):
            if name == "prepared":
                report["data"]["player"]["credits"] = 99999
        with self.assertRaisesRegex(AssertionError, "scalar mismatch: player.credits"):
            self.verify(mutate=mutate)
        self.assertFalse((self.output / "result.json").exists())

    def test_false_roundtrip_checksum_or_revision_rejected(self):
        for key, value in (("reserialized_identically", False), ("crc_valid", False),
                           ("reserialized_identically", 1), ("upstream_revision", "0" * 40)):
            self.output = self.root / f"bad-report-{key}-{value}"
            with self.subTest(key=key, value=value), self.assertRaisesRegex(AssertionError, "did not confirm"):
                self.verify(mutate=lambda report, name: report.update({key: value}))
            self.assertFalse((self.output / "result.json").exists())

    def test_source_hash_mismatch_stops_before_any_probe(self):
        (self.project / "codec.rs").write_bytes(b"changed after staging")
        with mock.patch.object(checker.subprocess, "run") as run, self.assertRaisesRegex(AssertionError, "source changed"):
            checker.verify(self.save, self.oracle, self.output, ADAPTER)
        run.assert_not_called()
        self.assertFalse(self.output.exists())
        self.assertEqual(self.save.read_bytes(), self.original)

    def test_mismatched_upstream_pin_stops_before_output(self):
        self.manifest["upstream_revision"] = "0" * 40
        self.manifest_path.write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(AssertionError, "different upstream revisions"):
            self.verify()
        self.assertFalse(self.output.exists())

    def test_oracle_failure_never_publishes_pass(self):
        with self.assertRaisesRegex(RuntimeError, "oracle rejected base"):
            self.verify(reject=True)
        self.assertFalse((self.output / "result.json").exists())

    def test_existing_output_is_not_reused(self):
        self.output.mkdir()
        marker = self.output / "existing.txt"
        marker.write_text("preserve")
        with self.assertRaises(FileExistsError):
            self.verify()
        self.assertEqual(marker.read_text(), "preserve")


class RealOracleIntegration(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("LE2_TSE_ORACLE") and os.environ.get("LE2_TEST_SAVE"),
                         "Set LE2_TSE_ORACLE and LE2_TEST_SAVE for real pinned-source decoding")
    def test_private_corpus_independent_decode_and_reserialize(self):
        oracle = Path(os.environ["LE2_TSE_ORACLE"]).resolve(strict=True)
        with tempfile.TemporaryDirectory() as temp:
            for index, name in enumerate(os.environ["LE2_TEST_SAVE"].split(os.pathsep)):
                source = Path(name).resolve(strict=True)
                with self.subTest(corpus_index=index):
                    result = checker.verify(source, oracle, Path(temp) / str(index), ADAPTER)
                    self.assertTrue(result["passed"])
                    self.assertEqual(result["native_load"], "untested")


if __name__ == "__main__":
    unittest.main()
