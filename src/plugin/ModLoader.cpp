// SPDX-License-Identifier: MIT
#include "plugin/ModLoader.hpp"

#include "core/Log.hpp"
#include "mod/ModApi.hpp"
#include "plugin/PresentHook.hpp"
#include "plugin/ProcessEventHook.hpp"

#if defined(MEVR_WITH_HALCYON)
#include "core/vr/Controllers.hpp"
#include "core/vr/AimRay.hpp"
#include "core/vr/Calibration.hpp"
#include "core/vr/Floor.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/Sights.hpp"
#include "core/vr/SquadMenu.hpp"
#include "plugin/Calibration.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/Vignette.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/MainMenu.hpp"
#endif
#endif

#include <LESDK/Headers.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

namespace mevr::host {
namespace {

std::atomic<bool> gProcessExiting{false};
// Why the current mod generation must stay loaded when it's unloaded, if it must.
std::string gKeepReason;

void KeepModLoaded(char const* reason) {
    log::Error("the mod's code must stay loaded: {}", reason != nullptr ? reason : "(no reason given)");
    if (gKeepReason.empty()) {
        gKeepReason = reason != nullptr ? reason : "unknown";
    }
}

#if defined(MEVR_MONOLITHIC)
constexpr auto* kHostLog = static_cast<void (*)(char const*, char const*)>(nullptr);  // one log already
#else
void HostLog(char const* level, char const* message) {
    log::Write(level, message);
}
constexpr auto* kHostLog = &HostLog;
#endif

// The mod's ProcessEvent callback. Clearing it waits for calls still inside it, except once the
// process is exiting; a call that doesn't leave in time keeps the mod loaded.
void HostSetProcessEventCallback(ProcessEventCallback callback) {
    bool const exiting = gProcessExiting.load();
    if (!process_event::SetCallback(callback, exiting ? 0 : process_event::kDrainWaitMs) && !exiting) {
        KeepModLoaded("a thread was still inside the mod's ProcessEvent callback");
    }
}
bool HostProcessExiting() {
    return gProcessExiting.load();
}
void HostKeepLoaded(char const* reason) {
    KeepModLoaded(reason);
}

#if defined(MEVR_WITH_HALCYON)
void HostControllers(vr::controllers::State* out) {
    *out = vr::controllers::Latest(UINT64_MAX);  // raw sample: the mod ages it itself
}
void HostView(vr::host_view::State* out) {
    *out = vr::host_view::Latest(UINT64_MAX);  // raw sample: the mod ages it itself
}
void HostSetHandAim(bool on) {
    halcyon::SetModHandAim(on);
}
void HostSetAimRay(vr::aim_ray::Ray const* ray) {
    vr::aim_ray::Publish(ray != nullptr ? *ray : vr::aim_ray::Ray{});
}
void HostSetSupportGrip(float distanceMeters, float acquireMeters, float releaseMeters, std::uint32_t flags) {
    halcyon::SetSupportGrip(distanceMeters, acquireMeters, releaseMeters, flags);
}
bool HostSupportGripHeld() {
    return halcyon::SupportGripHeld();
}
bool HostStatusWanted() {
    return testloop::ModStatusWanted();
}
void HostSetStatus(char const* json) {
    testloop::SetModStatus(json);
}
void HostHapticPulse(int hand, float seconds, float amplitude) {
    halcyon::RequestHaptic(hand, seconds, amplitude);
}
std::uint32_t HostRightGripFree() {
    return halcyon::RightGripFree();
}
void HostHapticPulseHz(int hand, float seconds, float amplitude, float frequencyHz) {
    halcyon::RequestHaptic(hand, seconds, amplitude, frequencyHz);
}
void HostHapticContinuous(int hand, float amplitude, float frequencyHz, float seconds) {
    halcyon::RequestContinuousHaptic(hand, amplitude, frequencyHz, seconds);
}
void HostFloor(vr::floor::State* out) {
    halcyon::Floor(out);
}
void HostSetSquadMenu(vr::squad_menu::View const* view) {
    vr::squad_menu::Publish(view != nullptr ? *view : vr::squad_menu::View{});
}
std::uint32_t HostLeftGripAway() {
    return halcyon::LeftGripAway();
}
void HostSetWheelPanel(vr::squad_menu::wheel::Panel const* panel) {
    if (panel != nullptr) {
        vr::squad_menu::wheel::PublishPanel(*panel);
    } else {
        vr::squad_menu::wheel::ResetPanel();
    }
}
void HostSetSight(vr::sights::Sight const* sight) {
    if (sight != nullptr) {
        vr::sights::Publish(*sight);
    } else {
        vr::sights::Reset();
    }
}
std::uint32_t HostSightView() {
    return vr::sights::LatestView();
}
void HostCalibration(vr::calibration::HostState* out) {
    calibration::HostStateOut(out);
}
std::uint32_t HostMainMenu() {
#if defined(MEVR_WITH_MAIN_MENU)
    return main_menu::ModFlags();
#else
    return 0;
#endif
}
void HostSetVignette(float amount) {
    vignette::SetTarget(amount);
}
HostServices const gServices{kHostServicesVersion, kHostLog, &HostControllers, &HostView, &HostSetHandAim,
    static_cast<std::uint32_t>(sizeof(vr::controllers::State)), static_cast<std::uint32_t>(sizeof(vr::host_view::State)),
    &HostSetProcessEventCallback, &HostProcessExiting, &HostKeepLoaded,
    static_cast<std::uint32_t>(sizeof(vr::aim_ray::Ray)), &HostSetAimRay, &HostSetSupportGrip, &HostSupportGripHeld,
    &HostStatusWanted, &HostSetStatus, &HostHapticPulse, &HostRightGripFree,
    static_cast<std::uint32_t>(sizeof(HostServices)), &HostHapticPulseHz, &HostHapticContinuous,
    static_cast<std::uint32_t>(sizeof(vr::floor::State)), &HostFloor,
    static_cast<std::uint32_t>(sizeof(vr::squad_menu::View)), &HostSetSquadMenu, &HostLeftGripAway,
    static_cast<std::uint32_t>(sizeof(vr::squad_menu::wheel::Panel)), &HostSetWheelPanel,
    static_cast<std::uint32_t>(sizeof(vr::sights::Sight)), &HostSetSight, &HostSightView,
    static_cast<std::uint32_t>(sizeof(vr::calibration::HostState)), &HostCalibration, &HostMainMenu, &HostSetVignette};
// A mod that's gone can't aim: Halcyon's head aim must come back, the reticle go, the pad mapping
// forget the two-handed grip (the grip holding the gun, the squad menu's controls), the squad menu and the
// wheel panel settings go, and the controllers stop vibrating.
void ForgetModState() {
    halcyon::SetModHandAim(false);
    vr::aim_ray::Reset();
    vr::squad_menu::Reset();
    vr::squad_menu::wheel::ResetPanel();
    vr::sights::Reset();
    halcyon::ForgetSupportGrip();
    halcyon::ForgetHaptics();
    vignette::Forget();
}
#else
HostServices const gServices{kHostServicesVersion, kHostLog, nullptr, nullptr, nullptr, 0, 0, &HostSetProcessEventCallback,
    &HostProcessExiting, &HostKeepLoaded, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    static_cast<std::uint32_t>(sizeof(HostServices))};
void ForgetModState() {}
#endif
ISharedProxyInterface* gSpi = nullptr;

} // namespace

void SetProcessExiting() {
    gProcessExiting.store(true);
}

bool ProcessExiting() {
    return gProcessExiting.load();
}

#if defined(MEVR_MONOLITHIC)

namespace {
bool gModActive = false;
}

void Start(ISharedProxyInterface* spi) {
    gSpi = spi;
    gModActive = MevrModStartup(gSpi, &gServices);
    if (gModActive) {
        present::SetCallback(&MevrModOnPresent);
    }
}

void Tick(float deltaSeconds) {
    if (gModActive) {
        MevrModTick(deltaSeconds);
    }
}

void Stop() {
    if (gModActive) {
        present::SetCallback(nullptr);
        MevrModShutdown();
        gModActive = false;
    }
    ForgetModState();
    gKeepReason.clear();  // nothing to unload: host and mod are one module
}

#else // hot reload

namespace {

namespace fs = std::filesystem;

constexpr double kPollSeconds = 0.5;

struct LoadedMod {
    HMODULE module = nullptr;
    fs::path shadowPath;
    MevrModTickFn tick = nullptr;
    MevrModShutdownFn shutdown = nullptr;
};

LoadedMod gMod;
fs::path gSourcePath;
fs::path gShadowDir;
FILETIME gLoadedStamp{};
FILETIME gPendingStamp{};
bool gHavePending = false;
double gPollTimer = 0.0;
unsigned gGeneration = 0;

fs::path HostDirectory() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&HostDirectory), &self);
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(self, buffer, MAX_PATH);
    return fs::path(buffer).parent_path();
}

