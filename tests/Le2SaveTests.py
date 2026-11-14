# SPDX-License-Identifier: MIT
"""Offline LE2 adapter checks. Optional LE2_TEST_SAVE enables an external corpus.

No private or upstream save binaries are committed. The minimal synthetic save is
constructed separately from the parser schema to catch field-order mistakes.
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

TOOL = Path(__file__).resolve().parents[1] / "tools" / "le2-save.py"
spec = importlib.util.spec_from_file_location("le2_save", TOOL)
le2 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(le2)


def integer(value=0):
    return struct.pack("<i", value)


def floating(value=0):
    return struct.pack("<f", value)


def string(value="", wide=False):
    if not value:
        return integer()
    raw = value.encode("utf-16-le" if wide else "cp1252") + (b"\0\0" if wide else b"\0")
    return integer(-len(raw) // 2 if wide else len(raw)) + raw


def fixture():
    # Explicit wire sequence: header, timestamp, transform, streamed world.
    raw = integer(30) + string("Fixture", True) + floating(12) + integer(1) + string("Biop_Nor")
    raw += b"\1" + integer() + integer(100) + integer(1) + integer(1) + integer(2026)
    raw += floating(1) + floating(2) + floating(3) + integer() * 4
    raw += integer() * 5  # levels, streaming, kismet, doors, pawns
    # Player, default appearance (no head morph), one power and one weapon.
    raw += integer() + string("Soldier") + integer(10) + floating(123) + string("Test") + integer()
    raw += b"\1\1" + integer(2) + string() * 3
    raw += b"\0" + integer() * 14  # 13 appearance IDs and option flag
    raw += integer(1) + string("AdrenalineRush") + floating(1) + string("PowerClass") + integer(0)
    raw += integer(1) + string("Pistol") + integer(3) + integer(24) + integer(1) + integer() + string()
    raw += string() * 6 + integer()  # loadout and empty hotkeys
    raw += b"".join(integer(n) for n in (100, 2, 3, 4, 5, 6, 7)) + floating(50) + string() + integer()
    raw += integer() * 2 + floating() * 5  # ME1 import bonus
    raw += integer(1) + string("hench_garrus") + integer() + integer(10) + integer(2) + string() * 7
    raw += integer(1) + struct.pack("<I", 0x80000002) + integer(2) + integer(-2) + integer(7) + integer(1) + floating(1.25)
    raw += integer() * 3  # journal
    raw += integer() * 2  # codex
    raw += integer() * 3  # ME1 plot
    raw += integer(1) + integer(42) + integer(1) + integer(1) + floating(0.2) + floating(0.3)
    raw += integer(1) + integer(100) + string("TestDLC")
    return with_crc(raw)


def with_crc(raw):
    return raw + struct.pack("<I", le2.crc32_bzip2(raw))


def recipe(save, edits=None):
    edits = edits or {"player.credits": 12345}
    return dict(schema_version=1, id="synthetic", expected_input_sha256=le2.sha256(save.raw),
                expect={path: save.value(path) for path in edits}, set=edits)


class SaveTests(unittest.TestCase):
    def setUp(self):
        self.raw = fixture()
        self.save = le2.Save(self.raw)

    def test_crc_known_vector(self):
        self.assertEqual(le2.crc32_bzip2(b"123456789"), 0xFC891918)

    def test_parse_independent_wire_fixture(self):
        self.assertEqual(self.save.value("debug_name"), "Fixture")
        self.assertEqual(self.save.value("player.weapons.0.ammo_total"), 24)
        self.assertEqual(self.save.value("squad.0.tag"), "hench_garrus")
        self.assertEqual(self.save.value("galaxy_map.planets.0.id"), 42)
        self.assertEqual(self.save.value("dependant_dlcs.0.name"), "TestDLC")
        self.assertTrue(self.save.value("plot.booleans.31"))

    def test_edits_touch_only_declared_spans_and_crc(self):
        edits = {"player.credits": 9999, "player.current_fuel": 999.5,
                 "player.weapons.0.ammo_used_count": 0, "player.weapons.0.ammo_total": 45,
                 "player.powers.0.rank": 3, "squad.0.talent_points": 5,
                 "location.z": -1.5, "rotation.yaw": -32768,
                 "plot.booleans.0": True, "plot.booleans.1": False,
                 "plot.integers.0": -999, "plot.floats.0": 2.5}
        output, changes = self.save.prepare(recipe(self.save, edits))
        allowed = set(range(len(self.raw) - 4, len(self.raw)))
        for change in changes:
            self.assertEqual(output.value(change["path"]), edits[change["path"]])
            allowed.update(range(change["offset"], change["offset"] + change["size"]))
        self.assertTrue(output.value("plot.booleans.31"))
        self.assertEqual(len(output.raw), len(self.raw))
        self.assertTrue(all(a == b or i in allowed for i, (a, b) in enumerate(zip(self.raw, output.raw))))
        self.assertEqual({c["path"] for c in le2.diff(self.save, output)}, set(edits))
        self.assertEqual(self.save.raw, self.raw)

    def test_float_receipt_reports_encoded_value(self):
        output, changes = self.save.prepare(recipe(self.save, {"location.x": 0.1}))
        self.assertEqual(changes[0]["after"], struct.unpack("<f", floating(0.1))[0])
        self.assertEqual(output.value("location.x"), changes[0]["after"])

    def test_noop_is_byte_identical(self):
        output, _ = self.save.prepare(recipe(self.save, {"player.credits": 100}))
        self.assertEqual(output.raw, self.raw)

    def test_bad_crc_version_truncation_trailing_and_count(self):
        cases = [self.raw[:-1], bytes([29]) + self.raw[1:], self.raw[:-4] + b"\0" * 4,
                 with_crc(self.raw[:-4] + b"unexpected"), with_crc(self.raw[:8])]
        length_offset = 4
        malformed = bytearray(self.raw[:-4])
        struct.pack_into("<i", malformed, length_offset, 2147483647)
        cases.append(with_crc(malformed))
        for raw in cases:
            with self.subTest(size=len(raw)), self.assertRaises(le2.SaveError):
                le2.Save(raw)

    def test_unknown_enum_boolean_and_nonfinite_float(self):
        for path, packed in (("difficulty", b"\xff"), ("player.is_female", integer(2)), ("location.x", floating(float("nan")))):
            raw = bytearray(self.raw[:-4])
            start = self.save.spans[path][0]
            raw[start:start + len(packed)] = packed
            with self.subTest(path=path), self.assertRaises(le2.SaveError):
                le2.Save(with_crc(raw))

    def test_recipe_rejects_wrong_hash_missing_or_failed_preconditions(self):
        cases = []
        wrong = recipe(self.save); wrong["expected_input_sha256"] = "0" * 64; cases.append(wrong)
        wrong = recipe(self.save); wrong["expect"] = {}; cases.append(wrong)
        wrong = recipe(self.save); wrong["expect"]["player.credits"] = 99; cases.append(wrong)
        wrong = recipe(self.save); wrong["expect"]["base_level_name"] = "Other"; cases.append(wrong)
        wrong = recipe(self.save); wrong["extra"] = 1; cases.append(wrong)
        wrong = recipe(self.save); wrong["schema_version"] = True; cases.append(wrong)
        for item in cases:
            with self.subTest(item=item), self.assertRaises(le2.SaveError):
                self.save.prepare(item)

    def test_recipe_rejects_unsafe_fields_and_values(self):
        cases = [("player.credits", -1), ("player.credits", True), ("player.credits", 1.5),
                 ("player.credits", 2147483648), ("player.current_fuel", float("inf")),
                 ("player.current_fuel", 1e300), ("player.current_fuel", 10 ** 400), ("plot.booleans.0", 1),
                 ("base_level_name", "Other"), ("player.weapons.0.class_name", "Other"),
                 ("galaxy_map.planets.0.visited", False), ("me1_plot.integers.0", 0)]
        for path, value in cases:
            with self.subTest(path=path, value=value), self.assertRaises(le2.SaveError):
                self.save.prepare(recipe(self.save, {path: value}))

    def test_unknown_and_noncanonical_indices(self):
        for path in ("plot.booleans.-1", "plot.booleans.01", "plot.booleans.99999", "player.unknown"):
            with self.subTest(path=path), self.assertRaises(le2.SaveError):
                self.save.value(path)

    def test_duplicate_json_keys_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "recipe.json"
            path.write_text('{"set":{"player.credits":1,"player.credits":2}}')
            with self.assertRaises(le2.SaveError):
                le2.load_recipe(path)

    def test_cli_exclusive_outputs_receipts_and_failure_cleanup(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            source, output, receipt_path, recipe_path = [folder / name for name in ("in.pcsav", "out.pcsav", "receipt.json", "recipe.json")]
            source.write_bytes(self.raw)
            recipe_path.write_text(json.dumps(recipe(self.save)))
            cmd = [sys.executable, str(TOOL), "prepare", str(source), str(output), "--recipe", str(recipe_path), "--receipt", str(receipt_path)]
            proc = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            receipt = json.loads(receipt_path.read_text())
            self.assertEqual(receipt["output_sha256"], le2.sha256(output.read_bytes()))
            self.assertEqual(source.read_bytes(), self.raw)
            self.assertEqual(subprocess.run(cmd, capture_output=True).returncode, 2)
            self.assertEqual(subprocess.run([sys.executable, str(TOOL), "roundtrip", str(source), str(source)], capture_output=True).returncode, 2)
            absent = folder / "absent.pcsav"
            with self.assertRaises(OSError):
                le2.write_outputs(absent, self.raw, folder / "missing" / "receipt.json", {})
            self.assertFalse(absent.exists())
            with self.assertRaises(le2.SaveError):
                le2.write_outputs(absent, self.raw, absent, {})

    def test_roundtrip_cli_receipt_keeps_null_recipe_fields_and_exact_bytes(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            source, output, receipt = [folder / name for name in ("source.pcsav", "copy.pcsav", "receipt.json")]
            source.write_bytes(self.raw)
            result = subprocess.run([sys.executable, str(TOOL), "roundtrip", str(source), str(output),
                                     "--receipt", str(receipt)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            expected = dict(schema_version=1, operation="roundtrip", game="LE2", save_version=30,
                            evidence="offline-only; game load and VR acceptance untested", upstream_pin=le2.UPSTREAM_PIN,
                            input_sha256=le2.sha256(self.raw), output_sha256=le2.sha256(self.raw),
                            recipe_sha256=None, recipe_id=None, changes=[], byte_identical=True)
            self.assertEqual(json.loads(result.stdout), expected)
            self.assertEqual(receipt.read_bytes(), (json.dumps(expected, indent=2) + "\n").encode("utf-8"))
            self.assertEqual(source.read_bytes(), self.raw)
            self.assertEqual(output.read_bytes(), self.raw)

    @unittest.skipUnless(os.environ.get("LE2_TEST_SAVE"), "External corpus is opt-in")
    def test_external_corpus_roundtrip_and_resource_edit(self):
        for name in os.environ["LE2_TEST_SAVE"].split(os.pathsep):
            with self.subTest(source=Path(name).name):
                raw = Path(name).read_bytes()
                save = le2.Save(raw)
                output, _ = save.prepare(recipe(save, {"player.credits": 123456}))
                self.assertEqual(output.value("player.credits"), 123456)
                self.assertEqual(le2.Save(save.raw).raw, raw)
                self.assertEqual([c["path"] for c in le2.diff(save, output)], ["player.credits"])


if __name__ == "__main__":
    unittest.main()
