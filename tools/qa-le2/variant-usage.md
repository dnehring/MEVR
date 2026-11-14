<!-- SPDX-License-Identifier: MIT -->
# Offline resource boundary batches

`prepare-variants.py` prepares zero, insufficient, exact and surplus resource balances from one hash-bound LE2 base save. It reuses `le2-save.py` for parsing, patching, checksums and semantic diff verification. It does not stage a profile, acquire a session lock, launch a game or promote a scenario.

```powershell
python tools/le2-save.py info <private-base.pcsav> --json
python tools/qa-le2/prepare-variants.py <private-base.pcsav> <new-private-batch-directory> --base-sha256 <lowercase-sha256> --spec tools/qa-le2/variant-specs/one-probe.json
```

The output directory must not exist and its parent must already exist. Put it in private storage outside live game/save/profile directories. Inputs and all output ancestors must be ordinary paths without symlinks/reparse points. No existing output is overwritten. Validation/preparation happens before creating the directory; handled failures and interruptions remove this invocation's partial directory. A force-killed process can leave a partial batch: **only a batch with `index.json` is complete**. Retain or remove incomplete private output deliberately; a rerun will refuse it.

Each spec describes one resource/action boundary. The resource must be `player.credits`, `player.medigel`, `player.eezo`, `player.iridium`, `player.palladium`, `player.platinum`, `player.probes` or `player.current_fuel`. Ammo, powers, plot and location fields are intentionally outside this tool's scope. For other actions, copy the probe spec privately and provide explicit values and provenance:

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
