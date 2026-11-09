<!-- SPDX-License-Identifier: MIT -->
# Automatic Citadel save loading

`tools/qa-hubs/citadel-session.ps1` owns the full launch/load/replay/exit cycle. Its private fixture is independent
of the player's original slot: that slot can later change or disappear. The immutable fixture and its checksum,
not the source pathname, supply subsequent runs. No save data belongs in Git.

## One-time fixture setup

Start with a prepared [recorded-walk package](citadel-recorded-walk.md). Select the save that belongs at the
beginning of that walk. If the original is uncertain, keep it as a loader-test candidate and validate/re-record
the route from a known starting save before accepting performance comparisons.

```powershell
$session = '.\tools\qa-hubs\citadel-session.ps1'
$package = '.\scratch\qa-hubs\citadel-recorded-walk'
& $session -Action Freeze -PackageDirectory $package -SaveFile '<career>_<slot>.pcsav'
& $session -Action Plan -PackageDirectory $package
```

Freeze validates the compressed save and checksums, confirms a unique Citadel location record, copies the
save and a profile fallback into `fixture/`, and pins their hashes. It refuses to replace an existing fixture
or substitute a different save for an already-bound route. The original filename must retain the career/slot;
the archived filename itself is fixed. `-ProfileTemplate` can supply an already-archived v50 gamer profile.
All source paths in the private manifest are provenance only; later runs do not read them.

## Automatic sessions

Close the game and launcher. Steam must be signed in and, for walking replay, the chosen OpenXR runtime/headset ready. The runner
uses that runtime as configured. It only enables the null driver with the explicit `-FakeHeadset` option and
never changes the global OpenXR runtime. Bind once, then Run:

```powershell
& $session -Action Bind -PackageDirectory $package
& $session -Action Run -PackageDirectory $package -Capture
```

Bind performs an automatic launch/load, checks the loaded map and position against the archived save, records
the exact live starting pose, then exits and restores current state. Binding needs a responsive game console,
but not headset tracking. It sends no walking input. Run repeats
the same load, checks the bound pose and replays once. `-CaptureStartSeconds` / `-CaptureSeconds` select the
timing window. Stop an active replay using `citadel-walk.ps1 -Action Stop -PackageDirectory ...`.

## Stationary fake-headset metrics

The fixed save can also supply a stationary head-turn workload without asserting a match to the unvalidated
walkthrough. `Measure` loads the archived save, checks its world position, starts a neutral-input 30-second
sinusoidal head-turn loop, takes a scene snapshot, settles, then captures 90 seconds. It verifies normal gameplay
and that the pawn stayed within 60 UU of its start afterward. No replay reload or snapshot occurs inside the
measurement window. The analyzer excludes one second at both clock-synchronization boundaries.

```powershell
& $session -Action Measure -PackageDirectory $package -FakeHeadset `
    -EyeWidth 2284 -EyeHeight 2448 -SweepDegrees 120 -CaptureSeconds 90
```

`-FakeHeadset` requires SteamVR already selected as the global runtime and no existing game/SteamVR session.
It backs up SteamVR settings, chaperone and null-driver settings alongside the player data, starts the null
driver at 90 Hz, and restores the exact pre-test files afterward. This uses fresh per-run backups, never an
older global fake-headset backup. Current game graphics settings and One Draw are retained. Actual resolution,
graphics settings, build hashes, hardware, raw frames/ticks/queued calls and an analysis JSON are recorded.
The scene snapshot and logs remain private. A null-driver throughput measurement does not establish physical
SteamVR pacing, Virtual Desktop transport latency, reprojection quality or walking-route performance.

The coordinator shares the QA lock with the existing harness. It refuses a running game/launcher, pending
requests, another owner, corrupted fixture or damaged backup. Before staging anything it backs up the complete
current player profile/save tree, ME1 configuration and both VR INIs. It temporarily installs the archived save
at its proper career/slot and prepares a copy of the **current** gamer profile with only the Resume selection
changed. This retains current preferences during the run. The immutable profile fallback is used only when
the current profile is absent. Steam/EA launch and title-screen Resume use the existing launch adapter.

After normal completion or a caught failure, it exits only its owned game, waits for closure, archives generated
or changed files, restores the exact pre-test file contents, and verifies hashes. It does not replace the
user's progress with the frozen checkpoint permanently. Empty directories created by the test may remain.
Steam/EA authentication, cloud synchronization prompts and headset availability remain external prerequisites.

## Evidence and recovery

Each attempt writes `sessions/<timestamp>/` with frozen scripts, invocation parameters, backup/staging manifests,
loaded-world observation, child replay artifacts and `session-result.json`. `nativeLoadValidated` means the
requested file was staged and the loaded map/position matched; it does not prove that this was the original
recording's slot. `nativeRouteValidated` stays false until an actual path review.

The private 2026-11-08 archive preserves all 14 current LE1 save/profile files. Two manual Citadel saves were
found, but neither stored location matched the world position queried 33 seconds into the user's recording.
The original slot remains uncertain. The separate `citadel-autoload-save96-check` package validates loading
and stationary head-turn measurements; it must not be relabeled as the original walkthrough fixture.

The first native stationary capture and repeatable analysis command are documented in the
[C-Sec performance report](../../../research/65-citadel-stationary-performance.md).

Native loader validation on 2026-11-08 succeeded with that separate Save 96 package: Steam/EA launch, two title
Enter presses, on-foot `BIOA_STA00`, and a loaded position within 0.047 UU of the stored save location. The
second attempt bound the live start without requiring headset frames, exited normally, and restored/hash-verified
all 37 protected files. Two changes (the temporary Resume profile and the game's rewritten settings) were archived.
Receipt: `scratch/qa-hubs/citadel-autoload-save96-check/sessions/20261108-225255-179/session-result.json`.
The preceding attempt also loaded the correct save and restored all files, but binding rejected stale headset
status; that check now applies only to walking replay. No walking input was sent during either native attempt.

If the game will not close or restoration fails, the lease and backups remain. After closing the owned game
and launcher, recover using the recorded session directory:

```powershell
& $session -Action Recover -RunDirectory '<package>\sessions\<timestamp>'
```

Recovery refuses another lease or a still-live controller. Inspect any residual test-loop request before the
next test; never clear a different owner's request. The recorded `source/tools/qa-hubs/citadel-session.ps1` can
also perform recovery. Do not delete the backup or lease before restoration is verified.

## Format boundary and checks

`save_fixture.py` is a bounded format adapter, not a general save editor. Its independent implementation validates
v50 compressed-save chunk lengths and CRC-32/BZIP2, and recognizes a unique profile footer containing the last
character and save strings. It alters those selection strings only in a temporary profile copy and recalculates
the checksum. Unknown versions, bad checksums, ambiguous footers and unsafe career/slot names are rejected.
The SDK's `FGamerProfileSaveRecord` identifies the selection fields; the compressed save layout and checksum
are also documented by the primary [Trilogy Save Editor implementation](https://github.com/KarlitosVII/trilogy-save-editor/blob/main/src/save_data/mass_effect_1_le/mod.rs).
Stored-position scanning is diagnostic and is checked against the live game before binding/replay.

```powershell
& .\tools\qa-hubs\test-offline.ps1
& .\tools\qa-hubs\test-citadel-replay.ps1
& .\tools\qa-hubs\test-citadel-session.ps1
```

These cover checksum/layout refusal, source-save deletion/overwrite, private fixture corruption, normal replay,
early stop, nested lease ownership, and restoration after simulated launch/replay failures. They do not by
themselves prove native automatic loading or route repeatability.
