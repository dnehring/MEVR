---
type: Gotcha
title: A replay substituted the head but not the floor sample, so the fake headset could never crouch, lean or reach
description: The test loop's replay puts the recorded head where Halcyon locates its views, while the host's floor sample (the eyes over STAGE, read by posture, floor lock, body scale, crouchBody, the holsters and VR cover) kept reading the null driver's still head at 1.7526 m; every recorded crouch stayed standing for them, so the owner found crouch bugs in the headset that a fake-headset run should have caught
tags: [test-loop, replay, floor, posture, floor-lock, fake-headset, host]
status: stable
timestamp: 2026-11-19T12:00:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-19T12:00:00Z }
bitten: [2026-11-07, 2026-11-16, 2026-11-18, 2026-11-19]
---

# Symptom

| wording | where |
|---|---|
| "Current replay cannot validate floor lock: recordings lack the corresponding STAGE/LOCAL_FLOOR sample and replay leaves that measurement live" | research/30-metric-stereo.md and research/58-body-scale.md section 6 "Replay limit", 2026-11-07 |
| `floor lock: ... correction +102.261, final eye 1.7526 m` while the replay's head was 0.9 m down: "a replayed crouch no longer lowers the eyes" | research/23-cover.md section 7.5, ME2, 2026-11-16 (VR cover's reach had to be widened for the run) |
| no `posture: physical crouch` for a replayed 0.45 m squat; scripts headed "only conclusive if the replayed head reaches the floor sample" | tools/le2-halcyon-run/posture-walk.ps1, crouch-anchor.ps1, 2026-11-18 |
| eyes-in-body.ps1 had to set `[Body] floorLock = 0` "the floor lock reads the live headset's height, which a replay doesn't move" | tools/eyes-in-body.ps1, LE1, 2026-11-18 |
| the owner, after a headset session: why did I have to find the physical crouch, the reach that crouched Shepard, the "magnetic" pull at cover and the eyes in the body myself? | 2026-11-19 |

# Cause

Two readers of the head, fed differently. The replay (`plugin/TestLoop`) substitutes the recorded eye views in the wrapped
`xrLocateViews` and the controllers at `OnFrameBegun`, so everything drawn follows the recording. The floor sample
(`plugin/XrHost` `MeasureFloor`: `core/xr/Floor`, the VIEW space located in STAGE or LOCAL_FLOOR, `HostServices::floor`) is
a separate `xrLocateSpace` against the runtime, never wrapped: on SteamVR's null driver the head stands still, so the eyes
stayed 1.7526 m over the floor whatever the replay did. Everything that judges the player's height reads that sample: the
physical crouch (`game/Posture`), the floor lock (the eye's height is the sample's, the replayed head's own height is
absorbed), body scale's standing reference, the torso's crouch (`crouchBody`), the holsters' slot heights, VR cover's eye
line. The recordings themselves held no floor sample, so even a replay of the owner's own headset session could not bring their
height back.

# Check

- The log at a replay's first frames since 2026-11-19: `test loop: the replay drives the floor sample too: the recording's own,
  N m over the floor` (a version 2 recording) or `... the live one moved by the replayed head's height over the live head's
  (-0.450 m now: 1.303 m)`; nothing like it means an older host.
- `floor lock: raw camera Z ..., STAGE eye N m, ..., final eye N m` during a replayed squat: the eye goes down with the head.
- `python tools/mevrrec.py <file>`: `floor: eyes A .. B m over the floor (STAGE)` for a recording with floor samples, `no floor
  samples (version 1 ...)` for one without.

# Fix in place

- While a replay runs, `MeasureFloor` publishes the replay's sample (`MeasureReplayedFloor`, out of line: without a replay
  `MeasureFloor` is its old body behind one `testloop::Replaying()` check, shown by the compiled code): the recording's own
  floor sample where it has one, else the live one moved by the replayed head's height in the app space over the live head's
  (`vr::recording::ReplayedFloor`, unit-tested in `tests/ReplayFloorTests.cpp`; `core/xr` `FloorSpace::HeadHeightIn`).
- Recordings are version 2 (260-byte frames): each frame carries the host's floor sample (`Recording::floors`,
  `SampleFloor`); version 1 files still play. `tools/make-walk-recording.py --stand-eye M` writes synthetic ones at a chosen
  standing height (the posture scripts pass the owner's calibrated `standEyeM`, `tools/owner-calibration.ps1`).
- When a new reader of the head appears (a third space, a second runtime query), it must take the replay's value too, or the
  same split comes back: grep for `xrLocateSpace` outside `plugin/TestLoop`'s wrappers.

# Citations

- research/le2-vrik.md "The replay STAGE-eye gap" (closed 2026-11-19, with the in-game measurements); research/58-body-scale.md
  section 6; research/23-cover.md section 7.5.
- `src/plugin/XrHost.cpp` (`MeasureFloor`, `MeasureReplayedFloor`), `src/plugin/TestLoop.cpp` (`ReplayFloor`, `NoteRecordedFloor`),
  `src/core/vr/PoseRecording.hpp`, `src/core/xr/Floor.hpp`, `tools/make-walk-recording.py`, `tools/owner-calibration.ps1`.
