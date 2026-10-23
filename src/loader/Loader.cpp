// bink2w64.dll: MEVR's own ASI loader (research/33). GPL-3.0, new code: it doesn't use or copy ME3Tweaks' LEBinkProxy.
//
// The game imports RAD's Bink video DLL by the name bink2w64.dll. The installer renames RAD's file to
// bink2w64_original.dll and puts this DLL in its place: every Bink export is forwarded to the original (Forwarders.hpp),
// so video calls never pass through our code. When the game loads us we start one thread that:
//   1. loads every Binaries\Win64\ASI\*.asi (by name order);
//   2. for plugins that declare the SPI interface (SpiAbi.hpp: what LExSDKv2-based ASIs such as LE1MEVR.asi expect),
//      attaches the "preload" ones at once;
//   3. waits until the EA DRM has decrypted the exe's code (the exe has an .ooa section and its .text on disk is
//      encrypted: we compare samples of the file with memory, LoaderCore::LooksDecrypted), at most 60 s;
//   4. attaches the rest, each on a thread of its own when it asks for one.
// Plugins hook through us (InstallHook / UninstallHook: MinHook) and find byte patterns in the exe (FindPattern).
// Plain ASIs without SPI exports are only loaded (their DllMain does the rest). The log is
// Binaries\Win64\Logs\MEVRLoader.log.

#include "loader/Forwarders.hpp"
#include "loader/LoaderCore.hpp"
#include "loader/SpiAbi.hpp"

#include <MinHook.h>
#include <Windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace mevr::loader;

// ---- Log ------------------------------------------------------------------------------------------------------------
std::mutex gLogMutex;
FILE* gLog = nullptr;

void Log(char const* format, ...) {
    std::lock_guard const lock(gLogMutex);
    if (gLog == nullptr) {
        return;
    }
    SYSTEMTIME t{};
    GetLocalTime(&t);
    std::fprintf(gLog, "%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, format);
    std::vfprintf(gLog, format, args);
    va_end(args);
    std::fputc('\n', gLog);
    std::fflush(gLog);
}

std::string Narrow(std::wstring const& w) {
    if (w.empty()) {
        return {};
    }
    int const n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

fs::path ExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path();
}

std::wstring ExeName() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).filename().wstring();
}

// ---- The SPI implementation -------------------------------------------------------------------------------------
class SharedInterface final : public spi::Interface {
public:
    explicit SharedInterface(std::optional<spi::Game> host) : host_(host) {}

    spi::Return GetVersion(unsigned long* out) override {
        if (out == nullptr) {
            return spi::Return::FailureInvalidParam;
        }
        *out = spi::kVersion;
        return spi::Return::Success;
    }
    spi::Return GetBuildMode(bool* out) override {
        if (out == nullptr) {
            return spi::Return::FailureInvalidParam;
        }
        *out = true;
        return spi::Return::Success;
    }
    spi::Return GetHostGame(spi::Game* out) override {
        if (out == nullptr) {
            return spi::Return::FailureInvalidParam;
        }
        *out = host_.value_or(spi::Game::Launcher);
        return host_ ? spi::Return::Success : spi::Return::FailureGeneric;
    }
    spi::Return FindPattern(void** out, char* text) override {
        if (out == nullptr || text == nullptr) {
            return spi::Return::FailureInvalidParam;
        }
        *out = nullptr;
        bool tooLong = false;
        auto const pattern = ParsePattern(text, &tooLong);
        if (!pattern) {
            return tooLong ? spi::Return::FailurePatternTooLong : spi::Return::FailurePatternInvalid;
        }
        auto* const base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        auto const* const dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
        auto const* const nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(base + dos->e_lfanew);
        IMAGE_SECTION_HEADER const* section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
                continue;
            }
            std::span<std::uint8_t const> const memory(base + section->VirtualAddress, section->Misc.VirtualSize);
            if (auto const at = mevr::loader::FindPattern(memory, *pattern)) {
                *out = base + section->VirtualAddress + *at;
                return spi::Return::Success;
            }
        }
        return spi::Return::FailureGeneric;
    }
    spi::Return InstallHook(char const* name, void* target, void* detour, void** original) override {
        if (name == nullptr || target == nullptr || detour == nullptr || original == nullptr) {
            return spi::Return::FailureInvalidParam;
        }
        std::lock_guard const lock(mutex_);
        if (hooks_.contains(name)) {
            Log("hook %s: a hook of that name exists", name);
            return spi::Return::FailureDuplicacy;
        }
        MH_STATUS status = MH_CreateHook(target, detour, original);
        if (status != MH_OK) {
            Log("hook %s: MH_CreateHook %s", name, MH_StatusToString(status));
            return spi::Return::FailureHooking;
        }
        status = MH_EnableHook(target);
        if (status != MH_OK) {
            Log("hook %s: MH_EnableHook %s", name, MH_StatusToString(status));
            MH_RemoveHook(target);
            return spi::Return::FailureHooking;
        }
        hooks_[name] = target;
        Log("hook %s at %p", name, target);
        return spi::Return::Success;
    }
    spi::Return UninstallHook(char const* name) override {
        if (name == nullptr) {
            return spi::Return::FailureInvalidParam;
        }
        std::lock_guard const lock(mutex_);
        auto const it = hooks_.find(name);
        if (it == hooks_.end()) {
            return spi::Return::FailureDuplicacy;
        }
        MH_STATUS const disabled = MH_DisableHook(it->second);
        MH_STATUS const removed = MH_RemoveHook(it->second);
        if (disabled != MH_OK || removed != MH_OK) {
            Log("unhook %s: %s / %s", name, MH_StatusToString(disabled), MH_StatusToString(removed));
            return spi::Return::FailureHooking;
        }
        hooks_.erase(it);
        Log("unhooked %s", name);
        return spi::Return::Success;
    }

