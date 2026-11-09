<!-- SPDX-License-Identifier: MIT -->
# Gameplay performance priorities

Updated 2026-11-09. Evidence: [head-turn investigation](../research/63-head-turn-performance.md),
[stationary C-Sec capture](../research/65-citadel-stationary-performance.md), and
[six-run preset matrix](../research/66-citadel-performance-matrix.md), and
[startup/title investigation](../research/67-startup-performance.md).
Reusable validation: [cafeteria QA](../tools/qa-cafeteria/README.md) and
[hub route catalog](../tools/qa-hubs/README.md). Hub drafts remain excluded until their actual scene and walking route are verified.

This is the order for the next engineering work: first the hitch reproduced across every preset, then the
remaining simulation gaps and host CPU bursts, followed by the separately measured physical-runtime stalls.
Keep One Draw enabled. Successful measurement/restoration does not mean the performance gate passed.

| Order | Problem | Evidence and next action | Status |
|---|---|---|---|
| 1 | Periodic garbage collection | Every Citadel preset/repeat has a 22.76-25.19 ms engine tick and a 23.07-25.47 ms whole hook. Stacks and the engine caller now identify collection on the configured 60-second timer. Measure phase/object pressure and investigate avoidable mod-owned churn; retain collection and bounded memory use. | Cause identified; mitigation remains open; no GC behavior changed |
| 2 | Long gaps outside measured ticks | Rare Normandy intervals reached 80-93 ms; the custom Citadel workload reached 36 ms with a 31 ms gap outside the full hook. Queued calls were absent and drains tiny in the Citadel trace. Inspect scheduling/waits; do not disable queued-call safety. | Not reproduced at the old severity in the six preset runs; remains open |
| 3 | Remaining host CPU bursts | Preset runs reach 7.76-10.31 ms host work; custom Citadel reached 25.56 ms, with zero settings writes. Sample the host/mod routines and distinguish polling/object scans from waits before optimizing. | Separate from the fixed diagnostic INI writes; cause unresolved |
| 4 | Physical-headset submission stalls | Earlier VDXR captures have 35-62 ms worst intervals; 68 of 72 long intervals are dominated by xrEndFrame. Correlate GPU/resource/scheduling evidence with the physical headset. | Null-driver runs cannot close this issue |
| 5 | Dense-scene render CPU and oversize source resolution | Preset p99 frame intervals are 9.62-10.29 ms; custom 3840-square source reaches 16.10 ms. Preset bundles also change shadows/culling/foliage, so isolate those costs before assigning causality. | Culling contribution unproven; preserve the oversize case as a stress test |
| 6 | Startup graphics resets and presentation pacing | Two title captures reproduce about 153 ms host stalls during the remaining 15-second blanket settings reset, despite healthy blur protection. Make settling correction selective/guard-aware. Separately, the approximately 32 ms title cadence is dominated by xrWaitFrame, not CPU busy time; verify on a physical headset. | Reset path measured; behavior fix and physical pacing validation open |
| 7 | Broader traversal | The fixed C-Sec benchmark does not validate the user's full walk or other hubs. Preserve the archived save and require an observed route before admitting another scene. | Other map development remains paused |

## Current regression gate

The six preset captures all pass measurement validity and p99 <=11.111 ms at nominal 90 Hz. All fail the
whole-hook <=22.222 ms limit; only two fail the frame-interval <=22.222 ms limit. Keep both hitch checks.
Preset median p99: Quality 10.16 ms, Balanced 10.07 ms, Performance 9.84 ms. Each passes the 72/90 Hz
arithmetic p99 budgets and fails the 120 Hz budget; none is a physical-headset pacing acceptance test.

