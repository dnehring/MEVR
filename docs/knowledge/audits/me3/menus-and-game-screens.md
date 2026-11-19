---
type: Audit
title: ME3 menus and game screens against ME1's contracts
description: Where each panel sits on ME3 and why; ME3 runs the sequels' shared host glue (MenusLe2, SequelOverlays) with ME1's numbers for our menu, the HUD panel, the wrist screens and the rail, has the pointer on its flat panel and a room anchor that ME2 lacks, but has no wrist or gun HUD pieces at all, so the wrist tap loses its weapon gate and its target on the drawn arm, the squad record and every screen the tag list does not know fall to a flat panel 2.2 m away and 3.2 m wide, and the game's wheels stay on the HUD panel.
tags: [audit, me3, parity, menus, hud, world-ui, omni-tool, pointer, galaxy-map]
status: draft
timestamp: 2026-11-18T10:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:00:00Z }
game: LE3
against: ../../me1/menus-and-game-screens.md
commit: 583591c6
---

# Verdict

Audited against [the ME1 catalog](../../me1/menus-and-game-screens.md). Nobody ran the game for this audit: every row
rests on reading code and notes at 583591c6, and says where a note records a run. ME3's runs so far are on the fake
headset from Save 31 (the Normandy, no weapon, no combat) and the CIC's AutoSave.

ME3 has no menu code of its own in the host: it compiles the same `plugin/MenusLe2`, `SequelOverlays`, `MainMenu`,
`WorldUi`, `OmniToolRail`, `GamePointer` and `WristBeam` as ME2, with ME1's numbers, and its game modes are translated
(GUI is 9, not 7) through `engine::adapter` and `core/vr/WorldUi.hpp`. That part of the port is real reuse and is ahead
of ME2 in two places (a pointer on the flat panel, a room screen placed toward what was used). What is lost:

1. **ME3 has no HUD pieces, and two other features read them.** `game/HudPieces` stands down by name on ME3 and the host
   serves no `setHudPieces`. So the health and the weapon panel stay on the HUD panel 1.5 m ahead, and the omni-tool's
   wrist tap never learns that a weapon is drawn (ME1 refuses the tap then) and never gets Shepard's drawn forearm as its
   target (it falls back to the controllers). A gesture registers on ME3 where ME1 would not take it.
