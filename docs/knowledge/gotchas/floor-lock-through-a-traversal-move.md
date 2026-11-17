---
type: Gotcha
title: The floor lock follows the pawn's steps through a root-motion move, and the hands leave the controllers
description: The floor lock solves the eye from the pawn's cylinder floor every frame; a native traversal move (ME2's vault) steps the pawn's place 0.8-0.9 m at a time while the game's camera rides smoothly on its bone, so the locked eye jumps with the pawn, ends up a metre from Shepard's animated head, and arm IK takes the view for third person
tags: [floor-lock, vrik, traversal, me2, host]
status: stable
timestamp: 2026-11-16T17:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-16T14:00:00Z }
bitten: [2026-11-16]
---

# Symptom

| wording | where |
|---|---|
| `vault.ps1 -Runs 3 -Mode tap`: 102 of 135, 107 of 142, 99 of 133 ticks within 0.15 m, mean 0.33 m, max 1.49 m (it had been every tick, max 0.08 m) | ME2, Freedom's Progress, Save 4, on main b8fbb98c (lane C3, 2026-11-16) |
| `arm IK: body anchor: third person (tracking origin at Shepard's animated head), view 104 units from Shepard's animated head bone` in the middle of a vault, first person again 0.4 s later | the same run's `LE2MEVR.log` |
| `arm IK: a step between the goals and the write: ... rule camera (the view isn't on it): the goals as published` | the same: arm IK's root-motion rule (research/le2-vrik.md "Arm IK across root-motion steps") stands down because the view no longer hangs on the camera's bone |

# Cause

`core/vr/FloorLock` puts the eye at `pawn.Location.z - CollisionHeight + unitsPerMeter * the headset's eye height`, solved
again every frame (`plugin/FloorLock` `Translate`). ME2's mantle (`BioCustomAction_CoverMantle`) composes the body and the
`Camera` bone where they will be once the actor has stepped, then steps the actor: the pawn's place moves in a few steps of up
to 0.9 m while the game's camera travels smoothly (lane E measured 0.054 m a frame at most). The floor-locked eye therefore
followed the pawn's steps, not the camera: up to a metre from Shepard's animated head for about 35 ticks. `game/HandTargets`
then left the first-person frame (the view past 75 units from the animated head, gotchas/arm-ik-third-person-anchor) and the
arms were solved in the third-person frame.

It appeared only once the floor lock ran on ME2 (the LE2 host publishes a floor sample since 2026-11-15; before that the log
said `floor lock: no fresh valid STAGE/LOCAL_FLOOR sample; using the old camera behaviour` and the vault passed).

# Check

- The log during a vault: `body anchor: third person` between `traversal: vault began` and `traversal: vault over`.
- The A/B that named it: `[Body] floorLock = 0` in `ASI\LE2MEVR.ini` (the host re-reads it), `vault.ps1 -Runs 2 -Mode tap`:
  136 of 136 and 137 of 137 ticks within 0.15 m, max 0.08 m.
- With the fix: `floor lock: a traversal move began: the pawn's floor held N units below the game's camera ...`, `floor lock:
  the traversal move ended after ... s (N frames): the held eye's largest step ... units a frame ...; the game's camera is N
  units off its place over the pawn before the move: held until it settles` and `floor lock: the traversal hold released N s
  after the move's end (N frames, the camera settled): ... the release N units` (a release of more than 2 units, or `eased after
  the wait`, says the camera didn't come back).

# Fix in place

`vr::floor_lock::MoveHold` (core, unit-tested in `tests/FloorLockTests.cpp`): while the mod's floor sample says a traversal
move plays (`PawnSample::traversalMove` = `game/Traversal`'s `Moving()`, in the sample's padding: no new HostServices version,
the size check is unchanged) the pawn's floor is held where it stood below the game's camera on the tick before the move began,
and the eye is solved from it with the headset's live eye height: the eye is the game's camera plus a constant. The hold lasts
past the move's end until the pawn's floor is back within 2 cm of the held one (ME2's camera is still 0.51 m high at the
custom action's end and settles about 0.6 s later); a camera that hasn't come back after 1.5 s is eased there at 0.5 m/s.
Releasing at the action's end (the first version, 36bc92aa) dropped the eye 52 units in one frame. ME1 has no traversal move,
so nothing changes there. Any new root-motion move the mod learns about (a climb, ME3's traversal) must set the same flag.

# Citations

- research/le2-traversal.md "The floor lock through the vault"; research/le2-vrik.md "Arm IK across root-motion steps".
- `src/core/vr/FloorLock.hpp` (`MoveHold`), `src/plugin/FloorLock.cpp` (`Translate`), `src/game/Player.cpp` (`FloorLockSample`).
