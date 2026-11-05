<!-- SPDX-License-Identifier: MIT -->
# What still needs testing in the headset

One list of everything that still needs **you in the headset** (tracked hands, your eyes, your comfort), compiled
2026-11-05 from every research note's open `[verify]` items. The step-by-step instructions for the older features are in
[next-vr-session.md](next-vr-session.md); the source after each item says where the details are.

**Keeping it current (every session):** when a check passes or fails, delete its line here and write the result in its
research note. When you build something that needs the headset, add a line under its area: what to do, what to look
for, the source, the date built. Items Claude can check alone on the fake headset go in the last section, not in the
player's list. Each area names the session that owns it (a first guess: each session corrects its own).

**Before a session:** close the game, deploy the host and the mod together from current main (`tools/deploy-asi.ps1
-Game LE1`); the log's `reflect: N of N checks agree` should show no disagreement. **Reporting:** say what you saw and
roughly when. A snapshot is End, or a quick tap of both stick clicks; a recording is Home. Claude reads the logs.

## 1. Newest (2026-11-03 to 11-05)

### Powers at the left hand (session: Biotic/tech attack targeting system)
- [ ] Hold X: the wheel opens at your left palm and stays put while your hand moves. Pick by moving your hand and by the
  left stick; letting go of X equips. On a real save, powers you don't have yet are dimmed. (research/55 section 6)
- [ ] Raise your left arm at an enemy: orange brackets lock on. Push your open left hand toward it: the power casts and
  lands on that enemy. Say whether the push (1.6 m/s over 10 cm) is too easy or too hard, and whether the brackets read
  at 5 m and at 35 m. (research/55 section 7)
- [ ] No casts you didn't mean: letting go of a grenade, a squad order or the gun with your left hand never casts.
  (research/55 section 7)
- [ ] How Shepard's arms look during the cast animation, with arm IK on. (research/55 section 4)
- [ ] Self-cast powers by gesture, the left hand open: the palm on your chest, held (Barrier, Shield Boost, Immunity,
  Adrenaline Burst); slapped onto the top of the gun in your right hand (Marksman, Overkill, Carnage, Assassination); raised
  above your head, held (Unity). Do they cast when you mean them and never by accident (a hand resting on the chest,
  reaching for the foregrip)? Tech powers (and Shield Boost, Unity, Immunity, Adrenaline Burst) show the omni-tool gauntlet on
  your left forearm while aimed or gestured: does the game also show one on the gun hand? (research/55 section 10)
- [ ] Cast Throw at an enemy (or Warp, Lift or Stasis where enemies in the level use them): a translucent blue biotic hand
  flies from your left hand and lands as the game's hit does. Is it the right size and brightness at your hand, and facing
  the right way? Where no enemy uses the power (Singularity, tech powers) the bolt is the power's own charge or omni-tool
  effect instead: does that read as the power? Cast with your gun out in the right hand too: the left hand alone must cast,
  and the right arm stays on the gun. (research/55 section 9, a prototype built 2026-11-05)
- [ ] Hold the right stick click 0.3 s: the game's own power wheel opens, steers with the right stick, closes when you let
  go. A quick click does nothing; a double-click still recenters. (research/19 "Hold only")

### Melee by gesture (session: Biotic/tech attack targeting system)
- [ ] With your weapon away, make a fist with your right hand (squeeze the grip and the trigger) and punch an enemy:
  Shepard's pistol whip plays and hurts it. An open hand does nothing. (research/20 "Gesture only")
- [ ] B no longer melees on foot (it still backs out of menus). Flailing gives at most one strike per second or so.
- [ ] Strike enemies to your side as well as ahead; aiming and reloading must never strike by themselves. Say whether
  the 2 m reach feels right. (research/20 section 4)

### Mako controls and cockpit (session: Asset Modeling)
- [ ] From the seat, grab the lever and the stick: your hands sit on the handles with no jerk at the grab. Say whether
  the controls should be closer (straight arms). (research/47 "The hands on the handles")
- [ ] Drive: no steering surge; the lever springs back to neutral; tilt steers, twist adds to it, twist at a standstill
  spins on the spot. (research/47 "Tilt and twist split", research/46 section 7)
- [ ] Buttons: lever held, X jumps (and rights the Mako), Y boosts; the left trigger off the lever no longer boosts.
  Stick held: trigger = machine gun, A = cannon, thumbstick = turret. (research/46 section 7.6)
