# Next test session

What to check next time, in order. The log is
`<ME1>\Binaries\Win64\Logs\LE1MEVR.log`; Claude reads it after each step, so just note what
you saw and roughly when.

Already done for you: `tools/vr-game-settings.ps1` turned VSync, motion blur and film grain off
and HDR to SDR in ME1's GamerSettings.ini (backup kept; `-Restore` undoes it).

## First: controller aim with Halcyon's renderer (LE1, 2026-10-08)

The host ASI changed, so close the game and redeploy (`tools/deploy-asi.ps1 -Game LE1`) before
this test; a hot reload alone isn't enough.

1. Start SteamVR, then the game, load a save, and pick up the Touch controllers.
2. On foot, draw a weapon. Look around with your head: the view should follow the head, but
   Shepard's aim should no longer swing with it.
3. Point the right controller at a target away from where you're looking and fire. Do the
   shots go where the controller points? Note any constant offset (left/right, high/low) and
   roughly how far away the target was. The HUD crosshair still sits in the middle of your view
   and doesn't show the controller's aim (known; no reticle yet).
4. Press `,` to switch to Halcyon's head aim (shots follow the head again) and `,` again to
   switch back. Say which felt right.
5. Put the controllers down until they sleep and use a gamepad: Halcyon's head aim should be
   back (the log says `controller aim stopped`).
6. The Mako should behave as before (its cannon follows the head).

What Claude looks for: `hand aim on`, `the right controller aims weapons`, `first world view`,
`game camera's right axis vs the renderer's view: N degrees apart` (should be about 0),
`first shot aimed by the right controller`, `shots start at ... off it` (how far shots pass
beside the controller's ray), and the heartbeat's `hand aim:` and `host view:` lines.

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

1. Launch ME1 and load a save where Shepard stands on foot (any level). If SteamVR complains
   about a missing headset, press `]` to turn VR off: the test needs VR off.
2. Press `.` once. The log says `arm IK on (no VR session: desktop test, ...)`.
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
