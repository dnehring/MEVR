<!-- SPDX-License-Identifier: MIT -->
# Offline resource and reload boundary batches

`prepare-variants.py` prepares zero, insufficient, exact and surplus resource balances from one hash-bound LE2 base save. It reuses `le2-save.py` for parsing, patching, checksums and semantic diff verification. It does not stage a profile, acquire a session lock, launch a game or promote a scenario.

```powershell
python tools/le2-save.py info <private-base.pcsav> --json
python tools/qa-le2/prepare-variants.py <private-base.pcsav> <new-private-batch-directory> --base-sha256 <lowercase-sha256> --spec tools/qa-le2/variant-specs/one-probe.json
```

The output directory must not exist and its parent must already exist. Put it in private storage outside live game/save/profile directories. Inputs and all output ancestors must be ordinary paths without symlinks/reparse points. No existing output is overwritten. Validation/preparation happens before creating the directory; handled failures and interruptions remove this invocation's partial directory. A force-killed process can leave a partial batch: **only a batch with `index.json` is complete**. Retain or remove incomplete private output deliberately; a rerun will refuse it.

Each spec describes one resource/action boundary. The resource must be `player.credits`, `player.medigel`, `player.eezo`, `player.iridium`, `player.palladium`, `player.platinum`, `player.probes` or `player.current_fuel`. Resource specs cannot edit ammo. The explicit reload spec below permits only two existing weapon ammo fields; powers, plot and location remain outside this tool's scope. For other actions, copy the probe spec privately and provide explicit values and provenance:

- `cost`: positive resource cost for the stated action.
- `quantum`: positive deficit used to form `insufficient = cost - quantum`; it must not exceed cost.
- `surplus`: **total starting balance**, strictly greater than cost, not an amount added to cost.
- `provenance`: `kind` (`source-inspected` or `native-measured`), a precise source/artifact `reference`, its lowercase `sha256`, and a `description` explaining the cost, chosen quantum/surplus and limitations.
- `nativeValidation`: always `pending`. This generator cannot accept or establish native readiness, even when supplied cost evidence came from a measured native run.

Costs and all balances for integer resources must fit nonnegative signed 32-bit integers. Fuel may use finite numbers; requested and encoded float32 balances are both recorded. Cost is encoded to float32 for the exact case, so native assertions must compare against the recorded `expectedValue`, not silently assume decimal precision. Distinct insufficient/exact/surplus boundaries that collapse after float32 encoding are rejected. Positive insufficient values that underflow to zero are also rejected.

When cost equals quantum, insufficient is zero. Both case labels then reference the **same** zero save, recipe and receipt; `equivalentTo: "zero"` makes that equivalence explicit. For the supplied one-probe spec, the unique balances are 0, 1 and 2. This is three different preparations, not four independent boundary outcomes.

The supplied [one-probe spec](variant-specs/one-probe.json) cites the inspected native `SFXGameModeOrbital.LaunchProbe` source and package hash recorded in [fixture acquisition](../../research/le2-fixture-acquisition.md) and [galaxy research](../../research/le2-galaxy-fixture-source.md). It reflects one probe deducted on a legal launch. Scanning mode, in-flight/impact state and other native gates still apply; sufficient probes alone do not guarantee a launch. No fuel/travel-cost template is supplied because route/action cost must come from specific inspected or measured evidence.

Provenance is a declared, hash-bound reference; the generator does not fetch or independently interpret that artifact. Review it and its applicability to the installed build. Do not label invented costs as evidence. Resource caps, action validity, intended story state, actual rendering and persistence require native validation.

The batch contains:

- `base.pcsav` and `spec.json`: frozen input bytes.
- `<case>.pcsav`, `<case>.recipe.json`, `<case>.receipt.json`: new saves and standard adapter provenance, omitting a redundant insufficient file when equivalent to zero.
- `index.json`: input/spec/tool hashes, per-file hashes/sizes, requested/expected balances, equivalence and case artifact references. Status is `offline-prepared`; scenario, native and VR gates remain pending.

Each recipe independently binds the original base hash and before-value. Receipt hashes bind the exact recipe bytes and output. A batch's index does not hash itself; retain its hash in the consuming workflow if needed. All products are private; the index and frozen save can contain user state. Verify index file hashes before use, then perform independent editor/oracle checking and the protected normal-load workflow in [README.md](README.md). Bind each case's actual recipe and expected balance in its scenario; never infer gameplay success from a generated label.

