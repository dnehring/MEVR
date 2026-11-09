<!-- SPDX-License-Identifier: MIT -->
# Gameplay performance priorities

Updated 2026-11-08. Evidence: [head-turn investigation](../research/63-head-turn-performance.md).
Reusable validation: [cafeteria QA](../tools/qa-cafeteria/README.md) and
[hub route catalog](../tools/qa-hubs/README.md). Hub drafts remain excluded until their actual scene and walking route are verified.

This is engineering order. Item 2 is the largest individual hitch; item 3 is directly observed in the physical
headset. Keep One Draw enabled and distinguish each problem's acceptance criteria.

| Order | Problem | Evidence and next action | Status |
|---|---|---|---|
| 1 | Camera-diagnostic disk writes | Recurring 12-18 ms host stalls; sampled inside synchronous INI writes. Measurements now publish in memory; three High runs had zero automatic settings writes and host maxima 7.03-7.54 ms. | Implemented; local checks passed; headset calibration pending |
| 2 | Rare 80-93 ms gameplay pauses | Most of the pause lies between the original measured game ticks. Buffered whole-hook, queue-drain and individual queued-call timings are implemented; validate the trace locally, then inspect scheduling/waits if the queue is quick. | Builds/automated checks passed; native capture deferred for another owned game test; cause unresolved |
| 3 | Physical-headset submission stalls | Earlier VDXR captures have 35-62 ms worst intervals; 68 of 72 long intervals are dominated by xrEndFrame. Correlate GPU/resource/scheduling evidence using a controlled headset sweep. | Wait location identified; underlying cause unresolved |
| 4 | Periodic engine CPU spikes | Approximately 19 ms CPU in the same sampled chain under UWorld::Tick. Identify the routine before changing engine behavior; GC remains a hypothesis. | Cause unresolved |
| 5 | Dense-scene rendering budget | Physical captures reach about 10 ms render CPU at p99 against 11.111 ms at 90 Hz. Measure visibility/query/replay costs per frame, then optimize the measured bottleneck. | Culling contribution unproven |

## Acceptance for item 1

- Automatic camera measurements no longer modify settings files during gameplay.
- The calibration menu receives current measurements without waiting for a file timestamp change.
- Trigger alignment requires a measurement taken after recentering; stale or invalid samples cannot be applied.
- Explicit camera alignment/profile saves and arm-length calibration saves remain functional.
- Compare repeated High captures with the original host-stall signature, preserving saves, settings, resolution and One Draw.
- Record any remaining large pauses separately. Improvement in item 1 does not close items 2-5.

## Baseline

Three clean 90-second cafeteria captures at source 3840 x 3840, eye 2688 x 2880, AA/shadows/AO on, blur off,
One Draw/sharePass=5: frame interval p99 7.73/7.80/7.73 ms; maxima 15.08/92.74/15.16 ms.
The severe-hitch regression gate fails on run 2. Null-headset throughput does not validate physical headset pacing.

## Item 1 implementation

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

## Item 1 local result

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
