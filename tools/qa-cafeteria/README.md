<!-- SPDX-License-Identifier: MIT -->
# Repeatable Normandy cafeteria QA

Walk Shepard from the original CIC starting position down the stairs and around the elevator to the mess table,
then record a 90-second head-turn sample. This is a Windows/Steam/LE1 native test using SteamVR's null headset.
Controller input performs the walk; pawn position is only read. One Draw is retained. No game saves, recordings,
images, logs or machine-specific configuration are shipped with this tool.

## Before running

Coordinate exclusive ownership of the game and SteamVR with other agents. A closed process list cannot tell you
whether another agent is about to launch. Close the game, Legendary Edition launcher, VR launcher and SteamVR;
leave Steam signed in. Use PowerShell 7 and Python 3. The shell needs permission to write the installed game and
SteamVR settings. The runner backs up the complete player profile/save tree, game configuration, ASI INIs, binary
and affected SteamVR files before starting, and verifies restoration in `finally`.

Resume must load **BIOA_NOR00**, approximately `(-5170.5,14705.2,-36047.9)`, pawn yaw `-16164` Unreal units
(-88.79 degrees), at the CIC. Route checks reject a different map, position, facing or deck. Prepare that save
through ordinary gameplay if it is not already available. The tool does not install a save, teleport the pawn,
or bypass a blocked waypoint. The fixed route can still be affected by different movement settings or NPCs.

Inspect paths and the installed ASI hash without launching or changing settings:

```powershell
& .\tools\qa-cafeteria\run.ps1 -Plan
```

Review that build's support for `mevr loc`, `mevr vehicle`, replayed controller input/FOVs, snapshots and the perf
probe, then supply its hash:

```powershell
& .\tools\qa-cafeteria\run.ps1 -Preset High -ExpectedAsiSha256 '<reviewed 64-character hash>'
```

`-Preset Current` retains graphics choices. `-SkipCapture` checks only the route and restoration. `-PythonExe`,
`-SteamRoot`, `-SteamVRRoot`, `-GameRoot` (Legendary Edition root), `-ProfileRoot`, `-LocalAppData` and `-OutputRoot`
override discovery. Defaults use Steam's library manifests and Windows' user folders, with output under ignored
`scratch/qa-cafeteria/`. `-Plan` is read-only; a sandbox that cannot read Steam's registry still needs native read access.

Normally SteamVR must already be the active OpenXR runtime; the tool never changes the global runtime.
`-RuntimeMode LegacyQaMarker` is a compatibility path **only** for the original private ASI with hash
`CE247578A6672A7786B024010EDEC1B11725791EEB8FF65F7A96D939EC89F216`. That binary honors an ASI-side QA marker;
ordinary repo builds do not. The marker is backed up/restored and the option rejects any other hash.

## Workload and evidence

High sets source rendering to 3840 x 3840, dynamic shadows/AO/AA on, foliage distance 1.0 and motion blur off.
The null driver's recommended eye size is 2688 x 2880, nominal 90 Hz; app resolution scale is 100%. It leaves the
installed binary and One Draw selection intact. Confirm the resulting probe's resolution and renderer flags before
comparing samples. Null-headset throughput does not validate physical headset pacing, Virtual Desktop's streaming,
Wi-Fi, encoding or decoding. In the original session it ran about 139 application FPS despite advertising 90 Hz.

Every run keeps a new backup, invocation context, source copies/hashes, transcript, checked waypoints, arrival PNGs,
raw probe CSV/JSON, `analysis.json`, and `restored.json`/`final-state.json`. These stay private under ignored scratch.
Synthetic inputs are generated from source, with explicit Quest-like asymmetric FOVs and 64 mm IPD; no private
recording is needed. They are a repeatable workload, not a physical-headset calibration.

The 30-second head turn loads **before** the 90-second capture and settles for five seconds. The clean sample has
no replay replacement or screenshots; analysis trims one second at each boundary. Route-time replay loads can
cause approximately 100 ms pauses, so traversal is functional QA and must not be treated as a clean performance
sample. Compare the stationary turn samples separately, keeping build, hardware, power state, settings and background
workload consistent. Report p99, maximum and counts above 11.111/22.222 ms alongside GPU, render-thread and tick data.
One lower maximum alone does not prove an improvement or establish a cause.

Run several independent launch/walk/capture/restore cycles and summarize them:

```powershell
& .\tools\qa-cafeteria\repeat.ps1 -Count 3 -Preset High -ExpectedAsiSha256 '<reviewed hash>'
python tools/qa-cafeteria/compare.py '<batch folder>'
python tools/qa-cafeteria/investigate.py '<one run folder>'
```

`report.ps1 -BatchDirectory '<batch folder>'` reruns both analyzers for every completed clean capture and then
compares the batch, preserving console summaries alongside the JSON reports.

An initial severe-hitch gate for this nominal 90 Hz scenario (not a physical-headset acceptance test) is:

```powershell
python tools/qa-cafeteria/compare.py '<batch folder>' --max-p99-ms 11.111 --max-interval-ms 22.222 --max-tick-ms 22.222 --output '<batch folder>/severe-hitch-gate.json'
```

The 2026-11-08 three-run baseline correctly **fails** this gate on a 92.7 ms interval; normal p99 is 7.7-7.8 ms.
This gate catches severe pauses without claiming that smaller misses or physical headset pacing are acceptable.

