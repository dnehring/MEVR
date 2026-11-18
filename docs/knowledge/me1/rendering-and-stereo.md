---
type: System
title: Rendering and stereo (ME1)
description: What ME1's VR picture is, as numbered contracts: Halcyon's renderer inside our host, the one supported configuration, life-size stereo with each eye on its own frustum, one-Draw stereo and the render levers that ship on, the eye-image pipeline (colour, dither, tonemap, the HDR answer, FSR), the performance modes, the engine settings the mod forces, recenter, and the fixes for the culling camera, the camera collision and the near plane.
tags: [me1, rendering, stereo, performance, colour, upscale, catalog]
status: stable
timestamp: 2026-11-18T07:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T07:00:00Z }
game: LE1
---

# What the player gets

- The world at life size in first person: each eye is rendered from where that eye really is, at 100 game units per metre,
  through that eye's own off-centre frustum. There is no convergence dial and no other stereo mode.
- The head only looks. The hand aims; the stick does not pitch the view.
- One picture pipeline per eye: the game's 8-bit frame, upscaled with FSR 1 from 0.85 of the headset's eye size, a colour
  profile chosen for the headset (streamed Quests get "Quest", the rest neutral), a half-step dither.
- A Display page with one Performance row (Quality, Balanced, Performance) whose Balanced is exactly what a fresh install
  runs; each lever also has its own row under Advanced > Performance.
- No motion blur, no dynamic shadows, grass drawn to half its distance: the mod sets these itself after every level load,
  whatever the game's ini files say.
- R, or a double-click of the right stick, recenters (yaw only); a recenter the player asks for also removes a tilted or
  upside-down tracking space.
- The player's own body never drops out of view under the eyes, the view never falls to Shepard's knees when a wall is
  close, and a conversation's camera never clips the floor away.

