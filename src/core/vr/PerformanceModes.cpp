// SPDX-License-Identifier: MIT
#include "core/vr/PerformanceModes.hpp"

#include "core/Ini.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <format>
#include <utility>

namespace mevr::vr::perf_modes {
namespace {

// The grass distance compares to the menu's step (5% of the game's); the rest are integers and flags.
constexpr float kFoliageTolerance = 0.025f;

bool Flag(std::optional<std::string> const& text, bool fallback) {
    if (!text) {
        return fallback;
    }
    std::string const t = ini::Lower(ini::Trim(*text));
    if (t == "true" || t == "1" || t == "on" || t == "yes") {
        return true;
    }
    if (t == "false" || t == "0" || t == "off" || t == "no") {
        return false;
    }
    return fallback;
}

bool IsFsr(std::string_view method) {
    // As core/vr/Upscale's Parse (and menu_settings::UpscaleChoice).
    std::string const m = ini::Lower(ini::Trim(method));
    return m == "fsr" || m == "fsr1" || m == "easu" || m == "on" || m == "1";
}

char const* ResolutionName(std::string_view chip) {
    if (chip == "headset") {
        return "the headset's full size";
    }
    if (chip == "headset85") {
        return "Headset \xC3\x97 0.85";
    }
    if (chip == "headset75") {
        return "Headset \xC3\x97 0.75";
    }
    return "a fixed size";
}

std::string Percent(float multiplier) {
    return std::format("{:.0f}%", FoliagePercent(multiplier));
}

} // namespace

std::vector<Mode> const& Modes() {
    // Measured at Eden Prime's two heaviest cells on the fake headset, research/60's tables.
    static std::vector<Mode> const modes = {
        {"quality", "Quality",
            "The headset's full eye size, grass drawn farther and small details kept: the sharpest picture, for when there's "
            "frame time to spare",
            Levers{"headset", true, 4, 0.75f, false, true}},
        {"balanced", "Balanced",
            "0.85 of the eye size sharpened back up by FSR, grass at half its distance: the tuned standard",
            Levers{"headset85", true, 8, 0.5f, false, true}},
        {"performance", "Performance",
            "0.75 of the eye size with FSR, shorter grass, small far objects culled, no ambient occlusion: the most headroom, "
            "a softer picture",
            Levers{"headset75", true, 24, 0.35f, false, false}},
    };
    return modes;
}

Mode const* Find(std::string_view key) {
    for (Mode const& mode : Modes()) {
        if (key == mode.key) {
            return &mode;
        }
    }
    return nullptr;
}

Levers Current(Getter const& get, std::string resolution) {
    Levers l;
    l.resolution = std::move(resolution);
    std::optional<std::string> const method = get("Upscale", "method");
    l.fsr = method ? IsFsr(*method) : true;
    // plugin/RenderLevers' read: minScreenRadiusTenths first (a tenth of a pixel), then minScreenRadiusPx; an empty value is
    // the default.
    std::optional<std::string> const tenths = get("Performance", "minScreenRadiusTenths");
    std::optional<double> const tenthsValue = tenths ? ini::Number(*tenths) : std::nullopt;
    if (tenthsValue) {
        l.minScreenRadiusPx = static_cast<int>(std::lround(*tenthsValue / 10.0));
    } else if (std::optional<std::string> const px = get("Performance", "minScreenRadiusPx")) {
        std::optional<double> const v = ini::Number(*px);
        l.minScreenRadiusPx = v ? static_cast<int>(*v) : 8;
    }
    if (std::optional<std::string> const foliage = get("SystemSettings", "FoliageDrawRadiusMultiplier")) {
        l.foliage = static_cast<float>(ini::Number(*foliage).value_or(0.5));
    }
    l.dynamicShadows = Flag(get("SystemSettings", "DynamicShadows"), false);
    l.ambientOcclusion = Flag(get("SystemSettings", "AmbientOcclusion"), true);
    return l;
}

bool Same(Levers const& a, Levers const& b) {
    return a.resolution == b.resolution && a.fsr == b.fsr && a.minScreenRadiusPx == b.minScreenRadiusPx &&
           std::fabs(a.foliage - b.foliage) < kFoliageTolerance && a.dynamicShadows == b.dynamicShadows &&
           a.ambientOcclusion == b.ambientOcclusion;
}

std::string ModeFor(Levers const& levers) {
    for (Mode const& mode : Modes()) {
        if (Same(levers, mode.levers)) {
            return mode.key;
        }
    }
    return "custom";
}

std::string Differences(Levers const& levers, Levers const& mode) {
    std::string out;
    auto add = [&](std::string text) { out += (out.empty() ? "" : ", ") + text; };
    if (levers.resolution != mode.resolution) {
        add(std::format("render size {}", ResolutionName(levers.resolution)));
    }
    if (levers.fsr != mode.fsr) {
        add(levers.fsr ? "FSR on" : "FSR off");
    }
    if (levers.minScreenRadiusPx != mode.minScreenRadiusPx) {
        add(levers.minScreenRadiusPx <= 0 ? std::string("no small-object culling")
                                          : std::format("small objects culled under {} px", levers.minScreenRadiusPx));
    }
    if (std::fabs(levers.foliage - mode.foliage) >= kFoliageTolerance) {
        add("grass at " + Percent(levers.foliage));
    }
    if (levers.dynamicShadows != mode.dynamicShadows) {
        add(levers.dynamicShadows ? "dynamic shadows on" : "dynamic shadows off");
    }
    if (levers.ambientOcclusion != mode.ambientOcclusion) {
        add(levers.ambientOcclusion ? "ambient occlusion on" : "ambient occlusion off");
    }
    return out;
}

Mode const& Nearest(Levers const& levers) {
    auto count = [&](Levers const& mode) {
        return (levers.resolution != mode.resolution ? 1 : 0) + (levers.fsr != mode.fsr ? 1 : 0) +
               (levers.minScreenRadiusPx != mode.minScreenRadiusPx ? 1 : 0) +
               (std::fabs(levers.foliage - mode.foliage) >= kFoliageTolerance ? 1 : 0) +
               (levers.dynamicShadows != mode.dynamicShadows ? 1 : 0) + (levers.ambientOcclusion != mode.ambientOcclusion ? 1 : 0);
    };
    Mode const* best = Find(kDefault);  // a tie names the mix against the standard
    for (Mode const& mode : Modes()) {
        if (count(mode.levers) < count(best->levers)) {
            best = &mode;
        }
    }
    return *best;
}

std::string Description(Levers const& levers) {
    std::string const key = ModeFor(levers);
    if (Mode const* mode = Find(key)) {
        return mode->description;
    }
    Mode const& nearest = Nearest(levers);
    return std::format("Your own mix: {} with {}", nearest.name, Differences(levers, nearest.levers));
}

std::vector<Edit> Edits(Levers const& levers) {
    auto boolean = [](bool on) { return std::string(on ? "True" : "False"); };
    return {
        {"Upscale", "method", levers.fsr ? "fsr" : "off"},
        {"Performance", "minScreenRadiusPx", std::format("{}", levers.minScreenRadiusPx)},
        {"SystemSettings", "FoliageDrawRadiusMultiplier", ini::Fixed(levers.foliage, 2)},
        {"SystemSettings", "DynamicShadows", boolean(levers.dynamicShadows)},
        {"SystemSettings", "AmbientOcclusion", boolean(levers.ambientOcclusion)},
    };
}

std::string RestartNote(bool pendingResolution) {
    return pendingResolution ? "Live, except the render resolution: it applies after a restart"
                             : "Applied live";
}

std::string CullChoice(int px) {
    static constexpr std::array<int, 6> kChips = {0, 4, 8, 16, 24, 32};
    int best = kChips[0];
    for (int const chip : kChips) {
        if (std::abs(chip - px) < std::abs(best - px)) {
            best = chip;
        }
    }
    return std::format("{}", best);
}

float FoliagePercent(float multiplier) {
    if (!std::isfinite(multiplier)) {
        return 50.0f;
    }
    return std::round(std::clamp(multiplier, 0.0f, 2.0f) * 100.0f);
}

float FoliageFromPercent(float percent) {
    if (!std::isfinite(percent)) {
        return 0.5f;
    }
    return std::clamp(percent, 0.0f, 200.0f) / 100.0f;
}

} // namespace mevr::vr::perf_modes
