// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// MEVR (ME VR Attempt) addition, GPL-3.0: the seams only Halcyon's ME2 renderer has. The seams every renderer shares are
// MevrSeams:: (src/halcyon/MevrSeams.h: one vocabulary for LE1, LE2 and LE3); what is declared here is ME2's own, each
// behind a capability bit of plugin/Renderer.hpp. Declared without the OpenXR SDK headers, because xr_types.h defines the
// same names; implemented in src/plugin/HalcyonLe2Bridge.cpp and MenusLe2.cpp. Handles are passed as void*.

#include "../MevrSeams.h"

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

struct ID3D11DeviceContext;

namespace ME2VR::MevrBridge
{
// After a corrected view's ViewOrigin and PreViewTranslation were written (FloorViewTranslation returned non-zero): the
// crash breadcrumbs' journal. Game thread.
void JournalFloorViewWrite(void const* origin, void const* preTranslation) noexcept;

// Game thread, calcview_hook.cpp's SFR branch while the 3D cinema screen is up: half the two cameras' distance (game
// units) for a view whose horizontal half angle is `halfH` (radians, 0 unknown), our plan for this frame
// (core/vr/CineScreen: the scene [Cinematics] convergeM away on the screen plane once each eye is cropped by its depth).
// LE1 plans its passes through PlanStereo instead.
float CineScreenHalfSeparationUU(float halfH) noexcept;

// Present thread, d3d_capture.cpp's PresentHook: the swap chain this Present is for. The renderer copies the eyes from
// the FIRST swap chain it saw (CaptureSwapChainInfoOnce); a later one presenting is logged (once per swap chain). Also
// the host's per-Present work (the test loop, voice, our menu closing itself when nobody can see it).
void OnSwapChainPresent(void* swapChain, void* capturedSwapChain) noexcept;

// One of the game's screens stands on the wrist or in the room (plugin/WorldUi, the mod's game/WorldUi; plugin/MenusLe2,
// research/le2-menus.md "ME2's screens on the wrist"), or is being opened (the mod reserved it): the GUI mode `mode` (7) is
// then not flat (d3d_capture.cpp's GetMenuMode answers false: the world in stereo behind the screen, its UI on our HUD
// panel), as LE1's MevrBridge::WorldUiOwnership in xr_session.cpp's decision. Any thread.
bool WorldUiOwnsGui(int mode) noexcept;

// Halcyon's hotkey polls go through this: MevrSeams::KeyDown in our ASI, the plain poll in an upstream build.
inline bool HotkeyDown(int vk) noexcept
{
#if defined(MEVR_ASI)
    return MevrSeams::KeyDown(vk);
#else
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#endif
}

// ---- Old names (before MevrSeams.h): thin forwarders, removed once nobody calls them -------------------------------------
using MevrSeams::OverlayFrame;
using MevrSeams::CineScreenFrame;
struct GalaxyPanel {
    float orientation[4] = {0, 0, 0, 1};
    float position[3] = {};
    float widthM = 0;
    float heightM = 0;
};
inline bool PlaceGalaxyPanel(OverlayFrame const& frame, bool imageReady, float aspect, GalaxyPanel& out) noexcept
{
    const float at[3] = {};
    const MevrSeams::PanelPose p = MevrSeams::PlacePanel(MevrSeams::PanelKind::ByGameMode, frame, at, 1.0f, aspect, imageReady);
    if (!p.placed) return false;
    for (int i = 0; i < 4; ++i) out.orientation[i] = p.orientation[i];
    for (int i = 0; i < 3; ++i) out.position[i] = p.position[i];
    out.widthM = p.widthM;
    out.heightM = p.heightM;
    return true;
}
inline void* StaticXrGetInstanceProcAddr() noexcept { return MevrSeams::StaticXrGetInstanceProcAddr(); }
inline void WrapSessionFunctions(void** acquireSwapchainImage, void** waitSwapchainImage, void** releaseSwapchainImage,
    void** waitFrame, void** beginFrame, void** endFrame, void** locateViews) noexcept
{
    MevrSeams::WrapSessionFunctions(acquireSwapchainImage, waitSwapchainImage, releaseSwapchainImage, waitFrame, beginFrame,
        endFrame, locateViews);
}
inline void OnSessionCreated(void* instance, void* session) noexcept { MevrSeams::OnSessionCreated(instance, session); }
inline void OnFrameBegun(void* session, void* space, std::int64_t time) noexcept { MevrSeams::OnFrameBegun(session, space, time); }
inline void OnViewBuilt(bool worldView, bool headLook, bool headAim, int lookYawUU, int lookPitchUU, float const headQ[4],
    float const headP[3], float offsetRightUU, float offsetUpUU, float offsetForwardUU, float unitsPerMeter,
    bool floorWorld) noexcept
{
    MevrSeams::OnViewBuilt(worldView, headLook, headAim, lookYawUU, lookPitchUU, headQ, headP, offsetRightUU, offsetUpUU,
        offsetForwardUU, unitsPerMeter, floorWorld);
}
inline float FloorViewTranslation(bool replay, float rawCameraZ, float rightZ, float upZ, float forwardZ, float* right,
    float* up, float* forward) noexcept
{
    return MevrSeams::FloorViewTranslation(replay, rawCameraZ, rightZ, upZ, forwardZ, right, up, forward);
}
inline void OnRecentered() noexcept { MevrSeams::OnRecentered(); }
inline void OnSessionStopped() noexcept { MevrSeams::OnSessionStopped(); }
inline void OnEyeSource(int eye, void* source) noexcept { MevrSeams::OnEyeSource(eye, source); }
inline void OnEyeCopied(int eye, void* image, std::uint32_t index, int acquired, int waited) noexcept
{
    MevrSeams::OnEyeCopied(eye, image, index, acquired, waited);
}
inline void OnEyeReleased(int eye, bool published) noexcept { MevrSeams::OnEyeReleased(eye, published); }
inline bool CopyIntoImage(void* context, void* image, void* source, int shiftX, int eye) noexcept
{
    return MevrSeams::CopyIntoImage(context, image, source, shiftX, eye);
}
inline bool HudPanelDraw(ID3D11DeviceContext* context, void (*draw)(void*), void* user, bool textBatch, bool afterLeftEye) noexcept
{
    return MevrSeams::HudPanelDraw(context, draw, user, textBatch, afterLeftEye);
}
inline unsigned OverlayLayers(const OverlayFrame& frame, const void** layersOut, unsigned capacity) noexcept
{
    return MevrSeams::OverlayLayers(frame, layersOut, capacity);
}
inline bool CineScreenWanted(bool conversation) noexcept { return MevrSeams::CineScreenWanted(conversation); }
inline bool CineScreenFullRate() noexcept { return MevrSeams::CineScreenFullRate(); }
inline unsigned CineScreenLayers(const CineScreenFrame& frame, const void** layersOut, unsigned capacity) noexcept
{
    return MevrSeams::CineScreenLayers(frame, layersOut, capacity);
}
inline const void* TransitionFadeLayer(void* session, void* viewSpace, ID3D11DeviceContext* context, int view) noexcept
{
    return MevrSeams::TransitionFadeLayer(session, viewSpace, context, view);
}
inline void OnFactory(void* factory) noexcept { MevrSeams::OnFactory(factory); }
inline DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept
{
    return MevrSeams::MergeMotionControllers(index, state, result);
}
inline bool OnGameThread() noexcept { return MevrSeams::OnGameThread(); }
inline bool QueueProcessEvent(void* object, void* function, void const* params, std::uint32_t size) noexcept
{
    return MevrSeams::QueueProcessEvent(object, function, params, size);
}
inline bool XrRuntimeIsSteamVr() noexcept { return MevrSeams::XrRuntimeIsSteamVr(); }
inline bool KeyDown(int vk) noexcept { return MevrSeams::KeyDown(vk); }
inline bool OnMenuKey(bool ownMenuOpen) noexcept { return MevrSeams::OnMenuKey(ownMenuOpen); }
inline bool MainMenuOpen() noexcept { return MevrSeams::MainMenuOpen(); }
}
