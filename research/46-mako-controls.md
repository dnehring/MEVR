# 46: Physical Mako controls, stage 1: a throttle lever and a twist stick on the chase camera (2026-10-31)

Goal (the user approved the layout, 2026-10-31; the idea of grabbable cockpit controls only, no code from any closed game):
two simple controls in the Mako, driven by real hand movement, with the current chase camera. No seated view and no cockpit
model: those are later stages. Built without the game running (the worktree rule): everything marked **[verify]** needs a
headset session in the Mako. Section 7 is the checklist.

| Hand | Control | Movement | Effect |
|---|---|---|---|
| Left | Throttle lever | Grip it, push forward or pull back | Speed or reverse. Springs back to neutral when let go (`leverReturns = 1`, the user 2026-11-03 and 2026-11-04; stage 1 held its place) |
| Left | Throttle lever | X and Y while held | X: jump jets, or upright when flipped. Y: boost |
| Right | Drive stick | Grip it, twist about the stick's own axis | Steering. Springs to centre on release |
| Right | Drive stick | Trigger while held | Machine gun |
| Right | Drive stick | A while held | Cannon (our addition: see section 3) |
| Right | Drive stick | Thumbstick while held | The turret, as yaw and pitch relative to the vehicle |

## 1. How the sticks reach the physics (bytecode of `SFXGame.pcc`; generated headers for names and flags)

Sources: a bytecode listing of `SFXGame.pcc` made for earlier research (research/19, research/20; decompressed read-only with
the game's Oodle DLL, a local scratch disassembler, nothing kept or committed), the LE1 headers of LExSDKv2 for names and
function flags. `Engine.pcc` wasn't read for this job (the job's rule: nothing under the game folder), so the one function
from it is marked below. The Ghidra project stayed locked by a running MCP server for the whole job (section 6).

- While driving, `BioPlayerController` is in its state `PlayerDriving`. Its script `PlayerMove` calls `UpdateRotation` (the
  right stick into the controller's rotation) and then `ProcessDrive(forward, strafe, PlayerInput.aUp, bPressedJump)`.
- `PlayerMove` has two branches on `bUseCameraRelativeVehicleSteering || WorldInfo.IsConsoleBuild()`. With it, the raw stick
  is turned by the camera's yaw and back by the vehicle's before it goes on: pushing a stick drives toward where the camera
  looks. Without it, `RawJoyUp` and `RawJoyRight` go on as they are. The flag is transient and comes from the profile's option
  30 (`UpdateInputConfiguration`).
- `ProcessDrive` is `Engine.PlayerController`'s (`FUNC` flags 0x00020002: script, and its state override lives in
  `Engine.pcc`). In the public UE3 script it forwards to `Vehicle.SetInputs(InForward, -InStrafe, InUp)`: so the game's
  `Steering` is + left. **[verify]** the sign here: `[Mako] stickInvert` flips it.
- `BioVehicleBase.SetInputs` (`FUNC_Simulated`, not native): with `m_oBehavior.m_bPowertrainEnabled` and not
  `m_oBehavior.m_bIsPaused`, the base `Vehicle.SetInputs` (fields `Throttle`, `Steering`, `Rise`); otherwise all three zero.
- `BioVehicleWheeled.Tick` (script) sets `Throttle = 1` while boosting and `Throttle = 0` without a controller. It also
  says which simulation the Mako runs: **`BioSVehicleSimTank(SimObj).m_TorqueCurve = BoostTorqueCurve`** on boost start. So the
  sim is `SFXGame.BioSVehicleSimTank` (a `Engine.SVehicleSimTank`) by the game's own cast; the log confirms it on first drive
  (`mako controls: simulation is ...`). A plain car sim would still work: twist steers the wheels, no spin on the spot, and the
  log says so.
- Native side, not read: the tank sim's `UpdateVehicle`. In the public UE3 code it reads the vehicle's `Throttle` and `Steering`
  into `OutputGas` and `OutputSteering` and, with no throttle and some steering, uses `TurnInPlaceThrottle`; that's what makes
  "throttle centred + full twist" spin on the spot **[verify]**. `BioSVehicleSimTank.m_fCurrentInputYaw`, `m_fPrevInputYaw` and
  `m_fYawDeltaRate` have no script references in `SFXGame.pcc` at all, so they are the native's own; whether they follow
  `Steering` or something camera-related isn't known without Ghidra **[verify: the heartbeat prints `input yaw`]**.

