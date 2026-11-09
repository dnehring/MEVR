<!-- SPDX-License-Identifier: MIT -->
# Hub walking regression tests

The user resumed Citadel script generation from their manual walkthrough. The new
[recorded-walk candidate](docs/citadel-recorded-walk.md) supports save/start binding, one-shot input replay
and an optional traversal timing capture. It still needs native route validation. Other map development is
[paused at the user's request](PAUSED.md); all declarative scenarios below remain drafts.

The [automatic session wrapper](docs/citadel-auto-load.md) adds an immutable private save fixture, automatic
Resume loading, and backup/restoration of the current profile/settings. Later edits to the original save slot
do not change the archived test input. Original-slot identification and actual route validation remain separate.

Declarative LE1 routes extend the Normandy cafeteria workload to the Citadel, Noveria and other major centers.
Each run uses controller walking, checks the starting map/position/facing, checks every waypoint, captures an
arrival image, and measures a stationary head-turn loop after settling. One Draw stays enabled. The shared
cafeteria kit owns save/settings backups, fake-headset configuration, process identity, capture and restoration.

## Inspect before running

```powershell
python tools/qa-hubs/scenario.py tools/qa-hubs/scenarios/citadel-csec.json
& .\tools\qa-hubs\run.ps1 -ScenarioFile '<scenario.json>' -Plan
& .\tools\qa-cafeteria\run.ps1 -Plan
```

Use the actual scenario filenames in `scenarios/`. The first two commands are offline/read-only and also
describe drafts. The last command discovers native paths and the installed ASI hash without launching.
`draft` means missing route evidence and **cannot execute**. `ready` means start/steps are specified and can
be attempted; it is not a completed walk. `validated` requires a recorded route receipt, still not a performance
pass. The reporter evaluates actual completed capture/restoration evidence and optional timing limits separately.

Prepare the stated existing save through the game's normal menus before running. The harness uses Resume;
it does not choose a career, inject a save, open maps, teleport, force streaming or bypass a blocked waypoint.
A separate fixture-acquisition experiment must retain its own evidence and state-restoration receipts. A map
opened from a different career is not equivalent to a played save. If no suitable fixture exists, leave the
scenario draft. Current acquisition blockers and sources belong in `docs/` and the scenario's prerequisites.

An observed acquisition hazard on 2026-11-08: a direct Noveria map-open attempt reported `BIOA_ICE00`,
mode 0 and an on-foot pawn while the eye image still showed the Normandy CIC at its original position.
That attempt was correctly refused as a Noveria fixture. A map-name/mode string alone is insufficient:
verify the rendered scene, actual start pose, selected save identity and normal load behavior before promotion.
Archived acquisition saves are not automatically replayable. A future staging operation must occur inside
the full-profile backup and prove selection through the ordinary load flow; never fabricate profile metadata.

## One protected run

Coordinate exclusive game/SteamVR ownership first; a clear process list does not grant a lease. Close game,
launchers and SteamVR, leave Steam signed in, and use PowerShell 7/Python 3 with native filesystem access:

```powershell
& .\tools\qa-hubs\run.ps1 -ScenarioFile '<scenario.json>' -Preset High -ExpectedAsiSha256 '<reviewed installed SHA-256>' -FixtureSaveFile '<selected private save.pcsav>' -FixtureSaveSha256 '<reviewed save SHA-256>'
```

`-SkipCapture` validates only traversal/restoration. `-Preset Current` keeps graphics settings.
The optional fixture arguments pin the intended Resume save's bytes and require them to be present in the
protected profile. Every run also records hashes of the complete starting save set, privately. They do not
automatically select the save; the operator must prepare that exact fixture through normal game flow. Without
selected-save identity the run may collect discovery evidence, but the regression reporter refuses acceptance.
Matching map/position/facing alone cannot establish the same story, NPC or mission state.

Path overrides
are the same as the cafeteria runner. Active SteamVR is required unless the explicitly hash-gated
`-RuntimeMode LegacyQaMarker` private test build is installed. No global runtime switch occurs.

High retains the cafeteria workload: 3840 x 3840 source, 2688 x 2880 eye, AA/shadows/AO on, blur off,
One Draw/sharePass=5. The hub capture is a neutral-input 30-second sinusoidal yaw loop around the scenario's
specified direction, measured for 90 seconds in the initial hub contract. It loads before capture and settles for five seconds.
Per-waypoint replay replacements can stall; traversal is functional evidence, not a clean performance sample.
Do not run builds, extra probes or other native tasks during clean captures.

The runner shares `%LOCALAPPDATA%/MEVR/qa-cafeteria.lock` with the cafeteria suite. It freezes scripts and
analyzers under each private run before launch, backs up the full player profile and affected configuration,
and restores in `finally`. If interrupted, retain the lock and recover through the cafeteria recovery script
using that run directory. Its `context.json` points at the frozen source. Do not delete a live owner's lock.
No game saves, recordings, images, binaries or personal paths belong in the public scenario catalog.

## Compare repeated runs

Repeat the same scenario from the same prepared save, build and settings. Different maps/routes are grouped
separately; no pooled average can conceal a regression in one hub.
The runner records hardware/driver/OS/power-plan identity before launch; this and both fixture hashes are part
of the comparison fingerprint. Background workload and real runtime pacing still require operator control.

```powershell
python tools/qa-hubs/report.py '<output root>' --min-runs 3 --max-p99-ms 11.111 --max-interval-ms 22.222 --max-tick-ms 22.222
& .\tools\qa-hubs\test-offline.ps1
```

Without limits, report success means comparable, complete measurements only. With limits it is a scenario
regression gate, not physical-headset pacing acceptance. Missing captures, failed/draft routes, wrong One Draw
state, faults, differing fingerprints and incomplete restoration fail. Tick instrumentation versions/scopes
are part of the fingerprint, and dropped tick/call samples fail admission. Null-headset results do not validate
Virtual Desktop transport, encoding, decoding or headset comfort. Main timing interpretation remains in
[performance priorities](../../docs/performance-priorities.md).

## Culling and startup comparisons

`citadel-culling.ps1 -PackageDirectory <private-fixture> -OutputDirectory <new-private-output>` runs
8/4/0/0/4/8-pixel small-object thresholds at fixed Quality settings, source/eye size 2284 x 2448,
AA and One Draw retained. Zero removes only the additional small-object filter. Normal visibility
culling remains active. Both repeats must complete with matching build/settings/fixture fingerprints;
live setting interventions invalidate the comparison. Read CPU time and draw counts alongside frame
intervals: a runtime wait limit can hide a change in available rendering throughput.

`citadel-startup.ps1 -PackageDirectory <private-fixture> -Phase Transition -Repeats 2` captures 120 seconds
across initial launch, title, Resume and gameplay. This requires render instrumentation version 3.
Thread handoff rows retain wall time but leave CPU busy columns blank because different threads have
unrelated cycle counters. Overlapping Presents or a changed render context invalidate the capture.
The `Splash` and `Flight` modes retain bounded 30-second captures. Launch events include QPC timestamps.

Build and test a candidate with `build-startup-candidate.ps1 -OutputDirectory <new-private-build>`.
Do not edit its compiled sources while it runs. The script records source hashes and preserves the
host/module pair. `test-startup-build.ps1 -CandidateDirectory <private-build> -OutputDirectory
<new-private-run> -PackageDirectory <private-fixture> -Mode Startup -Phase Transition` temporarily
deploys that exact pair, runs the protected captures, and verifies restoration of the original pair.
Use `-Mode Culling` for the six-run culling comparison. Coordinate exclusive native-session ownership
first; neither script grants permission to interrupt another user's or agent's session.

`resume-startup-comparison.ps1 -BaselineDirectory <private-build> -PackageDirectory <private-fixture>
-OutputDirectory <new-private-output>` preserves the full sequence: two baseline transitions, a
SystemSettings-only paired build, two candidate transitions and six culling runs. It is manually
started after the shared session is released, never at the expiry of an estimated coordination window.

## Manifest v1

Required top-level fields: `spdxLicenseIdentifier: MIT`, `schemaVersion: 1`, `id`, `label`, `game: LE1`,
`status`, `expectedMap`, `start`, `steps`, `capture`, `prerequisites`, `evidence`. IDs use lowercase slugs.
Drafts may have `start: null` and empty steps. Executable starts contain `positionUU`, `yawUnreal`, optional
`positionToleranceUU` (60) and `yawToleranceUnreal` (512). Capture contains `seconds`, `yawDegrees`,
`pitchDegrees`, `sweepDegrees`. Coordinates must come from observed native state, not guessed map locations.

Steps are `walk`/`checkpoint` (`name`, `positionUU`, optional `toleranceUU` 50), `look`
(`yawDegrees`, optional `pitchDegrees` 0), `pulse` (`yawDegrees`, `seconds`, optional `stick` .55 and pitch 0),
`use` (the selected object's normal gameplay Used action), or `wait` (`seconds`). Routes must contain a walk
and end on a checked position. Map/mode checks follow every step. These constraints keep a draft script,
arrival cinematic or unrelated resumed save from being mistaken for a successful regression run.
