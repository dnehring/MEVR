---
type: System
title: ME1 body holsters, physical grenades and melee
description: What ME1 does when a hand reaches for a weapon slot on the body, takes and throws a grenade, or strikes; the slots' geometry, the grab and holster state machine, every route by which the game may not draw the weapon, and the one melee dispatcher.
tags: [me1, holsters, grenades, melee, gestures, trilogy, parity]
status: draft
timestamp: 2026-11-18T07:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T07:00:00Z }
game: LE1
---

# What the player gets

- Four weapon slots hang on the body: pistol at the right hip, shotgun at the left hip (a cross-draw), assault rifle over
  the right shoulder, sniper rifle over the left shoulder. The right hand reaches into one, the controller buzzes, a
  squeeze of the right grip draws that weapon.
- The right grip then holds the gun. Let go at the gun's own slot and it is holstered at once; let go anywhere else and
  it goes back by itself 0.2 s later unless the grip closes again. A burst is never cut short by it.
- Nothing else draws the weapon: not the trigger, not the game's melee, grenade or holster buttons, not its weapon
  wheel. A refused draw is a short low buzz in the right hand (the trigger's refusal is silent). Medi-gel and powers
  still draw.
- A grenade is taken with either hand's grip at the left side of the chest, thrown by letting go while the hand moves
  (1.5 m/s or faster), and put back by letting go slowly. X never throws on foot.
- Melee is a gesture only. With the weapon out a fast stroke of either hand at an enemy in reach is the game's pistol
  whip or rifle butt. With the weapon away the right fist (grip and trigger held) punches. B never melees on foot.
- Seated players get the same slots at fixed reach points in front of and beside the head, and may also draw through
  the game's weapon wheel.

# Contracts

Tracking-space quantities are OpenXR LOCAL metres (+X right, +Y up, -Z forward). Slot offsets are right, up, forward
from the anchor in the body frame, written for a 1.65 m eye height.

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| HOL-01 | The slots hang from a body frame, not the head. The anchor is the eye as it would be with the head upright over its neck pivot, so looking down or tilting the head moves no slot; its height follows the head (a crouch lowers the slots), its horizontal place follows at once. | neck pivot 0.08 m behind and 0.08 m below the eyes; height time constant 0.08 s | holsters active (HOL-13) | [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) `NeutralEye`, `Anchor::Update`; [Holsters.cpp](../../../src/game/Holsters.cpp) `Tick` | unit test ([Main.cpp](../../../tests/Main.cpp)) |
| HOL-02 | The frame's yaw is arm IK's torso yaw; without it, a yaw that lags the head. The yaw is held while the hand is in a slot or a squeeze at one is in progress, so a glance at the hip does not move the hip. A carried gun's long hold does not hold the yaw. | lagging yaw: dead zone 0.6 rad, recentre 0.5 /s (`ik::BodyYaw` defaults) | `Machine::Busy()` | [Holsters.cpp](../../../src/game/Holsters.cpp) `Tick` (`gYaw`); [BodySlots.hpp](../../../src/core/ik/BodySlots.hpp) `Machine::Busy`; [Ik.hpp](../../../src/core/ik/Ik.hpp) `BodyYaw` | [verify] |
| HOL-03 | Slot offsets scale with the player's standing eye height, not the current head height: crouching does not shrink the layout. | scale = eye height / 1.65, clamped 0.5..1.5; radii are not scaled | standing play | [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) `Scale`, `SlotCenter` | unit test |
| HOL-04 | The standing eye height comes, in this order, from: the guided calibration's standing eye height; the head's height above the runtime's floor, taken at a recenter and then a slow-rising maximum that crouching never lowers; `[Holsters] eyeHeightM`. A new XR session forgets the floor's value. | plausible sample 0.9..2.3 m; rise time constant 2.0 s; fallback 1.65 m; logged on a 0.02 m change | `eyeHeightFromFloor = 1` and a host floor (HostServices v10) for the floor source | [Holsters.cpp](../../../src/game/Holsters.cpp) `UpdateEyeHeight`; [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) `StandingHeight` | unit test; [verify] headset (research/19 section 8) |
| HOL-05 | Which weapon sits where, standing: `hipRight` pistol, `shoulderRight` assault rifle, `hipLeft` shotgun, `shoulderLeft` sniper rifle. ME1 has no SMG slot: the fifth slot's weapon is None. | hipRight (0.20, -0.62, -0.05) r 0.14; shoulderRight (0.18, -0.08, -0.20) r 0.20; hipLeft (-0.20, -0.62, 0.00) r 0.14; shoulderLeft (-0.18, -0.08, -0.20) r 0.20 | each line editable in `[Holsters]`; weapon `none` turns a slot off | [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) `DefaultLayout`; [HandCalibration.hpp](../../../src/game/HandCalibration.hpp) `GameHolsterLayout` (`MEVR_GAME_LE1`: SMG off) | unit test (layout); headset for the hipLeft move (research/19 "Default slots", 2026-10-25) |
| HOL-06 | The hand is in the slot it is inside with the smallest distance / radius; it stays in that slot until it is past radius + exit margin. An untracked hand is in no slot. | exit margin 0.05 m | right grip pose valid | [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) `Machine::Update` | unit test |
| HOL-07 | Only a NEW squeeze of the right grip inside a slot acts. In a slot whose weapon is carried it draws that weapon. A squeeze held from before (through a menu, a pause, a wheel) is never taken for a new one. | grip latch on 0.6, off 0.4; a new squeeze = the host's count of squeezes it kept from the game (HostServices `rightGripFree`, v9) | holsters active | [Holsters.cpp](../../../src/game/Holsters.cpp) `Tick` (`newSqueeze`), `Draw`; [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) `UpdateHolding` | unit test; headset 2026-10-22 (research/19 section 11) |
| HOL-08 | The draw is the game's own animated switch: `SFXGameModeDefault.SwitchToWeapon(quick slot)` through ProcessEvent. What is carried is read from the pawn's quick slots (BioPawn, BioPawnBehavior, BioEquipment). A draw click is felt. | draw click 0.9 x strength, 320 Hz, 20 ms; the game's result is awaited 3 s and logged | the game allows combat (HOL-09) | [Holsters.cpp](../../../src/game/Holsters.cpp) `Draw`, `ReadCarried`, `FollowPending` | headset 2026-10-22 |
| HOL-09 | A grab asks `BioPlayerInput.IsCombatEnabled` first. Where the game allows no combat the weapon stays in its holster and the hand gets the refusal buzz, not the draw click. | refusal buzz 0.3 x strength, 60 Hz, 0.12 s | every game since 2026-11-17 (the owner's answer to question 15) | [Holsters.cpp](../../../src/game/Holsters.cpp) `Draw` (`CombatEnabled`) | [verify] |
| HOL-10 | A squeeze in a slot whose weapon is not carried draws nothing and is the refusal buzz. A squeeze within the exit margin of a slot but in none ("missed") draws nothing and is not RB. A squeeze at the held gun's own slot takes hold of it. | margin 0.05 m | — | [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) `Action::Unavailable`, `Missed`, `AlreadyHeld` | unit test |
| HOL-11 | The right grip holds the gun (`gripHolds = 1`). While it holds, the right grip is never RB. Letting go at the gun's own slot holsters it at once (`SFXGameModeDefault.TryHolster`, only while drawn: it is a toggle). Letting go anywhere else returns it after the grace unless the grip closes again; a new squeeze anywhere cancels the return. | `releaseGrace` 0.2 s (0.3 until 2026-11-06, migrated once); -1: never; holster click 0.65 x strength, 200 Hz, 20 ms | on foot and in the game's two wheels (modes 0, 2, 3); in the Mako and menus the grip is RB as always | [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) `UpdateHolding`; [Holsters.cpp](../../../src/game/Holsters.cpp) `Holster`, `AdjustPad` (`rightGripHolds`) | unit test; headset 2026-10-22, grace 2026-11-06 |
| HOL-12 | A return never happens mid-burst or mid-switch. While the trigger is held the grace does not run. While the game is switching weapons (`BioPawn.IsSwitchingWeapons`: the draw's animation, during which the game ignores `TryHolster`) the return waits for the switch's end. A gun let go at its own slot during a draw goes back with no grace once the draw is over. A let-go draw that never comes out is given up. | trigger "held" at 0.5; draw waited for 2.0 s (`kReturnWaitForDrawSeconds`) | `gripHolds` | [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) `UpdateHolding` (Returning) | unit test |
| HOL-13 | Holsters run only on foot with a head-tracked world view, tracked controllers, a valid right grip pose and no calibration running. Otherwise the machine resets: the hand is in no slot, a running return is dropped and the gun stays out. | game mode 0 exactly for the slots (the grip-holds mapping also covers the wheels' modes) | off in the Mako, menus, conversations, flat screens, VR off; `[Holsters] enabled = 0` | [Holsters.cpp](../../../src/game/Holsters.cpp) `Tick`, `Inactive` | [verify] |
| HOL-14 | Contact haptics: a continuous buzz while the hand overlaps a slot, stronger and higher toward the centre; an edge pulse on entering; halved while the grip carries a gun. | depth = 1 - distance / radius; amplitude (0.08 + 0.27 x depth) x 2 x strength, 70 + 130 x depth Hz; edge 0.55 x, 160 Hz, 35 ms; re-asked each tick, lasts 0.15 s unasked; `hapticAmplitude` 0.5 | host `hapticContinuous` (v10); a v9 host: one pulse on entry | [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) `Depth`, `Contact`, `EnterEdge`; [Holsters.cpp](../../../src/game/Holsters.cpp) `Contact` | unit test (curves); headset 2026-10-22 (research/19 section 7) |
| HOL-15 | Only holsters draw: the game's own `SFXPawn_Player.DrawPrimaryWeapon` is skipped for the player's pawn. This refuses the draws of the melee button, the grenade button, the weapon wheel's entry and the holster key's toggle. The right hand buzzes once per refused draw. | refusal buzz as HOL-09 | `onlyHolstersDraw = 1`, grip holds (HOL-11's modes), tracked controllers, a valid VR view, the CallFunction hook installed, and DrawPrimaryWeapon a script function without parameters; a pad player or sleeping controllers keep the game's draws | [Holsters.cpp](../../../src/game/Holsters.cpp) `OnDrawPrimaryWeapon`, `RefuseGameDraw`, `Tick` (`gBlockPawn`); [CallFunction.hpp](../../../src/engine/CallFunction.hpp) skip handler | fake headset 2026-11-04 (research/19 "The trigger's draw") |
| HOL-16 | The trigger never draws. RT is `SFXGameModeBase.FireWeapon`, which draws first; that draw is refused with no buzz (a pull with an empty hand is half of a fist). | — | as HOL-15 | [Holsters.cpp](../../../src/game/Holsters.cpp) `DrawRoute::Trigger` (`caller == gFireWeapon`) | fake headset 2026-11-04 |
| HOL-17 | Medi-gel and powers still draw. A draw whose calling script function is `SFXGameModeDefault.UseFirstAid`, `TryUseMappedPower` or `EnterPowerWheel` is let through and logged. | three callers, matched by the frame's Node | as HOL-15 | [Holsters.cpp](../../../src/game/Holsters.cpp) `gAllowedCallers`, `DrawRoute::Deliberate` | [verify] (the lead's decision 2026-10-26) |
| HOL-18 | A draw the mod itself wants from the game goes between `AllowGameDraw(true)` and `(false)`: taking a grenade with the weapon away (HOL-25). A weapon that comes out by any other route is logged with what the hands were doing ("came out without a holster grab"). | counted 3 s after our own draw or allowed draw | — | [Holsters.cpp](../../../src/game/Holsters.cpp) `AllowGameDraw`, `Tick` | [verify] |
| HOL-19 | Seated play uses fixed-metre reach points from the live neutral head, never the standing scale: raised, forward hips and nearer shoulders, larger zones. Weapon assignments stay the standing layout's. | seatedHipRight (0.28, -0.42, 0.18), seatedHipLeft (-0.28, -0.42, 0.18), seatedShoulderRight (0.28, -0.04, -0.14), seatedShoulderLeft (-0.28, -0.04, -0.14), all r 0.20; seatedGrenade (-0.10, -0.18, 0.08) r 0.10; scale 1 | calibration's play position = Seated | [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) `SeatedLayout`, `SeatedGrenadeSlot`; [HandCalibration.cpp](../../../src/game/HandCalibration.cpp) `GetHolsters`, `GetThrowables` | unit test ([BodySlotsSeatedTests.cpp](../../../tests/BodySlotsSeatedTests.cpp)); no headset acceptance (research/19 section 12) |
| HOL-20 | Seated, the game's weapon wheel is a button fallback: the wheel's close (`Deactivated`) may draw the chosen weapon; its entry's eager draw stays refused, without a buzz. Seated forces `weaponWheel` on. | — | Seated and `seatedWeaponWheel = 1` | [Holsters.cpp](../../../src/game/Holsters.cpp) `SeatedWeaponWheel`, `DrawRoute::SeatedWheelEnter` / `SeatedWheelClose` | [verify] |
| HOL-21 | RB moved off the grip: the right stick click held alone 0.3 s opens the game's power wheel (`EnterPowerWheel`, called by the mod), letting go closes it (`ExitPowerWheel`), a tap casts nothing and the click is never RB. A wheel the game opens late after the release is closed again. | hold 300 ms (`gamepad::kPowerHoldMs`); `powerWheel = r3` (y, x, b, none are RB sources: tap casts) | grip holds; opens only on foot | [Holsters.cpp](../../../src/game/Holsters.cpp) `TickPowerHold`; [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp) `PowerHoldFromStickClick`, `kFlagModPowerHold` | headset 2026-11-05 (research/19 "Hold only") |
| HOL-22 | The grenade slot is one more body slot, in the holsters' frame and scale: the left side of the chest. A hand entering it gets the holsters' edge pulse and contact buzz. | (-0.13, -0.33, 0.00) r 0.10; exit margin 0.05 | throwables active (HOL-30) | [HandCalibration.hpp](../../../src/game/HandCalibration.hpp) `Throwables::slot`; [Throwables.cpp](../../../src/game/Throwables.cpp) `Tick` | [verify] |
| HOL-23 | A new grip squeeze of either hand inside the grenade slot takes a grenade. The left grip's squeeze counts only if it began away from the gun (never the support grip). The right hand's claiming squeeze is not a holster grab and not RB. One grenade at a time. | `hands = both` (since 2026-11-07); the claim uses radius, + 0.05 if the hand was already in the slot | grenade claims run before holsters, the vent, the voice radio and squad orders | [Throwables.cpp](../../../src/game/Throwables.cpp) `Claim`; [Throwing.cpp](../../../src/core/ik/Throwing.cpp) `GrenadeMachine::Update` | unit test (machine) |
| HOL-24 | A grab is refused with the refusal buzz when: combat is disabled, Shepard is sprinting, no grenades are left, a throw is still running, or the last throw has not launched. | refusal buzz 0.3 x, 60 Hz, 0.12 s | — | [Throwables.cpp](../../../src/game/Throwables.cpp) `GrenadeRefusal` | [verify] |
| HOL-25 | ME1 throws only with a weapon out. Taking a grenade with the weapon away draws it (`UseGrenade`, which with the weapon away only draws; let through the holster rule). While the right hand holds a grenade, and after it threw or put one back until the right grip squeezes again, the gun stays out as if held. | `drawForThrow = 1`; a throw waits up to 2.0 s for the weapon | — | [Throwables.cpp](../../../src/game/Throwables.cpp) `Tick` (Grabbed), `RightHandBusy`, `FollowPending` | [verify] |
| HOL-26 | Letting go decides. The release velocity is the fastest moment of the last 120 ms, each a least-squares slope. At or above the minimum speed it is a throw; below, the grenade goes back (a stow click). Tracking lost while held: it goes back. The analog grip lets go early when it opens during a fast motion. | window 0.12 s, slope half-width 0.02 s, smoothing 0.03 s; `throwSpeedMinMS` 1.5; grip on 0.6, off 0.4, early release 0.3 below the squeeze's peak at 1.5 m/s or faster; history breaks at a 0.15 s gap or a 0.5 m step | — | [Throwing.cpp](../../../src/core/ik/Throwing.cpp) `EstimateRelease`, `UpdateGrip`; [Throwing.hpp](../../../src/core/ik/Throwing.hpp) | unit test |
| HOL-27 | The throw is the game's own grenade: `SFXGameModeDefault.UseGrenade` (count, cooldown, Shepard's animation), then a post-handler of `BioGrenade.LaunchGrenadeAtPoint` re-aims the disc: it starts at the throwing hand and flies at the point where the hand's throw would land. A grenade the game throws its own way is left as the game aimed it. | hand speed capped at 15 m/s, then x `throwGain` 2.5; landing arc under 9.81 m/s2, 0.05 s steps, 4 s, 50 m drop, traced with `Actor.Trace`; launch awaited 3 s; `flight = disc` (`arc` experimental) | needs a view anchor; else the grenade goes back | [Throwables.cpp](../../../src/game/Throwables.cpp) `Throw`, `CallUseGrenade`, `AimDisc`; [Throwing.cpp](../../../src/core/ik/Throwing.cpp) `ArcPoints` | headset 2026-10-22 (research/20 section 3) [verify] |
| HOL-28 | Grenade haptics: a click on taking, a faint buzz while held, a pulse on the throw, a click on putting back. | grab 0.85 x, 280 Hz, 25 ms; hold 0.06 x, 55 Hz; throw 0.5 x, 180 Hz, 40 ms; stow 0.6 x, 200 Hz, 20 ms; `holdBuzz = 1` | — | [Throwing.cpp](../../../src/core/ik/Throwing.cpp) `GrabClick` ... | unit test (values) |
| HOL-29 | X never throws a grenade on foot. With `[PowerWheel] on = 1` X is the hand power wheel's; with `grenadeButton = 0` the pad flag `kFlagNoGrenadeButton` keeps X from the game. | `grenadeButton = 0` (old 1 moved once, marker `powerWheelX`) | on foot while throwables are active; other game modes keep X | [Throwables.cpp](../../../src/game/Throwables.cpp) `AdjustPad`; [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp) `kFlagNoGrenadeButton` | [verify] |
| HOL-30 | Throwables run only on foot with a head-tracked world view, active controllers, no calibration and a view anchor. Otherwise a held grenade goes back, the strokes start over and B is the game's again. | game mode 0 | LE1 only: on the sequels the module is "LE1 only" inactive | [Throwables.cpp](../../../src/game/Throwables.cpp) `Tick`, `Inactive` | [verify] |
| HOL-31 | A melee stroke is a hand moving fast in one direction relative to the head (walking and turning add nothing), toward the body's front, with travel. One decision per stroke; the next needs the hand to slow below the speed first. | free hand 3.0 m/s, gun hand (right, weapon out) 3.5 m/s; travel 0.12 m; velocity slope over 0.04 s; faster than 12 m/s is a glitch; forward cosine at least 0; a direction change with cosine under 0.5 starts a new stroke | fed on new controller samples only | [Throwing.cpp](../../../src/core/ik/Throwing.cpp) `SwingDetector::Update`; [Melee.cpp](../../../src/core/ik/Melee.cpp) `StepPhysical`; [Melee.cpp](../../../src/game/Melee.cpp) `PhysicalStrokes` | unit test ([MeleeTests.cpp](../../../tests/MeleeTests.cpp)) |
| HOL-32 | A hand does not strike while: it holds a grenade; either hand let go of a grenade under 0.5 s ago; (right) the trigger is held with the weapon out (firing), the hand is in a weapon slot, or a holster draw was under 0.5 s ago; (left) it holds the support grip. The stroke is refused and spent. | 0.5 s after a release, 0.5 s after a draw; trigger 0.5 | — | [Throwables.cpp](../../../src/game/Throwables.cpp) `Tick` (`suppressed`); [Melee.cpp](../../../src/game/Melee.cpp) `PhysicalStrokes` | unit test (rules) |
| HOL-33 | Which melee: with the weapon out, either hand's stroke is the game's melee with it (`SFXGameModeDefault.TryMelee`: pistol whip for quick slot 0, rifle butt otherwise). With it away only the right FIST (grip and trigger held) punches, through `BioPlayerInput.ActivatePower(Pistol_Whip)` at the enemy; the left hand never punches; an open right hand does not. | fist: grip 0.6 and trigger 0.5 or more; `meleeFist = 1` | — | [Throwing.cpp](../../../src/core/ik/Throwing.cpp) `RouteMelee`, `Refusal`; [Melee.cpp](../../../src/game/Melee.cpp) `DispatchWeapon`, `DispatchFist` | unit test (route); `mevr melee punch` fake headset 2026-11-05 (research/20 "Gesture only") |
| HOL-34 | The dispatcher refuses, in this order: combat disabled; the route's refusal (HOL-33); sprinting; switching weapons, using a power or throwing a grenade; the cooldown; no enemy in reach. A refusal is logged, with no haptic. | — | `[Throwables] melee = 1` | [Melee.cpp](../../../src/game/Melee.cpp) `Strike` | [verify] |
| HOL-35 | Both hands share one cooldown, which also waits out the game's own melee power's cooldown read live, so the gesture never swings faster than the game. | `meleeCooldownS` 0.8 from the last strike the game swung; the game's 0.96 s pistol whip, 1.38 s rifle butt (settings comment; read live) | — | [Throwing.cpp](../../../src/core/ik/Throwing.cpp) `MeleeWait`; [Melee.cpp](../../../src/game/Melee.cpp) `Strike`, `FindMeleePower` | unit test (wait) |
| HOL-36 | A strike needs a living hostile in reach in front of the body (the body frame's yaw, not the head's). The refusal names the nearest hostile. | `meleeReachM` 2.0 (flat distance), `meleeConeDeg` 75, under 2.0 m height difference; `meleeNeedsEnemy = 1` | — | [Melee.cpp](../../../src/game/Melee.cpp) `FindEnemy` | [verify] |
| HOL-37 | A strike is a tick in the striking hand; damage seen on the target afterwards is a thump in the same hand. | tick 0.45 x, 140 Hz, 30 ms; thump 1.0 x, 80 Hz, 70 ms; target's health watched 1.2 s | — | [Melee.cpp](../../../src/game/Melee.cpp) `Strike`, `WatchHit` | [verify] |
| HOL-38 | B never melees on foot in VR: the pad flag `kFlagNoMeleeButton` keeps B from the game while throwables are active. B is squad placement's; placement stands down only while B really reaches the game. | `meleeButton = 0` (old 1 moved once, marker `meleeFist`) | on foot (HOL-30); needs a host with the flag (HostServices v14), else B still reaches the game and a warning is logged | [Melee.cpp](../../../src/game/Melee.cpp) `AdjustPad`, `BIsGameButton`; [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp) `kFlagNoMeleeButton` | [verify] |

# Arbitration

Order within one mod tick ([Mod.cpp](../../../src/mod/Mod.cpp)): `throwables::Claim`, then the vent's claim,
`holsters::Tick`, `power_wheel::Tick`, `squad_weapons::Tick`, `throwables::Tick` (which feeds the melee strokes),
`melee::Tick`, `vent::Tick`, `support_grip::Tick` (publishes the pad flags after `holsters::AdjustPad`,
`throwables::AdjustPad`, `melee::AdjustPad`), `voice::Tick`, `squad_orders::Tick`.

| input | first claim | then | the loser sees |
|---|---|---|---|
| right grip, new squeeze | grenade slot (`throwables::Claim`) | weapon slot (`Machine::Update`), else "takes hold of whatever is out" | a grenade's squeeze reaches the holster machine as a squeeze away from every slot: a gun that is out stays out, a running return is cancelled; the game never sees RB |
| right grip, release | grenade machine if it holds one (throw or put back) | holster or return (HOL-11) | while the right hand is "busy" with a grenade the holster machine sees the grip as still down |
| left grip, new squeeze | the host's support grip zone (never reported as "away from the gun") | grenade slot, then the vent, the voice radio, squad orders | squad orders, the vent and voice each test `throwables::ClaimedLeft()` and stand down |
| right trigger, weapon away | holsters refuse the game's draw silently (HOL-16) | with the grip also held it is the fist for a stroke (HOL-33) | the game's `FireWeapon` runs without its draw |
| right trigger, weapon out | the game fires | the right hand's stroke is suppressed (HOL-32); a pending return waits (HOL-12) | no strike while firing, no holster mid-burst |
| X on foot | the hand power wheel (`[PowerWheel] on = 1`) | — | the game never sees X as its grenade button (HOL-29) |
| B on foot | squad placement | — | the game never sees B as melee (HOL-38) |
| right stick click | 0.3 s hold: the mod opens the power wheel (HOL-21) | both clicks together belong to the host's chord (snapshot tap, menu hold) | never RB; a tap does nothing |
| a fast right-hand reach to a slot | the holster grab | — | no melee stroke in a slot or 0.5 s after a draw (HOL-32) |

# Problems ME1 already solved

| problem | fix | where |
|---|---|---|
| A belly slot for the shotgun sat in front of the right hand's rest and was entered a dozen times a session on the way to the hip. | The shotgun moved to the left hip, a cross-draw (2026-10-25). | [research/19](../../../research/19-body-holsters.md) "Default slots"; [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) |
| Slots that hang off the head move when the player looks down at the hip. | Anchor = the eye upright over its neck pivot; yaw from the torso, held while the hand is in a slot. | research/19 "Body frame" |
| Crouching shrank the layout when the scale followed the current head height. | Scale from the standing eye height (calibration, else the floor's slow-rising maximum). | research/19 section 8 |
| The grip as both "hold the gun" and RB cast powers on every squeeze. | The grip holds the gun and is never RB; RB moved to the right stick click. | research/19 sections 4, 6 |
| The game's RB casts the mapped power for any press under its own 0.3 s, so a tapped stick click cast powers. | The mod calls `EnterPowerWheel` / `ExitPowerWheel` itself after a 0.3 s hold; the click is never RB (`kFlagModPowerHold`). | research/19 "Hold only (2026-11-05)" |
| A holster asked for during the draw's animation was ignored by the game (`TryHolster` mid-switch does nothing). | The return waits for `IsSwitchingWeapons` to end, then acts with no grace if let go at the slot. | [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) `UpdateHolding`; research/19 section 4 |
| `TryHolster` is a toggle: called with the weapon away it draws. | It is called only while drawn (`Holster` returns early otherwise). | research/19 "What we call" |
| "Only holsters draw" did nothing for ten days: FFrame is packed on 4 bytes, `Node` sits at 0x14, and the skip handler read 0x18. | `engine::adapter::kFrame`; the skip consumes the call's end-of-parameters token. | [fframe-packed-on-four-bytes](../gotchas/fframe-packed-on-four-bytes.md); research/19 "The trigger's draw" |
| A skip handler on the binding's exec never ran: bindings reach their exec through ProcessEvent, not the script VM's CallFunction. | Hook the function the exec calls (`DrawPrimaryWeapon`), and match the caller's frame. | [binding-execs-come-through-process-event](../gotchas/binding-execs-come-through-process-event.md) |
| ME1's `SwitchToWeapon` returns at once where combat is disabled, but the grab was felt as a draw. | `IsCombatEnabled` asked first; a refusal buzz instead (2026-11-17). | [Holsters.cpp](../../../src/game/Holsters.cpp) `Draw` |
| A squeeze held through a pause or menu was taken as a new grab, or a held grip turned into RB when the wheel opened. | Our own latch kept whatever else happens; the host counts kept squeezes; grip holds through modes 0, 2, 3. | [Holsters.cpp](../../../src/game/Holsters.cpp) `Tick` |
| The runtime's grip button lets go late, so thrown grenades left the hand after the arm had slowed. | Analog early release below the squeeze's peak during a fast motion; release velocity = the fastest moment of the last 120 ms. | [research/20](../../../research/20-melee-and-grenades.md) section 1 "Throwing elsewhere" |
| ME1's grenade is a disc that flies straight at its own speed toward the camera's trace, not a ballistic object. | Post-handler re-aims the disc from the hand at the traced landing point of the hand's arc. | research/20 "The projectiles", "Grenades" |
| ME1 throws only with a weapon out; `UseGrenade` with the weapon away only draws. | Taking a grenade draws (let through the holster rule); the throw waits up to 2 s for the weapon. | research/20 "`SFXGameModeDefault.UseGrenade()`" |
| A throw is a fast forward stroke and struck as a melee; so did a fast reach to a holster. | Strokes suppressed 0.5 s after a release and after a draw, and while in a slot. | [Throwables.cpp](../../../src/game/Throwables.cpp); [Melee.cpp](../../../src/game/Melee.cpp) |
| `TryMelee` with the weapon away only draws the gun. | The fist punches through `ActivatePower(Pistol_Whip)` directly. | research/20 "Gesture only (2026-11-05)" |
| The gesture struck faster than the game swings; strikes were lost in the game's cooldown. | One shared cooldown that also waits out the melee power's own, read live. | research/20 "Gesture only" |
| Two gesture-to-melee paths (ME1's in Throwables, ME3's in Melee) drifted apart. | One gesture core and one dispatcher with per-game adapters (2026-11-17); ME1's detectors compared in a test. | research/20 "The trilogy's one path"; [ledger](../../trilogy-parity-ledger.md) Forks 12 |
| ProcessEvent skips natives with an opcode of their own (`Trace`). | Every call through `ue3::runtime::Call`. | [process-event-skips-opcode-natives](../gotchas/process-event-skips-opcode-natives.md) |
| LExSDKv2's generated wrappers corrupt the UFunction's flags. | Never a generated wrapper: ProcessEvent directly. | [lexsdk-function-wrappers](../gotchas/lexsdk-function-wrappers.md) |

# Code map

| part | file | scope |
|---|---|---|
| slot geometry, grab/holster machine, contact haptics, standing height | [BodySlots.hpp](../../../src/core/ik/BodySlots.hpp), [BodySlots.cpp](../../../src/core/ik/BodySlots.cpp) | `src/core`, game-independent; the SMG slot and `LostHand` are the sequels' |
| release velocity, grip latch, grenade machine, swing detector, melee route and wait, arc | [Throwing.hpp](../../../src/core/ik/Throwing.hpp), [Throwing.cpp](../../../src/core/ik/Throwing.cpp) | `src/core`, game-independent |
| gesture core (Physical / Staged rules) | [Melee.hpp](../../../src/core/ik/Melee.hpp), [Melee.cpp](../../../src/core/ik/Melee.cpp) | `src/core`, game-independent; ME1 uses `Rules::Physical` |
| holsters: frame, draw, holster, only-holsters-draw, power hold | [Holsters.hpp](../../../src/game/Holsters.hpp), [Holsters.cpp](../../../src/game/Holsters.cpp) | compiled for every game (ledger: run on LE1, LE2, LE3); `SwitchToWeapon`, the quick slots, the `DrawPrimaryWeapon` skip, its caller lists and the seated wheel callers are inside `MEVR_GAME_LE1` |
| grenades, and ME1's feed of the melee strokes | [Throwables.hpp](../../../src/game/Throwables.hpp), [Throwables.cpp](../../../src/game/Throwables.cpp) | LE1 only (ledger: run / out / out; the tick's body is inside `MEVR_GAME_LE1`) |
| melee dispatcher and adapters | [Melee.hpp](../../../src/game/Melee.hpp), [Melee.cpp](../../../src/game/Melee.cpp) | compiled for every game; `Strike`, `FindEnemy`, `PhysicalStrokes` shared; ME1's adapter (`Drawn`, `Busy`, `FindMeleePower`, `DispatchWeapon`, `DispatchFist`) inside `MEVR_GAME_LE1` |
| settings, defaults, migrations | [HandCalibration.hpp](../../../src/game/HandCalibration.hpp), [HandCalibration.cpp](../../../src/game/HandCalibration.cpp) | every game; `GameHolsterLayout` and the ini block differ by `MEVR_GAME_LE1` |
| pad flags | [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp) | every game; consumed by the host's XInput hook ([HalcyonBridge.cpp](../../../src/plugin/HalcyonBridge.cpp)) |
| skip handler mechanism | [CallFunction.hpp](../../../src/engine/CallFunction.hpp) | every game |
| tick order, links | [Mod.cpp](../../../src/mod/Mod.cpp) | every game |

HostServices used: `rightGripFree` and `hapticPulse` (v9), `hapticPulseHz`, `hapticContinuous`, the floor and
`kFlagRightGripHolds` (v10), `leftGripAway` (v11), `kFlagNoMeleeButton` / `kFlagNoGrenadeButton` (v14),
`kFlagModPowerHold` (a host of 2026-11-05 or later). Host and mod deploy together.

# Settings and defaults

All in `ASI\LE1MEVR.ini`, read by the mod (`hand_calibration`, re-read at most once a second). No row of our menu
edits `[Holsters]` slots; the menu's Controls page reaches the left trigger's weapon wheel and the play position
(Body & calibration) only [verify the full row list in `core/vr/MenuSettings`].

| section | key | default | note |
|---|---|---|---|
| Holsters | enabled | 1 | |
| Holsters | gripHolds | 1 | 0: the old mapping, the grip is RB except at a slot |
| Holsters | releaseGrace | 0.2 | seconds; -1 never. The struct's initializer in `HandCalibration.hpp` still says 0.3; the file block and the migration write 0.2 |
| Holsters | powerWheel | r3 | r3, y, x, b, none |
| Holsters | weaponWheel | 1 | 0: LB off while holsters are on; forced 1 seated with `seatedWeaponWheel` |
| Holsters | onlyHolstersDraw | 1 | |
| Holsters | holster | 1 | 0 never by hand, 1 own slot, 2 also at a slot with no weapon carried |
| Holsters | eyeHeightFromFloor / eyeHeightM | 1 / 1.65 | |
| Holsters | hapticAmplitude | 0.5 | 0..1; 0.5 = as designed |
| Holsters | hipRight, shoulderRight, hipLeft, shoulderLeft | HOL-05 | weapon, right, up, forward, radius |
| Holsters | seatedHipRight ... seatedGrenade | HOL-19 | fixed metres |
| Holsters | seatedWeaponWheel | 1 | |
| Throwables | enabled | 1 | also gates the melee gesture and B's rule |
| Throwables | hands | both | right, left, both |
| Throwables | slot | -0.13, -0.33, 0.00, 0.10 | |
| Throwables | throwSpeedMinMS / throwGain / throwSpeedMaxMS | 1.5 / 2.5 / 15 | |
| Throwables | flight / fromHand / drawForThrow / earlyRelease | disc / 1 / 1 / 0.3 | |
| Throwables | grenadeButton / powerWheelX | 0 / 1 | `powerWheelX` is the one-time migration's marker |
| Throwables | melee | 1 | |
| Throwables | meleeSpeedMS / meleeGunSpeedMS / meleeTravelM | 3.0 / 3.5 / 0.12 | |
| Throwables | meleeCooldownS | 0.8 | |
| Throwables | meleeNeedsEnemy / meleeReachM / meleeConeDeg | 1 / 2.0 / 75 | |
| Throwables | meleeButton / meleeFist | 0 / 1 | |
| Throwables | hapticAmplitude / holdBuzz | 0.5 / 1 | |

# Checks

- Logs: `holsters:` (every enter, leave, grab, refusal, return, eye height source, each refused game draw with its
  caller), `throwables:` (slot, grab, release speed, landing, launch delay), `melee:` (each stroke that was no strike
  and why, each strike with its call and target, the hit watch).
- Snapshot: `"mod"."holsters"` (slots with centres and distances, phase, `returnLeft`, eye height and its source,
  `powerHold`), `"mod"."throwables"` (slot, holder, `lastThrow`, `melee`).
- Console: `mevr weapon pistol|shotgun|assault|sniper` (the holsters' own draw, no grab), `mevr melee punch|strike`
  (the right hand's strike through `Strike`, no controllers), `mevr calltrace 10 drawprimary fireweapon -canfire` (which
  route the game takes to a draw).
- Unit tests (`mevr-tests`): body slots, release velocity, grip latch, grenade machine, swing detector in
  [Main.cpp](../../../tests/Main.cpp); seated layout in [BodySlotsSeatedTests.cpp](../../../tests/BodySlotsSeatedTests.cpp);
  the gesture core against ME1's two detectors in [MeleeTests.cpp](../../../tests/MeleeTests.cpp).
- No offline preview: the slots are not drawn.

# Citations

- [research/19: body holsters](../../../research/19-body-holsters.md): conventions, how LE1 switches weapons, the
  design, grip holds (section 4), where RB went (6), haptics (7), eye height (8), seated reach (12).
- [research/20: melee and grenades](../../../research/20-melee-and-grenades.md): how LE1 starts a melee and a grenade
  (section 2), the design (3), "Gesture only", "The trilogy's one path".
- [research/25: calibration](../../../research/25-calibration.md): the standing eye height and the play position.
- [docs/trilogy-parity-ledger.md](../../trilogy-parity-ledger.md): G42, G43, Forks 12, the modules' run/out table.
- [docs/trilogy-plan.md](../../trilogy-plan.md): the owner's questions 13 to 16.
- Gotchas: [fframe-packed-on-four-bytes](../gotchas/fframe-packed-on-four-bytes.md),
  [binding-execs-come-through-process-event](../gotchas/binding-execs-come-through-process-event.md),
  [process-event-skips-opcode-natives](../gotchas/process-event-skips-opcode-natives.md),
  [lexsdk-function-wrappers](../gotchas/lexsdk-function-wrappers.md).
- Source: [BodySlots.hpp](../../../src/core/ik/BodySlots.hpp), [Throwing.hpp](../../../src/core/ik/Throwing.hpp),
  [core Melee.hpp](../../../src/core/ik/Melee.hpp), [Holsters.cpp](../../../src/game/Holsters.cpp),
  [Throwables.cpp](../../../src/game/Throwables.cpp), [game Melee.cpp](../../../src/game/Melee.cpp),
  [HandCalibration.cpp](../../../src/game/HandCalibration.cpp), [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp).