# Contracts

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| REN-01 | LE1's VR renderer is Halcyon's, linked into the host ASI as a static library. It owns the OpenXR session, Present, the two stereo passes, first person and head look. It is not hot-reloaded: a change to `src/plugin` or `src/halcyon` needs the game closed. | two `FViewportClient::Draw` per present upstream; one with REN-07 | `MEVR_WITH_HALCYON_LE1`; the mod learns from `HostServices::controllers` that the host owns VR and skips its own VR output, scene view hook, XInput hook and hotkeys | [Halcyon.cpp](../../../src/plugin/Halcyon.cpp), [xr_session.cpp](../../../src/halcyon/le1/xr_session.cpp), [render_hook.cpp](../../../src/halcyon/le1/render_hook.cpp) | headset, 2026-10-08 on |
| REN-02 | One supported configuration. `ModernBaseline` pins 19 values after every `MELEVR.ini` load, profile switch and menu Reset, whatever the file says: Stereo 2 on; AER, DIBR and old side-by-side off; `sfr2Metric` on; `sfr2MetricEyeFrustum` on; `sfr2MetricDepth` 1; `questFovMatch`, `vrFovFillEnabled`, `leanEnabled` on; `lookSensitivity` 1; `menuFlat`, `galaxyFlat` on; `fpEnabled`, `fpHideHead` on; `combatHeadAim`, `makoHeadAim` off; `decoupledPitch` on; the depth-pop key off. | 19 pins; log `halcyon bridge: the MEVR baseline (launch / profile switch / reset)` | a pin that changed the VR mode rewrites the game's render size for the next launch (`ApplyResolutionForCurrentMode`); the classic menu can still change them live for that session only | [HalcyonBridge.cpp](../../../src/plugin/HalcyonBridge.cpp) `ModernBaseline`; called from [vr_config.cpp](../../../src/halcyon/le1/vr_config.cpp) | fake headset, 2026-11-09 |
| REN-03 | Life-size stereo: each Stereo 2 pass renders from its eye's real place (`xrLocateViews`), at the view's units per metre. No convergence shift at submit. The swap-eyes setting is ignored: pass 0 is the left eye. | 100 uu per metre (Halcyon's 50 x lean gain 2; `kLifeSizeUnitsPerMeter` 100); `sfr2MetricDepth` 1.0; upstream's `sfr2HalfEyeUU` 3.4 and `sfr2Convergence` 0.012 are not used | `sfr2Metric` (pinned on); when the render does not turn with the head (menu, head look off) the eye offsets are applied along the game camera's axes (`viewFollowsHead`) | [MetricStereo.hpp](../../../src/core/vr/MetricStereo.hpp) `BodyOffsets`, [MetricStereo.cpp](../../../src/plugin/MetricStereo.cpp) | unit test; `tools/stereo-check.py` |
| REN-04 | Mode 2: in gameplay with the UI on the HUD panel, the GPU projection of each pass is that eye's own off-centre frustum and the submit declares it. The CPU matrices (culling, Umbra, HUD markers) keep Halcyon's symmetric window, which holds both eyes' frusta. | the symmetric window: the widest half angle of both eyes each way, times the fill (`UnionWindow`); half angles capped at 1.45 rad | mode 2 only where the UI is off the eye images; mode 1 (the symmetric window for the GPU too) where the UI is in the eye images: a VR cine with the HUD panel off | [MetricStereo.hpp](../../../src/core/vr/MetricStereo.hpp) `Projection`, `WindowHolds`; `ApplyMetricFrustumSEH` in [render_hook.cpp](../../../src/halcyon/le1/render_hook.cpp) | unit test; fake headset |
| REN-05 | Known limit of mode 2: what the CPU projects through the symmetric window (the sun's sprite and its glare) lands off its place, in one eye only near the edges. Not fixed on ME1. | research/37, 2026-10-25 18:22 | mode 2 | [research/37](../../../research/37-single-pass-stereo.md) | headset |
| REN-06 | One-Draw stereo is the shipped path (`sharePass = 5`): one `FViewportClient::Draw` a frame, the first eye's render recorded and replayed in its own finish hook with the second view synthesised in the host. Each eye is on its own frustum (`oneDrawWindow = 0`). | 9.1 ms against 11.4 ms with two engine renders at Eden Prime's heaviest cell (20%); `replayInverse` 1 | stands down (`sharePass` read as 0) unless `sharePassVerified` and every row it needs exist; a scene render into another target (a SceneCapture2D) runs untouched with index -1 | [RenderLevers.cpp](../../../src/plugin/RenderLevers.cpp), [DrawReplay.cpp](../../../src/plugin/DrawReplay.cpp), [RenderLeversTable.hpp](../../../src/plugin/RenderLeversTable.hpp) | fake headset; headset 2026-11-07 |
| REN-07 | One-Draw health: while no record of pass 1's view matches the rendered one, Halcyon's second Draw resumes, and comes off again when records match. | 3 frames without a match: resume; 30 matching frames: back to one Draw; log `one Draw: ... Halcyon's second Draw resumes` | a cine camera edited after pass 0 recorded it (the intro after a load) | [RenderLevers.cpp](../../../src/plugin/RenderLevers.cpp) | fake headset |
| REN-08 | The replayed eye gets its own screen-space matrices: every pixel shader's reflection is read for `ScreenToShadowMatrix` and `ScreenToWorld`, and the replay rewrites them with `C x M1` before such a draw, then puts the first eye's bytes back. | `replayScreenMatrices` 1 | sharePass 2, 3, 5 | [ScreenMatrix.hpp](../../../src/core/vr/ScreenMatrix.hpp), [DrawReplay.cpp](../../../src/plugin/DrawReplay.cpp), seam `NotePixelShaderCreated` | unit test and a WARP replay test; headset 2026-11-11 |
| REN-09 | A dynamic vertex or index ring that wraps (a DISCARD map) inside the recorded tail is copied to a shadow buffer before the discard, and the replay binds the shadow for the draws before the wrap. An unshadowed one blocks the frame's replay. | rings 4 MB; 4-5% of quiet frames wrap, more in combat | replay paths | [DrawReplay.cpp](../../../src/plugin/DrawReplay.cpp) | headset |
| REN-10 | While the second eye is a replay, character light-environment shadows are off (the right eye drew them where the left eye sees them). The host says whether it is (`secondEyeIsReplay`, HostServices v43). | `scale set LightEnvironmentShadows False`; an older host: the file's `sharePass` 2/3/5 on LE1 | not a replay: the player's own `GamerSettings.ini` value is put back | [SystemSettings.cpp](../../../src/game/SystemSettings.cpp) `Wanted` | fake headset, 2026-11-13 |
| REN-11 | Umbra's occlusion queries are drawn with the eye's matrix, so objects do not pop with head motion under mode 2. | `umbraEyeMatrix` 1: view +0x1D0's world-space equivalent put into +0x260 around `IssueOcclusionQueries` | `umbraQueriesVerified` | [RenderLevers.cpp](../../../src/plugin/RenderLevers.cpp) | headset, 2026-10-24 |
| REN-12 | D3D11 bound shader states the engine rebuilds every draw are kept alive by one extra reference once their key is built a second time. | `keepBoundShaderStates` 1; 15.8-16.5 to 10.5-11.1 ms at the heaviest spot in a fresh session, about 1 ms warm; the kept set plateaus near 5,000 | `boundShaderStateVerified` | [RenderLevers.cpp](../../../src/plugin/RenderLevers.cpp) | fake headset, 2026-10-24 |
| REN-13 | Umbra's visible instances whose bounding sphere projects below a radius are not marked visible. | `minScreenRadiusPx` 8, in pixels of a 2160-high eye; 0.3 ms at the heavy spot; 32 px 0.8 ms with pop-in; the menu's chips 0, 4, 8, 16, 24, 32 | `umbraInstanceVerified`; an old `minScreenRadiusTenths` key is read first and emptied by a mode | [RenderLevers.cpp](../../../src/plugin/RenderLevers.cpp), [PerformanceModes.hpp](../../../src/core/vr/PerformanceModes.hpp) | fake headset |
| REN-14 | Levers that ship at the game's own behaviour, and must stay there on ME1 (ME1's measurements; a sequel sets its own by measurement): `umbraQueryWait` 1, `ue3QueryWait` 1, `perEyeOcclusion` 0, `perEyeViewState` 0, `umbraSharedVisibility` 0 (never set it), `umbraShareResults` 0, `pCoresOnly` 0, `eyeScissor` 0. | `umbraSecondEyeClearBits` 2 | read live from `[Performance]` | [RenderLevers.cpp](../../../src/plugin/RenderLevers.cpp) lines reading each key; [RenderLeversTable.hpp](../../../src/plugin/RenderLeversTable.hpp) static_asserts | fake headset (research/35) |
| REN-15 | Frame pacing: the late frame wait for a streamed Meta headset, bounded recovery when it sticks at half rate after a load. | `earlyFrameWait = auto`, `pacingRecovery = auto`, `pacingRecoveryTries = 3` per level (0..16), `frameLoop = halcyon` | recovery only on streamed Meta headsets; `frameLoop = full` overrides `earlyFrameWait` | [QuestPacing.cpp](../../../src/plugin/QuestPacing.cpp), [QuestPacing.hpp](../../../src/core/vr/QuestPacing.hpp) | headset (Quest), 2026-11-07 |
| REN-16 | The render target is 0.85 of the headset's own eye size (the runtime's recommended rect), rounded to even. With Setup's Automatic the host sets it once at the first VR start, for the launches after it. | chips `headset` 1.0, `headset85` 0.85, `headset75` 0.75; marker `[Display] eyeSizeSet = 1`; a fresh `MELEVR.ini` holds 4096 x 4250 until then | a restart applies a new size; the row's note says when one waits | [MenuSettings.hpp](../../../src/core/vr/MenuSettings.hpp) `FirstStartSize`, `HeadsetSize`; [MainMenu.cpp](../../../src/plugin/MainMenu.cpp) | fake headset |
| REN-17 | With the target below the eye size, each 8-bit eye is scaled up with FSR 1 (EASU into an intermediate, RCAS into the eye, the colour correction at the end) instead of Halcyon's bilinear fill. | `[Upscale] method = fsr` (the host's default when the key is missing; the core struct's default is Off), `sharpness = 0.9` (RCAS stops = (1 - sharpness) x 2) | acts only while the source is smaller than the eye; a same-size eye takes the plain blit | [Upscale.cpp](../../../src/plugin/Upscale.cpp), [Upscale.hpp](../../../src/core/vr/Upscale.hpp); seam `EyeUpscale` in `BlitTextureToEyeWithFill` | offline (ctest `mevr-upscale-shader`); headset |
| REN-18 | Colour per headset, on sRGB-encoded values in this order: black level, brightness times per-channel gain, mid-tones, contrast, saturation; then clamp. Auto means "quest" for a streamed Quest and "neutral" otherwise. Neutral is detected and skips the correction: Halcyon's own bytecode and raw copy. | Quest: contrast 1.06, saturation 1.12, gamma 0.90; Vivid: contrast 1.12, saturation 1.25, gamma 0.85 (their other fields: `Preset` in the core); ranges brightness and contrast 0.5..1.5, saturation 0..2, gamma 0.5..2, black -0.2..0.2 | applied to the world eye images and the game's UI (HUD and wheel panels); never to our own quads or Halcyon's menu | [Colour.hpp](../../../src/core/vr/Colour.hpp), [Colour.cpp](../../../src/core/vr/Colour.cpp) `Preset`, `Detect`; [Colour.cpp](../../../src/plugin/Colour.cpp) | unit test; ctest `mevr-colour-shader` |
| REN-19 | A triangular dither is added before the 8-bit store. | `[Colour] dither = 0.5` 8-bit steps (0..2; 0 off) | with the correction | [Colour.hpp](../../../src/core/vr/Colour.hpp) `Dither` | unit test |
| REN-20 | Holding A on the Display page shows the uncorrected picture while held. | the hold lapses 150 ms after the last report (`kHoldLapseMs`); `[Colour] compare = 1` does the same from the file | our menu open on Display | [Colour.cpp](../../../src/plugin/Colour.cpp) `HoldCompare` | fake headset |
| REN-21 | The game is told its display is SDR, so its backbuffer stays 8-bit with Windows HDR on. | `[Colour] hdrDisplay = -1` (default since 2026-11-14); `0` the display's own answer; `1` HDR10 for an SDR display; an old file's 0 moves to -1 once (`hdrDisplayMoved = 1`) | the hook on `IDXGIOutput6::GetDesc1` (output vtable slot 27) | [HdrDisplayPolicy.cpp](../../../src/plugin/HdrDisplayPolicy.cpp) `Answer` | fake headset; players' reports |
| REN-22 | A float (HDR) source gets a tone curve in place of Halcyon's per-pixel clip; an 8-bit source never does. | `tonemap = clip` by default (`aces`, `hable`, `reinhard`); `exposure` 0 stops (-3..3); `white` 4 (1..16); `toneHue` 1; `toneDesat` 0.15; `toneSat` 0.7 | only with the game in its HDR range and a float backbuffer | [Colour.hpp](../../../src/core/vr/Colour.hpp) `Hdr`, `Tone`; `MEVR_TONE` in [xr_session.cpp](../../../src/halcyon/le1/xr_session.cpp) | offline; fake headset (research/27 7d: the game's SDR pass keeps more gradation) |
| REN-23 | Display > Performance sets a bundle. Quality: headset size, FSR, cull 4 px, grass 0.75, dynamic shadows off, ambient occlusion on. Balanced (the shipped defaults): 0.85, FSR, 8 px, 0.5, off, on. Performance: 0.75, FSR, 24 px, 0.35, off, ambient occlusion off. A value changed by hand shows Custom with what differs from the nearest mode. | a mode writes every bundled key; live within a second except the render size (restart) | motion blur and FSR sharpness are no mode's | [PerformanceModes.cpp](../../../src/core/vr/PerformanceModes.cpp), [MainMenuSettings.cpp](../../../src/plugin/MainMenuSettings.cpp) | unit test (`tests/PerformanceModesTests.cpp`); offline `mevr-menu-preview` |
| REN-24 | The mod applies the VR engine settings itself through the local player's console: `scale set DynamicShadows False`, `MotionBlur False`, `FoliageDrawRadiusMultiplier 0.5`, and REN-10's key. Each boolean is sent through its opposite first (the same value alone changes nothing). | applied once a player exists, again after every level change, and at 3 s and 15 s after a world change unless the native motion-blur guard is healthy; a reapply of every boolean stalls the host about 153 ms | `[SystemSettings]` in `LE1MEVR.ini` overrides any key; `apply = 0` leaves the engine's values | [SystemSettings.cpp](../../../src/game/SystemSettings.cpp) | fake headset, 2026-10-28 |
| REN-25 | A native guard keeps motion blur off between the mod's applies (the blur's velocity is the other eye's view: each surface smears its own way). | `MotionBlurProxy` vtable swap, 5 pairs on LE1 (the parity ledger's R10) | installed at attach | [MotionBlurGuard.cpp](../../../src/plugin/MotionBlurGuard.cpp) | fake headset (research/62) |
| REN-26 | Setup and `tools/vr-game-settings.ps1` write the game's own ini values: a borderless window, VSync off, motion blur and film grain off, HDR to SDR, 16x anisotropic filtering, dynamic shadows off, frame smoothing off, and the stereo target mirrored into `MELEVR.ini`. | PS VR2's eye 2824 x 2880; Halcyon's Performance ladder step 3072 x 3188 | `-Restore` and Uninstall put the player's values back | [vr-game-settings.ps1](../../../tools/vr-game-settings.ps1), [Ini.cpp](../../../src/install/Ini.cpp) | fake game (`tools/setup/test-fake-game.ps1`) |
| REN-27 | Recenter is yaw only, as upstream. A recenter the player asks for takes the head's full orientation instead when the base space is tilted: rolled past 90 degrees, or the head's up more than a threshold from the base's up for a while. | `recenterTiltDeg` 60 (0 or less: never); 1.5 s of tilt (the watch shrinks three times as fast as it grows); Halcyon's own first recenter is never corrected | R with the game window in front, or a double-click of the right stick (upstream's); our menu, the HUD panel, wheel panel and world panels are placed afresh after a recenter | [Recenter.hpp](../../../src/core/vr/Recenter.hpp) `Decide`, [RecenterTilt.cpp](../../../src/plugin/RecenterTilt.cpp), seam in `DoRecenter` | unit test; headset |
| REN-28 | The player's body is not culled under the eye. The engine culls with the game's camera, which sits ahead of the drawn eye by the GPU-only view offset; the body's and its pose followers' boxes are grown by that offset and Umbra is handed the padded box. | pad = the view offset's length + 15 units, at most 150 units; 0.43 m with the shipped "view back" framing; sphere radius grown by pad x 1.732 | first person; the post-hook on `USkeletalMeshComponent::UpdateBounds` (0x58EEC0), then `UpdateUmbraBounds` (0x25CFE0) | [BodyBounds.cpp](../../../src/engine/BodyBounds.cpp), pad computed in [ArmIk.cpp](../../../src/game/ArmIk.cpp) | fake headset, 2026-11-10; headset culling check pending |
| REN-29 | ArmIk sets `bConsiderAllBodiesForBounds` on the driven body and its pose followers, journalled, rechecked for new pawns and meshes, restored at unload. | log `body bounds:` once per pawn and mesh | independent of the floor lock | [ArmIk.cpp](../../../src/game/ArmIk.cpp) | [verify] in the headset |
| REN-30 | The game's camera collision is off in first person: it pulled the camera back to the Camera bone when anything stood just ahead of the face, and the eye was drawn behind the knees. | `[Game] cameraCollision = 0` clears `SFXCameraMode.bCollisionEnabled` on the current mode; 1 keeps the game's | head-tracked view on foot only; put back otherwise; the game module's LE1 names sit under `MEVR_GAME_LE1` | [CameraCollision.cpp](../../../src/game/CameraCollision.cpp) | fake headset, 2026-11-11 |
| REN-31 | The near plane on foot is the game's. A conversation's director writes `LocalPlayer.NearClipPlane` per shot; while a first-person view is held the on-foot plane is written back each camera update. | on foot 10 units; the director's 0.3 to 2.8 m per shot; `[Cinematics] worldNearPlane = 1` | a world view held in a conversation or moment | [ConversationWorld.cpp](../../../src/game/ConversationWorld.cpp) (`Camera.UpdateCamera` post-handler) | fake headset, 2026-11-10 |
| REN-32 | The eye centre on foot is at the player's real eye height over the pawn's floor (the floor lock); both eyes share the correction before the metric eye offsets. The contracts are in [body-and-ik.md](body-and-ik.md). | `[Body] floorLock = 1` | ordinary on-foot first person only | [FloorLock.cpp](../../../src/plugin/FloorLock.cpp), [FloorLock.hpp](../../../src/core/vr/FloorLock.hpp) | fake headset |
| REN-33 | Every swapchain of ours is made through the budget. A refusal frees ours not shown for a second at the frame's end and the owner asks again; no owner gives up for the session. | SteamVR refuses past 16 a session whatever their size; Halcyon holds 4; retry next frame, else every 2 s; `[Debug] swapchainCap = N` tests it | all quads | [Swapchains.cpp](../../../src/plugin/Swapchains.cpp), [SwapchainBudget.hpp](../../../src/core/vr/SwapchainBudget.hpp) | unit test; fake headset 2026-11-06 |
| REN-34 | All D3D and OpenXR work happens on the render thread inside the host's Present callback; the game's device is SINGLETHREADED. Script calls Halcyon makes from there are queued for the game thread. | queue: `MevrBridge::QueueProcessEvent`; an off-thread call is logged `process event: off the game thread:` | always | [GameThreadCalls.cpp](../../../src/plugin/GameThreadCalls.cpp), [PresentHook.cpp](../../../src/plugin/PresentHook.cpp) | headset (research/36) |
| REN-35 | Halcyon's key polls (menu key, R, K, P, profile keys) count only with a window of the game in front. | `menuKey` 220 (`\`) in our deploy; upstream Insert (0x2D) | seam `MevrBridge::KeyDown` | [HalcyonBridge.cpp](../../../src/plugin/HalcyonBridge.cpp) | fake headset |
| REN-36 | Quitting from the headset's menu (session EXITING) turns VR off; it does not recreate the session. Nothing waits in the ExitProcess hook. | the process ends with `TerminateProcess` | always | [KeptLocks.cpp](../../../src/plugin/KeptLocks.cpp), [Entry.cpp](../../../src/plugin/Entry.cpp) | headset |

# Arbitration

| contested | who wins | the loser |
|---|---|---|
| The GPU's projection in a frame | mode 2 (the eye's own frustum) in gameplay with the UI on the HUD panel | mode 1 wherever the UI is in the eye images |
| The second eye's render | the replay (sharePass 5) | Halcyon's second Draw, which resumes only while the replay's health check fails (REN-07) |
| `MELEVR.ini` against the baseline | `ModernBaseline` | the file's value is ignored and logged by name |
| `BIOEngine.ini [SystemSettings]` against `GamerSettings.ini` | BIOEngine's wins in the engine, and the game rewrites it; the mod's `scale set` wins over both | a player's own value is honoured only through `[SystemSettings]` in `LE1MEVR.ini` |
| A scene capture inside an eye's render | the capture keeps its own target and takes no eye index | never recorded for the replay |
| An eye of the headset's size with a neutral profile | Halcyon's raw copy | the blit shader runs only when a correction, a tone curve or the upscale is on |

