---
type: Audit
title: ME3 presentation modes against ME1's contracts
description: What ME3 shows in each game mode and why; the 3D cinema screen, its backdrop, the dip from black and the room-fixed panels are ME1's modules reached through the sequels' shared glue, the game screens and the galaxy map hold the world as on ME1, but the decision is Halcyon ME3's own block with the screen admitted in two mode numbers only, there is no front-end or entry hold, the flat panel is 2.2 m away and 3.2 m wide, and the menu still tells the player the 3D screen does not exist.
tags: [audit, me3, parity, presentation, cinema, flat-panel, front-end]
status: draft
timestamp: 2026-11-18T10:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:00:00Z }
game: LE3
against: ../../me1/presentation-modes.md
commit: 583591c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/presentation-modes.md). Reading only: no game was run; where a note records
a run it is named. ME3's cinema screen has no separate-eye acceptance yet (ledger R21 "U").

The modules are ME1's: `core/vr/CineScreen`, `plugin/CineScreen`, `CinemaBackdrop`, `TransitionFade`, `WorldPanels`,
through `plugin/SequelCinema`, with ME1's defaults (2.9 m at 3.5 m, 1% depth, UI 0.5 m ahead, 0.35 s dip). ME3 compiles
exactly what ME2 compiles here. The decision of what to show is Halcyon ME3's own block in `me2_xr.cpp` with `[MEVR]`
seams, and ME1's hold rules (`core/vr/PresentationHold`) are not in the LE3 host.

1. **The screen is admitted in two raw mode numbers.** `mevrScreen` needs `gm == 7 || gm == 8` (ME3's conversation and
   cinematic). Upstream's own comment says ME3 scenes "are not obliged to use a mode the mod has already seen" and turned
   its allow-list into a deny-list for that reason; our gate put an allow-list back. A cinematic the camera test accepts
   in another mode goes to the flat panel, because Halcyon's VR cines are pinned off. How many scenes that is was never
   measured.
2. **The flat panel is 2.2 m away and 3.2 m wide** (ME1 1.4 m and 2.35 m), upstream ME3's defaults, unpinned (**fixed, lane AU1, 6abedb31: ME1's numbers as code and ini defaults**); it is what
   the title, the main menu, loads, movies, the squad record and every unknown screen use.
3. **No front end and no entry hold**, as on ME2, and **the menu disagrees with the renderer**: the host shows the 3D
   screen by default while the Comfort page on ME3 offers Flat screen only and refuses every `cine_*` write.

| rating | count | ids |
|---|---|---|
| same | 4 | PRS-03, 04, 16, 28 |
| same core, other inputs | 7 | PRS-02, 05, 06, 10, 18, 21, 30 |
| adapter | 6 | PRS-01, 09, 19, 20, 22, 26 |
| partial | 2 | PRS-07, 27 |
| missing | 7 | PRS-12, 13, 14, 17, 23, 24, 29 |
| n/a | 1 | PRS-25 |
| unknown | 3 | PRS-08, 11, 15 |

# The state table on ME3

ME3's mode byte as [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp) names it (lines 56-62), with the shipped
defaults. Reading only.

| mode | name | presentation on ME3 | against ME1 |
|---|---|---|---|
| -1 | unreadable | upstream's decision (camera class, scene alive, field of view) | ME1: the front end's flat panel |
| 0 | on foot | plain VR, the whole HUD on the body-locked panel | as ME1, without the wrist and gun pieces |
| 1 | vehicle | plain VR, HUD on the panel, no hand reticle | ME1's Mako has its ring and cockpit |
| 2 | Atlas | plain VR, HUD on the panel | no ME1 form; a game screen opened from it is flat |
| 3 | power wheel | plain VR, the wheel on the HUD panel | as ME1's default |
| 4 | weapon wheel | plain VR, the wheel on the HUD panel | ME1: a quad at the left hand |
| 5 | squad command | plain VR | as ME1 |
| 6 | injured Shepard | plain VR, HUD on the panel | no ME1 form |
| 7 | conversation | the 3D cinema screen when the camera test passes and `[Cinematics] conversations` is on; else the flat panel | ME1's mode 5; no first person, no "In VR" |
| 8 | cinematic | the 3D screen after 20 live presents with a non-gameplay camera (`s_gm8Vr`); the galaxy map's opening and closing: the world | ME1's mode 6; the entry discipline is upstream's |
| 9 | GUI | plain VR with the screen on the wrist or in the room while the mod owns it; else the flat panel (2.2 m, 3.2 m) | ME1's mode 7; the front end is flat on ME3 |
| 10 | movie | flat panel | as ME1's mode 8 |
| 11 | galaxy map | plain VR at Shepard's place in the CIC, the map a stereo window on the projector; the relay's movie flat; after a load inside the map: flat | ME1's mode 9 |
| 12 | orbit and scan | as 11 when the flow holds it (not seen in a run) | ME1 has no such mode |

