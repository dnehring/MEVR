# Next test session

LE1 now renders VR with Halcyon's renderer (since 2026-10-08), with our motion controllers, hand
aim and arm IK on top. Logs, both read by Claude after each step (just note what you saw and
roughly when):
- ours: `<ME1>\Binaries\Win64\Logs\LE1MEVR.log`;
- Halcyon's: `%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt` (settings: `MELEVR.ini` in `Binaries\Win64\ASI`).

Already done for you: `tools/vr-game-settings.ps1` turned VSync, motion blur and film grain off
and HDR to SDR in ME1's GamerSettings.ini (backup kept; `-Restore` undoes it). Halcyon's settings
may also set the game resolution per VR mode (3840x2160 for stereo), effective from the next
launch.

**Keys on LE1:** `\` = Halcyon's menu (Insert upstream) (the controllers navigate it as a gamepad; while it's open the game gets no input; clicking and holding both thumbsticks closes it), R = recenter
(or double-click the right stick), K = first person on/off (off at every launch). Ours: `,` hand
aim on/off and `.` arm IK on/off, both on by themselves once the controllers are tracked; the
other keys in the table further down are for our own renderer only.

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
   - `,` switches to Halcyon's head aim and back. Which felt right?
   - Put the controllers down until they sleep and use a gamepad: head aim should come back.
   - The Mako's cannon should still follow the head.
5. **Arms:** with the controllers tracked, wave your hands, in first person and in third person
   (K). Do Shepard's arms follow? Any tearing at the wrist or elbow, or fingers off the hand?
   Does the weapon follow the hand? `.` turns it off to compare. (The left hand may stay on the
   gun's grip while the weapon is out.)
6. **The gun points where the controller points (new):** first person (K), on foot, draw a
   pistol or rifle, and hold the right hand still for a moment (the mod measures how the gun sits
   in the hand; the log says `grip measured`).
   - Hold the controller normally and point it at something: the gun's barrel should point the
     same way, upright. (The `handPitch = 90` in `LE1MEVR.ini` no longer matters for guns; leave
     it.) Turn and tilt the controller: the gun should follow without lag or jitter.
   - Fire at a wall at different distances: impacts should land where the barrel points, and the
     tracers should leave the muzzle. Note any constant offset and the distance.
   - Wave the gun around fast: does the gun stay locked to the hand (no lag behind it)?
   - Holster and draw again, switch weapons: same behaviour each time? (Each new weapon is
     measured again: hold still a moment after drawing.)
   - Does the reticle (step 7) sit on the barrel's line?
   - `,` (hand aim off) gives Halcyon's head aim back; `.` (arm IK off) returns shots to the
     controller's ray without the gun following.
7. **HUD panel, reticle and the mod menu (new).** Do this in Stereo 2, on foot.
   - **Mod menu.** Press `\` (Halcyon's Insert menu; `\` in our deploy), once at the title screen
     and once in gameplay. Press `\` again or hold both sticks to close it.
     - Is the menu visible now? It's an opaque dark panel, a little nearer than before.
     - If it's still invisible, say so. The log's `[MENUVIS]` lines will show why.
   - **HUD.** The health, squad, radar and subtitles should sit on a panel about 1.5 m ahead,
     a bit below eye level, not stuck to your face.
     - Look around: the panel stays put, and the world shows no HUD in either eye.
     - Turn your body with the stick: the panel stays in front of you.
     - Keep your head turned more than about 50° for a second: the panel swings round.
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
   - **Other screens.** Open the pause menu, the galaxy map and a conversation. They should look
     as before, on the flat screen.
   - **Fades.** Watch for anything full-screen ending up on the panel instead of the world, such
     as a fade, a red damage tint or a flash. Note what and when.

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
- Insert menu: `[MENUVIS]` lines.
  - The ImGui texture and the swapchain image after the copy should both show visible pixels
    (about 100%).
  - `menu copy:` gives both descriptions (a format or size mismatch would drop the copy).
  - `menu quad submitted`.
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
