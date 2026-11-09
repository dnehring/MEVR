<!-- SPDX-License-Identifier: MIT -->
# Head-turn performance: visibility work and submission stalls

Status: analysis of the 2026-11-08 physical VDXR captures, followed by a local SteamVR null-headset
Normandy test at two source resolutions. Temporary runtime/resolution changes were restored and
verified. No culling behavior changed. Equivalent physical SteamVR traces and combined-build
measurements remain needed; the owner reports seeing spikes in both runtimes.

## Prioritized diagnosis after the repeated cafeteria runs

The maintained action list is [docs/performance-priorities.md](../docs/performance-priorities.md).

This is the recommended **engineering order**, balancing evidence, scope and player impact. Item 2 has the
largest individual stall; item 3 has the most direct evidence from the player's physical-headset complaint.
Item 1 comes first because it is a narrowly identified mod behavior with a concrete replacement. These are
separate signatures: removing one must not be reported as resolving the others.

| Order | Problem and diagnosis | Confidence and impact | Next action and acceptance evidence |
|---|---|---|---|
| 1 | Synchronous camera-diagnostic persistence on the game thread. ArmIk calls `WriteCameraOffset`; changed measurements cause three Win32 profile writes about once a minute. Host stalls coincide with the writes, and all three sampled host spikes are inside that API path. | High confidence that this path contributes to the recurring 12-18 ms host stalls. Exact removable time still needs an A/B. | Keep the current diagnostic sample in memory and expose it to the calibration menu; preserve explicit player saves. Repeat the same High test: automatic camera writes and their host stalls should disappear, while the menu still receives current measurements. Check settings reload activity as well. |
| 2 | Rare 80-93 ms pauses, with approximately 91 ms missing between measured game ticks in the clean batch. The render thread mostly waits; same-frame GPU work and submission do not explain the gap. | Confirmed symptom; underlying cause unassigned. Highest single-event severity, roughly eight nominal 90 Hz periods. Present in one of the three clean runs and an earlier separate capture. | Time entry to the entire tick hook, queue drain, each queued call, and the interval between hook invocations. Capture scheduling/wait evidence if the queue is quick. The current severe-hitch gate must pass, with a longer repeat/soak because three short runs can miss a rare stall. |
| 3 | Physical VDXR submission waits. In the earlier descriptive captures, 68 of 72 intervals above 22.222 ms spend more than half their time inside `xrEndFrame`; worst frame intervals span about 35-62 ms. | High confidence about the wait location; uncertain whether the producer is mod/driver resource synchronization, queued GPU work or runtime/compositor contention. This is not proof of a Virtual Desktop defect. | Repeat one controlled physical-headset sweep with matching build/settings, then correlate submission, GPU queues/copies and scheduling. Compare SteamVR separately. Acceptance requires lower repeated long-interval counts and stable headset motion; a null-headset pass cannot satisfy this item. |
| 4 | Periodic engine CPU work, approximately 19 ms CPU in clean runs and 20-21 ms wall when sampled. The same unnamed chain beneath `UWorld::Tick` repeats. | Confirmed engine cost; function purpose still unknown. Minute-scale housekeeping/GC is a hypothesis, not a diagnosis. | Identify the sampled function chain and add focused duration/count measurements. Only then select a bounded change. Validate engine-tick maxima separately from render intervals, which can conceal a simulation stall. |
| 5 | Dense-scene rendering leaves limited budget. Earlier physical captures reach approximately 10 ms active game-render CPU at p99 against an 11.111 ms display period. | Confirmed capacity pressure, but no evidence yet that a sudden culling burst explains the severe outliers. | Add frame-aligned visibility/query/replay counters and optimize whichever work consumes the budget. Preserve visibility correctness and One Draw; check pop-in as well as timing. |

The camera file writes also invalidate settings-file timestamps. `HandCalibration::Refresh` and the classic menu
re-read that file after a change. This is a credible secondary cost, not yet a measured explanation for another
specific spike. Moving the writes to a worker alone would leave that invalidation and possible profile-reader
contention; an in-memory diagnostic sample addresses the unnecessary persistence itself.

The largest clean pause is approximately 16 seconds from the closest camera-file write. Its engine tick also falls
outside the separate periodic CPU spike. Those observations prevent attributing every hitch to either known
minute-scale activity. The queue is a timing blind spot, not a proven culprit: do not drop or reorder queued
ProcessEvent calls before measuring them.

