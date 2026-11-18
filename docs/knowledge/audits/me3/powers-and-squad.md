---
type: Audit
title: ME3 audit - powers and squad
description: ME3's power wheel, lock, push, self-cast gestures, squad weapon menu, squad orders, placement, voice, Thespian and gauntlet rated contract by contract against ME1's catalog, with ME2's findings checked on ME3.
tags: [audit, me3, parity, powers, squad]
status: draft
timestamp: 2026-11-18T10:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:00:00Z }
game: LE3
against: ../../me1/powers-and-squad.md
commit: 583591c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/powers-and-squad.md) by reading code and notes at 583591c6; no game was
run. ME3 has been run only on Save 31 (the Normandy, a non-combat Vanguard with no squad): the roster reads and the
wheel stays shut where combat is disabled. Nothing has been cast, ordered, placed or spoken on ME3
([ledger](../../../trilogy-parity-ledger.md) G29 to G35, all "U").

- The hand wheel, the lock, the push, the gestures, placement's arc and the radio are ME1's shared code on ME3: 30 of
  39 contracts run the shared rule (`same`, `same core, other inputs` or `adapter`). ME3's power adapter
  ([Le3Powers.hpp](../../../../src/game/Le3Powers.hpp)) is the better of the two sequels': membership, availability,
  cooldown, range and target validity all come from the game, and the gesture is derived from the power's class.
- **Finding 1: squad placement on ME3 bypasses the one dispatcher, exactly as on ME2** (PWR-30): the same
  `GiveLe2Order` in `SquadPlace.cpp`. A placed hold has no standing order and no history.
- **Finding 2: every aimed ME3 power is a "place" power to the shared core** (`Aim::Place` for anything that is not a
  buff, ammo or revive). ME1's "a target power needs a locked enemy" refusal does not exist on ME3: a push with nothing
  locked casts at wherever the hand's ray lands. Grenades are among them: a grenade is thrown by the left hand's push.
- **Finding 3: the left grip has fewer claimants, and the left trigger never reaches ME3's own weapon wheel** (the
  drawn squad menu instead, mode 0 only). Both are ME2's findings unchanged; on ME3 the second one also bears on the
  heavy weapon (see [the holsters audit](holsters-throwables-melee.md), HOL-20).
- The squad's answer (PWR-37) is missing and Thespian (PWR-38) still lets Shepard repeat the line, as on ME2.

| rating | count | contracts |
|---|---|---|
| same | 14 | PWR-01, 03, 04, 06, 07, 08, 11, 12, 13, 21, 22, 28, 34, 36 |
| same core, other inputs | 9 | PWR-09, 10, 14, 17, 18, 20, 26, 29, 39 |
| adapter | 7 | PWR-02, 05, 19, 23, 32, 33, 35 |
| reinvented | 0 | (one leg of PWR-30, see Reinvented) |
| partial | 5 | PWR-24, 25, 30, 31, 38 |
| missing | 1 | PWR-37 |
| n/a | 3 | PWR-15, 16, 27 |
| unknown | 0 | (open points are named in the rows) |

"Same" means the code path, numbers and gates; none of it has been seen in an ME3 fight.

# Contract by contract

Files: PW = [game/PowerWheel.cpp](../../../../src/game/PowerWheel.cpp), L3P =
[game/Le3Powers.hpp](../../../../src/game/Le3Powers.hpp), NP =
[core/vr/NativePower.hpp](../../../../src/core/vr/NativePower.hpp), SW =
[game/SquadWeapons.cpp](../../../../src/game/SquadWeapons.cpp), SO =
[game/SquadOrders.cpp](../../../../src/game/SquadOrders.cpp), SP =
[game/SquadPlace.cpp](../../../../src/game/SquadPlace.cpp), SC =
[game/SquadContext.cpp](../../../../src/game/SquadContext.cpp).