`compare.py` rejects missing captures, missing restoration/route receipts, recorded faults, GPU timing drops,
draw-replay failures and differing build/workload fingerprints. Without explicit performance limits, success means
the measurements passed these checks, **not** that the hitch problem is solved. Optional `--max-p99-ms` and
`--max-interval-ms` and `--max-tick-ms` enforce chosen regression limits. Engine/host tick maxima are reported
separately: buffered render frames can mask a simulation stall. `investigate.py` aligns frame/tick outliers with the host log
and observed settings-file write times; a time match is evidence to investigate, not proof of causation.
`machine.ps1 -OutputDirectory '<batch folder>'` records hardware/driver/power information before capture.

For a separate intrusive diagnostic run, build `mevr-stack-sampler` in `build/Release`, then add `-Diagnostic`
to `run.ps1`. It samples the game thread every 10 ms, recording stacks, timestamps and the time spent suspending
the thread. It warms up before each of two separate 90-second probes in `diagnostic-1/` and `diagnostic-2/`.
Run `investigate.py` on each subfolder. `compare.py` excludes these runs. Sampling can introduce
hitches, so inspect the observer cost before interpreting a spike. This is optional and does not redeploy the game.

```powershell
cmake --build build --config Release --target mevr-stack-sampler -- /m:1 /nodeReuse:false /p:TrackFileAccess=false
python tools/qa-cafeteria/summarize-diagnostics.py '<diagnostic run folder>'
```

The summary reruns the investigation and maps sampled game addresses through the installed executable's unwind
table and existing symbol list. Use `--exe` for another copy of the matching executable. It records the mapping
binary's hash; do not use a different game version. Export-only names such as `SpiSupportDecl` can be misleading
nearest-symbol fallbacks. The two initial diagnostic captures sampled the repeated host hitch inside
`WritePrivateProfileStringW`, while the separate engine spike repeated in an unnamed chain beneath `UWorld::Tick`.
They did not reproduce the clean baseline's 93 ms pause. Median sampler suspension was about 0.55 ms per sample;
these are intrusive causal clues, not performance acceptance measurements.

`inspect-engine-spike.ps1` records the optional Ghidra lookup for that engine chain. It requires the existing private
LE1 project and refuses a locked project. All decompiled output stays in the private Ghidra cache outside the repo.
The original attempt encountered a project lock, so the unnamed chain's role remains unconfirmed.

The private predecessor completed the whole route and restored 42 protected files on 2026-11-08; its clean sample
caught an approximately 80 ms interval largely between instrumented game ticks. The portable version's current
validation is recorded in [research/63](../../research/63-head-turn-performance.md).

## Camera diagnostic candidate

For the camera-diagnostic fix, [the priority list](../../docs/performance-priorities.md) records acceptance criteria.
The following makes a frozen private build, temporarily installs it for three complete High runs, and restores the
original ASI afterward. Coordinate native-session ownership first. Use absolute paths and the reviewed installed hash:

```powershell
& .\tools\qa-cafeteria\validate-camera-source.ps1 -BuildDirectory build -TrilogyBuildDirectory build-trilogy
& .\tools\qa-cafeteria\build-camera-candidate.ps1 -StageDirectory '<checkout>/scratch/<new candidate folder>' -SteamVRManifest '<SteamVR>/steamxr_win64.json'
& .\tools\qa-cafeteria\run-camera-candidate.ps1 -CandidateReceipt '<candidate folder>/candidate.json' -ExpectedInstalledSha256 '<reviewed installed hash>' -GameRoot '<Legendary Edition root>'
python tools/qa-cafeteria/check-camera-diagnostic.py '<validation folder>/batch'
& .\tools\qa-cafeteria\verify-camera-restoration.ps1 -ValidationDirectory '<validation folder>'
```

The source checker uses already configured development build trees, builds LE1/LE2/LE3 and the test/setup
programs, and records CTest plus offline checks in a transcript. It refuses active game/SteamVR sessions.
Use `-TestRegex '<ctest regex>'` only for an explicitly narrowed rerun; omit it for the complete test suite.

The build script copies current source and shares dependency directories through junctions. It adds a private
process-only runtime shim and pins the private runner to the resulting binary hash; the public runner's allowlist
is unchanged. This QA binary is not a release artifact. Source hashes, build/test output, binary backup/restoration
and per-run receipts are retained. The camera check requires a fresh published sample and no settings-file writes
during capture. It does not certify manual alignment or resolve the other hitch categories. If interrupted, recover
the inner run first, then use `installation.json` and its hash-checked `before.asi` to restore the original binary.

## Recovery and offline checks

The runner refuses existing native sessions and pending test-loop requests. It creates `%LOCALAPPDATA%/MEVR/qa-cafeteria.lock`
using exclusive creation. If interrupted or restoration fails, that lock remains and names the run folder. **Do not
delete it to take over an active session.** Confirm the owning task has ended, close its game/launcher, then run:

```powershell
& .\tools\qa-cafeteria\session.ps1 -Action Restore -RunDirectory '<run folder>'
& .\tools\qa-cafeteria\session.ps1 -Action Verify -RunDirectory '<run folder>'
```

Alternatively, `recover.ps1 -RunDirectory '<run folder>'` closes only that run's recorded PID/start time,
restores and verifies its files, then removes the matching lock. It refuses a different lock owner.

Only after successful recovery, remove that run's lock. Restore refuses a changed ASI or global runtime and checks
every backup hash before replacing live files. Changed files and new autosaves are archived before restoration.
No script can guarantee cleanup if its process is killed; retain the backup until recovery is verified.

```powershell
& .\tools\qa-cafeteria\test-offline.ps1
python tools/check-spdx.py
```

Offline checks cover PowerShell syntax, neutral controller inputs, pose/FOV geometry, backup replacement refusal,
damaged-backup refusal, changed/deleted/new-file restoration and archival. They do not claim runtime acceptance.