fs::path ResolveSourcePath() {
    fs::path const dir = HostDirectory();
    fs::path const pointer = dir / (SDK_TARGET_NAME_W L"MEVR.modpath");
    std::ifstream in(pointer);
    std::string line;
    if (in && std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty()) {
            return fs::path(std::u8string(line.begin(), line.end()));
        }
    }
    return dir / (SDK_TARGET_NAME_W L"MEVR_mod.dll");
}

bool ReadStamp(FILETIME& out) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(gSourcePath.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }
    out = data.ftLastWriteTime;
    return true;
}

bool SameStamp(FILETIME const& a, FILETIME const& b) {
    return CompareFileTime(&a, &b) == 0;
}

// Keeps a module mapped until the process ends, whatever FreeLibrary calls follow.
void Pin(HMODULE module) {
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(module), &pinned)) {
        log::Warn("pinning the old mod DLL failed (error {}); it stays loaded anyway (never freed)", GetLastError());
    }
}

void Unload() {
    if (gMod.module == nullptr) {
        return;
    }
    // Waits for the render thread to leave the mod's present callback.
    present::SetCallback(nullptr);
    if (gMod.shutdown != nullptr) {
        gMod.shutdown();
    }
    ForgetModState();
    // The mod clears its ProcessEvent callback in its shutdown; make sure (an older or half-started
    // mod), since nothing may call into the DLL once it's freed.
    HostSetProcessEventCallback(nullptr);
    if (gProcessExiting.load()) {
        // Windows unloads it with the process. Freeing it now could pull code out from under a
        // thread that's still in a hook and never gets to leave.
        log::Info("mod shut down for exit; its DLL stays mapped");
    } else if (!gKeepReason.empty()) {
        Pin(gMod.module);
        log::Error("mod generation {} STAYS LOADED (pinned, not unloaded): {}. Its hooks may still run its code; "
                   "restart the game when convenient", gGeneration, gKeepReason);
        // The shadow copy is locked while mapped; the next session's cleanup removes it.
    } else {
        FreeLibrary(gMod.module);
        std::error_code ec;
        fs::remove(gMod.shadowPath, ec);
    }
    gKeepReason.clear();
    gMod = {};
}

