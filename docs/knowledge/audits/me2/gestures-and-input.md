---
type: Audit
title: ME2 gestures and input against ME1's contracts
description: Contract by contract, what each controller input and hand gesture does on ME2, who claims it, what the game still sees, and every action ME2 takes that ME1 does not.
tags: [audit, me2, parity, input, gestures, arbitration]
status: draft
timestamp: 2026-11-18T08:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:00:00Z }
game: LE2
against: ../../me1/gestures-and-input.md
commit: 940a75c6
---

# Verdict

Audited against [ME1 gestures and input](../../me1/gestures-and-input.md) by reading code and notes at 940a75c6; no
game was run. Rows that lean on the sibling audits say so:
[holsters](holsters-throwables-melee.md), [powers and squad](powers-and-squad.md), [weapons](weapons-and-sights.md),
[vehicles](vehicles.md).

- The mapping layer is ME1's own: one `GamepadMapping`, one `PadMerge`, one tick order in `Mod.cpp`, the same flags.
  ME2 does not have a second input system.
- **The gestures that "register actions ME1 doesn't" come from two places.** First, the same pad signal means something
  else in ME2 (LT is a zoom, X is reload, R3 is the map, B leaves cover and the level, A held sprints and tapped takes
  cover). Second, ME1's first claimants of the left grip (grenade slot, vent) do not exist on ME2, so squeezes fall
  through to the radio and the squad order.
- **Three ME1 "never" rules were broken on ME2 when it first ran and fixed a day later** (an R3 tap cast a power, B
  meleed, a grab drew where combat is off). One still differs by design: the triggers are LT and RT in conversations.
- Recentering from the controllers is gone: ME2's is a double tap of Back, which the controllers only produce from a
  menu button.

| rating | count | ids |
|---|---|---|
| same | 13 | INP-01, 02, 04, 05, 15, 17, 19, 23, 24, 25, 27, 40, 13 (its vent exception n/a) |
| same core, other inputs | 11 | INP-03, 07, 10, 11, 14, 18, 28, 29, 30, 34, 42 |
| adapter | 5 | INP-06, 16, 21, 32, 44 |
| partial | 2 | INP-12, 33 |
| missing | 6 | INP-09, 22, 35, 36, 37, 38 |
| n/a | 3 | INP-08, 26, 43 |
| unknown | 4 | INP-20, 31, 39, 41 |

# ME2's input table

On foot is ME2's game mode 0. ME2's mode numbers differ from ME1's (1 power wheel, 2 weapon wheel, 3 command, 4
vehicle, 10 orbital); shared code asks `engine::adapter::GameMode`
([GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp):39-77).

