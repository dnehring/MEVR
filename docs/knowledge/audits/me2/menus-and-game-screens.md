---
type: Audit
title: ME2 menus and game screens against ME1's contracts
description: Where each panel sits on ME2 and why; the shared placement code runs with ME1's numbers for our menu, the HUD panel, the wrist screens and the rail, while the game's wheels at the hand, the room screens' anchors, the pointer on flat panels and on the cinema UI layer, and the bypass pad are absent, and screens the tag list does not know fall to Halcyon's flat panel at 1.6 m and 2.2 m instead of ME1's 1.4 m and 2.35 m.
tags: [audit, me2, parity, menus, hud, world-ui, omni-tool, pointer, galaxy-map]
status: draft
timestamp: 2026-11-18T08:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:00:00Z }
game: LE2
against: ../../me1/menus-and-game-screens.md
commit: 940a75c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/menus-and-game-screens.md). Nobody ran the game for this audit: every row
rests on reading code and notes at 940a75c6, and says where a note records a run.

About two thirds of this category reaches ME2 through ME1's own modules (`plugin/MainMenu`, `HudPanel`, `HandReticle`,
`HudPieceLayers`, `WorldUi`, `OmniToolRail`, `GamePointer`, `WristBeam`, `GalaxyHologram`), with ME1's numbers, because
ME2's renderer keeps no settings for them and the host answers ME1's code defaults. What is lost is at the edges:

1. **Screens the tag list does not know fall to Halcyon's flat panel, and that panel is not ME1's.** ME2's shop,
   research, lockers, loadout, squad selection and both minigames are not in `core/vr/WorldUi`'s list, so they show flat,
   1.6 m away and 2.2 m wide (ME1: 1.4 m, 2.35 m), with no game pointer on them: the pad only.