One Draw remained healthy throughout the comparable batch: no access violations, GPU timestamp drops or draw-replay
failures. The earlier AA failure cascade, motion-blur return, and menu-reset behavior remain separate regression
checks; these stationary post-load probes do not revalidate menu/title transitions. No new evidence here justifies
disabling One Draw or changing garbage-collection/culling policy.

## What the captured spikes show

The private analyzer `scratch/performance-spikes-20261108/analyze.py` uses the repository's
`tools/perf-probe.py` frame calculations. It excludes known menu transitions and probe boundary
regions; all retained rows have xr_frame and should_render set. The explicit windows and source
files are saved in `analysis.json`. Views, head motion, build and resolution differ across runs:
these are descriptive samples, not a controlled resolution or guard A/B.

All four captures used 90 Hz (11.111 ms display period), AA and One Draw. Long intervals below
mean more than two display periods; they are not a direct count of compositor-dropped frames.

| Capture | Retained frames | Interval p99 / max (ms) | Intervals over 22.222 ms | Of those, xrEndFrame takes over half the interval |
|---|---:|---:|---:|---:|
| 14:21:36 large pan, source 3072 x 3188 | 2,226 | 37.91 / 62.43 | 36 | 36 |
| 14:22:23 large follow-up | 2,148 | 20.75 / 38.32 | 14 | 12 |
| 14:30:23 Balanced pan, source 2284 x 2448 | 1,482 | 18.43 / 34.71 | 5 | 4 |
| 15:09:24 guard post-conversation gameplay, Balanced | 5,247 | 16.44 / 40.90 | 17 | 16 |

The Balanced pan's worst retained frame (relative 15.1337 s) takes 34.7145 ms. Its active
game-render CPU time is 1.3484 ms, xrEndFrame takes 27.0508 ms, and the draw count is 643.
The three preceding frames have 663, 647 and 661 draws. GPU timestamp lag at Present is
34.9694 ms. The longest overlapping engine/host ticks are 2.4785/0.6574 ms. This is distinct
from the excluded 119.951 ms menu-return frame correlated with the old settings reset.

The guard capture's worst retained frame (relative 1.5449 s) takes 40.8990 ms, with 5.7461 ms
game-render CPU work and 34.7813 ms in xrEndFrame. Its 2,924 draws follow 3,100, 3,018 and
2,953 draws. Another spike at 43.6519 s takes 39.2118 ms with 29.7538 ms in xrEndFrame and
only 4.3294 ms game-render CPU work. The adjacent draw counts are similarly ordinary.
None of the guard capture's 17 long intervals exceeds that run's p95 draw count.

These observations make a same-frame explosion in visible geometry or CPU visibility processing
an insufficient explanation for the largest measured hitches. They do not identify which GPU pass,
driver operation, compositor workload or resource synchronization caused the wait. A stall observed
inside xrEndFrame is a location, not proof that Virtual Desktop itself caused it. GPU timestamps
here include idle/queued intervals; the analyzer's gpu_work is an upper bound, not shader-only GPU
execution time. Inspect preceding frames as well as the frame where a wait becomes visible.

## What remains plausible about culling

Dense views still increase normal render work: in the guard capture, game-render active CPU time
is 5.59 ms at the median, 7.76 ms at p95, 10.00 ms at p99 and 14.85 ms at maximum. At 90 Hz,
the higher values leave little or no time for remaining frame work. Optimizing this workload can
improve headroom even if it does not explain the separate 30-60 ms submission waits.

The existing `RenderLevers` hooks measure Umbra's instance, occluder/flush and result-read phases,
but publish aggregate log periods, not these counters alongside each perf CSV frame. The nearby
15:09:41 log reports a 3.64 ms average scene render, 0.51 ms in Umbra, 0.02 ms in its result reads,
and a 0.12 ms longest result read for that log period. This period overlaps several states and
does not cover the entire later gameplay capture. It cannot rule out a different query/read path
or attribute a particular CSV outlier. The occluder/flush category also includes real rendering
work; it is not a pure visibility-traversal timer.

One Draw was healthy and the logs report one scene render per frame. Older two-render experiments
in [research/35](35-performance-attempt-log.md) do not directly describe this current path.
Those experiments nevertheless establish why indiscriminate culling bypasses are risky:
per-eye histories caused periodic draw bursts, and answering pending Umbra queries as visible
doubled draws in the tested scene. The visibility/query lifecycle must remain intact.

