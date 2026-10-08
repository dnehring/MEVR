# Next test session

What to check next time, in order. The log is
`<ME1>\Binaries\Win64\Logs\LE1MEVR.log`; Claude reads it after each step, so just note what
you saw and roughly when.

Already done for you: `tools/vr-game-settings.ps1` turned VSync, motion blur and film grain off
and HDR to SDR in ME1's GamerSettings.ini (backup kept; `-Restore` undoes it).

Keys:

| Key | What it does |
|-----|--------------|
| `]` | VR on / off |
| `[` | head-tracked / screen mode |
| `\` | head path: render / camera / off |
| `'` | desktop head simulator: sweep / nod / off |
| `;` | stereo: mono / alternate-eye |
| `/` | 6DOF on / off |

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

1. Start SteamVR / Steam Link first, then the game.
2. **Nod test:** look slowly up and down, then side to side. Is the vertical judder gone, better
   or unchanged? (The default path is now render-only.)
3. **A/B:** press `\` to compare render vs camera path stutter. Say which is smoother. Return
   to render.
4. **Touch controllers:**
   - left stick: move;
   - right stick: turn;
   - triggers: aim / fire;
   - A / B / X / Y;
   - grips: LB / RB;
   - left menu: pause.

   Does pushing forward move where you look? It should only do so on foot; check that the
   dialogue wheel isn't rotated.
5. **6DOF:** press `/` and lean or crouch. Does the view move naturally, at a believable scale?
6. **Stereo:** press `;`. Is there depth? Comfortable, or ghosting and flicker on movement? Is
   the scale right? Press `;` again to compare with mono.
7. Look at a reflective floor (e.g. the Normandy) while turning. Do reflections still jerk?
8. Known: on the render path the crosshair no longer follows your head. Aim is still the game
   camera's.

What Claude looks for:
- queue depth all at 1;
- lockstep timeouts about 0, and drift recoveries 0;
- `frames tagged (N without a player view)`: N should be about 0 in gameplay;
- present intervals tight around 11.1 ms (90 Hz);
- fps now that VSync is off;
- `controller actions ready`;
- `stereo alternate-eye (L n / R n)` roughly equal;
- the half IPD in mm.