2. **The game's wheels never come to the hand, and the hand reticle's vehicle gate reads ME1's mode number.** `setWheelPanel`
   is LE1-only, so ME2's weapon and power wheels stay on the HUD panel 1.5 m ahead; `HandReticle` tests `gameMode == 1`
   (ME1's Mako), which on ME2 is the power wheel, and the Hammerhead is mode 4.
3. **The composition exists twice.** LE1 composes in `HalcyonOverlays.cpp` and `HalcyonBridge.cpp`; the sequels in
   `SequelOverlays.cpp` and `MenusLe2.cpp` ("LE1's HalcyonOverlays word for word"). Every fix to one must be repeated in
   the other, which is how the flat panel met ME1's two solved problems again on 2026-11-17.

| rating | count | ids |
|---|---|---|
| same | 12 | SCR-02, 04, 05, 06, 08, 24, 25, 28, 29, 34, 37, 10 |
| same core, other inputs | 8 | SCR-01, 09, 12, 15, 16, 20, 23, 27 |
| adapter | 7 | SCR-03, 07, 17, 21, 26, 30, 35 |
| reinvented | 1 | SCR-38 |
| partial | 6 | SCR-11, 14, 18, 19, 31, 33 |
| missing | 5 | SCR-13, 22, 32, 39, 40 |
| n/a | 1 | SCR-36 |

# Where each panel sits on ME2

The owner's first complaint. Distances from the eyes unless said; "as ME1" means the same function with the same constant.

| panel | ME2's place | from which constants | why it differs |
|---|---|---|---|
| Our main menu | as ME1: 1.30 m ahead, 0.10 m below the eyes, 1.45 x 0.906 m, fixed per opening | `main_menu::Layer` from `MenusLe2.cpp:398-443`; `[Menu]` in `ASI\LE2MEVR.ini` | no difference. The head is located with `xrLocateSpace(VIEW, appSpace)` (`MenusLe2.cpp:303-309`) because ME2's flat panel submits no eye views; a note records `main menu: placed 1.30 m ahead` on ME2 (research/le2-menus "The checks") |
| Halcyon's own menu (developer) | Halcyon ME2's ImGui menu, its own quad | upstream's; Ctrl or Shift + `\` | no "VR hands" tab and no laser pointer of ours (ledger G56 missing) |
| HUD panel | as ME1: body-locked, 1.5 m, 1.3 m wide, 0.25 m drop, follows past 70 degrees | `renderer::Hud()` returns ME1's code defaults (`RendererLe2.cpp:153-155`, `Renderer.hpp:162-171`) | the numbers cannot be changed on ME2: there is no key and the menu's HUD rows are greyed. It also carries ME2's weapon wheel, power wheel and squad command (modes 1-3 are HUD gameplay, `GameAdapter.hpp:84-87`) |
| Hand reticle | as ME1: 10 m on the mod's aim ray, 1.5 degrees | `SequelWeaponLayers.cpp`, `HandReticle.cpp`, `renderer::Hud()` | on foot the same. The vehicle branch tests raw mode 1 (`HandReticle.cpp:142`), ME2's power wheel; the Hammerhead (mode 4) gets no ring |
| The game's weapon wheel | on the HUD panel, 1.5 m ahead, 1.3 m wide | `IsHudGameplayMode` | not at the left hand: `setWheelPanel` is LE1-only (ledger G57). ME1: 0.27 m from the hand, 0.40 m wide |
| The game's power wheel | on the HUD panel | the same | as ME1's default (`powerWheelAt = hud`), but the key's other choices do nothing on ME2 |
| Wrist status | the left grip, offset (-0.03, 0.06, 0.05) m, 0.08 m wide | `HudPieceLayers`, `[Hud]` in `LE2MEVR.ini` | the place is ME1's; the band cut from the HUD is ME2's own (stage x 540..740, 110..2 px from the bottom: `HudPieces.cpp:36-39`) |
| Gun status | the drawn gun, 0.15 m back, 0.04 m up, 0.06 m wide | the same | band x 8..184, 108..3 px from the bottom (`HudPieces.cpp:40-43`); ME2 adds the ammo mount |
| Interaction label | at the selected object, 6 degrees tall | `hud_piece_layers::Interaction` (`SequelOverlays.cpp:224-226`) over `core/vr/InteractLabel` | band y 30..100 (`HudPieces.cpp:52-53`); composed by the sequels' path, not `HalcyonOverlays` |
| A game screen in the room | `world_ui::Current` places it as ME1 (0.95 m, 1.20 m wide, 0.50 m below) | `[WorldUi]` block, the same text as ME1's | only "straight ahead": the vendor, container and minigame anchors are ME1 handles (`game/WorldUi.cpp:51-52`). In practice every screen ME2 knows is a pause-menu page, so it goes to the wrist |
| A personal screen on the wrist | as ME1: bottom edge 0.10 m above the wrist, 0.70 m wide, at most 60 degrees, rides the wrist | `MenusLe2.cpp:207-216` (`WristPoint` with `[Hud] wristOffset`), `world_ui::Current` | the same. ME2's map page is `AreaMap`, mapped to ME1's `Map` (`WorldUi.cpp:49-51`); the Mission Computer's own movies behind a child page are hidden with `SetMovieVisibility` |
| The omni-tool's rail | as ME1: right of the wrist screen, 0.015 m gap; alone 0.32 m tall | `OmniToolRail` from `MenusLe2.cpp:340-376` | Mission Computer palette (`OmniToolRail.cpp:63-64`); the Equipment tab is drawn (`omnitool.rml:18`) and answers "use the game's weapon/armour stations" (`GameMenuRequest.cpp:1105-1106`) |
| The wrist-tap target | as ME1: Shepard's drawn forearm, 30% from the wrist | `MenusLe2.cpp:358-361` | the same once the mod's HUD pieces reach the host; otherwise the controllers |
| The bypass d-pad | absent | `plugin/OmniPad` is not in the LE2 host's list (`CMakeLists.txt:1222` is LE1's) | ME2's hacking and bypass are other games (ledger G24); they play on the flat panel with the pad |
| The game pointer's dot | on a wrist or room screen, as ME1 | `MenusLe2.cpp:377-394` | nothing registers the flat panel or the cinema UI layer as a pointer surface on ME2 (no `game_pointer` call in the sequels' bridge or `SequelCinema.cpp`) |
| The flat panel (fallback) | fixed in the room where it appears, at head height | 1.6 m and 2.2 m: `me2_xr.cpp:92-93`; placed by `world_panels::Place` (`HalcyonLe2Bridge.cpp:354-373`) | upstream's defaults, not ME1's 1.4 m and 2.35 m; a cutscene's width is also multiplied by `CineScreenZoom`. In the room only since e580550c (2026-11-17) |
| The galaxy map's window | on the CIC projector's disc, as ME1 | `galaxy_hologram::WindowLayers` (`SequelOverlays.cpp:100-117`) | the mod side is another flow (`game/GalaxyProjectorFlow` with `Le2GalaxyCapture`), not `game/GalaxyMap` |
| The galaxy map's UI | on the projector's disc, 1.50 m frame | `UiPanelWidthM(1.50f)` (`SequelOverlays.cpp:176`) | no pointer: `galaxyUiPointer` 0 (`HalcyonLe2Bridge.cpp:81-84`); the map keeps its own stick and buttons. Orbit and scan (mode 10) keep LT and RT |
| The cinema screen's UI layer | 0.5 m in front of the 3D screen, scaled to the same angle | `sequel_cinema::FillUi` (`SequelCinema.cpp:127-148`), `cs::PlaceUi` | placed as ME1; not a pointer surface (`CineScreenPointerPlane` is called only on LE1) |
| A first-person conversation's subtitle and wheel | absent | no conversation world on ME2 | deferred by the owner |

