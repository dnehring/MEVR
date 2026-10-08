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

inline constexpr int kHostServicesVersion = 3;

using ProcessEventCallback = void (*)(UObject* context, UFunction* function, void* parms, void* result);

struct HostServices {
    int version = kHostServicesVersion;
    // Null in the monolithic build, where host and mod share one log.
    void (*log)(char const* level, char const* message) = nullptr;
    // Non-null when the host runs the VR renderer itself (LE1: Halcyon's): the mod then does no
    // VR output, head tracking or XInput hooking of its own. Returns the latest motion controller
    // sample, published by the host's frame loop.
    void (*controllers)(vr::controllers::State* out) = nullptr;
    // sizeof(vr::controllers::State) as the host was built: a mod rebuilt against a changed
    // State must not read the host's (hot reload only replaces the mod).
    std::uint32_t controllerStateSize = 0;
    // The host's UObject::ProcessEvent hook (installed at attach, never removed) runs this after
    // the original. Clearing it waits for in-flight calls.
    void (*setProcessEventCallback)(ProcessEventCallback callback) = nullptr;
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
// before unloading the DLL.
MEVR_MOD_API void MevrModShutdown();

using MevrModStartupFn = decltype(&MevrModStartup);
using MevrModTickFn = decltype(&MevrModTick);
using MevrModOnPresentFn = decltype(&MevrModOnPresent);
using MevrModShutdownFn = decltype(&MevrModShutdown);
