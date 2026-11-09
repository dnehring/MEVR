<!-- SPDX-License-Identifier: MIT -->
# Citadel C-Sec stationary performance capture

2026-11-08: one protected 90-second SteamVR null-headset run reproduced frame-time spikes while retaining
One Draw. Median interval was **9.36 ms**, p99 **16.10 ms**, maximum **36.00 ms**. This establishes a repeatable
stationary benchmark at an archived save; the original user walkthrough's starting save remains unconfirmed.

## Workload and provenance

- Archived John00 Save 96, on foot in `BIOA_STA00`, stored subarea `MapSTA30_02` (C-Sec).
- Position `(-2010.6, 10009.2, -22187.9)`, pawn yaw `-19748`; load within 0.047 UU of the archived position.
  The pawn moved **0 UU** during measurement. A pre-capture image confirmed the C-Sec scene.
- Repeating 30-second sinusoidal head sweep, 120 degrees, pitch -10 degrees, neutral sticks/buttons and lowered hands.
- Source render resolution **3840 x 3840**; eye/recommended **2284 x 2448**, null driver 90 Hz, app scale 100%.
- Current settings retained: AA, dynamic shadows and AO on; motion blur off; One Draw/sharePass=5.
- i9-12900K, RTX 4090, driver 591.86, High Performance power plan.
- Host SHA-256 `9F093DDEAF4AD0DD44550410DDC1BB72E5C8E3C85624B3DD2D36A373C093BAA8`.
- Module SHA-256 `8249F353B95259B10391E0F493A2A8EDD9CC6B435A3B5A831D7A0F82A4D27112`.
  Both stayed unchanged. This is the installed combined development build, not a single-change A/B candidate.

Private artifacts (relative to repository root):
`scratch/qa-hubs/citadel-autoload-save96-check/sessions/20261108-232727-897`.
Probe: `2026-11-08_23-29-01-citadel-fixed-save-turns-probe.json` with frame, tick and call CSV files.
The directory also holds frozen scripts, config/build/machine receipts, a pre-capture snapshot, logs,
`analysis.json`, `investigation.json`, `tick-trace-check.json`, and restoration receipts. Saves, pictures,
recordings and raw traces remain private and are not committed.

## Timings

Actual capture 90.0073 seconds, 9,199 raw frames/ticks. The frame summary excludes the first/last second,
leaving 8,994 frames. Times are milliseconds; maxima in different rows need not be the same event.

| Scope | Median | p95 | p99 | Maximum |
|---|---:|---:|---:|---:|
| Frame interval | 9.3597 | 13.3224 | 16.1031 | 36.0044 |
| Render-thread busy work, excluding XR waits | 7.5263 | 11.7776 | 14.5577 | 21.2755 |
| GPU measured span | 9.0460 | 13.4001 | 16.3770 | 24.2042 |
| Game-thread engine tick | 3.5736 | 6.7155 | 8.6371 | 25.4217 |
| Game-thread host work | 0.2646 | 0.9880 | 2.8036 | 25.5610 |
| Whole game-thread hook | 3.9094 | 7.5200 | 10.2282 | 30.3781 |
| Queue drain | 0.0011 | 0.0021 | 0.0031 | 0.2044 |
| xrEndFrame wall time | 1.1811 | 3.0407 | 4.8827 | 11.5865 |
| xrWaitFrame wall time | 0.0199 | 0.0337 | 0.0419 | 1.1864 |

**1,492 / 8,994 intervals (16.59%) exceeded 11.111 ms; 5 exceeded 22.222 ms.** These are timing-budget
exceedances, not headset drop counts. The null driver does not reproduce physical display pacing or Virtual
Desktop encoding/network/decoding. GPU span can include idle time. Analyzer `game_wall`/`game_busy` fields
refer to the render-thread span before Present; actual game-thread work is reported separately above.

## Outlier evidence and next targets

1. **Waiting/scheduling outside the measured tick hook remains a target.** The worst two intervals at
   48.752/48.801 seconds were 36.004/32.246 ms. Their pre-Present render spans were 34.950/29.784 ms wall,
   but only 10.485/8.147 ms busy. They overlapped gaps of 30.923/29.247 ms outside the complete tick hook.
   Queue drains were tiny and no queued calls occurred. xrEndFrame was only 0.436/0.338 ms. This supports
   investigating thread waits/scheduling; without stacks it does not identify the wait or prove a culling cause.
2. **Host CPU still has a separate spike.** The 27.216 ms interval at 2.350 seconds overlapped a 30.378 ms
   hook, including 25.561 ms host wall / 25.033 ms host busy time. No settings-file writes occurred anywhere
   in the capture. This does not match the removed camera-diagnostic INI-write mechanism; sample the host
   routines to identify the remaining work. A single occurrence does not establish periodicity.
3. **Engine CPU also spikes.** The 23.865 ms interval at 34.054 seconds overlapped a 25.422 ms engine tick.
   No sampled stack was collected, so this capture cannot name the engine routine or establish GC.
4. **Steady rendering exceeds the 90 Hz budget in some views.** Render busy p99 was 14.558 ms, draw-count
   median/p99 3,434/3,807. Measure visibility/query/replay costs and correlate with sweep angle before changing
   culling. Resolution sensitivity needs a controlled repeat at this same save/build; earlier Normandy runs
   differ in scene, eye size and module and are not a controlled comparison.

The previous 80-93 ms pause did not recur in this run. The new v2 hook/drain trace passed native completeness
and timing checks with zero dropped tick/call rows. No queued calls were invoked, so their execution path
still lacks native coverage here. There were zero GPU timestamp drops, access violations, share failures or
draw-replay failures. No intrusive stack sampler, snapshot, console query or replay reload ran inside the
measurement window.

## Reproduction and restoration

Use [the automatic Citadel session instructions](../tools/qa-hubs/docs/citadel-auto-load.md). With SteamVR
already selected as runtime, Steam signed in, and the game/launcher/SteamVR closed:

```powershell
$package = '.\scratch\qa-hubs\citadel-autoload-save96-check'
& .\tools\qa-hubs\citadel-session.ps1 -Action Measure -PackageDirectory $package -FakeHeadset `
  -EyeWidth 2284 -EyeHeight 2448 -SweepDegrees 120 -CaptureSeconds 90
```

Re-analyze a completed session without launching anything:

```powershell
$run = '.\scratch\qa-hubs\citadel-autoload-save96-check\sessions\20261108-232727-897'
& .\tools\qa-hubs\analyze-session.ps1 -RunDirectory $run
```

Both scripts accept `-PythonExe` when Python is not on PATH. The private archived fixture is required;
future changes to the player's original save do not affect it. This command does not replay the user's walk.

The native run completed and restored all **40 protected files**, with zero hash mismatches. The game and
SteamVR were closed, the QA lease removed, the prior headset configuration restored, and the global runtime
left unchanged. `verified-final-state.json` records the final checks. Offline hub checks and four mocked
session lifecycle cases passed before the native run. These establish capture/restoration behavior, not
physical-headset smoothness or performance acceptance.
