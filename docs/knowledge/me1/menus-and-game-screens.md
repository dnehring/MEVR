---
type: System
title: Menus and game screens (ME1)
description: Where every panel of ME1's VR interface sits, how big it is and how it is operated, as numbered contracts with their source constants: our main menu, the HUD panel and what is cut out of it, the hand reticle, the game's wheels at the hand, the wrist and gun HUD pieces, the interaction label, the game's screens in the room and on the wrist, the omni-tool's rail and its wrist tap, the bypass d-pad, the game pointer, the galaxy map over the CIC's table and the cinema screen's UI layer.
tags: [me1, menus, hud, world-ui, omni-tool, pointer, galaxy-map, catalog]
status: stable
timestamp: 2026-11-18T07:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T07:00:00Z }
game: LE1
---

# What the player gets

- Hold both sticks: a glass board opens 1.3 m ahead, the game pauses. Point with either controller and pull the trigger, or
  move the focus with the left stick and press A. B backs out, then closes.
- The game's HUD floats 1.5 m ahead of the body, below eye level, and stays with the body when the stick turns the world.
- A ring 10 m out on the gun's line replaces the crosshair.
- Health and shields sit on the back of the left wrist; the gun's heat and grenades sit on the gun.
- What A would use is named at the object itself, the same size at any distance.
- The game's weapon wheel opens above the left hand.
- The game's own screens stay the game's: a shop stands between the player and the vendor at counter height; the
  inventory, squad, map, journal, codex and options unfold from the left wrist, 0.70 m wide.
- Tap the left forearm with the right index finger: a rail of the game's screens appears. Touch a tab, or point and pull.
- Every game screen is worked by a fingertip on its surface or by a controller's ray; the game sees its own mouse.
- The bypass puzzle is played on a touch d-pad beside the wrist screen.
- The galaxy map opens over the CIC's table, as a stereo window on the projector's disc.
- No feature depends on a controller's menu or system button.

# Where each panel sits

Distances are from the eyes unless said otherwise; every number is a default with its constant or key.