# Problems ME1 already solved

| problem | fix | where |
|---|---|---|
| Upstream's Stereo 2 with swap-eyes was divergent (1.27 degrees per eye; a pistol at 0.5 m read at 0.68 m) | metric stereo: real eye places, no shift | [research/30](../../../research/30-metric-stereo.md) |
| Whole meshes dropped out frame by frame on the symmetric window under one-Draw | each eye on its own frustum (`oneDrawWindow = 0`) | [research/37](../../../research/37-single-pass-stereo.md) |
| The right eye strobed: the replay drew from a ring buffer that had wrapped | shadow copies before the discard | [gotcha](../gotchas/dynamic-ring-wraps-inside-the-recorded-tail.md) |
| A vtable or single-body hook missed draws, clears and copies | hear the draws through seams in Halcyon's hooks, hook the other entries inline per variant | [gotcha](../gotchas/d3d11-per-context-vtable.md) |
| The right eye's shadows slid with the head | the replay's screen-space matrices (REN-08) | [research/37](../../../research/37-single-pass-stereo.md) |
| Character shadows drawn where the left eye sees them | `LightEnvironmentShadows False` while the second eye is a replay | [research/37](../../../research/37-single-pass-stereo.md) |
| Motion blur smeared each eye its own way, five times under five names | the mod's `scale set` through the opposite, after each level; the native guard | [gotcha](../gotchas/motion-blur-in-stereo.md), [research/62](../../../research/62-motion-blur-and-menu-pacing.md) |
| GamerSettings' shadows and blur values were overridden, and a loaded level rendered with the old ones | apply from the mod, per level, through the opposite | [gotcha](../gotchas/system-settings-precedence.md) |
| Objects popped with head motion in mode 2 | `umbraEyeMatrix` | [research/35](../../../research/35-performance-attempt-log.md) |
| A per-eye occlusion history tripled the meshes every tenth frame | `perEyeOcclusion` stays 0 | [research/35](../../../research/35-performance-attempt-log.md) |
| "Umbra's resolve once a frame" failed in three forms and deadlocks the game thread | `umbraSharedVisibility` stays 0; sharePass 3 keeps its own resolve | [research/35](../../../research/35-performance-attempt-log.md) |
| A fault inside a render-thread hook hung the game with no crash | read the views inside the render, wrap render-thread work in SEH | [gotcha](../gotchas/render-thread-fault-hangs-the-game.md) |
| A blue or black headset image with Windows HDR on; players could not launch | answer the game SDR by default | [gotcha](../gotchas/windows-hdr-desktop.md) |
| ACES per channel turned oranges red and oversaturated the float frame | the curve hue-preserving, `toneSat` 0.7, a path to white | [research/27](../../../research/27-colour.md) |
| The GPU was over budget rendering 18% more pixels than displayed | the headset's own eye size, then 0.85 with FSR | [research/22](../../../research/22-performance-levers.md), [research/32](../../../research/32-ue3-vr-performance.md) |
| A Quest stuck at half rate after every load | the late frame wait and bounded pacing recovery | [research/22](../../../research/22-performance-levers.md) |
| The player's body culled while in view | pad the boxes and hand Umbra the padded box | [gotcha](../gotchas/culling-camera-ahead-of-the-eye.md) |
| The view fell to Shepard's knees near a wall | camera collision off in first person | [gotcha](../gotchas/camera-collision-in-first-person.md) |
| A conversation camera's near plane removed the floor and the body | write the on-foot plane back while a view is held | [gotcha](../gotchas/conversation-camera-near-plane.md) |
| A dozen quads went missing after one swapchain refusal | the swapchain budget | [gotcha](../gotchas/openxr-swapchain-limit.md) |
| SteamVR started with the head rolled 180 degrees | the tilt-correcting recenter | [Recenter.hpp](../../../src/core/vr/Recenter.hpp) |
| A script call from the Present thread crashed the game | queue Halcyon's ProcessEvents for the game thread | [gotcha](../gotchas/process-event-off-the-game-thread.md) |
| Every quit crashed in a lock the game's teardown deleted | nothing waits in the ExitProcess hook; static locks kept | [gotcha](../gotchas/quit-time-fault-in-a-deleted-lock.md) |

