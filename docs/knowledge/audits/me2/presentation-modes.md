---
type: Audit
title: ME2 presentation modes against ME1's contracts
description: ME2 shows conversations and cutscenes on ME1's 3D cinema screen with ME1's geometry through shared host code, inside Halcyon ME2's own per-frame decision; the start-up front end, the plain-VR entry hold, first-person conversations and the pointer on flat surfaces are absent, and the flat panel keeps upstream's 1.6 m and 2.2 m.
tags: [audit, me2, parity, presentation, cinema-screen, front-end, flat-panel]
status: draft
timestamp: 2026-11-18T08:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:00:00Z }
game: LE2
against: ../../me1/presentation-modes.md
commit: 940a75c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/presentation-modes.md). Reading only: no game was run; where a note records
a run it is named.

The screen itself is real reuse: `core/vr/CineScreen`, `plugin/CineScreen`, `CinemaBackdrop`, `TransitionFade` and
`WorldPanels` are ME1's modules, reached through `plugin/SequelCinema`, with ME1's defaults (2.9 m at 3.5 m, 1% depth,
UI 0.5 m ahead, 0.35 s dip). The decision of what to show is not shared: it is Halcyon ME2's own block in `me2_xr.cpp`
with `[MEVR]` seams, and ME1's hold rules (`core/vr/PresentationHold`) are not in the LE2 host at all.

1. **No front end and no entry hold.** `plugin/PresentationHold` is in LE1's source list only and the capability
   `frontEnd` is false for ME2. The title and main menu are Halcyon's flat panel; whether ME2 flashes the world or a
   cinematic at start-up, as ME1 did, was never measured.
2. **The flat panel is upstream's size and was fixed in the room only on 2026-11-17,** after two headset reports that
   ME1's world-locked panels had already answered (followed the face, then lay at the floor).
3. **`[Cinematics] conversations = 0` means two things.** "In VR" on ME1, "flat panel" on ME2; and Halcyon ME2's own
   first-person conversation (`ConvoFirstPerson`) is not pinned off by the baseline, so a hand-edited file takes a
   conversation away from the screen.

| rating | count | ids |
|---|---|---|
| same | 5 | PRS-03, 04, 16, 21, 28 |
| same core, other inputs | 6 | PRS-02, 05, 06, 10, 18, 30 |
| adapter | 6 | PRS-01, 09, 19, 20, 22, 26 |
| partial | 2 | PRS-07, 27 |
| missing | 7 | PRS-12, 13, 14, 17, 23, 24, 29 |
| n/a | 1 | PRS-25 |
| unknown | 3 | PRS-08, 11, 15 |

# Contract by contract

