<!-- SPDX-License-Identifier: MIT -->
# ME2 validation workflow

This is the P9 tooling for the [validation goal](../../docs/le2-validation-goal.md).
Save preparation and protected session lifecycle have offline tests. The three
public scenarios are **drafts**, pending naturally acquired fixtures and native
observations. Neither a valid manifest nor a green integrity audit proves that
ME2 loaded, that a story edit is coherent, or that a VR interaction works.

Read [save format and recipe details](../../research/le2-save-adapter.md),
[coverage and remaining work](../../docs/le2-validation-coverage.md), and
[current evidence](../../research/le2-validation-evidence.md).

## 1. Inspect and prepare a private fixture

Use Python 3.10 or newer. Put every save, bound recipe, full inspection and capture
in private storage outside the user's live profile. `local-tools/` is ignored in
this repository. Keep the original save immutable and record its SHA256.

```powershell
python tools/le2-save.py info <base.pcsav> --json
python tools/le2-save.py prepare <base.pcsav> <new-prepared.pcsav> --recipe <recipe.json> --receipt <new-receipt.json>
python tools/le2-save.py diff <base.pcsav> <new-prepared.pcsav> --json
```

Angle-bracket arguments throughout this guide are placeholders. Outputs must be
new files. Recipes bind the base hash and expected before-value for every edit.
Only existing fixed-size fields can be patched; no squad/weapon record insertion
or automatic mission unlock is implemented. Resource types are format-verified;
legal game limits and progression dependencies need native verification.

For story beats, acquire normal before/after saves and use Legendary Explorer to
inspect the relevant conditions, conversations and script dependencies. Record
the source of every plot identifier and all dependent saved state. Do not promote
a raw plot edit into a named validated recipe until ordinary progression works.

## 2. Independently check save encoding

The optional oracle uses Trilogy Save Editor's actual pinned models and binary
codec, independently of our Python implementation. It stages source from Git
blobs, omits only UI derives, retains upstream licenses and records file hashes.
Its Rust dependencies are locked. Source and built artifacts remain private.

```powershell
python tools/qa-le2/build-tse-oracle.py reference/trilogy-save-editor <new-private-project>
cargo build --manifest-path <new-private-project>/Cargo.toml --release --locked
python tools/qa-le2/check-save-oracle.py <base.pcsav> <new-private-project>/target/release/mevr-tse-oracle.exe <new-private-probe-directory>
```

Use upstream revision `6451f23d1fba62358328dce4e87733970f36ebf1`. Record `rustc -Vv`,
the build command and build output alongside `source-manifest.json`, Cargo.lock
and the executable. The cross-check binds source, executable, adapter, recipe and
save hashes. It requires checksum validation, byte-identical independent
reserialization, matching edited values and no other semantic changes.

**The probe deliberately modifies arbitrary plot/position fields to test the
format. Never use its output as a gameplay fixture.** This is headless source
verification, not an editor GUI or native loading test. For a chosen gameplay
fixture, invoke the built oracle directly with INPUT_SAVE and NEW_OUTPUT_JSON;
then independently inspect it in the editor and perform native load/save/reload.

## 3. Bind a scenario before attempting it

Copy `scenarios/reload.json`, `dialogue-interrupt.json` or `planet-scanning.json`
to private storage. Complete its acquisition tasks. Bind the actual base hash,
recipe path/hash, starting map and explicit state checks, installation/DLC
prerequisites, case-specific resource costs and artifact expectations. Keep
unverified details marked pending. `ready` means prepared for an attempt; it does
not mean passed. Preserve the public draft as a reusable template.

```powershell
python tools/qa-le2/scenario.py validate <bound-scenario.json> --for-run
```

Use a separate bound scenario/recipe where variants need different starting
values. Capture load selection and the rendered scene as well as state; a map
string alone is insufficient.

## 4. Protect and stage a native session

Coordinate exclusive ownership first. The runner refuses active game/launcher/VR
processes, an existing `qa-cafeteria.lock`, or `game-slot/lock.json`. It never
deletes another owner's lock, runs an old slot rollback, or switches OpenXR.
These locks require cooperating agents; older tooling is not a universal mutex.

Create a private plan with this shape, replacing all placeholders:

```json
{
  "schemaVersion": 1,
  "coordinatedOwner": "confirmed owner and handoff reference",
  "profileRoot": "ABSOLUTE complete MELE documents profile",
  "gameRoot": "ABSOLUTE Legendary Edition installation",
  "exeSha256": "SHA256 of Game/ME2/Binaries/Win64/MassEffect2.exe",
  "recipePath": "ABSOLUTE bound recipe.json",
  "fixture": {
    "path": "ABSOLUTE prepared.pcsav",
    "sha256": "prepared save SHA256",
    "receiptPath": "ABSOLUTE preparation receipt.json",
    "targetRelativePath": "Save/ME2/DEDICATED_CAREER/Save_0001.pcsav"
  },
  "scenario": {"path": "ABSOLUTE bound scenario.json", "sha256": "scenario SHA256"},
  "additionalProtectedPaths": []
}
```

Choose a dedicated career/slot discoverable by normal Load Game. Do not assume an
arbitrary directory name is accepted; first establish the game's career naming
and selection behavior. The runner creates a new save slot and refuses overwrite.
It does not rewrite Resume/profile selection using LE1 offsets.

```powershell
python tools/qa-le2/session.py prepare <plan.json> <new-private-run-directory>
python tools/qa-le2/session.py request-load <run-directory>
```

Preparation freezes the scenario, recipe, receipt and save; backs up the complete
MELE profile, LE2 BioGame/Config and whole LE2 Win64 directory; and verifies copies
before staging. Use a run directory separate from all protected roots with enough
space for backups and post-test archives. Additional modified paths must be in
the plan before preparation.

`request-load` records an operator handoff; it does not launch a game. Start ME2
through the normal Steam/EA launcher, then bind the actual process and select the
staged save through Load Game:

```powershell
python tools/qa-le2/session.py bind-process <run-directory> <ME2-PID>
python tools/qa-le2/scenario.py check-start <run-directory>/scenario.json <observed-start.json> --recipe <run-directory>/recipe.json
```

Process binding checks executable path, hash and start identity. Automatic
launcher/profile selection and an LE2 runtime-state producer are not supplied.
Starting-state JSON must come from observed game state with capture/provenance;
do not relabel save inspection or expected values as runtime observations.

## 5. Exercise, inspect and reset

Run the chosen case through normal controls and record actual input, scene,
before/after state and any resulting saves. First establish the native baseline;
use real controller input for VR acceptance. Record zero/insufficient/exact/surplus
resources where relevant. Diagnostic grants, direct completion calls and
teleports are setup evidence only.

```powershell
python tools/qa-le2/session.py attach <run-directory> <artifact1> <artifact2>
python tools/qa-le2/session.py status <run-directory>
```

Attach archives evidence without accepting it. Build an evidence manifest using
`scenario.py`'s documented identity, artifact and review contract. Hash every
artifact and associate it with the same run/build/fixture/case. Retain original
captures and producer provenance; an attributed review remains a review, not an
automated measurement. Close the owned game and VR processes normally, then:

```powershell
python tools/qa-le2/session.py reset <run-directory>
python tools/qa-le2/scenario.py audit <run-directory>/scenario.json <evidence.json> --recipe <run-directory>/recipe.json --session <run-directory>
```

Reset verifies backups, archives test output (including newly generated files),
restores bytes/directory presence/file timestamps and releases only its own lock.
An interrupted reset can retry; an incomplete backup or corrupt backup refuses
restoration. Keep the run and recovery context until recovery is verified. Never
replace an old session context with a newer run or force a reset over later user
work. Audit returns nonzero for pending/failed assertions and never declares full
port acceptance. `--session` binds evidence identity to the protected context and
frozen preparation receipt. Exit zero also requires recorded process binding and
verified restoration. A successful native baseline can still exit 2 because VR
and headset assertions remain pending: inspect `gates.native` and preserve the
other pending gates instead of trying to force an overall pass.
Keep automated, native, stereo, physical-headset, performance
and campaign/DLC outcomes separate.

## Offline checks

```powershell
python tests/Le2SaveTests.py
python tools/qa-le2/test-session.py
python tools/qa-le2/test-scenario.py
python tools/qa-le2/test-oracle.py
python tools/qa-le2/test-workflow.py
```

`LE2_TEST_SAVE` optionally supplies semicolon-separated private/upstream saves on
Windows. Default tests use temporary synthetic fixtures/fake installations and
must not start the game. CTest exposes the same checks. Follow the evidence report
for exact coverage and skipped checks; passing offline tests does not promote any
public scenario to native or VR validated.