Tests: `python tools/qa-le2/test-variants.py`. Optional `LE2_TEST_SAVE` supplies a platform-path-separator-delimited list of real saves for read-only corpus preparation into temporary directories. These tests remain fully offline.

## Reload ammo batches

Use the **same CLI** with a private spec whose `kind` is `reload-ammo`. All existing
hash binding, nonreplacing publication, receipts, constrained semantic diff and
rollback behavior apply. No new runtime harness or weapon-selection action is
introduced. These products are preparations for normal-load validation, not
accepted reload fixtures.

Required spec keys (no arbitrary edits or extra keys):

- `schemaVersion: 1`, an `id` using the existing lowercase/hyphen convention,
  `kind: "reload-ammo"`, and `nativeValidation: "pending"`.
- `weapon`: exactly `index` (zero-based existing save record), `className` (exact
  bare saved class name), and `runtimeClass` (exact observed package-qualified
  class). The runtime class must be in the existing audited
  [reserve policy](weapon-policy.py). This list does not prove finite-ammo
  eligibility. The saved name must match its suffix and occur exactly once at
  the specified record index; duplicate class records are refused because the
  native loader finds records by that name.
- `capacity`: operator-supplied, actually observed positive integral magazine
  capacity for this runtime class/build, below `2^24`. Do not infer it from a
  weapon family name or saved ammo count.
- `partialUsed`: explicit missing-round count, less than capacity and at least
  two where possible. Capacity two permits one. Capacity one requires `null`
  because no integral partial magazine exists.
- `surplusReserve`: total starting reserve, strictly greater than capacity and
  at most `2^24`. These exact float32 integer limits prevent rounding from
  erasing the intended source-derived reserve boundaries. They do not establish
  the game's legal reserve cap.
- `ammoPolicy: "ordinary-finite"`: explicit reviewed applicability attestation.
  Verify the actual class, heat/infinite policy and normal reload behavior;
  absence of an infinite-ammo flag alone is insufficient.
- `provenance`: exactly `kind: "native-measured"`, `reference`, lowercase
  `sha256`, and `description`. Bind the archived capacity/weapon observation and
  explain the applicability of the finite-ammo policy and chosen values. The
  generator records this attestation; it does not interpret or authenticate the
  referenced artifact. Synthetic test fixtures are not native evidence.

The base hash still comes from mandatory `--base-sha256`. Original used count
must already be between zero and the attested capacity; original reserve must
be nonnegative and within the exact integer domain. Only
`player.weapons.INDEX.ammo_used_count` and `.ammo_total` are edited. Recipe
preconditions include the exact saved class, its `current_weapon` flag and both
original ammo values. No weapon is inserted, equipped, renamed or removed.

Let `C` be capacity, `U` the chosen partial used count and `R` surplus reserve.
For capacity one only, `U=C` and the reserve tests are explicitly marked as
potentially confounded by automatic reload.

| Case | Used count | Reserve |
| --- | --- | --- |
| `full-magazine` | 0 | R |
| `partial` | U | R; omitted for capacity one |
| `empty` | C | R |
| `no-reserve` | U | 0 |
| `insufficient-reserve` | U | U-1 |
| `exact-reserve` | U | U |
| `surplus-reserve` | U | R |

Reserve boundaries use a partially loaded magazine where possible to reduce
empty-magazine auto-reload ambiguity. `surplus-reserve` references the identical
`partial` fixture, or `empty` for capacity one. When U is one, insufficient and
no-reserve also share one artifact. Every equivalence is explicit in the index;
omitted partial cases have a reason, not a fabricated result.

The reload index retains requested/expected save-field values, the derived
`expectedLoadedAtAttestedCapacity`, `boundaryUsed`, original values, capacity and
policy attestation, policy-file hash, and saved selection information. LE2 v30
has per-record `current_weapon` flags, not a serialized `current_weapon_class`
scalar. Multiple current flags are refused. No current flag or a different
selected record remains explicit; `targetIsSavedCurrent` is false and
`actualSelectedWeaponVerified` is always false. Even a true saved flag does not
prove the later live pawn's selected weapon. Full runtime class binding remains
operator-attested because the save stores only the bare class name.

Recheck each final prepared triple with the existing independent save oracle,
then use normal protected loading and correlated weapon observation to establish
actual selection, capacity, used/loaded/effective reserve and finite policy.
Keep cancellation, pickup/grant/switch isolation, native reload timing, save/reload
persistence and VR acceptance gates from the reload scenario. A batch never
promotes scenario readiness or proves the authored values survived normal load.
