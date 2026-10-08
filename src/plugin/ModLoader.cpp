#include "plugin/ModLoader.hpp"

#include "core/Log.hpp"
#include "mod/ModApi.hpp"
#include "plugin/PresentHook.hpp"

#if defined(MEVR_WITH_HALCYON)
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "plugin/Halcyon.hpp"
#endif

#include <LESDK/Headers.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace mevr::host {
namespace {

void HostLog(char const* level, char const* message) {
    log::Write(level, message);
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
HostServices const gServices{kHostServicesVersion, &HostLog, &HostControllers, &HostView, &HostSetHandAim};
// A mod that's gone can't aim: Halcyon's head aim must come back.
void ForgetModState() {
    halcyon::SetModHandAim(false);
}
#else
HostServices const gServices{kHostServicesVersion, &HostLog, nullptr, nullptr, nullptr};
void ForgetModState() {}
#endif
ISharedProxyInterface* gSpi = nullptr;

} // namespace

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
    FreeLibrary(gMod.module);
    std::error_code ec;
    fs::remove(gMod.shadowPath, ec);
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
