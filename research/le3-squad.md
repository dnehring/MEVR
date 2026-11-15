# LE3 squad adapter: shared menu, placement and voice

2026-11-15. Implementation candidate, not native or headset acceptance. Builds and offline tests do not prove a companion obeyed an order.

## Reuse and differences audited

Reuse `game/SquadWeapons.cpp`, `Le2SquadMenu.inc`, `SquadContext.cpp`, `SquadOrders.cpp`, `SquadPlace.cpp` and the existing core menu, parser, placement machine and host view. The legacy `Le2Squad.hpp` filename now holds explicit sequel adapters; it is not a second menu or order framework. Existing left-hand ownership, pause ownership, generation cancellation, inventory revalidation and observed weapon confirmation remain in use.

Local reference: `external/LExSDKv2/Src/LESDK/LE3/{SFXGame_classes,SFXGame_f_structs,Engine_classes}.hpp`. Installed `SFXGame.pcc` script inspection used the existing private scanner, with extracts kept in ignored `build-le3-squad-audit`. No decompiled game implementation is included here.

| Contract | LE3 implementation |
|---|---|
| Current companions | `BioWorldInfo.m_playerSquad` -> `BioBaseSquad.Members`, excluding the player and requiring `SFXPawn_Henchman`. LE2's `GetSquadSize` / `GetMember` functions are not assumed. |
| Dispatch admission | Re-read current squad membership, living pawn, `SFXAI_Henchman` controller and its actual `IsEnabled` method. Require gameplay mode 0 and native combat-input admission. |
| Names | `BioPawn.GetActorGameName` FString return, freed through the proven engine allocator; class name fallback. LE3 has no LE2 behavior/name field. Native return ownership audit remains a focused acceptance check. |
| Weapons | Actual inventory linked list plus equipped object, no invented loadout. LE3 pistol/SMG classes are `SFXWeapon_Pistol_Base` / `SFXWeapon_SMG_Base`. The four menu slots represent carried objects, not four permitted weapon types; a member with more than four carried objects fails closed pending menu expansion. |
| Weapon command | `OrderWeaponSwitch(oOrderedPawn,Wpn,eInputMethod=0)`. Require the requested object still belongs to that member; report success only when the equipped object is observed. |
| Follow / hold / attack | Existing native `HenchmanOrderFollow`, `HenchmanOrderHoldPosition`, `HenchmanOrderAttackTarget`. LE3 adds `eInputMethod`; installed script defaults use `CIM_Default=0`. Do not invoke Kinect-specific handling or LE1 formation/movement writes. |
| Powers | Shared `Le3Powers.hpp` reads the member's actual `SFXPowerManager.Powers`. Native rank, visibility, enabled state, cooldown and grenade-count override remain authoritative. DLC/unknown identities enter the existing dynamic voice roster. |
| Power command | `SquadOrderUsePower`: explicit name, recipient, queue=0, indicator=true, desired selection target, input method=0, desired target and desired location. Recheck actual power `CanUsePower`; inspect native bool return. A true return is acceptance of the request, not proof of a cast. |

The installed power-wheel path calls `SFXGameModeDefault.UsePower`, which calls `SquadOrderUsePower`, using queue 1 only for permitted blocking-action cases, then `ApplyTacticalOrders`. Inspection of `ApplyTacticalOrders` shows it consumes the **player's** pending power/weapon and clears that player order. The LE3 companion-only adapter does not flush an unrelated player order. Native dispatch internals and outcome still need confirmation; requests during blocking actions are not claimed to have UI-equivalent queue behavior. LE1/LE2 dispatch paths remain unchanged.

The LE3 reflection table now compares squad/member/inventory/power fields and exact parameter blocks against the SDK, including byte input-method fields and the power return bool. Read-only native target rows for `SquadOrderUsePower` and `GetActorGameName` support a focused native audit; metadata agreement alone does not prove their semantics.

## Offline evidence

- `SquadWeapons`, `SquadContext`, `SquadOrders`, `SquadPlace` object builds passed for LE1, LE2 and LE3 with `/W4 /WX`; LE3 reflection table compiled against the SDK.
- Existing power-wheel/readiness/paging and placement tests passed.
- Existing squad language corpus: 177/177 orders, 18/18 non-orders, five previously recorded language gaps remain open.
- Added dynamic-roster regression in `tests/SquadPhrasings.cpp`: sequel/DLC-style name and power, SMG type, native cooldown conveyed to the dispatcher, removal of a power despite a remembered alias, and removal of the companion. This tests the shared parser/context contract, not native gameplay.

## Protected native acceptance still required

Use the existing owned session and request/replay tools. No new launcher, runner or fixture pipeline is required.

1. Reflection probe must agree on the new metadata. Audit recorded `GetActorGameName` and `SquadOrderUsePower` native targets before relying on returned string ownership / internal queue semantics as proven.
2. On a normally loaded combat save with actual companions, record `mevr squad` and `mevr context`. Compare names, carried objects, health, learned powers, cooldown and grenade resources with native UI. Do not infer party presence from a save's map name.
3. Open/point/confirm/cancel the existing hand menu via controller recordings. Observe the requested weapon object actually equipped. Check pause restoration, held-trigger entry/recenter, main-menu/power-wheel ownership and a level or companion change during selection.
4. Use `mevr order` / `mevr say` with identities returned by the current context. Verify follow, hold and attack using subsequent positions/targets; a dispatch log is insufficient. Run existing `mevr place aim ...`, `release`, `status` checks and recorded right-hand gestures, including cancellation on transition.
5. Command a ready hostile-target power, repeat during cooldown, then exhaust grenade stock and repeat. Verify native cooldown/resource change and actual cast; check unavailable/dead/removed companions and stale queued speech are refused. Verify no unrelated player power/weapon order executes.
6. Check seated/cinematic/shuttle modes deny squad traversal/order activation. First-person conversation and seated traversal work remain outside this increment.

## Remaining parity gaps

- Healing/revive and contextual ally/location power orders need their own native policy. The shared voice validator currently requires a target group for `Use`; this increment does not claim self/ally/location voice power parity.
- Specialized player weapon-buff gestures, DLC-specific visual treatment, unique power icons and physical headset acceptance remain tracked in `research/le3-powers.md` and the implementation plan.
- Companion portraits/role artwork remain generic where the shared menu has no asset; more than four carried weapons needs explicit UI work.
- Existing standing-order observer must be tested against actual ME3 AI outcomes, including failed navigation and interrupted casts. No synthetic success or resource/cooldown writes were introduced.
