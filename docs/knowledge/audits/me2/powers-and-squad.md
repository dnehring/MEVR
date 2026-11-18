---
type: Audit
title: ME2 audit - powers and squad
description: ME2's power wheel, lock, push, self-cast gestures, squad weapon menu, squad orders, placement, voice, Thespian and gauntlet rated contract by contract against ME1's catalog.
tags: [audit, me2, parity, powers, squad]
status: draft
timestamp: 2026-11-18T08:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:00:00Z }
game: LE2
against: ../../me1/powers-and-squad.md
commit: 940a75c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/powers-and-squad.md) by reading code and notes at 940a75c6; no game was run.

- Most of this category really is ME1's code on ME2: 27 of 39 contracts run the shared rule (same, other inputs, or a thin
  adapter). The hand power wheel, the lock, the push, placement's arc and the voice gesture have no ME2 copy.
- **Fixed 2026-11-18 (lane AU2, not run: Save 4's Soldier has none): a power the table doesn't name keeps its tile, as on
  ME3, its words from its base name, aimed at a place.** Was: the hand wheel drops every ME2 power its 26-row table does not
  name (Singularity, Shockwave, Charge, Combat Drone,
  Unity, the grenade powers): an Adept, Vanguard or Engineer loses class powers from the wheel. ME3 keeps unknown powers;
  ME2 returns nothing for them. Read from code; the only wheel seen in the game was a Soldier's.
- **Squad placement on ME2 does not go through the one dispatcher** (PWR-30): it calls `HenchmanOrderHoldPosition` itself,
  so a placed hold has no standing order, no history and no acknowledgement count. The left-grip and voice orders do.
- **The left grip has fewer claimants on ME2, so more squeezes become squad orders**: ME1's grenade slot and hot receiver
  do not exist, and the reload's claim is not asked by the flat orders or the radio.
- Three input rules ME1 had already written down were met again on ME2 on 2026-11-17 (an R3 tap cast a power, B meleed
  under placement, X was the game's own button).

| rating | count | contracts |
|---|---|---|
| same | 15 | PWR-01, 03, 04, 06, 07, 08, 10, 11, 12, 13, 21, 22, 28, 34, 36 |
| same core, other inputs | 6 | PWR-09, 17, 18, 26, 29, 39 |
| adapter | 6 | PWR-05, 14, 23, 32, 33, 35 |
| partial | 7 | PWR-02, 20, 24, 25, 30, 31, 38 |
| missing | 2 | PWR-19, 37 |
| n/a | 3 | PWR-15, 16, 27 |
| reinvented | 0 | (one leg of PWR-30, see Reinvented) |
| unknown | 0 | (open points are named in the rows) |

"Same" means the code path, numbers and gates; it does not mean seen in a headset. What was seen on the fake headset is
[research/55 "The whole wheel on Save 4"](../../../../research/55-hand-cast-powers.md) and
[research/54 "ME2 placement and orders on Save 4"](../../../../research/54-voice-squad-commands.md), both 2026-11-17.

# Contract by contract

Files: PW = [game/PowerWheel.cpp](../../../../src/game/PowerWheel.cpp), core PW =
[core/vr/PowerWheel.cpp](../../../../src/core/vr/PowerWheel.cpp), SW =
[game/SquadWeapons.cpp](../../../../src/game/SquadWeapons.cpp), SO =
[game/SquadOrders.cpp](../../../../src/game/SquadOrders.cpp), SP =
[game/SquadPlace.cpp](../../../../src/game/SquadPlace.cpp).

| id | rating | how ME2 gets it | where it differs | evidence |
|---|---|---|---|---|
| PWR-01 | same | The shared `Tick`: mode 0 (on foot in all three games), a pawn, host v28, view, controllers, menu and rail closed, weapon wheel closed, `IsCombatEnabled`. | Nothing. Seen 2026-11-17: opened 0.42 m wide at the left palm. | PW:1700-1751; [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp):40-54 |
| PWR-02 | partial | Roster from `SFXPowerManager.Powers` through `sequel_powers`; a tile only for a power `FindLe2Power` names (by base name, then name); "No power" last; pages shared. | A power not in the 26-row table is left out (`if (info == nullptr) return std::nullopt`). The table has no Singularity, Shockwave, Charge, Combat Drone, Unity, Flashbang or Inferno Grenade. ME3's reader keeps unknown powers with a generic icon. The cooldown ring: research/55 says "ME2's cooldown is not read (`roster` shows 0%)". Label spellings unchecked: reading only. | PW:351-376 (359), PW:303-318 (ME3); core PW:286-315 |
| PWR-03 | same | Shared `Pick` and `Step`. | Nothing. Seen: tiles by 7 cm of hand travel and by the stick. | PW:1753-1760; research/55 line 693 |
| PWR-04 | same | Shared machine. | Nothing. Seen: the tap, then a trigger pinch equips. A 200 ms hitch turned a tap into a hold (game time). | research/55 line 694 |
| PWR-05 | adapter | `Equip` stores the name in the mod (`gLe2Selected`); nothing is written to the game. | The game's own quick slots (LB, RB, Y) stay as they were, so the wheel's choice and the game's mapped powers are two things on ME2. The selection is dropped whenever the pawn or its power manager changes (a load, a new level); ME1's `MappedPower` lives in the game. | PW:549-553, 1709-1717 |
| PWR-06 | same | `options.powerWheel` while open; the LE2 host merges the pad through the shared `pad_merge`. | Nothing. | PW:1986-1988; [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp):353-386 |
| PWR-07 | same | `HandWheelOwnsX` and `handPowerWheel`, as ME1. | The rule is the same; the loss is not: ME2's X is its reload button. The owner's answer 2026-11-17: "automatic reload for now". See Forced by the game. | PW:1724, 1981-1985; [trilogy-plan.md](../../../trilogy-plan.md) line 1016 |
| PWR-08 | same | The LE2 host draws it from the shared `power_wheel::Layers`; the source is the gauntlet's display (`omni_tool::DisplayWorld`). | Nothing found. Headset not checked. | [SequelOverlays.cpp](../../../../src/plugin/SequelOverlays.cpp):284-300; PW:1941 |
| PWR-09 | same core, other inputs | Shared hint and coach. | The coach's controls line is "X: power wheel." (ME1's names the grenade throw). It showed ME1's line until 2026-11-17. | core [PowerWheel.hpp](../../../../src/core/vr/PowerWheel.hpp):434; research/55 lines 699-702 |
| PWR-10 | same | Shared `TickLock` and `Scan`: `IsHostile`, hidden, `bActive`, `FastTrace`; dead through `Engine.Pawn.IsDead`. | Only the `IsDead` name. Seen once: Slam locked on a drone at 9.4 m. | PW:577-590, 720-796, 1285-1305 |
| PWR-11 | same | Shared `UpdateLock`. | Not seen on ME2 (one hostile, pointed at). | PW:725-726 |
| PWR-12 | same | `kWindUpS` 1.5 s, shared. | Not seen on ME2. | PW:607 |
| PWR-13 | same | Shared `StepPush`; the 0.4 s wait after a left grip. | Seen once on ME2 by replay. | PW:618; research/55 line 697 |
| PWR-14 | adapter | `ActivatePower` with ME2's extra parameters (the head's place and facing as the "original camera"); refused first when combat is disabled or the pawn is in a blocking action. | The refusal text is "unavailable in the current game state" for both. The cast's effect on the target was not seen ("the drone did not visibly move under Slam"). | PW:865-876; research/55 line 711 |
| PWR-15 | n/a | The bolt is off (`gBoltOn = false`: "sequels use their native projectiles and effects"). | ME2's powers fly their own projectiles. From where they leave (the VR hand or the animated body's) is not recorded anywhere: a snapshot during a Throw would settle it. | PW:939-943 |
| PWR-16 | n/a | `AfterCast` does nothing more on the sequels ("the native cast owns release and animation"). | A chest cast with the weapon away took its cooldown on ME2 (research/55 line 695). A push with the weapon away was not tried. | PW:1210-1211 |
| PWR-17 | same core, other inputs | The shared chest zone and dwell. | The chest powers are ME2's: Adrenaline Rush, Barrier, Tech Armor, Tactical Cloak, Geth Shield Boost, Fortification. The settings text names only the first two. Seen: Adrenaline Rush. | core PW:692-702; PW:154-155 |
| PWR-18 | same core, other inputs | The shared gun capsule, slap and dwell, for the six ammo powers. | "Its own gun" is any gun that is not a heavy weapon (`Le2AmmoWeaponHeld`), not ME1's per-power weapon. Seen: Disruptor Ammo on the rifle; pistols and shotguns not tried. | PW:87-91, 369; core PW:700-701 |
| PWR-19 | missing | No ME2 power has the Raise gesture, and ME2's Unity is not in the table at all, so it has no tile and no gesture. | Nothing forces it: ME3 gives its revive power the raise (`NativeSelfGesture`). On ME2 Unity is reachable only through the game's own wheel (PWR-23). | core PW:286-315, 692-702; PW:321 |
| PWR-20 | partial | Ready = learned, the game's `IsEnabled`, and a gun held for the ammo powers; target hidden when not ready. | No "wrong weapon" case (any gun). The cooldown is not reliably read, so "Cooling down N%" may never be the reason given: research/55 "Not established". | PW:368-374; research/55 line 712 |
| PWR-21 | same | Shared confirmation and haptics. | Not looked at on ME2. | PW:1240-1251 |
| PWR-22 | same | Shared switch in `TickLock`. | Nothing. | PW:1322 |
| PWR-23 | adapter | `holsters::TickPowerHold` on every game since 2026-11-17: `SFXGameModeDefault.EnterPowerWheel` after 0.3 s, `ExitPowerWheel` at the release, the click never RB. | The press never reaches ME2 as R3 either (`keepPowerClick`: its R3 is the area map), the owner's answer to question 14; ME1's still does. Until 2026-11-17 an R3 tap cast a power on ME2 (Rediscovered problems). | [Holsters.cpp](../../../../src/game/Holsters.cpp):164-180, 467-523; [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp):373-377 |
| PWR-24 | partial | A left trigger pull opens ME1's *custom* drawn menu at the left hand (the squadmates' cards), never the game's wheel: `gStyleNative = false` on the sequels. | ME1's default is the game's own wheel on a hand quad. ME2 has no path to its own weapon wheel from the controllers on foot (the pull is the squad's; LB never reaches the game). The squad owns the trigger in mode 0 only (ME1: modes 0, 2, 3). The settings file still says `style = native`. With no squadmate the pull is still kept from the game; what the player then sees was not established. | SW:1693-1703, 1758-1761; [Le2SquadMenu.inc](../../../../src/game/Le2SquadMenu.inc) |
| PWR-25 | partial | The custom menu's rules (shared `squad_menu` machine): left stick the squadmate, right stick flick the weapon, let go or A gives, B cancels. | Not the native ring's steer, A and `ExitWeaponWheel`. Tiles are the squadmate's carried guns, four a page with a "more weapons" tile; the order is `OrderWeaponSwitch` and the outcome is read from `Pawn.Weapon`. Seen: one tile, Miranda, 6 of 6. | [Le2SquadMenu.inc](../../../../src/game/Le2SquadMenu.inc):12-30, 70-90; [le2-m6-squad-implementation.md](../../../le2-m6-squad-implementation.md) |
| PWR-26 | same core, other inputs | The shared pointing ("every game, both styles") preselects the pointed squadmate's card. | Not through the HUD's `nLastAngle`. Not seen on ME2 ("pointing at nobody" in every run). | SW:859; SW:1509 |
| PWR-27 | n/a | No game wheel is held, so nothing can be left open. | The menu's pause is the main menu's pause owner (`menu_pause::SetSquadPause`), tied to its world. | [Le2SquadMenu.inc](../../../../src/game/Le2SquadMenu.inc):49-66 |
| PWR-28 | same | The grip is never asked. | Nothing. | SW:1689-1703 |
| PWR-29 | same core, other inputs | The shared gesture machine with ME1's numbers; orders through `Le2Order`. | The squeeze's earlier claimants are fewer: no grenade slot, no vent, and `reload::ClaimedLeft` is not in the list. See Not forced. Seen: a move dispatched; attack not. | SO:884-967 (908, 915) |
| PWR-30 | partial | The dispatcher has an ME2 adapter: `HenchmanOrderAttackTarget`, `HenchmanOrderFollow`, `HenchmanOrderHoldPosition` after the move marker, `SquadOrderUsePower` then `ApplyTacticalOrders`. | Placement does not use it on ME2: `GiveLe2Order` in SP calls the hold and follow functions itself (ME1's branch calls `squad_orders::GiveAt` and `Give`). Attack and power orders have not been seen to act. **Fixed in code, lane AU1 (2026-11-18, not run in the game):** placement through `squad_orders::Give`/`GiveAt` (the sequels' adapter `Le2Order`); `GiveLe2Order` removed. | SO:256-278, 385-428; SP:52-96, 479-482, 502-505 |
| PWR-31 | partial | Standing orders run for the grip's and the voice's orders (seen: re-asserted once, ended with a follow when calm). | A placed hold gets none (PWR-30), so "holds until called back" rests on ME2's own AI and its 40 m tether. Each re-assert is a native order that ME2's AI may answer aloud (ME1's never chatter): not measured. The retreat to cover is unseen. | SP:502-505; research/54 lines 61-65, 38 |
| PWR-32 | adapter | `SquadContext` with ME2 readers: names through `SFXPawn.GetPrettyName`, powers through `le2_squad::Powers`, the same clustering core. | Power words come from the same 26-row table, so a squadmate's power outside it (Shockwave, Combat Drone ...) may have no word: reading only. The ledger lists `BioPawn.IsInCombatActionState` as an ME1-only handle this module asks on ME2; if it is null, `scene.combat` is never true there. | [SquadContext.cpp](../../../../src/game/SquadContext.cpp):146-150, 202-215, 299-304; ledger line 1127 |
| PWR-33 | adapter | The shared arc, ring and machine, ME1's numbers, on by default. | One more gate (`IsCombatEnabled`: no placement where combat is off) and a roster filtered to the living; a roster change while holding calls it off. Seen: 0.27 to 0.38 m from the ring. | SP:320-370; research/54 lines 45-58 |
| PWR-34 | same | The shared three-tile wheel on the power wheel's second canvas. | Seen: the left tile sent Miranda alone. The up tile needs about 13 cm of hand travel (the wheel faces the eyes from below). | research/54 lines 56-59 |
| PWR-35 | adapter | The shared landing rule; follow and regroup are `HenchmanOrderFollow`. | Dispatch is placement's own (PWR-30). Seen: the double B. Walls, slopes and the follow distance not seen on ME2. | SP:479-482, 502-505 |
| PWR-36 | same | `game/Voice` has no per-game branch: the same ear zone, holds and claims, on `runtime::Up()`. | No voice order has been heard on ME2: the protected run timed out before launch, and the first deploy had no model folder. The hold-to-talk key is off on ME2 (its capture is an LE1 renderer hook). The parser's names for ME2's squad were not checked here. | [Voice.cpp](../../../../src/game/Voice.cpp):405-416; research/54 lines 3-25 |
| PWR-37 | missing | `squad_ack::Install` needs `runtime::Ready()` and hooks ME1's `SquadHandleChatterEvent` and `PlaySoundSet`, absent on ME2. | ME2's AI answers by itself (`TriggerVocalizationEvent` 8, 7, 9); the module counts "2 expected, 0 answered" and plays no fallback. | [SquadAck.cpp](../../../../src/game/SquadAck.cpp):141-153; research/54 lines 62-65 |
| PWR-38 | partial | The shared matcher, turn rules and panel over ME2's conversation and handler readers. | Replies are taken by native slot; no mute and no `SkipNode`, so Shepard's recording plays after the player read the line. Ordinary and Investigate replies unproven. | [Thespian.cpp](../../../../src/game/Thespian.cpp):131-137, 156-163, 180-191; ledger G35 |
| PWR-39 | same core, other inputs | `omni_tool::Request` while the wheel is open and around a cast; the gauntlet through OmniTool's ME2 branch. | `UsesOmniTool` reads ME1's `BioPower.m_VFXAppearance` (not resolved for every game), so on ME2 only the table's tech discipline decides: no gauntlet for a non-tech power whose effect is the omni-tool's. | PW:1178-1200, 1881, 1976-1978 |

# Reinvented

| ME2 code | what it repeats | what differs | what unifying takes |
|---|---|---|---|
| `GiveLe2Order` in [SquadPlace.cpp](../../../../src/game/SquadPlace.cpp):52-96 | `Le2Order` in [SquadOrders.cpp](../../../../src/game/SquadOrders.cpp):256-278, reached through `squad_orders::GiveAt` / `Give` as ME1's branch does | The same two native calls and the same move marker, written twice. Placement's copy records no standing order and no history, and logs each AI state. | Delete the sequel branch at SP:479-482 and 502-505 and call `GiveAt` / `Give`; keep the living-roster filter. Check: `tools/le2-halcyon-run/place.ps1` passes and the log shows `squad orders:` lines for a placed hold. |
| `FindLe2Power` in [core/vr/PowerWheel.cpp](../../../../src/core/vr/PowerWheel.cpp):286 used as a membership test in `Le2Power` | ME3's `Le3Power` (PW:289-337) treats the table as "display text and icons only, not availability or membership" | ME2 hides what the table lacks; ME3 shows it with a generic icon and words split from the label. | One sequel reader: membership from the game (visible, learned), the table for names and icons. T44 already lists "one sequel power reader". |
| `Le2CanOrder` in SquadPlace.cpp:59-66 | `le2_squad::CanOrder` in [Le2Squad.hpp](../../../../src/game/Le2Squad.hpp), used two lines later | The same `IsCombatEnabled` question asked through a second set of handles. | Goes away with the first row. |

Already unified, for the record: the sequels' second squad menu tick (`TickLe2`, until 2026-11-16) and the second power
roster walk (ledger "Forks" 5 and 6).

# Rediscovered problems

| problem on ME2 | when, cost | ME1 already had it |
|---|---|---|
| An R3 tap cast a power (`TryUsePower 1` on the RB binding). Found by an adversarial review of 57ae5b4e, not by a lane. | 2026-11-17, lane FX2, a before and after run ([research/le2-vrik.md](../../../../research/le2-vrik.md) "The sequels' controls after the review", item 1) | Catalog "Problems ME1 already solved", first row; [research/19](../../../../research/19-body-holsters.md) "Hold only", 2026-11-05. The fix was ME1's rule moved out of its LE1 guard. |
| B's hold and double tap for placement were also the game's melee (`Console_Melee`), and the run script's verdict was a hard-coded PASS. | 2026-11-17, lanes REV and MEL; an owner question | Catalog Arbitration, row B: "The game sees no B on foot in VR (`kFlagNoMeleeButton`)". |
| X is the hand wheel's on foot, and X is ME2's reload. | 2026-11-17, an owner question ("automatic reload for now") | Catalog PWR-07 and "X both opened the wheel and threw a grenade or reached the game". |
| The coach card told ME2 players how to throw ME1's grenade. | 2026-11-17, seen in a snapshot | Not a solved problem, but ME1 text shipped unread: the same class as the settings text below. |
| A status reader asked `runtime::Ready()`, which is never true on the sequels (`PowerWheel::StatusJson`). | Fixed 2026-11-16 (ledger line 598) | The same test still gates `squad_ack::Install` (SquadAck.cpp:142) and the LE1 branch of `squad_place::Tick`. |

New on ME2, not rediscoveries: the hold order needs the squadmate's move marker placed first (nobody moved in the first
run, research/54), and `ActorGameName` is the class default "BioPawn" on every henchman.

