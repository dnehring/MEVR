---
type: Audit
title: ME2 against ME1's comfort and movement
description: Contract by contract, how LE2 turns, walks, sprints, crouches and takes cover in VR against ME1's catalog; what is ME1's shared code, what is an adapter, and what never came across (linear walking, the Hammerhead's vignette, the focus pause).
tags: [audit, me2, parity, comfort, movement, turning, sprint, crouch, cover]
status: draft
timestamp: 2026-11-18T08:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:00:00Z }
game: LE2
against: ../../me1/comfort-and-movement.md
commit: 940a75c6
---

# Verdict

Audited against [ME1's comfort and movement catalog](../../me1/comfort-and-movement.md) by reading code and notes at
940a75c6; no game was run. "Verified" below is what a research note or the ledger records, never this audit.

- This category is the port's best case: turn, vignette, hand walk, sprint, crouch, cover and shakes run ME1's modules
  (`ue3::kEveryGame` handles, `runtime::Up()`), with adapters only where ME2's script differs. 23 of 33 contracts are
  same, same core or adapter.
- **Walking itself is the game's third-person walk on ME2.** `game/CombatStance` is `MEVR_GAME_LE1`-only, so linear
  walk, the speed from the push and the start, stop and skid steps (MOV-14 to MOV-17) are missing, while `LE2MEVR.ini`
  says `linearWalk = 1`, `linearStick = 1`, `startStopAnims = 0`. This is the difference a player feels first.
- **The comfort turn has never been seen working in ME2** (ledger G17: the run timed out), and its gate lists ME1's five
  camera-mode class names (MOV-06). Whether ME2's sprint and aim cameras carry those names decides whether snap turn
  stands down there; the code cannot say.
- **The Hammerhead gets no vignette** (MOV-11): the vehicle's motion comes from `game/Mako`, LE1-only, and ME2's vehicle
  mode is 4, not 1.
- Camera shake's "Low" is "off" on ME2 (MOV-21), and the focus pause was never checked (MOV-31).

| rating | count | ids |
|---|---|---|
| same | 15 | MOV-01, 02, 03, 04, 05, 07, 08, 09, 10, 13, 20, 22, 24, 26, 32 |
| same core, other inputs | 3 | MOV-12, 19, 29 |
| adapter | 5 | MOV-18, 23, 27, 28, 30 |
| reinvented | 0 | |
| partial | 1 | MOV-21 |
| missing | 5 | MOV-11, 14, 15, 16, 17 |
| n/a | 1 | MOV-33 |
| unknown | 3 | MOV-06, 25, 31 |

# Contract by contract

| id | rating | how ME2 gets it | where it differs | evidence |
|---|---|---|---|---|
| MOV-01 | same | `[Comfort] turn = game` from the same block; the rate logged per camera mode through `kEveryGame` handles | none; Halcyon ME2's `DecoupledPitch` is pinned on, as ME1's | `src/game/HandCalibration.cpp:523`, `src/game/Comfort.cpp:195` |
| MOV-02 | same | `vr::comfort::StepSnap`, same settings reader, same menu row (not greyed) | never seen in an ME2 run (ledger G17 U) | `src/game/Comfort.cpp:341`, `src/plugin/MainMenuSettings.cpp:969` |
| MOV-03 | same | same code | as MOV-02 | `src/game/Comfort.cpp:345` |
| MOV-04 | same | `plugin/PadMerge` is in the LE2 host; the flags ride the pad options | none read | `CMakeLists.txt:1096`, `src/plugin/PadMerge.cpp:176`, `src/game/Comfort.cpp:403` |
| MOV-05 | same | `Turn` with `Engine.Actor.SetRotation` through `runtime::Call`, `view_anchor::NoteTurn` | the 2-degree "pawn faces the controller" rule is untested against ME2's pawn rotation in combat | `src/game/Comfort.cpp:225` |
| MOV-06 | unknown | the same `Why` chain; game mode 0 is on foot in every game; cover from `CoverType` and `CoverAction` | `vl::CameraModeAllows` is an exact list of ME1's class names (Explore, ExploreStorm, Combat, CombatStorm, TightAim). ME2's notes name `SFXCameraMode_Explore` and `_Combat`; its storm, aim, roll and cover modes are not listed anywhere. A mode outside the list hands the turn back to the game without a word but the log. Settles it: on Save 4 with `turn = snap`, read `comfort: ... (camera mode X)` while walking, aiming and sprinting | `src/core/vr/ViewLevel.cpp:71`, `src/game/Comfort.cpp:326` |
| MOV-07 | same | `squad_place::Holding()`; placement is on by default on ME2 since 4e25c7cd | none | `src/game/Comfort.cpp:282` |
| MOV-08 | same | `plugin/Vignette` in the LE2 host, first of our quads in `SequelOverlays`; `setVignette` from `ModLoader` | the layer path was seen once (run 20261114-212412), not a wanted vignette | `CMakeLists.txt:1109`, `src/plugin/SequelOverlays.cpp:83`, `src/plugin/ModLoader.cpp:224` |
| MOV-09 | same | same `VignetteDrive` | none | `src/game/Comfort.cpp:388` |
| MOV-10 | same | same | none | `src/game/Comfort.cpp:391` |
| MOV-11 | missing | `mako::CurrentMotion()` is filled only by `game/Mako`, whose startup is under `MEVR_GAME_LE1` | in the Hammerhead `inVehicle` is never true, so no vignette there whatever `vignetteWhen` says. ME2's vehicle mode is 4 (`GameAdapter` knows it); nothing measures the vehicle | `src/game/Mako.cpp:632`, `src/game/Comfort.cpp:385`, `src/engine/GameAdapter.hpp:52` |
| MOV-12 | same core, other inputs | ME2's `MoveFollowsHead` is forced on at every load; the menu's row writes only `[Comfort] walk` | no "body" state can exist on ME2 (ME1 can still load one from a file) | `src/halcyon/le2/me2_menu.cpp:585`, `src/plugin/MainMenuSettings.cpp:1081`, `:299` |
| MOV-13 | same | `WalkTowardHand` in the shared pad merge, taking ME2's head-look yaw off first | none read; not run on ME2 | `src/plugin/PadMerge.cpp:56`, `src/plugin/RendererLe2.cpp:115` |
| MOV-14 | missing | nothing: `game/CombatStance` compiles to stubs outside LE1 | ME2 has no `BioPawnBehavior` ramp fields; where its ramps live is `[verify]` (`BioPawn.InitializeSpeeds`). `LE2MEVR.ini` still says `linearWalk = 1` | `src/game/CombatStance.cpp:55`, `:1349`; `src/game/HandCalibration.cpp:194`; ledger G18 |
| MOV-15 | missing | nothing | ME2 has `Engine.Pawn.SetDesiredSpeed` (research/le2-vrik-audit.md line 47), so the rule's output call exists; nobody looked for ME2's move function and thresholds | `src/game/CombatStance.cpp:55` |
| MOV-16 | missing | nothing | whether ME2 has start, stop and skid flags is not recorded | `src/game/CombatStance.cpp:55` |
| MOV-17 | missing | nothing | only matters with linear walk off | `src/game/CombatStance.cpp:55` |
| MOV-18 | adapter | one module: `StormOn` runs, `bWantsToStorm` withdrawn in both hooks' post-handlers | ME2 reads `bStorming` on the controller and leaves the storm alone in the game's own cover (its `StormOn` breaks a lean into a sprint). Measured on the fake headset 2026-11-16 (ledger G58 V) | `src/game/Sprint.cpp:41`, `:55`, `:198` |
| MOV-19 | same core, other inputs | `SpeedFactor` over `StormSpeed` and `Pawn.GetMaxSpeed` | no `CombatStormSpeed` on ME2: one sprint speed drawn or holstered | `src/game/Sprint.cpp:326` |
| MOV-20 | same | `vr::sprint::Step`, the same keys | none | `src/core/vr/Sprint.hpp` |
| MOV-21 | partial | the camera's two shake modifiers (`ScreenShakeModifier`, `CameraShakeCamMod`) get `bDisabled` | on or off only: any `screenShake` below 1 is off, so the menu's Low chip equals Off (logged once). Which gun shakes pass through those modifiers is `[verify]`; the Hammerhead's shakes are not looked at | `src/game/CameraShake.cpp:190`, `:237`; `research/le2-menus.md:82` |
| MOV-22 | same | `SFXCameraMode.bIsCameraShakeEnabled` cleared on the current mode, game mode 0 | the class-name filter (Explore, Combat, TightAim) is ME1's; no ME2 log line `camera shake: <class> shook the first-person view` is on record | `src/game/CameraShake.cpp:288`, `:333` |
| MOV-23 | adapter | the same detector and thresholds; the call is `Engine.Pawn.ShouldCrouch` (ME2 has no `BioPawn` override) | none in numbers. Verified per research/23 section 7.4 | `src/game/Posture.cpp:161` |
| MOV-24 | same | `CrouchArbiter` shared | none | `src/core/ik/Posture.hpp` |
| MOV-25 | unknown | ME1's seam (`ConstantFpEye`, `crouchDrop`) is LE1's bridge only; on ME2 the floor lock (shared, in the LE2 host) sets the eye over the pawn's floor from the headset | whether ME2's crouch camera lowers the view a second time under a physical crouch is not measured. Settles it: crouch for real on Save 4 and read `posture: ... after the crouch: the view is N units above Shepard's origin` and the snapshot's `floorLock` | `src/plugin/HalcyonBridge.cpp:676`, `CMakeLists.txt:1098`, `src/game/Posture.cpp:417` |
| MOV-26 | same | `body_calibration::Position` shared | calibration never run with a real profile on ME2 | `src/game/Posture.cpp` |
| MOV-27 | adapter | ME2 takes cover by a button: `FindPlayerCover`'s answer inside `TryAcquireCover` made false for the player; the HUD's cover prompt hidden by its own `HideCoverWidget` | forced: no native auto-cover test to hook. A's use and the vault's out-of-cover mantle untouched. Verified 2026-11-14 (61705db8) | `src/game/Posture.cpp:194`, `:436` |
| MOV-28 | adapter | `TryExitCover` with `bForceExit = true` | ME2's exit takes a parameter | `src/game/Posture.cpp:389` |
| MOV-29 | same core, other inputs | `vr::cover::Judge`, `[Cover]` from the same block | ME2 adds its shield-recharge rule (`vr::cover::Shelter`, `recharge = 1`), which ME1 has no mechanic for | `src/game/VrCover.cpp:134`, ledger G39 |
| MOV-30 | adapter | answers inside `SFXGame.ModifyDamage`; the cone gets the surface's facing through `CoverLink.GetSlotRotation` with `BioPawn.CurrentLink` held for the call | another damage function and another way to give the normal; the same "inside one call only" rule | `src/game/VrCover.cpp:58`, `:94` |
| MOV-31 | unknown | `game/FocusPause` is `MEVR_GAME_LE1`-only | nobody checked whether ME2 freezes without focus (trilogy plan T36). `LE2MEVR.ini` still carries `[Game] pauseUnfocused = 0`. Settles it: alt-tab from ME2 on the fake headset and watch `WorldInfo.bPlayersOnly` with `mevr props` | `src/game/FocusPause.cpp:17`, `src/game/HandCalibration.cpp:917` |
| MOV-32 | same | `LegsFollow` with `Engine.Actor.Velocity` | ME2 lacks only the conversation `BodyAway` branch (LE1's) | `src/game/BodyFollow.cpp:384`, ledger G13 |
| MOV-33 | n/a | ME1 has none; ME2 has its vault (`game/Traversal`, double A at an obstacle, the game's own `MantleCover`) | ME2's own feature, outside this catalog | `src/game/Traversal.cpp:96` |

# Reinvented

Nothing in this category reimplements a shared module: each ME2 branch sits inside ME1's file behind a game guard and
keeps the shared rule (`Sprint.cpp`, `Posture.cpp`, `VrCover.cpp`, `CameraShake.cpp`).

Two places share by name what could share by table:

| what | where | note |
|---|---|---|
| Camera-mode class-name lists | `vl::CameraModeAllows` ([ViewLevel.cpp](../../../../src/core/vr/ViewLevel.cpp)) and `FirstPersonClass` ([CameraShake.cpp](../../../../src/game/CameraShake.cpp)) | two different ME1 lists (exact names in one, substrings in the other) decide ME2's gates; a per-game table in `engine/GameAdapter.hpp` with a ReflectCheck row per name would make a missing ME2 class a logged fact |
| "In a vehicle" | `mako::CurrentMotion` ([Mako.cpp](../../../../src/game/Mako.cpp)) | comfort asks an LE1 module; `engine::adapter::GameMode(mode) == Mode::Vehicle` already translates the mode for every game |

# Rediscovered problems

| problem on ME2 | already in ME1's list | sources | cost |
|---|---|---|---|
| A skip of A's `TryAcquireCover` could not be built in the script hook | "The sprint button's exec reaches the script through ProcessEvent" (the same cause, found on ME2 first and on ME1's sprint two days later) | [gotcha](../../gotchas/binding-execs-come-through-process-event.md), bitten 2026-11-14 (ME2 cover) and 2026-11-16 (sprint); [trilogy-sprint](../../../../research/trilogy-sprint.md) | ME1's sprint ran the game's own for ten days; lane F had to block one level down |
| The script frame's `Node` offset | "A skip handler read `Node` at the wrong offset" | [gotcha](../../gotchas/fframe-packed-on-four-bytes.md): 0x14 in LE1 and LE2 | not paid again: the adapter carries it |
| New to ME2, not a rediscovery: the floor lock followed the pawn's root-motion steps through a vault | (none: ME1 has no traversal) | [gotcha](../../gotchas/floor-lock-through-a-traversal-move.md), 2026-11-16 | hands a metre off for a third of a vault's ticks until `MoveHold` |

No note records ME2 meeting the linear-walk problems, because nothing was built: the first ME2 player report of "the
walk feels like the third-person game" would be ME1's of 2026-11-12 again ([research/38](../../../../research/38-combat-stance.md)).

# Forced by the game

- Cover by button, not by a native auto test (MOV-27): [research/23](../../../../research/23-cover.md) section 7.
- `Engine.Pawn.ShouldCrouch`, `TryExitCover(bForceExit)`, `SFXGame.ModifyDamage`, `bStorming` on the controller, no
  `CombatStormSpeed`: ME2's script ([le2-movement-source](../../../../research/le2-movement-source.md)).
- Shakes as camera modifiers (MOV-21): ME2 has no per-weapon shake objects to scale; a share below 1 would need a
  hook where a shake is added.
- No `BioPawnBehavior` ramp fields (MOV-14): the fields are really absent; the behaviour is not thereby impossible.
- The vault (MOV-33) and the shield-recharge rule (MOV-29) are ME2's own mechanics.

# Not forced

- Linear walk, linear stick and the start and stop steps left unported (MOV-14 to MOV-16): only the field names are
  missing; `SetDesiredSpeed` exists and the rule (`vr::combat_stance::LinearSpeed`, `WantedRamp`) is game-independent.
- The camera-mode gates using ME1's class names unchecked (MOV-06, MOV-22).
- The Hammerhead without the vehicle vignette (MOV-11).
- The menu's Low chip for camera shake shown on ME2 though it equals Off (MOV-21).
- Dead keys in `LE2MEVR.ini` (`linearWalk`, `linearStick`, `startStopAnims`, `exploreRamp`, `crouchDrop`,
  `pauseUnfocused`).
- The focus pause never checked (MOV-31).

# What to do

1. **Port linear walking to ME2** (MOV-14, 15, 16). Files: `src/game/CombatStance.cpp` (split the ramp and stick
   section from the LE1 stance code; per-game field names in `src/engine/GameAdapter.hpp`), ReflectCheck rows. First
   find ME2's ramp and start/stop state with `mevr calltrace` and `mevr props` on the pawn
   ([le2-vrik-audit](../../../../research/le2-vrik-audit.md) line 48). Check: ME1's own measure, `movement: response:`,
   on Save 4 under a `make-walk-recording.py -Inputs` replay: a run within 0.1 s of the push, half a push near 1.7 m/s.
2. **Run the comfort check that timed out** (MOV-02 to MOV-06, MOV-13): two right-stick flicks and a hand-yawed walk
   on Save 4 (docs/le2-m6-native-checks.md's comfort row). Check: `comfort: snap right 45 deg`, `halcyon bridge: the
   mod turns`, and the camera-mode names logged while aiming and sprinting; add any missing ME2 names to the gate.
3. **Vehicle motion from the game mode, not from `game/Mako`** (MOV-11). Files: `src/game/Comfort.cpp:385`, a small
   every-game reader of the pawn's speed and yaw rate feeding `vr::comfort::VehicleMotion`. Check: a vignette target
   above 0 in the snapshot's `"comfort"` while the Hammerhead drives.
4. **Measure the crouch's view drop** (MOV-25) and **the focus pause** (MOV-31): the two runs named in their rows.
   Each ends as a ledger cell (G40, G45) moved to verified or to a port.
5. **Make the menu tell the truth for shakes** (MOV-21): hide the Low chip under `le2` in `assets/menu/main.rml:180`.
   Check: `mevr-menu-preview` with the sequel flag.
6. **Guard the dead keys** as in the settings audit's step 4.

# Citations

- Catalog: [ME1 comfort and movement](../../me1/comfort-and-movement.md); method: [how to catalog](../../how-to-catalog.md);
  the sibling audit: [settings and defaults](settings-and-defaults.md).
- Ledger: [trilogy parity ledger](../../../trilogy-parity-ledger.md) rows G11, G13, G17, G18, G39, G40, G45, G58. This
  audit agrees with G18 (missing) and G45 (missing, unchecked). It reads G17's "shared" as "shared and unverified with
  an ME1 name list in its gate", and G11's "adapted" as partial for the screen shakes' share.
- Code: [Comfort.cpp](../../../../src/game/Comfort.cpp), [ViewLevel.cpp](../../../../src/core/vr/ViewLevel.cpp),
  [CombatStance.cpp](../../../../src/game/CombatStance.cpp), [Sprint.cpp](../../../../src/game/Sprint.cpp),
  [CameraShake.cpp](../../../../src/game/CameraShake.cpp), [Posture.cpp](../../../../src/game/Posture.cpp),
  [VrCover.cpp](../../../../src/game/VrCover.cpp), [FocusPause.cpp](../../../../src/game/FocusPause.cpp),
  [BodyFollow.cpp](../../../../src/game/BodyFollow.cpp), [Traversal.cpp](../../../../src/game/Traversal.cpp),
  [Mako.cpp](../../../../src/game/Mako.cpp), [PadMerge.cpp](../../../../src/plugin/PadMerge.cpp),
  [SequelOverlays.cpp](../../../../src/plugin/SequelOverlays.cpp), [RendererLe2.cpp](../../../../src/plugin/RendererLe2.cpp),
  [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp), [me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp),
  [main.rml](../../../../assets/menu/main.rml), [CMakeLists.txt](../../../../CMakeLists.txt).
- Research: [23 cover](../../../../research/23-cover.md), [31 comfort](../../../../research/31-comfort.md),
  [38 combat stance](../../../../research/38-combat-stance.md), [trilogy-sprint](../../../../research/trilogy-sprint.md),
  [le2-traversal](../../../../research/le2-traversal.md), [le2-movement-source](../../../../research/le2-movement-source.md),
  [le2-vrik-audit](../../../../research/le2-vrik-audit.md), [le2-menus](../../../../research/le2-menus.md).
- Gotchas: [binding execs come through ProcessEvent](../../gotchas/binding-execs-come-through-process-event.md),
  [FFrame packed on four bytes](../../gotchas/fframe-packed-on-four-bytes.md),
  [floor lock through a traversal move](../../gotchas/floor-lock-through-a-traversal-move.md).
