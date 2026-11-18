---
type: System
title: Presentation modes (ME1)
description: Which view the player gets in each of ME1's game modes and how a change between views is decided, as numbered contracts: plain VR, the 3D cinema screen, the flat panel, the front end and boot, world-held modes (game screens, the galaxy map, first-person conversations), the dip from black and the holds that stop one-frame flips.
tags: [me1, presentation, cinema, front-end, transitions, catalog]
status: stable
timestamp: 2026-11-18T07:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T07:00:00Z }
game: LE1
---

# What the player gets

- On foot, in the Mako and in the game's wheels: the world around them, head-tracked, both eyes, at the headset's rate.
- Conversations and cutscenes: a 3D cinema screen standing in the room, 2.9 m wide at 3.5 m, with real depth behind it, a
  dim floor and horizon around it, the dialogue wheel and subtitles on a layer half a metre in front of it. The screen stays
  where the scene began; leaning does not drag it along.
- The game's start: the logos and loads on one flat panel, the title and main menu on the 3D screen. No flip between views
  on the way to the first save.
- The game's own screens (inventory, shop, map, journal) do not replace the world: the world stays in stereo and the screen
  stands in it or is projected from the left wrist. Where they stand is in [menus-and-game-screens.md](menus-and-game-screens.md).
- The galaxy map opens over the CIC's table without leaving the CIC.
- A change between the world, the 3D screen and a cinematic's movie dips from black over a third of a second. Menus cut.
- Prerendered movies are always flat.
- With the experimental First person choice a conversation stays in the world; that mode's contracts are in
  [conversations-and-scenes.md](conversations-and-scenes.md).

# The state table

ME1's `SFXGameModeManager.EGameModes` byte, as [GameAdapter.hpp](../../../src/engine/GameAdapter.hpp) names it, and what is shown
with the shipped defaults.