- [ ] Jump jets: tilting the stick during a burst pitches and rolls the Mako as wanted (20 / 15 degrees); with the stick
  let go it stays level. (research/47 "Flying on the jets")
- [ ] Nothing keeps driving after our menu, the pause menu, calibration, a recenter or a controller lost for 0.5 s; the
  next grab starts clean. Haptics: the notch tick, the full-lock buzz, the engine rumble. (research/46 sections 7.8-9)
- [ ] Riding over rough ground: comfort with the full pitch and roll, terrain poking into the cabin, the cabin's scale,
  the side screens readable. (research/47 "Riding in the Mako", research/48 section 5)
- [ ] Hazard lights in a real fight at low hull: is the red flash comfortable? (research/47 "Hazard lights")
- [ ] Take the turret from the gunner with the thumbstick, and by aiming with your free right hand; it goes back after
  3 s. While you press the repair button or hold the extraction handle (or your right hand is at the handle) the gunner
  keeps it, even with a finger on the trigger. (research/47 "The gunner"; research/48 section 27, built 2026-11-05)
- [ ] Repair button on the right desk shelf: does it sit right (squared to the shelf, along its front edge), and can you
  reach it leaning forward and down (1.14 m from the shoulder)? The ring shows the hull: green, amber at half, red low.
  (research/48 section 27 "Where", built 2026-11-05)
- [ ] Repair button: press it with a fingertip from the front, hold 2 s while the squares fill blue, the repair runs.
  With no omni-gel the squares are solid red; right after a repair (cooling down) they flash red every 1.2 s; neither
  repairs. (research/48 section 27, red built 2026-11-05)
- [ ] Extraction handle overhead: grab it with a new squeeze (never fires the cannon or boosts), pull it toward you, hold
  at the end: the "Return to Normandy?" box shows at the windshield with the cockpit still around you, and takes a
  click, a touch, A and B. Say whether the handle swings the right way and its label reads from the seat. Shepard's hand
  closes on the bar and follows its swing; pressing the button, Shepard's index points. (research/48 section 27, hands built
  2026-11-05)
- [ ] The ceiling lamps' glow spills a haze over the windshield's top (kept for now as the cabin's light on the glass):
  keep it, or leave the lamps right above the windshield (the spine's drop panel) unlit? (research/48 section 26)
- [ ] Frame rate on the PS VR2 while driving with the cockpit up (more pixels than the fake headset). (research/47
  "The cockpit's cost")

### Game screens in the room (session: VR mod for Mass Effect Legendary Edition)
- [ ] At a vendor: click the shop's buttons with the pointer and by touch (the test rig never got a click through).
  (research/53 section 4)
- [ ] Loot bodies and containers, open an encrypted crate (the bypass on your left wrist), play the minigames in a
  mission. (research/53 sections 3, 5.1)
- [ ] The wrist projector: placement above the omni-tool's display, the fan of light, the unfold. Tap it with your right
  index: tune `[Pointer] touchTipM` until the press matches your fingertip; the hover and press ticks.
  (research/53 sections 6.1-6.2)
- [ ] Screens not yet seen in the room: Options, Save, Load, Specialization, Achievements, Additional content, Game
  over; tutorial pop-ups. (research/53 section 3)

### Galaxy map, conversations (session: VR mod for Mass Effect Legendary Edition)
- [ ] At the CIC projector: pick a cluster, a system, a planet, then Travel. Does the travel box take your click? (The
  test rig ignored it.) This is also the first headset check of clicks on any flat menu. (research/51 section 8)
- [ ] From the console: are the map's labels readable, and does the disc's near edge draw over the console? (research/51
  section 8.1)
- [ ] A conversation and a cutscene on the 3D screen: depth comfort, the dialogue wheel readable, the pointer picks a
  wheel option. (research/52 "Limits")

### The omni-tool menu on the wrist (session: VR performance optimization; on the release-blockers branch, not on main yet)
- [ ] Tap your left wrist with your right index: the omni-tool's rail opens beside it (Squad, Equipment, Map, Journal,
  Codex, Computer, Close), its entries readable; a tap on an entry opens that screen on the wrist. Reaching for a wrist
  screen's lower buttons must not close it. (research/57 sections 2-3 and 6, built 2026-11-04/05)
- [ ] On the wrist's equipment screen with your real fingertip: the weapon tabs, Shepard's and the squadmates' tabs, a list
  row, Equip, a swipe to scroll. Does each land on the first tap (the targets are 0.8-1.4 cm)? (research/57 section 6,
  built 2026-11-05)
- [ ] The squad member behind the equipment and squad screens: there, switching with the tabs, comfortable at the
  backdrop's opacity (`[WorldUi] figureOpacity`, 0 = the backdrop's 0.8)? (research/57 section 7, built 2026-11-05)
