<!-- SPDX-License-Identifier: MIT -->
# ME2 validation workflow

This is the P9 tooling for the [validation goal](../../docs/le2-validation-goal.md).
Read the [scripted testing handoff](../../docs/le2-scripted-testing.md) and its
linked repository skills before native work. Use the protected LE2 launch and
bound diagnostic request channel described below; the LE1 runners are not LE2
session adapters.
Save preparation and protected session lifecycle have offline tests. The three
public scenarios are **drafts**, pending naturally acquired fixtures and native
observations. Neither a valid manifest nor a green integrity audit proves that
ME2 loaded, that a story edit is coherent, or that a VR interaction works.

Read [save format and recipe details](../../research/le2-save-adapter.md),
[coverage and remaining work](../../docs/le2-validation-coverage.md), and
[current evidence](../../research/le2-validation-evidence.md).
Concrete acquisition leads are in [reload/interrupt fixture research](../../research/le2-fixture-acquisition.md)
and [Zanethu discovery dependencies](../../research/le2-galaxy-fixture-source.md).
Weapon fixtures also need the [native ammo and reload mapping](../../research/le2-ammo-fixture-source.md).

Offline preparation can continue while another session owns the game:
[resource boundary batches](variant-usage.md) create hash-bound zero/insufficient/exact/surplus cases;
[story transition review](story-review-usage.md) compares paired saves against scoped source effects;
[archived diagnostics and observer design](../../research/le2-runtime-observer.md) distinguish reusable capture formats from pending scenario-specific runtime assertions.
These tools do not establish native or VR acceptance. Keep all generated artifacts private.

The [live resource observer](../../research/le2-resource-observation.md) and
[state exporter](export-state.md) provide the next starting-state check: collect
`observe-resources` through the bound channel, then export its archived response
against the frozen scenario. Unavailable fields stay absent with reasons. A
matching report checks observed starting values only; scene review, normal
save/reload and VR interaction remain separate gates.

