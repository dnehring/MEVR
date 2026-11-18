---
type: System
title: ME1 weapons and sights
description: What ME1 does with the gun in the hand - hand aim, the support grip, red dots, the sniper scope and its zoom, ADS, the alignment assist, gun kick, the level camera and the vent - as numbered contracts.
tags: [me1, weapons, sights, scope, zoom, ads, catalog]
status: stable
timestamp: 2026-11-18T07:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T07:00:00Z }
game: LE1
---

# What the player gets

- The gun sits in the right hand and points where the controller points. Shots leave the muzzle along the barrel and land
  on what the sight line hits.
- The left hand on the gun steadies it; with both hands on a long gun at the shoulder the game fires "aimed" (LT).
- Every gun has a sight. Pistol, shotgun and assault rifle: a red dot that shows to one eye and stays on the target.
  Sniper rifle: a scope.
- The scope's picture appears only when the rifle is brought up to the eye (or into the calibrated shoulder). Lowered,
  the player sees the gun's own scope model and no picture.
- Raised, the scoped rifle slides a little so its eyepiece comes to the eye; the picture is a magnified disc with a
  duplex reticle, 3.5x on most sniper rifles, and a flick of the right stick forward steps to the weapon's second
  magnification (7x) where it has one.
- Nothing zooms the player's view. There is no scope camera, no change of field of view, no button for zoom.
- Shots do not tilt the world: the recoil is a kick of the gun model and a pulse in the hand.
- A hot gun is vented by racking its receiver with the left hand.

# Contracts

