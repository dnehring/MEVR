# SPDX-License-Identifier: MIT
"""Strict LE3 v59 codec and private, bounded resource preparation.

Independent wire-format implementation using our MIT LE2 primitives. No TSE
codec code is copied. Unknown structures fail closed; gameplay meanings are not
inferred from decoded fields. No command installs saves or launches the game.
"""
from __future__ import annotations

import argparse
import copy
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys
import re
import uuid

_spec = importlib.util.spec_from_file_location("mevr_le2_save_reader", Path(__file__).with_name("le2-save.py"))
reader = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(reader)
SaveError = reader.SaveError
UPSTREAM_PIN = reader.UPSTREAM_PIN
parse_recipe = reader.parse_recipe

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

# Keyed fields retain insertion order as {key,value} records, never array indices
# masquerading as plot IDs. Duplicate keys cannot roundtrip through TSE; reject them.
def ordered_map(key, value):
    return ("map", reader.fields(key=key, value=value))


APPEARANCE = copy.deepcopy(reader.APPEARANCE)
for _key in ("morph_features", "offset_bones", "scalar_parameters", "vector_parameters", "texture_parameters"):
    _head = APPEARANCE["head_morph"][1]
    _head[_key] = ("map", _head[_key][0])
POWER = reader.fields(name="s", rank="f", **dict.fromkeys((f"evolved_choice_{n}" for n in range(6)), "i"),
                      power_class_name="s", wheel_display_index="i")
WEAPON = reader.fields(class_name="s", ammo_used_count="i", ammo_total="i", current_weapon="b",
                       was_last_weapon="b", ammo_power_name="s", ammo_power_source_tag="s")
WEAPON_MOD = reader.fields(weapon_class_name="s", weapon_mod_class_names=["s"])
PLAYER = dict(PREFIX["player"], appearance=APPEARANCE, emissive_id="i", powers=[POWER],
              war_assets=ordered_map("i", "i"), weapons=[WEAPON], weapons_mods=[WEAPON_MOD],
              weapons_loadout=reader.LOADOUT, primary_weapon="s", secondary_weapon="s",
              loadout_weapon_group=["i"], hotkeys=[reader.fields(pawn_name="s", power_name="s")],
              health="f", **dict.fromkeys(("credits", "medigel", "eezo", "iridium", "palladium", "platinum", "probes"), "i"),
              current_fuel="f", grenades="i", face_code="s", localized_class_name="i", character_guid="guid")
SCHEMA = dict(PREFIX, player=PLAYER,
    squad=[reader.fields(tag="s", powers=[POWER], character_level="i", talent_points="i",
        weapon_loadout=reader.LOADOUT, mapped_power="s", weapon_mods=[WEAPON_MOD], grenades="i", weapons=[WEAPON])],
    plot=reader.fields(booleans="bits", integers=ordered_map("i", "i"), floats=ordered_map("i", "f")),
    journal=reader.fields(quest_progress_counter="i", quest_progress=[reader.fields(quest_counter="i",
        quest_updated="b", active_goal="i", history=["i"])], quest_ids=["i"]),
    codex=reader.SCHEMA["codex"], me1_plot=reader.PLOT, player_variables=ordered_map("s", "i"),
    galaxy_map=reader.fields(planets=[reader.fields(id="i", visited="b", probes=[reader.fields(x="f", y="f")],
        show_as_scanned="b")], systems=[reader.fields(id="i", reaper_alert_level="f", reaper_detected="b")]),
    dependant_dlcs=[reader.fields(id="i", name="s", canonical_name="s")],
    treasures=[reader.fields(level_name="s", credits="i", xp="i", items=["s"])], use_modules=["guid"],
    conversation_mode=("enum", 3), objective_markers=[reader.fields(marker_owned_data="s", marker_offset=reader.VECTOR,
        marker_label="i", bone_to_attach_to="s", marker_icon_type=("enum", 4))], saved_objective_text="i")


