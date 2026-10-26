// SPDX-License-Identifier: MIT
// ASI entry point, loaded through the SPI interface by our own loader (src/loader, installed by MEVR-Setup) or by
// ME3Tweaks' LEBinkProxy.
// The host stays small and stable: the exe gate, the engine tick hook, and the mod loader.
//
// The plugin side of the SPI (research/33, 34 stage 4) is ours: the five exports below, with the names, signatures
// and values the loaders look up (loader/SpiAbi.hpp; tools/loader-test checks a plugin against the same interface).

#include "core/Exe.hpp"
#include "core/Hook.hpp"
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/Version.hpp"
#include "engine/Addresses.hpp"
#include "engine/CodeDump.hpp"
#include "engine/GameAdapter.hpp"
#include "loader/SpiAbi.hpp"
#include "plugin/CrashBreadcrumbs.hpp"
#include "plugin/ModLoader.hpp"
#include "plugin/PresentHook.hpp"
#include "plugin/ProcessEventHook.hpp"

#if defined(MEVR_WITH_HALCYON)
#include "plugin/GameThreadCalls.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/PerfProbe.hpp"
#include "plugin/RenderLevers.hpp"
#endif
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/SetupRequest.hpp"
#endif
#include "mevr/BuildInfo.hpp"  // MEVR_GIT_COMMIT (tools/cmake/BuildInfo.cmake): the attach line, read by problem reports

#include <Windows.h>

#define MEVR_NAME_W L"MEVR"
#define MEVR_VERSION "0.1.0"

namespace spi = mevr::loader::spi;

#if defined(MEVR_GAME_LE1)
constexpr spi::Game kSpiGame = spi::Game::LE1;
#elif defined(MEVR_GAME_LE2)
constexpr spi::Game kSpiGame = spi::Game::LE2;
#else
constexpr spi::Game kSpiGame = spi::Game::LE3;
#endif

// Who we are, for the loader: name, author, version, the game we're for, the oldest SPI we can use.
extern "C" __declspec(dllexport) void SpiSupportDecl(wchar_t** name, wchar_t** author, wchar_t** version, int* gameFlags,
    int* minVersion) {
    *name = const_cast<wchar_t*>(MEVR_GAME_NAME_W MEVR_NAME_W);
    *author = const_cast<wchar_t*>(L"ME VR Attempt contributors");
    *version = const_cast<wchar_t*>(L"" MEVR_VERSION);
    *gameFlags = spi::GameFlag(kSpiGame);
    *minVersion = static_cast<int>(spi::kVersion);
}
// Attach after the DRM has decrypted the exe: our RVAs point into decrypted code.
extern "C" __declspec(dllexport) bool SpiShouldPreload() {
    return false;
}
// On a thread of its own.
extern "C" __declspec(dllexport) bool SpiShouldSpawnThread() {
    return true;
}

