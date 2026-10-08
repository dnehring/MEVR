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

## M1: plugin controls the game camera. Next

Plan:
1. Hook ProcessEvent and filter by cached `UFunction*` for `Engine.Camera.UpdateCamera`. The
   ConsoleExtension pattern, research/09 §4.
2. Override `CameraCache.POV` with a visible test motion (a slow yaw sweep on a hotkey).
3. In parallel, find LE1 `ULocalPlayer::CalcSceneView` (Ghidra and x64dbg; LE3 is at RVA
   `0x6cffd0` for comparison) for per-eye projection later.