| id | rating | how ME3 gets it | where it differs | evidence |
|---|---|---|---|---|
| PWR-01 | same | The shared `Tick`: mode 0, a pawn, host v28, view, controllers, menu and rail closed, weapon wheel closed, `IsCombatEnabled`. | Nothing. Run on Save 31: stays shut where combat is disabled. Mode 0 excludes the Atlas (2) and injured Shepard (6). | PW:1700-1751 |
| PWR-02 | adapter | The roster is the game's own power instances (`sequel_powers::ForEach`, `Le3Power`): visible, a valid native state, learned; a revive instance even if not "visible". `FindLe2Power` only supplies names and icons; an unnamed label is split into words with a generic icon. The cooldown and a grenade's count are read live. | Not ME1's fixed 19 labels: every learned power shows, DLC's too. Powers the table lacks get discipline 2 and icon 0 (most of ME3's own). Run: Pull, Shockwave, the ammo powers, Biotic Charge, Discharge, Unity, Flare listed. | PW:289-337; NP:22-43; ledger G29 |
| PWR-03 | same | Shared `Pick`, `Step`. | Not seen on ME3. | PW:1753 |
| PWR-04 | same | Shared machine. | Not seen on ME3. | PW:1753 |
| PWR-05 | adapter | `Equip` stores the name in the mod (`gLe2Selected`); nothing is written to the game. | As ME2: the wheel's choice and the game's mapped powers are two things; the choice is dropped when the pawn or its power manager changes. | PW:94-98, 549-553, 1709-1717 |
| PWR-06 | same | `options.powerWheel`; the LE3 host merges the pad through `pad_merge`. | Nothing. | PW:1986-1988; [PadMerge.cpp](../../../../src/plugin/PadMerge.cpp) |
| PWR-07 | same | `HandWheelOwnsX`, `handPowerWheel`. | ME3's X is reload (tap) and weapon swap (hold): both gone on foot. "Automatic reload for now" (the owner, for the sequels; checked on ME2). | PW:1724, 1981-1985 |
| PWR-08 | same | The LE3 host compiles the wheel's draw (`plugin/PowerWheel*`, `Glow`, `PowerIcons`) and the shared sequel overlays. | Not seen in a headset. | [CMakeLists.txt](../../../../CMakeLists.txt):1177-1182; [SequelOverlays.cpp](../../../../src/plugin/SequelOverlays.cpp) |
| PWR-09 | same core, other inputs | Shared hint and coach. | The coach kind comes from `CoachFor(aim, gesture)` with ME3's coarse aim (PWR-14): every aimed power gets the "place" card. The settings text names classes ("native buffs", "ammo powers", "Unity"), not powers. | PW:151-153, 323-324 |
| PWR-10 | same core, other inputs | Shared `TickLock`, `UpdateLock`, `FastTrace`; dead through `Engine.Pawn.IsDead`. | Who can be locked is `le3_powers::ValidTarget(power, pawn)`, not `IsHostile`: a support power may lock a living ally. The dormant test (`bActive`) is dropped: ME3 has no `m_oBehavior`; only hidden and dead pawns are skipped. | PW:740-746, 774-776 |
| PWR-11 | same | Shared `UpdateLock`. | Not seen. | PW:725-726 |
| PWR-12 | same | `kWindUpS` 1.5 s. | Not seen. | PW:607 |
| PWR-13 | same | Shared `StepPush`, the 0.4 s wait after a left grip. | Not seen. | PW:618 |
| PWR-14 | same core, other inputs | `ActivatePower` with all five ME3 parameters bound (the head's place and facing as the camera); before it the selected power is re-read, and `CanUse`, the cooldown, `IsCombatEnabled`, `ValidTarget` and `LocationInRange` are asked. | **No power is `Aim::Target`**: anything not self-cast is `Aim::Place`, so ME1's "needs a locked enemy" refusal never happens; with no lock the push casts at the ray's landing point if in the power's native range. **A grenade is such a power**: equipped, a left-hand push throws it. One refusal text for five reasons ("native power/loadout/target/resource guard refused"). Success is "request accepted", not a seen cast. | PW:323-324, 836-864, 889-890 |
| PWR-15 | n/a | The bolt is off on the sequels. | Where ME3's own projectile leaves from (the VR hand or the animated body) is not recorded: a snapshot during a Pull settles it. | PW:939-940 |
| PWR-16 | n/a | `AfterCast` does nothing more ("the native cast owns release and animation"). | ME3 has no weapon-away state on a mission by default. | PW:1210-1211 |
| PWR-17 | same core, other inputs | The shared chest zone and dwell. | The chest powers are "every native power of type Buff" that is not an ammo, weapon-buff or revive class; ME1 names four. A buff that is not felt as a self-cast (a squad buff) would still be a chest gesture: [research/le3-powers.md](../../../../research/le3-powers.md) "Other buff classes retain the chest role until their particular behavior is audited". | PW:320-322; NP:12-14 |
| PWR-18 | same core, other inputs | The shared gun capsule, slap and dwell for the ammo classes and the audited Marksman override. | "Its own gun" is the game's `CanUse`, not ME1's per-power weapon table; no "wrong weapon" wording. | PW:320-321, 333 |
| PWR-19 | adapter | The revive class (Unity) gets ME1's raise gesture (`NativeSelfGesture`). | ME3 has it where ME2 does not. Native effect not seen (Unity read "disabled" on Save 31). | PW:292-295, 321; NP:12-14 |
| PWR-20 | same core, other inputs | Ready = learned, the game's `enabled`, cooldown 0, and for a self power `CanUse(object, nullptr)`. | The refusal reasons `DrawWeapon` / `WrongWeapon` are not told apart from "unavailable": the game's one answer. | PW:330-334 |
| PWR-21 | same | Shared confirmation and haptics. | Not seen. | PW:1240-1251 |
| PWR-22 | same | Shared switch in `TickLock`. | Nothing. | PW:1322 |
| PWR-23 | adapter | `holsters::TickPowerHold`: `SFXGameModeBase.EnterPowerWheel` after 0.3 s, `ExitPowerWheel` at the release; a late opening is closed. | The press never reaches ME3 as R3 (question 14). Not run where combat is enabled. | [Holsters.cpp](../../../../src/game/Holsters.cpp):163-171, 467-533 |
| PWR-24 | partial | A left trigger pull opens the drawn squad menu at the left hand (`gStyleNative = false`), never ME3's weapon wheel. | As ME2: the squad owns the trigger in mode 0 only (ME1: modes 0, 2, 3); the game sees no LB; `[Squad] style = native` is still written to the file. Shepard's own weapon wheel has no controller route (see HOL-20). | SW:1693-1703, 1759 |
| PWR-25 | partial | The custom menu's rules over ME2's adapter with ME3's squad list (`squadMembers`) and input-method parameter. | Not the native ring. Not run: Save 31 has no squad. | SW:195-222, 1666-1668; [Le2SquadMenu.inc](../../../../src/game/Le2SquadMenu.inc) |
| PWR-26 | same core, other inputs | The shared pointing preselects a card. | Not through `nLastAngle`. Not seen. | SW:859 |
| PWR-27 | n/a | No game wheel is held. | — | [Le2SquadMenu.inc](../../../../src/game/Le2SquadMenu.inc) |
| PWR-28 | same | The grip is never asked. | Nothing. | SW:1689-1703 |
| PWR-29 | same core, other inputs | The shared gesture machine, ME1's numbers; orders through `Le2Order`. | Fewer earlier claimants: no grenade slot, no vent (both modules are inactive on ME3), and `reload::ClaimedLeft` is not in the list. A free left squeeze at the left chest points a squad order. | SO:908 |
| PWR-30 | partial | The dispatcher's sequel adapter: `HenchmanOrderAttackTarget`, `HenchmanOrderFollow`, `HenchmanOrderHoldPosition` after the move marker; a power through `SquadOrderUsePower` with ME3's extra parameters (no `ApplyTacticalOrders`: on ME3 it consumes the player's pending order). | **Placement does not use it**: `GiveLe2Order` in SP calls hold and follow itself. No order has been seen to act on ME3. **Fixed in code, lane AU1 (2026-11-18, not run in the game):** placement through `squad_orders::Give`/`GiveAt` (the sequels' adapter `Le2Order`); `GiveLe2Order` removed. | SO:256-278, 385-428; SP:52-96, 479-505 |
| PWR-31 | partial | Standing orders run for the grip's and the voice's orders. | A placed hold gets none. "Calm" needs `scene.combat` false, and on ME3 that is "a gun is held and combat is enabled" (ME1: the weapon drawn): with ME3's gun always in the hand (holsters audit, HOL-11) a standing order never ends as calm on a mission, only when relieved, when the group is gone or stale after 180 s. Reading only. | SC:299-301; SO:618; [SquadStanding.cpp](../../../../src/core/vr/SquadStanding.cpp):84 |
| PWR-32 | adapter | `SquadContext` with ME3 readers: names from `GetActorGameName`, powers from the native list with a self / hostile+ally+location class per power, health from `Engine.Pawn.Health`. | Hostile type words are the actor's game name, not ME1's actor type. | SC:202-213, 347-362 |
| PWR-33 | adapter | The shared arc, ring and machine, ME1's numbers, on by default. | As ME2: one more gate (`IsCombatEnabled`), a roster of the living (`RecipientReady`). Not run (no squad on Save 31). | SP:320-370, 70-72 |
| PWR-34 | same | The shared three-tile wheel on the power wheel's second canvas. | Not seen. | SP |
| PWR-35 | adapter | The shared landing rule; follow and regroup are `HenchmanOrderFollow`. | Dispatch is placement's own (PWR-30). | SP:479-505 |
| PWR-36 | same | `game/Voice` has no per-game branch. | Not heard on ME3. The host compiles `plugin/Voice` only in the configuration with the main menu (CMakeLists.txt:1189-1191). The parser's names for ME3's squad were not checked here. | [Voice.cpp](../../../../src/game/Voice.cpp):405-416 |
| PWR-37 | missing | `squad_ack::Install` needs `runtime::Ready()`, never true on the sequels, and hooks ME1's functions. | Which ME3 event voices an order's answer is not written down anywhere (ME2's is `TriggerVocalizationEvent`). | [SquadAck.cpp](../../../../src/game/SquadAck.cpp):142; SO:983 |
| PWR-38 | partial | The shared matcher, turn rules and panel over ME3's `BioConversationController` readers; the reader seen on three native wheels (ledger G35). | `kSkipSpokenReply = false`: no mute, no skip, Shepard says the line again (question 8, open). Spoken selection unverified. Thespian stands down unless the host owns VR and does not block it. | [Thespian.cpp](../../../../src/game/Thespian.cpp):41-43, 57-116; [Mod.cpp](../../../../src/mod/Mod.cpp):924-940 |
| PWR-39 | same core, other inputs | `omni_tool::Request` while the wheel is open and around a cast; OmniTool's ME3 branch. | `UsesOmniTool` reads ME1's `m_VFXAppearance` (not resolved on ME3), so only the table's tech discipline decides, and a power the table lacks is discipline 2: ME3's own tech powers get no gauntlet around the cast. | PW:1178-1200, 323 |

# Inputs that act differently on ME3

| input | ME1 | ME3 | forced? | the owner |
|---|---|---|---|---|
| left-hand push, a "target" power equipped, nothing locked | refused: "needs a locked enemy" | cast at where the ray lands, if in range | No: the adapter maps every aimed power to Place | not asked |
| left-hand push, a grenade equipped | n/a (the grenade is a grip at the chest and a throw) | throws the grenade (a power) | Yes in kind: ME3's grenades are powers; the gesture is the wheel's | not asked as an interaction |
| open left palm on the chest | four named powers | any native buff-type power | Partly | not asked |
| left hand pointed at a squadmate, a support power equipped | no lock (hostiles only) | may lock the ally | Yes: ME3 has ally powers | — |
| free left grip at the left chest | a grenade | a squad order (let go on nothing: follow) | Yes for the grenade; the fall-through is ours | not asked (ME2's open question) |
| left trigger, on foot | the game's weapon wheel at the hand | the drawn squad menu; no route to ME3's weapon wheel | Stated in code, not shown by a run | question 9, open |
| left trigger in the game's own wheels | the squad's | the game's | No | not asked |
| X | power wheel; grenade is physical | power wheel; no reload, no swap | The clash is ours | "automatic reload for now" |
| B | squad placement | squad placement; no melee, heavy melee or leave | The binding is the game's | approved, question 16 |
| right stick click | hold: the game's wheel; R3 reaches the game | hold: the game's wheel; R3 kept | Partly | approved, question 14 |

# Against ME2

| ME2 finding | on ME3 |
|---|---|
| The hand wheel drops every power its table does not name (PWR-02) | Fine: ME3 takes membership from the game. The cost moved to presentation: generic icons, discipline 2, no gauntlet (PWR-39). |
| Placement does not go through the one dispatcher (PWR-30) | Same defect: the same function. |
| The left grip has fewer claimants; `reload::ClaimedLeft` not asked (PWR-29) | Same defect. |
| Unity has no tile and no gesture (PWR-19) | Fine: the revive class gets the raise. |
| The cooldown is not read (PWR-20) | Fine as read: native remaining and total. Not seen. |
| "Its own gun" is any non-heavy gun (PWR-18) | Different: the game's `CanUse` decides. |
| `UsesOmniTool` by table discipline only (PWR-39) | Same defect, wider (few ME3 powers are in the table). |
| The drawn squad menu instead of the native wheel, mode 0 only, `style = native` in the file (PWR-24, 25) | Same defect. |
| `squad_ack` asks `Ready()` (PWR-37) | Same defect. |
| Thespian without mute or skip (PWR-38) | Same defect. |
| Rediscovered: R3 tap cast a power, B meleed under placement, X is the game's button | Fixed for both sequels in the shared code; none run in an ME3 fight. |
| The coach card showed ME1's grenade line | Fixed for the sequels; ME3's cards have the coarse-aim issue instead (PWR-09). |
| `scene.combat` may never be true on ME2 (`IsInCombatActionState`) | Different: on ME3 it is true whenever a gun is held (PWR-31). |
| The Y button's binding not written down | Same: not found for ME3 either; one `mevr calltrace` over a Y tap settles it. |
| ME1-only sections in the sequel's ini | Same: `[Mako]`, `[Cockpit]`, `[Vent]`, `[Throwables]` are written to `LE3MEVR.ini`. |

# Reinvented

| ME3 code | what it repeats | what differs | what unifying takes |
|---|---|---|---|
| `GiveLe2Order` in [SquadPlace.cpp](../../../../src/game/SquadPlace.cpp):78-96 | `Le2Order` in [SquadOrders.cpp](../../../../src/game/SquadOrders.cpp):256-278 through `squad_orders::GiveAt` / `Give` | The same native calls and marker written twice; placement's records no standing order. | Delete the sequel branch at SP:479-482 and 502-505 for both sequels at once. Check: a placed hold shows in `"mod"."squadOrders"`. |
| `Le2CanOrder` in SquadPlace.cpp:59-66 | `le2_squad::CanOrder` | The same question through a second set of handles. | Goes with the row above. |
| `Le3Power` and `Le2Power` in [PowerWheel.cpp](../../../../src/game/PowerWheel.cpp):289-376 | each other | ME3's is the model (membership from the game, the table for display); ME2's hides what the table lacks. | One sequel reader over `sequel_powers`, class-derived gestures where the game has classes (T44). |
| The pawn walk in `Scan` (PW:736-738), `SquadContext` (SC:340) and `melee::FindEnemy` | each other, and `LivePawns` in `game/ConversationWorld` | Three raw walks with a guard; none stops at the first dead entry. | One `player::ForEachLivePawn`. Every game. |

# Rediscovered problems

| problem on ME3 | when, cost | ME1 already had it |
|---|---|---|
| The R3 click cast on a tap and opened the area map on a hold. | 2026-11-17, lanes FX2 and CTL, an owner question (14). | Catalog "Problems ME1 already solved", first row; research/19 "Hold only". |
| B under placement was the game's melee. | 2026-11-17, lane MEL. | Catalog Arbitration, row B. |
| The area map's close was restarted by every B the mod sent. | 2026-11-17 ([gotcha](../../gotchas/me3-area-map-b-restarts-close.md)). | Near miss: research/57 section 3 on ME1 (B to the GUI manager, 4 ticks apart, at most 6). Outside this category's contracts, recorded because it is the squad and powers lanes' menu path. |
| A status or install path asks `runtime::Ready()`. | Still live: `squad_ack` (SquadAck.cpp:142, SO:983). | The port's own class (ME2's audit lists it); not ME1's. |
| `ApplyTacticalOrders` after a squad power order would have consumed the player's pending order. | Caught by reading the native code before a run ([research/le3-squad.md](../../../../research/le3-squad.md) "Native ownership and dispatch audit"). | Not ME1's; new on ME3. |

# Forced by the game

| difference | evidence |
|---|---|
| No writable `MappedPower`; the wheel's choice lives in the mod (PWR-05). | PW:94-98 |
| Grenades are powers; no weapon heat: ME1's grenade slot and vent claim no left squeeze. | [research/le3-powers.md](../../../../research/le3-powers.md); ledger G41, G42 |
| Powers are native custom actions that own their release and projectile (PWR-15, 16). | PW:939-940, 1210-1211; L3P |
| Ally and location powers exist: a lock on an ally is the game's own rule (PWR-10). | L3P `ValidTarget`; research/le3-powers.md "Native self, ally and location admission" |
| No `m_oBehavior`: no `bActive` dormancy test (PWR-10). | PW:740-741 |
| Orders are `HenchmanOrder*` with an input-method parameter; a squad power order must not be followed by `ApplyTacticalOrders`. | SO:403-424; research/le3-squad.md |
| X, B, R3 carry reload and swap, melee and leave, cover turn and area map. | trilogy-plan.md questions 14, 16 |
| ME3's weapon wheel is another UI (stated in code, not shown by a run; question 9 open). | SW:1759 |

# Not forced

- Every aimed power as `Aim::Place` (PWR-14): the native target rules are read (`ValidTarget`), so a power that
  needs an actor could be told apart from one that takes a point.
- Placement's own dispatch (PWR-30).
- `scene.combat` as "a gun is held" (PWR-31) on a game where the gun is always held.
- The flat order and the radio not asking `reload::ClaimedLeft` (PWR-29).
- The squad's trigger in mode 0 only (PWR-24).
- `UsesOmniTool` and the icons by ME2's table (PWR-39, PWR-02).
- `squad_ack` behind `Ready()` (PWR-37).
- The ME1-only sections and `style = native` in `LE3MEVR.ini`.

Where this audit differs from [the parity ledger](../../../trilogy-parity-ledger.md): G32's LE3 cell ("adapted U: the
same order path") is the placement fork ME2's audit named; G29's does not show the coarse aim (PWR-14) or that a
grenade is a pushed power; G31's does not show `scene.combat`'s meaning on ME3. G30, G33, G34 and G35 agree with what
was read.

# What to do

Ordered by what a player meets first. No new interaction is proposed.

1. **The combat fixture, then lane SQ's list** ([research/le3-combat-fixtures.md](../../../../research/le3-combat-fixtures.md)
   "The powers and the squad on the first combat save"): one cast of each gesture kind, one push at a lock, one
   order of each kind, a placed hold. Until then every row here is reading.
2. **Owner question: the grenade on ME3.** Equipped on the wheel it is thrown by the left hand's push at the ray's
   landing point. Is that ME3's grenade, or should ME1's grip at the chest and throw reach `ActivatePower` for the
   grenade power? (An interaction question; nothing changes without an answer.)
3. **Target powers need a target** (PWR-14): derive `Aim::Target` from the native power where it refuses a location
   (L3P), so ME1's "needs a locked enemy" answer returns. Check: `mevr powerwheel push` with nothing locked and Pull
   equipped is refused by name, not cast at the floor.
4. **Placement through the dispatcher** on both sequels (PWR-30, 31): `SquadPlace.cpp:479-505`. Check as ME2's item 3.
5. **`scene.combat` on ME3** (PWR-31): a fact that means "fighting" (hostiles awake near, or the game's own combat
   flag), in `SquadContext.cpp:299-301`. Check: a hold given in a quiet room ends as calm after 8 s.
6. **The left squeeze's claimants** (PWR-29): `reload::ClaimedLeft()` in `SquadOrders.cpp:908` and `Voice.cpp:416`;
   ME2's owner question about the dead grenade zone applies to ME3 unchanged.
7. **The squad's answer** (PWR-37): find ME3's vocalization event, start on `Up()`.
8. **The gauntlet and icons for ME3's own powers** (PWR-39, PWR-02): discipline from the native power, not ME2's
   table.
9. **Questions 8 and 9 stay open** (Thespian while Shepard speaks; the squad weapons' style). Meanwhile correct the
   `[Squad] style` text ME3 writes.
10. **The Y button**: one traced tap on the fixture.

# Citations

- [ME1 catalog: powers and squad](../../me1/powers-and-squad.md); [the ME2 audit](../me2/powers-and-squad.md);
  [How to catalog](../../how-to-catalog.md)
- [research/le3-powers.md](../../../../research/le3-powers.md), [research/le3-squad.md](../../../../research/le3-squad.md),
  [research/le3-combat-fixtures.md](../../../../research/le3-combat-fixtures.md)
- [research/55](../../../../research/55-hand-cast-powers.md), [research/54](../../../../research/54-voice-squad-commands.md),
  [research/70](../../../../research/70-thespian-mode.md)
- [docs/trilogy-plan.md](../../../trilogy-plan.md) (T44 to T48, questions 8, 9, 14, 16),
  [docs/trilogy-parity-ledger.md](../../../trilogy-parity-ledger.md) (G29 to G35)

Catalog rows found inexact about ME1: PWR-31 and PWR-32 do not say that ME1's "combat" for the calm rule is
`IsInCombatActionState`, the weapon drawn (SC:303; the holsters use the same call as "drawn"): on ME1 too a standing
order does not end as calm while the gun is out. The catalog's "without combat" reads as "no fighting".