# Code map

| part | files | scope |
|---|---|---|
| Stereo math | `src/core/vr/MetricStereo`, `ScreenMatrix`, `StereoPoseTag` | game-independent |
| Eye-image math | `src/core/vr/Colour`, `Upscale`, `PerformanceModes`, `MenuSettings` | game-independent |
| Recenter, swapchain rules, pacing rules | `src/core/vr/Recenter`, `SwapchainBudget`, `QuestPacing` | game-independent |
| Halcyon's LE1 renderer and its `[MEVR]` seams | `src/halcyon/le1` (`mevr_bridge.h`), `src/halcyon/MevrSeams.h` | LE1 only (`MEVR_WITH_HALCYON_LE1`); the sequels have their own trees |
| The bridge, the baseline | `src/plugin/HalcyonBridge.cpp`, `Halcyon.cpp`, `RendererLe1.cpp` | LE1 only |
| Metric stereo's plan, colour, upscale, HDR answer, recenter tilt, swapchains, the motion-blur guard | `src/plugin/MetricStereo`, `Colour`, `Upscale`, `HdrDisplayPolicy`, `RecenterTilt`, `Swapchains`, `MotionBlurGuard` | in every host's source list in `CMakeLists.txt`; LE1 reaches colour and upscale through its own blit seams, the sequels through `plugin/EyeCopy` |
| Render levers and the replay | `src/plugin/RenderLevers.cpp`, `RenderLeversTable.hpp`, `DrawReplay.cpp` | compiled for every game; every lever group is verified on LE1 only, and `sharePass`, `minScreenRadiusPx`, `perEyeOcclusion` and the capture hooks stand down on the sequels (the table's flags) |
| Pacing | `src/plugin/QuestPacing.cpp` | LE1 host |
| Engine settings | `src/game/SystemSettings.cpp` | every game; one `MEVR_GAME_LE2` branch (the 3 s wait); `engine::adapter::kVrDynamicShadows` per game |
| Body bounds | `src/engine/BodyBounds.cpp`, the pad in `src/game/ArmIk.cpp` | compiled for every game; the two addresses are LE1 rows (the ledger's R29: missing on the sequels) |
| Camera collision | `src/game/CameraCollision.cpp` | compiled for every game; acts under `MEVR_GAME_LE1` (the ledger's G12: n/a on ME2) |
| HostServices | v43 `secondEyeIsReplay`; v34 the floor-locked centre | host and mod deploy together |

# Settings and defaults

| section | key | default | read by | menu row |
|---|---|---|---|---|
| `MELEVR.ini` | the 19 baseline keys (REN-02) | pinned | host (Halcyon) | none |
| `MELEVR.ini` | `sfr2ResX`, `sfr2ResY` | 4096 x 4250 in a fresh file, then 0.85 of the eye | host | Display > Render resolution |
| `MELEVR.ini` | `recenterTiltDeg` | 60 | host | none |
| `MELEVR.ini` | `menuKey` | 220 in our deploy | host | none |
| `[Display]` | `eyeSizeSet` | written 1 once | host | none |
| `[Upscale]` | `method`, `sharpness` | `fsr`, 0.9 | host | Display > Upscale; Advanced > Performance |
| `[Colour]`, `[Colour.<headset>]` | `profile` | `auto` | host | Display |
| `[Colour]` | `dither`, `compare` | 0.5, 0 | host | hold A |
| `[Colour]` | `hdrDisplay` | -1 | host | none |
| `[Colour]` | `tonemap`, `exposure`, `white`, `toneHue`, `toneDesat`, `toneSat` | `clip`, 0, 4, 1, 0.15, 0.7 | host | none |
| `[Performance]` | `sharePass` | 5 | host | none |
| `[Performance]` | `oneDrawWindow`, `eyeScissor`, `replayInverse`, `replayScreenMatrices` | 0, 0, 1, 1 | host | none |
| `[Performance]` | `umbraEyeMatrix`, `keepBoundShaderStates`, `minScreenRadiusPx` | 1, 1, 8 | host | Advanced > Performance (the cull) |
| `[Performance]` | `umbraQueryWait`, `ue3QueryWait`, `perEyeOcclusion`, `perEyeViewState`, `umbraSharedVisibility`, `pCoresOnly`, `umbraSecondEyeClearBits` | 1, 1, 0, 0, 0, 0, 2 | host | none |
| `[Performance]` | `earlyFrameWait`, `pacingRecovery`, `pacingRecoveryTries`, `frameLoop` | `auto`, `auto`, 3, `halcyon` | host | none |
| `[SystemSettings]` | `DynamicShadows`, `MotionBlur`, `FoliageDrawRadiusMultiplier`, `AmbientOcclusion`, `apply` | False, False, 0.5, the game's (on), 1 | mod | Display > Performance; Advanced > Performance |
| `[Game]` | `cameraCollision` | 0 | mod | none |
| `[Cinematics]` | `worldNearPlane` | 1 | mod | none |
| `[Body]` | `floorLock` | 1 | host and mod | none |
| `[Debug]` | `swapchainCap` | none | host | none |

The whole table of shipped defaults is in [settings-and-defaults.md](settings-and-defaults.md).

# Checks

| kind | what |
|---|---|
| Logs | `halcyon bridge: the MEVR baseline`, `[MEVR-STEREO]`, `render levers:` (renders per frame, the replay's counts, `its health:`, `screen-space matrices:`, `large buffers discarded inside the tail:`), `one Draw:`, `upscale:`, `[MEVR-COLOUR]` / `colour:`, `hdr display:`, `system settings:`, `[MEVR-RECENTER]`, `body bounds:`, `camera collision:`, `swapchains:`, `first VR start:`, `main menu: performance mode` |
| Snapshot keys | `"stereo"`, `"colour"`, `"upscale"` (the probe's JSON), `"swapchains"`, `"floorLock"` |
| Tools | `tools/stereo-check.py` (far-scenery disparity), `tools/eye-diff.ps1` (one frame's two eyes call by call), `tools/perf-probe.ps1`, `tools/perf-ab.ps1`, `tools/measure-levers.ps1`, `tools/snapshot.ps1` |
| Console | `tools/console.ps1 "scale set <key> <value>"`, `"show <category>"`, `"mevr convo body"` (the near plane and hide flags) |
| Unit tests | `tests/Main.cpp` (metric stereo, colour, upscale constants, recenter, swapchain budget), `tests/PerformanceModesTests.cpp`, ctest `mevr-colour-shader`, `mevr-upscale-shader`, `mevr-rva-table`, the draw replay's GPU test |
| Offline previews | `mevr-upscale-preview` (`--check`, `--bench`), `tools/colour-preview`, `mevr-menu-preview` (`section-display-perf-*.png`, `advanced-perf.png`) |

# Citations

- Research: [22 performance levers](../../../research/22-performance-levers.md), [27 colour](../../../research/27-colour.md),
  [30 metric stereo](../../../research/30-metric-stereo.md), [32 UE3 VR performance](../../../research/32-ue3-vr-performance.md),
  [35 performance attempt log](../../../research/35-performance-attempt-log.md), [36 crash breadcrumbs](../../../research/36-crash-breadcrumbs.md),
  [37 single-pass stereo](../../../research/37-single-pass-stereo.md), [58 body scale and floor lock](../../../research/58-body-scale.md),
  [60 performance modes](../../../research/60-performance-modes.md), [62 motion blur](../../../research/62-motion-blur-and-menu-pacing.md),
  [69 conversations everywhere](../../../research/69-conversations-everywhere.md), [sequel render levers](../../../research/sequel-render-levers.md).
- Ledger: [trilogy parity ledger](../../trilogy-parity-ledger.md), rows R01-R35.
- Gotchas: [index](../gotchas/index.md), "Rendering and stereo".
- Source: [MetricStereo.hpp](../../../src/core/vr/MetricStereo.hpp), [Colour.hpp](../../../src/core/vr/Colour.hpp),
  [Upscale.hpp](../../../src/core/vr/Upscale.hpp), [PerformanceModes.hpp](../../../src/core/vr/PerformanceModes.hpp),
  [Recenter.hpp](../../../src/core/vr/Recenter.hpp), [RenderLeversTable.hpp](../../../src/plugin/RenderLeversTable.hpp),
  [HalcyonBridge.cpp](../../../src/plugin/HalcyonBridge.cpp), [SystemSettings.cpp](../../../src/game/SystemSettings.cpp),
  [vr_config.h](../../../src/halcyon/le1/vr_config.h).