| input or gesture | on ME2 | first claimant, then | what ME2 itself sees | against ME1 |
|---|---|---|---|---|
| right trigger | fires; never draws | game pointer on a screen > the game | RT; in a conversation RT is the Renegade interrupt | same on foot; the interrupt is ME2's |
| right grip | holds the gun; a new squeeze at a slot draws | holsters | nothing (never RB) | same rule; five slots (the SMG at the right thigh); in combat a let-go gun stays out with the refusal buzz |
| right fist, weapon away | nothing | - | nothing | ME1 punches |
| left trigger | opens our drawn squad menu | hand power wheel > squad menu (mode 0 only) > the game's LB | nothing on foot; **LT (Paragon interrupt) in a conversation and in orbital scanning** | ME1 opens the game's own wheel, also from modes 2 and 3 |
| left grip, new squeeze | support grip, radio at the ear, else a squad order | open power wheel > support zone (host) > radio > squad order | nothing; LT 255 while the support grip is at the shoulder (not a sniper rifle) | ME1 asks the grenade slot and the vent first; on ME2 those squeezes become orders. LT there starts ME2's zoom |
| A | the game's A | traversal watches for a double tap | A | **a double A at a vaultable obstacle vaults** (ME2 only); A never snaps into cover |
| B | squad placement (hold, double tap) | squad placement | nothing | same; ME2 loses leave-cover and leave-level on B (the menu's Leave entry stands in) |
| X | hand power wheel | power wheel | nothing | same; but X is ME2's reload, so the controllers have no reload button |
| Y | the game's Y | - | Y | what ME2 does with it: not established |
| left stick | walks toward the head | power wheel > squad menu > walk | stick | same |
| left stick click | crouch toggle after 100 ms | chord > the game | L3 | same |
| right stick X | turn (the game's, or snap) | squad menu > placement selector > comfort | stick X unless the mod turns | same |
| right stick Y | nothing of ours | - | stick Y | ME1 steps the scope's zoom under ADS; ME2 has one step |
| right stick click, held 0.3 s | the game's power wheel | chord > the mod's hold | **nothing: R3 never reaches ME2** | ME1 still sends R3 |
| right stick click, tap | nothing | - | nothing | same since 2026-11-17 (it cast power 2 before) |
| right stick double-click | nothing | - | nothing | **ME1 recenters** |
| both stick clicks | tap snapshot, hold our menu | chord | nothing | same |
| left menu button | Start; with the left grip Back; Back twice recenters | - | Start / Back | ME1 does not depend on it |
| fast stroke at an enemy, weapon drawn | melee | melee after holsters | `TryMelee` by call | same rule |
| open left hand pushed | casts the equipped power | power wheel | a call, no pad | same, plus ME2 needs `IsCombatEnabled` |
| left palm on chest / gun | self cast | power wheel | a call | same; over the head (Unity) has no power behind it |
| left grip at the hip pouch, clip to the gun | reload by hand: **off** (`[Reload] enabled = 0`) | - | - | ME2 only; the owner turned it off |
| head lowered | crouch | posture | `Pawn.ShouldCrouch` by call | same |
| fingertip on the left wrist | the omni-tool's rail | host | - | not traced here |

# Contract by contract

| id | rating | how ME2 gets it | where it differs | evidence |
|---|---|---|---|---|
| INP-01 | same | `gp::FromControllers` and `Merge`, unchanged | - | [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp):353-389 |
| INP-02 | same | `core/xr/Input` on Halcyon's ME2 session | - | [Input.cpp](../../../../src/core/xr/Input.cpp) |
| INP-03 | same core, other inputs | the same flags in the same order | the host adds `gateRightClick` and `keepPowerClick` (both true), and in conversation and orbital modes replaces the options with `ConversationTriggers` | [PadMerge.cpp](../../../../src/plugin/PadMerge.cpp):129-155; [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp):371-381 |
| INP-04 | same | same latches | - | [GamepadMapping.cpp](../../../../src/game/GamepadMapping.cpp) |
| INP-05 | same | `menus_le2::FilterPad` after the merge | - | [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp):389 |
| INP-06 | adapter | `IsCombatEnabled` answered false inside `FireWeapon` while the weapon is away | another mechanism, same result; four pulls refused in the run of 2026-11-16 | holsters audit, HOL-16 |
| INP-07 | same core, other inputs | same holsters module; the modes translated by `IsFootOrWheelMode` | a fifth slot (SMG, right thigh) on by default; a gun let go in combat stays out | [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp):74-77; holsters audit |
| INP-08 | n/a | ME2 has no grenade slot; a right squeeze at the left chest is an ordinary squeeze (holds the gun) | - | [Melee.cpp](../../../../src/game/Melee.cpp):1012-1014 |
| INP-09 | missing | no punch with the weapon away | forced: ME2's melee belongs to the held weapon | CLAUDE.md "One melee for the trilogy"; holsters audit HOL-33 |
| INP-10 | same core, other inputs | the same trigger latch opens the squad menu | our drawn menu, never the game's wheel; taken in mode 0 only (ME1: 0, 2, 3) | [SquadWeapons.cpp](../../../../src/game/SquadWeapons.cpp):1693-1701 |
| INP-11 | same core, other inputs | `leftTrigger = lb`; no LT from the support grip since be1a47ba (the weapons audit's WPN-09, option B) | the trigger IS LT in conversations and orbital scanning (the owner's rule 11). LT from the grip was ME2's zoom, not only aimed fire | [PadMerge.cpp](../../../../src/plugin/PadMerge.cpp):129-142; weapons audit WPN-09 |
| INP-12 | adapter (fixed) | the same order in `Mod.cpp`; the two absent claimants' zones held (lane AU6, c8f6987e) | No grenade slot and no vent, but their zones are kept. A new left squeeze within the grenade slot's radius (`game/Throwables` `ClaimChest`, its body frame `PlaceSlot` shared with ME1's) or over the receiver (ME1's vent zone, `game/Vent` `ClaimReceiver`, where the support grip didn't take it) is claimed and does nothing: no buzz (the owner: no haptic cue at the chest on ME2), no radio, no squad order. The reload's claim is still not asked by orders or voice (dormant while reload is off). V on Save 4: `throwables: a left squeeze at the left chest ... held, so it is no squad order` and no `squad orders:` line; with `[Throwables] hands = right` the same replay pointed and gave `ALL|-|-|move|-` | [Throwables.cpp](../../../../src/game/Throwables.cpp) `ClaimChest`; [Vent.cpp](../../../../src/game/Vent.cpp) `ClaimReceiver` |
| INP-13 | same | host mapping on a new squeeze | the hot-receiver exception does not exist | [Vent.cpp](../../../../src/game/Vent.cpp):184 |
| INP-14 | same core, other inputs | A passes | ME2 adds the double A vault (0.45 s between presses; within 0.39 m of the slot, facing within 45 degrees, gun drawn); cover snap on A refused | [Traversal.hpp](../../../../src/core/vr/Traversal.hpp):70-130; CLAUDE.md "ME2's vault" |
| INP-15 | same | `melee::AdjustPad`, every game | ME2 loses `TryExitCover` and `LeaveWorld` with it; the menu's Leave entry replaces the second. Traced on ME2 (`melee-b.ps1`) | CLAUDE.md; holsters audit HOL-38 |
| INP-16 | adapter | same arc and selector | the order is given by `SquadPlace`'s own ME2 function, not the one dispatcher | powers audit, PWR-33 |
| INP-17 | same | same core rule | - | [SquadPlace.hpp](../../../../src/core/vr/SquadPlace.hpp) |
| INP-18 | same core, other inputs | X reserved by the same flag | X is ME2's reload: with X taken and the hand reload off, the controllers cannot reload on demand (the game reloads an empty gun by itself) | [PowerWheel.cpp](../../../../src/game/PowerWheel.cpp):1982; powers audit |
| INP-19 | same | same flag | - | powers audit PWR-06 |
| INP-20 | unknown | Y passes | what ME2 binds to Y on foot was not established; `mevr calltrace 5` with a Y press would say | - |
| INP-21 | adapter | the mod calls `SFXGameModeDefault.EnterPowerWheel` after 0.3 s; a tap does nothing | R3 is kept from ME2 entirely (ME1 still gets it): ME2's R3 opens the area map | [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp):372-377; holsters audit HOL-21 |
| INP-22 | missing | Halcyon ME2's recenter is a double tap of Back | Back comes from the left menu button with the left grip: a menu button, which the owner's rule forbids relying on. Left: our menu's Recenter and opening the menu | [me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp):166-208, 485 |
| INP-23 | same | `GateClick`; on ME2 R3 is gated the same way | - | [GamepadMapping.cpp](../../../../src/game/GamepadMapping.cpp):574 |
| INP-24 | same | `UpdateStickChord` in the ME2 host | - | [MenusLe2.cpp](../../../../src/plugin/MenusLe2.cpp):170-186 |
| INP-25 | same | `game/Comfort` | see the comfort audit | - |
| INP-26 | n/a | one zoom step per weapon | the flick is read and does nothing | weapons audit WPN-21 |
| INP-27 | same | `WalkTowardHand`, then Halcyon's head-yaw turn | - | [PadMerge.cpp](../../../../src/plugin/PadMerge.cpp):175 |
| INP-28 | same core, other inputs | `ik::melee::Gestures`, ME1's `Physical` rules | ME2's own feed (`Le2Tick`) builds the body frame a second time; only with a weapon drawn | [Melee.cpp](../../../../src/game/Melee.cpp):1012-1051 |
| INP-29 | same core, other inputs | same push rule | an extra gate: `IsCombatEnabled` must be true | [PowerWheel.cpp](../../../../src/game/PowerWheel.cpp):1906-1910 |
| INP-30 | same core, other inputs | same `StepSelfCast` | which powers count as chest or gun comes from ME2's 26-row table; no power for the raise | powers audit PWR-17 to 19 |
| INP-31 | unknown | `OmniToolRail` is in the menu sources the ME2 host builds | not traced: whether the mod publishes the arm bones on ME2 | [CMakeLists.txt](../../../../CMakeLists.txt) line 1031; a replay of `tools/make-wrist-recording.py` on ME2 |
| INP-32 | adapter | `Engine.Pawn.ShouldCrouch` | names only | CLAUDE.md "ME2 has all three" |
| INP-33 | partial | a list of five functions refused | the weapon wheel's pick, `SwitchToBackupWeapon` and the holster key are not on it | holsters audit HOL-15 |
| INP-34 | same core, other inputs | by the same mapping | two of the five do not hold: the left trigger is LT in conversations, and recentering needs a menu button | this file |
| INP-35 | missing | no vehicle controls | the owner put the Hammerhead after ME2's first release | vehicles audit |
| INP-36 | missing | as INP-35 | - | vehicles audit |
| INP-37 | missing | in ME2's vehicle mode (4) the squad does not take the trigger (mode 0 only), so it is LB | by accident of the gate, not by a vehicle rule | [SquadWeapons.cpp](../../../../src/game/SquadWeapons.cpp):1697-1698 |
| INP-38 | missing | no cockpit | - | vehicles audit |
| INP-39 | unknown | each module's on-foot gate | not traced for ME2's mode 4. Two shared files still compare the raw mode with ME1's numbers (`mode == 1` as "vehicle"): on ME2 mode 1 is the power wheel | [MenuPause.cpp](../../../../src/game/MenuPause.cpp):187; [HandReticle.cpp](../../../../src/plugin/HandReticle.cpp):142 |
| INP-40 | same | our menu and calibration in the ME2 host | - | [MenusLe2.cpp](../../../../src/plugin/MenusLe2.cpp):160-170 |
| INP-41 | unknown | `plugin/GamePointer` is built into the ME2 host | not traced here; see the menus audit when written | [CMakeLists.txt](../../../../CMakeLists.txt) lines 1032-1038 |
| INP-42 | same core, other inputs | the pad is the game's | the triggers are ME2's interrupts; every mod owner is stripped from the options for the mode | [PadMerge.cpp](../../../../src/plugin/PadMerge.cpp):129-142 |
| INP-43 | n/a | ME2 never holds the game's weapon wheel; `kFlagSquadMenu` owns the sticks instead | - | powers audit PWR-24, 25 |
| INP-44 | adapter | `\` our menu, K first person, End and Home; keys only with the game in front | no R recenter (R is ME2's reload) | [me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp):482-490 |

# Actions ME2 takes that ME1 does not

| input | ME2's action | forced by the game? | the owner |
|---|---|---|---|
| double A at an obstacle | vault | yes: ME2 vaults, ME1 does not | approved 2026-11-14 |
| triggers in a conversation | Paragon / Renegade interrupts | yes | approved (rule 11) |
| left grip at the left chest or over the gun's receiver | nothing: the zones are held (lane AU6, c8f6987e) | no: ME1's zones kept with nothing in them | the owner, 2026-11-18: no haptic cue at the chest |
| support grip at the shoulder | LT, which on ME2 zooms the weapon (view magnified until 2026-11-17) | LT's meaning is the game's; sending it is our choice | complained 2026-11-17; the picture fixed, the cause not asked |
| right grip at the front of the right thigh | draws the SMG | ME2 has SMGs; the place is ours | "until the owner chooses" (ledger G08) |
| left grip at the hip pouch | reload by hand | no | turned off 2026-11-16 |
| letting the gun go in combat | stays out, refusal buzz | yes (`TryHolster` refuses) | reported, not asked |
| right stick click | never R3 | ME2's R3 is the map | approved (question 14) |
| B | also no leave-cover, no leave-level | ME2 binds them to B | approved (question 16) |
| left menu button with the grip, twice | recenter | Halcyon's ME2 choice | not asked; conflicts with "no menu buttons" |

# ME1 rules that do not hold on ME2

| ME1 rule | on ME2 | forced |
|---|---|---|
| the left trigger is never LT | LT in conversations and orbital scanning | yes |
| nothing uses a controller menu button | recentering does | no |
| R3 still reaches the game | never | yes (approved) |
| a right fist punches with the weapon away | does not | yes |
| a left squeeze is a grenade or a vent before anything else | neither exists; their zones are held (no action) | yes; that the squeeze does nothing there is ours (lane AU6) |
| the right stick double-click recenters | does nothing | no |
| the weapon wheel is the game's own, steered at the hand | our drawn menu | not established as forced (powers audit) |
| a flick under ADS steps the zoom | no second step | yes |

# Reinvented

| ME2 code | does what this already did | what unifying takes |
|---|---|---|
| `Le2Tick`'s body frame and on-foot gate in [Melee.cpp](../../../../src/game/Melee.cpp):1012-1051 | `Throwables`' frame on ME1 and `Holsters`' own (three copies, per the holsters audit) | one shared "on foot, body frame" sample that the three modules read |
| Halcyon ME2's Back double tap ([me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp):166-208) | ME1's right-stick double-click (`kRbDoubleClickMs`) | call `Recenter()` from the host on the mapping's double click, as LE1's bridge does |
| raw mode numbers in `MenuPause.cpp:187` and `HandReticle.cpp:142` | `engine::adapter::GameMode` | replace the comparisons; each is one line |

# Rediscovered problems

| problem on ME2 | ME1's catalog already had | date, cost |
|---|---|---|
| an R3 tap cast a power | "RB's tap casts the mapped power for any press under 0.3 s" (research/19 "Hold only", 2026-11-05) | found by review 2026-11-17, fixed 30cf473c |
| B meleed under squad placement | "B off; gesture only" (research/20) | 2026-11-17; the run script's verdict had been a fixed PASS |
| a grab drew a weapon where combat is off | ME1's draw goes through the game's own test | 2026-11-17, fixed b4f1fb31 |
| the reserved X hid ME2's reload | "The X that equipped a power threw a grenade" is the same class: X has a game meaning | noticed 2026-11-17; answered "automatic reload for now" |
| LT from the support grip zoomed the view and blocked the vault | research/18: aimed fire and the zoom share one flag | two lanes (C4, the 2026-11-17 fix) |

The pattern: each time, the mapping was carried over with ME1's flags and nobody listed what the same pad signal means
in ME2 before running it. [research/le2-diagnostic-pad-contract.md](../../../../research/le2-diagnostic-pad-contract.md)
has that list for the diagnostic host; the VRIK port did not start from it.

# Forced by the game

Interrupts on the triggers; the vault; X as reload; R3 as the map; B as leave-cover and leave-level; no weapon-away
melee; no grenades of ME1's kind; no heat; one zoom step; `TryHolster` refused in combat. Evidence is in the rows above
and in CLAUDE.md's ME2 paragraphs.

# Not forced

The left-grip squeezes that fall through to squad orders; recentering on a menu button; the squad menu in mode 0 only;
the SMG's slot place; LT sent by the support grip; the raw mode numbers; the reload claim not asked by orders and
voice.

# What to do

1. **Write ME2's pad contract into the catalog's shape first**: for each XInput signal, the ME2 function it reaches on
   foot (`mevr calltrace` per button on Save 4), including Y. Every later item is checked against it. Closes INP-20.
2. **Left grip with no claimant**: ask the owner what a squeeze at the chest or over the receiver should be on ME2
   (nothing, or an order). Until then the honest state is "orders fire where ME1 would not". Files:
   [SquadOrders.cpp](../../../../src/game/SquadOrders.cpp), [Voice.cpp](../../../../src/game/Voice.cpp); also ask
   `reload::ClaimedLeft()` there. Check: a replayed squeeze at the chest gives no `squad orders:` line.
3. **Recenter without a menu button**: the right-stick double-click as on ME1, from the ME2 host. Check: log
   `recentered` after a replayed double click.
4. **LT from the support grip**: with the weapons audit's item 2.
5. **Reload on demand**: an owner question (X is taken, the hand reload is off). No design proposed here.
6. **Vehicle mode and raw mode numbers**: fix the two comparisons, then trace which on-foot modules stay live in mode 4
   (closes INP-39) on a Hammerhead save.
7. Trace the wrist tap and the game pointer on ME2 (INP-31, INP-41) with the existing wrist and menu recordings.

# Citations

- [The catalog file](../../me1/gestures-and-input.md); [the parity ledger](../../../trilogy-parity-ledger.md) (G05, G08,
  G41; it has no row for input arbitration as such, which is why these differences were not visible there);
  [the trilogy plan](../../../trilogy-plan.md) for the owner's rules and answers.
- [research/le2-vrik.md](../../../../research/le2-vrik.md), [research/le2-traversal.md](../../../../research/le2-traversal.md),
  [research/le2-powers-input.md](../../../../research/le2-powers-input.md),
  [research/le2-interrupt-acquisition.md](../../../../research/le2-interrupt-acquisition.md).
