<!-- SPDX-License-Identifier: MIT -->
# ME1's VRIK on ME2 (M3, lane C)

Date: 2026-11-14, lane C of [the continuation plan](../docs/le2-continuation-plan.md) (M3: hand aim, arms and body, the gun
in the hand). The port recipe is research/le2-engine-access-discovery.md "M1: what ME2's code showed" (a module's
`Ready()` checks become `Up()`, its handles `ue3::kEveryGame`, one `engine/ReflectCheck` LE2 row per class, field,
function and parameter); the names were audited first in [research/le2-vrik-audit.md](le2-vrik-audit.md). Everything
below is ME2's exe 2.0.0.48602 (`MassEffect2.exe`, PE timestamp `0x60AEEFA5`); RVAs are of that exe.

**How the ME2 script was read** (a weaker model can repeat it; the output is game code: keep it in a scratch folder, never
in git): `local-tools\le2-inspection-root\conversation-scan.exe "<ME2>\BioGame\CookedPCConsole" <out.txt> --game LE2
--script <Class.Function>` (or `--script grep:<text> --only SFXGame.pcc` for every function holding a word). Natives:
`tools\ghidra\decompile.ps1 -Game LE2 <rva|name>` (`-Search <text>`).

## Hand aim (M3.1)

**What ME2 does with a shot** (script, `Engine.Weapon.InstantFire`, which `SFXWeapon.InstantFire` calls first):

1. `StartTrace = Instigator.GetWeaponStartTraceLocation()`: the player's `BioPawn` version takes the controller's
   `GetPlayerViewPoint` (the camera) and moves it forward along the view to the plane of the weapon's
   `GetPhysicalFireStartLoc(FireOffset)` (= `GetMuzzleLoc()`).
2. `EndTrace = StartTrace + Vector(GetAdjustedAim(StartTrace)) * GetTraceRange()`. `SFXWeapon.GetAdjustedAim` asks
   `Instigator.GetAdjustedAimFor` (`Engine.Pawn`), which asks `Controller.GetAdjustedAimFor`
   (`SFXGame.BioPlayerController.GetAdjustedAimFor`: `AimAssist.GetAdjustedAimFor` with a pad, else the pawn's base aim),
   then adds `GetPlayerAimError()` degrees of random spread to pitch and yaw.
3. `SFXWeapon.CalcWeaponFire` moves `StartTrace` once more, along the camera's own direction, onto the plane through the
   pawn's location (`StartTrace += CamRot * ((Pawn.Location - StartTrace) . CamRot)`), then traces.

All of it is script calling script, so ME1's two post-handlers on `BioPlayerController.GetAdjustedAimFor` (CallFunction
and ProcessEvent, `game/HandAim`) apply unchanged: the controller's answer is replaced by the right controller's ray (or
the barrel, once the gun is in the hand). New for ME2 (ME1's `BioWeaponRanged.GetAdjustedAim` doesn't exist): a
CallFunction post-handler on `SFXGame.SFXWeapon.GetAdjustedAim` sees each shot's final rotation (aim error included) for
the player's own weapon (`Pawn.Weapon`), and logs `hand aim: shot N: along the right controller's ray: the shot pitch P
yaw Y, the ray pitch p yaw y: D degrees apart (the weapon's aim error); the game's own aim was ...`. The view anchor needed
`PlayerController.PlayerCamera` on every game (`game/Player`).

**Check (fake headset):** `tools/make-walk-recording.py <out> --plan stand:120 --hands view --hand-yaw 30 --fire 1.5`
(new options: `--hand-yaw` turns only the right controller, `--fire EVERY[:HOLD]` pulls its trigger), replayed with
`tools/replay.ps1 -Path <out> -Loop -Inputs` after `tools/le2-halcyon-run/load.ps1`. See "The run" below: 34 shots,
each 30 degrees right of the game's own aim, within 0.4-0.8 degrees (the weapon's spread) of the gun's barrel.

**The shot's start, along the barrel.** Step 3 above moves any start onto the plane through the pawn perpendicular to the
camera's forward, so on ME2 the start isn't put at the muzzle (ME1's `GetWeaponStartTraceLocation` answer): a CallFunction
post-handler on `SFXGame.BioPawn.GetWeaponStartTraceLocation` returns the point where the barrel's line (the muzzle M along
the shot's direction D) meets that plane, `M + D * ((P - M) . F) / (D . F)` (F the camera's forward from
`Camera.CameraCache.POV`, P the pawn's location); the move is then nothing, and the trace runs along the barrel through
the muzzle (logged once, `hand aim: shot start on the barrel's line: ... N units from the muzzle`; -91 units with the gun
30 degrees off the view). [verify] that a start behind the muzzle never hits a wall at the player's back (ME1's wall check
from the pawn to the muzzle isn't on ME2).

## Recoil: ME2 puts it into the controller's rotation, as ME1

`SFXWeapon.PlayOwnedFireEffects` (each of the player's shots) calls `SetWeaponRecoil(GetWeaponRecoil() * 182.044)`; the
weapon hands it to `SFXInventoryManager.SetWeaponRecoil`, which queues pitch **and yaw** into `WeaponRecoilOffset` (and
`TotalRecoil`, capped by `RecoilCap`/`ZoomRecoilCap`): the yaw is `RecoilYawBias` plus a sine of the time
(`RecoilYawFrequency`), times the pitch kick and `RecoilYawScale * 0.5`. `BioPlayerInput.UpdateViewRotation` then adds
`UpdateKickback` (the queue, eased out at `RecoilInterpSpeed`) minus `UpdateKickbackFade` (the applied kick faded back,
pitch at `RecoilFadeSpeed`/`RecoilZoomFadeSpeed`, yaw toward 0) to the controller's rotation through `SetRotation`. So
ME2's recoil turns the controller like ME1's, and also turns its yaw, and fades back.

`game/ViewLevel` (ported unchanged: every one of its 29 names exists in ME2) holds the pitch and roll at 0 on foot while
the hand aims, counts the shots in its `SetWeaponRecoil` post-handler and pulses the hand. ME2's yaw kick is the one
deviation: in the same post-handler, while the hold applies, the shot's yaw kick is taken back out of `WeaponRecoilOffset`
and `TotalRecoil` (LE2 only; logged `view level: N shots queued a yaw kick too: ... taken back out`), so nothing turns
the view's yaw and nothing fades back. `game/GunKick` takes ME2's recoil from `SFXWeapon.GetWeaponRecoil` when a shot's
queued kick wasn't measured (ME1: `BioWeaponRanged.GetRecoil`).

## Camera shakes

ME1's sources (`SFXWeaponStatsBase.FiringShake`, `SFXCameraShakeBase`) don't exist in ME2, and no ME2 weapon script
starts a screen shake. ME2's script shakes all go to the player camera's `SFXCameraModifier_ScreenShake`
(`SFXPlayerCamera.AddScreenShake` from `BioPlayerController.CameraShake`, footsteps, hits, powers); the engine's own
(`CameraShake` objects from matinees, animation notifies, Kismet) to `Camera.CameraShakeCamMod`. Both are camera
modifiers, so on ME2 `game/CameraShake` sets their `CameraModifier.bDisabled` in VR while `[Comfort] screenShake` is
below 1 (journalled, put back otherwise; a share between none and all isn't built on ME2: logged once). The body's
camera shakes are ME1's switch unchanged: `SFXCameraMode.bIsCameraShakeEnabled` (ME2's `GetCurrentShake` reads the
pawn behaviour's camera shake interface only while it's set), cleared on the first-person modes (in the run:
`SFXCameraMode_Explore` and `SFXCameraMode_Combat`; both modifiers `off in VR`). [verify] in the game: which shakes ME2's
guns make at all (the muzzle flash particle systems carry `SFXParticleModule_CameraEffect`).

## The skeleton

`engine/skeleton` reads RefSkeleton's FMeshBones and SpaceBases' FBoneAtoms by layout (not in the reflection). ME2's
natives read them exactly as LE1's: `USkeletalMeshComponent::execGetBoneNames` (0x79DD90) copies each bone's name from
the asset's RefSkeleton (`SkeletalMesh +0xB4`, the component's `SkeletalMesh` at `+0x25C`) with a 0x50-byte stride;
`execGetParentBoneIndex` (0x79E2A0) reads `ParentIndex` at `+0x40`; `UpdateSkelPose` (below) sizes SpaceBases (`+0x2B8`)
and LocalAtoms (`+0x2C8`) with 0x20-byte FBoneAtoms. `engine/ReflectCheck`'s LE2 `skeleton` rows compare the reflection's
offsets with those three (they agree: `reflect: 273 of 273`).

