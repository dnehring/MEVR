---
type: Audit
title: ME2 against ME1's body and IK
description: Contract by contract, how ME2 reproduces ME1's arm IK, body follow, floor lock, body scale, fingers, weapon-drawn stance pieces and calibration, and which of them stop short.
tags: [audit, me2, parity, vrik, body, calibration]
status: draft
timestamp: 2026-11-18T08:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:00:00Z }
game: LE2
against: ../../me1/body-and-ik.md
commit: 940a75c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/body-and-ik.md) by reading code and notes at `940a75c6`; no game was run.
Line numbers are that commit's.

- The IK itself is ME1's on ME2: the writer, arm IK, the body follower, fingers and the calibration's rules are shared
  code with one address and a few names swapped. 27 of 40 contracts are `same`, `same core, other inputs` or `adapter`.
- **Finding 1: the body's size cannot settle on ME2 (reading alone).** `player::Crouched` resolves its field only on
  ME1, the body scale takes "unknown" as "crouched", and the standing reference is thrown away every tick. With the
  floor lock on, `[Body] scale = auto` stays at 1 and nothing is saved per mesh (BOD-16, 17, 18).
- **Finding 2: none of "weapon drawn, like exploring" runs on ME2** (BOD-22 to 27): the eye point moves when the weapon
  comes out (Halcyon ME2's explore and combat offsets differ by 25 units back), walking has the game's ramp and start
  steps, and a draw or holster takes the game's 0.6 to 1.1 s where ME1 ships 0.16 s. The ledger says "missing" (G18);
  nothing schedules it.
- **Finding 3: the calibration is per game.** It is stored in `ASI\LE2MEVR.ini`, so a player calibrated in ME1 is
  uncalibrated in ME2 and gets the first-run prompt again (BOD-36). Nothing forces that.
- The ledger's G14 cell ("body scale: shared, unverified") is where this audit disagrees: shared code, but its input
  is dead on ME2.

| rating | count | ids |
|---|---|---|
| same | 21 | BOD-02 04 05 06 07 08 09 10 11 15 19 20 21 30 31 32 33 34 35 37 39 |
| same core, other inputs | 3 | BOD-03 16 36 |
| adapter | 3 | BOD-01 28 29 |
| reinvented | 0 | |
| partial | 3 | BOD-12 14 17 |
| missing | 8 | BOD-18 22 23 24 25 26 27 38 |
| n/a | 1 | BOD-40 |
| unknown | 1 | BOD-13 |

# Contract by contract

Files: [ArmIk.cpp](../../../../src/game/ArmIk.cpp), [BodyFollow.cpp](../../../../src/game/BodyFollow.cpp),
[BodyScale.cpp](../../../../src/game/BodyScale.cpp), [Player.cpp](../../../../src/game/Player.cpp),
[CombatStance.cpp](../../../../src/game/CombatStance.cpp), [Addresses.hpp](../../../../src/engine/Addresses.hpp),
[MenusLe2.cpp](../../../../src/plugin/MenusLe2.cpp), [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp).
"V" is a fake-headset run the ledger or [research/le2-vrik.md](../../../../research/le2-vrik.md) records; the rest is
reading alone. Nothing here was seen in a headset on ME2.

| id | rating | how ME2 gets it | where it differs | evidence |
|---|---|---|---|---|
| BOD-01 | adapter | The shared writer on ME2's `UpdateSkelPose` `0x79B8A0`. V: wrists 0.07 m from the grips. | The address only. | Addresses.hpp:262-264; ledger G01 |
| BOD-02 | same | `ArmIk.cpp`'s reach scale, `[Arms] armLengthM` or the calibration's. | Not run with a measured arm (the fake headset has no calibration). | ledger G01, G16 |
| BOD-03 | same core, other inputs | ME1's 45 / 75 unit thresholds in `HandTargets`. | The `inView` caller for scenes in the world is ME1's: `bodyAway` is `false` on ME2. ME2 shows conversations on the cinema screen, so nothing asks. A wrist screen's hold is shared. | ArmIk.cpp:1175-1183 |
| BOD-04 | same | `bone_pose::AnimatedHead`. | Nothing. | ArmIk.cpp:1167-1174 |
| BOD-05 | same | `Request::keep`; ME2's camera bones were read before the first write. | Nothing. | [research/le2-vrik.md](../../../../research/le2-vrik.md) "The camera's hook" |
| BOD-06 | same | `core/ik/GoalStep`; the rule was built for ME2's vault. V: three vaults within 0.08 m. | Nothing. | ledger G01 |
| BOD-07 | same | The shared follower, ME1's constants. | Nothing. | BodyFollow.cpp:325-337 |
| BOD-08 | same | The shared slide. V: skeleton audit within 3 cm. | Nothing. | ledger G13 |
| BOD-09 | same | The shared anchor rules; the floor counter comes from the sequel host. | Nothing. | ledger services `floor` |
| BOD-10 | same | `Engine.Actor.Velocity` is `kEveryGame`. | The 60 / 30 units per second thresholds are ME1's; ME2's run speeds were not compared. | BodyFollow.cpp:52, 384-389 |
| BOD-11 | same | `steadyIdle`, off by default as on ME1. | The 2-3 cm idle sway was measured on ME1's skeleton only. | BodyFollow.cpp:419-430 |
| BOD-12 | partial | The shared rules through the sequel bridge's `FloorViewTranslation`; the mod's sample is `kEveryGame`. V on Save 4: eye 1.7526 m, correction +12.26. | The manual height trim is not wired (ledger G15). `cameraOwned` is decided by ME1's camera-mode name rule (`ConstantEyeApplies`) on ME2's mode names: which ME2 modes pass was checked only where the run stood. | Player.cpp:137-168; HalcyonSequelBridge.cpp:306 |
| BOD-13 | unknown | The host's hold code is shared; ME2 has wrist screens (`game/WorldUi` runs with "ME1-only handles") and no conversations in the world. | Run: open an omni-tool screen on ME2 and read `floor lock: world UI hold start` and `IK hold start`. | ledger modules table |
| BOD-14 | partial | A physical crouch is `Engine.Pawn.ShouldCrouch` (`game/Posture` runs). With the floor lock on, the eye is the real head's height, so the view lowers once. | The constant-eye seam and `kFlagPhysicalCrouch` are consumed only by ME1's bridge; `[Hands] crouchDrop` does nothing on ME2. Where the floor lock does not apply, Halcyon ME2's crouch state has its own offset. | [HalcyonBridge.cpp](../../../../src/plugin/HalcyonBridge.cpp):741-743 (the only reader) |
| BOD-15 | same | `MoveHold`, built for ME2's vault. V. | Nothing. | [gotcha](../../gotchas/floor-lock-through-a-traversal-move.md) |
| BOD-16 | same core, other inputs | The writer's scale and inverse goal map are shared; `mevr body scale <s>` works. | In `auto` the factor stays 1: see BOD-17. | BodyScale.cpp:227-233 |
| BOD-17 | partial | `StandingReference::Feed` is shared. | `gCrouched` in `Player.cpp` has no `kEveryGame`, so on ME2 `player::Crouched` is `nullopt`; `BodyScale.cpp` takes `value_or(true)`; `Feed` resets on `gameCrouch`. Shepard's reference is never measured. If the host's view is not floor-locked, the old chasing estimator runs instead: ME1's pre-2026-11-07 path. | Player.cpp:41, 126-128; BodyScale.cpp:190-195; [FloorLock.cpp](../../../../src/core/vr/FloorLock.cpp):99-102; [Handles.cpp](../../../../src/ue3/Handles.cpp):11-13 |
| BOD-18 | missing | The save and seed code is shared. | Nothing is ever measured, so nothing is saved or seeded (follows from BOD-17). | BodyScale.cpp:94-100 |
| BOD-19 | same | `game/Fingers`, ungated. | Bone names on ME2's skeleton: not run with a replayed trigger (ledger G03 "U"). | ledger G03 |
| BOD-20 | same | The gun rule from the shared core. | Not run. | ledger G03 |
| BOD-21 | same | The touch actions are the shared `core/xr/Input` on Halcyon ME2's session. | Not run. | ledger G03 |
| BOD-22 | missing | No seam in Halcyon ME2's first person; `MevrBridge::ConstantFpEye` is ME1's host only. | ME2's eye offsets are per state: explore (-35, 0, 35), explore sprint (-40, 0, 35), combat (-60, 5, 25), combat sprint (-70, 5, 25). Measured 2026-11-18 (lane AU3, Save 4, `mevr camera trace` across a draw): the game's camera goes from 35 ahead, 0, 85 up of the pawn to 30 ahead, 5 left, 75 up within 0.4 s; the floor lock takes the height, so the eye moves about 7 cm sideways and back. The table's difference (-25, +5) is not the measured one: each camera mode's own hook differs too, so pinning the table to S0 would not hold the eye; ME1's rule (the mode's hook minus the explore eye) needs a seam where Halcyon ME2 writes the offset (`FpApplyOffset`). | [MELE2VR.ini](../../../../src/halcyon/le2/runtime/MELE2VR.ini):74-98 |
| BOD-23 | missing | `game/CombatStance` is inside `MEVR_GAME_LE1`. | Off by default on ME1 too. | CombatStance.cpp:1392-1394 |
| BOD-24 | adapter (start) | Since 0601ef40: ME1's rule on `Engine.Pawn.AccelRate` alone (ME2 has none of the four ramp fields); see the comfort audit's MOV-14 for the measured start and the unchanged stop. | ME2's stop lever unread. | CombatStance.cpp section C; ledger G18 |
| BOD-25 | missing | The same module. | `Engine.Pawn.SetDesiredSpeed` exists on ME2 (the name audit); nobody looked for `PlayerMoveExplore` / `PlayerMoveCombat`. | CombatStance.cpp:1349-1391 |
| BOD-26 | same | Since 0601ef40: ME1's three flags on ME2's `BioPawnBehavior`, cleared in VR on foot. | No measured effect in the replayed walk (the steps' states never showed). | CombatStance.cpp |
| BOD-27 | missing | The same module. | Measured on ME2: rifle in hand after 0.62 s, holstered after 0.9-1.1 s. ME1 ships a 1 s draw in 0.16 s. | [research/le2-vrik.md](../../../../research/le2-vrik.md) "Holsters on the sequels" |
| BOD-28 | adapter | `plugin/Calibration` and `CalibrationDraw` in the LE2 host (`MEVR_SEQUEL_MENU_SOURCES`), glue in `MenusLe2`. | Never run through: the fake headset has no interaction profile (ledger G16). | MenusLe2.cpp:124-174, 317-333 |
| BOD-29 | adapter | `calibration::NoteFocused`, `NoteControllers`. | How "8 s of world frames on foot" is counted on ME2 was not traced. | MenusLe2.cpp:158-161 |
| BOD-30 | same | `core/vr/Calibration`. | — | unit tests |
| BOD-31 | same | The same. | — | unit tests |
| BOD-32 | same | The same. | — | unit tests |
| BOD-33 | same | The same. | — | unit tests |
| BOD-34 | same | The same; `handPitch` 90 is the shipped default on every game. | Whether ME2's hand model sits 90 degrees off the grip as ME1's does: not checked in a headset. | — |
| BOD-35 | same | `game/BodyCalibration`, ungated. | The SMG (ME2's fifth family) has no stance of its own; which stance it borrows was not traced. | ledger G16 |
| BOD-36 | same core, other inputs | `[Calibration.<key>]` and the host's save counter. | The section lives in `ASI\LE2MEVR.ini`: ME1's calibration is not read, and ME2 prompts again. | CLAUDE.md "Our settings file is `<exe dir>\ASI\<GAME>MEVR.ini`" |
| BOD-37 | same | `PlayPosition` in the shared core; the sample's `seated` is `kEveryGame`. | Not run. | Player.cpp:140-141 |
| BOD-38 | missing | `kSkelUpdateBounds` and `kPrimitiveUmbraBounds` are 0 on ME2; the module stands down. | By decision: "port it only if ME2 culls the player's body". ME2's two addresses are found and verified (`0x798350`, `0x3146C0`). | Addresses.hpp:265-270 |
| BOD-39 | same | `bConsiderAllBodiesForBounds` through `kEveryGame` handles. | Nothing. | ArmIk.cpp:181-210 |
| BOD-40 | n/a | Halcyon ME2 clears `bCollisionEnabled` itself. V, 2026-11-17: the flag 0 in every tick. | — | ledger G12 |

# Reinvented

1. **The eye point per camera state is Halcyon ME2's table where ME1 has one rule.** ME1: every standing mode gets the
   explore eye ([CombatStance.hpp](../../../../src/core/vr/CombatStance.hpp) `ConstantEyeOffset`, the seam in
   `HalcyonBridge.cpp`). ME2: seven `S<n>_x/y/z` rows in `MELE2VR.ini`, pinned only for `hideHead` and `hideBody` by
   `ApplyMevrBaseline` ([me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp):508-520). Unifying takes a seam where
   Halcyon ME2 applies a state's offset (`FpApplyOffset`) that asks the shared rule, or a baseline pin of the standing
   states to S0's values.
2. **Nothing else in this category is a second implementation.** The sequel work here went into the shared core
   (`GoalStep`, `MoveHold`), which is the pattern to keep.

# Rediscovered problems

| problem on ME2 | when, cost | the catalog already had |
|---|---|---|
| The VRIK body vanished with no weapon out (upstream ME2 hid the body exploring and sprinting). | The owner, in the headset, 2026-11-17; fixed the same day (`6f70774c`). | "What the player gets": the body is under the head, always. ME1's baseline pins first person; ME2's baseline pinned the head and not the body. |
| A module compiled for ME2 but resolved nothing there (`Ready()` handles). | Holsters 2026-11-16, camera shake (`45b4635d`); still live in `player::Crouched`. | Not ME1's: the port's own class. The ledger marks such modules "run (ME1-only handles)" without saying which handle matters. |
| The hands rode a root-motion step; the eye left the head in a vault. | 2026-11-15 and 11-16, lanes C2 and E. | New on ME2 (ME1 has no such move); solved once, in the core. Not a rediscovery. |

# Forced by the game

- **`UpdateSkelPose`'s address** and the weapon family's names.
- **No ramp fields on `BioPawnBehavior`** (BOD-24): the linear walk needs ME2's own fields
  ([research/le2-vrik-audit.md](../../../../research/le2-vrik-audit.md) table, `[verify]`).
- **Camera collision** is already cleared by Halcyon ME2 (BOD-40).
- **Conversations in the world** are deferred on ME2 by the owner (rule 9 of [the trilogy plan](../../../trilogy-plan.md)),
  so BOD-03's caller and BOD-13's conversation hold have nothing to do.

# Not forced

- `player::Crouched` without `kEveryGame` (BOD-17): one word.
- The eye moving on a draw (BOD-22).
- The speed from the push, the start and stop steps, the draw and holster rates (BOD-25, 26, 27): no game fact is
  recorded against them; they were never looked up.
- The calibration stored per game (BOD-36).
- The manual height trim not wired (BOD-12).
- Body bounds off (BOD-38): a decision, with the addresses ready.

# What to do

Ordered by what a player sees. Parity work and questions only.

1. **Body scale's crouch read** (BOD-17, 18): give `gCrouched` in `Player.cpp:41` `ue3::kEveryGame` and its ReflectCheck
   row for LE2 and LE3 (`gAdmitCrouched` at line 175 already has the LE3 shape). Check on the fake headset: `body scale:
   Shepard's standing eye ...` then `auto: fixed standing references`, and a `[Body.Shepard]` line in `LE2MEVR.ini`. The
   ledger's G14 notes a floor fixture is needed for the factor itself; the reference settling needs none. First confirm
   the symptom: today's ME2 log should say `auto: waiting for Shepard standing holstered` for a whole session.
2. **The eye on a draw** (BOD-22): measure first (`mevr camera trace` across a draw on Save 4: the camera against the
   pawn, holstered and drawn). If it moves, pin the standing states' offsets or add the seam (Reinvented 1). Closes
   with the same trace flat.
3. **Draw and holster rates** (BOD-27): find ME2's weapon equip nodes (`mevr props` on the AnimTree), then
   `game/CombatStance`'s rate part behind an adapter. Closes when `holsters: the ... is in hand after` reads near 0.15 s
   for the rifle.
4. **Speed from the push and the start steps** (BOD-25, 26): `python tools/le2-name-audit.py game/CombatStance`, then
   `mevr calltrace` for the move functions. The ledger calls this "the one players notice". Closes with `movement:
   response:` lines on ME2.
5. **Ask the owner: one calibration for the trilogy?** (BOD-36). A player's body and controllers are the same in each
   game; the gun stances may not be. No code before the answer.
6. **Wire the manual height trim** on the sequel bridge (BOD-12), as the ledger's G15 already lists.
7. **Run the unrun rows**: fingers (snapshot `mod.fingers` under a replayed trigger and grip), the wrist-screen hold
   (BOD-13), a calibration in a headset (BOD-28, 29, 34).
8. **Body bounds** (BOD-38): leave off unless a snapshot shows the body culled; the port is two constants.
9. **Make the ledger name dead handles.** "run (ME1-only handles)" hid BOD-17. `tools/parity-ledger.py` could list, per
   module, each handle without `kEveryGame` that a sequel build reads. A tool change, no game code.

# Sources

- [The ME1 catalog](../../me1/body-and-ik.md); [the parity ledger](../../../trilogy-parity-ledger.md) G01, G03, G12 to
  G16, G18 and its modules table; [the trilogy plan](../../../trilogy-plan.md).
- [research/le2-vrik.md](../../../../research/le2-vrik.md), [research/le2-vrik-audit.md](../../../../research/le2-vrik-audit.md),
  [research/le2-traversal.md](../../../../research/le2-traversal.md), [research/58](../../../../research/58-body-scale.md),
  [research/38](../../../../research/38-combat-stance.md).
- Gotchas: [floor lock through a traversal move](../../gotchas/floor-lock-through-a-traversal-move.md),
  [arm IK third-person anchor](../../gotchas/arm-ik-third-person-anchor.md).
- The sibling audit: [holsters, grenades and melee](holsters-throwables-melee.md).
