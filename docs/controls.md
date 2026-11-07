<!-- SPDX-License-Identifier: MIT -->
# MEVR controls and controllers

Back to the [README](../README.md).

## Headsets and controllers

**Headsets tried so far**, all with SteamVR as the OpenXR runtime:

- **PS VR2** (with Sony's PC adapter): the headset most test sessions used.
- **Quest 3 over Steam Link:** used until 2026-10-21, before most of today's features existed.
- **Meta's own runtime** (Quest Link, Air Link): only an early build ran on it (a Quest Pro, 2026-10-07), before the
  current renderer. Not tried since.
- **Virtual Desktop** and every other headset: not tried.

Many features added since late October were checked only on SteamVR's simulated headset (the null driver), not yet by
a person wearing one ([docs/release-checklist.md](docs/release-checklist.md) section 2).

**Controllers.** The mod suggests bindings for six interaction profiles (`src/core/xr/Input.cpp`); the runtime picks
one:

| Profile | Controllers | What works |
|---------|-------------|------------|
| `oculus/touch_controller` | Touch (Quest, Rift); the PS VR2 Sense controllers, which SteamVR presents as Touch | Everything. The only ones tried |
| `valve/index_controller` | Valve Index | Everything; the left controller's A and B are X and Y. Not tried |
| `hp/mixed_reality_controller` | HP Reverb G2 (when the runtime offers `XR_EXT_hp_mixed_reality_controller`) | Everything, laid out as on Touch. Not tried |
| `microsoft/motion_controller` | Windows Mixed Reality | The trackpad's click is A; no B, X or Y. Not tried |
| `htc/vive_controller` | HTC Vive wands | The trackpad is the stick; no A, B, X or Y. Not tried |
| `khr/simple_controller` | the runtime's fallback | Trigger and menu only: guns, holsters, walking and turning don't work |

The log names the profile the runtime chose and what's missing on it (`halcyon bridge: controller profile ...`).

## The basics

The controllers reach the game as its gamepad (Xbox pad 0, `src/game/GamepadMapping.*`), with hand gestures on top.
The **grip** is the button under the middle finger, the **trigger** the one under the index finger. Left-handed play
isn't supported yet. Settings named below are in `Game\ME1\Binaries\Win64\ASI\LE1MEVR.ini`; most also have a row in
the VR menu.

## On foot

| Control | What it does |
|---------|--------------|
| Left stick | Walk, toward where you look (or where the left controller points: VR menu, Comfort) |
| Right stick | Turn (the game's own turn; snap turning in the VR menu's Comfort page) |
| Right trigger | Fire |
| Right grip at a holster | Draw that weapon. Keep the grip squeezed to hold the gun. Let go at its holster to put it away; let go anywhere else and it goes back after 0.3 s unless you squeeze again |
| Left grip on the gun | Hold it with both hands (anywhere along the gun). With the gun raised to your cheek the game gives you its aimed fire |
| Left trigger | The weapon wheel, at your left hand. Point your left hand at a squadmate as you pull to start on their weapon; the right stick highlights a weapon; let go of the trigger to give it. B closes the wheel without a change |
| Right stick click, held | The game's own power wheel; steer with the right stick, let go to close it. A quick tap casts nothing |
| Right stick, double-click | Recenter the view |
| Left stick click | Crouch on or off |
| A | Use what you look at (talk, open, pick up). Hold to sprint |
| B, held | Send your squad: an arc leaves your right hand; let go and they hold where it lands (with two squadmates a small wheel at your right hand picks who goes). Double-tap B to call everyone back |
| X | Your power wheel, at your left palm: hold, move your hand toward a power, let go to equip it (or tap X and pick with a left trigger pinch) |
| Y | Medi-gel |
| Both stick clicks, held | Open or close the VR menu (or `\` on the keyboard) |
| Both stick clicks, tapped | Save what each eye sees, for a bug report (`%LOCALAPPDATA%\MEVR\snapshots`) |
| VR menu's top bar | **Game menu** opens the game's pause menu, **Holster** puts the weapon away (no controller menu button is needed) |

The gamepad's d-pad (squad orders) has no controller button.

**Holsters:** the pistol at your right hip, the assault rifle over your right shoulder, the shotgun at your left hip (a
cross-draw with the right hand), the sniper rifle over your left shoulder. The controller buzzes while your hand is in a
holster. Their places scale with your standing height; the calibration measures it. Only a holster draws a weapon on
foot: the game's own buttons don't pull your gun out for you (medi-gel and powers still do).

**With your hands:**

- **Grenade:** reach to the left side of your chest with either hand (the left while the right holds the gun) and
  squeeze the grip: you hold a grenade. Throw it with a quick release; let go slowly and it goes back.
- **Melee:** a fast punch or swing at an enemy within about 2 m in front of you; with your weapon away, a right fist
  (grip and trigger). B no longer melees.
- **Powers:** with the wheel closed, raise your left arm at an enemy (orange brackets lock on) and push your open left
  hand toward it to cast the equipped power. Powers on yourself: the open left palm on your chest, slapped onto the gun
  or raised above your head.
- **Squad orders:** squeeze the left grip away from the gun, point at an enemy, a spot or a squadmate, let go: attack,
  move there or regroup. Or hold the left grip at your left ear, speak the order, and let go.
- **Omni-tool:** tap the target on Shepard's left forearm with your right index finger for a menu of the game's
  screens (squad, equipment, map, journal, codex, the computer) projected from your wrist.
- **Vent:** cools a hot gun at once (a change to Mass Effect 1's rules). When the gun's heat is at least 30%, put your
  left hand on the gun just above and ahead of your right hand (a tick in the controller says you're there), squeeze
  the left grip, pull back fast and let go, like racking a slide. Once every 4 s. `[Vent] enabled = 0` turns it off.
- **Crouch:** crouch for real and Shepard crouches; stand up and he stands. The game's cover snapping is off in VR:
  take cover by crouching or leaning behind things.
- **Use:** look at what you want to use. Its name and action float at the object; the A button appears once you're
  close enough.
- **Fingers:** Shepard's index finger follows your trigger, his other fingers your grip.

## Screens and menus

- **VR menu:** both stick clicks held (or `\`). Point with either controller and pull the trigger, or move with the
  left stick and press A. The right stick scrolls; B goes back, then closes. Its top bar has **Game menu** (the game's
  pause menu) and **Holster**.
- **The game's screens** (shop, loot, equipment, journal, codex, map, pause menu) stand as a panel in the room. Point a
  controller at one (or touch it with a fingertip): its ray is the mouse, the trigger clicks, the right stick scrolls. The gamepad controls work
  there too. The screen's button hints switch to mouse and keyboard once you point; a press of A switches them back.
- **Hacking** (the bypass minigame) is projected from your left wrist, like the omni-tool. Play its puzzle with the
  d-pad beside it (a fingertip, or a ray and the trigger). The other personal screens come from the wrist too: VR menu,
  Comfort > Game screens > Omni-tool screens.
- **Galaxy map:** walk up to the console in the CIC and the galaxy stands over the projector. Point at a cluster,
  system or planet and pull the trigger to open it. The game's own map then shows over the table; answer its travel
  question with the pointer. Walk away from the table to close the map.
- **Conversations and cutscenes** play on a 3D screen in front of you. VR menu: Comfort > Conversations and cutscenes
  (In VR, 3D screen, Flat screen). The dialogue wheel works as on a gamepad.

## In the Mako

Until you take hold of a control, the Mako drives as in the flat game: the left stick drives, the right stick turns the
camera, the left trigger boosts, A fires the jump jets, B gets out. The right controller aims the turret wherever you
point it: the right trigger fires the machine gun, the right grip the cannon.

**The cockpit's controls** (`[Mako] controls = 1`, on by default): you sit in the Mako's cockpit, with the throttle
lever at your left armrest and the drive stick at your right. Bring your left hand to the lever, or your right hand to
the stick, and squeeze its grip to take it. The chase view is on the centre screen, the turret's view on the right one.

| Control | What it does |
|---------|--------------|
| Lever (left grip held) | Push forward to drive, pull back to reverse. It has a notch at neutral and returns there when you let go. X: jump jets (they also right a flipped Mako). Y: boost |
| Stick (right grip held) | Tilt left or right to steer. Twist it to steer harder; with the lever at neutral, a twist turns the Mako on the spot. While the jets or the boost fire, tilt it forward or back to pitch the Mako. It springs back when you let go. Trigger: machine gun. A: cannon. Thumbstick: aims the turret |
| Right hand off the stick | Point to aim the turret. Trigger: machine gun. Grip: cannon |
| B | Get out |

Once you've taken a control, the left stick doesn't drive any more, X, Y and the left trigger work only with the lever
held, and A only on the stick. The game's own controls come back when you get out, open the VR menu, or a controller loses tracking.
`[Cockpit] enabled = 0` keeps the game's chase camera (the lever and stick then show as two flat markers at your knees);
`[Mako] controls = 0` keeps the game's own controls.