## 2. The seam for input

**A post-handler of `BioVehicleBase.SetInputs` (`engine/call_function`, and `engine/process_event` should anything native call
it) overwrites `Vehicle.Throttle` and `Vehicle.Steering` while the mod drives.**

- Why there: it runs right after the game's own values went in, on every tick the controller is in `PlayerDriving`, and whatever
  the pad or the camera did above it is replaced. Substituting the parameters isn't possible for a script-to-script call
  (`engine/call_function` runs handlers after the original and has no parameter block), and isn't needed.
- `bUseCameraRelativeVehicleSteering` is **left alone**: its only script use is the branch above `ProcessDrive`, whose result we
  overwrite. Looking around never steers. Nothing to journal or restore. The log prints its value once.
- The handler keeps the game's own gate: with the powertrain off or the vehicle paused it leaves `SetInputs`' zeros alone
  (cinematic stops, `BioVehicleBehaviorBase.SetPowertrainState`).
- Boost: if the vehicle's `Tick` runs after the controller (as the game's own order must be for `Throttle = 1` to mean anything),
  boost still forces full throttle; ours doesn't fight it **[verify]**.
- Every write is journalled (`MakoControls.Throttle`, `MakoControls.Steering`).
- Fallback: no `SetInputs` seen for 200 ms while driving: the same write from the mod's tick, with one warning. That would be a
  frame late against the game's own write, so it's only a diagnostic stand-in (`[verify]`: the heartbeat counts `from the tick`).

A failed lookup of the fields stands the feature down with a line; the game's own controls stay.

## 3. The pad in the Mako

`BIOInput.ini`'s `[SFXGame.SFXGameModeVehicle]` binds (as read for research/19):

| Pad | Command | Today from Touch |
|---|---|---|
| left stick | `Console_Strafe`, `Console_Movement` (`RawJoy*`, driving) | left stick |
| right stick | `Console_LookX`, `Console_LookY` (the controller's rotation, the camera) | right stick |
| RT | `Console_Shoot` (`bFire`: `BioVehicleBase.Tick` acquires `BioActiveShootVehicleMachineGun`) | right trigger |
| RB | `Console_FireVehicleCannon`: `FireCannon` / `StopFiringCannon` (`bFireCannon`) | right grip |
| LB | `Console_VehicleBoost`: `Button bBoostersOn` | left trigger (`leftTrigger = lb`) |
| LT | `Console_VehicleAim`: `VehicleTightAim` | not from Touch by default |
| A | `Console_VehicleAction`: `Exclusive TryUprightVehicle \| Button bJumpJetsOn` | A |
| B | `Console_VehicleExit`: `TryExitVehicle` | B |
| Y | `Console_RepairVehicle`: `RepairVehicle` | Y (left) |
| X | `Console_VehicleLeaveWorld`: `LeaveWorld` | X (left) |
| R3 | `Console_ChangeMagnification` | right stick click |

`CanJump` and `CanBoost` are script gates on fuel, `m_bVehicleFunctionEnabled`, cinematic mode and healing. So jump, boost and
upright are all reached by passing the pad buttons the game already reads:

- **Left hand holds the lever (`gamepad::kFlagMakoLever`):** X is the game's A (jump jets; upright when flipped, by the same
  binding), Y is the game's LB (boost). The left trigger stays LB too. With the lever let go, X and Y are the game's again
  (leave the world, repair). The left stick's click on the lever is the repair since 2026-11-05: the mod calls
  `RepairVehicle` itself (game/CockpitPanel, research/48 section 27 "The repair on the throttle"); the vehicle mode binds no
  L3, so the pad's click reaches nothing.
- **Right hand holds the stick (`kFlagMakoStick`):** the trigger stays RT (the machine gun). A is the game's RB: the cannon
  **(our decision, not in the approved table: the grip, which is the cannon today, now holds the stick, and "trigger fires" reaches
  only the machine gun; A's own job is on the lever's X meanwhile).** The right thumbstick is kept from the game (no camera turn).
