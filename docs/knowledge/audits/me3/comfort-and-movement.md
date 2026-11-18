---
type: Audit
title: ME3 against ME1's comfort and movement
description: Contract by contract, how LE3 turns, walks, sprints, crouches and takes cover in VR against ME1's catalog; what is ME1's shared code behind ME3's native admission, what is an adapter, and what never came across (linear walking, the vehicle vignette, the focus pause, leaving a cover already taken).
tags: [audit, me3, parity, comfort, movement, turning, sprint, crouch, cover]
status: draft
timestamp: 2026-11-18T10:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:00:00Z }
game: LE3
against: ../../me1/comfort-and-movement.md
commit: 583591c6
---

# Verdict

Audited against [ME1's comfort and movement catalog](../../me1/comfort-and-movement.md) by reading code and notes at
583591c6; no game was run. "Verified" below is what a research note or the ledger records, never this audit.

- ME3 runs ME1's modules here (`game/Comfort`, `Sprint`, `Posture`, `VrCover`, `CameraShake`, the host's `Vignette` and
  `PadMerge`), 23 of 33 contracts by the shared rule. What ME3 adds is one admission read (`player::MovementRefused`)
  in front of each: the shared rule runs, but under more gates than ME1's.
- **Walking itself is the game's third-person walk**, as on ME2: `game/CombatStance` is `MEVR_GAME_LE1`-only, so
  MOV-14 to MOV-17 are missing while `LE3MEVR.ini` says `linearWalk = 1`, `linearStick = 1`, `startStopAnims = 0`.
- **The comfort turn's camera-mode gate is ME1's five class names, and ME3 is known to leave them several times a
  fight.** Halcyon's own ME3 notes measured `SFXCameraMode_Melee`, `_Roll`, `_HitReaction` and `_EnterCover` for about
  half a second each. With snap turn chosen, each hands the right stick back to the game's smooth turn (MOV-06).
- **Where ME3's admission fails, the press falls through to the game's own behaviour**: the sprint button while
  crouched, in a custom action or with the storm disabled is the game's storm with its camera (MOV-18); a cover the
  player is already in is never left (MOV-28); the physical crouch needs host focus and a fresh link (MOV-23).
- VR cover protects only behind an authored cover slot within reach (MOV-29); ME1 and ME2 protect behind any surface.

| rating | count | ids |
|---|---|---|
| same | 12 | MOV-01, 02, 03, 04, 05, 07, 08, 20, 22, 24, 26, 32 |
| same core, other inputs | 8 | MOV-06, 09, 10, 12, 13, 18, 19, 29 |
| adapter | 3 | MOV-23, 27, 30 |
| reinvented | 0 | |
| partial | 1 | MOV-21 |
| missing | 6 | MOV-11, 14, 15, 16, 17, 28 |
| n/a | 1 | MOV-33 |
| unknown | 2 | MOV-25, 31 |

# Contract by contract

