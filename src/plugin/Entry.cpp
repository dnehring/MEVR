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
#include "engine/SettingsFile.hpp"
#include "loader/SpiAbi.hpp"
#include "plugin/CrashBreadcrumbs.hpp"
#include "plugin/ModLoader.hpp"
#include "plugin/PresentHook.hpp"
#include "plugin/ProcessEventHook.hpp"

#if defined(MEVR_WITH_HALCYON) || ((defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)) && defined(MEVR_WITH_MAIN_MENU))
#include "plugin/GamePointer.hpp"
#endif
#if defined(MEVR_WITH_HALCYON)
#include "plugin/OpenAlSoft.hpp"
#include "plugin/UiWorldFigure.hpp"
#include "plugin/GameThreadCalls.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/KeptLocks.hpp"
#include "plugin/MotionBlurGuard.hpp"
#endif
#if defined(MEVR_WITH_PERF_PROBE)
#include "plugin/PerfProbe.hpp"      // the frame-breakdown probe and the render levers: one module each for all three
#include "plugin/RenderLevers.hpp"   // games (research/sequel-render-levers.md "Shared module")
#endif
#if defined(MEVR_WITH_HALCYON_LE2)
#include "plugin/GameThreadCalls.hpp"
#include "plugin/HalcyonLe2.hpp"
#include "plugin/MotionBlurGuard.hpp"
#endif
#if defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
#include "halcyon/MevrSeams.h"  // HostEndsProcess, below
#include "plugin/KeptLocks.hpp"
#endif
#if defined(MEVR_WITH_HALCYON_LE3)
#include "plugin/MotionBlurGuard.hpp"
#include "plugin/GameThreadCalls.hpp"
#include "plugin/HalcyonLe3.hpp"
#include "plugin/FlareWindow.hpp"
#include "plugin/Le3SceneCapture.hpp"
#endif
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/SetupRequest.hpp"
#endif
#include "mevr/BuildInfo.hpp"  // MEVR_GIT_COMMIT (tools/cmake/BuildInfo.cmake): the attach line, read by problem reports
#include "mevr/Version.hpp"    // MEVR_VERSION: project(MEVR VERSION ...) in CMakeLists.txt (tools/cmake/Version.hpp.in)

#include <Windows.h>

#define MEVR_NAME_W L"MEVR"

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
bool gEngineProbe = false;

void GameEngineTickHook(void* self, float deltaSeconds) {
    if (gEngineProbe) {
        mevr::host::crash::NoteGameThread();
        gGameEngineTickOrig(self, deltaSeconds);
        mevr::host::Tick(deltaSeconds);
        return;
    }
#if defined(MEVR_WITH_PERF_PROBE)
    mevr::host::perf::GameTickBegin();
#endif
    mevr::host::crash::NoteGameThread();
#if defined(MEVR_WITH_PERF_PROBE)
    mevr::host::perf::QueueDrainBegin();
#endif
#if defined(MEVR_WITH_HALCYON)
    // Halcyon's ProcessEvent calls from the Present thread, queued for here (script on the game thread only).
    mevr::host::game_thread::Drain();
#endif
#if defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
    mevr::host::game_thread::Drain();  // ME2's renderer's Present-thread ProcessEvent calls, queued for here (as LE1's)
#endif
#if defined(MEVR_WITH_PERF_PROBE)
    mevr::host::perf::EngineTickBegin();
#endif
    gGameEngineTickOrig(self, deltaSeconds);
#if defined(MEVR_WITH_PERF_PROBE)
    mevr::host::perf::EngineTickEnd();
#endif
#if defined(MEVR_WITH_PERF_PROBE) && !defined(MEVR_WITH_HALCYON)
    // The sequels: the probe's requests and captures and the render levers' settings (LE1: halcyon::Tick calls perf::Tick,
    // and render_levers::Tick is the line after it).
    mevr::host::perf::Tick();
    mevr::host::render_levers::Tick();
#endif
#if defined(MEVR_WITH_HALCYON)
    mevr::host::halcyon::Tick(deltaSeconds);
    mevr::host::render_levers::Tick();
    mevr::host::motion_blur_guard::Tick();
#endif
#if defined(MEVR_WITH_HALCYON_LE2)
    mevr::host::halcyon_le2::AfterTick(deltaSeconds);
    mevr::host::motion_blur_guard::Tick();
#endif
#if defined(MEVR_WITH_HALCYON_LE3)
    mevr::host::halcyon_le3::AfterTick(deltaSeconds);
    mevr::host::motion_blur_guard::Tick();
    mevr::host::flare_window::Tick();
#endif
    mevr::host::Tick(deltaSeconds);
    mevr::host::process_event::ReportOffThread();
#if defined(MEVR_WITH_PERF_PROBE)
    mevr::host::perf::HostTickEnd();
#endif
}

