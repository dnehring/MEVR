# 44: The Mako in VR: the turret aimed by the hand, the vignette, the reticle (2026-10-31)

Goal (the lead, 2026-10-31): in the Mako (game mode 1) the cannon and machine gun fire where the right controller
points and the turret model turns to it; the comfort vignette also while the Mako moves or turns; the hand reticle on
the controller's ray as on foot. Head aim stays Halcyon's business and stays off (`makoHeadAim = 0` in the deploy): the
head drives the view only.

Built without the game running (the worktree rule): everything below that says **[verify]** needs a headset session
in the Mako. Section 6 lists what to look at.

## 1. The vehicle classes (LExSDKv2's LE1 headers; `SFXGame_classes.hpp`, `_f_structs.hpp`, `_structs.hpp`)

The classes are read from the generated headers for their names, fields and function flags only (no LExSDKv2 code
runs in LE1; our reflection layer resolves everything by name at run time, `engine/ReflectCheck` cross-checks it in
development builds).

- `Engine.Vehicle` (an `Engine.Pawn`: `Driver`, `Steering`, `Throttle`, `bDriving`, `bSeparateTurretFocus`) →
  `Engine.SVehicle` (the PhysX car: `Wheels`, `SimObj`, `MaxSpeed`...) → **`SFXGame.BioVehicleBase`**
  (`m_oBehavior`, a `BioVehicleBehaviorBase`; `m_sVehicleName`; script `Tick`, `DriverEnter`/`DriverLeave`,
  `SetInputs`) → **`SFXGame.BioVehicleWheeled`**: the Mako (boost and jump fuel, thruster force; a native
  `GetWeaponStartTraceLocation(Weapon)` of its own, `FUNC_Simulated | FUNC_Native`, and a script `Tick`).
  While driving, the player controller possesses the vehicle: `Controller.Pawn` is the Mako, Shepard is its `Driver`.
  `SFXGameModeManager.CurrentMode` is 1 (`SFXGame.SFXGameModeVehicle`: `VehicleCam`, `VehicleTightAimCam`,
  `VehicleTightAimExCam`; exec functions `FireCannon`, `ToggleVehicleAim`, `VehicleTightAim`, `ChangeMagnification`,
  `TryExitVehicle`, `TryUprightVehicle`).
- **`SFXGame.BioVehicleBehaviorBase`** (a `BioEpicPawnBehavior` component of the vehicle, `m_oBehavior`):
  `m_aVehicleWeaponInfo` (an array of `BioVehicleWeaponInfo`: `oVehicleWeaponPtr` (a `BioWeapon`), `nTurretIndex`,
  `fTurretYawHalfAngleInDeg`, `fTurretPitchHalfAngleInDeg`, `bClampToTurret`), `m_nCurrentZoomTurret`,
  `m_bWeaponsEnabled`, and the turret natives: **`SetDesiredTurretAimPoint(int nTurretIndex, Vector vTargetLocation)
  → Rotator`**, `IsWorldLocationWithinTurretCone`, `IsWeaponAimWithinTurretCone(BioWeaponRanged)`,
  `UpdateCurrentZoomTurret`, `EnterZoomMode`/`ExitZoomMode`, `GetWeaponAtIndex`, `GetWeaponIndex`,
  `CreateAndAttachWeapon(class, item, turretIndex, info)`. All `FUNC_Native`: the game calls them from C++, and no
  ProcessEvent or CallFunction sees those calls. **`SFXGame.BioVehicleBehaviorWheeled`** adds `m_nVehicleCannon`,
  `m_nVehicleMachineGun` (weapon indices) and `OnWeaponInstantFire(BioWeaponRanged)` (native).
- The vehicle's weapons are **`SFXGame.BioWeaponRanged`** actors (the class has `m_fVehicleFireDistance`), created
  and attached to the turret sockets by `CreateAndAttachWeapon`. Their fire path is the on-foot one: the native
  `TickFire` asks the pawn (here the vehicle) `GetWeaponStartTraceLocation` through ProcessEvent, then the weapon's
  script `GetAdjustedAim(StartFireLoc)`, which asks the pawn, which asks its controller:
  **`BioPlayerController.GetAdjustedAimFor(Weapon, StartFireLoc)`** (script, `0x00020002`), the function
  `game/hand_aim` already rewrites on foot. `BioPlayerController` has nothing turret-specific of its own
  (`bUseCameraRelativeVehicleSteering`, `BioEnterVehicle`/`BioExitVehicle`, `Possess(pawn, bVehicleTransition)`).
