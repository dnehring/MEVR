<!-- SPDX-License-Identifier: MIT -->
# What still needs testing in the headset

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
  must stay as before. With no standing reference yet, holster and let Shepard stand for about a second to settle it.
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
- [ ] A conversation and a cutscene on the 3D screen at the new defaults (2.9 m wide at 3.5 m, depth 1.0%, 16:9, the
  headset's full rate; research/52 "In the headset"): no cross-eyed strain (also at a conversation's first second), subtitles
  inside the 16:9 frame (if cut: `[Cinematics] aspect = 0`, live), camera pans smooth at 90 (if they stutter: `fullRate = 0`,
  live), the dialogue wheel readable, the pointer picks a line. The 1.5% / 3 m at 2.6 m first default was "a little
  cross-eyed" (2026-11-06). The screen stays put when you lean or look away mid-scene. The floor and horizon around it give a
  sense of place without competing with it (too bright or too dark: `[Cinematics] backdropBrightness`, live; off: `backdrop = 0`).
  Entering and leaving a cutscene or conversation: a short dip from black instead of a hard cut, smooth, not too long or too
  short (`[Cinematics] fadeS`, 0.35 s, live; 0 off).
- [ ] **Walking to marks prototype** (built 2026-11-06, session: codex/convo-walk; First person itself QA-approved 2026-11-07,
  research/59): with conversations in the world and
  `worldStay = 1`, try `worldWalk = 1` (AI/slide fallback) and `2` (slide). Lasting marks should wait 2.5 s then walk; bouncing
  marks and observed returns should cancel; Shepard stays kept. Check locomotion/gestures, turns, collision/timeout, no drawn
  jumps, and no stale velocity or AI move on disable/end. Native returns exactly at the already-kept place cannot be detected
  by position sampling alone. Still off by default; NOT yet checked in the game. (research/59 "Walking to marks (prototype)")
### The omni-tool menu on the wrist

- [ ] 2026-11-08 right rail and power-up (research/57 section 9): tap the wrist, then Equipment; the rail slides right over 0.28 s
  and the right index reaches it without crossing the screen. Touch/ray during the slide must select only the drawn rail; no
  accidental screen click or wrist close. Switch all tabs (no new slide), close, reopen via the game, and compare `railSide = left`.
  With `mevr omnitool on`, judge the 0.60 s dim-to-lit start, subtle first 0.15 s flicker, both gauntlet/display and particles,
  matching fan/rail light, and the unchanged close. Confirm `omni tool: power-up through ...` names a resolved target; a
  `nothing found: full at once` line is fallback, not successful brightness QA. Repeat with the power wheel, seated and standing.
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

### The game's start (session: release blockers)
- [ ] Launch the game with the headset on: the boot and logos on the flat panel, then the title, the camera flight after a
  key and the main menu on the 3D cinema screen, fixed in the room like a monitor: turn and move your head and the picture
  inside stays still (no panning with the head), the planet scene with depth behind the screen. The load's movie on the flat
  panel, no flash of the world after the key, then the level in VR. (research/56 "The front end on the 3D screen",
  checked on the fake headset 2026-11-05)

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
- [ ] Crouch for real in combat: does the view drop below your own eyes (the floor seems to jump up)? Walk into walls:
  no cover snap. (research/23, research/38 B)
- [ ] VR cover: crouched behind a crate with enemies in front on Normal, do their hits stop hurting, and do they hurt again
  as soon as you raise your head over it to shoot? A flanker's hits always hurt. `mevr cover test` and the `vr cover:` log
  say what each shooter sees. Is 1.2 m (`[Cover] reachM`) right for tall cover you stand beside? Built 2026-11-05; the
  damage path measured on the fake headset with a scripted hit (no damage covered, full damage exposed); real enemy fire and
  the head raised over low cover not yet seen. (research/23 section 6)
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
- [ ] Display > Performance (built 2026-11-06, session: Performance modes): try Quality, Balanced and Performance at Eden
  Prime's heavy spot and in a fight, restarting after each (the render size waits for one; the row says so). Which holds 90
  Hz in fpsVR; how soft Performance looks (0.75 of the eye with FSR); pop-in from its culling and shorter grass; Quality's
  sharpness. Then change one lever in Advanced > Performance: the row shows Custom and names what differs. (research/60)
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
