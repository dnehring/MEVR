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

## The front end on the 3D screen (2026-11-05, second pass)

The owner, after the release's first QA: "in opening menu, we're doing the flat display, but right now it's actually just doing
head track. So when I move my head, I physically see the scene change. What I wanted is that the scene actually was within the
bounding box of like a visual monitor, but it was still 3D in appearance. So right now it's the worst of both worlds." And: "the
world position moves with my head position instead of staying still but it's rendered in 3d."

Why: the flat panel above is world-locked, but Halcyon parks head look only while its own menu is open or in a cinematic
(`menuOpen || (cinematicContext && ...)` in RunFrame). The front end's hold is neither, so the title's camera kept turning with
the head and the picture on the fixed panel panned with it.

Now the title and its main menu are a cutscene on the 3D cinema screen (research/52), when `[Cinematics] frontEnd = 1` (the
default; 0: the flat panel as above): `MevrBridge::FrontEndOnScreen` (the setting, before the first load from the menus),
`CineInputs::frontEndScreen` (also the hold and Stereo 2 on). In `DecideCine` it latches a cutscene in every front-end mode but
gameplay; the sustained-GUI rule doesn't flatten it (`adj.inMenuMode` off), `CineFinishDecision` puts it on the screen whatever
`[Cinematics] cutscenes` and Halcyon's VR-cutscene switch say, and the second-eye gate gets 6 for it, not 7. So head look is
parked (the screen's rule), both eyes render at the game's projection from parallel cameras, and the title's UI is in the
picture. Three edges, each found on the fake headset's first run:

- **The boot before the title** (the unreadable byte, the logos' 8; `FrontEnd::BeforeTitle`, until a 6 or 7 is read): flat, as a
  movie would be (`CineInputs::frontEndBoot` sets `adj.inMovie`; the gate as 7, no second eye), the cutscene still latched so
  head look stays parked. The first build put the boot's -1 on the screen, the logos' movie on the flat panel, then the title on
  the screen: two switches in 3 s.
- **Movies flatten at once** in the front end (`adj.inMovie` unsustained), as Halcyon does outside a latched cine; the latch's
  0.3 s movie sustain kept a load's movie on the screen for 1.35 s before the flat panel.
- **The load from the menus is the flat panel** from its first frame, as before (`FrontEndOnScreen` is false once `Starting()`
  ends: the load's -1 and 8 have no scene to show).

The title's one-frame 0 between 6 and 7 is still held back by `HoldPlainVr`; the latch ends at the first gameplay mode.

Checked on the fake headset (2026-11-05, 23:22-23:32, three launches): `[VRSTATE] gm -1/8 ... -> FLAT panel` with `headLook=0`,
`gm 7 alive cutscene -> 3D screen (MEVR)` at the title (`headLook=0`), the camera flight's 6 and the main menu's 7 on the screen,
the load's `gm -1 ... -> FLAT panel` at its first frame, then `gm 0 ... -> VR` with `headLook=1`. Under a replayed head sweeping
+-25 degrees the screen's picture held still (mean difference 2 of 255 between yaws 24.5, 7, -18.5 and 0, against 12 for the same
picture moved 20 px) and the screen stayed where it was in the room. Both eyes carry the title and its text, 35 px apart on a
2309 px eye image: the screen's crop (1.48% a side) puts them at the screen's infinity, 1.5% of its width behind it; the title's
scene (a planet from orbit) is all far away, so its 3D is that depth, not a near/far spread. The screen is the eye's shape (3.00 x
3.11 m at 2.6 m), not 16:9. Not yet seen by a person in the headset.

## The sequels (2026-11-18, lane FE, T18)

The owner, testing ME2 in the headset (2026-11-17): "the menus are not reliably using our almost like theater viewer system, and
the menus will just be at the floor ... This is for the starting and loading menus." The floor was fixed the same night
(e580550c: ME2's flat panels through `plugin/WorldPanels` at head height). This section is the rest: ME1's front end above on ME2
and ME3, one implementation.

### Before (main d6740ebe, fake headset)

Snapshots from launch through a load on each sequel (`%LOCALAPPDATA%\MEVR\fe\LE2-before-20261118-004640`,
`LE3-before3-20261118-004952`; the snapshots' JSON in `%LOCALAPPDATA%\MEVR\snapshots`, tags `boot-before*`, `load-before*`):

- **ME2** (`[VRSTATE]`, MELE2VR_Log.txt): the logos `gm 8 ... BINK -> FLAT menu panel`, the title `gm 7 live alive -> FLAT menu
  panel` (the "PRESS ANY KEY" screen over a live 3D scene of a sun and wreckage, a world-locked panel 1.6 m away: never the 3D
  screen), the load `gm -1 (eff 8 LOADMOVIE)` and its movie on the flat panel, then **`gm 6 live alive -> 3D screen (MEVR)` for
  0.27 s** and `gm 0 -> VR`. Every ME2 load ends so (four in the logs since 2026-11-17: 0.27-0.30 s of mode 6 after the movie);
  the screen's picture was black (snapshot `2026-11-18_00-48-00-855`), with the cinema's floor and horizon around it and two dips
  from black (`transition fade: a cinematic's flat panel -> the 3D cinema screen`, then `the 3D cinema screen -> the world`). A
  launch at 23:33 the evening before began `gm -1 live dead -> VR` for 0.2 s (ME1's "full view" start, short).
- **ME3** (`[PRESENTATION] submit=`, ME3PassiveDxgiProbe.log): from the first frame to the level `CINEMATIC_MONO`, Halcyon's
  flat cine panel (3.2 m wide, 2.2 m away), the title's "PRESS ANY KEY" over the Earth from orbit; a load from the title showed
  the title until the level came, in VR. No flips, but no 3D screen either. The title is ME3's Movie mode (10) over a live scene
  (older runs' `[CINEVR] FLAT panel (gm 10, gameplayCam 0, sceneAlive 1, fov 42)`, with blips of its GUI mode 9), which ME1's
  rules would have taken for a movie.

### What changed

- **One implementation** (`plugin/PresentationHold`, rules `core/vr/PresentationHold`): the sequels call it through the common
  seams `MevrSeams::FrontEndHold`, `FrontEndOnScreen`, `FrontEndBeforeTitle`, `HoldPlainVr` (research/trilogy-seams.md), with
  their own mode numbers, mapped to ME1's rules by `engine/GameAdapter.hpp` (`Me1Mode`). LE1's renderer calls the same functions
  by their LE1 names; its compiled code is unchanged (below).
- **ME2** (`me2_xr.cpp`, `d3d_capture.cpp`, `[MEVR]`): `FrontEndHold(gmRaw)` each frame; while it holds, the title and its menus
  (the GUI mode over a live scene) are ME1's 3D cinema screen (`mevrScreen`, head look parked, both SFR passes) and everything
  else of the front end is the flat panel (`GetMenuMode` answers the published state first: also the start's unreadable mode,
  which failed open to VR). Halcyon's own cinematic presentations stand down while the front end holds. `[VRSTATE]` marks it
  `frontend`.
- **ME2's load tail** (`MevrSeams::HoldLoadTail`, core `LoadTail`): a conversation or cinematic mode right after a movie is kept
  as the movie (the flat panel) until it has lasted 0.5 s; gameplay first ends it unseen (`presentation: game mode 6 (a load's
  tail) right after a movie was kept as the movie for 0.28 s, then mode 0: never shown as the 3D screen`); a real cutscene after
  a movie keeps its first 0.5 s on the flat panel (ME1's `[DEADVETO]` lead-in is the same idea). `[VRSTATE]` says `LOADTAIL`.
- **ME3** (`d3d_capture.cpp UpdatePresentationState`, `me2_xr.cpp`): the hold is decided first each Present with the raw mode
  read (the sticky mode hides the unreadable ones). ME3's adapter: its Movie mode over a live scene is the front end's GUI (ME1's
  7, the title), its Movie mode without a scene inside the front end ME1's unreadable mode (the boot's logos, or the load from
  the menus). The flat part is a menu presentation of its own (`[PRESENTATION] MENU_MONO enter: front end`); the 3D screen's
  part keeps the menu presentation off and its SFR submit does not wait for a captured pair (the live frame in both eyes on the
  screen, `stereoSource` false, until one is captured).
- **[Cinematics] frontEnd** is ME1's key and default (1) on every game; `0` keeps the front end on the flat panel.
- The load's tail counts as the load's movie for the dip from black (TransitionFade's view 2), so the world after a load comes up
  from black as on ME1 and ME3.
- **The front end's screen is the game pointer's** (`plugin/SequelOverlays.cpp`, as LE1's `CineScreenPointerPlane`): the whole
  game window on the UI layer's plane, wider and taller by the part the screen leaves out, so a controller's ray and trigger
  work the title and its menus (ME3's flat GUI panel had the pointer before; ME2's flat menu panel never had).

### After (fake headset, 2026-11-18)

Launch, the title, A replayed at the title (`tools/make-interrupt-recording.py press-a.mevrrec --seconds 3 --a 30`, `replay.ps1
-Inputs`), the main menu, `load.ps1` (ME2 Save 4, ME3 Save 31), snapshots every 0.5-1 s throughout
(`%LOCALAPPDATA%\MEVR\fe\LE2-a2-20261118-013736`, `LE3-a3-20261118-014041`; tags `boot-a2`, `press-a2`, `menu-a2`, `load-a2`, and
`-a3`):

- **ME2:** `gm -1 frontend live dead -> FLAT menu panel` at the first frame (was VR), the logos flat, `gm 7 frontend live alive ->
  3D screen (MEVR)` 3.6 s after XR started: the title in stereo on the screen (cameras 0.50 units apart, `stereoSource` true, depth
  1%, the floor and horizon around it; snapshot `2026-11-18_01-38-40-517`), A took it to the main menu (Resume, New Game, Load Game,
  Extras, Exit to Launcher over the Normandy; `01-39-27-466`) with no change of view (no `[VRSTATE]` line between 01:38:33 and the
  load). The load from the menu: the flat panel from its first frame (`presentation: mode -1 (unreadable) after the menus (a
  load)`), its mode 6 tail `gm 6 (eff 8 LOADTAIL) -> FLAT menu panel` for 0.28 s (`never shown as the 3D screen`), then VR. Views:
  flat, screen, flat, world.
- **ME3:** `[PRESENTATION] submit=MENU_MONO` (`front end`, the unreadable start) for 0.8 s, then the title on the 3D screen
  (`mode 7 (GUI) ... the 3D screen`; the first captured pair 0.8 s later: `the cameras 0.61 units apart for a 42.0-degree lens`;
  snapshot `2026-11-18_01-41-37-177`), the load `MENU_MONO enter: front end` (the 3.2 x 2.3 m flat panel), the level after 0.1 s of
  Halcyon's `CAPTURE_STALL_MONO`. The title read mode 9 (`mevr state`) this time. A replayed at ME3's title didn't pass "PRESS
  ANY KEY", so ME3's main menu was not reached by script; it is the same front end (GUI or Movie mode over the same scene).
- **The last two changes, ME2 again** (`%LOCALAPPDATA%\MEVR\fe\LE2-v2-*`, 01:59): the same sequence of views; with the replayed hands
  held ahead, `game pointer: the flat screen is up (the game's menus, 2.51 x 1.41 m, fixed in the room)` and `the right
  controller's ray is on the game's menus at (2194, 1282) px of the 4096x2304 window, 2.75 m away` (the hand 0.09 m right and
  0.08 m under the eyes: 147 and 131 px off the centre, exact); after the load `transition fade: a cinematic's flat panel ->
  the world: from black`.

### LE1 unchanged, by its compiled code

The LE1-visible changes are declarations (`MevrSeams.h`), an inline class LE1 never calls (`LoadTail`) and sequel-only code
(`#if !defined(MEVR_WITH_HALCYON)` in `plugin/PresentationHold.cpp`). Built at a clean commit (520d1bc4) against main d6740ebe in
`build-trilogy`: `LE1MEVR_mod.dll` identical in every section; `LE1MEVR.asi` identical in `.text`, `.data`, `.pdata` and `.reloc`,
with `.rdata` and `.rsrc` differing only in the commit string and the debug directory's timestamps and PDB age (a section-by-section
comparison of the PE files). No same-pose pair was needed.

### Not done

- The game pointer on the 3D screen's UI layer outside the front end (conversations): unchanged, ME1's CNV contract on the sequels.
- `HoldPlainVr` (plain VR after another view waits 100 ms) is wired on neither sequel: no blip of a gameplay mode was seen in
  their front ends (ME1's came from the title's camera flight).

## Left as it is

- An in-game load (`gm 0 -> -1`) still shows its first 0.4 s as plain VR. That's the last frame of the old level, frozen
  (snapshot `2026-11-05_08-30-15-975`), not the title's painting, and the loading movie then goes to the panel as before.
- A level that opens on a cutscene shows it on the 3D screen (the user's Comfort setting), and a GUI right after it (the squad
  screen on UNC20) stays on the 3D screen for the latch's menu sustain before the panel.
- A map opened from the command line (`launch-game.ps1 -Map`) passes no menus, so a cutscene before its first gameplay is shown
  on the panel. That only affects test launches.
- [verify] In the headset: launch, logos, title, a key, main menu, load: one panel, no VR phase, no 3D screen.
