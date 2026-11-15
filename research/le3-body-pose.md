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

The native follow-up below establishes the element layout and enables LE3's shared skeletal
reader. `Addresses::le3::kUpdateSkelPose` remains zero in this lane's candidate; the coordinator
owns the hook address and module enabling. `Weapon::TryStartup()` refuses unknown layouts.
Native layout evidence does not replace the live checks before pose writes.

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

## Original runtime-image skeletal follow-up

The coordinator acquired the fresh `60AEF0C4` original ME3 text image through the protected
dump-only route. Text RVA `0x1000`, size `0x1065FC1`, SHA-256
`50c79c1c3e64ceca9af90ce87af919f01361cc9f5adb7efa2de8166e58bcd95d`.
The dump receipt identifies one patched start, `0xCA830`, outside the functions below.
This lane reused `tools/ghidra/decompile.ps1 -Game LE3` and `tools/rva-patterns.py` read-only;
private output remains ignored. Original executable data-section values were not used to
infer runtime constants.

| Original function RVA | Direct code evidence |
| --- | --- |
| `0x796EB0` | Pose-update routine reads component mesh at `+0x258`, the asset reference-bone count at `+0xBC`, and resizes `SpaceBases +0x2B4` and `LocalAtoms +0x2C4` using 32-byte entries. It blends animation before invoking composition. Its entry is a `.pdata` function start and the existing pattern tool finds a unique signature. |
| `0x791B80` | Component tick path passes float delta time plus `1, 0` to the pose update, matching the existing four-register pass-through hook ABI. Another caller at `0x78ECF0` invokes it with zero time and `1` when refreshing the pose. |
| `0x785950` → `0x77FBA0` | Composition reads local entries and writes component entries. Quaternion arithmetic uses the four floats at `+0/+4/+8/+C`; translation occupies `+0x10/+0x14/+0x18`, scalar scale `+0x1C`. Parent traversal uses reference-bone stride `0x50` and parent index `+0x40`. This establishes the shared `BoneAtom` representation, not merely its size. |
| `0x7993F0` | Bone-name reader copies each 64-bit name at entry offset zero from the reference skeleton (`asset +0xB4`), advancing by `0x50`. |
| `0x799910` | Bounds-checks an input bone index against that reference count, then reads the parent at entry `+0x40` with stride `0x50`. |

The two reader labels describe their inspected behavior; native-registration name assignment
has not been independently captured. An initial prologue-based force-update candidate at
`0x778C00` calls a different routine and is **not** used as proof of `UpdateSkelPose`.

The shared `Skeleton` adapter can therefore admit LE3's element representation without adding
SDK offsets to production reads. The LE3 reflection table compares the three observed object
field offsets above, the skeletal/socket metadata, and the native bone-location query's named
parameters. `ArmIk::CheckLayout` still checks head/hand positions against the game's own
`GetBoneLocation` in component and world space and rejects implausible arm lengths before writing.
No body/armor mesh, attachment timing or headset alignment has yet passed that live gate.

`0x793850` is also a `.pdata`-aligned, uniquely matched bounds-update candidate with bounds
calculation behavior. Its runtime hook/restoration remains separate coordinator work; this
lane does not change the bounds address. The new skeleton/pose translation units pass syntax
checks for all three games using the project's existing `/wd4127` constant-condition setting;
the expanded LE3 SDK comparison also compiles.
