// ASI entry point, loaded by ME3Tweaks' LEBinkProxy through its SPI interface.
// The host stays small and stable: version check, the engine tick hook, and the mod loader.

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "core/Version.hpp"
#include "engine/CodeDump.hpp"
#include "plugin/ModLoader.hpp"
#include "plugin/PresentHook.hpp"
#include "plugin/ProcessEventHook.hpp"

#if defined(MEVR_WITH_HALCYON)
#include "plugin/Halcyon.hpp"
#include "plugin/PerfProbe.hpp"
#endif

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>

#define MEVR_NAME_W L"MEVR"
#define MEVR_VERSION "0.1.0"

SPI_PLUGINSIDE_SUPPORT(SDK_TARGET_NAME_W MEVR_NAME_W, L"ME VR Attempt contributors", L"" MEVR_VERSION,
    SPI_GAME_SDK_TARGET, SPI_VERSION_ANY);
// Attach after the DRM has decrypted the exe; the SDK's RVAs point into decrypted code.
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_ASYNCATTACH;

namespace {

// Attach runs on LEBinkProxy's thread while the game runs: every original is written before its
// hook goes live (core/Hook), so a detour entered at once never calls through null.
using tGameEngineTick = void(UGameEngine* self, float deltaSeconds);
tGameEngineTick* gGameEngineTickOrig = nullptr;

void GameEngineTickHook(UGameEngine* self, float deltaSeconds) {
#if defined(MEVR_WITH_HALCYON)
    mevr::host::perf::EngineTickBegin();
#endif
    gGameEngineTickOrig(self, deltaSeconds);
#if defined(MEVR_WITH_HALCYON)
    mevr::host::perf::EngineTickEnd();
    mevr::host::halcyon::Tick(deltaSeconds);
#endif
    mevr::host::Tick(deltaSeconds);
#if defined(MEVR_WITH_HALCYON)
    mevr::host::perf::HostTickEnd();
#endif
}

// Close the XR session before Windows starts unloading DLLs: the Oculus runtime's client DLL
// fail-fasts (0xc0000409) if it's torn down with a live session.
using tExitProcess = void(WINAPI)(UINT exitCode);
tExitProcess* gExitProcessOrig = nullptr;

void WINAPI ExitProcessHook(UINT exitCode) {
    mevr::log::Info("game exiting (code {}); shutting the mod down first", exitCode);
    // Nothing waits for other threads from here on, and the mod DLL stays mapped: the process is
    // going away, and a thread caught inside a hook may never run again.
    mevr::host::SetProcessExiting();
#if defined(MEVR_WITH_HALCYON)
    mevr::host::halcyon::Shutdown();
#endif
    mevr::host::Stop();
    mevr::log::Close();
    gExitProcessOrig(exitCode);
}

void HookExitProcess(::LESDK::Initializer& init) {
    auto* const target = reinterpret_cast<tExitProcess*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitProcess"));
    if (target == nullptr || !mevr::hook::Install(init, "ExitProcess", target, &ExitProcessHook, gExitProcessOrig)) {
        mevr::log::Warn("ExitProcess hook failed; XR may not shut down cleanly on exit");
    }
}

} // namespace

SPI_IMPLEMENT_ATTACH
{
    mevr::log::Open(SDK_TARGET_NAME_W L"MEVR.log");
#if defined(MEVR_MONOLITHIC)
    mevr::log::Info("MEVR {} (monolithic) attaching to {}", MEVR_VERSION, SDK_TARGET_NAME_A);
#else
    mevr::log::Info("MEVR {} (hot reload host) attaching to {}", MEVR_VERSION, SDK_TARGET_NAME_A);
#endif
    mevr::log::Info("log file: {} (pid {})", mevr::log::Path().string(), GetCurrentProcessId());

    // Fail closed: hardcoded RVAs are only valid for one exe build.
    auto const version = mevr::GetHostExeVersion();
    if (!version || *version != mevr::kSupportedGameVersion) {
        mevr::log::Error("unsupported game version {} (need {}); VR disabled, game untouched",
            version ? version->ToString() : "unknown", mevr::kSupportedGameVersion.ToString());
        return true;
    }
    mevr::log::Info("game version {} OK", version->ToString());
    // Research dump of the decrypted code, before our or Halcyon's hooks patch any of it.
    mevr::engine::code_dump::SaveOnce();
#if defined(MEVR_WITH_HALCYON)
    // Before anything else: it has to hook the system dxgi.dll before the game creates its device.
    mevr::host::halcyon::Attach();
#endif

    static ::LESDK::Initializer init{InterfacePtr, SDK_TARGET_NAME_A "MEVR"};
    auto* const tickTarget = init.ResolveTyped<tGameEngineTick>(BUILTIN_GAMEENGINETICK_RVA);
    if (tickTarget == nullptr) {
        mevr::log::Error("failed to resolve UGameEngine::Tick; VR disabled");
        return true;
    }

    // Before Halcyon's renderer hooks it too (at its first frame): ours must sit underneath and
    // never be removed. The mod's handlers come and go through its callback.
    if (!mevr::host::process_event::Install(init)) {
        mevr::log::Error("ProcessEvent hook failed; script-level features off");
    }

    // Load the mod before the tick hook goes live, so its first tick sees a started mod.
    mevr::host::Start(InterfacePtr);

    if (!mevr::hook::Install(init, "UGameEngine::Tick", tickTarget, &GameEngineTickHook, gGameEngineTickOrig)) {
        mevr::log::Error("failed to hook UGameEngine::Tick; VR disabled");
        mevr::host::Stop();
        return true;
    }
#if !defined(MEVR_WITH_HALCYON)  // Halcyon's renderer hooks Present itself
    // Owned by the host so the render thread never runs code from an unloading mod DLL.
    if (!mevr::host::present::Install(init)) {
        mevr::log::Error("present hook failed; VR output disabled");
    }
#endif
    HookExitProcess(init);
    mevr::log::Info("host attached");
    return true;
}

SPI_IMPLEMENT_DETACH
{
    (void)InterfacePtr;
    // LEBinkProxy detaches plugins as it unloads, i.e. at process exit: treat it like ExitProcess.
    mevr::host::SetProcessExiting();
#if defined(MEVR_WITH_HALCYON)
    mevr::host::halcyon::Shutdown();
#endif
    mevr::host::Stop();
    mevr::log::Info("detaching");
    mevr::log::Close();
    return true;
}