# Forced by the game

| difference | evidence |
|---|---|
| ME2 has no `MappedPower` the mod can write; its quick slots are three bindings. The wheel's choice lives in the mod (PWR-05). | PW:70-85; research/le2-vrik.md item 1 (`Console_PowerWheelAndQuickPower2`) |
| No grenades and no weapon heat: ME1's grenade slot and vent do not exist, so they claim no left squeeze. | ledger G42; [trilogy-plan.md](../../../trilogy-plan.md) T26 |
| ME2's weapon wheel is another UI, so the squad's weapons use the drawn menu and a squadmate's inventory is a list of weapon objects (PWR-24, 25). Stated in code, not shown by a run: T45 still asks to "port it or name the game fact that rules it out". | SW:1759; [le2-m6-squad-implementation.md](../../../le2-m6-squad-implementation.md) |
| Orders are `HenchmanOrder*` on the pawn, not `QuickCommand*` by index; a hold needs `UpdateMoveToIndicator` first. | research/54 lines 33-43 |
| ME2's AI voices its own order answers; ME1's ack functions are absent (PWR-37). | research/54 lines 62-65; ledger G34 |
| X is reload, B is melee, cover exit and leave, R3 is the area map: keeping ME1's X, B and R3 rules costs those buttons. The owner approved each: reload automatic (2026-11-17), B as on ME1 with a Leave entry in our menu (question 16), R3 kept (question 14). | trilogy-plan.md lines 1006-1018; CLAUDE.md "The owner's answers of 2026-11-17" |
| In the conversation mode the triggers are the game's LT and RT (interrupts); nothing of ours claims them there. Owner's rule 11. ME1 has no interrupts. | [PadMerge.cpp](../../../../src/plugin/PadMerge.cpp):129-145, 153-155 |
| The player's powers are native custom casts that own their release (PWR-15, 16). | PW:939-943, 1210-1211 |

