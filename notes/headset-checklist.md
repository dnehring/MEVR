<!-- SPDX-License-Identifier: MIT -->
# What still needs testing in the headset

- [ ] 2026-11-12 live controller overrides (research/25 section 14): under the collapsed Advanced calibration
  group choose each hand in turn, enter Adjust live, and use only the opposite controller. Confirm the real hand
  and held gun stay visible and update immediately for all six axes, with trigger fine adjustment and stick-click
  reset. Verify menu pause temporarily releases and returns, no movement/firing/holstering/melee occurs, and floor
  lock remains aligned seated and standing. Check Done persistence after restart versus Cancel rollback, untouched
  other-hand/height/guided values, tracking loss with held input, profile/session changes, world-mode changes,
  renderer failure and both-stick cancel. Confirm the panel sits clear of the selected hand in both eyes. Do not
  treat the offline preview's stand-in background as an in-game screenshot.
  Controller references: orange follows the selected raw grip, cyan the other; changing manual offsets moves
  Shepard's hand relative to the reference without moving the reference. Rotate wrists to inspect the rounded
  grip, thumb controls and forward arrow; the generic shell must not look like a required hand placement. Then
  look close/away, lose tracking independently, and leave live mode: no stale marker or remaining marker layer.
  Confirm stereo depth, readability without covering hands, frame cost, and the unavailable message on layer failure.
  Use baseline: with nonzero offsets on both hands and height, reset each selected hand in turn. Only its six manual
  values should zero immediately and remain zero after restart; height, other hand and guided measurements stay intact.
  Compare guided versus uncalibrated runtime alignment on real controllers before proposing hardware pitch presets.

- [ ] 2026-11-12 guided aiming visuals (research/25 section 13): sightless pistol pickup, stereo alignment in each
  eye, steady capture and final dot; rendering failure must not capture an invisible pistol. Rifle pickup remains
  two independent orbs: move each freely after grabbing, choose comfortable spacing and wrist angles, capture all
  targets, then compare several in-game rifles. No generic stock/foregrip or sight forces a pose during capture.
  Check seated and standing, release/re-grab, near-cheek orb suppression and redo/skip. Confirm no sights or aiming
  dot during capture, and the final dot follows the saved personal pose.

- [ ] 2026-11-12 manual calibration (research/25 section 12): seated, recenter comfortably, then try +/- 10 cm
  height and confirm view/hands move together without changing physical crouch or measured body proportions.
  Adjust each controller's position and pitch/yaw/roll independently, holstered, pistol, rifle and two-handed grip;
  check aim/reticle, support grip, holsters and wrist UI. Turn/tilt the controllers to check local offset directions.
  Reset offsets, restart, change controller profile, and rerun guided calibration: measured results stay separate,
  manual values persist per setup, reset leaves no residual shift. Check standing and seated with no floor tracking.

- [ ] 2026-11-13 character shadows off (research/37 "the correction doesn't reach the character shadows"): at the
  Presidium or any lit floor with people on it, no character shadow in either eye (Shepard's, people's), and the
  level's own lighting and shadows as before. The correction itself still fails in the headset (2026-11-13): the right
  eye's character shadows sat where the left eye sees them; check it again only once research/37 says it's fixed.

- [ ] 2026-11-09 Shepard's saved standing height (galaxy-labels; research/58 section 8): load a save, stand holstered a
  second (log `measured Shepard's standing eye ... saved`), then load again: the log names the saved value and Shepard is
  your size from the first frame, no slow resize. Swap armour (a new mesh: measured once). Seated: the same without standing.

- [ ] 2026-11-09 seated holster reach (galaxy-labels; research/19 section 12; owner builds): with seated mode and both
  a fresh profile and a tall saved standing height, draw/return pistol, cross-draw shotgun, both shoulder rifles; check
  chair arms/thigh clearance, early buzz, look-down/lean/yaw/recenter, no accidental draws while resting or aiming.
  Take/return/throw grenades with both hands, verify grenade versus holster/support-grip priority and repeat with weapon
  holsters disabled. Reload and restart after changing each seated offset/radius; standing offsets/remaps/off switches
  must survive. In a combat area use the native weapon wheel to choose Shepard's pistol/rifles from holstered, release,
  hold/release the right grip, cancel/close without a choice; verify fallback opt-out and trigger refusal. Switch Standing:
  original hip/shoulder/chest reach, height scaling, physical crouch, L3, grip-hold, powers and weapon-wheel policy remain.

- [ ] 2026-11-08 in-memory camera diagnostic (research/63, performance priority 1): with a normal build of
  the candidate, open classic VR hands calibration and confirm current camera measurements. Use trigger
  alignment after recentering, then verify the explicitly saved profile survives restart. Arm-length Save
  should still persist. Fake-headset validation checks publication and automatic-write removal; it does
  not certify the user's alignment or physical headset comfort. The private QA binary is restored after testing.

- [ ] 2026-11-08 Normandy pacing (research/63): repeat the fixed-save sweep under physical SteamVR
  and VDXR after frame-aligned instrumentation or a diagnostic-write candidate is ready. Local
  SteamVR null-headset runs at 2284x2448 and 3072x3188 expose periodic game-thread hitches but
  do not reproduce the 30-60 ms submission stalls; the fake session runs faster than its nominal
  90 Hz. Confirm the actual installed build includes conditional menu-return reset suppression.

- [ ] 2026-11-08 motion-blur guard (research/62): on a planet where nearby assets previously smeared or
  disappeared, pan slowly with AA and One Draw enabled. Check after a cinematic if one occurs naturally;
  finding a cutscene is not required for the native re-enable test, which already passed in Normandy.
  The installed guard passed forced native restore and navigator-conversation return instrumentation
  under Quest/VDXR; the owner could not judge the planetary symptom from that scene. Visual acceptance
  remains open. The combined candidate retains startup/load protection and restores the menu-return
  fallback if the guard is unavailable or sees an unknown effect.

- [ ] 2026-11-08 combined menu/VDXR/blur candidate (research/57 and research/62): launch through VD,
  confirm the rail slides to the screen's right and the omni-tool lights up, then open/close menus
  and return from a crew conversation. Confirm `skipped menu-return reapply` in the log while the
  guard is healthy, AA and sharePass 5 remain active, and compare the return hitch and slow pan.
  Combined headset QA remains pending; the earlier guard-only run did not contain the new rail.

- [ ] 2026-11-08 Virtual Desktop replay rejection (research/61): with the guarded-rearm build and automatic
  unsupported-call diagnostics, reproduce the player's natural front-end/Normandy failure on Quest + VDXR.
  Capture the normal report without changing graphics settings first; inspect caller stack and context identity
  and capture effective game anti-aliasing. Local FXAA-on testing now naturally reproduces three unsupported
  calls, first slot 51, under SteamVR at the report's dimensions (clear caller exe+0x99B277, FXAA exe+0x23ABB6).
  The fix now replays its uint clear and indirect dispatches, preserves UAV counters and avoids nested
  dispatch double-recording. WARP/hardware GPU tests and the local AA-on Normandy run pass at those dimensions
  (625 frames, no unsupported-call fallback). Check it under VDXR; compare title quads with asymmetric gameplay
  projections, verify AA remains enabled and One Draw remains active, and check for repeated jumps.
  Local native reproduction does not establish the original player's caller or headset acceptance.