private:
    std::optional<spi::Game> host_;
    std::mutex mutex_;
    std::map<std::string, void*> hooks_;
};

struct Plugin {
    std::wstring name;
    HMODULE module = nullptr;
    bool spi = false;
    bool preload = false;
    bool ownThread = false;
    bool attach = false;
    bool attached = false;
    spi::AttachPoint onAttach = nullptr;
    spi::AttachPoint onDetach = nullptr;
};

SharedInterface* gInterface = nullptr;
std::vector<Plugin> gPlugins;
std::mutex gPluginsMutex;

void Attach(Plugin& plugin) {
    if (plugin.onAttach == nullptr) {
        return;
    }
    Log("attaching %s", Narrow(plugin.name).c_str());
    bool const ok = plugin.onAttach(gInterface);
    {
        std::lock_guard const lock(gPluginsMutex);
        plugin.attached = true;
    }
    Log("%s attached%s", Narrow(plugin.name).c_str(), ok ? "" : " (it reported a failure)");
}

DWORD WINAPI AttachThread(void* parameter) {
    Attach(*static_cast<Plugin*>(parameter));
    return 0;
}

void AttachAll(bool preload) {
    for (Plugin& plugin : gPlugins) {
        if (!plugin.spi || !plugin.attach || plugin.preload != preload) {
            continue;
        }
        if (plugin.ownThread) {
            HANDLE const thread = CreateThread(nullptr, 0, &AttachThread, &plugin, 0, nullptr);
            if (thread != nullptr) {
                CloseHandle(thread);
                continue;
            }
        }
        Attach(plugin);
    }
}

// The .text of the exe on disk vs in memory (research/33): done once most samples differ.
bool WaitForDecryption(DWORD timeoutMs) {
    auto* const base = reinterpret_cast<std::uint8_t const*>(GetModuleHandleW(nullptr));
    auto const* const dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    auto const* const nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER const* text = nullptr;
    bool protectedExe = false;
    IMAGE_SECTION_HEADER const* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        char name[9]{};
        std::memcpy(name, section->Name, 8);
        if (std::strcmp(name, ".text") == 0) {
            text = section;
        } else if (std::strcmp(name, ".ooa") == 0) {
            protectedExe = true;  // EA's DRM section
        }
    }
    if (!protectedExe || text == nullptr) {
        Log("no DRM section: not waiting");
        return true;
    }
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::ifstream file(path, std::ios::binary);
    std::size_t const size = std::min<std::size_t>(text->SizeOfRawData, text->Misc.VirtualSize);
    constexpr std::size_t kWindow = 16;
    constexpr int kSamples = 256;
    std::vector<std::size_t> const offsets = SampleOffsets(size, kSamples, kWindow);
    std::vector<std::uint8_t> disk(offsets.size() * kWindow);
    for (std::size_t i = 0; i < offsets.size() && file; ++i) {
        file.seekg(static_cast<std::streamoff>(text->PointerToRawData + offsets[i]));
        file.read(reinterpret_cast<char*>(&disk[i * kWindow]), kWindow);
    }
    if (!file) {
        Log("couldn't read the exe's .text from disk: not waiting");
        return true;
    }
    ULONGLONG const start = GetTickCount64();
    int polls = 0;
    for (;;) {
        int differing = 0;
        for (std::size_t i = 0; i < offsets.size(); ++i) {
            std::uint8_t now[kWindow]{};
            SIZE_T read = 0;
            // ReadProcessMemory on ourselves: a page the DRM hasn't made readable yet fails instead of faulting.
            if (ReadProcessMemory(GetCurrentProcess(), base + text->VirtualAddress + offsets[i], now, kWindow, &read) && read == kWindow &&
                std::memcmp(now, &disk[i * kWindow], kWindow) != 0) {
                ++differing;
            }
        }
        ++polls;
        if (LooksDecrypted(differing, static_cast<int>(offsets.size()))) {
            Log("the exe's code is decrypted (%d of %zu samples differ from the file; %d polls, %llu ms)", differing, offsets.size(), polls,
                GetTickCount64() - start);
            return true;
        }
        if (GetTickCount64() - start > timeoutMs) {
            Log("the exe's code still looks encrypted after %lu ms (%d of %zu samples differ): attaching anyway", timeoutMs, differing,
                offsets.size());
            return false;
        }
        Sleep(polls < 200 ? 1 : 10);
    }
}

