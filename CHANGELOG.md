<!-- SPDX-License-Identifier: MIT -->
# Changelog

What changes for players from one release to the next. Newest first; each release is tagged `v<version>` in git, and
`tools/package-release.ps1` copies its entry into the release notes (`.github/release-notes-template.md`). The version
itself is written in one place only: `project(MEVR VERSION ...)` in `CMakeLists.txt`.

## Unreleased

- **Conversations in first person (preview).** Comfort > Conversations has a fourth choice: you stand where Shepard
  stands, people stay where they are, and you can walk during the conversation. A scene opens without a cut when it is
  where you already stand or a short walk away; leave the stick alone and you are walked there after a few seconds.
  Scenes made for the camera still go to the 3D screen. People no longer swivel on the spot toward whoever speaks:
  heads and shoulders turn, as the game animates them, and a body re-faces only when it must. A preview: off unless you
  choose it.

## 0.2.0 (2026-11-09)

- **One VR mode: life-size stereo in first person.** Advanced no longer offers the original MELE-VR renderer's choices
  (Mono and Depth modes, hand-set eye distance, convergence and eye swap, third person, lean, the field of view fill,
  fixed render sizes, the classic menu). An old `MELEVR.ini` that still sets them is ignored at launch, and the game
  log lists what was put back. *Reset VR settings* now returns to these standard settings, not the original mod's.

- **Reachable seated holsters.** Seated play raises and moves the hip grab zones forward over the thighs, with larger
  zones and independently adjustable shoulder/chest reach points. Standing placement is unchanged. The native weapon
  wheel can draw while seated as a button alternative; `[Holsters] seatedWeaponWheel = 0` disables that fallback.

- **Shepard's size is right from the first frame.** The mod remembers Shepard's standing height for each body model, so
  after a load your calibrated size applies at once instead of after standing holstered for a second and a slow resize.

- **Update directly from Setup.** Welcome > Updates can download, verify and install a newer release without opening
  a browser. It waits for the game to close and preserves game settings, VR profiles and calibration. Older installers
  need one manual upgrade to gain this feature.

- **Powers: the target on your gun only when it would work.** Overkill's target shows on the assault rifle only (Marksman
  the pistol, Carnage the shotgun, Assassination the sniper rifle), and no gesture target shows while the power cools down
  or while your left hand holds the gun.
  The power wheel's last tile, **No power**, puts your powers away until you pick one again.

- **Omni-tool:** the rail of the game's screens now stands to the right of a wrist screen, where your right hand reaches
  it, and slides out of the screen's edge after the screen unfolds. While a wrist screen is up, the rail, the screens and
  the hacking puzzle's d-pad answer Shepard's own fingertip. The gauntlet comes on like a light, dim to full.

- **Weapons after a crouch:** with a gun drawn, the weapons no longer stay low after you crouch and stand up again.

- **Galaxy map and landing:** the map stays on the CIC table through travel, movies and radio calls, its planet buttons
  and travel question answer the pointer, and a new planet's party screen opens in the room you land in.

- **Smoother start and play:** the game no longer resets its graphics four times while starting, each a stall; motion
  blur stays off; the right eye keeps the game's anti-aliasing; a first Automatic install starts at a lower resolution.

- **Squad voice orders:** a weapon named with an order is equipped first ("sniper, take the ones on the bridge"), and the
  recognizer's past tenses and fillers ("switched to", "took them out", "yeah") no longer stop an order.

## 0.1.1 (2026-11-07)

- **Seated play.** Choose *I play seated* at the calibration's height or crouch step, or later in *Body & calibration >
  Play position*. Lowering your head never crouches Shepard (the left stick click still does), Shepard keeps his standing
  height with your head movement on top, and the holsters and grenade slot are measured from your head. Saved for each
  controller type.
- **Your height without the crouch-and-recenter trick.** The floor lock is on again: the view sits at your real eye height
  above Shepard's floor, and the omni-tool's screens (Computer and the rest) no longer lift the arms above where they are
  on foot.
