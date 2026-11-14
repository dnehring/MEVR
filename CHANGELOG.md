<!-- SPDX-License-Identifier: MIT -->
# Changelog

What changes for players from one release to the next. Newest first; each release is tagged `v<version>` in git, and
`tools/package-release.ps1` copies its entry into the release notes (`.github/release-notes-template.md`). The version
itself is written in one place only: `project(MEVR VERSION ...)` in `CMakeLists.txt`.

## Unreleased

## 0.3.0 (2026-11-13)

- **Shadows match in both eyes.** With the faster one-render stereo (the default), the right eye's shadows were
  cast as the left eye saw them, so they slid when you moved your head. Each eye now gets its own. Headset
  verification pending.
- **Quitting the game no longer crashes.** Every quit, from the menu or by closing the window, ended in a crash in the
  game's own shutdown (no progress was lost), and the next start offered "The game crashed last session". The game now
  closes cleanly, and Windows no longer records a crash for it.
- **First-person conversations (experimental): your body stays with you.** After walking up to the Council with Anderson,
  Shepard's body could stay several metres from you for the whole hearing and the conversations after it. Your body now
  stands where you do in every first-person conversation, and a stick you are still pushing through a fade no longer walks
  you away from your place until you let go of it. Headset verification pending.
- **First-person conversations (experimental): the Council is there when you walk up with Anderson.** Udina and the
  councillors used to appear only when the screen went black at the top of the stairs; they now stand in the chamber as you
  climb, and the hearing starts as before. Saren's transmission still begins with the hearing. Headset verification pending.
- **Walking in VR follows the stick.** On foot you start moving the moment you push (the game held Shepard for about 0.15 s
  while a start animation played) and stop the moment you let go, with no coasting; the game's third-person speed ramp and its
  start, stop and skid steps are replaced in VR. Your speed is in proportion to how far you push: half a push walks, a full
  push runs (the game made anything under 70% a slow walk and anything past it a full run). In first-person conversations and
  scripted walks you walk faster (1.4 m/s), so you keep up with the people you walk with.
- **Thespian mode (experimental, off by default): speak Shepard's lines.** With it on (the menu's Voice page), a
  conversation's replies stand as a script in front of you: read one aloud, or just say its short label, and it is
  taken; the words you have said light up as you go. Shepard's recorded voice is skipped, so the other person answers
  you. No button: the microphone listens only while the replies are up, on your PC, with the same recognizer as the
  squad orders. You need not be word-perfect or finish the line (Relaxed, Performer and Method set how much of it
  counts), and the stick still picks as before. The game's names (Saren, Eden Prime, Benezia...) are listened for
  wherever they stand in a line, and a reply is never taken when the word that tells it from another was only half
  heard: the panel says "Not sure which" and you read it again or say its label. Replies that lead to the same answer
  under different labels show as one row. English game text only. Headset verification pending.
- **No more camera shake in VR.** Firing no longer shakes the world around you, nor do the Mako's jump jets and
  boost; running and sprinting no longer sway your view with Shepard's stride. Both are on the menu's Comfort page
  (Camera shake: Off, Low, Full; Running sway) if you want the game's effects back. Headset verification pending.
- **First-person conversations (experimental): scripted walks wait for you.** When Anderson (or your squad) walks beside
  Shepard and you fall about 3 m behind, the walk and its lines wait until you catch up, instead of the scene going on
  without you. You can also walk the whole way now: the conversation's 4 m boundary no longer stops you a few steps into
  such a walk. And walking during a conversation slides past a squadmate standing close instead of sticking.
- **The 3D cinema screen no longer crosses your eyes.** The dialogue wheel, subtitles and the title's menus used to sit
  deep behind the screen while drawn over faces and shoulders in front of it; they now stand just in front of the
  screen. Over-the-shoulder shots come out of the screen less, and the first frames of a scene and each camera cut no
  longer flash with too much depth.
- **Puzzle instructions back in view.** The Tower of Hanoi puzzle's instructions (and those of the Quasar machines and
  the toxin cure) were pushed off the HUD, out of sight. They now show where the game puts them. Headset verification
  pending.

- **Live controller adjustment.** Under Body & calibration > Advanced calibration, select a hand and choose
  Adjust live. Use the opposite controller's stick to fine-tune its position and angles while viewing the hand
  or held weapon. Trigger gives fine control; Done applies the offsets and Cancel restores the starting values.
  Rounded, translucent orange and cyan controller guides show raw grip positions and forward directions beside
  the corrected hands. Use baseline clears the selected hand's manual offsets to return to guided/runtime alignment.
  Physical-headset verification pending.

- **Clearer guided aiming calibration.** A sightless training pistol provides a visible one-hand reference.
  Rifle calibration keeps the free-moving hand orbs, with instructions to choose your own hand spacing and wrist
  angles so weapons follow your natural pose. The aiming dot appears in the final check. Headset verification pending.

- **Manual calibration.** Body & calibration now offers height offset and separate left/right controller
  position, pitch, yaw and roll adjustments. They save alongside the automatic calibration for each headset/controller
  setup; Reset offsets returns them to zero without erasing the measured calibration. These controls are hidden
  under Advanced calibration until you choose Show. Headset verification pending.

