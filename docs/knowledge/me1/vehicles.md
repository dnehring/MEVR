---
type: System
title: ME1 vehicles - the Mako's aim, physical controls, cockpit, gunner
description: What ME1 does in the Mako in VR - the turret and shots on the right controller's ray, the grabbed throttle lever and drive stick, the jets, the cockpit with Shepard seated, the repair button and extraction lever, hazard lights, the gunner, the reticle and the vignette - as numbered contracts.
tags: [me1, catalog, mako, vehicle, cockpit, controls]
status: draft
timestamp: 2026-11-18T07:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T07:00:00Z }
game: LE1
---

# What the player gets

- Entering the Mako (game mode 1) puts the player in a cockpit: a hull with windows around the head, the game's
  third-person chase view on the centre console's screen, the Mako's own cannon visible over the cabin.
- Shepard sits in the chair under the player. His hands close on the lever and the stick while the player holds them.
- The left hand grips a throttle lever on the left armrest: push forward, pull back, let go and it springs to neutral.
  While it is held X is jump (and upright), Y is boost, the left stick's click is the repair.
- The right hand grips a drive stick on the right armrest: tilt left and right to steer, twist to add steering or spin on
  the spot. While it is held the trigger is the machine gun, A is the cannon, the thumbstick aims the turret.
- While the jump jets fire, tilting the stick tips the Mako, which drifts that way.
- With the right hand off the stick, the hand itself aims: the turret and the shots go where the controller's ray hits,
  and the hand reticle sits on that ray.
- With nobody aiming, a squadmate gunner aims at the nearest visible hostile and fires bursts, the cannon at big targets
  or crowds. Thumbstick or hand takes the gun back at once.
- A repair button on the right desk shelf (held 2 s) and an overhead extraction handle (pulled down) are worked by hand.
- With the hull's health low the cabin flashes red.
- Until a control has been grabbed, the game's own pad controls work unchanged.

