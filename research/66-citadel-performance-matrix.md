<!-- SPDX-License-Identifier: MIT -->
# Citadel performance preset matrix

Measured 2026-11-08/09: six independent 90-second SteamVR null-headset captures at the archived C-Sec save,
in Quality/Balanced/Performance/Performance/Balanced/Quality order. All six passed measurement validity and
restoration checks. All passed the 90 Hz p99 budget; **all failed the separate 22.222 ms whole-hook limit**.

This is a comparison of menu preset bundles, not a single-variable resolution test. It uses the same save,
head sweep, installed host/module, hardware, power scheme and nominal 90 Hz null driver as
[the stationary benchmark](65-citadel-stationary-performance.md). Mako/Blender work was paused during capture.
The host/module hashes remained `9F093DDE...C093BAA8` / `8249F353...D27112` (full hashes in report 65 and receipts).

## Workloads

| Preset | Source resolution | Eye target | Small-object threshold | Foliage multiplier | Dynamic shadows | AO |
|---|---|---|---:|---:|---|---|
| Quality | 2284 x 2448 | 2284 x 2448 | 4 px | 0.75 | Off | On |
| Balanced | 1942 x 2080 | 2284 x 2448 | 8 px | 0.50 | Off | On |
| Performance | 1714 x 1836 | 2284 x 2448 | 24 px | 0.35 | Off | Off |

AA and One Draw/sharePass=5 remained on; motion blur remained off with the guard protecting gameplay.
FSR was selected throughout and verified active for Balanced/Performance with the expected source/target sizes.
Quality renders at the target size and bypasses scaling. Live settings-application logs matched each workload.
Every run restarted the game to apply the source resolution. No walking input was sent.

## Results

Frame summaries trim one second from each boundary. All times below are milliseconds. Rows are in run order.

| Preset/run | Frame p99 | Worst interval | Worst engine tick | Worst host work | Worst whole hook | Intervals >11.111 | Intervals >22.222 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Quality 1 | 10.0406 | 24.9006 | 22.8712 | 10.3120 | 23.2039 | 41 | 1 |
| Balanced 1 | 10.1894 | 23.4788 | 25.1941 | 8.5689 | 25.4748 | 44 | 1 |
| Performance 1 | 10.0667 | 21.4829 | 22.8868 | 8.6403 | 23.1630 | 31 | 0 |
| Performance 2 | 9.6191 | 20.6870 | 22.9741 | 8.8877 | 23.5665 | 23 | 0 |
| Balanced 2 | 9.9532 | 21.5971 | 22.7615 | 8.2056 | 23.0672 | 44 | 0 |
| Quality 2 | 10.2877 | 20.4289 | 23.0069 | 7.7584 | 23.4369 | 57 | 0 |

Across 73,553 measured frames, 240 intervals exceeded 11.111 ms (0.326%) and two exceeded 22.222 ms.
Median p99 across repeats: **Quality 10.16, Balanced 10.07, Performance 9.84 ms**. Every run passes p99 against
72 Hz (13.889 ms) and 90 Hz (11.111 ms), and fails against 120 Hz (8.333 ms). These are arithmetic budget
checks on a nominal 90 Hz null driver, not measurements at three physical refresh rates or headset drop counts.

The engine tick remains 22.76-25.19 ms across all presets. Buffered rendering can hide a simulation stall from
the largest frame interval: four runs pass the interval-only severe gate while their full game-thread hooks
still fail. Preserve both metrics in regression checks. The earlier 80-93 ms rare pause did not recur here.

GPU spans in the first pass were about 4-5 ms at p99, while render CPU p99 stayed about 9.4-9.5 ms. These
are GPU timestamp spans, not guaranteed GPU busy time. The small differences between presets are not proof
of a statistically significant speedup. Culling's contribution remains unproven; lowering settings did not
remove the timed engine hitch.

The earlier custom 3840 x 3840 source workload had p99 16.10 ms and a 36 ms worst interval. It also enabled
dynamic shadows and differed in foliage/culling, so retain it as a separate oversize-render stress case rather
than attributing the entire difference to resolution. Physical VDXR submission stalls remain a separate problem.

