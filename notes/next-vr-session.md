# Next test session

What to check next time, in order. The log is
`<ME1>\Binaries\Win64\Logs\LE1MEVR.log`; Claude reads it after each step, so just note what
you saw and roughly when.

Already done for you: `tools/vr-game-settings.ps1` turned VSync, motion blur and film grain off
and HDR to SDR in ME1's GamerSettings.ini (backup kept; `-Restore` undoes it).

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
5. Press `'` again to turn the simulator off, then quit.

What Claude looks for in the log:
- `hooked ULocalPlayer::CalcSceneView`;
- `render path: N player views, M GetPlayerViewPoint ... calls inside them`, with N about
  equal to fps × 5 and M ≥ 1 per view;
- no `falling back` warning;
- `blit: source ... (format 28 or 24)` instead of 10, which means HDR is off.

## B. In the headset (head-tracked mode, the default)

1. Start SteamVR / Steam Link first, then the game.
2. **Nod test:** look slowly up and down, then side to side. Is the vertical judder gone, better
   or unchanged? (The default path is now render-only.)
3. **A/B:** press `\` to compare render vs camera path stutter. Say which is smoother.
4. Look at a reflective floor (e.g. the Normandy) while turning. Reflections that jerk mean the
   capture-rate fix (research/13 §7) is next.
5. Known: the crosshair no longer follows your head on the render path. Aim is still the game
   camera's.

What Claude looks for:
- queue depth all at 1;
- lockstep timeouts about 0;
- present intervals tight around 11.1 ms (90 Hz);
- fps now that VSync is off.