2. **The flat panel is upstream ME3's: 2.2 m away and 3.2 m wide** (ME1 1.4 m and 2.35 m; ME2 1.6 m and 2.2 m), not pinned
   by the baseline. The squad record (the rail's Squad entry), shops, benches, lockers, squad selection, terminals and
   the galaxy map after a load all show there, while the journal and the map of the same Mission Computer are 0.70 m wide
   on the wrist. One menu system sits at three sizes and three distances.
3. **The game's wheels never come to the hand**, as on ME2 (`setWheelPanel` is LE1-only), and the rail shows two entries
   that do nothing on ME3 (Equipment, Codex).

| rating | count | ids |
|---|---|---|
| same | 10 | SCR-02, 04, 05, 06, 08, 24, 25, 28, 34, 37 |
| same core, other inputs | 7 | SCR-01, 09, 12, 20, 23, 27, 29 |
| adapter | 6 | SCR-03, 17, 21, 26, 30, 35 |
| reinvented | 1 | SCR-38 |
| partial | 8 | SCR-07, 11, 14, 18, 19, 31, 33, 39 |
| missing | 5 | SCR-13, 15, 16, 22, 40 |
| n/a | 2 | SCR-32, 36 |
| unknown | 1 | SCR-10 |

# Where each panel sits on ME3

The owner's first complaint. Distances from the eyes unless said; "as ME1" means the same function with the same constant.

| panel | ME3's place | from which constants | why it differs |
|---|---|---|---|
| Our main menu | as ME1: 1.30 m ahead, 0.10 m below the eyes, 1.45 x 0.906 m, fixed per opening | `main_menu::Layer` from `MenusLe2.cpp:396-443`; `[Menu]` in `ASI\LE3MEVR.ini` | no difference; the same glue as ME2. A run records the menu open and resumed on ME3 (ledger G19) |
| Halcyon's own menu (developer) | Halcyon ME3's ImGui menu on its own quad, 1.5 m (`g_menuQuadDistanceM`) | upstream's; Ctrl or Shift + `\` (`Seams.cpp:53-69`) | no "VR hands" tab and no laser pointer of ours (ledger G56 missing) |
| HUD panel | as ME1: body-locked, 1.5 m, 1.3 m wide, 0.25 m drop, follows past 70 degrees | `renderer::Hud()` returns ME1's code defaults (`RendererLe3.cpp:156-160`, `Renderer.hpp:162-171`) | the numbers cannot be changed (no key, no menu row). It carries more than ME1's: ME3's power wheel, weapon wheel, squad command, the Atlas' and injured Shepard's HUD (`GameAdapter.hpp:84-87`), and the health and weapon blocks ME1 moves to the wrist and gun. ME3's combat HUD on it is unverified (research/le3-hud-panel "Not verified") |
| Hand reticle | on foot as ME1: 10 m on the mod's aim ray, 1.5 degrees | `SequelWeaponLayers`, `HandReticle.cpp`, `renderer::Hud()` | ME3's adapter passes mode 0 on foot and -1 otherwise (`HalcyonLe3Bridge.cpp:244-246`): never a ring in the Atlas or a vehicle. ME3's crosshair movie is hidden by its alpha (`game/ScreenReticle`), unverified with a weapon drawn |
| The game's weapon wheel | on the HUD panel, 1.5 m ahead, 1.3 m wide | `IsHudGameplayMode` (mode 4 on ME3) | not at the left hand: `setWheelPanel` is LE1-only (ledger G57). ME1: 0.27 m from the hand, 0.40 m wide |
| The game's power wheel | on the HUD panel | the same (mode 3 on ME3) | ME1's default place, but `powerWheelAt`'s other choices do nothing |
| Wrist status | absent: the health, shields and squad stay on the HUD panel | `game/HudPieces.cpp:62-68`, `:178-185` (stand-down), `ModLoader.cpp:277-283`, `RendererLe3.cpp` (no `hudPieces`) | no ME3 crop was measured and no adapter written ("the port chooses ME3's own"). ME1: the left wrist, 0.08 m wide. ME2 has it |
| Gun status | absent: the ammo and weapon block stay on the HUD panel | the same | the same. ME1: on the gun, 0.06 m wide. ME2 has it |
| Interaction label | at the selected object, 6 degrees tall | `hud_piece_layers::Interaction` (`SequelOverlays.cpp:224-226`) over `core/vr/InteractLabel`; band stage y 0..70 (`HudPieces.cpp:54-55`) | ME3's prompt sits at the top edge of its stage; a run records the War Terminal's label at the console (research/le3-hud-panel) |
| A game screen in the room | as ME1: 0.95 m toward what was used, 1.20 x 0.68 m, 0.50 m below the eyes | `world_ui::Current`, `[WorldUi]` in `LE3MEVR.ini`; the whole 16:9 stage | only War Assets is known to stand there (toward the War Terminal's `SFXPointOfInterest`, research/le3-native-gui "The game's screens on ME3"). Shepard's head and body are hidden behind it (`WorldViewHeld` 2), which ME1 does not do |
| A personal screen on the wrist | as ME1: bottom edge 0.10 m above the wrist, 0.70 m wide (0.39 m tall), at most 60 degrees, rides the wrist | `MenusLe2.cpp:207-216`, `world_ui::Current`; projected from ME3's own omni-tool display when the mod sends it | the Mission Computer, Journal, Map, Options, Save, Load. The menu's Wrist screen size row is refused on ME3 (`MenuSettings.cpp:900-903`) although the reader behind it is the shared one |
| The squad record (the rail's Squad) | Halcyon ME3's flat panel: 2.2 m, 3.2 m wide | `game/WorldUi` `ShowsFigure`; `me2_xr.cpp:93-94` | its 3D Shepard lives in the UI world and `UiWorldFigure` is LE1-only, so the screen keeps the native panel. ME1: the wrist, 0.70 m, with the figure behind the UI |
| The omni-tool's rail | as ME1: right of the wrist screen, 0.015 m gap; alone 0.32 m tall | `OmniToolRail` from `MenusLe2.cpp:340-376` | ME3's Mission Computer colours (theme `me3`, `OmniToolRail.cpp:68-70`, `:277`), never ME1's art. Equipment and Codex are drawn and open nothing |
| The wrist-tap target | on the controller's wrist point, not on Shepard's drawn forearm | `MenusLe2.cpp:353-361`: `rail.body` is set only from the mod's HUD pieces, which ME3 never sends | ME1 moved the target to the drawn arm because the arm is drawn up to 15 cm from the controller (research/57). On ME3 that problem is back. The tap is also allowed with a weapon drawn (`rail.gunDrawn` is always false, `MenusLe2.cpp:362`, `OmniToolRail.cpp:854`) |
| The bypass d-pad | absent | `plugin/OmniPad` is not in the LE3 host's list | ME3 has no bypass minigame (ledger G24 n/a) |
| The game pointer's dot | on a wrist or room screen and on the flat GUI panel | `MenusLe2.cpp:377-394`; `HalcyonLe3Bridge.cpp:348-356` | ME3 registers its flat GUI panel as a pointer surface (ME2 does not). Not on flat conversations, not on the galaxy map, not on the cinema screen's UI layer |
| The flat panel (fallback) | fixed in the room where it appears, at head height and heading | 2.2 m and 3.2 m (about 72 degrees across): `me2_xr.cpp:93-94`, ini `MenuScreenDist` / `MenuScreenSize` (`me2_menu.cpp:575-578`); placed by `world_panels::Place` (`HalcyonLe3Bridge.cpp:315-361`) | upstream ME3's defaults, not ME1's 1.4 m and 2.35 m, not ME2's either; not in `ApplyMevrBaseline`, not in the installed `MELE3VR.ini`. Advanced's flat screen width row edits it (`RendererLe3.cpp:90-91`), though `Caps::ownMenuRows` is false |
| The galaxy map's window | on the CIC projector's disc, as ME1 | `galaxy_hologram::WindowLayers` (`SequelOverlays.cpp:100-117`); a run measured 2.64 x 1.94 m, 24 degrees off flat (research/le3-galaxy "The projector flow") | the mod side is the sequels' flow (`game/GalaxyProjectorFlow` with `Le3GalaxyCapture`), not `game/GalaxyMap`. After a save loaded inside the map the CIC's art is not loaded and the map is the flat panel until X and A again |
| The galaxy map's UI | on the projector's disc, 1.50 m frame | `UiPanelWidthM(1.50f)` (`SequelOverlays.cpp:176`) | no pointer (`galaxyUiPointer` 0, `HalcyonLe3Bridge.cpp:126-129`): ME3's map is flown with the stick, a click on a label did nothing in runs gx4 and gx5 |
| The cinema screen's UI layer | 0.5 m in front of the 3D screen | `sequel_cinema::FillUi`, `cs::PlaceUi` | placed as ME1; not a pointer surface, by a written decision (`HalcyonLe3Bridge.cpp:348-350`) |
| A flat conversation or cutscene (Flat chosen, or a mode the screen's gate misses) | 2.2 m, 3.2 m wide times `CineScreenZoom`, never follows | `wp::Kind::Conversation` / `Cutscene` (`HalcyonLe3Bridge.cpp:321-323`), `me2_xr.cpp:1960-1966` | see [presentation-modes.md](presentation-modes.md) PRS-18 |
| A first-person conversation's subtitle and wheel | absent | no conversation world on ME3 | deferred by the owner |

Which screens fall to Halcyon ME3's flat panel: the start screens, logos and main menu (no front-end hold:
[presentation-modes.md](presentation-modes.md) PRS-12, 13); every loading movie and the relay's travel movie; the squad
record; any GUI-mode screen whose tag `world_ui::FromMe3Tag` does not know (`WorldUi.cpp:22-63`: it knows `MenuBrowser`,
`SquadRecord`, `Journal`, `Areamap`, `Options`, `Save`, `Load`, `MessageBox`, `warAssets` and ME1's other names); a known
screen opened from the Atlas or as injured Shepard, or before any view on foot; one tick of every opening the game starts
itself (research/le3-native-gui lane FX3); the galaxy map when the flow does not hold it. Reading only: which tags ME3's
shops, weapon bench, armour locker, squad selection, Spectre and private terminals, medical bay and power evolution
screens carry was never listed in a run.

# Contract by contract

| id | rating | how ME3 gets it | where it differs | evidence |
|---|---|---|---|---|
| SCR-01 | same core, other inputs | `UpdateStickChord` on the player's own sample in the shared `menus_le2::OnFrameBegun`; the key through `MevrSeams::OnMenuKey`; `\` pinned over upstream's Insert | R3 is held back on the sequels' pad path and never reaches the game as R3 (`gateRightClick`, `keepPowerClick`); the glue is the sequels' second copy of LE1's | `MenusLe2.cpp:169-186`, `Seams.cpp:53-69`, le3 `me2_menu.cpp:528-530`, `HalcyonSequelBridge.cpp:372-377` |
| SCR-02 | same | `main_menu::Layer` | none in the numbers | `MenusLe2.cpp:396-443` |
| SCR-03 | adapter | `game/MenuPause`: ME3's native `BioWorldInfo.PauseGame` through `UnambiguousMethod`, `bPlayersOnly` journalled | another call than ME1's and ME2's; unverified in a run (ledger G20 "U"); in the Atlas not recorded | `MenuPause.cpp:65-130`; research/le3-menu-pause |
| SCR-04 | same | `gamepad::CloseLatch` in the shared `FilterPad` | none | `MenusLe2.cpp:59-61` |
| SCR-05 | same | the module's own pointer and pad | none | `MenusLe2.cpp:157-159` |
| SCR-06 | same | `menus_le2::OnPresent` from ME3's Present seam | none | `HalcyonLe3Bridge.cpp:305-311`, le3 `me2_xr.cpp:2912` |
| SCR-07 | partial | Game menu: `SFXGameModeBase.ShowMenu`; Leave as on ME2 | Holster is compiled out on ME3 and its button greyed ("Not on ME3 yet", the owner: greyed until seen) | `GameMenuRequest.cpp:229-230`, `:759-766`; `main.rml:36-37` |
| SCR-08 | same | `MainMenuSettings` | the rule is the same. On ME3 the core refuses every `cine_*`, `voice_*`, `mic*`, `thespian_*`, `world_ui`, `wrist_screen`, `wrist_width` and `galaxy_holo` write, whatever the page shows (see settings-and-defaults.md) | `MenuSettings.cpp:900-903` |
| SCR-09 | same core, other inputs | `plugin/HudPanel` fed by `EmitUiDraw`'s `MevrSeams::HudPanelDraw`; the gate is `IsHudGameplayMode`, translated for ME3 (on foot, vehicle, Atlas, both wheels, command, injured) | ME1's numbers, fixed; more on the panel than ME1 puts there (wheels, health, ammo); a few UI draws a second come before the left eye's capture and the later batch replaces them | `SequelOverlays.cpp:123-166`, `RendererLe3.cpp:55-57`, `:156-160`, `GameAdapter.hpp:84-87`; research/le3-hud-panel |
| SCR-10 | unknown | the panel's tint path is the shared module's | on the Normandy every UI draw was "over" (`src=5 dst=6`); ME3's wheels and combat HUD were never drawn on the panel in a run. A snapshot of the power wheel (`-hud.png` and the eyes' world layers) settles whether anything shows doubled | research/le3-hud-panel "In the game", "Not verified" |
| SCR-11 | partial | the label's band is masked out through `hpl::Interaction` | no wrist or gun mask (`caps.hudPieces` false: `SequelOverlays.cpp:207` is skipped), no wheel mask | `SequelOverlays.cpp:160`, `:207-226` |
| SCR-12 | same core, other inputs | `HandReticle` through `SequelWeaponLayers`, on `ControllerAimActive`; the crosshair movie's `_root._alpha` zeroed in HUD gameplay modes, the scope view left to the game | the mode handed over is 0 or -1, so the module's raw `gameMode == 1` vehicle branch (`HandReticle.cpp:142`) is never reached: no ring in the Atlas. The hide lapsed 0.2-0.4 s three times in a run (the 500 ms request); a crosshair drawn and hidden was never seen | `HalcyonLe3Bridge.cpp:244-246`, `SequelOverlays.cpp:261-266`; research/le3-hud-panel "The crosshair hidden" |
| SCR-13 | missing | none | the weapon wheel stays on the HUD panel. **Fixed in code, lane AU1 (2026-11-18, not run in the game):** `plugin/WheelPanel`, LE1's code shared, composed by `SequelOverlays.cpp` | ledger G57; `ModLoader.cpp:534` (LE1's list) |
| SCR-14 | partial | the power wheel's mode is HUD gameplay | `powerWheelAt = right, left, front` do nothing | `GameAdapter.hpp:84-87` |
| SCR-15 | missing | none | `game/HudPieces::Tick` only logs its stand-down; the service is not served | `HudPieces.cpp:62-68`, `:178-185`; `ModLoader.cpp:277-283` |
| SCR-16 | missing | none | the same; the raised-gun hand-over has nothing to hand over | the same |
| SCR-17 | adapter | gaze hook at ME3's `FindCurrentSelectionTarget` (0xA9BF90); the label through `hud_piece_layers::Interaction` | ME3's band (y 0..70); a failed panel leaves the native prompt, never a detached label. A's use after a gaze pick, combat targets: unverified | `SelectionGaze.cpp:17-19`, `HudPieces.cpp:54-55`, `SequelOverlays.cpp:157-162`; ledger G27 |
| SCR-18 | partial | `core/vr/WorldUi` in ME3's GUI mode 9; the focused `SFXGUIMovie`'s tag through `FromMe3Tag` | one screen at a time (the focused movie; `GetAllMovies` returned none), so ME1's "a visible unknown panel leaves the stack flat" is "an unknown focused movie is flat". The squad record and every ME3-only screen but War Assets are flat. No minigame mode. **Fixed in code, lane AU1 (6abedb31, not run in the game):** ME3's `Store` (ME1's shop), `WeaponSelect` (ME1's equipment), `ChoiceGUI`, `Terminal`/`Mail`/`Elevator`, `Accomplishments`, `HintBox`/`QueuedMessageBox` by tag; the squad record and the armour locker stay flat (UI world); the flat panel at ME1's 1.4 m / 2.35 m (research/le2-menus "The sequels' own screens") | `WorldUi.hpp:216-231`, `WorldUi.cpp:22-63`; research/le3-native-gui "The game's screens on ME3" |
| SCR-19 | partial | `Place` toward what A used (War Assets toward the War Terminal's point of interest) | ME1's vendor, loot and minigame anchors have no ME3 form because those screens are not known; otherwise straight ahead | research/le3-native-gui (run `om2-me3g`, `om3-me3c`) |
| SCR-20 | same core, other inputs | `DefaultCrop`'s whole-stage default | ME3's movies fill the 16:9 window; no tighter crop was taken; ME1's shop and loot crops never apply | research/le3-native-gui item 6 |
| SCR-21 | adapter | the view held through `MevrSeams::WorldViewHoldBegin/End` in ME3's calcview hook; `SFXGUIMovie.SetRequiresUIWorld(false)` in place of `bFullScreen`; a clean-eye hold for reserved openings | ME3 also hides Shepard's head (wrist) or head and body (room) while held; `FootViewMode` refuses the Atlas and injured Shepard | research/le3-native-gui lanes OM3, FX3; `RendererLe3.cpp:58-60` |
| SCR-22 | missing | none | the squad record keeps the native flat panel because its 3D Shepard needs the UI world | `game/WorldUi` `ShowsFigure`; ledger G23, R31 (ME3's scene capture is a fork, `plugin/Le3SceneCapture`) |
| SCR-23 | same core, other inputs | `OnWrist` with ME1's 12-tag default list | of the twelve, ME3 has MenuBrowser, Journal, Map, Options, Save, Load; CharacterRecord (`SquadRecord`) is in the list and still goes flat (SCR-22); SkillGame, Inventory, XMods, Specialization and Codex do not exist | `HandCalibration.cpp` (one `[WorldUi]` block for every game); `WorldUi.cpp:55-63` |
| SCR-24 | same | `world_ui::Current` with the wrist point; ME3's own omni-tool display as the emitter | none in the numbers | `MenusLe2.cpp:207-216`, `:357` |
| SCR-25 | same | `wrist_beam::Layer` | none | `MenusLe2.cpp:243-252` |
| SCR-26 | adapter | `game/OmniTool` starts ME3's own client effect (`RvrClientEffectManager.Start` of `CE_OmniTool`); the display is the player's live `OmniHand_Loop` | whether ME1's 0.60 s power-up drives anything on ME3's effect is not recorded; arm IK on the held frame: reading only | research/le3-native-gui "The omni-tool on ME3", lane FX3 item 5 |
| SCR-27 | same core, other inputs | the same rail and seven entries | Equipment answers "use the game's weapon/armour stations" and Codex "no page for it" (ME3's Mission Computer has none); both are drawn | research/le3-native-gui "Close from the area map" table; `GameMenuRequest.cpp:734-735` |
| SCR-28 | same | the rail's own slide | none | `MenusLe2.cpp:364-365` |
| SCR-29 | same core, other inputs | `omni_rail::Tap` | two of its inputs never arrive on ME3: `gunDrawn` (so the tap is allowed with a weapon drawn, where ME1 refuses and hides the target) and `body` (so the target is at the controller's wrist, not on the drawn forearm). The mode gate is translated (GUI 9, conversation 7 never opens the rail) | `MenusLe2.cpp:344-362`, `OmniToolRail.cpp:834`, `:854-857` |
| SCR-30 | adapter | `vr::game_menu::StepBackOut`: one B with its release per screen, the next once another has the focus 250 ms; pages by `ShowSelectedPanel` | ME1's "every 4 ticks, at most 6" restarted the area map's close animation on ME3; B alone does nothing without its release (37) | `GameMenuRequest.cpp:279-297`; research/le3-native-gui lane OMX |
| SCR-31 | partial | the rail has its own look in ME3's colours | not the game's art: ME3's frames are bitmap fills `core/gfx` skips and its font library is not a live object; `GameArt` stays LE1's | research/le3-native-gui "The rail in ME3's look" |
| SCR-32 | n/a | none | ME3 has no bypass puzzle (game knowledge, not checked in its data: ledger G24) | `CMakeLists.txt:1143-1193` (no `OmniPad`) |
| SCR-33 | partial | the pointer on wrist and room screens and on the flat GUI panel | not on the cinema UI layer, not on the galaxy UI, not on flat conversations | `MenusLe2.cpp:266-270`; `HalcyonLe3Bridge.cpp:126-129`, `:348-356` |
| SCR-34 | same | the fingertip rules | the fingertip is the controller's (no drawn body: SCR-29) | `MenusLe2.cpp:384-386` |
| SCR-35 | adapter | the same queue; ME3's numbers (move 6, left 31 / 52, wheel 8 / 9, B 16 with release 37); the pointer's mouse goes to the focused movie's own `SendMouseEvent`, the pad's events through `GuiKey` | `GuiKey` drops mouse events while the game is in its pad mode, which every B puts it in | `GuiCursor.cpp`; research/le3-native-gui "The movie's mouse" |
| SCR-36 | n/a | `HoldOffForPuzzle` is reachable, `puzzle` never set | no puzzle | `MenusLe2.cpp:263-265` |
| SCR-37 | same | `plugin/GamePointer` | none | shared module |
| SCR-38 | reinvented | host half shared; mod half is the sequels' `game/GalaxyProjectorFlow` with `Le3GalaxyCapture`, not `game/GalaxyMap` | no pointer (the map is flown); the opening's 2.5 s Kismet delay cut to 0.05 s as ME1's; the relay's movie flat with the window down; after a load inside the map: flat until reopened | `SequelOverlays.cpp:100-117`, `:171-186`; research/le3-galaxy lanes GX to GX4 |
| SCR-39 | partial | `[Galaxy] nativeTable = 1` (off) puts the flat native map image on a table with ME1's table numbers (0.70 m ahead, 0.45 m below, radius 0.48 m, tilt 35) | not ME1's hologram model with the map behind it: the map's picture itself lies on the table | `HalcyonSequelBridge.cpp:120-137`, `HalcyonLe3Bridge.cpp:329-347` |
| SCR-40 | missing | none | no first-person conversation on ME3 | `RendererLe3.cpp` (no `conversationWorld`) |

# Against ME2

| ME2 finding | on ME3 |
|---|---|
| Screens the tag list does not know fall to the flat panel | same defect, wider: ME3 also sends the squad record there, and its flat panel is larger and further (3.2 m at 2.2 m) |
| The flat panel is upstream's size, not ME1's | same defect, other numbers (ME2 1.6 / 2.2, ME3 2.2 / 3.2): three games, three flat panels |
| No pointer on the flat panel | fine: ME3 registers it (`HalcyonLe3Bridge.cpp:348-356`); ME2 should copy this |
| No pointer on the cinema UI layer or the galaxy UI | same; on ME3 the galaxy's is forced (the map is flown) and the UI layer's is a written deferral |
| The game's wheels never come to the hand | same defect |
| `HandReticle`'s raw `gameMode == 1` | different: ME3's adapter never passes a raw mode, so the wrong branch cannot fire, and no vehicle or Atlas ring exists either |
| Room screens only "straight ahead" (ME1's anchor handles) | different: ME3 places War Assets toward what A used; vendor and loot anchors still absent |
| The Equipment tab drawn and dead | same, plus Codex |
| ME1's absent tags written into `[WorldUi] wrist` | same |
| HUD panel and reticle numbers fixed, no key | same |
| The composition exists twice (LE1's and the sequels') | same file as ME2's, so ME3 adds no third copy |
| ME2 has the wrist and gun pieces | ME3 does not: its own loss, with the wrist tap's two gates behind it |

# Reinvented

| ME3 or sequel code | ME1 or shared code | what differs | what unifying takes |
|---|---|---|---|
| `plugin/SequelOverlays.cpp`, `plugin/MenusLe2.cpp` (both sequels) | `plugin/HalcyonOverlays.cpp`, `plugin/HalcyonBridge.cpp` | as the ME2 audit: one order written twice. ME3 adds nothing to it | move LE1 onto `SequelOverlays` (research/trilogy-seams "One overlay composition") |
| `game/GalaxyProjectorFlow.cpp` with `Le3GalaxyCapture` | `game/GalaxyMap.cpp` (ME1) | two flows for one behaviour; since lane GX4 the sequels at least share one | one flow with three adapters (ledger G25) |
| `plugin/Le3SceneCapture.cpp` | `plugin/RenderLevers`' capture hooks and `game/SceneCaptureRig` | ME3's own RVAs and its own module (ledger R31 "forked"); it is why the UI-world figure cannot simply be switched on | serve ME3's capture through the levers' table (`sceneCaptureVerified`), then `UiWorldFigure` |
| ME3's `FromMe3Tag` beside `FromTag` | `core/vr/WorldUi.cpp` `kTags` | a per-game function in shared code, the right shape; ME2 has none yet | the per-game table the ME2 audit asks for, with ME3's as the model |
| Halcyon ME3's flat panel distance and width | LE1's deploy values and `WorldPanels` | 2.2 m / 3.2 m against 1.4 m / 2.35 m | hand `PlacePanel` ME1's numbers, or pin `MenuScreenDist` and `MenuScreenSize` in the baseline |

# Rediscovered problems

| problem on ME3 | when and cost | ME1 already held it | sources |
|---|---|---|---|
| The first wrist screens stood in a dark void: the game rendered its UI world instead of the level | lane OM2, 2026-11-16 | "Mode 7's `bFullScreen` made the game render its UI world" | catalog "Problems ME1 already solved"; research/le3-native-gui item 5 |
| The pointer's clicks landed on the right pixel and the movie's mouse did not move | lanes OM2 and OM3, two lanes | "Posted mouse messages never moved Flash's cursor" (another cause on ME3: the pad mode gate) | research/le3-native-gui "The movie's mouse" |
| Close backed out eight times and the screen stayed | lanes CTL and OMX, 2026-11-17 | "B to the focused handler never closed a screen" (ME1's answer was `GuiKey(13)` on a tick schedule; ME3 needed the release and a wait per screen) | research/le3-native-gui "Close from the area map" |
| Behind a screen the eye was the GUI camera's, inside Shepard's chest | lane OM2, fixed by OM3 | SCR-21: the on-foot view held at render time | research/le3-native-gui "Shepard's view held behind a screen" |
| The game's crosshair floated on the panel next to the hand reticle | 2026-11-16 to 11-17 (lane CH) | SCR-12: all reticle panels suppressed in VR gameplay | research/le3-hud-panel "The crosshair hidden" |
| The galaxy map's first frames fell to the cinema screen or the flat panel | lanes GX3, GX4 | "The galaxy map's first frame fell to the flat panel when a use played a call first" | research/le3-galaxy |
| The arm is drawn away from the controller, and the tap's target is at the controller | not yet reported: waiting | "Nothing showed where to tap; the arm is drawn up to 15 cm from the controller" | this audit, SCR-29 |

# Forced by the game

- ME3's GUI is `SFXGUIInteraction` and `SFXGUIMovie`, not `BioSFManager` panels: the focused movie is the screen, a new
  movie per page, `SetRequiresUIWorld` in place of `bFullScreen` (research/le3-native-gui).
- GUI mode 9, conversation 7, cinematic 8, galaxy 11, orbit 12; two more gameplay modes (Atlas 2, injured 6).
- ME3's screens act on B's release, and the area map's B starts a 0.24 s close animation.
- ME3's mouse events are dropped in pad mode; `SendMouseEvent` is the route its own scripts use.
- No codex page in the Mission Computer, no equipment screen outside stations, no bypass puzzle.
- The galaxy map is flown, not picked; LT and RT are its scan and pursuit.
- ME3's art is bitmap fills outside the movies, so the rail cannot be drawn with it by `core/gfx` as it is.
- ME3's renderer keeps no HUD panel settings.

# Not forced

- No wrist status and no gun status: the panel image is there, the layers are in the host's list
  (`HudPieceLayers.cpp`), the capability and two crops are missing.
- The wrist tap allowed with a weapon drawn, and its target on the controller: both hang on the HUD pieces' publication,
  which also carries the drawn arm's bones and has nothing to do with the HUD's crops.
- The flat panel's 2.2 m and 3.2 m.
- The squad record flat (it needs the UI-world figure's port, which is work, not a wall).
- Equipment and Codex drawn on the rail.
- The game's wheels not at the hand.
- The menu refusing `wrist_width` on ME3 while the wrist screens work.
- ME3's own GUI screens missing from `FromMe3Tag`.

# What to do

1. **Publish the drawn arm and "a weapon is drawn" on ME3 now, apart from the HUD crops.** Split `game/HudPieces`'
   `FillBody` and `gunVisible` from the wrist and gun crops, serve `setHudPieces` on LE3 for those fields
   (`ModLoader.cpp` `WithHudPieces`, `RendererLe3.cpp`), leave the two quads off. Check: `omni-tool rail:` logs "a weapon
   is drawn (holster it first)" with a gun out, and the snapshot's `omniRail.target` sits on the drawn forearm.
2. **Give the flat panel ME1's numbers** on both sequels: `MevrSeams::PlacePanel` returns 1.4 m and 2.35 m, or
   `ApplyMevrBaseline` pins `MenuScreenDist` / `MenuScreenSize`. Check: `world panels: game menu` lines and a snapshot of
   the squad record.
3. **Name ME3's own screens.** A run with `mevr guimouse` (the focused movie's tag) at a shop, the weapon bench, the armour
   locker, squad selection, the Spectre and private terminals; add them to `FromMe3Tag`; place the ones with an owner by
   `Place`. Check: `mod.worldUi` names each and `flat-panel frames during reservation` reads 0.
4. **The wrist and gun pieces for ME3**: measure the two bands on a combat save (research/le3-combat-fixtures), add
   `Layout::Me3` crops to `core/vr/HudPieces.cpp`, set `caps.hudPieces`. Check: `-hud.png`, the `wrist` and `gun` layers.
5. **The squad record on the wrist**: the UI-world figure on ME3's capture (ledger R31). Check: the rail's Squad stands
   on the wrist with Shepard behind the UI.
6. **Hide Equipment and Codex on ME3's rail** (a data flag in `omnitool.rml`). Check: `mevr-menu-preview --le3`.
7. **The wheel at the hand**, with ME2 (ME2 audit, item 4).
8. **Un-refuse `wrist_width`** in `MenuSettings.cpp:900-903` now that the wrist screens are ME3's. Check: the slider
   changes `[WorldUi] wristMenuWidthM` in `LE3MEVR.ini`.

Question for the owner: with the squad record flat and the journal on the wrist, should the rail's Squad entry be greyed
on ME3 until the figure is ported, as Holster is?

# Citations

- Catalog: [menus-and-game-screens.md](../../me1/menus-and-game-screens.md), [presentation-modes.md](../../me1/presentation-modes.md).
  ME2's audit: [menus-and-game-screens.md](../me2/menus-and-game-screens.md).
- Ledger: [trilogy parity ledger](../../../trilogy-parity-ledger.md) rows R18-R20, R31, G19-G28, G56, G57. Disagreements:
  G22 "adapted V" hides that the tap's weapon gate and body target never reach ME3's host; G23 "adapted V" hides that the
  squad record and every ME3-only screen but War Assets are flat; G28 "missing" is right and understates what else reads
  the pieces; G26 is right and better than ME2's.
- Research: [le3-native-gui](../../../../research/le3-native-gui.md), [le3-hud-panel](../../../../research/le3-hud-panel.md),
  [le3-galaxy](../../../../research/le3-galaxy.md), [le3-menu-pause](../../../../research/le3-menu-pause.md),
  [le3-interaction-gaze](../../../../research/le3-interaction-gaze.md), [trilogy-seams](../../../../research/trilogy-seams.md),
  [57 omni-tool menu](../../../../research/57-omni-tool-menu.md).
- Source: [SequelOverlays.cpp](../../../../src/plugin/SequelOverlays.cpp), [MenusLe2.cpp](../../../../src/plugin/MenusLe2.cpp),
  [HalcyonLe3Bridge.cpp](../../../../src/plugin/HalcyonLe3Bridge.cpp), [RendererLe3.cpp](../../../../src/plugin/RendererLe3.cpp),
  [Renderer.hpp](../../../../src/plugin/Renderer.hpp), [ModLoader.cpp](../../../../src/plugin/ModLoader.cpp),
  [HandReticle.cpp](../../../../src/plugin/HandReticle.cpp), [OmniToolRail.cpp](../../../../src/plugin/OmniToolRail.cpp),
  [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp), [Seams.cpp](../../../../src/plugin/Seams.cpp),
  [me2_xr.cpp](../../../../src/halcyon/le3/me2_xr.cpp), [me2_menu.cpp](../../../../src/halcyon/le3/me2_menu.cpp),
  [WorldUi.hpp](../../../../src/core/vr/WorldUi.hpp), [WorldUi.cpp](../../../../src/core/vr/WorldUi.cpp),
  [HudPieces.cpp](../../../../src/core/vr/HudPieces.cpp), [MenuSettings.cpp](../../../../src/core/vr/MenuSettings.cpp),
  [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp), [SelectionGaze.cpp](../../../../src/engine/SelectionGaze.cpp),
  [HudPieces.cpp](../../../../src/game/HudPieces.cpp), [GameMenuRequest.cpp](../../../../src/game/GameMenuRequest.cpp),
  [MenuPause.cpp](../../../../src/game/MenuPause.cpp), [GalaxyProjectorFlow.cpp](../../../../src/game/GalaxyProjectorFlow.cpp),
  [main.rml](../../../../assets/menu/main.rml), [omnitool.rml](../../../../assets/menu/omnitool.rml),
  [CMakeLists.txt](../../../../CMakeLists.txt).
