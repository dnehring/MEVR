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

## What M3 still needs from the host (lane B2)

The mod's side of hand aim, arm IK, the torso, the fingers, the gun in the hand and the gun kick runs on ME2. What
waits for the LE2 host, by the mod's own warnings in that log: the reticle and the sights' drawing (`sights: the host
(services v39) can't draw gun sights`: `setSight`), the two-handed grip (`support grip: the host (services v39) can't take
it`: `setSupportGrip` / `supportGripHeld`, the pad mapping's grips), the holsters (services v9/v10: `rightGripFree`,
`gripHolds`), the squad and the throwables' grips (v11), body scale's floor lock (`setFloorLockSample`, v36), the
calibration (v15), the HUD pieces on the wrist and the gun (v22). The mod-side sights (`game/Sights`: the sight bones,
the scope pull, the convergence trace) are still ME1's only and are the next port once `setSight` exists.
