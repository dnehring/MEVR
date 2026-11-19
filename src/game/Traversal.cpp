// SPDX-License-Identifier: MIT
#include "game/Traversal.hpp"
#include "game/Le2Traversal.hpp"
#include "game/Le3Traversal.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CallFunction.hpp"
#include "engine/Engine.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/SettingsFile.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/ArmIk.hpp"
#include "game/ConsoleRequest.hpp"
#include "game/Player.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace mevr::game::traversal {
namespace {

namespace tv = vr::traversal;

tv::Settings gSettings;
std::optional<tv::View> gViewOverride;
std::optional<tv::Mask> gMaskOverride;
std::optional<bool> gCueOverride;
std::optional<bool> gTraceOverride;
std::optional<bool> gCommitOverride;
std::uint64_t gSettingsMs = 0;
std::string gSettingsText;
Link gLink;

void RefreshSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gSettingsMs != 0 && now - gSettingsMs < 2000) {
        return;
    }
    gSettingsMs = now;
    ini::Loaded const loaded = vr::settings_file::Load(engine::SettingsFile());
    tv::Settings s;
    auto const number = [&](char const* key) { return ini::GetNumber(loaded.file, "Traversal", key); };
    if (auto const v = number("enabled")) s.enabled = *v != 0.0;
    if (auto const v = number("cue")) s.cue = *v != 0.0;
    if (auto const v = ini::Get(loaded.file, "Traversal", "view")) s.view = tv::ParseView(*v).value_or(s.view);
    if (auto const v = ini::Get(loaded.file, "Traversal", "mask")) s.mask = tv::ParseMask(*v).value_or(s.mask);
    if (auto const v = number("vignetteStrength")) s.vignetteStrength = static_cast<float>(std::clamp(*v, 0.0, 1.0));
    if (auto const v = number("blinkInS")) s.blinkInS = static_cast<float>(std::clamp(*v, 0.0, 1.0));
    if (auto const v = number("blinkHoldS")) s.blinkHoldS = static_cast<float>(std::clamp(*v, 0.0, 3.0));
    if (auto const v = number("blinkOutS")) s.blinkOutS = static_cast<float>(std::clamp(*v, 0.0, 1.0));
    if (auto const v = number("settleS")) s.settleS = static_cast<float>(std::clamp(*v, 0.0, 2.0));
    if (auto const v = number("levelTauS")) s.levelTauS = static_cast<float>(std::clamp(*v, 0.0, 0.5));
    if (auto const v = number("cueHeightDeg")) s.cueHeightDeg = static_cast<float>(std::clamp(*v, 0.5, 10.0));
    if (auto const v = number("cueRangeM")) s.cueRangeM = static_cast<float>(std::clamp(*v, 0.5, 10.0));
    if (auto const v = number("doubleTapS")) s.doubleTapS = static_cast<float>(std::clamp(*v, 0.15, 1.5));
    if (auto const v = number("reachM")) s.reachM = static_cast<float>(std::clamp(*v, 0.3, 5.0));
    if (auto const v = number("commit")) s.commit = *v != 0.0;
    if (auto const v = number("trace")) s.trace = *v != 0.0;
    if (auto const v = ini::Get(loaded.file, "Traversal", "facing")) s.headFacing = *v != "pawn";
    if (auto const v = number("holstered")) s.holstered = *v != 0.0;
    if (gViewOverride) s.view = *gViewOverride;
    if (gMaskOverride) s.mask = *gMaskOverride;
    if (gCueOverride) s.cue = *gCueOverride;
    if (gTraceOverride) s.trace = *gTraceOverride;
    if (gCommitOverride) s.commit = *gCommitOverride;
    std::string const text = std::format("enabled {}, commit {} (the double A, {:.2f} s), cue {}, view {}, mask {} (vignette {:.2f}, blink {:.2f}/{:.2f}/{:.2f} s), "
                                         "settle {:.2f} s, reach {:.1f} m, facing {}, holstered {}",
        s.enabled ? 1 : 0, s.commit ? 1 : 0, s.doubleTapS, s.cue ? 1 : 0, tv::Name(s.view), tv::Name(s.mask), s.vignetteStrength, s.blinkInS,
        s.blinkHoldS, s.blinkOutS, s.settleS, s.reachM, s.headFacing ? "head" : "pawn", s.holstered ? 1 : 0);
    if (text != gSettingsText) {
        gSettingsText = text;
        log::Info("traversal: [Traversal] {}", text);
    }
    gSettings = s;
}

} // namespace