# Contract by contract

| id | rating | how ME3 gets it | where it differs | evidence |
|---|---|---|---|---|
| PRS-01 | adapter | Halcyon ME3's own decision each frame, logged `[CINEVR] <state> (gm, convoCam, gameplayCam, sceneAlive, paused, fov)` on every flip, with two states for our screen | not `DecideCine` / `CineFinishDecision`; the inputs are upstream's camera-class and scene-alive tests plus our seams | le3 `me2_xr.cpp:1700-1790` |
| PRS-02 | same core, other inputs | `mevrScreen` = not the galaxy's, SFR on, a cine field of view, `vrEligible`, no menu latch, `gm == 7 or 8`, and `MevrSeams::CineScreenWanted(gm == 7)` | two raw mode numbers (right for ME3 today) where upstream deliberately has none; Halcyon's own VR cines pinned off, where ME1 requires them on; the menu offers Flat screen only | le3 `me2_xr.cpp:1755-1766`; le3 `me2_menu.cpp:519-521`; `Seams.cpp:79-81`; `main.rml:187-190` |
| PRS-03 | same | `cine_screen::QuadsNow` and `world_panels::Place` | none: 2.9 m, 3.5 m, 16:9 | `SequelCinema.cpp`; le3 `me2_xr.cpp:2327` |
| PRS-04 | same | `wp::Kind::Conversation` / `Cutscene` take `ScreenPanelSettings` | none | `WorldPanels.cpp:100-102` |
| PRS-05 | same core, other inputs | the same crop per eye | ME3's eye images are its 3072x2216 render (about 1.39:1), neither ME1's headset shape nor ME2's 16:9; `tools/cine-screen-check.py` was never run on them | `SequelCinema.cpp`; `runtime/MELE3VR.ini` |
| PRS-06 | same core, other inputs | `CineScreenHalfSeparationUU(MevrCinemaHalfFov(view))` in the calcview hook, with the lens of the view being built | no after-the-fact `ScreenLensScale` (the hook has this frame's lens, as ME2) | le3 `calcview_hook.cpp:1480` |
| PRS-07 | partial | `sequel_cinema::FillUi`: the HUD panel's image on a layer `uiForwardM` in front, scaled to the same angle | no pointer plane, by a written decision ("authored dialogue ... retain native pad input until their own LE3 pointer ... contracts are accepted"); replies by the stick | `SequelOverlays.cpp:170`; `HalcyonLe3Bridge.cpp:348-350` |
| PRS-08 | unknown | the UI goes to the panel at the movie's own layout (ME3's per-element HUD moves never run in our host) | whether the wheel is ME1's size on the screen is not recorded. A snapshot's `-hud.png` of a wheel on the screen, measured against ME1's, settles it | `RendererLe3.cpp:179-182` |
| PRS-09 | adapter | `PaceDisplayLocked(!screen or CineScreenFullRate())` | ME1's key and default on upstream ME3's pacing | le3 `me2_xr.cpp:1475-1476` |
| PRS-10 | same core, other inputs | `cinema_backdrop::Layer` through `SequelCinema` | the image shape is derived for ME3's eye image; unseen in a run | `CMakeLists.txt:1169` |
| PRS-11 | unknown | `SetMevrCineScreen` tells the calcview hook the screen is up | head look and lean parked while it shows was not traced in ME3's hook. A replayed head turn during a screened scene (the eye images must not move) settles it | le3 `calcview_hook.cpp:1740-1741`, `me2_xr.cpp:898` |
| PRS-12 | missing (ported 2026-11-18 by lane FE: research/56 "The sequels", T18) | none | `plugin/PresentationHold` and its core are in LE1's list only; capability `frontEnd` false | `CMakeLists.txt:789-790`, `:907`; `RendererLe3.cpp:40-68` |
| PRS-13 | missing (ported 2026-11-18 by lane FE: research/56 "The sequels", T18) | the title and main menu are the flat panel in the room, 2.2 m, 3.2 m wide, with the pointer on it | no 3D screen for the front end, no boot rule | `HalcyonLe3Bridge.cpp:315-361` |
| PRS-14 | missing | upstream's own entry rules: 20 consecutive live presents before a mode-8 scene is a VR cine, a deny-list otherwise | not ME1's 100 ms hold of the previous view; whether a one-frame gameplay mode flashes the world between two others on ME3 is unmeasured | le3 `me2_xr.cpp:1704-1750` |
| PRS-15 | unknown | upstream classifies by camera class at every mode, not only while the byte is unreadable | whether that latches a cine wrongly at start-up was not read. A start-up log's `[CINEVR]` lines settle it | le3 `me2_xr.cpp:1700-1703` |
| PRS-16 | same | `MevrSeams::TransitionFadeLayer`, the frame's last layer; the view kinds from the frame (screen, world, a cinematic's flat movie, other flat) | "a cinematic's flat" is again `gm == 7 or 8` | le3 `me2_xr.cpp:2841-2846` |
| PRS-17 | missing | none | no first-person conversation; the seam never passes `cut` | PRS-23 |
| PRS-18 | same core, other inputs | Halcyon's flat quad re-placed by `MevrSeams::PlacePanel` through `world_panels::Place`: fixed where the head was, at its height and heading, follow and gap rules ME1's | 2.2 m and 3.2 m (ME1 1.4 m and 2.35 m; **ME1's since lane AU1, 6abedb31**); a cutscene's width times `CineScreenZoom`; conversations and cutscenes never follow | le3 `me2_xr.cpp:93-95`, `:1949-1966`, `:2813-2828`; `HalcyonLe3Bridge.cpp:315-361` |
| PRS-19 | adapter | modes 9 and 10 and an unearned 8 are upstream's flat sources; a stalled capture is `CAPTURE_STALL_MONO` | upstream's comment names 9 "galaxy map" and 10 "title/orbital" where `GameAdapter.hpp` says GUI and movie: the test is right under both readings, the comment is a trap for the next edit | le3 `me2_xr.cpp:1738-1746` |
| PRS-20 | adapter | `MevrSeams::WorldUiOwnsGui(mode)` in `d3d_capture.cpp`'s presentation decision keeps the GUI mode out of `MENU_MONO` | an opening the game starts itself is flat for one tick; under a stalled mod (400 ms) ME3 entered `MENU_MONO` twice in a run; an outgoing cinematic's latch is not cleared explicitly | le3 `d3d_capture.cpp:5643-5650`; research/le3-native-gui lane FX3 |
| PRS-21 | same core, other inputs | `world_ui::Ownership` bits; a clean-eye hold (`WorldUiHoldFrame`) for a reserved opening | play modes are ME3's 0..6, and the Atlas and injured Shepard reserve nothing (`FootViewMode`). A run counted 0 flat-panel frames during reservation | `MenusLe2.cpp:203-205`; le3 `me2_xr.cpp:2295-2299`; `WorldUi.hpp:216-240` |
| PRS-22 | adapter | `MevrSeams::GalaxyOwnsPresentation` for modes 11 and 12 and the opening and closing cinematic (8) | travel is a movie inside mode 11, shown flat with the window down; a save loaded in the map is flat; the opening's first 2-7 frames could show the cinema screen before the flow's bridge (a fix built, not yet seen in the game) | le3 `me2_xr.cpp:1757-1760`; research/le3-galaxy lanes GX3, GX4 |
| PRS-23 | missing | none | `plugin/ConversationWorld` (host) is LE1-only; upstream's `ConvoFp` is forced off | `CMakeLists.txt:1143-1193` |
| PRS-24 | missing | none | follows PRS-23 | |
| PRS-25 | n/a | ME3 has no bypass minigame mode | | `WorldUi.hpp:224-226` |
| PRS-26 | adapter | `SequelOverlays.cpp` routes the UI: HUD panel in gameplay, the screen's UI layer, a world screen's place, the galaxy disc | the sequels' composition; the log line is `overlays: game UI goes to the body-locked HUD panel` | `SequelOverlays.cpp:145-186`; research/le3-hud-panel |
| PRS-27 | partial | the pointer on world UI panels and on the flat GUI panel | not on the screen's UI layer, not on the galaxy map; flat conversations excluded as ME1 | `HalcyonLe3Bridge.cpp:348-356`, `:126-129` |
| PRS-28 | same | `menus_le2::Layers` puts the menu alone on top; `OnPresent` from ME3's Present | the pause is ME3's native `BioWorldInfo.PauseGame`, unverified in a run | `MenusLe2.cpp:396-443`; `HalcyonLe3Bridge.cpp:305-311` |
| PRS-29 | missing | `game/DepthOfFieldHold` on the sequels serves the galaxy flow only | no conversation hold | `DepthOfFieldHold.cpp:5` |
| PRS-30 | same core, other inputs | the screen takes every cine its gate admits; Halcyon's VR cines pinned off; `ConvoFp::SetEnabled` forces false under `MEVR_ASI` | a cine outside modes 7 and 8 gets neither the screen nor a VR cine: flat | le3 `me2_xr.cpp:1761-1766`, `me2_menu.cpp:519-521` |

# Against ME2

| ME2 finding | on ME3 |
|---|---|
| No front end and no entry hold | same defect |
| The flat panel is upstream's size; fixed in the room only on 2026-11-17 | the size: same defect, larger numbers. The room placement: fine, ME3 used `world_panels::Place` first and ME2 copied it |
| `[Cinematics] conversations = 0` means flat, not "In VR" | same |
| `ConvoFirstPerson` not pinned | fine: forced off at the setter on ME3 (not named in the baseline's log) |
| Modes 5 and 6 as raw numbers "that happen to be ME2's" | same shape with 7 and 8, and here it narrows upstream's own rule |
| No pointer on the UI layer | same, by a written decision |
| No pointer on the flat panel | fine on ME3 |
| The screen's depth never measured on the sequel's eye images | same |
| `scale set` during a load hung ME2 | different: ME3 has no 3 s wait and none was needed in its runs; unknown under depth of field at a load |
| The menu and the renderer agree on ME2 | different: on ME3 the menu hides the 3D screen the renderer shows |

# Reinvented

| ME3 code | ME1 or shared code | what differs | what unifying takes |
|---|---|---|---|
| The decision block in le3 `me2_xr.cpp` (upstream's, with our seams) | `DecideCine`, `CineFinishDecision` in LE1's `xr_session.cpp` with `core/vr/PresentationHold`; ME2's block | three decisions: ME1 holds the previous view, ME2 fails open to VR, ME3 counts 20 presents and denies by list | one game-independent function over facts (mode class from `engine::adapter`, movie, scene alive, camera class, owners), called from the three trees |
| `gm == 7 or gm == 8` in three places of le3 `me2_xr.cpp` | `engine::adapter::GameMode` | raw numbers inside a tree that also includes the adapter's header for other tests | `IsConversationMode` / `Mode::Cinematic` |
| `convo_fp.cpp` (Halcyon ME3's first-person conversation) | `game/ConversationWorld` | a parallel implementation, compiled, forced off | leave off; delete from the build when ME1's is ported |

# Rediscovered problems

| problem on ME3 | when and cost | ME1 already held it | sources |
|---|---|---|---|
| The galaxy map's opening cinematic showed on the cinema screen, its first frame flat | lanes GX3 and GX4, 2026-11-17 | PRS-22: table ownership covers the opening and closing mode and beats a cinematic latch | [research/le3-galaxy](../../../../research/le3-galaxy.md) |
| A game screen's first frame from the screen's own camera | lane FX3 | PRS-21's reservation and LE1's clean-eye guard | [research/le3-native-gui](../../../../research/le3-native-gui.md) |
| The bridge zeroed the left stick in conversation mode: no reply could be picked | fixed in 0344c2b9 | "while a scene is on the screen the pad reaches the game as in the flat game" | `HalcyonLe3Bridge.cpp:214-219` |
| Not yet met, waiting: the title's world flash, a cutscene latch outliving the menu | unmeasured on ME3 | the front-end hold and the 100 ms entry hold | [research/56](../../../../research/56-start-and-front-end.md) |

# Forced by the game

- ME3's mode numbers (conversation 7, cinematic 8, GUI 9, movie 10, galaxy 11, orbit 12) and its in-engine cutscenes
  that stay in other modes (upstream's `[CINEALLOW]` note).
- Level-streaming loads flap the scene's activity inside mode 8, hence upstream's 20-present entry rule.
- The relay's travel is a movie inside the map's mode.
- ME3's presentation latch lives in `d3d_capture.cpp` (`MENU_MONO`), so the world-UI seam sits there.
- No minigame mode.

# Not forced

- The screen's gate as two mode numbers.
- No front-end hold and no entry hold: game-independent code not linked.
- The flat panel's 2.2 m and 3.2 m.
- The menu's "3D cinema ... not available yet" over a renderer that shows it.
- No pointer on the UI layer.
- The cinema screen's depth unmeasured on ME3's 3072x2216 images.

# What to do

1. **Make the menu say what the renderer does**, either way: un-refuse `cine_*` and show the 3D screen chip on ME3
   (`MenuSettings.cpp:900-903`, `main.rml:187-190`, `MainMenuSettings.cpp`), or default `cine_screen::Wanted` to flat on
   ME3 until its separate-eye acceptance. Owner's question: is the 3D screen ME3's default today? Check:
   `mevr-menu-preview --le3` and a `[CINEVR]` line that matches the chip.
2. **Give the flat panel ME1's numbers** (menus-and-game-screens.md, item 2).
3. **Count the cinematics outside modes 7 and 8.** One played stretch with `[CINEVR]` lines: every `FLAT panel` flip
   whose `gameplayCam 0, sceneAlive 1` names a scene the gate turned away. Then gate by camera ownership as upstream
   does, through `engine::adapter` for the conversation test. Check: the same stretch shows `3D cutscene screen`.
4. **Measure the start-up** as the ME2 audit's item 1, on ME3; link `PresentationHold` only if a non-flat frame shows.
5. **Measure the screen's depth** with `tools/cine-screen-check.py` on an ME3 snapshot pair.
6. **The pointer on the UI layer** with ME2 (`CineScreenPointerPlane` from `SequelCinema.cpp`), once the owner accepts
   pointer replies on the sequels.
7. **One decision function** for the three renderers, after 3 and 4.

# Citations

- Catalog: [presentation-modes.md](../../me1/presentation-modes.md). ME2's audit:
  [presentation-modes.md](../me2/presentation-modes.md). Siblings: [menus-and-game-screens.md](menus-and-game-screens.md),
  [conversations-and-scenes.md](conversations-and-scenes.md).
- Ledger: [trilogy parity ledger](../../../trilogy-parity-ledger.md) R21, R22, R24. Disagreements: R21 "adapted U" hides
  the two-number gate and the menu's contradiction; R22 "missing" is right.
- Research: [le3-dialogue-cinema](../../../../research/le3-dialogue-cinema.md),
  [le3-shared-presentation](../../../../research/le3-shared-presentation.md),
  [le3-native-gui](../../../../research/le3-native-gui.md), [le3-galaxy](../../../../research/le3-galaxy.md),
  [52 cinema screen](../../../../research/52-cine-3d-screen.md), [56 start and front end](../../../../research/56-start-and-front-end.md).
- Source: [me2_xr.cpp](../../../../src/halcyon/le3/me2_xr.cpp), [me2_menu.cpp](../../../../src/halcyon/le3/me2_menu.cpp),
  [calcview_hook.cpp](../../../../src/halcyon/le3/calcview_hook.cpp), [d3d_capture.cpp](../../../../src/halcyon/le3/d3d_capture.cpp),
  [SequelCinema.cpp](../../../../src/plugin/SequelCinema.cpp), [SequelOverlays.cpp](../../../../src/plugin/SequelOverlays.cpp),
  [MenusLe2.cpp](../../../../src/plugin/MenusLe2.cpp), [HalcyonLe3Bridge.cpp](../../../../src/plugin/HalcyonLe3Bridge.cpp),
  [WorldPanels.cpp](../../../../src/plugin/WorldPanels.cpp), [RendererLe3.cpp](../../../../src/plugin/RendererLe3.cpp),
  [Seams.cpp](../../../../src/plugin/Seams.cpp), [PresentationHold.hpp](../../../../src/core/vr/PresentationHold.hpp),
  [WorldUi.hpp](../../../../src/core/vr/WorldUi.hpp), [MenuSettings.cpp](../../../../src/core/vr/MenuSettings.cpp),
  [DepthOfFieldHold.cpp](../../../../src/game/DepthOfFieldHold.cpp), [main.rml](../../../../assets/menu/main.rml),
  [CMakeLists.txt](../../../../CMakeLists.txt).
