// SPDX-License-Identifier: MIT
#include "plugin/OpenAlSoft.hpp"

#include "core/Hook.hpp"
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "engine/SettingsFile.hpp"

#include <MinHook.h>
#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace mevr::host::openal_soft {
namespace {

// ALC values (OpenAL's alc.h, OpenAL Soft's alext.h: ALC_SOFT_HRTF).
constexpr int kAlcDeviceSpecifier = 0x1005;
constexpr int kAlcHrtfSoft = 0x1992;
constexpr int kAlcHrtfStatusSoft = 0x1993;
constexpr int kAlcHrtfSpecifierSoft = 0x1995;
constexpr int kAlcOutputLimiterSoft = 0x199A;

using tOpenDevice = void* (*)(char const* name);
using tCreateContext = void* (*)(void* device, int const* attributes);
using tGetIntegerv = void (*)(void* device, int param, int size, int* values);
using tGetString = char const* (*)(void* device, int param);

enum class Mode : std::uint8_t { Hrtf, Soft, Game };

tOpenDevice gOpenDevice = nullptr;
tCreateContext gCreateContext = nullptr;
tGetIntegerv gGetIntegerv = nullptr;
tGetString gGetString = nullptr;
Mode gMode = Mode::Hrtf;
float gGainDb = 3.0f;
std::atomic<bool> gSwitched{false};   // every function of the router's goes to OpenAL Soft (the menu's Running)
std::atomic<bool> gInstalled{false};  // ASI\audio\soft_oal.dll was there at attach

std::mutex gDescribeMutex;
std::string gDescribe = "the game's own OpenAL (not looked at yet)";

void SetDescribe(std::string text) {
    std::lock_guard lock(gDescribeMutex);
    gDescribe = std::move(text);
}

Mode ReadMode(std::filesystem::path const& settings) {
    wchar_t text[64] = {};
    GetPrivateProfileStringW(L"Audio", L"spatial", L"hrtf", text, 64, settings.c_str());
    std::wstring value(text);
    for (wchar_t& c : value) {
        c = static_cast<wchar_t>(towlower(c));
    }
    if (value == L"game" || value == L"off" || value == L"0" || value == L"creative") {
        return Mode::Game;
    }
    if (value == L"soft" || value == L"pan" || value == L"speakers") {
        return Mode::Soft;
    }
    return Mode::Hrtf;
}

char const* HrtfStatusName(int status) {
    switch (status) {
    case 0: return "off";
    case 1: return "on";
    case 2: return "refused by the device";
    case 3: return "on (required by the device)";
    case 4: return "on (headphones detected)";
    case 5: return "unavailable for the output format";
    default: return "unknown";
    }
}

std::string ModuleOf(void const* address);  // below: "module+offset" of an address

// The game's alcOpenDevice: the device it asks for, else OpenAL Soft's default (a name only Creative's router knows).
void* OpenDevice(char const* name) {
    log::Info("audio: alcOpenDevice called from {} on thread {}", ModuleOf(_ReturnAddress()), GetCurrentThreadId());
    void* device = gOpenDevice(name);
    bool const fellBack = device == nullptr && name != nullptr;
    if (fellBack) {
        device = gOpenDevice(nullptr);
    }
    char const* const opened = device != nullptr && gGetString != nullptr ? gGetString(device, kAlcDeviceSpecifier) : nullptr;
    log::Info("audio: the game opened {} through OpenAL Soft (it asked for {}{})",
        device == nullptr ? std::string("no device") : std::format("\"{}\"", opened != nullptr ? opened : "?"),
        name != nullptr ? std::format("\"{}\"", name) : std::string("the default device"),
        fellBack ? ", which OpenAL Soft has not: its default instead" : "");
    if (device == nullptr) {
        SetDescribe("OpenAL Soft, but it opened no device: the game has no sound");
    }
    return device;
}

// The game's alcCreateContext: its attributes, with HRTF asked for (or refused) on top.
void* CreateContext(void* device, int const* attributes) {
    std::vector<int> wanted;
    int theirs = 0;
    for (int i = 0; attributes != nullptr && attributes[i] != 0 && i < 128; i += 2) {
        ++theirs;
        if (attributes[i] != kAlcHrtfSoft && (attributes[i] != kAlcOutputLimiterSoft || gGainDb <= 0.0f)) {
            wanted.push_back(attributes[i]);
            wanted.push_back(attributes[i + 1]);
        }
    }
    wanted.push_back(kAlcHrtfSoft);
    wanted.push_back(gMode == Mode::Hrtf ? 1 : 0);
    if (gGainDb > 0.0f) {
        wanted.push_back(kAlcOutputLimiterSoft);  // a louder mix never clips (OpenAL Soft's limiter, off for float output)
        wanted.push_back(1);
    }
    wanted.push_back(0);
    void* context = gCreateContext(device, wanted.data());
    bool const plain = context == nullptr;
    if (plain) {
        context = gCreateContext(device, attributes);  // whatever the HRTF attribute did, the game keeps its sound
    }
    int status = -1;
    if (context != nullptr && gGetIntegerv != nullptr) {
        gGetIntegerv(device, kAlcHrtfStatusSoft, 1, &status);
    }
    bool const hrtfOn = status == 1 || status == 3 || status == 4;
    char const* const which = hrtfOn && gGetString != nullptr ? gGetString(device, kAlcHrtfSpecifierSoft) : nullptr;
    std::string const description = std::format("OpenAL Soft, HRTF {}{}", HrtfStatusName(status),
        which != nullptr ? std::format(" (\"{}\")", which) : std::string());
    log::Info("audio: the game's context {} ({} attribute(s) of its own{}): {}", context != nullptr ? "made" : "NOT made", theirs,
        plain ? "; refused with HRTF's attribute, made without it" : "", description);
    SetDescribe(context != nullptr ? description : "OpenAL Soft, but it made no context: the game has no sound");
    return context;
}

// [Audio] trace = 1: every load of an OpenAL library in this process (its name, who asked, when), and the first call of the
// two wrappers: for finding how the game reaches its audio driver.
using tLoadLibraryExW = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
tLoadLibraryExW gLoadLibraryExW = nullptr;

std::string ModuleOf(void const* address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module) || module == nullptr) {
        return "?";
    }
    wchar_t name[MAX_PATH] = {};
    GetModuleFileNameW(module, name, MAX_PATH);
    return std::format("{}+0x{:X}", ini::Utf8(std::filesystem::path(name).filename()),
        reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module));
}

