---
type: Audit
title: ME3 audit - vehicles (the Atlas, mounted guns and shuttle scenes against ME1's Mako)
description: Every Mako contract of ME1's catalog rated on ME3, which has no player vehicle to drive, with what ME3 has instead (the Atlas mech, mounted turrets, authored shuttle rides), what the mod does in them today and which shared pieces would apply.
tags: [audit, me3, parity, vehicles]
status: draft
timestamp: 2026-11-18T10:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:00:00Z }
game: LE3
against: ../../me1/vehicles.md
commit: 583591c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/vehicles.md) by reading code and notes at `583591c6`; no game was run, and
no note records an Atlas, a mounted gun or a shuttle ride with this mod. Every claim below rests on reading.

- ME3 has no Mako and nothing the player drives: 30 of 36 contracts are `n/a`. The ledger's G44 says the same.
- What ME3 has instead is the **Atlas** (game mode 2, a pawn swap to `SFXPawn_Atlas`, camera `SFXCameraMode_Atlas`; the
  Leviathan Triton is the same shape), **mounted turrets** (also a pawn swap) and **authored seated scenes** (shuttle
  rides). The plan asks for "an explicit supported adapter or safe fallback" for the first two and "no traversal, head
  tracking kept" for the third. None of the three has an adapter, a fixture or a run.
- **Finding 1: in the Atlas the player may have neither crosshair nor ring.** The host asks the mod to hide the game's
  crosshair in every HUD gameplay mode, which on ME3 includes the Atlas, while the hand ring is drawn on foot only.
  Upstream relied on head aim to keep the crosshair with the view inside a mech; the MEVR build forces head aim off
  everywhere. So the Atlas aims by the stick alone, with the head free, and the crosshair hidden if its movie is the
  one the adapter finds.
- **Finding 2: "authored seated" is decided by ME1's signal.** The mod's one test is game mode 0 plus
  `authored_moment::TrackMoves`, which is fed by ME1's matinee move-track hook. On ME3 a seated scene that stays in
  mode 0 is seen only by each adapter's own native admission (a custom action, ignored move input).
- **Finding 3: on-foot systems switch off in the Atlas by the mode number, which is the safe direction**: arm IK, the
  floor lock, the hand ring and the wrist-screen hold all require mode 0. What the pad mapping does there (the left
  trigger, B, X, the right stick click) was not traced.

| rating | count | contracts |
|---|---|---|
| same | 0 | |
| same core, other inputs | 0 | |
| adapter | 0 | |
| reinvented | 0 | |
| partial | 1 | VEH-06 |
| missing | 3 | VEH-01 05 07 |
| n/a | 30 | VEH-03 04, 08 to 34, 36 |
| unknown | 2 | VEH-02 35 |

# Contract by contract

"LE1 guard" means the module's body is inside `#if defined(MEVR_GAME_LE1)`. ME3's game modes are 0 on foot, 1 vehicle,
2 Atlas, 6 injured Shepard ([GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp):56-62); mode 1 is in the enum but
no note records the base game or a DLC entering it.