| mode | name | presentation | second eye | head look | exceptions, in the order they are tested |
|---|---|---|---|---|---|
| -1 | unreadable (start-up, a load) | flat panel (the front end's menu) | no | parked before the title | after the first gameplay or load from the menus: Halcyon's own decision; the camera's name may latch a cine only here |
| 0 | on foot | plain VR, UI on the body-locked HUD panel | yes | yes | the start-up front end until 30 frames of gameplay; plain VR after another view waits 100 ms; a held moment whose matinee has the camera |
| 1 | vehicle (the Mako) | plain VR | yes | yes | the cockpit rides inside it ([vehicles.md](vehicles.md)) |
| 2 | power wheel | plain VR, the game paused | yes | yes | the wheel's part of the UI on the HUD panel (default) or a hand quad |
| 3 | weapon wheel | plain VR, the game paused | yes | yes | the wheel on a quad at the left hand |
| 4 | squad command | plain VR | yes | yes | none |
| 5 | conversation | 3D cinema screen | yes, at the game's own projection | parked | `[Cinematics] conversations = world`: the world, first person; Flat screen chosen: the flat panel; a set piece under `world`: the screen |
| 6 | cinematic | 3D cinema screen | yes | parked | the title's camera flight at start-up: the front end's screen; a matinee that walks Shepard, held by the mod: the world; the galaxy map opening or closing: the world; a dead scene (no 3D): flat |
| 7 | GUI | plain VR with the screen in the room or on the wrist | yes | yes | the front end (title, main menu): the 3D screen; a visible screen not in `[WorldUi] screens`, or no on-foot view to hold: Halcyon's flat panel |
| 8 | movie | flat panel | no | parked at start-up | always flat; dips from black when it follows the world or the screen in a cinematic |
| 9 | galaxy map | plain VR at Shepard's place in the CIC, the map a stereo window over the table | yes | yes | `[Galaxy] hologram = 0`: the flat panel |
| 10 | minigame (bypass) | plain VR, the puzzle projected from the left wrist | yes | yes | no tracked left controller: in the room |
| any | our main menu open | the current presentation with the menu's quad over it, the game paused on foot and in the Mako | as the mode | as the mode | not drawn for 1 s: it closes by itself |

# Contracts

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| PRS-01 | The decision is made once per frame from the game mode byte, the mod's published states and the settings, and is one of: plain VR, a VR cine on the 3D screen, a VR cine filling the view (Halcyon's, not offered by default), the flat panel. Each change of the whole decision is logged once. | log `[VRSTATE] gm <mode> ... -> 3D screen (MEVR)` | Stereo 2 required for any VR cine, else flat | `DecideCine`, `CineFinishDecision` in [xr_session.cpp](../../../src/halcyon/le1/xr_session.cpp) | fake headset |
| PRS-02 | A conversation (mode 5) or cutscene (mode 6) goes on the 3D cinema screen: both Stereo 2 passes render the shot at the game's own projection from two parallel cameras on the director's camera, and each eye's render goes on its own quad at one pose. | `[Cinematics] conversations` on (screen), `cutscenes = 1`; menu: Comfort > Conversations and cutscenes: In VR / 3D screen / Flat screen / First person (experimental) | `CineScreenWanted`; Halcyon's `cineVrConvo` / `cineVrCutscene` on; the scene alive (not prerendered) | [CineScreen.cpp](../../../src/plugin/CineScreen.cpp), [CineScreen.hpp](../../../src/core/vr/CineScreen.hpp), `PlanStereo`'s screen branch in [MetricStereo.cpp](../../../src/plugin/MetricStereo.cpp) | headset, 2026-11-06 |
| PRS-03 | The screen's size and place: placed when a sequence starts, ahead of the head, level. | `widthM` 2.9 (1..12), `distanceM` 3.5 (1..10): 45 degrees across; `aspect` 16:9 (the eye image's middle band; 0 the whole near-square image; 1..3) | placed afresh after a recenter | [CineScreen.hpp](../../../src/core/vr/CineScreen.hpp) `Settings`, `Rects`, `HeightM`; `PlacePanel` in [WorldPanels.cpp](../../../src/plugin/WorldPanels.cpp) | headset, 2026-11-06 |
| PRS-04 | The screen stays where the sequence began: it never follows the head, a conversation and a cutscene are one kind, and a short gap is the same showing. | follow off; gap 270 frames (3 s at 90 Hz) | recenter re-places | `ScreenPanelSettings` in [OverlayPlacement.cpp](../../../src/core/vr/OverlayPlacement.cpp) | headset, 2026-11-06 |
| PRS-05 | Depth behind the screen: each eye drops a sliver of its width on opposite sides (the left eye its left edge), which puts infinity behind the screen plane. | `depth` 0.010 of the width (0..0.05); crop c with c / (1 - c) = depth; infinity's disparity capped at 0.8 of the eye distance (`kDivergenceShare`; IPD fallback 0.064 m) | 1.5% was cross-eyed in the headset | [CineScreen.hpp](../../../src/core/vr/CineScreen.hpp) `CropFraction`, `MaxDepth`, `Plan` | unit test; headset; `tools/cine-screen-check.py` |
| PRS-06 | The cameras' distance follows each shot's lens so the scene at the convergence distance lands on the screen plane; the render hook scales the spacing to the lens the frame is drawn with. | `convergeM` 0.8 (0.5..20) at 100 units per metre; a lens wider than 80 degrees across is planned as 80 (`kMaxLensHalfH` 0.698 rad) | a conversation's first frame reports the gameplay window, hence the cap and `ScreenLensScale` | [CineScreen.hpp](../../../src/core/vr/CineScreen.hpp) `BaselineUU`; [render_hook.cpp](../../../src/halcyon/le1/render_hook.cpp) `MetricPass::lensTan` | unit test; headset 2026-11-12 |
| PRS-07 | The game's UI in a screened scene (wheel, subtitles, the title's menus) stands on a layer of its own in front of the screen, scaled to cover the same angle, cut to the band the screen shows. The game pointer works on that plane. | `uiLayer = 1`, `uiForwardM` 0.5 (0..1.5); invalid when the eyes are within `uiForwardM` + 0.25 m of the screen | 0: the UI stays in the eye images at the far depth | [CineScreen.hpp](../../../src/core/vr/CineScreen.hpp) `PlaceUi`, `UiPartShown`; [HalcyonOverlays.cpp](../../../src/plugin/HalcyonOverlays.cpp) | headset, 2026-11-12 |
| PRS-08 | The conversation movie is drawn at its authored size on the screen, not at Halcyon's half-size VR layout. | the wheel 1.75x larger than Halcyon's VR layout | the screen or a world conversation | seam in [xr_session.cpp](../../../src/halcyon/le1/xr_session.cpp) | headset |
| PRS-09 | The screen runs at the headset's rate. | `fullRate = 1`; 0: Halcyon's `cineFpsCap`, half the display rate | the screen | [CineScreen.cpp](../../../src/plugin/CineScreen.cpp), the pace seam | headset, 2026-11-06 |
| PRS-10 | A floor and a horizon surround the screen: a projection layer under it whose every pixel's ray meets the player's real floor or the sky. | `backdrop = 1`, `backdropBrightness` 1.0 (0..3); drawn at 0.5 of the eye's size; fades in over 0.6 s; not drawn for 500 ms: the next frame starts a new sequence; eye height fallback 1.6 m | procedural until the game's textures are cached | [CinemaBackdrop.cpp](../../../src/plugin/CinemaBackdrop.cpp) | headset; offline `mevr-cinema-preview` |
| PRS-11 | Head look and lean are parked while the screen shows: the picture is the director's. | none | the screen and the front end | park seams in [xr_session.cpp](../../../src/halcyon/le1/xr_session.cpp) | headset |
| PRS-12 | Start-up front end: from launch until the first load from the menus or enough gameplay, every mode but gameplay is the front end (never plain VR): the unreadable byte, the logos' movie mode, the title, its camera flight. After the menus a load's -1 and 8 are the menu too. | gameplay ends it after 30 consecutive frames of a mode 0-4 (`kGameplayFrames`) | game-independent rule; host seam `FrontEndHold` | [PresentationHold.hpp](../../../src/core/vr/PresentationHold.hpp) `FrontEnd`; [PresentationHold.cpp](../../../src/plugin/PresentationHold.cpp) | fake headset; unit test |
| PRS-13 | The title and the main menu are a cutscene on the 3D screen, in stereo, head look parked, whatever the cutscene choice. The boot before the title is flat with head look parked; movies are flat at once; the load from the menus is flat from its first frame. | `[Cinematics] frontEnd = 1`; 0: the flat panel throughout | `FrontEnd::BeforeTitle` (no 6 or 7 read yet) keeps the boot off the screen; the second-eye gate treats the front end as mode 6 | `FrontEndOnScreen`, `CineInputs::frontEndScreen` in [xr_session.cpp](../../../src/halcyon/le1/xr_session.cpp) | fake headset, 2026-11-05 |
| PRS-14 | Plain VR after another presentation waits with the previous one shown, so a one-frame gameplay mode between two others never puts the world in the view. | 100 ms (`VrEntry::kHoldMs`) | an admitted world-GUI handoff is accepted at once (`AcceptWorldUi`); so is the galaxy table's | [PresentationHold.hpp](../../../src/core/vr/PresentationHold.hpp) `VrEntry` | unit test; fake headset |
| PRS-15 | The camera's name latches a cine only while the mode byte is unreadable. | none | start-up | [xr_session.cpp](../../../src/halcyon/le1/xr_session.cpp) | fake headset |
| PRS-16 | A dip from black when the view changes between the world, the 3D screen and a cinematic's flat movie, in any direction: the frame after the cut starts black, holds, eases out. Menus, the galaxy map and the front end's loads cut. | `fadeS` 0.35 (0 off, at most 2 s); fully black 0.04 s first (`kHoldS`) | drawn as the frame's last layer, a head-locked black quad | [TransitionFade.hpp](../../../src/core/vr/TransitionFade.hpp) `Fades`, `Fade::Update`; [TransitionFade.cpp](../../../src/plugin/TransitionFade.cpp) | unit test; headset 2026-11-06 |
| PRS-17 | A cut inside one view (the player's place re-placed at a first-person conversation's opening) uses the same dip. | the same `fadeS` | the world mode | `Fade::Cut` | fake headset |
| PRS-18 | The flat panel: the game's frame on one quad, world-locked where the head was when it appeared, level, easing round only when the head stays turned away. | 1.4 m ahead; width 2.35 m for menus, the galaxy map, conversations, cutscenes and movies (clamped 1.0..3.5; the menu's flat size row sets all five); height from the frame's aspect (16:9 if out of 0.5..2.5); follow past 70 degrees for 1 s; not shown for more than 5 frames: placed afresh; a re-placement under 5 degrees and 5 cm keeps it | `worldLockedPanels = 1`; off: head-locked as upstream | [xr_session.cpp](../../../src/halcyon/le1/xr_session.cpp) flat quad block; [WorldPanels.cpp](../../../src/plugin/WorldPanels.cpp); [OverlayPlacement.hpp](../../../src/core/vr/OverlayPlacement.hpp) `WorldPanelSettings` | headset |
| PRS-19 | Prerendered movies and dead scenes (no 3D scene alive) are flat always, with no second eye. | none | mode 8, `[DEADVETO]` | `CineFinishDecision` | fake headset |
| PRS-20 | A game screen the mod admits (mode 7, or 10 with an active publication) owns the view: the world stays in stereo as on foot and the outgoing cinematic's latch is cleared, not merely vetoed. | `WorldGuiOwnsView(mode, owned, frontEnd)`: mode 7 or 10, owned, not the front end | never during the start-up front end | [PresentationHold.hpp](../../../src/core/vr/PresentationHold.hpp); `liveWorldGui` in [xr_session.cpp](../../../src/halcyon/le1/xr_session.cpp) | fake headset, 2026-11-08; headset pending |
| PRS-21 | A screen opened by our menu or the rail reserves its presentation before the game's call changes the mode, so no flat-panel frame shows in between. | reservation 500 ms (`Opening::kTimeoutMs`), matched by request id and visible screen, cancelled on failure; heartbeat `flat-panel frames during reservation` expected 0 | play modes 0..4 only | [WorldUi.hpp](../../../src/core/vr/WorldUi.hpp) `Opening`; [WorldUi.cpp](../../../src/plugin/WorldUi.cpp) | fake headset |
| PRS-22 | The galaxy map open in the CIC (mode 9, and the opening and closing mode 6) is the world as on foot: no flat panel, no 3D screen, and table ownership beats an outgoing movie's latch on the return from travel. | the mod's state is fresh for 400 ms; a console use keeps the opening's window 4 s, and through a call it began | `[Galaxy] hologram = 1` | `GalaxyHologram`, `GalaxyHeld` seams; [GalaxyHologram.cpp](../../../src/plugin/GalaxyHologram.cpp) | fake headset; headset pending |
| PRS-23 | A first-person conversation (`conversations = world`) is the world as on foot in mode 5; a matinee that walks Shepard, held by the mod, likewise in mode 6. The host trusts the mod's word for a bounded time. | fresh 1.5 s; through a stalled game thread up to 10 s; "armed" counts 150 ms; a mode change is bridged 250 ms; a handover to a GUI screen 300 ms | off by default | [ConversationWorld.cpp](../../../src/plugin/ConversationWorld.cpp); `ConversationWorldOrComing`, `ConversationInWorld`, `ConversationHandover` | fake headset |
| PRS-24 | Under the first-person choice the scan's set pieces and scenes with no place for the player still go to the 3D screen. | 13 set pieces (`cw::IsSetPiece`); `worldSetPieces = screen`; `worldAutoScreen = on` | the world mode | [ConversationWorld.hpp](../../../src/core/vr/ConversationWorld.hpp) | fake headset |
| PRS-25 | The bypass minigame (mode 10) renders the world with a replayed second eye like gameplay. | seam `SfrModeAllowsReplay` | mode 10 | [render_hook.cpp](../../../src/halcyon/le1/render_hook.cpp) | fake headset |
| PRS-26 | In plain VR gameplay the game's UI goes to the body-locked HUD panel; in a screened scene to the UI layer; with a game screen up to the room or wrist panel; over the galaxy table to its own panel. Otherwise it stays in the eye images. | log `overlays: game UI goes to ...` on each change | the panel is armed for the next frame only if this one showed it | [HalcyonOverlays.cpp](../../../src/plugin/HalcyonOverlays.cpp) | fake headset |
| PRS-27 | The game pointer is live wherever the game is shown on a flat quad (menus, the galaxy map when flat), on a world UI panel and on the screen's UI layer; not on flat conversations or cutscenes. | `[Pointer] gameMenus = 1`, `flatCine = 0`; the quad counts as shown 250 ms after its last submit | not while our menu, Halcyon's menu or the calibration is up | [GamePointer.cpp](../../../src/plugin/GamePointer.cpp) | fake headset |
| PRS-28 | Our main menu is drawn over whatever is presented and pauses the game on foot and in the Mako. Not drawn for over a second (the session stopped or the headset asleep) it closes by itself and a running calibration is cancelled. | `kNotDrawnMs` 1000; `[Menu] pause = 1` | the pause is `WorldInfo.PauseGame` through the mod, HostServices v16 | [MainMenu.cpp](../../../src/plugin/MainMenu.cpp) `OnPresent`; [MenuPause.cpp](../../../src/game/MenuPause.cpp) | fake headset |
| PRS-29 | The conversation camera's depth of field is turned off while a first-person conversation or moment lasts, and put back to the value read before. | `scale set DepthOfField False` | the world mode | [DepthOfFieldHold.cpp](../../../src/game/DepthOfFieldHold.cpp) | fake headset |
| PRS-30 | Halcyon's VR cine filling the view (the director's camera as the player's head) is not what ships: with the cutscene and conversation toggles on, the screen takes every VR cine. | `d.mevrScreen = vrCineActive && (frontEndScreen or CineScreenWanted)` | `[Cinematics] conversations` / `cutscenes` off: Halcyon's own VR cine or flat panel, by its toggles | `CineFinishDecision` | fake headset |

