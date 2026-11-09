<!-- SPDX-License-Identifier: MIT -->
# Gentler culling and startup handoffs

2026-11-09. Follow-up to [the preset matrix](66-citadel-performance-matrix.md) and
[the title investigation](67-startup-performance.md). The user released the shared native session;
the startup comparison below is complete. Physical-headset pacing and visual acceptance remain separate.

## Startup result

Two baseline and two candidate 120-second captures cover splash, title, Resume and C-Sec gameplay.
Host binary, runtime, hardware/power plan, settings, resolution and archived save match. Only
`SystemSettings.cpp` and its tests differ in candidate source. Both variants include the loading read
guards below. Every accepted capture had zero access violations, zero GPU timing drops, healthy One Draw
and blur protection, and verified profile/configuration restoration.
The actual source was 2284 x 2448 and the actual eye/recommended-eye size was **2796 x 2996** in all
four captures. SteamVR global scaling enlarged the requested null-driver size; this is a controlled
comparison at the observed size, not a 2284 x 2448 eye-size acceptance test.

| Measurement | Baseline repeats | Candidate repeats |
|---|---|---|
| Timed graphics resets per launch | 4 / 4 | **0 / 0** |
| Title host maximum | 144.77 / 156.96 ms | **1.92 / 2.60 ms** |
| Gameplay settling host maximum | 155.99 / 151.92 ms | **6.40 / 6.30 ms** |
| Whole-hook maximum, including loading | 1762.04 / 1452.79 ms | 1450.45 / 1791.30 ms |
| Tracked render-thread handoffs | 1 / 1 | 1 / 1 |

The candidate consumes the 3/15-second recovery deadlines without blanket graphics commands while the
native motion-blur guard is healthy. Initial application, world changes, actual player edits and lost-guard
retries remain; legacy/unhealthy guards retain both settling attempts. Each candidate applied settings
twice, compared with six baseline applications. AA and One Draw stayed enabled.

This removes the repeated reset stalls. It does **not** remove the large engine loading tick, initial
render initialization, or initial/world settings applications. The whole-hook 22.222 ms limit still fails;
the loading maxima do not establish an improvement there. The engine tick during Resume reaches roughly
1.45 seconds or more. The first candidate also has an 854.54 ms render CPU interval during splash.
Those costs need their own attribution/presentation work.

### Next startup target: repeated hook activation

Saved-data correlation (`inspect-startup-stalls.py`) places twelve draw-hook variant installation messages
inside the 854.54 ms render CPU interval. Their completion messages are approximately 71 ms apart.
`DrawReplay::VariantSweep` calls `hook::Install` separately for each new entry. Both our MinHook backend
and our SPI loader implementation enable each hook individually; MinHook's enable path freezes/unfreezes
threads per operation. This is a strong next candidate for the splash stall, not a measured batching win.

Investigate batch activation while preserving loader ownership, hook chaining and publication of every
trampoline before activation. SPI v3 exposes only individual installation; do not bypass another loader's
hook registry or assume it offers queued activation. Keep this separate from the engine's roughly
1.45-second Resume tick. No batching or engine-thread migration is implemented here.

## Render-thread continuity

The probe follows serialized Present-thread handoffs. Each boundary row retains the full wall interval
and identifies both threads, but omits CPU busy values because their cycle counters cannot be subtracted.
A nonblocking ownership check detects overlapping Presents without serializing the engine. Different
render contexts and overlaps invalidate capture. The probe retains its D3D context and recreates query
resources for a changed device between captures.

All four accepted transitions recorded one handoff, one CPU boundary row, and no overlap/context change.
This fixes measurement continuity across loading. It does not parallelize the renderer or move engine
and UnrealScript work onto a worker thread. Large loading gaps remain visible in the results.

## Loading read faults

The first full transition, `20261109-082304-256`, completed 120 seconds across a handoff but failed admission
with 177 handled access violations. All fell into two Resume render intervals, 19.877-20.523 seconds into
the probe. Logged sites resolved against the matching PDB to a controller-scan FOV read and a null HUD-name
read. Original symbols/source remain private. This rejected capture is diagnostic evidence, not an accepted
performance baseline.

