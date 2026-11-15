# SPDX-License-Identifier: MIT
"""Read-only, bounded LE3 v59 prefix inspection; NOT a full save codec.

Reuses our MIT LE2 binary reader and CRC. Field order is independently described
from the pinned TSE format; no CeCILL codec code is copied. Inventory/plot/galaxy
remain opaque. `roundtrip` preserves original bytes, not a decode/re-encode proof.
No edit, preparation, installation or game launch is supported. Output is private.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import struct
import sys

_spec = importlib.util.spec_from_file_location("mevr_le2_save_reader", Path(__file__).with_name("le2-save.py"))
reader = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(reader)
SaveError = reader.SaveError

# TSE mass_effect_3/mod.rs and player.rs, ending BEFORE player.appearance.
# Only wire-format facts; this does not establish native field semantics.
PREFIX = reader.fields(
    save_version="i", debug_name="s", seconds_played="f", disc="i",
    base_level_name="s", base_level_name_display_override="s",
    difficulty=("enum", 5), end_game_state=("enum32", 3),
    timestamp=reader.SCHEMA["timestamp"], location=reader.VECTOR, rotation=reader.ROTATOR,
    current_loading_tip="i", levels=reader.SCHEMA["levels"],
    streaming_states=reader.SCHEMA["streaming_states"],
    kismet_records=reader.SCHEMA["kismet_records"], doors=reader.SCHEMA["doors"],
    placeables=[reader.fields(guid="guid", is_destroyed=("enum", 2), is_deactivated=("enum", 2))],
    pawns=["guid"], player=reader.fields(
        is_female="b", class_name="s", is_combat_pawn="b", is_injured_pawn="b",
        use_casual_appearance="b", level="i", current_xp="f", first_name="s",
        localized_last_name="i", origin=("enum", 4), notoriety=("enum", 4), talent_points="i",
        mapped_power_1="s", mapped_power_2="s", mapped_power_3="s"))


class Save(reader.Save):
    def __init__(self, raw):
        if not isinstance(raw, bytes) or not 8 <= len(raw) <= reader.MAX_BYTES:
            raise SaveError("Invalid save size")
        if struct.unpack_from("<i", raw)[0] != 59:
            raise SaveError("Only little-endian LE3 version 59 is supported")
        if reader.crc32_bzip2(raw[:-4]) != struct.unpack_from("<I", raw, len(raw) - 4)[0]:
            raise SaveError("CRC32/BZIP2 mismatch")
        self.raw, self.pos, self.spans = raw, 0, {}
        self.data = self.read(PREFIX, "")
        if self.pos >= len(raw) - 4:
            raise SaveError("Missing opaque player/save tail; prefix alone is not a save")
        self.opaque = raw[self.pos:-4]

    def prepare(self, recipe):
        raise SaveError("LE3 mutations are disabled: only the prefix is decoded")

    def info(self):
        return dict(schema_version=1, operation="inspect", game="LE3", save_version=59,
                    sha256=reader.sha256(self.raw), byte_count=len(self.raw), crc_valid=True,
                    validation_scope="prefix-and-whole-file-crc", full_decode=False,
                    fixture_ready=False, mutation_supported=False, native_verified=False,
                    prefix_bytes=self.pos, opaque_bytes=len(self.opaque),
                    opaque_sha256=reader.sha256(self.opaque), upstream_pin=reader.UPSTREAM_PIN,
                    data=self.data)

    def roundtrip(self):
        # Preserve the opaque tail and original string representations; recheck the
        # reconstruction and its checksum. Do not call this a serialization test.
        raw = self.raw[:self.pos] + self.opaque + self.raw[-4:]
        after = Save(raw)
        if after.raw != self.raw or after.data != self.data:
            raise SaveError("No-op byte preservation failed")
        receipt = dict(self.info(), operation="roundtrip", input_sha256=reader.sha256(self.raw),
                       output_sha256=reader.sha256(after.raw), byte_identical=True,
                       serialization_roundtrip=False, changes=[],
                       evidence="offline prefix inspection and unchanged-byte preservation only")
        receipt.pop("data")
        return after.raw, receipt


def inventory(root):
    root = Path(root).resolve(strict=True)
    if not root.is_dir():
        raise SaveError("Inventory root must be a directory")
    entries = []
    for path in sorted(root.rglob("*.pcsav")):
        # A linked save outside the named tree is not part of this inventory.
        if not path.resolve().is_relative_to(root):
            raise SaveError("Save link escapes inventory root")
        row = {"relative_path": path.relative_to(root).as_posix()}
        try:
            row.update(Save.load(path).info())
        except (SaveError, OSError) as exc:
            row["error"] = str(exc)
        entries.append(row)
    return dict(schema_version=1, operation="inventory", game="LE3", save_version=59,
                fixture_ready=False, entries=entries, inspected=len(entries),
                failed=sum("error" in row for row in entries))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    inspect = commands.add_parser("info")
    inspect.add_argument("input")
    inspect.add_argument("--json", action="store_true", help="Include decoded private prefix values")
    inv = commands.add_parser("inventory")
    inv.add_argument("root", help="Private ME3 save directory, read-only")
    inv.add_argument("--output", help="New private JSON file; never overwrite")
    copy = commands.add_parser("roundtrip", help="Unchanged-byte copy, NOT full serialization validation")
    copy.add_argument("input")
    copy.add_argument("output")
    copy.add_argument("--receipt", required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "inventory":
            result = inventory(args.root)
            if args.output:
                reader.write_outputs(args.output, (json.dumps(result, indent=2, allow_nan=False) + "\n").encode(), None, None)
                result = {key: result[key] for key in ("game", "operation", "inspected", "failed", "fixture_ready")}
        else:
            save = Save.load(args.input)
            if args.command == "roundtrip":
                raw, result = save.roundtrip()
                reader.write_outputs(args.output, raw, args.receipt, result)
            else:
                result = save.info()
                if not args.json:
                    result.pop("data")
        print(json.dumps(result, indent=2, allow_nan=False))
        return 1 if result.get("failed") else 0
    except (SaveError, OSError, UnicodeError) as exc:
        print(f"LE3 save inspection refused: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
