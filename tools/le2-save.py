# SPDX-License-Identifier: MIT
"""Strict LE2 v30 save inspection and byte-preserving fixture preparation.

Independent implementation from the format described by Trilogy Save Editor,
pin 6451f23d1fba62358328dce4e87733970f36ebf1 (CeCILL-2.1). No code copied.
See research/le2-save-adapter.md for provenance and evidence boundaries.
No command installs a save or launches the game. Outputs must be new files.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import uuid

UPSTREAM_PIN = "6451f23d1fba62358328dce4e87733970f36ebf1"
MAX_BYTES = 64 * 1024 * 1024
MAX_ITEMS = 1000000


class SaveError(ValueError):
    pass


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def crc32_bzip2(raw):
    crc = 0xFFFFFFFF
    for byte in raw:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04C11DB7 if crc & 0x80000000 else 0)) & 0xFFFFFFFF
    return crc ^ 0xFFFFFFFF


def fields(**items):
    return items


VECTOR = fields(x="f", y="f", z="f")
ROTATOR = fields(pitch="i", yaw="i", roll="i")
POWER = fields(name="s", rank="f", power_class_name="s", wheel_display_index="i")
LOADOUT = dict.fromkeys(("assault_rifle", "shotgun", "sniper_rifle", "submachine_gun", "pistol", "heavy_weapon"), "s")
HEAD = fields(hair_mesh="s", accessory_mesh=["s"], morph_features=[fields(key="s", value="f")],
              offset_bones=[fields(key="s", value=VECTOR)], lod0_vertices=[VECTOR], lod1_vertices=[VECTOR],
              lod2_vertices=[VECTOR], lod3_vertices=[VECTOR], scalar_parameters=[fields(key="s", value="f")],
              vector_parameters=[fields(key="s", value=fields(r="f", g="f", b="f", a="f"))],
              texture_parameters=[fields(key="s", value="s")])
APPEARANCE = fields(combat_appearance=("enum", 2), **dict.fromkeys(("casual_id", "full_body_id", "torso_id", "shoulder_id", "arm_id", "leg_id", "specular_id", "tint1_id", "tint2_id", "tint3_id", "pattern_id", "pattern_color_id", "helmet_id"), "i"), head_morph=("option", HEAD))
PLAYER = fields(is_female="b", class_name="s", level="i", current_xp="f", first_name="s", localized_last_name="i",
                origin=("enum", 4), notoriety=("enum", 4), talent_points="i", mapped_power_1="s", mapped_power_2="s", mapped_power_3="s",
                appearance=APPEARANCE, powers=[POWER], weapons=[fields(class_name="s", ammo_used_count="i", ammo_total="i", current_weapon="b", last_weapon="b", ammo_power_name="s")],
                weapons_loadout=LOADOUT, hotkeys=[fields(pawn_name="s", power_id="i")],
                **dict.fromkeys(("credits", "medigel", "eezo", "iridium", "palladium", "platinum", "probes"), "i"),
                current_fuel="f", face_code="s", localized_class_name="i")
PLOT = fields(booleans="bits", integers=["i"], floats=["f"])
SCHEMA = fields(save_version="i", debug_name="s", seconds_played="f", disc="i", base_level_name="s", difficulty=("enum", 5), end_game_state=("enum32", 3),
                timestamp=fields(seconds_since_midnight="i", day="i", month="i", year="i"), location=VECTOR, rotation=ROTATOR, current_loading_tip="i",
                levels=[fields(name="s", should_be_loaded="b", should_be_visible="b")], streaming_states=[fields(name="s", is_active="b")],
                kismet_records=[fields(guid="guid", value="b")], doors=[fields(guid="guid", current_state="B", old_state="B")], pawns=["guid"], player=PLAYER,
                me1_import_bonus=fields(imported_me1_level="i", starting_me2_level="i", bonus_xp="f", bonus_credits="f", bonus_resources="f", bonus_paragon="f", bonus_renegade="f"),
                squad=[fields(tag="s", powers=[POWER], character_level="i", talent_points="i", weapon_loadout=LOADOUT, mapped_power="s")],
                plot=PLOT, journal=fields(quest_progress_counter="i", quest_progress=[fields(quest_counter="i", quest_updated="b", history=["i"])], quest_ids=["i"]),
                codex=fields(codex_entries=[fields(pages=[fields(page="i", is_new="b")])], codex_ids=["i"]), me1_plot=PLOT,
                galaxy_map=fields(planets=[fields(id="i", visited="b", probes=[fields(x="f", y="f")])]), dependant_dlcs=[fields(id="i", name="s")])


class Save:
    def __init__(self, raw):
        if not isinstance(raw, bytes) or not 8 <= len(raw) <= MAX_BYTES:
            raise SaveError("Invalid save size")
        if struct.unpack_from("<i", raw)[0] != 30:
            raise SaveError("Only little-endian LE2 version 30 is supported")
        if crc32_bzip2(raw[:-4]) != struct.unpack_from("<I", raw, len(raw) - 4)[0]:
            raise SaveError("CRC32/BZIP2 mismatch")
        self.raw, self.pos, self.spans = raw, 0, {}
        self.data = self.read(SCHEMA, "")
        if self.pos != len(raw) - 4:
            raise SaveError(f"Unparsed trailing payload at {self.pos}")

    @classmethod
    def load(cls, path):
        path = Path(path)
        if path.stat().st_size > MAX_BYTES:
            raise SaveError("Save exceeds size limit")
        return cls(path.read_bytes())

    def take(self, size):
        if size < 0 or self.pos + size > len(self.raw) - 4:
            raise SaveError(f"Truncated payload at {self.pos}")
        value = self.raw[self.pos:self.pos + size]
        self.pos += size
        return value

    def number(self, fmt):
        return struct.unpack("<" + fmt, self.take(struct.calcsize("<" + fmt)))[0]

    def count(self):
        count = self.number("I")
        if count > MAX_ITEMS or count > len(self.raw) - 4 - self.pos:
            raise SaveError("Invalid collection size")
        return count

    def read(self, schema, path):
        if isinstance(schema, dict):
            return {k: self.read(v, f"{path}.{k}" if path else k) for k, v in schema.items()}
        if isinstance(schema, list):
            return [self.read(schema[0], f"{path}.{i}") for i in range(self.count())]
        if isinstance(schema, tuple):
            kind, arg = schema
            if kind == "option":
                present = self.read("b", path + ".$present")
                return self.read(arg, path) if present else None
            start = self.pos
            value = self.number("B" if kind == "enum" else "I")
            if value >= arg:
                raise SaveError(f"Unknown enum at {path}")
            self.spans[path] = (start, "B" if kind == "enum" else "I", None)
            return value
        if schema == "bits":
            result = []
            words = self.count()
            if words > MAX_ITEMS // 32:
                raise SaveError("Plot bit count exceeds inspection limit")
            for _ in range(words):
                start = self.pos
                word = self.number("I")
                for bit in range(32):
                    self.spans[f"{path}.{len(result)}"] = (start, "bit", bit)
                    result.append(bool(word & (1 << bit)))
            return result
        start = self.pos
        if schema == "s":
            length = self.number("i")
            if abs(length) > MAX_BYTES // 2:
                raise SaveError(f"Invalid string length at {path}")
            if length == 0:
                return ""
            wide = length < 0
            raw = self.take(abs(length) * (2 if wide else 1))
            terminator = b"\0\0" if wide else b"\0"
            if not raw.endswith(terminator):
                raise SaveError(f"Unterminated string at {path}")
            try:
                return raw[:-len(terminator)].decode("utf-16-le" if wide else "cp1252")
            except UnicodeError as exc:
                raise SaveError(f"Invalid string at {path}") from exc
        if schema == "guid":
            return str(uuid.UUID(bytes_le=self.take(16)))
        value = self.number("I" if schema == "b" else schema)
        if schema == "b":
            if value not in (0, 1):
                raise SaveError(f"Invalid boolean at {path}")
            value = bool(value)
        if schema == "f" and not math.isfinite(value):
            raise SaveError(f"Non-finite float at {path}")
        self.spans[path] = (start, schema, None)
        return value

    def value(self, path):
        value = self.data
        try:
            for part in path.split("."):
                if isinstance(value, list):
                    if not re.fullmatch(r"0|[1-9][0-9]*", part):
                        raise KeyError(part)
                    value = value[int(part)]
                else:
                    value = value[part]
        except (KeyError, IndexError, TypeError, ValueError) as exc:
            raise SaveError(f"Unknown field: {path}") from exc
        return value

    def info(self):
        return dict(schema_version=1, game="LE2", save_version=30, sha256=sha256(self.raw), data=self.data)

    def prepare(self, recipe):
        validate_recipe(recipe)
        if recipe["expected_input_sha256"] != sha256(self.raw):
            raise SaveError("Input SHA256 does not match recipe")
        for path, expected in recipe["expect"].items():
            actual = self.value(path)
            if not scalar_equal(actual, expected):
                raise SaveError(f"Precondition failed for {path}: expected {expected!r}, found {actual!r}")
        raw = bytearray(self.raw)
        changes = []
        for path, value in recipe["set"].items():
            if path not in recipe["expect"]:
                raise SaveError(f"Edit requires explicit before-value: {path}")
            if not editable(path):
                raise SaveError(f"Field is not editable: {path}")
            before = self.value(path)
            if path not in self.spans:
                raise SaveError(f"Not a scalar field: {path}")
            start, fmt, bit = self.spans[path]
            if fmt in ("bit", "b"):
                if type(value) is not bool:
                    raise SaveError(f"Boolean required: {path}")
            elif fmt == "f":
                if type(value) not in (float, int) or (type(value) is float and not math.isfinite(value)):
                    raise SaveError(f"Finite number required: {path}")
            elif type(value) is not int:
                raise SaveError(f"Integer required: {path}")
            if not path.startswith(("plot.", "location.", "rotation.")) and value < 0:
                raise SaveError(f"Nonnegative value required: {path}")
            try:
                if fmt == "bit":
                    word = struct.unpack_from("<I", raw, start)[0]
                    word = (word | (1 << bit)) if value else (word & ~(1 << bit))
                    packed = struct.pack("<I", word)
                else:
                    packed = struct.pack("<" + ("I" if fmt == "b" else fmt), value)
                    if fmt == "f" and not math.isfinite(struct.unpack("<f", packed)[0]):
                        raise OverflowError()
            except (struct.error, OverflowError) as exc:
                raise SaveError(f"Value out of range: {path}") from exc
            raw[start:start + len(packed)] = packed
            changes.append(dict(path=path, before=before, requested=value, offset=start, size=len(packed)))
        struct.pack_into("<I", raw, len(raw) - 4, crc32_bzip2(raw[:-4]))
        result = Save(bytes(raw))
        for change in changes:
            change["after"] = result.value(change["path"])
        return result, changes


def scalar_equal(a, b):
    if type(a) is bool or type(b) is bool:
        return type(a) is type(b) and a == b
    return type(a) in (int, float, str, type(None)) and type(b) in (int, float, str, type(None)) and a == b


def editable(path):
    return bool(re.fullmatch(
        r"player\.(credits|medigel|eezo|iridium|palladium|platinum|probes|current_fuel|level|current_xp|talent_points)"
        r"|player\.weapons\.[0-9]+\.(ammo_used_count|ammo_total)"
        r"|player\.powers\.[0-9]+\.rank"
        r"|squad\.[0-9]+\.(character_level|talent_points)"
        r"|squad\.[0-9]+\.powers\.[0-9]+\.rank"
        r"|plot\.(booleans|integers|floats)\.[0-9]+"
        r"|location\.(x|y|z)|rotation\.(pitch|yaw|roll)", path))


def validate_recipe(recipe):
    required = {"schema_version", "id", "expected_input_sha256", "expect", "set"}
    if not isinstance(recipe, dict) or not required <= recipe.keys() or recipe.keys() - required - {"description", "source"}:
        raise SaveError("Recipe has missing or unknown keys")
    if type(recipe["schema_version"]) is not int or recipe["schema_version"] != 1:
        raise SaveError("Unsupported recipe schema_version")
    if not isinstance(recipe["id"], str) or not recipe["id"].strip():
        raise SaveError("Recipe id must be a nonempty string")
    if not isinstance(recipe["expected_input_sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", recipe["expected_input_sha256"]):
        raise SaveError("Recipe requires lowercase expected_input_sha256")
    for key in ("expect", "set"):
        if not isinstance(recipe[key], dict):
            raise SaveError(f"Recipe {key} must be an object")
        for path, value in recipe[key].items():
            if not isinstance(path, str) or type(value) not in (str, int, float, bool, type(None)):
                raise SaveError(f"Recipe {key} requires scalar field values")
            if type(value) is float and not math.isfinite(value):
                raise SaveError("Recipe contains a non-finite number")
    for key in ("description", "source"):
        if key in recipe and not isinstance(recipe[key], str):
            raise SaveError(f"Recipe {key} must be a string")


def load_recipe(path):
    return parse_recipe(Path(path).read_bytes())


def parse_recipe(raw):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise SaveError(f"Duplicate recipe key: {key}")
            result[key] = value
        return result
    try:
        return json.loads(raw.decode("utf-8-sig"), object_pairs_hook=unique)
    except ValueError as exc:
        raise SaveError(f"Invalid recipe JSON: {exc}") from exc


def flatten(value, prefix=""):
    if isinstance(value, (dict, list)):
        items = value.items() if isinstance(value, dict) else enumerate(value)
        result = {}
        for key, child in items:
            result.update(flatten(child, f"{prefix}.{key}" if prefix else str(key)))
        if not value:
            result[prefix] = value
        return result
    return {prefix: value}


def diff(before, after):
    a, b = flatten(before.data), flatten(after.data)
    return [dict(path=key, before=a.get(key), after=b.get(key), before_present=key in a, after_present=key in b)
            for key in sorted(a.keys() | b.keys()) if key not in a or key not in b or a[key] != b[key]]


def write_outputs(output, raw, receipt_path, receipt):
    paths = [Path(output).resolve()]
    if receipt_path:
        paths.append(Path(receipt_path).resolve())
    if len(set(paths)) != len(paths) or any(p.exists() for p in paths):
        raise SaveError("Output and receipt must be distinct new files; overwriting is disabled")
    # Exclusive create also protects against a file appearing after the preflight.
    created = []
    try:
        for path, content in zip(paths, [raw, (json.dumps(receipt, indent=2, allow_nan=False) + "\n").encode("utf-8")]):
            with path.open("xb") as stream:
                created.append(path)
                stream.write(content)
    except BaseException:
        for path in created:
            path.unlink()
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    info = commands.add_parser("info")
    info.add_argument("input")
    info.add_argument("--json", action="store_true")
    compare = commands.add_parser("diff")
    compare.add_argument("before")
    compare.add_argument("after")
    compare.add_argument("--json", action="store_true")
    for name in ("roundtrip", "prepare"):
        cmd = commands.add_parser(name)
        cmd.add_argument("input")
        cmd.add_argument("output")
        cmd.add_argument("--receipt", required=name == "prepare")
        if name == "prepare":
            cmd.add_argument("--recipe", required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "diff":
            before, after = Save.load(args.before), Save.load(args.after)
            result = dict(schema_version=1, before_sha256=sha256(before.raw), after_sha256=sha256(after.raw),
                          byte_identical=before.raw == after.raw, changes=diff(before, after))
        else:
            save = Save.load(args.input)
            if args.command == "info":
                result = save.info()
                if not args.json:
                    result = {k: result[k] for k in ("game", "save_version", "sha256")}
                    result.update(base_level_name=save.data["base_level_name"], location=save.data["location"], player=save.data["player"]["first_name"])
            else:
                output, changes = save, []
                recipe = None
                if args.command == "prepare":
                    recipe_raw = Path(args.recipe).read_bytes()
                    recipe = parse_recipe(recipe_raw)
                    output, changes = save.prepare(recipe)
                result = dict(schema_version=1, operation=args.command, game="LE2", save_version=30,
                              evidence="offline-only; game load and VR acceptance untested", upstream_pin=UPSTREAM_PIN,
                              input_sha256=sha256(save.raw), output_sha256=sha256(output.raw),
                              recipe_sha256=sha256(recipe_raw) if recipe else None,
                              recipe_id=recipe["id"] if recipe else None, changes=changes,
                              byte_identical=save.raw == output.raw)
                write_outputs(args.output, output.raw, args.receipt, result)
        print(json.dumps(result, indent=2, allow_nan=False))
        return 0
    except (SaveError, OSError, UnicodeError, json.JSONDecodeError) as exc:
        print(f"le2-save: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
