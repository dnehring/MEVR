# SPDX-License-Identifier: MIT
"""Prepare offline LE2 resource boundary cases in a new private batch directory.

Uses le2-save.py for all save parsing/editing. Never installs fixtures, starts a
game, edits profiles, acquires native locks or marks scenarios ready/passed.
See variant-usage.md. A batch is complete only when index.json exists.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import sys

ADAPTER_PATH = Path(__file__).resolve().parents[1] / "le2-save.py"
RESOURCE_PATHS = {"player." + name for name in
                  ("credits", "medigel", "eezo", "iridium", "palladium", "platinum", "probes", "current_fuel")}
LABELS = ("zero", "insufficient", "exact", "surplus")


class VariantError(ValueError):
    pass


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def json_bytes(value):
    return (json.dumps(value, indent=2, allow_nan=False) + "\n").encode("utf-8")


def read_spec(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise VariantError(f"Duplicate spec key: {key}")
            result[key] = value
        return result
    try:
        return json.loads(raw.decode("utf-8-sig"), object_pairs_hook=pairs)
    except (UnicodeError, ValueError) as error:
        raise VariantError(f"Invalid spec JSON: {error}") from error


def valid_sha(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def boundaries(spec):
    required = {"schemaVersion", "id", "resource", "cost", "quantum", "surplus", "provenance", "nativeValidation"}
    if not isinstance(spec, dict) or not required <= spec.keys() or spec.keys() - required - {"spdxLicenseIdentifier"}:
        raise VariantError("Spec has missing or unknown keys")
    if type(spec["schemaVersion"]) is not int or spec["schemaVersion"] != 1:
        raise VariantError("Unsupported spec schemaVersion")
    if not isinstance(spec["id"], str) or not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,79}", spec["id"]):
        raise VariantError("Spec id must use 1-80 lowercase letters, digits or hyphens")
    if not isinstance(spec["resource"], str) or spec["resource"] not in RESOURCE_PATHS:
        raise VariantError("Spec must target an allowlisted resource field")
    if spec["nativeValidation"] != "pending":
        raise VariantError("Offline generation requires nativeValidation: pending")
    if "spdxLicenseIdentifier" in spec and spec["spdxLicenseIdentifier"] != "MIT":
        raise VariantError("Unexpected spec license identifier")
    provenance = spec["provenance"]
    if not isinstance(provenance, dict) or set(provenance) != {"kind", "reference", "sha256", "description"}:
        raise VariantError("Provenance requires kind, reference, sha256 and description")
    if provenance["kind"] not in ("source-inspected", "native-measured"):
        raise VariantError("Cost must have inspected-source or measured-native provenance")
    if not valid_sha(provenance["sha256"]):
        raise VariantError("Provenance requires a lowercase SHA256 for its source/evidence artifact")
    for field in ("reference", "description"):
        if not isinstance(provenance[field], str) or not provenance[field].strip():
            raise VariantError(f"Provenance {field} must be nonempty")
    is_fuel = spec["resource"] == "player.current_fuel"
    for field in ("cost", "quantum", "surplus"):
        value = spec[field]
        if (type(value) not in (int, float) if is_fuel else type(value) is not int):
            raise VariantError(f"{field} must be {'numeric' if is_fuel else 'an integer'}")
        try:
            finite = math.isfinite(value)
        except OverflowError:
            finite = False
        if not finite or value <= 0:
            raise VariantError(f"{field} must be finite and positive")
    cost, quantum, surplus = (spec[k] for k in ("cost", "quantum", "surplus"))
    if quantum > cost or surplus <= cost:
        raise VariantError("Require quantum <= cost < surplus (surplus is the total starting balance)")
    requested = dict(zip(LABELS, (0, cost - quantum, cost, surplus)))
    encoded = {}
    for label, value in requested.items():
        try:
            encoded[label] = struct.unpack("<f", struct.pack("<f", value))[0] if is_fuel else value
        except (OverflowError, struct.error) as error:
            raise VariantError(f"{label} exceeds the resource storage range") from error
        if not math.isfinite(encoded[label]) or (not is_fuel and encoded[label] > 2147483647):
            raise VariantError(f"{label} exceeds the resource storage range")
    # The one-unit case intentionally has zero == insufficient. Other rounding
    # collisions would erase the claimed boundary and must not silently pass.
    if not (0 <= encoded["insufficient"] < encoded["exact"] < encoded["surplus"]):
        raise VariantError("Encoded resource boundaries collapse or change order")
    equivalent = requested["insufficient"] == 0
    if encoded["insufficient"] == 0 and not equivalent:
        raise VariantError("Positive insufficient balance collapses to zero in float32")
    return requested, encoded, equivalent


def no_links(path):
    path = Path(os.path.abspath(path))
    for entry in (path, *path.parents):
        try:
            linked = entry.is_symlink() or bool(getattr(entry.lstat(), "st_file_attributes", 0) &
                                               getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400))
        except FileNotFoundError:
            linked = False
        if linked:
            raise VariantError(f"Symlink/reparse path refused: {entry}")
    return path.resolve()


def load_adapter():
    spec = importlib.util.spec_from_file_location("le2_variant_adapter", ADAPTER_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_new(path, raw):
    with Path(path).open("xb") as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())


def prepare(base_path, output, base_sha256, spec_path):
    """Build one resource's cases. Return index; rollback owned output on failure."""
    if not valid_sha(base_sha256):
        raise VariantError("--base-sha256 requires 64 lowercase hexadecimal digits")
    base_path, output, spec_path = map(no_links, (base_path, output, spec_path))
    if output.exists():
        raise VariantError("Batch output must be a NEW directory")
    if not output.parent.is_dir():
        raise VariantError("Batch parent must already exist")
    spec_raw = spec_path.read_bytes()
    spec = read_spec(spec_raw)
    requested, encoded, equivalent = boundaries(spec)
    adapter = load_adapter()
    save = adapter.Save.load(base_path)
    if sha(save.raw) != base_sha256:
        raise VariantError("Base save hash does not match --base-sha256")
    before = save.value(spec["resource"])
    adapter_sha = sha(ADAPTER_PATH.read_bytes())
    # Prepare and reparse all variants in memory before creating any output.
    variants = {}
    for label in LABELS:
        if label == "insufficient" and equivalent:
            continue
        recipe = dict(schema_version=1, id=f"{spec['id']}-{label}", expected_input_sha256=base_sha256,
                      description=f"Offline resource {label} boundary; native/scenario readiness pending",
                      source=f"{spec['provenance']['reference']} SHA256 {spec['provenance']['sha256']}; spec SHA256 {sha(spec_raw)}",
                      expect={spec["resource"]: before}, set={spec["resource"]: requested[label]})
        result, changes = save.prepare(recipe)
        after = result.value(spec["resource"])
        if after != encoded[label]:
            raise VariantError(f"Adapter encoded an unexpected {label} value")
        actual_paths = {change["path"] for change in adapter.diff(save, result)}
        expected_paths = {spec["resource"]} if before != after else set()
        if actual_paths != expected_paths:
            raise VariantError("Adapter changed an undeclared field")
        recipe_raw = json_bytes(recipe)
        receipt = dict(schema_version=1, operation="prepare", game="LE2", save_version=30,
                       evidence="offline-only; game load and VR acceptance untested", upstream_pin=adapter.UPSTREAM_PIN,
                       input_sha256=base_sha256, output_sha256=sha(result.raw), recipe_sha256=sha(recipe_raw),
                       recipe_id=recipe["id"], changes=changes, byte_identical=result.raw == save.raw)
        variants[label] = (result.raw, recipe_raw, json_bytes(receipt))
    index = dict(schemaVersion=1, id=spec["id"], game="LE2", resource=spec["resource"], baseSaveSha256=base_sha256,
                 specSha256=sha(spec_raw), generatorSha256=sha(Path(__file__).read_bytes()), adapterSha256=adapter_sha,
                 status="offline-prepared", scenarioReadiness="pending", nativeValidation="pending", vrValidation="pending",
                 cost=spec["cost"], quantum=spec["quantum"], surplus=spec["surplus"], provenance=spec["provenance"],
                 originalValue=before, cases={}, files={})
    # Exclusive mkdir is the ownership boundary. Never clean an existing batch.
    output.mkdir()
    try:
        payloads = {"base.pcsav": save.raw, "spec.json": spec_raw}
        for label, (raw, recipe_raw, receipt_raw) in variants.items():
            payloads.update({f"{label}.pcsav": raw, f"{label}.recipe.json": recipe_raw, f"{label}.receipt.json": receipt_raw})
        for name, raw in payloads.items():
            write_new(output / name, raw)
            index["files"][name] = {"sha256": sha(raw), "bytes": len(raw)}
        for label in LABELS:
            canonical = "zero" if label == "insufficient" and equivalent else label
            index["cases"][label] = dict(requestedValue=requested[label], expectedValue=encoded[label],
                                        expectedResourcePath=spec["resource"], fixture=f"{canonical}.pcsav",
                                        recipe=f"{canonical}.recipe.json", receipt=f"{canonical}.receipt.json",
                                        equivalentTo="zero" if canonical != label else None,
                                        interpretation="balance only; action legality and resulting state untested")
        if sha(base_path.read_bytes()) != base_sha256 or spec_path.read_bytes() != spec_raw:
            raise VariantError("Input changed during preparation")
        for name, record in index["files"].items():
            if sha((output / name).read_bytes()) != record["sha256"]:
                raise VariantError(f"Output failed hash verification: {name}")
        write_new(output / "index.json", json_bytes(index))  # Last: completion marker.
    except BaseException:
        # Output was exclusively created here, is absolute, and was checked for
        # reparse ancestors. Refuse to follow a changed root during rollback.
        if no_links(output) != output:
            raise VariantError("Batch path changed; manual cleanup required")
        shutil.rmtree(output)
        raise
    return index


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--base-sha256", required=True)
    parser.add_argument("--spec", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        result = prepare(args.base, args.output, args.base_sha256, args.spec)
        print(json.dumps(result, indent=2, allow_nan=False))
        return 0
    except (ValueError, OSError) as error:
        print(f"prepare-variants: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