# Not forced

Inputs and gestures that act differently on ME2 than on ME1, with nothing in the game requiring it. None of these has a
recorded owner answer unless said.

| what | on ME1 | on ME2 | evidence |
|---|---|---|---|
| A free left grip squeeze at the left chest | Takes a grenade (claimed before the flat order) | Points a squad order: let go on nothing is a follow order to the whole squad | SO:908 |
| A left grip squeeze while the reload by hand is on | n/a | The reload claims it, but the flat orders and the radio do not ask `reload::ClaimedLeft`, so the same squeeze also points an order. The reload is off by default (the owner, 2026-11-16), so this bites when it comes back. | SO:908; [Voice.cpp](../../../../src/game/Voice.cpp):416; [Reload.cpp](../../../../src/game/Reload.cpp):504, 586 |
| The power wheel's roster | Every mappable power | Only table powers (PWR-02) | PW:359 |
| Unity | The raise gesture | No tile, no gesture (PWR-19) | core PW:286-315 |
| Placement's dispatch | The one dispatcher | Its own calls (PWR-30) | SP:479-505 |
| The equipped power after a load | Still equipped (the game holds it) | Cleared | PW:1711-1717 |
| The chest gesture's powers | Four named in the settings text | Six, two named in the text (Tech Armor, Tactical Cloak, Geth Shield Boost and Fortification are unlisted) | core PW:700; PW:155 |
| The left trigger in the game's own wheels | The squad's in modes 0, 2 and 3 | The squad's in mode 0 only | SW:1697-1700 |
| The Y button | The game's Y | The game's Y, whose ME2 binding this audit did not find written down. If it is a quick-power tap, it is a cast route ME1's "a tap casts nothing" rule does not cover. A `mevr calltrace` over a Y press on Save 4 settles it. | not established |
| `LE2MEVR.ini` text | n/a | The shared blocks are written unchanged: `[Squad] style = native` (ignored on ME2), `[Mako]`, `[Cockpit]`, `[Vent]`, `[Throwables]` sections for mechanics ME2 lacks | [HandCalibration.cpp](../../../../src/game/HandCalibration.cpp):2964-2998 |