- **B stays `TryExitVehicle` always; A with the stick let go stays jump and upright.** Nothing depends on a menu or system button.
- **A press keeps the meaning it began with** (`Latches::makoDown`, `makoAs`): letting go of the lever mid-press never turns a held
  X into "leave the world". A thumbstick pushed as the hand lets go stays the turret's until centred (`Latches::squadHeld`).
- **The mod drives (`kFlagMakoDrive`):** no left stick from the controllers. A real pad's still reaches the game, but `SetInputs`'
  result is overwritten anyway.
- **A squeeze at a control isn't a button:** a new right squeeze within reach of the stick is kept from RB by the holsters' seam
  (`Options::rightGripFree`), a left one within reach of the lever from LB (`gripShoulder = false`). A squeeze away from them is
  as today: the right grip fires the cannon.

Host v23 is required for these flags: on an older host the feature stays off (`Link::supported`).

## 4. Rules (`core/vr/MakoControls`, unit-tested: `TestMakoControls`)

- **Placement:** from the head's position and flat yaw when the Mako is entered and on each recenter (the host's floor counter, as
  `game/BodyFollow`), offsets along the seat's right, up and forward (`leverOffset`, `stickOffset`). A recenter lets go of the holds
  but leaves the lever where it is: it isn't a safety stop. A head looking straight down waits.
