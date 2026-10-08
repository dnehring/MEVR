# Next test session

What to check next time, in order. The log is
`<ME1>\Binaries\Win64\Logs\LE1MEVR.log`; Claude reads it after each step, so just note what
you saw and roughly when.

Already done for you: `tools/vr-game-settings.ps1` turned VSync, motion blur and film grain off
and HDR to SDR in ME1's GamerSettings.ini (backup kept; `-Restore` undoes it).

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
| `.` | arm IK off / on: Shepard's arms follow the controllers (if the game's IK controls exist) |
| `=` | conversations, cutscenes, menus and galaxy map on the virtual screen: on (default) / off |

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

## B. In the headset (head-tracked mode, the default)

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
9. **Arm IK:** wave the controllers. Hands are placed relative to Shepard's head and facing, so
   this works in the normal third-person view: do Shepard's arms follow your hands? (If the log
   says `route 1 unavailable`, nothing happens yet; that's expected. `.` compares.)
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
