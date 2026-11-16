// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// MEVR (ME VR Attempt) addition, GPL-3.0: the seams only Halcyon's ME3 renderer has. The seams every renderer shares are
// MevrSeams:: (src/halcyon/MevrSeams.h: one vocabulary for LE1, LE2 and LE3); what is declared here is ME3's own, each
// behind a capability bit of plugin/Renderer.hpp. Narrow ABI boundary: upstream xr_types.h stays separate from the
// official OpenXR headers. Implemented in src/plugin/HalcyonLe3Bridge.cpp.

#include "../MevrSeams.h"

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

namespace ME2VR::MevrBridge {
// Game thread, calcview_hook.cpp's SFR branch while the 3D cinema screen is up: half the two cameras' distance (game
// units) for a view whose horizontal half angle is `halfH` (radians, 0 unknown). As ME2's.
float CineScreenHalfSeparationUU(float halfH) noexcept;
// Present thread, every Present (also when XR frames stopped): the host's per-Present work (the test loop, voice, our
// menu closing itself when nobody can see it).
void OnPresent() noexcept;

// ---- Old names (before MevrSeams.h): thin forwarders, removed once nobody calls them -------------------------------------
using MevrSeams::OverlayFrame;
using MevrSeams::CineScreenFrame;
inline void OnFactory(void* factory) noexcept { MevrSeams::OnFactory(factory); }
inline bool CineScreenWanted(bool conversation) noexcept { return MevrSeams::CineScreenWanted(conversation); }
inline bool CineScreenFullRate() noexcept { return MevrSeams::CineScreenFullRate(); }
inline unsigned CineScreenLayers(CineScreenFrame const& frame, void const** layers, unsigned capacity) noexcept
{
    return MevrSeams::CineScreenLayers(frame, layers, capacity);
}
inline bool HudPanelDraw(ID3D11DeviceContext* context, void (*draw)(void*), void* user, bool afterLeftEye) noexcept
{
    return MevrSeams::HudPanelDraw(context, draw, user, false, afterLeftEye);
}
inline void const* TransitionFadeLayer(void* session, void* viewSpace, ID3D11DeviceContext* context, int view) noexcept
{
    return MevrSeams::TransitionFadeLayer(session, viewSpace, context, view);
}
inline bool OnMenuKey(bool ownMenuOpen) noexcept { return MevrSeams::OnMenuKey(ownMenuOpen); }
inline bool MainMenuOpen() noexcept { return MevrSeams::MainMenuOpen(); }
inline unsigned OverlayLayers(OverlayFrame const& frame, void const** out, unsigned capacity) noexcept
{
    return MevrSeams::OverlayLayers(frame, out, capacity);
}
inline bool PlaceNativePanel(OverlayFrame const& frame, float const viewPosition[3], float& width, float& height,
    void** space, float orientation[4], float position[3]) noexcept
{
    const MevrSeams::PanelPose p = MevrSeams::PlacePanel(MevrSeams::PanelKind::ByGameMode, frame, viewPosition, width, height, true);
    *space = p.space;
    for (int i = 0; i < 4; ++i) orientation[i] = p.orientation[i];
    for (int i = 0; i < 3; ++i) position[i] = p.position[i];
    width = p.widthM;
    height = p.heightM;
    return p.nativeMap;
}
inline void* StaticXrGetInstanceProcAddr() noexcept { return MevrSeams::StaticXrGetInstanceProcAddr(); }
inline void WrapSessionFunctions(void** acquire, void** release, void** endFrame, void** locateViews) noexcept
{
    MevrSeams::WrapSessionFunctions(acquire, nullptr, release, nullptr, nullptr, endFrame, locateViews);
}
inline void OnSessionCreated(void* instance, void* session) noexcept { MevrSeams::OnSessionCreated(instance, session); }
inline void OnSessionStopped() noexcept { MevrSeams::OnSessionStopped(); }
inline void OnFrameBegun(void* session, void* space, std::int64_t time) noexcept { MevrSeams::OnFrameBegun(session, space, time); }
inline void OnRecentered() noexcept { MevrSeams::OnRecentered(); }
inline void OnViewBuilt(bool worldView, bool headLook, bool headAim, int yaw, int pitch, float const q[4],
    float const p[3], float right, float up, float forward, float unitsPerMeter, bool floorWorld) noexcept
{
    MevrSeams::OnViewBuilt(worldView, headLook, headAim, yaw, pitch, q, p, right, up, forward, unitsPerMeter, floorWorld);
}
inline float FloorViewTranslation(bool replay, float rawCameraZ, float rightZ, float upZ, float forwardZ,
    float* right, float* up, float* forward) noexcept
{
    return MevrSeams::FloorViewTranslation(replay, rawCameraZ, rightZ, upZ, forwardZ, right, up, forward);
}
inline void OnEyeSource(int eye, void* source) noexcept { MevrSeams::OnEyeSource(eye, source); }
inline void OnEyeCopied(int eye, void* image) noexcept { MevrSeams::OnEyeCopied(eye, image, 0, 0, 0); }
inline void OnEyeReleased(int eye, bool published) noexcept { MevrSeams::OnEyeReleased(eye, published); }
inline bool CopyIntoImage(void* context, void* image, void* source, int shiftX, int eye) noexcept
{
    return MevrSeams::CopyIntoImage(context, image, source, shiftX, eye);
}
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
}