| id | rating | what ME3 has instead, and what the mod does | evidence |
|---|---|---|---|
| VEH-01 | missing | The Atlas's guns and a mounted turret are aimed by the game's camera and the right stick. No controller ray aims them. ME1's rule (the hand aims, never the head) has an ME3 mechanic to apply to and nothing does. | [Mako.cpp](../../../../src/game/Mako.cpp):54 (LE1 guard) |
| VEH-02 | unknown | The shared `GetAdjustedAimFor` handler in `game/HandAim` has no game-mode test of its own; whether it redirects an Atlas's or a turret's shots depends on whether `game/Weapon` resolves a held weapon on the swapped pawn. Run: an Atlas fixture with `hand aim: shot N:` in the log. A redirect there would be a hand ray with no ring. | [HandAim.cpp](../../../../src/game/HandAim.cpp):673-681; Mako.cpp:276 |
| VEH-03 | n/a | No separate turret model to turn: the Atlas's torso and a mounted gun follow the game's view. | - |
| VEH-04 | n/a | No cockpit. | - |
| VEH-05 | missing | The hand ring is drawn on foot only: `OverlayReticleMode` returns -1 outside mode 0. ME1 shows the ring in the vehicle on a valid mod ray. | [HalcyonLe3Bridge.cpp](../../../../src/plugin/HalcyonLe3Bridge.cpp):244-246 |
| VEH-06 | partial | On foot the game's crosshair is hidden through ME3's `SFXSFHandler_Reticle` adapter. The host's request also covers the Atlas and the vehicle mode (`IsHudGameplayMode`), where no ring replaces it. Whether the Atlas's crosshair is that movie: not established. No cockpit aim mesh. | [SequelOverlays.cpp](../../../../src/plugin/SequelOverlays.cpp):261-266; GameAdapter.hpp:84-92; [ScreenReticle.cpp](../../../../src/game/ScreenReticle.cpp):150-170 |
| VEH-07 | missing | The Atlas walks and turns by the sticks. `game/Comfort` darkens on foot or "in the Mako"; `mako::CurrentMotion()` is ME1's, so there is no vignette in the Atlas. The rule `vr::comfort::VehicleMotion` is game-independent and would apply to any pawn's speed and yaw rate. | [Comfort.cpp](../../../../src/game/Comfort.cpp):383-391 |
| VEH-08 | n/a | No lever or stick: the Atlas uses the on-foot sticks. | [MakoControls.cpp](../../../../src/game/MakoControls.cpp):58 |
| VEH-09 | n/a | - | as VEH-08 |
| VEH-10 | n/a | - | as VEH-08 |
| VEH-11 | n/a | - | as VEH-08 |
| VEH-12 | n/a | - | as VEH-08 |
| VEH-13 | n/a | No `SetInputs` seam to own. | as VEH-08 |
| VEH-14 | n/a | No chase camera to keep. | as VEH-08 |
| VEH-15 | n/a | The Mako pad flags are never set. What the Atlas binds to X, Y, the triggers and B, and what the mod's on-foot flags leave set in mode 2, is in no note. | MakoControls.cpp:625 |
| VEH-16 | n/a | - | as VEH-15 |
| VEH-17 | n/a | - | as VEH-15 |
| VEH-18 | n/a | - | as VEH-08 |
| VEH-19 | n/a | No jump jets. | as VEH-08 |
| VEH-20 | n/a | - | as VEH-08 |
| VEH-21 | n/a | - | as VEH-08 |
| VEH-22 | n/a | - | as VEH-08 |
| VEH-23 | n/a | No cockpit package for ME3. The Atlas is shown by the game's own third-person Atlas camera with head look (Halcyon ME3 has no first-person state for `SFXCameraMode_Atlas`). | [CockpitProbe.cpp](../../../../src/game/CockpitProbe.cpp):52; [engine_probe.cpp](../../../../src/halcyon/le3/engine_probe.cpp):169-178 |
| VEH-24 | n/a | The Atlas's HUD goes to the body-locked HUD panel like the on-foot HUD (`IsHudGameplayMode`). | GameAdapter.hpp:84-92 |
| VEH-25 | n/a | No seat; arm IK stops outside mode 0, so Shepard's arms are not posed in the Atlas. | [ArmIk.cpp](../../../../src/game/ArmIk.cpp):291-294 |
| VEH-26 | n/a | `core/vr/CockpitPanel` is not in the sequels' source list. | [CMakeLists.txt](../../../../CMakeLists.txt):772-777 against 409-416 |
| VEH-27 | n/a | - | as VEH-26 |
| VEH-28 | n/a | - | as VEH-26 |
| VEH-29 | n/a | - | as VEH-26 |
| VEH-30 | n/a | - | as VEH-26 |
| VEH-31 | n/a | The hazard rule compiles; nothing calls it. | CMakeLists.txt:774 |
| VEH-32 | n/a | No gunner: the player is the Atlas's only gunner. The rule compiles; nothing calls it. | [MakoGunner.cpp](../../../../src/game/MakoGunner.cpp):26; CMakeLists.txt:776 |
| VEH-33 | n/a | - | as VEH-32 |
| VEH-34 | n/a | - | as VEH-32 |
| VEH-35 | unknown | `game/FocusPause` is ME1's. Whether ME3 freezes without window focus (`bPlayersOnly`) has not been checked (ledger G45, plan T36 "check first"). It matters on foot too, not only for a vehicle's exit. Run: the fake headset with another window in front; does the world tick. | [FocusPause.cpp](../../../../src/game/FocusPause.cpp):17-24 |
| VEH-36 | n/a | No second cockpit. | - |