- **Arms no longer vanish** when you hold them out and turn with the stick: Shepard's body is no longer culled by a box
  that only covered his torso.
- **Galaxy map:** the system and cluster names sit on their stars and stay there when you move your head (they were
  projected through the headset's wider field of view), they no longer flicker, and leaving the map no longer stutters.
- **Omni-tool:** the rail closes by itself when you load a save, a cutscene or conversation starts, or you get in the Mako.
- **Smoother on Virtual Desktop:** less work on the render thread on a timer (a player saw a judder every 10 seconds),
  and the automatic frame-rate recovery stays off on Virtual Desktop's own runtime.
- **Updating keeps your settings:** installing a new version over an old one no longer resets your render resolution
  and the other game settings.
- **Launch:** if Windows refuses to start Steam (for example Steam set to run as administrator), Setup tries the other ways
  and says what failed; each attempt goes into problem reports. You can always start the game from Steam's library.
- Fixes from a code review: a power push started at your chest now casts once your arm is out; sprinting through a level
  change could touch a destroyed Shepard; the voice talk key survives a recognizer restart and the key picker works with
  the menu open; the controller guide follows your power-wheel settings.

## 0.1.0 (2026-11-07)

- Fixed a Quest over Link dropping to half rate after a level load and staying there: after a load the game rendered both
  eyes in full for about 11 s before going back to one render a frame, which the headset's compositor answered with half
  rate. It now goes back within about half a second. A detector logs the stuck state and asks the runtime to recover.
  The optional full OpenXR frame order (`[Performance] frameLoop = full`) is there for testing.
- Display: SteamVR players can turn motion smoothing off for the Steam version of this game, or return it to SteamVR's global setting, through MEVR Setup. No change until requested.
- Galaxy map: opening it at the CIC's console no longer closes it at once; the game's own projector hologram shows on
  approach, and the open map lies where that hologram is, at 0.7 of its size (`[Galaxy] windowScale`).
- Omni-tool rail openings reserve world UI before the game's opening call, preventing the intervening flat-menu decision.
  Rail Equipment and its related panels use the wrist even when a nearby useable was recently selected; use-opened screens keep their anchors.
- Optional conversation walking prototype: in First person conversations, `worldWalk = 1` requests an AI walk (slide fallback),
  or `2` slides NPCs toward lasting stage marks after a wait. Off by default (`0`); not yet checked in the game.

- Hide the game's centre reticle throughout VR gameplay with the hand reticle on, including holstered weapons and Mako controls; park the cockpit's orange aim marker on exit and refresh its eye-hidden ownership on possession changes. Native sniper scope and non-gameplay screens keep their presentation; `mevr reticles` diagnoses both reticle paths.
- First-person eye height follows your real floor height, through recentering, drawing weapons and crouching. Physical crouch
  lowers the view once; button crouch keeps its game effect while your real head controls the camera. Automatic body sizing
  uses a fixed standing reference. Off by default in this release (with it on, the arms sit too high in the omni-tool's
  screens); file setting `[Body] floorLock = 1` turns it on.

- Fixed X throwing a grenade while selecting powers: on foot with the hand power wheel on, taps and holds of X only
  use that wheel. Grenades: grab one at the left of your chest with either hand (the left while the right holds the gun) and throw; release slowly to put it back.
  An old `grenadeButton = 1` and the old right-hand-only grenades move once; X keeps its game meaning in the Mako, menus and conversations.
- Powers on yourself now show where to touch and a ring while you hold: gun powers such as Overkill use the gun's top.
  A flash, the power's name and a pulse in both hands confirm a cast; a red message explains a cooldown, unavailable power
  or holstered weapon.

The first public release, a **development preview**: **Mass Effect 1 (Legendary Edition) in VR**, played with motion
controllers. Built on Halcyon's MassEffect1-VR renderer, with our hands, body and menus on top. LE1 only; Mass Effect 2
and 3 come later. Much of it has been tried in a headset only a few times so far: reports are very welcome.

### Seeing the game
- Stereo 3D and head tracking (6DOF) in first person, at life size: each eye is rendered from where it really is, and
  your steps, leans and crouches move you one to one.
- Conversations and cutscenes on a large 3D cinema screen (16:9, 2.9 m wide at 3.5 m) at your headset's full frame rate.
  It stays where the scene began when you lean or look around, stands over a dim floor with a horizon behind it, and the
  switch to it and back dips from black. Or play them in VR, or on a flat screen: Comfort > Conversations and cutscenes.
- Motion blur stays off (in VR it smears each eye its own way), after level loads and cutscenes too.
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

### Performance
- A Performance setting on the menu's Display page: **Quality**, **Balanced** (the standard) and **Performance**. Each sets the
  render resolution, FSR and the settings that decide the frame rate together; the line under it says what it trades. Change
  one of those settings yourself and it shows **Custom**. Everything applies at once except the render resolution, which
  applies after a restart (the row says when one is waiting).
- The individual settings are in Advanced > Performance: small-object culling, grass distance, dynamic shadows, ambient
  occlusion and FSR sharpness, with the original fixed render sizes (2048 to 6144 wide).

### Your squad
- Point and order: squeeze the left grip away from the gun, point at an enemy, a spot or a squadmate, let go: attack, move
  there, or regroup. Pointing at a squadmate as you squeeze orders them alone. Your squad answers with the game's own
  lines.
- Send them somewhere: hold B and an arc leaves your right hand like a thrown ball; let go and your squad goes where
  it lands and holds that spot. With two squadmates, a small wheel at your right hand picks who goes (move your hand
  toward one, or up for both). Double-tap B to call everyone back.
- Say it: hold the left grip at your left ear, speak ("Garrus, take out those troopers on the bridge", "Wrex, hold the
  line", "Liara, lift the big one", "everyone on me", "get behind that crate"), let go. Call them by first name, surname,
  short form or rank ("Vakarian", "Ash", "Chief", "Alenko"), or "everyone". The recognizer's spelling is understood as
  said ("Wrex rushed them", "Garrus switched to your sniper rifle", a "yeah" or "mm" first). Recognized on your own machine, nothing leaves it; no
  microphone, no voice, the rest of the mod is unchanged. Needs the speech recognizer: MEVR Setup's Optional content
  page downloads it (about 70 MB from Moonshine Voice's GitHub release and Hugging Face, each file checked against its
  SHA-256; Remove and Uninstall take it out again). A line the rules can't place ("Ash, the thresher maw is yours") goes to
  a small language model trained for the squad (also on your machine, under a second): the same page's model tier adds it
  (llama.cpp and the model, about 665 MB). Until the model's own release is published, the setup installs the recognizer
  alone and the orders run on the built-in rules.
- The menu's Voice page: pick your microphone by name, watch its input level move as you speak, see whether the
  recognizer is ready, choose its size, the radio hum's strength and a keyboard key to hold for talking.

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
  it stays open until you pinch the left trigger. Its light comes from the omni-tool on your hand.
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
- Sprinting is a faster run: the camera and your gun stay as they are, you can still shoot, and it lasts about 6 seconds
  before it needs a rest (`[Hands] sprint = game` gives the game's own sprint back).

### The Mako
- When the Mako's hull is badly damaged, the cockpit's lights turn red and flash like a warning beacon until it's repaired.
- The Mako's cannon sits over the cockpit's roof: you see its barrels at the top of the windshield and the shots come from them.
- A squadmate runs the Mako's guns while you drive: they aim at the nearest enemy they can see, fire the machine gun, and
  switch to the cannon for heavy targets and groups. Push the stick's thumbstick or aim with your free right hand to take the
  guns yourself; they take them back a few seconds after you stop.
- Drive it with a throttle lever and a flight stick in the cockpit: grab them, tilt the stick to steer (twist it to turn on
  the spot; the stick turns in your hand), fire with the trigger and A, and tilt the stick while the jump jets fire to drift
  the Mako forward, back or sideways in the air.
- Repair it with the button on the desk, or by clicking the left thumbstick while your left hand holds the throttle. Pull
  the handle overhead down to call for extraction.
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