| panel | anchor | distance | size | operated by | source |
|---|---|---|---|---|---|
| Our main menu | fixed in the room where it opens, ahead of the head's heading, level, facing the head | 1.30 m (`[Menu] distanceM`, 0.6..3.0); centre 0.10 m below the eyes | 1.45 m wide (`widthM`, 0.6..3.0) x 0.906 m (canvas 1440 x 900) | either aim ray + trigger; left stick or d-pad focus; A; B; right stick scrolls | [MainMenu.cpp](../../../src/plugin/MainMenu.cpp) `Place` |
| Halcyon's classic menu (developer) | world-locked where it appears | 1.54 m (`menuDistanceM`) | Halcyon's | pad, or our laser pointer | [vr_config.h](../../../src/halcyon/le1/vr_config.h) |
| HUD panel | body-locked: tracking space, straight ahead at recenter | 1.5 m (`hudPanelDistanceM`; menu 1..3) | 1.3 m wide (`hudPanelWidthM`; menu 45..155%), height from the frame's aspect; centre 0.25 m below eye height (`hudPanelDropM`) | not operated | [HudPanel.cpp](../../../src/plugin/HudPanel.cpp), [OverlayPlacement.hpp](../../../src/core/vr/OverlayPlacement.hpp) `Panel` |
| Hand reticle | on the gun's aim ray, facing the eye | 10 m along the ray (`handReticleDistanceM`, 1..100) | 1.5 degrees across (`handReticleSizeDeg`, 0.2..10); 128 px ring with a centre dot | not operated | [HandReticle.cpp](../../../src/plugin/HandReticle.cpp) |
| The game's weapon wheel | above the left hand when it opens, facing the eyes | centre 0.27 m from the hand (`panelDistanceM`; 0.86 of it up, 0.51 out from the head) | 0.40 m wide (`panelWidthM`, 0.15..1.5) | right stick steers; pointing the left hand preselects; letting go of the left trigger gives | [SquadMenu.cpp](../../../src/core/vr/SquadMenu.cpp) `PlaceAtHand`, [HalcyonOverlays.cpp](../../../src/plugin/HalcyonOverlays.cpp) `PlaceWheel` |
| The game's power wheel | stays on the HUD panel (`powerWheelAt = hud`) | as the HUD panel | as the HUD panel | the game's own stick | [HandCalibration.hpp](../../../src/game/HandCalibration.hpp) |
| A wheel with no tracked hand | ahead of the head | spot 0.45 m ahead and 0.35 m below the head, then as at a hand | 0.40 m | as above | `PlaceWheel` |
| Wrist status | the left grip pose | offset (-0.03, 0.06, 0.05) m in grip axes: 3 cm over the back of the wrist, 6 cm up the forearm, 5 cm down the handle | 0.08 m wide (`wristWidthM`) | not operated | [HudPieces.hpp](../../../src/core/vr/HudPieces.hpp), [HudPieceLayers.cpp](../../../src/plugin/HudPieceLayers.cpp) |
| Gun status | the drawn gun | 0.15 m behind the muzzle (`gunBackM`), 0.04 m above the bore (`gunUpM`); the pistol 0.05 m to its left, 0 above (`pistolSideM`, `pistolUpM`) | 0.06 m wide (`gunWidthM`) | not operated | [HudPieces.hpp](../../../src/core/vr/HudPieces.hpp), [HudPieces.cpp](../../../src/game/HudPieces.cpp) |
| Interaction label | at the selected object: 12 units over its bounds' top (the top at most 60 over the centre), never more than 15 units over eye height | the object's | 6 degrees tall at any distance (`labelHeightDeg`) | A uses the object | [InteractLabel.hpp](../../../src/core/vr/InteractLabel.hpp) `LabelPoint`, `Place` |
| A game screen in the room | toward what it belongs to (vendor, container, the thing used), else the head's flat forward; fixed once per opening | 0.95 m (`distanceM`); never nearer the anchor than 0.35 m nor the eyes than 0.55 m | 1.20 m wide (`widthM`); centre 0.50 m below the eyes (`belowM`); dark parts at 0.80 opacity | fingertip touch; aim ray + trigger; right stick wheel | [WorldUi.hpp](../../../src/core/vr/WorldUi.hpp) `Place`, `PlaceDistance` |
| A game screen in the Mako's cockpit | toward the cockpit's HUD anchor | as `Place` gives | 0.85 of its on-foot angular size; centre 2 degrees above eye level | as above | [WorldUi.hpp](../../../src/core/vr/WorldUi.hpp) `InCockpit` |
| A personal screen on the wrist | bottom edge above the left wrist as the eyes see it, facing the eyes, riding the wrist | bottom edge 0.10 m above the wrist (`wristAboveM`); never nearer the eyes than 0.25 m, nor wider than 60 degrees | 0.70 m wide, the whole 16:9 stage (`wristMenuWidthM`; menu 0.4..1.0) | fingertip of the right hand; right aim ray; never the projecting hand | [WorldUi.hpp](../../../src/core/vr/WorldUi.hpp) `WristPlace`, `WristNearest` |
| The bypass minigame | the wrist, as above | as above | 0.30 m wide, its frame only (`wristWidthM`) | the d-pad | [WorldUi.cpp](../../../src/core/vr/WorldUi.cpp) `WristCrop` |
| The omni-tool's rail | beside the wrist screen in its plane, on the eye's right; alone: projected from the wrist | gap 0.015 m (`kGapM`) | as tall as the screen, 0.5625 of that wide (canvas 360 x 640); alone 0.32 m tall (`aloneHeightM`) | fingertip (clicks on contact); aim ray + trigger | [OmniRail.hpp](../../../src/core/vr/OmniRail.hpp), [OmniToolRail.cpp](../../../src/plugin/OmniToolRail.cpp) |
| The wrist-tap target | on Shepard's drawn left forearm, 30% of the way from wrist to elbow, lifted 0.035 m toward the eyes | the arm's | 0.04 m square | the right index fingertip | [OmniRail.hpp](../../../src/core/vr/OmniRail.hpp) `BodyTarget` |
| The bypass d-pad | to the right of the puzzle's screen, in its plane, level with its centre | gap 0.02 m | 0.17 m square (`padSizeM`); each arm about 5.7 cm | right fingertip, or ray + trigger, held on an arm | [OmniPad.hpp](../../../src/core/vr/OmniPad.hpp), [OmniPad.cpp](../../../src/plugin/OmniPad.cpp) |
| The game pointer's dot | on the quad pointed at | 5 mm in front of it (`kDotLiftM`) | 0.6 degrees (`dotDeg`, 0.1..5) | not operated | [GamePointer.cpp](../../../src/plugin/GamePointer.cpp) |
| The flat panel (fallback) | world-locked where the head was | 1.4 m | 2.35 m wide | the game pointer | [presentation-modes.md](presentation-modes.md) PRS-18 |
| The galaxy map's window | on the CIC projector's own hologram disc, at its tilt | the CIC's (the disc measured live: about 29 degrees, 6 m) | 0.7 of the projector's disc (`windowScale`); a disc (`windowRound`); upright fallback 2.0 m wide (`windowWidthM`) | aim ray + trigger as the game's mouse | [GalaxyMap.cpp](../../../src/game/GalaxyMap.cpp), [GalaxyHologram.cpp](../../../src/plugin/GalaxyHologram.cpp) |
| The galaxy map's Flash layer and travel box | the same place as the window | the same | 1.50 m wide UI frame (`kGalaxyUiWidthM`) | the game pointer | [HalcyonOverlays.cpp](../../../src/plugin/HalcyonOverlays.cpp) |
| The cinema screen's UI layer | 0.5 m in front of the 3D screen, toward the eyes | 3.0 m with the default screen | scaled to cover the same angle as on the screen | the pad; the game pointer | [CineScreen.hpp](../../../src/core/vr/CineScreen.hpp) `PlaceUi` |
| A first-person conversation's subtitle | under the speaker's face | 0.22 m below it (`subtitleBelowM`) | 4 degrees tall (`subtitleHeightDeg`, 0.5..20) | not operated | [ConversationWorld.cpp](../../../src/game/ConversationWorld.cpp) |
| A first-person conversation's wheel | in front of the head (`wheelHand = front`) | spot 0.45 m ahead, 0.35 m below; centre 0.12 m from it (`wheelDistanceM`) | 0.40 m wide (`wheelWidthM`) | left stick, A | the same file, `PlaceWheel` |

Our own power wheel, squad wheel, placement ring and coach card are in [powers-and-squad.md](powers-and-squad.md); the
calibration's panel and targets in [body-and-ik.md](body-and-ik.md); the Thespian panel in
[conversations-and-scenes.md](conversations-and-scenes.md); the sights and scope in
[weapons-and-sights.md](weapons-and-sights.md).

