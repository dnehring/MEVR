<!-- SPDX-License-Identifier: MIT -->
# ME1's menus on ME2 (M4, lane D)

Date: 2026-11-14, lane D of [the continuation plan](../docs/le2-continuation-plan.md) (M4: our main menu, the guided
calibration, the omni-tool). The rule (the owner, 2026-11-14): "The Omni tool to open up the menu, that should be exactly the
same, though I recognize that the model for the Omni tool will be different in Mass Effect 2. Same with our custom launcher
menu for when we're in the game, same for the calibration." So: the same modules, documents, styles, fonts, haptics and
defaults as ME1; each deviation below names the ME2 fact that forces it. Everything is ME2's exe 2.0.0.48602.

**How ME2's script was read** (game code: keep the output in a scratch folder, never in git):
`local-tools\le2-inspection-root\conversation-scan.exe "<ME2>\BioGame\CookedPCConsole" <out.txt> --game LE2 --script
<Class.Function>` (the same tool with `--game LE1` and ME1's CookedPCConsole for ME1's). Engine names against ME2:
`python tools/le2-name-audit.py game/<Module>` (research/le2-vrik-audit.md).

## M4.1: our main menu on ME2

### What runs where

The menu is the same code as ME1's: `plugin/MainMenu` (the quad, the pointer, the pad, the backdrop, the first run),
`plugin/MainMenuSettings` (what each row drives), `plugin/menu/*` (RmlUi through the `mevr-menu` library) and the document
`assets/menu/main.rml` + `main.rcss`. CMake builds them into the LE2 host with `MEVR_MAIN_MENU` (on by default) and defines
`MEVR_WITH_MAIN_MENU` there too (`mevr_add_plugin`, the LE2 block: "Our main menu and the guided calibration on ME2").

ME2's glue is one file, `plugin/MenusLe2.cpp` (LE1's is spread over `plugin/HalcyonBridge.cpp` and `plugin/HalcyonOverlays.cpp`;
ME2's bridge, `plugin/HalcyonLe2Bridge.cpp`, is lane B's, so it only calls in). Step by step, what a frame does on ME2:

1. `ME2VR::MevrBridge::OnSessionCreated` (bridge) -> `menus_le2::OnSessionCreated`: the runtime's and headset's names
   (`xrGetInstanceProperties`, `xrGetSystemProperties`) for the top bar and `calibration::SetSystem`.