The first follow-up diagnostic reproduced the collection chain twice, 60.008 seconds apart, and identified
its timer through the LE1 SDK field and a read-only engine inspection. Its sampler substantially changed
SteamVR pacing, so only the clean six-run matrix is used for performance acceptance. See the
[diagnostic evidence and limitations](../research/66-citadel-performance-matrix.md#first-priority-garbage-collection-identified).

## Completed improvement: camera diagnostic writes

The old first item is implemented: camera measurements publish in memory, and all six new preset captures
also had zero settings-file writes. Keep the calibration acceptance checks below; this does not close the
new host-burst item. Motion-blur protection and One Draw stayed active throughout the matrix.

## Camera diagnostic acceptance

- Automatic camera measurements no longer modify settings files during gameplay.
- The calibration menu receives current measurements without waiting for a file timestamp change.
- Trigger alignment requires a measurement taken after recentering; stale or invalid samples cannot be applied.
- Explicit camera alignment/profile saves and arm-length calibration saves remain functional.
- Compare repeated High captures with the original host-stall signature, preserving saves, settings, resolution and One Draw.
- Record remaining pauses separately. Removing diagnostic writes does not close the open list above.

## Baseline

Three clean 90-second cafeteria captures at source 3840 x 3840, eye 2688 x 2880, AA/shadows/AO on, blur off,
One Draw/sharePass=5: frame interval p99 7.73/7.80/7.73 ms; maxima 15.08/92.74/15.16 ms.
The severe-hitch regression gate fails on run 2. Null-headset throughput does not validate physical headset pacing.

A later [stationary C-Sec run](../research/65-citadel-stationary-performance.md) captured frame interval p99
16.10 ms / maximum 36.00 ms, a separate 25.56 ms host CPU spike with zero settings writes, and a 25.42 ms
engine tick. It validates the whole-hook trace and provides a fixed Citadel benchmark. Different scene,
eye resolution and module mean it is not a controlled comparison with the Normandy baseline.

## Camera diagnostic implementation

Camera observations now pass from the game-thread producer through HostServices v38 into a host-owned,
thread-safe in-memory sample. The classic calibration menu reads that sample directly. Samples expire after
15 seconds, and trigger alignment requires a sample newer than its recenter request. Hot reload clears the
sample. Older hosts safely disable publication instead of falling back to diagnostic file writes.
For development installs, deploy the updated host and mod together so calibration receives the new service.

Explicit camera alignment/profile saves and arm-length calibration saves retain their existing paths. Old
`[Camera] measured*` keys are left in the player's file but are no longer read or automatically updated.

The reusable private-candidate build, three-run validation, result checker and recovery instructions are in
[the QA README](../tools/qa-cafeteria/README.md). The private build selects SteamVR only for its own process;
it is not a release artifact. Physical headset calibration and comfort remain separate checks.

## Camera diagnostic local result

Three independent High launch/walk/90-second capture/restore runs retained the baseline resolution, AA,
shadows, AO and One Draw. Every run delivered a fresh camera sample and had zero settings-file writes.
Host tick maxima were **7.5417 / 7.5129 / 7.0280 ms**, versus **17.5310 / 16.6859 / 16.4921 ms** in the
earlier baseline. No access violations, GPU timestamp drops or draw-replay failures occurred.

Frame-interval p99 stayed approximately 7.8 ms. Each run still had an engine CPU spike: engine wall maxima
21.4346 / 19.4919 / 19.2030 ms. The general severe-hitch gate still fails because run 1's engine-plus-host
tick reaches 22.2758 ms against its 22.222 ms limit. The rare 80-93 ms pause did not recur; item 2 remains open.

These runs used the current combined development source, which includes other pending work beyond this
change. They validate live publication and removal of automatic writes; the timing comparison is not a
strict single-change A/B experiment. Full tables and receipts are in [research/63](../research/63-head-turn-performance.md).
All 42 protected files and the original installed ASI were restored and verified. The global VDXR runtime
was retained. No private QA binary remains installed.

Validation: LE1/LE2/LE3 builds, the main and private-candidate unit suites, all 22 CTest checks across the
initial pass and targeted installer retry, QA offline fixtures and SPDX checks passed. The general timing
gate and physical-headset acceptance remain open as described above.
