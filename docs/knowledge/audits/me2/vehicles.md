---
type: Audit
title: ME2 audit - vehicles (the Hammerhead against ME1's Mako)
description: Every Mako contract of ME1's catalog rated on ME2, where the Hammerhead has none of the Mako's aim, controls, cockpit or gunner yet, and what the Hammerhead gets today instead.
tags: [audit, me2, parity, vehicles]
status: draft
timestamp: 2026-11-18T08:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:00:00Z }
game: LE2
against: ../../me1/vehicles.md
commit: 940a75c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/vehicles.md) by reading code and notes at 940a75c6; no game was run and no
note records a Hammerhead run with this mod.

- Nothing of ME1's Mako runs on ME2: 34 of 36 contracts are missing. This is by the owner's decision (rule 13 of
  [the trilogy plan](../../../trilogy-plan.md): "the Hammerhead gets the Mako's controls and cockpit, in a pass of its
  own after ME2's first release"), and the ledger's G44 says the same.
- What the Hammerhead gets today is Halcyon's own vehicle view and the on-foot controller mapping, **which nobody has
  looked at**: no script reaches a Hammerhead save, and no research note describes the drive.
- Halcyon's `VehicleHeadMode` ships as 3 and is not pinned by ME2's baseline, while ME1's rule is no head aim in the
  vehicle (`makoHeadAim = 0`). What mode 3 does to the aim is not written down.
- The game-independent rules (`core/vr/MakoControls`, `MakoGunner`, `CockpitHazard`) already compile into the sequels'
  mods; every game-side module is inside `MEVR_GAME_LE1`.

| rating | count | contracts |
|---|---|---|
| same | 0 | |
| same core, other inputs | 0 | |
| adapter | 0 | |
| reinvented | 0 | |
| partial | 1 | VEH-06 |
| missing | 34 | VEH-01 to 05, 07 to 34, 36 |
| n/a | 0 | |
| unknown | 1 | VEH-35 |

# Contract by contract

"LE1 guard" means the module's body is inside `#if defined(MEVR_GAME_LE1)` and the ledger's generated table lists it
"run / out / out". ME2's vehicle game mode is 4, not ME1's 1
([GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp):50-52).

| id | rating | how ME2 gets it | where it differs | evidence |
|---|---|---|---|---|
| VEH-01 | missing | Not at all: the game aims the Hammerhead from its own driver view. | No controller ray, no trace. | [Mako.cpp](../../../../src/game/Mako.cpp):54 (LE1 guard) |
| VEH-02 | missing | - | Shots follow the game's aim. | Mako.cpp:276, 632 |
| VEH-03 | missing | - | The Hammerhead is a hover vehicle aimed by `DriverViewYaw/Pitch`, not `SetDesiredTurretAimPoint`: this one needs an adapter, not a rename. | [engine_probe.cpp](../../../../src/halcyon/le2/engine_probe.cpp):1656-1665 |
| VEH-04 | missing | - | No cockpit, so no turret over the cabin. | Mako.cpp:663-733 |
| VEH-05 | missing | - | Whether the hand reticle shows in ME2's mode 4 was not established. | [SequelOverlays.cpp](../../../../src/plugin/SequelOverlays.cpp):256 |
| VEH-06 | partial | On foot the game's crosshair is hidden through ME2's `SFXSFHandler_Reticle` adapter. | In the vehicle: not established. No cockpit aim mesh. | [ScreenReticle.cpp](../../../../src/game/ScreenReticle.cpp):235-243, 260-266 |
| VEH-07 | missing | `game/Comfort` asks `mako::CurrentMotion()`, which is ME1's. | No vignette for the Hammerhead's motion (read from the guard; not run). | [Comfort.cpp](../../../../src/game/Comfort.cpp):385 |
| VEH-08 | missing | - | No lever, no stick, no quads. | [MakoControls.cpp](../../../../src/game/MakoControls.cpp):58, 151 |
| VEH-09 | missing | - | | as VEH-08 |
| VEH-10 | missing | - | | as VEH-08 |
| VEH-11 | missing | - | | as VEH-08 |
| VEH-12 | missing | - | | as VEH-08 |
| VEH-13 | missing | - | ME1's seam is `BioVehicleBase.SetInputs`; the plan forbids reusing the Mako's physics writes on the Hammerhead. | [le2-from-le1.md](../../../le2-from-le1.md) section 3.8 |
| VEH-14 | missing | - | | MakoControls.cpp:593 |
| VEH-15 | missing | - | The Mako pad flags are never set on ME2; the Hammerhead's own bindings are not in any note. | MakoControls.cpp:625 |
| VEH-16 | missing | - | | as VEH-15 |
| VEH-17 | missing | - | | as VEH-15 |
| VEH-18 | missing | - | | as VEH-08 |
| VEH-19 | missing | - | | MakoControls.cpp:801-803 |
| VEH-20 | missing | - | | as VEH-19 |
| VEH-21 | missing | - | | as VEH-08 |
| VEH-22 | missing | - | | as VEH-08 |
| VEH-23 | missing | - | No cockpit package, script or ride for ME2. | [CockpitProbe.cpp](../../../../src/game/CockpitProbe.cpp):52 |
| VEH-24 | missing | - | | as VEH-23 |
| VEH-25 | missing | - | | as VEH-23 |
| VEH-26 | missing | - | `core/vr/CockpitPanel` is not even in the sequels' source list. | [CMakeLists.txt](../../../../CMakeLists.txt) lines 431-438 against 794-799; [CockpitPanel.cpp](../../../../src/game/CockpitPanel.cpp):62 |
| VEH-27 | missing | - | | as VEH-26 |
| VEH-28 | missing | - | | as VEH-26 |
| VEH-29 | missing | - | | as VEH-26 |
| VEH-30 | missing | - | | as VEH-26 |
| VEH-31 | missing | - | The hazard rule compiles; nothing calls it. | CMakeLists.txt line 796 |
| VEH-32 | missing | - | The gunner rule compiles; nothing calls it. | [MakoGunner.cpp](../../../../src/game/MakoGunner.cpp):26; CMakeLists.txt line 798 |
| VEH-33 | missing | - | | as VEH-32 |
| VEH-34 | missing | - | | as VEH-32 |
| VEH-35 | unknown | `game/FocusPause` is ME1's. | Whether ME2 freezes without window focus (`bPlayersOnly`) has not been checked (ledger G45). A fake-headset run with the game window behind another settles it. | [FocusPause.cpp](../../../../src/game/FocusPause.cpp):17 |
| VEH-36 | missing | - | | CockpitProbe.cpp:52 |

What ME2 has in the Hammerhead today:

- **Halcyon's vehicle view.** In mode 4 with a vehicle pawn the headset turns the view ("Cabin view uses the headset for
  looking and the original controls for driving"), by `VehicleHeadMode`: 0 off, 1 free look, 2 camera aim, 3 "cam fixed".
  The code's default is 1; the ini the deploy installs says 3
  ([me2_xr.cpp](../../../../src/halcyon/le2/me2_xr.cpp):918-935; [me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp):646;
  `src/halcyon/le2/runtime/MELE2VR.ini` line 41).
- **The on-foot controller mapping**, with whatever the mod's on-foot flags leave set outside mode 0. Which of ME1's on-foot
  rules stay active in ME2's mode 4 (the left trigger as LB, the right grip, B) was not traced.
- **Our menu's way into the game's menu** knows the vehicle mode on every game
  ([GameMenuRequest.cpp](../../../../src/game/GameMenuRequest.cpp):39, 101).

# Reinvented

Nothing. No ME2 vehicle code of ours exists. Halcyon's `[VEHAIM]` block is upstream's, and its own comment records that
"ME1 never fully solved vehicle aim (the Mako had 3 fighting aim reps)": the same question ME1's VEH-03 answers with three
self-measuring seams ([research/44](../../../../research/44-mako.md)).

# Rediscovered problems

None recorded: no ME2 note describes a Hammerhead drive. The catalog's solved list is what the Hammerhead pass should
read first; the ones most likely to return are the turret's aim order within a frame, the chase camera hanging on the
controller's rotation, a squeeze at a control reaching the game as a shoulder button, and the exit refused without
window focus.

# Forced by the game

| difference | evidence |
|---|---|
| The vehicle game mode is 4 on ME2 (1 on ME1): any `mode == 1` test must go through `engine::adapter::GameMode`. | [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp):40-54 |
| A hover vehicle with another class tree (`SFXVehicleHover`), aimed by the driver's view angles; no wheeled physics, no turret skeletal control. | engine_probe.cpp:1656-1664 |
| The Mako's physics writes and weapon assumptions are not to be reused; the control geometry and the panel code are. | [le2-from-le1.md](../../../le2-from-le1.md) section 3.8; trilogy-plan.md line 1249 |
| The cockpit's models are the Mako's (ME1 art). "ME1's Mako controls and cockpit, for now" is the owner's answer. | [le2-continuation-plan.md](../../../le2-continuation-plan.md) line 262 |

# Not forced

| what | on ME1 | on ME2 | evidence |
|---|---|---|---|
| Head aim in the vehicle | Off (`makoHeadAim = 0`, written by the deploy; the hand or the gunner aims) | `VehicleHeadMode = 3` ships and `ApplyMevrBaseline` does not pin it, though it pins on-foot head aim off | me2_menu.cpp:521, 646; `MELE2VR.ini` line 41 |
| The vignette for vehicle motion | On with the comfort vignette | None | Comfort.cpp:385 |
| The pause without window focus | Undone | Not checked | FocusPause.cpp:17 |
| The whole category deferred | - | The owner's decision, 2026-11-14 and 2026-11-15 | trilogy-plan.md rule 13, T35 |

# What to do

The Hammerhead pass is the owner's later pass; these are the steps before and inside it, in order.

1. **Look at the Hammerhead as it is.** A script that reaches a Hammerhead level (a Firewalker save through
   `tools/le2-halcyon-run/load.ps1`), snapshots of both eyes, and a `mevr calltrace` over each controller button and
   trigger in mode 4. Closes when a note in `research/` says what the view, the aim and each input do today. Without it
   the "safe fallback" before the pass is unknown.
2. **Owner question: `VehicleHeadMode`.** ME1's rule is no head aim. Say what mode 3 does (from step 1) and ask whether
   ME2's baseline should pin free look (1) until the Mako's controls arrive. No default changes without the answer
   (rule 19).
3. **Focus pause on ME2** (VEH-35, ledger G45): check `bPlayersOnly` with the window unfocused; if it freezes, move
   [game/FocusPause.cpp](../../../../src/game/FocusPause.cpp) out of its LE1 guard.
4. **The pass itself** (T35): a per-game adapter under the shared rules, not a copy: the vehicle pawn and mode through
   `engine::adapter`, `game/MakoControls`'s seat, grab and pad flags (VEH-08 to 18, 22) first since they are
   game-independent, then the Hammerhead's own throttle, steering, jump and aim seams in place of `SetInputs` and
   `SetDesiredTurretAimPoint`, then the cockpit. Add `core/vr/CockpitPanel` to the sequels' source list when the panel
   comes. Each catalog row's "verified" column names the check to repeat on ME2.

# Citations

- [ME1 catalog: vehicles](../../me1/vehicles.md); [How to catalog](../../how-to-catalog.md)
- [docs/trilogy-plan.md](../../../trilogy-plan.md) rule 13 and T35; [docs/le2-from-le1.md](../../../le2-from-le1.md) section 3.8; [docs/le2-continuation-plan.md](../../../le2-continuation-plan.md)
- [docs/trilogy-parity-ledger.md](../../../trilogy-parity-ledger.md) G44, G45 and the generated module table
- [research/44](../../../../research/44-mako.md), [research/46](../../../../research/46-mako-controls.md), [research/47](../../../../research/47-cockpit-in-game.md)

Catalog and ledger against each other: the catalog's code map says `core/vr/MakoControls` and `MakoGunner` are compiled
for every game (CMakeLists.txt lines 794-799 agree); the ledger's generated table lists `vr/MakoControls` and
`vr/MakoGunner` as LE1 only (its lines 754-755). One of the two tables counts another target; not resolved here.