Where this audit differs from [the parity ledger](../../../trilogy-parity-ledger.md): G29 ("adapted V") is right about
the mechanism but does not see the roster gap; G32 ("adapted V") does not see that placement's sequel branch is a second
order path, which the ledger's own vocabulary would call a fork inside a shared file. G30, G31, G33, G34 and G35 agree
with what was read.

# What to do

Ordered by what a player meets first. No new interaction is proposed.

1. **Every learned power on ME2's wheel.** In `Le2Power` ([game/PowerWheel.cpp](../../../../src/game/PowerWheel.cpp):351)
   take membership from the game as `Le3Power` does and use `FindLe2Power` for names and icons only; add the missing rows
   and their aim kinds to [core/vr/PowerWheel.cpp](../../../../src/core/vr/PowerWheel.cpp):286. First confirm the labels:
   `mevr powerwheel unlearned on` then `mevr powerwheel roster` on Save 4 lists what the game has against what the wheel
   shows. Closes when an Adept, a Vanguard and an Engineer save each show their class powers.
2. **Unity on ME2** (PWR-19): give ME2's revive power ME1's raise gesture through the same reader. Closes with
   `tools/le2-halcyon-run/powers.ps1` casting it by the raised hand. Question for the owner only if ME2's Unity should
   stay on the game's wheel instead.
