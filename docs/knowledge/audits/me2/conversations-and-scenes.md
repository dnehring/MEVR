---
type: Audit
title: ME2 conversations, cutscenes and scripted scenes against ME1's contracts
description: ME2 gets ME1's 3D cinema screen, backdrop and fade from shared code with ME1's numbers; the front-end hold, the pointer on the screen's UI layer and all of first person (31 of 37 contracts) are not on ME2.
tags: [audit, me2, parity, conversations, cinema-screen, first-person, interrupts]
status: draft
timestamp: 2026-11-18T08:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:00:00Z }
game: LE2
against: ../../me1/conversations-and-scenes.md
commit: 940a75c6
---

# Verdict

Audited against [the ME1 catalog file](../../me1/conversations-and-scenes.md) by reading code and notes at 940a75c6;
no game was run. Every claim below without a run id rests on reading alone.

- **The default presentation is real reuse.** ME2's conversations (mode 5) and in-engine cutscenes (6, and 8 when no
  movie decodes) go to ME1's 3D screen through the same `core/vr/CineScreen`, `plugin/CineScreen`, `WorldPanels`,
  `CinemaBackdrop` and `TransitionFade`, with ME1's defaults (2.9 m, 3.5 m, 1.0%, 0.8 m, UI 0.5 m ahead, 0.35 s). Seen on
  the fake headset once (run `20261115-042015-m5-cinema`).
- **Everything first person is absent** (CNV-07 to CNV-34, 36, 37): deferred by the owner 2026-11-15. The mod's modules
  compile for LE2 and stand down (`kModulesOnLayer` false, three hook addresses 0); the host half
  (`plugin/ConversationWorld.cpp`) is not in the LE2 host's source list; the menu hides the row.
- **Finding 1: upstream's own first-person conversation is not pinned off.** Halcyon ME2 ships `convo_fp.cpp`
  (`[FirstPerson] ConvoFirstPerson`), a second implementation of what `game/ConversationWorld` does.
  `ApplyMevrBaseline` pins `CineVrConvo` and `CineVrCutscene` off but not this key; with it set to 1 an armed scene takes
  the conversation away from the 3D screen. The default is 0, so a fresh install is right.
- **Finding 2: the start-up and front-end contract (CNV-06) is missing, and its two visible symptoms were met again in
  the headset on 2026-11-17** (menus followed the face; then menus lay at the floor), both things ME1's world panels
  and research/56 already answered.
- **Finding 3: the cinema screen's UI layer has no pointer plane on ME2**, and the check that proved ME1's depth numbers
  (`tools/cine-screen-check.py`) was never run on ME2's eye images, which have another shape (the game's 16:9 window).
- Interrupts (an ME2 mechanic, no ME1 contract): the triggers reach the game as LT and RT in the conversation mode
  through `pad_merge::ConversationTriggers`; the route was seen, a taken and a missed interrupt at Veetor were not.

| rating | count | ids |
|---|---|---|
| same | 0 | |
| same core, other inputs | 1 | CNV-01 |
| adapter | 3 | CNV-02, 04, 05 |
| reinvented | 0 | (see "Reinvented" for code that is not a contract row) |
| partial | 1 | CNV-03 |
| missing | 31 | CNV-06 to CNV-34, 36, 37 |
| n/a | 1 | CNV-35 |
| unknown | 0 | |

Against the ledger: R21 "adapted V" holds for the screen itself but hides the pointer gap, the unmeasured depth and the
unpinned `ConvoFirstPerson`. R22's cell ("Halcyon ME2's flat panel (MonoMenus)") is out of date since e580550c: the flat
panels are now placed by `world_panels::Place`. G36 "missing" is right.

# Contract by contract

