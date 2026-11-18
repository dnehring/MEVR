---
type: Audit
title: ME3 against ME1's body and IK
description: Contract by contract, how ME3 reproduces ME1's arm IK, body follow, floor lock, body scale, fingers, weapon-drawn stance pieces and calibration, which of them stop short, and which of ME2's findings recur.
tags: [audit, me3, parity, vrik, body, calibration]
status: draft
timestamp: 2026-11-18T10:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:00:00Z }
game: LE3
against: ../../me1/body-and-ik.md
commit: 583591c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/body-and-ik.md) by reading code and notes at `583591c6`; no game was run.
Line numbers are that commit's. "V" marks what a fake-headset run on the Normandy (Save 31) recorded; everything else is
reading alone. Nothing here was seen in a headset on ME3, and nothing was seen in a fight: ME3 has no accepted combat
fixture ([research/le3-combat-fixtures.md](../../../../research/le3-combat-fixtures.md)).

- The IK is ME1's on ME3: the writer, arm IK, the body follower, fingers and the calibration's rules are the shared code
  with one address, ME3's skeleton layout and a few names swapped. 26 of 40 contracts are `same`, `same core, other
  inputs` or `adapter`. No second IK solver exists.
- **Finding 1 (fixed for the floor lock, view level, the turn and the shakes 2026-11-18, lane AU2: ME3's action cameras count
  as the body's, `vr::IsActionCamera` behind `engine::adapter::kActionCameras`, and an evade is no floor move; a roll seen on the
  combat fixture, research/le3-combat-fixtures.md "A combat fixture by console"; Shepard's body, head and gun hidden while a roll
  lasts, lane AU8, e4d35c24; the melee camera seen third person on the fixture, open).** Was: first person, the floor lock and the
  body's anchor are tied to four camera-mode names, and ME3 changes
  camera mode constantly in a fight (reading alone).** Halcyon ME3 has first-person states only for `Explore`,
  `ExploreStorm`, `Combat`, `CombatStorm` and `TightAim`; the floor lock's `cameraOwned` is ME1's name rule (contains
  `Explore`, `Combat` or `TightAim`). ME3's own `SFXCameraMode_Melee`, `_Roll`, `_HitReaction`, `_EnterCover` and `_Atlas`
  match neither. Upstream's comment measured each of the first four at about 0.5 s per roll, melee, hit or cover entry.
  For that time the view is the game's third-person camera, the head is shown, the floor lock is off (the eye read
  1.43 m against 1.7526 m when the lock dropped under the menu on 2026-11-15) and arm IK can leave its first-person
  anchor. Every run so far was on the Normandy, in `Explore` only (BOD-03, 12, 22).
- **Finding 2: the body's size cannot settle on ME3, for two reasons.** ME2's defect is still in the tree
  (`player::Crouched` resolves on ME1 only and "unknown" is taken as "crouched"). ME3 adds a second: the standing
  reference also needs "holstered" (`weapon::Key() == 0`), and ME3 keeps no holstered state in a mission. With the floor
  lock on, `[Body] scale = auto` stays at 1 and nothing is saved per mesh (BOD-16, 17, 18).
- **Finding 3: walking and the weapon swap are the game's own** (BOD-23 to 27): the linear walk, the speed from the
  push, the start and stop steps and the draw rate are inside `MEVR_GAME_LE1`. Nothing records whether ME3 has the
  fields. The eye point on a draw is the one piece ME3 has, by Halcyon ME3's own table (BOD-22).
- Where this audit disagrees with the ledger: G14 ("body scale: shared, unverified") is shared code with two dead
  inputs; G15 ("floor lock: adapted, verified") is verified for one camera mode on a ship without combat.

| rating | count | ids |
|---|---|---|
| same | 21 | BOD-02 04 05 06 07 08 09 10 11 19 20 21 30 31 32 33 34 35 36 37 39 (36 since 2026-11-18) |
| same core, other inputs | 2 | BOD-03 16 |
| adapter | 3 | BOD-01 28 29 |
| reinvented | 1 | BOD-22 |
| partial | 3 | BOD-12 14 17 |
| missing | 7 | BOD-18 23 24 25 26 27 38 |
| n/a | 1 | BOD-40 |
| unknown | 2 | BOD-13 15 |

# Contract by contract

Files: [ArmIk.cpp](../../../../src/game/ArmIk.cpp), [BodyFollow.cpp](../../../../src/game/BodyFollow.cpp),
[BodyScale.cpp](../../../../src/game/BodyScale.cpp), [Player.cpp](../../../../src/game/Player.cpp),
[Posture.cpp](../../../../src/game/Posture.cpp), [CombatStance.cpp](../../../../src/game/CombatStance.cpp),
[Weapon.cpp](../../../../src/game/Weapon.cpp), [Addresses.hpp](../../../../src/engine/Addresses.hpp),
[FloorLock.cpp (core)](../../../../src/core/vr/FloorLock.cpp), [FloorLock.cpp (host)](../../../../src/plugin/FloorLock.cpp),
[RendererLe3.cpp](../../../../src/plugin/RendererLe3.cpp), [engine_probe.cpp](../../../../src/halcyon/le3/engine_probe.cpp),
[me2_menu.cpp](../../../../src/halcyon/le3/me2_menu.cpp), [CMakeLists.txt](../../../../CMakeLists.txt).

| id | rating | how ME3 gets it | where it differs | evidence |
|---|---|---|---|---|
| BOD-01 | adapter | The shared writer on ME3's `UpdateSkelPose` `0x796EB0`, ME3's 32-byte `BoneAtom` and 0x50 reference-bone layout admitted by the shared `Skeleton`. V: both hands in view 0.07 m from the grips (run 2026-11-17). | The address and the layout only. On foot is game mode 0 on every game. | Addresses.hpp:492; ArmIk.cpp:291-294; [le3-body-pose](../../../../research/le3-body-pose.md) "Original runtime-image skeletal follow-up"; [trilogy-first-person](../../../../research/trilogy-first-person.md) section 1 |
| BOD-02 | same | `ArmIk.cpp`'s reach scale, `[Arms] armLengthM` or the calibration's. | Not run with a measured arm (the fake headset has no calibration). | ledger G01, G16 |
| BOD-03 | same core, other inputs | ME1's 45 / 75 unit thresholds in `HandTargets`. | (a) `bodyAway` is `false` outside ME1: nothing asks, since ME3 shows conversations on the cinema screen. (b) ME3 alone opts in to a `HeadOnly` source: with no controller tracked the torso is still placed (ME1 and ME2 stop). (c) ME3 alone holds a stationary torso under the shared menu (`MenuBodyEligible`). (d) ME3's first-person camera stands 35 units behind the pawn's centre (ME2's 30 ahead), and in a camera mode Halcyon ME3 does not own the view is third person: whether the anchor falls to third person and returns after each roll or melee was never run. | ArmIk.cpp:296-309, 1175-1183; le3-body-pose "Fresh torso solve during controller loss"; trilogy-first-person section 1 |
| BOD-04 | same | `bone_pose::AnimatedHead`, scaled as drawn. | Nothing. | ArmIk.cpp:1160-1174 |
| BOD-05 | same | `Request::keep` from `info->cameraHooks`. | le3-body-pose lists "preserved camera bones before the first pose write" as a native gate; the runs since wrote poses with the view stable, but no note records the bone names read on ME3's skeleton. | ArmIk.cpp:1190-1193; le3-body-pose "Native gates still required" item 4 |
| BOD-06 | same | `core/ik/GoalStep`, the request's `frame`. | Unverified on ME3 (ledger G01: "the root-motion rule is the shared one, U on ME3"). ME3 has more root-motion moves than ME2 (rolls, mantles, ladders, the heavy melee). | ArmIk.cpp:1194-1199 |
| BOD-07 | same | The shared follower, ME1's constants. V: `torso: first person: the whole body under the view` with replayed hands. | Nothing. | BodyFollow.cpp:325-337 |
| BOD-08 | same | The shared slide. V as BOD-07. | The 0.10 m / 0.09 m neck model and the 0.75 m cap were not compared with ME3's skeleton. | BodyFollow.cpp:325-336 |
| BOD-09 | same | The shared anchor rules; the floor counter from the sequel host. ME3 also resets the follower on a changed world, pawn, mesh, session or recenter epoch. | An extra reset, not a different rule. | ArmIk.cpp:1150-1158; le3-body-pose "Fresh torso solve" |
| BOD-10 | same | `Engine.Actor.Velocity` is `kEveryGame`. | Built, not seen: the ledger asks for a walk under replay showing `torso: legs:`. The 60 / 30 units per second thresholds are ME1's; ME3's walk and run speeds were not compared. | BodyFollow.cpp:52, 384-388; ledger G13 |
| BOD-11 | same | `steadyIdle`, off by default as on ME1. | The idle sway was measured on ME1's skeleton only. | BodyFollow.cpp:419-430 |
| BOD-12 | partial | The shared rules through the sequel bridge's `FloorViewTranslation`; the mod's sample is `kEveryGame`. V on Save 31: `stageEyeM = finalEyeM = 1.7526`, correction 32.26 units, kept with the shared menu open. | (a) The manual height trim is not wired: `SetManualHeight` has one caller, ME1's bridge. (b) `cameraOwned` is ME1's name rule on ME3's names: `Melee`, `Roll`, `HitReaction`, `EnterCover` and `Atlas` fail it, so the lock drops for each of them. (c) The host's half is "first person is switched on", not "this mode is owned"; its own comment says `[verify] a mode it doesn't own`. (d) Slopes, stairs, combat: not run. | Player.cpp:157-166; [CombatStance.cpp (core)](../../../../src/core/vr/CombatStance.cpp):166-174; FloorLock.cpp (host):66, 104; [HalcyonBridge.cpp](../../../../src/plugin/HalcyonBridge.cpp):251; RendererLe3.cpp:130-136; engine_probe.cpp:169-178, 2703-2715 |
| BOD-13 | unknown | The host's hold code is shared. V: a wrist screen (`mevr omni map`) held Shepard's view on foot for 624 frames and gave it back. | No run recorded the held view's height or the frozen scale. Run: open the map on the wrist on Save 31 and read `floor lock: world UI hold start` and `floor lock: IK hold start` (raw, base, common and IK Z). Conversations in the world are deferred on ME3, so that hold has no caller. | trilogy-first-person section 1 |
| BOD-14 | partial | A physical crouch asks `Engine.Pawn.ShouldCrouch` behind ME3's own admission (host controls fresh, walking, no custom action, no cover, not storming). With the floor lock on, the eye is the real head's height. | (a) Whether ME3's player pawn crouches at all on that request is unproven: the note says a changed desire flag "is not proof of changed collision or eye height". (b) `kFlagPhysicalCrouch` and `[Hands] crouchDrop` are read only by ME1's bridge. (c) ME3 has no crouch button, so "L3 toggles in between" has nothing to do; whether L3 is free there was not traced. | Posture.cpp:160-165, 300-312, 363-372; HalcyonBridge.cpp:741-744; [le3-locomotion-audit](../../../../research/le3-locomotion-audit.md) |
| BOD-15 | unknown | `MoveHold` is shared; the sample's `traversalMove` is `le3::Moving()`. | `le3::Moving()` follows the lifecycle of moves the adapter observed (`gLife`), and its requests are off by default. Whether a roll or mantle the player starts with the game's own button raises it was not traced. Run (ledger G15): a ladder or mantle under `mevr camera trace` with `floor lock: a traversal move began` in the log. | Player.cpp:145; [Traversal.cpp](../../../../src/game/Traversal.cpp):125-133; [Le3Traversal.cpp](../../../../src/game/Le3Traversal.cpp):563 |
| BOD-16 | same core, other inputs | The writer's scale and the inverse goal map are shared; `mevr body scale <s>` works. | In `auto` the factor stays 1: see BOD-17. | BodyScale.cpp:227-233 |
| BOD-17 | partial | `StandingReference::Feed` is shared. | (a) `gCrouched` has no `kEveryGame`, so `player::Crouched` is `nullopt` on ME3 and `value_or(true)` resets the reference every tick (ME2's defect, unchanged; the same field already has an LE3 handle 134 lines below). (b) `Feed` also needs `holstered` = `weapon::Key() == 0`; ME3 keeps no holstered state, so in a mission the reference would not settle even with (a) fixed. On the Normandy (no weapon) it could. (c) Without a floor-locked view the old chasing estimator runs. | Player.cpp:41, 126-128, 175; BodyScale.cpp:188-194; FloorLock.cpp (core):90-101; Weapon.cpp:961-967; [Handles.cpp](../../../../src/ue3/Handles.cpp):11-13 |
| BOD-18 | missing | The save and seed code is shared. | Nothing is measured, so nothing is saved or seeded (follows from BOD-17). | BodyScale.cpp:192-195 |
| BOD-19 | same | `game/Fingers`, ungated, no per-game handle. | Bone names on ME3's skeleton: not run with a replayed trigger (ledger G03 "U"). In ME3's head-only solve the fingers are stripped with the hands, by design. | ledger G03; ArmIk.cpp:1324 |
| BOD-20 | same | The gun rule from the shared core. | Not run. | ledger G03 |
| BOD-21 | same | The touch actions are the shared `core/xr/Input` on Halcyon ME3's session. | Not run. | ledger G03 |
| BOD-22 | adapter (fixed, lanes AU5 and AU6) | ME1's rule through the shared seam `MevrSeams::ConstantFpEye` in Halcyon ME3's `FpApplyOffset` (HookOffset +0xC0); Combat's Offset (5, 20, 35) then (lane AU5). **The explore eye at Shepard's face (lane AU6, dadb7461; the owner, 2026-11-18: "Me3 eyes should match behavior from other games")**: the renderer's standing rows (-35, 0, 65), ME2's sign, the eye 35 units ahead of the hook bone (upstream's 35 put it 35 behind); a saved upstream default moves once (`[MEVR-FP]`); the height is the floor lock's | Measured (`arm IK: first person camera vs Shepard's head bone`): exploring on the Normandy forward 34, up 5 units (was -37, -15; ME2 24-29 and 25, ME1 4 and 15); in combat (fixture, assault rifle) forward 26, up 29, arm IK first person, the right hand 0.07 m from its controller (was 0.47) | [engine_probe.cpp](../../../../src/halcyon/le3/engine_probe.cpp) `g_fpDefs`, `FpApplyOffset`; [me2_menu.cpp](../../../../src/halcyon/le3/me2_menu.cpp) `LoadFpStates` |
| BOD-23 | missing | `game/CombatStance` is inside `MEVR_GAME_LE1`. | Off by default on ME1 too. | CombatStance.cpp:55, 281, 1349, 1399 |
| BOD-24 | missing | The same module. | Whether ME3's `BioPawnBehavior` has the ramp fields: no note looked. | ledger G18 |
| BOD-25 | adapter (fixed) | Lane AU6 (c8f6987e): the speed from the push on `BioPawn.fMoveMag` after `PlayerMoveExplore` and `PlayerMoveCombat` (the comfort audit's MOV-15). | Half a push 146 u/s (was 75). | [CombatStance.cpp](../../../../src/game/CombatStance.cpp) `OnSequelMove` |
| BOD-26 | missing | The same module. | Whether ME3 has start, stop and skid steps: not looked up. | - |
| BOD-27 | adapter (fixed) | Lane AU6 (c8f6987e): ME1's rule (`[Hands] drawRate` 6, `holsterRate` 15). ME3 plays its draw (custom action 12) in `AnimNodeSlot` channels (`Custom_Std_Upper`, `Custom_Std_Idle_Lower`, `Custom_Crouching_Upper`): a channel's sequence named `CB_Draw*` or `CB_Holster*` while it plays | Measured on the combat fixture (`holsters.ps1 -Game LE3`): holstered after 0.16 s (was 0.66-0.95); in hand after 0.06 s as before (ME3's current weapon changes at once) | [CombatStance.cpp](../../../../src/game/CombatStance.cpp) `UpdateSequelRates`; [research/38](../../../../research/38-combat-stance.md) "The sequels' walk and draw" |
| BOD-28 | adapter | `plugin/Calibration` and `CalibrationDraw` in the LE3 host (`MEVR_SEQUEL_MENU_SOURCES`), glue in `MenusLe2`. | Never run through: the fake headset has no interaction profile (ledger G16). | CMakeLists.txt:1161, 1187 |
| BOD-29 | adapter | The same glue as ME2's. | How "8 s of world frames on foot" is counted on ME3 was not traced. | [MenusLe2.cpp](../../../../src/plugin/MenusLe2.cpp) |
| BOD-30 | same | `core/vr/Calibration`. | - | unit tests |
| BOD-31 | same | The same. | - | unit tests |
| BOD-32 | same | The same. | - | unit tests |
| BOD-33 | same | The same. | - | unit tests |
| BOD-34 | same | The same; `handPitch` 90 on every game. | Whether ME3's hand model sits 90 degrees off the grip: not checked in a headset. | - |
| BOD-35 | same | `game/BodyCalibration`, ungated. | ME3's SMG borrows the rifle's calibration and default sight (`PresentationProfile`); ME3's sights keep the assist, convergence and aimed-fire overrides off. Both are in the sights category, noted here because the calibration feeds them. | le3-body-pose "Reuse and implemented differences" |
| BOD-36 | same | `[Calibration.<key>]` in `%LOCALAPPDATA%\MEVR\calibration.ini`, one file for the three games (`core/vr/CalibrationFile`, since 2026-11-18, lane AU7, 88e9a775; the owner: shared unless a game's VRIK differs), and the host's save counter. | Not run on ME3 (ME1 and ME2 were, research/25 section 17.1); ME3's host and mod take the same code. Was: the section lived in `ASI\LE3MEVR.ini`, a player calibrated in ME1 or ME2 prompted again. | [ME2 summary](../me2/summary.md) question 4 |
| BOD-37 | same | `PlayPosition` in the shared core; the sample's `seated` is `kEveryGame`. | Not run. ME3 keeps "physically seated play" apart from "an authored seated scene" (the second refuses traversal and gestures, the first does not). | Player.cpp:140-141; [Mod.cpp](../../../../src/mod/Mod.cpp):1067-1076 |
| BOD-38 | missing | `kSkelUpdateBounds` and `kPrimitiveUmbraBounds` are 0 on ME3; the module stands down. | By decision ("ported only if the bug shows"). ME3's two addresses are found and verified (`0x793850`, `0x32EC10`). ME3's camera is 35 units from the pawn's centre, about the size of ME1's 43-unit offset, so the bug's cause may be present; no snapshot looked for a culled body at steep head angles. | Addresses.hpp:466-471, 493-494 |
| BOD-39 | same | `bConsiderAllBodiesForBounds` through `kEveryGame` handles. | Nothing. | ArmIk.cpp |
| BOD-40 | n/a | Halcyon ME3 clears `bCollisionEnabled` itself on the mode it writes and on an interpolation's To. V on Save 31: the flag 0 in every tick, the camera 35.0 units from the pawn while pressed into geometry. | The `Interpolate` object's own flag is left set: during a blend the game could pull the camera. `[verify]` on a combat fixture. | trilogy-first-person section 3; ledger G12 |

Two things the task named that the catalog has no contract for: the camera shakes (`game/CameraShake` runs on ME3
through ME2's branch, the shake modifiers switched off rather than ME1's amplitudes scaled; commit `a10833f0`) and the
hidden head (pinned by `ApplyMevrBaseline`, with the body shown in every state since `6f70774c`).

# Against ME2

| ME2 finding | on ME3 |
|---|---|
| Body scale cannot settle: `player::Crouched` is ME1-only (ME2 BOD-17) | **Same defect**, same line (`Player.cpp:41`). ME3 adds the holstered gate on top. |
| Nothing saved per mesh (ME2 BOD-18) | Same. |
| The eye moves on a draw: no constant-eye seam (ME2 BOD-22) | **Different**: no seam either, but Halcyon ME3's table has the same offset for all four standing states, so the default result is ME1's. The transient cameras are ME3's own gap. |
| Explore stance, ramp, speed from the push, start and stop steps, draw rate missing (ME2 BOD-23 to 27) | Same. ME2 has one recorded fact (no ramp fields); ME3 has none. |
| Calibration stored per game (ME2 BOD-36) | Same; fixed for both 2026-11-18 (one file for the three games). |
| Manual height trim not wired (ME2 BOD-12) | Same. |
| `cameraOwned` by ME1's name rule (ME2 BOD-12) | Same mechanism, wider effect: ME2's states are all explore or combat names; ME3 adds action cameras. |
| Constant-eye flag and `crouchDrop` read by ME1's host only (ME2 BOD-14) | Same. |
| `bodyAway` false (ME2 BOD-03) | Same, for the same reason (conversations deferred). |
| Wrist-screen hold height unknown (ME2 BOD-13) | Same. The hold itself was seen on ME3. |
| Body bounds off with addresses ready (ME2 BOD-38) | Same. |
| Camera collision already cleared (ME2 BOD-40) | Fine, except ME3 leaves the `Interpolate` object's flag (ME2 clears it). |
| Traversal hold verified by three vaults (ME2 BOD-15, BOD-06) | **Not verified on ME3**: unknown here. |
| "run (ME1-only handles)" hides dead inputs in the ledger | Same. |

# Reinvented

1. **The eye point per camera state** (BOD-22). ME1: one rule in [CombatStance.hpp](../../../../src/core/vr/CombatStance.hpp)
   (`ConstantEyeOffset`, `ConstantEyeApplies`, the blend correction) behind the `ConstantFpEye` seam. ME3: Halcyon's
   `g_fpDefs` table in `engine_probe.cpp:169-178`, a third copy next to ME2's `S<n>` rows. They agree by the shipped
   numbers, not by code. Unifying: a `MevrSeams` seam where each sequel renderer picks a state's offset that asks the
   shared rule, or a baseline pin of the standing states to the explore state's values.
2. **"Which camera modes are ours" is written three times**: Halcyon ME3's state list (exact class names), the mod's
   `PawnSample::cameraOwned` (ME1's substring rule), and the host's `FirstPersonOwned` (a plain on/off). On ME1 the
   first and second are the same rule. Unifying: one seam that returns the mode Halcyon owns this frame, read by the
   floor lock.
3. **ME3's cover refusal had its own admission** and failed open (a press during a wheel, a held view or after a 100 ms
   hitch snapped Shepard in). Lane FX3 folded it into the one rule `ik::posture::RefuseCoverSnap` on 2026-11-17. The
   physical crouch on ME3 still uses the stricter ME3-only admission (`HostMovement`, `NativeMovement` in `Posture.cpp`),
   which ME1 and ME2 do not have; whether that makes a crouch fail to register during a wheel or after a hitch was not
   run.
4. **Nothing else in this category is a second implementation.** The controller-loss torso (`HeadOnly`) and the menu
   torso hold went into the shared `HandTargets` and `ArmIk` as opt-ins; only ME3 opts in.

# Rediscovered problems

| problem on ME3 | when, cost | already held by |
|---|---|---|
| ME3 started in third person with the head shown, the stick pitching the view, profile hotkeys on | Found 2026-11-17 for the owner's headset test (lane FP3, `8e64bc49`) | ME1's `ModernBaseline` and ME2's `ApplyMevrBaseline` (2026-11-14): the same pin list, written a third time |
| The body vanished with no weapon out | The owner, in the headset, 2026-11-17 (`6f70774c`) | The catalog's "the body is under the head"; ME2's audit has the same row |
| The shared menu switched the floor lock off: the eye dropped from 1.75 m to 1.43 m | A protected run and a source audit, 2026-11-15 | ME1's catalog rows BOD-12 and BOD-13 (held views keep the corrected height). The fix went into the shared host, so ME1 and ME2 got it too |
| A cover press snapped Shepard in during a wheel | An adversarial review, 2026-11-17 | ME1's and ME2's rule: refuse whenever VR is on foot |
| A handle without `kEveryGame` silently dead on a sequel | Camera shake (`45b4635d`), holsters (2026-11-16); still live in `player::Crouched` | ME2's audit, finding 1, written the same day as this audit's commit and not yet applied |
| Shepard's collar in view with no controllers | 2026-11-15, two runs | New: ME1 stops arm IK without controllers and a headset always has them. Solved once in shared code |

# Forced by the game

- **`UpdateSkelPose`'s address and the skeleton layout** (`0x796EB0`, 32-byte atoms).
- **No holstered state in a mission** (CLAUDE.md "ME3 keeps no holstered state"): BOD-17's "holstered" input, BOD-27's
  draw. The body scale needs another standing pose to measure, or a measured combat-stance offset.
- **No crouch button and cover owned by the game mode** ([le3-locomotion-audit](../../../../research/le3-locomotion-audit.md)):
  BOD-14's arbitration differs.
- **Action cameras** (`Melee`, `Roll`, `HitReaction`, `EnterCover`, `Atlas`) exist only on ME3. That they exist is
  forced; that first person and the floor lock drop for them is not (see below).
- **Camera collision** is already cleared by Halcyon ME3 (BOD-40).
- **Conversations in the world** are deferred by the owner, so BOD-03's caller and BOD-13's conversation hold have
  nothing to do.

# Not forced

- `player::Crouched` without `kEveryGame` (BOD-17): one word, and the LE3 handle exists at `Player.cpp:175`.
- First person and the floor lock dropping in the action cameras (BOD-03, 12, 22): a state row and a name in a list.
- The eye table left unpinned (BOD-22).
- The linear walk, the speed from the push, the start and stop steps (BOD-24 to 26): never looked up on ME3.
- The calibration stored per game (BOD-36): the owner has answered "shared"; built 2026-11-18 (88e9a775).
- The manual height trim not wired (BOD-12).
- Body bounds off (BOD-38): a decision, with the addresses ready.
- The `Interpolate` object's collision flag left set (BOD-40): ME2's branch clears it.

# What to do

Ordered by what a player sees. Parity work and questions only; each needs the combat fixture the ME3 plan is still
missing, so item 0 comes first.

0. **A combat fixture** ([research/le3-combat-fixtures.md](../../../../research/le3-combat-fixtures.md)). Items 1, 4 and 6
   cannot be closed on the Normandy.
1. **The action cameras** (BOD-03, 12, 22). First confirm by run: `mevr camera trace` through a roll, a melee and a hit
   with replayed hands; read the mode column, `floor lock:` and `arm IK: body anchor:` lines and take a snapshot
   mid-roll. If the view leaves first person: decide with the owner whether a roll and a melee stay first person (ME1
   has neither camera, so there is no ME1 behaviour to copy); then either add the states in
   `src/halcyon/le3/engine_probe.cpp` (`g_fpDefs`) and the names in `vr::combat_stance::ConstantEyeApplies`, or hold the
   last owned state through them as the upstream head-aim code already does for the weapon verdict. Closes when the
   trace shows the eye height unchanged within 2 cm through each.
2. **Body scale's two inputs** (BOD-17, 18): `ue3::kEveryGame` on `gCrouched` (`src/game/Player.cpp:41`) with its
   ReflectCheck rows, as ME2's audit item 1. For ME3 also decide what "holstered" means: on the Normandy `weapon::Key()`
   is 0 and the reference can settle there; in a mission either accept the seeded value from the ship or measure the
   combat stance's head drop (ME1: 16 cm). Closes with `body scale: ... auto: fixed standing references` and a
   `[Body.Shepard]` line in `LE3MEVR.ini` after a minute standing on the Normandy.
3. **One calibration for the trilogy** (BOD-36): the owner said shared. **Built 2026-11-18** (lane AU7, 88e9a775; research/25
   section 17.1): `%LOCALAPPDATA%\MEVR\calibration.ini` for the three games, a `[Calibration.<key>.LE3]` override if ME3's gun
   stances ever need their own. Seen on ME1 and ME2; closes for ME3 when a section written by ME1 is used on ME3 without a
   prompt (Save 31 with `[Debug] calibrationKey`).
4. **Speed from the push and the start steps** (BOD-24 to 26): look the names up on ME3 (`mevr props` on the pawn's
   behaviour, `mevr calltrace` for the move functions), then `game/CombatStance`'s walk part behind an adapter for both
   sequels at once. Closes with `movement: response:` lines on ME3.
5. **Pin the eye table or add the seam** (BOD-22, Reinvented 1): until the seam exists, `ApplyMevrBaseline` in
   `src/halcyon/le3/me2_menu.cpp` can pin the standing states' offsets as it pins `hideHead`. Closes with a flat camera
   trace across a weapon swap and a sprint.
6. **The traversal hold and the root-motion rule on ME3** (BOD-06, 15): a ladder and a mantle under the trace; check
   that `traversal::Moving()` is true for a move the game's own button starts.
7. **Wire the manual height trim** on the sequel bridge (BOD-12): ledger G15 lists it for ME2; the same call serves ME3.
8. **The physical crouch** (BOD-14): prove that `ShouldCrouch` lowers ME3's pawn at all (`bIsCrouched`, the capsule's
   height); if it does not, ask the owner what a real crouch should do on ME3.
9. **Run the unrun rows**: fingers under a replayed trigger and grip, the wrist-screen hold's height (BOD-13), a
   calibration in a headset (BOD-28, 29, 34), the legs under a replayed walk (BOD-10), a snapshot looking straight down
   and over each shoulder for a culled body (BOD-38), `modeCollision` during a blend (BOD-40).
10. **Catalog and ledger**: add camera shake and the hidden head to the ME1 catalog as contracts; let
    `tools/parity-ledger.py` name handles without `kEveryGame` that a sequel build reads (ME2's audit item 9).

# Sources

- [The ME1 catalog](../../me1/body-and-ik.md); [ME2's audit](../me2/body-and-ik.md) and [summary](../me2/summary.md);
  [the parity ledger](../../../trilogy-parity-ledger.md) G01, G03, G12 to G18; [the trilogy plan](../../../trilogy-plan.md);
  [the LE3 port plan](../../../le3-port-implementation-plan.md); [the ME3 handoff](../../../me3-overall-goal-handoff.md).
- [research/le3-body-pose.md](../../../../research/le3-body-pose.md),
  [research/trilogy-first-person.md](../../../../research/trilogy-first-person.md),
  [research/le3-locomotion-audit.md](../../../../research/le3-locomotion-audit.md),
  [research/le3-traversal-adapter.md](../../../../research/le3-traversal-adapter.md),
  [research/null-headset-height.md](../../../../research/null-headset-height.md),
  [research/58](../../../../research/58-body-scale.md), [research/38](../../../../research/38-combat-stance.md).
- Gotchas: [floor lock through a traversal move](../../gotchas/floor-lock-through-a-traversal-move.md),
  [arm IK third-person anchor](../../gotchas/arm-ik-third-person-anchor.md),
  [culling camera ahead of the eye](../../gotchas/culling-camera-ahead-of-the-eye.md),
  [camera collision in first person](../../gotchas/camera-collision-in-first-person.md).
- The sibling audit: [vehicles](vehicles.md).
