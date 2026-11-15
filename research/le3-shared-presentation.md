<!-- SPDX-License-Identifier: MIT -->
# ME3 shared presentation adapter (L3-4)

This increment reuses the existing presentation modules. It is not a completed native wrist-screen or 3D cinema port.

## What is implemented

- `src/halcyon/SequelPresentation.h` contains the common LE2/LE3 frame contract. LE3 publishes actual submitted projection images/FOVs and freshly located head/eye poses. There is still one OpenXR session and one `xrEndFrame` owner.
- `plugin/MenusLe2` is the existing shared menu/calibration/rail/pointer composition, with its legacy name retained to avoid duplicating or renaming callers. Its one raw GUI-mode check now uses `GameAdapter`. Session stop/replacement clears visibility, held input, screen reservations and the stick chord.
- `HalcyonLe3Bridge` feeds that module the live sample and the replay sample separately, publishes calibrated/controller-owner-filtered state, and always runs its close latch when the shared menu owns input even if no motion controllers are published. The normal menu key opens the shared menu; Ctrl/Shift preserves the original developer menu. The native window procedure respects shared-menu input ownership.
- Native flat menus and cinematics use the existing `WorldPanels` placement in the room. The full uncropped native GUI quad has a defined UV-to-window map and is passed to the shared game pointer. This is the native 2D image, not invented stereoscopic cinema. Conversation and galaxy panels retain their native pad behavior.
- Pointer ownership is renewed by each submitted GUI panel. A continuing target keeps its click/drag; a vanished panel releases the target before the next pointer step. The rail forgets both cached swapchains and its pointer state on session replacement.
- The single menu document distinguishes LE3. It does not advertise ME2's power gesture guide, voice/Thespian, first-person dialogue or 3D cinema as working in LE3. Unsupported setting writes are refused in the backend as well as hidden/disabled in the document. Flat size, smoothing, mirror, colour and calibration still use real shared/renderer settings.

## Coordinator wiring

No CMake, ModLoader, Entry or GameAdapter edits are included in this lane.

1. Always include `plugin/MenusLe2.cpp`, `plugin/WorldPanels.cpp` and `core/vr/OverlayPlacement.cpp` in the LE3 Halcyon host (MenusLe2 is a no-op without `MEVR_WITH_MAIN_MENU`). Both renderer bridge headers include the common frame header relatively, so the Halcyon archives need no new include directory.
2. For `MEVR_MAIN_MENU`, use the existing LE2 shared menu source block and `mevr-menu` target: MainMenu, MainMenuSettings, Calibration/CalibrationDraw, ArmSample, OmniToolRail/RailArt, WorldUi, GamePointer/WristBeam, their existing core menu/pointer/calibration/rail/HUD/gfx helpers, and SetupRequest/Json needed by Entry. MenusLe2's native-panel helpers reference HudPanel even though LE3 does not enable native HUD redirect yet, so link that shared module or split its optional helper implementation. MainMenu and its backend currently also reference the existing Voice API; link the shared implementation without publishing/initializing unsupported LE3 voice gameplay services. Do not clone these modules.
3. Populate working shared menu/calibration/arm-sample/controller-offset/rail-state queue services. Keep native `worldUi` redirect ownership, screen reticle hide, labels, spoken reply dispatch and other unsupported native callbacks unavailable. In particular **do not enable `takeGuiEvents` before a verified LE3 game-thread GuiKey consumer exists**: polling that service tells GamePointer to stop its window-message fallback.
4. Extend Entry's `GamePointer::InstallCursorHook` condition for LE3 when the shared menu is linked. It is the existing hook, not another mouse driver.
5. Extend TestLoop's module diagnostics for the actually linked LE3 menu/calibration/rail/pointer modules. Leave 3D cinema and Thespian diagnostics unavailable until those paths exist.

## Remaining native adapters and evidence gates

- Native screen identifiers/open-close dispatch, crop rectangles, HUD draw redirect/order and wrist projection must be verified for each LE3 screen. No LE2 crop or `WorldUiOwnsGui` override was enabled here. The rail's requested native actions require that game-side adapter before being accepted as functional buttons.
- True 3D cinema needs the existing `CineScreen::PlanFrame` separation applied to LE3's authored camera stereo passes, actual separate eye images, and a verified UI capture/crop path. Then reuse `CineScreen`, `CinemaBackdrop` and `TransitionFade`; do not claim native mono duplication has depth. This increment deliberately keeps the native flat cinematic path.
- Spoken replies require a verified LE3 conversation/text/slot adapter. No native reply selection, voice suppression or first-person scene camera is added here. Authored seated conversations still prohibit traversal.
- Palette choice remains the existing shared blue rail for LE3; this is provisional and has not been sampled/approved against LE3 native menus. ME2 keeps its amber palette.
- Build both development and player LE3 configurations, plus LE1/LE2 regression builds. Run existing menu/pointer/rail tests. Offline focused menu capability and VoiceMenuTests compiled and passed in this lane; full host compilation belongs to coordinator integration.
- Native acceptance: open the shared menu by key and stick chord on world/flat frames, calibrate/recenter, verify manual pose correction, aim/touch the rail, click/drag/scroll the full native GUI panel, then close with held triggers and confirm no leaked click. Test focus loss, native menu transitions, stop/restart and load. Observe native dialog interrupts and seated position ownership. Quest 3/Virtual Desktop review must check readable scale, projection origin and lack of overlapping owners.

No game, SteamVR, deployment or system runtime setting was touched for this increment.