2. `OnFrameBegun` (bridge) keeps `live` (the player's own sample) before `testloop::OnFrameBegun` may put a replay's in its
   place, then publishes what `menus_le2::OnFrameBegun` returns: the interaction profile every 90 frames
   (`calibration::SetProfile`), `calibration::NoteControllers`, `main_menu::NoteControllers`, `calibration::NoteFocused`;
   the controllers hidden while the calibration runs or our menu is open (`calibration::Hidden`), else with the
   calibration's manual offsets (`manual_calibration::Apply`), as LE1. The both-sticks hold (`gamepad::UpdateStickChord` on
   `live`, never a replay's) toggles our menu, closes Halcyon's, or cancels the calibration; a tap takes a snapshot.
3. `OverlayLayers` (bridge, after the vignette) -> `menus_le2::Layers`: the head located in the app space at the frame's time
   (`xrLocateSpace(VIEW, appSpace)`: ME2's flat panel submits no eye views, so the projection can't give it), the eyes from
   the projection layer when there is one; then the calibration's quads (alone while it runs) and our menu's (alone while
   open), the same composition as LE1's `HalcyonOverlays`.
4. `MergeMotionControllers` (bridge, Halcyon's XInput hook): `menus_le2::PadWanted()` keeps the hook going without motion
   controllers; `FilterPad` makes the pad neutral while our menu or the calibration has it (a real pad's state goes to
   `main_menu::NotePad` for navigation) and, after, keeps what was held at the close from the game until let go
   (`gamepad::CloseLatch`), as LE1.
5. `OnSwapChainPresent` (bridge, every Present of the captured swap chain) -> `main_menu::OnPresent`: the watchdog that
   closes a menu not drawn for a second, and the request file (below).
6. `OnRecentered` -> `main_menu::Recentered`, `calibration::Recentered`.

ME2's seams (`src/halcyon/le2`, marked `[MEVR]`):
- `me2_menu.cpp` `PollInsert`: the menu key goes to `MevrBridge::OnMenuKey(ownMenuOpen)` first (`menus_le2`): without Ctrl or
  Shift and with Halcyon's menu closed it toggles **our** menu; otherwise Halcyon's toggles as upstream (Ctrl or Shift + `\`
  opens its own), exactly LE1's `MevrBridge::OnMenuKey`.
- `ApplyMevrBaseline`: `MenuKey` Insert (upstream's default) becomes `\` (VK 220), ME1's key (LE1's deploy writes
  `menuKey = 220`); a key the player bound stays. Log `[MEVR-BASELINE] ... the menu key \ (MenuKey; upstream Insert)`.
- `MenuWndProc`: also swallows the desktop's mouse and keyboard while our menu is open (`MevrBridge::MainMenuOpen`), as
  LE1's Halcyon does.
- `ME2VR::Menu::SetOpen(bool)` (what its key does: load its values, save them on closing, recenter), `SaveSettings()`
  (`SaveValues`, MELE2VR.ini) and `GetMenuKey()`, for `plugin/RendererLe2.cpp`.

The renderer interface (`plugin/Renderer.hpp`) gained: `SetOwnMenuOpen` on ME2 (was a no-op), `OwnMenuKey()`,
`RequestRecenter()` (LE1 `MELEVR::Menu::RequestRecenter`, ME2 `Me2Xr::Recenter`), and `MenuRows` / `GetMenuRows` /
`SetMenuRows` / `SaveOwnSettings` for ME2's renderer rows (LE1 answers none: its rows edit Halcyon's `VrConfig` directly).

The host's services on ME2 (`plugin/ModLoader.cpp` `WithMenus`, LE2 only): v15 `calibration`, v16 `mainMenu`, v19
`takeGameMenuRequest`, the same functions as LE1's `WithLe1Modules` and `WithGameMenuRequest`.

### The rows on ME2

`MainMenuSettings.cpp` is one file for both games: `MEVR_MENU_LE1` (the LE1 host) compiles the parts that drive LE1-only
modules (Halcyon's `VrConfig`, its profiles and render size, `plugin/Colour`, `plugin/Voice`, `plugin/OpenAlSoft`,
`plugin/PerfProbe`, MEVR Setup's requests, the camera alignment of Halcyon's VR hands tab); ME2's host compiles the rest
plus ME2's renderer rows. The menu sets `le2` (a schema flag, `core/vr/MenuSettings`) on ME2, and the document greys out
(`data-class-disabled="le2"`, the existing `.disabled` style) what ME2 doesn't have yet, with "Not on Mass Effect 2 yet." on
each greyed group and one line at the top of the controller guide. On LE1 `le2` is 0 and nothing changes: the 91 PNGs of
`mevr-menu-preview` are pixel-identical with the old document (A/B with the same binary); `mevr-menu-preview <out> --le2`
also writes `le2-section-*.png` and `le2-advanced-*.png`, the pages as ME2 shows them.

| Row (key) | On ME2 | Why |
|---|---|---|
| Game menu (top bar) | ME2's pause menu: `SFXGameModeBase.ShowMenu` on the mode that has the input | ME2's Start (`ShowMenuGui` doesn't exist: ME2's pause menu is the browser wheel, `MassEffectGuiManager.ShowBrowserWheel`) |
| Holster (top bar) | `SFXGameModeDefault.TryHolster` | ME2's only ever holsters (`SetActionState(0)`, refused in combat, mid-reload, while a power runs): no "drawn?" check |
| Resume, Recenter, Snapshot, Record | as ME1 | Recenter: `Me2Xr::Recenter` |
| Pause while open (`pause`) | `[Menu] pause`, game/MenuPause on ME2 (below) | |
| Camera shake, Running sway | `[Comfort] screenShake`, `viewShake` (lane C's game/CameraShake on ME2) | "Low" is off on ME2 (its shakes are camera modifiers: on or off) |
| Fingers, Recoil buzz, Aim assist, Gun kicks | `[Hands]` (lane C's modules on ME2) | |
| Body & calibration: Calibrate, Play position, Measure, manual calibration, hand-model angles | as ME1 (M4.2) | Measure writes `[Arms] measure*` through `plugin/ArmSample` (ME2 has no VR hands tab) |
| Advanced > Tracking: Smoothing | ME2 `[VR] HeadLookSmoothing` (MELE2VR.ini) | |
| Advanced > Tracking: Flat screen width | ME2 `[VR] MenuScreenSize` | |
| Advanced > Graphics: Desktop mirror | ME2 `[VR] MirrorPresentEvery` (0 = every 8th) | |
| Comfort: Turning group, Walk toward, Physical crouch | greyed | game/Comfort and game/Posture are ME1's only (M6) |
| Conversations and cutscenes | greyed (flat) | ME2 shows them on Halcyon's flat cine panel until ME1's 3D screen is ported (M5; `ApplyMevrBaseline`) |
| Game screens (in the room, omni-tool screens, galaxy hologram) | greyed | M4.3 / M6 |
| Controls: Weapons (holsters), Left trigger, grenades and melee, Vent, Steady view, Draw speed, Mako | greyed | holsters wait for lane B3's host side; ME2 has no grenades nor heat (research/le2-vrik-audit.md); the rest ME1's modules (M6) |
| Voice, Sound pages | greyed | plugin/Voice, plugin/OpenAlSoft and their mod modules are ME1's (ME2 plays through Wwise) |
| Display: Colour, Image quality, Optional content | greyed | plugin/Colour, the render size, FSR and the levers are LE1's renderer's; MEVR Setup for ME2 is M7.2 |
| HUD & sights | greyed | lane B3's HUD panel, reticle, HUD pieces and sights |
| Advanced: Recenter key | greyed | R is ME2's reload: `ApplyMevrBaseline` turns the key off (the row says so) |
| Advanced: Depth of field | greyed | ME2's `[VR] DisableDof` drives nothing since upstream hid it (its GamerSettings write took the tonemapping off: me2_menu.cpp `[DOFWASH]`) |
| Advanced: Performance, Game UI, Profiles; Diagnostics' Report, Measure frame timing, Reset | greyed | LE1's renderer, MEVR Setup (M7.2) |

### The pause on ME2

ME2 has no `Engine.WorldInfo.PauseGame`. ME1's is one line, `bPlayersOnly = bPause` (LE1's script), and ME2's own weapon
wheel does the same (`SFXGameModeWeaponWheel.Activated`: `Outer.WorldInfo.bPlayersOnly = TRUE`; `Deactivated`: `FALSE`;
`MassEffectGuiManager.ShowBrowserWheel` keeps the value it found and sets it). So `game/MenuPause` on ME2 writes
`Engine.WorldInfo:bPlayersOnly` through the reflection layer (`ue3::kEveryGame`), journalled (`MenuPause.bPlayersOnly`), and
puts back the value it found. Log `menu pause: the game paused (...; WorldInfo.bPlayersOnly on, as ME2's weapon wheel)`.

### Opening the menu: the both-sticks hold first

The owner, 2026-11-14: "Keep them greyed until I can see them" (the rows above stay greyed) and "We'll maintain the same key,
but always prioritize the L3 + R3 stick click". So `\` stays ME2's menu key (as ME1's), and holding both stick clicks is the
primary way and always wins:
- **The chord** is ME1's, game-independent in `game/GamepadMapping` (host side, both hosts): `UpdateStickChord` on the
  player's own sample (`live`, never a replay's) in `menus_le2::OnFrameBegun` (LE1: `HalcyonBridge`'s `OnFrameBegun`): both
  held `kStickHoldMs` (400 ms) toggles our menu (or closes Halcyon's, or cancels the calibration); a quick tap of both
  (`kStickJoinMs` 100 ms to join, `kStickTapMs` 300 ms to let go) is a snapshot.
- **Neither click reaches the game while it might be the chord.** L3: `FromControllers` always gates it (`GateClick`,
  `Latches::leftClick`): held back `kStickJoinMs`, never sent once the other click is down, a quick click alone sent as a
  `kRbTapMs` press after it. R3: the same gate with `Options::gateRightClick` (`Latches::rightClick`, 2026-11-14), **set
  by ME2's pad path only**: on LE1 R3 still reaches the game at once (Halcyon's double-click recenter reads it, and ME1's R3,
  the scope's zoom step, does nothing in Halcyon's view), so ME1 is unchanged. The mod's uses of R3 (`RbFromStickClick`,
  `PowerHoldFromStickClick`: the power wheel only after 300 ms alone) already refuse a press with the other click down.
- **What ME2's pad path must call** (lane B3's `ME2VR::MevrBridge::MergeMotionControllers` in `plugin/HalcyonLe2Bridge.cpp`,
  when ME1's merge comes over): `game::gamepad::FromControllers(controllers, latches, options)` with `options` from the mod's
  flags as LE1's (`FromFlags`) **and `options.gateRightClick = true`**, on the `controllers::Latest()` sample (its `timeMs`
  is the gate's clock), then `menus_le2::FilterPad` as today. Until then ME2's bridge calls `FromControllers(controllers,
  gLatches)` (default options): L3 is gated, R3 is not.
- **The controller guide** names the hold first and the key second ("press and hold both thumbsticks, or press `\` on the
  keyboard"): `menu_key`, the key's name on the layout (`KeyName`: LE1 `menuKey`, ME2 `renderer::OwnMenuKey()`), in both
  games' guide.
- Checks: `TestStickChordFirst` (mevr-tests): R3 first or L3 first, held 0.5 s: one hold, neither click seen by the game; a
  quick tap of both: one snapshot, neither click seen; R3 alone: seen 100 ms after the press, a tap as a short press after it;
  R3 held alone past the join window then L3: R3 lets go, L3 never seen, the hold still fires. Live controllers in the
  headset: notes/headset-checklist.md section 5.

### Opening the menu by script

`%LOCALAPPDATA%\MEVR\mainmenu.request` (development builds, or `[Debug] requests = 1`; read once a second on the Present
thread, then deleted): `open [section]`, `close` or `toggle`. Log `main menu: request file: '<line>'`. A replay's stick
clicks never open the menu (the stick chord counts the player's own), so scripts use this; the menu's rows are then worked
with `tools/make-menu-recording.py` and `tools/replay.ps1 -Inputs` as on ME1.

## M4.2: the guided calibration on ME2

`plugin/Calibration` and `plugin/CalibrationDraw` build into the LE2 host unchanged but for two host functions that moved to
game-independent ones: the haptics and the floor through `plugin/XrHost` (LE1's `halcyon::RequestHaptic` and `Floor` only
forwarded there) and the T pose's arm sample through `plugin/ArmSample` on ME2 (LE1 keeps `MevrBridge::TakeArmSample`, which
also updates its VR hands tab). The mod's `game/BodyCalibration` is game-independent (it reads `[Calibration.<key>]` from
`LE2MEVR.ini` once the host's v15 state names the key) and lane C's arm IK and weapon frame already ask it.

## M4.3: the omni-tool

### The entries open ME2's screens (built, checked)

ME2 has no `Show<Screen>Gui` (`ShowSquadGui`, `ShowJournalGui` ... are ME1's). Its squad, journal, codex and map screens are
pages of its pause menu, the browser wheel ("Mission Computer"). So `game/GameMenuRequest` on ME2 (`StepPage`, a tick at a
time):
1. Not in the GUI mode (7): the pause menu, `SFXGameModeBase.ShowMenu` on the mode that has the input (on foot or a vehicle),
   as the Game menu button does; then wait.
2. In mode 7: the browser wheel's handler: `MassEffectGuiManager.m_oBWHandler`, empty on the PC, so the manager's native
   `GetBrowserHandler()` (the handler of its `MenuBrowser` panel). The GUI manager is `PlayerController.m_oCachedScaleFormManager`.
3. Its `lstPages` (config): `0: Save (type 1), 1: AreaMap (type 0), 2: CharacterRecord (type 2), 3: Journal (type 4),
   4: Codex (type 5), 5: None (type 7), 6: Options (type 6), 7: None (type 8), 8: Load (type 3)` (logged once:
   `omni-tool: ME2's pause menu pages: ...`). The page by Type (`MEBrowserWheelSubPages`: Map 0, CharacterRecord 2 = the
   squad, Journal 4) or, the codex, by its tag.
4. `BioSFHandler_BrowserWheel.ShowSelectedPanel(nPanel, oManager)`: what the wheel's own buttons call (its `HandleEvent`
   case 1). Log `omni-tool: opened the squad screen (ME2's pause menu page 2 through
   BioSFHandler_BrowserWheel.ShowSelectedPanel)`.

Equipment stands down on ME2 (no equipment screen outside its lockers: logged). `mevr omni <squad|map|journal|codex|computer|
close|holster>` is on ME2's console now. Checked on the fake headset (stretch `20261114-223915-lane-d-squad`): `mevr omni
squad` opened ME2's pause menu and then its squad page (the snapshot's flat panel: VR Shepard, Level 2 Soldier, the powers,
Miranda and Jacob). `reflect: 291 of 291`. Not yet: `CloseScreens` on ME2 (ME1's B through `GuiKey`; ME2's `GuiKey` event
numbers are [verify]).

### What the rest of M4.3 needs (not built)

In this order; each is LE1's module brought into the LE2 host as M4.1 brought the menu (CMake's LE2 block, `menus_le2`'s
calls), no ME2 copy:
1. **The rail's host** (`plugin/OmniToolRail`, `assets/menu/omnitool.rml`): its LE1 ties are `halcyon::RequestHaptic` (to
   `xr_host::RequestHaptic`, as MainMenu and Calibration did), `plugin/RailArt` (+ `core/gfx`: ME1's art from the player's
   game cache, `%LOCALAPPDATA%\MEVR\gameart`, written by the mod's `game/GameArt`; without it the rail draws its own look)
   and `plugin/WorldUi`'s `Shown` (the screen the rail stands beside). Its quad goes into `menus_le2::Layers` before our
   menu's, as in LE1's `HalcyonOverlays` (`omni_rail::Layer`, `TargetLayer`); `main_menu::ModFlags` then reports
   `kOmniRailOpen` on LE2 too (now 0 there).
2. **The tap** needs Shepard's drawn arm bones, which the mod sends in `hud_pieces::Pieces` (HostServices v22): lane B3's
   HUD pieces on LE2. Until then `omni_rail::Tap` can use the controllers (`[OmniTool] tapOnBody = 0`, LE1's fallback).
3. **ME2's screens on the wrist** (`plugin/WorldUi` + `game/WorldUi`): ME2's GUI panels are its own (`MenuBrowser`,
   `CharacterRecord`, `Journal`, `Codex`, `AreaMap` above; `BioSFHandler_Loot`, `_Shop`, `BioSkillGame_Handler` don't exist on
   ME2: `tools/le2-name-audit.py game/WorldUi`), and its renderer needs `[MEVR]` seams like LE1's `WorldUiActive` /
   `WorldUiOwnership` (xr_session.cpp) in me2_xr.cpp's flat-panel decision.
4. **ME2's omni-tool on Shepard's arm** (`game/OmniTool`): ME1's gauntlet is `BIOG_V_T_Omni_Tool_Z`'s crust effect; ME2's
   omni-tool effect names are to be found in its packages (the deviation the owner named: "the model for the Omni tool will be
   different in Mass Effect 2"). `tools/le2-name-audit.py game/OmniTool`: only `Engine.StaticMesh:LODInfo` is missing.

## The checks

**ME2 on the fake headset, 2026-11-14 22:28** (stretch `%LOCALAPPDATA%\MEVR\sessions\20261114-222813-lane-d-menu`; `prep.ps1
-Holder me2-lane-d`, `launch.ps1`, `load.ps1`: Freedom's Progress, Save 4; scripts in a scratch folder, recipe below):
- The baseline: `[MEVR-BASELINE] 7 setting(s) in MELE2VR.ini set to the MEVR baseline: ..., the menu key \ (MenuKey;
  upstream Insert)`. The mod: `reflect: 282 of 282 checks agree` (this lane's rows included).
- `open controls` in `mainmenu.request`: `main menu: opened (the request file)`, `menu: RmlUi 6.3 ready, 1440x900, 4 font
  faces`, `swapchains: main menu 1440x900 made`, `main menu: placed 1.30 m ahead`, the backdrop from the left eye
  (`4096x2304, format 27`); `menu pause: the game paused (the main menu is open; WorldInfo.bPlayersOnly on, as ME2's weapon
  wheel)`. The snapshot's `-mainmenu.png` is ME1's board over a frosted Freedom's Progress; the JSON's `mainMenu` block
  `open: true`. Cost: 0.5-1.2 ms CPU, 0.01-0.05 ms GPU per frame while open.
- A row saved: a replay (`make-menu-recording.py "point 1318 792; click; hold 3"`, `replay.ps1 -Inputs`) clicked Controls >
  Fingers follow yours: `main menu: [Hands] fingers = 0`, and `ASI\LE2MEVR.ini` has `fingers=0` (it had none).
- Game menu (`point 808 47; click`): `main menu: closed (Game menu)`, `menu pause: the game runs again (...
  WorldInfo.bPlayersOnly off ...)`, `main menu: opened the game's pause menu (SFXGameModeDefault.ShowMenu (declared on
  SFXGameModeBase) returned 1; game mode 0)`; the snapshot's flat panel shows ME2's Mission Computer.
- Resume (`point 1296 47; click`) closed it. `restore.ps1`: `verification: 0 differences`.
- Found: the first request line kept PowerShell's CR LF (`section "controls\r\n"`: an empty page): the request file now takes
  its first line only.
- Not by script: the `\` key itself (a scripted key press reaches the game only with its window in front, which a script must
  not force; `OnMenuKey` is LE1's logic) and the both-sticks hold (counts only live controllers).
- Seen in that log, not ours: a first-chance fault in Halcyon ME2's `PcHud::ReadFString` (pchud.cpp:253, under its own SEH)
  at the session's start.

**LE1 unchanged** (2026-11-14 23:07, `tools/fake-headset-session.ps1 -Tag laned-pose -BuildDir build-mono`, lane B's
same-pose script plus our menu opened by the request file; session `2026-11-14_23-06-45-laned-pose`), against lane B3's
"before" run of 23:03 (`2026-11-14_23-03-25-laneb3-before`): the same six layers at the same places (the label 0.2 mm, the
omni-tool target 0.9 mm apart), `hostView` identical (-43.3 / -4.1 / -18.1 uu, 100 uu/m), the HUD panel 0.11% of pixels apart
by more than 32/255. The eyes differ (15% of pixels): B3's build predates main's fc24c20d, which answers LE1's HDR question SDR
by default (its log: `[Colour] hdrDisplay = 0`, the float frame; ours: `answered SDR: the game keeps its 8-bit frame`), so the
two frames are different pipelines, not a change of ours. Then `open comfort` (with PowerShell's CR LF) opened our menu on
LE1 on the Comfort page (`mainMenu.open: true, section: comfort`). The menu's preview: the 91 PNGs pixel-identical (above).
ctest: 53 of 53 in `build` and `build-trilogy`.

**How to repeat it:** `tools\le2-halcyon-run\prep.ps1 -Holder <you> -BuildDir <worktree>\build-trilogy`, `launch.ps1`,
`load.ps1`; `Set-Content $env:LOCALAPPDATA\MEVR\mainmenu.request 'open controls' -NoNewline`; `python
tools\make-menu-recording.py rec.mevrrec "point 1318 792; click; hold 3"` and `tools\replay.ps1 -Path rec.mevrrec -Inputs`
(document points: `mevr-menu-preview <out> --le2` draws ME2's pages, 1440x900); `tools\snapshot.ps1`; then `launch.ps1
-Stop`, `restore.ps1`.

## What is left

- M4.3's rail, tap, wrist screens and ME2's omni-tool model (above, "What the rest of M4.3 needs").
- M4.2's own check: a guided calibration run through on ME2 and its `[Calibration.<key>]` read back by `game/BodyCalibration`
  (built and wired; the fake headset has no interaction profile, so no controller key: `calibration: ... no controller type
  known yet`; a replay of the calibration's poses, or the headset, is next).
- The headset: the menu's legibility and place on ME2, the `\` key and the both-sticks hold (notes/headset-checklist.md).
