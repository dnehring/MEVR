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
namespace vr::floor {
struct State;
}
namespace vr::squad_menu {
struct View;
namespace wheel {
struct Panel;
}
}

// Fields are only ever appended: a mod reads the fields of every version up to the host's (and
// its own), so a newer mod still runs on an older host, without what the older host lacks.
//
// Version 3 is ambiguous: two branches each shipped a v3 host with a different layout after
// `controllers`. The controller-aim branch had {view, setHandAim}; the review-fix branch (commit
// f95d01d) had {u32 controllerStateSize, setProcessEventCallback}. Version 4 is their merge. So a
// mod reads nothing past `controllers` from a v3 host: `view` and `setHandAim` only from v4 on.
inline constexpr int kHostServicesVersion = 13;

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

    // Version 8.
    // The test loop (plugin/TestLoop): while statusWanted() is true (a snapshot is being taken), the
    // mod passes a JSON object with its state (arm IK, hand aim, the weapon, the support grip) each
    // tick; the snapshot's JSON carries it as "mod". Non-null with `controllers`.
    bool (*statusWanted)() = nullptr;
    void (*setStatus)(char const* json) = nullptr;

    // Version 9: the body holsters (game/holsters, research/19).
    // A vibration on one controller (0 left, 1 right) for `seconds` at `amplitude` (0..1), applied
    // on the host's next frame (a newer request for the same hand replaces a pending one). Any
    // thread. Non-null with `controllers`.
    void (*hapticPulse)(int hand, float seconds, float amplitude) = nullptr;
    // The pad mapping's right grip (setSupportGrip's gamepad::kFlagRightGripFree): the number of
    // squeezes it kept from the game (no RB, because they began at a holster) shifted up one bit, and
    // in bit 0 whether the current squeeze is one (gamepad::RightGripReport). Squeezes while Halcyon's
    // menu is open never count. Non-null with `controllers`.
    std::uint32_t (*rightGripFree)() = nullptr;

    // Version 10: the grip holds the gun, contact haptics, the floor (research/19).
    // sizeof(HostServices) as the host was built: the mod reads a v10+ field only if the host's struct
    // reaches past it (HostHas in mod/Mod.cpp), so a later host that appends more stays readable and
    // an earlier v10 build can't be over-read.
    std::uint32_t servicesSize = 0;
    // setSupportGrip's flags also take gamepad::kFlagRightGripHolds and the RB source
    // (gamepad::kRbSourceMask): a host older than v10 ignores them, so the mod mustn't make the right
    // grip "hold the gun" there (RB would stay on it).
    // A pulse at a frequency (Hz; 0 = the runtime's choice), like hapticPulse.
    void (*hapticPulseHz)(int hand, float seconds, float amplitude, float frequencyHz) = nullptr;
    // A continuous vibration at `amplitude` (0 stops it) and `frequencyHz`, for `seconds` from now
    // (at most 0.5): the mod asks again each tick to keep it going, so it stops by itself within that
    // if the mod stalls or unloads. Pulses interrupt it and it resumes after them (core/vr/Haptics).
    void (*hapticContinuous)(int hand, float amplitude, float frequencyHz, float seconds) = nullptr;
    // The head's height above the floor (core/vr/Floor.hpp), the latest frame's; the mod reads it only
    // if floorStateSize matches its own sizeof(vr::floor::State).
    std::uint32_t floorStateSize = 0;
    void (*floor)(vr::floor::State* out) = nullptr;

    // Version 11: the squad weapon menu (game/squad_weapons, docs/squad-weapons-design.md).
    // setSupportGrip's flags also take gamepad::kFlagSquadMenu (a v10 host ignores it): while set, the
    // sticks, A, B and the stick clicks are the menu's, and each stays kept from the game until released.
    // The menu to draw (core/vr/SquadMenu.hpp), each tick while it's open and once when it closes; the
    // host draws it at the left hand while it's fresh (250 ms). The mod calls it only if squadMenuSize
    // matches its own sizeof(vr::squad_menu::View).
    std::uint32_t squadMenuSize = 0;
    void (*setSquadMenu)(vr::squad_menu::View const* view) = nullptr;
    // The pad mapping's left grip (gamepad::LeftGripReport): the count of squeezes that began away from
    // the gun's foregrip (not the support grip) shifted up one bit, and in bit 0 whether the current
    // squeeze is one. Squeezes while Halcyon's menu is open aren't counted.
    std::uint32_t (*leftGripAway)() = nullptr;

    // Version 12: the game's own weapon wheel at the hand ([Squad] style = native,
    // docs/squad-weapons-design.md "Native wheel").
    // setSupportGrip's flags also take gamepad::kFlagNativeWheel, kFlagWheelPressA and the wheel stick
    // (kWheelStickMask): while the flag is set the left grip holds the game's wheel open, one stick steers
    // it, A queues (plus the mod's own A), the rest is kept from the game until released.
    // The wheel panel's settings (core/vr/SquadMenu.hpp, squad_menu::wheel::Panel), each tick: while the
    // game's weapon or power wheel is up, the host shows its part of the UI on a panel of its own at a hand
    // (cut out of the HUD panel). The mod calls it only if wheelPanelSize matches its sizeof(Panel).
    std::uint32_t wheelPanelSize = 0;
    void (*setWheelPanel)(vr::squad_menu::wheel::Panel const* panel) = nullptr;

    // Version 13: physical melee and grenades (game/throwables, research/20). No new fields:
    // setSupportGrip's flags also take gamepad::kFlagNoMeleeButton and kFlagNoGrenadeButton (B and X
    // kept from the game on foot); a v12 host ignores them, so the buttons stay.
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
