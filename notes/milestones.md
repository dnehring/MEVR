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

## M2: game mirrored onto a virtual screen in the headset. In progress

### Attempt 1 (2026-10-07, 21:31–21:35)

- **Present hook:** host-owned `IDXGISwapChain::Present` hook via SPI, found through a
  throwaway device and installed at `0x7ffc16bd9000` (in dxgi.dll).
- **Blit failed first:** `CreateDeferredContext failed`. The game's D3D11 device is created with
  **flags `0x1` (`D3D11_CREATE_DEVICE_SINGLETHREADED`)**, which forbids deferred contexts.
  - Fix, hot-reloaded in game: draw on the immediate context inside our own
    `ID3DDeviceContextState` (`SwapDeviceContextState` in and back out).
- **After the hot reload:**
  - **Runtime and headset:** OpenXR runtime **Oculus 1.208.0**, system **Meta Quest Pro** (max
    16 layers). The session reached **FOCUSED** and the user saw the full-screen app view.
  - **Game device:** feature level 0xb000 (11_0).
  - **Backbuffer:** **2490×1401 `R16G16B16A16_FLOAT`**. That's FP16 scRGB (linear), so there's
    no sRGB decode. **[verify]** whether highlights above 1.0 clip, and whether HDR is on.
  - **Frame rate:** about 88–104 fps while the session ran, paced by the headset (Quest Pro
    90 Hz). In-game it was 240 fps uncapped before XR.
- **Bugs found:**
  1. **Input didn't reach the game.** The game window didn't have Windows focus (UE3 ignores
     keyboard, mouse and gamepad without it).
     - Fix: when the XR session becomes FOCUSED, bring the game window to the foreground
       (`AttachThreadInput` + `SetForegroundWindow`).
  2. **Quit from the Meta menu kept relaunching VR.** EXITING was treated like LOSS_PENDING and
     the session was recreated after 2 s, twice.
     - Fix: EXITING means the user quit. Tear XR down and stay off until `]` is pressed in game.
  3. **Crash on game exit.** The user closed the game from the desktop with the session live.
     Fail-fast `0xc0000409` in `RuntimeIPCServiceClient_64.dll` (Oculus) during process
     teardown.
     - Fix: the host hooks `ExitProcess` and shuts the mod (and XR) down before Windows unloads
       DLLs.

### Attempt 2 (2026-10-07, 21:41–21:44): virtual screen works

- **Game in the headset:** the user saw the game rendered in the headset.
- **`]` toggles:** VR went OFF and ON three times cleanly; each XR session closed in about 70 ms.
- **Quit from the Meta menu:** EXITING, then VR off, and it stayed off until `]`. No relaunch
  loop.
- **Not a bug:** head movement doesn't change the game view. That's expected for M2: the panel
  is fixed in LOCAL space, and head tracking comes in M3.
- **Earlier crash at 21:41:08:** the run that started 21:40:16 crashed 52 s in.
  - **Dump:** `%LOCALAPPDATA%\CrashDumps\MassEffect1.exe.32192.dmp`, triaged with
    `tools/minidump_scan.py`.
  - **What it shows:** access violation writing `0x24` in `ntdll+0x1a12d`. The faulting worker
    thread's whole stack is MassEffect1.exe plus `ws2_32`/`mswsock` (networking). No MEVR frames,
    and no Oculus DLLs loaded.
  - **Open question:** whether the user was closing the game at that moment. If so, our
    ExitProcess hook (which closes XR before the real exit) may widen an existing exit-time race
    in the game's network thread.
  - **Mitigation added:** the log now keeps the previous run as `LE1MEVR.prev.log` and records
    the pid, so the next incident keeps its evidence.

### Attempt 3: Steam Link

- The user switched to Steam Link, so SteamVR needs to be the active OpenXR runtime. The mod is
  runtime-agnostic. Fix: `]` off/on now always recreates the OpenXR instance, so a runtime
  switch is picked up without restarting.
- Worked under SteamVR. The user's next request was head tracking (M3).

## M3: head tracking drives the camera (mono). In progress

Design (2026-10-07):
- **Render thread (Present):** submit frame N, then immediately `xrWaitFrame` + `xrBeginFrame`
  for N+1 and locate the head (VIEW in LOCAL) at `predictedDisplayTime + predictedDisplayPeriod`.
  The extra period is because UE3 renders a frame behind the game thread. The pose is published
  through `core/vr/ViewSync`.
- **Game thread:**
  - At `Engine.Camera.UpdateCamera` (once per frame), read the latest pose and convert it to a
    UE3 rotator (`engine/XrToUnreal`).
  - Queue `{pose, CameraCache.POV.FOV}` for the render thread.
  - Every `PlayerController.GetPlayerViewPoint` that frame gets `yaw = game yaw + head yaw`,
    `pitch = head pitch`, `roll = head roll`.
- **Submission:** a **projection layer**. Both eyes show the same image (mono), posed at the
  queued head pose with the game's FOV (horizontal, symmetric; vertical from the aspect). The
  runtime's reprojection keeps it world-stable. Queue order (FIFO, cap 3) pairs images with
  poses, since UE3 ticks once per Present.
- **Modes:** `[` switches between head-tracked and screen (M2). `]` turns VR on and off.
- **Known limitations:**
  - Aim follows the head (weapons use GetPlayerViewPoint too).
  - Only the game's FOV is covered (black outside).
  - No positional tracking yet.
  - In vanilla third person the camera pivots in place behind Shepard.
- **[verify]:** whether ME's FOV is horizontal (UE3 default) or aspect-constrained. If the scale
  is wrong, the world will "swim" when you turn your head.

### Still to do

- Retest with the fixes. Check image correctness (colors and brightness vs the monitor).
- In parallel: find LE1 `ULocalPlayer::CalcSceneView` (Ghidra and x64dbg; LE3 is at RVA
  `0x6cffd0` for comparison).