- The turret's appearance: **`SFXGame.BioAppearanceVehicle.m_aTurrets`** (`BioVehicleTurretInfo`: `nmAttachSocket`,
  `nmTurretYawBone`, `nmTurretPitchBone`, `nmYawController`, `nmPitchController`), and at run time
  `SFXGame.BioInterfaceAppearanceVehicle.m_aTurretRuntimeInfo` (`m_fCurrentTurretRelativeYaw`,
  `m_fCurrentTurretRelativePitch`, `m_bTurretAutoTrackYaw`, `m_bTurretAutoTrackPitch`; the behavior's
  `m_oAppearanceType`). The named controllers are **`SFXGame.BioSkelControl_TurretConstrained`** (a
  `SkelControlSingleBone`) in the vehicle mesh's AnimTree: `DesiredBoneRotation` (a rotator), `MaxAngle`/`MinAngle`
  (`TurretConstraintData`: pitch, yaw, roll constraints), `LagDegreesPerSecond`, `m_fLagScale`, `bConstrainPitch`,
  `bConstrainYaw`, `bConstrainRoll`, `bInvertPitch`/`Yaw`/`Roll`. One control per axis (the yaw bone and the pitch
  bone), each lagging toward its `DesiredBoneRotation` at `LagDegreesPerSecond`.
- The camera modes: `SFXGame.SFXCameraMode_Vehicle` (the chase camera: `Blur`, `Sharpness`),
  `SFXCameraMode_VehicleTightAim` and `..Ex` (the zoomed turret views; `ChangeMagnification`).

## 2. How the game aims the turret, and what Halcyon found (`src/halcyon/le1/head_aim.cpp`, `xr_session.cpp`)

- Halcyon's 2026-07-08 notes (in its code; read, not run): in the Mako "its aim follows ControlRotation exactly like
  on-foot aim": the game builds the turret's aim point from the player controller's rotation (the chase camera's
  direction) and feeds it to `SetDesiredTurretAimPoint`; the cannon bone is driven "from a WORLD-SPACE aim point" by
  that native, and the shot goes along the same aim. Halcyon's experimental `makoHeadAim` writes the head's yaw and
  pitch into ControlRotation (the camera turns with the head too); its earlier attempts wrote the controls'
  `BoneRotation` (the bone followed, the shot didn't: the fire trace reads the game's aim, not the bone) and the
  runtime info's relative yaw/pitch (the values stuck, the bone stayed frozen; clearing the auto-track bits "froze
  the turret system"). Its step 3 called `SetDesiredTurretAimPoint` from the Present thread (a script call off the
  game thread: the 2026-10-23 crash class, research/36) with a far point along the head ray, and noted that a call
  landing after the game's own call wins the frame.
- In our deploy `makoHeadAim = 0` and Halcyon parks head look in the Mako (`SetHeadLook(0, 0, false)`): the view
  is the chase camera's, published as a world view (`OnViewBuilt` with `worldView` true and `headLook` false), so
  `game/view_anchor` maps tracking space into the world as for a head-locked view: the camera turned back by the
  head. **[verify]** that the Mako's picture is indeed head-locked (the view anchor's assumption; on foot with head
  look on it is world-locked). The controller's ray goes through this frame into the world, so the aim is where the
  player sees the controller point in the picture either way.
