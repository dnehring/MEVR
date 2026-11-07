<!-- SPDX-License-Identifier: MIT -->
# Changelog

What changes for players from one release to the next. Newest first; each release is tagged `v<version>` in git, and
`tools/package-release.ps1` copies its entry into the release notes (`.github/release-notes-template.md`). The version
itself is written in one place only: `project(MEVR VERSION ...)` in `CMakeLists.txt`.

## Unreleased

### Performance
- A Performance setting on the menu's Display page: **Quality**, **Balanced** (the standard) and **Performance**. Each sets the
  render resolution, FSR and the settings that decide the frame rate together; the line under it says what it trades. Change
  one of those settings yourself and it shows **Custom**. Everything applies at once except the render resolution, which
  applies after a restart (the row says when one is waiting).
- The individual settings are in Advanced > Performance: small-object culling, grass distance, dynamic shadows, ambient
  occlusion and FSR sharpness, with the original fixed render sizes (2048 to 6144 wide).

### Seeing the game
- Movement is always life-size: your steps, leans and crouches move you one to one. The Comfort toggle for the old
  half-scale movement (and Advanced > View's Movement scale) is gone; profiles that had it off move life-size now.
- The cinema screen for conversations and cutscenes is calmer: a 16:9 screen a little farther away (2.9 m wide at 3.5 m)
  with gentler depth, so it no longer pulls your eyes apart. The size, distance and depth are in Comfort > Conversations
  and cutscenes.
- Conversations and cutscenes on the screen run at your headset's full frame rate instead of half of it.
- No more burst of depth at the start of a conversation.
- Motion blur stays off after cutscenes and conversations (it could come back after one and make surfaces flicker).
- The cinema screen stays where a scene began when you lean or look around, and stands over a dim floor with a horizon
  behind it, so you keep a sense of place (Comfort > Conversations and cutscenes, or `[Cinematics] backdrop`).
- Entering and leaving a cutscene or conversation dips from black instead of cutting hard (`[Cinematics] fadeS`).

### The Mako
- Repair the Mako from the throttle: while your left hand holds the throttle lever, click the left thumbstick. The button on
  the desk still works too.
- The cockpit's joystick turns in your hand when you twist it (the twist always steered; now the stick shows it).
- The extraction handle overhead is pulled down, as its arrows say (it took a push up before).

## 0.1.0 (2026-11-05)

The first public release, a **development preview**: **Mass Effect 1 (Legendary Edition) in VR**, played with motion
controllers. Built on Halcyon's MassEffect1-VR renderer, with our hands, body and menus on top. LE1 only; Mass Effect 2
and 3 come later. Much of it has been tried in a headset only a few times so far: reports are very welcome.

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
- Colour presets per headset (brightness, contrast, saturation), and a render resolution sized to your headset: 85% of
  its eye size, sharpened back up by FSR 1, for frame rate. When MEVR Setup can't tell your headset's size yet, the first
  VR start sets it for the launches after it.
- Each frame renders the scene once and builds the second eye from it, about a fifth less work per frame than two full
  renders.

### Your squad
- Point and order: squeeze the left grip away from the gun, point at an enemy, a spot or a squadmate, let go: attack, move
  there, or regroup. Pointing at a squadmate as you squeeze orders them alone. Your squad answers with the game's own
  lines.
- Send them somewhere: hold B and an arc leaves your right hand like a thrown ball; let go and your squad goes where
  it lands and holds that spot. With two squadmates, a small wheel at your right hand picks who goes (move your hand
  toward one, or up for both). Double-tap B to call everyone back.
- Say it: hold the left grip at your left ear, speak ("Garrus, take out those troopers on the bridge", "Wrex, hold the
  line", "Liara, lift the big one", "everyone on me", "get behind that crate"), let go. Recognized on your own machine, nothing leaves it; no
  microphone, no voice, the rest of the mod is unchanged. Needs the voice files: MEVR Setup's Optional content page
  downloads them (about 70 MB from Moonshine's releases, each file checked; Remove and Uninstall take them out again). A
  line the rules can't place ("Ash, the thresher maw is yours") goes to a small language model trained for the squad (also
  on your machine, under a second): the same page's model tier adds it (llama.cpp and the model, about 665 MB, once the
  model's release is published).

### Your hands
- You aim with the controller, not your head. Your arms, hands and fingers follow the controllers; the gun is in
  your hand.
- Only reaching a holster draws your weapon: the trigger, melee, grenade and holster buttons no longer pull it out
  when it's away (medi-gel and powers still do, they need it).
- On Quest (Touch) and Index controllers your fingers follow the touch sensors too: lift your finger off the trigger
  to point, lift your thumb off the stick for a thumbs-up, and keep your finger off the trigger along the gun's frame.
- Hold a rifle with both hands: the left hand grabs the foregrip.
- Red dots and holo sights on the guns, and a real zoomed scope on the sniper rifle when you bring it to your eye.
- The gun kicks in your hand when you fire, muzzle up and back, harder for a shotgun or a sniper rifle and less with
  both hands on it; your view never kicks, and your aim and the shots stay where you point (Controls > Weapon drawn).
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
- Your powers on a wheel at your left hand: hold X and it opens around your palm with your biotic, tech and combat
  powers; move your hand toward one (or push the left stick toward it) and let go of X to equip it. Tap X instead and
  it stays open until you pinch the left trigger.
  X no longer throws a grenade the game's way on foot (throw real ones from your chest).
- Cast with your hand: with the wheel closed, raise your left arm and point it at an enemy; orange brackets lock onto the
  one nearest your hand's line (they stay on it while you sway, and through a moment behind cover). Point roughly and the
  lock finds the enemy your hand and your eyes agree on. Push your open left
  hand toward it to cast the equipped power at it. Singularity and the other placed powers land where your hand points
  when no enemy is locked.
- The game's own power wheel: hold the right stick click and steer with the same stick, let go to close it. A quick
  click no longer casts a power by accident; a double-click still recenters the view.
- Melee is a real strike only: B no longer melees on foot. With your weapon away, punch with your right fist (squeeze
  the grip and the trigger). Strikes come no faster than the game's own melee allows, so flailing doesn't spam them.

### Your body
- A guided calibration (about a minute) measures your height, arms and how you aim, and is saved per controller type.
- Crouch for real to crouch in the game; no automatic cover snapping.
- Cover protects you as it does in the game: with your head down behind cover, hits from enemies in front of it are cut
  as the game cuts them for Shepard in cover (all of the damage on Casual and Normal, 90% above). Raise your head over it
  to shoot and you're in the open again.
- Shepard's body stays under you as you turn and lean.
- Shepard's legs run where he runs: when you strafe, only his upper body turns toward you, so his feet no longer
  skate sideways (backing up, his legs still turn part of the way).

### Comfort
- Smooth turning (the game's own), or snap turns of 30, 45 or 60 degrees.
- An optional vignette while turning or moving; walk toward where your head or your left hand points.
- Recenter with R or a double-click of the right stick; a badly tilted start is fixed by recentering.

### The Mako
- When the Mako's hull is badly damaged, the cockpit's lights turn red and flash like a warning beacon until it's repaired.
- The Mako's cannon sits over the cockpit's roof: you see its barrels at the top of the windshield and the shots come from them.
- A squadmate runs the Mako's guns while you drive: they aim at the nearest enemy they can see, fire the machine gun, and
  switch to the cannon for heavy targets and groups. Push the stick's thumbstick or aim with your free right hand to take the
  guns yourself; they take them back a few seconds after you stop.
- Drive it with a throttle lever and a flight stick in the cockpit: grab them, tilt the stick to steer, fire with the
  trigger and A, and tilt the stick while the jump jets fire to drift the Mako forward, back or sideways in the air.
- The turret follows your right hand.

### The menu and the setup
- Our own VR menu: hold both sticks (or press `\`) to open it. Comfort, Controls, Body & calibration, Display,
  HUD & sights, and Advanced settings, with the game's pause menu and holster one click away. It pauses the game
  while it's open.
- **MEVR Setup** installs everything in one click, without ME3Tweaks Mod Manager, sets the game up for VR, and
  uninstalls exactly (the game's files and your settings are put back, including the copy the game keeps in its engine
  settings). Its Launch button starts SteamVR, then Mass Effect 1 straight past the game's launcher. Its Optional content
  page installs texture packs (ALOT and others) that you download from their own pages.
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
