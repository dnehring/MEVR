// Our side of the seams in Halcyon's renderer (src/halcyon/le1/mevr_bridge.h): the OpenXR loader,
// controller actions on Halcyon's session, and motion controllers as Xbox pad 0.

#include "halcyon/le1/mevr_bridge.h"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/xr/Input.hpp"
#include "game/GamepadMapping.hpp"

#include <openxr/openxr.h>

#include <atomic>
#include <cstring>
#include <mutex>

namespace MELEVR::MevrBridge {
namespace {

mevr::xr::Input gInput;
mevr::xr::Input::Target gTarget;  // Present thread
bool gInputFailed = false;
std::atomic<unsigned> gFrames{0};

std::mutex gPadMutex;  // the latches and packet numbering below (XInput is polled from any thread)
mevr::game::gamepad::Latches gLatches;
XINPUT_GAMEPAD gLastPad{};
DWORD gPacket = 0;

} // namespace

void* StaticXrGetInstanceProcAddr() noexcept {
    return reinterpret_cast<void*>(&xrGetInstanceProcAddr);
}

void OnSessionCreated(void* instance, void* session) noexcept {
    if (gInput.Matches(gTarget)) {
        gInput.Release(gTarget);  // a new session (upstream creates one per process, but be safe)
    }
    gTarget.instance = static_cast<XrInstance>(instance);
    gTarget.session = static_cast<XrSession>(session);
    ++gTarget.instanceGeneration;
    ++gTarget.sessionGeneration;
    gInputFailed = !gInput.Create(gTarget);
    if (gInputFailed) {
        mevr::log::Error("halcyon bridge: controller actions failed; motion controllers unavailable");
    }
}

void OnFrameBegun(void* session, void* space, std::int64_t predictedDisplayTime) noexcept {
    if (gInputFailed || session == nullptr || static_cast<XrSession>(session) != gTarget.session) {
        return;
    }
    gTarget.base = static_cast<XrSpace>(space);  // recentering replaces it
    mevr::vr::controllers::State state;
    gInput.Sync(gTarget, static_cast<XrTime>(predictedDisplayTime), state);
    mevr::vr::controllers::Publish(state);
    gFrames.fetch_add(1, std::memory_order_relaxed);
}

DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept {
    if (index != 0 || state == nullptr) {
        return result;
    }
    mevr::vr::controllers::State const controllers = mevr::vr::controllers::Latest();
    if (!controllers.active) {
        return result;
    }
    bool const realPad = result == ERROR_SUCCESS;
    std::lock_guard lock(gPadMutex);
    XINPUT_GAMEPAD const pad = mevr::game::gamepad::Merge(realPad ? state->Gamepad : XINPUT_GAMEPAD{},
        mevr::game::gamepad::FromControllers(controllers, gLatches));
    if (std::memcmp(&pad, &gLastPad, sizeof pad) != 0) {
        gLastPad = pad;
        ++gPacket;  // the game only re-reads a pad whose packet number changed
    }
    state->dwPacketNumber = gPacket;
    state->Gamepad = pad;
    return ERROR_SUCCESS;
}

} // namespace MELEVR::MevrBridge
