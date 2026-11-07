<!-- SPDX-License-Identifier: MIT -->
# Codex adversarial review fixes, 2026-11-07

Re-traced all 11 findings against `galaxy-labels` at `e134beb` before editing, including the callers and refusal paths.
**11 confirmed, 0 narrower, 0 false positives.** Fixed in this worktree; no commit, deployment or game session.
The findings' scopes still apply: floor lock and conversation walking are optional; the two tool findings affect QA.

## Findings and fixes

| id | verdict and trace | fix and files | check after building |
|---|---|---|---|
| game#1 (high) | Confirmed: both inactive and pawn-change paths restore through a cached pointer; `FieldRef::In` does not validate lifetime. | `src/game/Sprint.cpp`: remember the world; require the same known world, `runtime::IsAlive` and `player::IsBioPawn` before accessing the cached field. A changed world drops the old speed baseline too. Existing restore writes remain journalled. | Sprint across a load/pawn replacement; stop sprinting and leave VR on the same pawn. Normal speed returns without a stale-pawn write. |
| core#1 (medium) | Confirmed: `held` was recorded but full speed/travel was required again; the supplied samples miss their cast at .14 s. | `src/core/vr/PowerWheel.cpp`: a qualified held push can fire once allowed. The existing 40% slowdown miss runs first; firing still disarms and observes cooldown. | Push from the chest, extending while decelerating; one cast at reach. Stop short: one miss, no late cast. |
| context#2 (medium) | Confirmed: render-thread cycle time excludes GPU overload, so 3 ms CPU/over 13.9 ms GPU at 72 Hz could spend every attempt. | `src/core/vr/QuestPacing.*`, `src/plugin/QuestPacing.cpp`, `src/plugin/PerfProbe.*`: require completed, non-disjoint GPU evidence at most 250 ms old and below 80% of the native period. Missing/over-budget evidence suspends the window without spending attempts. Reuse completed DONOTFLUSH probe reads. | With a probe active, GPU-heavy 36 fps must not recover/cycle refresh; a later genuine lock in the same world retains its budget. No probe evidence: no automatic attempt. |
| context#1 (medium) | Confirmed with `floorLock=1`: `Translate` repeatedly called timestamp-renewing `Publish` after XR samples stopped. | `src/plugin/FloorLock.*`, `src/core/vr/HostView.*`, `src/plugin/HalcyonOverlays.cpp`: stamp at XR queue time, preserve that stamp on correction, expire the queued view and clear pending/publication on session stop. Queue generations reject publication racing stop. | Stop XR while the game renders flat: tracking-dependent overrides end, even with floor lock enabled. Restart XR and verify normal tracking/floor height. |
| plugin#1 (medium) | Confirmed: the LL hook swallows keyboard events before `GetAsyncKeyState` sees them; its existing rebind handoff already solves this. | `src/plugin/MainMenuSettings.*`, `src/halcyon/le1/vr_menu.*`: small `[MEVR]` capture lifecycle over that same atomic handoff. Timeout, B/cancel, Clear and menu closure cancel capture; the menu key remains able to close our menu. | Voice > Set key saves an eligible key. Test 10 s timeout, B, closing by menu key/Resume, reopening, and classic rebinds. |
| plugin#2 (medium) | Confirmed: `Apply` advanced `gListening` before availability/Ready/success; the restart resync alone could not retry a held input. | `src/plugin/Voice.cpp`: only accept the new listen state after Ready and a successful API call; otherwise the desired input is retried on Present. | Restart/change model or mic, hold talk during loading, keep holding through Ready: listening begins without a new press. Release during loading must not start later. |
| seams#1 (medium) | Confirmed: a start in phase 3 was consumed as BUSY, followed by an infinite wait after coalesced event signals. | `src/voice/Voice.cpp`: phase 3 never waits for another event. Retain the latest request through finalization and start failure; finalize first, then apply it. Shutdown clears old requests. | Release and immediately squeeze repeatedly, including during final transcription. Each listen finalizes once; the next starts, audio unducks, and subsequent orders work. |
| game#2 (medium) | Confirmed in the optional walker: 35 units at 140 units/s over .25 s crossed the fixed 30-unit threshold; the elapsed value was also capped at .1 s. | `src/core/vr/ConversationWorld.*`, `src/game/ConversationWorld.cpp`: test distance outside the segment the active walk could cover over actual elapsed time. Keep the existing threshold beyond that segment and retain the slide/turn step cap. | With `worldWalk=1/2`, hitch during a walk: no spurious cancel/snap-back. An excessive, backward or sideways placement still restarts settling; collision-shortened motion is allowed. |
| plugin#3 (low) | Confirmed: guide reads `PowerWheel` from a local section map that never loaded it. | `src/plugin/MainMenuSettings.cpp`: add `PowerWheel` to `Settings::Read`'s section list. | Disable `on`, `lock`, `selfGestures` individually and check Controller guide, including the X instructions. |
| install#1 (low) | Confirmed: exact `[Debug]` match missed trailing whitespace; the global requests search also matched unrelated sections. | `tools/power-arena.ps1`: section-aware `Enable-DebugRequests` updates the first key in the first matching section; tolerant whitespace/casing, inserts only when missing, idempotent. | On an INI fixture, `[Debug] ` with `requests=0` becomes effective `1` without a new section/key. Then check an arena launch on the monolithic build. |
| install#2 (low) | Confirmed: replay substitutes inputs after the live menu chord is evaluated, so its synthetic open step cannot open the menu. | `tools/make-menu-recording.py`: remove the step and stick-click segments; example requires the real menu key before replay. First point now starts at .5 s; later steps lose the old 1.2 s open interval. | Open the menu with its key, replay point/click/scroll with `-Inputs`; check pointer hit and screenshot timing. `open` should be rejected as unknown. |