3. **Placement through the dispatcher** (PWR-30, 31): replace the sequel branch in
   [game/SquadPlace.cpp](../../../../src/game/SquadPlace.cpp) with `squad_orders::GiveAt` / `Give`. Closes when
   `place.ps1` passes and a placed hold shows a standing order in `"mod"."squadOrders"`.
4. **The left squeeze's claimants** (PWR-29): add `reload::ClaimedLeft()` to
   [game/SquadOrders.cpp](../../../../src/game/SquadOrders.cpp):908 and [game/Voice.cpp](../../../../src/game/Voice.cpp):416
   before the reload returns. **Owner question:** on ME2 a free left squeeze at ME1's grenade slot points a squad order;
   keep, or leave that zone dead?
5. **The Y button and the solo left trigger**: one traced run on Save 4 (`mevr calltrace 10 trypower usepower`, a Y tap;
   then a left trigger pull with no squad) to say what each does. If Y casts, apply ME1's hold-only rule; report the solo
   pull to the owner.
6. **The squad's answer** (PWR-37): port `game/SquadAck` to listen for `SFXGame.TriggerVocalizationEvent` and start on
   `runtime::Up()` (ledger G34). Also measure whether re-asserted standing orders make ME2's squad talk every 4 s. Closes
   when `place.ps1` reports the expected answers heard.
