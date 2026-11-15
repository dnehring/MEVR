// SPDX-License-Identifier: MIT
// LE3-specific composition only. Input, floor, mapping, HDR/colour and test machinery are shared with LE1/LE2.
#include "halcyon/le3/mevr_bridge.h"
#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "engine/GameAdapter.hpp"
#include "plugin/Colour.hpp"
#include "plugin/EyeCopy.hpp"
#include "plugin/FloorLock.hpp"
#include "plugin/PadMerge.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/Swapchains.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/XrHost.hpp"
#include <openxr/openxr.h>
#include <atomic>
#include <cstring>
#include <mutex>
namespace ME2VR::MevrBridge {
namespace {
std::atomic<std::uint64_t> gFloorTranslatedMs{0};
std::mutex gPadMutex;
XINPUT_GAMEPAD gLastPad{};
DWORD gPacket = 0;
}
void* StaticXrGetInstanceProcAddr() noexcept { return reinterpret_cast<void*>(&xrGetInstanceProcAddr); }
void WrapSessionFunctions(void** acquire, void** release, void** endFrame, void** locateViews) noexcept {
    mevr::host::testloop::WrapSessionFunctions(acquire, release, endFrame, locateViews);
}
void OnSessionCreated(void* instance, void* session) noexcept {
    auto const xrInstance = static_cast<XrInstance>(instance);
    auto const xrSession = static_cast<XrSession>(session);
    mevr::host::testloop::OnSessionCreated(xrSession);
    mevr::host::swapchains::OnSessionCreated(xrSession);
    mevr::host::eye_copy::Reset();
    mevr::host::floor_lock::Reset();
    mevr::vr::controllers::Publish({});
    mevr::host::pad_merge::Reset();
    gFloorTranslatedMs.store(0, std::memory_order_relaxed);
    bool const ready = mevr::host::xr_host::OnSessionCreated(xrInstance, xrSession);
    mevr::log::Info("halcyon le3: shared controller actions {} on the renderer session", ready ? "attached" : "unavailable");
    XrSystemGetInfo info{XR_TYPE_SYSTEM_GET_INFO};
    info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
    XrInstanceProperties runtime{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetSystem(xrInstance, &info, &system)) &&
        XR_SUCCEEDED(xrGetSystemProperties(xrInstance, system, &properties)) &&
        XR_SUCCEEDED(xrGetInstanceProperties(xrInstance, &runtime))) {
        mevr::host::colour::SetHeadset(runtime.runtimeName, properties.systemName, properties.vendorId);
    }
}
void OnSessionStopped() noexcept {
    mevr::host::floor_lock::Reset();
    mevr::host::eye_copy::Reset();
    mevr::host::xr_host::ForgetHaptics();
    mevr::vr::controllers::Publish({});
    mevr::host::pad_merge::Reset();
}
void OnFrameBegun(void* session, void* space, std::int64_t time) noexcept {
    if (!session) return;
    mevr::vr::controllers::State state;
    auto const s = static_cast<XrSession>(session);
    auto const base = static_cast<XrSpace>(space);
    mevr::host::xr_host::OnFrameBegun(s, base, static_cast<XrTime>(time), state);
    mevr::host::testloop::OnFrameBegun(s, base, static_cast<XrTime>(time), mevr::host::xr_host::InputFocused(), state);
    // Inactive samples must replace the previous live sample too (focus loss/unplug).
    mevr::vr::controllers::Publish(state);
    if (mevr::host::renderer::GameMode() != 0 || mevr::host::renderer::OwnMenuOpen())
        mevr::vr::host_view::Reset(); // flat/menu paths do not call DriveHeadLook this frame
}
void OnRecentered() noexcept {
    mevr::vr::controllers::Publish({}); // old app-space poses must not mix with the new head space
    mevr::host::xr_host::NoteRecentered();
    mevr::host::floor_lock::Reset();
    gFloorTranslatedMs.store(0, std::memory_order_relaxed);
}
void OnViewBuilt(bool worldView, bool headLook, bool headAim, int yaw, int pitch, float const q[4],
    float const p[3], float right, float up, float forward, float unitsPerMeter, bool floorWorld) noexcept {
    mevr::vr::host_view::State view;
    view.valid = worldView;
    view.headLook = headLook;
    view.headAim = headAim;
    view.head.orientation = {q[0], q[1], q[2], q[3]};
    view.head.position = {p[0], p[1], p[2]};
    view.lookYaw = -yaw;
    view.lookPitch = pitch;
    view.offsetRight = right;
    view.offsetUp = up;
    view.offsetForward = forward;
    view.unitsPerMeter = unitsPerMeter;
    mevr::vr::floor::State floor;
    mevr::host::xr_host::Floor(&floor);
    mevr::host::floor_lock::Queue(view, floor, floorWorld);
    auto const now = GetTickCount64();
    if (!worldView || now - gFloorTranslatedMs.load(std::memory_order_relaxed) > 100)
        mevr::vr::host_view::Publish(view, now);
}
float FloorViewTranslation(bool replay, float rawCameraZ, float rightZ, float upZ, float forwardZ,
    float* right, float* up, float* forward) noexcept {
    gFloorTranslatedMs.store(GetTickCount64(), std::memory_order_relaxed);
    return mevr::host::floor_lock::Translate(replay, rawCameraZ, rightZ, upZ, forwardZ, right, up, forward);
}
void OnEyeSource(int eye, void* source) noexcept {
    mevr::host::testloop::OnEyeSource(eye, static_cast<ID3D11Texture2D*>(source));
}
void OnEyeCopied(int eye, void* image) noexcept {
    mevr::host::testloop::OnEyeCopied(eye, static_cast<ID3D11Texture2D*>(image));
}
bool CopyIntoImage(void* context, void* image, void* source, int shiftX, int eye) noexcept {
    return mevr::host::eye_copy::Copy(static_cast<ID3D11DeviceContext*>(context), static_cast<ID3D11Texture2D*>(image),
        static_cast<ID3D11Texture2D*>(source), shiftX, eye);
}
DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept {
    if (index != 0 || !state) return result;
    auto const controllers = mevr::vr::controllers::Latest();
    if (!controllers.active) { mevr::host::pad_merge::NoControllers(); return result; }
    std::lock_guard lock(gPadMutex);
    mevr::host::pad_merge::Renderer renderer;
    renderer.ownMenuOpen = mevr::host::renderer::OwnMenuOpen();
    renderer.headLookYawUU = &mevr::host::renderer::MoveStickHeadLookYawUU;
    renderer.gateRightClick = true;
    int const mode = mevr::host::renderer::GameMode();
    renderer.conversationTriggers = mevr::engine::adapter::IsConversationMode(mode);
    // No orbital mapping claim until LE3 native scan controls are audited (L3-9).
    auto pad = mevr::host::pad_merge::Merge(result == ERROR_SUCCESS ? state->Gamepad : XINPUT_GAMEPAD{},
        controllers, renderer, GetTickCount64());
    // Authored conversations/cinematics have native camera/position ownership. No artificial traversal.
    if (renderer.conversationTriggers || mevr::engine::adapter::GameMode(mode) == mevr::engine::adapter::Mode::Cinematic) {
        pad.sThumbLX = pad.sThumbLY = 0;
    }
    if (std::memcmp(&pad, &gLastPad, sizeof pad) != 0) { gLastPad = pad; ++gPacket; }
    state->dwPacketNumber = gPacket;
    state->Gamepad = pad;
    return ERROR_SUCCESS;
}
}