Gun units are the model's (about centimetres at 100 units per metre). "Raised weight" is `ads::Approach x
ads::DistanceWeight`, 0..1.

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| WPN-01 | Shots follow the gun as drawn: `BioPlayerController.GetAdjustedAimFor` (and the weapon's `GetAdjustedAim`) is answered with the barrel's direction, the shot's start moved to the muzzle. Without a verified barrel the right controller's aim ray is used. | start counts "at the muzzle" within 1 unit; a muzzle poked through a wall pulls the start back 2 units (checked up to 250 units from the pawn's start) | on foot (`head_tracking::OnFoot`), a verified ranged weapon in the right hand, arm IK pointing the gun (`arm_ik::RightHandAimsGun`); other pawns' aim queries skipped; no view anchor: the game's aim stays | [HandAim.cpp](../../../src/game/HandAim.cpp) `OnCallFunction`, `WhyNoBarrel`, `HandRotation` | headset 2026-10-21 (research/18) |
| WPN-02 | Shots converge on the sight: each tick the sight line is traced in the world (`Actor.Trace`) and the shot leaves the muzzle toward that hit, so it lands on the dot at any distance. | trace range 30,000 units; a hit less than 20 units ahead of the muzzle keeps the barrel's line; `[Sights] converge = 1`, `zeroM = 0` (sight line parallel to the barrel) | a sight published for the held gun; `Actor.Trace` found | [Sights.cpp](../../../src/game/Sights.cpp) `kConvergeRangeUnits`, `kMinAheadUnits`; [Sights.hpp](../../../src/core/vr/Sights.hpp) `ConvergedShot` | headset; log `hand aim: shot N:` |
| WPN-03 | The aim ray published to the host (the hand reticle) is the muzzle and the barrel in tracking space; in the Mako it is the right controller's ray. | - | hand aim on; else no ray ("none (why)") | [HandAim.cpp](../../../src/game/HandAim.cpp) `AimRay` | fake headset |
| WPN-04 | There is no head aim. The head drives the view only. | deploy writes `combatHeadAim = 0`, `makoHeadAim = 0`; the baseline pins them | always | [HalcyonBridge.cpp](../../../src/plugin/HalcyonBridge.cpp) `ModernBaseline` | headset |
| WPN-05 | With a saved calibration the barrel follows `grip x trim` (not the aim pose) and the gun's sight is moved onto the calibrated eye-to-target line. A long gun without a rifle calibration borrows the pistol's grip frame. A stance whose three aims spread more than 10 degrees is not used. | capture spread limits: rifle 4.5 degrees, pistol 3; use limit 10 degrees | `[Calibration] use = 1` and a `[Calibration.<controller key>]` section | [BodyCalibration.cpp](../../../src/game/BodyCalibration.cpp); [Calibration.hpp](../../../src/core/vr/Calibration.hpp) | headset (research/25) |
| WPN-06 | Support grip: a NEW left grip squeeze with the left hand in the gun's grab zone takes the gun with both hands. It lets go when the grip opens or the hand leaves the release distance; a hold lost by distance gives nothing until the grip opens. Holding the grip while moving in never grabs. | zone = capsule along the bore from 0.10 m ahead of the right hand to 0.10 m past the muzzle plus a ball at the foregrip, radius 0.18 m; release 0.26 m; grip latch on 0.6, off 0.4; hold blends in over 0.10 s | `[Controls] supportGrip = 1`, `supportZone = capsule`; arm IK asks for the grip; not while calibrating | [SupportGrip.cpp](../../../src/game/SupportGrip.cpp) `Tick`; [GamepadMapping.cpp](../../../src/game/GamepadMapping.cpp) `UpdateSupportGrip`; [TwoHand.hpp](../../../src/core/ik/TwoHand.hpp) `MakeSupportZone` | headset 2026-10-22 |
| WPN-07 | Taking hold anywhere in the zone does not swing the gun: the line between the hands is captured at the grab (`CapturedSpan`) and the barrel turns with that line from then on. A foregrip less than 10 units along the barrel is too short to aim with: the right hand keeps aiming. | `kMinAlongForTwoHandAim` 10 units; foregrip settles after 5 still ticks within 0.5 units | hold in progress | [SupportGrip.cpp](../../../src/game/SupportGrip.cpp); [TwoHand.hpp](../../../src/core/ik/TwoHand.hpp) `CapturedSpan` | headset |
| WPN-08 | With a calibration, a left grip within reach of the calibrated foregrip point takes the gun too, and two-hand aim keeps the calibrated hands' line. | the nearer of the two distances is used; the calibrated line only when the grab is within 0.08 m of the calibrated place | long guns only (not a pistol), both grips tracked | [SupportGrip.cpp](../../../src/game/SupportGrip.cpp) `CalibratedForegripDistance` | [verify] |
| WPN-09 | Aimed fire: while the support grip holds AND the right grip is at the shoulder, the game sees LT fully pulled (255). A sniper rifle never gets LT from the grip (its aimed mode is the game's scope camera). | shoulder zone below the eyes: drop 0.05..0.35 m, side up to 0.30 m, forward -0.15..0.40 m, hysteresis 0.05 m; calibrated: around the stock point, tolerance 0.12 m | `[Controls] aimAtShoulder = 1`, `sniperAims = 0` | [SupportGrip.cpp](../../../src/game/SupportGrip.cpp) `AtShoulder`, `options.supportAims`; [TwoHand.hpp](../../../src/core/ik/TwoHand.hpp) `ShoulderZone` | headset; log `support grip: aimed fire on` |
| WPN-10 | Which sight each weapon type gets: pistol dot, shotgun dot, assault rifle dot, sniper rifle `scope auto` (the held weapon's own magnification). `none`, `dot`, `holo`, `scope`, `scope N` (1-12) are the choices per type. | defaults in a fresh file: `pistol = dot`, `shotgun = dot`, `assaultRifle = dot`, `sniperRifle = scope auto`; an old `assaultRifle = scope 1.5` moved back to `dot` once (marker `ads`) | `[Sights] enabled = 1`; an unknown weapon type gets none | [Sights.hpp](../../../src/core/vr/Sights.hpp) `Settings::guns`, `ParseGun`, `MigrateAssaultRifle`; [HandCalibration.cpp](../../../src/game/HandCalibration.cpp) | headset |
| WPN-11 | Where the sight sits: on the model's own sight bone, followed live every tick (guns unfold for a second after a draw); else the bones' top; else a typical place from the gun's length. The game's four rifle meshes have measured optics. | `OpticFor`: SNPa `Scope_Eye` glass 13.49 back, -1.74 up, lens 2.10; SNPb `Scope_Rear` 1.44, -0.31, 1.70; ASLa `Scope` 7.44, 1.05, 2.15; ASLb `Scope` 1.94, 0, 1.70; model re-read every 0.25 s; bone still = within 0.3 units for 0.5 s | `<type>Place = auto` | [Sights.cpp](../../../src/core/vr/Sights.cpp) `kOptics`, `SightBone`, `ModelTop`, `AutoPlace`; [Sights.cpp](../../../src/game/Sights.cpp) `MeasureModel` | headset; log `sights: the <gun>'s sight sits on its '<bone>' bone` |
| WPN-12 | Red dot / holo: the reticle is a quad 50 m out along the sight line (collimated), shown to ONE eye only: the eye whose line along the sight passes within the eye box of the window's centre. A faint ring marks the window. | dot 0.25 degrees, holo ring 1.6 degrees, colour ff3020; window radius 3 units; eye box 6 units; eye between -0.08 m and 1.0 m behind the window; a 120 ms dropout is bridged (`EyeHold`) | a dot or holo published; `frame = 1` for the ring; the ring shows only with a dot unless raised | [SightLayers.cpp](../../../src/plugin/SightLayers.cpp) `kDotDistance`, `kDotMinAlong`, `kMaxAlong`; [Sights.hpp](../../../src/core/vr/Sights.hpp) `AimingEye`, `EyeHold` | headset 2026-11-06 |
| WPN-13 | Raised or at the shoulder, the nearer eye gets the dot however far it is off the line, and the dot is kept inside its window. | raised = raised weight above 0.20 (off below 0.08); eye switch only when the other is 0.02 m nearer; dot clamped to 0.8 of the window radius; range to 1.2 m behind | flags `kRaised`, `kShouldered` | [SightLayers.cpp](../../../src/plugin/SightLayers.cpp) `kEyeSwitchM`, `kDotInWindow`; [Sights.hpp](../../../src/core/vr/Sights.hpp) `NearestEye`, `ReticleInWindow`; [Sights.cpp](../../../src/game/Sights.cpp) `kLensRaisedOn` | headset 2026-11-06 (research/21 sections 21-22) |
| WPN-14 | A dot on a solid sight housing (the assault rifles' boxes) is seen through: the dot's eye gets the other eye's image at 1x on a disc at the window, behind the ring and the dot. | `dotWindow = 1`; eye 0.02..1.2 m behind the window | a model with a known optic, auto placement, not a scope | [Sights.cpp](../../../src/game/Sights.cpp) `Current` (`kWindow`); [Sights.hpp](../../../src/core/vr/Sights.hpp) `WindowView` | headset 2026-11-05 (research/21 section 17) |
| WPN-15 | Dots and holos never slide the gun sideways toward the eye. | `assist.dotMaxShiftM = 0` (0.03 migrated once, marker `dotWindow`); a holo may still be pulled back 5 cm | - | [Sights.hpp](../../../src/core/vr/Sights.hpp) `Assist`; [Sights.cpp](../../../src/game/Sights.cpp) `StepAssist`, `PullLimit` | headset 2026-11-05 |
| WPN-16 | A scope's picture shows ONLY under ADS. Below ADS the mod publishes no sight for a scoped gun: no picture, no reticle disc, the model's own lens visible, no scope capture, the floating ring reticle stays. | `ads = 1`; with `ads = 0` the picture shows whenever an eye is behind the eyepiece within `scopeMaxM` 0.70 m | sight kind Scope | [Sights.cpp](../../../src/game/Sights.cpp) `Current` (`gAdsGate && !gAds`: kind None) | headset 2026-10-27 (research/21 section 15) |
| WPN-17 | ADS comes on when the centre eye is inside Overlord's entry corridor against the gun (measured WITHOUT the assist's own move) and within reach of the sight, for the dwell; it goes off after the same dwell outside the wider exit corridor or reach. | entry: 0.15..1.5 m behind the muzzle, 0.09 m beside the bore, -0.025..+0.16 m above it, head within 30 degrees of the barrel; exit: 0.10..1.7 m, 0.13 m, -0.06..+0.22 m, 40 degrees; far depth extended by sight-to-muzzle + 0.60 - 1.5 m; dwell 0.10 s each way; reach `adsReachM` 0.55 m on, `adsReachExitM` 0.80 m off (at least 0.05 m more than on) | scopes only (`gAdsGate = scope && ads`); a dot is never gated by ADS | [Ads.hpp](../../../src/core/vr/Ads.hpp) `kEntry`, `kExit`, `kDwellS`, `Inside`, `Policy`; [Sights.cpp](../../../src/game/Sights.cpp) `StepAssist`, `ReachExit` | unit test (tests/Main.cpp); headset 2026-11-06 |
| WPN-18 | Shoulder ADS: a rifle or shotgun with the right grip in the calibrated shoulder zone and the head along the barrel is ADS and fully raised at once, whatever the eye's distance. | head within 25 degrees of the barrel to come on, 35 to stay; zone = `ShoulderZoneAround(stock, 0.12 m)`; raised weight forced to 1 | `shoulderAds = 1`; a calibration with a stock point; never a pistol; the dwell still applies to the latch | [Ads.hpp](../../../src/core/vr/Ads.hpp) `AtShoulder`; [Sights.cpp](../../../src/game/Sights.cpp) `StepAssist` | headset 2026-11-06 (research/21 section 20) |
| WPN-19 | The scope's picture is a virtual eyepiece: a disc at the eyepiece that looks a fixed angle across from wherever the eye is, the magnification true across it, a dark rim, and a shadow crescent as the eye leaves the line. It is drawn for the aiming eye and magnifies the OTHER eye's image. | `scopeFieldDeg` 22 (with the pull: what the model's eyepiece subtends at the relief, never smaller); `scopeEyeBoxM` 0.05 m shades half; eye box for choosing the eye max(2 lens radii, 2 x 0.05 m); eye not nearer than 0.03 m; under ADS the host's range is reach exit + 0.10 m (0.90 m); `source = other`; `focusM` 40; `resolution` 768 | host view bit `kViewScope` | [Sights.hpp](../../../src/core/vr/Sights.hpp) `VirtualView`, `PictureRadius`, `PulledFieldDeg`; [SightLayers.cpp](../../../src/plugin/SightLayers.cpp) | headset 2026-10-25 (research/21 section 13) |
| WPN-20 | Zoom is the held weapon's own: the scoped entries of its `Stats.AimModes`, each `ZoomFOV` against the 70 degree combat FOV. A sniper rifle shows the first scoped mode. A fixed `scope N` in the file overrides it. | zoom = tan(35 degrees) / tan(ZoomFOV / 2), clamped 1..12: 22.6 degrees = 3.5x, 11.4 degrees = 7.0x; at most 4 modes; fallback when none is read: 4.0x sniper, 1.5x any other type | read once per held weapon; `sniperRifle = scope auto` (zoom 0) | [Sights.cpp](../../../src/game/Sights.cpp) `ReadGameZoom`, `ZoomFor`, `FallbackZoom`; [Sights.hpp](../../../src/core/vr/Sights.hpp) `ZoomFromFov`, `GameZoom` | fake headset 2026-11-03 (research/21 section 16) |
| WPN-21 | Stepping zoom: under ADS, a flick of the right thumbstick forward steps to the weapon's next magnification, back to the one before; a detent pulse in the right hand. The level is kept for that weapon. | stick Y past 0.7 to step, back inside 0.3 to re-arm; pulse 0.03 s at 0.35 | `zoomStick = 1`; the weapon has more than one scoped mode; ADS on; zoom from the game (not `scope N`). Outside ADS the flick is re-armed and does nothing | [Sights.cpp](../../../src/game/Sights.cpp) `ZoomFor`; [Sights.hpp](../../../src/core/vr/Sights.hpp) `ZoomFlick` | unit test; headset [verify] |
| WPN-22 | The game's own zoom never shows. `bIsZoomed` is never set where the camera can see it; a scoped shot gets the zoomed aim error by setting it for ONE native aim-error call. The right stick click (the game's R3 `ChangeMagnification`) still reaches the game on LE1 and does nothing in Halcyon's view. | `GetPlayerAimError` post-handler: accuracy recovered from the unzoomed answers for 0 and 1, then the zoomed answer for it; journalled `Weapon.bIsZoomed (one aim-error call)` | `aimedFire = 1`; the scope's picture up for the held weapon (and ADS on); skipped when the weapon is already zoomed (the support grip's LT) | [Sights.cpp](../../../src/game/Sights.cpp) `OnAimError`, `SetHostView`; [Weapon.cpp](../../../src/game/Weapon.cpp) `ZoomedAimError` | fake headset; log `sights: first scoped shot with aimed fire` |
| WPN-23 | Pull to the eye: raised, a scoped gun slides back along its sight line until the model's eyepiece is the relief in front of the aiming eye; a gun pulled into the shoulder is pushed forward so the eyepiece stays the relief ahead. | relief `scopeReliefM` 0.12 m, at least near plane (10 units) + 0.02 m; limits: holo 0.05 m, scope under 3x 0.10 m, scope `scopePullMaxM` 0.15 m, scope under ADS `scopeAdsPullMaxM` 0.35 m; push `scopeAdsPushM` 0.15 m; spring 10/s at 0.25 m/s, under ADS 24/s at 0.9 m/s | assist enabled; weight = raised weight (angle full within 5 degrees, none at 45; distance full within 0.30 m, none at 0.60 m; under ADS full within 0.50, none at 0.70) | [Sights.cpp](../../../src/game/Sights.cpp) `PullLimit`, `StepAssist`; [Ads.hpp](../../../src/core/vr/Ads.hpp) `Approach`, `DistanceWeight`, `kFastSpring`; [Sights.hpp](../../../src/core/vr/Sights.hpp) `AssistTarget`, `ReliefFor` | headset 2026-11-06 (research/21 sections 14, 19) |
| WPN-24 | Alignment assist: raised, a scoped gun also slides sideways so the sight line runs through the aiming eye. The move is arm IK's `gunShift`; the muzzle, shots and sight follow the gun as drawn. | at most `assistMaxM` 0.10 m sideways, strength 1.0; eyes taken 0.032 m either side of the head's centre; logged on from weight 0.10, off below 0.02 | scopes only in effect (WPN-15); hands within `[Hands] reachNearFaceM` of the eyes are not reach-scaled | [Sights.hpp](../../../src/core/vr/Sights.hpp) `Assist`, `AssistSpring`; [Sights.cpp](../../../src/game/Sights.cpp) `TakeGunShift` | headset (research/21 section 8) |
| WPN-25 | The scope model's lens part is hidden while the picture is up and whenever the scoped gun is raised; shown again on lowering, a switch, a holster or unload. | raised on 0.20, off 0.08; parts back 150 ms after the picture goes; `sniperRifleHide = auto` (SNPa: `Scope_Lens`) | scope kind; never the root, the sight's own bone or its ancestors | [ScopeParts.cpp](../../../src/game/ScopeParts.cpp) `kUnhideAfterMs`; [Sights.hpp](../../../src/core/vr/Sights.hpp) `HideList`, `PartsToHide` | headset 2026-11-06 |
| WPN-26 | Scope capture: the picture comes from a real zoomed render (a `SceneCapture2DActor` with its own render target along the sight line), not the other eye's image enlarged; the eye copy is the fallback. The reticle stays collimated. | `scopeCapture = 1`; target 1024 px FloatRGB; rate 150; field = 2 atan(1.25 tan(11 degrees) / zoom) (margin 0.25); near plane 20 units past the muzzle; kept enabled 0.5 s after the eye leaves; player's post chain on (`scopeCapturePost = 1`) | enabled only while an eye is behind a scope (host `sightView`); forgotten on a level change | [ScopeCapture.cpp](../../../src/game/ScopeCapture.cpp); [ScopeCapture.hpp](../../../src/core/vr/ScopeCapture.hpp) `Settings`; [ScopeCaptureSource.cpp](../../../src/plugin/ScopeCaptureSource.cpp) | fake headset 2026-11-04 (research/39 section 12) |
| WPN-27 | The scope's reticle is the duplex (mil-dot and dot selectable). `reticle = game` (the game's own sniper view art) cannot load and falls back to the duplex. | `reticle = duplex`, `reticleColor = ff2020` | - | [Sights.hpp](../../../src/core/vr/Sights.hpp) `ScopeReticle`; [SightPictures.cpp](../../../src/plugin/SightPictures.cpp) | headset; research/21 section 16 |
| WPN-28 | While an eye looks through a sight the floating hand reticle (the ring on the aim ray) hides. | `replaceRing = 1` | host view bits left/right | [Sights.hpp](../../../src/core/vr/Sights.hpp) `kReplaceRing` | headset |
| WPN-29 | Gun kick: each shot turns the gun model muzzle-up about the hand and moves it back along the barrel, springing back; automatic fire stacks under a soft cap. Visual only: the aim ray, shots, ADS, the assist and the scope capture read the gun with the kick taken out; only the sight's published centre rides the kicked model. | 0.8..4.0 degrees and 0.4..2.2 cm by recoil / 3.0 degrees; spring omega 45 (peak at 22 ms, a tenth by about 118 ms); cap 6 degrees, 3 cm, knee 0.6; half with both hands; back motion faded by the raised weight; `gunKick = 1`, `gunKickScale` 1 (0..2) | the `SetWeaponRecoil` handler's shot; first person arm IK | [GunKick.hpp](../../../src/core/ik/GunKick.hpp) `Tuning`; [GunKick.cpp](../../../src/game/GunKick.cpp); [Weapon.cpp](../../../src/game/Weapon.cpp) `ToDrawn` | unit test; fake headset (research/24 section 8) |
| WPN-30 | Level camera: after each `PlayerTick` the controller's pitch and roll are put back to 0, so recoil never tilts the world; aim assist's zoom-snap target is dropped. The recoil is a hand pulse instead. | `levelCamera = 1`; `recoilHaptic` 0.5 | game mode 0, head-tracked view, hand aim on, no head aim, a pawn; camera modes Explore, Combat, TightAim and the sprint modes only. Not in cover, on a ladder, the sniper scope camera, the Mako, cinematics | [ViewLevel.hpp](../../../src/core/vr/ViewLevel.hpp) `Decide`, `CameraModeAllows`; [ViewLevel.cpp](../../../src/game/ViewLevel.cpp) | unit test; headset (research/24) |
| WPN-31 | Aim assist off: the game's view friction and adhesion are zeroed while the hand aims, so the stick's turn is never slowed or bent near enemies. | `aimAssist = 0` (1 keeps the game's) | as WPN-30 but cover included; not the sniper scope camera, the Mako, photo or cinematic modes | [ViewLevel.hpp](../../../src/core/vr/ViewLevel.hpp) `DecideAssist`, `CameraModeRemovesAssist` | unit test (research/24 section 7) |
| WPN-32 | Vent: with a HOT gun drawn, the left grip squeezed in the receiver zone, racked back fast and released, cools the gun at once (`m_fCurrentHeat = 0`, `m_bOverheated = false`, the game's cool-down effect). Both hands pulse. | zone from the right hand: 0.06 m behind, 0.14 m ahead, 0.14 m above the bore, 0.03 m below, 0.09 m either side; heat at least 0.3 of 1.0; release speed 1.0 m/s with 0.3 of it backward; travel 0.05 m; squeeze held at most 1.5 s; cooldown 4.0 s; grip latch 0.6 / 0.4 | `[Vent] enabled = 1`; weapon drawn; the support grip not held; the grenades did not claim the squeeze; a cool gun's receiver is the support grip | [Vent.hpp](../../../src/core/ik/Vent.hpp) `Zone`, `Params`; [Vent.cpp](../../../src/game/Vent.cpp) `Claim`, `Tick`, `MasksSupportZone` | fake headset (`mevr vent`), headset [verify the heat scale] |
| WPN-33 | The visible rack: while the hand holds a hot gun's receiver a part of the gun's own model follows it back and springs home at the release. | part by type: pistol `Barrel_Upper_front`, assault rifle `Scope`, sniper none; travel 3 units by default; spring 9 Hz, damping 0.5 | `[Vent] animate = 1` | [Vent.hpp](../../../src/core/ik/Vent.hpp) `Rack`, `AutoPartList`; [Vent.cpp](../../../src/game/Vent.cpp) | fake headset; headset [verify] (research/45 section 6) |
| WPN-34 | The weapon panel (silhouette, heat, grenades) rides over the gun's receiver and leaves for the HUD panel while the gun is raised, so the sights stay clear. | leaves at raised weight above 0.55, back under 0.35; 0.15 m back, 0.04 m up | `[Hud] gunStatus = 1` | [Sights.cpp](../../../src/game/Sights.cpp) `RaisedWeight`; [HandCalibration.hpp](../../../src/game/HandCalibration.hpp) `GetHud` | headset 2026-11-05 (research/41) |

# Arbitration

| input | claimants, in order | the loser sees |
|---|---|---|
| left grip squeeze near the gun | grenade slot (if the hand is in it) > vent (hot gun, hand in the receiver zone, grip not already holding) > support grip | while the vent masks the zone the pad mapping sees "no gun": the squeeze counts as away from the gun and the mod gets it. A held support grip is never let go for a vent |
| right stick Y | zoom flick (only under ADS with a second magnification) and the game's stick at once | the game still receives stick Y; pitch is off in VR (`decoupledPitch`), so nothing else moves |
| right stick click | the mod's 0.3 s hold (power wheel); the game's R3 | R3 still reaches ME1 (its `ChangeMagnification` acts only in the game's own zoom, which never shows) |
| LT | the support grip at the shoulder forces 255; the left trigger itself is the squad's (LB path) by default | a sniper rifle gets no LT from the grip |
| the gun's place | controller > calibration trim > support grip's two-hand line > gun kick > assist shift | the kick is taken out of every reading; the assist never feeds its own gate (ADS is measured without it) |

# Problems ME1 already solved

| problem | fix | where |
|---|---|---|
| Setting `bIsZoomed` for aimed fire switched the camera to the scope view | set it for one aim-error call only, never where the camera can see it | [research/18](../../../research/18-camera-and-hands.md), [research/21](../../../research/21-sights-and-scopes.md) |
| The scope's picture showed with the rifle at the hip and hid the model | ADS gate: Overlord's corridor, dwell and reach; no sight published below it | research/21 section 15 |
| A headset cannot bring the eye to an 8 cm eye relief | the virtual eyepiece (a fixed apparent field) and the pull to the eye | research/21 sections 13-14 |
| The eye passed the eyepiece when the rifle was pulled into the shoulder | the push away up to 0.15 m; shoulder ADS from the calibrated stock point | research/21 sections 19-20 |
| A red dot's gun slid 3 cm each time the hold crossed the corridor's edge | dots never slide (`dotMaxShiftM = 0`) | research/21 section 17 |
| The assault rifle's solid sight box hid the target behind the dot | the window: the other eye's image at 1x on a disc | research/21 section 17 |
| The dot's outline showed without its dot; the dot floated beside the housing | nearer eye when raised; the dot kept inside the window | research/21 sections 21-22 |
| Three sight swapchains were refused and the player had no dot all session | one swapchain, three `imageRect`s; every swapchain through `plugin/Swapchains` | [gotcha](../gotchas/openxr-swapchain-limit.md), research/21 section 18 |
| `BonePose::TakeRequest` never copied `gunShift`: the assist never reached the pose | fixed 2026-10-31 | [research/43](../../../research/43-fingers.md) |
| The game's sniper art textures are freed and cannot be loaded | `reticle = duplex` is the default | research/21 section 16 |
| Recoil tilted the head-tracked world (+30 degrees after 204 shots) | the level camera hold after `PlayerTick` | [research/24](../../../research/24-view-level.md) |
| Aim assist slowed and bent the stick's turn near enemies | friction and adhesion zeroed under hand aim | research/24 section 7 |
| A kicked gun model moved the aim ray and the sights | `weapon::Current`, `BoneWorld`, `ModelInGunFrame` take the kick out; `ToDrawn` puts only the sight's centre back | research/24 section 8 |
| The grip opened the weapon wheel whenever the hand was not right at the foregrip | the capsule zone; a left grip away from the gun is never LB | [research/19](../../../research/19-body-holsters.md) |
| Taking hold away from the foregrip swung the gun | the grab's line captured (`CapturedSpan`) | research/19 |
| Scope capture's black speckle in dark areas | known issue, kept; `scopeCapturePost = 0` removes it and the grade | [research/39](../../../research/39-scope-capture.md) section 12 |
| No game function cools a weapon | the vent writes the heat fields and calls `DoCoolDownEffect` | [research/45](../../../research/45-vent.md) |
| The skip handler read FFrame's `Node` at the wrong offset | FFrame is packed on 4 bytes | [gotcha](../gotchas/fframe-packed-on-four-bytes.md) |

# Code map

| part | file | scope |
|---|---|---|
| ADS corridor, dwell, weights | [core/vr/Ads.hpp](../../../src/core/vr/Ads.hpp) | game-independent (GPL, from Overlord) |
| sight settings, geometry, zoom, assist | [core/vr/Sights.hpp](../../../src/core/vr/Sights.hpp) | game-independent |
| scope capture rules | [core/vr/ScopeCapture.hpp](../../../src/core/vr/ScopeCapture.hpp) | game-independent |
| gun kick, vent, two-hand rules | [core/ik/GunKick.hpp](../../../src/core/ik/GunKick.hpp), [Vent.hpp](../../../src/core/ik/Vent.hpp), [TwoHand.hpp](../../../src/core/ik/TwoHand.hpp) | game-independent |
| level camera rules | [core/vr/ViewLevel.hpp](../../../src/core/vr/ViewLevel.hpp) | game-independent; camera mode names are LE1 class names |
| sights (mod) | [game/Sights.cpp](../../../src/game/Sights.cpp) | every game; `ReadGameZoom` reads `Stats.AimModes` under `MEVR_GAME_LE1` only |
| hand aim, weapon, gun kick, view level | [game/HandAim.cpp](../../../src/game/HandAim.cpp), [Weapon.cpp](../../../src/game/Weapon.cpp), [GunKick.cpp](../../../src/game/GunKick.cpp), [ViewLevel.cpp](../../../src/game/ViewLevel.cpp) | every game, per-game branches inside |
| support grip | [game/SupportGrip.cpp](../../../src/game/SupportGrip.cpp) | every game, no guards |
| scope parts | [game/ScopeParts.cpp](../../../src/game/ScopeParts.cpp) | every game |
| scope capture (mod) | [game/ScopeCapture.cpp](../../../src/game/ScopeCapture.cpp) | compiled for every game, runs on LE1 only (ledger: `out` on LE2, LE3; `setScopeCapture` is LE1's `WithScopeCapture`) |
| vent | [game/Vent.cpp](../../../src/game/Vent.cpp) | runs on LE1 only (the sequels have thermal clips) |
| host: sight quads, pictures | [plugin/SightLayers.cpp](../../../src/plugin/SightLayers.cpp), [SightPictures.cpp](../../../src/plugin/SightPictures.cpp) | all three hosts (`WithSights`) |
| HostServices | `setSight`, `sightView` (v13), `setScopeCapture` (v20), `setSupportGrip` | see [the ledger](../../trilogy-parity-ledger.md) rows G02-G10, G41 |

# Settings and defaults

| section | key | default | read by | menu row |
|---|---|---|---|---|
| Sights | `enabled` | 1 | mod | HUD & sights [verify] |
| Sights | `pistol`, `shotgun`, `assaultRifle`, `sniperRifle` | dot, dot, dot, scope auto | mod | [verify] |
| Sights | `dotSizeDeg`, `ringSizeDeg`, `windowUnits`, `eyeBox`, `frame`, `dotWindow` | 0.25, 1.6, 3, 6, 1, 1 | mod | file |
| Sights | `converge`, `zeroM` | 1, 0 | mod | file |
| Sights | `ads`, `adsReachM`, `adsReachExitM`, `shoulderAds` | 1, 0.55, 0.80, 1 | mod | file |
| Sights | `scopeFieldDeg`, `scopeEyeBoxM`, `scopeMaxM` | 22, 0.05, 0.70 | mod (host draws) | file |
| Sights | `scopeReliefM`, `scopePullMaxM`, `scopeAdsPullMaxM`, `scopeAdsPushM` | 0.12, 0.15, 0.35, 0.15 | mod | file |
| Sights | `assist`, `assistStrength`, `assistMaxM`, `assistDotMaxM` | 1, 1, 0.10, 0 | mod | file |
| Sights | `zoomStick`, `reticle`, `aimedFire`, `replaceRing`, `source` | 1, duplex, 1, 1, other | mod | file |
| Sights | `scopeCapture`, `scopeCaptureSize`, `scopeCaptureRate`, `scopeCapturePost` | 1, 1024, 150, 1 | mod | file |
| Controls | `supportGrip`, `supportZone`, `supportAcquireM`, `supportReleaseM`, `supportFrontM`, `supportPastMuzzleM` | 1, capsule, 0.18, 0.26, 0.10, 0.10 | mod | file |
| Controls | `sniperAims`, `aimAtShoulder` | 0, 1 | mod | file |
| Hands | `levelCamera`, `recoilHaptic`, `aimAssist`, `gunKick`, `gunKickScale` | 1, 0.5, 0, 1, 1 | mod | Controls > Weapon drawn (gun kick) |
| Vent | `enabled`, `ventCooldownS`, `ventMinHeat`, `ventMaxHeat`, `ventSpeedMS`, `ventTravelM`, `animate` | 1, 4, 0.3, 1.0, 1, 0.05, 1 | mod | file |

# Checks

- Logs: `hand aim:`, `support grip:`, `sights:` (`ADS on/off (...)`, `the scope zooms in to`, `first scoped shot`),
  `[MEVR-SIGHT]`, `scope capture:`, `gun kick:`, `view level:`, `aim assist:`, `vent:`.
- Snapshot keys: `"mod"."sights"` (`ads`, `window`, `pictureSource`), `"mod"."scopeCapture"`, `"mod"."gunKick"`,
  `"mod"."vent"`, `supportGrip`.
- Console: `mevr kick [recoilDeg] [shots] [ms]`, `mevr vent heat <f> [hold] | rack | part`.
- Unit tests: `tests/Main.cpp` (ADS, sights, view level, gun kick, support grip, pad mapping),
  [CalibrationWeaponTests.cpp](../../../tests/CalibrationWeaponTests.cpp).
- Offline look: `mevr-sight-preview` (PNGs, `scope-capture-*.png`).

# Citations

- [research/18](../../../research/18-camera-and-hands.md), [research/19](../../../research/19-body-holsters.md),
  [research/21](../../../research/21-sights-and-scopes.md), [research/24](../../../research/24-view-level.md),
  [research/25](../../../research/25-calibration.md), [research/39](../../../research/39-scope-capture.md),
  [research/41](../../../research/41-hud-pieces.md), [research/45](../../../research/45-vent.md).
- Where CLAUDE.md and the code differ: CLAUDE.md says `kShouldered` "widens the host's dot box to 0.10 m"; the code has no
  0.10 m box, it takes the nearer eye without a box when raised or shouldered (WPN-13). CLAUDE.md describes the assist's
  dot limit as "at most `assistDotMaxM` 0.03" in one sentence and 0 in a later one; the code is 0. `ads::Reach`'s struct
  defaults (0.40 / 0.50 m) are not what runs: the settings' 0.55 / 0.80 m are passed in.
