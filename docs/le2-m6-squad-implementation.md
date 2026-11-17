<!-- SPDX-License-Identifier: MIT -->
# ME2 squad parity: weapon menu and orders

Implementation candidate for M6 B/C. This reuses ME1's squad menu machine, panel renderer, input suppression, context/parser/standing-order machinery and shared pause owner. Native acceptance is still required; a successful ProcessEvent dispatch is not proof that an AI changed weapon or cast a power.

## Audited sources and adapters

| ME1 system | ME2 implementation |
| --- | --- |
| `game/SquadWeapons.cpp`, `core/vr/SquadMenu`, `plugin/SquadPanel` | Same hand panel, left-trigger open/release-confirm, left-stick recipient and right-stick weapon selection. ME2 always uses the custom panel because its native wheel differs from ME1. |
| `BioEquipment` four quick slots | Live `Pawn.InvManager.InventoryChain` and each `Inventory.Inventory` link, plus the currently equipped weapon. Slots hold actual `SFXWeapon` objects. No ME1 slot enum is sent to ME2. |
| ME1 weapon type labels | Per-member slot labels and icons; pistol, shotgun, assault rifle, sniper rifle and SMG recognized. The four-tile panel shows up to four actual carried guns per member, including SMGs. An unusual inventory with more than four guns is refused instead of silently truncating choices. |
| ME1 tactical equip path | `BioPlayerController.OrderWeaponSwitch(oOrderedPawn, Wpn)`. Original ME2 script inspection confirms that it gives a henchman weapon order with the selected weapon object. The adapter subsequently observes `Pawn.Weapon` to distinguish dispatch from equipment confirmation. |
| `QuickCommandAttackTarget(iSquadMember,pTarget)` | ME2 `HenchmanOrderAttackTarget(oHenchman,oTargetActor)`; explicit target retained. ME2's QuickCommand signature differs, so the ME1 call is not reused. |
| Follow and hold/move | `HenchmanOrderFollow` and `HenchmanOrderHoldPosition`. The latter supplies native movement to the explicit point; the ME1 `PushMoveToPoint` follow-up is not called. Existing ME2 squad placement remains separate. |
| `BioPower` list | `BioPawn.PowerManager.Powers`, resolving evolved powers using `HasPowerBeenEvolved`, learned rank, HUD visibility, native enabled state and cooldown. This mirrors the existing ME2 hand power wheel's native source. |
| Power dispatch | ME2 `SquadOrderUsePower` with power name, member, queue, indicator and desired target explicitly set, followed by `ApplyTacticalOrders`. Native AI chooses execution; the mod does not change cooldowns or ranks. |
| Main menu pause | ME2 custom squad panel joins `MenuPause`'s single pause owner. The existing ME2 `bPlayersOnly` preservation/restoration is reused. No call to ME1-only `WorldInfo.PauseGame`. |

`Le2Squad.hpp` holds native inventory/power readers and prerequisites; `Le2SquadMenu.inc` adapts the existing menu machine. Host/mod menu-view sizes must match: rebuild both when the new per-slot labels are introduced. The ME2 panel uses an amber palette; ME1 retains its existing palette.

## Input and lifecycle rules

- Main menu/wrist rail, the left-hand power wheel and a held support grip suppress squad-menu/gesture acquisition. A pull retains its original ownership until release, avoiding a stray native LB edge.
- The existing pad flag suppresses sticks/A/B while the custom squad panel is open. The hand power wheel retains its input ownership.
- An open panel closes if the mode, tracking, roster or any displayed weapon object changes. Equip is revalidated against native inventory before dispatch.
- Orders require live gameplay mode 0, a living player and combat-enabled input. Recipients are refreshed from the current native squad; attack targets must still be living hostiles. Power availability/cooldown is re-read when dispatching, including delayed voice/model orders.
- Scene/pose data resets on world change and when the player leaves gameplay. The dispatcher rejects world changes; asynchronous callers must additionally discard replies whose request scene has expired, even after returning to gameplay.
- Authored seated/conversation modes are not traversal modes. No conversation camera or player traversal behavior is introduced here.

## Acceptance in the existing test loop

Use `.claude/skills/fake-headset-run/SKILL.md` and the existing session/console/snapshot/replay scripts. One centrally owned game window, save/ini backups and automatic restoration remain required. Do not create a new launcher or input transport.