## Next measurement and optimization sequence

1. Pin the actual ASI hash, save/location, refresh rate, source/eye size, AA, One Draw and guard
   health. Confirm menu-return reset suppression in the running build. Another task has continued
   installing Mako candidates, so the 15:38 install receipt is not a permanent current-build identity.
2. Record one repeatable head sweep at a fixed Normandy position, bracketed by still-view periods.
   Replay that same pose sequence several times: first exposure versus warmed repetitions separates
   resource/shader warm-up from a recurring view-dependent cost. Keep the comparable scene and
   projection when repeating under SteamVR; do not compare unrelated sweeps as an A/B.
3. Extend capture-only frame telemetry with scene/Umbra phase time, timed query reads and maximum
   read latency, visible/cull counts, shader-state creations, offscreen-capture count/time, replay
   CPU time and draws. Associate it with the same frame ID and GPU/submission timestamps. Existing
   hooks supply most counters; no new visibility policy is needed to diagnose them. Record to a
   bounded buffer and write afterward, with no per-frame logging or added GPU drain.
4. If query latency or visible-set bursts match spikes, target that verified path while preserving
   the query lifecycle and checking pop-in. If first-use shader/resource work matches only cold
   sweeps, target cache/warm-up costs. If CPU/visibility remain normal while submission and GPU lag
   jump, capture a GPU/CPU scheduling trace to identify queue stalls, resource residency, copies,
   driver work and runtime contention before selecting an optimization.
5. Compare p95/p99 frame time and long-interval counts with the identical sweep, alongside visible
   correctness. Keep AA and One Draw enabled. Average FPS alone is insufficient for this symptom.

That physical-headset analysis motivates frame-aligned visibility/query telemetry plus GPU scheduling
evidence. The later local captures add the more specific leads ranked at the top of this document.
The available captures support investigating culling, but do not justify making culling more aggressive
or removing waits wholesale.

## Local fake-headset test, 2026-11-08

Ran the installed ASI unchanged with SteamVR/OpenXR 2.18.4's null driver, at the resumed
Normandy CIC save (`BIOA_NOR00`, gameplay mode 0). Eye images confirm the CIC; synthetic
stationary and 30-second full-turn pose files use the previously captured Quest projection
angles. Input replay is off. AA and One Draw remain enabled, the motion-blur guard reports
installed with zero unknown proxies, and the eye target stays 2688 x 2880. Each resolution
gets a 30-second still capture and a 90-second repeated-turn capture. Snapshots and console
requests are outside measured windows. Analysis excludes one second at each probe boundary.

| Source size / motion | Retained frames | Frame interval p99 / max (ms) | Game-render CPU p99 (ms) | Intervals over 22.222 ms |
|---|---:|---:|---:|---:|
| 2284 x 2448 / still | 3,896 | 9.87 / 21.25 | 8.90 | 0 |
| 2284 x 2448 / turns | 12,236 | 8.74 / 19.87 | 7.48 | 0 |
| 3072 x 3188 / still | 3,904 | 10.10 / 12.83 | 9.01 | 0 |
| 3072 x 3188 / turns | 12,268 | 8.33 / 15.20 | 6.71 | 0 |

This did **not** reproduce the physical VDXR submission stalls. Despite a nominal 90 Hz
display period, the fake session runs around 139 application frames/second. It lacks the
physical headset/streaming workload and does not establish real compositor pacing or
headset smoothness. Do not interpret the lower maximum in the high-resolution sample as
an improvement caused by raising resolution. The resolution change increases the still
view's median GPU timestamp span from 2.61 to 4.09 ms; these remain upper bounds on work.

