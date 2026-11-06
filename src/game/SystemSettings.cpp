// SPDX-License-Identifier: MIT
#include "game/SystemSettings.hpp"

#include "core/Log.hpp"
#include "engine/Engine.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/SettingsFile.hpp"
#include "game/ConsoleRequest.hpp"
#include "game/HeadTracking.hpp"

#include <Windows.h>

#include <cstring>
#include <format>
#include <iterator>
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
void const* gAppliedWorld = nullptr;  // the world the settings last went to: a new level gets them again
// The level's settle applies (2026-11-04, docs/release-checklist.md section 1): from a fresh launch the apply at the
// world change (the save's load) came during the load and the level rendered with the blur anyway (gotchas/motion-blur-
// in-stereo, 2026-11-02), while the same two lines by hand later turned it off. So each level gets them again once it has
// been the current world for 3 s and for 15 s, by when it renders.
constexpr float kSettleApplyAt[] = {3.0f, 15.0f};
void const* gSeenWorld = nullptr;
float gSinceWorldChange = 0.0f;
int gSettleApplies = 0;  // of kSettleApplyAt, done in this level
// Back to gameplay after a cinematic (2026-11-06, the owner in the PS VR2 on Eden Prime: "the exact same behavior", the
// assets flickering): the blur was off by hand at 10:12:58, a cutscene (10:13:10) and a conversation (10:16:11) played,
// and it was back by 10:18 with no level change, so nothing re-applied it. Every return to gameplay (modes 0-4) from any
// other mode (a conversation, a cutscene, a menu, a movie) applies the settings again, once.
bool gLeftGameplay = false;
int gLeftMode = -1;  // the last mode outside gameplay, for the log

fs::path FilePath() {
    return engine::SettingsFile();  // <exe dir>\ASI\<GAME>MEVR.ini, the host's file too
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

// A boolean's opposite, or empty for a number. The values are sent through the opposite first: `scale set MotionBlur
// False` with False already stored changed nothing in the renderer, while True then False turned the blur off (the
// "assets shift in different directions" flicker of 2026-10-27, measured 2026-10-28: a surface smeared along its own
// screen velocity on some frames; look-flips over a 4 s pan 40-57 before, 1-3 after; research/35).
std::string Opposite(std::string const& value) {
    if (SameKey(value, "False")) {
        return "True";
    }
    if (SameKey(value, "True")) {
        return "False";
    }
    return {};
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
    std::optional<unsigned char> const mode = head_tracking::GameMode();
    if (mode && *mode > 4) {
        gLeftGameplay = true;  // read every tick: a short cinematic between two polls still counts
        gLeftMode = *mode;
    }
    gSincePoll += deltaSeconds;
    if (gSincePoll < kPollSeconds) {
        return;
    }
    float const elapsed = gSincePoll;
    gSincePoll = 0.0f;
    fs::path const path = FilePath();
    std::error_code ec;
    fs::file_time_type const stamp = fs::last_write_time(path, ec);
    bool const changed = !ec && (!gHaveStamp || stamp != gStamp);
    // A new level (the save loaded after the title's menu world, a map change) renders with the blur again: apply again.
    void const* const world = engine::World();
    bool const newLevel = gApplied && world != nullptr && world != gAppliedWorld;
    if (world != gSeenWorld) {
        gSeenWorld = world;
        gSinceWorldChange = 0.0f;
        gSettleApplies = 0;
    } else {
        gSinceWorldChange += elapsed;
    }
    bool const settleDue = gApplied && world != nullptr && gSettleApplies < static_cast<int>(std::size(kSettleApplyAt)) &&
                           gSinceWorldChange >= kSettleApplyAt[gSettleApplies];
    bool const backToGameplay = gApplied && gLeftGameplay && mode && *mode <= 4;
    if (backToGameplay) {
        gLeftGameplay = false;
    }
    if (gApplied && !changed && !newLevel && !settleDue && !backToGameplay) {
        return;
    }
    std::vector<Setting> const wanted = Wanted(path);
    static std::vector<Setting> applied;  // what the game last got: a rewrite of the file with the same values is nothing
    auto const same = [&] {
        if (wanted.size() != applied.size()) {
            return false;
        }
        for (size_t i = 0; i < wanted.size(); ++i) {
            if (!SameKey(wanted[i].key, applied[i].key) || wanted[i].value != applied[i].value) {
                return false;
            }
        }
        return true;
    };
    if (gApplied && same() && !newLevel && !settleDue && !backToGameplay) {
        gStamp = stamp;
        gHaveStamp = !ec;
        return;
    }
    if (wanted.empty()) {
        if (!gToldOff) {
            gToldOff = true;
            log::Info("system settings: [SystemSettings] apply = 0: the engine's own values stay");
        }
        applied.clear();
        gApplied = true;
        gAppliedWorld = world;
        gStamp = stamp;
        gHaveStamp = !ec;
        return;
    }
    std::string appliedText;
    for (Setting const& s : wanted) {
        std::string const opposite = Opposite(s.value);
        if (!opposite.empty()) {
            std::string ignored;
            if (!console_request::RunCommand("scale set " + s.key + " " + opposite, &ignored)) {
                return;  // no local player yet: next second
            }
        }
        std::string output;
        if (!console_request::RunCommand("scale set " + s.key + " " + s.value, &output)) {
            return;  // no local player yet: next second
        }
        appliedText += (appliedText.empty() ? "" : ", ") + s.key + " " + s.value + (output.empty() ? "" : " (" + Trim(output) + ")");
    }
    log::Info("system settings: scale set {} (booleans through their opposite first){}", appliedText,
              newLevel              ? " (a new level: applied again)"
              : changed && gApplied ? " (the file's values changed)"
              : settleDue           ? std::format(" (the level has rendered {:.0f} s: applied again)", gSinceWorldChange)
              : backToGameplay      ? std::format(" (back to gameplay after mode {}: applied again)", gLeftMode)
                                    : " (BIOEngine.ini's [SystemSettings] wins over GamerSettings' and the game rewrites it)");
    if (settleDue && !newLevel) {
        ++gSettleApplies;
    }
    applied = wanted;
    gApplied = true;
    gAppliedWorld = world;
    gToldOff = false;
    gStamp = stamp;
    gHaveStamp = !ec;
}

} // namespace mevr::game::system_settings
