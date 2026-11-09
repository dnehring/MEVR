<!-- SPDX-License-Identifier: MIT -->
# Initial launch and title-screen performance

2026-11-09. Local investigation after the [six-run Citadel comparison](66-citadel-performance-matrix.md).
No game/rendering behavior or installed binary was changed for this investigation. One Draw, AA, the menu
changes and the native motion-blur guard remain enabled.

## Findings

The large startup hitches and the slow steady cadence are different problems:

1. **The remaining settings settling passes produce large host-thread stalls.** Two independent title
   captures place the 15-second settings reapply inside 153.00 and 153.23 ms host ticks, with 93.09 and
   93.76 ms of CPU busy time. Engine work in those ticks is only 1.15 and 1.13 ms. A captured 3-second
   settling pass reaches 157.09 ms host wall time and 94.91 ms busy. That first event is within the
   first second of the probe, so it is recorded separately from the normal trimmed performance window.
   The 15-second events are well inside both windows; neither overlaps test console calls.
2. **The approximately 32 ms steady title interval is mainly an OpenXR wait.** Median `xrWaitFrame`
   wall time is about 30.1 ms, with only 0.032 ms of CPU busy time in that call. Median render-thread
   CPU work is 1.29-1.37 ms, engine tick 1.40-1.43 ms and host tick 0.13-0.14 ms. The mod's explicit
   pacing hold is approximately 0.001 ms. This is not evidence of 32 ms of CPU computation per frame.
3. **Windows foregrounding alone does not remove that wait.** SteamVR brought its own window forward
   after the game window appeared. Reasserting the game foreground was recorded and confirmed; the
   subsequent title segment still has about 32 ms frame intervals and 30 ms in `xrWaitFrame`. OpenXR
   remains VISIBLE (state 4), not FOCUSED (5), in these null-driver tests. Physical-headset startup
   pacing is therefore still a separate acceptance check.

The two captured 15-second resets coincide with 92.99 and 92.84 ms maximum render-frame intervals.
Do not equate a game-thread tick's duration with one render-frame interval: those threads overlap.

| Title capture | Measured frames | Frame p50 / p99 / max (ms) | Render CPU p50 (ms) | xrWaitFrame p50 (ms) | Host maximum (ms) |
|---|---:|---:|---:|---:|---:|
| Splash 1 | 875 | 31.97 / 33.19 / 92.99 | 1.37 | 30.10 | 153.00 |
| Splash 2 | 873 | 32.02 / 33.28 / 92.84 | 1.29 | 30.16 | 153.23 |

Both use 30-second probes, with one second trimmed from each boundary. Foreground changes are retained
in `launch-events.jsonl`; `foreground_title_segment` excludes the first two seconds after that operation.
Splash 1 has only 4.74 seconds in that subset, while Splash 2 has 21.52 seconds. These are not two
identically focused full-window runs. Both reproduce the same settings-reset signature; the second
confirms it after the foreground operation has settled.

The GPU span estimate is approximately 8.6 ms at the median, not the renderer log's roughly 32 ms
`[GPU]` summary. The detailed probe separates submission/wait phases; the summary must not be used
alone to decide whether the GPU is saturated. GPU spans are still upper-bound estimates, not a
complete hardware utilization analysis.

## Why the guard has not removed these resets

`src/game/SystemSettings.cpp::Poll` already skips a menu/conversation-return reapply while native
motion-blur protection is healthy. It still applies on initial startup, a new world, actual settings
edits and the existing 3/15-second settling timers. Each boolean is sent through its opposite before
the desired value, including shadows, blur, AO and AA. Both successful title captures report the
guard installed, protecting gameplay, with no unknown proxies. The repeated blanket reset still runs.

The next targeted change is to make these settling passes guard-aware and apply only settings that
need correction. Preserve real player edits, required profile/load synchronization and the fallback
when native blur protection is unavailable. Measure that change with the same title capture and a
return-to-gameplay test. Do not solve this by disabling One Draw, AA, garbage collection or every
settings application. This investigation identifies the reset path and its timing; a behavior-changing
candidate and single-change A/B remain open.

## Scope and evidence

The machine and installed host/mod match the Citadel preset matrix: i9-12900K, RTX 4090, High Performance
power plan, nominal 90 Hz SteamVR null driver, source and eyes 2284 x 2448, Quality menu preset. These
are local startup diagnostics, not Virtual Desktop or physical-headset comfort acceptance.

Private artifacts are under `scratch/qa-hubs/citadel-autoload-save96-check/sessions/`:

- `20261109-002515-037`: first successful splash capture, manual foreground intervention recorded.
- `20261109-002720-327`: repeat with the late foreground step automated.

Each includes a frozen command source tree, installed binary hashes, workload/configuration, raw
frame/tick CSVs, logs, launch events, analysis and restoration receipts. Both have zero recorded access
violations, GPU timestamp drops, dropped ticks/calls or draw-replay failures. The final state verification
checks 40 protected files, unchanged host/mod binaries, the global runtime and a cleared QA lease.

Two earlier long probes (`20261109-001545-948`, `20261109-002026-430`) did not produce completed data
across startup/Resume and are excluded. An isolated animated-title attempt (`20261109-002935-212`)
also failed to finish its probe and restored automatically after its timeout. The probe binds its first
render thread and ignores other threads; these failures are consistent with render-thread handoffs.
Bounded splash captures complete. The revised harness refuses the next transition until its title
probe has completed. All three failed attempts were restored and independently verified. No timing
claim uses them. Animated-title capture remains experimental and is not admitted to the regression set.

Initial process loading, the temporary movie render thread and the Resume loading interval are outside
the bounded probes. They need segmented render-thread instrumentation before a complete launch-to-play
CPU timeline can be claimed. Retained startup logs do not substitute for that missing trace.

## Reproduce

Start Steam and sign in; close the game, launcher and SteamVR. Pause competing rendering/build work.
Use the private archived fixture from the Citadel tests; the harness never commits or distributes saves.

```powershell
& .\tools\qa-hubs\citadel-startup.ps1 -PackageDirectory '<private Citadel fixture>' -Phase Splash -Repeats 2
```

`-Phase Flight -Repeats 1` retains the rerunnable animated-transition experiment; it currently fails
to produce a completed probe on this installation. Do not count that attempt as a performance run.

The runner backs up settings and saves, enables the fake headset only for an already-selected SteamVR
runtime, records the bounded title segment, resumes the archived save for a load check, exits and restores
the normal headset and exact file contents. It changes neither the global runtime nor installed binaries.
For replayable diagnosis, re-run `analyze-session.ps1` then `analyze-startup.py` on a completed session.
The startup analyzer separates measurement validity from the 22.222 ms whole-hook regression gate:
both splash runs have valid data and fail that hitch gate. Null-driver frame cadence does not establish
a physical-headset FPS acceptance result.
`stop-owned-session.ps1` offers a checked graceful abort; `citadel-session.ps1 -Action Recover` handles
an interrupted controller, and `verify-session-state.ps1` verifies restoration independently.

Validation: 13 offline Python fixture checks, PowerShell parser/navigation checks and four synthetic
session lifecycle cases passed. Both native splash captures and all five native attempts' state
restorations were checked; game binaries remain unchanged. No performance fix is claimed by these tests.