Draw counts vary with direction (roughly 460-2,540 in the turn samples) and explain ordinary
render CPU cost well (the probe's linear fit has r-squared 0.88 in both turn runs). Repeated
turn sections have similar distributions. The steady 60-second log periods report exactly
one scene render per frame, no replay failures/fallbacks/re-arms, and no new shader-state
keys. Umbra resolves average 0.53/0.55 ms, with a longest result read 0.53/0.47 ms in the
Balanced/high-resolution steady periods. These aggregates cannot exclude an uninstrumented
per-frame query delay, but the test supplies no evidence of a culling-driven draw explosion.

### A separate, actionable hitch: diagnostic camera writes

Balanced's two largest interval outliers overlap two different game-thread events:

- At 21.092 s in the still probe and 43.911 s in the later turn probe, engine ticks take
  22.39/20.09 ms wall and 21.36/19.13 ms CPU. Their absolute times are about 60.005 s apart.
- At 25.096 s in the still probe and 47.916 s in the turn probe, host/mod ticks take
  25.88/25.23 ms wall but only 2.59/3.14 ms CPU. These also repeat about 60.006 s apart.
  The corresponding render intervals are 21.25/19.87 ms, with ordinary draw counts and
  only 0.26/0.25 ms in xrEndFrame.

The high-resolution turn's worst frame, 15.20 ms at 56.839 s, overlaps a host/mod tick of
18.46 ms wall / 2.03 ms CPU. Active game-render work is 4.99 ms and xrEndFrame is 0.30 ms.
A read-only timestamp monitor observes LE1MEVR.ini written at 17:26:57.6698, within the
approximate absolute interval of that frame (probe-start log at 17:26:00.823). The preceding
write is 17:25:57.6698, exactly a minute earlier.

`game::hand_calibration::WriteCameraOffset`, called by ArmIk's camera diagnostic on the game
thread, writes `measuredForward`, `measuredRight` and `measuredUp` through three synchronous
`WritePrivateProfileStringW` calls, throttled to once per minute when values change. The
following ArmIk log line is at 17:26:57.669. The same path exists in the installed source
snapshot. This is strong timing and code evidence for a diagnostic-persistence hitch,
but an instrumented/changed candidate has not yet isolated the write calls' duration.

The next narrow optimization should keep this diagnostic sample in memory during gameplay
and supply it to the classic calibration menu without periodic synchronous disk writes.
Preserve explicit calibration saves. Merely moving the writes to a worker may still contend
with other Win32 profile readers; repeat the same sweep with a measured candidate to verify.

The separate engine CPU spike remains unidentified. The local engine config specifies a
60-second pending-kill purge interval, making cleanup a hypothesis, not a confirmed cause.
No garbage-collection setting was changed. Add a stack trace or engine-specific timing
before attributing it to GC, visibility, or another engine task.

### Build and restoration boundary

The tested installed binary is a later Mako candidate, not the earlier combined-install
receipt. Its blur guard is present, but its logs still show the old menu-return settings
reapply at startup. The captures start after load/settle and exclude menu transitions;
this test does not validate conditional menu-return reset suppression.

Private evidence: `scratch/performance-spikes-20261108/fake-run/` contains the exact ASI hash,
42-file backup/restore manifest, four CSV/JSON probe sets, eye snapshots, logs, synthetic
poses, analyzer, write timestamps, and restoration receipt. All 42 protected files were
verified after restoration; no save changes, installed-binary changes, or pending requests
remain. SteamVR was stopped and the original global Virtual Desktop runtime retained.

## Cafeteria walk with higher settings (2026-11-08, evening)

The fake-headset test was extended from the CIC save to the mess table using controller
walking, the normal door Use action, the stairs, and the side passage around the elevator.
Private rerun entry: `scratch/performance-spikes-20261108/walk-run/run.ps1`; the folder also
contains input recordings, checked waypoints, the arrival image, transcripts and restoration
manifests. It makes a new backup folder per invocation. No pawn-location writes are used.

The higher workload is **3840 x 3840 source**, **2688 x 2880 eye**, dynamic shadows and AO on,
foliage multiplier 1.0, AA on, motion blur off, **One Draw/sharePass=5 retained**. The source
pixel count is 2.64 times the original Balanced 2284 x 2448. The installed ASI remains the
same CE247578... Mako candidate described above; this is not another combined-build install.

At the cafeteria, a 90-second turn probe (`2026-11-08_18-16-02-cafeteria-high-turns`) loads
the replay before capture and settles for five seconds. With one second trimmed at either
boundary, 12,255 frames have interval p99 **7.812 ms**, maximum **79.539 ms**; seven exceed
11.111 ms, one exceeds 22.222 ms. No AV, GPU timestamp drop or draw-replay failure occurs.

The 79.539 ms interval at 35.712 s has a start-to-Present span of 78.448 ms wall but only
1.734 ms render-thread CPU time,
7.112 ms GPU work and 0.286 ms xrEndFrame. Game-thread tick begins jump from 35,697.711 ms
to 35,784.369 ms, while the earlier tick records only 2.749 ms engine-plus-host work. Most
of the pause is **between the instrumented ticks**. It cannot yet be assigned to the prior
camera-write hypothesis, GC, streaming, culling or the OS scheduler. A nearby distinct
engine tick takes 22.631 ms wall / 22.483 ms CPU. Collect context-switch/stack evidence and
time the outer game-loop work before choosing a fix for this larger pause.

The steady cafeteria log reports Umbra resolve about 0.35 ms and longest result read
0.03 ms. This supplies no evidence of a culling draw explosion, but aggregate query timing
does not rule out an uninstrumented per-frame stall. The null headset still runs about
139 application FPS despite advertising 90 Hz; physical headset pacing remains unvalidated.
Other development tools ran on the PC, so background interference is also unresolved.

The separate `high-route01` capture includes navigation debugging and replay replacement.
Loading a clip can itself create roughly 100 ms pauses. Those events are excluded from the
clean cafeteria measurement and are not evidence of ordinary player head-turn hitches.

The final single-command route/restore check (`run.ps1 -Preset High -SkipCapture`) completed
successfully in `scratch/performance-spikes-20261108/cafeteria-20261108-182704-461/`.
Its arrival image and waypoint receipt verify the cafeteria destination. The 90-second
measurement above was run separately. All 42 protected files were restored and freshly
hash-checked, with no game/SteamVR processes or pending requests remaining and the original
global Virtual Desktop runtime retained.

## Reusable cafeteria validation (2026-11-08, evening)

The private route is now promoted to [tools/qa-cafeteria](../tools/qa-cafeteria/README.md), with path discovery,
synthetic input generation, a read-only plan, exclusive run lock, checked process identity, original-save checks,
hash-verified backup/restoration, a batch runner and offline fixture tests. The original private runtime marker
is restricted to its exact ASI hash; ordinary builds require SteamVR already selected. No global runtime switch,
save fixture, binary or recorded game content is shipped. The source route still uses controller walking and
the normal door action; One Draw stays on.

Three independent High launch/walk/capture/restore runs used the same CE247578... binary, source 3840 x 3840,
eye 2688 x 2880, AA/shadows/AO enabled, blur off, One Draw/sharePass=5. Hardware: i9-12900K, RTX 4090,
NVIDIA 591.86, Windows 11 build 26300, High performance power plan. Captures were 90 seconds, with one second
trimmed at either boundary. No builds, analysis or additional screenshots/replays were run during the clean
measurement windows. The runner polls its own process and the settings-file timestamp at 500 ms intervals.

| Capture start (local) | Frames | Interval p99 ms | Maximum ms | Intervals >11.111 / >22.222 ms | Engine tick max ms | Host tick max ms |
|---|---:|---:|---:|---:|---:|---:|
| 18:47:01 | 12,264 | 7.7262 | 15.0782 | 3 / 0 | 19.4178 | 17.5310 |
| 18:54:01 | 12,253 | 7.8013 | 92.7362 | 11 / 1 | 18.6976 | 16.6859 |
| 19:00:55 | 12,271 | 7.7272 | 15.1629 | 1 / 0 | 18.8296 | 16.4921 |

All three reached the same cafeteria spot within 0.5 Unreal units, retained the same renderer settings, and had
zero AVs, GPU timestamp drops or draw-replay failures. Each restored and verified 42 protected files and ended
with no game/SteamVR processes or pending requests. The comparison validates comparable measurements, not
acceptable VR performance: one of three runs still contains a severe outlier. Use repeated runs and maxima/counts
as well as p99, and include game-thread tick time because buffered rendering can hide a simulation stall.
The explicit initial gate (`--max-p99-ms 11.111 --max-interval-ms 22.222 --max-tick-ms 22.222`) correctly fails
run 2's severe interval while keeping the batch marked comparable. It is a regression check for this scenario,
not physical-headset acceptance; smaller missed budgets still matter.

The second run's 92.736 ms interval at 51.790 s has 90.251 ms start-to-Present wall time, only 2.876 ms
render-thread CPU in that span, 7.092 ms GPU work and 0.429 ms xrEndFrame. A 90.769 ms interval lies between
the previous tick's engine-plus-host end and the next tick's beginning. Its nearest settings write is approximately
16 seconds away. This reproduces the earlier large-gap pattern during a quiet capture and separates it from
the camera diagnostic write. It is not established as a culling or Virtual Desktop submission failure.

The per-minute file-write lead also repeats: for example, run 1's writes at 7.925 and 67.927 s land inside host
ticks lasting 16.786 and 17.531 ms, with only 1.976 and 2.024 ms host CPU. The camera-diagnostic log immediately
follows each write. Separate engine ticks cost approximately 19 ms CPU. Stack evidence is needed to identify
that engine work; the 60-second pending-kill interval alone does not prove garbage collection.

**Timing blind spot:** `GameEngineTickHook` runs `game_thread::Drain()` before `perf::EngineTickBegin()`.
Therefore an unmeasured tick gap can include our queued ProcessEvent calls, as well as outer engine work,
scheduling or synchronization. The queue is bounded and logs no drops/faults in these captures, but those counts
do not measure call duration. Do not equate "outside the measured ticks" with "outside the mod."

Private baseline: `scratch/qa-cafeteria/batch-20261108-184155-647/`, including raw probes, source copies,
hardware receipt, per-run `investigation.json`, and `comparison.json`. The earlier failed ownership check was
a PowerShell JSON timestamp-conversion bug in the harness, corrected and covered by an offline round-trip check;
its interrupted launch was separately restored before this three-run batch. No production performance fix was
built or installed by this validation work.

### Two separate stack-sampled captures

A fourth launch repeated the same High route, then ran two 90-second probes with an external game-thread sampler.
Sampling is intrusive and these probes are excluded from the baseline/gate. Each 110-second sampling window
contained 10,237 samples. Recorded suspension cost (including the suspend/resume calls) was about 0.54/0.55 ms
median, 2.52 ms p99, and 6.71/4.96 ms maximum. Do not interpret their lower frame maxima as an improvement.

The strongest new result is **three sampled host spikes inside `WritePrivateProfileStringW`**, through
`BaseDllReadWriteIniFile` and `NtCreateFile`. They lasted 13.351, 12.144 and 12.702 ms wall, with 1.912, 1.844
and 1.908 ms host CPU. The sampler accounted for approximately 0.900, 0.844 and 0.839 ms in those spans.
Together with the repeated settings timestamps and the camera-diagnostic write call site, this directly supports
synchronous INI persistence as a contributor to recurring host hitches. A controlled candidate that removes those
periodic diagnostic writes is still needed to quantify the improvement; explicit player saves must remain intact.

The separate engine spike repeated at 19:09:16.987 and 19:11:16.996 local (two minutes apart), costing
20.061/21.014 ms wall and 18.740/19.377 ms engine CPU. Samples in both captures share the chain
`UWorld::Tick -> 0x414E04 -> 0x172820 -> 0x6F170` (executable unwind-range starts; some can be function
fragments). Two samples in the first spike land in the final function; one in the second lands there, while its
other sample reaches heap allocation beneath viewport drawing. Total measured suspension during those engine
spikes is 1.173/1.616 ms. The matching chain narrows the search, but does not yet establish GC, culling or streaming.
The optional Ghidra lookup encountered an existing project lock; it was left intact. No unnamed routine was patched.
Export-only sampler labels such as `SpiSupportDecl` and `GetDenuvoTimeTicketRequest` are nearest-symbol fallbacks,
not evidence that those named exports caused the sampled work.

Neither diagnostic capture reproduced the 80-93 ms between-tick pause. The clean baseline still fails the
severe-hitch gate. Next, measure `game_thread::Drain` before the existing tick timer, including individual queued
call duration, and capture outer-loop/scheduling waits if that duration stays small. Keep this instrumentation
buffered and dump after capture. That will distinguish a slow queued call from a stall outside the hook without
guessing from aggregate logs. For the smaller host hitch, test an in-memory camera diagnostic independently using
the same three-run workload. Retain One Draw and the same source/eye resolution for both comparisons.

Private diagnostic evidence: `scratch/qa-cafeteria/cafeteria-20261108-190311-968/`, with two `diagnostic-*`
subfolders, raw stacks, per-sample timestamps/cycles, `investigation.json` and `diagnostic-summary.json`.
Reprocess with `summarize-diagnostics.py`; the optional engine lookup is `inspect-engine-spike.ps1`.
This fourth session also restored and verified all 42 protected files. A fresh verification found no remaining
game/SteamVR processes, requests or changed files, and the original global Virtual Desktop runtime was retained.

## In-memory camera diagnostic candidate (2026-11-08, late evening)

Priority 1 in [the saved list](../docs/performance-priorities.md) is implemented. `ArmIk::LogCameraOffset`
publishes its existing five-second observation through HostServices v38 into a host-owned, mutex-protected
sample. No camera-diagnostic Win32 profile writes remain. The classic calibration menu consumes this sample
directly, rejects readings older than 15 seconds, and requires a post-recenter observation for trigger alignment.
Hot reload invalidates the mailbox. Old hosts omit publication safely rather than reverting to file writes.
Development deployment must update both host and mod; an old mod still contains the old writer.
Explicit camera/profile and arm-length saves retain their existing paths; their physical headset workflow
has not been exercised in these captures and remains on the headset checklist.

Freshness, unchanged-value refresh, invalid samples, unload, old/short host ABI compatibility and concurrent
publication are covered by `CameraDiagnosticTests`. LE1 host/mod, LE2 and LE3 built successfully, and the main
test executable passed. The private monolithic candidate also built and passed its main test suite.
The full suite exposed a stale signature-test assumption that every code pattern uniquely relocates. The test
now treats the blur guard's deliberately fixed-address patterns separately: all must match the original RVA,
and shifted copies must fail its strict address gate. No production guard behavior changed in that correction.
The initial CTest pass completed 21 of 22 checks; the installer-update check timed out using an old Setup
executable. After rebuilding the game/test/Setup targets, both installer network and update checks passed
(61 seconds together), as did QA fixture/recording checks and the SPDX audit. All 22 distinct CTest checks
have passed across the initial run and targeted retry. Final source-check transcript:
`scratch/camera-source-check-20261108-202551-710/`.

Three independent runs used the same High workload as the earlier baseline: source 3840 x 3840, eye
2688 x 2880, AA/shadows/AO on, blur off, One Draw/sharePass=5. Each walked from the original Normandy save
to the cafeteria, settled and captured 90 seconds with one second trimmed at either boundary. The arrival
snapshot confirms the cafeteria scene. No builds or extra snapshots/replays ran in the clean capture windows.

| Capture start (local) | Frames | Interval p99 ms | Maximum ms | Intervals >11.111 / >22.222 ms | Engine tick max ms | Host tick max ms | Settings writes |
|---|---:|---:|---:|---:|---:|---:|---:|
| 20:09:45 | 12,266 | 7.7700 | 17.3203 | 1 / 0 | 21.4346 | 7.5417 | 0 |
| 20:16:39 | 12,268 | 7.7840 | 15.1546 | 1 / 0 | 19.4919 | 7.5129 | 0 |
| 20:23:36 | 12,266 | 7.8004 | 14.5346 | 1 / 0 | 19.2030 | 7.0280 | 0 |

All three have valid published camera samples (sequence 42/43/43, age 1.125/2.094/2.094 seconds at arrival),
the host's in-memory-publication startup log, zero settings writes during capture, and zero AVs, GPU timestamp
drops or draw-replay failures. `check-camera-diagnostic.py` passes all three. The previous per-minute 16-18 ms
host stalls do not recur; observed host maxima are 7.03-7.54 ms. The removed synchronous persistence path is
directly verified, but this candidate comes from current combined development source containing other pending
changes. It is not a single-change rebuild of the earlier CE247578 baseline; broader timing improvements cannot
all be attributed to this patch.

Each remaining >11.111 ms frame interval overlaps a separate engine tick: respectively 21.435/20.351,
19.492/19.402 and 19.203/18.346 ms wall/CPU. Host work in those ticks is only 0.841/0.777/0.764 ms. The general
regression gate remains **failed**: run 1 totals 22.2758 ms engine-plus-host against 22.222 ms. The other two
total 20.2689 and 19.9666 ms. No 80-93 ms between-tick pause occurs in these three windows; its absence does
not close priority 2. Physical VDXR submission timing and culling remain unvalidated by this null-headset batch.

Private candidate: `scratch/camera-diagnostic-20261108/`, frozen source plus private process-only SteamVR shim,
binary SHA-256 `C7BA337EB4BC083FE26F50DD629F67970B7D637BEAF8CA24BAA52789CEEA2A55`.
Evidence: `validation-20261108-200437-603/batch/` under that directory, with per-run analysis/investigation,
`camera-diagnostic-check.json`, `comparison.json` and `regression-gate.json`. The public QA runner's original
binary allowlist is unchanged; only the frozen private runner was pinned to this candidate. This artifact must
not be released or left installed.

Each run restored and verified its 42 protected files. The outer runner then restored the original
CE247578... ASI. A fresh read-only verification at 20:26:42 found all 42 files matching, no game/SteamVR
processes, no requests or QA lock, and the original global Virtual Desktop OpenXR runtime. The reusable build,
source checks, candidate runner and restoration verifier are documented in [the QA README](../tools/qa-cafeteria/README.md).

## Whole-hook and queued-call trace (2026-11-08)

Priority 2 now has buffered instrumentation before the original `EngineTickBegin` boundary. The tick CSV
retains its original six columns and appends whole-hook wall/CPU time, queue-drain wall/CPU time and a queued
call count. A separate `-calls.csv` records each queued call's validation/dispatch duration, outcome and numeric
object/function identities. Those identities are process-local addresses, not resolved names. Timing storage
is reserved before capture; formatting and file writes occur afterward. Additional timestamp queries have
diagnostic cost, so a changed distribution alone is not evidence of a gameplay optimization.

The probe metadata identifies this as instrumentation version 2 and reports dropped tick/call records. The
analyzers use the full hook for overlap and between-tick gaps, preserve the old engine/host metrics, and label
legacy gaps as outside **engine/host only**. Batch comparisons reject dropped records and different trace
versions. An old capture cannot establish that a stall was outside our hook.

`check-tick-trace.py` checks that every queued call belongs to a recorded tick and lies within that tick's drain,
that counts and duration boundaries agree, and that the trace did not overflow. The unit fixture places a
90 ms call before the old timer: the old view shows a 91 ms gap; the full-hook view correctly includes the call
and leaves a 1 ms gap. A malformed trace with the call moved into the engine portion is rejected.
These checks validate attribution, not a fix for the original pause. LE1/LE2/LE3 builds, the main C++ suite,
five Python probe checks and the QA offline checks passed. Build/test transcript:
`scratch/tick-trace-check-20261108-210401-074/`. Two sandboxed development-build attempts stalled before
compilation and were canceled; the successful check ran with normal native-process access.

The private monolithic candidate also built and passed its unit suite. Its six timing-related C++ source files
match the working source; it uses the same process-only SteamVR shim as the earlier camera candidate.
Private artifact: `scratch/tick-trace-20261108/`, SHA-256
`6F2F4E4FA5EBC8C06E527C20EAFA95EC5F4F23301247C6BA883B86F2EBAADE17`.
`run-local.ps1` records the exact one-run validation command with `-RequireTickTrace`.

The native attempt at 21:14 stopped before High settings, launch, traversal or capture because a game launcher
appeared during preparation. All 42 protected files, original CE247578... ASI and global VDXR were restored and
verified at 21:14:58; no processes, requests or QA lock remained at that verification. A separate active Mako
cockpit test then owned the game session. It was left untouched, and no second native attempt was made.
Receipt: `scratch/tick-trace-20261108/validation-20261108-211425-597/`. This is a recovered preflight refusal,
not a timing result. Native trace validation and the cause of the 80-93 ms pause remain open. Recheck the
installed binary hash and session ownership before retrying; another task may legitimately deploy a new build.

The reusable build/check commands and protected candidate runner are in
[the cafeteria QA README](../tools/qa-cafeteria/README.md). One Draw and the High workload remain unchanged.
Separate [hub scenarios](../tools/qa-hubs/README.md) preserve route-discovery work, now
[paused at the user's request](../tools/qa-hubs/PAUSED.md). All seven remain drafts and are excluded from
regression runs. Noveria loaded at its authored embedded-Normandy start; that successful map load did not yet
acquire Port Hanshan. Its normal airlock path remains the next supported discovery route, not arbitrary save
flag injection. No wider-map measurements are included in the CPU baseline.

## API references

Microsoft documents that query results can be unavailable (`S_FALSE`), and that continually polling
with DONOTFLUSH can fail to make progress if needed commands remain buffered. A nonblocking query
optimization must account for result readiness and submission, not simply remove every wait:
[GetData](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata),
[D3D11_ASYNC_GETDATA_FLAG](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_async_getdata_flag).
The [Khronos frame-submission guide](https://github.com/KhronosGroup/OpenXR-Guide/blob/main/chapters/frame_submission.md)
describes the wait/begin/end state sequencing; a CPU API timing alone does not partition underlying
GPU and runtime work. These references explain constraints, not the cause of this game's outliers.
