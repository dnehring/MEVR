# SPDX-License-Identifier: MIT
"""Synthetic archived outputs only: no native process, requests or live captures."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


observe = module("observe", HERE / "observe.py")
scenario = module("observe_scenario", HERE / "scenario.py")
recording = module("observe_recording", HERE.parent / "mevrrec.py")


class ObserveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.snapshot = self.root / "capture.json"
        # Deliberately only a PNG header; the inspector must not claim pixel validation.
        png = b"\x89PNG\r\n\x1a\n" + struct.pack(">I4sII", 13, b"IHDR", 100, 80) + bytes(9)
        for name in ("left.png", "right.png"):
            (self.root / name).write_bytes(png)
        self.body = {"stamp": "synthetic", "files": ["left.png", "right.png"], "errors": [],
                     "eyes": [{"eye": eye, "file": eye + ".png", "worldFile": None,
                               "size": [100, 80], "hasWorld": True} for eye in ("left", "right")],
                     "layers": [], "frame": 20, "unixMs": 1000, "replay": None,
                     "controllers": {"active": True}, "sessionFocused": True,
                     "mod": {"weapon": {"status": "LE1 pose description"}}}
        self.save_snapshot()

    def save_snapshot(self):
        self.snapshot.write_text(json.dumps(self.body), encoding="utf-8")

    def inspect_snapshot(self):
        return observe.inspect("snapshot", self.snapshot, observe.sha(self.snapshot.read_bytes()))

    def make_recording(self, times=(0, 10_000_000), trailing=b"", bad_float=None):
        path = self.root / "recording.mevrrec"
        raw = recording.HEADER.pack(recording.MAGIC, 1, recording.HEADER.size, recording.FRAME.size, 0, 1000)
        for time in times:
            values = [time, 3] + [0.0] * 22 + [0] + [0.0] * 18 + [0] + [0.0] * 18
            if bad_float is not None:
                values[2] = bad_float
            raw += recording.FRAME.pack(*values)
        path.write_bytes(raw + trailing)
        return path

    def inspect_recording(self, path):
        return observe.inspect("recording", path, observe.sha(path.read_bytes()))

    def test_snapshot_reports_hashes_but_never_native_identity_or_acceptance(self):
        report = self.inspect_snapshot()
        self.assertEqual(report["gameAttribution"], "unknown")
        self.assertFalse(report["runtimeEvidenceEligible"])
        self.assertFalse(report["inputEvidenceEligible"])
        self.assertEqual(report["source"], "offline-recording-inspection")
        self.assertNotIn("identity", report)
        self.assertNotIn("state", report)
        self.assertEqual(len(report["diagnostics"]["files"]), 2)
        self.assertTrue(report["diagnostics"]["modStatusPresent"])
        self.assertTrue(scenario.check_start({"expectedStart": {"map": "test", "checks": []}}, report))
        self.assertIn("pixels were not decoded", report["diagnostics"]["limits"][0])

    def test_missing_images_replay_and_dimension_disagreement_are_visible(self):
        (self.root / "right.png").unlink()
        self.body["eyes"][0]["size"] = [50, 50]
        self.body["replay"] = {"inputs": True, "path": "private recording"}
        self.save_snapshot()
        issues = self.inspect_snapshot()["diagnostics"]["issues"]
        self.assertTrue(any("missing image" in item for item in issues))
        self.assertTrue(any("dimensions differ" in item for item in issues))
        self.assertTrue(any("replay declared" in item for item in issues))

    def test_parent_reference_is_rejected_before_reading_outside_archive(self):
        self.body["files"] = ["../outside.png"]
        self.save_snapshot()
        with self.assertRaisesRegex(ValueError, "sibling PNG"):
            self.inspect_snapshot()

    def test_wrong_hash_and_non_snapshot_json_refused(self):
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            observe.inspect("snapshot", self.snapshot, "a" * 64)
        self.snapshot.write_text('{"source":"runtime-observation","state":{}}')
        with self.assertRaisesRegex(ValueError, "snapshot structure"):
            self.inspect_snapshot()

    def test_duplicate_keys_and_nonfinite_json_refused(self):
        for text in ('{"stamp":1,"stamp":2}', '{"x":NaN}', '{"x":1e400}'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                observe.read_json(text.encode())

    def test_recording_has_counts_and_explicit_truncation_without_input_claim(self):
        path = self.make_recording(trailing=b"partial")
        report = self.inspect_recording(path)
        self.assertEqual(report["diagnostics"]["completeFrames"], 2)
        self.assertEqual(report["diagnostics"]["trailingBytes"], 7)
        self.assertEqual(report["diagnostics"]["controllersActiveFrames"], 2)
        self.assertAlmostEqual(report["diagnostics"]["durationSeconds"], 0.01)
        self.assertEqual(report["gameAttribution"], "unknown")
        self.assertFalse(report["inputEvidenceEligible"])

    def test_recording_invalid_headers_empty_and_bad_samples_refused(self):
        path = self.make_recording(times=())
        with self.assertRaisesRegex(ValueError, "no complete"):
            self.inspect_recording(path)
        path.write_bytes(b"short")
        with self.assertRaisesRegex(ValueError, "header"):
            self.inspect_recording(path)
        path = self.make_recording(times=(5, 5))
        with self.assertRaisesRegex(ValueError, "times must increase"):
            self.inspect_recording(path)
        path = self.make_recording(bad_float=float("nan"))
        with self.assertRaisesRegex(ValueError, "nonfinite"):
            self.inspect_recording(path)

    def test_exact_version_one_layout_only(self):
        path = self.make_recording()
        raw = bytearray(path.read_bytes())
        struct.pack_into("<I", raw, 16, recording.FRAME.size + 4)
        path.write_bytes(raw)
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            self.inspect_recording(path)

    def test_outputs_are_exclusive_and_cannot_be_shared_request_files(self):
        report = self.inspect_snapshot()
        before = self.snapshot.read_bytes()
        with self.assertRaises(FileExistsError):
            observe.write_report(self.snapshot, report)
        self.assertEqual(self.snapshot.read_bytes(), before)
        with self.assertRaisesRegex(ValueError, "new .json"):
            observe.write_report(self.root / "console.request", report)
        out = self.root / "inspection.json"
        observe.write_report(out, report)
        self.assertEqual(json.loads(out.read_text())["acceptance"], "not-evaluated")

    def test_live_data_and_profile_paths_refused_without_touching_them(self):
        with mock.patch.dict(os.environ, {"LOCALAPPDATA": str(self.root)}):
            for path in (self.root / "MEVR/snapshots/test.json", self.root / "MELEVR/output.json",
                         self.root / "Game/ME2/Binaries/Win64/test.json", self.root / "Save/ME2/test.json"):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    observe.archive_path(path)
                self.assertFalse(path.exists())

    def test_reparse_ancestor_rejected_without_privileged_link_creation(self):
        original = Path.is_symlink
        with mock.patch.object(Path, "is_symlink", lambda p: p == self.root or original(p)):
            with self.assertRaisesRegex(ValueError, "Symlink/reparse"):
                self.inspect_snapshot()


if __name__ == "__main__":
    unittest.main()
