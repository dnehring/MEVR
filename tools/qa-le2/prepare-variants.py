# SPDX-License-Identifier: MIT
"""Prepare offline LE2 resource or reload-ammo boundary cases in a new private batch directory.

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


def common_spec(spec, required):
    if not isinstance(spec, dict) or not required <= spec.keys() or spec.keys() - required - {"spdxLicenseIdentifier"}:
        raise VariantError("Spec has missing or unknown keys")
    if type(spec["schemaVersion"]) is not int or spec["schemaVersion"] != 1:
        raise VariantError("Unsupported spec schemaVersion")
    if not isinstance(spec["id"], str) or not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,79}", spec["id"]):
        raise VariantError("Spec id must use 1-80 lowercase letters, digits or hyphens")
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


def boundaries(spec):
    common_spec(spec, {"schemaVersion", "id", "resource", "cost", "quantum", "surplus", "provenance", "nativeValidation"})
    if not isinstance(spec["resource"], str) or spec["resource"] not in RESOURCE_PATHS:
        raise VariantError("Spec must target an allowlisted resource field")
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


def reload_cases(spec, save):
    common_spec(spec, {"schemaVersion", "kind", "id", "weapon", "capacity", "partialUsed", "surplusReserve",
                       "ammoPolicy", "provenance", "nativeValidation"})
    if spec['kind'] != 'reload-ammo' or spec['ammoPolicy'] != 'ordinary-finite':
        raise VariantError('Reload spec requires ordinary-finite ammoPolicy')
    if spec['provenance']['kind'] != 'native-measured':
        raise VariantError('Reload capacity requires native-measured provenance for the exact runtime class')
    weapon = spec['weapon']
    if not isinstance(weapon, dict) or set(weapon) != {'index', 'className', 'runtimeClass'}:
        raise VariantError('Reload weapon requires exact index, className and runtimeClass')
    policy_path = Path(__file__).with_name('weapon-policy.py')
    policy_spec = importlib.util.spec_from_file_location('le2_variant_weapon_policy', policy_path)
    policy = importlib.util.module_from_spec(policy_spec)
    policy_spec.loader.exec_module(policy)
    if not isinstance(weapon['runtimeClass'], str) or weapon['runtimeClass'] not in policy.RESERVE_CLASSES:
        raise VariantError('Unsupported runtime weapon class; use the existing audited reserve policy')
    if weapon['className'] != weapon['runtimeClass'].rsplit('.', 1)[1]:
        raise VariantError('Saved bare className must match the observed runtimeClass exactly')
    records = save.value('player.weapons')
    index = weapon['index']
    if type(index) is not int or index < 0 or index >= len(records):
        raise VariantError('Weapon index outside existing save records')
    matches = [i for i, row in enumerate(records) if row['class_name'] == weapon['className']]
    if matches != [index]:
        raise VariantError('Missing, mismatched or ambiguous saved weapon class/index')
    current = [i for i, row in enumerate(records) if row['current_weapon']]
    if len(current) > 1:
        raise VariantError('Ambiguous saved current_weapon selection')
    capacity, partial, surplus = spec['capacity'], spec['partialUsed'], spec['surplusReserve']
    if type(capacity) is not int or not 1 <= capacity < 2**24:
        raise VariantError('Observed capacity must be a positive exact float32-domain integer below 2^24')
    if type(surplus) is not int or not capacity < surplus <= 2**24:
        raise VariantError('surplusReserve must exceed capacity within the exact float32 integer domain')
    if capacity == 1:
        if partial is not None:
            raise VariantError('Capacity one has no integral partial magazine; partialUsed must be null')
    elif type(partial) is not int or not (min(2, capacity-1) <= partial < capacity):
        raise VariantError('partialUsed must leave rounds loaded and miss at least two where possible')
    row = records[index]
    if not 0 <= row['ammo_used_count'] <= capacity or not 0 <= row['ammo_total'] <= 2**24:
        raise VariantError('Original ammo outside observed capacity/exact nonnegative integer range')
    prefix = f'player.weapons.{index}'
    used_path, reserve_path = prefix+'.ammo_used_count', prefix+'.ammo_total'
    deficit = capacity if partial is None else partial
    amounts = {'full-magazine': (0, surplus)}
    if partial is not None:
        amounts['partial'] = (partial, surplus)
    amounts.update({'empty': (capacity, surplus), 'no-reserve': (deficit, 0),
                    'insufficient-reserve': (deficit, deficit-1), 'exact-reserve': (deficit, deficit),
                    'surplus-reserve': (deficit, surplus)})
    cases = {label: {used_path: used, reserve_path: reserve} for label, (used, reserve) in amounts.items()}
    selection = dict(currentRecordIndex=current[0] if current else None,
                     currentClassName=records[current[0]]['class_name'] if current else None,
                     targetIsSavedCurrent=current == [index], actualSelectedWeaponVerified=False,
                     reason='LE2 v30 stores per-record current_weapon flags, not current_weapon_class; live pawn selection must be observed')
    metadata = dict(kind='reload-ammo', weapon=weapon, capacity=capacity, partialUsed=partial,
                    surplusReserve=surplus, ammoPolicy=spec['ammoPolicy'], boundaryUsed=deficit,
                    capacityEvidence='operator-attested native measurement; artifact not independently interpreted',
                    runtimeClassBinding='operator-attested full class; save record stores bare class name',
                    savedSelection=selection, weaponPolicySha256=sha(policy_path.read_bytes()),
                    originalValues={used_path: row['ammo_used_count'], reserve_path: row['ammo_total']},
                    omittedCases={'partial': 'capacity-one-has-no-integral-partial-magazine'} if partial is None else {},
                    autoReloadAmbiguous=partial is None,
                    expectedValuesAre='save-field preparations; post-load ammo, capacity, finite policy and native legality unverified')
    expectations = {prefix+'.class_name': weapon['className'], prefix+'.current_weapon': row['current_weapon'],
                    **metadata['originalValues']}
    return cases, expectations, metadata


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
    """Build one spec's cases. Return index; rollback owned output on failure."""
    if not valid_sha(base_sha256):
        raise VariantError("--base-sha256 requires 64 lowercase hexadecimal digits")
    base_path, output, spec_path = map(no_links, (base_path, output, spec_path))
    if output.exists():
        raise VariantError("Batch output must be a NEW directory")
    if not output.parent.is_dir():
        raise VariantError("Batch parent must already exist")
    spec_raw = spec_path.read_bytes()
    spec = read_spec(spec_raw)
    reload = isinstance(spec, dict) and spec.get('kind') == 'reload-ammo'
    if not reload:
        requested, encoded, equivalent = boundaries(spec)
    adapter = load_adapter()
    save = adapter.Save.load(base_path)
    if sha(save.raw) != base_sha256:
        raise VariantError("Base save hash does not match --base-sha256")
    if reload:
        case_values, expectations, metadata = reload_cases(spec, save)
    else:
        before = save.value(spec['resource'])
        case_values = {label: {spec['resource']: requested[label]} for label in LABELS}
        expectations = {spec['resource']: before}
        metadata = dict(resource=spec['resource'], cost=spec['cost'], quantum=spec['quantum'],
                        surplus=spec['surplus'], originalValue=before)
    index = dict(schemaVersion=1, id=spec['id'], game='LE2', **metadata, baseSaveSha256=base_sha256,
                 specSha256=sha(spec_raw), generatorSha256=sha(Path(__file__).read_bytes()), adapterSha256=sha(ADAPTER_PATH.read_bytes()),
                 status='offline-prepared', scenarioReadiness='pending', nativeValidation='pending', vrValidation='pending',
                 provenance=spec['provenance'], cases={}, files={})
    # Prepare/reparse/diff every unique variant before owning any output directory.
    variants, canonical_values = {}, {}
    for label, edits in case_values.items():
        signature = tuple(sorted(edits.items()))
        canonical = canonical_values.get(signature, label)
        if canonical == label:
            canonical_values[signature] = label
            recipe = dict(schema_version=1, id=f"{spec['id']}-{label}", expected_input_sha256=base_sha256,
                          description=f"Offline {'reload ammo' if reload else 'resource'} {label} boundary; native/scenario readiness pending",
                          source=f"{spec['provenance']['reference']} SHA256 {spec['provenance']['sha256']}; spec SHA256 {sha(spec_raw)}",
                          expect=expectations, set=edits)
            result, changes = save.prepare(recipe)
            after = {path: result.value(path) for path in edits}
            expected = edits if reload else {spec['resource']: encoded[label]}
            if after != expected:
                raise VariantError(f'Adapter encoded unexpected {label} values')
            actual_paths = {change['path'] for change in adapter.diff(save, result)}
            expected_paths = {path for path, value in after.items() if save.value(path) != value}
            if actual_paths != expected_paths:
                raise VariantError('Adapter changed an undeclared field')
            recipe_raw = json_bytes(recipe)
            receipt = adapter.make_receipt('prepare', save, result, changes, recipe=recipe, recipe_raw=recipe_raw)
            variants[label] = (result.raw, recipe_raw, json_bytes(receipt))
        case = dict(fixture=f'{canonical}.pcsav', recipe=f'{canonical}.recipe.json', receipt=f'{canonical}.receipt.json',
                    equivalentTo=canonical if canonical != label else None,
                    interpretation='ammo fields only; native legality/selection and resulting state untested' if reload else
                                   'balance only; action legality and resulting state untested')
        if reload:
            case.update(requestedValues=edits, expectedValues=edits,
                        expectedLoadedAtAttestedCapacity=spec['capacity']-edits[f"player.weapons.{spec['weapon']['index']}.ammo_used_count"])
        else:
            case.update(requestedValue=requested[label], expectedValue=encoded[label], expectedResourcePath=spec['resource'])
        index['cases'][label] = case
    # Exclusive mkdir is the ownership boundary. Never clean an existing batch.
    output.mkdir()
    try:
        payloads = {"base.pcsav": save.raw, "spec.json": spec_raw}
        for label, (raw, recipe_raw, receipt_raw) in variants.items():
            payloads.update({f"{label}.pcsav": raw, f"{label}.recipe.json": recipe_raw, f"{label}.receipt.json": receipt_raw})
        for name, raw in payloads.items():
            write_new(output / name, raw)
            index["files"][name] = {"sha256": sha(raw), "bytes": len(raw)}
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