// The game's exit. By the time it calls ExitProcess it has torn itself down (UObjects purged, its critical
// sections deleted) while its worker threads still run: one of them enters a deleted lock (exe+0x1BA22C0, an AV at
// ntdll+0x1A12D writing 0x24) if the process lives on long enough after the teardown. The real ExitProcess, like
// TerminateProcess, kills those threads first; every millisecond this hook spends before that is a window for the
// fault (research/36 "The fault at quit", docs/knowledge/gotchas/quit-time-fault-in-a-deleted-lock.md):
// - 2026-10-25: the mod's shutdown here (unhooking freezes and thaws every thread, it read engine memory) crashed
//   every quit, 27 ms after "game exiting". Never shut the mod down or unhook in this hook.
// - 2026-11-05 to 11-13: SetProcessExiting stopped the voice recognizer and the model tier (thread joins, the
//   capture's stop), and every quit crashed again, 2-27 ms after "game exiting", always while the hook was still in
//   those joins (it never reached its next line). Nothing that waits here.
// Even a 0.1 ms hook lost the race now and then (the worker faults within milliseconds of the deletion, before or as
// ExitProcess is called), so the lock itself is kept: plugin/KeptLocks skips the deletion of the exe's static critical
// sections (the lock is a thread library's global mutex whose destroy leaves its "initialized" byte set).
// So the hook does only what can't wait and touches nothing of the game's, then ends the process itself with
// TerminateProcess (as Halcyon's own ExitProcess hook, [EXITKILL], under ours does), so the XR runtime's DLLs are never
// unloaded with a live session and no DLL detach runs. halcyon::Shutdown only tells its threads. A fault that still
// comes is recorded apart (crash::NoteProcessExit: crash-exit.txt, no crash offer). The log isn't closed: every line
// is flushed as written, and a fault in the last moment still gets its line.
using tExitProcess = void(WINAPI)(UINT exitCode);
tExitProcess* gExitProcessOrig = nullptr;

