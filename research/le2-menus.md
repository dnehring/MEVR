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

Not yet (see "What is left").

## The checks

(filled in below as they run)

## What is left