7. **Attack and power orders in combat** (PWR-30): `mevr order` lines on a combat save, judged by the target's health and
   the power's cooldown, not the dispatch log (T46's closing check).
8. **The cooldown on ME2** (PWR-20): make `sequel_powers::Seconds` read it or name why it cannot; closes when a gesture
   during a cooldown answers "Cooling down N%".
9. **The weapon wheel's style** (PWR-24): T45's open item: port ME1's native wheel at the hand or write down the game
   fact that rules it out; meanwhile correct the `[Squad] style` text ME2 writes.
10. **Voice and Thespian** (PWR-36, 38): a `voice.request wav` run on ME2 with the models deployed; Thespian's mute and
    skip (T48).
11. **ME2's settings text**: the `[PowerWheel]` chest list, and the ME1-only sections in `LE2MEVR.ini`.

# Citations

- [ME1 catalog: powers and squad](../../me1/powers-and-squad.md); [How to catalog](../../how-to-catalog.md)
- [research/55](../../../../research/55-hand-cast-powers.md) "The whole wheel on Save 4"
- [research/54](../../../../research/54-voice-squad-commands.md) "ME2 host integration", "ME2 placement and orders on Save 4"
- [research/le2-vrik.md](../../../../research/le2-vrik.md) "The sequels' controls after the review of 57ae5b4e"
- [research/70](../../../../research/70-thespian-mode.md) "ME2 cinema adapter"
- [docs/le2-m6-squad-implementation.md](../../../le2-m6-squad-implementation.md), [docs/trilogy-plan.md](../../../trilogy-plan.md) (T44 to T48, the answers of 2026-11-17), [docs/trilogy-parity-ledger.md](../../../trilogy-parity-ledger.md) (G29 to G35)

Catalog rows this audit found inexact about ME1: PWR-24's gate says "game mode 0 or 3"; the code takes the trigger in
modes 0, 2 and 3 (SW:1700, "on foot, the power wheel, the weapon wheel").