What ME3 has today in each stand-in:

- **The Atlas (mode 2).** The game's own camera and controls, the head turning the view. Halcyon ME3 marks any pawn
  whose name lacks "Player" as a mech and treats it as armed, which upstream used to keep head aim on inside it
  (measured there: without head aim "the view rotated but ControlRotation did not, so the crosshair separated from
  where the player was looking"). The MEVR build forces head aim off in `SetHeadAimEnabled`, so that upstream cure is
  off and nothing replaces it. Our menu's way into the game's menu and the menu pause know the Atlas mode
  ([MenuPause.cpp](../../../../src/game/MenuPause.cpp):185-187); a game screen opened from the Atlas keeps the native
  panel and holds no view on foot ([research/le3-native-gui.md](../../../../research/le3-native-gui.md) item 4). ME3's
  Atlas melee is a separate context the melee adapter does not serve
  ([research/le3-melee-audit.md](../../../../research/le3-melee-audit.md)).
- **Mounted turrets.** The same pawn swap; whether they run in mode 0 or 1 is in no note. If mode 0 with a non-player
  pawn, `player::BioPawn` may still resolve and the on-foot systems could stay on: the case the plan's "cannot enter
  Shepard's normal input path accidentally" is about.
- **Authored seated scenes (shuttles).** Shown as conversations or cinematics on the cinema screen (modes 7 and 8),
  where traversal, melee and the physical controls are refused because the mode is not 0 and head tracking is the
  screen's. A seated stretch that stays in mode 0 is refused only by ME3's native admission in each adapter. The
  handoff says so itself: "Seated no-traversal behavior still needs an explicit runtime attempt".

# Against ME2

| ME2 finding | on ME3 |
|---|---|
| Nothing of the Mako runs; the category is deferred by the owner | Same outcome for another reason: no vehicle to drive (n/a, not missing). |
| The vehicle as it is today was never looked at | **Same**: no Atlas, turret or shuttle fixture, no note. |
| `VehicleHeadMode = 3` ships unpinned against ME1's "no head aim in the vehicle" | **Different**: ME3 has no such key. Head aim is forced off everywhere, the mech included; what is lost is upstream's crosshair-follows-view behaviour in the mech, with no hand aim in its place. |
| No vignette for the vehicle's motion | Same, for the Atlas. |
| Hand reticle and crosshair in the vehicle mode "not established" | **Different, read further**: the ring is off outside mode 0 and the crosshair's hide request is on in the Atlas. |
| Focus pause not checked (VEH-35) | Same. |
| The game-independent rules compile, every game module is ME1's | Same. |
| The catalog and the ledger disagree on whether `core/vr/MakoControls` and `MakoGunner` compile for the sequels | Same: CMakeLists.txt lists them for the sequels (lines 772-777), the ledger's generated table says LE1 only. |

# Reinvented

Nothing. No ME3 vehicle code of ours exists. Halcyon ME3's mech detection (`g_mechPawn`, by the pawn's name) is
upstream's; the shared `engine::adapter::GameMode` already names `Atlas`, which is the test our own modules use.

# Rediscovered problems

None recorded: no ME3 note describes an Atlas or a turret under the mod. The catalog's solved list the Atlas pass
should read first: the turret's aim order within a frame (three self-measuring seams), a crosshair fixed at the view
centre after leaving the vehicle (`[MEVR-CROSSHAIR]`, research/44 "Persistent centre cursor"), and the exit refused
without window focus.

# Forced by the game

| difference | evidence |
|---|---|
| No player vehicle: no lever, stick, jets, cockpit, gunner, repair or extraction to reproduce | ledger G44; [trilogy-plan.md](../../../trilogy-plan.md) T35 |
| The Atlas is its own game mode (2) with a swapped pawn and its own camera; injured Shepard is mode 6 | GameAdapter.hpp:56-62; engine_probe.cpp (`SFXCameraMode_Atlas`, `SFXPawn_Atlas`, `SFXPawn_DivingAtlas`) |
| Shuttle rides are authored scenes, not driven | [le3-port-implementation-plan.md](../../../le3-port-implementation-plan.md) lines 265-270; trilogy-plan.md rule 10 |

