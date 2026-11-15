<!-- SPDX-License-Identifier: MIT -->
# LE3 read-only save foundation

2026-11-15. L3-0, based on `83539790`. `tools/le3-save.py` inspects little-endian
version-59 save prefixes and verifies the entire file's CRC32/BZIP2. It reuses our
MIT `le2-save.py` reader, bounds checks, checksum and exclusive output creation.
It does not copy the CeCILL codec or include game/save bytes.

Format authority is the local Trilogy Save Editor source pinned at
`6451f23d1fba62358328dce4e87733970f36ebf1`, specifically
`src/save_data/mass_effect_3/mod.rs` and `player.rs`, plus shared scalar types.
The [existing save-adapter research](le2-save-adapter.md) records the source and
licensing boundary. Its LE2 oracle does not yet validate this LE3 prefix decoder.

## What this establishes

The decoded prefix includes map identity, timestamp, saved location/rotation,
streamed-level records, placeables and player identity/class/level/mapped powers.
It stops **before player appearance**. Remaining player data, weapons, resources,
plot, galaxy, DLC and objective data are opaque bytes, not interpreted or validated.
A valid checksum and prefix do not establish that the entire save is structurally
valid, loads in the game, or forms a coherent progression fixture.

The six existing private ME3 saves passed prefix/CRC inspection and unchanged-byte
reconstruction in a read-only corpus run. No input changed. Synthetic negatives
exercise wrong game/version/endian, checksum, collection bounds, booleans, enums,
nonfinite values, strings and truncation. A deliberately invalid opaque tail is
accepted only as opaque; the result still says `full_decode: false`,
`fixture_ready: false`, `native_verified: false`. That boundary is tested.

## Existing-tool entry points

```powershell
py -3.12 tools/le3-save.py info <private-save> --json
py -3.12 tools/le3-save.py inventory <private-ME3-save-folder> --output <new-private-json>
py -3.12 tools/le3-save.py roundtrip <private-save> <new-private-copy> --receipt <new-private-json>
```

Full decoded output contains private character and world data. Keep it in an
ignored `build-*` folder or private evidence storage. Output and receipt paths must
be new and distinct; source paths and existing files are never overwritten.
`roundtrip` means an exact-byte no-op copy preserving the opaque remainder, **not**
independent decode/re-encode proof. Its receipt says `serialization_roundtrip: false`.
There is no `prepare` command; the inherited API is overridden to reject mutations.

`tools/qa-le2/game_contract.py` describes LE2 and LE3 without adding a competing
runner. The existing `session.py` preserves implicit LE2 for old plans and rejects
explicit LE3 native fixture sessions before path access. Scenario identity and
preparation receipts must match the selected game, version, base, output and recipe
hashes. An inspection/no-op-copy receipt cannot become preparation evidence.
LE3's native-session and mutation capabilities remain false. Source-format availability
in TSE must not be reported as working LE3 fixture preparation.

## Validation and remaining work

Passed: `tests/Le3SaveTests.py` (10 with private corpus), `tests/Le2SaveTests.py`
(15, optional corpus skipped), `tools/qa-le2/test-session.py` (34, Windows symlink
case skipped where unavailable), `test-scenario.py` (22), `test-workflow.py` (2).
Run the LE3 tests without `LE3_TEST_SAVE_ROOT` for synthetic-only CI. Coordinator
can register this existing-style test in CMake; this lane does not modify CMake.

Next: independently compare every decoded field with a pinned TSE ME3 oracle,
complete and validate the remaining schema, then authorize bounded fixture edits
with before-values and hashes. Native normal-load/save/reload and resource/story
acceptance remain separate gates. No runtime or installation was changed here.
