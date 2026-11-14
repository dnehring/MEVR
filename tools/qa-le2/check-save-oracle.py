# SPDX-License-Identifier: MIT
"""Cross-check LE2 preparation against the independent TSE source oracle.

All products stay in a new private output directory. Use the default synthetic
format probe or supply the actual prepared save, recipe and receipt to check.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def flattened(value, path=""):
    if isinstance(value, (dict, list)):
        result = {}
        for key, child in (value.items() if isinstance(value, dict) else enumerate(value)):
            result.update(flattened(child, f"{path}.{key}" if path else str(key)))
        if not value:
            result[path] = value
        return result
    return {path: value}


def normalized(report):
    data = report["data"]
    for group in ("plot", "me1_plot"):
        words = data[group]["booleans"]
        data[group]["booleans"] = [bool(word & (1 << bit)) for word in words for bit in range(32)]
    return flattened(data)


def verify(save_path, oracle, output, adapter, *, prepared=None, recipe=None, receipt=None):
    source_manifest = oracle.parents[2] / "source-manifest.json"
    manifest = json.loads(source_manifest.read_text(encoding="utf-8"))
    for name, expected in manifest["files"].items():
        if digest(source_manifest.parent / name) != expected:
            raise AssertionError(f"Oracle source changed since staging: {name}")
    spec = importlib.util.spec_from_file_location("mevr_le2_oracle_adapter", adapter)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    save = module.Save.load(save_path)
    initial_hash = hashlib.sha256(save.raw).hexdigest()
    if manifest["upstream_revision"] != module.UPSTREAM_PIN:
        raise AssertionError("Oracle and adapter use different upstream revisions")
    supplied = (prepared, recipe, receipt)
    if any(supplied) and not all(supplied):
        raise ValueError("Supply prepared save, recipe and receipt together")
    if prepared:
        recipe_raw = recipe.read_bytes()
        receipt_raw = receipt.read_bytes()
        body = module.parse_recipe(recipe_raw)
        edited, changes = save.prepare(body)
        expected_receipt = module.make_receipt("prepare", save, edited, changes,
                                               recipe=body, recipe_raw=recipe_raw)
        if edited.raw != prepared.read_bytes() or expected_receipt != json.loads(receipt_raw):
            raise ValueError("Prepared save or receipt differs from the exact recipe output")
    output.mkdir(parents=True, exist_ok=False)
    base = output / "base.pcsav"
    base.write_bytes(save.raw)
    if prepared:
        (output / "prepared.pcsav").write_bytes(edited.raw)
        (output / "recipe.json").write_bytes(recipe_raw)
        (output / "receipt.json").write_bytes(receipt_raw)
    else:
        prepare_format_probe(save, initial_hash, adapter, output, module)
    return compare_oracle(save_path, initial_hash, oracle, source_manifest, adapter, module.UPSTREAM_PIN, output)


def prepare_format_probe(save, initial_hash, adapter, output, adapter_module):
    base = output / "base.pcsav"
    values = {}
    for name in ("credits", "medigel", "eezo", "iridium", "palladium", "platinum", "probes"):
        values[f"player.{name}"] = 2 if save.value(f"player.{name}") != 2 else 3
    values["player.current_fuel"] = 42.5
    values["location.x"] = save.value("location.x") + 16.0
    values["rotation.yaw"] = 1234
    for path, value in (("player.weapons.0.ammo_total", 20),
                        ("player.weapons.0.ammo_used_count", 1),
                        ("player.powers.0.rank", 1.0),
                        ("squad.0.character_level", 2),
                        ("squad.0.talent_points", 1),
                        ("squad.0.powers.0.rank", 1.0),
                        ("plot.integers.0", -123), ("plot.floats.0", 1.25)):
        try:
            save.value(path)
        except adapter_module.SaveError:
            continue
        values[path] = value
    for index in (0, 1, 31):
        path = f"plot.booleans.{index}"
        try:
            values[path] = not save.value(path)
        except adapter_module.SaveError:
            pass
    recipe = {"schema_version": 1, "id": "offline-format-probe-not-gameplay",
              "description": "Never load this format-test output as a story fixture",
              "expected_input_sha256": initial_hash,
              "expect": {path: save.value(path) for path in values}, "set": values}
    recipe_path = output / "recipe.json"
    recipe_path.write_text(json.dumps(recipe, indent=2) + "\n", encoding="utf-8")
    edited = output / "prepared.pcsav"
    receipt = output / "receipt.json"
    invocation = subprocess.run([sys.executable, str(adapter), "prepare", str(base),
                                 str(edited), "--recipe", str(recipe_path),
                                 "--receipt", str(receipt)], capture_output=True, text=True)
    if invocation.returncode:
        raise RuntimeError(invocation.stderr)


def compare_oracle(save_path, initial_hash, oracle, source_manifest, adapter, upstream_pin, output):
    base, edited = output / "base.pcsav", output / "prepared.pcsav"
    recipe_path, receipt = output / "recipe.json", output / "receipt.json"
    oracle_reports = []
    for name, path in (("base", base), ("prepared", edited)):
        report_path = output / f"{name}-tse.json"
        result = subprocess.run([str(oracle), str(path), str(report_path)], capture_output=True, text=True)
        (output / f"{name}-oracle.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"Independent oracle rejected {name}: {result.stderr}")
        report = json.loads(report_path.read_text(encoding="utf-8"))
        if (report.get("crc_valid") is not True or report.get("reserialized_identically") is not True
                or report.get("upstream_revision") != upstream_pin):
            raise AssertionError("Oracle did not confirm checksum and independent roundtrip")
        oracle_reports.append(normalized(report))
    before, after = oracle_reports
    actual_changes = {path for path in before.keys() | after.keys() if before.get(path) != after.get(path)}
    receipt_data = json.loads(receipt.read_text(encoding="utf-8"))
    expected_changes = {change["path"] for change in receipt_data["changes"]
                        if change["before"] != change["after"]}
    if actual_changes != expected_changes:
        raise AssertionError(f"Independent semantic diff mismatch: {actual_changes ^ expected_changes}")
    for change in receipt_data["changes"]:
        if before[change["path"]] != change["before"] or after[change["path"]] != change["after"]:
            raise AssertionError(f"Independent scalar mismatch: {change['path']}")
    if digest(save_path) != initial_hash:
        raise AssertionError("Original save changed during read-only probe")
    result = {"schema_version": 1, "gate": "offline-independent-save-format",
              "passed": True, "native_load": "untested", "gui_open": "untested",
              "input_sha256": initial_hash, "prepared_sha256": digest(edited),
              "adapter_sha256": digest(adapter), "oracle_sha256": digest(oracle),
              "source_manifest_sha256": digest(source_manifest),
              "cargo_lock_sha256": digest(source_manifest.parent / "Cargo.lock"),
              "recipe_sha256": digest(recipe_path), "receipt_sha256": digest(receipt),
              "source_revision": upstream_pin,
              "independently_verified_paths": sorted(change["path"] for change in receipt_data["changes"]),
              "actual_changed_paths": sorted(actual_changes)}
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("save", type=Path)
    parser.add_argument("oracle", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--adapter", type=Path,
                        default=Path(__file__).resolve().parents[1] / "le2-save.py")
    parser.add_argument("--prepared", type=Path)
    parser.add_argument("--recipe", type=Path)
    parser.add_argument("--receipt", type=Path)
    args = parser.parse_args()
    result = verify(args.save.resolve(strict=True), args.oracle.resolve(strict=True),
                    args.output.resolve(), args.adapter.resolve(strict=True),
                    prepared=args.prepared, recipe=args.recipe, receipt=args.receipt)
    print(json.dumps(result, indent=2))
