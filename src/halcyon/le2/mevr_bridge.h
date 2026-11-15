// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// MEVR (ME VR Attempt) addition, GPL-3.0: the seams where our host plugs into Halcyon's ME2 renderer, as
// src/halcyon/le1/mevr_bridge.h does for LE1. Declared without the OpenXR SDK headers, because xr_types.h defines the
// same names; the implementation is in src/plugin/HalcyonLe2Bridge.cpp. Handles are passed as void*.

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

struct ID3D11DeviceContext;

namespace ME2VR::MevrBridge
{
// The statically linked OpenXR loader's xrGetInstanceProcAddr (upstream loads openxr_loader.dll, which our ASI doesn't
// ship). Our controller actions must come from the same loader instance.
void* StaticXrGetInstanceProcAddr() noexcept;

// The session functions are resolved (me2_xr.cpp's ResolveFunctions; each argument is the address of a g_fn slot): we
// may replace a slot with a wrapper that calls the real function (plugin/TestLoop: the snapshots' image copies, the
// frame's layers, the head recorded or replayed). As LE1's MevrBridge::WrapSessionFunctions.
void WrapSessionFunctions(void** acquireSwapchainImage, void** waitSwapchainImage, void** releaseSwapchainImage,
    void** waitFrame, void** beginFrame, void** endFrame, void** locateViews) noexcept;

// The session and its spaces exist (Present thread): create and attach our controller actions.
void OnSessionCreated(void* instance, void* session) noexcept;

// After a successful xrBeginFrame (Present thread): sync the controller actions and publish their state, with poses
// located in `space` (Halcyon's LOCAL app space, re-origined at recenter: the head pose's space).
void OnFrameBegun(void* session, void* space, std::int64_t predictedDisplayTime) noexcept;

// Present thread, each frame DriveHeadLook ran (stereo, AER, SFR): the view the next game frame is built with, so our mod
// can map the controllers into the world, as LE1's MevrBridge::OnViewBuilt. `worldView`: a head-tracked 3D view (not the
// menu). `lookYawUU`/`lookPitchUU`: CalcViewHook's head look (its own signs: yaw + when the view turns left).
// `headQ` xyzw and `headP` metres: the head DriveHeadLook drove, in the LOCAL app space. Offsets: ApplyHeadPosition's
// along the game camera's right/up/forward, in UE units; `unitsPerMeter` its scale (50 x HeadPosScale). `floorWorld`:
// ordinary gameplay (game mode 0, no VR cine), where the floor lock may apply.
void OnViewBuilt(bool worldView, bool headLook, bool headAim, int lookYawUU, int lookPitchUU, float const headQ[4],
    float const headP[3], float offsetRightUU, float offsetUpUU, float offsetForwardUU, float unitsPerMeter,
    bool floorWorld) noexcept;

// The LOCAL app space was re-origined at the head (RecenterAppSpace): recenter counters, quads placed again.
void OnRecentered() noexcept;

// The session stopped (STOPPING, after xrEndSession): our quads' swapchains and the published view go.
void OnSessionStopped() noexcept;

// Present thread: `source` (an ID3D11Texture2D) is about to be copied into eye `eye`'s image (me2_xr.cpp's
// CopyTextureToEyeFullFrame; 0 left, 1 right). A snapshot armed now saves it too (plugin/TestLoop: <stamp>-source-*.png).
void OnEyeSource(int eye, void* source) noexcept;
// Right after the copy into eye `eye`'s image `image` (index `index`), before its release; `acquired`/`waited` the
// XrResults of its acquire and wait (a failed one means no copy): counted, and logged when one fails.
void OnEyeCopied(int eye, void* image, std::uint32_t index, int acquired, int waited) noexcept;

// Present thread, in place of CopyResource(image, source) into an eye or the flat panel's image (ID3D11DeviceContext,
// ID3D11Texture2D): true if it drew `source` into `image` because D3D11 can't copy between their formats (ME2's swap chain
// is R16G16B16A16_FLOAT in gameplay, the images R8G8B8A8; plugin/EyeCopy), false when the caller's copy is right.
// `shiftX`: the SFR convergence shift in pixels.
bool CopyIntoImage(void* context, void* image, void* source, int shiftX) noexcept;

// What RunFrame knows about the frame it is submitting, for our layers (LE1's MevrBridge::OverlayFrame, the same names;
// the fields LE2 has so far). Present thread.
struct OverlayFrame
{
    void* session = nullptr;    // XrSession
    void* appSpace = nullptr;   // XrSpace: Halcyon's recentered LOCAL app space (the projection layer's)
    void* viewSpace = nullptr;  // XrSpace: VIEW (head-locked quads)
    ID3D11DeviceContext* context = nullptr;   // the game's immediate context
    bool worldFrame = false;    // this frame submits the head-tracked world (the projection layer), not a flat panel
    bool menuOpen = false;      // Halcyon's Insert menu is open
    int gameMode = -1;          // the engine's game mode (EngineProbe::ReadGameModeSEH), -1 unreadable
    // The world layer's views as submitted (each eye's swapchain, rect, pose and FOV), when projectionValid.
    bool projectionValid = false;
    void* projectionSwapchain[2] = {nullptr, nullptr};   // XrSwapchain
    int projectionRect[2][4] = {};                       // x, y, width, height (pixels)
    float projectionOrientation[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    float projectionPosition[2][3] = {};
    float projectionFov[2][4] = {};                      // angleLeft, angleRight, angleUp, angleDown (radians)
    bool stereo = false;        // SFR (the stereo mode whose eyes carry no HUD while the HUD panel takes it)
    // The head at the frame's display time in appSpace (the located views: their mean position, the left eye's
    // orientation, as LE1's), when headValid.
    bool headValid = false;
    float headOrientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // xyzw
    float headPosition[3] = {};
};

// Render thread, from d3d_capture.cpp's draw hooks: an SFR UI draw (IsUiDrawNow, not a menu or cinematic frame).
// `draw(user)` re-issues the game's draw; called with our HUD panel bound while it is armed (plugin/HudPanel: "over"
// draws and Scaleform's blend modes), only once the left eye is captured (`afterLeftEye`, SfrUiMirrorArmed: the frame's
// end, where the left-eye mirror would draw it too; earlier ones are counted). True if the draw went to the panel: the
// caller skips its own.
bool HudPanelDraw(ID3D11DeviceContext* context, void (*draw)(void*), void* user, bool textBatch, bool afterLeftEye) noexcept;

// Present thread (RunFrame), every frame right before xrEndFrame, after Halcyon's own layers are made: our quads (the
// host modules on LE2 so far: plugin/Vignette), up to `capacity` XrCompositionLayerQuad* into `layersOut`, drawn over
// Halcyon's. Returns how many. LE1's MevrBridge::OverlayLayers.
unsigned OverlayLayers(const OverlayFrame& frame, const void** layersOut, unsigned capacity) noexcept;

// d3d_capture.cpp's TryInstallFactoryHooks, for each DXGI factory the game makes (an IDXGIFactory*): our hook on the
// first output's IDXGIOutput6::GetDesc1, where the game asks whether the display is HDR (research/27 7c for LE1).
void OnFactory(void* factory) noexcept;

// Present thread, d3d_capture.cpp's PresentHook: the swap chain this Present is for. The renderer copies the eyes from
// the FIRST swap chain it saw (CaptureSwapChainInfoOnce); a later one presenting is logged (once per swap chain).
void OnSwapChainPresent(void* swapChain, void* capturedSwapChain) noexcept;

// From Halcyon's XInput hooks, right after the real read: merges the motion controllers into pad 0 (Touch -> Xbox
// mapping, game/GamepadMapping) before Halcyon's own pad handling (menu block, move-follows-head).
DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept;

// UnrealScript runs on the game thread only (research/36): the VM keeps its state in process-wide globals, so a script
// call from the Present thread races the game thread's script. True on the engine tick's thread.
bool OnGameThread() noexcept;
// Off the game thread: queues `function` on `object` with a copy of its `size`-byte parameter block for the start of the
// next engine tick, where it runs if both are still in GObjects. True if queued. Results and out parameters are never
// filled in: for setters only.
bool QueueProcessEvent(void* object, void* function, void const* params, std::uint32_t size) noexcept;

// The OpenXR runtime this process uses is SteamVR's: XR_RUNTIME_JSON if set (our [Debug] openxrRuntime, the fake
// headset; set at attach), else the machine's active runtime.
bool XrRuntimeIsSteamVr() noexcept;

// The key is down and a window of the game is in front (LE1's MevrBridge::KeyDown): a hotkey typed in another program
// (Discord, a browser) must not recenter, switch profiles or open the menu.
bool KeyDown(int vk) noexcept;

// Halcyon's hotkey polls go through this: KeyDown in our ASI, the plain poll in an upstream build.
inline bool HotkeyDown(int vk) noexcept
{
#if defined(MEVR_ASI)
    return KeyDown(vk);
#else
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#endif
}
}
