<!-- SPDX-License-Identifier: MIT -->
# ME3 tracked body and weapon pose adapter

2026-11-15, L3-3, starting at `f5b75758`. This is an implementation and evidence record,
not native or headset acceptance. No game was launched by this lane.

## Reuse and implemented differences

The shared `engine/Skeleton`, `engine/BonePose`, `game/ArmIk`, `HandTargets`, `SupportGrip`,
finger solver and guided/manual/live calibration remain the implementation. ME3 does not
get a second IK solver, controller correction, grip cache or weapon-pose writer.

`game/Weapon.cpp` now compiles the reflected sequel adapter on LE3. It reads the player's
`Pawn.Weapon`, requires a skeletal `Weapon.Mesh`, resolves `SFXWeapon.MuzzleSocketName`,
and retains the existing socket/native-muzzle comparison before accepting a weapon's pose.
The support grip still comes from the model's live sockets/hand attachment and settled
measurement. Existing owner/world/bone-name/age checks on pose writes remain in place.

The LE3 differences are explicit:

- Pistol classification uses `SFXGame.SFXWeapon_Pistol_Base`; LE2 retains
  `SFXWeapon_HeavyPistol_Base`.
- LE3's zoom flag is read by reflection from `SFXWeapon_NativeBase.bIsZoomed`.
  The LE1 scoped-accuracy write is not extended to this flag.
- `SFXWeapon_SMG_Base` has a distinct `RangedType::Smg`. Native eligibility still sees
  that family; it is not silently relabeled as an assault rifle or assigned a fifth holster.
- `PresentationProfile()` maps the SMG to the existing rifle calibration and default sight
  settings. Existing LE1 indices 0–3 stay stable. The shared sights retain the actual weapon
  key, mesh name and measured optic/sight bones, so profile borrowing does not replace model
  identity. This is borrowed default geometry, not accepted per-SMG optics tuning.
- Model-specific profile lookup remains game-specific: this change does not apply ME1's
  measured optic table to ME3 meshes. LE3 sights keep sequel assist/convergence/aimed-fire
  overrides disabled pending their own proof.

`game/HandAim.cpp` now uses the shared reflected aim query on LE3 while retaining the existing
legacy fallback when reflection is off. It registers the sequel shot observer and start
correction through the same CallFunction/ProcessEvent services. Shot spread, penetration,
hit processing and damage remain the game's. Hook delivery is a runtime gate below.

## Original installed-package evidence

Inspected base `Game/ME3/BioGame/CookedPCConsole/SFXGame.pcc`, SHA-256
`1d8f7acb6f28e78030982c9f2d935cddec16877272aba314ce1c21dff5ed2d18`.
Reused the ignored LE3 retarget of `tools/conversation-scan`, whose private CLI still says
`--game LE2` but whose inspected package context verifies **LE3**. Exact function selectors
returned one export each. Raw extracts stay ignored under `local-tools/le3-body-audit/`.
The following records behavior and signatures, not copied game implementations.

| Export in `SFXGame.pcc` | Finding relevant to this adapter |
| --- | --- |
| `SFXWeapon.GetMuzzleLoc` | Returns a vector. Visible skeletal weapons use their mesh's named muzzle socket. There are separate pawn-socket, hand and actor-location fallbacks; those do not prove the held skeletal model's muzzle. The adapter intentionally retains its model/socket agreement check. |
| `SFXWeapon.GetPhysicalFireStartLoc` | Returns the muzzle query's result. This supports the model's muzzle as a candidate physical origin but does not prove every projectile subclass or beam route. |
| `BioPlayerController.GetAdjustedAimFor` | Takes weapon plus start vector and returns a rotator; forwards through the native game's aim-assist module. The shared local-controller-only post-handler remains the override point. |
| `SFXWeapon.GetAdjustedAim` | Takes start vector, gets the instigator's aim and applies player accuracy spread. The adapter observes the final shot direction and does not erase native spread. |
| `SFXWeapon.InstantFire` | Uses the pawn's start query, weapon aim and weapon trace before native hit processing. |
| `SFXWeapon.CalcWeaponFire` | Moves the trace origin along the camera direction onto the plane through the pawn, as LE2 does. The existing barrel/plane intersection correction therefore applies to this script path; ME3 then delegates penetration to `CalcWeaponFire_Native`. |
| `BioPawn.GetWeaponStartTraceLocation` | Native vector return with an optional weapon argument. The script declaration alone does not prove its internal algorithm or hook delivery. |

The LE3 SDK comparison is limited to names/signatures: `SFXGame_classes.hpp` declares
`SFXWeapon_NativeBase`/zoom flag around line 19597, `SFXWeapon` muzzle/aim methods around
27474, and pistol/SMG base classes around 30039/30056. The SDK is never a source of guessed
raw pose layouts. `SFXGame_f_structs.hpp` also describes the controller's named parameter
block; the production adapter resolves fields by reflection instead of calling generated wrappers.

## Native gates still required

`Skeleton::Supported()` remains false on LE3 and `Addresses::le3::kUpdateSkelPose` remains
zero in this candidate. `Weapon::TryStartup()` now explicitly refuses unknown skeletal element
layouts. The coordinator owns native discovery and module enabling; this lane changes neither.

1. Establish `FMeshBone` stride/name/parent and the actual `SpaceBases`/`LocalAtoms` element
   representation from the original runtime image. Reflected array declarations alone are
   insufficient: LE1/LE2 expose misleading `int`/`Matrix` element names for these native arrays.
2. Verify `UpdateSkelPose` entry, callers, ABI and attachment timing before installing its hook.
   Verify bounds behavior separately. A prologue match is only a discovery candidate.
3. Cross-check all reflected weapon fields, functions and parameter sizes in the running game.
   Prove CallFunction post-handler delivery for the native start query and script aim path.
4. Observe body/armor skeleton chains and preserved camera bones before the first pose write.
   Confirm stale world/owner/mesh requests stop through load, death and pawn replacement.
5. Observe pistol, rifle and SMG: socket/native muzzle agreement, right-hand attachment, grip
   settling, same-frame muzzle following, support release, instant-hit and projectile impacts.
   Test wall-adjacent starts and off-axis aiming separately; the camera-plane formula does not
   itself establish obstruction safety or every DLC weapon override.
6. Confirm authored melee/traversal ownership, first-person body visibility and restoration;
   authored seated/shuttle modes must not admit traversal. First-person conversations remain
   deferred. Standing and seated Quest 3/Virtual Desktop alignment require a physical headset.

LE3 ammo-effect socket placement, native scope zoom/profile tuning and unusual weapon families
remain their weapon/UI lanes' work. `AmmoSocketAsDrawn()` retains its LE2-only implementation.

## Offline checks and integration

MSVC C++20 `/Zs /W4 /WX /permissive-` passed the nine changed translation units for each of
LE1, LE2 and LE3. The existing `CalibrationWeaponTests` executable, including stable legacy
profile and explicit SMG/unknown-family checks, passed. These checks do not accept native poses.
The LE3 `ReflectCheck` table now covers the native weapon families, mesh/muzzle/zoom fields,
the controller/weapon aim and start functions, muzzle/socket functions and each named parameter
and parameter-block size against the pinned SDK. Its LE3 SDK and SDK-free syntax checks passed;
the SDK check uses the project's external-header warning treatment and required Windows/SDK
preincludes. No running-game comparison result is claimed.

No CMake change is required: the existing source list and calibration test entry cover these files.
The coordinator must add `RangedType::Smg` to `Mod.cpp`'s weapon-name diagnostic switch. The global
module gate stays closed until the plan's foundation and feature acceptance gates are satisfied.