| id | rating | how ME2 gets it | where it differs | evidence |
|---|---|---|---|---|
| PRS-01 | adapter | Halcyon ME2's own decision each frame, logged `[VRSTATE]` on every change with a bit for our screen | not `DecideCine` / `CineFinishDecision`; the inputs are upstream's (`moviePlayback`, `s_fovCine`, its conversation latch) plus our seams | `me2_xr.cpp:1899-1941` |
| PRS-02 | same core, other inputs | `mevrScreen` = SFR on, mode 5 or 6, a cine field of view, no movie, no first-person latch, and `MevrSeams::CineScreenWanted` | the baseline pins Halcyon's own VR cines off and the screen stands beside them, where ME1 requires them on. The menu offers 3D screen or flat only. Modes 5 and 6 are raw numbers that happen to be ME2's | `me2_xr.cpp:1906-1921`, `me2_menu.cpp:524-526`; research/le2-cinema |
| PRS-03 | same | `cine_screen::QuadsNow` and `world_panels::Place` | none: 2.9 m, 3.5 m, 16:9 | `SequelCinema.cpp:44-49`; research/le2-cinema (defaults) |
| PRS-04 | same | `wp::Kind::Conversation` / `Cutscene` take `ScreenPanelSettings` | none | `WorldPanels.cpp:102`, `SequelCinema.cpp:47` |
| PRS-05 | same core, other inputs | the same crop per eye | ME2's eye images are the game's 16:9 window, not the headset's shape; `tools/cine-screen-check.py` was never run on them, so infinity's disparity is unmeasured | `SequelCinema.cpp:83-99` |
| PRS-06 | same core, other inputs | `HalfSeparationUU(halfH)` plans with the lens of the view being built | no after-the-fact `ScreenLensScale`: ME2's calcview hook has this frame's lens, so ME1's first-frame problem does not arise; unknown lens: planned at the 80-degree cap, as ME1 | `SequelCinema.cpp:15-23` |
| PRS-07 | partial | `FillUi`: the HUD panel's image on a layer `uiForwardM` in front, scaled to the same angle, cut to the band | the game pointer has no plane there (`CineScreenPointerPlane` is called only on LE1): replies are picked with the stick | `SequelCinema.cpp:127-148`, `SequelOverlays.cpp:170` |
| PRS-08 | unknown | the UI goes to the panel at the movie's authored layout (`SetAuthoredHudLayout`) | whether ME2's renderer ever used a half-size conversation layout, and whether the wheel is ME1's size on the screen, is not recorded. A snapshot's `-hud.png` of a wheel on the screen, measured against ME1's, settles it | `SequelOverlays.cpp:188` |
| PRS-09 | adapter | `allow1x = !screen or CineScreenFullRate()` in upstream's pacing | ME1's key and default; the mechanism is Halcyon ME2's display-locked pacing. A note records 11.11 ms and 22.22 ms targets at 90 Hz | `me2_xr.cpp:1686`; research/le2-cinema "Freedom's Progress evidence" |
| PRS-10 | same core, other inputs | `cinema_backdrop::Layer` | its image shape is derived from the eye's field of view because ME2's eye image is 16:9; no floor under a head-locked screen | `SequelCinema.cpp:51-80` |
| PRS-11 | unknown | research/le2-cinema: "calcview_hook.cpp keeps headset motion out of the director's camera" | not traced in code for this audit; lean and the front end are not covered. A replayed head turn during a screened scene (the eye images must not move) settles it | research/le2-cinema |
| PRS-12 | missing (ported 2026-11-18 by lane FE: research/56 "The sequels", T18) | none | `plugin/PresentationHold` and `core/vr/PresentationHold` are not in the LE2 host's list; `frontEnd` capability false | `CMakeLists.txt:811-812`, `:929`; `RendererLe2.cpp:41-63` |
| PRS-13 | missing (ported 2026-11-18 by lane FE: research/56 "The sequels", T18) | the title and main menu are the flat panel in the room | no 3D screen for the front end, no boot rule | the same; `HalcyonLe2Bridge.cpp:347-373` |
| PRS-14 | missing | upstream's own transient rules (`[VRFAILOPEN]`: unknown modes fail open to VR) | the opposite default from ME1's 100 ms hold of the previous view | `me2_xr.cpp:1901-1904` |
| PRS-15 | unknown | upstream | whether ME2's renderer latches a cine by camera name at all was not read | a start-up log with `[VRSTATE]` lines |
| PRS-16 | same | `MevrSeams::TransitionFadeLayer`, the frame's last layer | none in the rule; a note records the 0.35 s fade back to mode 0 | `me2_xr.cpp:3058-3062`; research/le2-cinema |
| PRS-17 | missing | none | no first-person conversation | PRS-23 |
| PRS-18 | same core, other inputs | Halcyon's flat quad, re-placed by `MevrSeams::PlacePanel` through `world_panels::Place` | 1.6 m and 2.2 m (ME1 1.4 m and 2.35 m); a cutscene's width times `CineScreenZoom`; follow and gap rules ME1's | `me2_xr.cpp:92-93`, `:2020-2033`, `:3041-3050`; `HalcyonLe2Bridge.cpp:354-373` |
| PRS-19 | adapter | `moviePlayback` from upstream's decoder and loading detection keeps a movie flat | not the mode byte's 8 | `me2_xr.cpp:1901-1903`, `:1917` |
| PRS-20 | adapter | `MevrSeams::WorldUiOwnsGui(mode)` keeps the GUI mode out of the flat branch | whether an outgoing cinematic's latch is cleared (ME1's landing party screen case) is not handled explicitly | `MenusLe2.cpp:550-558` |
| PRS-21 | same | `world_ui::Ownership` bits: active, or a reservation pending in a play mode | none | `MenusLe2.cpp:203-205` |
| PRS-22 | adapter | `MevrSeams::GalaxyOwnsPresentation` for the map's modes and its opening and closing cinematic | ME2's map modes are 9 and 10 (orbit); travel and the return from it were not seen in a run | `me2_xr.cpp:1913-1921`; ledger G25 |
| PRS-23 | missing | none | `plugin/ConversationWorld` (host) is LE1-only; capability false | `CMakeLists.txt:956-957` |
| PRS-24 | missing | none | follows PRS-23 | |
| PRS-25 | n/a | ME2 has no bypass minigame mode; its mode 10 is the orbital scan | the scan is the galaxy flow's or flat | `GameAdapter.hpp:51-53`, `WorldUi.hpp:224-226` |
| PRS-26 | adapter | `SequelOverlays.cpp` routes the UI: HUD panel in gameplay, the screen's UI layer, a world screen's place, the galaxy disc | a second composition beside LE1's; ME1's `overlays: game UI goes to ...` change log was not found on this path | `SequelOverlays.cpp:145-186` |
| PRS-27 | partial | the pointer on world UI panels | not on the flat panel (menus, galaxy when flat), not on the screen's UI layer | `MenusLe2.cpp:266-270` |
| PRS-28 | same | `menus_le2::Layers` puts the menu alone on top; `main_menu::OnPresent` | the pause is ME2's `bPlayersOnly`; in the Hammerhead not recorded | `MenusLe2.cpp:396-443`, `HalcyonLe2Bridge.cpp:303` |
| PRS-29 | missing | `game/DepthOfFieldHold` on ME2 serves the galaxy flow only | a port must not `scale set` during a load on ME2 (gotcha me2-scale-set-during-load) | `DepthOfFieldHold.cpp:5` |
| PRS-30 | same core, other inputs | the screen takes every cine that `CineScreenWanted` admits; Halcyon's VR cines are pinned off | `[FirstPerson] ConvoFirstPerson` is not pinned; set by hand it latches the conversation away from the screen | `me2_xr.cpp:1899-1900`, `:1917`; `me2_menu.cpp:524-526` |

