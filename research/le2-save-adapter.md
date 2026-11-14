<!-- SPDX-License-Identifier: MIT -->
# LE2 save fixture adapter

Date: 2026-11-13. Scope: offline LE2 save preparation for P9 validation. Native loading, progression, save/reload persistence and physical VR interactions remain untested by this work.

## Source and implementation choice

Format authority: [Trilogy Save Editor](https://github.com/KarlitosVII/trilogy-save-editor/tree/6451f23d1fba62358328dce4e87733970f36ebf1), KarlitosVII, version 2.2.1, pinned at `6451f23d1fba62358328dce4e87733970f36ebf1`, CeCILL-2.1. Inspected `src/save_data/mass_effect_2/{mod,player,squad,galaxy_map}.rs`, `src/save_data/shared/{mod,plot,player,appearance}.rs`, GUID serialization in `src/save_data/mod.rs`, and `src/unreal/deserializer.rs`. The upstream LE2 serialization test provides a second implementation to check against.

The inspected tree provides a Yew/Wasm application with private `save_data` and `unreal` modules in `src/main.rs`; it has no `src/lib.rs` or exposed batch-editing API. Its model types also use GUI-related macros and mutable reference wrappers. Direct library reuse would require an upstream refactor. A small Rust command-line fork could reuse its parser, but would bring the model/macro dependencies and CeCILL attribution/distribution obligations into a separate maintained component. That remains useful as an independent verification executable, rather than a prerequisite for every agent's preparation run.

Selected approach: an original, standard-library-only Python format adapter in [tools/le2-save.py](../tools/le2-save.py). No upstream code or save binary is copied. The schema follows the public serialized field order; the parser records scalar offsets, and editing patches only allowlisted fixed-size fields plus the checksum. This deliberately avoids a second general-purpose serializer and preserves strings, morph data, GUIDs, world state and unknown gameplay meanings byte for byte. Existing LE1 tools are retained unchanged. Add this source pin and format-only attribution to `NOTICE.md` when integrating; the new code and tests carry MIT SPDX headers.

LE2 uses little-endian version **30**, plain Unreal serialization and a final little-endian CRC-32/BZIP2. It is not the LE1 compressed container. Original ME2 version 29, Xbox saves, other games, bad checksums, invalid enums/booleans, non-finite floats, invalid strings, truncated collections and unparsed trailing bytes are rejected. A 64 MiB file limit and collection limits bound malformed input handling. The checksum matches the published `123456789` check value `FC891918`.

## Agent contract

```text
python tools/le2-save.py info INPUT --json
python tools/le2-save.py roundtrip INPUT NEW_OUTPUT --receipt NEW_RECEIPT.json
python tools/le2-save.py diff BEFORE AFTER --json
python tools/le2-save.py prepare INPUT NEW_OUTPUT --recipe RECIPE.json --receipt NEW_RECEIPT.json
```

All output files must be new, distinct paths. Existing files, including input/receipt aliases, are refused. Commands do not install saves, edit profiles, select Resume, alter config or launch games. Keep input/output saves, recipes bound to private saves, receipts and full `info` output in private storage. `info` includes character/world data and should not be committed.

`info --json` returns `schema_version: 1`, `game: "LE2"`, `save_version: 30`, lowercase SHA256 in `sha256`, and `data`. Data follows the upstream serialized field names, including `base_level_name`, `location`, `rotation`, `player`, `squad`, `plot`, `me1_plot`, `galaxy_map`, streaming/world records and DLC entries. Enum values are numeric upstream discriminants. Plot booleans expand the saved `u32` words least-significant bit first. Appearance maps use ordered `{key,value}` records so parsing need not discard a repeated key. These representations are inspection APIs, not new gameplay identifiers.

A preparation recipe has exactly these required keys:

```json
{
  "schema_version": 1,
  "id": "scanning-zero-probes",
  "expected_input_sha256": "REPLACE_WITH_64_LOWERCASE_HEX_DIGITS_FROM_PRIVATE_BASE",
  "expect": {"player.probes": 7, "base_level_name": "Biop_Nor"},
  "set": {"player.probes": 0}
}
```

This example is illustrative and intentionally unbound; obtain actual before-values from the selected save. Optional `description` and `source` strings record intent/provenance. Unknown keys and duplicate JSON keys are errors. Every edited path requires its before-value in `expect`; all expectations and the base hash must match before an output is created. Numeric float preconditions use the exact decoded value from `info`. Additional expectations may bind weapon/power names and squad tags to ensure an index still refers to the intended item. Empty `set` is valid for a hash- and precondition-checked baseline.

Editable fields:

| Group | Paths and limits |
|---|---|
| Resources | `player.credits`, `medigel`, `eezo`, `iridium`, `palladium`, `platinum`, `probes`: nonnegative signed 32-bit integers; `player.current_fuel`: finite nonnegative float32 |
| Existing weapon records | `player.weapons.N.ammo_used_count`, `ammo_total`: nonnegative signed 32-bit integers |
| Existing powers and squad | `player.powers.N.rank`, `squad.N.powers.N.rank`: finite nonnegative float32; `squad.N.character_level`, `talent_points`: nonnegative signed 32-bit integers |
| Player progression | `player.level`, `talent_points`: nonnegative signed 32-bit integers; `player.current_xp`: finite nonnegative float32 |
| Position/facing | `location.x/y/z`: finite float32; `rotation.pitch/yaw/roll`: signed 32-bit Unreal rotation units, not degrees |
| Current-game plot | `plot.booleans.N`: JSON boolean; `plot.integers.N`: signed 32-bit integer; `plot.floats.N`: finite float32 |

Only existing indices may be edited. No insertion, weapon/class reassignment, squad member creation, map replacement, visited-planet mutation, ME1-import plot mutation or automatic story recipe is supplied. Format verification establishes where these values serialize; it does **not** establish a weapon's clip capacity, native ammo accounting, a power's legal evolution, resource caps, a safe world position, or coherent story progression. Use the native game and independently inspected conditions to establish those meanings. Prefer a naturally obtained pre-beat save. Plot recipes must retain provenance for every dependency and remain unvalidated until ordinary loading/progression succeeds.

`prepare` emits a receipt with `schema_version`, `operation`, `game`, `save_version`, `input_sha256`, `output_sha256`, exact-file `recipe_sha256`, `recipe_id`, `upstream_pin`, `byte_identical`, `evidence`, and `changes`. Each change contains `path`, `before`, `requested`, actual encoded `after`, `offset`, and `size`. Float32 rounding is visible in `after`. Multiple edits within one plot word preserve each other and all unrelated bits. The receipt deliberately omits machine-specific input paths; hashes bind content identity. Receipts are provenance records, not signed attestations.

`roundtrip` validates the entire format/checksum and writes the original bytes unchanged. Its byte identity proves parser coverage and preservation, **not** independent reserialization. `diff` reports semantic field additions/removals/changes and byte identity; byte differences with no semantic changes can still occur when another editor re-encodes strings. Native validation must independently reopen prepared output with Trilogy Save Editor, then load it normally in ME2 and test persistence.

## Checks performed

`python tests/Le2SaveTests.py`: 13 tests, 12 passing and one optional corpus test skipped when no private corpus is supplied. Coverage includes an independently laid-out synthetic wire fixture, CRC known vector, byte-span isolation, shared plot-word masks, floats/overflow, input hash and before-value enforcement, malformed data, unsupported edits, duplicate keys, CLI receipts, output exclusivity and cleanup after a failed receipt write.

With `LE2_TEST_SAVE` set to a platform-path-separator-delimited list, all 13 tests pass against the upstream `test/ME2LeSave.pcsav` plus three existing private LE2 saves. Those four files parse to full payload EOF with valid CRC; a resource edit reparses and changes only the declared semantic field. No private save bytes were written back to the source files. The current private corpus is narrow and does not represent all classes, morphs, DLC or campaign stages.

Independent upstream decoder/editor reopening and native game loading are separate integration checks; they are not claimed by these tests. Integrator should add `tests/Le2SaveTests.py` to the Python CTest suite and record upstream verification evidence beside the broader validation report. No build targets, runtime code or LE1 behavior change in this work package.