## Validity and restoration

All captures had zero settings-file writes, access violations, GPU timestamp drops and draw-replay/share
failures. Whole-hook v2 traces passed their consistency checks. The pawn remained at the validated archived
save location. All six backup manifests were rechecked after the batch: 40 protected files each, no mismatches,
unchanged installed binaries/runtime, no remaining game/SteamVR processes or QA lease.

The batch ledger is private: `scratch/qa-hubs/citadel-matrix-20261108/{plan,runs,comparison}.json`.
Its run directories are under `scratch/qa-hubs/citadel-autoload-save96-check/sessions/`:
`20261108-234032-626`, `20261108-234353-927`, `20261108-234713-646`, `20261108-235035-227`,
`20261108-235357-970`, `20261108-235720-569`. Each retains raw metrics, config/build/machine receipts,
source scripts, scene snapshot, outlier analysis and restoration evidence.

Re-run with [citadel-matrix.ps1](../tools/qa-hubs/citadel-matrix.ps1); re-analyze with
`python tools/qa-hubs/compare-matrix.py <batch>`. The checker separates invalid evidence from a valid slow
capture and rejects One Draw failures, actual resolution/FSR mismatches, changed builds/settings/hardware,
and incomplete run plans. Offline workload edit/restore checks, session lifecycle checks and negative
comparison cases passed. See [the runner instructions](../tools/qa-hubs/docs/citadel-auto-load.md).

## First priority: garbage collection identified

After consolidating the clean results, a separate Balanced diagnostic session
`20261109-000217-243` captured the game-thread stacks. It is **excluded from the performance comparison**:
SteamVR pacing changed substantially (xrWaitFrame p50 25.59 ms; frame interval p50 31.83 ms). The sampler
suspended the thread 10,375 times: median 0.432 ms, p99 2.040 ms, maximum 5.778 ms, total 6.253 seconds.
These measurements cannot be treated as unperturbed gameplay timings.

Nevertheless, two engine spikes at probe times 21.748 and 81.757 seconds sampled the same chain as the
earlier Normandy diagnostics: UWorld::Tick -> wrapper at RVA 0x414DD0 -> collection routine 0x172820 ->
object-reference traversal 0x6F170. The two ticks were 26.216 and 23.729 ms wall time, including measured
sampler suspensions of 3.469 and 1.216 ms. Raw unwind fragment 0x414E04 belongs to the containing wrapper
0x414DD0; the previous report's address was a fragment boundary, not a distinct function.

A read-only inspection of the saved local engine database identified the caller's timer comparison against
the engine field at offset 0x834. The LE1 SDK independently names that field
`TimeBetweenPurgingPendingKillObjects`; the preserved BIOEngine.ini config sets it to **60 seconds**.
The wrapper invokes object collection and resets its elapsed timer. The sampled inner routine traverses
object references and updates reachability state. Together with the repeated 60.008-second separation,
this identifies **periodic garbage collection**, rather than relying on the interval alone to infer it.

The original Ghidra project was already open and remained untouched. The first isolated-copy attempt
stopped on its exclusively locked unsaved page store. The successful retry copied and hash-verified only
the stable saved database files; unsaved analysis was explicitly excluded. Private receipt/output:
`%LOCALAPPDATA%/MEVR/ghidra/isolated/engine-spike-20261109-000744-709/`.
Reproduction: `tools/qa-cafeteria/inspect-engine-spike-isolated.ps1`. No decompiled engine content is committed.

The diagnostic also caught a host burst in the mod, but its available symbols resolve only to export
fallbacks (`MevrModTick`). That does not identify an individual host routine. One additional long engine
tick sampled actor work and the message pump; do not label every spike as garbage collection.

All 40 protected files and the installed binaries/runtime were verified again after the diagnostic. No
gameplay/GC behavior was changed. The first improvement now has a confirmed target: measure collection
phases and object pressure, then reduce avoidable mod-owned object churn or evaluate scheduling with bounded
memory and latency acceptance. Disabling collection or merely lengthening its interval would hide the
benchmark event without demonstrating a lower collection cost. The remaining priorities are preserved in
the [consolidated list](../docs/performance-priorities.md).