## Voice state machine

The host retains desired input until Ready and API acceptance. The DLL worker retains an accepted request until applied:
idle (0) -> listening (1) -> tail (2) -> draining (3) -> idle (0). Re-squeeze during tail resumes 1; during 3 it waits for
the old transcript/end marker. Phase 3 is runnable without a wake, failed starts retry at the existing interval, and a
newer off request cancels a pending start. Recognition and ProcessEvent thread ownership are unchanged.

## Validation

- Direct MSVC `/W4 /WX /permissive-` build/run of `PowerWheelTests`, `QuestPacingTests`, `ConversationWorldTests` and
  `FloorLockTests` with their production core sources: **passed**. Added the supplied push samples and simultaneous miss,
  GPU-heavy/missing/nonfinite/stale evidence plus retained budget, hitch/direction/overshoot walking, and repeated stale
  view publication/reset tests. `CMakeLists.txt` now links `HostView.cpp` into the offline tests.
- Direct build/run of all **10 Quest pacing host scenarios**: passed; the idle probe stub supplies no GPU evidence.
- Temporary harness in ignored `scratch/review-2026-11-07/`, executing the actual extracted worker/host functions with
  stub recognition: **passed** readiness/refusal/restart, coalesced notifications, wake-free drain, cancellation and failed
  start retry. The original worker from `e134beb` **fails** that deterministic harness. This is not a microphone test.
- Changed game/plugin/voice translation units passed direct compiler syntax checks with the host's `/W4 /WX` settings;
  Halcyon's menu passed with its existing C++17 `/W3` settings. Voice used the existing SDK headers from the main checkout.
- PowerShell AST parse and six section/key/idempotence fixtures: passed. `git diff --check`: passed.
- Standard CMake test build stopped at MSBuild SDK discovery: sandbox denied the user-local Microsoft SDKs directory.
  Python execution was also denied, so the recording generator and Python repository checks were not run.

## Remaining checks and limits

All full builds, full ctest, deployment and the table's game/headset checks remain with the owner. Source/syntax checks
and stub recognition do not establish runtime acceptance. No new engine writes or off-thread ProcessEvent calls added.
GPU evidence currently exists only while the perf probe runs, and expires by source frame time. The measured span covers
scene/overlays/eye copies before `xrEndFrame`, including idle gaps; it excludes subsequent runtime waits and is conservative.
Without that evidence automatic recovery stays off; explicit debug force still bypasses eligibility. No new blocking
GPU query, clock synchronization or capture is initiated by the detector. The optional walker may still reject detours
more than the existing teleport tolerance away from its expected path.
