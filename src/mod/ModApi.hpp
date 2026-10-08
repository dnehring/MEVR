#pragma once

// ABI between the host ASI (stable, loaded by LEBinkProxy) and the mod, which is either linked
// into the host (monolithic release build) or a separate DLL the host hot-reloads (dev build).

class ISharedProxyInterface;
struct IDXGISwapChain;

namespace mevr {

inline constexpr int kHostServicesVersion = 1;

struct HostServices {
    int version = kHostServicesVersion;
    void (*log)(char const* level, char const* message) = nullptr;
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