- Where in the frame the game's own call happens is **not known** without Ghidra (the lead's rule for this job:
  no Ghidra). Candidates: the behavior component's native tick (components tick after their actor's `Tick` event),
  the player controller's native tick, or the camera mode. The mod's tick runs **after** `UGameEngine::Tick` (the
  host's `GameEngineTickHook`, `plugin/Entry.cpp`), so a value written there is in place for the next frame's world
  tick, where the game's own call overwrites it before the pose if the game calls every frame. Hence three seams
  (section 3), with a measurement of which one the game lets stand.

## 3. What was built (`game/Mako`, mod side, hot-reloaded; `[Mako]` in `ASI\LE1MEVR.ini`)

Every engine read goes through `src/ue3/` handles; `engine/ReflectCheck` has a row for each class, field, element,
function and parameter (`SFXGame.BioVehicleBase`, `BioVehicleWheeled`, `BioVehicleBehaviorBase`,
`BioSkelControl_TurretConstrained`; `m_oBehavior`, `m_aVehicleWeaponInfo` (+ `oVehicleWeaponPtr`, `nTurretIndex`),
`DesiredBoneRotation`, `bConstrainYaw`, `bConstrainPitch`; `SetDesiredTurretAimPoint` (+ its three parameters and
ParmsSize), `BioVehicleWheeled.Tick`, `BioVehicleWheeled.GetWeaponStartTraceLocation` (+ ReturnValue)). A lookup that
fails stands that part down with a `mako:` line.

**The aim (each mod tick, `handAim = 1`).** In the Mako (`CurrentMode` 1 and the controller's pawn a
`BioVehicleBase`), with a head-tracked world view, our menu closed and the right controller tracked: the controller's
aim pose, or with `calibratedLine = 1` and a saved pistol calibration its calibrated line (research/25, as on foot), is
mapped into the world through the view anchor and traced (`Actor.Trace` on the vehicle, which it ignores, through
ProcessEvent, `bTraceActors`; a hit point that doesn't come back is found by bisection as `game/sights` does) up to
`aimRangeM` (200 m). The aim point is what it hits, or the ray's end.

**Shots.** `game/hand_aim`'s `GetAdjustedAimFor` handler (both the CallFunction and the ProcessEvent paths) asks
`mako::ShotRotation` when the on-foot barrel path doesn't apply: the direction from the shot's start toward the aim
point (converged on what the controller points at), where the start is the vehicle's own answer to
`GetWeaponStartTraceLocation`, seen through a post-handler within the last 200 ms (the native `TickFire` asks it right
before the aim query); without one, along the controller's ray from the controller. The start is never rewritten (the
game's gun socket is right). The game's rotation is unwound next to its own, roll 0. Logs `mako: first shot aimed by the
right controller: ...`, then `mako: shot N: from the gun's start (...) toward <actor> D m out; X degrees off the
controller's ray` (the first three, then one every two seconds).

**The turret model (`turret = 1`, `turretSeam` bits).** The game's own `SetDesiredTurretAimPoint(index, point)` through
ProcessEvent on the behavior, for every turret index the weapons' info names (deduplicated; turret 0 when the info
is empty), at up to three seams:
- bit 1: the mod's tick (after the world tick);
- bit 2: a post-handler of the vehicle's own script `Tick` (`SFXGame.BioVehicleWheeled.Tick`), inside the world tick,
  after whatever the vehicle's native tick did and before its components tick;
- bit 4 (the pose seam, `engine/bone_pose`'s `TurretPre`): the values the native wrote into the turret's two
  `BioSkelControl_TurretConstrained` controls (`DesiredBoneRotation`, read back right after our call) are written
  into them again right before `UpdateSkelPose` composes the vehicle mesh's pose (the same hook arm IK uses, so it
  needs arm IK's hook in, which VR with controllers installs), checked like the stance pre-stage: the mesh by
  address, its AnimTree, the game thread, the age (500 ms), inside SEH; journalled `BonePose.makoTurret`.
  Whatever the game set after our calls, the pose is composed from the values the game's own function computed
  for our point. The default is 7 (all three).
The controls are found each tick in the vehicle mesh's AnimTree (`SkelControlLists` → the chains, `IsA
BioSkelControl_TurretConstrained`), logged once with their names and which axis each constrains. Before each of our
calls, the controls' `DesiredBoneRotation` is compared with what we last wrote: `held` or `changed` (over a degree).
With both call seams on, the check before the vehicle-Tick call measures the world tick up to the vehicle's own Tick
against the mod tick's value (`heldAfterTick`/`changedAfterTick`), and the check at the mod tick measures the rest of
the world tick against the vehicle-Tick value (`heldAfterPawn`/`changedAfterPawn`). The heartbeat line prints all
four; a warning (every 30 s at most) says when the game re-aimed after our vehicle-Tick call in most checks (then
only the pose seam holds our aim at pose time). Snapshot `"mod"."mako"."turret"`: `callsTick`, `callsPawnTick`,
`heldAfterTick`, `changedAfterTick`, `heldAfterPawn`, `changedAfterPawn`, `poseWrites`, `poseRejected`, the
controls with their current desired rotation and ours.

**The reticle (`reticle = 1`).** `hand_aim::AimRay()` returns the controller's ray (tracking space: the aim pose's
position and forward, or the calibrated line) while the Mako aim is valid; `Mod.cpp` publishes it as before. The
host (`plugin/HalcyonOverlays`, a host change: redeploy) used to show the hand reticle on foot only; now also in game
mode 1, but only on a valid ray from the mod (never the controller fallback), and it hides the game's crosshair
while it shows as on foot. **[verify]** whether the Mako HUD's own reticle is the same `Pchud` crosshair widget
(if not, both show: then the Mako's widget needs its own hide).

**The vignette (`vignette = 1`, with `[Comfort] vignette` on).** `core/vr/Comfort`'s `VehicleMotion` (unit-tested):
the vehicle's speed (`Actor.Velocity`, units a second) and yaw rate (`Actor.Rotation.yaw`'s change per tick, the wrap
handled, low-passed over 0.1 s so one frame's jolt doesn't count) against `moveSpeed` (150 units/s) and `turnRate`
(20 deg/s) with hysteresis (off at 60%). `game/comfort`'s vignette logic takes "turning" and "moving" from it in the
Mako (`vignetteWhen = turn`: turning only; `move`: both) instead of the sticks, everything else as on foot (a world
view, our menu closed, the squad menu closed, controllers tracked). No snap pulse and no turning by the mod in the
Mako.

**Settings** (`[Mako]`, appended to an existing file with defaults): `handAim`, `turret`, `turretSeam`, `reticle`,
`vignette`, `moveSpeed`, `turnRate`, `aimRangeM`, `calibratedLine`. Log `mako: settings: ...`.

**Logs.** `mako: driving <vehicle> (behavior found, N turrets from the weapons' info, M turret controls in the
AnimTree, the vehicle's Tick hooked, its shot start hooked)`, `mako: the vehicle's turret controls ...`, `mako: the
right controller aims the turret` / `the game aims the turret (<why>)`, `mako: first aim: ...`, `mako: first turret
aim: SetDesiredTurretAimPoint(turret 0, (x, y, z)) ran and returned pitch P yaw Y roll R`, the shots, `mako: left
<vehicle>`, and the heartbeat `    mako: ...`. Snapshot JSON `"mod"."mako"`.

## 4. What works without the headset

- Builds: the mod, the host and the tests, `/W4 /WX`; the `VehicleMotion` tests (straight drive, hysteresis, the yaw
  wrap at 65536 and for signed yaws, the smoothing, clamped frame times, sanitized thresholds).
- The reflection rows are declared; whether every one resolves in the running game shows in `reflect: N of M checks
  agree` and `reflect-check.json` at the next session (the names are LExSDKv2's, which match the game's).

## 5. What stands down, and why

- Without `SetDesiredTurretAimPoint` (not found): the turret model keeps the game's aim; shots still follow the
  controller (logged once).
- Without `BioVehicleWheeled.Tick` or its `GetWeaponStartTraceLocation`: the mod's tick and the pose seam aim the
  turret; shots run along the controller's ray from the controller (parallel to it, from the hand, a few metres off
  the gun) instead of converged from the gun's start.
- Without a head-tracked world view, with our menu open, or without a tracked right controller: the game aims
  (`mako: the game aims the turret (...)`).
- Nothing was found that needs a native hook: the aim itself goes through functions the game exposes to script.
  What could not be settled without the game is the *order* of the game's own turret aim within the frame (section
  2), which is why the three seams exist and measure themselves.

## 6. To check in the headset [verify]

1. Enter the Mako. The log says `mako: driving ...` with the behavior found, 1 (or 2) turret indices and 2 turret
   controls; `reflect:` has no new failures.
2. Point the controller: the cannon turns with it (the turret model), and the hand reticle shows on the ray. If the
   turret follows the camera instead, read the heartbeat: `held ... changed ...` per seam and `pose seam N writes`. The
   pose seam writing with the turret still on the camera would mean the controls' `DesiredBoneRotation` isn't what
   the pose reads (then `m_aTurretRuntimeInfo`'s relative yaw/pitch, which Halcyon's step 2 wrote, is the next
   candidate, or the control's `BoneRotation`).
3. Fire: the shot lands under the reticle (`mako: shot N: ... X degrees off the controller's ray`: a few degrees at
   most, from the gun's start toward the aim point). If the shot still goes along the camera, the vehicle's
   `GetAdjustedAim` doesn't reach `BioPlayerController.GetAdjustedAimFor` (then hook `BioWeaponRanged.GetAdjustedAim`'s
   return for the vehicle's weapons instead).
4. Drive and turn: the vignette darkens the edges (with `[Comfort] vignette = 1`; `vignetteWhen = move` for driving).
5. The game's own Mako crosshair: hidden with the hand reticle, or doubled (section 3).
6. The view anchor's head-locked assumption (section 2): the reticle sits where the controller appears to point in
   the picture.
7. `turretSeam = 1`, `2`, `4` one at a time, to learn which seam the game lets stand (for the record here).