namespace {

// Attach runs on the loader's thread while the game runs: every original is written before its
// hook goes live (core/Hook), so a detour entered at once never calls through null.
using tGameEngineTick = void(void* self, float deltaSeconds);  // UGameEngine::Tick
tGameEngineTick* gGameEngineTickOrig = nullptr;

void GameEngineTickHook(void* self, float deltaSeconds) {
    mevr::host::crash::NoteGameThread();
#if defined(MEVR_WITH_HALCYON)
    // Halcyon's ProcessEvent calls from the Present thread, queued for here (script on the game thread only).
    mevr::host::game_thread::Drain();
#endif
#if defined(MEVR_WITH_HALCYON)
    mevr::host::perf::EngineTickBegin();
#endif
    gGameEngineTickOrig(self, deltaSeconds);
#if defined(MEVR_WITH_HALCYON)
    mevr::host::perf::EngineTickEnd();
    mevr::host::halcyon::Tick(deltaSeconds);
    mevr::host::render_levers::Tick();
#endif
    mevr::host::Tick(deltaSeconds);
    mevr::host::process_event::ReportOffThread();
#if defined(MEVR_WITH_HALCYON)
    mevr::host::perf::HostTickEnd();
#endif
}

// The game's exit. By the time it calls ExitProcess it has torn itself down (UObjects purged, its critical
// sections deleted) while its worker threads still run; the real ExitProcess kills them first. So this hook does
// only what can't wait and touches nothing of the game's: it must not shut the mod down here (unhooking freezes and
// thaws every thread, the mod's shutdown reads engine memory, and a game worker meanwhile entered a deleted lock:
// the 2026-10-25 crashes on quit, both an AV at ntdll+0x1A12D writing 0x24 on exe+0x1BA22C0, 27 ms after "game
// exiting"). Halcyon's own ExitProcess hook ([EXITKILL]) ends the process with TerminateProcess, so the XR runtime's
// DLLs are never unloaded with a live session; halcyon::Shutdown only tells its threads.
using tExitProcess = void(WINAPI)(UINT exitCode);
tExitProcess* gExitProcessOrig = nullptr;

void WINAPI ExitProcessHook(UINT exitCode) {
    mevr::log::Info("game exiting (code {}); the mod stays loaded and the process ends", exitCode);
    // Nothing waits for other threads from here on, and the mod DLL stays mapped: the process is
    // going away, and a thread caught inside a hook may never run again.
    mevr::host::SetProcessExiting();
#if defined(MEVR_WITH_MAIN_MENU)
    // The menu's "Install after I quit" (research/33): MEVR Setup starts, waits for us to be gone.
    mevr::host::setup_request::LaunchIfRequested();
#endif
#if defined(MEVR_WITH_HALCYON)
    mevr::host::halcyon::Shutdown();
#endif
    mevr::log::Close();
    gExitProcessOrig(exitCode);
}

void HookExitProcess(mevr::hook::Context& init) {
    auto* const target = reinterpret_cast<tExitProcess*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitProcess"));
    if (target == nullptr || !mevr::hook::Install(init, "ExitProcess", target, &ExitProcessHook, gExitProcessOrig)) {
        mevr::log::Warn("ExitProcess hook failed; XR may not shut down cleanly on exit");
    }
}

// The exe gate (research/34 stage 4): the supported version, and every address of our RVA table where the table
// says (core/Exe). On another exe the table is still checked, so the log names each address a patch moved. Before
// any hook of ours or Halcyon's patches the code (other ASIs' hooks are recognised by their jumps).
bool ExeGate() {
    auto const version = mevr::GetHostExeVersion();
    bool const versionOk = version && *version == mevr::kSupportedGameVersion;
    mevr::exe::Report const report = mevr::exe::Init(mevr::engine::addresses::Table());
    if (!versionOk) {
        mevr::log::Error("unsupported game version {} (need {}); VR disabled, game untouched (our RVA table: {})",
            version ? version->ToString() : "unknown", mevr::kSupportedGameVersion.ToString(), report.summary);
        return false;
    }
    if (!report.ok) {
        mevr::log::Error("game version {}, but our RVA table doesn't match it ({}); VR disabled, game untouched",
            version->ToString(), report.problems);
        return false;
    }
    mevr::log::Info("game version {} OK; {}", version->ToString(), report.summary);
    return true;
}

} // namespace

extern "C" __declspec(dllexport) bool SpiOnAttach(spi::Interface* InterfacePtr) {
    mevr::log::Open(MEVR_GAME_NAME_W L"MEVR.log");
#if defined(MEVR_MONOLITHIC)
    mevr::log::Info("MEVR {} (monolithic, commit {}) attaching to {}", MEVR_VERSION, MEVR_GIT_COMMIT, MEVR_GAME_NAME_A);
#else
    mevr::log::Info("MEVR {} (hot reload host, commit {}) attaching to {}", MEVR_VERSION, MEVR_GIT_COMMIT, MEVR_GAME_NAME_A);
#endif
    mevr::log::Info("log file: {} (pid {})", mevr::ini::Utf8(mevr::log::Path()), GetCurrentProcessId());

    // Fail closed: hardcoded RVAs are only valid for one exe build.
    if (!ExeGate()) {
        return true;
    }
    // What touched the engine last, written out if the process crashes (research/36).
    mevr::host::crash::Install();
    // Research dump of the decrypted code, before our or Halcyon's hooks patch any of it.
    mevr::engine::code_dump::SaveOnce();
#if defined(MEVR_WITH_HALCYON)
    // Before anything else: it has to hook the system dxgi.dll before the game creates its device.
    mevr::host::halcyon::Attach();
#endif

    // Hook names: "<owner>_<name>", as before stage 4 ("LE1MEVR_UGameEngine::Tick").
    static mevr::hook::Context init{InterfacePtr, MEVR_GAME_NAME_A "MEVR"};
    if (InterfacePtr == nullptr) {
        mevr::log::Warn("no SPI interface from the loader: hooking with our own MinHook");
    }
    auto* const tickTarget = init.ResolveTyped<tGameEngineTick>(mevr::engine::addresses::kGameEngineTick);
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
#if defined(MEVR_WITH_HALCYON) && defined(MEVR_GAME_LE1)
    mevr::host::render_levers::Install(init);  // research/22: the render thread's levers and accounting
#endif
    mevr::log::Info("host attached");
    return true;
}

extern "C" __declspec(dllexport) bool SpiOnDetach(spi::Interface* InterfacePtr) {
    (void)InterfacePtr;
    // LEBinkProxy detaches plugins as it unloads, i.e. at process exit: treat it like ExitProcess (no mod shutdown:
    // the game is torn down and the loader lock may be held, see ExitProcessHook).
    mevr::host::SetProcessExiting();
#if defined(MEVR_WITH_HALCYON)
    mevr::host::halcyon::Shutdown();
#endif
    mevr::log::Info("detaching");
    mevr::log::Close();
    return true;
}
