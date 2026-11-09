<!-- SPDX-License-Identifier: MIT -->
# Gentler culling and startup handoffs

2026-11-09. Follow-up to [the preset matrix](66-citadel-performance-matrix.md) and
[the title investigation](67-startup-performance.md). Native validation is awaiting the shared session;
do not interpret implementation or unit tests as headset acceptance.

## Changes under test

The performance probe now follows serialized Present-thread handoffs. Each boundary row retains
the full wall interval and identifies both threads, but omits CPU busy values because their cycle
counters cannot be subtracted. A nonblocking ownership check detects overlapping Presents without
serializing the engine. Different render contexts and overlapping Presents invalidate the capture.
The probe retains its D3D context and recreates query resources for a changed device between captures.
This repairs measurement continuity; it does not parallelize the game's renderer or eliminate loading.

SystemSettings now consumes the 3/15-second recovery deadlines without issuing blanket graphics
commands when the native motion-blur guard is healthy. Initial application, world changes, actual
player edits and lost-guard retries remain. Legacy/unhealthy guards retain both settling attempts.
The measured target is the approximately 153 ms host stall in the earlier title captures.

## Controlled comparisons

The culling batch holds the Quality bundle, source and eyes at 2284 x 2448, AA and One Draw constant.
Only the additional small-object radius changes: 8, 4, 0, 0, 4, 8 pixels. Zero removes that extra filter;
it leaves normal visibility culling active. Runs use the archived C-Sec save and stationary head turns,
not the unbound manual Citadel walk. Live settings changes invalidate a clean comparison.

The first 8-pixel run completed and restored all 40 protected files. Its frame median/p99 were
31.94/33.08 ms, render CPU median/p99 5.81/7.73 ms, and engine tick median/p99 3.35/5.00 ms.
It was dominated by runtime waiting, unlike the earlier uncapped preset matrix. A subsequent 4-pixel
run included a late-wait intervention and was excluded. Moving the wait did not remove the approximately
31 fps cadence. The incomplete batch cannot establish equivalence between culling thresholds.

The startup harness has a 120-second `Transition` mode spanning initial launch, title, Resume and
gameplay. QPC-aligned launch events and explicit handoff rows retain the transition gaps. Two baseline
and two candidate captures are planned with the same host binary; the module source delta is restricted
to SystemSettings and its regression tests. No performance improvement is claimed until those captures
complete. GC, physical-runtime submission stalls and the unexplained wait cadence remain separate issues.

## Reproduction and evidence

Use the [QA scripts and ownership/restoration procedure](../tools/qa-hubs/README.md#culling-and-startup-comparisons).
Builds and protected temporary deployment are scripted; raw game data stays private.

- Baseline pair: `scratch/startup-candidate-20261109/baseline-pair`.
- Initial culling batch: `scratch/qa-hubs/culling-20261109` (incomplete; second capture excluded).
- Culling session IDs: `20261109-062251-810`, `20261109-062619-603`.
- Probe build and four focused CTest suites passed. Synthetic workload isolation/restoration,
  capture admission and session failure-recovery checks passed.
- The first full-transition launch was refused before deployment because a game/SteamVR session
  had opened outside the runner. The user confirmed another agent was measuring for approximately
  30 minutes. Native testing and builds are on hold until that session is released; elapsed time alone
  does not release it. The settling-change candidate has not yet been compiled or run.

No tested culling default or startup candidate has been promoted to the installed game.

After release of the shared session, the complete retained sequence is:

```powershell
pwsh -NoProfile -File tools/qa-hubs/resume-startup-comparison.ps1 `
  -BaselineDirectory scratch/startup-candidate-20261109/baseline-pair `
  -PackageDirectory scratch/qa-hubs/citadel-autoload-save96-check `
  -OutputDirectory scratch/qa-hubs/startup-culling-resume -PythonExe python
```

Use a fresh output directory and a working Python 3 executable. The candidate builder checks that
only SystemSettings and its tests differ from the retained baseline and reuses the identical host ASI.
If another agent changed compiled sources in the meantime, it refuses the comparison; establish a
new paired baseline instead of discarding their edits. Native capture validity, performance thresholds,
physical-headset validation and promotion remain distinct checks.