**ME2's player skeleton is ME1's** (`skeleton:` lines, Freedom's Progress, `SFXPawn_PlayerSoldier`, mesh
`SFXCompositeSkeletalMesh`, AnimTree `BIOG_AnimTree_Humanoid.BIOG_General_Player_O`): 110 bones, `God` the root, `Root`,
`Camera` (104), `RHIK`, `RFIK`, `LFIK` and the four `*-Swivel` bones its children; the spine `LowerBack > Chest > Chest1 >
Chest2`, `LeftCollar`/`RightCollar`, the arms `LeftShoulder(57) > LeftElbow(58) > LeftWrist(59)` and `RightShoulder(80) >
RightElbow(81) > RightWrist(82)` with twist bones, five fingers of three joints each (`<Side><Finger>Finger[1,2]`),
`Prop01` (98) under the right wrist (the gun hangs on it), `Prop02` under the left, `LHIK` (109) under `Prop01` (the
AnimTree's off-hand IK goal), `Head`, `headBase`, the eyes and the face. Every name ME1's code looks for is there, so
arm IK, the torso, the fingers and the support grip find their bones with no change.

## UpdateSkelPose

`USkeletalMeshComponent::UpdateSkelPose` = **0x79B8A0** (lane A's candidate, confirmed): `execForceSkelUpdate`
(0x77D750) calls it as `(this, 0, 1)` as LE1's does; it resets SpaceBases to identity when its size differs from the
RefSkeleton's and sets the force-refpose flag, as LE1's 0x592440. Pattern (unique in ME2's .text, from
`tools/rva-patterns.py`): `48 8B C4 44 89 40 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 A8 48 81`
(`engine/Addresses.hpp` le2).

## The camera's hook

ME2's camera modes hook on a bone by name: `SFXCameraMode.GetActorCameraHook` -> `BioPawn.CalculateOffsetHook(HookOffset,
HookName)` -> `BioPawn.GetCameraHook(HookName)` = `Mesh.GetBoneLocation(HookName)` (SpaceBases: if arm IK moved that bone
the view would follow it), `CoverSlot` the cover slot, `None` the feet. Halcyon's ME2 first person zeroes each mode's
`Offset`, so the eye sits at the hook. **ME2's camera hooks on `Camera`, as ME1's**: `camera: mode
SFXCameraMode_Explore ...: HookName Camera, HookOffset (0, 0, 120), Offset (-35, 0, 35); hook bone Camera(104) ...; the
formula from it puts the camera at ...: 0.0 units from the mode's m_pov` (`game/CameraProbe`, ported to LE2), and the same
for `SFXCameraMode_Combat`. `Camera` hangs off `God`, outside every subtree arm IK and the torso write, and the writer
keeps `Camera` and `God` by name anyway (`arm IK: camera hook bones God(0, parent none: the root), Camera(104, parent God(0)):
outside every subtree we write`). Nothing to change for ME2.

## The run (fake headset, 2026-11-14 21:15, Freedom's Progress, Save 4)

`tools/le2-halcyon-run` (`prep.ps1 -Holder me2-lane-c`, `launch.ps1`, `load.ps1`, the replay, `launch.ps1 -Stop`,
`restore.ps1`: `verification: 0 differences`); the recording `make-walk-recording.py --plan stand:120 --hands view
--hand-yaw 30 --fire 1.5`, replayed with `-Inputs`. What the mod's log showed, in order:

- `reflect: 273 of 273 checks agree`; `exe (mod): 13 of 13 addresses at their RVAs` (UpdateSkelPose's pattern included).
- With the replay's controllers: `hand aim on`, `arm IK: UpdateSkelPose at ... (entry bytes 48 8b c4 44 89 40 18 ...)`,
  `hooked USkeletalMeshComponent::UpdateSkelPose`, `arm IK on`; `arm IK: bone layout verified` (SpaceBases against the
  engine's own GetBoneLocation for Head and both wrists, component and world: match); arm lengths 28.9 / 26.3 units.
- The first trigger pull drew the heavy pistol: `weapon: SFXWeapon_HeavyPistol ...: muzzle socket 'Flash_2' on bone
  Root(0) ...: muzzle ... vs GetMuzzleLoc ...: match (0.00 units)`, `its mesh hangs on Prop01(98) under RightWrist(82)`,
  the grip measured; `hand aim: shots follow the gun barrel from the muzzle`.
- `hand aim: shot 1: along the gun barrel: the shot pitch 17 yaw -10405, aimed pitch 37 yaw -10473: 0.39 degrees apart
  (the weapon's aim error); the game's own aim was pitch 0 yaw -16002, 30.7 degrees from the shot`; 34 shots in 60 s, 22
  lines, every one 29.8-30.7 degrees from the game's aim and 0.4-0.8 degrees from the barrel. **The shots follow the
  controller.**
- `view level: first shot seen (SFXInventoryManager.SetWeaponRecoil): pitch kick 546 UU (+3.00 deg) queued for the
  controller's rotation and a yaw kick -1 UU`; over the minute `recoil queued +101.98 deg by 34 shots`, the pitch as the
  game left it -0.40..+1.02 deg, **put back to 0 in 4723 of 4723 ticks**, roll 0; `34 shots queued a yaw kick too: -4.44 deg
  in all ... taken back out`: ControlRotation's yaw -16002 UU the whole time.
- `gun kick: first kick: the gun's recoil 3.00 degrees ... -> 4.00 degrees muzzle up and 2.2 cm back at its peak`.
- The snapshot's `mod.armIk.hands`: first person, each wrist 0.07 m from its controller's grip, in view (LE1's numbers at
  the same replayed pose: 0.07 and 0.07).
- `mevr camera trace 10` during the replay, `python tools/skeleton-audit.py <csv>`: every traced bone within 3 cm peak to
  peak of `Root` (Head 1.3 / 0.8 / 0.6 cm, the ankles 2.6 / 1.1 / 0.6), the game camera 3.5 / 0.4 / 1.6 cm against the pawn.

**Again on main with lane B2's eye images** (21:41, the same replay): the same numbers (31 shots, the pitch held, the yaw
kicks taken out, both hands 0.07 m from the grips), and the left eye's snapshot shows it: Shepard's open left hand low in
front, the heavy pistol in his right hand turned 30 degrees right as the controller, its holographic sight lit.

**LE1 unchanged** (the owner's career on Noveria, `tools/fake-headset-session.ps1` with the same replayed pose, the base
commit's build against this one): the same arm IK targets ((24.8, -13.1, 156.8) and (24.8, 4.9, 156.8)) and elbows
within 0.1 unit, `hostView` offsets identical (-43.3 / -4.1 / -18.1 uu), both hands 0.07 m from the grips, the same
`hand aim:` lines (no gun was drawn there: `mevr weapon pistol` found no pistol in that career's quick slots).

## Arm IK across root-motion steps (lane C2, 2026-11-15)

**The problem** (lane E's finding, research/le2-traversal.md "The hands"): through ME2's vault the drawn wrists left the
controllers by 0.38-0.43 m at the rise, 0.87-1.07 m at the 2 m step and 0.21-0.56 m at the drop, for about two frames each.

**What happens, measured.** `engine/BonePose` now logs `arm IK: a step between the goals and the write: the camera's bone moved
N units (x, y, z) and the root (...) in component space, the mesh M units in the world; rule R: ...` for every step past 20 units,
and counts every write by how far the camera's bone moved (`mevr armik step`, the heartbeat's `arm IK writeback: N steps ...`).
On the fixture (Save 4, `CoverLink_10` slot 0, `vault.ps1 -Mode tap`, nine vaults):
- **The mesh component never moved** between the mod's tick (where the goals are computed, after the frame) and the next
  `UpdateSkelPose` (where they are written): 0.0 units in 3,201 writes. The game moves the actor *after* the pose is composed.
- **The `Camera` bone moved in component space at each step** (the root `God` stayed at 0): at the rise -22, -36, -51 units in
  z over three writes, at the forward step -61 to -113 in x over three, at the drop +20 to +30 in z over four or five. The vault
  animation composes the body and the camera's bone where they will be once the actor has stepped; the actor then steps, so
  in the world the body and the camera stay put. Goals left in component space rode the step.
- Standing, the camera's bone moves under 1 unit a write (745 of 1,097 writes in one stretch of three vaults and the standing
  between them; 305 moved 1-5 units, 30 5-20, 17 past 20, all inside the vaults).

**The fix** (`core/ik/GoalStep`, unit-tested in `tests/GoalStepTests.cpp`; `engine/BonePose` `GoalFrame`, `StepSince`;
`game/ArmIk` fills the frame):
1. The mod's request carries where its goals were computed (`Request::frame`): the mesh's `LocalToWorld`, the `Camera` bone's
   index, name and SpaceBases translation at that tick, and `viewOnAnchor` (first person on foot with tracked controllers, the
   view not held by `view_anchor::Hold` and the body not away: the camera hangs on that bone).
2. In each write the writer compares the bone in the pose being written with where it stood (`ik::FindStep`). If it moved past
   `units` (default **1 unit**) and the view hangs on it, the animated pose is moved by the inverse of that move into the goals'
   frame, everything is solved there (torso, arms, support grip, gun kick, legs, fingers, body scale), and the written bones are
   moved back (`ik::Compose(bone, step)`); the call's gun kick sample is moved with them. The samples the mod reads next tick
   (`AnimatedHead`, `AnimatedHands`, the unscaled body) stay in the call's own frame.
3. Not while the view is held (a conversation shown in the world, a scripted walk): an animation's jump of the bone there must not
   move the hands. A teleport moves the component alone (the bone stays): the goals ride the component as before.

**Checked** (`tools/le2-halcyon-run/vault.ps1 -Runs 3 -Mode tap`, the traversal's own count of the drawn wrists against the
controllers through the move and its blend-out; stretch `20261115-010524-lane-c2-vault`, restored `verification: 0 differences`):

| rule (`mevr armik step ...`) | ticks within 0.15 m, three vaults | mean | max |
|---|---|---|---|
| off (as before) | 131/142, 134/144, 129/140 | 0.11 m | 0.88, 1.04, 0.83 m |
| camera, past 20 units | 142/144, 141/145, 146/148 | 0.09 m | 0.21 m |
| camera, past 5 units | 139/139, 148/148, 149/149 | 0.08 m | 0.11-0.12 m |
| **camera, past 1 unit (the default)** | 145/145, 143/143, 146/146 | 0.07 m | 0.08 m |

Standing, both wrists are 0.07 m from the grips (the replayed hands' wrist-to-grip offset), so with the default the hands stay
where they rest through the whole vault. A third rule tried first, the goals kept fixed in the world when the component moved, never
fired (the component doesn't move between the goals and the write) and was dropped. Console: `mevr armik step [off|camera]
[units]` (LE1 and LE2; the rule and the counts with no word).

**LE1 unchanged** (`tools/fake-headset-session.ps1 -Tag lanec2-le1pose -BuildDir build`, session
`2026-11-15_01-28-33-lanec2-le1pose`, the run script lane B4's `laneb4-after` session ran too: 20 s in the world, the standing
replay with the hands in view, 12 s, `pause`, two snapshots, then a 2 s-per-direction walk loop): the camera's bone moved **0.0
units** in all 1,890 standing writes and all 3,466 writes with the walk (on LE1 the mesh does move up to 7.1 units between the goals
and the write while walking; the rule doesn't look at it), so nothing was solved in another frame. Against lane B4's `laneb4-after`
snapshot (main f71e7a71 before this work, the same script): both wrists 0.07 m from the grips, `hostView` identical (-43.3 / -4.1
/ -18.1 uu at 100 uu/m), the six layers at the same places (the omni-tool target 0.1 mm apart), the HUD panel, wrist piece,
label and target identical pixel for pixel, the eyes 0.025% / 0.014% of pixels apart by more than 32/255.

**A finding for "vault while holding our weapons":** with the left hand on the gun and its grip held (`vault.ps1 -Support`, the
support grip taken: `halcyon bridge: support grip taken ...: LT to the game instead of LB`), ME2's `CanPerformMantle` refuses the
vault (`the game refuses it`, the double tap never fires). See "Vaulting with the weapon held" below.

### Vaulting with the weapon held

`CanPerformMantle`'s pawn test (`0xABD700`, called first by the body `0xABD8E0`; Ghidra, `tools/ghidra/decompile.ps1 -Game LE2
0xABD700`) refuses when the pawn's `Weapon` is an `SFXWeapon` whose `IsZoomed` is set (`0xBF0CF0`, `ASFXWeapon_NativeBase::
execIsZoomed`'s body: `bIsZoomed`, bit 5 of the word at +0x3D0), then when the pawn performs a blocking action
(`IsPerformingBlockingAction`, `0xAA4D90`), then (out of cover) unless `m_bCanMantleOutsideOfCover`, and needs
`ActionState` combat. The support grip hands ME2 LT, its aim, and the aim zooms the weapon (`SFXWeapon.SetZoomed(true)`), so a
two-handed hold refuses every vault. The right hand alone holding the gun (`gripHolds`) needs ME2's holsters (step 3, below);
once they are in, `vault.ps1 -Grip` checks it, and `-Grip -Support` the two-handed hold. A fix for the zoom is the traversal's:
its one-call writes around `CanPerformMantle` (and the commit) could clear `bIsZoomed` for that call as they set
`m_bCanMantleOutsideOfCover` [verify] (whether `MantleCover`'s custom action then plays with LT still held).
**Done 2026-11-16 (lane C4):** the admission clears `bIsZoomed` for its one call and the two-handed hold vaults with LT held
(the custom action plays in the TightAim camera mode); letting the support grip go mid-move drops arm IK to third person for
0.7 s (research/le2-traversal.md "The two-handed hold").

## The sights on the sequels, mod side (lane C2, 2026-11-16; run on ME2 by lane C3 the same day)

The drawing came to ME2 and ME3 with 6d11493b ("Prototype shared VR weapon optics in LE2": `plugin/SightLayers` in both sequel
hosts, `setSight`; research/le2-le3-vr-optics.md). That pass kept the shots and the gun out of it: on the sequels
`sights::TryStartup` returned false and `sights::Current` forced the alignment assist, the shots' convergence and a scope's
aimed fire off. This step puts the mod side on ME1's path, one module for the three games with each game's names behind it.

**What is each game's** (the adapter; everything else is `game/Sights` as ME1 runs it):

| | ME1 | ME2 | ME3 |
|---|---|---|---|
| the aim error for an accuracy (the handler's function, and the call with the zoom flag set) | `BioWeaponRanged.GetPlayerAimError(Accuracy)` (native) | `SFXWeapon.GetAimError(Accuracy)` (script; `GetPlayerAimError()` takes no accuracy and calls it) | `SFXWeapon.GetPlayerAimError(Accuracy)` (script) |
| the zoom flag set for that one call | `BioWeaponRanged.bIsZoomed` | `SFXWeapon_NativeBase.bIsZoomed` (0x3D0 bit 0x20) | `SFXWeapon_NativeBase.bIsZoomed` (0x3BC bit 0x80) |
| who reads the flag | the native itself | `SFXWeapon.GetWeaponAimErrorRange` (`MinZoomAimError`/`MaxZoomAimError` instead of `MinAimError`/`MaxAimError`) | the same function |
| a scope's magnification | `Stats.AimModes` (`ZoomFOV`, `bScoped`) | `SFXWeapon.ZoomFOV` (sniper rifles; one step) | not read yet (`SFXWeapon.AimModes`, an `FSFXWeaponAimMode` array: left for the ME3 sniper fixture) |
| the sight line's trace, the near plane, the lens part | `Engine.Actor.Trace`, `LocalPlayer.NearClipPlane`, `SkeletalMeshComponent.HideBone` / `UnHideBone` / `IsBoneHidden`: the same on all three |

**How it works on a sequel** (ME2's script, read with `conversation-scan.exe --script`): each shot's `SFXWeapon.GetAdjustedAim`
asks `Instigator.GetAdjustedAimFor` (hand aim's handler answers with the barrel, or with `sights::ShotAlongSight`: from the
muzzle toward what the sight line hit this tick), then adds `GetPlayerAimError() * 182.044` of random spread. The spread is
`GetAimError(InvManager.Accuracy)` = `Lerp(range.X, range.Y, Accuracy)` with the range picked by `bIsZoomed`. With a scope's
picture up (`gScopeUp`: the host shows it and ADS holds), `sights::OnAimError` (a CallFunction post-handler on the function
in the table) takes the unzoomed answer, finds the accuracy it was made from (the function called for accuracy 0 and 1),
and replaces it by the zoomed answer for that accuracy (`weapon::ZoomedAimError`: the flag set, the function called, the flag
cleared, on the game thread inside the handler; journalled `Weapon.bIsZoomed (one aim-error call)`). The camera never sees
the flag, so the game's own zoom view doesn't start.

**Files** (`git show` of the step's commit): `game/Sights.cpp` (handles `kEveryGame`, `TryStartup` on every game, no
forced-off block: `converge` and `aimedFire` stay off only where `Actor.Trace` or the aim-error function wasn't found),
`game/Weapon.cpp` (`AimError`, `ZoomedAimError` on every game), `game/ScopeParts.cpp` (the hide on every game; its material
log stays ME1's), `engine/ReflectCheck.cpp` (LE2 and LE3 rows), `game/HandAim.cpp` (the shot line says what the sight line
hit and how far the shot is off it), `game/Holsters.cpp` + `ConsoleRequest.cpp` (`mevr weapon <pistol|shotgun|assault|sniper>`
on the sequels: the inventory chain by family from `game/Le2Squad`, `BioPlayerController.SwitchWeapon`).

**Not ported:** `game/ScopeCapture` (the scope's own zoomed render): the sequel hosts don't serve `setScopeCapture` and have
no scene-capture plumbing (ledger R31); a sequel's scope shows the other eye's image enlarged, as ME1 did before research/39.
ME3's zoom steps (above). `weapon::Zoomed()` still answers nothing on ME2 (it would stand the level camera down while the
support grip's LT zooms the gun: decide with the view level's owner).

**The check to run once a game starts again** (queued 2026-11-16: Steam on the machine waited on a dialog with no window and
started no game). On ME2, with a stretch open and Save 4 loaded:

```
python tools/make-ads-recording.py <out>\pistol.mevrrec 40 90 1.75 0.085 1.5 0.1 --no-support --no-grip
tools\replay.ps1 -Path <out>\pistol.mevrrec -Loop -Inputs          # the pistol's sight 8.5 units over the bore: the right eye on its line
tools\snapshot.ps1 -Delay 6 -Tag sights-pistol                      # pass: the json's "sights" block names the right eye, the right eye's PNG shows the dot
# the log: `sights: the right eye looks through it`; `sights: shots converge on the sight line (Actor.Trace found) ...`;
#          `hand aim: shot N: along the gun barrel toward what the sight line hit ...; the sight line hit <what> D m out: aimed
#          0.0xx degrees off the sight line, x.xxx off the barrel`  (pass: under the weapon's aim error off the sight line)
tools\console.ps1 "mevr weapon sniper"                               # a Soldier carries one after Freedom's Progress's loadout; else it lists what is carried
python tools/make-ads-recording.py <out>\sniper.mevrrec 40 90 1.75 <the sniper's sight height in m, from its `sights:` line> 3 0.1 --no-support --no-grip
# pass: `sights: ADS on`, a "sights" block with the scope's picture up, `sights: first scoped shot with aimed fire: aim error A -> B`,
#       and the shot line within B degrees of the sight line; `sights: the sniper rifle is raised to the eye: hid ...` if its model has a lens bone
```
Then LE1 at the same pose as before the change (`tools/fake-headset-session.ps1`, the standing replay, two snapshots,
`mevr weapon pistol` if the career carries one): the same `sights:` and `hand aim:` lines and the same layers.

The whole check as one script: `tools\le2-halcyon-run\sights.ps1 -Holder <you>` (results in the stretch's `sights-<time>` folder).

**Run on ME2** (lane C3, 2026-11-16 13:23, main b8fbb98c merged; stretch `20261116-131605-407-LE2-lane-c3-sights`, its
`sights-132301` folder with the two snapshots and `LE2MEVR-run.log`; restored `verification: 0 differences`):
- **The pistol** (`SFXWeapon_HeavyPistol`, automatic placement: 19.8 units behind the muzzle, 8.5 above the bore): the
  snapshot's `sights` block says `kind: red dot`, `dotEye: right`; the right eye's PNG shows the window's ring with the red dot
  in it. 14 shots: `hand aim: shot N: along the gun barrel toward what the sight line hit`, the shot 0.12 to 0.68 degrees
  from the aimed direction (the weapon's own aim error), the aimed direction 0.21 to 0.61 degrees off the sight line, which
  is the 8.5-unit height of the sight over the bore seen from the hit (8.2 to 23.5 m out: atan(0.085 / d)): the shots land
  on what the sight line hits.
- **The sniper rifle** (Save 4 carries one; `mevr weapon sniper`): `sights: SFXWeapon_SniperRifle_1 ZoomFOV 10.0 degrees,
  8.0x`, the sight on the model's `Scope_Eye` bone, `sights: the sniper rifle is raised to the eye: hid 'Scope_Lens'`,
  `sights: ADS on` 0.1 s later, the scope's picture up for 1,922 frames from the eye copy (the snapshot: `kind: scope`,
  `scopeDrawn: true`, `aimingEye: left`, `sourceEye: right`, `pictureSource: eye copy`; the left eye's PNG shows the
  magnified disc over the scope), `sights: first scoped shot with aimed fire: aim error 10.0000 -> 0.0000` and nine shots
  0.00 degrees from the aimed direction; `showed 'Scope_Lens' again` when the picture went.
- **Seen, not judged here:** in the pistol's PNG the ring stands about a hand's width left of the gun's own top. The eyes
  on the fake headset take the monitor's shape on ME2 (4096x2304, research/le2-renderer-integration.md), so a metric quad
  and the world don't line up there; [verify] in the headset. With the default `-SniperDropM 0.10` the eye sits 6 cm
  under the scope's line (its sight is 13.4 units over the bore) and the assist slides the gun: pass `-SniperDropM 0.134`
  for the eye on the line.

**LE1 unchanged** (lane C3, the same day; `tools/fake-headset-session.ps1` with `scratch/c3/run-le1.ps1`: 20 s in the world,
the standing replay with the hands in view, `pause`, two snapshots; then the pistol and the assault rifle drawn by `mevr
weapon`, each raised and fired by `make-ads-recording.py ... --no-support`'s replay, a snapshot under `pause`). Three sessions of a build of main b8fbb98c (`2026-11-16_13-37-51-c3-le1-before`, `..._14-03-25-c3-le1-before2`, `..._14-26-12-c3-le1-before3`) against one of this step (`..._13-53-39-c3-le1-after`): the standing pose has an identical `hostView` (-43.3 / -4.1 / -18.1 uu), the same six layers at the same places (the omni-tool target 0.2 mm apart), the HUD panel, label, wrist piece and target identical pixel for pixel, the eyes 0.03 to 0.19% of pixels apart by more than 32/255; with the assault rifle raised (before3 against after) the `hostView` is identical again, the layers the same, the eyes 0.003% apart, the dot in the right eye in both, the sight on the `Scope` bone 42.1 / 9.7 against 41.8 / 9.6 units; the pistol has no sight in the owner's settings (`pistol = none`) and logs the same lines. Every `sights:`, `hand aim:` and holster draw line of the new build (numbers masked) is in the build of main's log. before2 ran while other lanes built (the game hitched: `the right controller isn't tracked` every second, a draw in 0.95 s instead of 0.13) and is kept only as that: a same-pose run needs a quiet machine.

## The shouldered gun on the sequels (lane AU8, 2026-11-18)

The owner's answer of 2026-11-18 (docs/trilogy-plan.md "Answered 2026-11-18, later": option B of lane AU2's trace in
docs/knowledge/audits/me2/weapons-and-sights.md, "Proposal: the shouldered gun's accuracy without the game's zoom"). ME1
keeps its behaviour: both hands on a long gun at the shoulder hold LT, the game's own aimed fire.

**What changed on ME2 and ME3.**
1. The support grip holds no LT (`engine::adapter::kSupportGripHoldsLt` false; `game/SupportGrip` sends no
   `gamepad::kFlagSupportAims`). The sequels' LT is `SFXGameModeDefault.TightAim` -> `SFXWeapon.SetZoomed(true)`: the camera's
   zoom, a sniper's sway (`SFXInventoryManager.ApplySniperDrift`), ME2's sniper time dilation
   (`SFXGameEffect_SniperTimeDilation`), the zoomed recoil and the refusals of `TryMelee`, `TryHeavyMelee`, ME3's `TryRoll`,
   `TryForwardRoll` and cover moves. None of them is reached now: the game's zoom state never starts from the grip.
2. **`game/ZoomedShots`** (new, the sequels only; ME1 compiles nothing in it): while the gun is shouldered (the grip held,
   the right grip at the calibrated shoulder unless `[Controls] aimAtShoulder = 0`: the rule that sent LT, without ME1's
   sniper exclusion, which was LT's scope camera) or a scope's picture is up, both with `[Sights] aimedFire`, each call in
   `engine::adapter::kZoomedCalls` runs with the held weapon's `SFXWeapon_NativeBase.bIsZoomed` set for that call only: an
   `engine::call_function` scoped handler (`AddScopedHandler`) sets it on the way in when the game hasn't (journalled
   `ZoomedShots.bIsZoomed (one call)`) and clears it in the hook's `__finally` if the weapon still lives. The calls:
   - `SFXInventoryManager.UpdatePlayerAccuracy` (ME2) / `SFXPlayerInventoryManager.UpdatePlayerAccuracy` (ME3), each tick from
     the manager's `Tick` for the human player: the zoomed base inaccuracy (`Base_Acc_Zoom`, not `Base_Acc_Standing`), the
     weapon's `ZoomAccFirePenalty` and `ZoomAccFireInterpSpeed`. Its object is the inventory manager; the flag is the held
     weapon's (`Pawn.Weapon`). The `Accuracy` it writes is what each shot's aim error lerps.
   - `SFXWeapon.GetWeaponAimErrorRange` (both): `MinZoomAimError`/`MaxZoomAimError`, from `GetAimError` (ME2) or
     `GetPlayerAimError` (ME3) at each shot.
   - `SFXWeapon_SniperRifle_Base.GetFireModeBaseDamage` (ME3 only): no `SniperRifleDamagePenalty` (ME3 multiplies an unzoomed
     player's sniper damage by it; every VR sniper shot was penalized before, scope or not).
   Nothing else reads the flag in those calls but what the zoom should change: `UpdatePlayerAccuracy` also asks
   `GetPlayerViewPoint` (the camera's cached view, no FOV) and the weapon's rate of fire.
3. `game/Sights`' one-call rewrite of the aim error (ME1's route, which the sequels used for a scope's picture) stands down
   on the sequels while the zoomed calls are on: the range is already the zoomed one inside the call.

**Not carried over: ME3's shotgun cone.** `SFXWeapon_Shotgun_Base.PollLatentFire` narrows the pellets past 5 m when zoomed
(`ZoomAccuracyBonus` against `AccuracyBonus`, with a selected target over 500 units away), but it runs from a timer
(`FirePellets` sets `SetTimer(..., 'PollLatentFire')`), which the engine calls through ProcessEvent: no script call for the
CallFunction hook to bracket, and the host's ProcessEvent hook runs the mod's handlers only after the call. Options, for the
owner: (A) a ProcessEvent bracket in the host (a HostServices entry: the mod's enter and leave around the call for the
functions it names; host and mod deploy together), then `PollLatentFire` joins the list; (B) leave the cone as the hip's
(its narrowing applies only to a selected target past 5 m).

`mevr zoomshots [on|off|auto]` forces the rule (a test without hands at the shoulder); the heartbeat's `zoomed shots:` line
and the snapshot's `"mod"."sights"."game"."zoomedShots"` give the accuracy and each shot's aim error, zoomed against plain,
the last spread range and (ME3) the last sniper damage of each.

**In the game (fake headset, 2026-11-18; the rule forced with `mevr zoomshots on`, shots by a replayed right trigger; sessions
`%LOCALAPPDATA%\MEVR\sessions\20261118-155608-952-LE3-au8-me3` and `...-161504-261-LE2-au8-me2`; scripts in the lane's scratch).**
- ME3's combat fixture (`me3-combat.ps1 -Loadout '2 1 all'`), the Avenger: each shot's aim error 3.63 hip (17 shots) -> 0.40
  zoomed (41), the spread range 3.0-9.3 -> 0.3-1.5. The Black Widow: 3.15 -> 0.30, the range 2.4-4.65 -> 0-0, and **its base
  damage 257.0 hip -> 514.1 zoomed** (`SniperRifleDamagePenalty` 0.5 not applied).
- ME2's Save 4, the pistol: the aim error 1.25 (23 shots) -> 0.375 (21), the range 1.25-6.875 -> 0.375-1.875.
- `mevr calltrace` of `tightaim`/`setzoomed` through the zoomed firing: no call on either game; on ME3 the camera's
  `GetStandingCombatCamera`, the reticle, `PostProcessInput`, `PlayHit` and the recoil's `SetWeaponRecoil` asked `IsZoomed`
  outside the brackets (false) and only `UpdatePlayerAccuracy`'s own question saw it on. The support grip's heartbeat
  said `bIsZoomed off` throughout; the eyes' FOVs in the snapshots unchanged.
- Not run: a real support grip at the shoulder (the fake headset has no hands to shoulder a gun: the rule that opens the
  gate is the unchanged `AtShoulder` one), and melee or the roll in the same seconds (by construction the game's
  `IsZoomed` never saw the flag outside our calls).

## Holsters and the reload on the sequels: the plan (the holsters built 2026-11-16: the next section)

`plugin/PadMerge` with the grip flags is in both sequel hosts (f71e7a71), so the mod's side is what is missing. Both are
one shared module each; what is each game's goes behind a small table, as above.

**Holsters** (`game/Holsters.cpp`, 1,160 lines, today `#if MEVR_GAME_LE1` around every game access):
1. Separate the game access into one struct (`holsters::Game` already holds the read state): `Carried()` (which families are
   carried), `Draw(family)`, `Holster()`, `Drawn()`, `Switching()`, `HeldFamily()`. ME1's are the quick slots,
   `SFXGameModeDefault.SwitchToWeapon`, `TryHolster`, `IsInCombatActionState`, `IsSwitchingWeapons`, `GetRangedWeaponType`.
2. The sequels' (ME2's script read 2026-11-16; ME3's names are the same in LExSDKv2's headers, [verify] its script):
   - carried: the pawn's inventory chain by family, `le2_squad::Weapons` / `WeaponType` (five families: the SMG is ME2's
     `SFXWeapon_AutoPistol_Base`, ME3's `SFXWeapon_SMG_Base`; `core/ik/BodySlots`' `Weapon` enum has four: add the fifth and
     give it a slot, docs/le3-port-reuse-audit.md "Holsters");
   - draw: `BioPlayerController.SwitchWeapon(oWpn)` (-> `SFXInventoryManager.SetWeaponIfAvailable`), and the weapon out of
     its holster is the pawn's action state: `m_oBehavior.SetActionState(1)` (combat; what `SFXGameModeBase.FireWeapon` and
     `NextWeapon` do first). `mevr weapon <family>` already calls the first;
   - holster: `SFXGameModeDefault.TryHolster` (ME2: refuses while reloading, in combat (`InCombat()`), using a power or in an
     animated transition, then `SetActionState(0)`);
   - drawn: `BioPawn.ActionState == 1` (`IsCombatActionState`); switching: the weapon's state `WeaponEquipping` /
     `WeaponPuttingDown` (`IsInState`), which `NextWeapon` and `SwitchToBackupWeapon` also test.
3. "Only holsters draw": ME2 has no `DrawPrimaryWeapon`; the game's own draws are `SetActionState(1)` from
   `SFXGameModeBase.FireWeapon` (the trigger), `TryReload`, `NextWeapon`, `SwitchToBackupWeapon` (`SetCombatState_Active`) and
   the powers. `BioPawnBehavior.SetActionState` takes a parameter, so ME1's skip handler (parameterless script functions only)
   doesn't fit: use a CallFunction pre/answer on the callers instead (skip `FireWeapon`'s body while holstered), or leave the
   trigger drawing on the sequels and say so. Find the routes with `mevr calltrace 10 setactionstate`.
4. The pad: once `holsters::TryStartup` succeeds on a sequel, `AdjustPad` sends `gripHolds`, and the right grip stops being
   RB (today it opens ME2's power wheel after 0.3 s). The power wheel then needs ME1's "right stick click held" route or the
   hand power wheel (GF's lane).
5. Checks: a replayed right-grip squeeze at the hip slot draws the pistol (`holsters:` lines, `mevr state`); `vault.ps1 -Grip`
   (the gun held through the vault), and `-Grip -Support` once the traversal clears `bIsZoomed` for its `CanPerformMantle`
   call (above: the two-handed hold zooms the gun and the game refuses the vault).

**The reload** (a new `game/Reload` over `core/vr/PhysicalReloadRequest`, the coordinator; `core/ik/BodySlots` for the clip
pouch; `core/ik/Vent`'s receiver zone for the gun's side):
1. The gesture: the left hand at the clip pouch (a body slot, left hip by default) with a grip squeeze takes a clip
   (`Signals::acquire`), the hand brought into the gun's receiver zone (`SupportSample::alongMeters`/`upMeters`/`sideMeters`,
   as `vent::InZone`) is the insert (`Signals::insert`); `eject` is implicit at the grab (ME2's clip ejects in the game's own
   reload animation). The hand grant is the left hand while no support grip is held.
2. The native entry: `SFXWeapon.TryReload()` only if `CanReload()` (ME2: `bWeaponCanBeReloaded && AmmoUsedCount > 0 &&
   HasSpareAmmo()`, and the pawn's `CanReload`), i.e. `StartFire(1)`: never `DoReload`. `SFXGameModeDefault.TryReload` is the
   button's route (it first draws a holstered gun and returns). ME3: the same names in the SDK, [verify] its script.
3. Feedback for the coordinator: accepted = `BioPawn.IsReloading(true)` turned true (or the weapon entered its reload state);
   finished = `AmmoUsedCount` back to 0 with `CurrentSpareAmmo` down (the LE2 observers read these: ReflectCheck has the rows);
   aborted = `IsReloading` false with the counts unchanged.
4. The check (the plan's): Save 4's pistol reads 12 / 0 / 60 (magazine, used, spare), one shot 11 / 1 / 60, the replayed
   gesture 12 / 0 / 59; `R` never sent. `tools/make-rack-recording.py` is the model for the recording.

## Holsters on the sequels (lanes C3 and C4, 2026-11-16)

The plan above, built: `game/Holsters` is one module for the three games with each game's names in its adapter (995dca38 and
this landing), and "only holsters draw" is one rule, `RefuseGameDraw` (the owner, 2026-11-16: "trigger draw a holstered weapon?
No, none of the games should do that."; docs/trilogy-plan.md rule 31).

| | ME1 | ME2 | ME3 |
|---|---|---|---|
| what is carried | the quick slots (`BioEquipment.m_QuickSlotArray`) | the pawn's inventory chain by weapon family (`le2_squad::Weapons` / `WeaponType`; the SMG has no body slot yet) | the same |
| draw | `SFXGameModeDefault.SwitchToWeapon(slot)` | `BioPlayerController.SwitchWeapon(oWpn)`, then `BioPawnBehavior.SetActionState(1)` if holstered | `SwitchWeapon` alone (no action state) |
| holster | `SFXGameModeDefault.TryHolster` | the same (`SetActionState(0)`, refused in combat, mid-reload, in a power or a transition) | the same name [verify on a mission] |
| drawn | `BioPawn.IsInCombatActionState` | `BioPawn.ActionState == 1` | an equipped gun |
| mid-switch | `BioPawn.IsSwitchingWeapons` | the weapon's state `WeaponEquipping` / `WeaponPuttingDown` (`Object.GetStateName`) | the same |
| the game's own draws refused | `SFXPawn_Player.DrawPrimaryWeapon` skipped by its caller (ME1's skip handler) | `BioPlayerInput.IsCombatEnabled` answered false inside `SFXGameModeBase.FireWeapon`, `SFXGameModeDefault.TightAim`, `TryReload`, `NextWeapon`, `PrevWeapon` while `ActionState` isn't combat (each button's own first test; they draw by `SetActionState(1)` after it) | nothing to refuse: no holstered state on foot |
| the right stick click | holds the game's power wheel open (the mod's own Enter/ExitPowerWheel) | the same since 2026-11-17 (`SFXGameModeDefault.EnterPowerWheel`; plain RB until then: a tap quick-cast power 2) | the same since 2026-11-17 (`SFXGameModeBase.EnterPowerWheel`) |

The gate was ME1's: `ReadGame` asked `ue3::runtime::Ready()`, which is true only where ME1's modules run on the layer, so on ME2
the holsters stood down (`holsters: our reflection layer is off`). They ask `Up()` now (on LE1 the same answer). Every class,
field, function and parameter has its ReflectCheck row (`BioPawn.m_oBehavior`, `ActionState`, `BioPawnBehavior.SetActionState`
`eState`, `Object.GetStateName` `ReturnValue`, `SFXGameModeDefault.TryHolster`, `BioPlayerController.SwitchWeapon` `oWpn`).

**The check** (`tools\le2-halcyon-run\holsters.ps1 -Holder <you>`, replayed right-hand grabs from `tools/make-holster-recording.py`;
ME2, Save 4, the pistol out; stretch `20261116-191210-770-LE2-lane-c4-holsters`, its `holsters-191431` folder, restored with
`verification: 0 differences`):
- startup: `holsters: script functions found (BioPlayerController.SwitchWeapon true, SFXGameModeDefault.TryHolster true, the inventory
  chain and game modes true, the action state ... found, Object.GetStateName true)`, `the game's own draws with the weapon away can
  be refused ...: BioPlayerInput.IsCombatEnabled is answered false inside ... FireWeapon, ... TightAim, ... TryReload, ... NextWeapon,
  ... PrevWeapon`; on foot in VR `the right grip holds the gun (no RB from it); RB is the right stick click`; `the player carries:`
  the grenade launcher (a heavy weapon: no slot), the heavy pistol, the shotgun, the assault rifle, the sniper rifle.
- the right shoulder: `grab at shoulderRight: drawing the assault rifle (BioPlayerController.SwitchWeapon; was pistol)`, in hand after
  0.63 s; let go there: `the assault rifle goes back in its holster (SFXGameModeDefault.TryHolster)`, holstered after 1.14 s.
- **the trigger with the weapon away** (four pulls, `make-ads-recording.py`'s replay): four times `holsters: the game wanted to draw
  your weapon (Function SFXGame.SFXGameModeBase.FireWeapon): refused, only a grab at a holster draws ...; trigger ..., so no buzz`, and
  no `came out without a holster grab` line: the gun stayed away.
- the right hip: `drawing the pistol (BioPlayerController.SwitchWeapon, BioPawnBehavior.SetActionState(combat); was holstered)`, in
  hand after 0.03 s; let go there, holstered after 0.91 s; the snapshot with it held (`2026-11-16_19-15-22-885`).
- the hip, let go in front of the chest (the release grace): not seen: the head-tracked view went invalid three times for a few
  frames during that replay (`holsters: no head-tracked world view`), which resets the slots' machine, and the pistol stayed out.
  **Seen since** (lane WP, stretch `20261116-224515-586-LE2-wp-weapons`, the same replays): `let go of the pistol away from its
  slot: it goes back in 0.20 s unless you squeeze again`, `let go 0.20 s ago away from hipRight: the pistol goes back in its
  holster (SFXGameModeDefault.TryHolster)`, `the pistol is holstered after 0.93 s`; the rifle at the shoulder (in hand after 0.62
  s, holstered after 1.13 s), the pistol at the hip (0.03 s, 0.92 s) and the four refused trigger pulls as before.
- `vault.ps1 -Runs 3 -Mode tap -Grip` (the right grip holding the pistol through the vault): three vaults admitted, every tick's
  wrists within 0.15 m (left 0.11, right 0.08 m at most). `-Grip -Support` (both hands on the gun): the game refuses the vault, as
  before (the two-handed hold: research/le2-traversal.md "What is left").

**ME1 at the same pose** (`tools/fake-headset-session.ps1`, lane C4's `scratch/c4/run-le1.ps1`: the standing replay paused, two
snapshots, then the pistol and the assault rifle drawn by `mevr weapon`, raised and fired, a snapshot each; before = a build of main
ad0db88c, after = that main with the floor lock's hold and these holsters; sessions `2026-11-16_19-05-06-c4-le1-before`,
`..._19-09-00-c4-le1-after`): the standing pose's `hostView` identical (-43.3 / -4.1 / -18.1 uu, 100 uu/m), the same layers at the
same places, the HUD panel, wrist piece, label and target identical pixel for pixel, the eyes 0.49% / 0.33% of pixels apart by more
than 32/255 (what animates between launches), both wrists 0.07 m from the grips; the guns' snapshots the same layers, the HUD
identical but for 0.007% of the pistol's, the eyes 0.08% apart. Every `holsters:`, `sights:`, `hand aim:`, `floor lock:` and `support
grip:` line of the after build (numbers masked) is in the before build's log; only arm IK's timing-dependent first write after the
replay's restart and one `weapon: grip moved` line differ.

## The reload by hand: the gesture's core (lane C4, 2026-11-16; the game's side not built yet)

`core/vr/ReloadGesture` (MIT, unit-tested in `tests/ReloadGestureTests.cpp`) is the plan's step 1, game-independent: it qualifies
the gesture and gives `vr::reload::Coordinator` (`core/vr/PhysicalReloadRequest.hpp`, the P0 contract: one correlated native
request per insertion, nothing retried while one is uncertain) its three signal levels.
- **Take a clip:** a NEW left squeeze (the pad mapping's latch, 0.6 on, 0.4 off) that no other feature claimed
  (`Input::squeezeClaimed`: grenades, the support grip, the vent) within `pouchRadiusM` (0.13) of the pouch's centre, with a gun in
  the right hand and the game willing (`Input::reloadable`). `eject` goes high on that tick (ME2's clip ejects in its own
  animation), `acquire` on the next (the coordinator takes one edge a tick). No gun, or nothing to reload: `Refused` with a reason.
- **Insert:** the hand, grip still closed, ENTERS the receiver (`core/ik/Vent`'s box in the gun's frame, the same numbers as the
  vent's) from outside it: `insert` high for that tick, then everything low (the coordinator re-arms only after a full release).
- **Overlord's contact invariants** (research/le2-overlord-reload-reuse.md, translated, no code copied): a clip taken with the hand
  already in the receiver, a step past `maxStepM` (0.20 m) in one tick, or the gun's frame lost and found again needs the hand
  to leave the receiver and come back; a held contact inserts once; a grip let go before the receiver is `Dropped`, a hand that
  loses tracking `Lost` (the adapter then calls `Coordinator::CancelGesture`), a clip held past `holdTimeoutS` (8 s) is dropped.
- Tested with the coordinator: one gesture gives Ejected, Acquired, Requested; a second gesture while the request is pending gets
  WaitingNative; after `Finished` (reconciled) the next gesture asks again; a dropped clip asks nothing.

**What the game's side (`game/Reload`, the adapter) still has to do**, ME2 first (the names are the plan section's, above):
1. Each tick on foot in VR with a gun in the right hand: the pouch's centre from `core/ik/BodySlots` (a fifth slot, the left hip,
   `[Reload] pouch*` keys, scaled with the standing eye like the holsters' slots), the left hand's place in the gun's frame from
   `engine::bone_pose::LatestSupport()` (as `game/Vent` reads it), the left grip, and the claim (`throwables::Claim`,
   `support_grip`, `vent` run first in Mod.cpp's tick). `reloadable` = `SFXWeapon.CanReload()` (ME2:
   `bWeaponCanBeReloaded && AmmoUsedCount > 0 && HasSpareAmmo()`).
2. The coordinator's frame: identity generations from the world, the held weapon's object and the context (on foot, not a menu);
   the grant = the left hand while no support grip is held (a new lease each time it is granted again).
3. On the coordinator's `request`: `SFXWeapon.TryReload()` through `ue3::runtime::Call` on the game thread, never `DoReload`, never
   an `R` press. Feedback: `Accepted` when `BioPawn.IsReloading(true)` turns true, `Finished` (reconciled) when `AmmoUsedCount` is
   back to 0 with `CurrentSpareAmmo` down, `Aborted` when `IsReloading` goes false with the counts unchanged.
4. ReflectCheck rows for every name, a `[Reload]` block in `ASI\<GAME>MEVR.ini` (ME1's keys and defaults where ME1 has them: the
   vent's zone), logs `reload:`, a snapshot block `"mod"."reload"`, `mevr reload ...` console lines, and
   `tools/make-reload-recording.py` (the model: `tools/make-rack-recording.py`).
5. The check: Save 4's pistol 12 / 0 / 60 (magazine, used, spare); one shot 11 / 1 / 60; the replayed gesture 12 / 0 / 59; a
   full clip refused; ME1 untouched (n/a there: it has heat and the vent).

## ME3's holsters, from its own script (lane WP, 2026-11-16)

Read with the private LegendaryExplorerCore scanner (`local-tools/le2-le3-optics-survey/scanner`, `MEVR_OPTICS_PRIVATE_LE3=1`,
`--script SFXGameModeDefault.TryHolster` and greps over `SFXGame.pcc`; the decompiled text stays in the lane's scratch folder).
In words:
- **ME3 has no holstered state on a mission.** `SFXGameModeDefault.TryHolster` only breaks the player out of cover (when not
  reloading, not in combat, not using a power or in a transition); it never puts the weapon away. The cheat manager's
  `HolsterWeapon` likewise only leaves cover. A weapon is put down only inside a switch: `SFXWeapon`'s `WeaponPuttingDown`
  state plays the holster custom action (11), waits for its end, re-attaches the gun to its slot and calls
  `InvManager.ChangedWeapon`. That state's begin handles a switch to **no** weapon (`InvManager.PendingWeapon == None`: the
  pawn's weapon animations are set up for none).
- **The trigger can't draw a weapon that isn't equipped.** `SFXGameModeBase.FireWeapon` only sets the controller's `bFire`
  after `BioPlayerInput.IsCombatEnabled()`; the pawn's `BioPawn.HandleWeaponFiring` returns at once when `Pawn.Weapon` is
  None. `NextWeapon`/`PrevWeapon` return with no weapon; `TightAim` and `TryReload` (the button's) begin with the same
  `IsCombatEnabled` test as ME2's.
- So **ME1's holster is reachable on ME3 through the game's own put-down into no weapon**: `SFXInventoryManager.SetCurrentWeapon
  (None)` (the base `InternalSetCurrentWeapon` sets no pending weapon and calls the held gun's `TryPutDown`). The draw from no
  weapon is `BioPlayerController.SwitchWeapon(oWpn)` (`SetWeaponIfAvailable` -> `SetCurrentWeapon(gun)`: with no current
  weapon `ChangedWeapon` at once, the equip animation).

Built (`game/Holsters`' ME3 adapter): **TryHolster is never called on ME3** (it pulled a player out of cover each time a gun
was let go). The holster is `SFXInventoryManager.SetCurrentWeapon(None)` behind `[Holsters] le3PutAway` (file-only, **0 by
default**: no ME3 save on this machine loads on a mission with a weapon, so the put-down is unverified; with 0 a let-go gun
stays in the hand and the release grace never runs: `holster` is Off for the machine). Only holsters draw is the same rule
as ME2's: `IsCombatEnabled` answered false inside FireWeapon, TightAim, TryReload, Next/PrevWeapon while the weapon is
away, which on ME3 means the pawn carries weapons and has none equipped (the Normandy's non-combat pawn carries none: nothing
is refused there). ReflectCheck rows: `SFXInventoryManager.SetCurrentWeapon` `DesiredWeapon`, `IsCombatEnabled` `ReturnValue`.

**The ME3 saves on this machine** (every career folder and every retained session copy, decoded read-only with
`tools/le3-save.py inventory`; 19 distinct files by hash): Save 31, AutoSave, QuickSave, LegendSave and the navigation
lanes' retained autosaves are all `BioP_Nor` with a `VanguardNonCombat` pawn and no weapons; the chapter save is London's
`Biop_End001/2` (the injured Shepard's own game mode) and the NG+ save the ending. So ME3's draw, put-away and trigger checks
still wait for a combat fixture (research/le3-combat-fixtures.md, docs/trilogy-plan.md T05).

## A body slot for the SMG (lane WP, 2026-11-16; a recommendation until the owner's headset check)

The sequels' fifth weapon family (ME2's `SFXWeapon_AutoPistol_Base`, ME3's `SFXWeapon_SMG_Base`) had no slot: the game's own
switch drew it. Every ME2 class but the Soldier carries both the heavy pistol and the SMG, and ME3 lets any class carry
both, so the SMG needs its own place, not the pistol's. `core/ik/BodySlots` has five slots now (`kSmgSlot`, `Weapon::Smg`);
ME1 has no SMG, so its game code switches the fifth off (`hand_calibration::GameHolsterLayout`, its key isn't read and ME1's
`holsters:` lines and snapshot list the same four slots as before).

**`thighRight`: right 0.08, up -0.90, forward 0.15 m from the eyes at a 1.65 m eye height, radius 0.12** (seated: 0.15,
-0.60, 0.55, r 0.20, over the right knee). The same reasoning as research/19's "Default slots":
- a one-hander like the pistol, so low on the right side, as the thigh holsters for one-handers in the survey (The Walking
  Dead: Saints & Sinners) and GoldenEye VR's hip pair;
- 0.36 m from the pistol's centre: more than both radii plus both exit margins (0.14 + 0.12 + 2 x 0.05), the rule the shotgun's
  move to the left hip set (the belly slot at 0.29 m was "reliably too close"); 0.42 m from the shotgun's cross-draw;
- 0.25 m from a right hand hanging relaxed at the side (no contact buzz while walking), 0.48 m from the low hip-fire hand and
  0.35 m from the belly rest (both tested);
- in front of the body, where an inside-out headset (the owner's Quest) still sees the controller: the small of the back,
  one of the sequels' own attach slots (`EAttachSlot.EASlot_LowerBack`), is out of the cameras' view;
- smaller than the hip's zone so a hand on its way to the pistol doesn't brush it.
Not seen in the game: Save 4's Soldier carries no SMG. `[Holsters] thighRight` / `seatedThighRight` move it.

## The SMG at the small of the back (lane SG, 2026-11-17; an option, the default the owner's choice)

The owner (2026-11-16): "In mass effect, I think we need to add an option for the lower back on the rear as another option.
Since I think that's where the holster position exists on the equipment menu." Built as `[Holsters] smgSlot = thighRight |
lowerBack` (sequels only; **`thighRight` stays the default** until the owner chooses).

**Where the sequels hang their weapons on Shepard**, read offline with Legendary Explorer's library (the nightly in
`mod-manager/`, read only; a probe in the lane's scratch folder, its output game data, never committed): every class default
and every archetype in all of ME2's 6,541 and ME3's 7,943 packages (DLC included) for `SFXWeapon.AttachSlot` and
`BioPawn.AttachSlots`, and every human mesh's sockets (`BIOG_HM*`: 265 meshes in ME2, 332 in ME3, male and female) placed on
the mesh's reference skeleton. Both games are the same: no archetype overrides a default, and every Shepard mesh has the same
five sockets (within 1-3 cm on a few). Places are from Shepard's eyes (`Eye_Right`/`Eye_Left`, 1.716 m up in the reference
pose), scaled to the holsters' 1.65 m eye height, as the slots are written (right, up, forward in metres):

| attach slot (`EAttachSlot`) | the pawn's socket (`BioPawn.AttachSlots[i]`) | its bone | the place on Shepard | the weapons the game hangs there (`SFXWeapon.AttachSlot`) |
|---|---|---|---|---|
| 0 `EASlot_Holster` | `Socket_05` | `Chest` | -0.16, -0.52, -0.06: the left side of the waist | the pistol (ME2 `SFXWeapon_HeavyPistol`, ME3 `SFXWeapon_Pistol_Base`) **and the SMG** (ME2 `SFXWeapon_AutoPistol`, ME3 `SFXWeapon_SMG_Base`) |
| 1 `EASlot_LowerBack` | `Socket_06` | `LowerBack` | 0.10, -0.60, -0.20: the small of the back, right of the spine | the shotgun (ME2 `SFXWeapon_Shotgun`, ME3 `SFXWeapon_Shotgun_Base`) |
| 2 `EASlot_LeftShoulder` | `Socket_03` | `Chest2` | -0.05, -0.25, -0.24: high on the back, left | the sniper rifle |
| 3 `EASlot_RightShoulder` | `Socket_04` | `Chest2` | 0.09, -0.38, -0.25: the back, right | the assault rifle (`SFXWeapon`'s own default) |
| 4 `EASlot_CenterBack` | `Socket_07` | `Chest2` | 0.02, -0.50, -0.25: the middle of the back | heavy weapons (`SFXHeavyWeapon`) |

The script, in words (`SFXWeapon.GivenTo`, `AssignToSlot`, `SFXInventoryManager.GetWeaponInSlot`, the end of the
`WeaponPuttingDown` state, `SFXWeapon.AttachWeaponTo`): a weapon given to the pawn takes its family's slot only if no other
weapon it carries has that slot; a put-down gun is hung on `AttachSlots[CharacterSlot]`, and a weapon without a slot is hung on
no socket at all, so it isn't shown on the body while away. With a pistol and an SMG (every ME2 class but the Soldier) only the
first one given is shown, at the left side of the waist, muzzle up and forward along the flank (the SMG's muzzle socket placed
through `Socket_05`'s rotation); a gun on `Socket_06` lies flat across the small of the back, muzzle to the right.

**So the games never carry the SMG at the lower back:** its holster is the pistol's, at the left side of the waist (a
cross-draw), and the lower back is the shotgun's. A gun the owner remembers across the small of the back is the shotgun's
place. The equipment screen itself was not looked at: no save on this machine has an SMG (Save 4's Soldier carries none), and
the data above needed no game run.

**`lowerBack`: right 0.00, up -0.60, forward -0.26 m, radius 0.12** (seated 0.30, -0.48, -0.24, r 0.20). From the game's
`Socket_06` (0.10, -0.60, -0.20), moved onto the spine and 6 cm further back: the game's point and a hand flat on the back are
inside the zone, and its centre is 0.29 m from the pistol's right hip (`hipRight`, 0.20, -0.62, -0.05, r 0.14). That is more
than both radii (the zones never overlap, from a 1.50 m to a 1.95 m eye height: tested) but less than radii plus both exit
margins (0.36 m, thighRight's rule): a lower back can't be 0.36 m from a right-hip pistol, so their hysteresis bands overlap
as the two shoulders' zones do, and the machine takes the nearer by distance over radius. The other slots are unchanged.
Seated, a chair's back covers the small of the back, so the seated place is beside it, behind the right hip: 0.42 m from the
raised seated hip and 0.45 m from the seated right shoulder (all seated zones 0.20; none overlaps another or the grenade's).
Tested (`tests/BodySlotsSmgTests.cpp`): a right hand swung back from hanging at the side, round under the pistol's hip zone to
the small of the back, enters no other slot and ends in `lowerBack`, where a squeeze draws the SMG; the hand hanging, the low
hip-fire hand, the belly rest and the shouldered aim point are clear of it.

**A controller behind the back** drops out of an inside-out headset's view (the owner's Quest; the PS VR2's cameras): OpenXR
then reports its pose not valid for a moment or keeps a frozen or IMU-predicted one. Before, a pose that wasn't valid turned
the holsters off for that tick (`the right controller isn't tracked`), which reset the slots' machine and lost a squeeze made
then. Now (`ik::body_slots::LostHand`, `KeepsLostHand`): while the tracked hand is in or near the `lowerBack` zone its place on
the body (right, up, forward from the anchor) is remembered; when the pose stops being valid, that place counts as the hand for
`[Holsters] lostHandHoldS` (0.75 s; 0 never), so the contact buzz goes on and a squeeze there draws, and the place moves with
the body frame, not the room. Only that slot: a lost hand anywhere else (and every lost hand on ME1) turns the slots off as
before. A pose the runtime still reports valid while it is frozen or drifting is taken as it is (the controllers' state has
no "tracked" flag; adding one changes the host's services). Logs `holsters: the right controller lost tracking at lowerBack:
its last place on the body is kept for up to 0.75 s`, `... is tracked again after N s`, `the grab at lowerBack came while the
controller was untracked`, and the heartbeat's `lost behind the back N times`; the snapshot's holsters block has `smgSlot` and
`lostHand` (the sequels only: ME1's lines, snapshot and ini are as before).

**The risks** (notes/headset-checklist.md): with a pistol carried, the hand on its way round the right hip passes the pistol's
zone (a buzz, no draw without a squeeze); behind the back the controller may be lost longer than 0.75 s or come back far from
where it was lost; seated, the place beside the chair's back may be blocked by an armrest. Not seen in the game: no save here
carries an SMG.

## The sequels' controls after the review of 57ae5b4e (lane FX2, 2026-11-17)

An adversarial review of main 57ae5b4e named five defects in the sequels' controls. Each was confirmed in the code and, where
a run could show it, on ME2 with main's mod before the fix (hot-reloaded) and with the fix after. Stretch
`%LOCALAPPDATA%\MEVR\sessions\20261117-082114-057-LE2-fx2-controls` (Save 4, Freedom's Progress; then the Normandy, Save 36
of the John career, read only; restored with `verification: 0 differences`): folders `fx2-before-082552`, `fx2-after-082927`,
`fx2-normandy-083702`, each with `steps.txt`, `console.txt`, `lines.txt` and the recordings; the lane's runner was a scratch
script over `tools/make-walk-recording.py --plan ...,r3:1.5,...,r3:0.12,...`, `make-holster-recording.py --slot hip` and
`holsters.ps1`.

1. **An R3 tap cast a power on ME2.** With holsters on (4a366d9a) the right grip holds the gun and RB comes from the right stick
   click (`gamepad::RbFromStickClick`). ME2's and ME3's RB is `Console_PowerWheelAndQuickPower2` = `OnTap 0.3 TryUsePower 1 |
   OnHold 0.3 EnterPowerWheel` (ME2's `BIOInput.ini`; ME3's `Coalesced.bin`, `sfxgame.sfxgamemodedefault`, read with Legendary
   Explorer's `CoalescedConverter`). **Before:** a tap logged `call trace: event SFXGame.SFXGameModeDefault.TryUsePower`.
   **The fix is ME1's rule** (research/19 "Hold only") in the shared module: `holsters::TickPowerHold` runs on every game, the
   click is never RB (`kFlagModPowerHold`), and the mod opens the wheel after 0.3 s alone and closes it at the release. The
   adapter: ME2 `SFXGameModeDefault.EnterPowerWheel` (`GameModeManager2.EnableMode(1)`), ME3 `SFXGameModeBase.EnterPowerWheel`
   (`EnableMode(3)`, inherited by the default mode), both `SFXGameModePowerWheel.ExitPowerWheel`; the modes by the adapter's
   `engine::adapter::GameMode` (ME1 0/2, ME2 0/1, ME3 0/3). ME3's open sets a 0.2 s timer and tries again during an animated
   transition, so a wheel the game opens after the release is closed too (`gamepad::WatchPowerWheel`, kPowerLateMs 1 s,
   unit-tested). The right grip holds the gun on foot and in the two wheels on every game now (`IsFootOrWheelMode`: ME1's 0,
   2, 3; on the sequels it was on foot only). **After:** the hold `SFXGameModeDefault.EnterPowerWheel opened the game's power
   wheel`, game mode 1, the release `ExitPowerWheel`; the tap: no `TryUsePower`.
   **The raw R3 still reaches the game** (it stays R3 for Halcyon's double-click recenter, as on ME1): ME2's R3 is
   `Console_ShowMap` (`OnTap 0.3 ShowAreaMap | OnHold 0.3 TurnObjectiveRadarDisplayOn | OnRelease
   TurnObjectiveRadarDisplayOff`); the trace shows the tap's `SFXGameModeDefault.ShowAreaMap` (no area map at Freedom's
   Progress: the game stayed in mode 0) and `TurnObjectiveRadarDisplayOff` at each release. ME3's R3 is
   `Console_NavAssistanceOrCoverTurn` (`Exclusive TryCoverTurn | Exclusive ActivatePOI | OnRelease DeactivatePOI | ShowAreaMap`,
   every press). Left with the owner (docs/trilogy-plan.md question 14).
2. **A holster grab drew where the game forbids combat.** The sequels' draw (`SwitchWeapon`, ME2's `SetActionState(1)`) skipped
   the test every game draw makes first (`BioPlayerInput.IsCombatEnabled`; ME2's own FireWeapon, NextWeapon, EnterWeaponWheel,
   EnterPowerWheel, TryUsePower; ME1's `SwitchToWeapon` itself). ME2 sets it per area (`BioSeqAct_NonCombatArea`:
   `BioPlayerSquad.m_bCombatEnabled`). **Before**, with `set BioPlayerSquad m_bCombatEnabled False`: `grab at hipRight: drawing
   the pistol ... in hand after 0.07 s`. **After:** `the pistol stays in its holster: the game allows no combat here
   (BioPlayerInput.IsCombatEnabled false ...)`, the refusal buzz (`body_slots::Refused`), and the machine forgets the draw
   (`Machine::ForgetDraw`, unit-tested: the release sends nothing back). On the Normandy (Save 36, combat off, the pistol
   carried) the same for a hip grab and for `mevr weapon pistol`, and the R3 hold: `EnterPowerWheel didn't open it`.
3. **The holster's click while the gun stayed out.** ME2's `TryHolster` refuses silently mid-reload (`IsReloading(TRUE)`), in
   combat (`InCombat`: the AI's enemies, or `SFXGame.bInCombat`/`bForceCombat`), while a power runs and in an animated
   transition; `ActionState` turns explore only when the holster animation ends (the run: `ME2's action state right after
   TryHolster: 1`, holstered 1.16 s later), so it can't be read after the call. The four tests are asked first, in the game's
   order, and a refused holster is felt as ME1's refusal. **Before**, with `set SFXGame bForceCombat True`: `let go at hipRight:
   the pistol goes back in its holster` (the click) and 3 s later `the game didn't holster the pistol`. **After:** `the pistol
   stays out: the game won't holster it now (in combat: SFXGameModeDefault.TryHolster refuses then)`; with combat ended the
   same let-go holstered it in 0.94 s.
4. **ME3's heavy weapon, confirmed in the code, not changed.** A heavy weapon has no slot (`le2_squad::WeaponType` -1), so
   `Held()` is none and a right-grip squeeze inside a slot draws; ME3's `BioPlayerController.SwitchWeapon` calls
   `SFXHeavyWeapon.PutDownWeapon`, which in single player throws the weapon away when `bTossOnWeaponSwitch` (or out of ammo,
   or the config's `DropHeavyWeaponOnHolster`, false). ME1 has no heavy weapon; the options are the owner's (docs/trilogy-
   plan.md question 13). ME2's heavy weapon stays in the inventory on a switch.
5. **`Weapon::ZoomedAimError`** set `bIsZoomed` with a journal line but put it back outside any `__finally`, after a
   ProcessEvent into the script: a fault caught further up would leave the weapon zoomed where the camera sees it
   (research/18). Now the vault's pattern: journalled once, set and put back in a `__finally` (`WithFlagSet`, POD only).

**ME1 at the same pose** (`tools/fake-headset-session.ps1`; the standing replay paused, two snapshots, then the pistol drawn,
the right stick click tapped and held under a call trace, the pistol held and let go at the hip; before = this landing's build
with main's `Holsters.cpp` and `Weapon.cpp`, after = this landing; sessions `2026-11-17_08-45-13-fx2-le1-before`,
`2026-11-17_08-40-22-fx2-le1-after`): `hostView` identical (-43.3 / -4.1 / -18.1 uu), the same six layers at the same places
(the omni-tool target 2.7 mm apart: the drawn arm), the HUD panel, label, wrist piece and target and both eyes' world layers
identical pixel for pixel, the composed eyes 0.91% / 0.58% apart, all of it at the omni-tool target. Every `holsters:` and
call-trace line (numbers masked) is in both logs and no other: the hold opened and closed the wheel
(`SFXGameModeDefault.EnterPowerWheel` / `ExitPowerWheel`), the tap called nothing.

## The right stick click, the heavy weapon and ME1's refused draw (lane CTL, 2026-11-17)

The owner took the recommendations for FX2's three questions ("All recommendations", 2026-11-17; docs/trilogy-plan.md
questions 13-15). What each game does now:

| | ME1 | ME2 | ME3 |
|---|---|---|---|
| the right stick click on foot (and in the wheels), its hold the power wheel's | still R3 to the game: `ChangeMagnification` (a zoomed scope's step), and Halcyon LE1's double-click recenter reads R3 from the game's pad | kept from the game: no `ShowAreaMap` (tap), no objective radar (hold) | kept from the game: no point of interest, no `ShowAreaMap` |
| the area map | (ME1's own map screen: the omni-tool's Map entry) | the omni-tool's Map entry (the Mission Computer's `AreaMap` page) | the omni-tool's Map entry |
| a grab at a slot with a heavy weapon in hand | no heavy weapons | draws (ME2's `SwitchWeapon` keeps the heavy weapon) | refused, the refusal buzz |
| a grab where the game allows no combat | refused, the refusal buzz (was the draw's click) | refused, the refusal buzz | refused, the refusal buzz |

1. **R3 (question 14).** The click reaches the game through the host's pad merge (`plugin/PadMerge`, `game/GamepadMapping`'s
   `FromControllers`), not the mod; the mod only says it owns the click (`kFlagModPowerHold`, set by `game/Holsters` on foot and in
   the two wheels while the hold is the power wheel's). New host-only option `gamepad::Options::keepPowerClick`
   (`pad_merge::Renderer::keepPowerClick`), set by `plugin/HalcyonSequelBridge` (both sequels): a press that begins while the mod
   owns the click is kept from the game until it is let go (`Latches::rightClickKept`, decided at the press like the face
   buttons' RB), and it never reaches the R3 gate (`gateRightClick`), so no tap pulse follows its release. A press that began
   before (a menu, a conversation) stays the game's until let go. The mod's own hold reads the live controllers, so the power
   wheel is untouched. The host logs `halcyon bridge: first right stick click kept from the game ...` once. Why ME1 keeps
   its R3: Halcyon LE1's `DetectRecenterCombo` (`vr_menu.cpp`) reads R3 after the merge, and ME1's R3 is harmless (below);
   the sequels' Halcyon recenter is a double Back (`me2_menu.cpp` `CheckR3Recenter` reads `XINPUT_GAMEPAD_BACK` in both
   trees), so nothing of theirs needs R3. Tests: `TestPowerClickKept` in `tests/PowerWheelTests.cpp` (the mapping: hold, tap,
   ungated, a press keeping its meaning, LE1) and `tests/PadMergeTests.cpp` (the merge with the sequels' and LE1's renderer).
2. **ME3's heavy weapon (question 13).** `core/ik/BodySlots`' `Input::switchLosesHeld`: with it, a new squeeze at a slot whose
   gun would be drawn is `Action::SwitchRefused` (nothing on its way, the release sends nothing); a slot whose gun isn't carried
   stays `Unavailable`. `game/Holsters` sets it on ME3 while `Pawn.Weapon` is an `SFXGame.SFXHeavyWeapon` (ReflectCheck row) and
   logs the change (`holsters: a heavy weapon is in hand (...): a grab at a slot is refused while it is ...`); `mevr weapon
   <family>` refuses too. The refusal is ME1's refusal buzz and `holsters: grab at <slot>: the <gun> stays in its holster: a
   heavy weapon is in hand and a switch would throw it away (SFXHeavyWeapon.PutDownWeapon); the game's weapon wheel switches
   away`. ME2: its `SFXHeavyWeapon` has no `PutDownWeapon`, `DropGun` or `bTossOnWeaponSwitch` (LExSDKv2's LE2 header) and its
   `BioPlayerController.SwitchWeapon` only calls `SetWeaponIfAvailable` (FX2's read of its script), so a switch keeps the heavy
   weapon: no loss, no rule. Unit-tested in `tests/BodySlotsSmgTests.cpp` (four slots, with and without the gun-holding grip, ME2's
   draw). Not seen in the game: no save here holds a heavy weapon (research/le3-combat-fixtures.md, the weapon checks' item 4).
3. **ME1's refused draw (question 15).** The draw's `IsCombatEnabled` test (FX2's, sequels only) moved to the shared top of
   `Draw()`; ME1 reads it through `Engine.PlayerController.PlayerInput` and `SFXGame.BioPlayerInput.IsCombatEnabled` (ME1's
   ReflectCheck rows existed). ME1's `SFXGameModeDefault.SwitchToWeapon` begins with the same test and returns, so the only change
   is what the player feels and the log: the refusal buzz instead of the draw's click, `the pistol stays in its holster: the game
   allows no combat here` instead of `drawing the pistol` and, 3 s later, `the game didn't draw the pistol`; the machine forgets
   the draw, so the release sends nothing back.

**ME1 at the same pose** (`tools/fake-headset-session.ps1`; before = main 79e97c4a's players' build, after = this landing's;
sessions `2026-11-17_11-49-58-ctl-le1-before`, `2026-11-17_11-54-31-ctl-le1-after`; the standing replay paused, two snapshots,
then the pistol drawn, the click tapped and held under `mevr calltrace 15 tryusemappedpower enterpowerwheel exitpowerwheel
changemagnification`, the pistol at the hip, then `set BioPlayerSquad m_bCombatEnabled False`, the hip grab and `mevr weapon
pistol`, combat back on and the hip grab again): `hostView` identical (-43.3 / -4.1 / -18.1 uu), the same six layers at the same
places (the omni-tool target 1.2 mm apart: the drawn arm), both eyes' world layers, the HUD panel, label, wrist piece and target
identical pixel for pixel, the composed eyes 0.12% / 0.08% of pixels over 32/255 apart, at the target. The holsters' and call
trace's lines (numbers masked): 30 shared; R3 reached ME1 in both (`ChangeMagnification` at the tap and at the hold), the hold
opened and closed the power wheel in both; only the combat-off grab differs, as meant: before `drawing the pistol
(SFXGameModeDefault.SwitchToWeapon(0) ...)`, `the game didn't draw the pistol within 3 s`, the let-go waiting for a draw and
`the pistol stays out (it's already away, or never came out)`; after `grab at hipRight: the pistol stays in its holster: the
game allows no combat here ...` and the console's `grab at the console: ...` the same. (`mevr calltrace ... iscombatenabled`
fills its 600 lines in 2.5 s: the HUD asks it every frame; trace `switchtoweapon` alone.)

**ME2 and ME3 on the fake headset** (one stretch, holder CTL, 12:42-12:47; each restored with `verification: 0 differences`;
the runner `run-sequel.ps1` in the lane's scratch folder: prep, launch, load, then `make-walk-recording.py --plan
'stand:2,r3:1.5,stand:3,r3:0.12,stand:5' --hands view` replayed with `-Inputs` under a 15 s call trace, then `mevr omni map`
and `mevr omni close`):
- **ME2, Save 4** (Freedom's Progress, the pistol drawn): the trace (`tryusepower enterpowerwheel exitpowerwheel showareamap
  objectiveradar`) has two lines: `SFXGameModeDefault.EnterPowerWheel` at the hold (`opened the game's power wheel`, game mode
  1) and `SFXGameModePowerWheel.ExitPowerWheel` at its release; the tap 4 s later called nothing. FX2's run of the same replay
  on main 30cf473c (the code before this lane, `20261117-082114-057-LE2-fx2-controls\fx2-after-082927\lines.txt`) had
  `SFXGameModeDefault.ShowAreaMap` (`MassEffectGuiManager.ShowAreaMap`) at the tap and `TurnObjectiveRadarDisplayOff`
  (`ToggleObjectiveRadarDisplay`) at both releases. The host's `halcyon bridge: first right stick click kept from the game ...`
  came at the hold's press. `mevr omni map`: the Mission Computer, then its page 1 (`AreaMap`, type 0) through
  `ShowSelectedPanel` (`omni-tool: opened the map ...`), the area map on the HUD panel (Freedom's Progress shows Omega's
  layout: the game's own data), placed in the room (the replay's left controller had stopped); `mevr omni close` back to game
  mode 0.
- **ME3, Save 31** (`-RendererPreview`, the Normandy, `SFXPawn_PlayerVanguardNonCombat`): the trace (`tryusepower
  enterpowerwheel exitpowerwheel showareamap activatepoi deactivatepoi trycoverturn navassistance`) has one line,
  `SFXGameModeBase.EnterPowerWheel` at the hold (`didn't open it`: no combat on the Normandy); no `ShowAreaMap`, `ActivatePOI`,
  `DeactivatePOI` or `TryCoverTurn` at the hold, its release or the tap (ME3's R3 binding, FX2's read of Coalesced.bin, calls one
  of them at every press). `mevr omni map`: the Mission Computer's page 3 (`Areamap`, type 0), the deck map (Combat Information
  Center, its points of interest) on the HUD panel, game mode 9. **Found, not this lane's:** on ME3 `mevr omni close` from the
  `Areamap` backed out 8 times (`B through SFXGameModeGUI.GuiKey`) and left the Mission Computer (`MenuBrowser`) up in game mode
  9 (`omni-tool: close the game's screens not done: the game stayed in its GUI mode after 8 backs out`); ME2's closed at once.
  Fixed by lane OMX the same day (each B restarted the map's close animation; research/le3-native-gui.md "Close from the area
  map").

## A gun let go in a fight (lane CV2, 2026-11-18)

The owner, from the headset (ME2, Freedom's Progress, firefights): "Letting go of a weapon does not reliably remove it from
Shepard's possession; in firefights I'd let go and it would still be in my hand."

**What the run's log shows** (`LE2MEVR.log`, 20:34-20:51): of 16 let-gos away from a slot, 10 went back (`holsters: let go 0.20 s
ago away from ...: the X goes back in its holster (SFXGameModeDefault.TryHolster)`, then `the X is holstered after 0.11-0.12 s`) and
6 stayed out, every one in a fight: `holsters: let go 0.20 s ago away from hipLeft: the shotgun stays out: the game won't holster it
now (in combat: SFXGameModeDefault.TryHolster refuses then)` four times in four seconds (20:44:11-15), the same at the hip's own slot
at 20:44:16, and twice for the rifle (20:47:42-43). ME2's `TryHolster` returns in combat (`if (ChkPawn.InCombat()) return;`), and lane
FX2 (2026-11-17) had made that felt as a refusal. No let-go was called off by a twitch of the grip (no `the X stays out (squeezed
again)` in the run; the grip latches at 0.6 and lets go at 0.4): the 0.2 s grace held. Once the game drew a gun by itself:
`[20:50:32.254] holsters: the assault rifle came out without a holster grab ...: not through DrawPrimaryWeapon, so it wasn't
refused`, 0.4 s after the hand power wheel closed with nothing picked (`power wheel: closed without a choice`).

**ME2's own routes to the drawn state** (its script, `conversation-scan ... --script grep:SetActionState --only SFXGame.pcc`): besides
the five buttons the rule already refuses (FireWeapon, TightAim, TryReload, Next/PrevWeapon), the player's action state is made
combat by `SFXGameModePowerWheel.Deactivated`, `SFXGameModeWeaponWheel.Deactivated` and `SFXGameModeCommand.Deactivated` (`if
(BioPawn(Outer.Pawn).ActionState == ActionState_Explore) m_oBehavior.SetActionState(1)`: the draw at 20:50:32; the hand power wheel
and the squad's weapon wheel open these modes), `BioPlayerController.ApplyTacticalOrders` (the same test on the behaviour's own state
when a wheel's order is applied), `SFXGame.OnCombatStart` (every fight's start), `SFXPawn_Player.SetCombatState_Active` (the 0.25 s
timer of `SetCombatState(true)`), `SwitchToBackupWeapon`, and Kismet (`BioSeqAct_SetActionState`, `BioSeqAct_EnableCombat`,
`BioSeqAct_ToggleCombat`). `BioPawnBehavior.SetActionState` is a native with an opcode (script calls to it never reach the
CallFunction hook) and takes parameters, so none of these can be refused before it runs.

**Built** (game/Holsters, ME2's adapter; ME1's and ME3's code unchanged):
- **The let-go in a fight:** where `TryHolster` would refuse only for combat, its own last step is called:
  `BioPawnBehavior.SetActionState(0)` (explore). Mid-reload, while a power runs or in an animated transition (TryHolster's other
  three tests) the let-go waits and is tried again each tick, at most 4 s, called off when the grip closes on the gun again, the gun
  changes or is already away; never under the trigger. Logs `holsters: <why>: the X goes back in its holster (in combat, where
  SFXGameModeDefault.TryHolster refuses: its own last step, BioPawnBehavior.SetActionState(explore))`, `holsters: <why>: the X waits
  (mid-reload: ...)`, `holsters: the X stays out (the grip closed on it again; ...)`. `[Holsters] combatHolster = 0` keeps the old
  refusal (read from the file, ME2 only).
- **A draw nobody asked for is put back** (only holsters draw, `[Holsters] onlyHolstersDraw = 1`, the owner's rule of 2026-11-16):
  when the weapon comes out on foot in VR with no grab of ours and the right grip open (the existing `came out without a holster
  grab` test), it goes back at once by the same holster, unless a power runs (`BioPawn.IsUsingPower`) or a wheel's order waits for
  combat mode (`BioPlayerController.m_currentOrder.nmPower`: ApplyTacticalOrders draws, then casts): the draws powers need. A game that
  draws a third time within 10 s is left alone for 20 s (no flicker). Logs `holsters: the X came out without a holster grab ...: a
  route no hook refuses (a wheel's close, a fight's start, a script)`, then the holster's line (`only holsters draw: put back at
  once`), or `came out for a power (...): let through`; the heartbeat `holsters: this period N let-gos put away in a fight ...`.
- `mevr weapon away` on ME2 too (the held gun put away as a let-go does), for the vault's holstered check.
ReflectCheck rows: `BioPlayerController.m_currentOrder` and its `nmPower` (LE2). **[verify] in the game:** `SetActionState(0)` in
combat (ME2's own code never makes that call there: the holster animation, and whether the AI's combat redraws it), the put-back
after the hand power wheel and after a fight's start, the let-go mid-reload; then the headset (notes/headset-checklist.md, ME2).

## The reload by hand: the game's side (lane WP, 2026-11-16)

**Off by default since 2026-11-16** (the owner: "putting my left hand to the hip makes no sense ... So don't enable that
flow"): `[Reload] enabled = 0` in a new file and `hand_calibration::Reload::enabled`'s code default; an older file's `enabled = 1`
(the first default) moves to 0 once (marker `enabledMoved`). The code stays for the gesture's redesign with the owner;
`reload.ps1` warns while it's off. The game's own reload button and its automatic reload work as always.

`game/Reload` (MIT; the sequels only: on ME1 `TryStartup` is false and nothing runs) over `core/vr/ReloadGesture` and
`vr::reload::Coordinator`, the five steps above:
1. **The pouch** is a body slot for the LEFT hand on the holsters' body frame and scale (`holsters::SlotFrame`: the smoothed
   neutral eye, the body yaw, the standing eye height's scale; fixed metres seated): `[Reload] pouch = -0.20, -0.60, 0.02,
   0.13` (the left hip; the shotgun's cross-draw is the RIGHT hand's, so the two never compete), `seatedPouch = -0.28, -0.42,
   0.18, 0.20`. 0.28 m from the grenade slot (either hand, at the left chest). The left hand in the gun's frame is
   `engine::bone_pose::LatestSupport()` as `game/Vent` reads it; **the receiver is the vent's own zone** (`[Vent] zone*`: one
   receiver for the vent and the reload, ME1's keys and defaults). A squeeze the grenades, the vent, the support grip, the
   voice radio or the power wheel took is not the reload's (`squeezeClaimed`); the reload's held clip is in `ActionHandFree`.
2. **The coordinator's frame**: identities from the world (the pawn's `WorldInfo` and the pawn), the held weapon object and the
   on-foot context (a new generation each time the module becomes active); the grant is the left hand while no support grip
   holds it (a new lease each time). `eligible` = the weapon's own `CanReload()` (its class's function) while the game isn't
   reloading; `supportedFiniteReserve` = not `bInfiniteAmmo` and `bWeaponCanBeReloaded`.
3. **The request** is the button's own route, `SFXGameModeDefault.TryReload` on the player controller's default game mode
   (`holsters::DefaultGameMode`), through `ue3::runtime::Call` on the game thread: it calls `Weapon.TryReload()` through the
   script VM, so the weapon's state decides (ME2's `StartReload`/`Reloading` states ignore a second one; calling
   `SFXWeapon.TryReload` by ProcessEvent would have bypassed them). Never `DoReload`, never an R press; the game's R and its
   automatic reload stay. **Feedback**: accepted when `BioPlayerController.IsReloading()` turns true (or the weapon is in
   `Reloading`/`StartReload`), finished when it ends with the magazine's count up (or the used or spare count down), aborted
   when it ends with the counts unchanged, rejected when nothing began within 0.6 s, aborted when the gun changes; each one
   reconciled for the coordinator.
4. ReflectCheck rows (LE2: `SFXWeapon.CanReload`, `GetAmmoCountInMagazine`, `BioPlayerController.IsReloading` with their
   parameters, `bInfiniteAmmo`, `bWeaponCanBeReloaded`, `SFXGameModeDefault.TryReload`'s parameter block; LE3 had them);
   `[Reload]` appended to `ASI\LE2MEVR.ini` / `LE3MEVR.ini` (the ME3 preview runner's allowed sections list it); logs
   `reload:`; snapshot `"mod"."reload"`; `mevr reload [status | try]` (`try`: the same request outside the coordinator);
   `tools/make-reload-recording.py` and `tools/le2-halcyon-run/reload.ps1 -Holder <you> [-Game LE3]`.
5. **In the game** (ME2, Save 4, the fake headset; stretch `20261116-224515-586-LE2-wp-weapons`, restored with `verification: 0
   differences`; `reload.ps1`): startup `reload: the reload by hand is ready (SFXGameModeDefault.TryReload true, SFXWeapon.CanReload
   true, GetAmmoCountInMagazine true, AmmoUsedCount and CurrentSpareAmmo true, BioPlayerController.IsReloading true, ...)`, `reflect:
   733 of 733 checks agree`. The pistol drawn: `magazine 12, used 0, spare 60`; one replayed trigger pull: `magazine 11, used 1,
   spare 60`; the replayed gesture: `the left hand is at the clip pouch (0.13 m from its centre)`, `the left hand took a clip from
   the pouch`, `the clip went into the receiver (0.04 m along the gun, 0.05 above, -0.08 beside)`, `SFXGameModeDefault.TryReload,
   the game's own reload (request 1; ...)`, `the game reloads (0.01 s after the request ...)`, `reloaded after 1.50 s: magazine 12,
   used 0, spare 59 (was magazine 11, used 1, spare 60)`: **12 / 0 / 60 -> 11 / 1 / 60 -> 12 / 0 / 59**, no R pressed. The same
   gesture with the magazine full: `a squeeze at the pouch: no clip (nothing to reload now ...; magazine 12, used 0, spare 59)`, a
   refusal buzz, the ammo unchanged. ME3 (Save 31, the Normandy's non-combat pawn): the names resolve (`reflect: 710 of 710 checks
   agree`), `mevr reload status`: `no gun`; its reload waits for a combat fixture.
6. **Found on the way:** a replay whose right grip is already closed on its first frame keeps that squeeze as RB (the pad
   mapping decides a squeeze's meaning when it begins, before the mod had published that the grip holds the gun): that
   squeeze was RB, ME2's power-wheel button, and the trigger fired nothing. `make-reload-recording.py --grip-held` closes the grip
   at 0.5 s instead, and then the shot fires with the grip held (`magazine 11, used 1, spare 59`, stretch
   `20261116-230334-332-LE2-wp-weapons`, which also repeated the reload: `reloaded after 1.50 s`). Whether a
   player's squeeze held through a load or a menu does the same until it is let go once is [verify].

**Next (not built): the replay STAGE-eye gap.** Replays don't drive the floor lock's eye height: the host's floor sample
(`plugin/XrHost` `MeasureFloor`) reads the runtime's real head over STAGE while the replay substitutes the recorded head, so a
recorded crouch or a taller recorded head never reaches the body scale or the holsters' slots. The minimal fix (lane C4's
idea): while a replay runs, add the replayed head's height change (its LOCAL head Y minus the runtime's) to the sample in
`MeasureFloor`; the full one is research/58's per-frame floor payload in the recording format. Shared host code: it needs an
LE1 same-pose pair before and after.

## What M3 still needs from the host (lane B2)

The mod's side of hand aim, arm IK, the torso, the fingers, the gun in the hand and the gun kick runs on ME2. What
waits for the LE2 host, by the mod's own warnings in that log: the reticle and the sights' drawing (`sights: the host
(services v39) can't draw gun sights`: `setSight`), the two-handed grip (`support grip: the host (services v39) can't take
it`: `setSupportGrip` / `supportGripHeld`, the pad mapping's grips), the holsters (services v9/v10: `rightGripFree`,
`gripHolds`), the squad and the throwables' grips (v11), body scale's floor lock (`setFloorLockSample`, v36), the
calibration (v15), the HUD pieces on the wrist and the gun (v22). The mod-side sights (`game/Sights`: the sight bones,
the scope pull, the convergence trace) are still ME1's only and are the next port once `setSight` exists.