# Arbitration

The tests in `CineFinishDecision` run in this order; the first that matches decides.

| order | owner | condition | result |
|---|---|---|---|
| 1 | the front end hold | start-up, before the first gameplay or load | the front end's panel or screen; a world-GUI opening never overrides it |
| 2 | the galaxy table | mode 9 with the hologram, or `GalaxyHeld` (opening, closing, return from travel) | plain VR |
| 3 | an admitted game screen | `WorldGuiOwnsView` | plain VR; the cinematic latch cleared |
| 4 | a first-person conversation or held moment | mode 5 or 6 with the mod's state fresh or coming; mode 7 right after one | plain VR |
| 5 | a cinematic context | conversation or cutscene with its VR toggle on and the scene alive | the 3D screen |
| 6 | flat | a manual flat flag, a cinematic without VR, a menu with `menuFlat` and no world GUI, the galaxy map without the hologram, a movie | the flat panel |
| 7 | the VR-entry hold | plain VR decided after another view | the previous view for 100 ms |

Inputs: while a scene is on the screen the pad reaches the game as in the flat game (the wheel, skip); the triggers are kept
from the game only while a controller's ray is on the UI layer or a flat screen.

# Problems ME1 already solved

| problem | fix | where |
|---|---|---|
| The title's 3D backdrop showed as plain VR for 1.5-2 s; the title's 6, 0, 7 flashed the world for a frame; a cutscene latch outlived the main menu | the front end hold and the 100 ms VR-entry hold | [research/56](../../../research/56-start-and-front-end.md) |
| The front end's flat picture moved with the head | the front end on the fixed 3D screen | [research/56](../../../research/56-start-and-front-end.md) |
| "A little cross-eyed", characters "physically large" on a 3.0 m screen at 2.6 m with 1.5% depth | 2.9 m at 3.5 m, depth 1.0% | [research/52](../../../research/52-cine-3d-screen.md) |
| A conversation's first frame reported the gameplay lens and set the cameras 6.5 units apart | plan at most an 80-degree lens; scale the spacing to the drawn lens | [research/52](../../../research/52-cine-3d-screen.md) |
| The wheel and subtitles sat behind the faces they cover, crossing the eyes | the UI on its own layer 0.5 m in front | [research/52](../../../research/52-cine-3d-screen.md) |
| An over-the-shoulder head came 1.33 degrees out of the screen | `convergeM` 0.8 | [research/52](../../../research/52-cine-3d-screen.md) |
| The screen moved when the player leaned in a scene that flips between conversation and cutscene | one kind, no follow, 3 s gaps | [research/52](../../../research/52-cine-3d-screen.md) |
| Half the display rate on the screen read as 30 ms of CPU frame time at 90 Hz | `fullRate` | [research/52](../../../research/52-cine-3d-screen.md) |
| Tunnel vision and slight motion sickness around a screen in the dark | the floor and horizon | [research/52](../../../research/52-cine-3d-screen.md) |
| The game's hard camera cut between world and cinematic | the dip from black | [research/52](../../../research/52-cine-3d-screen.md) |
| A game screen opened from the rail showed the flat panel for a few frames | the 500 ms opening reservation | [research/57](../../../research/57-omni-tool-menu.md) |
| Landing's party screen after a movie, with no mode-0 tick, kept the cinema presentation | world-GUI ownership clears the latch | [research/53](../../../research/53-world-screens.md) |
| A returning galaxy map went to the flat panel behind the outgoing movie's latch | table ownership beats it | [research/51](../../../research/51-galaxy-map.md) |
| A first-person conversation flashed the 3D screen at its start, on a handover to a shop, and after a stalled tick | "armed" 150 ms, handover 300 ms, the stall rule up to 10 s | [research/69](../../../research/69-conversations-everywhere.md) |
| A mode-7 screen's `bFullScreen` made the game render its UI world instead of the level | cleared on our screens while they stand in the room | [research/53](../../../research/53-world-screens.md) |