| id | rating | how ME2 gets it | where it differs | evidence |
|---|---|---|---|---|
| CNV-01 | same core, other inputs | `me2_xr.cpp` decides `mevrScreen` for mode 5 or 6 when `[Cinematics]` wants it; `sequel_cinema::Layers` places two eye quads with `world_panels::Place` (ME1's `ScreenPanelSettings`: 3 s gaps, no follow) and `cine_screen::QuadsNow`. Defaults are the shared `cs::Settings`: 2.9 m, 3.5 m, 16:9, `fullRate` 1. | `conversations = 0` means the flat cine panel on ME2 (ME1: the director's camera in VR); "In VR" does not exist. The gate also needs stereo on, no movie decoding and upstream's first-person latch not armed. The eye images are the game's 16:9 window, not the headset's shape. Mode 8 without a movie counts as 6. | `src/halcyon/le2/me2_xr.cpp:1743`, `:1917-1920`, `:2395-2419`; `src/plugin/SequelCinema.cpp:43-47`, `:83-106`; `src/plugin/WorldPanels.cpp:100-102`; `src/core/vr/CineScreen.hpp:23-34`; run `20261115-042015-m5-cinema` |
| CNV-02 | adapter | `sequel_cinema::HalfSeparationUU` asks ME1's `cine_screen::PlanFrame` with the view's own lens, the measured eye distance and 100 units per metre; `calcview_hook.cpp` parks head look, lean and the FOV fill on the screen. | The lens is this view's, so ME1's `ScreenLensScale` correction is not needed; an unknown lens is planned at `kMaxLensHalfH` as on ME1. No disparity was measured on ME2. | `src/plugin/SequelCinema.cpp:15-23`; `src/halcyon/le2/calcview_hook.cpp:811-814`, `:950`, `:1529-1541`; `src/core/vr/CineScreen.hpp:67`, `:74` |
| CNV-03 | partial | The game's scene UI goes to the shared HUD panel; `sequel_cinema::FillUi` places it with ME1's `cs::PlaceUi` and `UiPartShown`, `uiForwardM` 0.5. Reply wheel and subtitles were on their own layer in the M5 run. | No pointer on that plane: ME1's `CineScreenPointerPlane` is LE1-only and the sequels name a game UI quad to the pointer only for the galaxy map and menus. ME1's "authored size, not the half-size VR layout" seam has no ME2 counterpart; whether ME2 needs one is not recorded. | `src/plugin/SequelCinema.cpp:127-148`; `src/plugin/SequelOverlays.cpp:170`, `:239-243`; `src/plugin/CineScreen.cpp:250`, `:275-294` |
| CNV-04 | adapter | ME1's `cinema_backdrop::Layer` under the two quads, `backdrop` 1, brightness 1.0. | Its image size is derived from the headset's field of view because ME2's eye images are 16:9; no floor under a head-locked screen. | `src/plugin/SequelCinema.cpp:50-82`; `CMakeLists.txt:1142-1143` |
| CNV-05 | adapter | ME1's `transition_fade::Layer` as the frame's last layer; ME2 classifies world, screen, cinematic flat, other flat. 0.35 s seen on the return to mode 0. | The sequel seam never passes ME1's `cut` (only a first-person conversation uses it). | `src/halcyon/le2/me2_xr.cpp:3059-3066`; `src/plugin/HalcyonSequelBridge.cpp:344-346`; `src/plugin/TransitionFade.cpp:119-135`, `:178-186`; [le2-cinema](../../../../research/le2-cinema.md) |
| CNV-06 | missing | `core/vr/PresentationHold` and `plugin/PresentationHold.cpp` are in LE1's host list only; the sequel bridge has no `FrontEndHold`, `HoldPlainVr`, `FrontEndOnScreen`. ME2's title and menus are upstream's flat panel (`MonoMenus`), placed in the room by `world_panels::Place` since 2026-11-17. | No 3D title screen, no 30-frame hold, no 100 ms wait before plain VR. Whether ME2 flips between views at start-up was never measured (the ledger's own next step). Upstream has its own latches against flapping instead. | `CMakeLists.txt:929` (LE1 list), `:1086-1156` (LE2 list, absent); `src/plugin/HalcyonLe2Bridge.cpp:347-354`; `src/halcyon/le2/me2_xr.cpp:1777-1791`, `:1862`, `:1877` |
| CNV-07 | missing | Not on ME2. `game/ConversationWorld` compiles and ticks but its handles are LE1-reach and `ue3::runtime::Ready()` is false on LE2; the host's `plugin/ConversationWorld.cpp` (`setConversationWorld`'s receiver, `ConversationInWorld`) is not in the LE2 host list; `me2_xr.cpp` has no first-person seam of ours. | The menu hides "First person" on the sequels and reads `conversations = world` as the screen; the host's `cine_screen` also treats `world` as screen, so a hand-written `world` falls back safely. | `src/engine/GameAdapter.hpp:246-250`; `src/ue3/Runtime.cpp:475-477`; `CMakeLists.txt:956-957`; `src/plugin/CineScreen.cpp:89-90`; `src/plugin/MainMenuSettings.cpp:343-348`; `assets/menu/main.rml:187-188` |
| CNV-08 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-09 | missing | `kConversationSetPositionRotator` is 0 for LE2; `conversation_hooks::Install` returns before hooking. | No LE2 address was looked for. | `src/engine/Addresses.hpp:280-282`; `src/engine/ConversationHooks.cpp:155` |
| CNV-10 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-11 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-12 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-13 | missing | Part of CNV-07's module. | The 0.43 m and 0.18 m view offset is Halcyon LE1's; ME2's renderer has no such GPU-only offset recorded, so the numbers would not carry over as they are. | as CNV-07; [ledger](../../../trilogy-parity-ledger.md) row R29 |
| CNV-14 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-15 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-16 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-17 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-18 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-19 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-20 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-21 | missing | Part of CNV-07's module; the table `cw::IsStagedScene` holds ME1 conversation names only. | | `src/core/vr/ConversationWorld.cpp` |
| CNV-22 | missing | Not needed while every ME2 conversation is on the screen; `cw::IsSetPiece` holds ME1 names only and `tools/conversation-scan` has no ME2 verdict run on record. | | `src/core/vr/ConversationWorld.cpp` |
| CNV-23 | missing | Part of CNV-07's module. On the screen the director's near plane and depth of field are wanted, so nothing is lost today. | A port must not copy ME1's `scale set DepthOfField` during a load: that hung ME2 9 of 9 times. | as CNV-07; [gotcha](../../gotchas/me2-scale-set-during-load.md) |
| CNV-24 | missing | Host half absent (the subtitle and wheel quads are `plugin/ConversationWorld`'s). | | `CMakeLists.txt:956-957` |
| CNV-25 | missing | `game/ConversationFlow::Tick` returns at `!Ready()`; `kInterruptConversation` is 0; `cw::FlowEntries` is ME1's table. | | `src/game/ConversationFlow.cpp:639`; `src/engine/Addresses.hpp:281` |
| CNV-26 | missing | Part of CNV-25. | | as CNV-25 |
| CNV-27 | missing | Part of CNV-07's module. | The owner's 2026-11-15 deferral names seated world conversations explicitly. | commit 2ea1426c |
| CNV-28 | missing | `game/AuthoredMoment::Tick` returns at `!Ready()`; `kMoveWithInterpMoveTrack` is 0. A matinee that walks Shepard is a mode-6 cutscene on the screen. | | `src/game/AuthoredMoment.cpp:349`; `src/engine/Addresses.hpp:282` |
| CNV-29 | missing | Part of CNV-28. | | as CNV-28 |
| CNV-30 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-31 | missing | Part of CNV-28. | | as CNV-28 |
| CNV-32 | missing | Part of CNV-07's module. | | as CNV-07 |
| CNV-33 | missing | No "armed" or "handing over" word reaches ME2's presentation decision; with no world mode there is nothing to flash between. The screen-to-GUI and screen-to-world changes go through CNV-05's dip. | | `src/halcyon/le2/me2_xr.cpp:1917-1928` |
| CNV-34 | missing | See the audio audit, AUD-07 to AUD-09: no source hook on ME2 (`kSoundSourceUpdate` 0) and no world conversation to place voices in. | | `src/engine/Addresses.hpp:271`; `src/engine/VoicePlace.cpp:374-376` |
| CNV-35 | n/a | ME2 plays through Wwise, not ISACT over OpenAL; `openal_soft::Attach` is compiled for LE1 only. See the audio audit, AUD-01. | The exe's imports were never checked (ledger G37's open step). | `src/plugin/Entry.cpp:260-264`; [research/69](../../../../research/69-conversations-everywhere.md) "LE1 has no Wwise" |
| CNV-36 | missing | Part of CNV-07's module; `cw::ShownEarlyFor` is an ME1 table. | | as CNV-07 |
| CNV-37 | missing | For conversations: no world view is held. The three helpers: `engine/BodyBounds` addresses are 0 on LE2 (the functions were found: 0x798350, 0x3146C0), `game/CameraCollision` is inside `MEVR_GAME_LE1`, the `head_aim.cpp` seams are LE1's. ArmIk's `bConsiderAllBodiesForBounds` part runs. | Whether ME2 culls the body or pulls the camera in on foot is a first-person question (another audit); the ledger's R29 check was not run. | `src/engine/Addresses.hpp:269-270`, `:393`; `src/game/CameraCollision.cpp:21`; `src/engine/BodyBounds.cpp:133` |