- [ ] The wrist screens' size: 0.70 m wide now (0.55 before). Set it live with our menu's Comfort > Game screens > Wrist screen
  size (0.40-1.00 m): which size reads best without turning your head, with the rail beside it? (research/57 section 2, built
  2026-11-05)

### Squad orders by gesture (session: Low-latency voice squad commands)
- [ ] Squeeze the left grip with the hand away from the gun's foregrip zone, the grenade slot and a hot receiver, and
  point with it: let go on an enemy group (attack), on the ground past 2 m (move there), near you, on a squadmate or on
  nothing (follow). Pointing at a squadmate as you squeeze gives the order to them alone; otherwise both get it. Judge
  the feel: the cones (8 degrees on an enemy, 12 on a squadmate), the haptic ticks, whether a squeeze meant as the
  support grip or a grenade ever becomes an order, and how long the AI takes to react (the game's own 1-1.5 s order
  delays). Built 2026-11-04. (research/54 "The flat orders", `[SquadOrders]`)
- [ ] In a fight, `mevr say "<name>, hold here"` typed by Claude while you watch: does a held squadmate stay put under
  the HOLD formation, does a hurt or crowded one visibly fall back to cover and come back when it's calm, and does being
  re-ordered every few seconds make them twitch. Claude can read the logs; only you can see whether it looks right.
  Built 2026-11-04/05. (research/54 "Standing orders", "A real hold", "Retreat to cover")
- [ ] For the voice step (not built yet: no microphone capture, no recognizer): record your own spoken orders over Link /
  Steam Link, and measure the mic's lag by tapping a controller on the headset. (research/54 plan steps 2-3)

## 2. Hands, body and the gun (session: VR mod for Mass Effect Legendary Edition)
- [ ] Arms fully out, then the gun at the cheek: the hands stay on the controllers, the elbows bend naturally, the arm
  length feels right, a gun at your face isn't pulled off your eye. (research/14 "Reach")
- [ ] Take the gun with your left hand anywhere along it: no zoom, no camera jump, no gun swing at the grab.
  (research/14 "Two-handed grip")
- [ ] Turn your real body 90 degrees, step, lean, walk sideways and backward: chest and hips face your way, Shepard steps
  under you; any moonwalking legs, neck stump looking steeply down, or view judder? (research/29)
- [ ] Holsters: draw from every slot (the right shoulder without hitting your head; the left-hip shotgun doesn't catch a
  resting hand). Hold the gun through a long fight: grip fatigue, slips, the 0.3 s grace. (research/19 sections 3, 5)
- [ ] With the weapon away, melee, a grenade, the weapon wheel, the holster key and the trigger don't draw it (a buzz,
  except the trigger): how do the refusals feel? (research/19 "The trigger's draw")
- [ ] Holster buzz: does its pitch rise as your hand goes deeper, on the PS VR2 and on the Quest? Bend at the waist: do
  the low slots drop too far? (research/19 sections 7-8)
- [ ] The weapon drawn: the view doesn't turn during a first draw's blend; walking drawn starts and stops like exploring.
  (research/38 B, C)
- [ ] Fingers: with the weapon away, grip and trigger close the fingers into the palm (not backward, not through it);
  with a gun, the right index rests along the frame. Make an open hand, a point and a thumbs-up: does the PS VR2 Sense
  report its touch sensors at all? (research/43)
- [ ] Vent: on a hot gun, squeeze the receiver with your left hand and rack back fast: heat gone, both hands pulse, the
  game's cool-down sound. The zone fits the pistol's slide and the rifle's receiver; a cool gun's receiver is still the
  two-handed grip; a hot gun still lets the left hand take a grenade. (research/45 sections 1, 4)
- [ ] Grenades: the delay after you let go, the disc leaving from your hand, a lob over cover landing where you threw,
  the throw animation with arm IK. (research/20 sections 3-4)
- [ ] Calibration on each controller type: the rifle step finishes, the far orb is in reach, the cyan ring sits where
  your hand feels it. PS VR2 and a Quest over Steam Link get different calibrations. (research/25 sections 4, 8-9)