HMODULE WINAPI LoadLibraryExWTrace(LPCWSTR name, HANDLE file, DWORD flags) {
    void* const caller = _ReturnAddress();
    HMODULE const module = gLoadLibraryExW(name, file, flags);
    if (name != nullptr) {
        std::wstring lower(name);
        for (wchar_t& c : lower) {
            c = static_cast<wchar_t>(towlower(c));
        }
        if (lower.find(L"oal") != std::wstring::npos || lower.find(L"openal") != std::wstring::npos ||
            lower.find(L"dsound") != std::wstring::npos) {
            log::Info("audio trace: LoadLibraryExW(\"{}\", flags 0x{:X}) from {} on thread {}: {}", ini::Utf8(std::filesystem::path(name)),
                flags, ModuleOf(caller), GetCurrentThreadId(), module != nullptr ? "loaded" : "failed");
        }
    }
    return module;
}

// Every named export of a module: address -> name.
std::unordered_map<std::uintptr_t, std::string> Exports(HMODULE module) {
    std::unordered_map<std::uintptr_t, std::string> out;
    auto const* const base = reinterpret_cast<std::uint8_t const*>(module);
    auto const* const dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    auto const* const nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY const& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress == 0) {
        return out;
    }
    auto const* const exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY const*>(base + dir.VirtualAddress);
    auto const* const names = reinterpret_cast<DWORD const*>(base + exports->AddressOfNames);
    auto const* const ordinals = reinterpret_cast<WORD const*>(base + exports->AddressOfNameOrdinals);
    auto const* const functions = reinterpret_cast<DWORD const*>(base + exports->AddressOfFunctions);
    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        DWORD const rva = functions[ordinals[i]];
        if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size) {
            continue;  // a forwarder
        }
        out.emplace(reinterpret_cast<std::uintptr_t>(base + rva), reinterpret_cast<char const*>(base + names[i]));
    }
    return out;
}

} // namespace

