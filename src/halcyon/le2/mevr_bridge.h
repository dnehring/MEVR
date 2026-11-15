// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../SequelPresentation.h"

// MEVR (ME VR Attempt) addition, GPL-3.0: the seams where our host plugs into Halcyon's ME2 renderer, as
// src/halcyon/le1/mevr_bridge.h does for LE1. Declared without the OpenXR SDK headers, because xr_types.h defines the
// same names; the implementation is in src/plugin/HalcyonLe2Bridge.cpp. Handles are passed as void*.

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

struct ID3D11DeviceContext;

namespace ME2VR::MevrBridge
{
struct OverlayFrame;
// [MEVR] The native galaxy/orbital image on ME1's table geometry. Updated before OverlayLayers.
struct GalaxyPanel {
    float orientation[4] = {0, 0, 0, 1};
    float position[3] = {};
    float widthM = 0;
    float heightM = 0;
};
bool PlaceGalaxyPanel(OverlayFrame const& frame, bool imageReady, float aspect, GalaxyPanel& out) noexcept;
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

// Game thread, in ApplyHeadPosition (calcview_hook.cpp) for each pass's scene view, before the lean moves the camera: the
// floor lock (our plugin/FloorLock, as LE1's render_hook.cpp ApplyViewOffsetSEH). `rawCameraZ` the camera's height
// (-PreViewTranslation.Z), `rightZ`/`upZ`/`forwardZ` the camera axes' Z; `right`/`up`/`forward` the lean in UE units, which
// it may replace with the view the mod was told about. Returns the height to add (UE units), 0 where it doesn't apply.
// `replay`: Halcyon's second pass (t_replay), which reuses the first pass's answer.
float FloorViewTranslation(bool replay, float rawCameraZ, float rightZ, float upZ, float forwardZ, float* right, float* up,
    float* forward) noexcept;
// After a corrected view's ViewOrigin and PreViewTranslation were written (the crash breadcrumbs' journal).
void JournalFloorViewWrite(void const* origin, void const* preTranslation) noexcept;

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
void OnEyeReleased(int eye, bool published) noexcept; // receipt only, no released-image access

// Present thread, in place of CopyResource(image, source) into an eye or the flat panel's image (ID3D11DeviceContext,
// ID3D11Texture2D): owns the complete validated raw-copy or shader-conversion operation.
// True means issued; false is failure, never permission for a raw-copy fallback.
// `shiftX`: the SFR convergence shift in pixels.
bool CopyIntoImage(void* context, void* image, void* source, int shiftX) noexcept;

// What RunFrame knows about the frame it is submitting, for our layers (LE1's MevrBridge::OverlayFrame, the same names;
// the fields LE2 has so far). Present thread.


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

// ---- ME1's 3D cinema screen (research/52; research/le2-cinema.md; plugin/CineScreen, CinemaBackdrop, TransitionFade) -------
// Present thread, each frame a conversation (`conversation`) or cutscene is decided (me2_xr.cpp's VRCINE block): whether it
// goes on the 3D screen ([Cinematics] conversations / cutscenes in ASI\LE2MEVR.ini, ME1's keys and defaults).
bool CineScreenWanted(bool conversation) noexcept;
bool CineScreenFullRate() noexcept;
// Game thread, calcview_hook.cpp's SFR branch while the screen is up: half the two cameras' distance (game units) for a view
// whose horizontal half angle is `halfH` (radians, 0 unknown), our plan for this frame (core/vr/CineScreen: the scene
// [Cinematics] convergeM away on the screen plane once each eye is cropped by its depth).
float CineScreenHalfSeparationUU(float halfH) noexcept;
// Present thread, at the SFR submit while the screen is up: the eyes as located (app space) and each eye's image.
// The screen's layers in submit order (the floor and horizon's projection layer, then the left and right eyes' quads), at
// most `capacity` into `layersOut`; 0 if it can't be shown (the caller then submits its projection layer as before). The
// screen's place is kept for this frame's OverlayLayers (the game's UI on its own layer in front of it).
unsigned CineScreenLayers(const CineScreenFrame& frame, const void** layersOut, unsigned capacity) noexcept;
// Present thread, the frame's last layer: this frame's view (0 the world, 1 the 3D screen, 2 a cinematic on the flat panel,
// 3 another flat panel, -1 none submitted); a head-locked black quad (an XrCompositionLayerQuad*) to submit last, or null.
const void* TransitionFadeLayer(void* session, void* viewSpace, ID3D11DeviceContext* context, int view) noexcept;

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

// The menu key went down (me2_menu.cpp's PollInsert; `ownMenuOpen`: Halcyon's menu is open). True if our main menu took it
// (opened or closed; plugin/MenusLe2), false if Halcyon's menu toggles as upstream: with Ctrl or Shift held, or while its
// menu is open, as LE1's MevrBridge::OnMenuKey.
bool OnMenuKey(bool ownMenuOpen) noexcept;
// Our main menu is open (plugin/MainMenu): Halcyon's window procedure swallows the desktop's input as for its own menu.
bool MainMenuOpen() noexcept;

// One of the game's screens stands on the wrist or in the room (plugin/WorldUi, the mod's game/WorldUi; plugin/MenusLe2,
// research/le2-menus.md "ME2's screens on the wrist"), or is being opened (the mod reserved it): the GUI mode `mode` (7) is then
// not flat (d3d_capture.cpp's GetMenuMode answers false: the world in stereo behind the screen, its UI on our HUD panel), as
// LE1's MevrBridge::WorldUiOwnership in xr_session.cpp's decision. Any thread.
bool WorldUiOwnsGui(int mode) noexcept;

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