# Code map

| part | files | scope |
|---|---|---|
| Rules | `src/core/vr/CineScreen`, `PresentationHold`, `TransitionFade`, `OverlayPlacement`, `WorldUi` (`Opening`) | game-independent, unit-tested in `tests/Main.cpp` |
| The decision | `src/halcyon/le1/xr_session.cpp` (`DecideCine`, `CineFinishDecision`, the `[MEVR]` seams) | LE1 only |
| Host: front end and VR-entry holds | `src/plugin/PresentationHold.cpp` | LE1 host only (the ledger's R22: missing on the sequels) |
| Host: screen, backdrop, fade, world-locked panels | `src/plugin/CineScreen`, `CinemaBackdrop`, `CinemaBackdropDraw`, `TransitionFade`, `WorldPanels` | in the LE1, LE2 and LE3 host lists (`CMakeLists.txt`); the sequels drive them from `plugin/SequelCinema` |
| Host: overlay composition | `src/plugin/HalcyonOverlays.cpp` | LE1 only (the sequels: `SequelOverlays.cpp`) |
| Host: first-person conversation state | `src/plugin/ConversationWorld.cpp` | LE1 host |
| Mod | `src/game/ConversationWorld`, `AuthoredMoment`, `GalaxyMap`, `WorldUi`, `MenuPause`, `DepthOfFieldHold` | compiled for every game; `ConversationWorld`, `AuthoredMoment` and `GalaxyMap` resolve ME1's names only (the ledger's G36: missing on the sequels) |
| Game mode table | `src/engine/GameAdapter.hpp` `GameMode` | per game, by `MEVR_GAME_LE1/2/3`; ME1's numbers are the ones in the state table |
| HostServices | v16 the menu's pause, v27 `setWorldUi`, v33 `setConversationWorld`, v26 the galaxy scene | host and mod deploy together |

