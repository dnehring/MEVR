<!-- SPDX-License-Identifier: MIT -->
# LE3 save codec and private resource variants

Updated 2026-11-15, L3-0. `tools/le3-save.py` now decodes the complete known little-endian v59
structure and CRC32/BZIP2, reconstructs every field, and requires byte-identical no-op output.
This supersedes the earlier prefix-only inspection described by the initial foundation commit.
Normal loading, save/reload persistence, progression and VR acceptance remain **untested**.

## Reuse and provenance

The adapter independently describes wire-format facts from the local Trilogy Save Editor source,
pinned at `6451f23d1fba62358328dce4e87733970f36ebf1`: `src/save_data/mass_effect_3/{mod,player,
squad,plot,galaxy_map}.rs` and shared appearance/player/plot/GUID structures. It reuses our MIT
LE2 reader, bounds checks, CRC, scalar spans, recipe parser, semantic diff and exclusive output
creation. No CeCILL codec implementation was copied into the adapter. The shared oracle generator
stages unchanged upstream Git blobs and original licenses into ignored private storage; generated
source and executables are neither committed nor shipped. See [LE2 provenance](le2-save-adapter.md).

Full coverage includes appearance and optional head morphs, evolved-power choices, war assets,
weapons/ammunition/mods, player and squad resources, keyed plot integers/floats, imported ME1 plot,
player variables, journal/codex, planets and system Reaper state, DLC, treasure, GUID collections,
conversation mode and objective records. These are serialized fields, not verified gameplay meanings.
Version 59 is also used by original ME3; the header alone cannot prove Legendary Edition provenance.
The private corpus came from the installed Legendary Edition ME3 profile.

Ordered maps are exposed as `{key,value}` records. Plot keys are **IDs**, not dense array indices.
Duplicate keys, invalid booleans/enums/strings, nonfinite floats, oversized counts, truncation and
unknown trailing structures are rejected. There is no opaque unvalidated tail anymore: unsupported
structures fail closed. Unknown gameplay identifiers/meanings remain typed values and are preserved.
No-op serialization rebuilds values rather than concatenating source spans; it retains only original
string encoding/empty-string representation choices. This also preserves negative zero and GUID bytes.

## Agent entry points

```powershell
py -3.12 tools/le3-save.py info <private-save> --json
py -3.12 tools/le3-save.py inventory <private-ME3-save-folder> --output <new-private-json>
py -3.12 tools/le3-save.py roundtrip <private-save> <new-private-copy> --receipt <new-private-json>
py -3.12 tools/le3-save.py prepare <private-base> <new-private-copy> --recipe <private-recipe.json> --receipt <new-private-json>
```

All files must be new and distinct; no command installs saves, overwrites sources, launches a game or
changes progression in the live profile. Keep outputs in ignored `build-*` storage. Full inspection
and oracle JSON contain private character/world data and must not be published.

Recipes use LE2's schema plus mandatory `game: "LE3"` and `save_version: 59`. The exact input SHA-256
and all edited before-values must match. For example, after obtaining the real hash and credits value:

```json
{
  "schema_version": 1,
  "id": "private-zero-credits",
  "game": "LE3",
  "save_version": 59,
  "expected_input_sha256": "REPLACE_WITH_THE_PRIVATE_BASE_HASH",
  "expect": {"player.credits": 1234},
  "set": {"player.credits": 0}
}
```

The example is intentionally unbound and refused as written. Allowed resource fields are:

| Paths | Type and additional preconditions |
|---|---|
| `player.credits`, `player.medigel`, `player.grenades` | Nonnegative signed 32-bit integer; booleans/fractions refused |
| `player.current_fuel`, `player.health` | Finite nonnegative float32; overflow refused, actual rounding recorded |
| `player.weapons.N.ammo_used_count`, `.ammo_total` | Nonnegative signed 32-bit integer; existing index and matching `.class_name` in `expect` |
| `squad.N.grenades` | Nonnegative signed 32-bit integer; existing index and matching `.tag` in `expect` |
| `squad.N.weapons.N.ammo_used_count`, `.ammo_total` | As above, with both squad tag and weapon class expectations |

