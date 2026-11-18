---
type: Audit
title: ME3 conversations, cutscenes and scripted scenes against ME1's contracts
description: ME3 reaches ME1's 3D cinema screen, backdrop and fade through the same sequel code as ME2 with ME1's numbers, but its menu says the screen does not exist and refuses the setting; the front-end hold, the pointer on the screen's UI layer and all of first person (31 of 37 contracts) are not on ME3.
tags: [audit, me3, parity, conversations, cinema-screen, first-person, interrupts]
status: draft
timestamp: 2026-11-18T10:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:00:00Z }
game: LE3
against: ../../me1/conversations-and-scenes.md
commit: 583591c6
---

# Verdict

Audited against [the ME1 catalog file](../../me1/conversations-and-scenes.md) by reading code and notes at 583591c6; no
game was run. Every claim below without a run id rests on reading alone.

- **The default presentation is the same reuse as ME2's.** A corroborated conversation (mode 7) or in-engine cinematic
  (mode 8) goes to ME1's 3D screen through `plugin/SequelCinema`, `plugin/CineScreen`, `WorldPanels`, `CinemaBackdrop`
  and `TransitionFade`, with ME1's defaults (2.9 m, 3.5 m, 1.0%, 0.8 m, UI 0.5 m ahead, 0.35 s). No ME3-only copy of any
  of it exists. No separate-eye acceptance run is on record (ledger R21 "adapted U").
- **Everything first person is absent** (CNV-07 to CNV-34, 36, 37), deferred by the owner. ME3 isolates it harder than
  ME2: the mod's three modules are compiled out of the tick and start-up, upstream's `convo_fp` is forced off in its
  setter, and the host half is not in the LE3 host's source list.
- **Finding 1: the menu and the renderer disagree about what a conversation looks like.** The host shows the 3D screen
  by default (`cs::Settings::conversations = true`), while the menu on ME3 always shows "Flat screen", hides the "3D
  screen" button, says "3D cinema ... not available yet", greys the width, distance and depth sliders and refuses every
  `cine_*` write. A player cannot choose, size or move the screen they are looking at except by editing the file.