# Contracts

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| SCR-01 | Holding both stick clicks opens and closes our main menu; so does the menu key. A quick tap of both is a snapshot, never the menu. Resume or B close it. | hold 400 ms (`kStickHoldMs`); tap: both down within 100 ms and up within 300 ms of the first press (`kStickTapMs`); L3 is held back 100 ms; `menuKey` 220 (`\`) with a game window in front | LE1 host; Ctrl or Shift + the key opens Halcyon's menu instead | [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp) `UpdateStickChord`, [HalcyonBridge.cpp](../../../src/plugin/HalcyonBridge.cpp) | unit test; headset |
| SCR-02 | The menu is a quad fixed in the room for one opening, placed ahead of the head's heading, level. | 1.30 m, 0.10 m below the eyes, 1.45 x 0.906 m; clamps 0.6..3.0 | placed again at each opening | [MainMenu.cpp](../../../src/plugin/MainMenu.cpp) `Place`, [MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp) | fake headset |
| SCR-03 | While it is open the game sees no input and the mod no controllers; on foot and in the Mako the game is paused. | `[Menu] pause = 1`; `WorldInfo.PauseGame` as the game's weapon wheel does | not paused during live calibration adjustments | [MainMenu.cpp](../../../src/plugin/MainMenu.cpp), [MenuPause.cpp](../../../src/game/MenuPause.cpp); HostServices v16 | fake headset |
| SCR-04 | Buttons and triggers down when the menu or the calibration closes are kept from the game until released. | a trigger counts as let go below 30 of 255 (`kCloseLatchTriggerOff`) | the XInput hook | [GamepadMapping.hpp](../../../src/game/GamepadMapping.hpp) `CloseLatch` | unit test |
| SCR-05 | The menu's pointer: a controller's aim ray is the mouse, the trigger clicks, the right stick scrolls, haptic ticks. The pad or left stick moves the focus, A activates, B backs out then closes. | trigger latch 0.6 on / 0.4 off; stick focus at 0.6, dead zone 0.2; scroll 10 steps a second at full deflection; pointer smoothing 0.5, dead band 1.5 px; focus repeat 400 ms then 140 ms | open | [MainMenu.cpp](../../../src/plugin/MainMenu.cpp), [MenuPointer.hpp](../../../src/core/vr/MenuPointer.hpp) | fake headset (`tools/make-menu-recording.py`) |
| SCR-06 | Not drawn for over a second the menu closes itself and a running calibration is cancelled, so the desktop game never stays paused with no input. | 1000 ms (`kNotDrawnMs`) | every Present | [MainMenu.cpp](../../../src/plugin/MainMenu.cpp) `OnPresent` | fake headset |
| SCR-07 | The menu's top bar is the way into the game's own menus: Game menu and Holster close our menu and ask the mod, which calls the game's own function on the game thread. Never a pad press. | 2 ticks after the menu's pause is lifted; `ShowMenuGui` on the active game mode; `TryHolster` only while a weapon is drawn | Leave is never shown on ME1 | [GameMenuRequest.hpp](../../../src/core/vr/GameMenuRequest.hpp), [GameMenuRequest.cpp](../../../src/game/GameMenuRequest.cpp); HostServices v19 | fake headset, 2026-10-26 |
| SCR-08 | A row's value is written key by key, never the whole file; a slider's only once it has rested. Closed, the menu costs nothing. | slider 300 ms after it last moved; a toggle at once; settings re-read every 500 ms while open; the frosted backdrop refreshed every 100 ms from the left eye's image | open | [MenuSettings.hpp](../../../src/core/vr/MenuSettings.hpp) `PendingWrites` | unit test |
| SCR-09 | The game's UI draws go to the HUD panel in gameplay modes 0-4. The panel is fixed in tracking space, so it stays in front of the physical body while the stick turns the world, and swings round when the head stays turned away. | 1.5 m, 1.3 m wide, drop 0.25 m; follow past 70 degrees (`hudPanelFollowDeg`; 0 never) held 1 s (`followDelay`); recenter puts it straight ahead | `hudPanel = 1`; not in conversations, cinematics, GUI screens, movies, the map | [HudPanel.cpp](../../../src/plugin/HudPanel.cpp), [OverlayPlacement.hpp](../../../src/core/vr/OverlayPlacement.hpp), [GameAdapter.hpp](../../../src/engine/GameAdapter.hpp) `IsHudGameplayMode` | headset |
| SCR-10 | Scaleform's non-"over" blend modes go to the panel too, through a tint texture its alpha is made from (else the wheel showed doubled in the eyes). | none | panel on | [HudPanel.cpp](../../../src/plugin/HudPanel.cpp) | headset |
| SCR-11 | Three parts are masked out of the HUD panel while they show elsewhere: the wrist status, the gun status, the interaction label; and the wheel's part while a wheel panel is up. The radar and power bar stay. | the panel shader takes three masks | each piece shown | [HalcyonOverlays.cpp](../../../src/plugin/HalcyonOverlays.cpp) | fake headset |
| SCR-12 | The hand reticle follows the mod's aim ray (the muzzle and barrel); before the mod's first ray it follows the right controller's aim pose. While the mod's ray stops coming the ring stays where it last aimed. All of the game's Reticule panels are suppressed in VR gameplay. | 10 m, 1.5 degrees; a ray is "stale" after 1000 ms (logged, not a fallback) | on foot with hand aim; in the Mako only on a ray the mod publishes; hidden while a sight hides the ring, in a non-world frame, with the menu open | [HandReticle.cpp](../../../src/plugin/HandReticle.cpp), [ScreenReticle.cpp](../../../src/game/ScreenReticle.cpp) | headset; `mevr reticles` |
| SCR-13 | While the game's weapon wheel is up, its part of the UI is cut out of the HUD panel and shown on a quad above the left hand, placed when the wheel opens, when the mod reopens it, and on recenter. | 0.27 m from the hand, 0.40 m wide; the crop guessed from the stage, then measured once per session 600 ms after a wheel opens (`kWheelGridAfterMs`), a HUD-only grid every 1000 ms until measured | `[Squad] wheelPanel = 1`, `weaponWheelAt = left`; `wheelCrop = auto` | [WheelCrop.hpp](../../../src/core/vr/WheelCrop.hpp), [HalcyonOverlays.cpp](../../../src/plugin/HalcyonOverlays.cpp); HostServices v12 `setWheelPanel` | headset |
| SCR-14 | The game's power wheel (right stick click held) stays on the HUD panel. | `powerWheelAt = hud` (the user, 2026-10-22: the right hand is busy with the gun) | `right`, `left`, `front` move it to a quad | [HandCalibration.hpp](../../../src/game/HandCalibration.hpp) | headset |
| SCR-15 | The player's status (health and shield arc, squad portraits, medi-gel) rides on the back of the left wrist, facing the eyes, upright. | band: stage x 440..840, 165..25 px from the frame's bottom (1280 across the width); 0.08 m wide; offset (-0.03, 0.06, 0.05) | left grip tracked; `[Hud] wristStatus = 1`; else it stays on the HUD panel | [HudPieces.hpp](../../../src/core/vr/HudPieces.hpp) `DefaultWristCrop`, `WristPoint` | fake headset |
| SCR-16 | The weapon panel (silhouette, heat strip, grenades) rides over the gun's receiver. Raised to the eye it leaves the gun for the HUD panel so the sights stay clear. | band: stage x 56..224, 145..10 px from the bottom; 0.06 m wide; 0.15 m back, 0.04 m up; the pistol's 0.05 m to the left; leaves at a raised weight over 0.55, returns under 0.35 | a weapon drawn on foot; `[Hud] gunStatus = 1`; the mod's pieces fresh within 300 ms | [HudPieces.cpp](../../../src/game/HudPieces.cpp), [HudPieces.hpp](../../../src/core/vr/HudPieces.hpp) `GunPoint`; HostServices v22 | headset, 2026-11-05 |
| SCR-17 | What A uses is chosen from where the head looks, and the game's prompt (name, action, the A glyph in use range) is shown on a quad at the object. | band: stage x 384..896, y 30..100; 6 degrees tall; the label point 12 units over the top, capped | on foot; combat targets stay on the HUD unless `combatLabel = 1`; `[Interact] gaze = 1`, `label = 1` | [SelectionGaze.cpp](../../../src/engine/SelectionGaze.cpp), [Interact.cpp](../../../src/game/Interact.cpp), [InteractLabel.hpp](../../../src/core/vr/InteractLabel.hpp); HostServices v21 | fake headset |
| SCR-18 | Every flat screen of the game stands in the room (the front end excepted): in modes 7 and 10 the first visible panel that is one of ours places the screen; a visible one not in `screens` leaves the whole stack on Halcyon's flat panel. | 20 screen tags (`Screen`); `screens = all` | needs an on-foot view to hold, except PartySelect (`CanPlaceWithView`) | [WorldUi.hpp](../../../src/core/vr/WorldUi.hpp), [WorldUi.cpp](../../../src/game/WorldUi.cpp); HostServices v27 | fake headset |
| SCR-19 | A room screen is placed once per opening toward what it belongs to: the shop's vendor (the pawn nearest `BioSFHandler_Shop.m_shop`), loot's container or body, the minigame's dependent, what A would have used. Without an anchor: straight ahead. | 0.95 m; 0.35 m short of the anchor at the nearest; 0.55 m from the eyes at the nearest; centre 0.50 m below the eyes; faces the eyes | a new screen inside mode 7 is a new opening: placed, labelled and unfolded again | [WorldUi.hpp](../../../src/core/vr/WorldUi.hpp) `Place`; [WorldUi.cpp](../../../src/game/WorldUi.cpp) | fake headset |
| SCR-20 | A room screen shows the 16:9 stage's content without the empty sides, its dark parts see-through. | shop: stage x 0.14..0.86; loot: x 0.30..0.70, y 0.04..0.95 and it keeps the stage's scale (0.48 m wide at the default); others the whole stage; `backdrop` 0.80 | `[WorldUi] crop` by hand | [WorldUi.cpp](../../../src/core/vr/WorldUi.cpp) `DefaultCrop`, `RoomWidth` | fake headset |
| SCR-21 | Shepard's view on foot is held at render time while a screen is up, and `bFullScreen` is cleared on our screens so the level keeps rendering. | the hold's state fresh 500 ms; the screen's state fresh 400 ms | put back if ours goes down | [WorldUi.cpp](../../../src/plugin/WorldUi.cpp), [WorldUi.cpp](../../../src/game/WorldUi.cpp) | fake headset |
| SCR-22 | The equipment, XMods and squad screens' squad member is drawn behind the game's UI, over the 16:9 stage: the mod ticks the game's UI world and points a scene capture of ours with its camera. | `[WorldUi] figure = 1`, `figureOpacity` 0 (the backdrop's) | those three screens | [UiWorldFigure.cpp](../../../src/game/UiWorldFigure.cpp), [UiWorldFigure.cpp](../../../src/plugin/UiWorldFigure.cpp) | fake headset |
| SCR-23 | Every personal screen is projected from the left wrist: SkillGame, Inventory, XMods, CharacterRecord, Specialization, Journal, Codex, Map, MenuBrowser, Options, Save, Load. A screen a use opened (a locker's equipment) still stands by what was used. Shop, loot and the rest stay in the room. | 12 tags in `[WorldUi] wrist` | no tracked left controller when it opens: in the room; an opening from the rail always goes to the wrist | [WorldUi.cpp](../../../src/core/vr/WorldUi.cpp) `OnWrist` | fake headset, 2026-11-04 |
| SCR-24 | A wrist screen faces the eyes with its bottom edge above the wrist across the line of sight, rides the wrist, and is pushed out so it never looks too wide. | 0.70 m wide (minigame 0.30); 0.10 m above; nearest 0.25 m; at most 60 degrees wide (0.70 m: at least 0.61 m away); wrist smoothing 0.05 s; `wristFollow = 1`, `wristTowardM` 0 | projected from the omni-tool's display as rendered on Shepard's arm when the mod sends it, else the controller's wrist | [WorldUi.hpp](../../../src/core/vr/WorldUi.hpp) `WristPlace`; [WorldUi.cpp](../../../src/plugin/WorldUi.cpp) | fake headset |
| SCR-25 | It unfolds out of the wrist when it opens, and a faint fan of light runs from the wrist to its bottom edge. | unfold 0.18 s, the width from 0.6, eased out, the bottom edge fixed; fan alpha 0.30 at the wrist to 0.05 at the panel, apex 0.04 of the width; a screen left where it was projected keeps its fan 0.6 s | `wristBeam = 1` | [WorldUi.hpp](../../../src/core/vr/WorldUi.hpp) `Unfold`, `BeamTo`; [WristBeam.cpp](../../../src/plugin/WristBeam.cpp) | fake headset |
| SCR-26 | While a wrist screen or the rail is up Shepard's forearm wears the game's own omni-tool, and arm IK keeps running on the held on-foot frame. | powers up over 0.60 s with a first flicker | `[WorldUi] omniTool = 1` | [OmniTool.cpp](../../../src/game/OmniTool.cpp), [OmniPower.hpp](../../../src/core/vr/OmniPower.hpp) | fake headset; headset pending |
| SCR-27 | The rail lists Squad, Equipment, Map, Journal, Codex, Computer, Close, on the right of the wrist screen. | 7 entries; gap 0.015 m; `[OmniTool] railSide = right` | `[OmniTool] enabled = 1` | [OmniRail.hpp](../../../src/core/vr/OmniRail.hpp) `Entry`, `Beside` | fake headset, 2026-11-04 |
| SCR-28 | The rail reveals after the screen's unfold by sliding out from under the screen's edge; a rail already standing alone fades in place instead of travelling across the screen. The drawn and the touchable part are clipped alike. | wait 0.18 s, slide 0.43 s (cubic ease in-out, about 0.237 m), alone fade 0.10 s | tab back-outs keep the slide | [OmniRail.hpp](../../../src/core/vr/OmniRail.hpp) `Slide`, `Exposed` | headset, 2026-11-09 |
| SCR-29 | Tapping the left forearm with the right index fingertip opens the rail alone on the wrist; a tap or Close shuts it and the screens. | press within 0.04 m coming from beyond 0.08 m; cooldown 0.6 s; target 30% up the forearm, lifted 0.035 m; 0.04 m square; faint with the wrist raised (within 0.45 m below the eyes and 0.60 m across), brighter with the fingertip within 0.30 m, cyan when armed | no weapon drawn, no menu of ours up, both hands tracked; not while the fingertip goes for a wrist panel (SCR-34); `[OmniTool] tap = 1`, `tapOnBody = 1` | [OmniRail.hpp](../../../src/core/vr/OmniRail.hpp) `Tap`, `BodyTarget`, `TargetLookFor` | fake headset; [verify] distances with a person |
| SCR-30 | A rail entry backs out of the screen that is up with the player's B as the game hears it, then opens the wanted screen with the game's own call. | `GuiKey(13)` every 4 ticks, at most 6 times, until the game leaves mode 7; then `Show<Screen>Gui(true)` | one B per screen | [GameMenuRequest.cpp](../../../src/game/GameMenuRequest.cpp) | fake headset, 2026-11-04 |
| SCR-31 | The rail is drawn with ME1's own art, copied from the game's movies into the player's cache at run time; without the cache it has its own look. | six movies to `%LOCALAPPDATA%\MEVR\gameart` once per level | never committed or shipped | [GameArt.cpp](../../../src/game/GameArt.cpp), [RailArt.cpp](../../../src/plugin/RailArt.cpp) | fake headset |
| SCR-32 | The bypass puzzle is played with a touch d-pad: a held arm sends the game's own d-pad GUI events; sliding to the next arm changes direction without lifting. | 0.17 m; hub 0.13 of the size; press events 11.., releases 30-33; the pad stays 300 ms through a hitch | while the handler's `m_bGameActive` is set | [OmniPad.hpp](../../../src/core/vr/OmniPad.hpp), [OmniPad.cpp](../../../src/plugin/OmniPad.cpp), [GuiCursor.cpp](../../../src/game/GuiCursor.cpp) | fake headset, 2026-11-05 |
| SCR-33 | A controller's aim ray on a game screen is the game's mouse: the hit's UV is the window's client pixel, the trigger is the left button, the right stick is the wheel. The pointing hand's triggers are kept from the game. | trigger latch 0.6 / 0.4; wheel 6 notches a second; stick dead zone 0.2; smoothing 0.5; dead band 1 px; dot 0.6 degrees | flat quads for menus and the galaxy map; room and wrist screens; the UI layer; not flat conversations or cutscenes (`flatCine = 0`) | [GamePointer.hpp](../../../src/core/vr/GamePointer.hpp), [GamePointer.cpp](../../../src/plugin/GamePointer.cpp) | fake headset |
| SCR-34 | A fingertip near a screen's visible part takes over from the rays: it points while hovering, presses at the surface from the front, lets go when pulled back. A tap clicks where the finger touched. | tip 0.04 m ahead of the aim pose (`touchTipM`; the rail and pad use 0.035); hover 0.06 m; press 0.005 m; release 0.015 m; holds 0.06 m behind; margin 0.01 m; click if let go within 0.15 s; a drag past 0.02 m scrolls, a notch per 0.013 m; a poke 0.01 m through clicks once | `[Pointer] touch = 1`; the hand a wrist screen is projected from neither touches nor points at it | [GamePointer.hpp](../../../src/core/vr/GamePointer.hpp) `Settings` | unit test; headset 2026-11-06 |
| SCR-35 | The pointer reaches the game's Flash screens through the game's own GUI events and an answered `GetCursorPos`, with no real input and no window focus. | events through `SFXGameModeBase.GuiKey`: 6 motion, 50 / 52 the left button, 28 / 29 the wheel; the cursor answer held 150 ms; the mod counts as taking events 1000 ms after its last take | `[Pointer] gameCursor = 1`; not the galaxy map; without a mod that takes them: `SendInput` nudges, foreground only | [GuiEvents.hpp](../../../src/core/vr/GuiEvents.hpp), [GuiCursor.cpp](../../../src/game/GuiCursor.cpp); HostServices v30 | fake headset, 2026-11-05 |
| SCR-36 | While the bypass puzzle is played the pointer stays off every game quad and `GetCursorPos` is not answered: any mouse motion spins the puzzle's marker. | until 1000 ms after the last frame of play (`kPuzzleHoldMs`) | `world_ui::State::puzzle` | [GamePointer.cpp](../../../src/plugin/GamePointer.cpp) | fake headset |
| SCR-37 | The cursor and the mouse messages are posted from a worker thread, not the Present callback. | `postThread = 1`; `moveMessages = 1`, `setCursor = 1`, `sendInput = 0` | fallback paths | [GamePointer.cpp](../../../src/plugin/GamePointer.cpp) | fake headset, 2026-11-03 |
| SCR-38 | The galaxy map opens in the CIC: the view stays Shepard's, the map's own 3D scene is a stereo window on the projector's hologram disc, its Flash layer and the travel question on a panel at the same place, clicked by the game pointer. Walking away closes it. | window 0.7 of the disc, depth 0.03, backdrop 0.8, a disc; UI frame 1.50 m; the opening sequence's 2.5 s delay set to 0.05 s | `[Galaxy] hologram = 1`, `inWorld = 1`, `window = 1`; opened at the console with A (`onFootHologram = 0` is the shipped default: our on-foot model with trigger picks is opt-in) | [GalaxyMap.cpp](../../../src/game/GalaxyMap.cpp), [GalaxyHologram.hpp](../../../src/core/vr/GalaxyHologram.hpp), [GalaxyHologram.cpp](../../../src/plugin/GalaxyHologram.cpp); HostServices v26 | fake headset; headset pending |
| SCR-39 | With `inWorld = 0` the hologram stands on a table in front of the player and the game's map is a screen behind it. | table 0.70 m ahead, 0.45 m below the eyes, radius 0.48 m; on-foot model radius 1.40 m, tilt 35 degrees | file-only | [GalaxyHologram.hpp](../../../src/core/vr/GalaxyHologram.hpp), [HandCalibration.cpp](../../../src/game/HandCalibration.cpp) | offline `mevr-galaxy-preview` |
| SCR-40 | In a first-person conversation the subtitle band goes on a quad under the speaker's face and the game's wheel on a quad in front. | subtitle 4 degrees tall, 0.22 m below the face; wheel 0.40 m wide, `wheelHand = front` | `[Cinematics] conversations = world` | [ConversationWorld.cpp](../../../src/game/ConversationWorld.cpp), [HalcyonOverlays.cpp](../../../src/plugin/HalcyonOverlays.cpp) | fake headset |

# Arbitration

| contested input | who claims it, in order | the loser |
|---|---|---|
| Both stick clicks | a tap (300 ms): the snapshot; a hold (400 ms): our menu | L3 is delayed 100 ms so the tap is never a crouch; R3 opens the power wheel only after 300 ms alone |
| Every input while our menu or the calibration is open | the menu | the game sees a neutral pad, the mod no controllers; held buttons stay latched off until released |
| A trigger while its ray is on a game screen, the rail, the UI layer or the galaxy window | the pointer (`OwnsTriggers`) | the game sees no trigger from that hand |
| Fingertip against ray on a screen | the fingertip within 0.06 m of the visible part | the rays |
| The right fingertip near the left wrist | a wrist panel's touch when the tip is within 0.15 m in front of a panel's face (0.03 m behind, 0.02 m margin: `GoingForPanel`) | no wrist tap |
| The left hand and its own wrist screen | nobody: the projecting hand neither touches nor points | the right hand works it |
| The mouse during the bypass puzzle | the d-pad | the pointer is held off for 1 s after play |
| The right stick with a screen pointed at | the wheel (scroll) | the game's own right-stick use |
| The left trigger on foot | the squad's weapon wheel (`OwnTrigger`), per pull | the game sees no LB; in the Mako LB stays the game's |
| A wrist tap with a weapon drawn | not allowed | nothing happens; the target is hidden |
| A screen opened by a use against the wrist list | the use: it stands by what was used | not on the wrist |
| The HUD's bottom widgets | the wrist and gun quads | masked out of the HUD panel while they show |

# Problems ME1 already solved

| problem | fix | where |
|---|---|---|
| The HUD's wheel showed doubled: non-"over" blend modes stayed in the eyes | the tint texture | [research/15](../../../research/15-hud.md) |
| The HUD frame is about 1306 stage pixels tall and its bottom widgets are anchored to the bottom | bottom-anchored crops, measured on 591 snapshots of four frame sizes | [research/41](../../../research/41-hud-pieces.md) |
| The weapon panel on the gun blocked the iron sights; on a pistol's top it blocked them too | it leaves a raised gun; the pistol's sits beside it | [research/41](../../../research/41-hud-pieces.md) |
| The game picked what A uses from the body's camera, not the head | the POV swapped for the one native call | [research/40](../../../research/40-interaction.md) |
| A fake Start press never reached the game | the game's own `ShowMenuGui` on the active mode | [research/28](../../../research/28-main-menu.md) |
| A `\` typed in another window opened the menu | key polls only with a game window in front | [research/28](../../../research/28-main-menu.md) |
| The paused desktop game with the headset asleep | the not-drawn-for-1-s close | [research/28](../../../research/28-main-menu.md) |
| Mode 7's `bFullScreen` made the game render its UI world instead of the level | cleared on our screens | [research/53](../../../research/53-world-screens.md) |
| A wrist screen straight above the wrist hid the wrist behind it | above across the line of sight | [research/53](../../../research/53-world-screens.md) |
| The equipment screen stood 0.34 m from the eyes, 77 degrees wide | at most 60 degrees: pushed out | [research/57](../../../research/57-omni-tool-menu.md) |
| The beam darkened what was behind it | premultiplied in linear light | [research/53](../../../research/53-world-screens.md) |
| Posted mouse messages never moved Flash's cursor; clicks landed where the game's cursor was | the GUI events and the answered `GetCursorPos` | [research/57](../../../research/57-omni-tool-menu.md) |
| A click posted from the Present callback was ignored by a message box | post from a worker thread | [research/51](../../../research/51-galaxy-map.md) |
| B to the focused handler never closed a screen | `GuiKey(13)` on the active mode | [research/57](../../../research/57-omni-tool-menu.md) |
| A reach for the Equip button passed 3.2 cm from the wrist and closed everything | `GoingForPanel` | [research/57](../../../research/57-omni-tool-menu.md) |
| Nothing showed where to tap; the arm is drawn up to 15 cm from the controller | the target on the drawn forearm, measured from the drawn fingertip | [research/57](../../../research/57-omni-tool-menu.md) |
| The rail's tabs were too small for a finger and needed perfect alignment | a 360 x 640 canvas (tabs about 3 x 14.5 cm) and click on contact | [research/57](../../../research/57-omni-tool-menu.md) |
| An angled poke slid along the surface and became a scroll | drag threshold 0.02 m, the poke rule | [research/42](../../../research/42-game-pointer.md) |
| Any mouse motion spun the bypass puzzle's marker | the d-pad and the pointer hold-off | [research/53](../../../research/53-world-screens.md) |
| Replays with controller poses moved the real cursor | `[Pointer] setCursor = 0` for such tests | [research/57](../../../research/57-omni-tool-menu.md) |
| The rail, the sights and a dozen quads went missing after one swapchain refusal | the swapchain budget | [gotcha](../gotchas/openxr-swapchain-limit.md) |
| A game-thread stall moved the reticle off the gun's line | the ring stays on the mod's last ray | [research/trilogy-seams](../../../research/trilogy-seams.md) |
| The galaxy map's first frame fell to the flat panel when a use played a call first | the console's opening window stays open through the call | [research/69](../../../research/69-conversations-everywhere.md) |
| `SetLocation` through ProcessEvent did nothing (the map's cross-hair) | `ue3::runtime::Call` clears the native index | [gotcha](../gotchas/process-event-skips-opcode-natives.md) |

# Code map

| part | files | scope |
|---|---|---|
| Placement and pointer rules | `src/core/vr/OverlayPlacement`, `WorldUi`, `OmniRail`, `OmniPad`, `OmniPower`, `GamePointer`, `GuiEvents`, `MenuPointer`, `HudPieces`, `InteractLabel`, `WheelCrop`, `GalaxyHologram`, `GalaxyMap`, `GameMenuRequest`, `MenuSettings`, `SquadMenu` | game-independent; `WorldUi.hpp` picks the GUI mode number by `MEVR_GAME_LE*` (7 and minigame 10 on ME1) |
| Main menu | `src/plugin/MainMenu`, `MainMenuSettings`, `src/plugin/menu`, `assets/menu/main.rml`, `main.rcss` | host; `MEVR_MAIN_MENU`; in the LE1 host and, per `CMakeLists.txt`, the sequel hosts (CLAUDE.md still says LE1 only) |
| HUD panel, reticle, HUD pieces | `src/plugin/HudPanel`, `HandReticle`, `HudPieceLayers` | in every host's list |
| Overlay composition, wheel panel, label, convo UI, galaxy UI panel | `src/plugin/HalcyonOverlays.cpp` | LE1 only; `setWheelPanel` is LE1-only (the ledger's G57) |
| World UI, rail, wrist beam, pointer | `src/plugin/WorldUi`, `OmniToolRail`, `RailArt`, `WristBeam`, `GamePointer` | LE1 host and the sequel menu sources |
| D-pad, UI-world figure | `src/plugin/OmniPad`, `UiWorldFigure` | LE1 host only |
| Galaxy | `src/plugin/GalaxyHologram`, `GalaxyHologramDraw` (`MEVR_GAME_LE1` branches); `src/game/GalaxyMap`, `SceneCaptureRig` | `game/GalaxyMap` resolves ME1's names; the sequels have their own flow files |
| Mod | `src/game/WorldUi`, `GuiCursor`, `GameMenuRequest`, `OmniTool`, `HudPieces`, `Interact`, `MenuPause`, `SquadWeapons`, `HandCalibration` (the `[Squad]`, `[WorldUi]`, `[Galaxy]`, `[Interact]` readers) | compiled for every game with per-game branches; `UiWorldFigure` and `GameArt` act under `MEVR_GAME_LE1` only |
| Engine | `src/engine/SelectionGaze` (0xC79D70 on ME1) | per-game addresses |
| Seams | `MevrBridge::OnFlatQuad`, `PlacePanel`, the `HudPanel*` draw seams in `d3d_capture.cpp`, `GalaxyViewSwapBegin/End` | `src/halcyon/le1` |
| HostServices | v12 wheel panel, v16 menu pause, v19 game menu request, v21 interact label, v22 HUD pieces, v26 galaxy, v27 world UI, v30 GUI events, v34 rail origin | size-checked structs: host and mod deploy together |

# Settings and defaults

| section | key | default | read by | menu row |
|---|---|---|---|---|
| `[Menu]` | `distanceM`, `widthM`, `pause`, `controlsShown`, `assets` | 1.30, 1.45, 1, 0, none | host | Comfort (pause) |
| `MELEVR.ini` | `hudPanel`, `hudPanelDistanceM`, `hudPanelWidthM`, `hudPanelDropM`, `hudPanelFollowDeg` | 1, 1.5, 1.3, 0.25, 70 | host | HUD & sights (distance, size); Advanced > HUD placement |
| `MELEVR.ini` | `handReticle`, `handReticleDistanceM`, `handReticleSizeDeg` | 1, 10, 1.5 | host | HUD & sights |
| `MELEVR.ini` | `menuDistanceM`, `worldLockedPanels`, `panelFollowDeg` | 1.54, 1, 70 | host | none |
| `[Squad]` | `wheelPanel`, `weaponWheelAt`, `powerWheelAt`, `wheelCrop`, `panelDistanceM`, `panelWidthM` | 1, `left`, `hud`, `auto`, 0.27, 0.40 | mod | classic menu's VR hands tab (style) |
| `[Hud]` | `wristStatus`, `gunStatus`, `wristWidthM`, `gunWidthM`, `wristOffset`, `gunBackM`, `gunUpM`, `pistolSideM`, `pistolUpM`, `wristCrop`, `gunCrop` | 1, 1, 0.08, 0.06, -0.03 0.06 0.05, 0.15, 0.04, -0.05, 0, measured, measured | host (quads) and mod (anchor) | HUD & sights |
| `[Interact]` | `gaze`, `label`, `combatLabel`, `labelHeightDeg`, `crop` | 1, 1, 0, 6, measured | mod | HUD & sights (label) |
| `[WorldUi]` | `on`, `widthM`, `distanceM`, `belowM`, `backdrop`, `screens` | 1, 1.20, 0.95, 0.50, 0.80, all | mod | Comfort > Game screens |
| `[WorldUi]` | `wrist`, `wristWidthM`, `wristMenuWidthM`, `wristAboveM`, `wristTowardM`, `wristFollow`, `wristBeam`, `omniTool`, `figure`, `figureOpacity` | the 12 tags, 0.30, 0.70, 0.10, 0, 1, 1, 1, 1, 0 | mod (and host for the menu's slider) | Omni-tool screens; Wrist screen size (0.4..1.0) |
| `[OmniTool]` | `enabled`, `tap`, `tapOnBody`, `railSide`, `aloneHeightM`, `pad`, `padSizeM` | 1, 1, 1, `right`, 0.32, 1, 0.17 | host | none |
| `[Pointer]` | `gameMenus`, `flatCine`, `moveMessages`, `setCursor`, `sendInput`, `wheel`, `wheelPerSecond`, `dotDeg`, `postThread`, `gameCursor` | 1, 0, 1, 1, 0, 1, 6, 0.6, 1, 1 | host | none |
| `[Pointer]` | `touch`, `touchTipM`, `touchHoverM`, `touchPressM`, `touchReleaseM` | 1, 0.04, 0.06, 0.005, 0.015 | host | none |
| `[Galaxy]` | `hologram`, `inWorld`, `window`, `windowOnHologram`, `windowScale`, `windowRound`, `windowDepth`, `windowBackdrop`, `windowWidthM`, `onFootHologram` | 1, 1, 1, 1, 0.7, 1, 0.03, 0.8, 2.0, 0 | mod | none |
| `[Galaxy]` | `worldRadiusM`, `worldTiltDeg`, `worldLiftM`, `tableDistanceM`, `tableBelowM`, `tableRadiusM` | 1.40, 35, 0, 0.70, 0.45, 0.48 | mod | none |
| `[Cinematics]` | `uiLayer`, `uiForwardM`, `subtitleHeightDeg`, `wheelHand`, `wheelWidthM`, `wheelDistanceM` | 1, 0.5, 4, `front`, 0.40, 0.12 | host; mod | none |

Migrations that touch placement: an old `[WorldUi] wrist = SkillGame` becomes the 12-tag list once (marker: the presence of
`wristMenuWidthM`); an old `wristMenuWidthM = 0.55` becomes 0.70 once (marker `figure`). The full list is in
[settings-and-defaults.md](settings-and-defaults.md).

# Checks

| kind | what |
|---|---|
| Logs | `main menu:` (`placed ... m ahead`, cost every 10 s, `request:`), `[MEVR-HUD]` (first hand reticle, wrist, gun), `[MEVR-WHEEL]`, `[MEVR-INTERACT]`, `interact:`, `hud pieces:`, `world ui:`, `omni-tool:`, `omni-tool rail:` (`a reach for the wrist target`, the slide's distance and time), `omni-tool pad:`, `game pointer:`, `galaxy hologram:`, `[MEVR-CONVO]`, `overlays:` |
| Snapshot | PNGs `-hud`, `-wheel`, `-menu`; layers `mainmenu`, `label`, `wrist`, `gun`, `beam`, `omnirail`, `omnipad`, `gamepointer`, `galaxy-left` / `-right`; JSON `"mainMenu"`, `"gamePointer"`, `"omniRail"` (with `"target"`), `"omniPad"`, `"galaxyHologram"`, `"mod"."worldUi"`, `"mod"."hudPieces"`, `"mod"."interact"` |
| Console | `mevr panel`, `mevr gui <Equipment|Squad|Codex|Journal|Map|Menu>`, `mevr omni <equipment|squad|map|journal|codex|computer|close|holster>`, `mevr guimouse`, `mevr guikey <event>`, `mevr skillgame start`, `mevr loot <n>`, `mevr reticles`, `mevr galaxy dump|where|pick|click|use` |
| Recordings | `tools/make-menu-recording.py` (ray clicks on the menu), `tools/make-wrist-recording.py` (the wrist tap and a rail press), `tools/make-galaxy-pick-recording.py` |
| Unit tests | `tests/Main.cpp` (`TestWorldUi`, overlay placement, game pointer, wheel crop, HUD pieces, interact label, omni rail, menu settings), `tests/OmniPadTests.cpp` |
| Offline previews | `mevr-menu-preview` (every section, `rail-*.png`, `pad-*.png`), `mevr-galaxy-preview`, `mevr-squad-preview` |

# Citations

- Research: [15 HUD](../../../research/15-hud.md), [28 main menu](../../../research/28-main-menu.md),
  [40 interaction](../../../research/40-interaction.md), [41 HUD pieces](../../../research/41-hud-pieces.md),
  [42 game pointer](../../../research/42-game-pointer.md), [51 galaxy map](../../../research/51-galaxy-map.md),
  [52 cinema screen](../../../research/52-cine-3d-screen.md), [53 world screens](../../../research/53-world-screens.md),
  [57 omni-tool menu](../../../research/57-omni-tool-menu.md), [59 conversations in the world](../../../research/59-conversations-in-world.md).
- Design: [squad weapons design](../../squad-weapons-design.md). Ledger: [trilogy parity ledger](../../trilogy-parity-ledger.md), rows R18-R20, G19-G28, G57.
- Catalog: [presentation-modes.md](presentation-modes.md), [settings-and-defaults.md](settings-and-defaults.md),
  [powers-and-squad.md](powers-and-squad.md), [body-and-ik.md](body-and-ik.md).
- Source: [WorldUi.hpp](../../../src/core/vr/WorldUi.hpp), [OmniRail.hpp](../../../src/core/vr/OmniRail.hpp),
  [OmniPad.hpp](../../../src/core/vr/OmniPad.hpp), [GamePointer.hpp](../../../src/core/vr/GamePointer.hpp),
  [HudPieces.hpp](../../../src/core/vr/HudPieces.hpp), [InteractLabel.hpp](../../../src/core/vr/InteractLabel.hpp),
  [OverlayPlacement.hpp](../../../src/core/vr/OverlayPlacement.hpp), [WheelCrop.hpp](../../../src/core/vr/WheelCrop.hpp),
  [SquadMenu.hpp](../../../src/core/vr/SquadMenu.hpp), [main.rml](../../../assets/menu/main.rml),
  [omnitool.rml](../../../assets/menu/omnitool.rml), [omnipad.rml](../../../assets/menu/omnipad.rml).