# Settings and defaults

| section | key | default | read by | menu row |
|---|---|---|---|---|
| `[Cinematics]` | `conversations` | the 3D screen (`world` for first person) | host and mod | Comfort > Conversations and cutscenes |
| `[Cinematics]` | `cutscenes`, `frontEnd` | 1, 1 | host | the same row; none |
| `[Cinematics]` | `distanceM`, `widthM`, `aspect` | 3.5, 2.9, 16:9 | host | none |
| `[Cinematics]` | `depth`, `convergeM` | 0.010, 0.8 | host | none |
| `[Cinematics]` | `uiLayer`, `uiForwardM` | 1, 0.5 | host | none |
| `[Cinematics]` | `fullRate` | 1 | host | none |
| `[Cinematics]` | `backdrop`, `backdropBrightness` | 1, 1.0 | host | none |
| `[Cinematics]` | `fadeS` | 0.35 | host | none |
| `[Cinematics]` | `worldSetPieces`, `worldAutoScreen` | `screen`, `on` | mod | none |
| `MELEVR.ini` | `menuFlat`, `galaxyFlat` | pinned on | host | none |
| `MELEVR.ini` | `cineVrConvo`, `cineVrCutscene`, `movieFlatEnabled`, `cineFpsCap` | 1, 1, 1, 1 (the code's) | host | the Conversations row sets the first two |
| `MELEVR.ini` | `menuMonoQuadWidth`, `galaxyMonoQuadWidth`, `convoMonoQuadWidth`, `cutsceneMonoQuadWidth`, `movieMonoQuadWidth` | 2.35 each | host | Advanced > Game UI (flat size) |
| `MELEVR.ini` | `worldLockedPanels`, `panelFollowDeg` | 1, 70 | host | none |
| `[Menu]` | `pause` | 1 | host | Comfort |
| `[Pointer]` | `gameMenus`, `flatCine` | 1, 0 | host | none |
| `[Galaxy]` | `hologram`, `inWorld`, `window` | 1, 1, 1 | mod | none |
| `[WorldUi]` | `screens` | all | mod | Comfort > Game screens |

# Checks

| kind | what |
|---|---|
| Logs | `[VRSTATE]` (every change of the decision, with the toggles and the reason bits), `presentation:`, `cine screen:`, `cinema backdrop:`, `transition fade:`, `world ui: reservation Presents ..., flat-panel frames during reservation ...`, `conversation world:`, `overlays: game UI goes to ...`, `galaxy hologram:` |
| Snapshot keys | `"cineScreen"`, `"worldUi"` in `"mod"`, `"galaxyHologram"`, `"mainMenu"`; layers `backdrop`, `mainmenu`; the per-eye PNGs show which view was up |
| Tools | `tools/cine-screen-check.py` (the screen's disparities from a snapshot), `tools/conversation-session.py` (a played session's flashes and dips), `tools/snapshot.ps1` |
| Console | `mevr convo world on|off`, `mevr convo stall <ms>`, `mevr galaxy use`, `mevr gui <screen>` |
| Unit tests | `tests/Main.cpp`: `TestCineScreen`, `TestPresentationHold`, `TestTransitionFade`, `TestWorldUi` |
| Offline previews | `mevr-cinema-preview` (the backdrop) |
| Runs | `tools/fake-headset-session.ps1` from launch: the boot, title, main menu and a load show flat, screen, flat in the `[VRSTATE]` lines |

# Citations

- Research: [51 galaxy map](../../../research/51-galaxy-map.md) section 8, [52 the 3D cinema screen](../../../research/52-cine-3d-screen.md),
  [53 world screens](../../../research/53-world-screens.md), [56 start and front end](../../../research/56-start-and-front-end.md),
  [57 omni-tool menu](../../../research/57-omni-tool-menu.md), [59 conversations in the world](../../../research/59-conversations-in-world.md),
  [69 conversations everywhere](../../../research/69-conversations-everywhere.md).
- Ledger: [trilogy parity ledger](../../trilogy-parity-ledger.md), rows R20-R22, G23, G25, G36.
- Catalog: [menus-and-game-screens.md](menus-and-game-screens.md), [conversations-and-scenes.md](conversations-and-scenes.md),
  [rendering-and-stereo.md](rendering-and-stereo.md), [vehicles.md](vehicles.md).
- Source: [CineScreen.hpp](../../../src/core/vr/CineScreen.hpp), [PresentationHold.hpp](../../../src/core/vr/PresentationHold.hpp),
  [TransitionFade.hpp](../../../src/core/vr/TransitionFade.hpp), [OverlayPlacement.hpp](../../../src/core/vr/OverlayPlacement.hpp),
  [GameAdapter.hpp](../../../src/engine/GameAdapter.hpp), [xr_session.cpp](../../../src/halcyon/le1/xr_session.cpp),
  [vr_config.h](../../../src/halcyon/le1/vr_config.h).
