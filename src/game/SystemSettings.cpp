// SPDX-License-Identifier: MIT
#include "game/SystemSettings.hpp"

#include "core/Log.hpp"
#include "engine/GameAdapter.hpp"
#include "game/ConsoleRequest.hpp"

#include <Windows.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mevr::game::system_settings {
namespace {

namespace fs = std::filesystem;

constexpr float kPollSeconds = 1.0f;

struct Setting {
    std::string key;
    std::string value;
};

float gSincePoll = 0.0f;
bool gApplied = false;
fs::file_time_type gStamp{};
bool gHaveStamp = false;
bool gToldOff = false;

fs::path FilePath() {
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return fs::path(buffer).parent_path() / L"ASI" / (MEVR_GAME_NAME_W L"MEVR.ini");
}

std::string Narrow(std::wstring_view wide) {
    std::string out;
    for (wchar_t const c : wide) {
        out.push_back(c < 128 ? static_cast<char>(c) : '?');
    }
    return out;
}

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.pop_back();
    }
    size_t const start = s.find_first_not_of(" \t");
    return start == std::string::npos ? std::string() : s.substr(start);
}

bool SameKey(std::string const& a, std::string const& b) {
    return _stricmp(a.c_str(), b.c_str()) == 0;
}

// The built-in values, then the file's section over them (a key the file sets replaces the built-in one; apply = 0
// returns nothing).
std::vector<Setting> Wanted(fs::path const& path) {
    std::vector<Setting> wanted{
        {"DynamicShadows", "False"},
        {"MotionBlur", "False"},
        {"FoliageDrawRadiusMultiplier", "0.5"},
    };
    std::vector<wchar_t> buffer(32768);
    DWORD const n = GetPrivateProfileSectionW(L"SystemSettings", buffer.data(), static_cast<DWORD>(buffer.size()),
                                              path.wstring().c_str());
    for (wchar_t const* line = buffer.data(); n > 0 && *line != L'\0'; line += wcslen(line) + 1) {
        std::string const text = Narrow(line);
        size_t const eq = text.find('=');
        if (eq == std::string::npos || text[0] == ';' || text[0] == '#') {
            continue;
        }
        std::string const key = Trim(text.substr(0, eq));
        std::string const value = Trim(text.substr(eq + 1));
        if (key.empty()) {
            continue;
        }
        if (SameKey(key, "apply")) {
            if (value == "0" || SameKey(value, "false") || SameKey(value, "off")) {
                return {};
            }
            continue;
        }
        bool replaced = false;
        for (Setting& s : wanted) {
            if (SameKey(s.key, key)) {
                s.value = value;
                replaced = true;
            }
        }
        if (!replaced) {
            wanted.push_back({key, value});
        }
    }
    return wanted;
}

} // namespace

void Tick(float deltaSeconds) {
    gSincePoll += deltaSeconds;
    if (gSincePoll < kPollSeconds) {
        return;
    }
    gSincePoll = 0.0f;
    fs::path const path = FilePath();
    std::error_code ec;
    fs::file_time_type const stamp = fs::last_write_time(path, ec);
    bool const changed = !ec && (!gHaveStamp || stamp != gStamp);
    if (gApplied && !changed) {
        return;
    }
    std::vector<Setting> const wanted = Wanted(path);
    if (wanted.empty()) {
        if (!gToldOff) {
            gToldOff = true;
            log::Info("system settings: [SystemSettings] apply = 0: the engine's own values stay");
        }
        gApplied = true;
        gStamp = stamp;
        gHaveStamp = !ec;
        return;
    }
    std::string applied;
    for (Setting const& s : wanted) {
        std::string output;
        if (!console_request::RunCommand("scale set " + s.key + " " + s.value, &output)) {
            return;  // no local player yet: next second
        }
        applied += (applied.empty() ? "" : ", ") + s.key + " " + s.value + (output.empty() ? "" : " (" + Trim(output) + ")");
    }
    log::Info("system settings: scale set {}{}", applied,
              changed && gApplied ? " (the file changed)"
                                  : " (BIOEngine.ini's [SystemSettings] wins over GamerSettings' and the game rewrites it)");
    gApplied = true;
    gToldOff = false;
    gStamp = stamp;
    gHaveStamp = !ec;
}

} // namespace mevr::game::system_settings
