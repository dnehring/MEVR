// Our side of the seams in Halcyon's renderer (src/halcyon/le1/mevr_bridge.h): the OpenXR loader,
// controller actions on Halcyon's session, motion controllers as Xbox pad 0, the view published
// for mapping the controllers into the world, and controller aim replacing Halcyon's head aim.

#include "halcyon/le1/mevr_bridge.h"

#include "core/Log.hpp"
#include "core/Window.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/xr/Input.hpp"
#include "game/GamepadMapping.hpp"
#include "halcyon/le1/render_hook.h"
#include "plugin/Halcyon.hpp"

#include <openxr/openxr.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace MELEVR::MevrBridge {
namespace {

mevr::xr::Input gInput;
mevr::xr::Input::Target gTarget;  // set on Halcyon's XR thread before frames start, then Present thread
bool gInputFailed = false;
std::atomic<HWND> gGameWindow{nullptr};
bool gWasFocused = false;  // Present thread
std::atomic<unsigned> gFrames{0};

std::mutex gPadMutex;  // the latches and packet numbering below (XInput is polled from any thread)
mevr::game::gamepad::Latches gLatches;
XINPUT_GAMEPAD gLastPad{};
DWORD gPacket = 0;

// Controller aim: the mod aims with the right controller (HostServices::setHandAim) and that
// controller's aim pose was tracked within the hold time, so a moment of lost tracking doesn't
// hand aiming back and forth between the head and the hand.
constexpr std::uint64_t kControllerAimHoldMs = 1000;
std::atomic<bool> gModHandAim{false};
std::atomic<std::uint64_t> gRightAimTrackedMs{0};
bool gControllerAim = false;  // Present thread: the last answer, to log the switches
bool gLoggedView = false;     // Present thread

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
    if (state.active && state.right.aimValid) {
        gRightAimTrackedMs.store(GetTickCount64(), std::memory_order_relaxed);
    }
    gFrames.fetch_add(1, std::memory_order_relaxed);
    bool const focused = gInput.Focused();
    if (focused && !gWasFocused) {
        mevr::window::BringToForeground(gGameWindow.load());
    }
    gWasFocused = focused;
}

void SetGameWindow(HWND window) noexcept {
    gGameWindow.store(window);
}

void OnViewBuilt(bool worldView, bool headLook, bool headAim, float const headOrientation[4],
    float const headPosition[3], float offsetRightUU, float offsetUpUU, float offsetForwardUU,
    float unitsPerMeter) noexcept {
    mevr::vr::host_view::State view;
    view.valid = worldView;
    view.headLook = headLook;
    view.headAim = headAim;
    view.head.orientation = {headOrientation[0], headOrientation[1], headOrientation[2], headOrientation[3]};
    view.head.position = {headPosition[0], headPosition[1], headPosition[2]};
    // Halcyon's head look yaw is + when the view turns left; ours (UE rotators) + right. Pitch is
    // + up in both. Zero while head look is off.
    view.lookYaw = -MELEVR::RenderHook::HeadLookYawUU();
    view.lookPitch = MELEVR::RenderHook::HeadLookPitchUU();
    view.offsetForward = offsetForwardUU;
    view.offsetRight = offsetRightUU;
    view.offsetUp = offsetUpUU;
    view.unitsPerMeter = unitsPerMeter;
    view.baseRightValid =
        MELEVR::RenderHook::GetLastViewRight(&view.baseRight[0], &view.baseRight[1], &view.baseRight[2]);
    mevr::vr::host_view::Publish(view);
    if (worldView && !gLoggedView) {
        gLoggedView = true;
        mevr::log::Info("halcyon bridge: first world view: head look yaw {} pitch {} (head aim {}), view offset "
                        "fwd {:.1f} right {:.1f} up {:.1f}, {:.1f} units per meter, head at ({:.2f}, {:.2f}, {:.2f}) m",
            view.lookYaw, view.lookPitch, headAim ? "on" : "off", offsetForwardUU, offsetRightUU, offsetUpUU,
            unitsPerMeter, headPosition[0], headPosition[1], headPosition[2]);
    }
}

bool ControllerAimActive() noexcept {
    std::uint64_t const tracked = gRightAimTrackedMs.load(std::memory_order_relaxed);
    bool const on = gModHandAim.load(std::memory_order_relaxed) && tracked != 0 &&
        GetTickCount64() - tracked < kControllerAimHoldMs;
    if (on != gControllerAim) {
        gControllerAim = on;
        mevr::log::Info("halcyon bridge: {}", on ? "the right controller aims weapons; Halcyon's on-foot head aim is "
                                                   "off (the head only looks)"
                                                 : "controller aim stopped; Halcyon's head aim is back");
    }
    return on;
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

namespace mevr::host::halcyon {

void SetModHandAim(bool on) {
    if (MELEVR::MevrBridge::gModHandAim.exchange(on, std::memory_order_relaxed) != on) {
        log::Info("halcyon bridge: the mod {} weapons with the right controller", on ? "aims" : "no longer aims");
    }
}

} // namespace mevr::host::halcyon