- [ ] 2026-11-07 seated play (galaxy-labels; owner builds/deploys host + mod): choose I play seated at height and at crouch
  in separate runs; never need to stand/crouch, B goes back and Save/restart retains position per controller. Compare a new
  seated profile (Shepard's standing size) with one holding a known standEyeM. Lean/lower/raise the head: view moves 1:1,
  body size stays fixed, no physical crouch; L3 crouches/stands. Recenter seated, switch Standing/Seated in the menu;
  check hips/shoulder holsters, grenade chest slot, wrist target and Computer screen holds; standing calibration/crouch
  must stay as before. With no saved standing reference for his mesh yet (`[Body.Shepard]`), holster and let Shepard stand
  for about a second to settle it; it is saved for the next loads.
  See research/25 section 11; offline previews and rules tests are not headset acceptance.

- [ ] 2026-11-07 held floor lock + body bounds (galaxy-labels; source changes, owner builds/deploys): with `[Body] floorLock = 1`,
  compare arms on foot vs omni-tool rail > Computer/MenuBrowser (mode 7), moving both controllers; no height jump on open/close.
  Stand and recenter without crouching, draw/holster, physically crouch and button crouch; check world conversation/map holds too.
  Hold elbows at 90 degrees and arms out while turning right with the stick; repeat with floorLock = 0 (bounds independent).
  Check `floor lock: world UI hold start` delta near 0, `IK hold start` floor/scale, `body bounds:` per pawn/mesh after loads.
  See research/58 section 7; no build, deployment or headset result claimed for this patch.

- [ ] 2026-11-07 adversarial-review fixes (full builds/deployment pending): run the per-finding checks in
  [codex-review-2026-11-07.md](codex-review-2026-11-07.md), especially held voice through restart/drain, XR stop with floor
  lock, sprint across loads, and GPU-heavy Quest pacing with/without completed probe evidence.
- [ ] 2026-11-07 Quest pacing (quest-pacing): repeated identical loads on Meta OpenXR and SteamVR over Quest Link,
  `frameLoop = halcyon` then `full`; 30 s captures and `quest pacing:` logs/snapshot. Confirm sustained full cadence,
  refresh restored after a cycle, no blank frames or cine/menu pacing regressions; PS VR2 default regression (research/22).
- [ ] 2026-11-07 reticle fix: UNC20 holstered/drawn, exit/re-enter Mako with cockpit on (also controls/gunner); no centre cursor in either eye or mirror, aim-screen marker retained; check sniper, galaxy, cutscene, world UI and VR-off restoration with `mevr reticles` (research/44).

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

### ME3 Miranda's re-rigged hair, body and look (session: hair-miranda, built 2026-11-19; tools/rig/README.md)
- [ ] With `MEVR_Miranda.pcc` installed (`tools/rig/install.ps1 -Game LE3`), at her Citadel apartment: her hair at rest
  looks like the game's (no stray cards, the side locks forward to the jaw), and bends and swings as she or the head moves
  (`[Hair] followBody` 0.7: how much the ends rest on the shoulders; stiffness and damping as before).
- [ ] The breasts and glutes at `jiggleIntensity` 0.5 (+-1 cm on a jump): visible at conversation distance, and natural?
  `mevr hair jiggle 0.75|1|off` for a quick A/B.
- [ ] Her warm blond highlight (`kMirandaLook`) in two lights (the apartment's window, a warm interior): right strength?
  `mevr hair param <name> <value>` live; the pick goes into `[Hair] look` or the table.

### Floor lock (session: floor-lock, built 2026-11-07; research/58 section 6 and research/30 section 8)
- [ ] After a separately authorized host+mod v34 deployment, PS VR2 standing eye 1.84-1.85 m: recenter standing and bent,
  explore/draw/holster/ADS, idle for a minute, stairs and slopes. `floor lock:` final eye height should equal live STAGE
  height; raw camera changes should be cancelled. Verify reflection CollisionHeight and PVT sign against the actual view.
- [ ] Physically crouch: one real-head drop. L3/button crouch while standing: game crouch/collision retained, no extra camera
  drop. Body-scale target stays fixed (~1.10-1.11 for the measured Shepard); heartbeat names its standing reference.
- [ ] K third person, cover, Mako, front end, menus, 3D/flat cinema and the galaxy's own camera: no floor correction.
  World conversation entry/exit and galaxy-map hold retain the corrected on-foot height, with staging and ADS still aligned.
  File `[Body] floorLock = 0` restores the former view and filtered sizing; no menu row. Old replay recordings cannot prove
  floor lock without the per-frame floor fixture described in research/58.

### Powers at the left hand (session: Biotic/tech attack targeting system)
- [ ] X only selects powers on foot (tap and hold); a grip at the left chest throws on a fast release; X still works in Mako/menus/conversations; check Overkill disabled diagnostics with assault rifle drawn/holstered (2026-11-07, x-button; research/55 section 6).
- [ ] Self-cast confirmation (2026-11-07, power-confirmation; research/55 section 10): equip Overkill/Marksman with a rifle
  and pistol; find the orange target on the gun's receiver/barrel, approach to cyan, slap fast and touch slowly (0.35 s
  ring), then check the cyan flash, power name and two beats in BOTH hands. Retry during cooldown and with the gun holstered
  (right hand raised): red flash and a readable reason. Try Chest/Raise too; a closed foregrip must never cast. Targets must
  still show after more than three equips. Check target height/size, text by the hand, and one diagnostic line per attempt.
- [ ] Only a ready power shows its target; No power (2026-11-09, research/55 section 10): with Overkill equipped, draw the
  pistol and shotgun: no target on them (a slap says "Assault rifle only"); the assault rifle: the target; cast it, and the
  target is gone until the cooldown ends, then back. A hand resting on the gun meanwhile gives no tick. Hold the foregrip with
  the left hand: no target while held; let go and move the hand away: it's back. Then pick No power, the
  wheel's last tile (a struck-through ring): no target on any gun, no brackets, the game's HUD still fine; pick Overkill again.
  The log's `power wheel: Overkill's gesture target hidden: it needs the assault rifle` / `ready` lines follow the switches.
- [ ] Hold X: the wheel opens at your left palm and stays put while your hand moves. Pick by moving your hand and by the
  left stick; letting go of X equips. On a real save, powers you don't have yet are dimmed. (research/55 section 6)
- [ ] The wheel's light comes from the omni-tool on your hand (2026-11-06): with the wheel open, does the bright point sit on the
  omni-tool's display on Shepard's hand and follow it as your hand moves, and do the pulses running out to the tiles, the
  shimmer and the turning rings feel alive without being busy? The log's `power wheel: #N its light comes from the omni-tool's
  display, (x, y, z) cm from the left grip` says what it used. (research/55 "The light from the omni-tool, alive")
- [ ] How to cast it (2026-11-06): equip a power on the wheel; a card where the wheel was names it, draws its gesture and says how
  to cast it, and goes when you cast. Is it in a good place, readable, and do the drawings show the gesture (Throw: raise and push;
  Barrier: palm on the chest; Marksman: slap the gun; Unity: hand over the head)? Also the menu's Controller guide > Powers.
  (research/55 "How to cast it, on a card")
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
- [ ] Cockpit joystick: twist it about its axis while holding it: the grip model turns with your hand (up to the 42-degree
  lock), on top of its tilt, and springs back straight when let go. Does it turn the same way as your hand? (research/47 "The
  stick tilts", the model's twist built 2026-11-05)
- [ ] Repair on the throttle: holding the throttle lever, click the left thumbstick: the repair runs at once, a strong
  pulse in the left hand. With no omni-gel or cooling down, two buzzes and the shelf's squares red; a click begun off the
  lever does nothing. Is the click easy to find while driving, and never pressed by accident? (research/48 section 27 "The
  repair on the throttle", built 2026-11-05)
- [ ] Extraction handle overhead: grab it with a new squeeze (never fires the cannon or boosts), pull it down about 5 cm
  (as its arrows point; pushing up does nothing since 2026-11-05: the model swings 10 degrees down, the bar about 1 cm), hold
  at the end: the "Return to Normandy?" box shows at the windshield with the cockpit still around you, and takes a
  click, a touch, A and B. Say whether the handle swings the right way and its label reads from the seat. Shepard's hand
  closes on the bar and follows its swing; pressing the button, Shepard's index points. (research/48 section 27, hands built
  2026-11-05)
- [ ] The ceiling lamps' glow spills a haze over the windshield's top (kept for now as the cabin's light on the glass):
  keep it, or leave the lamps right above the windshield (the spine's drop panel) unlit? (research/48 section 26)
- [ ] Frame rate on the PS VR2 while driving with the cockpit up (more pixels than the fake headset). (research/47
  "The cockpit's cost")
- [ ] Head aim in the cockpit (built 2026-11-18): the turret, the shots and the reticle follow where you look while both
  hands drive; does it feel right in a fight, and is the centred reticle comfortable? (research/47 "Head aim and the
  auto-turret switch")
- [ ] The auto-turret switch on the stick's base (the toggle nearest you): flip it with a fingertip (either hand): the lever
  tips away from you, a click in that hand, its glow turns from orange to blue and the gunner takes the turret; flip it back
  and your head aims again. Is it easy to find and flip without looking long, never flipped by accident while gripping the
  stick, and does the lever swing the right way? (research/47 "Head aim and the auto-turret switch")

### Game screens in the room (session: VR mod for Mass Effect Legendary Edition)
- [ ] At a vendor: click the shop's buttons with the pointer and by touch (the test rig never got a click through).
  (research/53 section 4)
- [ ] Loot bodies and containers, open an encrypted crate (the bypass on your left wrist), play the minigames in a
  mission. (research/53 sections 3, 5.1)
- [ ] The wrist projector: placement above the omni-tool's display, the fan of light, the unfold. Tap it with your right
  index: tune `[Pointer] touchTipM` until the press matches your fingertip; the hover and press ticks.
  (research/53 sections 6.1-6.2)
- [ ] Play the bypass puzzle with the d-pad to the right of it: can you reach and hold its arms with your right index while the
  left wrist holds the screen up, does sliding between arms feel right, is 0.17 m (`[OmniTool] padSizeM`) the right size, and
  do In/Out and the arrows read as the puzzle's moves? (research/53 section 6.3, built 2026-11-05)
- [ ] Screens not yet seen in the room: Options, Save, Load, Specialization, Achievements, Additional content, Game
  over; tutorial pop-ups. (research/53 section 3)

### Galaxy map, conversations (session: VR mod for Mass Effect Legendary Edition)
- [ ] 2026-11-20 People leaving a first-person conversation (lane EXIT; research/conversation-exits.md): John02 slot 80, the
  council hearing played to its end in first person (`conversations = world`; `worldExit` is walk by default). After the
  conversation with Anderson and Udina, Udina should turn and walk off across the chamber and be gone once out of your view,
  not vanish beside Anderson; looking at him the whole time, he goes after a few seconds under a short fade. Also Wrex at
  Fist's door (slot 87). Does the walk look like his own (no slide), does the fade feel justified, and is anyone the next scene
  needs ever missing? `mevr convo exit off` is the old pop, for comparison.
- [ ] 2026-11-17 ME3's map on the CIC's projector (lane GX3; docs/galaxy-projector-handoff.md "travel, the opening and closing"):
  from the Normandy's CIC console, A: the camera's flight into the map should not show (the CIC stays, 2.5 s), then the map on
  the projector with no blue band over the pit. Fly to the Pax system's Mass Relay, travel to another cluster: the relay's movie
  flat, with no disc or labels beside it, then the new system on the projector at once. Recenter with the map open (Back
  twice): the disc should stay on the projector. Is the map readable from the console, and is the stick's flight comfortable?
  X back to the room.
- [ ] The hitches at the conversation director's cuts (research/69 item 24, "Frame timing at the director's cuts"): play the
  council hearing in first person (`conversations = world`) with a frame capture running (menu: Advanced > Diagnostics >
  Measure frame timing, or `tools/perf-probe.ps1 -Seconds 110` as it begins), turning the head now and then. The fake headset
  shows no stall at any cut with `worldCamera` 0 or 1; this capture says whether the headset's do and whether GPU, render
  thread or game thread (2026-11-13, VR performance session).
- [ ] 2026-11-08 landing squad screen (codex/galaxy-one-display, research/53 section 7): Land on an uncharted Mako planet
  after map travel; PartySelect must be readable and clickable in the room, with no remaining cinema screen, old Normandy
  viewpoint or floor clipping. Pick two squadmates and confirm; check the Mako view and extraction/return to Normandy.
  Repeat a normal airlock PartySelect and a shop/omni-tool screen. Log should discard the saved view on player/world change;
  landing before an on-foot camera should say `uses the destination's current camera` and show no held-view swap.
- [ ] 2026-11-08 table return (codex/galaxy-one-display, research/51 8.4): from the CIC, select galaxy -> cluster -> system ->
  planet, use Survey / Return and open/cancel/confirm Travel with ray and fingertip. All map pages and buttons stay at the
  projector; no Halcyon galaxy flat panel, including warm-up. Travel through the movie and any arrival radio conversation;
  the return must align with the same console, not float tens of metres away. Back out each level, close/reopen twice,
  then reopen directly at system/planet. Check both eyes and labels; expect one capture pause per interruption, a
  `returning to the CIC table` line and one window restart, no repeated start/pause loop. Not run in game for this patch.
- [ ] At the CIC projector: pick a cluster, a system, a planet, then Travel. Does the travel box take your click? (The
  test rig ignored it.) This is also the first headset check of clicks on any flat menu. (research/51 section 8)
- [ ] From the console: are the map's labels readable, and does the disc's near edge draw over the console? (research/51
  section 8.1)
- [ ] The 3D screen without crossed eyes (2026-11-12, research/52 "Cross-eyed"; players reported it): the dialogue wheel,
  subtitles and the title's menus now stand 0.5 m in front of the screen (`[Cinematics] uiLayer`, `uiForwardM`, live), and
  over-the-shoulder shots no longer come out of it (`convergeM` 0.8). Read the wheel over Shepard's shoulder (Avina on the
  Citadel): it should look in front of the shoulder, with no doubling. Faces should still look round, not cardboard (if flat:
  `convergeM = 1.0`). Pick a reply with the pointer on the title's menus: the dot should land on the button you aim at.
- [ ] A conversation and a cutscene on the 3D screen at the new defaults (2.9 m wide at 3.5 m, depth 1.0%, 16:9, the
  headset's full rate; research/52 "In the headset"): no cross-eyed strain (also at a conversation's first second), subtitles
  inside the 16:9 frame (if cut: `[Cinematics] aspect = 0`, live), camera pans smooth at 90 (if they stutter: `fullRate = 0`,
  live), the dialogue wheel readable, the pointer picks a line. The 1.5% / 3 m at 2.6 m first default was "a little
  cross-eyed" (2026-11-06). The screen stays put when you lean or look away mid-scene. The floor and horizon around it give a
  sense of place without competing with it (too bright or too dark: `[Cinematics] backdropBrightness`, live; off: `backdrop = 0`).
  Entering and leaving a cutscene or conversation: a short dip from black instead of a hard cut, smooth, not too long or too
  short (`[Cinematics] fadeS`, 0.35 s, live; 0 off).
- [ ] **People walk to their marks** (2026-11-11, research/69 "People walking to their marks"; `[Cinematics] worldWalk = 1`, the
  default with First person): in the medical bay scene on the Normandy (Anderson, Ashley), when the scene moves someone to
  another place they turn and walk there with their own walk, not a slide or a jump, and stop on it; seen on the fake headset
  (Anderson mid-stride, then standing on his mark; the ankles alternate 30-70 units apart at 130 u/s). Watch for: gestures
  coming back on their next line, a walker turning to face the scene once there, anyone walking through a bed or a person,
  and anyone who stops short (they stay where they got to; the log says `the game's AI didn't move it`).
  `worldWalk = 0` off, `2` the old slide.
- [ ] **Groups composed** (2026-11-11, research/69 "The composition of groups"): when a scene opens with a cut and someone
  is brought into view, they stand turned in toward the group (not square to you); a squadmate who isn't in the
  conversation stands beside you, about 1 m to the side; in a scene that opens without a cut, someone out of view within
  8 m walks into the group instead of appearing. Watch for anyone placed inside furniture or facing away from the speaker.
  Since 2026-11-11 those brought into view take the seats that let the people who talk to each other see each other
  (research/69 "Seats for the pairs"): in Joker's cockpit scene Kaidan, who talks with Joker, should be where he can look at
  him without twisting.
- [ ] **Walking during conversations** (built 2026-11-09; checked on the fake headset with `mevr convo walk`: 5.47 m during the
  Garrus wheel conversation, nobody snapped; the squad stays put; a 0.7 m clearance around people built after it, unchecked:
  walk straight at Garrus and you should go around him; research/59 "Walking during conversations";
  `[Cinematics] worldMove = 1` with `conversations = world`): in a wheel conversation (Garrus at C-Sec Academy), with the wheel
  down, push the left stick: you walk where you look at a walking pace, the people turn to follow you, the subtitle stays under the
  speaker's face and the wheel in front of you when it opens; at 3 m from the speaker you slide along an invisible circle, a wall or
  a ledge stops you; the wheel takes the stick while it's up. Watch for your body or arms left behind, a snap back onto the mark at
  a camera cut (the log's `kept in place` for Shepard), or the squad standing where they were. `worldMoveLeashM`, `worldMoveSpeedMS`
  live; `worldMove = 0` off.
- [ ] **The fade to black moves you** (2026-11-16, research/69 item 41; Claude can check the council walk on the fake headset
  first): in first-person conversations and scripted walks, whenever the game fades to black and moves Shepard (into an elevator,
  across a room), you come out of the fade where he is, never left behind; the walk with Anderson to the Council still walks.
  Log `convo: at N s under the game's black screen the player goes with Shepard`, `moment: ... under the game's fade to black`.
  `[Cinematics] worldBlackMoves = 0` is the old rule; `worldCuratedWalks` adds walks that keep walking.
  The lift up from Liara's cell on Therum (research/69 item 42): you ride up with it, the conversation's view never stays at the
  bottom, and you can walk on the lift (log `convo: ... the player rides with it`).
  The dancer's seat at Chora's Den (research/69 item 43): first person now, not the 3D screen; you sink into the seat with
  Shepard, facing the dancer, the stick doesn't walk, you come back up when he stands (logs `convo: ... Shepard sits down`,
  `... gets up`). Comfort of the sink and the seated eye height.