These are wire bounds, **not game caps**. Native ammunition accounting, grenade/medigel limits, health
semantics and fuel capacity still require observation. Legacy mining-resource fields are inspectable
but not editable. No story/plot edits, map teleport, weapon/class changes, power evolution/rank changes,
war assets or Reaper-state edits are enabled. No list/map insertions or deletions are supported.
Every preparation first performs a full byte-identical reconstruction, patches only declared scalar
spans plus CRC, reparses, and reconstructs the result. Receipts record exact base/output/recipe hashes,
changes, offsets, sizes and rounded values. `full_decode` and `serialization_roundtrip` are true;
`fixture_ready` and `native_verified` remain false. Offline resource support does not enable the native
QA session: `tools/qa-le2/game_contract.py` still refuses LE3 sessions.

## Independent validation before private variants

Reuse the existing tools, with a newly staged oracle supporting v30 and v59:

```powershell
py -3.12 tools/qa-le2/build-tse-oracle.py <pinned-TSE-checkout> <new-private-oracle-project>
cargo build --manifest-path <new-private-oracle-project>/Cargo.toml --release --locked --offline
py -3.12 tools/qa-le2/check-save-oracle.py <private-base> <oracle.exe> <new-private-check-directory> --adapter tools/le3-save.py
```

For LE3, the checker first asks TSE to decode and independently serialize the original source, requires
exact original bytes, and compares **every field** against our decoder. Only then does it create any
save copies and the bounded resource probe. It verifies independent reconstruction and full-field
equality again for baseline and edited copies, checks the exact semantic diff, and re-hashes the
unchanged source. JSON normalization only bridges enum names, GUID tuples, color arrays, keyed maps,
packed plot words and float32 formatting. Extra/missing fields fail. The probe is a format test,
not an approved gameplay fixture. `--prepared`, `--recipe` and `--receipt` can verify a chosen variant
through the existing exact-artifact workflow. A passing report explicitly leaves GUI and native loading
untested; hashes are provenance records, not signed attestations.

## Evidence and remaining gates

The six existing private ME3 saves pass full parsing, exact reconstruction and independent TSE full-field
comparison. Resource probes are private clones and change only declared fields. Source saves are read-only
and re-hashed after validation. This narrow corpus does not cover six classes, every DLC, progression beat
or native combat fixture. No new checkpoint has been acquired by this work.

Synthetic tests exercise independently laid-out wire records, enum/CRC/string/count/truncation failures,
unknown tails, duplicate map keys, complete serialization, typed bounds, fixed-byte isolation, identity
preconditions, unsupported story changes and output exclusivity. The oracle test also proves an independent
baseline failure publishes no save clone or passing receipt. Optional `LE3_TEST_SAVE_ROOT` and
`LE3_TSE_ORACLE` enable private-corpus integration in `tests/Le3SaveTests.py`. Existing LE2 and QA tests remain
required because the shared oracle builder/checker and game description were extended.

Validation at handoff: LE3 suite 14/14 with both private-corpus options enabled; existing
oracle suite 14/14 with the extended oracle and upstream LE2 reference fixture; LE2 codec
15 tests (one optional private-corpus skip); QA session suite 34 tests (one Windows symlink
skip). Six retained private oracle reports cover credits, fuel, grenades, health, medigel,
player ammo total/used and squad grenades where present. Squad weapon-ammo edits have synthetic
byte-isolation coverage; no private corpus record exercised that edit. SPDX and diff checks pass.

Next gate: select a naturally obtained checkpoint; review a resource-only variant and its independent
receipt; load normally in ME3 under the protected session; verify observed resources and native inventory;
save/reload and confirm persistence; restore profile hashes. Combat availability, story coherence, galaxy
alerts, six-class behavior and physical VR acceptance must be proven separately. Never treat a successful
codec or an offline report as a passed gameplay fixture.