class Save(reader.Save):
    def __init__(self, raw):
        if not isinstance(raw, bytes) or not 8 <= len(raw) <= reader.MAX_BYTES:
            raise SaveError("Invalid save size")
        if struct.unpack_from("<i", raw)[0] != 59:
            raise SaveError("Only little-endian LE3 version 59 is supported")
        if reader.crc32_bzip2(raw[:-4]) != struct.unpack_from("<I", raw, len(raw) - 4)[0]:
            raise SaveError("CRC32/BZIP2 mismatch")
        self.raw, self.pos, self.spans, self.string_forms = raw, 0, {}, {}
        self.data = self.read(SCHEMA, "")
        if self.pos != len(raw) - 4:
            raise SaveError(f"Unsupported trailing payload at {self.pos}")

    def read(self, schema, path):
        if isinstance(schema, tuple) and schema[0] == "map":
            values = super().read([schema[1]], path)
            if len({row["key"] for row in values}) != len(values):
                raise SaveError(f"Duplicate map key at {path}")
            return values
        if schema == "s":
            self.string_forms[path] = struct.unpack_from("<i", self.raw, self.pos)[0] if self.pos + 4 <= len(self.raw) - 4 else 0
        return super().read(schema, path)

    def encode(self, schema, value, path=""):
        """Rebuild every field from decoded values; only string representation choices are retained."""
        if isinstance(schema, dict):
            return b"".join(self.encode(child, value[key], f"{path}.{key}" if path else key) for key, child in schema.items())
        if isinstance(schema, list) or (isinstance(schema, tuple) and schema[0] == "map"):
            child = schema[0] if isinstance(schema, list) else schema[1]
            return struct.pack("<I", len(value)) + b"".join(self.encode(child, item, f"{path}.{n}") for n, item in enumerate(value))
        if isinstance(schema, tuple):
            kind, child = schema
            if kind == "option":
                return struct.pack("<I", value is not None) + (self.encode(child, value, path) if value is not None else b"")
            return struct.pack("<B" if kind == "enum" else "<I", value)
        if schema == "bits":
            if len(value) % 32:
                raise SaveError("Partial plot word")
            return struct.pack("<I", len(value) // 32) + b"".join(struct.pack("<I", sum(int(bit) << j for j, bit in enumerate(value[n:n + 32]))) for n in range(0, len(value), 32))
        if schema == "s":
            form = self.string_forms[path]
            if form == 0:
                if value:
                    raise SaveError("Changed zero-length string")
                return struct.pack("<i", 0)
            wide = form < 0
            payload = value.encode("utf-16-le" if wide else "cp1252") + (b"\0\0" if wide else b"\0")
            size = -len(payload) // 2 if wide else len(payload)
            return struct.pack("<i", size) + payload
        if schema == "guid":
            return uuid.UUID(value).bytes_le
        return struct.pack("<" + ("I" if schema == "b" else schema), value)

    def prepare(self, recipe):
        validate_recipe(recipe)
        self.roundtrip()  # No edits if independent reconstruction cannot preserve this structure.
        if recipe["expected_input_sha256"] != reader.sha256(self.raw):
            raise SaveError("Input SHA256 does not match recipe")
        for path, expected in recipe["expect"].items():
            if not reader.scalar_equal(self.value(path), expected):
                raise SaveError(f"Precondition failed for {path}")
        raw, changes = bytearray(self.raw), []
        for path, value in recipe["set"].items():
            if not editable(path) or path not in recipe["expect"] or path not in self.spans:
                raise SaveError(f"Edit requires an allowlisted scalar and before-value: {path}")
            if ".weapons." in path:
                identity = path.rsplit(".", 1)[0] + ".class_name"
                if identity not in recipe["expect"]:
                    raise SaveError(f"Weapon edit requires identity precondition: {identity}")
            if path.startswith("squad."):
                identity = ".".join(path.split(".")[:2]) + ".tag"
                if identity not in recipe["expect"]:
                    raise SaveError(f"Squad edit requires identity precondition: {identity}")
            start, fmt, _ = self.spans[path]
            if (fmt == "i" and type(value) is not int) or (fmt == "f" and type(value) not in (int, float)):
                raise SaveError(f"Typed resource required: {path}")
            if value < 0 or (type(value) is float and not math.isfinite(value)):
                raise SaveError(f"Nonnegative finite resource required: {path}")
            try:
                packed = struct.pack("<" + fmt, value)
                if fmt == "f" and not math.isfinite(struct.unpack("<f", packed)[0]):
                    raise OverflowError()
            except (struct.error, OverflowError) as exc:
                raise SaveError(f"Resource outside wire range: {path}") from exc
            raw[start:start + len(packed)] = packed
            changes.append(dict(path=path, before=self.value(path), requested=value, offset=start, size=len(packed)))
        struct.pack_into("<I", raw, len(raw) - 4, reader.crc32_bzip2(raw[:-4]))
        after = Save(bytes(raw))
        after.roundtrip()
        for change in changes:
            change["after"] = after.value(change["path"])
        return after, changes

    def info(self):
        return dict(schema_version=1, operation="inspect", game="LE3", save_version=59,
                    sha256=reader.sha256(self.raw), byte_count=len(self.raw), crc_valid=True,
                    validation_scope="full-v59-structure-and-crc", full_decode=True,
                    fixture_ready=False, mutation_supported=True, native_verified=False,
                    decoded_bytes=self.pos, opaque_bytes=0, upstream_pin=reader.UPSTREAM_PIN,
                    data=self.data)

    def roundtrip(self):
        payload = self.encode(SCHEMA, self.data)
        raw = payload + struct.pack("<I", reader.crc32_bzip2(payload))
        after = Save(raw)
        if after.raw != self.raw or after.data != self.data:
            raise SaveError("No-op byte preservation failed")
        receipt = dict(self.info(), operation="roundtrip", input_sha256=reader.sha256(self.raw),
                       output_sha256=reader.sha256(after.raw), byte_identical=True,
                       serialization_roundtrip=True, changes=[],
                       evidence="offline full decode/re-encode; external oracle and native acceptance are separate gates")
        receipt.pop("data")
        return after.raw, receipt


def editable(path):
    return bool(re.fullmatch(r"player\.(credits|medigel|grenades|current_fuel|health)"
        r"|(player|squad\.(0|[1-9][0-9]*))\.weapons\.(0|[1-9][0-9]*)\.(ammo_used_count|ammo_total)"
        r"|squad\.(0|[1-9][0-9]*)\.grenades", path))


def validate_recipe(recipe):
    if not isinstance(recipe, dict) or recipe.get("game") != "LE3" or type(recipe.get("save_version")) is not int or recipe["save_version"] != 59:
        raise SaveError("LE3 recipe requires game LE3 and save_version 59")
    reader.validate_recipe({k: v for k, v in recipe.items() if k not in ("game", "save_version")})


def make_receipt(operation, before, after, changes, *, recipe=None, recipe_raw=None):
    return dict(schema_version=1, operation=operation, game="LE3", save_version=59,
        full_decode=True, serialization_roundtrip=True, fixture_ready=False, native_verified=False,
        upstream_pin=UPSTREAM_PIN, input_sha256=reader.sha256(before.raw), output_sha256=reader.sha256(after.raw),
        recipe_sha256=reader.sha256(recipe_raw) if recipe else None, recipe_id=recipe["id"] if recipe else None,
        changes=changes, byte_identical=before.raw == after.raw,
        evidence="offline-only resource variant; independent oracle, normal load and persistence required")


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
    inspect.add_argument("--json", action="store_true", help="Include decoded private save values")
    inv = commands.add_parser("inventory")
    inv.add_argument("root", help="Private ME3 save directory, read-only")
    inv.add_argument("--output", help="New private JSON file; never overwrite")
    copy = commands.add_parser("roundtrip", help="Full decode/re-encode with identical-byte requirement")
    copy.add_argument("input")
    copy.add_argument("output")
    copy.add_argument("--receipt", required=True)
    prep = commands.add_parser("prepare", help="Create a private resource variant; never installs")
    prep.add_argument("input")
    prep.add_argument("output")
    prep.add_argument("--receipt", required=True)
    prep.add_argument("--recipe", required=True)
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
            elif args.command == "prepare":
                recipe_raw = Path(args.recipe).read_bytes()
                recipe = parse_recipe(recipe_raw)
                after, changes = save.prepare(recipe)
                result = make_receipt("prepare", save, after, changes, recipe=recipe, recipe_raw=recipe_raw)
                reader.write_outputs(args.output, after.raw, args.receipt, result)
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