- [ ] **First person for every conversation: the robustness round** (built 2026-11-09, research/69 "To check in the game"; Claude
  checks these on the fake headset first, when the game folder is free). Checked there 2026-11-09: placements withheld at their
  source (the wake-up scene: 28 withheld, 0 undone), the walk stopping at a trigger's edge, and another scene's start held during
  a flowed scene and fired again after it (Chorban over the Garrus argument). Still for the fake headset: `worldAutoScreen = log`
  naming the scenes with no place for the player. Then in the headset: a staged conversation
  (the medical bay, Anderson on the Normandy) with nobody shifting at the cuts, and people still turning to face the speaker.
- [ ] **First person from the menu** (2026-11-10: Comfort > Conversations > First person (preview); the owner's call whether
  the button stays): choose it in a level, talk to someone, switch back to 3D screen: each change applies to the next
  conversation, nothing stays held. Then the sample in research/69 "The headset sample", one scene of each kind.
- [ ] **The validation saves** (2026-11-10: John02 slots 80 to 89, table in scratch/validation-saves.md; written, not yet loaded
  once): does each load, do you stand a few metres before the scene with nothing started, and does the scene start when you
  use the person or walk in? Note the slots that do not start (their plot may want more than the generator set).
- [ ] **The council from slot 80** (research/69 "The view walks"): use Anderson. He turns and walks up the first flight; a short
  dip as the script switches him off; the hearing begins with no dip while you stand at the stairs; walk up, or leave the
  stick and be walked the 21 m to the podium, where your body is under you again. Look for: your own body visible anywhere
  on the way, the flash of the cinema screen as his walk begins, how the dip reads, and whether arriving next to Udina with
  Anderson already there is acceptable until he walks the rest himself.
- [ ] **Voices from where people stand** (research/69 "The voices, placed", 2026-11-10; measured from the PC's output,
  never heard): slot 81 at the podium with headphones: Anderson beside you should sound to your right and stay there
  in the room as you turn your head; the Council should sound as before (in the middle, an intercom). Slots 87 and 89:
  the speaker in front. Walk away from someone speaking: a little quieter by 4 m. Look for: a voice that jumps between
  ears on small head turns (the pan is plain left/right, no HRTF), the step at 8 m, a voice too quiet at a distance
  (`worldVoiceFalloffDb = 0`), and whether the far voices want an intercom's colour (none yet). `worldVoices = game` is
  the game's own mix.
- [ ] **The game's audio for two ears (OpenAL Soft)** (research/69 "OpenAL Soft as the mod ships it", 2026-11-10; in the
  deployed build, measured only): with headphones, on foot and in fights, do sounds come from where they are, including
  behind you; does anything sound wrong or missing; is the level the same as before (`[Audio] gainDb`, 3, makes up the
  3 dB OpenAL Soft's mix was under the game's)? A
  conversation: a voice beside you is beside you. `[Audio] spatial = game` in `ASI\LE1MEVR.ini` (next start) is the game's
  own audio to compare; `soft` is OpenAL Soft without the head filters (for loudspeakers).
- [ ] **People talking near you, and your own gun** (research/69 "The audio list", 2026-11-11; built offline, not yet run in
  the game: Claude checks items 6 to 9 of research/69's "To check in the game" on the fake headset first). With headphones,
  on foot: people talking within a few steps (the Garrus argument at C-Sec with first-person conversations on, a squadmate's
  banter, people chatting in the Wards) should sound from where they stand and stay there as you turn, not inside your head;
  walking away they get a little quieter, past 8-10 m they go back to the game's mix. Your gun's shots: from the gun in your
  hand, not from between your ears (if they still sound centred, the shot belongs to Shepard's pawn: say so). Listen for: a
  voice that starts in the middle and jumps to its place, someone placed who isn't talking (their other sounds should not
  move), a crowd where the speaker isn't placed. The menu's Sound page: Headphones 3D / Speakers / The game's own and Loudness
  (both from the next start), People around you and Your gun (at once).
- [ ] **Who looks at whom, and nobody pivots** (research/69 "Who attends to whom", 2026-11-10; run in the ceremony on the
  fake headset): slot 81 at the podium. When a councillor speaks, Anderson and Udina should glance at the Council; when
  Anderson speaks to you his head and chest turn to you while his feet stay toward the Council, and Udina keeps his back
  to him; nobody's body turns. Then walk behind someone who is speaking to you (a 1:1 conversation, slot 87 or 89): past
  about 80 degrees held a second the body re-faces you, slowly, and it is a slide (no animation exists). Look for: the
  speaker's chest twist looking strained (`worldLookReach = 0` is the game's own, weaker reach), bystanders' glances that
  feel like too much or too little (`worldGlanceDeg`), a slide that bothers you (`worldTurn = off`: no body ever turns).
  Since 2026-11-11 nobody re-faces you while you stand where the scene put Shepard, and nobody seated ever does (Joker
  turned in his pilot's chair: research/69 "Virmire and the cockpit"): walk off the mark to see a re-face at all.
- [ ] **The ceremony again, slot 81** (the three fixes of research/69 "The ceremony in the headset"; not yet run in the game):
  your own body and arms stay while the council speaks; picking a reply with the stick no longer moves you; no blink at the
  end of the walk along the pathway (the blink as the walk begins is still there: known). And slot 80, the hearing: the
  dip as Anderson is switched off on the stairs should still come.
- [ ] **Scripted walks met in play** (2026-11-11, research/69 "The scripted walks, named"; on the fake headset): Noveria, when
  the whole squad goes down and the script brings you round: the world stays (no cinema screen for a moment while the screen is
  black); the double homicide in Port Hanshan after talking to Anoleis: one dip to the crime scene and you face it (not a wall);
  Feros's first creeper on foot: you walk after Shepard, no cut. Look for a flash of the screen at a scene's start, and where
  you face after any dip.
- [ ] **The hearing after walking with Anderson** (2026-11-13, research/69 "The body that stayed at the mark"; fake headset
  with replayed hands: the body within 0.11 m of the eyes, from 3.85 m): slot 80, use Anderson and walk with the stick while he
  walks up, and keep pushing through the fade. At the podium your body should be under you, and stay under you through the
  hearing and the two conversations with Anderson after it; the stick should not walk you until you let go of it once. Look
  down and at your hands. The log should have no `arm IK: body anchor: third person` during the scenes, and the summaries
  `"thirdPersonS": 0.0`. While you climb the stairs behind Anderson, Udina should stand at the podium and the three
  councillors on their balcony (research/69 "The council during the walk"; Saren appears with the hearing); say whether the
  chamber still looks empty or anyone pops in.
- [ ] **Kirrahe's speech, slot 88 again** (2026-11-11, research/69 "Kirrahe's camp"; the save fixed, not yet loaded): use Kirrahe:
  the planning conversation should play first (Kirrahe on the stage, not missing from his shots), then the speech to his troops
  with its music, and the screen should not stay black afterwards. Still on the 3D screen until item 20 is run in the world.
- [ ] **Long dialogue choices** (2026-11-11, research/69 item 23; built, not yet run in the game): a wheel with long replies
  (the hearing's "The Council won't help us.") shows each reply whole on the wheel, the same text size as before, and no stray
  letters elsewhere in view.
- [ ] **The council hearing at your own height** (2026-11-13, research/69 item 39, "The view that floated"; fake headset: the
  opening on the mark, the cut under the fade): John02 slot 80, walk with Anderson, then the hearing: you stand on the podium's
  floor at your own eye height, not floating; when the hearing ends and the screen goes black you are with Anderson and Udina
  (a short fade), and you can walk with them or stay. `python tools/conversation-session.py` afterwards: no `audit:eye-height`.
- [ ] **Virmire's briefing and the fade inside a scene** (2026-11-14, research/69 item 40; fake headset: the 0.66 m step walked,
  no second blink; a move past 1.5 m under a scene's own fade not seen in the game yet): Kirrahe's briefing at the camp: when the
  screen goes black and people move, you end up beside Shepard's new place (a short step walked, a longer one under one dip), with
  no 3D-screen flash, even when the game hitches. Say if any black screen inside a conversation leaves you behind.
- [ ] **Conversations where you start them** (2026-11-12, research/69 item 28 "The conversational circle", second design; run on
  the fake headset): any conversation (slots 83, 85, 87, 89): you stay where you started it, the person you talk to stays where
  they are, nothing walks you. Someone who takes part later walks over a few lines before speaking; Avina (the hologram) and anyone
  seated never move. Walk around: slower than on foot, at most about 4 m from where you started, not while the wheel is up. Turn
  with the right stick (your comfort setting: snap or smooth; never tried with controllers). Nods and gestures as the game has them.
  Staged: the Council's hearing (slot 80) and Kirrahe's camp (slot 88, now in first person) open with a short fade to the scene's
  own places and nobody turns toward you. Also: the staged list in research/69 "The staged scenes" (which scenes should be staged).
  Your squad (2026-11-12, research/69 "The squad's formation, held"; fake headset): start a conversation with squadmates near you
  (your own career's saves: any NPC on the Citadel): they stay with you, stroll a step or two to your left and right, a little
  behind, and face whoever you talk to; no squadmate says "on it" or similar; after the conversation they follow you again.
  Heads (2026-11-12, research/69 "Heads that look past the player"; fake headset): in Girard's scene (Presidium, a squad save in
  `%LOCALAPPDATA%\MEVR\squad-saves`, slot 80) Girard looks at your eyes, not at an empty spot beside him.
  Virmire's start (2026-11-12, research/69 "Virmire's start from above"; `%LOCALAPPDATA%\MEVR\squad-saves\John02_70.pcsav`, Kirrahe
  in front of you): after the planning the screen goes black; when it clears Kaidan and Ashley stand beside you and Kirrahe walks
  out to his troops behind you; you are walked half a metre as if by your stick (push yours to take over); turn round to watch.
  Eyes (2026-11-12, research/69 "Heads at the drawn eyes"; fake headset): in Virmire's start and at the Council's hearing,
  Kirrahe and the councillors look you in the eye, not just past you, and nothing of Shepard's own body is in front of your
  face; turn with the right stick during any conversation: you turn on the spot, not round a point in front of you.
  Walking along (2026-11-12, research/69 "Walking along"; fake headset pending): in Anderson's walk to the Council (John02 slot
  80), stand still: once he is 3 m ahead the walk stops and waits; walk up to him and it goes on. Walk along with him: it never
  stops.
- [ ] **The view beside walls** (2026-11-11, research/69 "The camera that fell to Shepard's knees"; fake headset): on foot,
  walk face-first into a wall, stand right in front of a pillar, let a squadmate cross close in front: the view stays at
  your eyes, never drops to Shepard's knees. `[Game] cameraCollision = 1` brings the game's camera collision back to compare.
- [ ] **Hackett's call at the galaxy map** (2026-11-11, research/69 "Hackett's call and the surveyor, run again"; fake headset):
  use the CIC's console with a transmission pending (after a mission): Joker's line and Hackett's call play where you stand,
  no cut; when the map opens after it, no flash of the flat screen and no double blink.
- [ ] **First person after many loads** (2026-11-11, research/69 "The coverage pass from saves"; fake headset): quickload
  20 times in a row without opening the pause menu (die and reload, or F9): the view on foot stays at your eyes, never behind
  Shepard. Halcyon's log says `[FPCAM] owned table full: N dead entries of earlier levels dropped` once you pass 16.
