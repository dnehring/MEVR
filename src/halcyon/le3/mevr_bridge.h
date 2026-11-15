// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "../SequelPresentation.h"
// [MEVR] Narrow ABI boundary: upstream xr_types.h stays separate from official OpenXR headers.
#include <Windows.h>
#include <Xinput.h>
#include <cstdint>
namespace ME2VR::MevrBridge {
// [MEVR] Shared menu/input/layer bridge. Native screen routing is independently gated.
bool CineScreenWanted(bool conversation) noexcept;
bool CineScreenFullRate() noexcept;
float CineScreenHalfSeparationUU(float halfH) noexcept;
unsigned CineScreenLayers(CineScreenFrame const& frame, void const** layers, unsigned capacity) noexcept;
bool HudPanelDraw(ID3D11DeviceContext* context, void (*draw)(void*), void* user, bool afterLeftEye) noexcept;
void const* TransitionFadeLayer(void* session, void* viewSpace, ID3D11DeviceContext* context, int view) noexcept;
bool OnMenuKey(bool ownMenuOpen) noexcept;
bool MainMenuOpen() noexcept;
void OnPresent() noexcept;
unsigned OverlayLayers(OverlayFrame const& frame, void const** out, unsigned capacity) noexcept;
void PlaceNativePanel(OverlayFrame const& frame, float const viewPosition[3], float width, float height,
    void** space, float orientation[4], float position[3]) noexcept;
void* StaticXrGetInstanceProcAddr() noexcept;
void WrapSessionFunctions(void** acquire, void** release, void** endFrame, void** locateViews) noexcept;
void OnSessionCreated(void* instance, void* session) noexcept;
void OnSessionStopped() noexcept;
void OnFrameBegun(void* session, void* space, std::int64_t time) noexcept;
void OnRecentered() noexcept;
void OnViewBuilt(bool worldView, bool headLook, bool headAim, int yaw, int pitch, float const q[4],
    float const p[3], float right, float up, float forward, float unitsPerMeter, bool floorWorld) noexcept;
float FloorViewTranslation(bool replay, float rawCameraZ, float rightZ, float upZ, float forwardZ,
    float* right, float* up, float* forward) noexcept;
void OnEyeSource(int eye, void* source) noexcept;
void OnEyeCopied(int eye, void* image) noexcept;
bool CopyIntoImage(void* context, void* image, void* source, int shiftX, int eye) noexcept;
DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept;
bool OnGameThread() noexcept;
bool QueueProcessEvent(void* object, void* function, void const* params, std::uint32_t size) noexcept;
bool XrRuntimeIsSteamVr() noexcept;
bool KeyDown(int vk) noexcept;
}