# Contracts

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| VEH-01 | In the Mako the right controller's aim ray, mapped into the world and traced, is the aim point for turret and shots. With a saved pistol calibration its calibrated line stands in for the raw aim pose. | Trace range `aimRangeM` 200 m (clamp 5..2000); aim older than 500 ms aims nothing (`kAimFreshMs`); `calibratedLine = 1` | Game mode 1 with a `BioVehicleBase` pawn; head-tracked world view; our menu closed; right controller tracked; `[Mako] handAim = 1`. Otherwise the game aims, log `mako: the game aims the turret (<why>)` | [Mako.cpp](../../../src/game/Mako.cpp) `Tick`; [Mako.hpp](../../../src/game/Mako.hpp) | fake headset (trace 2026-11-03, research/47 "Why Trace never hit"); headset drive 2026-11-03 |
| VEH-02 | Shots (cannon and machine gun) leave the gun's own start toward the aim point, so they converge on what the controller points at. Without a start seen, they run along the controller's ray from the controller. | Shot start (`BioVehicleWheeled.GetWeaponStartTraceLocation` post-handler) fresh within 200 ms (`kStartFreshMs`); roll 0; first 3 shots logged, then one every 2 s | Asked from `game/hand_aim`'s `GetAdjustedAimFor` handler when the on-foot barrel path does not apply; also with hand aim off while the gunner aims (`GunnerAiming`) | [Mako.cpp](../../../src/game/Mako.cpp) `ShotRotation`; [HandAim.cpp](../../../src/game/HandAim.cpp) | fake headset 2026-11-03 (research/47 "Shots go where the reticle is") |
| VEH-03 | The turret model turns to the aim point through the game's own `BioVehicleBehaviorBase.SetDesiredTurretAimPoint`, for every turret index the weapons' info names (turret 0 if empty). | `turretSeam` bits: 1 the mod's tick, 2 a `BioVehicleWheeled.Tick` post-handler, 4 a bone-pose pre-stage rewriting `BioSkelControl_TurretConstrained.DesiredBoneRotation` (age limit 500 ms). Shipped default 3 (code); "held" = moved under 182 units (1 degree); at most 4 turrets | `[Mako] turret = 1`; function missing: model keeps the game's aim, shots still follow | [Mako.cpp](../../../src/game/Mako.cpp); [BonePose.cpp](../../../src/engine/BonePose.cpp) (`BonePose.makoTurret`) | [verify] which seam the game lets stand (research/44 section 6.7) |
| VEH-04 | Riding in the cockpit with `showTurret = 1`, the Mako's turret meshes stay visible over the cabin and shots start at the firing gun's muzzle (`GetMuzzleLoc`), not at the camera in front of the face. | Cockpit eye `0 1.0 0.25` m (right, forward, up) from the Mako's origin; the turret pivot is 0.75 m over the origin; five turret meshes | `[Cockpit] showTurret = 1`; 0: turret hidden, the game's shot start | [Mako.cpp](../../../src/game/Mako.cpp) start-trace handler; [place.py](../../../tools/cockpit/place.py) `--eye` | fake headset 2026-11-04 (research/47 "The cannon over the cabin") |
| VEH-05 | The hand reticle shows in the Mako only on a valid ray the mod publishes (never the controller fallback). While the stick is held it sits on the line from the head to what the thumbstick's turret ray hits. | Reticle settings are the on-foot ones (`MELEVR.ini handReticle*`) | Game mode 1; `[Mako] reticle = 1`; Mako aim valid | [Mako.cpp](../../../src/game/Mako.cpp) `AimRay`; [HalcyonOverlays.cpp](../../../src/plugin/HalcyonOverlays.cpp) | fake headset; headset [verify] placement |
| VEH-06 | The game's own Reticule panels are hidden in VR gameplay whenever the hand reticle option is on (alpha 0 on each Reticule handler's `_root`), in the Mako too; the cockpit's orange aim mesh is seen only by the cockpit's captures and parks outside the Mako. | Host request expires after 500 ms; native sniper mode (2) and non-gameplay modes exempt | HostServices v35; hand reticle option on; XR gameplay frame | [ScreenReticle.cpp](../../../src/game/ScreenReticle.cpp); [ScreenReticle.hpp](../../../src/core/vr/ScreenReticle.hpp); [CockpitProbe.cpp](../../../src/game/CockpitProbe.cpp) `ShowCockpit` | unit test `TestScreenReticle`; game run [verify] (research/44 "Persistent centre cursor") |
| VEH-07 | The comfort vignette also darkens while the Mako turns, and with `vignetteWhen = move` while it drives. The mod never snap-turns or turns the player in the Mako. | Moving from 150 units/s (`moveSpeed`), turning from 20 deg/s (`turnRate`), off at 0.6 of each, yaw rate low-passed 0.1 s, frame time clamped 1 ms..0.1 s, yaw wrap at 65536 | `[Mako] vignette = 1` and `[Comfort] vignette` on; world view, menus closed, controllers tracked | [Comfort.hpp](../../../src/core/vr/Comfort.hpp) `VehicleMotion`; [Mako.cpp](../../../src/game/Mako.cpp) `CurrentMotion`; [Comfort.cpp](../../../src/game/Comfort.cpp) | unit test; headset [verify] |
| VEH-08 | The lever and the stick are fixed in tracking space at the seat. With the cockpit up they are the cockpit models' grips; without it they are placed from the head's position and flat yaw on entering and on each recenter, and drawn as two flat quads (slot and dial). | Offsets from the head (right, up, forward): lever -0.362 -0.446 0.286 m, stick 0.375 -0.415 0.314 m; quads 0.06 x 0.40 m and 0.13 m; cyan idle, white in reach, amber held; a recenter drops the holds, keeps the lever | `[Mako] controls = 1`; host v23 (pad flags); a head looking straight up or down waits; with the cockpit up the flat quads are hidden | [MakoControls.cpp](../../../src/core/vr/MakoControls.cpp) `PlaceSeat`; [CockpitProbe.cpp](../../../src/game/CockpitProbe.cpp) `ControlSeat`; [HalcyonOverlays.cpp](../../../src/plugin/HalcyonOverlays.cpp) | fake headset 2026-11-03; headset 2026-11-03 |
| VEH-09 | A control is grabbed by a NEW grip squeeze within reach of it. A squeeze begun elsewhere never grabs. Movement counts from where the hand grabbed, so a grab never jerks the Mako. | Grip latch 0.6 on / 0.4 off; `grabRadiusM` 0.15 (clamp 0.02..1.0); the stick's zero follows the hand for 0.2 s after the grab (`kGrabSettleS`) | Until the first grab in a drive nothing changes: the game's own controls work | [MakoControls.hpp](../../../src/core/vr/MakoControls.hpp) `Controls::Step` | unit test `TestMakoControls`; fake headset by replay |
| VEH-10 | The throttle lever: hand travel along the seat's forward sets the throttle; pushing past an end does not wind up. Let go, it returns to neutral. | Full at `throttleTravelM` 0.055 m; neutral notch `throttleDetent` 0.15 of the travel in the code default and the appended block (a per-key append writes 0.08: see Problems); returns at 4 travels/s (`kLeverReturnRate`); `leverReturns = 1`; a haptic tick entering the notch | Held by the left hand | [MakoControls.cpp](../../../src/core/vr/MakoControls.cpp) `ThrottleFromLever` | unit test; headset 2026-11-04 (the return) |
| VEH-11 | The drive stick tilts: the hand's travel from the grab is a tilt; left/right steers, limited to a share of full lock. A twist about the stick's axis adds to it. The cockpit's grip model tilts and turns with the hand. | `stickTiltDeg` 20 for full; `stickHeightM` 0.20 (about 7 cm of travel); dead zone `stickDeadZoneDeg` 4; `tiltSteerMax` 0.5; twist full lock `stickTwistDeg` 42; steering moves at most 12/s held, springs back at 6/s; buzz at full lock, tick at centre | `stickTwist = 0` (default); `stickTwist = 1` is stage 1's twist-only steering; `stickInvert` | [MakoControls.cpp](../../../src/core/vr/MakoControls.cpp) `TiltFromTravel`, `FromTilt`, `TwistDegrees` | unit test; fake headset 2026-11-03 (research/47 "The stick tilts") |
| VEH-12 | With the throttle near zero the twist alone spins the Mako on the spot: the throttle sent is exactly 0 and the body's yaw rate is driven to the asked rate. | `steerInPlaceBelow` 0.2; `spinRateDeg` 60 deg/s; rate gain 0.35 per tick; the sign flips itself after 0.3 s of turning the wrong way above 20 deg/s | Driving; `spinRateDeg = 0`: the game's pivot | [MakoControls.cpp](../../../src/game/MakoControls.cpp) spin (`SetRBAngularVelocity`, `bAddToCurrent`) | fake headset 2026-11-03 (research/47 "Which way the body turns") |
| VEH-13 | The mod's throttle and steering overwrite `Vehicle.Throttle` / `Steering` right after the game's own values, so the pad and the camera never steer while the mod drives. Looking around never steers. | `BioVehicleBase.SetInputs` post-handler (CallFunction and ProcessEvent); none seen for 200 ms: the mod's tick writes instead; writes journalled `MakoControls.Throttle` / `.Steering` | Driving (a control grabbed); left alone where the game zeroed them (powertrain off, vehicle paused) | [MakoControls.cpp](../../../src/game/MakoControls.cpp) `ApplyInputs`, `OnSetInputsCall` | fake headset 2026-11-03 |
| VEH-14 | While the mod drives, the chase camera keeps its angle to the vehicle: the vehicle's yaw change goes into ControlRotation. With the cockpit this is the centre screen's view. | `[Mako] cameraFollow = 1`; `[Cockpit] followCamera = 1`; journalled `MakoControls.ControlRotation` | Driving | [MakoControls.cpp](../../../src/game/MakoControls.cpp) | headset comfort [verify] (research/46 section 5) |
| VEH-15 | Left hand on the lever: X is the game's A (jump jets; upright when flipped), Y is the game's LB (boost), the left trigger stays LB. Lever let go: X and Y are the game's again (leave world, repair). A press keeps the meaning it began with. | Flag `kFlagMakoLever` (bit 20); latches `makoDown`, `makoAs` | Only in the Mako; lever held | [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp); [GamepadMapping.cpp](../../../src/game/GamepadMapping.cpp); [MakoControls.cpp](../../../src/game/MakoControls.cpp) `AdjustPad` | unit test (mapping); headset 2026-11-03 |
| VEH-16 | Right hand on the stick: the trigger stays RT (machine gun), A is the game's RB (cannon), the right thumbstick is the turret's and is kept from the game (no camera turn) until centred after release. | Flag `kFlagMakoStick` (bit 21) | Only in the Mako; stick held | [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp); [MakoControls.cpp](../../../src/game/MakoControls.cpp) `AdjustPad` | unit test; headset 2026-11-03 |
| VEH-17 | A squeeze at a control is never a button: a new right squeeze within reach of the stick is not RB (the cannon), a left one within reach of the lever is not LB. A right squeeze away from the stick fires the cannon as the game binds it. While the mod drives, the controllers' left stick does not reach the game. B is always `TryExitVehicle`. | `Options::rightGripFree`, `gripShoulder = false`; `kFlagMakoDrive` (bit 19) | In reach = `grabRadiusM`; a real pad's left stick still reaches the game (and is overwritten) | [MakoControls.cpp](../../../src/game/MakoControls.cpp) `AdjustPad` (lines 781-793) | unit test; research/46 section 3 |
| VEH-18 | While the stick is held its thumbstick sets the turret's yaw and pitch relative to the vehicle; the ray starts above the vehicle's origin. Grabbing seeds it from where the turret aims, so nothing jerks. Let go: the hand's ray takes over. | `stickAimRate` 90 deg/s; dead zone 0.15; pitch -45..60 degrees; ray origin 150 units up (`kStickRayUp`) | Stick held | [MakoControls.hpp](../../../src/core/vr/MakoControls.hpp) `StepAim`, `SeedAim`; [MakoControls.cpp](../../../src/game/MakoControls.cpp) `Tick` | unit test; fake headset |
| VEH-19 | While the jump jets fire, the stick's tilt tips the Mako's rigid body and the jets push it that way; level when the stick is not held. The sideways tilt then does not steer (the twist still does). The booster is the game's own. | `thrusterPitchDeg` 20, `thrusterRollDeg` 15 (clamp 25; the game resists rolls past 30), at most `thrusterRateDeg` 60 deg/s; angle gain 3.0/s, rate gain 0.35; pitch dead zone `stickPitchDeadZoneDeg` 8; measured: full tilt gains 15.5 m/s forward, 11-13 m/s sideways per burst | `bJumpJetsOn`; `thrusterStabilize = 1` | [MakoControls.cpp](../../../src/game/MakoControls.cpp) `StabilizeOnThrusters`; [MakoControls.hpp](../../../src/core/vr/MakoControls.hpp) `JetTarget` | fake headset 2026-11-04 (research/47 "Flying on the jets") |
| VEH-20 | The jets' and booster's fuel is partly given back while they fire, so a burst lasts longer. Each burst is logged with the speed it gained. | `thrusterBurnScale` 0.4 (2.5x as long; clamp 0.05..1; LE1's jets have 0.7 s) | In the Mako | [MakoControls.cpp](../../../src/game/MakoControls.cpp) | fake headset 2026-11-04 |
| VEH-21 | The holding hand feels the engine: a rumble that rises with speed. | `rumble` 0.12 x speed share, 60 Hz, asked for 0.1 s each tick (stops by itself if the mod stalls) | A hand holds a control | [MakoControls.cpp](../../../src/game/MakoControls.cpp) | headset [verify] |
| VEH-22 | A drive stops safely: throttle and steering to zero once, the holds dropped, the game's own controls back until the next grab. A grip held through a stop needs a new squeeze. | Stops: left the Mako (mode != 1), our menu, the calibration, no head-tracked view, a controller untracked over 0.5 s (`kTrackingGraceS`), the level gone, `controls = 0`, the mod unloading | Always | [MakoControls.hpp](../../../src/core/vr/MakoControls.hpp) `Stop`; [MakoControls.cpp](../../../src/game/MakoControls.cpp) (line 697) | unit test; fake headset (death, cutscene, level load, research/47) |
| VEH-23 | Entering the Mako runs the cockpit's script (parts, console screen, aim screen, HUD displays, seat, panel, ride) and leaving parks it. Riding, the eye is the cockpit's eye in the vehicle's full frame and the head looks round the cabin; the chase POV feeds the console's screen. | Script `ASI\cockpit\cockpit.txt`; screens 768x343 and 768x306; eye `0 1.0 0.25` m minus the head's offset when the ride began; a recenter takes a new seat; pad flag `cockpitLook` | `[Cockpit] enabled = 1`; the cockpit package installed in `CookedPCConsole`; game mode 1 | [CockpitProbe.cpp](../../../src/game/CockpitProbe.cpp) `Tick`, ride; [SupportGrip.cpp](../../../src/game/SupportGrip.cpp) (`cockpitLook`); [place.py](../../../tools/cockpit/place.py) | fake headset 2026-11-03; headset 2026-11-03 |
| VEH-24 | Riding, the Mako's status and radar show on the cockpit's displays and the HUD panel moves to its anchor in the cabin. | Layout from the script's `hud` line | Riding with a `hud` layout; wheel not up | [CockpitProbe.cpp](../../../src/game/CockpitProbe.cpp) `FillHud`; [HalcyonOverlays.cpp](../../../src/plugin/HalcyonOverlays.cpp) `cockpitHud` | fake headset 2026-11-03 (research/47 "The Mako's HUD in the cockpit") |
| VEH-25 | Shepard sits in the chair under the player: shown to the eyes only, head meshes hidden, lit by the vehicle's light environment, hips on the H-point, legs by two-bone IK, hands on the controllers. A held control's hand closes on the handle's axis. | Head bone 0.10 m behind the eye (`kHeadBoneBehindEyeM`); slide back up to 0.25 m; recline at most 35 degrees; ankle 0.08 m above the heel | `[Cockpit] seated = 1` and a `seat` layout | [CockpitProbe.cpp](../../../src/game/CockpitProbe.cpp) `CurrentSeat`, `NoteSeatHips`; [ArmIk.cpp](../../../src/game/ArmIk.cpp) `DriveSeated` | fake headset 2026-11-03 (research/47 "Shepard in the seat", "The hands on the handles") |
| VEH-26 | The repair button on the right desk shelf: a fingertip coming from in front pushes the cap; pressed and held, the plate's eight squares fill and `SFXGameModeVehicle.RepairVehicle` runs once. Lifting early empties the progress. | Hold `holdS` 2.0 s; fingertip 0.04 m ahead of the aim pose; radius 0.03 m; from in front within 0.05 m; pressed at 0.6 of the travel, released at 0.3; cap travel 0.0028 m; 8 progress squares | Riding with a `panel` layout; one repair per press | [CockpitPanel.cpp](../../../src/core/vr/CockpitPanel.cpp) `Panel::Step`; [CockpitPanel.cpp](../../../src/game/CockpitPanel.cpp) | unit test `RunCockpitPanelTests`; fake headset (research/48 section 27) |
| VEH-27 | While the repair cannot run, a press fills nothing and calls nothing: solid red squares with no omni-gel, a red flash while it cools down. | Flash every `refusalFlashS` 1.2 s (lit its first half); reads the `VehicleRepair` power's `IsRequiredResourceAvailable` and `m_fCooldownRemaining`; no gel wins over cooling | Button pressed | [CockpitPanel.hpp](../../../src/core/vr/CockpitPanel.hpp) `Refusal`; [CockpitPanel.cpp](../../../src/game/CockpitPanel.cpp) | unit test |
| VEH-28 | The left thumbstick's click while the left hand holds the throttle lever runs the repair at once, cockpit or not. A click begun off the lever counts for nothing. A refusal shows red and buzzes the left hand twice. | `throttleClick = 1`; refusal shown `clickRefusalS` 2.4 s or while held; L3 is unbound in the vehicle mode | Lever held, our menu not up | [CockpitPanel.hpp](../../../src/core/vr/CockpitPanel.hpp) `ThrottleClick` | unit test; headset [verify] |
| VEH-29 | The overhead extraction handle: a new grip squeeze near its grip takes it, pulling the hand down swings it, held at the end it calls `SFXGameModeVehicle.LeaveWorld` once. Let go, it swings back. | Grab radius 0.10 m; `pullTravelM` 0.05 m for the whole 10-degree swing; at the end = 0.85 of the pull held 0.4 s; returns at 240 deg/s; re-armed below half the pull | Riding with a `panel` layout | [CockpitPanel.cpp](../../../src/core/vr/CockpitPanel.cpp); [CockpitPanel.cpp](../../../src/game/CockpitPanel.cpp) `CallGame` | unit test; headset 2026-11-05 (direction fixed: pull, not push) |
| VEH-30 | The repair ring's nine lamps show the hull's health. | 9 lamps, each a share of full health; green over 0.6, amber over 0.3, red below | Riding with a panel layout | [CockpitPanel.hpp](../../../src/core/vr/CockpitPanel.hpp) `HealthLamps`, `HealthColour`; [CockpitProbe.cpp](../../../src/game/CockpitProbe.cpp) `ShowPanelLamps` | unit test |
| VEH-31 | Hazard lights: with the hull's health low the cabin's lamps flash red like a beacon and the rest of the cabin washes red in time; back to its own materials once repaired. | On below `hazardBelow` 0.30, off above it + 0.10; one flash per `hazardPeriodS` 1.1 s (clamp 0.2..5), a cosine cubed, first flash at once; lamp brightness 0.8..10, wash 0.22..1.05; health read every 0.1 s | `[Cockpit] hazard = 1`; `hazardWash = 0`: lamps only | [CockpitHazard.hpp](../../../src/core/vr/CockpitHazard.hpp); [CockpitProbe.cpp](../../../src/game/CockpitProbe.cpp) | unit test `TestCockpitHazard`; fake headset 2026-11-04 |
| VEH-32 | The gunner: riding, with the player not aiming, a squadmate aims the turret at the nearest visible hostile within range. The current target is kept unless another is clearly nearer. | Range `gunnerRangeM` 120 m; scan every 0.25 s; sticky factor 0.7; visible = `FastTrace` from the muzzle; dormant pawns (hidden, no collision) skipped; hostility by the driver's `IsHostile` | Riding in the cockpit; `[Mako] gunner = 1`; needs a target | [MakoGunner.hpp](../../../src/core/vr/MakoGunner.hpp) `Choose`; [MakoGunner.cpp](../../../src/game/MakoGunner.cpp) `Scan` | unit test `TestMakoGunner`; fake headset 2026-11-04 |
| VEH-33 | The gunner fires the machine gun in bursts and the cannon at big targets or crowds. The player's own trigger fires the machine gun where the gunner aims without being cut by its pauses. | Settle 0.35 s on a new target; burst 1.2 s, pause 0.5 s; cannon at most every 3 s, held 0.25 s; big = collision radius 1.4 m; crowd = `gunnerCrowd` 3 within `gunnerCrowdRadiusM` 6 m | `gunnerCannon = 1` for the cannon; player firing = right trigger over 0.1 | [MakoGunner.hpp](../../../src/core/vr/MakoGunner.hpp) `Fire`, `UseCannon`; [MakoGunner.cpp](../../../src/game/MakoGunner.cpp) | unit test; fake headset 2026-11-04 |
| VEH-34 | The player takes the gun at once: by the held stick's thumbstick past its dead zone, or by the free right hand aiming (finger on the trigger or pulling it, ray out the front). The gunner takes it back after a pause. | Thumbstick dead zone 0.25; hand intent = trigger touch or trigger over 0.05; handback `gunnerHandbackS` 3 s | A hand working the cockpit's panel (or the right hand at the handle) leaves the gun with the gunner at once; the V2 scope open keeps manual control | [MakoGunner.hpp](../../../src/core/vr/MakoGunner.hpp) `Step`, `HandAims`, `StickAims` | unit test; headset [verify] |
| VEH-35 | The game no longer freezes while its window has no focus, so the Mako's exit works under a headset whose window is not in front. The pad reaches the game only with focus; the cockpit's controls work without. | `[Game] pauseUnfocused = 0` (default since 2026-11-05) | All modes | [FocusPause.cpp](../../../src/game/FocusPause.cpp) | fake headset 2026-11-03 (research/47 "Why the Mako wouldn't let Shepard out") |
| VEH-36 | A second cockpit (V2, "bottom" seat) can be chosen; it has deployable forward optics that take over the weapon sight and zoom, during which the gunner stands down. | `[Cockpit] seatPosition = top / bottom`; V2 needs `cockpit\cockpit-v2.txt` and `BIOG_MEVR_CockpitV2.pcc`, else top is used | Menu row; optics aiming = native zoom or R3 | [MakoSeat.hpp](../../../src/core/vr/MakoSeat.hpp); [CockpitProbe.cpp](../../../src/game/CockpitProbe.cpp) `OpticsAiming`, `UpdateOptics` | [verify]: no research note found; read from code only |

# Arbitration

| input | claimed by | order and rule | what the loser sees |
|---|---|---|---|
| Right grip squeeze | The extraction handle (within 0.10 m of its grip), then the drive stick (within `grabRadiusM`), else the game | A new squeeze in reach of a control is the control's (`rightGripFree`); elsewhere it is the game's RB | In reach: the cannon does not fire. Away: the cannon fires (VEH-17) |
| Left grip squeeze | The throttle lever within reach, else the game | `gripShoulder = false` in reach | No LB (boost) from a squeeze at the lever |
| A | The game's RB (cannon) while the stick is held; else the game's A (jump, upright) | The press keeps the meaning it began with | Jump is on the lever's X while both are held |
| X, Y | Jump and boost while the lever is held; else the game's leave-world and repair | `makoDown` / `makoAs` latches | Letting go of the lever mid-press never turns a held X into "leave the world" |
| Right thumbstick | The turret while the stick is held (kept until centred); else the game's camera look | `kFlagMakoStick` | No camera turn while the stick is held |
| Left thumbstick | Nothing while the mod drives (`kFlagMakoDrive`); else the game's driving | - | The game's result is overwritten anyway |
| Left stick click | The repair, while the lever is held | Mod calls `RepairVehicle` itself | The vehicle mode binds no L3 |
| Turret aim | Stick's thumbstick, then the free hand's ray, then the gunner | Manual input wins at once; the gunner returns after 3 s with a target; a hand at the panel hands the gun to the gunner at once | The hand's ray aims nothing while the gunner or the thumbstick has it |
| Left trigger | The game's LB (boost) in the Mako | The squad wheel's trigger claim is on foot only (`squad_menu::OwnTrigger`; "the Mako keeps LB") | No squad wheel in the Mako |
| B | The game's `TryExitVehicle`, always | No mod claim in mode 1 (squad placement and the melee rule are on foot) | - |

# Problems ME1 already solved

| problem | fix | where |
|---|---|---|
| `Actor.Trace` through ProcessEvent never hit: natives with their own opcode are skipped | `ue3::runtime::Call` clears `iNative` for its call | [gotcha](../gotchas/process-event-skips-opcode-natives.md); research/47 "Why Trace never hit" |
| The Mako would not let Shepard out: ME1 sets `bPlayersOnly` while its window has no focus | `[Game] pauseUnfocused = 0`, `game/FocusPause` | [research/47](../../../research/47-cockpit-in-game.md) "Why the Mako wouldn't let Shepard out" |
| The turret followed the camera: the order of the game's own turret aim within a frame is unknown | Three seams that measure themselves (held / changed per seam) | [research/44](../../../research/44-mako.md) sections 2-3 |
| Shots started at the camera, in front of the face, in the cockpit | The start-trace handler returns the firing gun's `GetMuzzleLoc` | research/47 "The cannon over the cabin" |
| The first jet control added the stick's pitch each tick and flipped the Mako (+16 to +84 degrees in a burst) | A rate controller on angular velocity toward a target attitude, rate-limited | research/47 "Flying on the jets"; [MakoControls.cpp](../../../src/game/MakoControls.cpp) |
| A sideways tilt that also steered turned the Mako 88 degrees in a jet burst | During the jets the tilt rolls only; the twist steers | research/47 "Flying on the jets" |
| Steering at zero throttle drove both tracks and surged | Throttle sent exactly 0 below `steerInPlaceBelow`; the spin by angular velocity | research/47 "Which way the body turns", "Tilt and twist split" |
| A squeeze shifted the hand: each grab started tilted forward | The stick's zero follows the hand 0.2 s after a grab; `stickHeightM` 0.20 (0.10 made 2 cm of drift 10 degrees) | [MakoControls.hpp](../../../src/core/vr/MakoControls.hpp) comments |
| The chase camera does not follow the vehicle (it hangs on ControlRotation) | The vehicle's yaw change goes into ControlRotation while driving | [research/46](../../../research/46-mako-controls.md) section 5 |
| A scene capture took an eye index and its post pass bound the eye's target | `RenderLevers` hooks `0x742C80` and `0x773D30`; the capture runs with index -1 | research/47 "Why a capture took an eye" |
| `SetView` was overwritten by the capture actor's transform | `bUpdateMatrices` false | research/47 "The capture's view" |
| The cockpit model was one colour: an enum value with a numeric suffix read past the enum's end (`LODGroup 27`) | Write the name and the suffix plus one in the package | research/47 "Fourth run" |
| The seated driver drew black | Lit by the vehicle's light environment, owned by the vehicle (`bOnlyOwnerSee`) | research/47 "Shepard in the seat" |
| The eye was inside the back of Shepard's head | The hips slide back until the head bone is 0.10 m behind the eye | research/47 "The seat further back" |
| An orange ring fixed at the view centre after leaving the Mako | The cockpit's aim mesh parks with the cockpit; Reticule panels hidden by alpha | research/44 "Persistent centre cursor after the Mako drop" |
| The extraction handle took a push up | Pull along the cabin's down (`pullDir`) | [research/48](../../../research/48-hull-assembly.md) section 27 |
| The repair button was too far away | The left stick's click on the held lever | research/48 section 27 "The repair on the throttle" |
| Test runs autosave into the career at the Mako drop | Back up the Save folder before a run | [gotcha](../gotchas/test-runs-overwrite-saves.md) |
| A new quad pair risks the runtime's swapchain limit | All swapchains through `plugin/Swapchains` | [gotcha](../gotchas/openxr-swapchain-limit.md) |
| `throttleDetent` has two defaults: 0.15 in `Settings` and the appended `[Mako]` block, 0.08 in the per-key append table and `View` | Not solved: a file that had `[Mako]` without the key gets 0.08 | [HandCalibration.cpp](../../../src/game/HandCalibration.cpp) lines 799 and 3492 |

# Code map

| part | file | scope |
|---|---|---|
| Lever, stick, jets target, pictures (rules) | [core/vr/MakoControls](../../../src/core/vr/MakoControls.cpp) | game-independent; compiled into every game's mod and the tests |
| Gunner rules | [core/vr/MakoGunner](../../../src/core/vr/MakoGunner.cpp) | game-independent; compiled for every game |
| Panel rules (button, handle, throttle click, lamps) | [core/vr/CockpitPanel](../../../src/core/vr/CockpitPanel.cpp) | game-independent; listed in LE1's sources and the tests |
| Hazard rules | [core/vr/CockpitHazard](../../../src/core/vr/CockpitHazard.cpp) | game-independent; compiled for every game |
| Seat choice (V1 / V2) | [core/vr/MakoSeat.hpp](../../../src/core/vr/MakoSeat.hpp) | game-independent, header only |
| Vehicle motion for the vignette | [core/vr/Comfort](../../../src/core/vr/Comfort.hpp) | game-independent |
| Aim, turret, shots, reticle ray | [game/Mako](../../../src/game/Mako.cpp) | LE1-only: bodies inside `#if defined(MEVR_GAME_LE1)`; the ledger lists it "run / out / out" |
| Physics seam, pad flags, jets, spin | [game/MakoControls](../../../src/game/MakoControls.cpp) | LE1-only (`MEVR_GAME_LE1` guards) |
| Gunner in the world | [game/MakoGunner](../../../src/game/MakoGunner.cpp) | LE1-only (`MEVR_GAME_LE1` guards) |
| Cockpit parts, ride, seat, HUD, hazard materials, V2 optics | [game/CockpitProbe](../../../src/game/CockpitProbe.cpp) | LE1-only |
| Panel in the game | [game/CockpitPanel](../../../src/game/CockpitPanel.cpp) | LE1-only |
| Screen reticle suppression | [game/ScreenReticle](../../../src/game/ScreenReticle.cpp) | see the ledger |
| Focus pause | [game/FocusPause](../../../src/game/FocusPause.cpp) | see the ledger |
| Pad options and flags | [game/GamepadMapping](../../../src/game/GamepadMapping.hpp) | shared; the Mako flags are set only in the Mako |
| Host: flat quads, reticle in mode 1, cockpit HUD | [plugin/HalcyonOverlays](../../../src/plugin/HalcyonOverlays.cpp) | LE1 host |
| Settings reader and appended blocks | [game/HandCalibration](../../../src/game/HandCalibration.cpp) | shared file; `[Mako]`, `[Cockpit]` |
| Menu rows | [plugin/MainMenuSettings](../../../src/plugin/MainMenuSettings.cpp) | LE1 host |

HostServices: v23 `setMakoControls` (service `WithMakoControls`, LE1 only in [the ledger](../../trilogy-parity-ledger.md));
the pad flags `kFlagMakoDrive` / `Lever` / `Stick` (bits 19-21) need host v23; `kFlagCockpitLook` travels with them; the
cockpit's HUD uses `hud_pieces::Pieces`; v35 carries the reticle request. Halcyon seams: head look in mode 1 while
`cockpitLook` is set ([HalcyonBridge.cpp](../../../src/plugin/HalcyonBridge.cpp)); `makoHeadAim = 0` stays. The ledger's
row G44 rates the whole category missing on ME2 (the Hammerhead's later pass) and n/a on ME3.

# Settings and defaults

All read by the mod (`game/HandCalibration`) from `ASI\LE1MEVR.ini` unless noted.

| section | key | default | menu row |
|---|---|---|---|
| Mako | handAim, turret, reticle, vignette, calibratedLine | 1 each | no |
| Mako | turretSeam | 3 (bits 1+2; 7 adds the pose seam) | no |
| Mako | moveSpeed / turnRate / aimRangeM | 150 / 20 / 200 | no |
| Mako | controls | 1 | yes (`mako_controls`) |
| Mako | grabRadiusM | 0.15 | no |
| Mako | stickTwist / stickTiltDeg / stickHeightM | 0 / 20 / 0.20 | no |
| Mako | stickTwistDeg / stickDeadZoneDeg / stickPitchDeadZoneDeg / stickInvert | 42 / 4 / 8 / 0 | no |
| Mako | tiltSteerMax / steerInPlaceBelow / spinRateDeg | 0.5 / 0.2 / 60 | no |
| Mako | leverReturns / throttleTravelM / throttleDetent | 1 / 0.055 / 0.15 | no |
| Mako | leverOffset / stickOffset | -0.362 -0.446 0.286 / 0.375 -0.415 0.314 | no |
| Mako | stickAimRate / rumble / cameraFollow | 90 / 0.12 / 1 | no |
| Mako | thrusterBurnScale / thrusterStabilize / thrusterPitchDeg / thrusterRollDeg / thrusterRateDeg | 0.4 / 1 / 20 / 15 / 60 | no |
| Mako | gunner / gunnerRangeM / gunnerHandbackS / gunnerCannon / gunnerCrowd / gunnerCrowdRadiusM | 1 / 120 / 3 / 1 / 3 / 6 | no |
| Cockpit | enabled | 1 | yes (`cockpit`) |
| Cockpit | script | `cockpit\cockpit.txt` | no |
| Cockpit | seatPosition | unset (top) | yes (top / bottom) |
| Cockpit | followCamera / seated / showTurret | 1 / 1 / 1 | no |
| Cockpit | hazard / hazardBelow / hazardPeriodS / hazardWash | 1 / 0.30 / 1.1 / 1 | no |
| Game | pauseUnfocused | 0 | no |
| Comfort | vignette, vignetteWhen | 0, turn | yes (Comfort) |

The panel's numbers (`holdS`, `pullTravelM`, `throttleClick` ...) are `cockpit_panel::Settings` defaults; which of them
`[Cockpit]` reads is [verify] (the header says game/HandCalibration and game/CockpitPanel read them; `ReadCockpit` reads
none). An old file holding the three pre-cockpit defaults (`throttleTravelM` 0.15, offsets +-0.20 -0.55 0.25) is moved
once to the cockpit's grips.

# Checks

| kind | what |
|---|---|
| Logs | `mako:` (driving, first aim, shots, the seams' heartbeat), `mako controls:` (placement, first grab, seam, stops, each jet burst), `mako gunner:`, `cockpit probe:`, `cockpit panel:`, `cockpit hazard:`, `[MEVR-MAKO]`, `[MEVR-HUD] the cockpit's HUD`, `[MEVR-CROSSHAIR]` |
| Snapshot | `"mod"."mako"` (with `"turret"`), `"mod"."makoControls"`, `"mod"."makoGunner"`, `"cockpitProbe"` (with `"panel"`); layers `makolever`, `makostick` |
| Console | `mevr vehicle` (`enter`, `party`, `guns`, `fire [cannon/mg] [s]`, `jets <s>`, `angvel ...`), `mevr cockpit ...` (`ride`, `hazard [on/off/auto/health <f>]`, `panel [repair/extract/ask/throttle]`, `optics`), `mevr gunner [wake <n>]`, `mevr reticles` |
| Unit tests | [tests/Main.cpp](../../../tests/Main.cpp) `TestMakoControls`, `TestMakoGunner`, `TestCockpitHazard`, `TestScreenReticle`, the `VehicleMotion` tests; [tests/CockpitPanelTests.cpp](../../../tests/CockpitPanelTests.cpp) |
| Offline | `mevr-mako-preview <folder>` ([tools/mako-preview](../../../tools/mako-preview/Main.cpp)): the lever's and stick's pictures |
| Runs without a save | [tools/mako-start.ps1](../../../tools/mako-start.ps1) (`open BIOA_UNC20`, `mevr vehicle party`; never `mevr vehicle enter` before the drop); [tools/make-drive-recording.py](../../../tools/make-drive-recording.py) drives by replay |
| Assets | [tools/cockpit/place.py](../../../tools/cockpit/place.py) `--script`, [panel_controls.py](../../../tools/cockpit/panel_controls.py), [hull_assemble.py](../../../tools/cockpit/hull_assemble.py) |

# Citations

- [research/44](../../../research/44-mako.md): the turret by the hand, the vignette, the reticle; the centre cursor.
- [research/46](../../../research/46-mako-controls.md): the physical controls, stage 1; the pad in the Mako (section 3).
- [research/47](../../../research/47-cockpit-in-game.md): the cockpit in the game, riding, the seat, hazard lights, the
  cannon over the cabin, the gunner, flying on the jets.
- [research/48](../../../research/48-hull-assembly.md): the hull; section 22 (the grips), section 27 (the panel controls).
- [docs/mako-cockpit-goal.md](../../mako-cockpit-goal.md): the cockpit's goal list.
- [docs/trilogy-parity-ledger.md](../../trilogy-parity-ledger.md): row G44 and the generated module table.
- Gotchas: [process-event-skips-opcode-natives](../gotchas/process-event-skips-opcode-natives.md),
  [test-runs-overwrite-saves](../gotchas/test-runs-overwrite-saves.md),
  [openxr-swapchain-limit](../gotchas/openxr-swapchain-limit.md).