Which screens still fall to Halcyon's flat panel on ME2: the start screens, logos and main menu (no front-end hold:
[presentation-modes.md](presentation-modes.md) PRS-12, 13); every loading movie; any GUI-mode screen whose tag
`world_ui::FromTag` does not know (shops, research terminal, armour and weapon lockers, squad selection, hacking and
bypass, the private terminal, message boxes over them); a known screen opened with no on-foot view to hold; the galaxy
map and orbit when the mod's projector flow does not hold them. Reading only: the list of unknown tags is from
`WorldUi.cpp:24-28`, not from a run that opened each screen.

# Contract by contract

| id | rating | how ME2 gets it | where it differs | evidence |
|---|---|---|---|---|
| SCR-01 | same core, other inputs | `UpdateStickChord` on the player's own sample; the key through `MevrSeams::OnMenuKey`; `\` pinned over upstream's Insert | R3 is also held back on ME2's pad path (`gateRightClick`), since ME2's R3 is a game button; the glue is a second copy of LE1's | `MenusLe2.cpp:169-186`, `Seams.cpp:53`, `me2_menu.cpp:531` |
| SCR-02 | same | `main_menu::Layer` | none in the numbers; legibility in the headset is on the checklist | `MenusLe2.cpp:398-443` |
| SCR-03 | adapter | `game/MenuPause` writes `WorldInfo.bPlayersOnly`, as ME2's weapon wheel does | ME2 has no `PauseGame`; in the Hammerhead: not recorded **Fixed in code, lane AU1 (2026-11-18, not run in the game):** `MenuPause` pauses on foot, in a vehicle or ME3's Atlas through the adapter on every game (ME2's raw 1 was its power wheel). | `MenuPause.cpp:65-99`, research/le2-menus "The pause on ME2" |
| SCR-04 | same | `gamepad::CloseLatch` in `FilterPad` | none | `MenusLe2.cpp:501-529` |
| SCR-05 | same | the module's own pointer and pad | none | `MenusLe2.cpp:157-159`, `:504-505` |
| SCR-06 | same | `main_menu::OnPresent` from ME2's Present seam | none | `HalcyonLe2Bridge.cpp:303` |
| SCR-07 | adapter | Game menu: `SFXGameModeBase.ShowMenu`; Holster: `TryHolster` | no "drawn?" check (ME2's only ever holsters); a third entry, Leave, by the owner's answer | research/le2-menus "The rows on ME2"; `GameMenuRequest.cpp` |
| SCR-08 | same | `MainMenuSettings` | the rule is the same; most groups are greyed on ME2 (see settings-and-defaults.md) | research/le2-menus "The rows on ME2" |
| SCR-09 | same core, other inputs | `plugin/HudPanel` fed by one `MevrHudPanelDraw` seam; the gate is `IsHudGameplayMode`, translated per game | ME1's numbers but fixed (no keys, rows greyed); ME2's wheels and squad command are on the panel too; the HUD is drawn twice a frame and the later batch wins | `SequelOverlays.cpp:123-166`, `RendererLe2.cpp:153-155`, `GameAdapter.hpp:84-87` |
| SCR-10 | same | the panel's tint texture is in the shared module | reading only: that ME2's one draw seam routes non-"over" blends to the tint was not traced; a snapshot of ME2's wheel on the panel settles it | `HudPanel.cpp:40-56` |
| SCR-11 | partial | wrist, gun and label masks through `hpl::Decide` and `Interaction` | no wheel mask: there is no wheel panel | `SequelOverlays.cpp:207-226` |
| SCR-12 | same core, other inputs | `HandReticle` with `OverlayReticleMode` = the raw game mode; the crosshair hidden by `game/ScreenReticle` (`_alpha = 0`) | `gameMode == 1` is ME1's Mako and ME2's power wheel; the Hammerhead is 4 and gets no ring. Until a host matched the mod, ME3 also asked for a fresh ray (unified 2026-11-16) **Fixed in code, lane AU1 (2026-11-18, not run in the game):** the vehicle gate through `engine::adapter::GameMode` (`Mode::Vehicle`). | `HandReticle.cpp:142-143`, `HalcyonLe2Bridge.cpp:147-149`, `GameAdapter.hpp:51-53` |
| SCR-13 | missing | none | the weapon wheel stays on the HUD panel; `setWheelPanel`, `WheelCrop` and the measured crop are LE1-only. **Fixed in code, lane AU1 (2026-11-18, not run in the game):** `plugin/WheelPanel`, LE1's code shared, composed by `SequelOverlays.cpp` | ledger G57 |
| SCR-14 | partial | the power wheel stays on the HUD panel because its mode is HUD gameplay | `powerWheelAt = right, left, front` do nothing on ME2 | `GameAdapter.hpp:84-87` |
| SCR-15 | same core, other inputs | `HudPieceLayers` with `Layout::Me2` | ME2's band (x 540..740, 110..2 px above the bottom); place and width ME1's | `HudPieces.cpp:36-39`, `:223-224`; `HalcyonLe2Bridge.cpp:80` |
| SCR-16 | same core, other inputs | the same, with ME2's band and its ammo mount | band x 8..184, 108..3 px; the raised-gun hand-over is the mod's shared rule (not re-read here) | `HudPieces.cpp:40-43`, `:230-231`; `game/HudPieces.cpp:37-38` |
| SCR-17 | adapter | gaze hook at ME2's address; the label through `hud_piece_layers::Interaction` | a per-game band; a failed panel leaves the native prompt in place, never a detached label | `SequelOverlays.cpp:157-162`, `:224-250`; ledger G27 |
| SCR-18 | partial | `core/vr/WorldUi` in mode 7; ME2's pause-menu pages by ME1's tags | ME2's own screens (shop, research, lockers, squad selection, minigames) are not in the list: the stack stays flat. No minigame mode (`kHasMinigameMode` false). **Fixed in code, lane AU1 (6abedb31, not run in the game):** ME2's `ChoiceGUI`/`MissionCompletion` (research, lockers, stores, bonus power) in the room toward what was used, `SkillGameBypass`/`SkillGameDecryption` as ME1's `SkillGame` on the wrist; the armour locker (`Personalization`, the UI world's Shepard) stays flat (research/le2-menus "The sequels' own screens") | `WorldUi.hpp:217-232`, `WorldUi.cpp:24-51`, `game/WorldUi.cpp:51-52` |
| SCR-19 | partial | `Place` runs | no vendor, loot or minigame anchor on ME2 (ME1's handler classes); straight ahead only | `game/WorldUi.cpp:51-52` |
| SCR-20 | same core, other inputs | `DefaultCrop` | the shop and loot crops never apply; the whole stage otherwise | `WorldUi.cpp` `DefaultCrop` |
| SCR-21 | adapter | the view held through `MevrSeams::WorldViewHoldBegin` in ME2's calcview hook; `bFullScreen` cleared; ME2's background browser movies hidden | `SetMovieVisibility` is ME2's own need | `game/WorldUi.cpp:69-70`, `RendererLe2.cpp:44-45`; research/le2-menus "Game screens and pointer on the wrist" |
| SCR-22 | missing | none | the squad page shows no 3D Shepard behind it; `UiWorldFigure` is LE1-only and ME2's scene-capture hooks stand down | ledger G23, R31; `UiWorldFigure.cpp:30` |
| SCR-23 | same core, other inputs | `OnWrist` with ME1's 12-tag default | four of the twelve tags do not exist on ME2 (SkillGame, Inventory, XMods, Specialization); the file still lists them | `HandCalibration.cpp:2958-2964` (one block for every game) |
| SCR-24 | same | `world_ui::Current` with the wrist point | none | `MenusLe2.cpp:207-216` |
| SCR-25 | same | `wrist_beam::Layer` | none | `MenusLe2.cpp:243-252` |
| SCR-26 | adapter | `game/OmniTool` with ME2's crust effect, restarted every 1.8 s | the 0.60 s power-up finds nothing to drive: full at once | research/le2-menus "ME2's omni-tool"; `OmniTool.cpp:29` |
| SCR-27 | same core, other inputs | the same rail and entries | Equipment is drawn and does nothing; recoloured | `omnitool.rml:18`, `GameMenuRequest.cpp:554`, `:1105-1106` |
| SCR-28 | same | the rail's own slide | none | `MenusLe2.cpp:364-365` |
| SCR-29 | same | `omni_rail::Tap` with the body target | none in the rule; distances unverified with a person on either game | `MenusLe2.cpp:343-376` |
| SCR-30 | adapter | `StepPage`: the pause menu, then `BioSFHandler_BrowserWheel.ShowSelectedPanel`; back-out by `GuiKey(13)` | ME2 has no `Show<Screen>Gui`; a child page is backed out of before another opens | research/le2-menus "The entries open ME2's screens" |
| SCR-31 | partial | `RailArt` reads ME1's cache | only for a player who ran ME1 with the mod; otherwise the rail's own look. `GameArt` writes nothing on ME2 | `GameArt.cpp:29`, research/le2-menus "ME1's art on ME2" |
| SCR-32 | missing | none | ME2's minigames are played flat with the pad | `CMakeLists.txt:1222`; ledger G24 |
| SCR-33 | partial | the pointer on wrist and room screens | not on the flat panel, the galaxy UI or the cinema UI layer | `MenusLe2.cpp:266-270`, `:377-394`; `HalcyonLe2Bridge.cpp:81-84` |
| SCR-34 | same | the fingertip rules | none | `MenusLe2.cpp:384-386` |
| SCR-35 | adapter | the same queue; the mod sends ME2's numbers: move 6, press and release 48 / 50, wheel 27 / 28 | ME1: 50 / 52 and 28 / 29. The cursor hook is installed in every host | `GuiCursor.cpp:107-117`, `Entry.cpp:312`; research/le2-menus |
| SCR-36 | n/a | `HoldOffForPuzzle` is reachable but `puzzle` is never set | no bypass puzzle | `MenusLe2.cpp:263-265` |
| SCR-37 | same | `plugin/GamePointer` | none | shared module |
| SCR-38 | reinvented | the host half is shared; the mod half is `game/GalaxyProjectorFlow` + `Le2GalaxyCapture`, not `game/GalaxyMap` | no pointer; the map's own controls; travel, orbit and scan not seen in a run | `GalaxyProjectorFlow.cpp:7`, `SequelOverlays.cpp:171-186`; ledger G25 |
| SCR-39 | missing | none | the opt-in table was retired on ME2; the fallback is the flat panel | `HalcyonLe2Bridge.cpp:39-40` |
| SCR-40 | missing | none | no first-person conversation on ME2 | `RendererLe2.cpp` (no `conversationWorld` capability) |

# Reinvented

| ME2 or sequel code | ME1 or shared code | what differs | what unifying takes |
|---|---|---|---|
| `plugin/SequelOverlays.cpp` `OverlayLayers`, `plugin/MenusLe2.cpp` `Layers`, `BeforePanel`, `PanelLayers`, `FilterPad`, `OnFrameBegun` | `plugin/HalcyonOverlays.cpp` `OverlayLayers`, `plugin/HalcyonBridge.cpp` (`OnFrameBegun`, `OnMenuKey`, the pad merge) | the same order and rules written twice; the sequels' copy asks capabilities, LE1's reads Halcyon LE1's state at a dozen places. LE1 alone has the wheel panel, the convo UI, the pad and the UI-world figure | move LE1 onto `SequelOverlays` behind capabilities (research/trilogy-seams "One overlay composition" lists what must move first); until then a parity test that both compositions emit the same layer order for the same frame facts |
| `game/GalaxyProjectorFlow.cpp` (sequels) | `game/GalaxyMap.cpp` (ME1) | two flows for one behaviour; the sequels' has ME3's reservation and bridges, ME1's has the pick and travel-return rules | one flow with three adapters, as the ledger's G25 notes plan |
| `hud_piece_layers::Interaction` for the label | the label block in `HalcyonOverlays.cpp` | the same core (`core/vr/InteractLabel`), two callers | LE1 calls `Interaction` |
| `ApplyMevrBaseline`'s menu key and Halcyon ME2's flat panel distance and width | LE1's deploy values and `WorldPanels` | 1.6 m / 2.2 m against 1.4 m / 2.35 m | pin them in the baseline or hand `PlacePanel` ME1's numbers (below) |

# Rediscovered problems

| problem on ME2 | when and cost | ME1 already held it | sources |
|---|---|---|---|
| The flat menus followed the face | owner's headset report, 2026-11-17, c4602f1f | world-locked panels (`worldLockedPanels`, PRS-18) | catalog "Problems ME1 already solved"; `me2_xr.cpp:2023-2027` |
| Then the menus lay at the floor and opening one felt like rising 5 feet | a second report the same day, e580550c | every flat panel placed at the head's height and heading by `world_panels::Place` | `HalcyonLe2Bridge.cpp:347-354` |
| A game screen showed the Mission Computer's artwork behind the child page | 2026-11-15, the wrist-visual run | ME1's `bFullScreen` and stack rules (research/53) | research/le2-menus "Game screens and pointer on the wrist" |
| A click through `GuiKey` on the galaxy map's Exit did nothing | lane GX2, 2026-11-17 | ME1's galaxy map is excluded from the GUI cursor for the same reason (SCR-35's gate) | `HalcyonLe2Bridge.cpp:81-84`; research/le2-galaxy-table |
| The right stick's Y switched Scaleform into pad mode while it drove the pointer's wheel | 2026-11-15 | "the right stick with a screen pointed at" in ME1's arbitration | `MenusLe2.cpp:511-513` |

# Forced by the game

- ME2 has no `Show<Screen>Gui`: its squad, journal, codex and map are pages of one pause menu, hence `StepPage`
  (research/le2-menus "The entries open ME2's screens").
- No equipment screen outside lockers, no heat, no bypass puzzle of ME1's kind, so SCR-32 and SCR-36 have no ME2 form.
- ME2's HUD is another movie: its bands are its own (`HudPieces.cpp:36-53`).
- ME2 has no `PauseGame`; `bPlayersOnly` is what its own wheel sets.
- ME2's GUI event numbers differ by two (48 / 50, 27 / 28).
- The galaxy map's own controls (stick, A, X, Y, LB / RB) and the scanner's triggers.
- ME2's renderer keeps no HUD or panel settings, so those numbers have no file key.

# Not forced

- The flat panel's 1.6 m and 2.2 m.
- No game pointer on the flat panel or the cinema UI layer.
- The game's wheels not at the hand (the HUD panel's draws are already captured; the crop and the quad are host code).
- `HandReticle`'s raw `gameMode == 1`.
- The Equipment tab shown on ME2's rail.
- ME2's own GUI screens missing from `world_ui`'s tag list.
- ME1's four absent tags written into `LE2MEVR.ini`'s `[WorldUi] wrist`.
- HUD panel and reticle rows greyed with no file key behind them.

# What to do

1. **Name ME2's own screens.** List the GUI tags ME2 shows (a run with `mevr panel` at a shop, the research terminal, a
   locker, squad selection, both minigames), add them to `core/vr/WorldUi.cpp` with a per-game table, place the ones with
   an owner (vendor, terminal) by `Place`. Check: the snapshot's `mod.worldUi` names each, and `world ui: reservation
   Presents ..., flat-panel frames` reads 0.
2. **Give the flat panel ME1's numbers and a pointer.** `HalcyonLe2Bridge.cpp` `PlacePanel` returns ME1's 1.4 m and
   2.35 m (or the baseline pins `MenuScreenSize` and the distance), and registers the quad with
   `game_pointer` as LE1's `OnFlatQuad` does. Check: `tools/make-menu-recording.py` clicks ME2's main menu by ray.
3. **Translate the reticle's vehicle gate.** `HandReticle.cpp:142` asks `engine::adapter::GameMode(mode) == Mode::Vehicle`.
   Check: a unit test over the three games' tables; ME1's snapshot unchanged.
4. **The wheel at the hand.** Serve `setWheelPanel` on the sequels and compose the wheel quad in `SequelOverlays.cpp`
   from `core/vr/WheelCrop`. Check: `-wheel.png` in an ME2 snapshot.
5. **The pointer on the cinema UI layer.** Call `CineScreenPointerPlane` from `SequelCinema.cpp`. Check: a replayed ray
   picks a dialogue reply.
6. **Hide the rail's Equipment tab on ME2** (a data flag in `omnitool.rml`, as `main.rml`'s `le2`). Check:
   `mevr-menu-preview --le2`'s `rail-*.png`.
7. **One composition.** Move LE1 onto `SequelOverlays` step by step (research/trilogy-seams). Check: LE1's layer order in
   a same-pose snapshot pair before and after.

# Citations

- Catalog: [menus-and-game-screens.md](../../me1/menus-and-game-screens.md), [presentation-modes.md](../../me1/presentation-modes.md).
- Ledger: [trilogy parity ledger](../../../trilogy-parity-ledger.md) rows R18-R22, G19-G28, G56, G57. Disagreements: G23
  "adapted V" hides that only pause-menu pages are known; G26 "adapted V" hides that flat panels have no pointer; R19
  "adapted V" hides the raw vehicle mode.
- Research: [le2-menus](../../../../research/le2-menus.md), [le2-galaxy-table](../../../../research/le2-galaxy-table.md),
  [le2-cinema](../../../../research/le2-cinema.md), [trilogy-seams](../../../../research/trilogy-seams.md).
- Source: [SequelOverlays.cpp](../../../../src/plugin/SequelOverlays.cpp), [MenusLe2.cpp](../../../../src/plugin/MenusLe2.cpp),
  [HalcyonLe2Bridge.cpp](../../../../src/plugin/HalcyonLe2Bridge.cpp), [RendererLe2.cpp](../../../../src/plugin/RendererLe2.cpp),
  [Renderer.hpp](../../../../src/plugin/Renderer.hpp), [HandReticle.cpp](../../../../src/plugin/HandReticle.cpp),
  [SequelCinema.cpp](../../../../src/plugin/SequelCinema.cpp), [OmniToolRail.cpp](../../../../src/plugin/OmniToolRail.cpp),
  [me2_xr.cpp](../../../../src/halcyon/le2/me2_xr.cpp), [me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp),
  [WorldUi.hpp](../../../../src/core/vr/WorldUi.hpp), [WorldUi.cpp](../../../../src/core/vr/WorldUi.cpp),
  [HudPieces.cpp](../../../../src/core/vr/HudPieces.cpp), [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp),
  [GameMenuRequest.cpp](../../../../src/game/GameMenuRequest.cpp), [GalaxyProjectorFlow.cpp](../../../../src/game/GalaxyProjectorFlow.cpp),
  [CMakeLists.txt](../../../../CMakeLists.txt).