# Reinvented

| what | ME2's code | the shared or ME1 code | what unifying takes |
|---|---|---|---|
| First-person conversations | Upstream Halcyon ME2's `src/halcyon/le2/convo_fp.cpp` (reads `m_oCurrentConversation`, the staged player, speaker and listener; eye pose, turn rate, its own `ConvoFpDisableDof`), settings read at `me2_menu.cpp:624-635`, presentation latch at `me2_xr.cpp:1898-1900` | `src/game/ConversationWorld.cpp` + `src/core/vr/ConversationWorld.cpp` (ME1, 37 contracts' worth) | Not a merge: pin `ConvoFirstPerson` off in `ApplyMevrBaseline` (as LE3's host forces "Halcyon's own VR cine off") so there is one owner; when G36 is ported, upstream's reads of the staged player are useful evidence for the adapter. |
| The presentation decision | `me2_xr.cpp:1794-1928`: a FOV detector with hysteresis, `[CINELATCH]`, `[ENGCINE]` scene-alive latch, `[GAMEPLAYVR]` | LE1's `DecideCine` in `xr_session.cpp` with `core/vr/PresentationHold` (unit-tested) | Upstream's code, kept by the "touch Halcyon as little as possible" rule. Unifying would mean a shared decision core fed semantic contexts (trilogy-plan rule 2); only worth it if CNV-06 is ported. |
| The screen's layer composition | `plugin/SequelCinema.cpp` `Layers` and `FillUi` (both sequels) | LE1 composes the same quads inline in `xr_session.cpp` through `MevrBridge::CineScreenQuadsNow`, and repeats `FillUi`'s `PlaceUi` step in `CineScreenPointerPlane` (`plugin/CineScreen.cpp:275-294`) | The copy that is left is LE1's (ledger R20 "Forks 1"). Move LE1's submit onto `sequel_cinema::Layers`; then a pointer plane written once serves three games. |
| The fade seam | `HalcyonSequelBridge.cpp:344-346` | `TransitionFade.cpp:178-186` (LE1, with the conversation's `cut`) | One `MevrSeams::TransitionFadeLayer` in `plugin/Seams.cpp` that asks `conversation_world::Current()` only where the host has it. |
| Game modes as numbers | n/a today (modules stand down) | `game/ConversationWorld.cpp:7712`, `:7723`, `:7740` and `game/SoundPlace.cpp` compare raw mode numbers (0, `kModeConversation`, 6) | ME2's 5 and 6 happen to match ME1's, ME3's do not (7, 8). A port must go through `engine::adapter::GameMode` (`IsConversationMode` exists, `GameAdapter.hpp:71`). |

# Rediscovered problems

| problem on ME2 | date, cost | ME1 already had | sources |
|---|---|---|---|
| The flat menu panel followed the face (submitted in VIEW space). | 2026-11-17, an owner headset report, commit c4602f1f | "Flat front end moved with the head" and the world-locked panels (`worldLockedPanels`, `plugin/WorldPanels`) | catalog "Problems ME1 already solved"; [research/56](../../../../research/56-start-and-front-end.md) "The front end on the 3D screen" |
| After that fix the start and loading screens lay at the floor: ME2's `PlacePanel` returned the renderer's own LOCAL-space pose instead of going through `world_panels::Place`. | 2026-11-17, a second owner report the same day, commit e580550c | ME1 places every flat panel at the head's height and heading when it appears | `src/plugin/HalcyonLe2Bridge.cpp:347-354`; `src/plugin/WorldPanels.cpp` |
| Presentation flapping between VR and the flat panel at scene boundaries and within a conversation (upstream's notes of 2026-07-30 and 07-31 in the code). | Upstream's cost, before our fork | ME1's presentation hold and the 100 ms wait (research/56) answer the same class of flip | `src/halcyon/le2/me2_xr.cpp:1782-1790`, `:1852-1877` |
| A stale eye pair on the cinema quads. | 2026-11-15, commit 54904310 | Not in ME1's list (ME1's eyes are submitted by Halcyon LE1's own path): new on ME2, not a rediscovery | `src/halcyon/le2/me2_xr.cpp:2395` |

Not yet rediscovered, but waiting for a port of first person: the director's near plane, the unreadable far-shot camera
mode, the stick still out after a pick, `EndConversation` alone crashing, freed pawns in the pawn list, and the depth of
field route (which on ME2 collides with [the scale-set gotcha](../../gotchas/me2-scale-set-during-load.md)). The catalog's
list is the reading before that work starts.

# Forced by the game

| difference | evidence |
|---|---|
| Interrupts: the triggers are the game's in the conversation mode (LT Paragon, RT Renegade); nothing of ours claims them there. ME1 has no such mechanic. | `src/plugin/HalcyonSequelBridge.cpp:378-381`; `src/plugin/PadMerge.cpp:129`, `:154`; [le2-cinema](../../../../research/le2-cinema.md); [interrupt acquisition](../../../../research/le2-interrupt-acquisition.md) |
| Mode 8 is both prerendered movies and in-engine cutscenes on ME2; decoder activity decides. | `src/halcyon/le2/me2_xr.cpp:1733-1743` |
| The eye images are the game's 16:9 window (upstream ME2's renderer), so the backdrop sizes itself from the field of view. | `src/plugin/SequelCinema.cpp:72-78` |
| The lens is known in the view that is built, so no `ScreenLensScale`. | `src/plugin/SequelCinema.cpp:17-19` |
| Hook addresses: ME1's three conversation functions and the move track have other addresses in `MassEffect2.exe`; none is in the table. | `src/engine/Addresses.hpp:280-282` |
| ME1's tables (set pieces, staged scenes, flow entries, shown early, curated walks) name ME1 conversations; ME2 needs its own scan. | `src/core/vr/ConversationWorld.cpp`; `tools/conversation-scan.py` |
| Thespian on ME2 cannot mute or skip Shepard's line (no verified Wwise route). | `src/game/Thespian.cpp:43` |

# Not forced

| difference | note |
|---|---|
| `[FirstPerson] ConvoFirstPerson` left to the ini | Nothing forces a second first-person owner; ME1's rule is one supported configuration. |
| `conversations = 0`: flat on ME2, "In VR" on ME1 | The owner's rule 9 (no VR cine on the sequels) explains dropping "In VR"; the same key value meaning two things across games is a choice. `conversations = flat` would be clearer. |
| No pointer on the screen's UI layer | The game pointer module is in the LE2 host (`MEVR_SEQUEL_MENU_SOURCES`); only the call is missing. |
| No front-end hold, no 3D title | The core is game-independent and unit-tested; the ledger asks for a measurement first, which was not made. |
| First person deferred | The owner's decision (2026-11-15), not the engine's: ME2's conversation objects are ME1's lineage (`BioConversation`, `m_oCurrentConversation`), as upstream's `convo_fp` shows. |
| ME1's depth check not run | `tools/cine-screen-check.py` works on snapshots; nothing ties it to ME1. |

# What to do

Parity work only, ordered by what a player sees.

1. **Pin upstream's first-person conversation off.** `src/halcyon/le2/me2_menu.cpp` `ApplyMevrBaseline`: add
   `ConvoFp::SetEnabled(false)` to the pinned list (and the same in LE3's baseline if it reads the key). Check: with
   `ConvoFirstPerson=1` written into `MELE2VR.ini`, the log's `[MEVR-BASELINE]` line names it and `[VRSTATE] gm 5` shows
   `3D screen (MEVR)` in the shuttle conversation (`tools/le2-halcyon-run`, research/le2-cinema.md's step 2).
2. **Close the interrupt check** (T15): a save before Veetor, a trigger replayed inside the prompt's window and one
   outside (`tools/make-interrupt-recording.py`); also a snapshot showing the prompt on the UI layer in front of the
   screen. Files: none to change unless it fails.
3. **Measure the start-up before porting CNV-06**: snapshots every half second from launch to the main menu and through
   the first load (`tools/snapshot.ps1 -Count`), reading `[VRSTATE]` lines beside them. Port
   `core/vr/PresentationHold` into the sequel bridge only if a frame of plain VR or a changing panel shows.
4. **Measure the screen's depth on ME2**: `python tools/cine-screen-check.py` on a mode-5 and a mode-6 snapshot pair from
   Save 4; expected: the UI layer nearer than every face, infinity at 1.0% of the width. ME2's 16:9 eye images make this
   a real question, not a formality.
5. **The pointer on the UI layer**: in `plugin/SequelOverlays.cpp` after `sequel_cinema::FillUi`, name the screen UI
   quad to `game_pointer::NoteGameUiQuad`, or move `CineScreenPointerPlane`'s body into `SequelCinema` for all three
   games. Check: a replayed right-hand ray on a reply moves the game's cursor (`mevr guimouse`). First settle whether
   ME2's wheel takes a mouse at all in mode 5; if not, record it and rate CNV-03's pointer n/a.
6. **Make LE1 use `sequel_cinema::Layers`** (rename it), so the screen has one composition. Check: ME1 unchanged by
   same-pose snapshots before and after, `mevr-tests`.
7. **When the owner lifts the deferral of first person (T49)**: read the catalog's "Problems ME1 already solved" first;
   then, in order, the three addresses with patterns (`tools/rva-patterns.py`, `engine/Addresses.hpp`), the handles to
   `kEveryGame` with `ReflectCheck` rows, mode numbers through `engine::adapter::GameMode`, `plugin/ConversationWorld.cpp`
   into the LE2 host list with seams in `me2_xr.cpp` (where `s_convoVrLatch` sits today), an ME2 run of
   `tools/conversation-scan` for the set-piece, staged and flow tables, and the audit set (`tools/conversation-audit.ps1`)
   pointed at ME2 saves. The view-offset numbers of CNV-13 must be measured on ME2, not copied.

# Citations

- Catalog: [ME1 conversations and scenes](../../me1/conversations-and-scenes.md); [how to catalog](../../how-to-catalog.md).
- Ledger and plans: [trilogy parity ledger](../../../trilogy-parity-ledger.md) rows R20, R21, R22, R29, G35, G36;
  [trilogy plan](../../../trilogy-plan.md) T15, T48, T49; [ME2 from ME1](../../../le2-from-le1.md);
  [continuation plan](../../../le2-continuation-plan.md) M5, M6.
- Research: [le2-cinema](../../../../research/le2-cinema.md), [le2-interrupt-acquisition](../../../../research/le2-interrupt-acquisition.md),
  [le2-renderer-integration](../../../../research/le2-renderer-integration.md), [trilogy-seams](../../../../research/trilogy-seams.md),
  [52 cine 3D screen](../../../../research/52-cine-3d-screen.md), [56 start and front end](../../../../research/56-start-and-front-end.md),
  [69 conversations everywhere](../../../../research/69-conversations-everywhere.md), [70 Thespian mode](../../../../research/70-thespian-mode.md).
- Gotchas: [ME2 scale set during load](../../gotchas/me2-scale-set-during-load.md), [ME2 float swap chain](../../gotchas/me2-float-swap-chain.md).
- ME2 renderer: [me2_xr.cpp](../../../../src/halcyon/le2/me2_xr.cpp), [me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp),
  [calcview_hook.cpp](../../../../src/halcyon/le2/calcview_hook.cpp), [convo_fp.h](../../../../src/halcyon/le2/convo_fp.h).
- Host: [SequelCinema.cpp](../../../../src/plugin/SequelCinema.cpp), [CineScreen.cpp](../../../../src/plugin/CineScreen.cpp),
  [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp), [HalcyonLe2Bridge.cpp](../../../../src/plugin/HalcyonLe2Bridge.cpp),
  [SequelOverlays.cpp](../../../../src/plugin/SequelOverlays.cpp), [TransitionFade.cpp](../../../../src/plugin/TransitionFade.cpp),
  [WorldPanels.cpp](../../../../src/plugin/WorldPanels.cpp), [PadMerge.cpp](../../../../src/plugin/PadMerge.cpp),
  [MainMenuSettings.cpp](../../../../src/plugin/MainMenuSettings.cpp), [Entry.cpp](../../../../src/plugin/Entry.cpp).
- Mod and engine: [game/ConversationWorld.cpp](../../../../src/game/ConversationWorld.cpp), [ConversationFlow.cpp](../../../../src/game/ConversationFlow.cpp),
  [AuthoredMoment.cpp](../../../../src/game/AuthoredMoment.cpp), [Thespian.cpp](../../../../src/game/Thespian.cpp),
  [ConversationHooks.cpp](../../../../src/engine/ConversationHooks.cpp), [Addresses.hpp](../../../../src/engine/Addresses.hpp),
  [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp), [Runtime.cpp](../../../../src/ue3/Runtime.cpp),
  [CameraCollision.cpp](../../../../src/game/CameraCollision.cpp), [BodyBounds.cpp](../../../../src/engine/BodyBounds.cpp).
- Build lists: [CMakeLists.txt](../../../../CMakeLists.txt) (LE2 host sources 1086-1162, sequel menu sources 1021-1049).
- Commits: c4602f1f, e580550c (2026-11-17), 54904310, 1834c240 (2026-11-15), 2ea1426c.