void WINAPI ExitProcessHook(UINT exitCode) {
    LARGE_INTEGER frequency{}, begin{}, end{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    // Before anything else: a fault from here on is the quit's, not a crash of play.
    mevr::host::crash::NoteProcessExit(exitCode);
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
    QueryPerformanceCounter(&end);
    mevr::log::Info("game exiting (code {}); the hook's work took {:.3f} ms; the process ends now{}", exitCode,
        1000.0 * static_cast<double>(end.QuadPart - begin.QuadPart) / static_cast<double>(frequency.QuadPart),
#if (defined(MEVR_WITH_HALCYON) && defined(MEVR_GAME_LE1)) || defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
        "; kept locks: " + mevr::host::kept_locks::Summary());
#else
        "");
#endif
#if defined(MEVR_WITH_HALCYON) || defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
    TerminateProcess(GetCurrentProcess(), exitCode);
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

#if defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
// The sequels' renderers hook ExitProcess after us ([EXITKILL], on top of ours) and used to terminate before our hook ran:
// no exit line, no kept locks' summary, no NoteProcessExit. They hand us the call when our hook is in.
bool MevrSeams::HostEndsProcess() noexcept {
    return gExitProcessOrig != nullptr;
}
#endif

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
#if !defined(MEVR_LE2_DIAGNOSTIC)
    mevr::engine::code_dump::SaveOnce();
#if !defined(MEVR_MONOLITHIC)
    // Development-only diagnostic launch: dump before any renderer, tick or mod hooks.
    if (GetPrivateProfileIntW(L"Debug", L"dumpOnly", 0, mevr::engine::SettingsFile().c_str()) != 0) {
        mevr::log::Info("dump-only: code dump attempted; renderer and hooks disabled");
        return true;
    }
#endif
#endif
    // Hook names: "<owner>_<name>", as before stage 4 ("LE1MEVR_UGameEngine::Tick").
    static mevr::hook::Context init{InterfacePtr, MEVR_GAME_NAME_A "MEVR"};
#if defined(MEVR_WITH_HALCYON) && defined(MEVR_GAME_LE1)
    // Before the game opens its audio device: its OpenAL to OpenAL Soft (research/69 "Real 3D audio"). Quick (a library
    // load and a few dozen detours), so Halcyon's dxgi hooks still come before the game makes its device.
    mevr::host::openal_soft::Attach(init);
#endif
#if defined(MEVR_WITH_HALCYON)
    // Before anything else: it has to hook the system dxgi.dll before the game creates its device.
    mevr::host::halcyon::Attach();
#endif
#if defined(MEVR_WITH_HALCYON_LE2)
    // Halcyon's ME2 renderer, likewise first (plugin/HalcyonLe2).
    mevr::host::halcyon_le2::Attach();
#endif
#if defined(MEVR_WITH_HALCYON_LE3)
    gEngineProbe = mevr::engine::DevelopmentEngineProbe();
    if (!gEngineProbe) {
        mevr::host::le3_scene_capture::Attach(init);
        mevr::host::halcyon_le3::Attach();
    }
    else mevr::log::Info("engine-probe: host renderer disabled");
#endif
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
#if !defined(MEVR_WITH_HALCYON) && !defined(MEVR_WITH_HALCYON_LE2) && !defined(MEVR_WITH_HALCYON_LE3) && !defined(MEVR_LE2_DIAGNOSTIC)  // Halcyon's own, or none
    // Owned by the host so the render thread never runs code from an unloading mod DLL.
    if (!mevr::host::present::Install(init)) {
        mevr::log::Error("present hook failed; VR output disabled");
    }
#endif
    HookExitProcess(init);
#if defined(MEVR_WITH_HALCYON) || ((defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)) && defined(MEVR_WITH_MAIN_MENU))
    if (!gEngineProbe) mevr::host::game_pointer::InstallCursorHook(init);  // present the tracked pointer's position to the game's GUI
#endif
#if defined(MEVR_WITH_HALCYON) && defined(MEVR_GAME_LE1)
    mevr::host::kept_locks::Install(init);  // research/36 "The fault at quit": the game's teardown deletes a lock in use
    mevr::host::render_levers::Install(init);  // research/22: the render thread's levers and accounting
    mevr::host::motion_blur_guard::Install(init);
    mevr::host::ui_world_figure::Install(init);  // research/57 section 7: no Umbra query for the UI world's capture
#endif
#if defined(MEVR_WITH_HALCYON_LE2) || defined(MEVR_WITH_HALCYON_LE3)
    // As LE1's (research/36 "The fault at quit"): the game's static critical sections are never deleted, so a teardown that
    // destroys a lock its worker still takes can't fault the quit. No per-game address: any lock inside the exe's image.
    mevr::host::kept_locks::Install(init);
#endif
#if defined(MEVR_WITH_HALCYON_LE2)
    mevr::host::motion_blur_guard::Install(init);  // ME1's and ME3's guard (research/le2-motion-blur-guard.md)
#endif
#if defined(MEVR_WITH_HALCYON_LE3)
    if (!gEngineProbe) mevr::host::motion_blur_guard::Install(init);
    if (!gEngineProbe) mevr::host::flare_window::Install(init);  // research/30 section 9.6: lens flares in mode 2
#endif
#if defined(MEVR_WITH_PERF_PROBE) && !defined(MEVR_WITH_HALCYON)
    // The sequels: ME1's render levers, each by what this game's rows of engine/Addresses.hpp have (plugin/RenderLeversTable);
    // a lever without its rows stands down with one log line.
    if (!gEngineProbe) mevr::host::render_levers::Install(init);
#endif
    mevr::log::Info("host attached");
    return true;
}

extern "C" __declspec(dllexport) bool SpiOnDetach(spi::Interface* InterfacePtr) {
    (void)InterfacePtr;
    // LEBinkProxy detaches plugins as it unloads, i.e. at process exit: treat it like ExitProcess (no mod shutdown:
    // the game is torn down and the loader lock may be held, see ExitProcessHook).
    mevr::host::crash::NoteProcessExit(0);
    mevr::host::SetProcessExiting();
    mevr::host::StopWorkersForUnload();  // the game's threads are gone by now: nothing races these joins
#if defined(MEVR_WITH_HALCYON)
    mevr::host::halcyon::Shutdown();
#endif
    mevr::log::Info("detaching");
    mevr::log::Close();
    return true;
}