void Attach(hook::Context& init) {
    std::filesystem::path const settings = engine::SettingsFile();
    gMode = ReadMode(settings);
    std::filesystem::path const dll = settings.parent_path() / L"audio" / L"soft_oal.dll";
    std::error_code ec;
    gInstalled.store(std::filesystem::exists(dll, ec));
    if (gMode == Mode::Game) {
        log::Info("audio: the game's own OpenAL ([Audio] spatial = game)");
        SetDescribe("the game's own OpenAL ([Audio] spatial = game)");
        return;
    }
    if (!gInstalled.load()) {
        log::Info("audio: no {}: the game's own OpenAL (pans only; tools/fetch-openal-soft.ps1 builds OpenAL Soft)", ini::Utf8(dll));
        SetDescribe("the game's own OpenAL (no ASI\\audio\\soft_oal.dll)");
        return;
    }
    HMODULE const router = GetModuleHandleW(L"OpenAL32.dll");
    if (router == nullptr) {
        log::Warn("audio: the game's OpenAL32.dll isn't loaded yet: its own OpenAL plays");
        SetDescribe("the game's own OpenAL (its OpenAL32.dll wasn't loaded at attach)");
        return;
    }
    if (GetModuleHandleW(L"wrap_oal.dll") != nullptr) {
        log::Warn("audio: the game has already opened its OpenAL device (wrap_oal.dll is loaded): its own OpenAL plays");
        SetDescribe("the game's own OpenAL (its device was open before the host attached)");
        return;
    }
    // The whole mix louder by [Audio] gainDb (default 3): OpenAL Soft's mix is about 3 dB under Creative's (an unplaced
    // voice: -29.4 against -26.2 dB a channel, 2026-11-10). Its only knob for that is its config's volume-adjust, so the
    // host writes a config of its own (%LOCALAPPDATA%\MEVR\openal-soft.ini) and names it in ALSOFT_CONF before the library
    // loads; its statically linked C runtime copies the environment then. A player's own %AppData%\alsoft.ini is read
    // before it, so only volume-adjust is ours.
    {
        wchar_t text[32] = {};
        GetPrivateProfileStringW(L"Audio", L"gainDb", L"3", text, 32, settings.c_str());
        if (auto const value = ini::Number(ini::Utf8(std::filesystem::path(text)))) {
            gGainDb = static_cast<float>(std::clamp(*value, -12.0, 12.0));
        }
        wchar_t local[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) > 0) {
            std::filesystem::path const dir = std::filesystem::path(local) / L"MEVR";
            std::filesystem::path const conf = dir / L"openal-soft.ini";
            std::filesystem::create_directories(dir, ec);
            std::string const body = std::format("# Written by MEVR's host at each start (plugin/OpenAlSoft): [Audio] gainDb in ASI\\LE1MEVR.ini.\n"
                                                 "[general]\nvolume-adjust = {:.2f}\n", gGainDb);
            if (FILE* file = _wfopen(conf.c_str(), L"wb")) {
                std::fwrite(body.data(), 1, body.size(), file);
                std::fclose(file);
                SetEnvironmentVariableW(L"ALSOFT_CONF", conf.c_str());
            }
        }
    }
    HMODULE const soft = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (soft == nullptr) {
        log::Warn("audio: {} didn't load (error {}): the game's own OpenAL", ini::Utf8(dll), GetLastError());
        SetDescribe("the game's own OpenAL (ASI\\audio\\soft_oal.dll didn't load)");
        return;
    }
    gOpenDevice = reinterpret_cast<tOpenDevice>(GetProcAddress(soft, "alcOpenDevice"));
    gCreateContext = reinterpret_cast<tCreateContext>(GetProcAddress(soft, "alcCreateContext"));
    gGetIntegerv = reinterpret_cast<tGetIntegerv>(GetProcAddress(soft, "alcGetIntegerv"));
    gGetString = reinterpret_cast<tGetString>(GetProcAddress(soft, "alcGetString"));
    if (gOpenDevice == nullptr || gCreateContext == nullptr) {
        log::Warn("audio: {} has no alcOpenDevice or alcCreateContext: the game's own OpenAL", ini::Utf8(dll));
        FreeLibrary(soft);
        SetDescribe("the game's own OpenAL (ASI\\audio\\soft_oal.dll isn't an OpenAL)");
        return;
    }
    // Every function of the router's that OpenAL Soft has too goes to OpenAL Soft's: the game's audio engine reaches
    // the router through its imports and through GetProcAddress (it opened Creative's device with every import slot
    // changed), and every route ends in these. The device and its context through the two wrappers.
    std::vector<std::pair<std::uintptr_t, std::string>> exports;
    for (auto const& [address, name] : Exports(router)) {
        exports.emplace_back(address, name);
    }
    std::sort(exports.begin(), exports.end(), [](auto const& a, auto const& b) { return a.second < b.second; });
    // All of them switched at once: each created first, then enabled together under one freeze of the other threads
    // (MinHook's queue). The game sets its audio up while the host attaches (it made its context half way through 93
    // detours installed one by one, 2026-11-10: the dialogue was silent), so a partial switch is the one thing to avoid.
    MH_STATUS const status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        log::Error("audio: MinHook didn't start ({}): the game's own OpenAL", static_cast<int>(status));
        SetDescribe("the game's own OpenAL (no MinHook)");
        return;
    }
    std::vector<void*> created;
    std::string notInSoft;
    bool failed = false;
    for (auto const& [address, name] : exports) {
        void* detour = reinterpret_cast<void*>(GetProcAddress(soft, name.c_str()));
        if (name == "alcOpenDevice") {
            detour = reinterpret_cast<void*>(&OpenDevice);
        } else if (name == "alcCreateContext") {
            detour = reinterpret_cast<void*>(&CreateContext);
        }
        if (detour == nullptr) {
            notInSoft += (notInSoft.empty() ? "" : ", ") + name;
            continue;
        }
        void* original = nullptr;
        MH_STATUS const made = MH_CreateHook(reinterpret_cast<void*>(address), detour, &original);
        if (made != MH_OK) {
            log::Error("audio: the game's OpenAL {} couldn't be redirected (MinHook {})", name, static_cast<int>(made));
            failed = true;
            break;
        }
        created.push_back(reinterpret_cast<void*>(address));
    }
    auto const removeAll = [&] {
        for (void* target : created) {
            MH_RemoveHook(target);  // disables it first if enabled
        }
    };
    if (failed || created.empty()) {
        removeAll();
        log::Error("audio: OpenAL Soft not used (a function of the router's couldn't be redirected): the game's own OpenAL");
        SetDescribe("the game's own OpenAL (its functions couldn't be redirected)");
        return;
    }
    for (void* target : created) {
        MH_QueueEnableHook(target);
    }
    MH_STATUS const applied = MH_ApplyQueued();
    // Creative's driver loaded by now means the router opened a device before the switch (it loads the driver then, and
    // after the switch nothing reaches the router): that device is the router's, so everything goes back to it.
    if (applied != MH_OK || GetModuleHandleW(L"wrap_oal.dll") != nullptr) {
        removeAll();
        log::Warn("audio: {}: everything put back, the game's own OpenAL", applied != MH_OK
            ? std::format("the switch to OpenAL Soft failed (MinHook {})", static_cast<int>(applied))
            : std::string("the game opened its audio device through Creative's router just before the switch"));
        SetDescribe("the game's own OpenAL (the switch came too late)");
        return;
    }
    wchar_t trace[8] = {};
    GetPrivateProfileStringW(L"Audio", L"trace", L"0", trace, 8, settings.c_str());
    if (trace[0] == L'1') {
        HMODULE const kernelBase = GetModuleHandleW(L"KernelBase.dll");
        void* const target = kernelBase != nullptr ? reinterpret_cast<void*>(GetProcAddress(kernelBase, "LoadLibraryExW")) : nullptr;
        void* original = nullptr;
        if (target != nullptr && hook::Install(init, "audio trace LoadLibraryExW", target, reinterpret_cast<void*>(&LoadLibraryExWTrace), &original)) {
            gLoadLibraryExW = reinterpret_cast<tLoadLibraryExW>(original);
            log::Info("audio trace: LoadLibraryExW traced ([Audio] trace = 1)");
        }
    }
    log::Info("audio: the game's OpenAL ({} functions of {}) now goes to {} (HRTF {}, the mix {:+.1f} dB: [Audio] gainDb): no game "
              "file changed{}", created.size(), exports.size(), ini::Utf8(dll),
        gMode == Mode::Hrtf ? "asked for: [Audio] spatial = hrtf" : "off: [Audio] spatial = soft", gGainDb,
        notInSoft.empty() ? std::string() : std::format("; not in OpenAL Soft, left to the router: {}", notInSoft));
    SetDescribe("OpenAL Soft, the game's device not open yet");
    gSwitched.store(true);
}

std::string Describe() {
    std::lock_guard lock(gDescribeMutex);
    return gDescribe;
}

std::string Running() {
    if (!gSwitched.load()) {
        return "game";
    }
    return gMode == Mode::Hrtf ? "hrtf" : "soft";
}

float RunningGainDb() {
    return gSwitched.load() ? gGainDb : 0.0f;
}

bool Installed() {
    return gInstalled.load();
}

} // namespace mevr::host::openal_soft
