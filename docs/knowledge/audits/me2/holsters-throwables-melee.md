---
type: Audit
title: ME2 against ME1's body holsters, grenades and melee
description: Contract by contract, how ME2 reproduces ME1's holster grab, "only holsters draw", the grenade slot and the melee gesture, and every input that acts differently on ME2.
tags: [audit, me2, parity, holsters, melee, gestures]
status: draft
timestamp: 2026-11-18T08:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:00:00Z }
game: LE2
against: ../../me1/holsters-throwables-melee.md
commit: 940a75c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/holsters-throwables-melee.md) by reading code and notes at `940a75c6`; no
game was run. Line numbers are that commit's.

- The holsters and the melee gesture are ME1's code on ME2: one module each, the slot machine, the frame, the haptics and
  the stroke rules shared, ME2's names in an adapter. 25 of the 28 contracts that apply are `same`, `same core, other
  inputs` or `adapter`. The ten grenade contracts are `n/a` (ME2 has no grenades).
- **Finding 1: three inputs act differently on ME2 and each was first shipped wrong, then found by a review a day
  later** (the right stick click's tap cast a power, B meleed under squad placement, a grab drew where combat is off).
  All three were problems ME1's catalog lists as solved. They are fixed; the pattern is the finding.
- **Finding 2: "only holsters draw" is a deny-list of five functions on ME2** (ME1: one skipped function with three
  allowed callers). The weapon wheel's pick, `SwitchToBackupWeapon` and the seated wheel fallback (HOL-20) are outside
  it; whether they draw a holstered gun on ME2 was never run.
- **Finding 3: ME2 has reach zones and refusals ME1 has not**, some forced, one not yet the owner's: a fifth slot at the
  front of the right thigh (the SMG), no punch with the weapon away, a let-go gun that stays out in combat, the vault
  on a double A, and X with no reload behind it. The list is under [Inputs that act differently](#inputs-that-act-differently-on-me2).
- The ledger's G08 and G43 cells ("adapted, verified") hold for the code. They do not show Finding 2 or the dead
  grenade settings ME2's file ships.

| rating | count | ids |
|---|---|---|
| same | 13 | HOL-01 02 03 04 06 07 09 10 13 14 31 34 36 |
| same core, other inputs | 4 | HOL-05 19 32 38 |
| adapter | 8 | HOL-08 11 12 16 17 21 35 37 |
| reinvented | 0 | |
| partial | 2 | HOL-15 33 |
| missing | 0 | |
| n/a | 10 | HOL-18 22 23 24 25 26 27 28 29 30 |
| unknown | 1 | HOL-20 |

# Contract by contract

`Holsters.cpp` is [src/game/Holsters.cpp](../../../../src/game/Holsters.cpp), `Melee.cpp` is
[src/game/Melee.cpp](../../../../src/game/Melee.cpp), `Throwables.cpp` is
[src/game/Throwables.cpp](../../../../src/game/Throwables.cpp), `HandCalibration` is
[src/game/HandCalibration.cpp](../../../../src/game/HandCalibration.cpp) and
[.hpp](../../../../src/game/HandCalibration.hpp). "Run" means a fake-headset run recorded in
[research/le2-vrik.md](../../../../research/le2-vrik.md); everything else is from reading alone.

| id | rating | how ME2 gets it | where it differs | evidence |
|---|---|---|---|---|
| HOL-01 | same | The shared tick: `NeutralEye`, `Anchor::Update`. | Nothing. | Holsters.cpp:1328-1344 |
| HOL-02 | same | Arm IK's torso yaw, else the lagging yaw, held while `Machine::Busy()`. | Nothing. | Holsters.cpp:1332-1343 |
| HOL-03 | same | `slots::Scale`, `SlotCenter`. | Nothing. | Holsters.cpp:1346-1368 |
| HOL-04 | same | `UpdateEyeHeight`: calibration, the floor, the setting, in ME1's order. The host serves `floor` and `calibration` on LE2 (ledger services table). | Not seen in a headset on ME2. | Holsters.cpp:853-917 |
| HOL-05 | same core, other inputs | ME1's four slots with ME1's numbers; what is carried comes from the inventory chain by weapon family. | A fifth slot, the SMG: `thighRight` (0.08, -0.90, 0.15) r 0.12 by default, or `lowerBack` (0.00, -0.60, -0.26) r 0.12. A heavy weapon has no slot. | HandCalibration.hpp:189-201; HandCalibration.cpp:288-300; Holsters.cpp:630-650 |
| HOL-06 | same | `Machine::Update`. | Only with `smgSlot = lowerBack`: a hand lost there keeps its place 0.75 s (`lostHandHoldS`); ME1's "untracked = no slot" holds at every default slot. | Holsters.cpp:1296-1307, 1414-1428 |
| HOL-07 | same | The host's count of kept squeezes, our own latch 0.6 / 0.4. | Nothing. | Holsters.cpp:1208-1225 |
| HOL-08 | adapter | `BioPlayerController.SwitchWeapon(oWpn)`, then `BioPawnBehavior.SetActionState(1)` if holstered; ME1's click and 3 s follow. Run: rifle in hand after 0.63 s, pistol 0.03 s. | No quick slots. The draw is not sped up (ME1's `drawRate` 6 is in the stance module, out on ME2: see the body audit, BOD-27). | Holsters.cpp:705-762 |
| HOL-09 | same | `CombatEnabled` at the shared top of `Draw`, ME1's refusal buzz. Run on the Normandy 2026-11-17. | Nothing. | Holsters.cpp:676-689, 716-722 |
| HOL-10 | same | The machine's `Unavailable`, `Missed`, `AlreadyHeld`. | Nothing. | Holsters.cpp:1461-1491 |
| HOL-11 | adapter | `gripHolds`, ME1's grace 0.2 s, `SFXGameModeDefault.TryHolster`. Run: back after 0.20 s, holstered after 0.93 s. | ME2's `TryHolster` refuses in combat, mid-reload, in a power and in a transition, so the mod asks those four first: **a gun let go in combat stays out and the hand gets the refusal buzz**. On ME1 it goes back. | Holsters.cpp:181-191, 608-627, 784-793; mode gate GameAdapter.hpp:74-77 |
| HOL-12 | adapter | The machine's Returning rules; "switching" is the held weapon's state `WeaponEquipping` / `WeaponPuttingDown`. | ME2's holster animation takes 0.9-1.1 s (ME1's `holsterRate` 15 does not run). | Holsters.cpp:666-667 |
| HOL-13 | same | Mode 0, a valid view, tracked controllers, a valid right grip, no calibration. Mode 0 is on foot in every game; the wheels' modes are translated (ME2: 1, 2). | The reflection gate is `Up()`; it was `Ready()` until 2026-11-16 and the module stood down on ME2. | Holsters.cpp:391, 1242-1243, 1287-1313 |
| HOL-14 | same | `Contact`, `EnterEdge`, `Depth`; the host serves `hapticContinuous` on LE2. | Nothing. | Holsters.cpp:1519-1525 |
| HOL-15 | partial | `BioPlayerInput.IsCombatEnabled` answered false inside `FireWeapon`, `TightAim`, `TryReload`, `NextWeapon`, `PrevWeapon` while `ActionState` is not combat; one `RefuseGameDraw` for the three games. Run: four trigger pulls refused. | A deny-list, not ME1's skip of the one draw function. Not on it: the weapon wheel's entry and pick, `SwitchToBackupWeapon` ([research/le2-vrik.md](../../../../research/le2-vrik.md) line 333 names it as a draw route), the holster key. ME1 refuses the wheel's entry draw. | Holsters.cpp:145-162, 990-1021, 1156-1173 |
| HOL-16 | adapter | The first two routes (`FireWeapon`, `TightAim`) are refused without a buzz. Run. | The aim trigger is silent too (ME1's left trigger is the squad wheel). | Holsters.cpp:161, 1013-1014 |
| HOL-17 | adapter | Powers and the game's own combat start are not among the five routes, so they draw. | No allowed-caller list and no "let through" log line; medi-gel is a power on ME2 (Unity). Not run. | Holsters.cpp:151-152, 1170-1173 |
| HOL-18 | n/a | `AllowGameDraw` has no caller on ME2 (no grenades). The "came out without a holster grab" line is shared. | — | Holsters.cpp:1026-1032, 1317-1321 |
| HOL-19 | same core, other inputs | `SeatedLayout`, ME1's four seated points and radii. | Plus `seatedThighRight` (0.15, -0.60, 0.55) r 0.20 and `seatedLowerBack`. `seatedGrenade` is written to ME2's file and never read by anything that runs. | HandCalibration.cpp:302-312 |
| HOL-20 | unknown | The seated wheel's two callers and both `SeatedWheel*` routes are inside `MEVR_GAME_LE1`. `SeatedWeaponWheel()` still answers on ME2. | ME2's weapon wheel is never refused, seated or standing (HOL-15). Whether its pick draws a holstered gun: run `mevr calltrace 10 switchweapon setactionstate enterweaponwheel` with the weapon away and pick in the wheel. | Holsters.cpp:76-78, 966-985, 1038-1042 |
| HOL-21 | adapter | ME1's `TickPowerHold` on every game: `SFXGameModeDefault.EnterPowerWheel` after 300 ms, `ExitPowerWheel` at the release, the click never RB. Run 2026-11-17. | The click is also kept from the game as R3 on the sequels (`keepPowerClick`): no area map on a tap, no objective radar on a hold. ME1's R3 still reaches the game. The owner's answer to question 14. | Holsters.cpp:163-180, 467-533; [research/le2-vrik.md](../../../../research/le2-vrik.md) "The right stick click, the heavy weapon and ME1's refused draw" |
| HOL-22 | n/a | No grenades in ME2. `throwables::Tick` is `Inactive("LE1 only")`. | A squeeze at the left chest claims nothing; see the inputs list. | Throwables.cpp:1078-1081 |
| HOL-23 | n/a | `Claim` runs but `gActive` is never true. | — | Throwables.cpp:851 |
| HOL-24 | n/a | — | — | Throwables.cpp:886 |
| HOL-25 | n/a | — | — | |
| HOL-26 | n/a | The release estimator is shared core, unused on ME2. | — | |
| HOL-27 | n/a | — | — | Throwables.cpp:777-821 |
| HOL-28 | n/a | — | — | |
| HOL-29 | n/a | X is the hand power wheel's on foot on ME2 too (`HandWheelOwnsX`, mode 0). `throwables::AdjustPad` never acts (not active). | ME2's X is reload, not a grenade: the reload button is gone on foot. The owner: "automatic reload for now" ([trilogy plan](../../../trilogy-plan.md), answered 2026-11-17). | [FootControls.hpp](../../../../src/core/vr/FootControls.hpp):10-12; Throwables.cpp:1084-1096 |
| HOL-30 | n/a | The module is out. Its role as the melee's on-foot gate is `melee::SequelOnFoot`. | See Reinvented 1. | Melee.cpp:981-1009 |
| HOL-31 | same | `ik::melee::Rules::Physical` through `PhysicalStrokes`, fed by `Le2Tick` on new samples; ME1's speeds and travel from `[Throwables]`. | The body's front is not held while a hand is in a slot (ME1's feed holds it). | Melee.cpp:926-976, 1019-1051 |
| HOL-32 | same core, other inputs | Right: trigger with the weapon out, the hand in a slot, 0.5 s after a draw. Left: the support grip. | The two grenade suppressions do not exist (`tick.suppressed` stays false). Nothing suppresses a stroke during a vault. | Melee.cpp:946-954, 1022-1026 |
| HOL-33 | partial | Weapon out: `SFXGameModeDefault.TryMelee` (the held weapon's `MeleePowerName`). Run: a strike hit a drone, 100% to 5%. | **No punch with the weapon away**: holstered, `Pawn.Weapon` is None and ME2's melee is the held weapon's; the fist passes `RouteMelee` and is refused in `Busy`. `DispatchFist` exists and is unreachable in that state. | Melee.cpp:416-430, 457-495 |
| HOL-34 | same | The shared `Strike`, ME1's order. | ME2's `Busy` adds the game's own refusals: zoomed, in the cover state, a blocking action. | Melee.cpp:863-906, 416-447 |
| HOL-35 | adapter | `MeleeWait` with the BioPower's `m_fCooldownRemaining` read live, `meleeCooldownS` 0.8. | The power is the held weapon's, not Pistol_Whip / Rifle_Butt by quick slot. | Melee.cpp:449-454, 889-893 |
| HOL-36 | same | The shared `FindEnemy`: 2.0 m, 75 degrees, 2.0 m of height. | `Engine.Pawn.IsDead`. The pawn list is walked raw with a guard of 512 on every game, not with `LivePawns` (the catalog's freed-pawn hazard, CLAUDE.md "Gotchas"). | Melee.cpp:805-856, 158-162 |
| HOL-37 | adapter | `StrikeTick`, `WatchHit`, `HitThump`. | "Health" is health plus shields over their maxima. | Melee.cpp:510-522, 1146-1168 |
| HOL-38 | same core, other inputs | `kFlagNoMeleeButton` while `SequelOnFoot`. Run: `melee-b.ps1` with its canary. | ME2's B is `TryExitCover 1 | TryMelee | LeaveWorld`: all three are kept. Leave is a top-bar entry of our menu (question 16). The gate is a second copy of ME1's. | Melee.cpp:1183-1206 |

# Inputs that act differently on ME2

What the owner asked for by name: every case where a hand or a button does on ME2 what it does not on ME1, or an ME1
"never" that does not hold.

| input | ME1 | ME2 | forced? | the owner |
|---|---|---|---|---|
| A, twice within 0.45 s, standing at an obstacle | nothing of ours | the game's vault (`MantleCover`) | Yes in kind: no cover snap, so the game's own vault route is gone ([research/le2-traversal.md](../../../../research/le2-traversal.md)) | approved, 2026-11-14 ("the conventional button is double a") |
| right grip at the front of the right thigh | no slot there | draws the SMG if one is carried; the contact buzz when the hand passes | No: the place is ours | **not confirmed**: "thighRight the default until the owner chooses" (ledger G08) |
| right grip let go in combat | the gun goes back after 0.2 s | the gun stays out, refusal buzz | Yes: `TryHolster` refuses | not asked; reported by lane FX2 |
| right fist stroke, weapon away | punches | refused, logged, no haptic | Yes as recorded (Save 4: no weapon on the pawn) | told (G43) |
| a stroke while zoomed or in the game's cover state | n/a | refused | Yes: `TryMelee`'s own tests | — |
| B on foot | never melee | never melee, **nor leave cover, nor leave the level** | The binding is the game's | approved, 2026-11-17 (question 16) |
| X on foot | power wheel; the grenade is physical | power wheel; **no reload button** | The clash is ours (X) | "automatic reload for now" |
| right stick click, tap | nothing; R3 reaches the game (harmless) | nothing; R3 kept from the game (no area map) | Partly: ME2 binds R3 to the map | approved (question 14) |
| aim trigger (LT route `TightAim`), weapon away | n/a (LT is the squad wheel) | silently refused | Yes: it draws on ME2 | covered by "only holsters draw" |
| left grip at the left chest | takes a grenade | held: nothing, no buzz (lane AU6, c8f6987e; before: fell through to the radio's and the squad orders' tests) | Yes: no grenades | the owner, 2026-11-18: no haptic cue at the chest |
| left grip at the left hip, then to the receiver | nothing (the vent is at the receiver, hot gun only) | the reload by hand, **off by default** (`[Reload] enabled = 0`) | No | rejected 2026-11-16; off until redesigned |
| weapon wheel pick, weapon away | the entry's draw refused | not refused: unknown whether it draws | No | not asked |

# Reinvented

1. **The on-foot gate and the body frame exist three times.** `melee::SequelOnFoot` and `Le2Tick` (Melee.cpp:981-1051)
   rebuild what `throwables::Tick` has for ME1 (Throwables.cpp:891-950) and `holsters::Tick` has for every game
   (Holsters.cpp:1287-1343): mode 0, a view, controllers, no calibration, then "arm IK's torso yaw, else a lagging head
   yaw". ME2's copy reads `player::GameMode` where the others read `head_tracking::GameMode`, and does not hold the yaw
   while a hand is busy. Unifying: `holsters::SlotFrame()` already publishes the frame (Holsters.cpp:1549-1560); feed
   the melee from it on every game and delete the two copies.
2. **"Drawn" and "mid-switch" are read by each module in its own way.** Holsters: a held weapon and `ActionState == 1`
   (Holsters.cpp:652), the weapon's state name for the switch (666-667). Melee: `ActionState` alone (Melee.cpp:396-398),
   the state name and `BioPawn.IsSwitchingWeapons` (431-442). ME1 has the same split across Holsters, Melee and
   Throwables. One `weapon::Drawn()` / `Switching()` per game in `game/Weapon` would serve all three.
3. **ME2's settings file carries ME1's grenade keys.** `kThrowablesBlock` and `seatedGrenade` are written to
   `LE2MEVR.ini` unchanged (HandCalibration.cpp:2962, 312, 3021): `hands`, `slot`, `throw*`, `flight`, `drawForThrow`,
   `grenadeButton` do nothing there, and `[Throwables] enabled` is the switch for melee and B. Not a second
   implementation, but a player reading the file is told about grenades ME2 has not.

Not reinvention: the deny-list of HOL-15 (ME2 has no `DrawPrimaryWeapon`; `SetActionState` takes a parameter, so ME1's
skip handler does not fit) and `game/Reload` (it reuses the holsters' frame and `core/ik/Vent`'s zone).

# Rediscovered problems

| problem on ME2 | when, cost | the catalog already had |
|---|---|---|
| A tap of the right stick click cast the second mapped power. | Shipped with the holsters 2026-11-16 (`4a366d9a`), found by a review of `57ae5b4e`, fixed 2026-11-17 (`30cf473c`). One day on main, one review lane. | "The game's RB casts the mapped power for any press under its own 0.3 s": the mod opens the wheel itself, research/19 "Hold only (2026-11-05)". ME2's and ME3's RB binding is ME1's. |
| B meleed on foot, also under squad placement's hold and double tap. | Found by the same review; fixed `1131f513`, 2026-11-17. | HOL-38 and "Gesture only (2026-11-05)": B never melees on foot. |
| A holster grab pulled a gun out where the game allows no combat. | Found by the review; fixed `30cf473c`. | ME1's `SwitchToWeapon` asks `IsCombatEnabled` itself. The adapter called lower functions (`SwitchWeapon`, `SetActionState`) that skip it: a port to a lower-level call loses the upper call's tests. |
| The holster's click while the gun stayed out (`TryHolster` refused in combat). | Found by the review; fixed `30cf473c`. | "A holster asked for during the draw's animation was ignored by the game": ask the game's own condition before the haptic that says it worked. |
| The module stood down on ME2 with "our reflection layer is off". | Found in the first holster run, 2026-11-16 ([research/le2-vrik.md](../../../../research/le2-vrik.md) "Holsters on the sequels"). | Not ME1's: the `Ready()` / `Up()` and `kEveryGame` rule is the port's own. The same class is still live in `player::Crouched` (see [the body audit](body-and-ik.md), BOD-17). |

# Forced by the game

- **No grenades.** HOL-22 to HOL-30. [research/le2-vrik-audit.md](../../../../research/le2-vrik-audit.md) "What ME2
  doesn't have".
- **No `DrawPrimaryWeapon`.** Each button's exec draws by itself with `SetActionState(1)`; hence the answer handler on
  `IsCombatEnabled` (Holsters.cpp:145-155).
- **No quick slots.** The inventory chain by family (Holsters.cpp:103-116).
- **`TryHolster` refuses in combat, mid-reload, in a power, in a transition.** Read from ME2's script by lane FX2
  (Holsters.cpp:182-185).
- **No punch with the weapon away.** Seen on Save 4, 2026-11-17 (Melee.cpp:426-429). Reading alone cannot say whether a
  state exists with a weapon on the pawn and `ActionState` not combat; there `DispatchFist` would run.
- **B and R3 carry other functions** (`TryExitCover`, `LeaveWorld`; `ShowAreaMap`): ME2's bindings.

# Not forced

- The SMG's slot at the front of the right thigh, its radius 0.12 (ME1's hips are 0.14), and that it is on by default.
- The weapon wheel, `SwitchToBackupWeapon` and the holster key left out of the refused routes (HOL-15, HOL-20).
- No "let through" log line for a power's draw (HOL-17): ME1 logs each one, ME2 is silent.
- The melee's body front not held while a hand is in a slot (HOL-31).
- The grenade keys in `LE2MEVR.ini`.
- X as the power wheel's button on a game whose reload is X (the owner accepted it "for now").

# What to do

Ordered by what a player meets first. Parity work and questions only.

1. **Ask the owner about the SMG's thigh slot** before it is seen as a default: the choice (`thighRight`, `lowerBack`,
   or off until the headset check) is recorded as open in the ledger's G08. Files: `HandCalibration.cpp` (the block's
   `smgSlot`). Closes with the owner's answer in [docs/trilogy-plan.md](../../../trilogy-plan.md).
2. **Run the three undrawn routes with the weapon away** (HOL-15, HOL-20): the weapon wheel's pick, the backup-weapon
   switch, the holster key. `tools/le2-halcyon-run/holsters.ps1` plus `mevr calltrace 10 setactionstate switchweapon
   enterweaponwheel`. If any draws, add its exec to `gDrawRoutes` (Holsters.cpp:1156) and decide the seated fallback as
   ME1's HOL-20. Closes when the log shows `refused` for each, or the row says "does not draw".
3. **Tell the player why the gun stays out in combat** (HOL-11): today it is a buzz with no word. A question for the
   owner, not a design: is the refusal buzz enough, since ME1 never refuses there?
4. **Ask the owner about reload** again when the release nears: X has no reload on foot and the hand reload is off;
   "automatic reload for now" is the recorded answer.
5. **One body frame and one on-foot gate** (Reinvented 1): `Melee.cpp` `Le2Tick`, `Throwables.cpp` `Tick`, fed from
   `holsters::SlotFrame()`. Closes with `tests/MeleeTests.cpp` unchanged and ME1 at the same pose identical (the
   lanes' `fake-headset-session` pair).
6. **One `Drawn()` / `Switching()` per game** (Reinvented 2) in `game/Weapon`, used by Holsters, Melee and Throwables.
7. **Write only the keys ME2 has** (Reinvented 3): split `kThrowablesBlock` into the melee keys and ME1's grenade keys;
   drop `seatedGrenade` from the sequels' `[Holsters]` block. Closes when a fresh `LE2MEVR.ini` has no grenade word.
8. **Walk the pawn list with `LivePawns`** in `FindEnemy` on every game (HOL-36), or record why the guard is enough.
9. **Before the next port, read the catalog's "Problems ME1 already solved" against the adapter's call depth**: three of
   the five rediscovered problems came from calling a lower function than ME1's and losing the tests of the upper one.
   A line in [how-to-catalog](../../how-to-catalog.md) or the ledger's rules; no code.

# Sources

- [The ME1 catalog](../../me1/holsters-throwables-melee.md); [the parity ledger](../../../trilogy-parity-ledger.md) G08,
  G41, G42, G43; [the trilogy plan](../../../trilogy-plan.md) "Open owner questions".
- [research/le2-vrik.md](../../../../research/le2-vrik.md): "Holsters on the sequels", "The sequels' controls after the
  review of 57ae5b4e", "The right stick click, the heavy weapon and ME1's refused draw".
- [research/20](../../../../research/20-melee-and-grenades.md) "The trilogy's one path";
  [research/19](../../../../research/19-body-holsters.md); [research/le2-vrik-audit.md](../../../../research/le2-vrik-audit.md).
- Code: [Holsters.cpp](../../../../src/game/Holsters.cpp), [Melee.cpp](../../../../src/game/Melee.cpp),
  [Throwables.cpp](../../../../src/game/Throwables.cpp), [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp),
  [Mod.cpp](../../../../src/mod/Mod.cpp) (the tick order, lines 1837-1897).