The normal pause and Save-menu transitions now have protected native proof,
including the closed PowerWheel HUD case and readable menu review. New Save
submission is implemented behind separate diagnostic permission but remains
unproven: the current Save-list validator rejects a displayed row, and its
asynchronous initialization handling needs correction. Do not skip those
refusals or substitute a synthetic successful result. Follow the
[normal save workflow](control.md#submit-new-save-and-preserve-its-output) and
[file evidence collector](save-evidence.md): preserve a reviewed pre-save
inventory, submit once, archive and independently decode the actual new file,
then reload it through a fresh protected session.

The [capture tool](capture.md) preserves failed black PrintWindow samples.
Its explicit `--method desktop-client` fallback has live proof on the normal
Save screen, requires the exact owned game window already in front, and sends
no focus or input commands. Review the resulting image; successful pixel
capture alone does not establish the expected scene or enabled menu action.

The [Steam launch adapter](launch.md) provides read-only preflight and a guarded
launch path where installed launcher support is verified. The [bound control
client](control.md) supplies request correlation and uncertainty recovery. An
opt-in monolithic diagnostic host implements `ping`, structural `observe` and a
separately gated scalar `probe-fov`. Reviewed integrations also expose read-only
`observe-save-menu` and independently permitted splash/load-menu actions. A
separately gated frozen-fixture load path has one bounded native proof, including
post-load world/pawn and reviewed scene evidence; see the workflow below. Each
run still requires verified module mapping, process binding
and a real producer handshake. Diagnostic observations and menu opening do not
load a save or establish scenario/VR acceptance.

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

The optional oracle requires Rust/Cargo and the Windows MSVC build tools. It uses
Trilogy Save Editor's actual pinned models and binary
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

This workstation's verification used an isolated toolchain in ignored storage,
without modifying the system PATH. To reuse it from the repository root:

```powershell
$env:RUSTUP_HOME = (Resolve-Path local-tools/le2-oracle/rustup).Path
$env:CARGO_HOME = (Resolve-Path local-tools/le2-oracle/cargo).Path
& local-tools/le2-oracle/cargo/bin/cargo.exe build --manifest-path <new-private-project>/Cargo.toml --release --locked
```

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
It does not rewrite Resume/profile selection using LE1 offsets. To request the
narrow diagnostic fixture-load path, add the reviewed `fixtureSelection` block
[documented here](control.md#reviewed-frozen-fixture-loading) **before** preparation.
It binds the complete observed descriptor to this manual-save target and hash,
plus a reviewer attestation referencing private observation/capture artifacts.
Do not derive the SaveName or target filename from the descriptor Index.

```powershell
python tools/qa-le2/session.py prepare <plan.json> <new-private-run-directory>
python tools/qa-le2/session.py request-load <run-directory>
```

Preparation freezes the scenario, recipe, receipt and save; backs up the complete
MELE profile, LE2 BioGame/Config and whole LE2 Win64 directory; and verifies copies
before staging. Use a run directory separate from all protected roots with enough
space for backups and post-test archives. Additional modified paths must be in
the plan before preparation.

`request-load` records an operator handoff; it does not launch a game. The
[protected Steam adapter](launch.md) can forward to the normal launcher and bind
the new ME2 process when its reviewed capability requirements pass. Follow its
one-attempt/operator handoff rules. If starting through the normal launcher
manually, bind the actual process explicitly. Select the staged save through
normal Load Game:

```powershell
python tools/qa-le2/session.py bind-process <run-directory> <ME2-PID>
python tools/qa-le2/scenario.py check-start <run-directory>/scenario.json <observed-start.json> --recipe <run-directory>/recipe.json
```

Process binding checks executable path, hash and start identity. The diagnostic
producer provides structural observations and one bounded scripted manual-fixture
load has been proven. General profile selection and authoritative scenario
starting-state assertions remain unvalidated. Use the
[bound control workflow](control.md) for attachment and diagnostic evidence;
starting-state JSON still needs observed game state with capture/provenance.
Do not relabel save inspection, expected values or a title-screen handshake as
proof that the selected fixture loaded. The [save/load source handoff](../../research/le2-save-load-entrypoints.md)
identifies existing game routes and their unresolved prerequisites.

`probe-fov --allow-probe-fov` is a separate, explicitly requested native scalar
probe, limited to one admitted native entry per process. It does not enable
general console commands or save/load automation. Follow the
[probe admission and recovery rules](control.md#explicit-scalar-fov-probe):
`dispatched` is bounded native-return evidence, while an error or timeout leaves
uncertainty and must not trigger another attempt.

For the current panel and bounded save-list evidence, use the empty-input
`observe-save-menu` query. The [normal-menu workflow](control.md#observe-and-open-the-normal-load-menu)
describes `advance-splash --allow-menu-actions` and
`open-load-menu --allow-menu-actions`, each with its own one-native-entry limit
per process. Use them only on a reviewed host advertising that capability, after
observing the exact ready active panel. Re-observe the asynchronous transition
before the next stage; `dispatched` means the call returned, not that the next
panel or save is ready. Never repeat an uncertain action. Selecting the intended
career/fixture and proving its descriptor-to-file mapping remain separate work;
opening the Load panel does not perform a load.

For a new run with a valid reviewed `fixtureSelection` plan binding and a host
implementing that capability, use empty-input `observe-fixture`, inspect its
unique current descriptor match, then explicitly request
`load-fixture --allow-fixture-load` only within the authorized test. Follow the
[frozen-fixture workflow](control.md#reviewed-frozen-fixture-loading): no client
row/pointer/path target, manual Type 0 only, no career-picker navigation, exact
staged/frozen file hashes and one admitted native entry per process. A returned
`dispatched` response is not a loaded-fixture result; require fresh observed state
and capture. The [recorded native proof](../../research/le2-validation-evidence.md#frozen-manual-fixture-loaded-through-the-normal-menu)
covers one manual fixture, not resource values, save/reload persistence or VR.
Missing or invalid bindings reject fixture requests, and uncertain outcomes must
not be retried. Next work reuses this path for normal save/reload and bounded
resource observation.

## 5. Exercise, inspect and reset

Run the chosen case through normal controls and record actual input, scene,
before/after state and any resulting saves. First establish the native baseline;
use real controller input for VR acceptance. Record zero/insufficient/exact/surplus
resources where relevant. Diagnostic grants, direct completion calls and
teleports are setup evidence only.

Capture the exact owned ME2 desktop window without focusing it or sending input:

```powershell
python tools/qa-le2/capture.py <run-directory> --name native-start-001
```

The [capture helper](capture.md) writes a new private PNG and receipt under the
run, with process/build identity, dimensions, timestamps and image hash. Inspect
the image and correlate it with same-run state observations. Black/constant
images are explicitly suspect and retained for review; even `captured` does not
mean the intended scene rendered or a scenario passed. No live image or native
acceptance is implied by the helper's offline tests.

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