- **Grab:** a NEW squeeze (the pad mapping's latch, 0.6 on and 0.4 off) within `grabRadiusM` of the control. A squeeze that began
  elsewhere never grabs. The lever's grab point moves with it, so the hand finds it where it was left. No exact spot.
- **Lever:** the hand's travel along the seat's forward, step by step, so pushing past an end doesn't wind up; full at
  `throttleTravelM`. Nothing inside `throttleDetent` (a fraction of the travel), rescaled outside it; a tick on entering the notch.
- **Stick:** swing-twist about the axis at grab time: the grip pose's -Z, by OpenXR's definition the tube the fingers close around,
  little finger to thumb. The rotation since the grab, in tracking space, has twist angle `2 atan2(dot(xyz, axis), w)`. Moving or
  swinging the arm gives none. Unwrapped step by step (past half a turn the raw angle jumps, the steering doesn't). Dead zone
  `stickDeadZoneDeg`, full lock `stickTwistDeg`, clockwise from above is right. A tick on returning to centre, a stronger buzz on
  reaching full lock, re-armed below 90%. On release it springs back at 6 per second; held it moves at most 12 per second.
- **Rumble:** while a hand holds a control, `rumble` x `|ForwardVel| / MaxSpeed`, asked each tick for 0.1 s at 60 Hz (so it stops by
  itself if the mod stalls).
- **Fallback:** until a control has been grabbed in this drive nothing changes: the game's own controls work as before.
- **Safety:** throttle and steering to zero, written once, and the game's own controls back until the next grab: mode != 1 (on foot, a game
  menu, a cutscene), our menu, the calibration, no head-tracked view, a controller untracked over 0.5 s, the vehicle gone with a
  level, `controls = 0` in the file, the mod unloading. A grip held through a stop needs a new squeeze.
- **Released right hand:** the Mako goes straight at the set throttle; the right hand aims directly (`game/mako`'s ray, research/44).

## 5. The turret and the camera

- **Thumbstick:** while the stick is held, yaw and pitch relative to the vehicle (`stickAimRate`, pitch -45..60), seeded at the grab
  from where the turret aims now, so a grab doesn't jerk it. Its ray (from 150 units above the vehicle's origin, along the vehicle's
  axes) replaces the hand's in `game/mako`: same trace, same `SetDesiredTurretAimPoint`, same `ShotRotation`. **The reticle** is
  published on the line from the head to what that ray hits, so it sits on the target. Let go: the hand's ray takes over.
- **The chase camera doesn't follow the vehicle** **(our addition, needed by this layout)**. `SFXCameraMode_Vehicle` hangs on the
  controller's rotation, which only the right stick turns (`PlayerDriving.UpdateRotation`), and on a pad the camera-relative steering
  makes that fine: you steer toward where you look. With twist steering the Mako would turn away under a still camera, and
  the right stick is the turret's while the stick is held. So while the mod drives, the vehicle's yaw change goes into ControlRotation
  in the mod's tick (`MakoControls.ControlRotation`, journalled; `view_anchor::NoteTurn` as `game/comfort` does), and the camera
  keeps its angle to the vehicle. `[Mako] cameraFollow = 0` turns it off. That turns the world on every steer: the vignette's
  `turnRate` is there for it **[verify the comfort, and whether SFXCameraMode's native update does anything of its own here]**.

## 6. What was built, and what couldn't be determined

- Core: `core/vr/MakoControls` (pure logic, pictures, the host link). Game: `game/MakoControls`, called from `game/mako`'s tick. Host:
  `plugin/HalcyonOverlays` draws two flat quads at the seat (`makolever`, `makostick` in snapshots; drawn on the CPU and uploaded only when
  the picture changes), a slot and a dial seen from above, cyan idle, white with a hand in reach, amber in the hand. Offline look:
  `mevr-mako-preview <folder>`. `[MEVR]` in `xr_session.cpp`: `kMaxLayers` 14, `mevrQuads` 10. HostServices v23 `setMakoControls`.
- Settings: `[Mako]` `controls`, `grabRadiusM`, `stickTwistDeg`, `stickDeadZoneDeg`, `stickInvert`, `throttleTravelM`, `throttleDetent`,
  `leverOffset`, `stickOffset`, `stickAimRate`, `rumble`, `cameraFollow`, appended to old files.
- Logs: `mako controls:` (settings, placement, first grab per hand, simulation class, the seam, stops), a heartbeat with what the physics
  shows (`Throttle`, `Steering`, `OutputGas`, `OutputSteering`, `OutputBrake`, `ForwardVel`, `ActualSteering`, tracks, input yaw); `[MEVR-MAKO]`
  from the host; snapshot `"mod"."makoControls"`.
- **Not determined without the game or Ghidra:** the tank sim's native (`UpdateVehicle`, the `m_fCurrentInputYaw` family), which is
  the one thing that could still tie steering to the camera; the order of the controller's and the vehicle's ticks; the state's
  `ProcessDrive` in `Engine.pcc` (the sign).

## 7. To check in the headset [verify]

1. Enter the Mako: `reflect:` has no new failures; `mako controls: placed from the head`; `simulation is ...BioSVehicleSimTank`. The lever and the
   stick are at your knees (`[MEVR-MAKO]`, or a snapshot's `makolever` and `makostick` layers). Thumbsticks still drive as before.
2. Grab the lever (a squeeze near it, off the spot): nothing jerks; `mako controls: first grab`, `seam: ... through CallFunction`, the heartbeat's
   `written` counting and the vehicle's `Throttle` equal to ours. Push: it drives; let go: it keeps going; pull through the notch: a tick; pull
   further: reverse. If `Throttle` matches and `OutputGas` doesn't, the native reads something else: read the heartbeat, then the native.
3. Grab the stick: twist right: it turns right, or set `stickInvert`. Move the arm around without twisting: nothing steers. `input yaw` in the
   heartbeat: does it follow `Steering`, or the camera?
4. Throttle centred, full twist: spins on the spot (the tank's `TurnInPlaceThrottle`, printed once).
5. Look around with the head and, hand released, the right stick: the Mako's course doesn't change, with `bUseCameraRelativeVehicleSteering` on or off.
6. Lever held: X jumps (and uprights a flipped Mako), Y boosts (and does the throttle go to 1: both tick orders). Stick held: trigger = machine gun,
   A = cannon, thumbstick = turret and reticle, no camera turn. Let go of the stick: the hand's ray takes over, the right grip away from
   the stick fires the cannon.
7. `cameraFollow`: does the view stay behind the Mako as it turns, and is it comfortable with the vignette? With `cameraFollow = 0`?
8. Stops: open our menu (both sticks held), the game's pause menu (our top bar), the calibration, exit the Mako, recenter (holds let go, controls move,
   throttle kept), hide a controller for over half a second. Each: the Mako doesn't run on, the thumbsticks drive again, and the next grab starts
   clean. `controls = 0`: exactly as before, live.
9. Haptics: the tick, the lock's buzz, the engine's rumble rising with speed.
10. `grabRadiusM`, offsets, `stickTwistDeg`: do the defaults suit a seated and a standing player? The quads are at knee height from the head's
    place at entry.