### The game's start on one panel (session: Weapon-specific scope models)
- [ ] Launch the game with the headset on: from the first frame the logos, the title, the camera flight after a key, the
  main menu and the load's movie all show on one flat panel that stays where it appeared; no moment of the title's
  planet painting filling the view, no 3D cinema screen in between, no flash of the world after the key. The level then
  comes in as VR. (research/56, built 2026-11-05)

## 3. Aiming, sights and scopes (session: Weapon-specific scope models)
- [ ] Raise the sniper rifle: the picture only at the eye (never at a casual hold), no on/off flicker, the eye reaches
  the eyepiece; lowered, the gun eases back. (research/21 section 15)
- [ ] Under the scope, flick the right stick forward and back: the zoom steps 3.5x / 7x with a tick; the reticle reads.
  (research/21 section 16)
- [ ] The scope's real zoomed render: sharp, upright, not mirrored, the reticle on target; the world stays put when only
  your head moves and follows the gun without lag. A black speckle in dark areas is known and kept for now
  (`[Sights] scopeCapturePost = 0` removes it and the grade); say if it bothers you. (research/39 sections 10, 12)
- [ ] The assault rifle's red dot at the cheek: shows without hunting, lines up. Close each eye in turn: the dot and the
  scope show in the aiming eye only, on SteamVR and on the Oculus runtime. (research/21 sections 7, 11)
- [ ] Shot groups: the pistol at 10-20 m on the dot, the sniper at 5 m and 20-40 m on the crosshair, no drift to the
  right. (research/21 sections 9-10)

## 4. Moving and comfort (session: VR mod for Mass Effect Legendary Edition)
- [ ] Crouch for real in combat: does the view drop below your own eyes (the floor seems to jump up)? Walk into walls:
  no cover snap. (research/23, research/38 B)
- [ ] Snap turn at 30, 45, 60 degrees, standing and walking, drawn and holstered: no hand or shoulder lag; nothing turns
  in the Mako, wheels, scope, cover, conversations or menus. (research/31 section 8)
- [ ] The vignette: shape and strength comfortable, the HUD and sights above it. Walking toward the left hand goes where
  it points. (research/31 sections 3-4)
- [ ] Recoil taps in the right hand (and the left on the two-handed grip); a stick turn past an enemy keeps its speed.
  (research/24 sections 4-7)
- [ ] Life-size stereo: close each eye in turn on fog, light shafts, flares and the sun: the same place in both eyes,
  nothing popping in at the edges; faces and the wheel fuse in conversations; the pistol and doors look the right size.
  (research/30 section 6)
- [ ] Head-pose latency and judder with `earlyFrameWait = 1`. (research/22)

## 5. Menus and the HUD (session: VR mod for Mass Effect Legendary Edition)
- [ ] Our menu: the glass look, the text size at 1.3 m, the pointer's feel, the pause. (research/28 "Open")
- [ ] Use with A by looking: crates, lockers, doors, squadmates without turning your body; the label floats above the
  object, readable but not in the way; too narrow at the edge of your view? (research/40 section 4)
- [ ] The game's menus by pointer (inventory, pause menu, journal, map): the dot under the ray, the hover follows, the
  trigger clicks; the right stick scrolls the journal; on the map click, drag and zoom. (research/42 section 5)
- [ ] The wrist status: tune `wristOffset` / `wristWidthM` until it sits on your forearm; portraits readable at 8 cm.
  The gun's heat strip readable at arm's length; the sniper scope doesn't hide it. (research/41 section 7)
- [ ] A cutscene running into a conversation, menus over a flat screen: the panel is placed once in front of you.
  (research/15 "World-locked flat panels")

## 6. Picture and performance (session: VR performance optimization)
- [ ] Wear the PS VR2 at Eden Prime's heavy spot, on a Presidium walk and in a fight with today's defaults; Claude
  reads fpsVR and the frame probe. (research/35 attempts 41-43)
- [ ] Motion blur: in the first minutes after a cold launch and after each level change, does one eye smear on its own?
  (research/35 attempt 49)
- [ ] `sharePass = 5` (experimental) in a heavy fight: a strobing eye or objects jumping? (research/37)
- [ ] FSR upscale on and off at the same render scale: sharper than bilinear, no ringing, HUD text clean. (research/32)
- [ ] Quest 3 over Steam Link: the headset name in `[MEVR-COLOUR] headset:`, the Quest profile against neutral (hold A
  to compare), banding on the grey ramp, black level and white point. (research/27 sections 5, 8)