enum class LoadResult { Loaded, CopyFailed, Failed };

// Copies the DLL so the original stays writable for the next build, then loads the copy.
LoadResult Load() {
    fs::path const shadow = gShadowDir /
        std::format(L"{}MEVR_mod.{}.dll", SDK_TARGET_NAME_W, ++gGeneration);
    if (!CopyFileW(gSourcePath.c_str(), shadow.c_str(), FALSE)) {
        return LoadResult::CopyFailed;  // usually the linker still writing; retry next poll
    }

    HMODULE const module = LoadLibraryW(shadow.c_str());
    if (module == nullptr) {
        log::Error("LoadLibrary failed for {} (error {})", shadow.string(), GetLastError());
        return LoadResult::Failed;
    }
    auto const startup = reinterpret_cast<MevrModStartupFn>(GetProcAddress(module, "MevrModStartup"));
    auto const tick = reinterpret_cast<MevrModTickFn>(GetProcAddress(module, "MevrModTick"));
    auto const shutdown = reinterpret_cast<MevrModShutdownFn>(GetProcAddress(module, "MevrModShutdown"));
    auto const onPresent = reinterpret_cast<MevrModOnPresentFn>(GetProcAddress(module, "MevrModOnPresent"));
    if (startup == nullptr || tick == nullptr || shutdown == nullptr || onPresent == nullptr) {
        log::Error("mod DLL is missing its entry points");
        FreeLibrary(module);
        return LoadResult::Failed;
    }
    if (!startup(gSpi, &gServices)) {
        log::Error("mod startup failed; waiting for a new build");
        FreeLibrary(module);
        return LoadResult::Failed;
    }
    gMod = {module, shadow, tick, shutdown};
    present::SetCallback(onPresent);
    log::Info("mod generation {} loaded from {}", gGeneration, gSourcePath.string());
    return LoadResult::Loaded;
}

void CleanShadowDirectory() {
    std::error_code ec;
    fs::create_directories(gShadowDir, ec);
    for (auto const& entry : fs::directory_iterator(gShadowDir, ec)) {
        fs::remove(entry.path(), ec);  // leftovers from earlier sessions; locked ones are skipped
    }
}

void CheckForNewBuild() {
    FILETIME stamp{};
    if (!ReadStamp(stamp) || SameStamp(stamp, gLoadedStamp)) {
        gHavePending = false;
        return;
    }
    // Wait until the file has been unchanged for one poll so we don't copy a half-written DLL.
    if (!gHavePending || !SameStamp(stamp, gPendingStamp)) {
        gPendingStamp = stamp;
        gHavePending = true;
        return;
    }
    log::Info("new mod build detected; reloading");
    Unload();
    LoadResult const result = Load();
    if (result == LoadResult::CopyFailed) {
        return;  // stamp not recorded, so the next poll retries
    }
    gLoadedStamp = stamp;
    gHavePending = false;
}

} // namespace

void Start(ISharedProxyInterface* spi) {
    gSpi = spi;
    gSourcePath = ResolveSourcePath();

    wchar_t localAppData[MAX_PATH]{};
    GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    gShadowDir = fs::path(localAppData) / L"MEVR" / L"shadow";
    CleanShadowDirectory();

    log::Info("hot reload: watching {}", gSourcePath.string());
    FILETIME stamp{};
    if (!ReadStamp(stamp)) {
        log::Warn("mod DLL not found yet; it will load when it appears");
        return;
    }
    if (Load() != LoadResult::CopyFailed) {
        gLoadedStamp = stamp;
    }
}

void Tick(float deltaSeconds) {
    gPollTimer += deltaSeconds;
    if (gPollTimer >= kPollSeconds) {
        gPollTimer = 0.0;
        CheckForNewBuild();
    }
    if (gMod.tick != nullptr) {
        gMod.tick(deltaSeconds);
    }
}

void Stop() {
    Unload();
}

#endif

} // namespace mevr::host
