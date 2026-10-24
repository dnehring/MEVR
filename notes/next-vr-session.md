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

**Keys on LE1:** `\` = our main menu (since 2026-10-23, step 25), or click and hold both thumbsticks to
open or close it; **Ctrl + `\`** = Halcyon's old menu (with the VR hands tab). While either is open the
game gets no input. Point at it with a controller (laser pointer: trigger clicks, right stick scrolls),
or navigate it as a gamepad (left stick or d-pad moves, A activates, B backs out). R = recenter
(or double-click the right stick), K = first person on/off (off at every launch). Ours: `,` hand
aim on/off and `.` arm IK on/off, both on by themselves once the controllers are tracked; the
other keys in the table further down are for our own renderer only.

## Test loop: let Claude see what you see (new)

The host can now save what each eye shows (PNG snapshots) and record your head and controllers,
so Claude can look for itself and replay your movements later without you in the headset.
Redeploy the host first (close the game, `tools/deploy-asi.ps1 -Game LE1`). Files go to
`%LOCALAPPDATA%\MEVR\snapshots` and `\recordings`.

- **Snapshot:** press **End** (game window focused), or **tap both stick clicks together** (click
  and let go of both within about a third of a second; since 2026-10-22, step 12: holding both
  still opens Halcyon's menu instead), or in Halcyon's menu (`\`) open **VR hands -> Test loop ->
  Snapshot in 3 s** and close the menu. Log: `test loop: both sticks tapped: snapshot` then `test
  loop: snapshot saved in ...`. Claude can also take them any time while you play
  (`tools/snapshot.ps1`): just keep still and say "go" when you're in position.
- **Recording a short session for Claude** (about 20-60 s is plenty):
  1. In game, on foot, with a weapon drawn. Recenter first (R).
  2. Press **Home** (or Halcyon's menu: VR hands -> Test loop -> Start recording) to start. Log:
     `test loop: recording the head and controllers to ...\<stamp>.mevrrec`. (The old grip chord
     for recording is gone: both grips are the gun and the squad menu now.)
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
11. **Body holsters, the grip holds the gun (new, 2026-10-22; HOST REDEPLOY: services v10).**
    Draw weapons by reaching for them, carry them by keeping the right grip squeezed
    ([research/19](../research/19-body-holsters.md) sections 3-8). Close the game and run
    `tools/deploy-asi.ps1 -Game LE1` first: without the new host the mod logs `holsters: gripHolds
    = 1 needs the redeployed host (services v10)` and falls back to the old squeeze-to-draw mapping
    (the right grip stays RB), with one buzz on entering a slot instead of the contact buzz.
    - **Where:** pistol at the right hip, assault rifle over the right shoulder, shotgun in front
      of the belly, sniper rifle over the left shoulder.
    - **How:** reach there with the right hand (you feel a buzz while the hand is in the slot,
      stronger toward its middle) and squeeze the right grip: the gun comes out. **Keep squeezing
      to hold it.** Let go at its own slot to holster it. Let go anywhere else and it goes back to
      its holster after 0.3 s, unless you squeeze again (never while you hold the trigger).
    - **RB (the power wheel) is now the right stick click:** hold it and tilt the right stick to
      choose a power, let go to use it; a quick click casts your mapped power (about a third of a
      second later: it waits to be sure it isn't a double-click recenter).
    - **Settings:** `[Holsters]` in `Binaries\Win64\ASI\LE1MEVR.ini`, applied within a second.
      New keys (added to your file with their defaults): `gripHolds = 1` (0 = the old mapping),
      `releaseGrace = 0.3` (seconds; -1 = a let-go gun stays out), `powerWheel = r3` (or `y`, `x`,
      `b`, `none`), `eyeHeightFromFloor = 1`. `hapticAmplitude` (0.5) is now the strength of all
      the holster haptics (0 = none). The game's weapon wheel stays on the left trigger (it also
      picks your squad's weapons).

    Steps (first person, on foot). **Recenter with R while standing straight** first: that's when
    your standing eye height is taken.
    - **Eye height (log first):** `floor: the head's height above the floor comes from STAGE
      (reference spaces: ...)`, `floor: first sample: the head is 1.xx m above the floor`, then
      `holsters: recentered with the head 1.xx m above the floor: taken as the standing eye
      height` and `holsters: eye height 1.xx m from the floor (STAGE), standing (slot scale
      0.9x)`. Is 1.xx your eye height (roughly your height minus 12 cm)? If it says `from [Holsters]
      eyeHeightM (...)`, the reason in brackets says why (no floor set up in the headset, an older
      host): tell Claude. Crouch: the scale must not change (the slots come down with you).
    - **Contact haptics:** move the right hand slowly into the hip slot and through it. A firmer
      edge as you cross in, then a buzz that grows toward the middle and fades toward the edge,
      and stops when you're out. Same at each slot. Does it feel like touching something? Too
      strong or too weak (`hapticAmplitude`)? Can you tell the middle from the edge? (If the pitch
      doesn't change, the runtime ignores the frequency: say so.)
    - **Draw and carry:** squeeze at the hip: a crisp click, the pistol comes out. Logs: `holsters:
      grab at hipRight: drawing the pistol (SFXGameModeDefault.SwitchToWeapon(0); was holstered);
      keep the grip squeezed to hold it` and `holsters: the pistol is in hand after N s`. Keep
      squeezing and fight with it: no power wheel opens (Halcyon's log: `first right-grip squeeze
      that holds the gun: kept from the game (no RB); RB comes from the right stick click`).
    - **Holster at the slot:** carry the pistol back to the hip and let go: a softer click, it's
      put away. Log: `holsters: let go at hipRight: the pistol goes back in its holster
      (SFXGameModeDefault.TryHolster)`, `the pistol is holstered after N s`.
    - **Let go elsewhere:** hold the pistol out in front, let go: `holsters: let go of the pistol
      away from its slot: it goes back in 0.30 s unless you squeeze again`, then 0.3 s later `let
      go 0.30 s ago away from hipRight: the pistol goes back in its holster`. Again, but squeeze
      again quickly: `the pistol stays out (squeezed again)`.
    - **Never mid-burst:** hold the trigger (assault rifle, burst fire), let go of the grip, keep
      firing: the gun stays out while you fire (`waits for the trigger to be let go`), then goes
      back 0.3 s after you stop.
    - **Swap:** holding the pistol, reach to the right shoulder and let go there, then squeeze there
      again: the rifle. Or let go anywhere and squeeze at the shoulder within 0.3 s.
    - **A weapon you don't carry:** a dull longer buzz, log `you carry no <type>`.
    - **The power wheel on the right stick click:** hold the click: the wheel opens (about 0.4 s);
      tilt the same stick to choose, let go. A quick click casts the mapped power. A double click
      recenters (R3 twice quickly) and must **not** cast anything. Clicking while turning with the
      stick pushed over does nothing. Both sticks held still open Halcyon's menu (no wheel).
      If holding the click while steering is awkward, try `powerWheel = y`.
    - **Two-handed and aimed fire:** squeeze the left grip at the foregrip while holding the gun:
      as before (`support grip: held ...`, aimed fire at the shoulder). Crouch (left stick click)
      and the power wheel (right stick click) must still work while you aim two-handed.
    - **Test loop chords:** replaced in step 12 (a snapshot is a tap of both stick clicks; the grip
      chords are gone).
    - **Menus and wheels:** open the power wheel (R3 hold) or the pause menu while holding the gun,
      close it, let go: the gun goes back as usual. In the Mako the right grip fires the cannon
      as before (`holsters: the right grip is RB (gripHolds off, or not on foot)`).
    - **Accidents:** fight for a minute, including hip fire low in front of you and aiming with the
      gun at your cheek. Did anything switch or holster by itself? Is holding the grip through a
      fight tiring? (`gripHolds = 0` brings the old mapping back.)
    - **Turning and looking:** look down at the hip and belly slots: they must not move. Turn your
      whole body: the slots come round after the torso follows.
    - **Fit:** if a slot is hard to reach or too easy to hit, say which. Edit its line (`weapon,
      right, up, forward, radius` in meters) and retry; Claude can also read the slot centers from
      a snapshot (End) taken with your hand at the slot (the `holsters` object in the mod status:
      `eyeHeight`, `eyeHeightSource`, `floorEyeHeight`, `contact`, `phase`, `returnLeft`).
    - Logs at startup: `holsters: script functions found (...)`, `holsters: on, the right grip
      holds the gun (let go elsewhere: back after 0.30 s), RB from the right stick click, ...`,
      `hands: added gripHolds, releaseGrace, powerWheel, eyeHeightFromFloor (defaults) to
      [Holsters]` (once), `holsters: the right grip holds the gun (no RB from it); RB is the right
      stick click`, and in game `holsters: the player's quick slots: ...; slot = weapon type, as
      research/19 expects` (**NOT slot = weapon type** means our reading is off: tell Claude).
    - The heartbeat's `holsters:` line: entries, draws, holsters, refusals, the strongest contact
      buzz, the eye height and its source, the head's height now. Halcyon's side: `halcyon bridge:
      first controller vibration (right hand, N ms at 0.xx, NNN Hz): applied`.
12. **Squad weapons at the left hand, and the snapshot tap (new, 2026-10-22; HOST REDEPLOY:
    services v11).** Since step 13 this is `[Squad] style = custom` (our drawn menu, the fallback);
    the default is the game's own wheel (step 13). Give a squadmate a weapon without the game's wheel
    ([docs/squad-weapons-design.md](../docs/squad-weapons-design.md), research/19 section 9). Close
    the game and run `tools/deploy-asi.ps1 -Game LE1` first: without the new host the mod logs
    `squad: the host can't draw the squad menu ...: the menu stays off; ... redeploy the host`.
    - **How:** on foot, with two squadmates. **Point your left hand at one of them and squeeze the
      left grip** (away from the gun's foregrip). A menu appears above your left hand: their two
      cards left and right (the one you pointed at outlined in amber), and in the middle the
      weapons the chosen one carries: assault rifle up, shotgun left, sniper rifle right, pistol
      down. **Flick the right stick** toward one (it turns amber, a tick in the right hand), **let
      go of the grip**: a firm click in the left hand, and they switch. B cancels; letting go with
      nothing chosen cancels; A confirms too. The left stick (left or right) picks the other card.
    - **Pointing at nobody:** keep the grip squeezed about a third of a second: it opens with the
      squadmate you gave a weapon last (or the one nearest where you point). A quick squeeze at
      nobody does nothing, and so does one right next to the gun's foregrip (a missed grab).
    - **While it's open** the game doesn't see the sticks, A or B (you stop walking and turning) or
      the stick clicks; the game keeps running (`pause = 1` pauses it like ME1's own wheel). After
      it closes, a stick or button you still hold stays off until you let go of it.
    - **What to check:** the panel's place (above the hand, readable, about 40 cm wide, still while
      open even if the hand moves); the names, classes, health and the weapon in hand on the cards;
      only weapons they carry are shown (the others are faint empty outlines); the ticks and the
      click; the switch itself. With your own weapon **out**, the game's AI switches theirs (watch
      whether they play an animation); with it **holstered**, it switches at once (as ME1's wheel
      does). Does it take long? Does anything else happen to the squad (their orders reset)?
    - **Snapshot of it:** with the menu open, tap both stick clicks: the snapshot has
      `<stamp>-squad.png` and the menu in `-left.png` / `-right.png`.
    - **Settings:** `[Squad]` in `Binaries\Win64\ASI\LE1MEVR.ini` (added to your file): `enabled`,
      `pointConeDeg = 10`, `pointMaxM = 40`, `openHoldS = 0.35`, `panelDistanceM = 0.27`,
      `panelWidthM = 0.40`, `confirmOnRelease = 1` (0: the menu stays open after you let go; A gives,
      B or a new squeeze cancels), `pause = 1` (like ME1's wheel; 0 keeps the game running),
      `hapticAmplitude = 0.5`.
    - **The both-sticks tap (snapshots):** click and let go of both sticks together quickly: one
      snapshot (`test loop: both sticks tapped: snapshot`). Hold both: Halcyon's menu opens, and no
      snapshot. Click the left stick alone: you crouch, about a tenth of a second later than before
      (it waits to see whether the right click joins); a both-sticks tap never crouches. A quick
      right click still casts the mapped power about a third of a second later.
    - **Logs (ours):** `squad: script functions found`, `squad: settings: on; pointing within 10
      degrees ...`, `squad: squadmate 0x...: "GARRUS" from its class (class 8 = TURIAN AGENT;
      ActorGameName "...", actor type's "...", tag "...")` (tell Claude the names in quotes: they
      show what the game itself calls them), `squad: <name>'s quick slots: ...` (**NOT slot =
      weapon type** means our reading is off), `squad: <name> (<class>; health N%; in hand ...;
      carries ...)` per change, `squad: left grip squeezed away from the gun: pointing at <name>
      (N deg off, N m)`, `squad: menu #N opened (pointed): <name> chosen; cards: left .., right ..`,
      `squad: highlight: the assault rifle (up)`, `squad: menu #N confirmed (grip let go): <name>
      gets the ...`, `squad: <name> ordered to the ... : SquadOrderEquipWeapon, then
      ApplyTacticalOrders (...)` or `... ChangeToQuickSlot and SetMemberSwitchWeapon(4) (...)`,
      then `squad: <name> holds the ... after N s` (or a warning that they don't), and the
      heartbeat's `squad:` line. Halcyon's side: `halcyon bridge: squad menu open: the sticks, A, B
      and the stick clicks are its own`, `squad panel: ImGui context of its own ..., font
      bahnschrift.ttf`, `squad panel: swapchain 1024x600`, `squad panel: menu #N placed 0.27 m from
      the left grip, at (...)`.

13. **The game's own weapon wheel at your hand (new, 2026-10-22; HOST REDEPLOY: services v12).**
    The squad gesture now opens ME1's real weapon wheel (its look, its pause, its rules), shown on a
    panel above your left hand ([docs/squad-weapons-design.md](../docs/squad-weapons-design.md)
    "The game's own wheel", research/19 section 10, research/15). Close the game and run
    `tools/deploy-asi.ps1 -Game LE1` first: with the old host the mod logs `squad: style = native,
    but the host can't show the game's wheel at the hand (services older than v12 ...): our drawn
    menu instead`.
    - **Superseded (late 2026-10-22): the left trigger opens it, not the grip: step 17.**
    - **How:** on foot. **Point your left hand at a squadmate and squeeze the left grip** (away from
      the gun), or keep it squeezed a third of a second pointing at nobody. The game's wheel opens
      (the game pauses, as on a flat screen) on a panel about 40 cm wide above your left hand,
      fixed in the room while it's open. Pointing at someone first puts the highlight on their
      weapon in hand. **Push the right stick** toward a weapon (one ring: Shepard's four weapons,
      and each squadmate's four in their own arc): the highlight moves, the game plays its sound,
      and your right hand ticks (a soft buzz on a weapon they don't carry). **Let go of the grip:**
      the mod presses A for you (the game's own "pick this one") and closes the wheel; the
      squadmate switches. A picks too (you can pick for both squadmates, then let go); B closes it
      without picking. Opening the wheel draws Shepard's weapon if it's holstered: ME1's own wheel
      does that (EnterWeaponWheel calls DrawPrimaryWeapon). Tell Claude if that's unwanted.
    - **While it's open:** a stick that was pushed when it opened (turning) steers nothing until you
      let it come back to centre; the left stick doesn't walk; B and the stick clicks don't reach the
      game. After it closes, what you still hold stays off until you let go.
    - **The wheel shows once:** nothing of it should stay stuck to your view, in either eye (the
      doubled wheel of 2026-10-21). The rest of the HUD stays on the HUD panel, with the wheel's
      part cut out of it.
    - **Also check:** the left trigger (LB) still opens the wheel the flat-game way, now also on the
      panel at the left hand. R3 held (the power wheel) shows on a panel at your right hand
      (`powerWheelAt`), steered with the right stick in both directions (decoupled pitch no longer
      cuts its up/down).
    - **What to check:** the panel's place and size; whether the crop shows the whole wheel (squad
      portraits included) and nothing else; the highlight following the stick, all twelve entries
      reachable; the preselect when pointing; letting go gives the highlighted weapon; how long the
      squadmate takes to switch, weapon out and holstered.
    - **Snapshot of it:** with the wheel open, tap both stick clicks: `<stamp>-wheel.png` (the
      wheel panel), `-hud.png` (the HUD panel with the hole), and both in `-left.png` / `-right.png`.
      Claude can tune the crop from these.
    - **A/B in the headset:** Halcyon's menu (`\` or both sticks held) -> VR hands -> "Squad
      weapons": the game's own wheel or our menu, and which stick steers the wheel (takes effect
      within a second).
    - **Settings:** `[Squad]` in `Binaries\Win64\ASI\LE1MEVR.ini` (new keys added to your file):
      `style = native` (`custom`: step 12's menu), `wheelStick = right` (`left`: the stick on the
      hand that holds the grip; `either`), `wheelPreselect = 1`, `wheelPanel = 1`, `weaponWheelAt =
      left` (`right`, `front`, `hud`), `powerWheelAt = right`, `wheelCrop = auto` (or `x, y, width,
      height` as fractions of the frame), and as before `panelDistanceM`, `panelWidthM`,
      `confirmOnRelease` (0: letting go closes without picking; only A picks), `openHoldS`,
      `hapticAmplitude`.
    - **Logs (ours):** `squad: script functions found ...; the game's own wheel can be opened and
      closed`, `squad: style: the game's own weapon wheel at the hand`, `squad: wheel #N: opening
      the game's weapon wheel, pointed at <name> (SFXGameModeDefault.EnterWeaponWheel ...)`,
      `squad: wheel #N open after N s (game mode 3 ...); HUD handler BioSFHandler_...HUD ...,
      its movie uses the gamepad` (**tell Claude if it says "doesn't use"**), `squad: wheel #N:
      preselected <name>'s <weapon> (wheel member 1, slot 2: ...)`, `squad: wheel highlight: member
      M slot S = <name>'s <weapon>` per change (**check the name and weapon match what the wheel
      shows**), `squad: wheel #N: grip let go: giving <name> the <weapon> (A for the game ...)`,
      `squad: wheel #N: closing (ExitWeaponWheel ...); queued: <name>: <weapon>` (**nothing
      queued after a pick means the A didn't reach the wheel**), `squad: wheel #N closed after N
      s`, `squad: <name> holds the <weapon> after N s`. Halcyon bridge: `halcyon bridge: the
      game's weapon wheel held by the left grip: the right stick steers it ...`, `A pressed for the
      mod`. Overlays (both logs): `[MEVR-WHEEL] the weapon wheel on its own panel: 0.27 m from the
      left hand, at (...), 0.40 m wide; the UI's part (x, y) WxH of 3072x3188 (the default guess /
      measured)`, and once per session `[MEVR-WHEEL] measured: the wheel is at (...)` (or why it
      wasn't), `overlays: wheel panel swapchain WxH`, `[MEVR-WHEEL] the weapon wheel closed`. The
      doubled wheel: any `[MEVR-HUD] ... to the HUD panel (a Scaleform blend mode, as a tint) ps=..`
      and `[MEVR-HUD] Scaleform blend mode src=9 dst=1 ...: colour to the HUD panel, and its effect
      on what's behind as the panel's tint` lines, and `overlays: first frame with Scaleform
      blend-mode draws in the panel's tint`.
14. **Gun sights and the sniper scope (new, 2026-10-22; HOST REDEPLOY: services v13).** Every gun
    has a sight now: a red dot on the pistol, a holographic ring and dot on the shotgun and the assault
    rifle, a 4x scope on the sniper rifle ([research/21](../research/21-sights-and-scopes.md)). Close
    the game and run `tools/deploy-asi.ps1 -Game LE1` first: with the old host the mod logs `sights:
    the host (services v12) can't draw gun sights: close the game and redeploy the host`.
    - **Dot and holo:** first person (K), on foot, draw the pistol. A faint dark ring marks the sight's
      window on the gun. Bring the gun up to your eye and look through that ring: the dot appears, out
      on whatever you aim at, only in the eye that looks through it. Move your head a little behind the
      sight: the dot stays on the target (it's collimated), and goes when your eye leaves the window.
      The floating ring reticle hides while you look through. Fire: shots land at the dot (a few cm
      under it: the sight sits above the barrel). Same with the shotgun and the assault rifle (a ring
      and a dot).
    - **Scope:** draw the sniper rifle, shoulder it, and bring your right eye about 10 cm behind the
      back of its scope. A round picture appears there: the view 4x magnified, a black crosshair with a
      red centre. Move your eye sideways: a black crescent creeps in (the scope's shadow) while the
      crosshair stays on the target. Pull your head back: the picture shrinks. The other eye sees the
      world as usual (keep both eyes open). Fire through it: the shots get the game's aimed-fire
      accuracy (no game scope camera, no third-person jump).
    - **What to check:** where each sight sits on each gun (too high, too far back?), the dot's size and
      colour, whether you can get your eye close enough to the scope with Shepard's gun, how sharp the
      scope's picture is, whether the crosshair is where the shots land, any flicker as the eye enters
      or leaves the sight. **Close each eye in turn** while looking through: the dot and the scope's
      picture should be in the looking eye only (if the other eye sees them too, the runtime ignores
      per-eye quads: tell Claude).
    - **Snapshot of it:** look through the sight and tap both stick clicks: `<stamp>-sight.png` (the
      dot), `-sightframe.png` (the window ring), `-scope.png` (the scope's picture), and both eyes with
      them in `-left.png` / `-right.png` (the dot or the scope only in the eye looking through). The JSON's
      `"sights"` says where each eye is against the sight (`along`, `lateral`), which eye aims, and the
      mod's `"sights"` the placement. Claude can tune the placement from these.
    - **Settings:** `[Sights]` in `Binaries\Win64\ASI\LE1MEVR.ini` (added to your file, live):
      `pistol = dot`, `shotgun = holo`, `assaultRifle = holo` (try `scope 2`), `sniperRifle = scope 4`
      (`none` turns one off); `pistolPlace = auto` (or `back, height` in gun units: behind the muzzle,
      above the bore; also `shotgunPlace`, `assaultRiflePlace`, `sniperRiflePlace`); `dotColor = ff3020`
      (or red, green, amber, cyan, white), `dotSizeDeg = 0.25`, `ringSizeDeg = 1.6`, `windowUnits = 3`,
      `frame = 1`, `zeroM = 0`; scope: `lensUnits = 2`, `reticle = duplex` (`mildot`, `dot`),
      `reticleColor = ff2020`, `source = other` (`same`: magnify the aiming eye's own picture),
      `focusM = 40`, `eyeReliefM = 0.12`, `resolution = 768`; `replaceRing = 1`, `aimedFire = 1`.
    - **A/B in the headset:** Halcyon's menu (`\` or both sticks held) -> VR hands -> "Gun sights": per
      weapon none / dot / holo / scope with its zoom, and which eye's picture a scope magnifies (takes
      effect within a second).
    - **Offline look:** `build\Release\mevr-sight-preview.exe <folder>` renders the scope (several eye
      positions and reticles) and the reticles to PNGs without the game.
    - **Logs (ours):** `sights: settings: on: pistol red dot (placed auto), ...`; per gun `sights: the
      sniper rifle has a scope sight (4.0x): N units behind the muzzle, N above the bore (auto; the gun is
      N units from muzzle to grip), eyepiece radius 2.0 units (N m at N units per meter)` (**tell Claude
      the numbers if a sight looks misplaced**); `sights: the host draws a ... sight for the ...`;
      `[MEVR-SIGHT] first sight window ring at (...)`, `[MEVR-SIGHT] first holographic reticle: the
      right eye looks through the window (N m behind it, N m off the line, ...)`, `[MEVR-SIGHT] the right
      eye through the red dot sight` / `no eye through the sight` as it changes; scope: `sights: the scope
      magnifies a copy of the eye image (WxH, format N)`, `[MEVR-SIGHT] first scope picture: the right eye
      aims (N m behind the eyepiece, ...), 4.0x from the left eye's image ...`, `sights: the right eye
      sees the scope's picture`, and on the first scoped shot `sights: first scoped shot with aimed fire:
      aim error A -> B` (B smaller); the heartbeat's `sights: ...; scoped aimed fire on N of M aim-error
      queries`.
15. **A recenter that fixes an upside-down or tilted world (new, 2026-10-22; HOST REDEPLOY).** If the
    world ever shows upside down or tilted (SteamVR started wrong), look straight ahead, level, and
    recenter (R, double-click the right stick, or Halcyon's menu). Normally a recenter is the same as
    before (yaw only).
    - **What to check:** a normal recenter still just turns the world to face you. After 5 s of an
      upside-down world, our log hints to recenter. Don't recenter while staring at the floor for a few
      seconds: that would tilt the world (recenter again looking ahead to undo it).
    - **Settings:** `recenterTiltDeg = 60` in `MELEVR.ini` (0 = Halcyon's yaw-only recenter, always).
    - **Logs (both ours and Halcyon's):** every recenter adds `[MEVR-RECENTER] yaw-only recenter
      (upstream): the head is level enough in the base space (head up N deg from the base space's up,
      roll N deg, pitch N deg)` after Halcyon's `[M1] recenter - app space re-origined to head yaw=N`;
      the first one says `, automatic`; a correction says `[MEVR-RECENTER] tilt-corrected recenter: the
      head is rolled past 90 degrees in the base space (...)`; the hint is `[MEVR-RECENTER] the world
      has looked upside down for 5 s ...`. Snapshots carry `"recenter"` (recenters, corrected, the head's
      tilt now).

16. **Physical grenades and melee (new, 2026-10-22; the mod hot-reloads; the host redeploy
    (services v14) is only needed to turn B or X off).** Research and design:
    [research/20](../research/20-melee-and-grenades.md). B and X still work as before.
    - **Grenade:** on foot, weapon out (with it away, taking a grenade draws it first: ME1 throws
      only with a weapon out). Reach your **left hand to the left side of your chest** (about a
      hand's width left of the sternum, a little below the collarbone): it buzzes like a holster.
      **Squeeze the left grip** there (a new squeeze; the left grip at the gun's foregrip is still
      the two-handed grip): a click, then a faint buzz while you hold it. **Throw it and let go**
      while your hand moves: Shepard plays his throw, and the grenade (the game's disc) leaves from
      your hand toward where your throw would land, flying straight and sticking there (a lob over
      cover lands behind it). **Let go slowly** (under 1.5 m/s): it goes back (a softer click).
      The right hand can do the same (it crosses to the left side of the chest); while it holds a
      grenade the gun stays out, and after the throw squeeze the right grip again to hold the gun.
    - **Melee:** with a weapon out and an enemy within about 2 m in front of you, **punch or swing**
      either hand fast (a pistol whip or rifle butt with the gun hand, a punch with the free hand):
      the game's melee once (a tick in that hand; a thump when it hurts the enemy). Not while
      firing, holding a grenade, just after a throw or a draw from a holster, or with the left hand
      on the foregrip (the gun hand's swing counts then). Pulling back fast isn't a strike.
    - **What to check:** the slot's place (too high, low, far?); grabs you didn't mean (reaching
      for the sniper rifle, the support grip, the squad menu); how long Shepard takes to throw after
      you let go (`launched N s after`); whether the grenade leaves from your hand and lands where
      you threw; gentle tosses vs hard throws (too short, too far? `throwGain`); melee strokes that
      didn't count (the log says why) and strikes you didn't mean while aiming, reloading, turning;
      whether the game's melee hits what's in front of the hand or of Shepard.
    - **Snapshot:** End (or the stick tap) while holding a grenade: the mod status's `throwables`
      has the slot's centre, both hands' distances, the holder, the last throw (hand speed,
      direction, landing, launch delay) and the last melee.
    - **Settings:** `[Throwables]` in `Binaries\Win64\ASI\LE1MEVR.ini` (added to your file):
      `enabled`, `hands = both | left | right`, `slot = right, up, forward, radius` (-0.13, -0.33,
      0.00, 0.10), `throwSpeedMinMS = 1.5`, `throwGain = 2.5`, `throwSpeedMaxMS = 15`, `flight =
      disc` (`arc`: the game's unused ballistic grenade with your hand's velocity, experimental),
      `fromHand = 1`, `drawForThrow = 1`, `earlyRelease = 0.3` (0: the grip lets go only when
      opened), `grenadeButton = 1`, `melee = 1`, `meleeSpeedMS = 3.0`, `meleeGunSpeedMS = 3.5`,
      `meleeTravelM = 0.12`, `meleeCooldownS = 0.8`, `meleeNeedsEnemy = 1`, `meleeReachM = 2.0`,
      `meleeConeDeg = 75`, `meleeButton = 1`, `hapticAmplitude = 0.5`, `holdBuzz = 1`.
      `grenadeButton = 0` / `meleeButton = 0` keep X / B from the game on foot (needs the redeployed
      host; else logged once: `throwables: meleeButton/grenadeButton = 0 needs the redeployed host`).
    - **Logs (ours):** `throwables: script functions found`, `throwables: settings: ...`,
      `throwables: active`, `throwables: left hand entered the grenade slot (...)`, `throwables:
      grenade taken with the left hand (N left)` (or `a squeeze at the grenade slot ...: no grenade
      (why)`), `squad: the left squeeze took a grenade (the grenade slot): no menu`, `holsters: the
      right hand has a grenade: the gun stays out as if held`, `throwables: grenade thrown with the
      left hand at N m/s (fastest N ms before the release, N samples), direction (...), N degrees
      up; world velocity (...) (x2.5); lands on <actor> at (...), N m away (flight: disc)`,
      `throwables: Actor.Trace through ProcessEvent: the hit point came back` (**or "didn't come
      back": tell Claude**), `throwables: SFXGameModeDefault.UseGrenade called N s after the
      release: Shepard is throwing`, **`throwables: grenade launched N s after the left hand's
      release (LaunchGrenadeAtPoint(...) -> BioGrenadeProjectileFrisbee_N): the disc re-aimed,
      moved N units ... to the hand to (...) (N m, ...)`** (the game's own target is the first
      vector: compare it with where you looked), `throwables: the left hand let go at N m/s (under
      1.5): the grenade goes back`, `throwables: melee: the right hand's N m/s stroke (N m, N toward
      the front) -> SFXGameModeDefault.TryMelee (pistol whip); target <pawn> at N m, N degrees,
      health N%`, `throwables: melee: hit <pawn> after N s (health N% -> N%)` or `no damage seen
      ...`, `throwables: melee: ... isn't a strike: <why>` / `... no strike (<why>)`, and the
      heartbeat's `throwables:` line (counts, peak hand speeds, grenades left). Problems:
      `the game didn't launch the ... grenade within 3 s`, `the weapon didn't come out within 2 s`.

17. **The left trigger opens the squad's weapon wheel; the left grip never does (new, late 2026-10-22;
    mod only, hot reload).** The 19:52 session's wheel openings were all left-grip squeezes right after
    a draw (the foregrip not measured yet). Now ([docs/squad-weapons-design.md](../docs/squad-weapons-design.md)
    "The left trigger opens it"):
    - **How:** on foot, **pull the left trigger**: the game's wheel opens above your left hand at once
      (the game pauses). Point your left hand at a squadmate as you pull: the wheel opens on their
      weapon in hand. Steer with the right stick; **let go of the trigger** to give the highlighted
      weapon (A also picks, B closes without). A quick pull and let go opens and shuts it.
    - **The left grip:** squeezing it anywhere away from the gun does nothing at all (no wheel, no LB).
      On the gun it's the two-handed grip (step 18).
    - **Also check:** a quick trigger tap never swaps Shepard's weapon (the game sees no LB on foot);
      in the Mako the left trigger is still the boost (LB); a trigger pulled in Halcyon's menu (the laser
      pointer's click) never opens the wheel. Custom style (`[Squad] style = custom`): the same trigger
      opens our drawn menu at once (at nobody: the squadmate you gave a weapon last).
    - **Needs** `[Controls] leftTrigger = lb` (your file has it); otherwise logged once: `squad: the left
      trigger opens the squad's weapon wheel only with [Controls] leftTrigger = lb ...`.
    - **Logs (ours):** `squad: the left trigger opens the squad's weapon wheel (no LB to the game)` when
      you're on foot (and `...is the game's LB again` in the Mako or a menu), `squad: left trigger
      pulled: pointing at <name> (...)` / `pointing at nobody (nearest ...)`, `squad: wheel #N: opening
      the game's weapon wheel, pointed at <name> (... the left trigger, at once)`, `squad: wheel #N:
      trigger let go: giving <name> the <weapon>`, `squad: wheel #N: closing (ExitWeaponWheel ...);
      queued: ...`. No `squad: left grip squeezed away from the gun` lines any more.

18. **The two-handed grip takes the gun anywhere along it (new, late 2026-10-22; mod only, hot
    reload).** The grab zone is a capsule along the gun, from 10 cm ahead of your right hand to 10 cm
    past the muzzle, 18 cm around (it was a 12 cm ball at the foregrip). Shepard's left hand still goes
    to the gun's foregrip, and the gun doesn't swing when you take hold: from then on it turns with
    the line between your hands.
    - **How:** draw the assault rifle; squeeze the left grip with the left hand anywhere on or near
      the gun: under the barrel, near the muzzle, beside it. Each should take the gun (aimed fire at
      the shoulder as before). It lets go when you open the grip or move the hand 26 cm off the gun.
    - **Right after a draw:** reach for the foregrip at once. A redrawn gun is now aimed right away
      (its grip is remembered: `weapon: ...: grip as measured the last time it was held`), so the
      zone is there within about 0.1 s of the gun reaching the hand. A squeeze that comes before that
      does nothing; squeeze again.
    - **What to check:** grabs that still miss (tell Claude where the hand was); a gun that jumps when
      you take hold (it shouldn't now); where Shepard's left hand sits on the gun; the pistol (cupped
      under the grip).
    - **Settings** (`[Controls]`, your file migrated: `supportAcquireM` 0.12 -> 0.18 and
      `supportReleaseM` 0.22 -> 0.26, as they were the old defaults): `supportZone = capsule` (`point`:
      the foregrip alone), `supportAcquireM` (the zone's radius), `supportReleaseM`, `supportFrontM =
      0.10`, `supportPastMuzzleM = 0.10`.
    - **Logs (ours):** `hands: the two-handed grip's grab zone is a capsule along the gun now: ...`
      (once, the migration), `controls: two-handed grip on (a capsule along the gun from 0.10 m ahead of
      the right hand to 0.10 m past the muzzle, and the foregrip, grab radius 0.18 m, lets go past 0.26
      m, ...)`, `support grip: held (N m from the grab zone, N m from the foregrip, N m along the barrel
      from the right hand; ...): the barrel turns with the line between the hands (taken where you
      grabbed: no swing)`, the heartbeat's `support grip: ... last write N m from the grab zone (N m
      from the foregrip, N m along), ... two-hand aim (the grab's line), swing N degrees`.

19. **The sight lines up with your eye (new, late 2026-10-22; mod only, hot reload).** Two changes
    ([research/21](../research/21-sights-and-scopes.md) section 8):
    - **Near your face the gun sits on the controller:** your arm-length calibration (`armLengthM
      0.723`) pulled Shepard's hands 24% toward his shoulder, which put the sight about 5 cm right of
      where your eye was (the "move the rifle a long way sideways"). Within 30 cm of your eyes a hand
      now follows its controller 1:1 (`[Hands] reachNearFaceM = 0.30`, fading back over 15 cm).
    - **Alignment assist:** raise the gun to your face with your aiming eye roughly behind the sight
      (within 10 degrees of its line): the gun slides, at most 10 cm and smoothly, so the sight line
      runs through that eye; lower it and it eases back. Shots follow the gun as drawn.
    - **How:** pistol (red dot), then the assault rifle (holo), then the sniper rifle (scope): bring
      each up to your eye and look through. The dot or the scope picture should appear without hunting
      sideways. Then hold the gun out and lower it: it should drift back without a jump.
    - **What to check:** whether it feels like the gun is pulled (too strong? `assistStrength = 0.5`);
      whether 10 degrees catches too much or too little (`assistConeDeg`); any wobble while aiming; the
      gun at the cheek now where the controller is (with the calibration on).
    - **Settings** (`[Sights]`, added to your file): `assist = 1`, `assistConeDeg = 10`, `assistStrength
      = 1`, `assistMaxM = 0.10`, `assistRangeM = 0.65` (the sight within this of the eye); `[Hands]
      reachNearFaceM = 0.30` (0: always scaled).
    - **Logs (ours):** `sights: alignment assist on: the right eye N cm left of and N cm above the
      sight line (N degrees, the sight N m ahead): the gun slides N cm toward it`, `sights: alignment
      assist off (the gun lowered ...)`, the heartbeat's `sights: ...; alignment assist on N ticks: the
      aiming eye sat on average N cm right of and N cm above the unassisted sight line (N degrees); the
      gun moved at most N cm` (**a steady offset there means the sight's place on the gun is off: tell
      Claude the numbers**), and `hands: ... hands within 0.30 m of the eyes unscaled`. Snapshot JSON:
      the mod's `"sights"."assist"` (eye, angle, offset, goal, shift).

20. **No cover snapping; crouch for real (new, 2026-10-22; the mod hot-reloads, no host redeploy).**
    Research and design: [research/23](../research/23-cover.md). ME1 takes cover by itself when you
    walk into a wall or low cover (there's no cover button: A only leaves it) and its cover camera is
    third person; in VR on foot that's now off, and your real crouch crouches Shepard.
    - **Walls and low cover:** on foot, first person, weapon out and away. Walk straight into a wall,
      then into a waist-high crate or barrier, with the stick pushed into it for a few seconds; also
      sideways along it, and with your head turned toward it. Shepard should **never snap into cover**
      and the view should stay first person (he just stops at the wall). Log, once per push:
      `posture: refused a cover entry: high (standing) / low (crouching) cover ahead, the move N
      degrees off its normal`.
    - **A still does everything else:** open a door, talk to someone, pick up an item, use a
      terminal or an elevator, get into the Mako, all with A. **Hold A** while walking forward:
      Shepard sprints (also right after walking into a wall).
    - **Crouch:** behind a crate, crouch down for real (lower than about three quarters of your
      standing eye height) and stay a moment: **Shepard crouches** (log: `posture: physical crouch:
      eyes N m = N of the standing N m, below 0.75 for 0.20 s` then `posture: Shepard crouches (...):
      BioPawn.ShouldCrouch(true)`). Stand up: he stands (`... Shepard stands`). Does the view feel
      right at that moment, or does the world jump up (the game's crouch camera is 45 units lower in
      combat on top of your own lean)? About a second later the log says how far the view moved:
      `posture: 0.75 s after the crouch: the view is N units above Shepard's origin (was M: -D)`.
      **Tell Claude how it felt**; that number decides whether the view needs fixing.
    - **Crouch-walk** with the weapon out (ME1 allows it in combat). With the weapon away, moving
      stands Shepard up (ME1 does that); the log says `Shepard's crouch changed without us`, and he
      crouches again only after you stand up and crouch again.
    - **L3 still toggles crouch:** standing, click L3: he crouches and stays crouched; click it again
      or crouch and stand for real: he stands. Crouching for real, click L3: he stands; your next
      stand-up and crouch take over again. Sprinting while crouched: the crouch waits for the sprint
      to end (`posture: the physical crouch waits: Shepard is sprinting`).
    - **Elsewhere nothing changes:** conversations, menus, the Mako (crouching in a conversation does
      nothing). Off the headset for a few seconds while crouched: Shepard stands up (`VR is gone`).
    - **Already in cover** (only if it happens, e.g. a save taken in cover): Shepard leaves it by
      himself, as A does (`posture: Shepard is in ... cover ...: leaving it as A does`).
    - **Snapshot** (End or the stick tap) while crouched: the mod status's `posture` has the eye
      height, the standing eye height and its source, the fraction, physical and game crouch, and the
      cover counts.
    - **Settings:** `[Hands]` in `Binaries\Win64\ASI\LE1MEVR.ini` (added to your file): `noCover = 1`
      (0: the game's cover as before), `physicalCrouch = 1`, `crouchBelow = 0.75`, `standAbove =
      0.85`, `crouchDwellS = 0.2` (fractions of your standing eye height; seconds).
    - **Logs (ours):** `hooked ABioPawn::TryAcquireCover` (with `cover: the cover test at ... (entry
      bytes ...)`), `posture: script functions found`, `posture: settings: ...`, `posture: active`,
      `posture: standing eye height N m, from the floor (STAGE)` (or the fallback: `the head's drop
      since the recenter over [Holsters] eyeHeightM (...)`), the crouch and stand lines above, and the
      heartbeat's `posture:` line (tests and entries refused, crouches, stands, changes without us).
21. **The world stays level while you shoot (new, 2026-10-22; the mod only, it hot-reloads).** The
    tilt you saw was the guns' recoil: LE1 kicks the game camera up with every shot and the rifles'
    kick never comes back (a flat-screen player pulls the stick down); by the end of that session the
    camera looked about 30 degrees up. [research/24](../research/24-view-level.md). Now, on foot with
    the hand aiming, the camera is held level; the recoil is a tap in your hand instead.
    - **Test:** recenter (R) looking straight ahead. Find a long straight edge (a walkway, a wall's
      base) and note where the horizon sits. **Fire a rifle for about a minute** (bursts, hip and
      two-handed at the shoulder). The horizon stays where it was; no slow tilt. Each shot taps the
      right controller (the left too while both hands hold the gun). Then take an End snapshot.
    - Also: go into cover and shoot out of it, get in the Mako, talk to someone, look through the
      sniper scope: those keep the game's own camera as before; coming back to walking levels the
      camera at once (one logged snap, not a slow turn).
    - **Settings:** `[Hands] levelCamera = 1` (0 = the old behaviour: the tilt comes back) and
      `recoilHaptic = 0.5` (0..1; 0 = no tap), added to your `LE1MEVR.ini`. Try `levelCamera = 0` for
      a minute of firing to see the old tilt (the log then says `kept (off ...)`).
    - **Logs (ours):** `view level: handlers on PlayerController.PlayerTick, ...` at startup;
      `view level: holding level (camera mode SFXCameraMode_Combat, ...)` and the other states (`in
      cover`, `a camera mode with its own pitch`, `not on foot`...); while firing, about once a second:
      `view level: the game moved the camera's pitch +0.62 deg (...) in 1.1 s (6 shots, recoil queued
      +0.60 deg); ControlRotation as the game left it: pitch ..., yaw ..., roll ...; put back to level`;
      `view level: first shot seen (...)`; `view level: took hold of a camera pitched ...: levelled at
      once`; and the heartbeat's `view level:` line: the last minute's drift (recoil and the rest),
      min/max pitch, what the hold put back, shots, `PlayerTick posts` (must count up; else `view
      level: no PlayerTick seen ...` and it levels a frame late) and `zoom snaps dropped`. Snapshots
      have `"viewLevel"` in the mod's status.

22. **Calibrate your aim, hands and body (new, 2026-10-22; HOST REDEPLOY: services v15).** A guided
    minute in the headset: stand tall, arms out (a T), arms ahead, crouch, then grab an orb for a pistol
    and aim it naturally at three targets, then two orbs for a rifle, shoulder and aim. Where you aim
    becomes where the guns' sights sit for your eye, the barrel's angle in your hand (instead of the
    controller's aim pose), the left hand's place on the rifle, "at the shoulder", and the crouch
    height. [research/25](../research/25-calibration.md).
    - **Start it:** stand somewhere safe and roomy, on foot. Open the menu (`\` or both sticks held),
      VR hands tab, **Calibrate** (the menu closes). Or, the first time with these controllers, a panel
      asks "CALIBRATE THESE CONTROLLERS?" after about 8 s on foot: point at CALIBRATE NOW and pull the
      trigger (NOT NOW won't ask again for them).
    - **Do it:** follow the panel. Nothing needs a button: hold each pose still until the ring fills (a
      tap in the hands says it's taken). For the aims, look at the lit target and aim the orb the way you
      naturally aim a pistol (then a rifle at your shoulder). If three aims disagree (3 degrees for the
      pistol, 4.5 for the rifle) it asks again for the one that was off (step 26). **B** goes back a step (or the last target); **both sticks held** cancels.
      The buttons answer a controller pointed at them and the trigger (SKIP, REDO THIS STEP).
    - **Check, then save:** squeeze the right grip and aim at a target: a red dot shows where the new
      pistol sight line meets the board; both grips: the rifle's. It should sit on what you aim at. Try
      SWITCH EYE if it sits beside it (the panel says which eye it measured). Then SAVE (or REDO BODY /
      PISTOL / RIFLE). Take an End snapshot (or tap both stick clicks) during the check, with the dot on a
      target: the snapshot has the board, the dot, the panel and the numbers.
    - **Then play:** draw the pistol: its sight should sit in front of your eye when you aim naturally,
      without the assist having to slide it (`[Sights] assist = 0` for an A/B). The rifle the same at the
      shoulder; look through the sniper scope (the complaint was that it wasn't lined up). Grab the
      rifle's foregrip with the left hand where you held the far orb: it should take every time (the log
      says `at your calibrated foregrip place`). Crouch: Shepard crouches about halfway down to your
      measured crouch. Then switch controllers (Quest, if you have it at hand): it asks again for them, and
      switching back keeps the PSVR 2 one.
    - **Settings:** the VR hands tab: Calibrate again, the aiming eye for the next calibration
      (measured, left, right) and "Use the calibration" (`[Calibration] use = 0` = everything as before:
      an A/B for the guns). The results are `[Calibration.<controllers>]` in your `LE1MEVR.ini`.
    - **Logs (ours):** `calibration: controllers ... (section [Calibration.<key>]): not calibrated` (the
      key: profile and headset); `calibration: started`, `calibration: step ...`, one line per capture
      (`standing: ...`, `T pose: ...`, `arms forward: ...`, `crouched ...`, `pistol aim N taken`), the
      solves (`calibration: pistol: right eye (measured), spread 1.20 deg, trim (...), sight line ... m from
      the grip ...`), `saved to [Calibration....]`. The mod: `calibration: the player is calibrating: ...
      stand down`, `calibration: [Calibration.<key>] (...): right eye; pistol trim ...`, and when a gun is
      drawn `calibration: the held gun follows the calibrated pistol stance: its sight moves N cm up and M
      cm left onto your sight line`. Snapshots: the host's `"calibration"` (step, samples, stances) and
      the mod's status `"calibration"`.

23. **The stick turns you at its own speed near enemies (new, 2026-10-22; the mod only, it
    hot-reloads).** ME1's controller aim assist used to slow the right stick's turn while an enemy was
    near the middle of the view (friction) and to turn the view after a target while either stick moved
    (adhesion: strafing past an enemy turned the world). In VR the hand aims, so on foot both are off
    now. [research/24](../research/24-view-level.md) section 7.
    - **Test:** in a fight, put an enemy straight ahead of your body and **turn with the right stick
      past them**: the turn keeps the same speed as anywhere else (no sticky slow-down over the enemy).
      Then **strafe left and right with the left stick** with an enemy ahead: the world doesn't turn to
      follow them. Shooting with the hand is unchanged. Take an End snapshot during the fight.
    - Also: in the Mako and through the sniper scope aim assist is the game's as before (the log says
      `the game's friction and adhesion (not on foot)` / `(a camera mode that keeps them ...)`).
    - **Setting:** `[Hands] aimAssist = 0` (added to your `LE1MEVR.ini`); `1` brings the game's friction
      and adhesion back under hand aim, for an A/B: with it the turn should drag over enemies again.
    - **Logs (ours):** `view level: handlers on ...; aim assist from SFXModule_AimAssist.ViewFriction and
      ViewAdhesion` at startup; `aim assist: friction and adhesion removed (the hand aims; ...) (camera
      mode ...)` and the other states when they change; `aim assist: first friction seen (...): N% of the
      stick's turn taken away ...; removed` and `aim assist: first adhesion seen (...)`; and the heartbeat
      line under `view level:`: `aim assist: ...; last 60 s: friction would have taken N% of the stick's
      turn on average (M% at most) in T ticks with a target in the soft margin, K of them while the look
      stick turned; removed in R; adhesion would have turned the view in A ticks (yaw ... deg, pitch ...
      deg in all); removed in B; ... ViewFriction and ... ViewAdhesion calls`. The calls must count up in
      a fight (else the hand isn't aiming yet, or no gamepad input reached the game). Snapshots have
      `"viewLevel": {..., "aimAssist": {...}}`.

24. **Colour per headset (new, 2026-10-22; HOST REDEPLOY).** The Quest's flat, washed-out picture gets a
    gentle correction of its own: a little more contrast and colour, slightly deeper mid-tones, on the world
    and the HUD. The PS VR2 stays exactly as before. [research/27](../research/27-colour.md).
    - **PS VR2 first (nothing should change):** play a minute. The picture is the same as yesterday. Halcyon's
      log has `[MEVR-COLOUR] headset: runtime 'SteamVR/OpenXR', system 'SteamVR/OpenXR : playstation_vr2' ...
      automatic: neutral` and `[MEVR-COLOUR] profile auto (= neutral) ...: neutral: no correction`.
    - **Then the Quest 3 (Steam Link, or Link):** the log names the headset (the line Claude needs: which system
      name SteamVR gives Steam Link) and says `automatic: quest` and `correcting the eyes and the HUD panel`. In
      a lit room and a dark one (the Normandy's lower deck, a night area), open the menu (`\` or both sticks
      held), VR hands tab, **Colour** at the bottom: **hold "Hold to compare"** with the trigger: the picture
      goes back to uncorrected while held. Which looks better: corrected or not? Do dark corners lose detail?
    - **Tune it:** the profiles (Automatic, Neutral, Quest streamed, Vivid, Custom) apply at once; the sliders
      (brightness, contrast, saturation, mid-tones: below 1 darker, black level: below 0 deeper, red/green/
      blue) make your own values for this headset (saved as `[Colour.<headset>]` in `LE1MEVR.ini`, kept apart
      from the PS VR2's). Tell Claude the values you end up with. Try Vivid too.
    - Take an End snapshot (or tap both stick clicks) with the correction on and one while holding compare:
      the PNGs show what the headset got; `"colour"` in the JSON has the profile, values and cost.
    - **Settings:** `[Colour] profile = auto | neutral | quest | vivid | custom` (for any headset; a headset's
      own section wins), `compare = 1` (uncorrected, for an A/B without the menu), `dither = 0.5` (8-bit steps,
      0 off).
    - **Logs:** ours `colour: headset: ...`, `colour: profile ...` on each change, `colour: comparing ...` /
      `comparison let go`, and every 10 s while correcting `colour: GPU time per draw, last 10 s: eye blit with
      the correction X ms ...` (Claude wants that number; hold compare a few seconds to get the plain blit's
      for the difference); `colour: corrected variants of Halcyon's eye shaders ready` at the start of VR (or an
      error: then nothing is corrected).
    - (Since step 25: `\` and both sticks open our new menu; the Colour controls are on its **Display** page,
      "hold A" compares. The VR hands tab is still in the classic menu: **Ctrl + `\`**.)

25. **Our new VR main menu (new, 2026-10-23; HOST REDEPLOY: services v16, and `ASI\menu` deployed).** The
    approved glass board replaces Halcyon's ImGui menu: frosted panels over the game, a section rail (Comfort,
    Controls, Body & calibration, Display, HUD & sights, Advanced), rows with toggles, sliders and chips.
    [research/28](../research/28-main-menu.md).
    - **Open and close:** hold both stick clicks (about half a second), or press `\`. It appears about 1.3 m
      ahead, level, and stays put in the room (turn your head: it doesn't follow). **Resume**, **B**, `\` or
      both sticks again close it. On foot the game pauses while it's open (Comfort > "Pause while open";
      the log says `menu pause: the game paused`). The game must not react to anything while it's open: the
      sticks, the trigger, A/B, the keyboard and the mouse do nothing in the game.
    - **Point and click:** point either controller at it: a blue dot follows the ray (orange while the trigger
      is held), each control under it gives a small tick in the hand, a click a firmer one. The right stick
      scrolls the long pages (Controls, Body, Advanced). Hover the orange **Resume**: a light sweeps across
      it; press a button: it dips.
    - **Pad:** with a gamepad (or the left Touch stick) move the focus (a cyan ring) up, down, left and
      right; A presses, B goes back to the rail, B again closes. On the Display page left/right on a focused
      slider moves it.
    - **Every row does something real (or says Soon):** change a few and close the menu, then check they hold:
      Comfort > Physical crouch off (crouching no longer crouches Shepard); Controls > Left trigger "Aim" (LT
      is the game's aim again) and back to "Weapon wheel"; HUD & sights > Distance and Size move the HUD panel;
      Sniper rifle "Holo"; Display > Colour profile and the sliders (hold **A** on the Display page: the
      picture goes back to the original while held; the test strip on the right shows the adjusted one);
      Body > Recenter; Advanced > Stereo > Eye distance (the world's scale), Advanced > First person. Open
      `LE1MEVR.ini`: only the keys you changed changed (no rewrite); `MELEVR.ini`'s active profile has the
      renderer changes after the menu closed (log `main menu: renderer settings saved to profile ...`).
      Turn style, Turn speed, Comfort vignette and Walk toward "Left hand" are marked Soon and do nothing.
    - **Body & calibration:** the tiles show your saved calibration (eye heights, arm length, aiming eye,
      spreads); **Start calibration** closes the menu and starts the guided calibration (step 22). **Measure**
      (arms only): arms out in a T, pull a trigger: the row says how far apart the controllers were. **Align**
      closes the menu; stand naturally and pull the left trigger (as the VR hands tab's alignment).
    - **The glass:** the panels should look frosted: the game behind the menu blurred, darker and less
      colourful, updating about ten times a second as the world moves (walk around with the menu open and
      the game unpaused: Comfort > Pause while open off). Over a flat screen (a game menu, a cutscene on the
      flat screen) the glass is plain dark.
    - **Developer fallback:** **Ctrl + `\`** (or Advanced > Diagnostics > Classic renderer menu) opens
      Halcyon's old menu as before.
    - **Cost:** with the menu open and the pointer moving, the log says every 10 s `main menu: open N s: X ms
      CPU per frame, Y ms GPU per frame, R renders/s, B backdrops/s` (Claude wants those numbers). Closed it
      costs nothing (no line, no layer). A perf probe (Advanced > Diagnostics > Measure frame timing closes
      the menu first) should match the numbers without the menu.
    - **Snapshot:** with the menu open take an End snapshot (End on the keyboard, or Diagnostics > Snapshot,
      which closes the menu first): `<stamp>-mainmenu.png` is the menu as submitted and `"mainMenu"` in the
      JSON has its place, the pointer and the cost.
    - **Look while it runs:** `assets/menu/main.rcss` and `main.rml` can be edited live: point `[Menu]
      assets` in `LE1MEVR.ini` at the repo's `assets\menu` folder, open the menu, save a change: it reloads
      within a second (log `menu: main.rml or main.rcss changed: reloaded`). `[Menu] distanceM` and `widthM`
      move and size it (next opening).
    - **Logs (ours):** `main menu: ready in N ms (1440x900 canvas, documents ..., fonts ...)` at the first
      opening (or an error naming what failed: the documents not deployed, the fonts), `menu: RmlUi 6.3
      ready ...`, `main menu: placed 1.30 m ahead ...`, `main menu: the backdrop samples the left eye's image
      (WxH, format F)`, `main menu: opened (both sticks held)` / `closed (B)`, one line per setting written
      (`main menu: [Hands] physicalCrouch = 0`), `main menu: renderer settings saved to profile ...`,
      `rmlui:` warnings if the document has a mistake. The mod: `menu pause: the game paused (the main menu is
      open; ...)` and `runs again (the main menu closed)`.
26. **The rifle calibration finishes, and the orbs are where your hands are (new, 2026-10-23; HOST
    REDEPLOY).** The rifle step used to go round and round: it took three aims, judged them by the pistol's
    3 degrees, and started all three over without saying why where you could see it. Now it says why on the
    panel and across the top of the target board, redoes only the aim that was off, and after two tries keeps
    the two that agree. A ring marks each hand while it has no orb. [research/25](../research/25-calibration.md)
    section 9.
    - **Start:** the VR hands tab's Calibrate (or the main menu's Body > Start calibration); SKIP to the rifle
      (or do the body first: the far orb is then placed for your arms).
    - **The rings:** at the grab steps a small cyan ring follows each hand. Does it sit in your palm, where
      you feel your hand? Say where it sits if not (ahead of the palm, behind it, under it). Then take the
      orbs: does each one take when your hand (the ring) is on it, with no reaching? Is the far orb a
      comfortable foregrip distance from the near one?
    - **The rifle aims:** shoulder it naturally, right hand by your cheek, left hand where you'd hold a
      foregrip (below the sight line is fine). Watch the top of the target board: it says what's missing
      (LOOK AT TARGET 2, STOCK TO YOUR SHOULDER, POINT THE RIFLE AT TARGET 2, LEFT HAND FURTHER OUT, SQUEEZE
      BOTH GRIPS) or HOLD STILL while the ring fills. Let go of the left grip for a moment mid-aim: it waits
      ("Squeeze the left grip again") instead of sending you back to the orbs.
    - **A redo:** aim one target a little off on purpose (point a hand-width beside it): after the third aim
      the board should say AIM N AGAIN and only that target lights; aim it properly and the step finishes.
      Do it twice in a row: it keeps the other two, and the check screen says "Rifle: two aims within ...
      (aim N left out ...)".
    - **The pistol** behaves the same (3 degrees).
    - **Snapshot** during the rifle grab (End, or tap both stick clicks): `calibration-hand` quads at the
      hands, the orbs, and the JSON's `"calibration"`.
    - **Logs (ours):** `calibration: started ...; grab point 0.0000, 0.0000, 0.0000 m from the grip origin ...;
      rifle spread limit 4.5 deg`, `calibration: rifle orbs: near (...), far (...): 0.28 m along the bore (arm
      ...)`, a line per grip press (`left grip squeezed, out of reach at rifle-grab: grab point ..., the far orb
      ... m away (reach 0.22); the orb ... right, ... up, ... ahead of the hand`, or `takes`), `rifle aim N: not
      capturing: <reason> (gaze, right hand from the eyes, hands apart, their line off the target)` (every 2 s
      at most), `rifle aim N: the hold broke after ... s: the left hand moved ...`, `rifle aim N taken`, `the
      rifle aims judged: accepted | redo-one | redo-all | accepted-pair (spread ..., farthest aim ...)`.
      Claude wants these lines: the reasons say which threshold is too strict, the grab lines whether the
      grab point needs `[Calibration] grabOffset`.

27. **Shepard's body stays under you (new, 2026-10-23; the mod only, it hot-reloads).** Your report: after
    turning your head and walking, the shoulders seemed somewhere else. Five causes, all fixed
    ([research/29](../research/29-body-follow.md)): Shepard turns to where he runs (backing up, he faced you),
    the camera sits up to 25 cm from where the animation put his head, physical steps never moved him, turning
    your head leaned his spine, and a recenter left his body turned. Now his whole body (not the camera bones)
    turns and steps under your view, and his spine only leans the last 20 cm. First person (K), on foot.
    - **Turn and walk:** stand still, turn your real body (and head) 90 degrees right, wait a second, look
      down: chest, hips and legs face where you face. Then walk with the stick sideways and backward (Shepard
      would run that way): the shoulders keep facing your way, the arms come from your sides; his legs may
      look like they run sideways (a known look, tell Claude if it bothers you).
    - **Head turn only:** keep the body still and turn just your head left and right, nod: the shoulders don't
      sway.
    - **Step:** take a real step or two forward, back, to the side, and stand: Shepard steps with you and
      straightens up within a few seconds. Lean over something without stepping: his upper body leans. Walk
      more than about 0.75 m from where you recentered: his body stops following (the log says capped); press
      R (or double-click the right stick) and it comes back under you.
    - **Recenter** after turning around: the body faces forward again at once.
    - **Arms:** with a gun out, reach forward, to the sides and up: the elbows bend from where your shoulders
      are; no arm from the middle of the chest.
    - **Judder check:** none of this may move the view (the camera bones are never written). If the view
      shakes when you step or turn, `[Hands] bodyMove = 0` in `LE1MEVR.ini` brings back the old spine-only
      body at once (no restart): tell Claude.
    - Take an End snapshot (or tap both stick clicks) while it looks wrong, and one while it looks right.
    - **Settings (`[Hands]`):** `bodyMove = 1` (0: the spine alone, as before), `leanM = 0.20` (how far your
      head may lean before his feet step), `bodyMoveMaxM = 0.75` (how far his body may follow you from where
      the game put him).
    - **Logs (ours):** `torso: re-anchored (start | recenter | tracking jump): ...` (at a recenter, after a
      cutscene or menu); `torso: shoulders turned off the player's | head off the view | body slide capped |
      large correction (...): head yaw ..., body yaw ..., pawn ..., ControlRotation ..., camera ... deg; turn
      wanted T = body X + spine Y (short Z) ...; the view D m from where the game put Shepard, body slid S m;
      Shepard's head G m from the head's pivot; lean L m ...` (at most every 2 s; "large correction" lines
      while backing up or after a step are expected, "short" should stay near 0); the heartbeat's `torso:`
      and `arm IK torso` lines (`body turn N of M deg, body shift N units`). Snapshots: `"torso"` in the mod's
      status.

28. **Shots on the dot, a red dot on the rifles, the sight on the model (new, 2026-10-23; HOST REDEPLOY: the dot's
    eye box and the sight struct; the rest is the mod's and hot-reloads).** [research/21](../research/21-sights-and-scopes.md)
    sections 9-12. Know first: Halcyon's Stereo 2 moves each eye's picture about 1.27 degrees outward and renders the
    eyes 3.4 cm apart each, while our dot and scope are drawn at their true place; life-size stereo (step 29, on
    in the deploy) fixes that. Until it's on, a right-eye dot reads about 1.27 degrees left of the hits (20-40 cm right
    at 10-20 m). The stopgap, Halcyon's settings: Advanced > Stereo > Eye distance 3.1 and Convergence 0.
    - **Pistol at a wall 10-20 m away:** raise it to your right eye, put the red dot on a mark, fire 5-10 single
      shots: the group should sit on the dot's height (before: a few cm low at every distance), sideways as above
      (right) unless the metric stereo (or the stopgap) is on, then on the dot. Take a snapshot (both stick clicks)
      while the dot is on the mark.
    - **Assault rifle and shotgun:** a red dot now (not the holo; an old settings file switches once: log `hands:
      [Sights] shotgun and assaultRifle holo (the old default) -> dot`). Raise the gun to the cheek: the dot should
      appear without hunting for it (the eye counts within 6 units of the window's centre, `[Sights] eyeBox`), to one
      eye only. Fire bursts at a mark.
    - **Sniper rifle:** look through the scope: does the round picture sit on the model's scope (its blue lens glow)?
      Take a snapshot while looking through it; Claude reads the model's numbers from the log and sets the place.
      Fire at a mark at 20-40 m and at 5 m: both on the crosshair's height (the shot converges).
    - **Compare:** HUD & sights > "Shots land on the dot" off: impacts a few cm under the dot; back on.
    - **Calibration without the rifle step:** the long guns now use the pistol stance's grip angle (log
      `calibration: ... rifle not measured ... (the long guns take the pistol's grip frame, not its sight line)`): do
      the rifles point where your hand points?
    - **Logs:** per gun `sights: the <gun> model against its bore (gun units): bounds ...; Root (...); socket ...` and
      `sights: the <gun> has a red dot sight: ... (auto: the length share, the height from the model's top) ...`;
      `sights: the sight line's trace (Actor.Trace through ProcessEvent): the hit point came back`; `[MEVR-SIGHT]
      first red dot reticle: the right eye looks through the window (..., eye box 0.0600 m)`; per shot `hand aim: shot
      N: from the muzzle (0.00 units from the game's start) toward what the sight line hit, D m past the sight (...):
      A degrees off the sight line`; the heartbeat's `hand aim: ... N of them toward what the sight line hit` and `...
      other weapons' aim queries skipped` (squadmates: no more `a shot along the barrel started 273 units from the
      muzzle` warnings).
    - **Snapshot JSON:** host `"sights"` `"dotEye"`, `"eyeBox"`; mod `"sights"."place"` (`modelTop`, `model`),
      `"sights"."converge"` (`distanceM`, `what`), `"handAim"."shots"."lastShot"` (`toSightDeg`, `startOffMuzzle`
      about 0).

29. **Life-size stereo (new, 2026-10-23; HOST REDEPLOY, the deploy turns it on).** Your reports: the pistol
    looks huge, close things strain the eyes, the scope disc isn't on the model's lens, the orbs looked closer
    than they were. Cause ([research/30](../research/30-metric-stereo.md)): Halcyon shifted each eye's picture
    1.27 degrees outward (its convergence, made divergent by swap-eyes), so everything read further away than
    our metric quads: a pistol at 0.5 m read at 0.68 m, far things beyond infinity. Now each eye renders from
    where it really is, at the world's own scale, with no shift. Stereo 2, on foot, first person (K).
    - **Look at things:** hold the pistol at your face, then at arm's length; walk up to a wall, a crate, a
      squadmate. Do sizes look right (a pistol pistol-sized, a door door-sized)? Close things easier on the
      eyes? Far scenery comfortable (it no longer asks your eyes to diverge)?
    - **Turn your head** 45-90 degrees to the side and look at something close: depth stays the same (it used to
      flatten as you turned). Tilt your head sideways: no double image.
    - **The sights:** draw the pistol and the sniper rifle: the dot and the scope picture should sit on the
      model's sight and lens, and shots land on the dot (the 1.27-degree drift to the right should be gone).
    - **Calibration orbs** (VR hands > Calibrate): the orb and the ring sit where your hand feels it is.
    - **A/B:** our menu, Advanced > Stereo > Life-size stereo off brings Halcyon's old picture back at once (its
      Eye distance, Convergence and Swap eyes reappear); on again. Which is better? "Extra depth" (1.00x = your
      own eye distance) is there to taste.
    - **Side effects:** (a) the HUD panel, reticle, the weapon wheels, the squad menu: unchanged? (b) the sun,
      lights, lens flares, light shafts: anything glowing in the wrong place in one eye? If so,
      `sfr2MetricEyeFrustum = 0` in `MELEVR.ini` (each eye then renders Halcyon's shared window, still
      life-size) and tell Claude. (c) Conversations and cutscenes: faces, the wheel and subtitles fuse?
      (d) The edges of the view: nothing missing or popping in at the sides (and no `[MEVR-STEREO] WARNING ...
      WIDER than the CPU's culling window` in the logs). (e) Effects that rebuild positions from the screen:
      fog and haze (bands or edges that slide with your head or differ between the eyes), ambient-occlusion
      shadows offset from the objects they belong to, light shafts or flares away from their light. Close one
      eye, then the other: do they sit in the same place on the world? If not, `sfr2MetricEyeFrustum = 0` and
      tell Claude which effect (research/30, risks).
    - **Snapshots:** one on far scenery (End, or a quick tap of both stick clicks) with life-size stereo on and
      one with it off, standing still. Claude checks them with `python tools/stereo-check.py <stamp> x,y ...`
      (far points: about 0 px when metric, -30 px or so when off).
    - **Frame timing:** `tools/perf-probe.ps1 -Tag metric`, then switch it off, `-Tag upstream`: expect no
      difference.
    - **Settings (`MELEVR.ini`, every profile):** `sfr2Metric = 1`, `sfr2MetricDepth = 1.00`,
      `sfr2MetricEyeFrustum = 1`. Life-size needs Life-size movement on (lean gain 2; your active profile has
      it); at lean gain 1 the world reads twice its size and the log says so.
    - **Logs (Halcyon's and ours):** `[MEVR-STEREO] stereo mode 2: life-size, each eye's own frustum; eye
      distance 0.06xx m = 6.xx uu at 100.0 uu/m ...` (mode 1 in conversations, cutscenes and with the HUD panel
      off, with the reason); `[MEVR-STEREO] submit (pair rendered in mode 2): left eye declared at ... fov
      (-61.5, 43.4, 53.0, -53.0) deg ...`; in Halcyon's: `[MEVR-STEREO] pass 0 (eye 0) from its eye: right
      -3.1x ...` and `[MEVR-STEREO] pass 0 frustum: ... (game's own p8 0.0000 p9 0.0000 p11 1.0000)` (other
      values there: tell Claude). `[VIEWFILL_RENDER_MATCH]`'s subRect should now be the whole 2824 px eye.
30. **Comfort: snap and smooth turning, the vignette, walking toward the left hand (new, 2026-10-23; HOST
    REDEPLOY, services v17).** Our menu's Comfort page ([research/31](../research/31-comfort.md)). On foot, first
    person. Smooth (the default) is the game's own turn, unchanged, with no speed setting
    (its rate can't be set); `[Comfort] turn = smooth` in `ASI\LE1MEVR.ini` tries the mod's steady turn (`turnSpeed`).
    - **Smooth:** hold the right stick: the game's turn, as before.
    - **Snap:** Turn style > Snap, 45 deg. Flick right, left: one turn per flick, none while held, the next after
      letting the stick come back. 30 and 60. **Look at your hands and shoulders at the moment of a snap,
      standing and walking, gun drawn (combat) and holstered (explore):** they must not swing round or lag a
      frame. Snapshot right after a snap (End, or a quick tap of both stick clicks) and say what you saw.
    - **Where it must not turn:** the Mako (the stick steers as before), the weapon and power wheels (the stick
      picks), the squad menu, the sniper scope, cover, conversations, our menu (open it with the stick pushed:
      nothing turns).
    - **Vignette:** Comfort vignette on: the edges darken while turning and fade out after (0.15 s); after each
      snap a short pulse. Vignette when > Turning and moving: also while walking with the stick (not when you
      step for real). Strength 10% to 100%. The HUD, the reticle and the sights stay clear above it. Snapshot
      while turning: the layer list should have `vignette`.
    - **Walk toward > Left hand:** point the left controller left and push forward: you walk left; turn your head
      meanwhile: the walk doesn't change. Pointing the hand at the floor keeps the last direction. The squad
      trigger's pointing and the two-handed grip behave as before. Back to Where I look.
    - **Frame timing:** `tools/perf-probe.ps1 -Tag vignette` while turning with the vignette on, and `-Tag novig`
      with it off: expect no difference beyond a quad.
    - **Logs (ours):** `comfort: settings: ...`, `comfort: the mod turns the player` / `comfort: the game turns
      (<why>)`, `comfort: snap right 45 deg ...`, once `comfort: the pawn faced where the controller does, so it
      turned with it (Actor.SetRotation): its mesh turned +45.0 deg at once` (if it says the mesh waits, tell
      Claude), `comfort: the game's own stick turn in SFXCameraMode_Explore: SFXCameraInput MaxCameraRotationSpeed
      ...` (one per camera mode: the game's own numbers), `halcyon bridge: the mod turns the player ...`,
      `halcyon bridge: the left stick walks toward ...`, `vignette: first shown ...`, `vignette: shown in N frames
      ..., M texture refills (X ms CPU each ...)`, and the heartbeat's `comfort:` line. No `torso: re-anchored
      (tracking jump)` at a snap.
31. **Display: the headset's own resolution and the upscale in our menu (new, 2026-10-23; HOST REDEPLOY).** Our
    menu's Display page ([research/28](../research/28-main-menu.md), "What each section drives"; research/32). Open it in VR, scroll
    to Image quality.
    - **Render resolution:** three chips, Headset / Headset × 0.85 / Headset × 0.75; the line under the label
      says "Now 2824 × 2880 per eye" and "Headset 2824 × 2880 · × 0.85: 2400 × 2448 · × 0.75: 2118 × 2160" on the
      PS VR2. With the deploy's target, **Headset** shows selected.
    - **Fixed sizes:** Halcyon's ladder as before (Low to Max); none of them selected at 2824 × 2880.
    - Pick **Headset × 0.85**: "Now 2400 × 2448"; log `main menu: render resolution headset85 (2400x2448),
      GamerSettings.ini set to 2400x2448`. Then **Upscale > FSR**: log `main menu: [Upscale] method = fsr` and,
      within a second, `upscale: ...`. Restart the game: Halcyon's log `[TARGETCHK] Stereo 2 target OK` at
      2400x2448, and the probe's `"upscale"` active. Look at it against Headset with Upscale Off, and say which
      you'd keep. Halcyon's old menu (Ctrl + `\`) must not show "RESOLUTION WAS RESET" after the restart.
    - Before restarting, a size you picked shows selected; a larger one than the game runs at now may make
      Halcyon's old menu warn "RESOLUTION WAS RESET" until the restart (its watchdog compares with the new size).
    - Put it back: **Headset** and **Upscale > Off** (or keep what you liked), restart.
32. **Everything on our own reflection layer (new, 2026-10-23; the mod only, it hot-reloads; research/34 stage 2).**
    Every game module (holsters, grenades and melee, sights, hand aim, the squad wheel, arm IK, Shepard's body,
    the weapon, the console request) now finds what it reads and calls through our layer instead of LExSDKv2.
    Nothing should look or feel different: this step is a check that it doesn't.
    - **The log, before anything else:** `reflect: ready: ...`, then `reflect: the engine's allocator: appMalloc at
      RVA 0xF2330 (by rva), appFree at RVA 0xF22F0 (by rva), GMalloc at RVA 0x16B6700`, then `reflect: N of N
      checks agree` (410 or so now: 5 new rows). `%LOCALAPPDATA%\MEVR\reflect-check.json` must show
      `"mismatches": 0`; the new rows are `global allocator`, `sample engine strings`,
      `param SFXGame.BioPawn.GetWeaponStartTraceLocation(ReturnValue)` and the two `parms` rows of
      `EnterWeaponWheel` / `ExitWeaponWheel`. No `reflect: field ... not found`, `parameter ... not found` or
      `function ... not found` warnings anywhere in the session.
    - **Smoke test each feature, a minute each, as in its own step above:**
      - holsters (step 12 and later): draw the pistol and the rifle from their slots, let go to holster;
        `holsters: script functions found`, `the pistol is in hand`, `is holstered`;
      - grenades and melee (step 16): take a grenade at the chest, throw it, put one back; strike an enemy
        in reach; `throwables: script functions found`, `grenade launched ... the disc re-aimed`, `melee: hit`;
      - sights (steps 14 and 28): the red dot and the sniper scope, a scoped shot (`sights: first scoped shot
        with aimed fire`), and shots landing on the dot (`hand aim: shot N: from the muzzle ... toward what the
        sight line hit`);
      - hand aim: `weapon: ... muzzle ... vs GetMuzzleLoc ...: match`, `shot start moved to the muzzle`, `the
        shot started at the muzzle`;
      - the squad wheel (step 17): pull the left trigger, point at a squadmate, give them a weapon;
        `squad: script functions found`, `wheel #N open`, `preselected`, `holds the ...`;
      - arm IK and the two-handed grip (step 18): `arm IK: bone layout verified`, the left hand on the rifle;
      - Shepard's body (step 27): turn and lean; the heartbeat's `torso:` line has non-zero pawn, ControlRotation
        and camera yaws;
      - the console request: `tools/console.ps1 "stat unit"` must answer (`console: stat unit -> ...`), and at
        every start `system settings: scale set ...` must appear (it runs through the same engine strings: no
        line there, or `console: the engine's allocator wasn't found`, means the allocator check failed).
    - If any feature says `our reflection layer is off`, or a feature stands down where it worked before, send
      the log: the `reflect:` lines say which lookup failed.
33. **The engine side on our layer too (new, 2026-10-23; the mod only, it hot-reloads; research/34 stage 3).** The
    startup gate, the bones (arm IK, the gun on its socket, Shepard's body), the world's identity, the cover test's
    description and the natives dump now read through our layer; `engine/Objects` is gone. Nothing should look
    or feel different. Do it together with step 32 (one session covers both).
    - **The log at startup:** `reflect: ready: ...` as before (perhaps after one `reflect: not ready yet: ...;
      trying again` if the mod started early: the gate retries each second), then `reflect: N of N checks agree`
      with **423 rows** (410 + 13). `reflect-check.json` must show `"mismatches": 0`; the new rows are
      `global World` (ours must say `(WorldInfo's outer World)`, the same object as GWorld), `global GNatives`,
      `class Engine.World` and ten `skeleton` rows (each field engine/skeleton resolved, against research/14's
      offset: `0x2B0` SpaceBases, `0xB4` RefSkeleton, `0xC0` LocalToWorld, `0x3B4` Attachments, element size
      `0x34`, `0x144` Sockets, `0x60` / `0x6C` / `0x84` / `0x8C` the socket fields).
    - No `skeleton: ... not found through our reflection layer`, `world: ... not found`, `arm IK: Engine.ActorComponent.Owner
      not found` or `cover: Actor.Acceleration ... not found` warnings.
    - The heartbeat's `GWorld=` is non-zero in a level and `objects=` is the GObjects count (as before).
    - **Smoke tests:** arm IK writes (`arm IK writeback: ... written`, no `rejected (last: the world changed ...)`
      while standing in one level; one such reject right after a level load is right); the gun hangs on its
      socket in the hand (`in the right hand: its mesh hangs on Prop01(98)`); with `noCover = 1`, walking into a
      wall logs refused cover entries with a type and an angle (the posture heartbeat line). The skeleton
      probe's `GetBoneNames through ProcessEvent returned 0 names` line has lost its bytecode bytes (our layout
      has no UStruct.Script yet); the rest of it is as before.
    - Load a save (a level change) and check arm IK picks the new pawn up within a second.

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

## Performance levers, built blind (2026-10-23, research/32): what to measure in one session

All in `[Performance]` of `ASI\LE1MEVR.ini`, live within a second; the host is deployed with them (off by default).
Stand still in gameplay (or `tools/console.ps1 pause`) for each A/B; each step is one command from the repo.
1. **The eye diff first** (30 s): `tools/eye-diff.ps1`. Its verdict says whether the draw replay (`sharePass = 2`)
   is right as built: "aligned and fully covered" = go; "structural mismatches" = the replay would issue the wrong
   calls (don't use 2); "not covered" = some constants (listed by shader and offset) would be wrong in the second eye.
2. **`tools/perf-ab.ps1 -Key sharePass -Values 0,1,2 -Rounds 2`**: the game's own / the second eye on the first
   eye's renderer / the draw replay. Then with `sharePass = 1` and `= 2` for a minute each, look: both eyes still
   stereo (`python tools/stereo-check.py` on a snapshot), nothing flickering or missing, shadows and highlights
   agreeing between the eyes. Watch `render levers:` in LE1MEVR.log: "sharePass: N renders on the previous one's
   renderer" and, for 2, "draw replay: N replays ... fell back ...".
3. **`tools/perf-ab.ps1 -Key umbraQueryWait -Values 1,0`**: before it, read the log's `longest results read` in
   the `render levers:` line with the game's own setting: if it's ever tens of ms or more, the Umbra resolve was
   spinning for the GPU; 0 stops that.
4. **`tools/console.ps1 "UMBRA TOGGLEOCCLUSION"`** then a `tools/perf-probe.ps1 -Tag noqueries` (and again to
   toggle back): UE3's own per-primitive query wait, if any, goes away with it.
5. **`tools/perf-ab.ps1 -Key pCoresOnly -Values 0,1`**.
6. If anything looks wrong: set the key back (perf-ab restores it by itself); an exception has already turned the
   lever off for the session (the log says so). Then `tools/sample-render-thread.ps1` once more under the best setting.
0. **Before the levers: the stereo target is now 2824x2880** (the headset's own eye size; it was 3072x3188).
   Halcyon's log should say `[TARGETCHK] Stereo 2 target OK` at that size; if it complains or the picture looks
   wrong, `tools/vr-game-settings.ps1 -Game LE1` puts the old rung back. In SteamVR's per-app settings try
   Throttling "Fixed" with motion smoothing, and 120 Hz (60 real frames, smoothed) against 90 Hz: which feels
   steadier is yours to judge; the probe's numbers are the same either way.
7. **The eye upscale (FSR 1), for the GPU side** (research/32): with the game closed, `tools/vr-game-settings.ps1
   -Game LE1 -Scale 0.75` (2824x2880 -> 2118x2160), then in `ASI\LE1MEVR.ini` add `[Upscale]` with `method = fsr`
   (live; `sharpness = 0.9`). Expect `[TARGETCHK] Stereo 2 target OK` at 2118x2160, `upscale: FSR 1 shaders
   ready` and a `upscale: ... ms of GPU per eye` line in LE1MEVR.log, and the probe's GPU span near 56% of
   before. Compare by eye with `method = off` (the bilinear fill of the same small target) and with the full-size
   target (`-ResX 2824 -ResY 2880` again, game closed). If anything looks wrong, `method = off` is instant.
