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

## What M3 still needs from the host (lane B2)

The mod's side of hand aim, arm IK, the torso, the fingers, the gun in the hand and the gun kick runs on ME2. What
waits for the LE2 host, by the mod's own warnings in that log: the reticle and the sights' drawing (`sights: the host
(services v39) can't draw gun sights`: `setSight`), the two-handed grip (`support grip: the host (services v39) can't take
it`: `setSupportGrip` / `supportGripHeld`, the pad mapping's grips), the holsters (services v9/v10: `rightGripFree`,
`gripHolds`), the squad and the throwables' grips (v11), body scale's floor lock (`setFloorLockSample`, v36), the
calibration (v15), the HUD pieces on the wrist and the gun (v22). The mod-side sights (`game/Sights`: the sight bones,
the scope pull, the convergence trace) are still ME1's only and are the next port once `setSight` exists.