bool TryStartup() {
#if defined(MEVR_GAME_LE3)
    return le3::TryStartup();
#elif defined(MEVR_GAME_LE2)
    if (!ue3::runtime::Up()) {
        log::Warn("traversal: our reflection layer is off ({}): no vault", ue3::runtime::Status());
        return false;
    }
    RefreshSettings();
    return le2::TryStartup(gSettings, gSettingsText);
#else
    return false;
#endif
}

void Tick(float /*deltaSeconds*/, Link const& link) {
    gLink = link;
#if defined(MEVR_GAME_LE3)
    RefreshSettings();
    le3::Tick(link, gSettings);
#elif defined(MEVR_GAME_LE2)
    if (!le2::Started()) {
        return;
    }
    RefreshSettings();
    le2::Tick(link, gSettings, gSettingsText);
#endif
}

bool Moving() {
#if defined(MEVR_GAME_LE3)
    return le3::Moving();
#elif defined(MEVR_GAME_LE2)
    return le2::Moving();
#else
    return false;
#endif
}

bool FloorMoving() {
#if defined(MEVR_GAME_LE3)
    return le3::FloorMoving();
#elif defined(MEVR_GAME_LE2)
    return le2::Moving();
#else
    return false;
#endif
}

std::string StatusJson() {
#if defined(MEVR_GAME_LE3)
    return le3::StatusJson();
#elif defined(MEVR_GAME_LE2)
    return le2::StatusJson();
#else
    // ME1 has no traversal; the status keeps the shape it had while ME2's observer lived in this file.
    return "{\"started\": false, \"settings\": \"\", \"slots\": 0, \"near\": null, \"moving\": false, \"moves\": 0, \"presses\": 0, "
           "\"doubleTaps\": 0, \"commits\": 0, \"commitsRefused\": 0, \"violations\": 0, \"last\": null}";
#endif
}

void LogStats() {
#if defined(MEVR_GAME_LE3)
    log::Info("    traversal: {}", le3::StatusJson());
#elif defined(MEVR_GAME_LE2)
    le2::LogStats();
#endif
}

std::string Command(std::string const& args, Link const& freshLink) {
    gLink = freshLink;
    return Command(args);
}
std::string Command(std::string const& args) {
    std::istringstream words(args);
    std::string verb;
    words >> verb;
    verb = ini::Lower(verb);
    auto const onOff = [&](std::optional<bool>& target, char const* what) {
        std::string v;
        words >> v;
        v = ini::Lower(v);
        if (v == "on" || v == "1") target = true;
        else if (v == "off" || v == "0") target = false;
        else if (v == "auto" || v == "ini") target.reset();
        gSettingsMs = 0;
        return std::format("traversal: {} {}", what, target ? (*target ? "on" : "off") : "from the ini");
    };
    if (verb == "view") {
        std::string v;
        words >> v;
        gViewOverride = v == "ini" ? std::nullopt : tv::ParseView(v);
        gSettingsMs = 0;
        return std::format("traversal: view {}", gViewOverride ? tv::Name(*gViewOverride) : "from the ini");
    }
    if (verb == "mask") {
        std::string v;
        words >> v;
        gMaskOverride = v == "ini" ? std::nullopt : tv::ParseMask(v);
        gSettingsMs = 0;
        return std::format("traversal: mask {}", gMaskOverride ? tv::Name(*gMaskOverride) : "from the ini");
    }
    if (verb == "cue") return onOff(gCueOverride, "cue");
    if (verb == "trace") return onOff(gTraceOverride, "trace");
    if (verb == "commit") return onOff(gCommitOverride, "commit on the double A");
#if defined(MEVR_GAME_LE3)
    return le3::Command(args, gLink);
#endif
#if defined(MEVR_GAME_LE2)
    if (std::string handled = le2::Command(args, gLink); !handled.empty()) {
        return handled;
    }
#endif
    return "usage: mevr traversal status | slots | near | place <link> <slot> [backM] [sideM] [yawDeg] | go | try | pause <s> | "
           "view game|level|ini | mask none|vignette|blink|ini | cue on|off|ini | trace on|off | commit on|off|ini";
}

void Shutdown() {
#if defined(MEVR_GAME_LE3)
    le3::Shutdown();
#elif defined(MEVR_GAME_LE2)
    le2::Shutdown();
#endif
    gLink = {};
    gSettingsMs = 0;
    gSettingsText.clear();
}

} // namespace mevr::game::traversal