The controller scan read camera/FOV fields on every object before checking its class. Its readable-span
check ended at camera offset 0x6B0, before FOV at 0x758. The fix checks class first, then validates the full
controller-field span. HUD name/class/outer helpers return their existing null result directly for null
input. SEH remains for world-churn races. Upstream was compared before these narrow changes. All four
accepted A/B transitions have zero access violations.

## Culling comparison

The batch holds Quality settings, source and eyes at 2284 x 2448, AA and One Draw constant. Only the extra
small-object radius changes: **8, 4, 0, 0, 4, 8 pixels**. Zero removes that filter; normal visibility culling
remains. The workload is the archived C-Sec save with stationary 120-degree head turns, not the unbound
manual Citadel walk. Live settings interventions invalidate a clean run.

All six fixed-scale captures passed measurement, build/settings/fixture consistency and restoration checks.
There were no access violations, GPU timing drops, replay failures or settings writes during capture.
Actual source and eyes were 2284 x 2448 throughout. AA, One Draw and blur protection remained active.

| Extra cutoff | Render CPU median, each repeat | Render CPU p99, each repeat | Median draw calls across repeats |
|---|---|---|---|
| 8 px | 5.75 / 6.23 ms | 7.73 / 10.27 ms | 2865.5 |
| 4 px | 5.85 / 5.96 ms | 8.08 / 8.03 ms | 2920.5 |
| 0 px | 5.80 / 5.84 ms | 8.02 / 7.86 ms | 2990.0 |

Gentler culling has comparable typical CPU cost in this fixed C-Sec view. Zero draws about 4.3% more calls
than 8 pixels. The last 8-pixel run is slower than its first repeat; that variation exceeds the differences
between the threshold averages. This does not establish that disabling the extra filter is faster.
Four pixels is a reasonable gentler test setting, and zero is also worth physical visual testing here.
No shipped culling default changed; this stationary scene does not cover the Normandy or traversal.

All runs remain limited to about 31 fps by runtime waiting: median xrWaitFrame wall time is about 25.4-25.5 ms.
Frame-interval p99 is about 33 ms, so none passes the nominal 90 Hz frame budget. Whole-hook maxima range
23.76-33.38 ms, independently failing the 22.222 ms hitch limit. Lowering the cutoff did not eliminate these
spikes. CPU work and draw counts must accompany frame intervals; equal capped rates do not establish
equal uncapped throughput or physical headset comfort. The existing periodic-GC diagnosis remains separate;
these clean captures did not sample stacks to attribute every new maximum.

The first culling attempt after startup was rejected because actual eyes were 2796 x 2996, not the planned
2284 x 2448. Per-app 100% did not override SteamVR global automatic scaling. The fake headset now has an
explicit `FixedRenderScale` option; the hub harness temporarily pins global manual 100% and per-app 100%
inside the existing complete backup. An offline fixture verifies exact restoration. The replacement batch
is `scratch/qa-hubs/culling-fixed-scale-20261109-0856`; never pool the rejected run.
The completed session IDs, in 8/4/0/0/4/8 order, are `20261109-085538-904`, `20261109-085902-197`,
`20261109-090225-256`, `20261109-090548-715`, `20261109-090906-177`, `20261109-091226-340`.
The wrapper restored the original monolithic host and absence of its separate module DLL after the batch.

Earlier incomplete evidence is separate: `20261109-062251-810` completed at 8 pixels; the subsequent
4-pixel run `20261109-062619-603` contained a live wait intervention and is excluded. Moving the wait did
not remove the cadence. Both an old uncapped run and the current startup run reached XR state 4 without
state 5, so this observation alone does not explain their pacing difference.

## Build provenance and reproduction

Conversation/squad changes and a monolithic installed host arrived during the session hold. We froze
current combined source privately rather than rolling the shared checkout back. The protected wrapper
restores file absence as well as contents, so the original monolithic installation returns without an
extra module DLL. Synthetic restoration checks cover both layouts, including failed batches.

