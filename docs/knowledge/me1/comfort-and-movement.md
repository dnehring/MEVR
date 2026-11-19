---
type: System
title: Comfort and movement (ME1)
description: How the player turns, walks, sprints, crouches and takes cover in ME1 in VR, and what the mod takes away from the game's third-person movement (snapping, shakes, ramps, the focus freeze), as numbered contracts.
tags: [me1, comfort, movement, turning, sprint, crouch, cover, catalog]
status: stable
timestamp: 2026-11-18T07:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T07:00:00Z }
game: LE1
---

# What the player gets

- The right stick turns. By default it is the game's own smooth turn; the menu's Snap turns 30, 45 or 60 degrees per
  flick. Nothing else about the turn is a setting the menu shows.
- An optional comfort vignette darkens the edges of the view while the stick turns the player (or also while it walks
  them), on foot and in the Mako.
- The left stick walks toward where the player looks, or toward where the left hand points.
- Walking answers the stick at once: half a push walks, a full push runs, letting go stops. The game's start step, stop
  slide and skid are gone, and so is its 0.67 s speed ramp.
- A held (the game's sprint button) is a faster run with the gun still up, limited by a stamina of the mod's own. The
  game's storm posture and its camera never come.
- The view does not shake when a gun fires, when the Mako jumps or boosts, or with Shepard's run.
- Crouching for real crouches Shepard. L3 still toggles the crouch in between.
- The game never snaps the player into cover. Ducking the head behind something near gives the game's own cover
  protection against shooters the head is hidden from.
- The game keeps running when its window loses focus (it used to freeze everything but the player).
- ME1 has no vault, climb or mantle in VR: the traversal module is ME2's and ME3's.

# Contracts

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| MOV-01 | By default the right stick's X turns the player with the game's own turn (its per-camera-mode rate, acceleration and dead zone). The mod only logs that rate once per camera mode. | `[Comfort] turn = game`; the menu shows it as Smooth. No turn-speed row. | Always, unless MOV-02 or MOV-03 is chosen. | `vr::comfort::TurnStyle::Game`; `game/Comfort.cpp` `Tick`, `LogGameTurnRate` | headset (the default since 2026-10-23) |
| MOV-02 | Snap turn: one flick of the right stick turns the player by the snap angle at once; the next flick needs the stick back near the centre and a pause. A stick already held when the mod takes the turn over does not snap until let go. | `snapDeg` 45 (menu 30, 45, 60); flick past 0.7; re-armed inside 0.3; 0.25 s apart | MOV-06 | `vr::comfort::StepSnap`, `SnapTurn`; `game/Comfort.cpp` `Tick` | unit test (`TestComfort`); headset [verify] |
| MOV-03 | Smooth turn by the mod (file only): linear past the dead zone, full speed at full deflection. Slow turns are not lost to rounding. | `turn = smooth`; `turnSpeed` 120 deg/s; `deadzone` 0.15; 65536 yaw units a turn with the remainder carried | MOV-06 | `vr::comfort::SmoothTurnDegrees`, `YawUnits::Take` | unit test; headset [verify] |
| MOV-04 | While the mod turns, the game never sees the controllers' right-stick X (a real pad's still turns). The mod turns only from the tick after it published that flag, so the game and the mod never both turn in one tick. | flag `kFlagModTurns` (bit 17), HostServices v17 | MOV-06 | `game/Comfort.cpp` `AdjustPad`, `gOwnPublished`; `plugin/PadMerge.cpp` | fake headset [verify] |
| MOV-05 | A mod turn goes into the controller's rotation in the mod's tick, before arm IK. The pawn turns with it in the same tick when it already faces the controller's way (combat), and the view anchor is told, so no frame shows arms or shoulders posed for the old heading. Body follow is not re-anchored. | pawn follows when within 364 yaw units (2 degrees) of the controller's yaw | With MOV-02 or MOV-03 | `game/Comfort.cpp` `Turn` (`Actor.SetRotation` through `ue3::runtime::Call`, `view_anchor::NoteTurn`); `vr::comfort::TurnCamera` | unit test (`TurnCamera`); headset [verify] |
| MOV-06 | The mod's turn stands down, and the game turns, when: the host is older than v17, the reflection layer is off, no head-tracked world view, our menu is open, not on foot (the Mako, a wheel, a conversation, a menu, a cutscene), no pawn, in the game's cover, on a ladder, a camera mode of its own (scope, cover, cinematic), the squad menu has the sticks, no tracked controllers. Each change of reason is logged, at most every 2 s. | ladder = `Physics` 9; camera modes by `vr::view_level::CameraModeAllows` | n/a | `game/Comfort.cpp` `Why`, `LogWhy` | unit test (the mode rule); fake headset [verify] |
| MOV-07 | While squad placement's B is held, the right stick steps its selector and does not turn. | n/a | squad placement holding | `game/Comfort.cpp` `Tick` (`squad_place::Holding`) | [verify] |
| MOV-08 | Comfort vignette (off by default): a head-locked black quad whose alpha rises toward the edges, faded in and out, the first of our quads in the frame. Nothing is drawn while it is not wanted. | `vignette` 0; `vignetteStrength` 0.6 (menu 10-100% in steps of 5); clear inside 30 degrees from the centre, full past 58; fade 0.15 s; quad 1 m ahead in a VIEW space, sized to both eyes' fields of view; texture 128 px, refilled at 64 strength levels | Drawn only in a world frame with no menu open; the host drops a target older than 250 ms | `vr::comfort::VignetteMask`, `VignetteFade`, `CoverFov`; `plugin/Vignette.cpp`; `setVignette` (HostServices v17) | unit test; fake headset (the layer path seen); headset [verify] |
| MOV-09 | The vignette is wanted while the stick turns the player: under the game's turn or the mod's smooth turn while the stick is past the dead zone; under snap for a moment after each snap. | dead zone 0.15; 0.2 s after a snap | on foot, head-tracked, our menu closed, squad menu closed, controllers tracked | `vr::comfort::VignetteDrive::Target`, `StickTurns`; `game/Comfort.cpp` `Tick` | unit test |
| MOV-10 | With `vignetteWhen = move` the vignette is also wanted while the left stick walks the player. | left stick length past 0.2 | as MOV-09; not while the squad's or the power wheel or squad placement has the stick | `game/Comfort.cpp` `Tick` (`moveThreshold`) | unit test |
| MOV-11 | In the Mako the vignette follows the vehicle: wanted while it drives or turns, with hysteresis so an idling Mako does not flicker it. | moving at 150 units/s (about 1.5 m/s); turning at 20 deg/s of yaw (0.1 s low-pass); off below 0.6 of each threshold | game mode 1 with the vehicle's vignette wanted (`[Mako]`); `vignetteWhen` applies | `vr::comfort::VehicleMotion::Step`; `game/Mako.cpp` `CurrentMotion`; `game/Comfort.cpp` | unit test; [verify] in the game |
| MOV-12 | Walk toward: the left stick's forward is where the player looks (Halcyon's `moveFollowsHead`). The menu offers only "Where I look" and "Left hand"; the old Body choice was removed (it brought back "look left, move right"). | `[Comfort] walk = head` | on foot | `plugin/MainMenuSettings.cpp` (`walk`); Halcyon's stick turn after the merge | headset |
| MOV-13 | Walk toward the left hand: the host turns the controllers' left stick to the left controller's aim, laid flat on the floor, before Halcyon's head-look turn (which is taken off first). A hand pointing nearly straight up or down keeps the last usable yaw. | `walk = hand`; flat yaw refused within about 75 degrees of vertical; flag `kFlagHandWalk` (bit 18) | on foot, head-tracked, our menu closed, no squad wheel, power wheel or squad placement | `vr::comfort::FlatYawLeft`, `HandWalkUnits`; `plugin/PadMerge.cpp` `WalkTowardHand` | unit test; headset [verify] |
| MOV-14 | Linear walk: every speed ramp of the pawn reaches the stick's target within a tenth of a second, and the pawn's acceleration takes the fastest run from a standstill in the same time. Never slower than the game's own. Each value is put back if it is still ours. | `[Hands] linearWalk = 1`; `linearWalkS` 0.10 (clamped 0.03..1); top speed 600 units/s; the game's: 1.5 per s each way exploring (0.67 s), 3 and 2 with a weapon out, `AccelRate` 1500 | VR on foot, head-tracked | `vr::combat_stance::WantedRamp`; `game/CombatStance.cpp` (ramp section, `m_fMaxDesSpd{,Tac}{Accel,Decel}Rate`, `Pawn.AccelRate`) | fake headset 2026-11-13 (a run within 0.1 s of the push, the stop at once) |
| MOV-15 | Linear stick: the speed is in proportion to the push. After the game's own move function ran in a tick, the mod sets the pawn's desired speed to the push over `linearFullAt`. | `[Hands] linearStick = 1`; `linearFullAt` 0.90 (clamped 0.5..1); measured: half a push 167 units/s (the game's 75), 0.68 gives 267, 0.8 gives 333; the game's own: a crawl under 0.65 of MoveMag, the run past 0.70 | VR on foot. The game's value is kept when not accelerating, with move input ignored, with the walk toggle, crouched while exploring, in cover while in combat, and at or under the game's idle threshold. The sprint is MOV-18's | `vr::combat_stance::LinearSpeed`; `game/CombatStance.cpp` (`PlayerTick` post, `BioPawn.SetDesiredSpeed`, `PlayerMoveExplore` / `PlayerMoveCombat` posts) | fake headset 2026-11-13 |
| MOV-16 | The game's start step, stop slide and skid are off: Shepard starts, stops and turns about at once. The three flags are put back if still ours. | `[Hands] startStopAnims = 0`; the start step had held Shepard 0.15-0.19 s | VR on foot | `game/CombatStance.cpp` (`m_bEnableStartAnim`, `m_bEnableStopAnim`, `m_bEnableSkidAnim`) | fake headset 2026-11-13 |
| MOV-17 | Without linear walk, walking with a weapon out ramps as gently as exploring (the combat ramp set to the explore values, restored after). | `[Hands] exploreRamp = 1`; only acts when `linearWalk = 0` | VR on foot | `vr::combat_stance::RampNeedsWrite`, `WantedRamp` | unit test |
| MOV-18 | Sprint: holding the game's sprint button (A held) while running is a faster run. The game's `StormOn` runs as written and its intent is withdrawn right after, so the storm posture, its camera and the lowered gun never come. The gun stays up and can fire. | `[Hands] sprint = speed` (`game`: the game's own sprint); counts as running above 200 units/s horizontal (the walk is 130, the run 400) | VR on foot; ME1's `StormOn` refuses in cover itself | `game/Sprint.cpp` (post-handlers on both hooks, `BioPlayerController.bWantsToStorm`); `vr::sprint::Step` | fake headset 2026-11-16 (`running fast: x1.16`, no storm camera mode) |
| MOV-19 | The faster run's speed is the game's own sprint speed: `GroundSpeed` and `CombatGroundSpeed` are raised by one factor, the game's sprint speed over the run in effect, and put back after. ME1 has a sprint speed of its own with a weapon drawn. | factor within 1..2 (1 for anything implausible); measured holstered: 400 to 462 units/s; `StormSpeed` holstered, `CombatStormSpeed` drawn; the run in effect from `Pawn.GetMaxSpeed` | while MOV-18 boosts | `vr::sprint::SpeedFactor`; `game/Sprint.cpp` | fake headset 2026-11-16 |
| MOV-20 | The sprint has a stamina of the mod's own: it runs down at speed, refills after a pause, and once emptied needs a refill and a new press. | `sprintStaminaS` 6 (1..60); `sprintRefillS` 8 (1..120); refill starts 1.0 s after the last sprinting tick; emptied: again only past 0.3 and a new press | as MOV-18 | `vr::sprint::Settings`, `Step` | unit test (`TestSprint`) |
| MOV-21 | The guns' and the Mako's screen shakes are off: each shake's rotation, place and FOV amplitudes are scaled at the source and the originals put back. | `[Comfort] screenShake = 0` (0..1); the menu's Camera shake: Off 0, Low 0.3, Full 1 | while the view is head-tracked; sources: the held weapon's `FiringShake` and `TightAimFiringShake`, the Mako's `JumpShake` and `BoostShake` | `game/CameraShake.cpp` `TickScreen`, `Scale`, `WriteAmplitudes` | [verify] in the game (research/71 section 2.3) |
| MOV-22 | The body's camera shakes (the run, shots, blasts) are off on the first-person on-foot camera modes; put back when the setting goes on or the view is no longer head-tracked. | `[Comfort] viewShake = 0`; the menu's Running sway; modes whose class name holds Explore, Combat or TightAim, never Vehicle, Sniper or HipAim | game mode 0, head-tracked | `game/CameraShake.cpp` `TickView`, `ClearMode` (`SFXCameraMode.bIsCameraShakeEnabled`) | [verify] in the game (research/71 section 3.4) |
| MOV-23 | Physical crouch: with the eyes below three quarters of the standing eye height for a moment, Shepard crouches; above 0.85 for a moment he stands. The height is from the floor (STAGE or LOCAL_FLOOR), else from the drop below the recenter height. | `[Hands] physicalCrouch = 1`; `crouchBelow` 0.75 (0.3..0.95); `standAbove` 0.85 (at least crouchBelow + 0.02, at most 0.99); `crouchDwellS` 0.2 (0..2); a guided calibration's standing and crouched heights replace the thresholds | VR on foot; off in seated play (MOV-26) | `ik::posture::CrouchDetector`, `FractionFromFloor`; `game/Posture.cpp` (`SFXGame.BioPawn.ShouldCrouch` through ProcessEvent) | unit test; headset [verify] the thresholds |
| MOV-24 | The physical posture acts on its changes only, so L3 (the game's crouch toggle) and the game's own changes in between are kept until the next real crouch or stand. A change the game cannot take now (sprinting) waits; a crouch the mod asked for is released when physical crouch goes off or VR is gone. | at most 3 asks per change; VR gone after 2 s | VR on foot | `ik::posture::CrouchArbiter`; `game/Posture.cpp` | unit test (`tests/PostureOwnershipTests.cpp`) |
| MOV-25 | A real crouch lowers the view once: while physically crouched, the crouch camera modes keep the standing eye, so only the real head lowers the view. A crouch by L3 while standing lowers the view as the game does. | `[Hands] crouchDrop = 0` (1: the old double drop; the game's crouch camera is 45 units lower in combat) | host side; flag `kFlagPhysicalCrouch` (bit 23) | `vr::combat_stance::ConstantEyeApplies`; `plugin/HalcyonBridge.cpp` (`ConstantFpEye`) | unit test; headset [verify] |
| MOV-26 | Seated play turns the physical crouch off; L3 remains. | the calibration's play position = seated | n/a | `game/Posture.cpp` (`body_calibration::Position`) | [verify] |
| MOV-27 | No cover snapping: ME1 takes cover by itself (no button) when the move pushes into a surface; that native test is refused for the player's pawn, so no cover state is ever written. The game's own cover trace still runs, so a refused entry is logged. | `[Hands] noCover = 1`; the game's test: every 0.1 s, the move within 20 degrees into a surface 80 units ahead, the camera within 60 degrees; hook at `0xAB1430`, trace `0xAA92F0` | VR on foot with a head-tracked view; decided once a game tick; nothing else lifts it | `ik::posture` `CoverSnapInput`; `engine/CoverAcquire.cpp`; `game/Posture.cpp` | fake headset [verify]; headset (no snapping reported since) |
| MOV-28 | A cover the player is already in (taken before the rule applied, or by a script) is left as A leaves it; if that does not leave it in time the game's cover-lost way is used. | `TryExitCover` asked every 0.25 s; fallback after 2.0 s | as MOV-27 | `game/Posture.cpp` (`SFXGameModeDefault.TryExitCover`, `kExitRetrySeconds`, `kExitFallbackSeconds`) | [verify] |
| MOV-29 | VR cover: a hit on the player counts as taken in cover when the line from the eyes to the shooter meets a surface near the eyes. The head raised over the cover is the game's "leaning out": exposed, full damage. | `[Cover] protect = 1`; `reachM` 1.2 (0.2..3.0); a surface whose normal is steeper than 0.7 (a crate's top) gets its facing from the shot; a shooter nearly straight above or below gives no cover | on foot, head-tracked; per hit, per shooter | `vr::cover::Judge`; `game/VrCover.cpp` | fake headset (covered: no damage; exposed or off: full damage) |
| MOV-30 | The verdict reaches the game inside that one damage call only: `IsInCover` is answered true and `IsInCoverLeaning` false for the player, and `BioPawn.CoverNormal` holds the surface's facing until the call returns. The game's own rule then cuts the hit. | the game's rule: all of the hit on Casual and Normal, 90% above, for a shot from the cover's front (direction against the normal below -0.3) and from more than 300 units | inside `BioPawnBehavior.ProxyTakeDamage` for the player's pawn | `vr::cover::GameCone`; `game/VrCover.cpp` (`OnIsInCover`, `OnIsInCoverLeaning`; `engine/CallFunction` answer handlers) | fake headset; unit test (`tests/VrCoverTests.cpp`) |
| MOV-31 | The game no longer freezes the world when its window loses focus: the freeze is undone each world tick. The pad still reaches the game only with focus. | `[Game] pauseUnfocused = 0` | every game mode | `game/FocusPause.cpp` (`BioWorldInfo.Tick` handler; `m_bPausedByFocusLoss`, `bPlayersOnly`) | headset (default since 2026-11-05) |
| MOV-32 | While Shepard moves, his legs keep the run's direction (where the run, strafe and backpedal animations point) and only the spine twists toward the view; the whole body turns by the rest. | moving above 60 units/s; `[Hands] legsFollowMotion = 1`; `legsTwistDeg` 60 (30..90) | first person, body follow on | `ik::body_follow` `LegsFollow` (`core/ik/BodyFollow.hpp`); `game/BodyFollow.cpp` | replay on the fake headset; headset [verify] |
| MOV-33 | ME1 has no traversal move in VR: the traversal module starts nothing on LE1. | n/a | n/a | `game/Traversal.cpp` `TryStartup` (returns false outside LE2 and LE3) | read from the code |

# Arbitration

| input | claimants, in order | what the loser sees |
|---|---|---|
| right stick X on foot | squad placement's selector while B is held; the squad's weapon wheel (`gamepad::kFlagNativeWheel`); the mod's turn (snap or smooth); else the game's turn | the turn stands down for that tick; the vignette is not wanted while the squad wheel is up |
| left stick on foot | the power wheel, the squad wheel and squad placement take it; else it walks (toward the head or the left hand) | no hand-walk rotation and no "moving" vignette while a wheel has it |
| A held | the game's use of a selected object runs first in its own binding; then `StormOn`, whose intent the sprint withdraws | the game's sprint never starts while `sprint = speed` |
| L3 | the stick chord (both clicks within 100 ms: a snapshot or our menu) holds L3 back 100 ms; else the game's crouch toggle, kept by the crouch arbiter until the next physical change | a physical crouch never undoes an L3 crouch made since |
| crouch against sprint | a physical crouch or stand that arrives while sprinting waits until the game takes it | logged `the physical ... waits: Shepard is sprinting` |

# Problems ME1 already solved

| problem | fix | where |
|---|---|---|
| The sprint button's exec reaches the script through ProcessEvent, so a CallFunction skip on `StormOn` never ran: the game's own sprint ran from 2026-11-06 to 2026-11-16. | Let `StormOn` run and withdraw `bWantsToStorm` in post-handlers on both hooks. | [gotcha](../gotchas/binding-execs-come-through-process-event.md), [research/trilogy-sprint](../../../research/trilogy-sprint.md) |
| `Actor.SetRotation`, `SetLocation` and other natives with an opcode of their own did nothing through ProcessEvent. | Call through `ue3::runtime::Call`, which clears the native index for its call. | [gotcha](../gotchas/process-event-skips-opcode-natives.md) |
| A turn written in the `PlayerTick` handler showed a frame with the arms posed for the old heading. | The turn goes into ControlRotation in the mod's tick before arm IK, the pawn turns with it, `view_anchor::NoteTurn` maps the view. | [research/31](../../../research/31-comfort.md) section 2 |
| The game's turn rate cannot be set (it is per camera mode). | No turn-speed row; `turn = game` keeps the game's own; the rate is only logged. | [research/31](../../../research/31-comfort.md) section 1 |
| "Body" walking (`moveFollowsHead` off) brought back Halcyon's "look left, move right". | The menu offers only head and left hand (2026-11-04). | [research/31](../../../research/31-comfort.md) section 4 |
| ME1 enters cover by a native test with no button, so there is no script call to refuse. | A pre-hook on the test (`0xAB1430`) refuses it for the player's pawn. | [research/23](../../../research/23-cover.md) sections 1 and 2 |
| With snapping off the game's cover protection never applied to a VR player. | The verdict is answered inside the damage call only, and the game's own cone does the rest. | [research/23](../../../research/23-cover.md) section 6 |
| A skip handler read the script frame's `Node` at the wrong offset and every skip failed silently. | `FFrame` is packed on 4 bytes: `Node` at 0x14. | [gotcha](../gotchas/fframe-packed-on-four-bytes.md) |
| The game's 0.67 s speed ramp, start step and stop slide felt like the third-person game. | `linearWalk`, `linearStick`, `startStopAnims = 0`, each measured with `movement: response:`. | [research/38](../../../research/38-combat-stance.md) "Linear walking" |
| The game's crouch camera lowered the view a second time under a real crouch. | The crouch modes keep the standing eye while the crouch is physical. | [research/38](../../../research/38-combat-stance.md), [research/23](../../../research/23-cover.md) section 3 |
| The guns and the Mako shook the view; the run put a jog on it. | Shake amplitudes scaled at the source; the modes' body shakes cleared in first person. | [research/71](../../../research/71-halcyon-and-camera-shake.md) sections 2 and 3 |
| The game's camera collision dropped the first-person view to Shepard's knees near a wall. | `game/CameraCollision` clears `bCollisionEnabled` in first person on foot. | [gotcha](../gotchas/camera-collision-in-first-person.md) |
| ME1 froze the world without window focus (it also refused the Mako's exit). | `game/FocusPause` undoes `bPlayersOnly` each world tick. | [research/47](../../../research/47-cockpit-in-game.md) |
| The whole body turning toward the view made the legs run sideways. | Moving, the legs keep the pawn's facing and the spine twists. | [research/29](../../../research/29-body-follow.md) "The legs keep the run" |

# Code map

| part | file | scope |
|---|---|---|
| Turn, vignette and hand-walk rules | [core/vr/Comfort.hpp](../../../src/core/vr/Comfort.hpp) | game-independent, unit-tested |
| Turn and vignette driver | [game/Comfort.cpp](../../../src/game/Comfort.cpp) | compiled for every game (`ue3::kEveryGame`; LE3 adds its native admission) |
| Vignette quad | [plugin/Vignette.cpp](../../../src/plugin/Vignette.cpp) | host, every game's host |
| Hand-walk stick rotation, pad flags | [plugin/PadMerge.cpp](../../../src/plugin/PadMerge.cpp), [game/GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp) | host, both host kinds |
| Walk ramps, linear stick, start and stop steps | [core/vr/CombatStance.hpp](../../../src/core/vr/CombatStance.hpp), [game/CombatStance.cpp](../../../src/game/CombatStance.cpp) | core game-independent; the game module reads `BioPawnBehavior` fields without `kEveryGame`: LE1-only in effect (ledger G18: missing on ME2) |
| Sprint | [core/vr/Sprint.hpp](../../../src/core/vr/Sprint.hpp), [game/Sprint.cpp](../../../src/game/Sprint.cpp) | shared; `CombatStormSpeed` is `MEVR_GAME_LE1`-only |
| Camera shakes | [game/CameraShake.cpp](../../../src/game/CameraShake.cpp) | the screen-shake sources are `MEVR_GAME_LE1`-only; the camera modes' switch is every game's |
| Crouch and cover-snap rules | [core/ik/Posture.hpp](../../../src/core/ik/Posture.hpp) | game-independent, unit-tested |
| Crouch and cover driver | [game/Posture.cpp](../../../src/game/Posture.cpp) | every game; `BioPawn.ShouldCrouch` and the native cover refusal are the LE1 branch |
| Native cover test hook | [engine/CoverAcquire.cpp](../../../src/engine/CoverAcquire.cpp), [engine/Addresses.hpp](../../../src/engine/Addresses.hpp) | LE1-only (the address is 0 on LE2 and LE3) |
| VR cover | [core/vr/VrCover.hpp](../../../src/core/vr/VrCover.hpp), [game/VrCover.cpp](../../../src/game/VrCover.cpp) | core shared; `ProxyTakeDamage`, `CoverNormal` and `mevr cover hit` are the LE1 branch |
| Focus pause | [game/FocusPause.cpp](../../../src/game/FocusPause.cpp) | `MEVR_GAME_LE1`-only |
| Legs rule | [core/ik/BodyFollow.hpp](../../../src/core/ik/BodyFollow.hpp) | game-independent |
| Traversal | [game/Traversal.cpp](../../../src/game/Traversal.cpp) | starts nothing on LE1 |
| Settings reader | [game/HandCalibration.cpp](../../../src/game/HandCalibration.cpp) | mod; appends the default blocks |
| Menu rows | [plugin/MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp), [assets/menu/main.rml](../../../assets/menu/main.rml) | host |

HostServices: v17 (`setVignette`, the turn and hand-walk flags). The pad's flags travel with the pad options each tick.
The parity ledger's rows are G11, G17, G18, G39, G40, G45 and G58 in
[docs/trilogy-parity-ledger.md](../../trilogy-parity-ledger.md).

# Settings and defaults

All in `ASI\LE1MEVR.ini`.

| section | key | default | read by | menu row |
|---|---|---|---|---|
| Comfort | turn | game | mod | Comfort > Turn style (Snap, Smooth = game) |
| Comfort | snapDeg | 45 | mod | Comfort > Snap angle (30, 45, 60) |
| Comfort | turnSpeed | 120 | mod | none (file only, with `turn = smooth`) |
| Comfort | deadzone, snapThreshold, snapRearm, snapCooldownS | 0.15, 0.7, 0.3, 0.25 | mod | none |
| Comfort | vignette | 0 | mod | Comfort > Comfort vignette |
| Comfort | vignetteStrength | 0.6 | mod | Comfort > Vignette strength |
| Comfort | vignetteWhen | turn | mod | Comfort > Vignette when |
| Comfort | walk | head | mod (the host turns the stick) | Comfort > Walk toward |
| Comfort | screenShake | 0 | mod | Comfort > Camera shake (Off, Low 0.3, Full) |
| Comfort | viewShake | 0 | mod | Comfort > Running sway |
| Hands | linearWalk, linearWalkS | 1, 0.10 | mod | none |
| Hands | linearStick, linearFullAt | 1, 0.90 | mod | none |
| Hands | startStopAnims | 0 | mod | none |
| Hands | exploreRamp | 1 | mod | none |
| Hands | sprint | speed | mod | none |
| Hands | sprintStaminaS, sprintRefillS | 6, 8 | mod | none |
| Hands | physicalCrouch | 1 | mod | Comfort > Physical crouch |
| Hands | crouchBelow, standAbove, crouchDwellS | 0.75, 0.85, 0.2 | mod | none (the calibration measures them) |
| Hands | crouchDrop | 0 | host | none |
| Hands | noCover | 1 | mod | none |
| Hands | legsFollowMotion, legsTwistDeg | 1, 60 | mod | none |
| Cover | protect, reachM | 1, 1.2 | mod | none |
| Game | pauseUnfocused | 0 | mod | none |

Migration: a file whose `[Hands] sprint` still holds the first default `game` and has no `sprintDefault` marker is
moved to `speed` once.

# Checks

| kind | what |
|---|---|
| log prefixes | `comfort:`, `vignette:`, `halcyon bridge: the left stick walks toward`, `movement:` (`movement: response:` measures a push), `sprint:`, `camera shake:`, `posture:`, `vr cover:`, `focus pause:`, `torso: legs keep the run` |
| snapshot keys | `"comfort"`, the `vignette` layer, `"stance"`, `"mod"."cameraShake"`, `"mod"."vrCover"`, `torso.legs` |
| console | `mevr shake [screen <0..1> \| view on\|off \| status]`, `mevr cover [on\|off \| test \| pawns \| hit <damage> <n> \| probe <yawDeg> \| snap on\|off\|auto]`, `mevr findplayer [pawn\|behavior\|controller]` |
| unit tests | `tests/Main.cpp` `TestComfort`, `TestSprint`, `TestCombatStance`; [tests/VrCoverTests.cpp](../../../tests/VrCoverTests.cpp); [tests/PostureOwnershipTests.cpp](../../../tests/PostureOwnershipTests.cpp); [tests/PadMergeTests.cpp](../../../tests/PadMergeTests.cpp) |
| replay | [tools/make-walk-recording.py](../../../tools/make-walk-recording.py) with `-Inputs` walks and sprints on the fake headset |

# Citations

- [research/31: comfort](../../../research/31-comfort.md)
- [research/23: cover and crouch](../../../research/23-cover.md)
- [research/38: combat stance, linear walking](../../../research/38-combat-stance.md)
- [research/71: camera shake](../../../research/71-halcyon-and-camera-shake.md)
- [research/trilogy-sprint](../../../research/trilogy-sprint.md)
- [research/29: body follow](../../../research/29-body-follow.md)
- [research/44: the Mako](../../../research/44-mako.md)
- [gotcha: binding execs come through ProcessEvent](../gotchas/binding-execs-come-through-process-event.md)
- [gotcha: ProcessEvent skips opcode natives](../gotchas/process-event-skips-opcode-natives.md)
- [gotcha: FFrame packed on four bytes](../gotchas/fframe-packed-on-four-bytes.md)
- [gotcha: camera collision in first person](../gotchas/camera-collision-in-first-person.md)
- [How to catalog](../how-to-catalog.md)