# Reinvented

| ME2 code | ME1 or shared code | what differs | what unifying takes |
|---|---|---|---|
| The decision block in `me2_xr.cpp` (upstream's, with our seams) | `DecideCine`, `CineFinishDecision` in LE1's `xr_session.cpp`, with `core/vr/PresentationHold` | two decisions with different defaults for a transient mode (ME2 fails open to VR; ME1 holds the previous view) | lift the decision into a game-independent function taking facts (mode class, movie, scene alive, owners) and call it from both trees; `PresentationHold` is already game-independent and unit-tested |
| `convo_fp.cpp` (Halcyon ME2's first-person conversation) | `game/ConversationWorld` | a parallel implementation, compiled in, off by default, not pinned | pin it off in `ApplyMevrBaseline` until ME1's is ported |
| LE1 composes the screen's quads inline in `xr_session.cpp`; the sequels in `sequel_cinema::Layers` | the same module underneath | here LE1 holds the older copy | move LE1 to `sequel_cinema::Layers` (capability `hostCineScreen`) |

# Rediscovered problems

| problem on ME2 | when and cost | ME1 already held it | sources |
|---|---|---|---|
| The flat menus moved with the head | owner's headset report, 2026-11-17 (c4602f1f) | "The front end's flat picture moved with the head" | catalog "Problems ME1 already solved"; [research/56](../../../../research/56-start-and-front-end.md) |
| The flat panels lay at the floor before a recenter | the same day (e580550c) | panels placed at the head's height and heading when they appear (PRS-18) | `HalcyonLe2Bridge.cpp:347-354` |
| A `scale set` during a load faulted the render thread in the first gameplay frame | 9 of 9 runs, 2026-11-14 | ME1 applies its engine settings only once a player exists and again per level; the depth-of-field hold reads the value before | [gotcha](../../gotchas/me2-scale-set-during-load.md) |
| Not yet met but waiting: the title's world flash and a cutscene latch outliving the menu | unmeasured on ME2 | the front-end hold and the 100 ms entry hold | [research/56](../../../../research/56-start-and-front-end.md) |

# Forced by the game

- ME2's renderer detects movies by decoder activity, not by a mode byte (upstream's `[VRFAILOPEN]`).
- ME2's eye images are the game's 16:9 window; ME1's are the headset's shape. The backdrop and the band computations
  must derive the shape.
- ME2's orbit and scan mode, and its interrupts on the triggers during a screened conversation
  (`PadMerge::ConversationTriggers`); a taken and a missed interrupt are still unverified (research/le2-cinema).
- ME2 has no minigame mode of ME1's kind.

# Not forced

- No front-end hold and no entry hold: the rules are game-independent code that is simply not linked.
- The flat panel's 1.6 m and 2.2 m.
- No pointer on the flat panel or the UI layer.
- The meaning of `conversations = 0`.
- `ConvoFirstPerson` unpinned.
- The cinema screen's depth never measured on ME2.

# What to do

1. **Measure the start-up.** One fake-headset launch with `[VRSTATE]` lines from process start to the first save's
   gameplay; list every frame that is not the flat panel. If any: link `PresentationHold` into the LE2 host and call
   `FrontEndHold` and `HoldPlainVr` from `me2_xr.cpp`'s decision. Check: `tests` for `PresentationHold` already exist; the
   run shows `presentation:` lines and no world frame before the first load.
2. **The flat panel's numbers and pointer** (menus-and-game-screens.md, item 2).
3. **Pin `ConvoFirstPerson` off** in `me2_menu.cpp` `ApplyMevrBaseline`. Check: `[MEVR-BASELINE]` names it with a
   hand-edited file.
4. **One meaning for `[Cinematics] conversations`** across games (`plugin/CineScreen`'s reader and the menu's chips).
   Check: `mevr-menu-preview --le2` and a unit test of the reader.
5. **Measure the screen's depth on ME2** with `tools/cine-screen-check.py` on a snapshot pair from the shuttle
   conversation. Check: infinity's disparity within ME1's 0.8 of the eye distance.
6. **The pointer on the UI layer** (`CineScreenPointerPlane` from `SequelCinema.cpp`). Check: a replayed ray picks a reply.
7. **One decision function** for the three renderers, after 1.

# Citations

- Catalog: [presentation-modes.md](../../me1/presentation-modes.md). Sibling audits:
  [menus-and-game-screens.md](menus-and-game-screens.md), [conversations-and-scenes.md](conversations-and-scenes.md).
- Ledger: [trilogy parity ledger](../../../trilogy-parity-ledger.md) R21, R22, R24. Disagreements: R21 "adapted V" hides
  the missing pointer plane, the unmeasured depth and the unpinned first-person key; R22's "Halcyon ME2's flat panel
  (MonoMenus)" is stale since e580550c (the panels are placed by `world_panels::Place`), and still correct that the
  hold rules are missing.
- Research: [le2-cinema](../../../../research/le2-cinema.md), [52 cinema screen](../../../../research/52-cine-3d-screen.md),
  [56 start and front end](../../../../research/56-start-and-front-end.md), [trilogy-seams](../../../../research/trilogy-seams.md).
- Source: [me2_xr.cpp](../../../../src/halcyon/le2/me2_xr.cpp), [me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp),
  [SequelCinema.cpp](../../../../src/plugin/SequelCinema.cpp), [SequelOverlays.cpp](../../../../src/plugin/SequelOverlays.cpp),
  [MenusLe2.cpp](../../../../src/plugin/MenusLe2.cpp), [HalcyonLe2Bridge.cpp](../../../../src/plugin/HalcyonLe2Bridge.cpp),
  [WorldPanels.cpp](../../../../src/plugin/WorldPanels.cpp), [RendererLe2.cpp](../../../../src/plugin/RendererLe2.cpp),
  [PresentationHold.hpp](../../../../src/core/vr/PresentationHold.hpp), [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp),
  [CMakeLists.txt](../../../../CMakeLists.txt).
