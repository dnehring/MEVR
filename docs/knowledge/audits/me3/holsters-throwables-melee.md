---
type: Audit
title: ME3 against ME1's body holsters, grenades and melee
description: Contract by contract, how ME3 reproduces ME1's holster grab, "only holsters draw", the grenade slot and the melee gesture, every input that acts differently on ME3, and which of ME2's findings recur.
tags: [audit, me3, parity, holsters, melee, gestures]
status: draft
timestamp: 2026-11-18T10:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:00:00Z }
game: LE3
against: ../../me1/holsters-throwables-melee.md
commit: 583591c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/holsters-throwables-melee.md) by reading code and notes at `583591c6`; no
game was run. Line numbers are that commit's. ME3 has no combat fixture yet
([research/le3-combat-fixtures.md](../../../../research/le3-combat-fixtures.md)): nothing in this category has been
seen in a fight on ME3, so every row below is from reading unless it says "run".

- The slots, the grab machine, the frame and the haptics are ME1's code on ME3 (10 `same`, 8 `same core, other
  inputs`, 4 `adapter` of the 28 contracts that apply). The ten grenade contracts are `n/a`: ME3's grenades are powers.
- **Finding 1 (fixed 2026-11-18, lane AU2: `le3PutAway = 1` by default after the owner's answer, run on the combat fixture;
  research/le3-combat-fixtures.md "A combat fixture by console").** Was: on ME3 the holsters draw but do not holster.
  `[Holsters] le3PutAway = 0` shipped, so letting go of the
  right grip does nothing, at the slot or away from it. ME1's central contract (HOL-11: the grip holds the gun, let go
  and it goes back) is half there, and "only holsters draw" (HOL-15, HOL-16) has no state to act in: a grab at a slot
  is a weapon switch, not a draw. Forced in part (ME3 has no holstered state on a mission); the put-down that would
  give ME1's behaviour exists and has never been run.
- **Finding 2: ME3's melee gesture is a second rule set with other inputs, not ME1's.** `Rules::Staged` needs every
  control of the striking hand released (trigger and grip under 0.15, no button), so the hand that grips the gun as on
  ME1 never strikes and there is no fist; it ignores `[Throwables] meleeSpeedMS`, `meleeGunSpeedMS` and `meleeTravelM`
  (the detector's built-in 3.0 m/s and 0.12 m for both hands); and an open hand swung with a gun out is a melee, which
  on ME1 needs no open hand. The owner approved "stroke melee on by default as on ME1 and ME2" (2026-11-17); the rule
  set that runs is not the one ME1 and ME2 run.
- **Finding 3: the enemy-in-reach cone on ME3 is measured from tracking-space forward, not from the body.**
  `FindEnemy` uses `gYaw`, which only `StepPhysical` sets; ME3 never calls it, so `gYaw` stays 0. A player who has
  physically turned in the room has the 75 degree cone pointing where the room's forward is. Read from code; one run
  settles it (see What to do 2).
- The ledger's G08 and G43 LE3 cells ("adapted, U") are right about the code. They do not show Finding 3, nor that the
  gesture's numbers on ME3 are not the settings', nor that a heavy weapon in hand may have no controller route away
  from it (HOL-20).

| rating | count | ids |
|---|---|---|
| same | 10 | HOL-01 02 03 04 06 07 09 13 14 34 |
| same core, other inputs | 8 | HOL-05 10 19 31 32 35 36 38 |
| adapter | 4 | HOL-08 16 17 21 |
| reinvented | 0 | |
| partial | 5 | HOL-11 12 15 33 37 |
| missing | 0 | |
| n/a | 10 | HOL-18 22 23 24 25 26 27 28 29 30 |
| unknown | 1 | HOL-20 |

# Contract by contract

`Holsters.cpp` is [src/game/Holsters.cpp](../../../../src/game/Holsters.cpp), `Melee.cpp` is
[src/game/Melee.cpp](../../../../src/game/Melee.cpp), `core Melee.cpp` is
[src/core/ik/Melee.cpp](../../../../src/core/ik/Melee.cpp), `Throwables.cpp` is
[src/game/Throwables.cpp](../../../../src/game/Throwables.cpp), `Mod.cpp` is
[src/mod/Mod.cpp](../../../../src/mod/Mod.cpp), `HandCalibration` is
[src/game/HandCalibration.cpp](../../../../src/game/HandCalibration.cpp) and
[.hpp](../../../../src/game/HandCalibration.hpp). "Run" means a fake-headset run on Save 31 (the Normandy, a
non-combat pawn that carries no weapon) recorded in the ledger or the trilogy plan.

| id | rating | how ME3 gets it | where it differs | evidence |
|---|---|---|---|---|
| HOL-01 | same | The shared tick: `NeutralEye`, `Anchor::Update`. | Nothing. | Holsters.cpp:1328-1344 |
| HOL-02 | same | Arm IK's torso yaw, else the lagging yaw, held while `Machine::Busy()`. | Nothing. | Holsters.cpp:1332-1343 |
| HOL-03 | same | `slots::Scale`, `SlotCenter`. | Nothing. | Holsters.cpp:1346-1368 |
| HOL-04 | same | `UpdateEyeHeight`, ME1's order of sources. | Not seen on ME3 with a weapon carried. | Holsters.cpp:1282 |
| HOL-05 | same core, other inputs | ME1's four slots and numbers; what is carried is the inventory chain by weapon family (`le2_squad::Weapons`), ME2's reader. | A fifth slot, the SMG at `thighRight` (0.08, -0.90, 0.15) r 0.12 or `lowerBack`, as ME2. A heavy weapon has no slot. | HandCalibration.hpp:189-201; HandCalibration.cpp:292-300; Holsters.cpp:630-650 |
| HOL-06 | same | `Machine::Update`. | As ME2: only with `smgSlot = lowerBack` a lost hand keeps its place 0.75 s. | Holsters.cpp:1296-1307 |
| HOL-07 | same | The host's count of kept squeezes, our latch 0.6 / 0.4. | Nothing. | Holsters.cpp:1208-1225 |
| HOL-08 | adapter | `BioPlayerController.SwitchWeapon(oWpn)` alone (ME3 has no action state to raise); ME1's click and 3 s follow. | **A grab is a weapon switch, never a draw from a holster**: on a mission a gun is always in the hand, so a squeeze in another weapon's slot swaps guns. With the held gun's own slot it is `AlreadyHeld`. No quick slots. Not run with a weapon. | Holsters.cpp:109-116, 654, 705-762 |
| HOL-09 | same | `CombatEnabled` at the shared top of `Draw`, ME1's refusal buzz. | Nothing; not run on ME3 with a carried weapon (Save 31 carries none). | Holsters.cpp:676-689, 716-722 |
| HOL-10 | same core, other inputs | The machine's `Unavailable`, `Missed`, `AlreadyHeld`. | One more refusal: `SwitchRefused` while `SFXHeavyWeapon` is in hand (a switch would throw it away). The owner's answer to question 13. Unit-tested, not run. | Holsters.cpp:134-138, 655-664, 1470-1476 |
| HOL-11 | partial | `gripHolds` and the pad mapping run (the right grip is never RB on foot and in ME3's two wheels, modes 0, 3, 4 through `IsFootOrWheelMode`). | **Letting go never holsters by default**: `le3PutAway = 0` sets `input.holster = Off`; no grace runs, no click. With `le3PutAway = 1` the holster is `SFXInventoryManager.SetCurrentWeapon(None)`, never run. `TryHolster` is never called on ME3 (it only leaves cover). | Holsters.cpp:769-782, 1396-1408; HandCalibration.cpp:313-316; [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp):56-62, 74-77 |
| HOL-12 | partial | The machine's Returning rules are shared; "switching" is the weapon's state `WeaponEquipping` / `WeaponPuttingDown`. | Unreachable by default (HOL-11). | Holsters.cpp:666-667 |
| HOL-13 | same | Mode 0, a valid view, tracked controllers, a valid right grip, no calibration; `Up()`. | ME3's Atlas (mode 2) and injured Shepard (mode 6) are not mode 0: the slots are off there, the grip is RB. | Holsters.cpp:391, 1287-1313 |
| HOL-14 | same | `Contact`, `EnterEdge`, `Depth`. | Nothing. | Holsters.cpp:1519 |
| HOL-15 | partial | ME2's deny-list: `IsCombatEnabled` answered false inside `FireWeapon`, `TightAim`, `TryReload`, `NextWeapon`, `PrevWeapon` while "away". | "Away" on ME3 is "carries weapons, none equipped", which only `le3PutAway`'s put-down produces. By default the rule never acts. The weapon wheel and any other switch route are outside the list, as on ME2. | Holsters.cpp:152-154, 990-996, 1156-1175 |
| HOL-16 | adapter | The first two routes are refused without a buzz. | By default the gun is always out and the trigger fires. With no weapon equipped ME3's trigger fires nothing by itself (`HandleWeaponFiring`). | Holsters.cpp:154, 1013-1014 |
| HOL-17 | adapter | Powers are not among the five routes. | No allowed-caller list, no "let through" line (as ME2). | Holsters.cpp:151-152 |
| HOL-18 | n/a | `AllowGameDraw` has no caller on ME3. | — | Holsters.cpp:1026-1032 |
| HOL-19 | same core, other inputs | `SeatedLayout`, ME1's four seated points. | Plus the SMG's seated slots; `seatedGrenade` is written and read by nothing that runs. | HandCalibration.cpp:302-312 |
| HOL-20 | unknown | ME1's seated wheel routes are inside `MEVR_GAME_LE1`. | ME3's weapon wheel is never refused. Open: whether it can be reached at all on foot in VR. The left trigger is the squad's (the game sees no LB), X is the hand power wheel's, and with a heavy weapon in hand the slot grabs are refused "the game's weapon wheel switches away" (Holsters.cpp:659-661). Run: on a combat save pick up a heavy weapon, then `mevr calltrace 10 enterweaponwheel switchweapon nextweapon` while pulling the left trigger with and without a squad. | Holsters.cpp:966-985, 1038-1042; [SquadWeapons.cpp](../../../../src/game/SquadWeapons.cpp):1697-1703 |
| HOL-21 | adapter | ME1's `TickPowerHold`: `SFXGameModeBase.EnterPowerWheel` after 300 ms, `ExitPowerWheel` at the release or when the wheel opens late (ME3 retries by a 0.2 s timer in a transition). | The click is kept from the game as R3 (question 14; ME3's R3 is cover turn, point of interest, area map). Run on Save 31: no area map. The wheel's opening is not run (combat is disabled there). | Holsters.cpp:163-171, 467-533; [PadMerge.cpp](../../../../src/plugin/PadMerge.cpp):152 |
| HOL-22 | n/a | ME3's grenades are powers ([research/le3-powers.md](../../../../research/le3-powers.md)); `throwables::Tick` is `Inactive("LE1 only")`. | A squeeze at the left chest claims nothing. A grenade is a tile on the power wheel, thrown by the left hand's push (see the powers audit, PWR-14). | Throwables.cpp:1078-1081 |
| HOL-23 | n/a | `gActive` is never true. | — | Throwables.cpp:851 |
| HOL-24 | n/a | The grenade count is the power's (`EvaluateNativePower`: zero dims the tile). | — | [NativePower.hpp](../../../../src/core/vr/NativePower.hpp):37-43 |
| HOL-25 | n/a | — | — | |
| HOL-26 | n/a | The release estimator is shared core, unused. | — | |
| HOL-27 | n/a | — | — | |
| HOL-28 | n/a | — | — | |
| HOL-29 | n/a | X is the hand power wheel's on foot (`HandWheelOwnsX`). | ME3's X is reload (tap) and weapon swap (hold): both gone on foot. The owner: "automatic reload for now" (answered for "the sequels", checked on ME2 only). | [FootControls.hpp](../../../../src/core/vr/FootControls.hpp); [trilogy-plan.md](../../../trilogy-plan.md) "Answered 2026-11-17" |
| HOL-30 | n/a | The module is out; the on-foot gate for B is `melee::SequelOnFoot`, for the strokes `ActionSceneAllowed`. | Two more copies of the gate (Reinvented 1). | Melee.cpp:981-1009; Mod.cpp:1067-1076 |
| HOL-31 | same core, other inputs | ME1's `SwingDetector` inside `Rules::Staged`. | `Le3Tick` calls `StepAll(input)` with no parameters: the detector's defaults (3.0 m/s, 0.12 m, 0.8 s) for both hands, not `[Throwables] meleeSpeedMS / meleeGunSpeedMS / meleeTravelM` (ME1's gun hand needs 3.5). A hand must first be seen "neutral" (all its controls released) after any interruption; one request a step; the clock is the controller sample's `timeMs`, fed every tick (ME1: new samples only). | Melee.cpp:1057-1102 (1079); core Melee.cpp:101-158; Mod.cpp:1167-1184 |
| HOL-32 | same core, other inputs | The staged link's `available`: not in a slot, not just drawn, no support grip, no claimed squeeze. | Another list with other numbers, built in `Mod.cpp`, not ME1's in `PhysicalStrokes`: 0.3 s after a draw (ME1 0.5); also refused while squad placement holds, the power wheel owns the left hand, a traversal move runs, and **whenever the hand's trigger or grip is above 0.15 or any of its buttons is down** (ME1: the trigger only, and only with the weapon out). A refused hand starts over (ME1: the stroke is spent, history kept). | Mod.cpp:1077-1098, 1173-1183; core Melee.cpp:128-137 |
| HOL-33 | partial | Weapon out: `SFXGameModeDefault.TryMelee` through the shared `Strike`. | **No fist** (`fist` is always false; `DispatchFist` refuses; the admission needs a drawn `SFXWeapon`). **The gripping gun hand never strikes** (HOL-32), where on ME1 the gun hand strikes while it holds the grip. A heavy melee (`TryHeavyMelee`) exists behind the opt-in chamber gesture (question 7, open). | Melee.cpp:653-654, 708-710, 761-764, 1094; Mod.cpp:191-200 |
| HOL-34 | same | The shared `Strike`, ME1's order of reasons. | `Busy` is ME3's whole admission (movement input, combat input, cover, storm, a running custom action, reload, zoom, weapon state). | Melee.cpp:634-665, 863-906 |
| HOL-35 | same core, other inputs | `MeleeWait` with `meleeCooldownS`. | No game cooldown is read (ME3's melee is a custom action, `FindMeleePower` returns 0). Two waits stack: the staged rules' own 0.8 s from the request (the code default, not the setting) and `Strike`'s `meleeCooldownS` from the strike. | Melee.cpp:720-722, 889-893, 907; core Melee.cpp:149-152 |
| HOL-36 | same core, other inputs | The shared `FindEnemy`: 2.0 m, 75 degrees, 2.0 m of height. | **The cone's front is `gYaw`, which is never set on ME3** (only `StepPhysical` writes it, Melee.cpp:1139): it stays 0, the tracking space's forward. ME1 and ME2 use the body's yaw. Also the raw pawn walk with a guard of 512 (ME2's finding). Reading only. | Melee.cpp:70, 814-823, 829, 1139 |
| HOL-37 | partial | The strike's tick (`StrikeTick`). | No thump on a hit: `kWatchHits = false`, `Health` returns -1. A "readiness" pulse of its own for the armed heavy chamber. | Melee.cpp:766-770, 909, 1080-1082 |
| HOL-38 | same core, other inputs | `kFlagNoMeleeButton` while `SequelOnFoot`. Run: `melee-b.ps1 -Game LE3` (ledger G32). | ME3's B is melee (tap), heavy melee (hold) and the leave: all kept. Leave is a top-bar entry (question 16). The gate is a copy of ME1's. | Melee.cpp:1183-1206 |

# Inputs that act differently on ME3

| input | ME1 | ME3 | forced? | the owner |
|---|---|---|---|---|
| right grip let go, anywhere | the gun goes back (at once at its slot, else after 0.2 s) | nothing: the gun stays in the hand | Partly: no holstered state on a mission; the put-down exists (`le3PutAway`), unrun | not asked as a default; "0 until a combat fixture has checked it" is a lane's choice |
| right grip squeezed in another weapon's slot, gun out | draws that weapon (the same) | the same switch; it is the only thing a slot does | — | — |
| right grip in a slot, heavy weapon in hand | n/a | refused, refusal buzz | Yes: ME3 throws the heavy weapon away on a switch | approved, question 13 (a) |
| right grip at the front of the right thigh | no slot | switches to the SMG if carried; contact buzz in passing | No: the place is ours | not confirmed (ledger G08, as ME2) |
| open hand (no grip, no trigger) swung fast at an enemy, gun out | a melee (either hand; no open hand needed) | a melee (either hand) | — | approved 2026-11-17 ("stroke melee on by default") |
| right hand swung while it grips the gun | a melee (the gun hand, 3.5 m/s) | **nothing** (the grip above 0.15 makes the hand unavailable) | No | not asked |
| right fist (grip and trigger) swung, weapon away | a punch | nothing: no weapon-away state by default, no fist route | Mostly: ME3's melee wants a drawn weapon | told (ledger G43) |
| right hand held still 0.35 s in a box ahead of the right shoulder, then struck | nothing of ours | heavy melee, **only** with `mevr melee heavy-gestures on` | The game has a heavy melee; the gesture is ours | open: question 7 |
| a stroke after physically turning away from the room's forward | a melee | may be refused "no enemy within 2.0 m and 75 degrees" (HOL-36) | No: a defect, reading only | — |
| B on foot | never melee | never melee, never heavy melee, no leave | The binding is the game's | approved, question 16 |
| X on foot | power wheel | power wheel; no reload, no weapon swap | The clash is ours | "automatic reload for now" |
| right stick click, tap | nothing; R3 reaches the game | nothing; R3 kept (no area map, no cover turn) | Partly | approved, question 14 |
| left grip at the left chest | takes a grenade | falls through to the radio's and the squad order's tests | Yes: grenades are powers | — |
| left grip at the left hip, then the receiver | nothing | the reload by hand, off by default (`[Reload] enabled = 0`) | No | rejected 2026-11-16, off until redesigned |
| A, twice | nothing of ours | ME3's traversal (the vault lane's; outside this category) | — | double A approved for ME2 (2026-11-14) |

No ME3 input in this category was found that triggers a game action by a gesture ME1's player would make innocently,
except the open-hand stroke (approved) and, opted in, the chamber. The losses run the other way: ME1 gestures that do
nothing on ME3 (let go to holster, the gripping hand's strike, the fist).

# Against ME2

| ME2 finding | on ME3 |
|---|---|
| Finding 1: R3 tap cast a power, B meleed, a grab drew where combat is off (each shipped wrong, fixed 2026-11-17) | Fine: all three fixes are in the shared code and cover ME3 (Holsters.cpp:163-171, 716-722; Melee.cpp:1183). None was seen in an ME3 fight. |
| Finding 2: "only holsters draw" is a deny-list of five functions; the weapon wheel and other routes outside it | Same defect, and weaker: the rule has no state to act in by default (HOL-15). |
| Finding 3: the SMG's thigh slot not the owner's | Same defect (the shared layout). |
| A let-go gun stays out in combat (ME2's `TryHolster` refuses) | Different: on ME3 it stays out always, silently (no refusal buzz, one log line once). |
| No punch with the weapon away | Same, and wider: no fist at all, and no strike from a gripping hand. |
| X with no reload behind it | Same; ME3's hold-X weapon swap is lost too. |
| Reinvented 1: the on-foot gate and body frame exist three times | Different and worse: ME3 adds a fourth gate (`ActionSceneAllowed`, Mod.cpp:1067) and a fourth yaw (Mod.cpp:1167, never held while a hand is busy), and the dispatcher's own `gYaw` is left unset (HOL-36). |
| Reinvented 2: "drawn" and "mid-switch" read per module | Same: Holsters `held != nullptr` (654), Melee `IsA(Pawn.Weapon, SFXWeapon)` (708) and the state name twice (Holsters.cpp:666, Melee.cpp:660-663). |
| Reinvented 3: ME1's grenade keys in the sequel's ini | Same: `kThrowablesBlock`, `kVentBlock`, `seatedGrenade` are written to `LE3MEVR.ini` unchanged (HandCalibration.cpp:2962-2963). On ME3 the melee speed keys in that block are also dead (HOL-31). |
| HOL-36: the raw pawn walk, not `LivePawns` | Same. |
| HOL-31: the melee's front not held while a hand is in a slot | Same for the stroke's direction; the cone has the larger defect above. |

# Reinvented

1. **A second stroke-suppression list.** ME1's and ME2's is in `PhysicalStrokes` (Melee.cpp:946-954: firing, in a
   slot, 0.5 s after a draw, the support grip, plus Throwables' grenade rules). ME3's is `ActionHandFree` +
   `ActionControlsReleased` + `AllowMeleeGesture` in Mod.cpp:1077-1098, with 0.3 s after a draw and a blanket "no
   control touched". Both answer "may this hand strike now". Unifying: one function in `game/Melee` that both rule sets
   ask, with the staged extras (placement, power wheel, traversal) added for every game or for none.
2. **A second gate and frame for the strokes.** `ActionSceneAllowed` (Mod.cpp:1067-1076) repeats
   `melee::SequelOnFoot` (Melee.cpp:981-1009), which repeats `holsters::Tick`'s gate; the staged link computes its own
   body yaw (Mod.cpp:1167) beside `holsters::SlotFrame()` and ME2's `Le2Tick`. ME3's copy forgot to hand the yaw to the
   dispatcher (`gYaw`).
3. **Two cooldowns for one strike** (HOL-35): the staged rules' `readyAt_` and `Strike`'s `gReadyAt`. ME1's design
   note says the shared cooldown is the dispatcher's.
4. **Two rule sets at all.** `Rules::Staged` was written before the owner's "melee is ME1's fist gesture"
   (2026-11-17); ME2 was moved to `Rules::Physical` that day, ME3 kept Staged "its staged rules ... kept" (ledger G43).
   `tests/MeleeTests.cpp` proves Physical equals ME1; nothing compares Staged with ME1.

Not reinvention: `SwitchRefused` (a new fact of ME3's), `le3PutAway` (ME3's own put-down), ME3's `Refusal` admission
(the game's own tests, read before `Try*` is called: the lesson of ME2's "a lower call loses the upper call's tests").

# Rediscovered problems

| problem on ME3 | when, cost | the catalog already had |
|---|---|---|
| A grab at a slot threw the heavy weapon away (`SwitchWeapon` puts it down for good). | Found by lane FX2's review 2026-11-17, an owner question (13), fixed by lane CTL. | Not ME1's (no heavy weapon); the class is ME2's "a lower call than the game's own loses its tests". |
| The right stick click's tap cast a power and its hold opened the area map. | Shipped with the sequels' holsters 2026-11-16, fixed 2026-11-17 (`30cf473c`, `b4f1fb31`). | Catalog "Problems ME1 already solved": the RB binding casts under 0.3 s; research/19 "Hold only". |
| B was the game's melee under squad placement. | Fixed `1131f513`, 2026-11-17, for both sequels. | HOL-38. |
| ME3's `TryHolster` is not a holster (it leaves cover); the first adapter would have called it. | Caught by reading the script, lane WP 2026-11-16, before it shipped. | Catalog: "`TryHolster` is a toggle: called with the weapon away it draws": the same lesson, check what the game's function does in the other state. |
| The dispatcher's enemy cone without a body yaw (HOL-36). | Not yet found by a run; open. | Catalog HOL-36: "in front of the body (the body frame's yaw, not the head's)"; research/19 "Body frame". |
| The gesture's speeds not read from the settings (HOL-31). | Open. | Catalog HOL-31's numbers and settings table. |

# Forced by the game

- **Grenades are powers** (`SFXPower_GrenadeBase`, [research/le3-powers.md](../../../../research/le3-powers.md)): HOL-22
  to HOL-30.
- **No holstered action state on a mission**: `BioPawn.ActionState`, `m_oBehavior` and `SetActionState` are not in
  ME3's script; a gun is out whenever one is equipped (Holsters.cpp:109-116). This forces HOL-08's "a grab is a switch"
  and the default half of HOL-11, HOL-15, HOL-16.
- **`TryHolster` only leaves cover** on ME3 (Holsters.cpp:111-112).
- **A heavy weapon is thrown away on a switch** (`SFXHeavyWeapon.PutDownWeapon`, `bTossOnWeaponSwitch`).
- **Melee is a custom action, not a power**: no cooldown to read, light and heavy separate entry points, the game's
  own admission ([research/le3-melee-audit.md](../../../../research/le3-melee-audit.md)).
- **Melee needs a drawn weapon** as recorded (`Refusal`: "no supported drawn weapon"). Reading alone cannot say whether
  ME3 punches bare-handed in some state.
- **B, X and R3 carry other functions** (melee and heavy melee, reload and swap, cover turn and area map).

# Not forced

- `le3PutAway = 0` as the default: the put-down is the game's own call; what holds it back is the missing combat
  fixture, not the engine.
- The staged rules instead of ME1's physical ones: the released-controls requirement, the built-in speeds, the neutral
  priming, 0.3 s after a draw, the second cooldown.
- `gYaw` unset (HOL-36).
- No hit thump (HOL-37): `Health` could read `Engine.Pawn.Health` as `SquadContext` does on ME3.
- The SMG's thigh slot as the default.
- The grenade, vent and melee-speed keys written to `LE3MEVR.ini`.
- X as the power wheel's button on a game whose reload and swap are X (accepted "for now").

# What to do

Ordered by what a player meets first. Parity work and questions only.

1. **Get the ME3 combat fixture and run lane WP's and lane MEL's lists** before anything else is rated above "U":
   [research/le3-combat-fixtures.md](../../../../research/le3-combat-fixtures.md) "The weapon checks", "Melee on the
   first combat save". Closes when the ledger's G08 and G43 LE3 cells say V.
2. **Give the dispatcher ME3's body yaw** (HOL-36): `Le3Tick` (Melee.cpp:1057) sets `gYaw` from the link's body
   forward, or better all three games read `holsters::SlotFrame()`. Check: on the fixture, face an enemy after
   turning 180 degrees in the room (a replayed head recording), `mevr melee strike`; the log must not say "no enemy
   within ... degrees".
3. **Ask the owner about letting go on ME3** (HOL-11): with the gun always out, should a let-go gun be put away
   (`le3PutAway = 1`, ME1's behaviour) once the put-down is checked, or stay in the hand? Then run `le3PutAway = 1`:
   let go at the slot, let go away from it, the trigger with nothing equipped, and each of the five routes
   (`mevr calltrace 10 fireweapon tightaim tryreload nextweapon prevweapon`). Files: `HandCalibration.cpp:313-316`.
4. **Ask the owner whether ME3 keeps its staged rules or takes ME1's** (HOL-31 to HOL-33): today a hand that grips
   the gun cannot strike. If ME1's: feed `Rules::Physical` on ME3 as `Le2Tick` does, keeping ME3's `Refusal` in
   `Busy`. If Staged stays: pass `[Throwables]` speeds and travel into `StepAll` (Melee.cpp:1079) and say in the
   controller guide that the hand must be open. Closes with `tests/MeleeTests.cpp` covering the chosen rule on ME3's
   inputs.
5. **The heavy weapon's way out** (HOL-20): one traced run to show a controller route to the game's weapon wheel or a
   swap while a heavy weapon is held; if there is none, an owner question (question 13's answer assumed the wheel).
6. **Question 7 stays open** (the heavy chamber). No change until answered.
7. **One suppression list, one gate, one cooldown** (Reinvented 1 to 3): `Mod.cpp:1067-1098` into `game/Melee`.
   Closes with ME1 and ME2 preprocessing to the same text and the unit tests unchanged.
8. **The hit thump on ME3** (HOL-37): `Health` from `Engine.Pawn.Health` / `HealthMax`, `kWatchHits` on.
9. **Write only the keys ME3 has** to `LE3MEVR.ini`: no grenade, vent or (if Staged stays) melee-speed keys.
10. **Walk the pawn list with `LivePawns`** in `FindEnemy` (as ME2's item 8).

# Sources

- [The ME1 catalog](../../me1/holsters-throwables-melee.md); [the ME2 audit](../me2/holsters-throwables-melee.md);
  [the parity ledger](../../../trilogy-parity-ledger.md) G08, G41, G42, G43;
  [the trilogy plan](../../../trilogy-plan.md) questions 7, 13 to 16 and "Answered 2026-11-17".
- [research/le3-melee-audit.md](../../../../research/le3-melee-audit.md),
  [research/le3-weapon-ammo.md](../../../../research/le3-weapon-ammo.md),
  [research/le3-combat-fixtures.md](../../../../research/le3-combat-fixtures.md),
  [research/le3-powers.md](../../../../research/le3-powers.md),
  [research/20](../../../../research/20-melee-and-grenades.md) "The trilogy's one path",
  [research/le2-vrik.md](../../../../research/le2-vrik.md) "Holsters on the sequels".
- Code: [Holsters.cpp](../../../../src/game/Holsters.cpp), [Melee.cpp](../../../../src/game/Melee.cpp),
  [core Melee.cpp](../../../../src/core/ik/Melee.cpp), [Throwables.cpp](../../../../src/game/Throwables.cpp),
  [Mod.cpp](../../../../src/mod/Mod.cpp) (the staged link 1037-1186, the tick order 1837-1894),
  [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp).

Catalog rows found inexact about ME1: none in this file.
