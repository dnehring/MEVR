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

The host view still holds twelve tiles, with no ABI change. Up to eleven powers
keep the original LE1/LE2 layout. Larger rosters use shared pages of up to ten
real powers, No power and a visible `More powers N/M` navigation tile. Selecting
More keeps the wheel open, clears the highlight and requires a fresh confirmation;
it never equips or casts a synthetic power. Every page retains No power; the last
page wraps to the first. A native roster identity change cancels an open selection
instead of reinterpreting its old index. Full DLC-specific gestures/icons remain
open work; paging compilation/tests do not prove headset usability.

## Proof gates

Offline: existing `tests/PowerWheelTests.cpp` covers left-hand ownership,
selection, tracking refusals and gesture state machines. Added cases cover live
cooldown denominators, zero/missing/negative grenade counts, unlearned/disabled
powers, invalid float values and native gesture roles. Compile the game source
for all three game defines with warnings as errors. These tests do not simulate
UE3 reflection or prove native release.

Shared paging tests cover every power exactly once across rosters of 0 through
128 sampled sizes, original single-page layouts, partial last pages, wraparound,
all four existing confirmation routes, held-confirm suppression and cancellation.
The existing renderer consumes the refreshed tiles every frame, so it needs no
page cache, new input binding or separate UI implementation.

For a protected native session, reuse the existing `tools/console.ps1`,
`record.ps1`/`replay.ps1 -Inputs`, `snapshot.ps1` and protected session runner.
Use normal-load validated private fixtures, never fabricated progression.

1. For each of six classes, compare `mevr powerwheel roster` with the native wheel
   and actual learned powers; capture ranks, cooldowns and disabled entries.
   Exercise an unknown/DLC power if a genuine fixture supplies one.
   For an overflow roster, visit every page, confirm No power on each, wrap once,
   and verify a held pinch/grip/X never dispatches a power after navigation.
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


## Native self, ally and location admission follow-up

2026-11-15 candidate: the existing left-hand gesture scanner now calls the actual LE3 power object's `IsValidTarget` override. The base method admits hostile BioPawns; an actual support/DLC override can admit a living ally. This does not invent an ally-compatible power or claim the installed base roster has one. Dispatch rechecks the same target predicate. The shared scan still excludes dead/dormant pawns; Unity uses the native squad-wide self route instead of targeting a corpse. Combat-placeable/non-pawn lock selection remains an explicit further target-scan case.

Location casts use the same hand-ray trace and existing push gesture, with an added check against the power's live `MinimumRange.CurrentValue` / `MaximumRange.CurrentValue`. The stock `CanUsePower` range branch only checks a non-null actor; the explicit null-actor range guard prevents a location request bypassing that limit. Native parameters, target/resource override and action execution remain authoritative. Unknown/nonfinite/inverted range observations fail closed. Reflection metadata compares both nested paths and the target-method parameter block with the SDK.

For self powers, the roster and gesture ready state consult the actual `CanUsePower(nullptr)` override as well as native learned/enabled/cooldown values. This supplies current weapon restrictions and Unity's native medi-gel/squad-health refusal before showing a ready gesture. A real learned `SFXPowerCustomAction_Unity` instance can use the existing raised-hand gesture even if omitted from the flat HUD power list; no instance or resource is created. Hidden non-Unity powers remain excluded.

Installed `BioP_Char.pcc` Unity scripts were read using the existing private scanner. `CanUsePower` first invokes `ShouldUsePower`, which checks native medi-gel count and whether player or squad health/shields need recovery. `StartPower` consumes medi-gel through the native resource method, subject to the game's upgrade exception. `OnImpact` owns companion resurrection, temporary immunity and health/shield recovery. The mod calls none of those effects directly and writes no health, inventory, plot or cooldown field. Actual native recovery/consumption remains pending a suitable normally loaded fixture.

Offline tests add native location minimum/maximum boundary and invalid-value cases; all existing power gesture/readiness/paging tests still run. Native acceptance must include a real injured/downed companion, full-health refusal, zero-resource refusal, one successful Unity cast and its resource outcome, plus any genuinely ally-compatible power if the fixture has one. This increment improves player gestures; the separate squad voice validator still needs explicit self/ally/location intent rather than forcing every Use action to an enemy group.