- **Finding 2: the screen's gate is two mode numbers.** `mevrScreen` needs `gm == 7 || gm == 8`. A cinematic the
  renderer detects by camera and field of view in any other mode (upstream's own note: ME3's in-engine cutscenes "stay
  at gm 0") passes `cine` and `vrEligible`, fails that test, and with `CineVrCutscene` pinned off plays on the flat
  panel. How many scenes that is was never measured.
- **Finding 3: the start-up and front-end contract (CNV-06) is missing**, as on ME2, and the screen's UI layer has no
  pointer (CNV-03), as on ME2; on ME3 the second is a written decision, not an omission.

| rating | count | ids |
|---|---|---|
| same | 0 | |
| same core, other inputs | 1 | CNV-01 |
| adapter | 3 | CNV-02, 04, 05 |
| reinvented | 0 | (see "Reinvented" for code that is not a contract row) |
| partial | 1 | CNV-03 |
| missing | 31 | CNV-06 to CNV-34, 36, 37 |
| n/a | 1 | CNV-35 |
| unknown | 0 | (two open measurements sit inside CNV-01 and CNV-03) |

Against the ledger: R21 "adapted U" is right for the code and hides finding 1 (a player-facing default the menu
denies) and finding 2. R22 "missing" is right. G36 "missing: deferred; `convo_fp` hard-off" is right. `CLAUDE.md` and
the baseline's own comment ("conversations and cutscenes on the native flat screen", `me2_menu.cpp:475`) describe the
state before commit 707921b7 and are out of date.

# Contract by contract

| id | rating | how ME3 gets it | where it differs | evidence |
|---|---|---|---|---|
| CNV-01 | same core, other inputs | `me2_xr.cpp` sets `mevrScreen` for a cinematic in mode 7 or 8 when `MevrSeams::CineScreenWanted` says so; the actual pass-0 and pass-1 images go to `sequel_cinema::Layers`, which places two eye quads with `world_panels::Place` (ME1's `ScreenPanelSettings`: 3 s gaps, no follow) and `cine_screen::QuadsNow`. Defaults are the shared `cs::Settings`: 2.9 m, 3.5 m, 16:9, `fullRate` 1 (the pace asks `CineScreenFullRate`). | The gate: stereo (SFR) on, the FOV and camera detector's `cine`, `vrEligible`, no latched menu, the galaxy map not holding the view, and mode 7 or 8 only (finding 2). `conversations = 0` is the flat panel (ME1: the director's camera in VR; `CineVrConvo` and `CineVrCutscene` are pinned off). A conversation's first second is flat until the conversation camera's class arrives (upstream's `[CONVOLATCH]` note). The menu denies the feature (finding 1). The eye images are the game's window (3072x2216 by default), not the headset's shape. Not seen in a separate-eye run. | `src/halcyon/le3/me2_xr.cpp:1545-1548`, `:1552-1559`, `:1690-1703`, `:1746-1767`, `:1475-1476`, `:2306-2327`; `src/plugin/Seams.cpp:79-85`; `src/plugin/SequelCinema.cpp:25-50`; `src/core/vr/CineScreen.hpp:23-25`; `src/halcyon/le3/me2_menu.cpp:518-520` |
| CNV-02 | adapter | `calcview_hook.cpp` asks `MevrBridge::CineScreenHalfSeparationUU` with the view's own lens; that is `sequel_cinema::HalfSeparationUU`, ME1's `cine_screen::PlanFrame` with the measured eye distance and 100 units per metre. Head look, lean and the FOV fill return early while the screen is up; gameplay convergence and swap-eyes are ignored for the pair. | As ME2: the lens is this view's, so no `ScreenLensScale`; an unknown lens is planned at `kMaxLensHalfH`. No disparity was measured on ME3. A stale pass 0 puts the same centred crop in both eyes (`MonoSourceRects`), counted as `monoFallbackFrames`. | `src/halcyon/le3/calcview_hook.cpp:852`, `:945`, `:1386`, `:1478-1480`; `src/plugin/HalcyonSequelBridge.cpp:413`; `src/plugin/SequelCinema.cpp:15-23`; `src/halcyon/le3/me2_xr.cpp:2192-2233` |
| CNV-03 | partial | Late Scaleform draws go to the shared HUD panel (`MevrSeams::HudPanelDraw` before upstream's mirror); `sequel_cinema::FillUi` places it with ME1's `cs::PlaceUi` and `UiPartShown`, `uiForwardM` 0.5. Upstream's own UI overlay quad is suppressed while the screen is submitted. A fake-headset run recorded the reply wheel in the `-hud.png`, not in the cinema image (session `20261115-200511-376-LE3-navigation`). | No pointer on that plane: `CineScreenPointerPlane` is LE1's, and the LE3 bridge names a quad to the game pointer only for a full GUI panel (mode 9) and the galaxy map; its comment keeps "authored dialogue" on the native pad until a pointer contract is accepted. ME1's "authored size, not the half-size VR layout" seam has no ME3 counterpart; whether upstream's `[UIRATIO]` viewport still shrinks the redirected draws was not settled by reading. | `src/halcyon/le3/d3d_capture.cpp:1828-1838`, `:2004`; `src/plugin/SequelOverlays.cpp:167-170`; `src/plugin/SequelCinema.cpp:124-148`; `src/halcyon/le3/me2_xr.cpp:2626`; `src/plugin/HalcyonLe3Bridge.cpp:347-356`; `src/plugin/CineScreen.cpp:275-280`; [le3-dialogue-cinema](../../../../research/le3-dialogue-cinema.md) |
| CNV-04 | adapter | ME1's `cinema_backdrop::Layer` under the two quads from `sequel_cinema::Layers`; `backdrop` 1, brightness 1.0. | As ME2: its image size is derived from the headset's field of view because the eye images are the game's window; no floor under a head-locked screen. | `src/plugin/SequelCinema.cpp:50-82`; `CMakeLists.txt:1169` |
| CNV-05 | adapter | ME1's `transition_fade::Layer` as the frame's last layer; ME3 classifies the screen (1), the world (0), a native cinematic on the flat panel in mode 7 or 8 (2) and other flat (3). | As ME2: the sequel seam never passes ME1's `cut`. A flat cinematic outside modes 7 and 8 is classed "other flat". | `src/halcyon/le3/me2_xr.cpp:2842-2847`; `src/plugin/HalcyonSequelBridge.cpp:344-346`; `CMakeLists.txt:1170` |
| CNV-06 | missing (ported 2026-11-18 by lane FE: research/56 "The sequels", T18) | `plugin/PresentationHold.cpp` is in LE1's host list only; no `FrontEndHold`, `HoldPlainVr`, `FrontEndOnScreen` on ME3. The title, its menus, loads and movies are the renderer's mono panel, placed in the room by `PlaceNativePanel` (`world_panels::Place`, kind by `engine::adapter::GameMode`). | No 3D title screen, no 30-frame hold, no 100 ms wait before plain VR. Upstream has its own latches against flapping (`[MOVIEMONO]`, `[ENGCINE]`, a submit-topology guard). Whether ME3 flips between views at start-up was never measured (the ledger's own next step). | `CMakeLists.txt:907` (LE1 list), `:1143-1192` (LE3 list, absent); `src/plugin/HalcyonLe3Bridge.cpp:315-325`; `src/halcyon/le3/me2_xr.cpp:1646-1659`, `:1705-1720`, `:2815` |
| CNV-07 | missing | Not on ME3. `conversation_world::TryStartup` and `Tick` are inside `#if !defined(MEVR_GAME_LE3)`; `ue3::runtime::Ready()` is false off LE1; `plugin/ConversationWorld.cpp` is not in the LE3 host list, so `setConversationWorld` is not served; upstream's `ConvoFp::SetEnabled` forces false under `MEVR_ASI`. A mode-7 conversation is the 3D screen (CNV-01). | The game itself locks movement and look in the conversation mode (`SFXGameModeConversation`: `bAllowMovement` and `bAllowRotationUpdate` false), so a seated scene rejects traversal natively; head tracking is parked on the screen, not retained. A hand-written `conversations = world` reads as the screen. | `src/mod/Mod.cpp:392-394`, `:1915-1921`, `:755-760`; `src/ue3/Runtime.cpp:475-477`; `CMakeLists.txt:934-935`; `src/halcyon/le3/convo_fp.cpp:205-207`; `src/plugin/CineScreen.cpp:83-90`; [le3-dialogue-cinema](../../../../research/le3-dialogue-cinema.md) |
| CNV-08 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-09 | missing | `kConversationSetPositionRotator` is 0 for LE3 and `EnsureConversationHooks` is compiled out. | No LE3 address was looked for. | `src/engine/Addresses.hpp:501`; `src/mod/Mod.cpp:1915-1918` |
| CNV-10 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-11 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-12 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-13 | missing | Part of CNV-07's module. | The 0.43 m and 0.18 m view offset is Halcyon LE1's; ME3's would have to be measured. | as CNV-07 |
| CNV-14 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-15 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-16 | missing | Part of CNV-07's module. The LE3 bridge keeps the left stick's axes in the conversation mode for the game's reply wheel and zeroes them in the cinematic mode. | ME3's reply wheel reads the left stick; the game ignores move input there. | `src/plugin/HalcyonLe3Bridge.cpp:204-219` |
| CNV-17 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-18 | missing | Part of CNV-07's module; with no walk there is nothing to latch. | | as CNV-07 |
| CNV-19 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-20 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-21 | missing | Part of CNV-07's module; `cw::IsStagedScene` holds ME1 conversation names only. | | `src/core/vr/ConversationWorld.cpp` |
| CNV-22 | missing | Not needed while every ME3 conversation is on the screen; `cw::IsSetPiece` holds ME1 names only; no ME3 scan on record. | | `src/core/vr/ConversationWorld.cpp` |
| CNV-23 | missing | Part of CNV-07's module. On the screen the director's near plane and depth of field are wanted. | Upstream's `ConvoFpDisableDof` is dead with `convo_fp` off. | as CNV-07 |
| CNV-24 | missing | Host half absent (the subtitle and wheel quads are `plugin/ConversationWorld`'s). | | `CMakeLists.txt:934-935` |
| CNV-25 | missing | `conversation_flow::Tick` is compiled out on LE3; `kInterruptConversation` is 0; `cw::FlowEntries` is ME1's table. | | `src/mod/Mod.cpp:1923-1927`; `src/engine/Addresses.hpp:502` |
| CNV-26 | missing | Part of CNV-25. | | as CNV-25 |
| CNV-27 | missing | Part of CNV-07's module. The owner's deferral names seated scenes; ME3's native movement lock is what rejects traversal there. | "Retaining head tracking" in a seated scene is not met: the screen parks head look. | as CNV-07; `src/halcyon/le3/calcview_hook.cpp:852` |
| CNV-28 | missing | `authored_moment::Tick` is compiled out on LE3; `kMoveWithInterpMoveTrack` is 0. A matinee that walks Shepard is a cinematic on the screen or the flat panel. | | `src/mod/Mod.cpp:1923-1927`; `src/engine/Addresses.hpp:503` |
| CNV-29 | missing | Part of CNV-28. | | as CNV-28 |
| CNV-30 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-31 | missing | Part of CNV-28. | | as CNV-28 |
| CNV-32 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-33 | missing | No "armed" or "handing over" word reaches ME3's decision; the screen-to-GUI and screen-to-world changes go through CNV-05's dip. | Upstream records an unsolved "mono blip at the start of a conversation" (`[CINETRACE]`), the same class of flash ME1's hold answers. | `src/halcyon/le3/me2_xr.cpp:1809-1823` |
| CNV-34 | missing | See [the audio audit](audio.md), AUD-07 to AUD-09: no source hook (`kSoundSourceUpdate` 0, the hook's install compiled out) and no world conversation. | | `src/engine/Addresses.hpp:495`; `src/mod/Mod.cpp:1915-1919` |
| CNV-35 | n/a | ME3 is taken to play through Wwise; `openal_soft::Attach` is compiled for LE1 only. See the audio audit, AUD-01. | The exe's imports were never checked (ledger G37's open step). | `src/plugin/Entry.cpp:260-264` |
| CNV-36 | missing | Part of CNV-07's module; `cw::ShownEarlyFor` is an ME1 table. | | as CNV-07 |
| CNV-37 | missing | For conversations: no world view is held. The helpers: `kSkelUpdateBounds` and `kPrimitiveUmbraBounds` are 0 on LE3 (the ledger records both functions found, R29), `game/CameraCollision` is inside `MEVR_GAME_LE1`, the `head_aim.cpp` seams are LE1's. | Whether ME3 culls the body or pulls the camera in on foot is a first-person question (the body audit). | `src/engine/Addresses.hpp:493-494`; `src/game/CameraCollision.cpp:21`; `src/engine/BodyBounds.cpp:55` |

# Against ME2

| ME2 finding | on ME3 | note |
|---|---|---|
| Upstream's `ConvoFirstPerson` is not pinned off | fine | ME3's `ConvoFp::SetEnabled` forces false under `MEVR_ASI` (`convo_fp.cpp:205-207`). It is pinned at the setter, not in `ApplyMevrBaseline`, so the baseline's log line does not name it; ME2 still lacks either. One rule in both baselines would be the unified form. |
| CNV-06 missing; panels followed the face, then lay at the floor (2026-11-17) | same gap, not the two symptoms | ME3's native panels went through `world_panels::Place` first (112264d5); ME2's fixes copied ME3 ("like ... ME3's menus"). The start-up flips were measured on neither. |
| No pointer on the screen's UI layer | same defect | On ME3 it is a recorded decision in `HalcyonLe3Bridge.cpp:347-349`, awaiting a pointer contract for the wheel. |
| ME1's depth check (`tools/cine-screen-check.py`) never run on the sequel's eye images | same defect | ME3's window is 3072x2216, a third shape. |
| `conversations = 0` means flat, not "In VR" | same | `CineVrConvo`, `CineVrCutscene` pinned off (`me2_menu.cpp:518-520`). |
| The fade seam never passes `cut` | same | shared `HalcyonSequelBridge.cpp:344-346`. |
| A stale eye pair on the cinema quads (54904310) | fine | The shared `Layers` shows one centred crop in both eyes for a stale source and counts it. |
| First-person modules compile, tick and stand down by `Ready()` | different | ME3 compiles the calls out of `Mod.cpp`; the modules' `LogStats`, `StatusJson` and `Shutdown` still run. |
| Interrupt route seen, a taken and a missed interrupt not | same | The triggers pass through `IsConversationMode` (mode 7 on ME3); no ME3 interrupt window was run. |
| The menu is honest about what is missing | different, worse | ME2's menu offers 3D or flat and both work. ME3's shows flat and refuses, while the 3D screen plays (finding 1). |

Of the summary's five mechanisms: 1 (a gate in ME1's terms) does not occur here, because ME3's host goes through
`engine::adapter::GameMode` and the renderer's raw 7 and 8 are ME3's own; a relative of it does (finding 2: a gate in
mode numbers where the detector is semantic). 2 (half a feature) is finding 1 turned round: the renderer half is in, the
menu half was never updated. 3, 4 and 5 were not found in this category.

# Reinvented

| what | ME3's code | the shared or ME1 code | what unifying takes |
|---|---|---|---|
| First-person conversations | Upstream Halcyon ME3's `src/halcyon/le3/convo_fp.cpp` (eye pose, turn to speaker, its own depth-of-field switch), read at `me2_menu.cpp:648-655`, latched at `me2_xr.cpp:1560-1561`, `:1702-1703` | `src/game/ConversationWorld.cpp` + `src/core/vr/ConversationWorld.cpp` | Nothing to merge: it is forced off. Its reads of the live conversation are evidence for a later adapter; its ImGui rows (`me2_menu.cpp:1227-1244`) still show in the developer menu and do nothing. |
| The presentation decision | `me2_xr.cpp:1545-1767`: FOV hysteresis, camera classes, `[CONVOLATCH]`, `[LATCHDECAY]`, `[ENGCINE]`, `[GM8FOV]`; a third copy beside ME2's | LE1's `DecideCine` with `core/vr/PresentationHold` (unit-tested) | Upstream's, kept by the "touch Halcyon as little as possible" rule. A shared decision core fed semantic contexts would close CNV-06 and finding 2 on both sequels at once; only worth it if CNV-06 is ported. |
| The screen's layer composition | none of its own: `plugin/SequelCinema.cpp` for both sequels | LE1 composes the same quads inline through `MevrBridge::CineScreenQuadsNow` and repeats `PlaceUi` in `CineScreenPointerPlane` | The remaining copy is LE1's (ledger R20). Moving LE1 onto `sequel_cinema::Layers` gives one pointer plane for three games. |
| What ME3's mode numbers mean | Upstream's comments call 8 "Movie", 9 the galaxy map and 10 "title/orbital" (`me2_xr.cpp:1580`, `:1737-1739`) | `engine::adapter::GameMode`: 8 Cinematic, 9 Gui, 10 Movie, 11 Galaxy, 12 Orbital (`GameAdapter.hpp:57-63`) | The deny-list `gm == 9 or 10` happens to be right under both readings. Replace the three raw tests (`:1746`, `:1760`, `:2842`) with the adapter's predicates so one table names the modes. |
| The spoken-reply reader | `game/Thespian.cpp`'s LE3 branches (`BioConversationController`, six slots, `SelectConversationEntry`) | the same file's LE1 and LE2 branches over shared `core/vr/Thespian` and `core/voice/ScriptMatch` | Already the right shape (one policy, a per-game adapter in one file). Not a contract of this catalog file. |

# Rediscovered problems

| problem on ME3 | date, cost | ME1 already had | sources |
|---|---|---|---|
| The bridge zeroed the left stick in the conversation mode: no reply could be chosen. | 2026-11-15, a protected run and commit 0344c2b9 | The catalog's arbitration: the left stick is the wheel's while it is up (CNV-18) | `src/plugin/HalcyonLe3Bridge.cpp:214-219`; [le3-dialogue-cinema](../../../../research/le3-dialogue-cinema.md) |
| A latched conversation presentation carried into the squad menu and stretched its models; a menu read as a conversation. | Upstream's cost, before our fork (`[CONVOLATCH]`, `[LATCHDECAY]`) | ME1's hand-over between a conversation and a GUI screen (CNV-33) and the presentation hold answer the same class | `src/halcyon/le3/me2_xr.cpp:1669-1696` |
| A flat blip at the start of every conversation, unsolved. | Upstream's, recorded as open (`[CINETRACE]`) | ME1's "armed" word and the 100 ms wait (research/56, CNV-33) | `src/halcyon/le3/me2_xr.cpp:1809-1823` |
| The menu text written before the feature and never revisited ("3D cinema ... not available yet"). | Not yet met by a player; a headset report waiting to happen | ME1's rule: every row drives a real setting or is marked Soon | `assets/menu/main.rml:187-190`; `src/plugin/MainMenuSettings.cpp:350-352` |

Not yet rediscovered, waiting for a port of first person: the catalog's whole "already solved" list from "Conversations
teleport actors onto marks" down. ME3 adds one of its own to read first: the conversation mode's native
`IgnoreMoveInput(true)`, which a walk during a conversation would have to lift or work beside.

# Forced by the game

| difference | evidence |
|---|---|
| Mode numbers: 7 conversation, 8 cinematic, 9 GUI, 10 movie, 11 and 12 the maps. | `src/engine/GameAdapter.hpp:57-63` |
| Mode 8 covers in-engine cutscenes and some loading or movie frames; scene activity and the camera class decide. The camera class changes per shot, so a conversation is latched. | `src/halcyon/le3/me2_xr.cpp:1580-1596`, `:1669-1696` |
| The live conversation is `BioConversationController` behind `BioWorldInfo.GetConversationManager()`, not ME1's `BioConversation`; replies go through six visible slots. | [le3-dialogue-cinema](../../../../research/le3-dialogue-cinema.md) "Verified native dialogue contract" |
| The conversation mode ignores move and look input natively; seated scenes cannot be walked. | the same note |
| Interrupts on LT and RT in the conversation mode. | `src/plugin/HalcyonSequelBridge.cpp:378-381` |
| ME1's three hook addresses and its tables (set pieces, staged, flow, shown early, curated) are ME1's exe and ME1's conversation names. | `src/engine/Addresses.hpp:501-503`; `src/core/vr/ConversationWorld.cpp` |
| The eye images are the game's window, so the backdrop sizes itself from the field of view; the lens is known in the view that is built, so no `ScreenLensScale`. | `src/plugin/SequelCinema.cpp:17-19`, `:72-78` |

# Not forced

| difference | note |
|---|---|
| The menu shows "Flat screen", hides "3D screen" and refuses `cine_*` on ME3 | Left from 112264d5; the screen was wired later (707921b7). ME2's menu rows work on the same host code. |
| `mevrScreen` only in modes 7 and 8 | The detector beside it is by camera and field of view; the mode test was added with the screen. Whether to widen it needs the measurement in "What to do" 2. |
| No pointer on the screen's UI layer | The game pointer is in the LE3 host; the wheel's mouse contract was not audited. |
| No front-end hold, no 3D title | The core is game-independent and unit-tested; the measurement the ledger asks for first was not made. |
| First person deferred | The owner's decision, not the engine's. |
| ME1's depth check not run | `tools/cine-screen-check.py` works on snapshots of any game. |
| The baseline's comment and `CLAUDE.md` say "native flat screen" | Documentation behind the code. |

# What to do

Parity work only, ordered by what a player sees.

1. **Make the menu tell the truth on ME3.** `assets/menu/main.rml:187-190` (show "3D screen" for `le3`, drop the "not
   available yet" text), `src/plugin/MainMenuSettings.cpp:350-352` (read `[Cinematics]` as ME2's branch does) and
   `src/core/vr/MenuSettings.cpp:900-903` (take `cine_` out of `UnavailableInLe3Presentation`). Check:
   `mevr-menu-preview` for LE3 shows the chosen value; a click on Flat writes `conversations = 0` to `LE3MEVR.ini` and
   the log's `cine screen:` line follows; `tests/VoiceMenuTests.cpp`-style unit test for the key list. Question for the
   owner first if the screen is not meant to be ME3's default yet: then the default, not the menu, is what is wrong.
2. **Measure which scenes miss the screen.** One session through a conversation, an in-engine cutscene and a level's
   scripted camera, reading `[CINEVR]` lines: every `FLAT panel` with `gameplayCam 0, sceneAlive 1` outside modes 9 and
   10 is a scene finding 2 sends flat. Widen `me2_xr.cpp:1760` to `vrEligible` scenes only if the list is not empty.
3. **Separate-eye acceptance of the screen** (T15): snapshots in a mode-7 conversation and a moving mode-8 cutscene,
   `"cineScreen"."stereoSource"` true and `monoFallbackFrames` near 0, then `python tools/cine-screen-check.py` on the
   pair (the UI layer nearer than every face, infinity at 1.0% of the width).
4. **Measure the start-up before porting CNV-06**: snapshots every half second from launch through the first load,
   beside the renderer's presentation lines. Port `core/vr/PresentationHold` into the sequel bridge (once, for ME2 and
   ME3) only if a frame of plain VR or a changing panel shows.
5. **The pointer on the UI layer**: settle first whether ME3's wheel takes a mouse in mode 7 (`mevr guimouse`); if it
   does, name the screen's UI quad to `game_pointer::NoteGameUiQuad` after `sequel_cinema::FillUi` for both sequels; if
   not, record it and rate CNV-03's pointer n/a.
6. **One rule for upstream's first person**: move ME3's hard-off into both baselines (`ApplyMevrBaseline` in
   `src/halcyon/le2/me2_menu.cpp` and `src/halcyon/le3/me2_menu.cpp`) so the `[MEVR-BASELINE]` line names it on both.
7. **Mode tests through the adapter**: `me2_xr.cpp:1746`, `:1760`, `:2310`, `:2842` use `engine::adapter` predicates;
   correct the comments that call 9 the galaxy map. Check: both sequel hosts build, `[CINEVR]` lines unchanged in a run.
8. **When the owner lifts the deferral (T49)**: as the ME2 audit's step 7, with ME3's `BioConversationController`
   adapter from `game/Thespian.cpp` as the reader's starting point and the native movement lock as the first question.

Questions for the owner: is the 3D screen meant to be ME3's default today (the code says yes, the menu and `CLAUDE.md`
say no)? Should a seated scene keep head tracking before first person is ported (it cannot on the screen)?

# Citations

- Catalog: [ME1 conversations and scenes](../../me1/conversations-and-scenes.md); [how to catalog](../../how-to-catalog.md).
- ME2's audits: [conversations and scenes](../me2/conversations-and-scenes.md), [summary](../me2/summary.md).
- Ledger and plans: [trilogy parity ledger](../../../trilogy-parity-ledger.md) rows R20, R21, R22, R29, G35, G36;
  [trilogy plan](../../../trilogy-plan.md) T15, T48, T49; [ME3 implementation plan](../../../le3-port-implementation-plan.md);
  [reuse audit](../../../le3-port-reuse-audit.md).
- Research: [le3-dialogue-cinema](../../../../research/le3-dialogue-cinema.md), [le3-shared-presentation](../../../../research/le3-shared-presentation.md),
  [le3-menu-body-presentation](../../../../research/le3-menu-body-presentation.md), [le3-renderer-integration](../../../../research/le3-renderer-integration.md),
  [trilogy-first-person](../../../../research/trilogy-first-person.md), [trilogy-seams](../../../../research/trilogy-seams.md),
  [52 cine 3D screen](../../../../research/52-cine-3d-screen.md), [56 start and front end](../../../../research/56-start-and-front-end.md),
  [69 conversations everywhere](../../../../research/69-conversations-everywhere.md).
- ME3 renderer: [me2_xr.cpp](../../../../src/halcyon/le3/me2_xr.cpp), [me2_menu.cpp](../../../../src/halcyon/le3/me2_menu.cpp),
  [calcview_hook.cpp](../../../../src/halcyon/le3/calcview_hook.cpp), [convo_fp.cpp](../../../../src/halcyon/le3/convo_fp.cpp),
  [d3d_capture.cpp](../../../../src/halcyon/le3/d3d_capture.cpp).
- Host: [SequelCinema.cpp](../../../../src/plugin/SequelCinema.cpp), [CineScreen.cpp](../../../../src/plugin/CineScreen.cpp),
  [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp), [HalcyonLe3Bridge.cpp](../../../../src/plugin/HalcyonLe3Bridge.cpp),
  [SequelOverlays.cpp](../../../../src/plugin/SequelOverlays.cpp), [Seams.cpp](../../../../src/plugin/Seams.cpp),
  [MainMenuSettings.cpp](../../../../src/plugin/MainMenuSettings.cpp), [main.rml](../../../../assets/menu/main.rml),
  [MenuSettings.cpp](../../../../src/core/vr/MenuSettings.cpp), [CineScreen.hpp](../../../../src/core/vr/CineScreen.hpp),
  [Entry.cpp](../../../../src/plugin/Entry.cpp).
- Mod and engine: [Mod.cpp](../../../../src/mod/Mod.cpp), [game/ConversationWorld.cpp](../../../../src/game/ConversationWorld.cpp),
  [Thespian.cpp](../../../../src/game/Thespian.cpp), [Addresses.hpp](../../../../src/engine/Addresses.hpp),
  [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp), [Runtime.cpp](../../../../src/ue3/Runtime.cpp),
  [CameraCollision.cpp](../../../../src/game/CameraCollision.cpp), [BodyBounds.cpp](../../../../src/engine/BodyBounds.cpp).
- Build lists: [CMakeLists.txt](../../../../CMakeLists.txt) (LE3 host sources 1143-1192, sequel menu sources 999-1023).
- Commits: 112264d5, 707921b7, 0344c2b9, 3a7bffff.