- [ ] One cutscene with `cineFpsCap = 0` against the capped 45 fps: which judders less? (research/16)

## 7. Claude checks these alone on the fake headset (no need to wear it)
Ask Claude to run them when the machine is free; each is a console command, a snapshot, a replay or a probe.
- Powers: the glow's render-thread cost; the self-cast powers (Barrier...) with the weapon away. (research/55)
- Melee: `mevr melee punch` with the weapon away: the pistol whip plays and hurts, no holster draw logged. (research/20)
- Right stick hold: the game's power wheel opens and closes from an input replay. (research/19 "Hold only")
- Mako: the cannon and the gunner against several woken enemies; `turretSeam` 1, 2, 4 one at a time. (research/47, 44)
- Cockpit panel by replay (the press, the swing's sign by the rules, the squares filling blue, `panel ask`'s three answers
  and the box at the windshield were checked 2026-11-05, research/48 section 27): the red refusals in a snapshot (press
  during the 30 s cooldown), the handle model's swing seen in a snapshot, the gunner's `(a hand works the cockpit's panel)`
  line (only logged when the gun was the player's before the press); the repair button's place on the desk shelf in a seat
  snapshot; the seat sampled too early (`seated with the head 0.00 ...`, 1 entry in 3; fixed in c330d4e: the line now ends
  `(N of M samples with a head offset)`); the fist on the handle's bar through the swing and the index pointing at the cap
  (2cac56e). (research/48 section 27)
- Squad orders: `mevr order` / `mevr say` / `mevr context` against woken mercenaries: the squad index base (is Shepard
  index 0?), `QuickCommandAttackTarget` honouring its target, the move point dropped along the swapped Rotation, HOLD
  keeping them put and `GetMemberFormation` reading HOLD back, retreat to cover (does the AI take the slot or stand beside
  it), the AI accepting re-orders every `reassertS`, `mevr context`'s type words, power names and cooldowns, `mevr say` on
  the compound sentence, a direction order's point (`mevr say "Garrus, go left"`: 8 m to the player's left), whether
  `BioPawn.PlaySoundSet(59)` (HoldingPosition) has a line for a squadmate, and the trace cost while a hold is kept.
  (research/54, every `[verify]` after the trace section)
- Galaxy map: the top level in the window, going back out a level. (research/51 section 8.1)
- Screens: the omni-gel override at an encrypted crate (`mevr skillgame`). (research/53 section 6)
- Omni-tool screens: fingertip replays on Squad, Map, Journal, Codex and Computer; the squad member on the XMods screen
  and with a squad of three; Toggle Helmet where helmets come off (the Normandy) against the flat screen; the UI world
  capture's cost at a heavy spot. (research/57 sections 6-7)
- Spectator camera: head camera with `viewActor = 1`, the cost off / 1080p / 4K. (research/49)
- Citadel: a crowd spot with `sharePass = 0`. (research/50)
- Gun: the muzzle through a wall doesn't shoot through it; camera modes' hook-bone gap near 0. (research/14, 18)
- Wheels: the weapon and power wheels' crops in snapshots. (research/15)
- Sights: a snapshot per gun model under an ADS replay (window ring, scope picture, the sniper's lens glow); the near
  plane's log line. (research/21 sections 13-16)
- Scope capture: picture checks against `scopeCapture = 0`, the frame checks under two Draws and `sharePass 5`, the cost
  and the dark speckle at the heavy cell. (research/39)
- Level camera under a firing replay; the logs of comfort, colour, the main menu and life-size stereo. (research/24, 27,
  28, 30, 31)
- Combat stance: the blend fix after a load, `drawRate` 1 / 2, crouch drop 0 / 1, `exploreStance` (off by default:
  ship or hide it). (research/38)
- Vent: the heat at which each gun overheats, from a firing replay. (research/45 section 1)
- Performance: the kept shader states level off, memory flat; ProcessEvent calls per frame. (research/35, 36)
- Reflection: `reflect: N of N checks agree`, then mark research/34's stages verified. (research/34)
- Installer: our loader on the real game (Steam, EA app), Repair after Steam's verify, Uninstall byte for byte; the
  Launch button; "Install after I quit"; Report a problem; the first start of a never-started game. (research/33)
