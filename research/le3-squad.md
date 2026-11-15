# LE3 squad adapter: shared menu, placement and voice

2026-11-15. Implementation candidate, not native or headset acceptance. Builds and offline tests do not prove a companion obeyed an order.

## Reuse and differences audited

Reuse `game/SquadWeapons.cpp`, `Le2SquadMenu.inc`, `SquadContext.cpp`, `SquadOrders.cpp`, `SquadPlace.cpp` and the existing core menu, parser, placement machine and host view. The legacy `Le2Squad.hpp` filename now holds explicit sequel adapters; it is not a second menu or order framework. Existing left-hand ownership, pause ownership, generation cancellation, inventory revalidation and observed weapon confirmation remain in use.

Local reference: `external/LExSDKv2/Src/LESDK/LE3/{SFXGame_classes,SFXGame_f_structs,Engine_classes}.hpp`. Installed `SFXGame.pcc` script inspection used the existing private scanner, with extracts kept in ignored `build-le3-squad-audit`. No decompiled game implementation is included here.

| Contract | LE3 implementation |
|---|---|
| Current companions | `BioWorldInfo.m_playerSquad` -> `BioBaseSquad.Members`, excluding the player and requiring `SFXPawn_Henchman`. LE2's `GetSquadSize` / `GetMember` functions are not assumed. |
| Dispatch admission | Re-read current squad membership, living pawn, `SFXAI_Henchman` controller and its actual `IsEnabled` method. Require gameplay mode 0 and native combat-input admission. |
| Names | `BioPawn.GetActorGameName` FString return, freed through the proven engine allocator; class name fallback. LE3 has no LE2 behavior/name field. Returned string ownership was verified from the native exec after the 259-row probe; see the native audit below. |
| Weapons | Actual inventory linked list plus equipped object, no invented loadout. LE3 pistol/SMG classes are `SFXWeapon_Pistol_Base` / `SFXWeapon_SMG_Base`. The four menu slots represent carried objects, not four permitted weapon types; inventories above four objects use three weapons plus a More weapons tile, preserving the existing four-tile host ABI. Voice sees the complete inventory independently of the visible page. |
| Weapon command | `OrderWeaponSwitch(oOrderedPawn,Wpn,eInputMethod=0)`. Require the requested object still belongs to that member; report success only when the equipped object is observed. |
| Follow / hold / attack | Existing native `HenchmanOrderFollow`, `HenchmanOrderHoldPosition`, `HenchmanOrderAttackTarget`. LE3 adds `eInputMethod`; installed script defaults use `CIM_Default=0`. Do not invoke Kinect-specific handling or LE1 formation/movement writes. |
| Powers | Shared `Le3Powers.hpp` reads the member's actual `SFXPowerManager.Powers`. Native rank, visibility, enabled state, cooldown and grenade-count override remain authoritative. DLC/unknown identities enter the existing dynamic voice roster. |
| Power command | `SquadOrderUsePower`: explicit name, recipient, queue=0, indicator=true, desired selection target, input method=0, desired target and desired location. Recheck current inventory/learned state, cooldown and grenade observations, plus the actual target predicate; preserve the native player-ordered setup before its `CanUsePower` evaluation. Inspect native bool return. A true return is acceptance of the request, not proof of a cast. |

The installed power-wheel path calls `SFXGameModeDefault.UsePower`, which calls `SquadOrderUsePower`, using queue 1 only for permitted blocking-action cases, then `ApplyTacticalOrders`. Inspection of `ApplyTacticalOrders` shows it consumes the **player's** pending power/weapon and clears that player order. The LE3 companion-only adapter does not flush an unrelated player order. The native dispatch split is confirmed below; actual gameplay outcome still needs confirmation; requests during blocking actions are not claimed to have UI-equivalent queue behavior. LE1/LE2 dispatch paths remain unchanged.

The LE3 reflection table now compares squad/member/inventory/power fields and exact parameter blocks against the SDK, including byte input-method fields and the power return bool. Read-only native target rows supplied the targets for the following audit. Metadata agreement alone did not establish their semantics.

## Offline evidence

- `SquadWeapons`, `SquadContext`, `SquadOrders`, `SquadPlace` object builds passed for LE1, LE2 and LE3 with `/W4 /WX`; LE3 reflection table compiled against the SDK.
- Existing power-wheel/readiness/paging and placement tests passed.
- Existing squad language corpus: 177/177 orders, 18/18 non-orders, five previously recorded language gaps remain open.
- Added dynamic-roster regression in `tests/SquadPhrasings.cpp`: sequel/DLC-style name and power, SMG type, native cooldown conveyed to the dispatcher, removal of a power despite a remembered alias, and removal of the companion. This tests the shared parser/context contract, not native gameplay.

## Protected native acceptance still required

Use the existing owned session and request/replay tools. No new launcher, runner or fixture pipeline is required.

1. Coordinator reports expanded reflection probe 259/259 with zero mismatches. The native audit below establishes string ownership and the player/companion dispatch split. Further metadata additions, actual names and native AI outcomes still require acceptance in the corresponding build.
2. On a normally loaded combat save with actual companions, record `mevr squad` and `mevr context`. Compare names, carried objects, health, learned powers, cooldown and grenade resources with native UI. Do not infer party presence from a save's map name.
3. Open/point/confirm/cancel the existing hand menu via controller recordings. Observe the requested weapon object actually equipped. Check pause restoration, held-trigger entry/recenter, main-menu/power-wheel ownership and a level or companion change during selection.
4. Use `mevr order` / `mevr say` with identities returned by the current context. Verify follow, hold and attack using subsequent positions/targets; a dispatch log is insufficient. Run existing `mevr place aim ...`, `release`, `status` checks and recorded right-hand gestures, including cancellation on transition.
5. Command a ready hostile-target power, repeat during cooldown, then exhaust grenade stock and repeat. Verify native cooldown/resource change and actual cast; check unavailable/dead/removed companions and stale queued speech are refused. Verify no unrelated player power/weapon order executes.
6. Check seated/cinematic/shuttle modes deny squad traversal/order activation. First-person conversation and seated traversal work remain outside this increment.

