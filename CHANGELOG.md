<!-- SPDX-License-Identifier: MIT -->
# Changelog

What changes for players from one release to the next. Newest first; each release is tagged `v<version>` in git, and
`tools/package-release.ps1` copies its entry into the release notes (`.github/release-notes-template.md`). The version
itself is written in one place only: `project(MEVR VERSION ...)` in `CMakeLists.txt`.

## 0.1.0 (unreleased)

The first public release: **Mass Effect 1 (Legendary Edition) in VR**, played with motion controllers. Built on
Halcyon's MassEffect1-VR renderer, with our hands, body and menus on top. LE1 only; Mass Effect 2 and 3 come later.

### Seeing the game
- Stereo 3D and head tracking (6DOF) in first person, at life size: each eye is rendered from where it really is.
- Conversations and cutscenes on a large 3D cinema screen in front of you (or in VR, or on a flat screen: your choice
  in the menu).
- The HUD on a panel that follows your body; your health and shields on your left wrist, your weapon's heat and
  grenades on the gun.
- The game's own screens (shop, loot, journal, codex, map) as panels in the room, used with a laser pointer.
- Your omni-tool: tap your left wrist with your right index finger and a menu of the game's screens (squad, equipment, map,
  journal, codex, the mission computer) appears on your wrist; touch one and it's projected from your wrist, the menu
  beside it to switch. The hacking screen comes from the omni-tool too.
- The galaxy map as a hologram on the Normandy.
- Colour presets per headset (brightness, contrast, saturation), optional FSR 1 upscaling, and a render resolution
  sized to your headset: when MEVR Setup can't tell your headset's size yet, the first VR start sets it for the
  launches after it.

### Your hands
- You aim with the controller, not your head. Your arms, hands and fingers follow the controllers; the gun is in
  your hand.
- Only reaching a holster draws your weapon: the trigger, melee, grenade and holster buttons no longer pull it out
  when it's away (medi-gel and powers still do, they need it).
- On Quest (Touch) and Index controllers your fingers follow the touch sensors too: lift your finger off the trigger
  to point, lift your thumb off the stick for a thumbs-up, and keep your finger off the trigger along the gun's frame.
- Hold a rifle with both hands: the left hand grabs the foregrip.
- Red dots and holo sights on the guns, and a real zoomed scope on the sniper rifle when you bring it to your eye.
- Body holsters: the pistol at your right hip, the shotgun at your left, the rifles over your shoulders. Reach there
  and squeeze the grip to draw; let go there to put the gun away.
- Throw real grenades (take one from your chest), strike in melee with a fast swing, and vent an overheated gun by
  racking it with your left hand.
- When you rack a hot gun, a part of the gun itself (the pistol's slide, the assault rifle's receiver cover) moves
  back with your hand and snaps forward when you let go. Guns without such a part (the sniper rifles so far) stay
  still.
- Give your squad weapons from the game's weapon wheel at your left hand: pull the left trigger, point at a squadmate,
  let go.
- Use doors, lockers and people by looking at them and pressing A; the prompt appears at the object.

### Your body
- A guided calibration (about a minute) measures your height, arms and how you aim, and is saved per controller type.
- Crouch for real to crouch in the game; no automatic cover snapping.
- Shepard's body stays under you as you turn and lean.
- Shepard's legs run where he runs: when you strafe, only his upper body turns toward you, so his feet no longer
  skate sideways (backing up, his legs still turn part of the way).

### Comfort
- Smooth turning (the game's own), or snap turns of 30, 45 or 60 degrees.
- An optional vignette while turning or moving; walk toward where your head or your left hand points.
- Recenter with R or a double-click of the right stick; a badly tilted start is fixed by recentering.

### The Mako
- Drive it with a throttle lever and a flight stick in the cockpit: grab them, tilt the stick to steer, fire with the
  trigger and A, and pitch the Mako on its thrusters in the air.
- The turret follows your right hand.

### The menu and the setup
- Our own VR menu: hold both sticks (or press `\`) to open it. Comfort, Controls, Body & calibration, Display,
  HUD & sights, and Advanced settings, with the game's pause menu and holster one click away. It pauses the game
  while it's open.
- **MEVR Setup** installs everything in one click, without ME3Tweaks Mod Manager, sets the game up for VR, and
  uninstalls exactly (the game's files and your settings are put back). Its Optional content page installs texture
  packs (ALOT and others) that you download from their own pages.
- **Report a problem** (in the setup, or the VR menu's Advanced > Diagnostics) saves a zip with the logs and a summary
  on your Desktop, with your user and computer names taken out. Nothing is uploaded.

### Known issues
- Needs `MassEffect1.exe` version 2.0.0.48602 (Steam or the EA app); on any other version the mod stays off.
- Tested on a Quest 3 (Steam Link) and a PS VR2, both through SteamVR. Other headsets and runtimes may work.
- The heaviest scenes don't hold 90 frames a second everywhere; a lower render resolution helps.
- Walking more than about 0.75 m around your room from where you recentered leaves Shepard behind: recenter (R or a
  double-click of the right stick) to stand over him again.
- Steam's "verify files" can remove the ASI loader: run MEVR Setup's Repair.
- The binaries aren't code-signed yet: Windows SmartScreen or your antivirus may warn about them.