| id | rating | how ME3 gets it | where it differs | evidence |
|---|---|---|---|---|
| MOV-01 | same | `[Comfort] turn = game` from the same block; the rate logged per camera mode through `kEveryGame` handles | none; `DecoupledPitch` is pinned on as ME1's | `src/game/Comfort.cpp:195`, `:308`; `src/halcyon/le3/me2_menu.cpp:516` |
| MOV-02 | same | `vr::comfort::StepSnap`, the same reader and menu row (not greyed) | never seen in an ME3 run (ledger G17 U) | `src/game/Comfort.cpp:341`, `assets/menu/main.rml:164` |
| MOV-03 | same | same code | as MOV-02 | `src/game/Comfort.cpp:345` |
| MOV-04 | same | `plugin/PadMerge` is in the LE3 host; the flags ride the pad options | none read | `CMakeLists.txt:1153`, `src/game/Comfort.cpp:403` |
| MOV-05 | same | `Turn` with `Engine.Actor.SetRotation` through `runtime::Call`, `view_anchor::NoteTurn` | the 2-degree "pawn faces the controller" rule is untested against ME3's pawn | `src/game/Comfort.cpp:225` |
| MOV-06 | same core, other inputs | the same `Why` chain; game mode 0 is on foot in every game | (1) `onFoot` also needs ME3's admission (no custom action, physics and cover readable, move input not ignored): forced, an authored scene keeps mode 0. (2) `vl::CameraModeAllows` is ME1's exact five names. ME3 has them, and aiming keeps `_Combat`, but Halcyon's ME3 notes (2026-08-14) measured `_Melee`, `_Roll`, `_HitReaction`, `_EnterCover` for about 0.5 s each, plus `_Atlas`: during each the mod's turn stands down, `kFlagModTurns` drops and a held stick turns smoothly by the game. Settles it: a combat fixture with `turn = snap`, the stick held through a hit, reading `comfort: ... (camera mode X)` | `src/game/Comfort.cpp:132`, `:285`, `:326`; `src/core/vr/ViewLevel.cpp:71`; `src/halcyon/le3/engine_probe.cpp:171`, `:2561` |
| MOV-07 | same | `squad_place::Holding()` | none | `src/game/Comfort.cpp:282` |
| MOV-08 | same | `plugin/Vignette` in the LE3 host, first of our quads in `SequelOverlays` | not seen wanted in an ME3 run | `CMakeLists.txt:1173`, `src/plugin/SequelOverlays.cpp:85` |
| MOV-09 | same core, other inputs | same `VignetteDrive` | `inPlay` uses the admitted `onFoot`: no vignette while a custom action runs or move input is ignored, although the game's own turn still turns the view under `turn = game` | `src/game/Comfort.cpp:285`, `:387` |
| MOV-10 | same core, other inputs | same | the same extra gate | `src/game/Comfort.cpp:391` |
| MOV-11 | missing | `mako::CurrentMotion()` is filled only by `game/Mako`, `MEVR_GAME_LE1` | ME3's vehicle mode is 1 and its Atlas mode 2 (`GameAdapter` knows both); nothing measures either, so no vignette in an Atlas or a turret | `src/game/Mako.cpp:632`, `src/game/Comfort.cpp:385`, `src/engine/GameAdapter.hpp:58` |
| MOV-12 | same core, other inputs | ME3's `MoveFollowsHead` defaults to 1 and the fresh `MELE3VR.ini` says 1 | it is read from the file and not pinned (ME2 forces it on at every load). The menu assumes it is on (`followsHead = true`) and its row writes only `[Comfort] walk`: a file with `MoveFollowsHead=0` gives the removed "body" walk, shown as "Where I look", and picking that chip does not fix it | `src/halcyon/le3/me2_menu.cpp:542`, `src/plugin/MainMenuSettings.cpp:300`, `:1082`; `src/halcyon/le2/me2_menu.cpp:585` |
| MOV-13 | same core, other inputs | `WalkTowardHand` in the shared pad merge, taking ME3's head-look yaw off first | `gHandWalk` uses the admitted `onFoot`. Not run on ME3 | `src/game/Comfort.cpp:380`, `src/plugin/RendererLe3.cpp:117` |
| MOV-14 | missing | nothing: `game/CombatStance` compiles to stubs outside LE1 | where ME3's ramps live is not recorded. `LE3MEVR.ini` still says `linearWalk = 1` | `src/game/CombatStance.cpp:55`, `:1349`; ledger G18 |
| MOV-15 | missing | nothing | the rule's output call and ME3's move function were not looked for | `src/game/CombatStance.cpp:55` |
| MOV-16 | missing | nothing | whether ME3 has start, stop and skid flags is not recorded | `src/game/CombatStance.cpp:55` |
| MOV-17 | missing | nothing | only matters with linear walk off | `src/game/CombatStance.cpp:55` |
| MOV-18 | same core, other inputs | one module: `StormOn` runs, `bWantsToStorm` withdrawn in both hooks' post-handlers. Run on Save 31: withdrawn, x1.12 (ledger G58 V) | the withdrawal needs ME3's admission at the press and every tick: walking physics, out of cover, **not crouched**, no custom action, move input taken, alive, `IsStormDisabled` false, host controls fresh within 100 ms. When any fails the press is "the game's own sprint": the storm posture and camera ME1 never shows. A player crouched for real who holds the button is the plain case. `bStorming` is read on the pawn | `src/game/Sprint.cpp:72`, `:119`, `:261`, `:53` |
| MOV-19 | same core, other inputs | `SpeedFactor` over `StormSpeed` and `Pawn.GetMaxSpeed` | no `CombatStormSpeed` on ME3: one sprint speed drawn or holstered | `src/game/Sprint.cpp:326` |
| MOV-20 | same | `vr::sprint::Step`, the same keys | none | `src/game/Sprint.cpp:294` |
| MOV-21 | partial | ME2's branch: the camera's two shake modifiers get `bDisabled` | on or off only: the menu's Low chip equals Off. Which of ME3's shakes pass through them is `[verify]`; not run in ME3 (ledger G11 U). The row's text names the Mako | `src/game/CameraShake.cpp:190`, `:237`; `assets/menu/main.rml:180` |
| MOV-22 | same | `SFXCameraMode.bIsCameraShakeEnabled` cleared on the current mode, game mode 0 | the substring filter (Explore, Combat, TightAim) is ME1's: ME3's `_Roll`, `_HitReaction`, `_Melee` keep their body shakes. Whether that is wanted is undecided; no ME3 log line on record | `src/game/CameraShake.cpp:288`, `:333` |
| MOV-23 | adapter | the same detector and thresholds; the call is `Engine.Pawn.ShouldCrouch` | more gates than ME1's "VR on foot": host controls fresh, focused, no menu, native movement admitted, no cover, not storming. A set `bWantsToCrouch` is not proof ME3's player pawn crouches (ME3 has no crouch of its own outside cover); never run (ledger G40 U) | `src/game/Posture.cpp:164`, `:364`, `:926`; [le3-locomotion-audit](../../../../research/le3-locomotion-audit.md) |
| MOV-24 | same | `CrouchArbiter` shared, with `ResetTracking` for ME3's generations | whether ME3 has any button crouch for the arbiter to keep is `[verify]`; the menu hides "L3 still toggles crouch" on the sequels | `src/game/Posture.cpp:913`, `assets/menu/main.rml:178` |
| MOV-25 | unknown | ME1's seam (`ConstantFpEye`, `crouchDrop`) is LE1's bridge only; on ME3 the floor lock (shared, in the LE3 host) sets the eye over the pawn's floor | whether ME3 lowers the view a second time under a physical crouch is not measured. Settles it: crouch for real and read `posture: ... after the crouch: the view is N units above Shepard's origin` | `CMakeLists.txt:1153`, `src/game/Posture.cpp:430`, `src/game/Player.cpp:163` |
| MOV-26 | same | `body_calibration::Position` shared | calibration never run with a real profile on ME3 | `src/game/Posture.cpp` |
| MOV-27 | adapter | `SFXGameModeDefault.FindCover`'s answer inside the mode's `TryAcquireCover` made false; the cover prompt hidden by `SFXGUIInteraction.HideCoverWidget`. One rule for the three games since lane FX3 (`ik::posture::RefuseCoverSnap`) | forced: cover acquisition belongs to the mode. Not seen in a fight (Save 31 has no cover; ledger G39 U) | `src/game/Posture.cpp:316`, `:439`, `:922` |
| MOV-28 | missing | `TickCover` returns before the exit logic: "existing native cover is never forcibly exited" | a cover taken before the rule applied or by a script stays the game's third-person cover state. ME3's `TryExitCover` has no force parameter and its own latch; no fallback was built | `src/game/Posture.cpp:479`; [le3-locomotion-audit](../../../../research/le3-locomotion-audit.md) |
| MOV-29 | same core, other inputs | `vr::cover::Judge` decides concealment; `[Cover]` from the same block | then ME3 needs a live authored `CoverLink` slot, enabled and not unsafe, within `reachM` of the pawn: a crate without slots gives no protection ("physical judgment alone never grants immunity"). Also gated by the snap refusal and the physical-movement admission (host controls fresh) | `src/game/Le3Cover.inc:115`, `:142`, `:149` |
| MOV-30 | adapter | answers inside `SFXPawn_PlayerParty.ModifyDamage`; a real slot is borrowed for that call only | another damage function and another way to give the facing; the same "inside one call only" rule | `src/game/VrCover.cpp:53`, `src/game/Le3Cover.inc:210` |
| MOV-31 | unknown | `game/FocusPause` is `MEVR_GAME_LE1`-only | nobody checked whether ME3 freezes without focus (trilogy plan T36). `LE3MEVR.ini` carries `[Game] pauseUnfocused = 0`. Settles it: alt-tab on the fake headset and read `WorldInfo.bPlayersOnly` with `mevr props` | `src/game/FocusPause.cpp:17` |
| MOV-32 | same | `LegsFollow` with `Engine.Actor.Velocity` | built, not seen (ledger G13: U for the legs) | `src/game/BodyFollow.cpp:384` |
| MOV-33 | n/a | ME1 has none; ME3 has its own traversal adapter (ladder, vault, climb, evade; requests off by default) | ME3's own feature, outside this catalog | `src/game/Traversal.cpp:98`, `src/game/Le3Traversal.cpp` |

# Against ME2

| ME2 finding | on ME3 |
|---|---|
| Walking is the game's third-person walk (MOV-14 to 17) | **same defect**, same cause, same dead keys in the file |
| The comfort turn never seen working; the gate lists ME1's camera-mode names (MOV-06) | **same gate, and on ME3 the answer is partly known**: the five names exist, and transient cameras outside the list are measured in Halcyon's ME3 tree |
| The Hammerhead gets no vignette (MOV-11) | **same defect** for ME3's Atlas and turrets |
| Camera shake's Low equals Off (MOV-21) | **same defect** (one `#if` for both sequels) |
| The focus pause never checked (MOV-31) | **same** |
| The crouch's view drop not measured (MOV-25) | **same** |
| `MoveFollowsHead` forced on (MOV-12) | **different, worse**: ME3 reads the file |
| Sprint left alone in the game's own cover (MOV-18) | **different**: ME3 leaves the sprint to the game under nine conditions, crouched among them |
| Cover exit with `bForceExit` (MOV-28) | **different**: ME3 never leaves a cover already taken |
| VR cover behind any surface, plus the shield-recharge rule (MOV-29) | **different**: ME3 needs an authored slot; no recharge rule (ME3 has no such mechanic in the code) |
| Mechanism 1 (a gate in ME1's terms) | **present**: `ViewLevel.cpp:71`, `CameraShake.cpp:288`, and `Player.cpp:41` `gCrouched` still declared without `kEveryGame` |
| Mechanism 2 (half a feature) | **fine here**: `Vignette`, `PadMerge`, `FloorLock` and `TraversalCue` are in the LE3 host's list |

# Reinvented

Nothing in this category reimplements a shared module. Three places are close to it:

| what | ME3's code | what exists | what differs | what unifying takes |
|---|---|---|---|---|
| "May the player move now" | `NativeLocomotionAllowed` ([Comfort.cpp](../../../../src/game/Comfort.cpp)), `NativeAllowed` ([Sprint.cpp](../../../../src/game/Sprint.cpp)), `NativeMovement` and `HostMovement` ([Posture.cpp](../../../../src/game/Posture.cpp)) | `player::MovementRefused` ([Player.cpp](../../../../src/game/Player.cpp)), already the one read | three callers each pick their own facts and add their own host-freshness test (100 ms in two of them); ME1 and ME2 have no such gate at all | one named admission per purpose in `game/Player`, compiled for every game (always true where the game needs none), so a module has one code path |
| Camera-mode class-name lists | `vl::CameraModeAllows` ([ViewLevel.cpp](../../../../src/core/vr/ViewLevel.cpp)), `FirstPersonClass` ([CameraShake.cpp](../../../../src/game/CameraShake.cpp)), Halcyon ME3's `g_fpDefs` ([engine_probe.cpp](../../../../src/halcyon/le3/engine_probe.cpp)) | three lists of the same family, one exact, one by substring, one in the renderer | the renderer's notes hold the list of ME3's transient cameras; the mod's gates were never told | a per-game table in `engine/GameAdapter.hpp` with a ReflectCheck row per name |
| Traversal | [Le3Traversal.cpp](../../../../src/game/Le3Traversal.cpp), 629 lines beside `Le2Traversal.cpp` | `core/vr/Traversal` (shared rules and lifecycle), `game/Traversal` (one lifecycle and command surface) | two game adapters under one front; by the note the shared lifecycle is used. Outside this catalog (MOV-33); named so the next audit looks | nothing here |

# Rediscovered problems

| problem on ME3 | already in ME1's list | sources | cost |
|---|---|---|---|
| The no-snap refusal failed open: it also asked for host controls and native movement, so a cover press during a wheel, at a roll's end or after a 100 ms hitch snapped Shepard in | MOV-27's gate: "decided once a game tick; nothing else lifts it" | [trilogy-first-person](../../../../research/trilogy-first-person.md) "Lane FX3" (2026-11-17), commit 8584e117 | found by an adversarial review, not by play; the same admission still gates the crouch, the sprint and VR cover |
| ME3's sprint adapter was first written on the skip-handler mechanism (2026-11-15) | "The sprint button's exec reaches the script through ProcessEvent, so a CallFunction skip never ran" (met on ME2's cover on 2026-11-14) | [le3-locomotion-audit](../../../../research/le3-locomotion-audit.md) "enabling the existing skip-handler mechanism"; [gotcha](../../gotchas/binding-execs-come-through-process-event.md); [trilogy-sprint](../../../../research/trilogy-sprint.md) | replaced a day later by the unified sprint (lane GR) |
| The script frame's `Node` offset | "A skip handler read `Node` at the wrong offset" | [gotcha](../../gotchas/fframe-packed-on-four-bytes.md): 0x18 only in LE3 | not paid again: `engine::adapter::kFrame` carries it |
| Upstream ME3 went third person in cover | (ME1 and ME2 never do; no list entry) | commit 8584e117; `le3 me2_menu.cpp:500` | pinned 2026-11-17 |

No note records ME3 meeting the linear-walk problems, because nothing was built: the first headset report of "the walk
feels like the third-person game" would be ME1's of 2026-11-12 a third time ([research/38](../../../../research/38-combat-stance.md)).

# Forced by the game

- Mode 0 kept through authored scenes that ignore input (MOV-06's admission): `src/game/Comfort.cpp:129`.
- Cover acquisition on the game mode, `TryExitCover` without a force parameter, `SFXPawn_PlayerParty.ModifyDamage`
  indexing a real slot array before the direction query (MOV-27, 28, 29, 30):
  [le3-locomotion-audit](../../../../research/le3-locomotion-audit.md) "Native cover differs materially".
- `bStorming` on the pawn, no `CombatStormSpeed`, `IsStormDisabled` (MOV-18, 19).
- Shakes as camera modifiers (MOV-21), as ME2.
- Transient action cameras (roll, hit reaction, melee, enter cover) exist in ME3; that the turn's gate excludes them is
  ours.

# Not forced

- Linear walk, linear stick and the start and stop steps left unported (MOV-14 to 16).
- The camera-mode gate's ME1 list against ME3's known transient cameras (MOV-06, MOV-22).
- `MoveFollowsHead` unpinned while the menu assumes it (MOV-12).
- The sprint press falling to the game's storm when the player is crouched (MOV-18): ME1 lets `StormOn` decide and
  withdraws whatever it set.
- A cover already taken never left (MOV-28): no exit was tried, forced or not.
- The vehicle vignette (MOV-11), the Low chip (MOV-21), the unchecked focus pause (MOV-31).
- Dead keys in `LE3MEVR.ini` (`linearWalk`, `linearStick`, `startStopAnims`, `exploreRamp`, `crouchDrop`,
  `pauseUnfocused`).

# What to do

1. **Port linear walking to the sequels** (MOV-14, 15, 16), one change for ME2 and ME3. Files:
   `src/game/CombatStance.cpp` (the ramp and stick section split from the LE1 stance code; per-game names in
   `src/engine/GameAdapter.hpp`), ReflectCheck rows. Check: `movement: response:` under a
   `make-walk-recording.py -Inputs` replay: a run within 0.1 s of the push, half a push near 1.7 m/s.
2. **Decided and built 2026-11-18 (lane AU2; the owner: the snap turn stays the player's through a roll):** `TurnCameraAllows`
   in game/Comfort takes ME3's action cameras too. Was: **decide the turn's gate for ME3's transient cameras** (MOV-06). Files: `src/core/vr/ViewLevel.cpp:71` (a per-game
   list). Check: the run named in MOV-06's row; no `comfort: ... camera mode` line while a snap-turn player is hit.
   **Owner question:** during a roll or a hit reaction, should the mod keep the turn, or should nobody turn?
3. **Pin `MoveFollowsHead`** (MOV-12), with the settings audit's step 4.
4. **Sprint while crouched** (MOV-18). Files: `src/game/Sprint.cpp:76`. Check: a replay that crouches, then holds the
   sprint button: `sprint: running fast` and no `_CombatStorm` or `_ExploreStorm` camera mode in `comfort:`'s line.
   **Owner question:** stand up and run fast (ME1's order: the crouch waits for the sprint), or refuse the sprint?
5. **Run the checks nobody ran**: the comfort row (two flicks, a hand-yawed walk), a real crouch with the view's
   height read (MOV-23, 25), the focus pause (MOV-31), a fight with cover for MOV-27 to 30
   ([le3-combat-fixtures](../../../../research/le3-combat-fixtures.md)'s first check). Each moves a ledger cell (G17,
   G39, G40, G45).
6. **A cover already taken** (MOV-28): try ME3's own `TryExitCover` as A does, with ME1's 0.25 s retry and no forced
   fallback. Check: `mevr cover put` then `posture:` logs the exit.
7. **Vehicle motion from the game mode** (MOV-11), with ME2's step 3: `engine::adapter::GameMode` gives Vehicle and Atlas.
8. **Hide the Low chip on the sequels and drop the Mako from the row's text** (MOV-21): `assets/menu/main.rml:180`.

# Citations

- Catalog: [ME1 comfort and movement](../../me1/comfort-and-movement.md); method: [how to catalog](../../how-to-catalog.md);
  ME2's audit of the same file: [comfort and movement](../me2/comfort-and-movement.md), [summary](../me2/summary.md);
  the sibling audit: [settings and defaults](settings-and-defaults.md).
- Ledger: [trilogy parity ledger](../../../trilogy-parity-ledger.md) rows G11, G13, G17, G18, G39, G40, G45, G58. This
  audit agrees with G18 and G45 (missing). It reads G58's LE3 "shared V" as shared only while ME3's admission holds
  (MOV-18), G17's "adapted U" as carrying an ME1 name list ME3 is known to step outside, and G39's "adapted" as leaving
  MOV-28 out and narrowing MOV-29 to authored slots.
- Catalog rows read as wrong about ME1: the settings table says `[Comfort] walk` is read by "mod (the host turns the
  stick)" while the settings catalog says host; the menu's row writes it and `game/Comfort.cpp:380` reads it (mod), the
  host acts on the flag. MOV-06 does not say that the vignette and the hand walk share its on-foot test.
- Code: [Comfort.cpp](../../../../src/game/Comfort.cpp), [ViewLevel.cpp](../../../../src/core/vr/ViewLevel.cpp),
  [Player.cpp](../../../../src/game/Player.cpp), [CombatStance.cpp](../../../../src/game/CombatStance.cpp),
  [Sprint.cpp](../../../../src/game/Sprint.cpp), [CameraShake.cpp](../../../../src/game/CameraShake.cpp),
  [Posture.cpp](../../../../src/game/Posture.cpp), [VrCover.cpp](../../../../src/game/VrCover.cpp),
  [Le3Cover.inc](../../../../src/game/Le3Cover.inc), [FocusPause.cpp](../../../../src/game/FocusPause.cpp),
  [BodyFollow.cpp](../../../../src/game/BodyFollow.cpp), [Traversal.cpp](../../../../src/game/Traversal.cpp),
  [Mako.cpp](../../../../src/game/Mako.cpp), [SequelOverlays.cpp](../../../../src/plugin/SequelOverlays.cpp),
  [RendererLe3.cpp](../../../../src/plugin/RendererLe3.cpp), [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp),
  [le3 me2_menu.cpp](../../../../src/halcyon/le3/me2_menu.cpp), [le3 engine_probe.cpp](../../../../src/halcyon/le3/engine_probe.cpp),
  [main.rml](../../../../assets/menu/main.rml), [CMakeLists.txt](../../../../CMakeLists.txt).
- Research: [le3-comfort](../../../../research/le3-comfort.md), [le3-locomotion-audit](../../../../research/le3-locomotion-audit.md),
  [le3-traversal-adapter](../../../../research/le3-traversal-adapter.md), [trilogy-sprint](../../../../research/trilogy-sprint.md),
  [trilogy-first-person](../../../../research/trilogy-first-person.md), [23 cover](../../../../research/23-cover.md),
  [31 comfort](../../../../research/31-comfort.md), [38 combat stance](../../../../research/38-combat-stance.md).
- Gotchas: [binding execs come through ProcessEvent](../../gotchas/binding-execs-come-through-process-event.md),
  [FFrame packed on four bytes](../../gotchas/fframe-packed-on-four-bytes.md),
  [floor lock through a traversal move](../../gotchas/floor-lock-through-a-traversal-move.md).
