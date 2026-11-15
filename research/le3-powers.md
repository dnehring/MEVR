<!-- SPDX-License-Identifier: MIT -->
# LE3 player powers: shared wheel, native execution

Implementation candidate for L3-6, 2026-11-15. Offline compilation and deterministic
wheel tests are separate from native acceptance. No game was launched for this work.

## Reuse and inspected contracts

`src/game/PowerWheel.cpp` remains the owner of the left-hand wheel, selection,
target lock, push and chest/gun/raised-hand gestures. `src/core/vr/PowerWheel` and
the existing host wheel renderer and pad ownership stay shared with LE1/LE2.
There is no second dispatch loop or fixed six-class roster. LE3 reflection is
isolated in `src/game/Le3Powers.hpp`; readiness math is in `core/vr/NativePower.hpp`.

The local SDK under `external/LExSDKv2/Src/LESDK/LE3/` supplies these contracts:

| File and line | Contract used |
| --- | --- |
| `SFXGame_classes.hpp:22280` | `BioPawn.PowerManager` |
| `SFXGame_classes.hpp:23377` | `SFXPowerManager.Powers` contains `SFXPowerCustomActionBase`, not LE2 `SFXPower` |
| `SFXGame_classes.hpp:19042` | Native name, rank, visibility, enable state and current/total cooldown |
| `SFXGame_classes.hpp:4132` | Power type: instant 0, projectile 1, melee 2, buff 3 |
| `SFXGame_classes.hpp:17816` | `BioPlayerInput.ActivatePower`: name, actor target, world target location, camera location and camera rotation; Boolean return |
| `SFXGameContent_classes.hpp:1534` | Grenade base, native `GetGrenadeCount` and `CanUsePower` |
| `SFXGameContent_classes.hpp:1870` | Unity native class |
| `SFXGameContent_classes.hpp:5701` | Ammo-power native class |

Read-only inspection of installed LE3 `SFXGame.pcc` confirmed
`BioPlayerInput.ActivatePower`, `SFXPowerManager.GetPowerWheelPowers`,
`SFXPowerCustomActionBase.IsEnabled`, `SFXPowerCustomAction.CanUsePower`,
`GetPowerCooldown` and manager shared-cooldown behavior. Dedicated
`SFXPower_GrenadeBase.pcc` and `SFXPower_AmmoPower.pcc` supply their overrides.
Raw script output is private and ignored; this document records interface facts
and our implementation decisions, not reconstructed game source.

The native wheel filters for learned, HUD-visible, enabled instances. Our wheel
keeps learned disabled powers visible but dim, matching the existing shared UI.
We enumerate the live array instead of calling the native output-array function,
which also processes ordering and would introduce an allocator ownership boundary.
Known LE2 names supply display labels/icons only; unknown names retain their
native identity and get a generic icon. This is not complete LE3 art/localization.

## Dispatch and authority

Ammo subclasses use the shared gun-contact gesture, buffs the chest gesture,
and Unity the raised-hand gesture. Other native powers use the shared aimed push
with an actor lock or world location. These are class/type-derived roles, not
invented powers. Native targeting still decides whether a request is valid.
Weapon-specific buffs beyond ammo use the chest role until separately audited;
their specialized coaching/art remains a follow-up, not a claim of full parity.

The adapter rereads the selected power and manager at dispatch. It calls the
actual object's `CanUsePower` override, checks combat availability and cooldown,
and requires all five reflected `ActivatePower` inputs to bind. Native grenade
count comes from the grenade instance's inventory-backed accessor; missing,
negative or nonfinite observations fail closed. Zero grenades dim the entry.
Current/total cooldown comes from live native values; changes from weight,
rank/evolution or game effects are not recalculated or overwritten by the mod.

`ActivatePower` accepting a request does **not** prove release, damage, resource
consumption or successful custom-action start. The installed implementation does
not propagate the custom-action start result. LE3 logs explicitly say request
accepted; the existing `casts` counter therefore counts accepted requests.
No LE1 synthetic projectile or manual unarmed-release workaround runs on LE3.
Native resources, rank, evolution, inventory, quick slots and loadout are never
written. Pawn/manager changes clear the mod's selection; it must be reselected.

The wheel's fixed view supports eleven powers plus No power. Larger custom
rosters fail closed instead of hiding powers; disable `[PowerWheel] on` to return
to native controls. Paging and full DLC-specific gestures/icons are open work.

## Proof gates

Offline: existing `tests/PowerWheelTests.cpp` covers left-hand ownership,
selection, tracking refusals and gesture state machines. Added cases cover live
cooldown denominators, zero/missing/negative grenade counts, unlearned/disabled
powers, invalid float values and native gesture roles. Compile the game source
for all three game defines with warnings as errors. These tests do not simulate
UE3 reflection or prove native release.

For a protected native session, reuse the existing `tools/console.ps1`,
`record.ps1`/`replay.ps1 -Inputs`, `snapshot.ps1` and protected session runner.
Use normal-load validated private fixtures, never fabricated progression.

1. For each of six classes, compare `mevr powerwheel roster` with the native wheel
   and actual learned powers; capture ranks, cooldowns and disabled entries.
   Exercise an unknown/DLC power if a genuine fixture supplies one.
2. Select and push a targeted power at a valid target and invalid/occluded target.
   Prove native action/release and world effect separately from request logs.
3. Replay gun-contact ammo activation with a drawn compatible weapon, stowed gun
   and heavy weapon; check native refusal and actual ammo effect. Repeat chest
   buff and Unity raise with valid/invalid native availability.
4. Use a grenade power at zero and nonzero stock. Verify one native consumption
   per accepted execution, no synthetic projectile, no repeat while held.
5. Repeat cooldown refusal/recovery across a native weapon/loadout weight change;
   compare native cooldown timing rather than a hard-coded duration.
6. Open wrist, main and squad menus during selection/gesture; no leaked X,
   trigger or locomotion. Test death, normal load, pawn/manager replacement,
   tracking loss and held-trigger recenter without unintended activation.
7. Authored seated/scripted scenes (including shuttles) remain non-traversable;
   no first-person conversation work is implied by this candidate. Confirm the
   shared scene gates suppress gestures there and restore them on foot.

Host-service activation, global LE3 gates and command wiring belong to the
coordinator's integration. This increment edits none of those shared boundaries.
