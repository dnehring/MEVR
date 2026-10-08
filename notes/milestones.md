# Milestone log

## M0: plugin loads and hooks the engine (LE1). **Passed 2026-10-07**

- **Setup:**
  - Loader: ME3Tweaks Mod Manager's ASI loader (`bink2w64.dll` proxy 426,984 bytes plus
    `bink2w64_original.dll`).
  - Plugin `LE1MEVR.asi` 0.0.1 (commit `857079c`), deployed with `tools/deploy-asi.ps1`.
  - Game launched from Steam, LE1 vanilla apart from the loader.
- **Attach** (from `Win64/Logs/LE1MEVR.log`): about 13 s after process start. The version check
  passed, all globals resolved and the tick hook was installed within 43 ms:
  ```
  [20:57:39.265] MEVR 0.0.1 attaching to LE1
  [20:57:39.266] game version 2.0.0.48602 OK
  [20:57:39.266] resolved GMalloc / GObjObjects / SFXName pools / SFXName init / GEngine / GWorld
  [20:57:39.266] resolved UGameEngine::Tick at 0x7ff62a92a090   (image base + 0x3ca090)
  [20:57:39.308] hooked UGameEngine::Tick
  ```
- **Heartbeat:** every 5.00 s for 67+ beats (more than 5 minutes), with no errors.
  - GWorld changed from `0x2423f0cc060` (menu) to `0x24243331940` (in level), so we can detect
    level changes.
  - Object count: about 140k in the menu, about 166k in game.
- **Frame rate:** about **190 fps uncapped at the menu, 60 fps locked in game** (dips to 52 fps).
  VR needs 72/80/90 Hz, so the in-game cap (VSync or UE3 `bSmoothFrameRate`/`MaxSmoothedFrameRate`)
  must be lifted. **[verify]** which one.
- The loader writes its own log to `Win64/Logs/bink2w64_proxy.log`, not to `Win64/` as
  research/09 assumed.

## M1: plugin controls the game camera (LE1). **Passed 2026-10-07**

- **Dev loop:** a hot-reload host (`LE1MEVR.asi` 0.1.0) plus the mod DLL
  (`build/Release/LE1MEVR_mod.dll`, via `LE1MEVR.modpath`).
  - **First hot reload in a running game:** at 21:19:57 the host saw the new build and
    swapped it in.
  - Loaded generation 2. From build finished to mod live: under 10 s.
  - No restart needed.
- **First attempt failed safely:** at attach, `Engine.PlayerController.GetPlayerViewPoint` didn't
  exist yet, because the script packages load after the DRM wait.
  - Fix: retry the script-level setup every 1 s from the tick. The ProcessEvent hook is
    installed only after the handlers are registered.
- **Camera functions found:**
  - `Engine.Controller` / `PlayerController` / `AIController` / `SFXGame.BioAiController.GetPlayerViewPoint`
  - `Engine.Camera.UpdateCamera`, `Engine.Camera.GetCameraViewPoint`
  - `Engine.Actor` / `SVehicle.CalcCamera`
- **Call rates in game:**
  - `PlayerController.GetPlayerViewPoint`: about **10,000/s, roughly 43 per frame**. Many
    systems query the player view point, not just rendering.
  - `Camera.UpdateCamera`: **once per frame**.
  - The others: 0.
- **Test:** the backslash key toggles a ±30° yaw sine sweep (4 s period), added to
  `GetPlayerViewPoint`'s `out_Rotation` after the call (ProcessEvent post-handler, matched by
  cached `UFunction*`).
  - **The user confirmed the view sways left and right.**
  - Log: 12,282 and 43,067 adjusted views per 5 s window while on.
- **F9 note:** F9 is bound in game, so don't use F-keys for dev hotkeys. Backtick opens the UE
  console.
- **Frame rate this session:** about 230–240 fps in the heartbeat, versus the 60 fps lock seen
  in M0. Likely a different game state (menu or pause?) or the VSync setting. Still open (H14).

### Implications for VR

- `GetPlayerViewPoint` is shared by everything that asks "where is the player looking", so
  adding head rotation there probably also moves **aim**, AI perception, audio and so on. For VR
  the **render view** should come from `ULocalPlayer::CalcSceneView` (still needs locating in
  LE1), keeping gameplay aim on the controller.
- `Camera.UpdateCamera` (once per frame) is a clean per-frame point for writing the base
  camera pose (`CameraCache.POV`).

## M2: game mirrored onto a virtual screen in the headset. Next

1. D3D11 Present hook (REFramework's vtable approach, MIT), routed through our own hook manager.
2. OpenXR session on the game's D3D11 device (Oculus runtime), one quad layer showing the
   backbuffer copy.
3. In parallel: find LE1 `ULocalPlayer::CalcSceneView` (Ghidra and x64dbg; LE3 is at RVA
   `0x6cffd0` for comparison).
