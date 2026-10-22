# Next test session

LE1 now renders VR with Halcyon's renderer (since 2026-10-08), with our motion controllers, hand
aim and arm IK on top. Logs, both read by Claude after each step (just note what you saw and
roughly when):
- ours: `<ME1>\Binaries\Win64\Logs\LE1MEVR.log`;
- Halcyon's: `%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt` (settings: `MELEVR.ini` in `Binaries\Win64\ASI`).

Already done for you: `tools/vr-game-settings.ps1` turned VSync, motion blur, film grain and
dynamic shadows off and HDR to SDR in ME1's GamerSettings.ini, and set what Halcyon's installer
sets: a borderless window at its near-square stereo target, 3072x3188 (backup kept; `-Restore`
undoes it). `MELEVR.ini` is Halcyon's release default again: its tuned eye separation (3.4), eye
order, VR conversations and cutscenes; head aim is off (the hand aims). The old file
is `MELEVR.ini.mevr_old`. Halcyon's log should now show `[TARGETCHK] Stereo 2 target OK` and, in
gameplay, `[PASSCAP] published complete stereo pair` lines (none of those = no stereo).

**Keys on LE1:** `\` = Halcyon's menu (Insert upstream), or click and hold both thumbsticks to
open or close it. While it's open the game gets no input. Point at it with a controller (laser
pointer: trigger clicks, right stick scrolls), or navigate it as a gamepad (left stick or d-pad
moves, A activates, B backs out). R = recenter
(or double-click the right stick), K = first person on/off (off at every launch). Ours: `,` hand
aim on/off and `.` arm IK on/off, both on by themselves once the controllers are tracked; the
other keys in the table further down are for our own renderer only.

## Test loop: let Claude see what you see (new)

The host can now save what each eye shows (PNG snapshots) and record your head and controllers,
so Claude can look for itself and replay your movements later without you in the headset.
Redeploy the host first (close the game, `tools/deploy-asi.ps1 -Game LE1`). Files go to
`%LOCALAPPDATA%\MEVR\snapshots` and `\recordings`.

- **Snapshot:** press **End** (game window focused), or squeeze **both grips and click the left
  stick**, or in Halcyon's menu (`\`) open **VR hands -> Test loop -> Snapshot in 3 s** and close
  the menu. Log: `test loop: snapshot saved in ...`. Claude can also take them any time while
  you play (`tools/snapshot.ps1`): just keep still and say "go" when you're in position.
- **Recording a short session for Claude** (about 20-60 s is plenty):
  1. In game, on foot, with a weapon drawn. Recenter first (R).
  2. Press **Home** (or both grips + right stick click) to start. Log: `test loop: recording the
     head and controllers to ...\<stamp>.mevrrec`.
  3. Do the thing you want Claude to check, slowly and deliberately, and say out loud (or note)
     roughly when: e.g. "aim at the crate, 5 s; both hands on the rifle, 5 s; look left, arms out".
     Hold each pose for a couple of seconds.
  4. Press **Home** again to stop (log: `recording stopped ... N frames, X s`). Tell Claude the
     file name and your notes.
- Claude then replays it (`tools/replay.ps1`), taking snapshots at the interesting moments. The
  replay moves the view and the controllers as you did (not the buttons, unless asked), so you can
  take the headset off. **But keep the headset awake**: the game only runs VR frames while the
  VR session runs, and a sleeping headset stops it.
  - Quest + Steam Link / SteamVR: the headset sleeps a few seconds after you take it off. Cover
    the proximity sensor (between the lenses) with a bit of tape or cloth, or in the Meta Quest
    Developer Hub turn the proximity sensor off [verify which works with Steam Link].
  - Leave the controllers on a table, untouched: the replay replaces their poses, but their
    buttons and sticks still reach the game unless the replay replays buttons too.
  - If `%LOCALAPPDATA%\MEVR\testloop.status` says `"framesRunning": false`, the session has
    stopped; put the headset on briefly to wake it.

## LE1 in the headset

1. Start SteamVR, then the game, and load a save on foot.
   - Does the headset show the game, in stereo (depth)? If there's **no VR at all**, say so
     first: Halcyon's code has to catch the game's swap chain at startup.
   - Is head motion smooth now, in both eyes?
2. **Controllers:** move, turn, fire, A/B/X/Y, grips, left menu = pause. Does the game react
   without clicking the desktop window first? Open the mod menu (`\`): can the controllers move
   through it?
3. **First person:** press K. How does it look (Shepard's head hidden, camera at the eyes)?
   Recenter with R if the view is offset.
4. **Controller aim:** on foot, draw a weapon.
   - Look around: the view follows the head, but Shepard's aim no longer swings with it.
   - Point the right controller at a target away from where you're looking and fire. Do shots go
     where the controller points? Note any constant offset and roughly how far the target was.
     The game's crosshair should be gone; our ring reticle (step 6) shows the aim instead.
   - Head aim is off (`combatHeadAim = 0`): with the controllers asleep, the right stick aims.
   - The Mako's cannon should still follow the head.
5. **Arms:** with the controllers tracked, wave your hands, in first person and in third person
   (K). Do Shepard's arms follow? Any tearing at the wrist or elbow, or fingers off the hand?
   Does the weapon follow the hand? `.` turns it off to compare. (The left hand may stay on the
   gun's grip while the weapon is out.)
   - **Reach and shoulders (new).** First person (K), on foot:
     - Log first: `arm IK: body anchor: first person (the view's frame), view N units from
       Shepard's animated head bone, 50.0 units per meter`. Tell Claude N, in first and third
       person (it picks first person below 45 units).
     - Hold a controller in front of your face, then stretch both arms fully out, forward, to the
       sides and up. **Do Shepard's hands sit on the controllers** (where you feel them), at full
       stretch too? His arms may look bent at your full reach (he's drawn about twice your size at
       Halcyon's scale); say whether that bothers you.
     - Lean forward, sideways and turn your head and body: **the shoulders should stay under you**
       (the spine bends and twists; the torso follows the head only past about 35 degrees, and
       turns toward your hands). Look down: does the body look right, or poke into the view?
     - Log: `arm IK: first write, torso (component space): shoulders (...) -> (...) (wanted (...),
       N short, limited), twist, bend, clavicles, arm stretch ...`, and the same in the heartbeat
       (`arm IK torso`). "N short" is how far the limits held the shoulders back.
     - Raise a hand high: the shoulder should shrug up a little. Reach far: the arm may stretch up
       to 1.25x before the hand stops.
     - A/B: `bodyFollowsHead = 0` in `ASI\LE1MEVR.ini` `[Hands]` turns the torso off (live);
       `armStretch = 1` turns the stretch off; `trackingScale = 100` brings back the old hand
       scale (hands twice as far out). Leave them at 1 / 1.25 / 0 afterwards.
     - Draw the pistol and hold still: the log's `hand aim: first aim ray (tracking space): muzzle
       ...` should now be well under a metre from the head and about the gun's length (0.5-0.8 m)
       from the right grip, not 2 m.
6. **The gun points where the controller points (new):** first person (K), on foot, draw a
   pistol or rifle, and hold the right hand still for a moment (the mod measures how the gun sits
   in the hand; the log says `grip measured`).
   - Hold the controller normally and point it at something: the gun's barrel should point the
     same way, **upright (not upside down or on its side, pistol and rifle alike: the gun's top
     now comes from the hand's thumb side, not the weapon model)**. Roll the controller: the gun
     rolls with the wrist. (The `handPitch = 90` in `LE1MEVR.ini` no longer matters for guns;
     leave it.) Turn and tilt the controller: the gun should follow without lag or jitter.
     - Log: `arm IK: gun held thumb side up, ... the weapon model's up is N degrees from it
       (ignored)`, once per weapon (about 180 for the pistol, about 90 for the rifle).
     - Still rolled? Set `gunRoll` under `[Hands]` in `LE1MEVR.ini` (degrees, positive rolls the
       top to the right; applies within a second, logged as `hands: calibration ... gun roll`)
       and note the value that looks right.
   - Fire at a wall at different distances: impacts should land where the barrel points, and the
     tracers should leave the muzzle. Note any constant offset and the distance.
   - Wave the gun around fast: does the gun stay locked to the hand (no lag behind it)?
   - Holster and draw again, switch weapons: same behaviour each time? (Each new weapon is
     measured again: hold still a moment after drawing.)
   - Does the reticle (step 7) sit on the barrel's line?
   - `,` (hand aim off) leaves aim to the right stick; `.` (arm IK off) returns shots to the
     controller's ray without the gun following.
7. **HUD panel, reticle and the mod menu (new).** Do this in Stereo 2, on foot.
   - **Mod menu.** Press `\` (Halcyon's Insert menu; `\` in our deploy), once at the title screen
     and once in gameplay. Press `\` again or hold both sticks to close it.
     - Is the menu visible now? It's an opaque dark panel, a little nearer than before.
     - If it's still invisible, say so. The log's `[MENUVIS]` lines will show why.
   - **Mod menu with the controllers (fixed and new).** The pad never worked in this menu, not
     even an Xbox pad: the ImGui setup threw every gamepad key away. Now:
     - **Gamepad:** left stick or d-pad moves the highlight, A activates, B backs out. Check a
       checkbox, a slider (highlight it, then hold left/right) and a tab (move up to the tab row,
       then left/right and A).
     - **Laser pointer:** point the right controller at the menu. A blue dot should sit where it
       points (the menu now stays where it opened, step 10). Pull the
       trigger to click (the dot turns orange): click a tab, a checkbox, drag a slider. Push the
       right stick up/down over a long tab to scroll. Point with the left hand instead: it takes
       over once it is on the menu (or pulls its trigger there).
     - Does the dot land exactly where you point, or is it offset (which way, how much)? Is it
       steady enough to hit small checkboxes?
     - Move the desktop mouse: it should take the cursor back; pointing again takes it over.
   - **VR hands tab (new, last tab of the menu).** Pitch, yaw and roll for how Shepard's hands sit
     on the controllers, the same values as `[Hands]` in `ASI\LE1MEVR.ini`.
     - Press +10 on Pitch with the menu open: the file changes, and within a second Shepard's
       hands should turn (visible behind the menu, or after closing it). Put it back afterwards
       (the gun is aimed separately and doesn't use these).
     - **Measure arm length:** press it, then hold both arms straight out to the sides (a T)
       and pull either trigger. It should say `Sample 1 taken: N m between the controllers`
       (roughly your arm span minus a little), and `ASI\LE1MEVR.ini` gets an `[Arms]` section.
       Within a second the mod logs `hands: arm length 0.6x m (left ..., right ...)` and writes
       `armLengthM`, which the tab then shows. From then on Shepard's full reach matches yours
       (his hands go out farther than the controllers when your arm is straight). Compare with and
       without: delete the `armLengthM` line (or set it to 0) to turn it off. A bad pose logs
       `isn't a T pose` and changes nothing.
   - **HUD.** The health, squad, radar and subtitles should sit on a panel about 1.5 m ahead,
     a bit below eye level, not stuck to your face.
     - Look around: the panel stays put, and the world shows no HUD in either eye.
     - Turn your body with the stick: the panel stays in front of you.
     - Keep your head turned more than about 70° for a second: the panel swings round (it was 50°
       until 2026-10-21; the deploy turns a saved `hudPanelFollowDeg = 50.0` into 70). Below 70°
       it never moves.
     - R recenters it.
     - Is the text readable? Is the panel too big, small, near or low? The mod menu's HUD tab
       has sliders for these.
     - Untick **HUD on a panel in front of you** to compare with the old HUD. A/B both, then
       leave it on.
   - **Reticle.** Draw a weapon and point the right controller.
     - A small ring should float along the controller's aim, about 10 m out, with the game's
       crosshair gone.
     - Do shots land on the ring? The ring follows the controller until the mod's weapon aim ray
       arrives.
     - Note the size you'd like. The HUD tab has distance and size sliders, and the reticle
       checkbox for A/B.
   - **Other screens.** Open the pause menu, the galaxy map and a conversation. They should show
     on the flat screen, which now stays put in the room (step 10).
   - **Fades.** Watch for anything full-screen ending up on the panel instead of the world, such
     as a fade, a red damage tint or a flash. Note what and when.
8. **Two-handed grip = focused fire (new; needs the redeployed host, see below).** First person
   (K), on foot, assault rifle out, right hand held still a moment (`grip measured`), then a few
   seconds with the left hand anywhere (the mod settles where the left hand goes on this gun).
   - Log first, once per weapon: `weapon: ... (assault rifle); mesh sockets: ...; off-hand
     socket: ...` (tell Claude the socket names), `support grip: candidates in the right hand's axes
     ...: socket ...; LHIK ...; animated left hand ...; the AnimTree's off-hand IK (LeftArm) is
     on/off/blending: strength N, target N, from the AddCombatIdleOffset node / set by the weapon
     code (draw or holster); the animated left wrist is N units from LHIK (held there / not
     there)`, then `support grip: foregrip set from <source>: (...), N along the barrel and N off
     it; ...`.
   - **Native off-hand IK (new, research/18):** `support grip: the AnimTree's off-hand IK (LeftArm)
     is on|off|blending: strength ..., target ...` on each change. Expect "on" with a rifle out in
     combat stance, "blending" then "off" while drawing or holstering (set by the weapon code),
     and "off" whenever the combat-idle node has no weight. Note when you see "off" with the gun
     out: that's a stance where the game itself lets go of the gun.
   - **Grab:** bring the left hand to where Shepard's left hand would hold the rifle's foregrip
     (where you see it on the gun) and squeeze the left grip. It should take hold
     (`halcyon bridge: support grip taken (left hand 0.0x m from the foregrip): LT to the game
     instead of LB` and `support grip: held ...`). **No power wheel** opens.
     - Squeezing the left grip with the hand away from the gun (more than about 12 cm from the
       foregrip) must still be LB (the power wheel), as before.
   - **While held:** Shepard's left hand sits on the gun and stays there as you move. The barrel
     points from your right hand toward your left: move the left hand up/down/sideways and the
     gun swings with it; roll the right wrist and the gun rolls with it. Shots and the reticle
     follow the gun. Does it feel steadier than one-handed? Is the aim more accurate (focused
     fire = the game's LT aim bonus)?
     - **Aimed fire in the log (new, research/18):** `support grip: aimed fire on (both hands, the
       gun at the shoulder): the game sees LT held; the weapon's bIsZoomed is off`, then a tick or
       two later `support grip: the weapon's bIsZoomed is on (our grip forces LT)`. That second
       line is the game's own flag for the smaller aim error and recoil, so it proves focused fire
       without judging the spread by eye. On release: `bIsZoomed is off (our grip doesn't force
       LT)`. `bIsZoomed is on` with "doesn't force LT" means the real left trigger did it. Held
       but not at the shoulder: no LT, and bIsZoomed should stay off. The heartbeat's `support
       grip:` line ends with the native IK state and `bIsZoomed on/off`.
     - **Does the view zoom?** By the code it shouldn't (Halcyon replaces the game's projection
       with the headset's every frame, `[RENDER_FOV_MATCH] applied` in its log, constant
       halfH/halfV), and the first-person camera's aimed-mode offset is within 1 unit of the
       normal one. Say if anything moves or zooms when the grip takes hold.
   - **Release:** open the grip, or pull the left hand more than about 22 cm away from the
     foregrip (`support grip: released`). Pulling away while still squeezing gives nothing until
     you open the grip (no surprise LB).
   - Bring the hands very close together (crossed): it lets go.
   - **Pistol:** the left hand goes under the grip (cupping it), unless the log found a foregrip
     for it. The right hand keeps aiming. Note where you'd want the left hand.
   - **Shotgun:** like the rifle. **Sniper rifle:** the grip holds the gun but sends no LT (its LT
     is the scope camera); set `sniperAims = 1` to try LT anyway.
   - **Settings** (`ASI\LE1MEVR.ini [Controls]`, live): `supportAcquireM` / `supportReleaseM`
     (meters), `supportGrip = 0` turns it off, `leftTrigger = none` stops the left trigger from
     reaching the game (aimed fire then comes from the grip only). Tell Claude which left-trigger
     use you'd like (a power, a squad command).
   - Heartbeat: `support grip: HELD / not held, foregrip <source>; nearest 0.xx m; last write ...
     m, blend, two-hand aim, swing N degrees, left wrist N units from its goal` (a large left-wrist
     number = the arm can't reach the foregrip).
   - **Host redeploy:** the pad mapping lives in the host ASI. Close the game, run
     `tools/deploy-asi.ps1 -Game LE1`. With an older host the mod logs `support grip: the host
     (services v6) can't take it` and everything stays as before.

9. **The camera hook and the torso (new, research/18; mod only, no host redeploy).** First
   person (K), on foot.
   - Log once per skeleton: `arm IK: camera hook bones God(0, parent none: the root),
     Camera(104, parent God(0)): outside every subtree we write (spine LowerBack(..), clavicle
     ..., arm ...), so the view can't follow our writes; the writer skips them anyway`. A
     `WARN ... INSIDE a subtree we write` or `no Camera or God bone` line means research/18 doesn't
     hold for this skeleton: tell Claude.
   - The heartbeat should never show `arm IK writeback: N writes moved a camera hook bone`.
   - Camera mode log, on each change (walk, draw a weapon, crouch, hold LT or the two-hand grip,
     take cover): `camera: mode Class SFXGame.SFXCameraMode_Combat '...CombatCam0' (was ...):
     HookName Camera, HookOffset (...), Offset (...); hook bone Camera(104) at (...) world, F
     forward, R right, U up of the game camera (...) (yaw ...); the formula from it puts the camera
     at (...): N units from the mode's m_pov (...), M from the game camera; LastHookPos ... is K
     from the bone`. **N near 0** confirms the camera hangs off the Camera bone in game (M differs
     by Halcyon's head look and the camera's collision). Cover modes say `CoverSlot`; the sniper
     crouch mode `God`. Tell Claude the F/R/U values per stance: they're why first person needs
     per-state offsets.
   - **The head is no longer pinned:** lean forward and sideways, turn: the view must not move
     with Shepard's body (no judder, no drift), only with your head. The torso and (hidden) head
     now lean together; if anything looks or feels worse than before (a head shadow left behind,
     the view swimming as you lean), say so: `TorsoGoal::keepBone` can come back.

10. **World-locked flat screens and menu (new, 2026-10-21; host redeploy).** Halcyon's flat screen
    (the "view box": movies, cutscenes, flat conversations, the game's menus, the galaxy map) and
    its `\` menu used to ride the head. Now each stays where it appeared: in front of your head at
    that moment, level, at Halcyon's usual distance and size. Log (ours): `world panels: the flat
    screen slot is world-locked`, then one `world panels: <kind> appeared|kind changed|recentered|
    follows the head: heading N deg, eyes at (...), quad at (...) m` per placement.
    - **Intro movie** (launch the game): look around and lean sideways and forward. The screen
      stays put; leaning shows it from a slightly different angle (parallax), and nothing is
      glued to your face. Turn your head well away (more than 70°) and hold it a second: the
      screen eases round in front of you.
    - **Main menu:** when the movie ends and the menu comes up, the screen is placed in front of
      wherever you face then (the kind changed). Press R: it comes back straight ahead.
    - **Pause menu** in gameplay: it opens in front of you, stays while you look around, and
      closes; opening it again facing elsewhere puts it there.
    - **Galaxy map** (Normandy, flat): same.
    - **A flat conversation** (or a cutscene, if VR conversations/cutscenes are off): the screen
      stays put through the shots. Note any jump between a cutscene and the conversation it
      leads into (each kind change re-places it in front of the head, unless the head barely moved).
    - **HUD in gameplay:** the body-locked HUD panel (step 7) only swings round past 70° now.
    - **Pointer on the world-locked menu:** open `\`, then turn your head away (keep the menu in
      view at the edge): the menu stays, and the controller's blue dot still lands exactly where
      the ray hits it. Lean in and out: the dot stays under the ray. Click a tab and drag a slider
      with your head turned. Recenter (R) with the menu open: it jumps straight ahead and the
      pointer still works.
    - **A/B:** in the menu's HUD tab, untick **Flat screens and this menu stay where they appear**
      (`worldLockedPanels = 0` in `MELEVR.ini`): everything rides the head again, as upstream.
      The slider below it (`panelFollowDeg`, 0 = never) sets the swing-round angle. Say which you
      prefer, and whether 70° suits you.
    - **Snapshot** (End) with the menu open and the head turned: Claude checks the menu quad sits
      at its app-space pose in the composite.
11. **Body holsters (new, 2026-10-22; host redeploy for the buzz and the RB block).** Draw
    weapons by reaching for them ([research/19](../research/19-body-holsters.md)):
    - **Where:** pistol at the right hip, assault rifle over the right shoulder, shotgun in front
      of the belly, sniper rifle over the left shoulder.
    - **How:** reach there with the right hand (a short buzz says you're in) and squeeze the right
      grip once. To put the gun away, squeeze and release the right grip with the gun at its own
      slot.
    - **Settings:** `[Holsters]` in `Binaries\Win64\ASI\LE1MEVR.ini`. It applies within a second,
      so you can tune while playing. Set `eyeHeightM` to your eye height (about your height minus
      12 cm) first. The game's weapon wheel stays on the left trigger (`weaponWheel = 1`).

    Steps (first person, on foot, recentered with R while standing straight):
    - **Each slot, with a weapon drawn:**
      - Reach slowly to each slot. Note where the buzz starts (log: `holsters: right hand entered
        <slot> (..., 0.xx m from its center ...)`) and whether that matches where you'd expect
        the weapon.
      - Squeeze there. The weapon switch animation runs and the new gun appears in the hand. Logs:
        `holsters: grab at <slot>: drawing the <type> (SFXGameModeDefault.SwitchToWeapon(N) ...)`
        and `holsters: the <type> is in hand after N s`.
      - The power wheel (RB) must **not** open. If it does, the host wasn't redeployed: say so.
    - **Holstered** (put the gun away with the game's own key, or below): squeeze at a slot. The
      game should draw that weapon (log as above).
    - **Holster:** with the pistol drawn, hand at the hip, squeeze and release. Log:
      `holsters: squeezed at hipRight with the pistol in hand: release here to holster it`, then
      `the pistol goes back in hipRight (SFXGameModeDefault.TryHolster)` and
      `the pistol is holstered after N s`.
      - Squeeze, move the hand away, and release: nothing happens (log `holster ... called off`).
      - Squeeze, pull the trigger, and release: nothing happens either.
    - **A weapon you don't carry:** a weaker, longer buzz, log `you carry no <type>`.
    - **Accidents:** fight normally for a minute, including hip fire low in front of you and
      aiming with the gun at your cheek. Did anything switch or holster by itself? Did the power
      wheel stop working anywhere except right at the slots?
    - **Turning and looking:**
      - Look down at the hip and belly slots. They must not move as you look (log `yaw from arm
        IK's torso` or `the head (lagging)`).
      - Turn your whole body: the slots come round with you after the torso follows.
    - **Fit:** if a slot is hard to reach or too easy to hit, say which. Edit its line (`weapon,
      right, up, forward, radius` in meters) and retry; Claude can also read the slot centers from
      a snapshot (End) taken with your hand at the slot (the `holsters` object in the mod status).
    - **Optional:**
      - `weaponWheel = 0` turns the wheel's LB off (the left trigger then does nothing). Try it
        once holsters feel right.
      - `holster = 0` turns holstering by hand off.
    - Log at startup: `holsters: script functions found (...)`, `holsters: on, weapon wheel kept
      (LB), holster 1, ...`, and once in game `holsters: the player's quick slots: 0: ...
      (pistol); 1: ...; slot = weapon type, as research/19 expects`. **NOT slot = weapon type**
      would mean our reading of the quick slots is off: tell Claude.
    - The heartbeat's `holsters:` line counts entries, draws, holsters and refusals, and lists
      the slot centers from the eyes. Halcyon's side: `halcyon bridge: first controller vibration
      (... applied)` and `first right-grip squeeze at a holster: kept from the game (no RB)`.

What Claude looks for:
- Halcyon: `[XR]` session lines, the swap chain capture, `using the statically linked OpenXR
  loader (MEVR)`, its pacing lines.
- HUD panel and reticle, in Halcyon's log and ours:
  - `[MEVR-HUD] HUD panel texture WxH fmt=..`;
  - the `[MEVR-HUD] to the HUD panel ps=..` / `left in the frame ps=..` shader list;
  - `[MEVR-HUD] panel layer: N UI draws this frame`;
  - `[MEVR-HUD] PCHUD movie layout: authored`;
  - `[MEVR-CROSSHAIR] found ...` and `game crosshair hidden`;
  - `[MEVR-HUD] first hand reticle`;
  - `[MENUVIS]` lines for the HUD panel's swapchain image and texture (visible pixels > 0%).
- World-locked panels (ours): `world panels: the ... slot is world-locked`, one placement line per
  appearance or kind change (many in a row = a kind that flickers), and Halcyon's `[MENUVIS] menu
  quad submitted: ... app space`.
- Insert menu: `[MENUVIS]` lines.
  - The ImGui texture and the swapchain image after the copy should both show visible pixels
    (about 100%).
  - `menu copy:` gives both descriptions (a format or size mismatch would drop the copy).
  - `menu quad submitted`.
  - Ours: `menu pointer: the right controller's ray is on the menu (x, y) px, N m away`, `menu
    pointer: the left/right controller points`, `menu: hand trims ...`, `menu: arm-length sample`.
- Ours: `halcyon: renderer attached`, `hooked UObject::ProcessEvent (host-owned)`, `OpenXR:
  controller actions ready`, `game window to foreground`, `hand aim on`, `first world view`, the
  camera-vs-view check (about 0 degrees), `first shot aimed by the right controller`, `arm IK on`,
  the skeleton and arm-chain lines, `bone layout verified`, `first write`, and the heartbeat's
  `hand aim:`, `host view:` and `arm IK writeback:` lines.
- The gun ([research/14](../research/14-vrik.md), "The gun in the hand"): `weapon: natives:`,
  per weapon `muzzle ... vs GetMuzzleLoc ...: match`, the `GetSocketWorldLocationAndRotation`
  line (do struct out parameters come back?), `weapon: barrel: the muzzle socket's X axis`,
  `in the right hand: its mesh hangs on Prop01(98)`, `grip measured`, `follow check ...: follows
  the written hand in the same frame` (or LAGS: then the fix in research/14), `arm IK: driving:
  ... (the right hand points the gun's barrel ...)`, `hand aim: shots follow the gun barrel from
  the muzzle`, `shot start moved to the muzzle`, `the shot started at the muzzle`, `aim ray from
  the muzzle along the barrel`, and the heartbeat's `weapon:` and `hand aim:` counts.

## LE1 on the desktop (no headset): arm test

Start the game with no headset session (headset off or SteamVR closed: once Halcyon has shown a
view or read a controller, the test stays off for that game session). Load a save on foot, press
`.`: the right hand should come up and stay in front of Shepard's chest (details in A0 below).
`.` again hands the arm back.

## Our own renderer (LE2/LE3, or LE1 built without Halcyon)

Keys: `]` is the only one you need. It turns VR on and off, and the VR features come with it:
once the headset session starts, stereo, 6DOF, hand aim and arm IK are all on, and VR off
turns them all off. The rest are for testing:

| Key | What it does |
|-----|--------------|
| `]` | VR on / off, with all VR features (each `]` on resets the four keys below `'`) |
| `[` | head-tracked / screen mode |
| `\` | head path: render / camera / off |
| `'` | desktop head simulator: sweep / nod / off |
| `;` | stereo off / on (alternate-eye) |
| `/` | 6DOF off / on |
| `,` | hand aim off / on: shots follow the right controller (on foot) |
| `.` | arm IK off / on: Shepard's arms follow the controllers. Without a VR session: the desktop arm test (below) |
| `=` | conversations, cutscenes, menus and galaxy map on the virtual screen: on (default) / off |

## A0. Desktop arm test (no headset, about 2 minutes)

Checks the new arm writer (route 2, [research/14](../research/14-vrik.md)): the mod rewrites
Shepard's arm bones after the animation each frame.

1. Launch ME1 and load a save where Shepard stands on foot (any level). On LE1 (Halcyon's
   renderer) start without a headset session (headset off or SteamVR closed); with our own
   renderer press `]` to turn VR off if SteamVR complains.
2. Press `.` once. The log says `arm IK on (no headset to follow: desktop test, ...)`.
3. Within a second, Shepard's **right hand should come up and stay in front of his chest** at
   about shoulder height, with the elbow bent and out to the side. Watch from the normal
   third-person camera, and rotate the camera around him if the body hides it.
4. Walk around, turn, holster or draw the weapon. The hand should stay in front of the chest. The
   fingers should keep their pose on the hand, with no stretched or torn mesh at the wrist or
   elbow. Note whether the weapon (if drawn) moves with the hand.
5. Press `.` again: the arm returns to the animation.
6. Note what you saw (hand moved or not, any tearing, weapon or not), then quit.

What Claude looks for:
- `skeleton: GetBoneNames through ProcessEvent returned 0 names ...` (the root-cause check) and
  the bone list with parents;
- `arm IK: skeleton ... right arm RightShoulder(..) > RightElbow(..) > RightWrist(..)` (or
  whatever the names are);
- `arm IK: layout check ... match` lines and `bone layout verified`;
- `arm IK: first write, right arm ...`: target vs wrist after (distance ~0), the arm lengths;
- heartbeat `arm IK writeback: N UpdateSkelPose calls, M for the player mesh (from exe+0x58D709...),
  K written, 0 rejected`.

## A. Desktop only (no headset needed, about 2 minutes)

Checks the new render-only head path on the monitor.

1. Launch ME1 and load a save. If SteamVR starts complaining that there's no headset, press `]`
   to turn VR off.
2. Press `'` once: **sweep**. The *view* should slowly swing left/right and up/down while the
   game camera behind Shepard stays where it is.
3. Press `'` again: **nod** (pitch only). Watch for jerkiness on the monitor.
4. Press `\` to switch to the **camera** path, the old behaviour: the camera boom swings around
   Shepard. Compare the smoothness of the two paths. Press `\` twice more to return to
   **render**.
5. With the sweep running, walk forward with a gamepad's left stick. On the render path, during
   on-foot gameplay, you should move where the view points. Only the stick is rotated, not the
   keyboard, and not in conversations, menus, wheels, the galaxy map or the Mako.
6. Press `'` again to turn the simulator off, then quit.

What Claude looks for in the log:
- `hooked ULocalPlayer::CalcSceneView`;
- `render path: N player views, M GetPlayerViewPoint ... calls inside them`, with N about
  equal to fps × 5 and M ≥ 1 per view;
- no `falling back` warning;
- `blit: source ... (format 28 or 24)` instead of 10, which means HDR is off;
- `gamepad: hooked XInputGetState@...` and the poll count;
- `reflections: ... raised N` on a level with reflective floors.

### B. In the headset (head-tracked mode, the default)

1. Start SteamVR / Steam Link first, then the game, and load a save. Everything comes on with
   the headset session (the log says `VR features on`).
2. **Nod test:** look slowly up and down, then side to side. Is the vertical judder gone, better
   or unchanged? (The default path is now render-only.)
3. **If anything feels wrong** (flicker, ghosting, wrong scale, nausea), switch features off one
   at a time to find which: `;` stereo first, then `/` 6DOF. Note which one made the
   difference, then press it again to turn it back on.
4. **A/B:** press `\` to compare render vs camera path stutter. Say which is smoother. Return
   to render.
5. **Touch controllers:**
   - left stick: move;
   - right stick: turn;
   - triggers: aim / fire;
   - A / B / X / Y;
   - grips: LB / RB;
   - left menu: pause.

   Does pushing forward move where you look? It should only do so on foot; check that the
   dialogue wheel isn't rotated.
6. **6DOF:** lean or crouch. Does the view move naturally, at a believable scale? (`/` compares
   with it off.)
7. **Stereo:** is there depth? Comfortable, or ghosting and flicker on movement? Is the scale
   right? (`;` compares with mono.)
8. **Hand aim:** draw a weapon and fire at something while pointing the right controller
   elsewhere than your head. Do shots land where the controller points? (There's no reticle
   yet, so watch the impacts; `,` compares with the game's aim.)
9. **Arm IK:** wave the controllers. Hands are placed relative to the head-tracked view, so
   this works in the normal third-person view: do Shepard's arms follow your hands? Twist a
   controller: the hand should turn with it, from its animated pose. Note any tearing at the
   elbow or wrist, and whether the weapon follows the right hand. (`.` compares.)
10. **Conversations:** talk to someone. After about a third of a second the view should switch to
   the flat virtual screen, placed in front of wherever you're facing, and go back to
   head-tracked afterwards. Press `=` to compare with head-tracked
   conversations.
11. Look at a reflective floor (e.g. the Normandy) while turning. Do reflections still jerk?
12. Known: on the render path the crosshair no longer follows your head. Aim is still the game
    camera's.
13. If the game crashes, say what you were doing. Next launch, switch the features off with
    `;` `/` `,` `.` right after the session starts and bring them back one at a time.

What Claude looks for:
- queue depth all at 1;
- lockstep timeouts about 0, and drift recoveries 0;
- `frames tagged (N without a player view)`: N should be about 0 in gameplay;
- present intervals tight around 11.1 ms (90 Hz);
- fps now that VSync is off;
- `controller actions ready`;
- `stereo alternate-eye (L n / R n)` roughly equal;
- the half IPD in mm;
- the `skeleton:` lines (bones, positions, skeletal controls): Claude reads them for VRIK
  ([research/14](../research/14-vrik.md)); nothing to do in game.