- [ ] **The ending's last walk, from Shepard's eyes** (2026-11-11, research/69 "The scripted walks, named", "The ending's other
  branch"; fake headset): after the Council's conversation on the 3D screen, one dip into the tower and about 15 s in first
  person before the ending's movie. In the snapshots the platform under you is pure black up to an edge 2 m ahead (the
  cutscene's cameras never look at it; not the near plane, not culling). Say whether it reads as a dark floor or as a hole
  you stand over: a hole sends this walk to the 3D screen. Both branches (paragon over renegade or not) behave the same.
- [ ] **Walking follows the stick** (research/38 "Linear walking" and "The start step and the speed from the push",
  2026-11-13; fake headset measured): on foot, push and let go of the left stick: you should move at once (no start delay), stop
  at once (no coasting), and your speed should follow the push (half a push a walk, a full push the run). Say how the legs look
  when you look down at in-between speeds, and whether a full push in every direction reaches the run (`[Hands] linearFullAt`
  0.9 lowers it). For comparison: `[Hands] startStopAnims = 1` (the game's start and stop steps), `linearStick = 0` (the game's
  walk/run jump at 70%), `linearWalk = 0` (the game's ramp). In a conversation the walk is 1.4 m/s again: say if that's too fast.
- [ ] **A scripted walk, standing still and walking along** (built and run on the fake headset 2026-11-10, research/69 "The
  script's Shepard, the player's view"; walking along since 2026-11-12, "Walking along"): slots 80 and 81. Stand still after
  using Anderson: no flash as the walk begins; nobody walks you any more: once Anderson is 3 m ahead the walk waits. Walk along
  instead: no blink on the walkway, and no wall a few metres in (fixed 2026-11-12). Look for: how it feels to have no body
  while your view is away from Shepard's (the cost of this design), how often the walk waits for you at full stick (three
  times in the Spectre's walk on the fake headset: the walk is faster than the stick), the blur gone from people during it.
- [ ] **No flash as a conversation begins** (the same note): any ordinary conversation from on foot (slots 85, 87, 89):
  until 2026-11-10 the 3D screen showed for an eighth of a second at every start.
- [ ] **The legs in a scripted walk and the walkway's blink** (research/69 "The legs under the player, the walkway's
  blink", 2026-11-10): slot 81. Stand still after using Anderson and lean forward to see your legs: they should stand
  (not walk in place); once you are walked or walk yourself they walk. Walk along with the stick: no blink on the walkway
  as the Council appears ahead (it pops in); is that better than the blink? (`worldDipWithinM = -1` is the old blink.)
- [ ] **Your own body in a conversation and a scripted walk** (research/69 "The first shot" and "The body that stayed
  culled", 2026-11-10; fixed on the fake headset): look down at yourself during an ordinary conversation (slots 87, 89), on
  the walk to the Council and at the podium (slot 81), from several head angles and standing off your room's centre: arms,
  hands and chest should be there throughout, the floor under you and Udina beside you too. Also on foot: glance down
  about 45 degrees with your arms at your sides, then raise your hands. Look for: the legs walking in place under you
  during the scripted walk (not fixed yet).
- [ ] **A walk-in you don't walk** (built 2026-11-10, research/69 "The guided walk-in"; on the fake headset Virmire's first
  conversation walked the player 5.3 m toward a 7.1 m mark; `[Cinematics] worldWalkInS = 5`, -1 never): start a conversation
  from a few metres away and leave the stick alone: after five seconds you should be walked to Shepard's place at walking
  pace, around people, and stop when you touch the stick. Is five seconds right, is being walked comfortable (no vignette
  yet), and does it read as the game leading you or as losing control?
- [ ] **Becoming a Spectre in first person** (ran on the fake headset 2026-11-10 with nobody at the stick, fired past its plot
  check: held 22.7 s, the track withheld, the script's teleport 9.8 m up the stairs taken as the same space): you should walk
  up to the council yourself, or be carried there after 12 s. Look for black flashes and for where the council's
  conversation finds you.
- [ ] **An NPC who walks during a conversation** (Lieutenant Durand on Chohe; research/69 "The morning round"): her walk
  should play as authored and end without a flicker (the walk's last step is accepted, not fought).
- [ ] **A conversation that opens without a cut** (built 2026-11-09, research/69 "The opening without a cut"; on the fake
  headset three Noveria conversations opened with no dip and the view where it was; `[Cinematics] worldOpening = auto` with
  `conversations = world`): walk up to someone and talk: no dip from black, you stay exactly where you stood, the person turns to
  you; Shepard's body should be under you, not a step away on his mark. Then one whose mark is across the room: still no dip, you
  walk over while they talk, the wheel waits. A dip should come only when the scene is somewhere else (after a cutscene,
  another room). Watch for people popping onto their marks in front of you at the start.
- [ ] **The walk to the council with Anderson** (built 2026-11-10, research/69 "A moment in the world"; ran on the fake headset
  with nobody at the stick: the view held, the teleport withheld, the hearing opened as a walk-in 21 m from the podium;
  all defaults of the first-person mode since 2026-11-10: only `conversations = world` is needed): in the Citadel Tower use Anderson at the stairs: he says
  his line and walks up; you walk with him in first person (no 3D screen, no black). **Since 2026-11-10 the hearing itself
  opens with a dip to the podium** (it begins 2.3 m into your walk, and putting Shepard back under you then ended it in 9 of
  16 runs: research/69 "The hearing that ended at once"); say whether that dip is acceptable until the walk-in is rebuilt
  so that the view walks and Shepard stays where the script has him. Stand still instead: after 12 s you are carried to the top of the stairs
  (`worldMomentPatienceS`). Look for: the black screen flashing, Anderson vanishing and another one standing at the podium,
  Shepard's legs walking on the spot, and how the hearing reads from the podium (it was made for cameras 38 m away).
- [ ] **A moment on foot: Chellick's bump** (built 2026-11-11, research/69 "A moment on foot"; seen on the fake headset with a
  replayed head and hands; host and mod together): on the Citadel's lower wards, use the spot where Chellick bumps into Shepard.
  You see it from your own eyes, full frame (no black bars, no shot of Shepard from behind); Shepard's body goes with you while
  the script walks his pawn; the conversation with him opens without a cut, a step away. Look for: a frame of the matinee's
  camera as it starts or ends, where Chellick bumps (he aims at Shepard's pawn, not at you), and the guided walk's pace.
  **Benezia's entrance on Noveria** the same way (built 2026-11-11, research/69 "A moment after a conversation"): after her
  conversation the walk down the stairs is first person, not the 3D screen; look for a flash of the screen between the two and
  how the squad in her stasis reads from there.
- [ ] **Flowing conversations** (built 2026-11-09; two Noveria chains checked as barks on the fake headset, research/59 "Flowing
  conversations" > "In the game"; `[Cinematics] worldFlow = all` with `conversations = world`): in a career that hasn't seen them,
  the walk-up scenes play as barks while you keep walking (Garrus and the Executor arguing at C-Sec, Chellick's opening lines,
  Udina and Anderson's arrival lines, Joker's first line), then the wheel conversation starts as First person has it. Look for
  the log's `convo flow: ... plays as barks`, **the subtitle on the HUD panel (none showed in the fake headset's frames, and none for the game's own barks either
  there: research/69 "The subtitle of a line played as a bark"; with your subtitles on, compare a bark of the game's own, people
  talking as you pass, with a flowed scene)**; since 2026-11-11 a flowed scene's lines are subtitled on the HUD panel above the
  health arc (research/69 "Subtitles for flowed lines"): say whether they are big enough to read, no camera cut, the people standing where the level left them (the stage's
  placement is skipped), the scene ending early if you walk off beyond 25 m. `worldFlow = off` puts the game's flags back live;
  `mevr convo flow` lists them.
### The omni-tool menu on the wrist

- [ ] 2026-11-09 rail/fingertip/power follow-up (research/57 section 9): tap the wrist, then Equipment. The standalone rail
  fades in place for 0.10 s during the 0.18 s screen unfold, then slides from behind the right edge over 0.43 s (cubic in-out),
  without crossing the screen. Completion log must include distance, duration, frames (about 39 at 90 Hz) and elapsed time.
  Touch/ray during emergence selects only the visible rail. Compare drawn right fingertip with touch on rail, Inventory's
  tabs/Equip, bypass opening buttons and puzzle d-pad: source logs must say Shepard's drawn fingertip through modes 7/10,
  with no accidental wrist close or projecting-left-hand click. Switch all tabs, close/reopen via the game, compare railSide=left.
  With `mevr omnitool on` and `mevr omnitool params`, inspect first-wake emitter/LOD0/distribution/InstanceParameters/material
  discovery and cache `0 entries` vs unavailable. Judge the 0.60 s dim-to-lit gauntlet/display/fan and unchanged close.
  A `nothing found: full at once` line is fallback; a named route needs visible coverage QA. Repeat power wheel and seated/standing.
- [ ] Rail handoff and provenance (2026-11-07, research/57): test touch and ray picks with identical wrist/head poses, including
  Equipment while facing a selected useable. No large Halcyon menu quad or UI-world backdrop should appear; Equipment -> XMods
  and Computer -> Options/Save/Load stay on the wrist, without `opened by using ...`. Genuine use-opened equipment keeps its anchor.
  Repeat with an excluded `[WorldUi] screens` entry (Halcyon panel), world UI off, a rejected opening, backout/screen switches,
  a load/unload, and the Mako; verify galaxy mode 9, mode 10's puzzle/d-pad, front end, movies, conversations and cinematics.
  Arm burst snapshots before selection; inspect composed eyes and layer JSON. The heartbeat's per-Present `flat-panel frames
  during reservation` must be 0; burst snapshots alone are not every-frame evidence. Check reservation release/cancel/timeout logs.
  Release tooling needs `[Debug] requests = 1`; pose replay needs `[Pointer] setCursor = 0`. No runtime checks performed for this patch.
- [ ] Tap your left wrist with your right index: the omni-tool's rail opens beside it (Squad, Equipment, Map, Journal,
  Codex, Computer, Close), its entries readable; a tap on an entry opens that screen on the wrist. Reaching for a wrist
  screen's lower buttons must not close it. (research/57 sections 2-3 and 6, built 2026-11-04/05)
- [ ] The hands' neutral trim (squad-placement session, research/25 section 10; second rule 2026-11-06 after the first rolled the
  hands palm up): calibrate again (Body & calibration > Start calibration), arms straight with wrists straight in the T pose and
  ahead. The log's `calibration: left hand: neutral trim (...), N deg from the default hand angle; the arms-ahead and T poses'
  fingers M deg apart` (and right). Without touching the hand angle, do the empty hands sit naturally on yours? Do the pistol and
  rifle targets show (the board failed for lack of swapchains on 2026-11-06)?
- [ ] The body at your size (squad-placement session, research/58, built 2026-11-06): on foot in first person Shepard's
  body, hands and gun are scaled so his eyes are at the view (`[Body] scale = auto`; the heartbeat's `body scale:` line says
  the factor). Your `Custom 1` profile lowers the view 8.2 cm (`cameraOffsetUp = -0.082` in MELEVR.ini): try it near 0 so the
  view is at your real eye height (the factor then about 1.08). Do the hands sit on your real hands, does the rifle feel
  life-size, do the sights still line up? `mevr body scale off` compares.
- [ ] The wrist tap's target (squad-placement session; built 2026-11-05 evening after the first try found nothing to aim at):
  weapon holstered, raise the left wrist: a faint orange button on the back of Shepard's forearm, just behind the wrist?
  Touch it with Shepard's right index fingertip (the hand you see, pointing): brighter, then cyan with a light tick, and the
  tap opens the rail. Does the drawn finger reach it comfortably, is 4 cm right? The log's `omni-tool rail: a reach for the
  wrist target` lines give the closest approach of each try. (research/57 section 3)
- [ ] The tap opens the rail late in a long session too (Biotic/tech session, 2026-11-06: on 11-06 the tap fired ten times
  and the rail never showed: SteamVR refused its swapchain, XR_ERROR_LIMIT_REACHED, after the calibration, the main menu and
  the power wheel had taken theirs). Play a while (calibrate, open our menu, use the power wheel, a scope, the galaxy map),
  then tap: the rail, the sights and the targets all still show? The log's `swapchains:` lines say what was refused and what
  was freed for it (`plugin/Swapchains`, gotchas/openxr-swapchain-limit).
- [ ] On the wrist's equipment screen with your real fingertip: the weapon tabs, Shepard's and the squadmates' tabs, a list
  row, Equip, a swipe to scroll. Does each land on the first tap (the targets are 0.8-1.4 cm)? (research/57 section 6,
  built 2026-11-05)
- [ ] The squad member behind the equipment and squad screens: there, switching with the tabs, comfortable at the
  backdrop's opacity (`[WorldUi] figureOpacity`, 0 = the backdrop's 0.8)? (research/57 section 7, built 2026-11-05)
- [ ] The wrist screens' size: 0.70 m wide now (0.55 before). Set it live with our menu's Comfort > Game screens > Wrist screen
  size (0.40-1.00 m): which size reads best without turning your head, with the rail beside it? (research/57 section 2, built
  2026-11-05)

### Thespian mode (session: Low-latency voice squad commands; research/70; built 2026-11-13, off by default)

Turn it on: the menu's Voice page > Thespian mode > Speak Shepard's lines (or `[Thespian] on = 1`).

- [ ] The panel: readable at its distance, low enough that the speaker's face is clear, not in the way of the hands;
      in a first-person conversation and on the 3D screen (there it stands ahead of the head, under the horizon).
- [ ] Read a whole line: the row lights within a few words, the words light as said, the reply is taken about a
      quarter second after the last word, Shepard's recording is not heard, the other person answers at once.
- [ ] Start reading while the other person still talks: nothing is taken until their line has ended.
- [ ] Say only the label; stop half-way through a long line (taken after about a second with Performer); say
      something that is no reply ("Didn't catch that", nothing taken); cough, talk to someone in the room.
- [ ] A real microphone's quiet clock: does a noisy room (a fan, the game through speakers) keep it from ever being
      "quiet"? (`mevr thespian status` shows `quiet N ms` and the level.)
- [ ] Pick with the stick instead: the game's wheel is hidden under the panel (Thespian) until the left stick moves; then it
      comes back where it always stands and the panel folds away. Shepard's voice plays as ever (or passes silently with
      "Silent"). Does a resting thumb bring it back by accident?
- [ ] Investigate topics from the main level; a charm or intimidate you don't have is greyed and can't be said.
- [ ] Names (since 2a7768a8, research/70 section 10): read lines with Saren, Eden Prime, Benezia at the Council hearing
      (validation save John02 80); does the log's `key terms` list them and the transcript spell them? Labels that share
      a reply (the hearing's last wheel: three labels, one line) stand as one row. Mumble the one word that tells two
      similar replies apart: "Not sure which" shows, nothing is taken, and reading it again or saying the label takes it.
- [ ] The squad's radio (left grip at the ear) still works on foot after a conversation, with its own key terms.

### Squad orders by gesture (session: Low-latency voice squad commands)
- [ ] Squeeze the left grip with the hand away from the gun's foregrip zone, the grenade slot and a hot receiver, and
  point with it: let go on an enemy group (attack), on the ground past 2 m (move there), near you, on a squadmate or on
  nothing (follow). Pointing at a squadmate as you squeeze gives the order to them alone; otherwise both get it. Judge
  the feel: the cones (8 degrees on an enemy, 12 on a squadmate), the haptic ticks, whether a squeeze meant as the
  support grip or a grenade ever becomes an order, and how long the AI takes to react (the game's own 1-1.5 s order
  delays). Built 2026-11-04. (research/54 "The flat orders", `[SquadOrders]`)
- [ ] Squad placement (session: VR mod for Mass Effect Legendary Edition, built 2026-11-05): hold the right B on foot, an arc
  leaves your right hand and a ring lies where it lands. With two squadmates a wheel opens at your right hand first: move the
  hand left (first squadmate), up (squad) or right (second) to pick, then aim; let go and they walk there and hold it.
  Double-tap B: everyone back to you. Drop it at your feet for a regroup (green); on a wall or out of reach it's red and
  calls off. Judge: the wheel's size and the 7 cm pick distance, whether a pick happens by accident when you start aiming,
  whether the pulses tell who without looking, the double tap's timing (0.35 s), the arc's reach (14 m/s: 20 m at 45
  degrees), and whether the arc drawn over walls (no depth test) confuses. With two squadmates, both sent to one ring: do they
  crowd? (research/54 "Placement by an arc", "The selector", "The squad wheel")
- [ ] In a fight, `mevr say "<name>, hold here"` typed by Claude while you watch: does a held squadmate stay put under
  the HOLD formation, does a hurt or crowded one visibly fall back to cover and come back when it's calm, and does being
  re-ordered every few seconds make them twitch. Claude can read the logs; only you can see whether it looks right.
  Built 2026-11-04/05. (research/54 "Standing orders", "A real hold", "Retreat to cover")
- [ ] Voice orders (built 2026-11-05, never heard a real microphone yet): hold the LEFT grip with the hand at your left ear
  (the radio), say "Garrus, take out those troopers on the bridge", let go. Judge: does the squeeze at the ear ever
  become a grenade, a support grip or a flat order (and the other way round); the haptic ticks (start, release, one for
  an order given, two short for "say again?"); whether the order lands before you've lowered your hand (the release to
  the squadmate's move or line); whether the game's own line comes after a move or attack, and whether our fallback line
  (`PlaySoundSet` 70 / 61) fits when it doesn't; names over Link / Steam Link ("Rex", "Gurus" on synthesized speech: the
  parser absorbs those; a name the rules can't place goes to the model); the ducked game audio. Set `[Voice] model`
  (tiny / small / medium) if the partials lag. (research/54 "The voice path built", `[Voice]`)
- [ ] The recognizer on one thread (2026-11-06 late, research/54 "One thread for the recognizer"; needs the host and `ASI\voice`
  redeployed together, the game closed for it): speak a few orders; does the game keep its frames while you talk (the log's
  `render levers: last 10 s: N frames` near 899 during a listen, 700 before the fix), Task Manager one busy core, not all,
  and `voice: ... the costliest N ms` under 100 for tiny? If a partial costs more than 250 ms now, `[Voice] intervalMs = 500`.
- [ ] The menu's Voice page (2026-11-06, research/54 "The menu's Voice page"; needs the host and `ASI\voice` redeployed
  together): open the menu > Voice. Is your headset's microphone listed and picked, and does the Input level bar move and say
  "Hearing you" when you speak (and stay low when you don't)? Pick another microphone: the status says Restarting, then Ready
  with the new name within a few seconds, and the bar follows the new one. Try Small, then back to Tiny.
- [ ] The ear's contact buzz (2026-11-05 evening, `[Voice] earCue`, `earCueStrength` 0.4): the open left hand hums in the
  left controller as it comes within `radioRadiusM` (0.20 m) of the ear point (8 cm left and 3 cm below the eyes), stronger
  nearer it, and stops when you squeeze (the listen's own tick) or leave. Does it find the spot without looking, and is 0.4
  felt but not annoying while the hand passes the head (reloads, the wrist tap)? (`game/Voice`, `EarCue`)
- [ ] A spoken or pointed move lands where meant (2026-11-05 evening: a move is the HOLD formation at the point plus the AI's
  `PushMoveToPoint`, the placement's road, since `QuickCommandMoveTo` ignores the point in VR): "Ashley, move up" with the
  left hand on a spot 5 m off, or the left grip let go on the floor there; she should walk to it and hold until it's calm,
  then come back. (`mevr voice say Ashley, move up`, log `squad orders: move to`)
- [ ] Landmarks (2026-11-05 evening, research/54 "Landmarks"): on a level with cover (Eden Prime's crates; the UNC20 drop zone
  has NO placed cover links, checked 19:50, and `open BIOA_PRO10` from its save did nothing: load an Eden Prime save),
  `mevr context` lists `L1 cover | left near level` lines (the game's own cover links within 25 m); "Ashley, get behind
  that crate" with the left hand on it, or "take cover behind the cover on the left", sends her to the slot on its far side
  from the enemies (log `squad orders: ... cover L1`). Every piece is called "cover" until the mesh words are learned; the
  objects walk every 2 s is a cost to read in the heartbeat. (`[SquadContext] landmarkRangeM`, `maxLandmarks`)
- [ ] The microphone's lag over your link: tap a controller on the headset while a recording runs with `mevr voice listen 5`;
  the tap's jolt in the poses against the click in the transcript's word times says how much tail `[Voice] tailMs` needs.
  (research/54 step 3)
- [ ] The model tier in play (`[Voice] llm`, llama.cpp and the fine-tuned GGUF in `ASI\voice\llm`, `model.gguf` since
  2026-11-05's run1: 93.9% exact on held-out synthetic lines): say something the rules refuse ("Garrus, you know what to do
  with the big one", "everyone, deal with the ones on the balcony") and watch the log for the model's answer and its time
  (the bench says about a second beside the game); the frame cost while it decodes (a perf probe during a few asks). Its
  misses are mostly which E group a description means, so the first real transcripts are the data the next run needs
  (`tools/voice-model/README.md`). (research/54 "The fine-tune")

## 2. Hands, body and the gun (session: VR mod for Mass Effect Legendary Edition)
- [ ] Arms fully out, then the gun at the cheek: the hands stay on the controllers, the elbows bend naturally, the arm
  length feels right, a gun at your face isn't pulled off your eye. (research/14 "Reach")
- [ ] Take the gun with your left hand anywhere along it: no zoom, no camera jump, no gun swing at the grab.
  (research/14 "Two-handed grip")
- [ ] Turn your real body 90 degrees, step, lean, walk sideways and backward: chest and hips face your way, Shepard steps
  under you; any moonwalking legs, neck stump looking steeply down, or view judder? (research/29)
- [ ] 2026-11-18 hand contact (research/73, `[HandContact] on = 1` by default, every game): reach into a squadmate's chest,
  shoulder, head and arm: the drawn hand stops at their body and slides along it, no snap through to the far side, a click on
  contact and a hum while pressed. Push the gun into someone: it stops too (the aim follows the controller). Too strong a
  hum, a hand stopped short of the body, or someone you can still reach through (another race's rig: `mevr contact people`)?
- [ ] 2026-11-18 the eyes inside the body (research/29 "The eyes inside the body"; `[Hands] crouchBody = 1`, on by default,
  read live): squat a little (10-30 cm, not deep enough for the game's crouch), bend at the waist to look at the floor, and
  look straight down at your chest, standing and squatting. You should see Shepard's chest and legs below you with his
  knees bent and his feet on the floor, never the inside of his collar or armour; the body follows smoothly down and back
  up. Compare with `crouchBody = 0` (the old way: the eyes sink into his neck). A kneel on the floor and looking straight
  down at the collar are the known leftovers: say whether they show.
- [ ] 2026-11-19 `crouchBodyUp = 1` (on by default since the owner's yes, research/29 "The body up to a standing head"): stand
  and press the left stick click (Shepard crouches by the game): his body comes up under your view, legs nearly straight, his
  hands on your controllers, feet on the floor; crouch deep enough for the game's crouch and come halfway up, and draw a
  weapon standing: the same. Does the straightened-legs crouch look odd, and does cover behind a low wall still count?
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
- [ ] ME2 (lane C, research/le2-vrik.md, checked on the fake headset 2026-11-14): Shepard's arms on the controllers, the
  gun in the right hand, shots along the barrel, no camera recoil (pitch held, the yaw kick removed), the gun kicking in
  the hand instead. In the headset: do the hands and the gun sit where yours are, does a shot hit what the barrel points
  at with the gun held well off the view (the start on the barrel's line, behind the muzzle), and does any shake remain?

### The game's start (session: release blockers)
- [ ] Launch the game with the headset on: the boot and logos on the flat panel, then the title, the camera flight after a
  key and the main menu on the 3D cinema screen, fixed in the room like a monitor: turn and move your head and the picture
  inside stays still (no panning with the head), the planet scene with depth behind the screen. The load's movie on the flat
  panel, no flash of the world after the key, then the level in VR. (research/56 "The front end on the 3D screen",
  checked on the fake headset 2026-11-05)
- [ ] ME2 cinema (M5, [replay and evidence](../research/le2-cinema.md)): play the Freedom's Progress shuttle conversation and
  its cutscene. Turn and lean to check that the screen stays in the room and the director's framing stays inside it. Check
  close-ups, reply text, floor and horizon, the 3D and flat choices under Comfort, and the fade back to gameplay. Check for
  discomfort or stutter at the headset's own refresh rate. With a pre-Veetor checkpoint, verify the prompt stays readable
  and that each controller trigger takes its matching native interrupt only while the prompt is available.

## 3. Aiming, sights and scopes (session: Weapon-specific scope models)
- [ ] The sniper rifle pushed into the shoulder, then held out at arm's length: the picture stays both ways (the gun moves off
  the shoulder by itself, up to 15 cm: comfortable?), no white glow from the lens while raised; the rifle's red dot stays with
  the eye at or past the housing; after drawing any gun, no game crosshair beside the hand reticle. (research/21 section 19)
- [ ] Calibrated (Body & calibration), shoulder the rifle and the sniper: the dot and the scope are there at once and stay
  while you shift your cheek; lowered off the shoulder they go. (research/21 section 20)
- [ ] The rifle's red dot at an ordinary raised hold (not pressed in): it shows, on the sight rather than beside it, and
  the shots land near it. (research/21 section 21)
- [ ] The rifle's outline (ring) and its red dot always together at any raised hold; pushed in past the window the dot
  stays; lowered, both go. (research/21 section 22)
- [ ] Raise the sniper rifle: the picture only at the eye (never at a casual hold), no on/off flicker, the eye reaches
  the eyepiece; lowered, the gun eases back. (research/21 section 15)
- [ ] Under the scope, flick the right stick forward and back: the zoom steps 3.5x / 7x with a tick; the reticle reads.
  (research/21 section 16)
- [ ] The scope's real zoomed render: sharp, upright, not mirrored, the reticle on target; the world stays put when only
  your head moves and follows the gun without lag. A black speckle in dark areas is known and kept for now
  (`[Sights] scopeCapturePost = 0` removes it and the grade); say if it bothers you. (research/39 sections 10, 12)
- [ ] The assault rifle's red dot at the cheek: shows without hunting, lines up. Close each eye in turn: the dot and the
  scope show in the aiming eye only, on SteamVR and on the Oculus runtime. (research/21 sections 7, 11)
- [ ] The assault rifle raised to the right eye: you see through its sight housing (the target and the dot in a round
  window, the box around it; with the left eye closed too); the gun stays where your hand puts it (no sliding by itself);
  the dot doesn't blink, also when the support hand's aimed fire comes on. Say if the window's edge or a near object in it
  looks shifted. (research/21 section 17, built 2026-11-05)
- [ ] Shot groups: the pistol at 10-20 m on the dot, the sniper at 5 m and 20-40 m on the crosshair, no drift to the
  right. (research/21 sections 9-10)

## 4. Moving and comfort (session: VR mod for Mass Effect Legendary Edition)
- [ ] ME2's vault (lane E, research/le2-traversal.md; built 2026-11-14, checked on the fake headset): load the VR Shepard career's
  Save 4 (Freedom's Progress), walk to the low wall of the spaceport (`CoverLink_10`, about 28 m from the start; or `mevr traversal
  place CoverLink_10 0 0.25`), gun drawn and held, stand square to it and double-tap A: one vault, no cover snap. Judge: is the
  standing distance the game asks for (within about 0.39 m of the slot, facing within 45 degrees) findable without the cue, and
  with `[Traversal] cue = 1`? Is the view's rise over the wall (about 0.5 m, smooth, level) comfortable as the game moves it
  (`view = game`, today's), against `view = level`, `mask = vignette` and `mask = blink` (each in `ASI\LE2MEVR.ini [Traversal]`,
  read live)? Where are your hands and the gun during the move and the second after it (the body's landing crouch)? Does a single
  A ever do anything near the wall? Seated and with the left hand on the gun too (both hands vault since 2026-11-16: does the
  zoomed view through the mantle feel right?). With the floor lock (since 2026-11-16 the
  eye rides the game's camera through the move and joins the floor-locked height once that camera has settled, about 0.8 s
  after the landing; research/le2-traversal.md "The floor lock through the vault"): no drop, bob or second rise at the landing?
- [ ] 2026-11-14 the idle sway (research/72; `[Hands] steadyIdle = 1` in ASI\LE1MEVR.ini, off by default until this
  check): stand still and look down at Shepard's feet for 30 s: they stay planted (without it they slide 2-3 cm, 16 cm in an
  idle fidget, while the hips stay under you); then walk off and stop: no jump or skating as it blends in and out. Compare
  with `steadyIdle = 0` (the mod reads the file live). While in the headset, the audit's untraced states (research/72 "To
  trace next": a gun drawn, firing, a cast, crouching, near people) can be traced from here with `mevr camera trace`.
- [ ] Camera shake (research/71, built 2026-11-12, not yet run in the game): firing a rifle and a shotgun, and the Mako's
  jets, with Comfort > Camera shake Off: does the world stay still? Low: a slight shake, not uncomfortable? Running and
  sprinting with Running sway off: no sideways sway or roll of the view? If the sprint still sways, research/71 section
  3.4 says what to check next.
- [ ] Crouch for real in combat: does the view drop below your own eyes (the floor seems to jump up)? Walk into walls:
  no cover snap. (research/23, research/38 B)
- [ ] VR cover: crouched behind a crate with enemies in front on Normal, do their hits stop hurting, and do they hurt again
  as soon as you raise your head over it to shoot? A flanker's hits always hurt. `mevr cover test` and the `vr cover:` log
  say what each shooter sees. Is 1.2 m (`[Cover] reachM`) right for tall cover you stand beside? Built 2026-11-05; the
  damage path measured on the fake headset with a scripted hit (no damage covered, full damage exposed); real enemy fire and
  the head raised over low cover not yet seen. (research/23 section 6)
- [ ] VR cover crouched (2026-11-19, the owner's yes; research/23 section 7.8, every game): behind a low wall (about 1 m, ME2's
  Freedom's Progress courtyard), crouch with L3 standing tall, then seated, then by your own natural crouch: do the hits in
  front stop hurting (the log's `vr cover: a hit ... | judged from Shepard's crouched head ...`), and hurt again when you
  stand (L3 or up)? Does it feel fair that Shepard's crouch, not your real eyes, decides?
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
- [ ] ME2 (lane D, research/le2-menus.md, checked on the fake headset 2026-11-14): our menu opens with `\` and with both
  sticks held, Ctrl + `\` opens Halcyon's own; the greyed-out rows read as "not yet"; the game stays paused while it's open;
  Game menu opens the Mission Computer; the guided calibration runs through and the guns follow it.
- [ ] ME2's omni-tool (lane D2, research/le2-menus.md "The rail on ME2", "ME2's omni-tool"; checked on the fake headset
  2026-11-15): the rail at the left wrist with ME1's art, its entries by fingertip, Close closing ME2's pause-menu pages; ME2's
  orange gauntlet on the forearm while the rail is open: does its 1.8 s restart (ME2's effect is a 2 s one) show as a pulse or
  a flicker? The tap is at the controllers until ME2 serves the HUD pieces (then on the drawn forearm).
- [ ] ME2, the both-sticks hold first (the owner, 2026-11-14: "always prioritize the L3 + R3 stick click"; research/le2-menus.md
  "Opening the menu"; unit-tested in `TestStickChordFirst`, replays never count as the hold): with live controllers, L3 + R3
  held 0.4 s opens and closes our menu in play, in cover, sprinting and in the Mission Computer, whichever click goes down
  first, and the game does nothing with either click meanwhile; L3 or R3 alone still does its game action (100 ms late); a
  quick tap of both takes a snapshot. Needs lane B3's ME1 pad merge on ME2 with `Options::gateRightClick` set.
- [ ] Use with A by looking: crates, lockers, doors, squadmates without turning your body; the label floats above the
  object, readable but not in the way; too narrow at the edge of your view? (research/40 section 4)
- [ ] The game's menus by pointer (inventory, pause menu, journal, map): the dot under the ray, the hover follows, the
  trigger clicks; the right stick scrolls the journal; on the map click, drag and zoom. (research/42 section 5)
- [ ] The wrist status: tune `wristOffset` / `wristWidthM` until it sits on your forearm; portraits readable at 8 cm.
  The gun's heat strip readable at arm's length; the sniper scope doesn't hide it. (research/41 section 7)
- [ ] A cutscene running into a conversation, menus over a flat screen: the panel is placed once in front of you.
  (research/15 "World-locked flat panels")
- [ ] The Tower of Hanoi puzzle (Mira's, likely Noveria's Peak 15), Quasar and the toxin cure: their instructions in view on
  the HUD panel; the Saren fight's timer still where it was. (research/53 section 5.1, the 2026-11-12 report)

## 6. Picture and performance (session: VR performance optimization)
- [ ] Wear the PS VR2 at Eden Prime's heavy spot, on a Presidium walk and in a fight with today's defaults; Claude
  reads fpsVR and the frame probe. (research/35 attempts 41-43)
- [ ] Motion blur: in the first minutes after a cold launch and after each level change, does one eye smear on its own?
  (research/35 attempt 49)
- [ ] `sharePass = 5` (experimental) in a heavy fight: a strobing eye or objects jumping? (research/37)
- [ ] FSR upscale on and off at the same render scale: sharper than bilinear, no ringing, HUD text clean. (research/32)
- [ ] Display > Performance (built 2026-11-06, session: Performance modes): try Quality, Balanced and Performance at Eden
  Prime's heavy spot and in a fight, restarting after each (the render size waits for one; the row says so). Which holds 90
  Hz in fpsVR; how soft Performance looks (0.75 of the eye with FSR); pop-in from its culling and shorter grass; Quality's
  sharpness. Then change one lever in Advanced > Performance: the row shows Custom and names what differs. (research/60)
- [ ] Quest 3 over Steam Link: the headset name in `[MEVR-COLOUR] headset:`, the Quest profile against neutral (hold A
  to compare), banding on the grey ramp, black level and white point. (research/27 sections 5, 8)
- [ ] One cutscene with `cineFpsCap = 0` against the capped 45 fps: which judders less? (research/16)
- [ ] ME2's dark scenes (built 2026-11-14, session: HDR desktops / ME2 picture; research/le2-picture.md): at Freedom's
  Progress at night and on the Normandy's crew deck, in the PS VR2 neutral against the game's `gamma 2.5` (console) and
  against `[Colour] profile = custom`, `gamma = 1.10` in `ASI\LE2MEVR.ini`; on the Quest, ME1's `quest` profile against
  contrast 1.06, saturation 1.12, mid-tones 1.0, black +0.03 (custom). Which keeps the shadows readable without looking
  grey; any bands in the dark sky or ceilings.

## 7. Claude checks these alone on the fake headset (no need to wear it)
Ask Claude to run them when the machine is free; each is a console command, a snapshot, a replay or a probe.
- Hand contact (research/73 section 3, 2026-11-18): beside a squadmate with a hand replay held into them, `mevr contact
  people` lists them with 12+ capsules a side, and a snapshot shows `mod.handContact.hands[*].touching` and the drawn wrist
  off its controller; on ME1, ME2 and ME3.
- ME2's vault with the gun held (lane C2): `tools/le2-halcyon-run/vault.ps1 -Runs 3 -Mode tap` again on main after dc7c6f98
  (every tick's wrists within 0.15 m; 0.08 m at most on 2026-11-15), and `-Support`: the two-handed hold is refused by the
  game (the support grip's LT zooms the gun, `CanPerformMantle` tests it).
- ME2 with Halcyon's renderer (research/le2-renderer-integration.md "The first run on the fake headset", 2026-11-14:
  done on the fake headset): once the deploy installs `MELE2VR.ini`, the same run in the headset (head tracking, first
  person without K, the eye size, the pad from the controllers).
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
- Voice (checked 2026-11-05 afternoon, research/54 "In the game"): the file feed through host, parser and dispatcher, the
  key terms, the acknowledgement's trace (a follow's bark is Shepard's own; a hold is answered by the squadmate; a move
  by our fallback line), the frame probe at the drop zone. Left: an attack's acknowledgement with an enemy present
  (Eden Prime's Save 7), the early dispatch of a settled partial (needs a long enough line), a probe at a heavy spot with
  the recognizer's thread priority lowered. The fine-tuned model's answers in the game: checked 2026-11-05 16:39 (`mevr
  voice say`, five refused lines, five sensible orders 0.7-0.85 s after the ask, research/54 "The fine-tune").
- Omni-tool screens: fingertip replays on Squad, Map, Journal, Codex and Computer; the squad member on the XMods screen
  and with a squad of three; Toggle Helmet where helmets come off (the Normandy) against the flat screen; the UI world
  capture's cost at a heavy spot. (research/57 sections 6-7)
- Spectator camera: head camera with `viewActor = 1`, the cost off / 1080p / 4K. (research/49)
- Citadel: a crowd spot with `sharePass = 0`. (research/50)
- Gun: the muzzle through a wall doesn't shoot through it; camera modes' hook-bone gap near 0. (research/14, 18)
- Wheels: the weapon and power wheels' crops in snapshots. (research/15)
- Sights: a snapshot per gun model under an ADS replay (window ring, scope picture, the sniper's lens glow); the near
  plane's log line. (research/21 sections 13-16) What moves in the tick the support grip's aimed fire switches (needs the
  game window focused, or the pad never reaches it). (research/21 section 17; the window itself checked 2026-11-05)
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

## 8. The sequels, gathered from the plans (2026-11-16)
Headset checks the ME2 and ME3 plans named that had no line here (ME3 had none at all). Gathered for
[docs/trilogy-plan.md](../docs/trilogy-plan.md) "Headset-only checks"; T-numbers are its features. ME2's earlier lines
stay where they are (sections 2, 4, 5, 6, 7 and "The game's start"). Each needs the feature seen working by script first.

### One settings file for the three games (lane SET, 2026-11-18; research/25 section 20)

Claude can check the first three alone on the fake headset (section 7's runs; back up `%LOCALAPPDATA%\MEVR` first, or use
`tools/fake-headset-session.ps1`, which puts settings.ini back).
- [ ] The first launch after the update (any game): `settings:` lines in its log say `LE1MEVR.ini is the base`, `LE2MEVR.ini
  taken`, each value ME2 keeps for itself, and `marked [Settings] shared = 1`; `%LOCALAPPDATA%\MEVR\settings.ini` opens with its
  header comment; ME1 plays as before (every setting the same: the stance, the sights, the comfort rows).
- [ ] A row changed in ME2's menu (the physical crouch, the owner's example) is the same row's value in ME1's and ME3's menus at
  their next start; a performance row (Display > Performance) stays each game's own.
- [ ] A key edited by hand in `ASI\LE1MEVR.ini` while the game runs reaches ME1 within a few seconds (`settings: [X] k = v in
  LE1MEVR.ini: ME1's own`) and not ME2.
- [ ] The owner: share the seven values ME2 kept (docs/trilogy-plan.md "Asked 2026-11-18, night"), or leave each to a menu change.

### The sequels' render size from the headset (lane AU4, 2026-11-18; research/le3-flares-and-window.md section 6)

- [ ] ME2 and ME3 after a first VR start on the PS VR2: the second launch renders ME3 at 2400x2448 and ME2 at 4352x2448 (16:9,
  its HUD isn't drawn narrower); both sharp and smooth, ME2's HUD, menus, wheel and room screens whole. (ME2's 16:9 against
  4:3 was settled on the fake headset by lane AU9: 16:9; research/le3-flares-and-window.md section 6.1.)

### Tonight's fixes and ME2's hacking games (2026-11-18, session "VR mod for Mass Effect Legendary Edition")

- [ ] Squad moves only by the B arc (b3b7f95a, every game): in a fight on ME1, ME2 and ME3, squeeze and release the left grip
  anywhere away from the gun while pointing at the ground: nobody moves. Then place the squad with the right hand's B arc
  and call them back with a double B: they go and come back. The log has no `squad orders: let go on the ground` lines any
  more.
- [ ] ME2's bypass and hacking minigames by the right fingertip on the left-wrist screen (the owner's choice, 2026-11-18; not
  fixed yet: this run diagnoses it). Freedom's Progress has a bypass panel at the Vorcha (`BioD_ProFre_200Vorcha`
  `SFXPointOfInterest_0`). Open it, then: hover the right index fingertip over a node without touching (does the node reveal
  its symbol, and does it reveal the node under the fingertip or one beside it?), press a node (does it select?), and try the
  right controller's ray and trigger the same way. Note how far off the highlight is from the fingertip, in which direction,
  and how late it follows. Take a snapshot (both sticks tapped) with the fingertip on a node. Do the same at a hacking
  (decryption) panel if one is near. What Claude reads afterwards: the log's `game pointer:` lines (the screen's size at
  opening read 0.42 x 0.00 m on 2026-11-18, then 1,174 mouse moves at the window's right edge and 0 clicks), whether any
  `gui cursor:` line appears (the game's own mouse route; none on 2026-11-18, only window messages), and the snapshot's
  `gamePointer` and `mod.guiCursor`.

### The sequels' eye distance, an A/B (lane AU9, 2026-11-18; research/38 "The sequels' eye distance, an A/B")

The owner: "Need to validate in headset, whatever gets us closest to feeling like we are attached to the body." Today's eye
sits 24-34 units (cm) ahead of Shepard's head bone on ME2 and ME3; ME1's about 4. One session, both games, no file to edit:
- [ ] On ME2 (any on-foot save) and then ME3, recenter standing straight, open our menu (both sticks held), Advanced >
  Tracking > Eye position (headset test), and try each of **Today**, **Between** and **At the face** (it applies within a
  second; close the menu after each pick). At each: look straight down at the chest and the belt (the body should be under you,
  not behind you), hold both hands out and bring them to the face (the drawn hands should meet the real ones without the arms
  looking short or bent back), walk forward and turn your head left and right (no body or shoulder in front of the eyes, no
  neck or collar in view), draw a gun and raise it to the eye. Note which feels most attached to the body, per game, and
  anything that pokes into the view (the collar, the hood, hair) at Between or At the face.
- [ ] Say which one becomes the default per game (today's stays until then). The log line `constant eye: the first-person eye
  ...` names the one in use; `arm IK: first person camera vs Shepard's head bone: forward N` the distance (fake headset: ME2
  29 today, 18 Between, 4 At the face; ME3 29-32, 17-20, about 3).

### Walking and crouching on ME2 (lane M2W, 2026-11-18; research/38 "ME2's late start", research/23 section 8)

- [ ] ME2, weapon away: push the stick the way Shepard faces, then the opposite way, a few times. Shepard runs at once both
  ways (before: about 0.3 s late after a turn), and stops at once when you let go.
- [ ] ME2: crouch for real with the weapon away, then walk a step: he stands to walk (ME2 has no crouched walk exploring) and
  crouches again when you stop while you stay down; with a weapon drawn he walks crouched. Stand up: he stands. Log
  `posture: the game stood Shepard up while you are crouched`.
- [ ] ME2 and ME3: tap the left stick click on foot: Shepard crouches, tap again: he stands; no area map and no objective
  radar. Both stick clicks held still open our menu.
- [ ] Reach toward a squadmate (ME2, then ME1; `[Hands] crouchMinDropM = 0.25` and `crouchLeanRatio = 1.0`, the defaults
  since 2026-11-18): no crouch, and the log's `posture: a lean, not a crouch`; a real crouch still crouches.
- [ ] Walk backwards with the weapon away (question 19): today the legs run sideways; say which option to build.

### The crouch and the run, the body under you (lane ANC, 2026-11-18; research/29 "The crouch that went third person", "A run never lowers the body")

- [ ] ME2 (then ME1): crouch for real behind a barrier until Shepard crouches, peek over it, stay a few seconds, stand up.
  What you should feel: the body stays under you the whole time, no click toward the barrier and back (the owner's "almost
  magnetic" pull), the hands on the controllers. The log has no `arm IK: body anchor: third person` through the crouch; an
  anchor line reads `... (N across: the view's height is your own head's ...)`.
- [ ] The same with the left stick click (the game's crouch while you stand): the body stays under you, your hands with it.
- [ ] Run forward and back a few times standing straight (weapon away, then drawn), and stop: the body never sinks or bends its
  knees (before: 12-18 cm mid-run). The log's `torso: crouching with you: the body down N cm` stays at 3 cm or less while
  running; a real squat still takes the body down with you.

### ME2
- [ ] 2026-11-18 squad weapons, the game's wheel only (research/19 section 13; built offline, not run in the game): on foot with
  Miranda and Jacob, pull the left trigger with your left hand open: ME2's own weapon wheel at your left hand (hold the trigger
  to keep it), never our cards-and-tiles menu; give a squadmate a weapon there and see it in their hands. The same with both
  hands on the gun. ME3 (ME3 section) and ME1 (the hand open: the wheel at once, pointing preselects) likewise.
- [ ] The game's start (T18, research/56 "The sequels", lane FE, seen on the fake headset 2026-11-18): as ME1's line in "The
  game's start": the logos flat, the "PRESS ANY KEY" title and the main menu on the 3D cinema screen at head height, fixed in
  the room with the floor and horizon around it, the scene with depth, the picture still while the head turns; no change of
  view at the key; the load flat from its first frame and no black 3D screen at its end, then the level in VR. The controller's
  ray on the menu moves the game's cursor and the trigger clicks (the front end's screen is the game pointer's now).
- [ ] Stereo (T06; research/sequel-eye-pose-tags.md): both eyes fuse while still and while turning, through menu and
  resume; no doubled or swimming scenery.
- [ ] Life-size stereo (T09; research/30 section 9, lane LS 2026-11-16): the world at its real size and depth (a squadmate
  at arm's length reads at arm's length, a doorway its height, the pistol in the hand not "huge"), far scenery relaxed at
  infinity, leaning forward 1:1, comfortable after ten minutes; each eye now renders its own frustum in a wider window:
  nothing pops in at the outer edge of either eye (a PS VR2's and a Quest's eyes are asymmetric, the fake headset's not),
  and the view reaches each eye's full width.
- [ ] The second eye's speed-up and dynamic shadows on ME2 and ME3 (T08, T11; the owner's yes 2026-11-17, lane DEF:
  `umbraShareResults = 1` on both, `sfrReplayRetry = 0` on ME2, `DynamicShadows` on; research/sequel-render-levers.md "The
  batch before the headset test"): squadmates' shadows under their feet in both eyes, fusing; look past a door frame's or a
  pillar's edge and close each eye in turn: nothing missing in the right eye for a frame as the head moves (the first eye's
  occlusion stands in for the second's); the frame rate holds while a power or weapon wheel is up (the paused world).
- [ ] Floor lock and height (T21; research/le2-renderer-integration.md "What is left of M2"): the eye height standing,
  seated and physically crouched; the calibration's manual height once it is wired.
- [ ] Guided calibration (T27): the whole run with each controller type; the fake headset has no interaction profile, so
  only a headset can show it.
- [ ] One calibration for the trilogy (T27, research/25 section 17, since 2026-11-18): the first launch of any game after the
  update says `calibration: [Calibration.<key>] taken from LE1MEVR.ini` once per controller type (LE1MEVR.log or
  LE2MEVR.log), then ME2 (or ME3) with the same controllers shows "Calibrated <ME1's date>" in Body & calibration, no
  first-run prompt, and the guns sit in the hand as in ME1. A calibration saved in ME2 is ME1's next launch's too. (On the
  fake headset 2026-11-18 with `[Debug] calibrationKey`: ME1 same values before and after, ME2 the same values, no prompt.)
- [ ] Wrist status and the gun's panel (T41; research/41 "ME2's ammo display"): readable on small and large guns, after a
  weapon swap and after an ammo power.
- [ ] Sights (T24; research/le2-vrik.md "The sights on the sequels, mod side"; seen on ME2's fake headset 2026-11-16, LE1 at
  the same pose unchanged): the pistol's dot on the gun's own sight (on the fake headset its ring stood a hand's width left
  of the gun: the eyes take the monitor's shape there), the sniper rifle's eyepiece picture and its pull to the eye, shots
  landing where the dot or the reticle sits. ME3 the same once it has a combat fixture.
- [ ] Comfort and sprint (T30, T32; docs/le2-m6-native-checks.md): snap turns, walking toward the hand, the vignette;
  the sprint as ME1's faster run on all three games since 2026-11-16 (lane GR, research/trilogy-sprint.md; on ME1 it never
  ran before, the game's storm did): A held with the stick forward runs faster with the gun up, no storm posture or camera,
  its speed comfortable, the release back to the run; the same with a weapon drawn.
- [ ] The SMG's body slot (T25; research/le2-vrik.md "A body slot for the SMG", lane WP 2026-11-16, a recommendation):
  with a class that carries an SMG (not the Soldier), reach down in front of the right thigh and squeeze: the SMG comes out;
  let go there and it goes back. Watch for: a hand hanging at your side or on its way to the pistol buzzing in it, a reach
  that feels too low or too far forward. `[Holsters] thighRight` (right, up, forward, radius) moves it.
- [ ] The SMG at the small of the back (T25; research/le2-vrik.md "The SMG at the small of the back", lane SG 2026-11-17, the
  owner's ask; then the owner picks the default): in `ASI\LE2MEVR.ini` (or LE3MEVR.ini) set `[Holsters] smgSlot = lowerBack`,
  with a class that carries an SMG reach behind your back with the right hand, to the small of the back, and squeeze: the SMG
  comes out; let go there and it goes back. Watch for: the draw failing or coming late when the headset loses the controller
  behind you (the log's `holsters: the right controller lost tracking at lowerBack` and `the grab at lowerBack came while the
  controller was untracked`; `[Holsters] lostHandHoldS`, 0.75 s, is how long its last place counts), the controller coming back
  far from where it was lost, the pistol's right-hip buzz on the way round, a reach too high, too low or too far back
  (`[Holsters] lowerBack`); seated, the place beside the chair's back (`seatedLowerBack`). Then thighRight against lowerBack:
  which should be the default.
- [ ] The right stick click and the holsters' refusals (T25; research/le2-vrik.md "The sequels' controls after the review of
  57ae5b4e", lane FX2 2026-11-17; seen on the fake headset): hold the right stick click: the power wheel opens after 0.3 s and
  closes when you let go; a quick tap casts nothing (it used to cast the second mapped power). Since 2026-11-17 (lane CTL,
  the owner's answer to docs/trilogy-plan.md question 14) the click no longer reaches the game on foot: neither the tap nor
  the hold may bring up ME2's area map or objective radar, or ME3's point of interest or area map; the omni-tool's Map entry
  opens the map. On the Normandy or in a hub, a grab at the hip gives the short refusal buzz and no gun (in a fight a let-go
  now holsters: the next line). ME3 the same once it has
  a combat fixture, and there with a heavy weapon in hand (question 13): a grab at any slot gives the refusal buzz and the
  heavy weapon stays in the hand (never thrown away); the game's weapon wheel still switches away from it.
- [ ] ME2's firefights (lane CV2, 2026-11-18; built, not run: the owner was in the headset): (1) in a fight, let go of the gun
  anywhere: it goes back (`holsters: ... goes back in its holster (in combat, where SFXGameModeDefault.TryHolster refuses: its
  own last step ...)`), also mid-reload once the reload ends; does the game draw it again by itself while the fight goes on? (2)
  With the gun holstered, open and close the hand power wheel (X) and the squad's weapon wheel without a pick, and walk into a
  new fight: the gun stays away (`only holsters draw: put back at once`; a flash of it in the hand for a frame or two is the
  known cost), but a power cast from the wheel still casts. (3) Crouch behind a waist-high barrier with drones or mechs
  shooting: do the hits stop (`vr cover: a hit from ... covered ... (by the level look)`), and with your eyes over the top do
  they land? Each judged hit's detail line says the eyes' height and what each look met: send the log either way. (4) Was the
  "magnetic" pull felt while crouching behind cover or while walking up to it standing? `posture: cover watch:` lines, if any,
  name what moved Shepard (research/23 section 7.6).
- [ ] ME2's mandatory vault (lane CV2, 2026-11-18; research/le2-traversal.md "The vault Freedom's Progress requires"; built, not
  run): at Freedom's Progress, the barrier before the warehouse (`CoverLink_5`, where the squad went over), walk up to it, look
  at it and double-tap A, with the gun drawn and again holstered: one vault each time (`traversal: next to CoverLink_5 ... the
  game admits it (...; asked with the head's facing ...; the weapon away: asked as drawn)`). Is it the spot that stopped the
  mission ("at the very beginning ... I'm never able to get into position")?
- [ ] ME1's refused draw (lane CTL, 2026-11-17, the owner's answer to docs/trilogy-plan.md question 15; seen on the fake
  headset): on the Normandy (or anywhere ME1 allows no combat) a grab at the hip gives the short refusal buzz, not the draw's
  click, and nothing comes out; elsewhere the hip grab draws as always.
- The reload by hand (T26): off by default on ME2 and ME3 since 2026-11-16 (the owner: a hand at the hip makes no sense);
  nothing to test until its gesture is redesigned with the owner. The game's own reload button and its automatic reload
  work as always.
- [ ] ME2's faster shield recharge behind cover (T31, research/23 section 7.5): crouched behind a crate with every enemy
  hidden, the shields come back sooner than standing in the open; a flanking enemy in view takes it away.
- [ ] Powers (T44; research/55 "Mass Effect 2"): the push at a hostile, the chest and gun gestures' targets, the coach card.
- [ ] Squad (T45, T46): the weapon menu at the left hand, the placement ring and arc, a hold and a call back.
- [ ] Melee by gesture and B (T34; research/20 "The trilogy's one path", lane MEL 2026-11-17; seen on the fake headset): ME1's
  rules on ME2. With the gun out, a fast stroke of either hand near an enemy (within 2 m) is the game's melee (the pistol whip) and
  hurts it; the right hand with its finger on the trigger fires instead. With the weapon away there is no melee on ME2 (the game's
  melee is the held weapon's, and a holstered Shepard holds none): a fist does nothing there. B never melees on foot any more: holding B
  is squad placement's arc and a double B calls the squad back, and neither swings. Flailing gives at most one strike a second or
  so; aiming, firing and reaching for a holster never strike by themselves. Say whether 2 m and the punch's speed feel right.
- [ ] Voice and Thespian (T47, T48): a real microphone at the ear; the reply panel's place and legibility on the cinema
  screen.
- [ ] Leave on our menu's top bar (T37, question 16; research/28 "Leave", lane LV 2026-11-17; seen on the fake headset at
  `BioP_N7Mine`): finish an assignment, choose to stay on the planet, then open our menu on foot: Leave is there next to Game menu
  and Holster and asks "return to the Normandy?" as B did. Elsewhere (a hub, the Normandy, in combat, in a conversation) it is
  not on the bar. Known: during an unfinished assignment it shows but the game's script answers nothing (plot state 4849).
- [ ] Galaxy projector (T42; docs/galaxy-projector-handoff.md "2026-11-17: ME2's", lane GX2): at the CIC's console press A:
  the map opens as a disc lying on the projector (the CIC around it in stereo, no flat screen), the system's name and the
  Exit prompt on the same disc; look around and lean; the map's depth, legibility, comfort, GPU cost (two captures). The map's
  own controls (the stick, A, Y probes, LB/RB tabs), X back to the room. Not yet: travel, orbit and the scan through it.
- [ ] A streamed Quest (T10): full rate after a load, with no runtime setting changed.
- [ ] The shouldered gun (lane AU8, option B; research/le2-vrik.md "The shouldered gun on the sequels"), ME2 and ME3: both
  hands on a rifle at the shoulder: the view never zooms, the shots group tighter than from the hip (the heartbeat's
  `zoomed shots:` aim error), a melee stroke and (ME3) a roll still work while shouldered; on ME3 a shouldered sniper shot
  hits twice as hard as before.

### ME3
- [ ] 2026-11-18 squad weapons, the game's wheel only (research/19 section 13): the left trigger with the left hand open and with
  both hands on the gun opens ME3's own weapon wheel at the hand (Halcyon's `[WHEELOWNER] LB edge: qualified=1`), never our menu.
- [ ] The game's start (T18, research/56 "The sequels", lane FE, seen on the fake headset 2026-11-18): the title (the Earth from
  orbit) and the main menu on the 3D cinema screen in stereo, at head height and fixed in the room, the start's first second
  flat; the load flat, then the level in VR. Not reached by script: the main menu after "PRESS ANY KEY" (a replayed A didn't pass
  it) and the ray on it.
- [ ] Stereo (T06; research/sequel-eye-pose-tags.md, research/le3-renderer-preview-evidence.md): both eyes fuse while
  still and turning, through menu, recenter and a reload.
- [ ] Life-size stereo (T09; research/30 section 9): as ME2's line; upstream ME3 squeezed everything into the first metre,
  so the scale change is large: check depth and size, and that the HUD panel and sights still sit where they should.
  **Lamps' lens flares and sharpness** (research/le3-flares-and-window.md; the owner's answer to question 17, 2026-11-17,
  lane DEF2: ME3 ships mode 2, each eye's own frustum, `MELE3VR.ini [VR] MetricEyeFrustum = 1`, with the flare fix,
  `ASI\LE3MEVR.ini [Stereo] flareWindow = 1`, at 3072x2216; seen on the fake headset with your PS VR2's eyes replayed): look
  at a ceiling lamp on the Normandy with each eye closed in turn: its flare on the lamp in both eyes, fusing; nothing missing
  at either eye's outer edge; the room as sharp across and down as ME2 (each eye 19.2 x 14.6 px per degree, mode 1 had 14.6
  x 7.3). If a flare sits off its lamp, `[Stereo] flareWindow = 0` shows the engine's own placement (off, differently per
  eye) and `MetricEyeFrustum = 0` the old mode 1. Still open (question 17 (b)): `[Stereo] windowEyes = 1`, the HUD panel's
  elements on their world points in a scene with a HUD, before it becomes the default.
- [ ] Changes of view on ME2 and ME3 (research/30 9.4, lane FXR): leaving the 3D cinema screen, a load's end, a menu and
  back: no single frame with the eyes swapped or the world jumping in depth.
- [ ] First person from the start (T01's R32; research/trilogy-first-person.md, lane FP3 2026-11-17; seen on the fake
  headset): ME3 now opens in first person with Shepard's head hidden, like ME1 and ME2 (it used to start in third person
  until K). Check: no head or hair in view, no shoulders in front of the eyes once the controllers are tracked, the eye at
  your height walking up to a wall or a railing and backing into one (the view must not jump back toward the body), the
  stick not tilting the view up or down, conversations still on the cinema screen.
- [ ] No cover snapping (T31; research/le3-combat-fixtures.md "First on the combat save"): on a mission next to cover, A
  doesn't snap Shepard into it; crouching behind it, hits from beyond it don't hurt, standing in the open they do. The
  game's "take cover" prompt may still show (not hidden on ME3 yet): report it if it does.
- [ ] The game's crosshair hidden (T14; research/le3-hud-panel.md "The crosshair hidden", lane CH 2026-11-17; unseen: the
  Normandy has no weapon): on a mission with a gun drawn, no crosshair in the middle of the HUD panel, only our ring where the
  hand aims, as on ME1 and ME2; no crosshair flashing at a hitch; the game's own sniper scope still shows when zoomed (if the
  game's zoom can be reached); the crosshair back on the flat game when VR stops.
- [ ] Hands and the gun (T21, T22; research/le3-body-pose.md): the raw controller against Shepard's hand (place, twist,
  reach), the gun in the grip, the two-handed support, standing and seated.
- [ ] Sights (T24): the eye box of the dot and the scope per weapon and optic mod.
- [ ] Holsters, and putting a gun away (T25; research/le2-vrik.md "ME3's holsters, from its own script", lane WP
  2026-11-16; unverified: no ME3 save here loads on a mission with a weapon): on a mission a grab at a slot switches to that
  weapon (the pistol at the right hip, the rifle at the right shoulder, the shotgun at the left hip, the sniper rifle at the
  left shoulder, the SMG at the front of the right thigh). By default a gun you let go stays in your hand (ME3 has no
  holstered state). To try ME1's behaviour, set `le3PutAway = 1` under `[Holsters]` in `ASI\LE3MEVR.ini` (the mod reads it
  within a second): letting go at its slot, or anywhere for 0.2 s, puts the gun away with the game's own holster animation.
  Watch for: the gun really gone from the hand and the HUD's weapon panel, the trigger doing nothing while it's away (four
  pulls: the log says `holsters: the game wanted to draw your weapon (...FireWeapon): refused` each time), a grab at a slot
  drawing it again, no stuck state (powers, melee, cover, a reload, a conversation and a cutscene with the gun away), and
  no error dialog or crash. Report what you see; it becomes ME3's default only once it works.
- [ ] Menus (T37-T39; research/le3-native-gui.md): our menu, calibration, the rail and wrist projection, the game's
  screens and the pointer.
- [ ] The game's screens on the wrist and in the room (T39; research/le3-native-gui.md "The game's screens on ME3", lane OM2
  2026-11-16): the Mission Computer and the map projected from the left wrist with the rail beside them, the War Terminal's
  War Assets standing toward the console, the Normandy visible behind them (not the dark nebula), the squad record still on
  the flat panel. Since lane OM3 (2026-11-17; research/le3-native-gui.md "The movie's mouse and the view behind a
  screen"): a ray and trigger or a fingertip press on a button of the Mission Computer opens its page, War Assets' entries
  take clicks, and with a screen up your view stays where you stood (your arm on the wrist screens; at the War Terminal
  Shepard's body is hidden while War Assets stands, or his arm on the console would cover it). Check: clicks land where the
  dot is, after pressing B too (the game switches between its pad and mouse modes); nothing of Shepard in front of War
  Assets; no jump of the view as a screen opens or closes. Report legibility, reach, and whether the screen's place feels
  right.
- [ ] Close on the rail (T38; research/le3-native-gui.md "Close from the area map", lane OMX 2026-11-17; seen on the fake
  headset): open the map from the wrist (the rail's Map), then press the rail's Close: the map folds back into the Mission
  Computer for a quarter of a second, then that closes too and you are back on foot. The same from the squad record, the
  journal, the Mission Computer itself, and after opening the Mission Computer with the left controller's menu button (where
  it reaches the game). Watch for
  a screen left up, a flash of the Mission Computer in the wrong place, or the rail staying open after everything closed.
- [ ] Cinema (T15; research/le3-dialogue-cinema.md): the screen fixed in the room, the reply wheel and subtitles readable,
  the interrupt prompt and its triggers.
- [ ] Melee and traversal (T34, T33; research/le3-melee-audit.md, research/le3-traversal-adapter.md): no accidental
  strike while handling the gun; the horizon and heading through a ladder, a vault and a roll. ME3's stroke melee is on by
  default since 2026-11-17 (lane DEF2, as on ME1 and ME2; `[Throwables] melee = 0` turns it off), B is squad placement's
  (lane MEL), and the heavy "chamber and strike" stays opt-in until question 7 (`mevr melee heavy-gestures on`). In a fight:
  a fast stroke of either hand at an enemy within reach melees with the gun out, a stroke while aiming or reloading doesn't;
  the checks of research/le3-combat-fixtures.md "Melee on the first combat save", then the feel.
- [ ] Leave (T37, question 16; research/28 "Leave"; not seen in the game: no save reaches its levels): in the Citadel DLC (a
  party conversation, the dance floor, the casino race, the combat simulator's summary) Leave on our menu's top bar does what
  B's tap did; nowhere in the base campaign.
- [ ] Galaxy projector (T42; docs/galaxy-projector-handoff.md "2026-11-17", lane GX): at the CIC's console press A: the map
  opens as a disc lying on the projector (the CIC around it in stereo, no flat screen), the system's names and the Exit/Scan
  prompts on the same disc; look around and lean; the map's depth (the planets above or below the disc), the labels on their
  objects in both eyes, legibility at the console, comfort, GPU cost (two captures). The map's own controls on the
  projector (the left stick flies the Normandy, LT scans, A at a relay), X back to the room. Not yet: travel, orbit and the
  scan through it.
- [ ] The omni-blade and the roll (lane AU8; research/le3-combat-fixtures.md "The omni-blade with the gun put away"): put the gun
  away (let go of it), make a fist with the right hand (grip and trigger) and strike at a trooper: the class's heavy melee
  (the Soldier's omni-blade punch, a Vanguard's or Adept's biotic one) hits it; an open hand doesn't. Known: the game's melee
  camera is third person for that second. A roll (forward and A): Shepard's body and the gun in hand vanish from view for
  the roll and come back after it; say whether that, or the body seen rolling, is better.
- [ ] Performance (T07, T10; docs/me3-overall-goal-handoff.md section 4): frame pacing in a real headset; the desktop
  mirror's cadence and the null driver's rate say nothing about it.
