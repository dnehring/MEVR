#pragma once

// MEVR (ME VR Attempt) addition, GPL-3.0: the seams where our host plugs into Halcyon's code.
// Declared without the OpenXR SDK headers, because xr_types.h defines the same names; the
// implementation is in src/plugin/HalcyonBridge.cpp. Handles are passed as void*.

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

struct ID3D11BlendState;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;

namespace MELEVR::MevrBridge
{
// The statically linked OpenXR loader's xrGetInstanceProcAddr (upstream loads openxr_loader.dll,
// which our ASI doesn't ship). Our controller actions must come from the same loader instance.
void* StaticXrGetInstanceProcAddr() noexcept;

// The session and its spaces exist (Halcyon's XR thread, before xrBeginSession; published to the
// Present thread through its g_ready): create and attach our controller actions.
void OnSessionCreated(void* instance, void* session) noexcept;

// The game's window (Present thread, once known): brought to the foreground when the headset
// session gains input focus, since UE3 ignores input without window focus.
void SetGameWindow(HWND window) noexcept;

// After xrBeginFrame (Present thread): sync the controller actions and publish their state, with
// poses located in `space` (Halcyon's recentered LOCAL app space, the head pose's space).
void OnFrameBegun(void* session, void* space, std::int64_t predictedDisplayTime) noexcept;

// From Halcyon's XInput hooks, right after the real read: merges the motion controllers into pad
// 0 (Touch -> Xbox mapping) before Halcyon's own pad handling (menu block, move-follows-head).
DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept;

// Present thread, once per frame after the view for the next game frame is set up (head look,
// view offset): publishes how that view relates to the game camera, so our mod can map the
// controllers into the world (hand aim, arm IK). `worldView`: a head-tracked 3D view (head
// valid, no menu, not the flat screen). `head`: the roll-removed head pose the view is built for
// (orientation xyzw, position; Halcyon's app space). Offsets are the view offset in UE units
// along the camera's right/up/forward; `unitsPerMeter` is the view's movement per meter of head
// movement. Head look is read from RenderHook.
void OnViewBuilt(bool worldView, bool headLook, bool headAim, float const headOrientation[4],
                 float const headPosition[3], float offsetRightUU, float offsetUpUU, float offsetForwardUU,
                 float unitsPerMeter) noexcept;

// True while our mod aims weapons with the right controller and that controller is tracked
// (held for a moment through brief tracking loss): Halcyon's on-foot combat head aim then stays
// off, so the head only looks (render-side) and the controller aims. False with a plain gamepad.
bool ControllerAimActive() noexcept;

// ---- Body-locked HUD panel and hand reticle (src/plugin/HalcyonOverlays.cpp) --------------------
// The game's UI draws of a head-tracked world frame go into a panel texture instead of the eye
// images (d3d_capture.cpp's draw hooks), and RunFrame shows that texture as a quad fixed in the
// tracking space in front of the player's body, plus a small reticle quad on the weapon's aim ray.

// Render thread (draw hooks): UI draws go to the HUD panel. Armed by OverlayLayers for the frame
// after a head-tracked world frame, while the panel is on.
bool HudPanelArmed() noexcept;

// Render thread, for one UI draw into a `width` x `height` target whose render target view has
// DXGI format `format`: the panel texture's render target (made or remade on demand), or nullptr if
// it can't take the draw (the draw then goes to the frame as before).
ID3D11RenderTargetView* HudPanelTarget(ID3D11DeviceContext* context, unsigned width, unsigned height,
                                       int format) noexcept;

// The game's blend state for that draw, but with alpha accumulated "over" (one, inverse source
// alpha) and alpha writes on, so the panel ends up with premultiplied colour and coverage. Cached per
// game state; nullptr keeps the game's own.
ID3D11BlendState* HudPanelBlend(ID3D11DeviceContext* context, ID3D11BlendState* game) noexcept;

// Counts one UI draw that went to the panel (OverlayLayers logs and resets the count).
void NoteHudPanelDraw() noexcept;

// What RunFrame knows about the frame it is submitting.
struct OverlayFrame
{
    void* session = nullptr;                 // XrSession
    void* appSpace = nullptr;                // XrSpace: Halcyon's recentered LOCAL app space
    ID3D11DeviceContext* context = nullptr;  // the game's immediate context
    bool worldFrame = false;   // this frame submits the head-tracked world (the projection layer)
    bool menuOpen = false;     // Halcyon's Insert menu is open
    bool sfr2 = false;         // Stereo 2 is the active VR mode (the only one with a HUD-free frame)
    int gameMode = -1;         // USFXGameModeManager::CurrentMode: 0 on foot, -1 unreadable
    bool headValid = false;
    float headOrientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // xyzw, app space
    float headPosition[3] = {0.0f, 0.0f, 0.0f};            // eye center, app space, meters
    float floatWhiteScale = 1.0f;  // for a float (scRGB) panel: multiplier that maps SDR white to 1.0
};

// Present thread (RunFrame), once per frame after the eyes are filled, every frame (also flat and
// menu frames): copies the HUD panel texture into its swapchain, places the panel and the reticle,
// and writes up to `capacity` quad layers (XrCompositionLayerQuad*, panel first) into `layersOut`.
// Also clears the panel texture, arms the redirect for the next frame, and hides the game's own
// crosshair while the hand reticle replaces it. Returns the number of layers written.
unsigned OverlayLayers(const OverlayFrame& frame, const void** layersOut, unsigned capacity) noexcept;

// True when the frame's panel texture is a float (scRGB) format, so RunFrame passes its white scale.
bool HudPanelIsFloat() noexcept;

// Recentering re-origined the app space: the panel goes straight ahead again.
void OnRecentered() noexcept;

// Insert menu: re-place the panel in front of where the head points now (next frame).
void RequestHudPanelReplace() noexcept;

// Insert menu status: UI draws that went to the panel in the last world frame.
unsigned HudPanelDrawsLastFrame() noexcept;

// [MENUVIS] One-shot diagnostics for Halcyon's Insert menu quad (Present thread): reads back
// `texture` (the menu's ImGui texture, or the swapchain image right after the copy into it) a few
// frames later without stalling, and logs how many pixels are visible (alpha > 0). `tag` names it.
void ProbeTextureAlpha(ID3D11DeviceContext* context, ID3D11Texture2D* texture, const char* tag) noexcept;
}