DWORD WINAPI Run(void*) {
    fs::path const exeDir = ExeDir();
    std::optional<spi::Game> const host = HostFromExeName(ExeName());
    {
        std::error_code ec;
        fs::path logDir = exeDir / L"Logs";
        fs::create_directories(logDir, ec);
        if (ec) {
            logDir = exeDir;
        }
        std::lock_guard const lock(gLogMutex);
        _wfopen_s(&gLog, (logDir / L"MEVRLoader.log").c_str(), L"w");
    }
    Log("MEVR ASI loader (bink2w64.dll proxy, SPI %lu) in %s", spi::kVersion, Narrow(ExeName()).c_str());
    if (host == spi::Game::Launcher) {
        Log("the launcher: no ASIs here");
        return 0;
    }
    MH_STATUS const mh = MH_Initialize();
    if (mh != MH_OK) {
        Log("MinHook didn't initialise (%s): plugins can't hook", MH_StatusToString(mh));
    }
    gInterface = new SharedInterface(host);

    fs::path const asiDir = exeDir / L"ASI";
    std::vector<std::wstring> names;
    std::error_code ec;
    for (auto const& entry : fs::directory_iterator(asiDir, ec)) {
        if (entry.is_regular_file(ec) && _wcsicmp(entry.path().extension().c_str(), L".asi") == 0) {
            names.push_back(entry.path().filename().wstring());
        }
    }
    if (ec) {
        Log("no ASI folder (%s)", Narrow(asiDir.wstring()).c_str());
    }
    for (std::wstring const& name : SortAsiNames(names)) {
        Plugin plugin;
        plugin.name = name;
        plugin.module = LoadLibraryW((asiDir / name).c_str());
        if (plugin.module == nullptr) {
            Log("%s didn't load (error %lu)", Narrow(name).c_str(), GetLastError());
            continue;
        }
        auto const decl = reinterpret_cast<spi::SupportDecl>(GetProcAddress(plugin.module, "SpiSupportDecl"));
        plugin.onAttach = reinterpret_cast<spi::AttachPoint>(GetProcAddress(plugin.module, "SpiOnAttach"));
        plugin.onDetach = reinterpret_cast<spi::AttachPoint>(GetProcAddress(plugin.module, "SpiOnDetach"));
        if (decl == nullptr || plugin.onAttach == nullptr) {
            Log("loaded %s (a plain ASI)", Narrow(name).c_str());
            gPlugins.push_back(plugin);
            continue;
        }
        wchar_t* pluginName = nullptr;
        wchar_t* author = nullptr;
        wchar_t* version = nullptr;
        int flags = 0;
        int minVersion = 0;
        decl(&pluginName, &author, &version, &flags, &minVersion);
        auto const preload = reinterpret_cast<spi::ShouldFlag>(GetProcAddress(plugin.module, "SpiShouldPreload"));
        auto const ownThread = reinterpret_cast<spi::ShouldFlag>(GetProcAddress(plugin.module, "SpiShouldSpawnThread"));
        plugin.spi = true;
        plugin.preload = preload != nullptr && preload();
        plugin.ownThread = ownThread != nullptr && ownThread();
        plugin.attach = ShouldAttach(flags, minVersion, host);
        Log("loaded %s: \"%s\" %s by %s (games 0x%x, SPI %d+, %s, %s)%s", Narrow(name).c_str(), pluginName ? Narrow(pluginName).c_str() : "",
            version ? Narrow(version).c_str() : "", author ? Narrow(author).c_str() : "", flags, minVersion,
            plugin.preload ? "before the DRM" : "after the DRM", plugin.ownThread ? "own thread" : "in order",
            plugin.attach ? "" : ": not for this game or SPI version, not attached");
        gPlugins.push_back(plugin);
    }
    AttachAll(true);
    if (host) {
        WaitForDecryption(60000);
    }
    AttachAll(false);
    Log("done: %zu ASI(s)", gPlugins.size());
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(instance);
        // The work runs once the loader lock is released (the thread starts after DllMain returns).
        HANDLE const thread = CreateThread(nullptr, 0, &Run, nullptr, 0, nullptr);
        if (thread != nullptr) {
            CloseHandle(thread);
        }
        break;
    }
    case DLL_PROCESS_DETACH: {
        // As LEBinkProxy does (our host treats it as the process exiting; it has usually shut down in its
        // ExitProcess hook already).
        std::vector<Plugin> attached;
        {
            std::lock_guard const lock(gPluginsMutex);
            for (Plugin const& p : gPlugins) {
                if (p.attached && p.onDetach != nullptr) {
                    attached.push_back(p);
                }
            }
        }
        for (Plugin const& p : attached) {
            p.onDetach(gInterface);
        }
        Log("detached");
        std::lock_guard const lock(gLogMutex);
        if (gLog != nullptr) {
            std::fclose(gLog);
            gLog = nullptr;
        }
        break;
    }
    default:
        break;
    }
    return TRUE;
}
