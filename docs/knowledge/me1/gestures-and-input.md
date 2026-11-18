---
type: System
title: ME1 gestures and input arbitration
description: Every physical input and hand gesture on ME1 - what it does, who owns it, the order features are asked, and what the game is kept from seeing - as numbered contracts.
tags: [me1, input, gestures, arbitration, gamepad, catalog]
status: stable
timestamp: 2026-11-18T07:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T07:00:00Z }
game: LE1
---

# What the player gets

- The motion controllers reach the game as Xbox pad 0. On foot most buttons no longer mean what the pad meant: the
  hands do the work.
- Right hand: the trigger fires, the grip holds the gun (it is never a button), A uses, B held places the squad, the
  stick turns, the stick click held opens the game's power wheel.
- Left hand: the trigger opens the squad's weapon wheel, the grip takes the gun's foregrip, a grenade, the radio at the
  ear, a hot gun's receiver, or points a squad order; X is the hand power wheel; Y is the game's Y; the stick walks;
  the stick click crouches.
- Both stick clicks together: a tap is a snapshot, a hold opens our menu.
- Gestures without buttons: a fast stroke is melee, a push of the open left hand casts, a palm on the chest, the gun or
  over the head casts a self power, a fingertip on the left wrist opens the omni-tool's rail.
- A squeeze, pull or press keeps the meaning it began with until it is let go. An action never fires because a hand
  drifted into another zone mid-squeeze.
- In menus, conversations and the Mako the pad is the game's again, except where a table below says otherwise.

# Contracts

Controller words: `primary` is A (right) / X (left), `secondary` is B (right) / Y (left). Game modes: 0 on foot, 1 the
Mako, 2 and 3 the game's power and weapon wheels, 5 conversation, 7 GUI, 9 galaxy map, 10 minigame.

## The mapping layer

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| INP-01 | The base mapping: sticks to sticks, right trigger RT, A to A, Y to Y, stick clicks to L3 / R3, left menu to Start (with the left grip squeezed: Back). A real gamepad keeps working beside the controllers: buttons combined, each stick and trigger from whichever is pushed further. | grip latch on 0.6, off 0.4 (triggers as buttons the same) | always; nothing in the mod depends on a menu or system button | [GamepadMapping.cpp](../../../src/game/GamepadMapping.cpp) `FromControllers`, `Merge` | unit test (tests/Main.cpp, [PadMergeTests.cpp](../../../tests/PadMergeTests.cpp)) |
| INP-02 | Bindings per interaction profile. Touch: everything, with touch sensors. Index: A / B on each hand are primary / secondary, no menu button, sensors. HP Reverb G2: as Touch, menu on both hands. WMR: squeeze is a click, trackpad click is primary (A / X), no secondary (no B, no Y). Vive wand: trackpad is the stick and its click, squeeze a click, no A / B / X / Y. `khr/simple_controller`: select is the trigger, no grip, no sticks. | six profiles suggested; G2 only when the runtime lists `XR_EXT_hp_mixed_reality_controller`; touch sensors suggested with a fallback without them | a limited profile logs `halcyon bridge: controller profile ...: <what's missing>` | [Input.cpp](../../../src/core/xr/Input.cpp) | headset (Touch, PS VR2 Sense); others [verify] |
| INP-03 | The mod's options travel to the host as flags every tick (`setSupportGrip`), built in a fixed order: support grip's base, then holsters, squad weapons, power wheel, throwables, melee, comfort, squad placement, Mako controls, cockpit panel. A later module can only restrict further. The host uses them while fresh. | 27 flag bits (table below) | stale flags: default options (the plain pad) | [SupportGrip.cpp](../../../src/game/SupportGrip.cpp) `Tick`; [PadMerge.cpp](../../../src/plugin/PadMerge.cpp) `Options`, `Merge` | unit test |
| INP-04 | A press keeps its meaning. Each right-grip squeeze, RB-source press, Mako control button and stick click decides what it is when it goes down and keeps that until released, whatever the options say meanwhile. | - | - | [GamepadMapping.cpp](../../../src/game/GamepadMapping.cpp) (`rightFree`, `faceRb`, `makoAs`, `rightClickKept`) | unit test |
| INP-05 | Controls in use when a mod wheel or menu closes stay kept from the game until each is released or centred: the flick that chose a weapon does not turn, the A that confirmed does not "use", the X that equipped does not reach the game, the trigger that clicked Resume does not fire. | stick centred inside 0.25; trigger or grip in use above 0.2; a trigger let go below 30 / 255 after our menu | squad menu / native wheel (`squadHeld`), power wheel (`powerHeld`), our menu and calibration (`CloseLatch`) | [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp) `Latches`, `CloseLatch` | unit test |

