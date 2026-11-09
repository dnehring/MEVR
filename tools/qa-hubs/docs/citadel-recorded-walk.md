<!-- SPDX-License-Identifier: MIT -->
# Replay a recorded Citadel walk

`tools/qa-hubs/citadel-walk.ps1` turns a private `.mevrrec` walkthrough into a bounded, repeatable input
replay candidate. It runs once in an explicitly selected, already-running LE1 session. It does not launch the
game, enable the null headset, switch OpenXR runtimes, change graphics settings, teleport or inject saves.
Use the normal headset or an independently prepared fake-headset session. Keep the VR session awake.

The 2026-11-08 manual walkthrough contains 10,047 frames over 167.507 seconds. Stick movement starts at
41.151 seconds and ends at 161.535 seconds. The original clip is preserved byte-for-byte, including recorded
buttons, triggers, head motion and controller poses. Sleeping controllers contain neutral input; the last
half-second also has neutral input. A local package has been prepared under
`scratch/qa-hubs/citadel-recorded-walk/`; the recording and save must remain private.

## Prepare and bind the original save

PowerShell 7 and Python 3 are required. `Plan` is offline and never sends input. Replace the example private
paths with actual files. The checked-in script contains no personal paths or recordings.

```powershell
$walk = '.\tools\qa-hubs\citadel-walk.ps1'
$package = '.\scratch\qa-hubs\citadel-recorded-walk'
# Once, if this private package has not already been prepared:
$recording = '<private walkthrough.mevrrec>'
& $walk -Action Prepare -PackageDirectory $package -RecordingPath $recording `
    -ExpectedRecordingSha256 (Get-FileHash -LiteralPath $recording).Hash
& $walk -Action Plan -PackageDirectory $package
```

Reload the **same save used at the start of the original walkthrough**, before moving Shepard. Use the same
recentering/facing setup and controller/turn/locomotion settings. Identify that save file and bind its hash plus
the live world position/yaw once:

```powershell
Get-Process MassEffect1 | Select-Object Id, StartTime
& $walk -Action BindStart -PackageDirectory $package -GameProcessId <LE1-PID> `
    -SaveFile '<original .pcsav file>'
```

`BindStart` reads the map, mode and pawn position through `mevr vehicle` / `mevr loc`, checks on-foot Citadel
gameplay, and privately copies the selected save. It does not load or modify that save. Its identity is an
operator attestation: the engine query does not expose which slot was loaded. A new package is needed to
bind a different start; existing bindings cannot be silently overwritten.

## Run, measure, stop

Reload that save before **each** run, including the first validation. Do not touch the controls during replay.
The script refuses a changed recording/save, wrong map/mode, position farther than 60 Unreal units from the
bound start, or yaw farther than 1,024 Unreal units (5.625 degrees). It also refuses stale headset status,
another replay/recording, pending requests and another holder of the shared `qa-cafeteria.lock`.

```powershell
& $walk -Action Run -PackageDirectory $package -GameProcessId <LE1-PID>
# On a separate repeat after reloading the same save:
& $walk -Action Run -PackageDirectory $package -GameProcessId <LE1-PID> -Capture
# From another PowerShell window, to request an early stop:
& $walk -Action Stop -PackageDirectory $package
```

`-Capture` measures 90 seconds beginning around replay second 45, after the recording has loaded and walking
has begun. The current probe supports at most 120 seconds; this is a window of the walk, not the full clip.
`-CaptureStartSeconds` and `-CaptureSeconds` select another bounded window. No snapshots, location queries or
replacement replays are issued inside it. Probe clock synchronization still affects boundary frames as usual.
Current runtime FOVs are retained; recorded FOVs are not forced. One Draw and all graphics settings are retained.

Each attempt stores a transcript, frozen source, invocation parameters, start/end observations, runtime and
on-disk build hashes, mod settings and an explicit `result.json`. Timing runs copy frames, ticks, queued calls
(when supported), and probe metadata. Analyze that metadata with `tools/perf-probe.py`.
For frozen-source reruns, invoke `runs/<timestamp>/source/tools/qa-hubs/citadel-walk.ps1` with the same
`-PackageDirectory` and the **current** game PID. Inspect `invocation.json` for the other original arguments.

On normal completion or a caught error the runner requests replay-stop and checks acknowledgment. It never
kills the game. If release cannot be verified, it retains the lease and records `releaseError`; inspect that
receipt and end the owned replay before another test. An abrupt termination of PowerShell cannot guarantee
`finally` execution: use `tools/replay.ps1 -Stop` for the identified session, verify `testloop.status` is current
and `replaying=false`, and only then remove the abandoned lease. Do not clear another test's live lease.

## Validation limits

The user confirmed the original recording began at a freshly loaded save. Its first world-position query was
33.265 seconds into the clip; that is **not** an exact frame-zero pose capture. Binding the reloaded save now
supplies a checked start without inventing coordinates. Recorded headset poses are local tracking coordinates,
not world-space waypoints. Timed movement can drift due to recentering, collisions, NPCs, gestures, turning
settings or performance. Watch the first replay and compare its actual path and endpoint before accepting it
as a regression route. Successful replay completion alone is not route validation or a performance pass.

This candidate is separate from the seven declarative `scenarios/` drafts and their stationary-capture reporter.
Those drafts stay unvalidated; the recorded walk's results must not be pooled with them. Other-map development
remains paused. For CPU comparisons, repeat only after the starting save, actual path, settings and capture
window match; compare builds separately and retain raw artifacts.

Offline checks (no game access):

```powershell
& .\tools\qa-hubs\test-offline.ps1
& .\tools\qa-hubs\test-citadel-replay.ps1
```