## Remaining parity gaps

- Healing/revive uses the player Unity raised-hand gesture, audited in `research/le3-powers.md`; native effect acceptance remains open. Explicit squad self/ally/location intent is implemented below but not yet validated in native gameplay.
- Specialized player weapon-buff gestures, DLC-specific visual treatment, unique power icons and physical headset acceptance remain tracked in `research/le3-powers.md` and the implementation plan.
- Companion portraits/role artwork remain generic where the shared menu has no asset; overflow weapon page appearance and interaction still need native/headset review.
- Existing standing-order observer must be tested against actual ME3 AI outcomes, including failed navigation and interrupted casts. No synthetic success or resource/cooldown writes were introduced.

## Native ownership and dispatch audit

Coordinator's protected probe supplied native exec RVAs from the installed LE3 executable; Ghidra reads used the existing original-code project. No game was launched by this lane and no raw decompiled code is committed.

- `BioPawn.GetActorGameName` exec `0xB3F8B0` obtains a temporary FString through a virtual getter, resizes the caller's return block through `0x73440`, copies the UTF-16 payload, and frees the temporary through `0xBF0D0`. These are the same allocator helpers independently followed in the descriptor ownership audit. The return buffer is caller-owned; one `ue3::memory::FreeString` is correct.
- `SquadOrderUsePower` exec `0xB64A20` forwards its exact parameter block to `0xAA38B0`. The implementation resolves the recipient's manager/power, requires learned rank and no remaining cooldown, and invokes reflected target selection. Player recipients write the player's pending order and camera data. Other recipients dispatch a reflected event with the exact `HenchmanOrderUsePower` argument layout, including queue and input method; its identification is supported by that ABI and the installed wrapper. The latter path does not populate the player's pending order. Return true follows event dispatch and does not prove AI execution.
- Installed `HenchmanOrderUsePower` delegates through `GiveHenchmanOrder`, which checks the enabled companion AI before adding the order. The adapter separately checks live membership and the actual AI enablement override before dispatch. Native cast and resource outcomes remain to be tested.

## Overflow inventory follow-up

The shared menu machine now treats an optional More weapons tile as navigation, leaves the menu open and clears the highlight. A held confirm or stick cannot dispatch the replacement tile. Release-to-confirm navigation also stays open and has an idle timeout; the normal at-most-four layout remains unchanged. The game adapter compares the complete live inventory while open, cancels when that inventory changes, and observes the actual equipped object after a real selection. Voice weapon orders select from the complete inventory. Tests cover 0/4/5/7-object pagination, held confirm, release confirmation, real selection after navigation, and idle cancellation. No host view structure changed.


## Explicit squad power intent follow-up

The existing core `Power` observation now carries intent capabilities, defaulting to hostile-only for compatibility. LE3 derives self intent from the real native buff/Unity class/type; other native powers allow an actor/location request, with actual `IsValidTarget` and native execution deciding compatibility. This is not a fabricated roster or a promise that a hostile power can buff an ally.

- A self buff no longer requires an invented enemy group. A named external target on that self buff is refused. `SELF` remains available in the existing `mevr order` format.
- `ALLY:L` explicitly names a current living party member. The shared parser preserves the recipient in phrases like “Javik use dark channel on Liara” and carries Liara as the requested target; actual Dark Channel still fails its native hostile-only target predicate. This example distinguishes parsing intent from approving a cast. An absent/downed ally is refused by ordinary support intent; Unity's native squad-wide recovery is a separate player gesture.
- A location-capable `Use` may resolve a spoken pointing sample or known landmark. Dispatch requires an actually observed finite point, checks the recipient power's live native range, and rejects missing points. It does not substitute the origin or a guessed enemy.
- Installed `ChooseTargetForPlayer` prefers a valid native selection even when a desired location was supplied. Therefore a location order refuses while such a selection would override it; the mod neither rewrites selection nor silently casts at a different actor. Existing reflection checks now cover the two selection pointers. The selected actor path still uses normal native targeting/LOS.

The pre-dispatch `CanUsePower` call was removed for **squad** orders: the installed ammo-power override examines `m_bPlayerOrderedPowerUse`, and native target selection establishes that state before native AI admission. Calling the override early can wrongly reject an explicitly ordered ammo buff. We keep read-only rank/enabled/cooldown/grenade guards, current squad/AI membership and actual target validation; the normal native queue owns subsequent `CanUsePower` and effects. Player gesture dispatch still checks its actual `CanUsePower` as described in the power research.

Offline regression cases cover self/no-enemy admission, refusal of an external target for a self buff, named-ally parsing and protocol roundtrip, absent/downed ally refusal, unchanged hostile-only defaults, and location intent versus missing-enemy refusal. The existing 177-order/18-refusal corpus remains green. These are intent and safety checks, not gameplay acceptance. Repeat tests in a real combat fixture must prove the intended companion, actor/location, resource changes and actual effect, including native selected-target ambiguity and cooldown/reload/action interruption.