## On foot (game mode 0, head-tracked view, tracked controllers)

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| INP-06 | Right trigger: RT, the game fires. It NEVER draws a holstered weapon: `FireWeapon`'s draw is refused, without a buzz. | - | `onlyHolstersDraw = 1` (HOL-16) | [Holsters.cpp](../../../src/game/Holsters.cpp) `DrawRoute::Trigger` | fake headset 2026-11-04 |
| INP-07 | Right grip: holds the gun. A new squeeze in a body slot draws that slot's weapon; held, it carries; let go at the gun's slot it holsters, elsewhere the gun goes back after the grace. The right grip is NEVER RB, at a slot or anywhere else. | `gripHolds = 1`; grace 0.2 s; flag `kFlagRightGripHolds` | modes 0, 2 and 3 (`IsFootOrWheelMode`); in our menu a squeeze is not counted (never draws) | [Holsters.cpp](../../../src/game/Holsters.cpp) `AdjustPad`; HOL-07, HOL-11 | headset 2026-10-22 |
| INP-08 | A right squeeze in the grenade slot takes a grenade and is not a holster grab: the grenades are asked before the holsters. | slot (-0.13, -0.33, 0) r 0.10 m | throwables active; `hands = both` | [Mod.cpp](../../../src/mod/Mod.cpp) (`throwables::Claim` before `holsters::Tick`); HOL-23 | [verify] |
| INP-09 | Right grip and trigger together with the weapon away is a fist: a fast stroke then punches. The left hand never punches; an open right hand does not. | grip 0.6, trigger 0.5 | `meleeFist = 1` (HOL-33) | [Melee.cpp](../../../src/game/Melee.cpp) | fake headset (`mevr melee punch`) |
| INP-10 | Left trigger: a new pull opens the game's weapon wheel at the left hand (the squad's); letting go gives the highlighted entry. The game sees neither LB nor LT from that pull. | pull on 0.6, off 0.4 | `[Squad] enabled`, `[Controls] leftTrigger = lb`, modes 0, 2, 3; not with our menu open, the hand power wheel owning the left hand, or the support grip held. Elsewhere the trigger is the game's LB | [SquadWeapons.cpp](../../../src/game/SquadWeapons.cpp) `OwnTrigger`, `AdjustPad`; PWR-24 | headset |
| INP-11 | The left trigger is NEVER LT on foot by default; LT comes only from the support grip at the shoulder (aimed fire). | `leftTrigger = lb`; LT forced to 255 by the grip | `lt` in the file gives the trigger back as LT (then no squad wheel) | [SupportGrip.cpp](../../../src/game/SupportGrip.cpp); WPN-09 | headset |
| INP-12 | Left grip: NEVER LB and never opens a wheel. A new squeeze is, in this order: (1) a grenade if the hand is in the grenade slot; (2) the vent if the hand is in a hot gun's receiver zone; (3) the support grip if the hand is in the gun's grab zone (decided in the host's mapping, on a new squeeze only); (4) the radio if the hand is at the left ear; (5) a flat squad order with the left aim ray. | zones: HOL-22, WPN-32, WPN-06, PWR-36, PWR-29; `gripShoulder` forced false by squad weapons | (1), (2), (4), (5) need the squeeze to have begun away from the gun (host count `leftGripAway`); while the hand power wheel is open the grip is the wheel's | [Mod.cpp](../../../src/mod/Mod.cpp) tick order; [Throwables.cpp](../../../src/game/Throwables.cpp) `Claim`; [Vent.cpp](../../../src/game/Vent.cpp) `Claim`; [Voice.cpp](../../../src/game/Voice.cpp); [SquadOrders.cpp](../../../src/game/SquadOrders.cpp) | unit tests per rule; order in play [verify] |
| INP-13 | A left squeeze in the support zone is the support grip and never a grenade, a vent, the radio or an order. The exception: a HOT gun's receiver, where the zone is hidden from the mapping so the squeeze vents; a cool gun's receiver is the support grip. | - | the mask applies only while nothing holds the grip | [SupportGrip.cpp](../../../src/game/SupportGrip.cpp) (`vent::MasksSupportZone`) | fake headset |
| INP-14 | A: the game's A (use; what it uses is what the head looks at). With the game's weapon wheel held, A queues the entry. | - | kept from the game while the squad menu is open | [GamepadMapping.cpp](../../../src/game/GamepadMapping.cpp); research/40 | headset |
| INP-15 | B NEVER reaches the game on foot: it does not melee, leave cover or anything else. B is squad placement's. | `meleeButton = 0`; flag `kFlagNoMeleeButton` | while throwables are active on foot (HOL-38); menus and other modes keep B | [Melee.cpp](../../../src/game/Melee.cpp) `AdjustPad` | fake headset (`melee-b` trace on ME2; ME1 [verify]) |
| INP-16 | B held: the squad placement arc from the right hand (with two squadmates the three-tile wheel first); let go: they go there. While B is held the right stick is the selector's and the game sees it centred (no turn). | press under 0.15 s is a blip; over 15 s called off; flag `kFlagSquadPlace` | `[SquadOrders] place = 1`; not in the squad's or the power wheel; stands down while B really reaches the game (`melee::BIsGameButton`) | [SquadPlace.cpp](../../../src/game/SquadPlace.cpp); [SquadPlace.hpp](../../../src/core/vr/SquadPlace.hpp); PWR-33..35 | fake headset (`mevr place`) |
| INP-17 | B double-tapped: the whole squad back to Shepard. | second press within 0.35 s of a blip's release | as INP-16 | [SquadPlace.hpp](../../../src/core/vr/SquadPlace.hpp) `doubleTapS` | unit test |
| INP-18 | X: the hand power wheel. Held it opens at the left palm and equips on release; tapped it stays open. X NEVER reaches the game on foot, tap or hold, also while the wheel is closed, and NEVER throws a grenade. X is never an RB source. | tap time 0.30 s; flags `kFlagHandPowerWheel`, `kFlagNoGrenadeButton` | `[PowerWheel] on = 1`, mode 0, a pawn, a valid view, our menu closed; other modes keep game X; with `on = 0` and `grenadeButton = 1` X is the game's again | [PowerWheel.cpp](../../../src/game/PowerWheel.cpp) `AdjustPad`; [GamepadMapping.cpp](../../../src/game/GamepadMapping.cpp) (`powerHeld`); PWR-01..07 | unit test; headset 2026-11-07 |
| INP-19 | While the hand power wheel is open the left stick, L3, X, the left trigger and the left grip are the wheel's; the right hand stays the game's. The squad's trigger stands down. | flag `kFlagPowerWheel` | wheel open or held after | PWR-06 | unit test |
| INP-20 | Y: the game's Y, unchanged. | - | RB source only if `[Holsters] powerWheel = y` | [GamepadMapping.cpp](../../../src/game/GamepadMapping.cpp) | [verify] what ME1 binds to Y on foot |
| INP-21 | Right stick click held alone: after the hold the mod opens the game's power wheel; letting go closes it. A tap does NOTHING (no cast). The click is never RB. It still reaches ME1 as R3. | hold 300 ms; a press with the stick past 0.5 (turning), with L3 within 100 ms, or as the second click within 400 ms never opens it; a wheel that opens within 1 s after the release is closed | `[Holsters] powerWheel = r3`; flag `kFlagModPowerHold`; LE1 leaves `keepPowerClick` off | [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp) `PowerHoldFromStickClick`, `WatchPowerWheel`; [Holsters.cpp](../../../src/game/Holsters.cpp) `TickPowerHold`; HOL-21 | headset 2026-11-05 |
| INP-22 | Right stick double-click: Halcyon's recenter (as R); neither click opens the power wheel. | press to press within 400 ms | - | [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp) `kRbDoubleClickMs` | headset |
| INP-23 | Left stick click: the game's L3 (crouch toggle), held back to see whether the right click joins. Alone and held it passes; released sooner it reaches the game as a short press afterwards; joined by the right click it never does. | wait 100 ms; tap pulse 80 ms | - | [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp) `GateClick` | unit test |
| INP-24 | Both stick clicks tapped: a snapshot. Both held: our main menu opens or closes (a running calibration is cancelled instead; Halcyon's menu, if open, closes). Never both, never L3 or the power wheel. Only the player's live clicks count, never a replay's. | second click within 100 ms of the first; both up within 300 ms of the first press = tap; both down 400 ms = hold | on LE1 R3 itself is not gated (it always reaches the game) | [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp) `UpdateStickChord`; [HalcyonBridge.cpp](../../../src/plugin/HalcyonBridge.cpp) | headset |
| INP-25 | Right stick X: the game's turn by default; with snap turn the mod turns and the game sees no right-stick X from the controllers (a real pad's still turns). | flag `kFlagModTurns` | never while the squad menu or the game's wheel owns the stick; see MOV-01 in comfort-and-movement | [Comfort.cpp](../../../src/game/Comfort.cpp) `AdjustPad` | headset |
| INP-26 | Right stick flick forward / back under ADS: steps the scope's magnification. Outside ADS a flick is nothing of ours. | past 0.7, re-arm inside 0.3 | WPN-21 | [Sights.cpp](../../../src/game/Sights.cpp) `ZoomFor` | unit test |
| INP-27 | Left stick: walks, toward where the head looks (or the left controller's flat yaw with `walk = hand`). | flag `kFlagHandWalk` | not while the squad menu, the game's wheel or our menu owns it | [PadMerge.cpp](../../../src/plugin/PadMerge.cpp) `WalkTowardHand` | headset |
| INP-28 | Fast stroke of a hand toward an enemy in reach: the game's melee. The gesture is the only melee on foot. | HOL-31..36 | not while firing, holding a grenade or the support grip, at a slot, within 0.5 s of a draw or a grenade release | [Melee.cpp](../../../src/game/Melee.cpp) `Strike` | unit test ([MeleeTests.cpp](../../../tests/MeleeTests.cpp)) |
| INP-29 | Open left hand pushed along its ray with the arm out: casts the equipped power at the lock. Not a cast: a push with the left grip squeezed in the last 0.4 s (a throw, an order's point), or a self-gesture power. | 1.6 m/s over 0.10 m; PWR-13, PWR-22 | a power equipped; wheel closed | [PowerWheel.cpp](../../../src/game/PowerWheel.cpp) `TickLock` | unit test; fake headset |
| INP-30 | Open left palm on the chest, slapped on the gun, or raised over the head: casts the equipped self power of that kind. Never with the left trigger or grip in use; never while or just after the left hand holds the gun. Only a ready power shows a target. | PWR-17..20 | `selfGestures = 1` | [PowerWheel.cpp](../../../src/core/vr/PowerWheel.cpp) `StepSelfCast` | unit test ([PowerWheelTests.cpp](../../../tests/PowerWheelTests.cpp)) |
| INP-31 | Right index fingertip tapped on the left wrist's target: opens or closes the omni-tool's rail. | press within 0.04 m, release past 0.08 m; target 30% from wrist to elbow, lifted 0.035 m | not with a gun drawn; not while the fingertip goes for a wrist screen or the rail | [OmniRail.hpp](../../../src/core/vr/OmniRail.hpp) `TapSettings`, `GoingForPanel`; [OmniToolRail.cpp](../../../src/plugin/OmniToolRail.cpp) | fake headset 2026-11-04; headset [verify distances] |
| INP-32 | Physical crouch: the head below a share of the standing eye height crouches Shepard, above it stands him. L3 still toggles in between. | below 0.75, above 0.85, dwell 0.2 s | `physicalCrouch = 1`; on physical changes only | [Posture.cpp](../../../src/game/Posture.cpp) | headset |
| INP-33 | The game's own draws are refused with the weapon away: the melee and grenade buttons, the weapon wheel's entry, the holster key, the trigger. Medi-gel and powers still draw. | HOL-15..17 | `onlyHolstersDraw = 1` | [Holsters.cpp](../../../src/game/Holsters.cpp) | fake headset 2026-11-04 |
| INP-34 | Nothing on foot uses: both grips as a chord, a controller menu button, the right grip as a button, the left trigger as LT, a tap of R3. | - | by design | this table | - |

## In the Mako (game mode 1)

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| INP-35 | Without a held control the pad is the game's, with two exceptions while the mod drives (`makoDrive`): the left stick is not the game's, and the left hand's X, Y and trigger and the right hand's A reach the game as nothing unless the press began on a control. | flags `kFlagMakoDrive`, `kFlagMakoLever`, `kFlagMakoStick` | `[Mako] controls = 1` | [GamepadMapping.cpp](../../../src/game/GamepadMapping.cpp) (`makoDrive` block); VEH-15..17 | fake headset; headset [verify] |
| INP-36 | Left grip on the lever: X is the game's A (jets), Y the game's LB (boost), L3 the repair. Right grip on the stick: trigger RT (machine gun), A the game's RB (cannon), the thumbstick the turret's (kept from the game until centred). A squeeze at a control is never RB or LB. | grab by a new squeeze within reach | VEH-09, VEH-15, VEH-16, VEH-28 | [MakoControls.cpp](../../../src/game/MakoControls.cpp) `AdjustPad` | fake headset |
| INP-37 | The left trigger is the game's LB in the Mako (the squad does not take it there); off the lever, while the mod drives, it is nothing. | - | mode 1 | [GamepadMapping.cpp](../../../src/game/GamepadMapping.cpp); [SquadWeapons.cpp](../../../src/game/SquadWeapons.cpp) (`modeTakesIt`) | unit test |
| INP-38 | A right squeeze at the cockpit's overhead handle takes the handle (not RB); a fingertip on the repair cap presses it. While a hand works the panel the gunner keeps the turret. | VEH-26, VEH-29 | riding in the cockpit | [CockpitPanel.cpp](../../../src/game/CockpitPanel.cpp) `AdjustPad` | fake headset |
| INP-39 | No holsters, grenades, melee, vent, hand power wheel, squad placement, snap turn or body slots in the Mako. | - | mode != 0 | each module's `Inactive` | code |

## Menus, conversations and screens

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| INP-40 | Our main menu open: the game sees a neutral pad and the mod no controllers. A controller's aim ray is the mouse (trigger clicks, right stick scrolls); the left stick or d-pad moves the focus, A activates, B backs out then closes. Whatever is down as it closes stays kept until released. | - | also during the guided calibration | [MainMenu.cpp](../../../src/plugin/MainMenu.cpp); `CloseLatch` | headset |
| INP-41 | A game screen on a flat or world panel: a controller's aim ray is the game's mouse, the trigger the left button (latched), the right stick the wheel; a fingertip touch takes over from the rays near a screen. While a ray is on a screen the pad's triggers are kept from the game. | trigger latch 0.6 / 0.4; wheel 6 per second; touch hover 0.06 m, press 0.005 m, release 0.015 m | not while our menu, Halcyon's menu or the calibration is up; off during the bypass puzzle (d-pad only) | [GamePointer.hpp](../../../src/core/vr/GamePointer.hpp); [GamePointer.cpp](../../../src/plugin/GamePointer.cpp) | fake headset; headset [verify] |
| INP-42 | In a conversation (mode 5) the pad is the game's: the left stick steers the dialogue wheel and the game's own buttons pick and skip (which buttons: [verify], not read in code). No on-foot gesture runs. In a first-person conversation the left stick walks only while the wheel is down and only after it has been let go once following a pick. | - | mode 5; see conversations-and-scenes | [ConversationWorld.hpp](../../../src/core/vr/ConversationWorld.hpp) `StickWalks` | headset 2026-11-10 |
| INP-43 | In the game's weapon wheel held by the left trigger (mode 3) the right stick steers once centred, A queues, B closes; the left stick, L3 and R3 are kept from the game. | flag `kFlagNativeWheel` | PWR-25 | [GamepadMapping.cpp](../../../src/game/GamepadMapping.cpp) (`nativeWheel`) | unit test |
| INP-44 | Keyboard: `\` opens our menu, Ctrl or Shift + `\` Halcyon's; R recenters, K toggles first person; End a snapshot, Home recording. All only with a game window in front. | - | LE1; our own VR hotkeys are off | [HalcyonBridge.cpp](../../../src/plugin/HalcyonBridge.cpp) `KeyDown` | headset |

## Flags across the host / mod boundary (`gamepad::kFlag...`)

| flag | bit | meaning | set by |
|---|---|---|---|
| `kFlagLeftTrigger` | 0 | the left trigger reaches the game as LT | support grip (`[Controls] leftTrigger`) |
| `kFlagSupportAims` | 1 | a held support grip forces LT | support grip (shoulder, not a sniper) |
| `kFlagGripShoulder` | 2 | a left grip away from the gun is LB | off on ME1 (squad weapons clear it) |
| `kFlagLeftTriggerShoulder` | 3 | the left trigger is LB | on by default; cleared per pull by the squad |
| `kFlagRightGripFree` | 4 | a new right squeeze is not RB | grenade slot, Mako stick, cockpit handle |
| `kFlagRightGripHolds` | 5 | the right grip is never RB | holsters |
| `kFlagSquadMenu` | 6 | the custom squad menu owns sticks, A, B, clicks | squad weapons |
| `kFlagNativeWheel` | 7 | the game's weapon wheel is held | squad weapons |
| `kRbSourceShift` | 8-11 | which control is RB | holsters (`powerWheel`) |
| `kFlagWheelPressA` | 12 | send A to confirm the wheel | squad weapons |
| `kWheelStickShift` | 13-14 | which stick steers the wheel | squad weapons |
| `kFlagNoMeleeButton` | 15 | B kept from the game | melee |
| `kFlagNoGrenadeButton` | 16 | X kept from the game | power wheel, throwables |
| `kFlagModTurns` | 17 | no right-stick X for the game | comfort |
| `kFlagHandWalk` | 18 | left stick follows the left hand's yaw | comfort |
| `kFlagMakoDrive`, `kFlagMakoLever`, `kFlagMakoStick` | 19-21 | the Mako's controls | Mako controls |
| `kFlagCockpitLook` | 22 | head look in mode 1 | support grip tick (cockpit riding) |
| `kFlagPhysicalCrouch` | 23 | the crouch camera keeps the real head's height | support grip tick (posture) |
| `kFlagPowerWheel` | 24 | the hand power wheel is open | power wheel |
| `kFlagModPowerHold` | 25 | the mod opens the power wheel from R3's hold | holsters |
| `kFlagSquadPlace` | 26 | right stick is the placement selector's | squad placement |
| `kFlagHandPowerWheel` | 27 | X reserved on foot | power wheel |

Host-only options, never across the boundary: `gateRightClick`, `keepPowerClick` (both false on LE1), `countKept`
(cleared while our menu is open), `stickClicksReserved` (unused).

# Arbitration

| input | order asked (first wins) | what the loser and the game see |
|---|---|---|
| new right grip squeeze | grenade slot (`throwables::Claim`) > body slot (`holsters::Tick`) > holding the gun | never RB; a squeeze at a slot whose weapon is not carried is the refusal buzz |
| new left grip squeeze | open hand power wheel (confirms the pick) > support zone (host mapping) > grenade slot > hot receiver (vent) > ear (radio) > free (squad order) | never LB; the support grip squeeze is not counted as "away", so no mod feature sees it |
| left trigger pull | open hand power wheel (pinch equips) > squad weapon wheel (modes 0, 2, 3) > the game's LB | no LB / LT for a pull the squad owns; in a conversation or the Mako the game's own |
| X | hand power wheel (always, on foot) | the game never sees X on foot |
| B | squad placement (hold, double tap) | the game never sees B on foot |
| right stick click | both-sticks chord (tap, hold) > double-click recenter > the mod's 0.3 s hold | R3 reaches ME1 regardless; never RB |
| left stick click | both-sticks chord > the game's L3 after 100 ms | - |
| right stick | squad menu / native wheel > placement selector (B held) > Mako turret (stick held) > snap turn (X only) > zoom flick (Y, under ADS) > the game | the game sees it centred while a mod owner has it, and until centred after |
| left stick | hand power wheel > squad menu / native wheel > Mako drive > walk | - |
| right trigger | our menu / game pointer on a screen > the game's RT | kept from the game while a pointer ray is on a screen |
| a hand's fast motion | grenade throw (held grenade's release) > vent rack (left, at a hot receiver) > melee stroke > power push (open left hand, no grip for 0.4 s) | one decision per stroke; a refused stroke is spent |

# Problems ME1 already solved

| problem | fix | where |
|---|---|---|
| The right grip as RB cast the mapped power on every short squeeze at a holster | the grip holds the gun and is never RB | [research/19](../../../research/19-body-holsters.md) "The right grip holds the gun" |
| RB's tap casts the mapped power for any press under 0.3 s | the mod calls `EnterPowerWheel` / `ExitPowerWheel` itself; no RB | research/19 "Hold only" |
| The left grip opened the weapon wheel whenever the hand was off the foregrip | the grip never opens it; the wheel moved to the left trigger | [squad weapons design](../../squad-weapons-design.md) |
| The game's bindings run through ProcessEvent timers, so a CallFunction skip never saw them | hook both; see the gotcha | [gotcha](../gotchas/binding-execs-come-through-process-event.md) |
| The draw skip failed silently for weeks | FFrame is packed on 4 bytes (`Node` at 0x14) | [gotcha](../gotchas/fframe-packed-on-four-bytes.md) |
| The trigger drew the weapon through `FireWeapon` | refused by caller, no buzz | research/19 "The trigger's draw" |
| B's `TryMelee` with the weapon away only drew the gun | B off; the fist calls `ActivatePower(Pistol_Whip)` | [research/20](../../../research/20-melee-and-grenades.md) "Gesture only" |
| The X that equipped a power threw a grenade as the wheel closed | `powerHeld`; X reserved on foot even while closed | [research/55](../../../research/55-hand-cast-powers.md) |
| A snapshot tap crouched or opened the power wheel | L3 held back 100 ms; R3's hold needs 300 ms alone | `UpdateStickChord`, `GateClick` |
| The A or trigger that closed our menu acted in the game | `CloseLatch` | [research/28](../../../research/28-main-menu.md) |
| A fake Start press never reached the game | the menu's top bar calls the game's own functions | research/28 "The game's menus" |
| The Mako's jets fired with the hand off the lever; A off the stick jumped | buttons count only when the press began on a control | [research/46](../../../research/46-mako-controls.md) |
| A vent squeeze was taken by the support grip | the zone is hidden from the mapping at a hot receiver | [research/45](../../../research/45-vent.md) |
| The wrist tap fired while reaching for a wrist screen | `GoingForPanel` | [research/57](../../../research/57-omni-tool-menu.md) |
| A stick still out as the dialogue wheel closed walked Shepard | the stick walks only once let go after a pick | [research/69](../../../research/69-conversations-everywhere.md) |
| Mod settings appended after XR was ready were missed | see the gotcha | [gotcha](../gotchas/mod-settings-appended-after-xr-ready.md) |

# Code map

| part | file | scope |
|---|---|---|
| pad mapping, flags, chords, latches | [game/GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp), [.cpp](../../../src/game/GamepadMapping.cpp) | pure, no game guards; used by all three hosts and mods |
| actions and profiles | [core/xr/Input.cpp](../../../src/core/xr/Input.cpp) | game-independent |
| the mod's XInput hook (our own renderer) | [game/GamepadBridge.cpp](../../../src/game/GamepadBridge.cpp) | every game; unused on LE1 while Halcyon's host owns VR |
| host merge | [plugin/PadMerge.cpp](../../../src/plugin/PadMerge.cpp) | all three hosts; `ConversationTriggers`, `gateRightClick`, `keepPowerClick` are the sequels' |
| chord, keys, menu toggle | [plugin/HalcyonBridge.cpp](../../../src/plugin/HalcyonBridge.cpp) | LE1 host (the sequels: `plugin/MenusLe2.cpp`) |
| flag assembly and tick order | [game/SupportGrip.cpp](../../../src/game/SupportGrip.cpp), [mod/Mod.cpp](../../../src/mod/Mod.cpp) | every game |
| owners | holsters, throwables, melee, vent, squad weapons, power wheel, squad orders, voice, squad placement, comfort, posture, Mako controls, cockpit panel (each `game/<Name>.cpp`) | every game compiles them; vent, throwables' grenades, the Mako and cockpit run on LE1 only |
| HostServices | `setSupportGrip` (flags), `rightGripFree` (v9), `leftGripAway` (v11), `controllers` | see [the ledger](../../trilogy-parity-ledger.md) |

# Settings and defaults

| section | key | default | read by | menu row |
|---|---|---|---|---|
| Controls | `leftTrigger` | lb | mod | file |
| Controls | `leftGripAway` | none | mod | file |
| Controls | `supportGrip` | 1 | mod | file |
| Holsters | `gripHolds`, `powerWheel`, `weaponWheel`, `onlyHolstersDraw` | 1, r3, 1, 1 | mod | file |
| Throwables | `grenadeButton`, `meleeButton`, `meleeFist`, `hands` | 0, 0, 1, both | mod | file |
| PowerWheel | `on` | 1 | mod | [verify] |
| Squad | `enabled`, `style`, `wheelStick` | 1, native, right | mod | VR hands tab (`style`) |
| SquadOrders | `gesture`, `place` | 1, 1 | mod | file |
| Voice | `gesture` | 1 | mod | Voice page |
| Comfort | `turn`, `walk` | game, head | mod | Comfort |
| Hands | `physicalCrouch` | 1 | mod | file |
| OmniTool | `tap`, `tapOnBody` | 1, 1 | host | file |

# Checks

- Logs: `halcyon bridge:` (profile, squad menu, the game's weapon wheel, mod turns), `holsters: the game wanted to draw
  your weapon (<caller>): refused`, `squad: the left trigger ...`, `support grip: the grab zone is hidden from`,
  `test loop: both sticks tapped`.
- Console: `mevr calltrace 10 <word> -<word>` shows which route the game takes for a press.
- Unit tests: `tests/Main.cpp` (mapping, chords, gates, latches), [PadMergeTests.cpp](../../../tests/PadMergeTests.cpp),
  [MeleeTests.cpp](../../../tests/MeleeTests.cpp), [PowerWheelTests.cpp](../../../tests/PowerWheelTests.cpp),
  [SquadPlaceTests.cpp](../../../tests/SquadPlaceTests.cpp), [PostureOwnershipTests.cpp](../../../tests/PostureOwnershipTests.cpp).
- Replays with `-Inputs` (tools/make-wrist-recording.py, make-rack-recording.py) drive gestures on the fake headset.

# Citations

- [research/19](../../../research/19-body-holsters.md), [research/20](../../../research/20-melee-and-grenades.md),
  [research/23](../../../research/23-cover.md), [research/28](../../../research/28-main-menu.md),
  [research/31](../../../research/31-comfort.md), [research/42](../../../research/42-game-pointer.md),
  [research/45](../../../research/45-vent.md), [research/46](../../../research/46-mako-controls.md),
  [research/54](../../../research/54-voice-squad-commands.md), [research/55](../../../research/55-hand-cast-powers.md),
  [research/57](../../../research/57-omni-tool-menu.md).
- Sibling catalog files hold the numbers behind the cross-references: [holsters-throwables-melee](holsters-throwables-melee.md)
  (HOL), [powers-and-squad](powers-and-squad.md) (PWR), [vehicles](vehicles.md) (VEH), [weapons-and-sights](weapons-and-sights.md)
  (WPN).
- Where CLAUDE.md and the code differ: `core/vr/VoiceGesture.hpp`'s header says the radio squeeze is claimed before the
  support grip and the grenade slot; the tick order in `mod/Mod.cpp` asks the grenades and the vent first and only the flat
  orders after the radio. The header comment of `GamepadMapping.hpp` still describes grips as LB / RB and "X (grenade), B
  (melee)"; on ME1 none of those reach the game on foot. CLAUDE.md says the holsters act "on foot"; the grip-holds mapping
  also covers the two wheel modes (2, 3).