An initial candidate attempt reused the baseline object: copying the saved source preserved a timestamp
older than that object. Its module hash was identical and both captures still performed four resets.
That attempt is rejected as A/B evidence. The builder now refreshes reviewed changed-source timestamps
and refuses identical module output after a production-source change. Comparison independently rejects
identical variants and reused captures. The corrected build compiled both changed sources, produced a
different module and passed all four focused CTest suites. Logs, symbols, saves and binaries remain private.

Private evidence:

- Frozen source: `scratch/startup-paired-20261109-0817`; guarded baseline: `baseline-probe-safe` beneath it.
- Baseline captures: `20261109-083227-892`, `20261109-083522-249`.
- Corrected candidates: `20261109-084501-560`, `20261109-084755-781`.
- Accepted startup comparison and culling: `scratch/qa-hubs/startup-culling-corrected-20261109-0845`.
- Rejected identical-module attempt: `scratch/qa-hubs/startup-culling-guarded-20261109-0832`.
  Its valid baseline captures are retained as existing evidence, not counted as new repetitions.

Use the [QA scripts and ownership/restoration procedure](../tools/qa-hubs/README.md#culling-and-startup-comparisons).
For another comparison with the current harness, create a fresh paired stage. Do not reuse this report's
historical startup captures after changing the render-scale policy: their actual eyes were larger.
The retained old settings implementation/tests supply the baseline behavior; current shared source and
common fixes are copied to both variants.

```powershell
pwsh -NoProfile -File tools/qa-hubs/prepare-settling-baseline.ps1 `
  -StageDirectory scratch/<new-paired-stage> `
  -BaselineSettings scratch/startup-candidate-20261109/SystemSettings.baseline.cpp `
  -BaselineTests scratch/startup-candidate-20261109/SystemSettingsTests.baseline.cpp
pwsh -NoProfile -File tools/qa-hubs/run-frozen-startup-comparison.ps1 `
  -StageDirectory scratch/<new-paired-stage> `
  -PackageDirectory scratch/qa-hubs/citadel-autoload-save96-check `
  -OutputDirectory scratch/qa-hubs/<new-comparison> -PythonExe python
```

Use a working Python 3 executable and exclusive native-session ownership. The runner builds in the private
source tree, captures two transitions, validates the comparison and reset removal, then captures culling.
No elapsed coordination window grants ownership. Validity, performance thresholds, headset acceptance and
production promotion remain separate checks.

## Final checks

LE1 built and all four focused CTest suites passed. LE2 and LE3 then compiled in the private build after
native captures finished. Hub offline tests, comparison admission, successful/failed deployment recovery,
present/absent module restoration, fixed render-scale restoration and SPDX checks passed. These build
checks are not native LE2/LE3 acceptance. The final post-wrapper check verified all 40 protected files,
original monolithic host, absent separate module, original runtime and cleared game/SteamVR ownership.
The automated batch restored the original installation and did not change a culling default.

Final validation receipt: `scratch/qa-hubs/performance-checkpoint-20261109-0918/result.json`.
The last session's `verified-after-build-restoration.json` checks the original installation after the
outer wrapper; the ordinary inner-session verifier expects the test binaries still to be installed.

## Manual headset handoff

At the user's request on November 9, the exact validated candidate was installed for physical-headset QA
at 09:49 local time. Host SHA-256 starts `522F6B0867CD`, module `B2EE1DFF913D`; both match the retained
candidate manifest. Settings and the active SteamVR runtime were preserved, and the null headset remains
disabled. The larger splash-hook and engine-loading stalls are still present; hook batching is not included.
This is a manual test deployment, not a public release.

Install/rollback receipt: `scratch/qa-hubs/manual-headset-performance-20261109-094949`.
The previous monolithic host and original module absence are backed up there. The reusable command is
`tools/qa-hubs/deploy-validated-build.ps1`; the [QA README](../tools/qa-hubs/README.md#manual-headset-candidate-installation)
documents installation and restoration.

The user subsequently reported that this candidate "worked beautifully" and approved publishing the
committed changes to `main`. That is qualitative headset acceptance of this test build, not a new timing
capture. An attempted Citadel start had left Shepard unable to leave the Normandy; the user continued
through normal story progression before reporting success. Its cause was not established, so neither a
save-state repair nor a Citadel loading fix is claimed here. The remaining splash/loading work above stays open.