# Not forced

| what | on ME1 | on ME3 | evidence |
|---|---|---|---|
| Aim inside a piloted machine | The hand's ray, with the ring on it | The stick, no ring, the crosshair possibly hidden | HalcyonLe3Bridge.cpp:244-246; SequelOverlays.cpp:265-266 |
| The vignette while a machine turns | On with the comfort vignette | None in the Atlas | Comfort.cpp:383-391 |
| "Authored seated" | `authored_moment::TrackMoves` (ME1's matinee hook) plus the mode | The same test, whose first half never fires on ME3 | [Mod.cpp](../../../../src/mod/Mod.cpp):1067-1076; [AuthoredMoment.cpp](../../../../src/game/AuthoredMoment.cpp):416-424 |
| The pause without window focus | Undone | Not checked | FocusPause.cpp:17 |

# What to do

Short, and all of it is the campaign pass's "special actors" work (T35, T52). In order:

1. **Look at the Atlas as it is.** A save at an Atlas (Sur'Kesh, Grissom Academy or the Leviathan dive), loaded by
   `tools/le2-halcyon-run/load.ps1`; snapshots of both eyes; `mevr reticles`; `mevr calltrace` over each button; the
   log's `hand aim:` lines. Closes when a note in `research/` says what the view, the aim, the crosshair and each input
   do in mode 2 today. This settles VEH-02 and VEH-06.
2. **The crosshair in the Atlas** (VEH-05, 06): if step 1 shows no crosshair and no ring, stop the hide request
   outside mode 0 until a ring exists there (`src/plugin/SequelOverlays.cpp:265`, one condition), or show the ring on a
   valid ray as ME1 does in the Mako. Which of the two is an owner question: ME1's rule is "the hand aims"; whether the
   Atlas's guns should follow the hand is not decided.
3. **A seated scene in mode 0** (trilogy-plan rule 10): find one shuttle ride, read its game mode and
   `MovementRefused`'s answer during it, then try a melee stroke and a double A under replay. Closes when the log shows
   both refused with the head still tracked. If the native admission lets one through, give `ActionSceneAllowed`
   (`src/mod/Mod.cpp:1067`) an ME3 fact rather than ME1's track test.
4. **Mounted turrets**: read the game mode and the pawn's class at one (Rannoch); confirm arm IK, holsters, melee and
   the power wheel stand down. If the mode is 0, the on-foot gates need the pawn test Halcyon already uses.
5. **Focus pause** (VEH-35, ledger G45): the check in the table above; if ME3 freezes, move `game/FocusPause` out of
   its LE1 guard for both sequels.
6. **The vignette in the Atlas** (VEH-07): feed `vr::comfort::VehicleMotion` from the Atlas pawn's velocity and yaw in
   `game/Comfort` behind `engine::adapter::Mode::Atlas`; closes with the `vignette:` line while the Atlas turns.

# Citations

- [ME1 catalog: vehicles](../../me1/vehicles.md); [ME2's audit](../me2/vehicles.md); [How to catalog](../../how-to-catalog.md)
- [docs/trilogy-plan.md](../../../trilogy-plan.md) rule 10, T35, T36; [docs/le3-port-implementation-plan.md](../../../le3-port-implementation-plan.md);
  [docs/le3-port-reuse-audit.md](../../../le3-port-reuse-audit.md); [docs/me3-overall-goal-handoff.md](../../../me3-overall-goal-handoff.md)
- [docs/trilogy-parity-ledger.md](../../../trilogy-parity-ledger.md) G44, G45
- [research/le3-traversal-adapter.md](../../../../research/le3-traversal-adapter.md),
  [research/le3-native-gui.md](../../../../research/le3-native-gui.md),
  [research/le3-melee-audit.md](../../../../research/le3-melee-audit.md),
  [research/44](../../../../research/44-mako.md)
- The sibling audit: [body and IK](body-and-ik.md)
