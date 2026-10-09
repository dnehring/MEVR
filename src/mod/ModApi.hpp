#pragma once

// ABI between the host ASI (stable, loaded by LEBinkProxy) and the mod, which is either linked
// into the host (monolithic release build) or a separate DLL the host hot-reloads (dev build).

#include <cstdint>

class ISharedProxyInterface;
class UFunction;
class UObject;
struct IDXGISwapChain;

namespace mevr {

namespace vr::controllers {
struct State;
}
namespace vr::host_view {
struct State;
}
namespace vr::aim_ray {
struct Ray;
}

// Fields are only ever appended: a mod reads the fields of every version up to the host's (and
// its own), so a newer mod still runs on an older host, without what the older host lacks.
//
// Version 3 is ambiguous: two branches each shipped a v3 host with a different layout after
// `controllers`. The controller-aim branch had {view, setHandAim}; the review-fix branch (commit
// f95d01d) had {u32 controllerStateSize, setProcessEventCallback}. Version 4 is their merge. So a
// mod reads nothing past `controllers` from a v3 host: `view` and `setHandAim` only from v4 on.
inline constexpr int kHostServicesVersion = 7;

using ProcessEventCallback = void (*)(UObject* context, UFunction* function, void* parms, void* result);

struct HostServices {
    int version = kHostServicesVersion;
    // Null in the monolithic build, where host and mod share one log.
    void (*log)(char const* level, char const* message) = nullptr;
    // Non-null when the host runs the VR renderer itself (LE1: Halcyon's): the mod then does no
    // VR output, head tracking or XInput hooking of its own. Returns the latest motion controller
    // sample, published by the host's frame loop.
    void (*controllers)(vr::controllers::State* out) = nullptr;

    // Version 4: from here through setProcessEventCallback. (The two v3 layouts each had some of
    // these, not all at these offsets: from a v2 or v3 host the mod reads none of them and runs
    // without them.)
    // The host renderer's latest view, relative to the game camera (core/vr/HostView.hpp): what
    // maps the controllers into the world. Non-null with `controllers`.
    void (*view)(vr::host_view::State* out) = nullptr;
    // The mod aims weapons with the right controller (game/HandAim) or has stopped. While it
    // does and the right controller is tracked, the host's renderer stops aiming with the head
    // (Halcyon's on-foot combat head aim). The host forgets it when it unloads the mod.
    void (*setHandAim)(bool on) = nullptr;

    // sizeof(vr::controllers::State) and sizeof(vr::host_view::State) as the host was built: a
    // mod rebuilt against a changed State must not read the host's (hot reload only replaces the
    // mod).
    std::uint32_t controllerStateSize = 0;
    std::uint32_t viewStateSize = 0;
    // The host's UObject::ProcessEvent hook (installed at attach, never removed) runs this after
    // the original. Clearing it waits (bounded) for in-flight calls; if one doesn't leave in time,
    // the host keeps the mod DLL loaded. No wait once the process is exiting.
    void (*setProcessEventCallback)(ProcessEventCallback callback) = nullptr;

    // Version 5.
    // True once the process has begun exiting (the host's ExitProcess hook): shutdown must not
    // wait for other threads, which may never run again. The host won't unload the DLL then.
    bool (*processExiting)() = nullptr;
    // The mod's shutdown couldn't make unloading safe: a hook it couldn't remove, or a call still
    // inside one when it gave up waiting. The host then pins the DLL instead of freeing it.
    void (*keepLoaded)(char const* reason) = nullptr;

    // Version 6.
    // The weapon's aim ray (core/vr/AimRay.hpp), each tick: the host's renderer draws the reticle
    // along it. sizeof(vr::aim_ray::Ray) as the host was built; the mod only calls setAimRay when
    // it matches. Non-null with `controllers`.
    std::uint32_t aimRaySize = 0;
    void (*setAimRay)(vr::aim_ray::Ray const* ray) = nullptr;

    // Version 7.
    // The two-handed grip (game/support_grip), each tick: the left hand's distance from the held
    // gun's foregrip (meters; negative: no gun to hold), the acquire and release distances, and
    // gamepad::Options flags (kFlagLeftTrigger: the left trigger is the game's LT; kFlagSupportAims:
    // the grip holds LT). The host's pad mapping (gamepad::UpdateSupportGrip) takes hold on a new
    // left-grip squeeze within `acquire`: LT instead of LB, until the grip opens or the hand leaves
    // `release`. Forgotten (no grip, LT from the trigger) when not called for 250 ms. Non-null with
    // `controllers`.
    void (*setSupportGrip)(float distanceMeters, float acquireMeters, float releaseMeters, std::uint32_t flags) = nullptr;
    // Whether the pad mapping holds the support grip now.
    bool (*supportGripHeld)() = nullptr;
};

} // namespace mevr

#if defined(MEVR_MOD_DLL)
#define MEVR_MOD_API extern "C" __declspec(dllexport)
#else
#define MEVR_MOD_API extern "C"
#endif

// Installs the mod's hooks. Returning false means the mod stays inactive.
MEVR_MOD_API bool MevrModStartup(ISharedProxyInterface* spi, mevr::HostServices const* host);
// Called by the host every UGameEngine::Tick, after the engine's own tick.
MEVR_MOD_API void MevrModTick(float deltaSeconds);
// Render thread, before each IDXGISwapChain::Present. The host guarantees this isn't running
// when MevrModShutdown is called.
MEVR_MOD_API void MevrModOnPresent(IDXGISwapChain* swapChain);
// Removes every hook the mod installed and releases its XR/D3D resources. The host calls this
// before unloading the DLL; whatever couldn't be made safe to unload is reported through
// HostServices::keepLoaded, and the host then keeps the DLL loaded.
MEVR_MOD_API void MevrModShutdown();

using MevrModStartupFn = decltype(&MevrModStartup);
using MevrModTickFn = decltype(&MevrModTick);
using MevrModOnPresentFn = decltype(&MevrModOnPresent);
using MevrModShutdownFn = decltype(&MevrModShutdown);