1. In the user's Freedom's Progress combat save, inspect `mevr squad`, `mevr context` and the snapshot's squad status. Check both names, carried categories and current equipped weapons against the game.
2. Open the custom panel pointing at each squadmate; confirm each carried gun with trigger release, then test A-confirm/B-cancel. Verify the requested weapon object actually becomes equipped and capture both eyes with hands visible. Uncarried tiles must not issue an order.
3. Test left-hand power-wheel and support-grip ownership, then the main menu/wrist rail. No overlap, stray native LB, character movement, or shooting from a menu interaction. Pause must restore its prior value on close, mode change and hot unload.
4. Use existing `mevr order`/`mevr say` pathways for each recipient and ALL: attack a living hostile, hold an explicit point, follow, switch weapon and cast an available power. Verify native movement, target behavior, power animation/effect and cooldown change, not merely dispatch logs.
5. Refuse a dead target/member, missing gun, cooling/disabled/unlearned power, changed roster/inventory and stale model response after a load or conversation. No duplicate power dispatch from standing-order maintenance.
6. Run the existing offline squad menu, parser, gesture, pad and standing-order tests; SMG code/parser cases extend the shared regression suite. `ReflectCheck` now covers the ME2 inventory/power fields and native order parameter layouts. Those reflected checks require a live game and are not replaced by compilation.

Remaining runtime boundaries: ME2 native attack/power outcome, actual inventory completeness, all unusual henchman loadouts, physical-controller arbitration and panel comfort. The fake headset cannot establish real-headset comfort or performance.

## In the game (2026-11-16, lanes GF and GF2)

After the one tick (`SquadWeapons.cpp` `TickCustom` over `Le2SquadMenu.inc`'s `native` adapter, docs/trilogy-parity-ledger.md
Forks 5), on the fake headset at Freedom's Progress (Save 4), the left trigger replayed through
`%LOCALAPPDATA%\MEVR\gf2-lane\make-squad-recording.py` (six pulls of 0.9 s, 3 s apart, the right stick down 0.3-0.6 s
into each: slot 0 of the page, which every squadmate carries; up is slot 2, which neither carries there):

- Session `20261116-153741-665-LE2-gf2-run2`: `squad: menu #1 opened (pointing at nobody): Miranda chosen ...; cards:
  left Jacob, right Miranda`, `menu #1 confirmed (trigger let go): Miranda gets the pistol`, `Miranda: PISTOL order
  dispatched`, `Miranda: requested weapon observed equipped` 0.8 s later; `mevr squad` then shows her `current` PISTOL
  (SMG before). Six pulls, six confirmed.
- **The names.** ME2's `BioActorBehavior.ActorGameName` is the class default "BioPawn" on every henchman. The roster now
  takes `SFXPawn.GetPrettyName` (a string reference) through the game info's `GetSimpleString` (`game/Le2Squad.hpp`
  `ActorName`, ReflectCheck rows in `CheckLe2Squad`), with the behaviour's own `GetActorGameName` second; `mevr squad`
  shows `"name": "Miranda", "nameSource": "SFXPawn.GetPrettyName"` and Jacob. The log names the sources once per
  squadmate (`squad: squadmate 0x...: "Miranda" from SFXPawn.GetPrettyName (PrettyName <ref> = ...)`).
- **The fake headset's dropouts.** The null driver's view and controllers drop out under machine load (`squad: no
  head-tracked world view`, `the controllers aren't active`): 91 such lines in run2 (none during the pulls), 182 in the
  next run, where one pull's menu closed before its flick (`menu closed (no head-tracked world view ...)`). Run with no
  builds going.
- Not shown: Jacob, the other tiles, pointing at a squadmate, the page tile, A/B.

## Offline evidence (2026-11-15)

- Isolated Release `MEVR-LE2` host and `MEVR-LE2-mod` compiled and linked successfully after the final ownership/generation review.
- Existing `mevr-tests.exe`: exit 0, all tests passed, including the added SMG codes and spoken aliases. The existing squad phrasing set reports 177/177 orders and 18/18 non-orders; its five previously documented language gaps remain open.
- `git diff --check`: clean. No deployment or game launch by this lane. The combined integration needs the LE1 build/regression check and the native acceptance above.

Pause-owner follow-up: the saved ME2 `bPlayersOnly` value is now tied to its original WorldInfo. A world transition restores that owner (or discards a dead/replaced owner) without writing the new world. Failed restores retain ownership and retry while running; unload failure is reported explicitly. The focused original-world, pre-existing-pause, destroyed-owner and failed-restore/retry cases passed in the existing offline suite; the ME2 mod rebuilt successfully. Native load/close acceptance remains pending.