- **3D audio for headphones.** Every sound the game places in the world (gunfire, footsteps, explosions, voices near
  you) now reaches your ears as it would from where it is, including from behind, instead of only being panned left and
  right. It comes from OpenAL Soft, which MEVR Setup installs in the mod's own folder; no game file is changed.
  The menu's new **Sound** page chooses Headphones 3D, Speakers or the game's own sound, and its loudness (both from the
  next start).
- **People talking near you sound from where they stand**, on foot too: a conversation you overhear, people arguing in
  front of you, a squadmate's remark, no longer play inside your head. It can be turned off on the Sound page.
- **Conversations in first person (experimental).** Comfort > Conversations has a fourth choice: you stand where Shepard
  stands, people stay where they are, and you can walk during the conversation. A scene opens without a cut when it is
  where you already stand or a short walk away; leave the stick alone and you are walked there after a few seconds.
  Scenes made for the camera, and scenes Shepard isn't in (a voice far away, out of sight), go to the 3D screen. People no longer swivel on the spot toward whoever speaks:
  whoever speaks to you turns head and shoulders to you, bystanders only glance at the speaker when he is in front of
  them, and a body re-faces only for you, when you stand beside or behind someone who is talking with you. A scene no longer begins
  with a flash of the cinema screen, and a scripted walk (the walk to the Council) plays out whether you walk along or
  stand and watch. Your own body, the floor under you and people standing close no longer vanish from shot to shot.
  Voices come from where people stand when they are near you (within 8 m) and stay with them as you turn your head;
  someone far across a hall, like the Council, still sounds from the middle.
  When a scene moves someone to another spot during a conversation, they now walk there with their own walk (turning,
  stepping, stopping) instead of jumping. A short scripted scene on foot that walks Shepard with its own camera (Chellick
  bumping into you on the Citadel) is seen from your own eyes too, instead of from behind Shepard with black bars, and so is
  a scripted walk that follows a conversation at once (Benezia's entrance on Noveria), instead of the cinema screen.
  Scenes that play out around you as you walk keep their subtitles (when the game's subtitles are on).
  When people are brought round into view at a scene's start, those who talk to each other stand where they can see each
  other. A scripted scene that fades to black before it moves you no longer flashes the cinema screen first, and a cut to
  another place inside one leaves you facing the scene. Riding Eden Prime's tram, you ride along.
  Experimental: off unless you choose it.
- **The Council's hearing stays first person when you walk along with Anderson.** Walking during his walk up the stairs
  could leave the view behind and above Shepard for the whole hearing. You now stay at your own eyes and walk on to the podium.
- **Conversations happen where you start them** (first person, experimental): nothing walks you anywhere, and the person you
  talk to stays where they are. Others who take part walk over to join you, setting off a few lines before they speak; people
  who don't take part, or can't walk (a hologram, someone seated), stay put. You can walk around a little during a conversation
  (more slowly, within a few metres, not while choosing a reply) and turn with the right stick. Staged scenes (the Council,
  Kirrahe's camp on Virmire, now in first person) keep their own places, with a short fade to put you where the scene needs you.
  Nods and gestures play as the game has them.
- **Your squad gathers beside you in conversations** (first person): instead of being moved to the scene's marks next to where
  Shepard's script wanted him, your squadmates stay with you and stroll into a loose formation at your sides, each on their own
  side, facing whoever you talk to. When a scene needs a short fade, they are simply there when it clears. They follow you again
  when the conversation ends, without a word.
- **People look at you** in first-person conversations where the scene keeps Shepard's body on its own mark away from you
  (Girard on the Presidium, Khalisah's interview): their heads used to turn to that empty spot instead of to you.
- **Scenes that rearrange everyone behind a fade** (Kirrahe's speech on Virmire): the others are in their new places when the
  fade clears instead of walking there in front of you, and you are walked the step or two to where the scene wants you, as if
  with your own stick (push it to take over); you are never moved by force.
- **People in first-person conversations look you in the eye in scripted scenes too.** In scenes that open at Shepard's own
  spot (the Council, Kirrahe's camp on Virmire) your eyes were placed a third of a metre behind Shepard's, so everyone talking
  to you looked just past you, at where his hidden head was. Turning with the right stick no longer swings you round a point in
  front of you, and people who join the conversation stand round you, not round a spot half a metre ahead.
- **Long dialogue choices are shown whole** in first-person conversations: the end of a long reply was cut off the wheel and
  floated elsewhere in view.
- **The view stays at your eyes beside walls.** Standing close to a wall, a pillar or someone in front of you could drop the
  view to Shepard's knees, behind his legs (the game's own camera pulling itself in). It now stays where your eyes are.
- **A conversation that ends in a shop no longer flickers** (first person, experimental): talking to a merchant
  such as Opold on Noveria could flash the flat screen for a moment before the shop stood in the room.
- **Hackett's calls at the galaxy map**: the map that opens after a call no longer flashes the flat screen, and the call
  starts where you stand at the console instead of cutting to Shepard 1 m away.
- **First person stays first person after many loads.** Loading saves 16 times without opening the pause menu (say,
  quickloading after deaths) left the view behind Shepard, as the game's own third-person camera, until the pause menu was
  opened. It now stays at your eyes however often you load.

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
