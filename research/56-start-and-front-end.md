# 56: The game's start and front end on one panel (2026-11-05)

The user (2026-11-05): "When the game first launches the VR experience is buggy, it will [go] to full view head tracked
painting and then will resolve into the VR view. It makes the experience jarring. This is for the title sequence."

## What the headset showed (before)

Halcyon decides each frame between plain VR (the world in the eyes), a VR cine (here: our 3D cinema screen, research/52) and
its flat panel (`DecideCine` and `CineFinishDecision` in `src/halcyon/le1/xr_session.cpp`), from the engine's game mode byte
(`SFXGameModeManager.CurrentMode`: 0-4 gameplay, 5 conversation, 6 cinematic, 7 GUI, 8 movie, -1 unreadable), a Bink-decoding
movie detector and the camera mode's class name. Its `[VRSTATE]` line logs every change. A launch on the fake headset
(2026-11-05 08:08, the same sequence in every launch logged since 2026-11-04 22:27, 25 launches):

| Time | `[VRSTATE]` | What the eyes showed |
|---|---|---|
| 0.0 s | `gm -1 alive -> VR` | the title's 3D backdrop (a matte painting of a planet and moon behind the "press any key" UI) rendered in stereo, filling the view, head-tracked: **the "full view head tracked painting"**. The mode manager doesn't exist yet (-1), a perspective view renders (alive), so Halcyon's rules give plain VR. 1.5-2 s, at 2 presents a second while the start-up loads |
| 1.5 s | `gm 8 alive movie -> FLAT panel` | the logos' movie on the flat panel (2.35 m wide, 1.4 m away), for 0.1-0.2 s |
| 1.6 s | `gm 8 alive movie cutscene -> 3D screen (MEVR)` | the movie, then the title, on the 3D cinema screen (3.0 m wide, 2.6 m away, a 16:9 crop): the camera's class name reads `SFXCameraMode_Cinematic` at the title, and Halcyon's name fallback latched a cutscene although the byte was readable (8). 3-5 s |
| 3.4 s | `gm 7 alive cutscene -> FLAT panel` | the title on the flat panel once the GUI mode had lasted the latch's menu sustain (45 presents) |
| key | `gm 6 alive cutscene -> 3D screen`, `gm 0 alive -> VR` (one frame, 38 ms), `gm 7 alive -> FLAT panel` | the camera flight to the main menu on the 3D screen for 2.5 s, one frame of the world filling the view, the main menu on the panel |
| load | `gm -1 alive -> VR` (0.4 s), `gm 8 ... cutscene -> 3D screen` | the main menu's 3D scene in VR again, then the load's movie on the 3D screen: the title's cutscene latch (from mode 6, never cleared because the main menu holds a latch) outlived the main menu |

In another launch (08:25) the logos' movie wasn't seen decoding at all (no `[MOVIE]` line), and `gm 8 alive -> VR` showed the
backdrop in VR for 1.5 s instead.

Snapshots: `%LOCALAPPDATA%\MEVR\snapshots\2026-11-05_08-08-47-591` (the title on the 3D screen), `-08-49-385`, `-08-51-228`
(the flat panel). The first phase isn't in a snapshot: the test loop polls its request files on the game thread's tick,
which runs from about 3 s after the session starts.

## What changed (three `[MEVR]` seams, `core/vr/PresentationHold`, `plugin/PresentationHold`)

1. **The front end is the menu.** `MevrBridge::FrontEndHold(gameMode)` each frame; RunFrame's `inMenuMode` is `gameMode == 7 ||`
   it, so the flat panel shows (with `menuFlat`, the default) and the second eye doesn't render (the SFR gate is published as
   7). `presentation::FrontEnd`: from start-up until the first load from the menus (an unreadable byte after mode 7) or the
   first gameplay (a mode 0-4 for 30 frames, so the title's one-frame 0 doesn't count), every mode but gameplay is the menu:
   the unreadable start, the logos' movie mode, the title, the camera flight (6). After the menus, a load's unreadable byte and
   its movie mode are the menu too. In-game loads (-1 after gameplay) and the game's own cinematics (a new game's opening
   cutscene, a save's) are untouched: they come after the first load.
2. **The camera's name latches a cine only when the byte is unreadable**, as Halcyon's own comment on the fallback says, and
   never in the front end. A mode the front end shows as its menu doesn't latch a conversation or cutscene either.
3. **Plain VR after another view waits 100 ms** (`MevrBridge::HoldPlainVr`, `presentation::VrEntry`): the previous frame's
   decision is shown instead, so a short gameplay mode between two other views (the title's 6, 0, 7) never puts the world in
   the view; entering any other view is immediate. A cutscene's end into gameplay shows its screen 100 ms longer.

Logs: `presentation:` in `LE1MEVR.log` (each mode the front end holds, when Halcyon decides again, the start-up's end, each
blip of plain VR never shown).

## Checked (fake headset, 2026-11-05 08:17-08:30)

Launch to gameplay, `[VRSTATE]`:

```
gm -1 alive -> FLAT panel          start-up (was VR)
gm 8 alive -> FLAT panel           the logos (was VR, then the 3D screen)
gm 8 alive movie -> FLAT panel
gm 7 alive -> FLAT panel           the title
gm 6 alive -> FLAT panel           the camera flight after a key (was the 3D screen)
gm 0 alive -> FLAT panel           its one frame of mode 0 (was VR): "plain VR held back"
gm 7 alive -> FLAT panel           the main menu
gm -1 alive -> FLAT panel          the load (was VR)
gm 8 alive movie -> FLAT panel     its movie (was the 3D screen)
gm 0 dead -> VR                    the level
```

One panel from the first frame until the loaded level, which then comes in as VR. Gameplay after it is VR as before (snapshot
`2026-11-05_08-29-45-625`). The panel is world-locked where it first appears (`worldLockedPanels`), so it doesn't move between
the logos, the title and the main menu (`world panels:` logs one placement). Unit tests: `TestPresentationHold`.

## Left as it is

- An in-game load (`gm 0 -> -1`) still shows its first 0.4 s as plain VR. That's the last frame of the old level, frozen
  (snapshot `2026-11-05_08-30-15-975`), not the title's painting, and the loading movie then goes to the panel as before.
- A level that opens on a cutscene shows it on the 3D screen (the user's Comfort setting), and a GUI right after it (the squad
  screen on UNC20) stays on the 3D screen for the latch's menu sustain before the panel.
- A map opened from the command line (`launch-game.ps1 -Map`) passes no menus, so a cutscene before its first gameplay is shown
  on the panel. That only affects test launches.
- [verify] In the headset: launch, logos, title, a key, main menu, load: one panel, no VR phase, no 3D screen.
