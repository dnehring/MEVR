// SPDX-License-Identifier: MIT AND BSD-3-Clause
#include "install/SteamVrSettings.hpp"

#include "core/Json.hpp"
#include "install/Manifest.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <set>
#include <vector>

namespace mevr::install::steamvr {
namespace {
namespace fs = std::filesystem;
constexpr char kKey[] = "motionSmoothingOverride";

// Minimal ABI prefix adapted from Valve's openvr.h (Copyright Valve Corporation, BSD-3-Clause):
// https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h
// IVRSettings_003 has NO destructor or Sync slot. Keep every slot through GetInt32 in header order.
// See NOTICE.md and LICENSES/BSD-3-Clause-Valve.txt for the retained license.
enum class SettingsError : int { None = 0, Unset = 5 };
enum class InitError : int { None = 0 };
enum class ApplicationType : int { Background = 3 };
class Settings {
public:
    virtual char const* GetSettingsErrorNameFromEnum(SettingsError) = 0;
    virtual void SetBool(char const*, char const*, bool, SettingsError*) = 0;
    virtual void SetInt32(char const*, char const*, std::int32_t, SettingsError*) = 0;
    virtual void SetFloat(char const*, char const*, float, SettingsError*) = 0;
    virtual void SetString(char const*, char const*, char const*, SettingsError*) = 0;
    virtual bool GetBool(char const*, char const*, SettingsError*) = 0;
    virtual std::int32_t GetInt32(char const*, char const*, SettingsError*) = 0;
protected:
    ~Settings() = default; // not a vtable slot; never delete a runtime-owned interface
};
using Init = std::uint32_t (__cdecl*)(InitError*, ApplicationType, char const*);
using Shutdown = void (__cdecl*)();
using Interface = void* (__cdecl*)(char const*, InitError*);

bool AppKey(std::string_view app) {
    return !app.empty() && app.size() < 128 && std::all_of(app.begin(), app.end(), [](unsigned char c) {
        return c >= 33 && c <= 126 && c != '"' && c != '\\';
    });
}

std::optional<fs::path> Path(std::string_view paths, char const* key) {
    auto const root = json::Parse(paths);
    if (!root || !root->IsObject()) return std::nullopt;
    auto const& list = root->Get(key);
    if (!list.IsArray()) return std::nullopt;
    for (auto const& item : list.Items()) {
        if (!item.IsString() || item.AsString().empty() || item.AsString().find('\0') != std::string::npos) continue;
        std::string const& text = item.AsString();
        fs::path p(std::u8string(text.begin(), text.end()));
        if (p.is_absolute()) return p;
    }
    return std::nullopt;
}

// Parse already validated JSON into byte spans, without reserializing numbers, strings or whitespace.
struct Member { std::string key; std::size_t start, end; };
class Spans {
public:
    explicit Spans(std::string_view text) : text_(text) {}
    void Space(std::size_t& p) const { while (p < text_.size() && (text_[p] == ' ' || text_[p] == '\r' || text_[p] == '\n' || text_[p] == '\t')) ++p; }
    void String(std::size_t& p) const {
        ++p;
        while (p < text_.size()) {
            char const c = text_[p++];
            if (c == '\\') ++p;
            else if (c == '"') break;
        }
    }
    void Value(std::size_t& p) const {
        if (text_[p] == '"') { String(p); return; }
        if (text_[p] == '{' || text_[p] == '[') {
            char const close = text_[p++] == '{' ? '}' : ']';
            while (text_[p] != close) {
                Space(p);
                if (text_[p] == close) break;
                if (text_[p] == ',' || text_[p] == ':') ++p;
                else Value(p);
            }
            ++p;
        } else {
            while (p < text_.size() && text_[p] != ',' && text_[p] != '}' && text_[p] != ']' &&
                   text_[p] != ' ' && text_[p] != '\t' && text_[p] != '\r' && text_[p] != '\n') ++p;
        }
    }
    std::optional<std::vector<Member>> Object(std::size_t start, std::size_t& close) const {
        if (text_[start] != '{') return std::nullopt;
        std::vector<Member> members;
        std::set<std::string> keys;
        std::size_t p = start + 1;
        Space(p);
        while (text_[p] != '}') {
            std::size_t const keyStart = p;
            String(p);
            auto key = json::Parse(text_.substr(keyStart, p - keyStart));
            if (!key || !keys.insert(key->AsString()).second) return std::nullopt;
            Space(p);
            ++p; // colon
            Space(p);
            std::size_t const valueStart = p;
            Value(p);
            members.push_back({key->AsString(), valueStart, p});
            Space(p);
            if (text_[p] == ',') { ++p; Space(p); }
        }
        close = p;
        return members;
    }
private:
    std::string_view text_;
};

std::string Insert(std::string_view text, std::size_t start, std::size_t close,
                   std::vector<Member> const& members, std::string const& property) {
    std::string result(text);
    bool const multiline = text.substr(start, close - start).find('\n') != std::string_view::npos;
    std::string const nl = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
    std::string indent = "   ";
    if (multiline && !members.empty()) {
        auto const first = text.find('"', start + 1);
        auto const line = text.rfind('\n', first);
        if (line != std::string_view::npos) indent = text.substr(line + 1, first - line - 1);
    } else if (multiline) {
        auto const line = text.rfind('\n', close);
        indent = std::string(text.substr(line + 1, close - line - 1)) + "   ";
    }
    std::size_t const at = members.empty() ? start + 1 : members.back().end;
    result.insert(at, (members.empty() ? "" : ",") + (multiline ? nl + indent : " ") + property);
    return result;
}

// Fail closed on enumeration errors: never infer that SteamVR is stopped from an inaccessible snapshot.
std::optional<bool> ServerRunning() {
    HANDLE const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    bool found = false;
    BOOL ok = Process32FirstW(snapshot, &entry);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    while (ok) {
        if (_wcsicmp(entry.szExeFile, L"vrserver.exe") == 0) { found = true; break; }
        ok = Process32NextW(snapshot, &entry);
        if (!ok) error = GetLastError();
    }
    CloseHandle(snapshot);
    if (!found && error != ERROR_NO_MORE_FILES) return std::nullopt;
    return found;
}

struct Api {
    HMODULE dll = nullptr;
    Shutdown shutdown = nullptr;
    bool attempted = false;
    ~Api() { if (attempted && shutdown) shutdown(); if (dll) FreeLibrary(dll); }
};
struct CommandLock {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\MEVRSteamVrSettings");
    bool held = false;
    CommandLock() {
        if (mutex) {
            DWORD const wait = WaitForSingleObject(mutex, 10000);
            held = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        }
    }
    ~CommandLock() { if (held) ReleaseMutex(mutex); if (mutex) CloseHandle(mutex); }
};
} // namespace

std::optional<fs::path> RuntimePath(std::string_view paths) { return Path(paths, "runtime"); }
std::optional<fs::path> ConfigPath(std::string_view paths) { return Path(paths, "config"); }

std::optional<std::string> EditSmoothing(std::string_view text, std::string_view app, int value) {
    if (!AppKey(app) || (value != 0 && value != 2)) return std::nullopt;
    auto const parsed = json::Parse(text);
    if (!parsed || !parsed->IsObject()) return std::nullopt;
    std::size_t start = text.starts_with("\xEF\xBB\xBF") ? 3 : 0;
    Spans spans(text);
    spans.Space(start);
    std::size_t close = 0;
    auto const root = spans.Object(start, close);
    if (!root) return std::nullopt;
    std::string const property = std::string("\"") + kKey + "\" : " + std::to_string(value);
    for (auto const& section : *root) {
        if (section.key != app) continue;
        std::size_t sectionClose = 0;
        auto const members = spans.Object(section.start, sectionClose);
        if (!members) return std::nullopt;
        for (auto const& member : *members) {
            if (member.key != kKey) continue;
            std::string result(text);
            result.replace(member.start, member.end - member.start, std::to_string(value));
            return result;
        }
        return Insert(text, section.start, sectionClose, *members, property);
    }
    return Insert(text, start, close, *root, "\"" + std::string(app) + "\" : { " + property + " }");
}

Result Smoothing(std::string_view command, std::string_view app) {
    if ((command != "get" && command != "off" && command != "default") || !AppKey(app))
        return {2, "Usage: --steamvr-smoothing get|off|default [--app <key>]"};
    CommandLock const lock;
    if (!lock.held) return {3, "Another SteamVR settings request is busy; try again."};
    wchar_t local[32768]{};
    DWORD const length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
    if (length == 0 || length >= std::size(local)) return {3, "Cannot find LOCALAPPDATA for OpenVR paths."};
    auto const paths = ReadFile(fs::path(local) / L"openvr" / L"openvrpaths.vrpath");
    auto const runtime = paths ? RuntimePath(*paths) : std::nullopt;
    if (!runtime) return {3, "Cannot read a SteamVR runtime from openvrpaths.vrpath."};
    Api api;
    api.dll = LoadLibraryExW((*runtime / L"bin/win64/openvr_api.dll").c_str(), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    Init init = api.dll ? reinterpret_cast<Init>(GetProcAddress(api.dll, "VR_InitInternal2")) : nullptr;
    Interface get = api.dll ? reinterpret_cast<Interface>(GetProcAddress(api.dll, "VR_GetGenericInterface")) : nullptr;
    api.shutdown = api.dll ? reinterpret_cast<Shutdown>(GetProcAddress(api.dll, "VR_ShutdownInternal")) : nullptr;
    int const wanted = command == "off" ? 2 : 0;
    InitError error = InitError::None;
    if (init && get && api.shutdown) {
        api.attempted = true;
        init(&error, ApplicationType::Background, nullptr);
        if (error == InitError::None) {
            auto* settings = static_cast<Settings*>(get("IVRSettings_003", &error));
            if (!settings || error != InitError::None) return {3, "SteamVR IVRSettings_003 is unavailable."};
            SettingsError settingError = SettingsError::None;
            std::string const key(app);
            if (command != "get") {
                settings->SetInt32(key.c_str(), kKey, wanted, &settingError);
                if (settingError != SettingsError::None) return {3, "SteamVR could not save the smoothing override."};
            }
            int value = settings->GetInt32(key.c_str(), kKey, &settingError);
            if (command == "get" && settingError == SettingsError::Unset) { value = 0; settingError = SettingsError::None; }
            if (settingError != SettingsError::None || (command != "get" && value != wanted))
                return {3, "SteamVR could not confirm the smoothing override."};
            return {0, key + ": motionSmoothingOverride=" + std::to_string(value) +
                (command == "get" ? " (SteamVR settings)." : " saved through SteamVR; change sent immediately.")};
        }
    }
    auto const running = ServerRunning();
    if (!running || *running) return {3, "SteamVR settings connection failed; no file edited while vrserver may be running."};
    if (command == "get") return {1, "SteamVR is not running; no live smoothing setting available."};
    auto const config = ConfigPath(*paths);
    if (!config) return {3, "SteamVR is not running; cannot find its config folder in openvrpaths.vrpath."};
    fs::path const file = *config / L"steamvr.vrsettings";
    auto const before = ReadFile(file);
    // Never overwrite an unreadable existing file. A genuinely missing file can start as an empty object.
    std::error_code ec;
    bool const exists = fs::exists(file, ec);
    if (ec || (exists && !before)) return {3, "SteamVR is not running; cannot read steamvr.vrsettings."};
    auto const after = EditSmoothing(before.value_or("{\n}\n"), app, wanted);
    if (!after) return {3, "SteamVR is not running; invalid or ambiguous steamvr.vrsettings; left unchanged."};
    fs::path tmp = file;
    tmp += L".mevr-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << *after;
        out.close();
        if (!out) { fs::remove(tmp, ec); return {3, "SteamVR is not running; could not write settings .tmp."}; }
    }
    // Check again immediately before replacing, including concurrent edits since the read.
    auto const stillRunning = ServerRunning();
    if (!stillRunning || *stillRunning || ReadFile(file) != before ||
        !MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(tmp, ec);
        return {3, "SteamVR settings changed, SteamVR started, or replacement failed; no settings replaced."};
    }
    return {0, std::string(app) + ": motionSmoothingOverride=" + std::to_string(wanted) +
        " saved while SteamVR is not running; applies on next launch."};
}
} // namespace mevr::install::steamvr
