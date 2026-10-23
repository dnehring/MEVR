#include "core/vr/MenuSettings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>

namespace mevr::vr::menu_settings {
namespace {

std::string Signed(float value, int decimals, char const* unit) {
    // No "-0" and no "+0": zero reads plain.
    float const rounded = std::round(value * std::pow(10.0f, static_cast<float>(decimals))) / std::pow(10.0f, static_cast<float>(decimals));
    std::string const number = std::format("{:.{}f}", std::fabs(rounded), decimals);
    char const* sign = rounded > 0.0f ? "+" : rounded < 0.0f ? "-" : "";
    return std::string(sign) + number + unit;
}

std::string Plain(float value, int decimals, char const* unit) {
    float const rounded = std::round(value * std::pow(10.0f, static_cast<float>(decimals))) / std::pow(10.0f, static_cast<float>(decimals));
    return std::format("{:.{}f}", rounded == 0.0f ? 0.0f : rounded, decimals) + unit;
}

std::string Lower(std::string_view text) {
    std::string out;
    for (char const c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string Trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

} // namespace

std::string FormatValue(Format format, float value) {
    if (!std::isfinite(value)) {
        return "?";
    }
    switch (format) {
    case Format::None: return Plain(value, 2, "");
    case Format::SignedPercent: return Signed(value, 0, "%");
    case Format::Percent: return Plain(value, 0, "%");
    case Format::SignedDegrees: return Signed(value, 0, "\xC2\xB0");
    case Format::Degrees: return Plain(value, 0, "\xC2\xB0");
    case Format::DegreesOrNever: return std::lround(value) <= 0 ? std::string("Never") : Plain(value, 0, "\xC2\xB0");
    case Format::DegreesPerSec: return Plain(value, 0, "\xC2\xB0/s");
    case Format::Fixed1: return Plain(value, 1, "");
    case Format::Fixed2: return Plain(value, 2, "");
    case Format::Fixed3: return Plain(value, 3, "");
    case Format::SignedFixed3: return Signed(value, 3, "");
    case Format::Times1: return Plain(value, 1, "\xC3\x97");
    case Format::Times2: return Plain(value, 2, "\xC3\x97");
    case Format::Meters1: return Plain(value, 1, " m");
    case Format::Meters2: return Plain(value, 2, " m");
    case Format::SignedMeters2: return Signed(value, 2, " m");
    }
    return Plain(value, 2, "");
}

std::vector<Field> const& Schema() {
    using F = Format;
    static std::vector<Field> const fields = {
        // The menu's own state.
        {"section", Type::Choice, 0, "comfort", 0, 1, F::None, true},
        {"advtab", Type::Choice, 0, "tracking", 0, 1, F::None, true},
        {"fine", Type::Flag, 0, "", 0, 1, F::None, true},
        {"reset_armed", Type::Flag, 0, "", 0, 1, F::None, true},
        {"comparing", Type::Flag, 0, "", 0, 1, F::None, true},
        // The top bar.
        {"chip_headset", Type::Text, 0, "Headset not started"},
        {"chip_controllers", Type::Text, 0, "No controllers"},
        {"chip_calibrated", Type::Text, 0, "Not calibrated"},
        {"calibrated", Type::Flag, 0},
        // Comfort.
        {"turn", Type::Choice, 0, "smooth"},
        {"snap", Type::Choice, 0, "45"},
        {"vignette", Type::Flag, 0},
        {"vignette_strength", Type::Number, 60, "", 10, 100, F::Percent},
        {"vignette_when", Type::Choice, 0, "turn"},
        {"walk", Type::Choice, 0, "head"},
        {"crouch", Type::Flag, 1},
        {"lifesize", Type::Flag, 0},
        {"pause", Type::Flag, 1},
        // Controls.
        {"grip_holds", Type::Flag, 1},
        {"holsters", Type::Flag, 1},
        {"holster_buzz", Type::Number, 50, "", 0, 100, F::Percent},
        {"left_trigger", Type::Choice, 0, "lb"},
        {"throwables", Type::Flag, 1},
        {"recoil_buzz", Type::Number, 50, "", 0, 100, F::Percent},
        {"aim_assist", Type::Flag, 0},
        // Body and calibration.
        {"cal_running", Type::Flag, 0},
        {"cal_status", Type::Text, 0, "Not calibrated for these controllers yet"},
        {"cal_action", Type::Text, 0, "Start calibration"},
        {"t_stand", Type::Text, 0, "\xE2\x80\x93"},
        {"t_crouch", Type::Text, 0, "\xE2\x80\x93"},
        {"t_arm", Type::Text, 0, "\xE2\x80\x93"},
        {"t_eye", Type::Text, 0, "\xE2\x80\x93"},
        {"t_pistol", Type::Text, 0, "\xE2\x80\x93"},
        {"t_rifle", Type::Text, 0, "\xE2\x80\x93"},
        {"measure_msg", Type::Text, 0, ""},
        {"align_msg", Type::Text, 0, ""},
        {"trim_pitch", Type::Number, 0, "", -30, 30, F::SignedDegrees},
        {"trim_yaw", Type::Number, 0, "", -30, 30, F::SignedDegrees},
        {"trim_roll", Type::Number, 0, "", -30, 30, F::SignedDegrees},
        // Display.
        {"profile", Type::Choice, 0, "auto"},
        {"profile_info", Type::Text, 0, ""},
        {"c_brightness", Type::Number, 0, "", -20, 20, F::SignedPercent},
        {"c_contrast", Type::Number, 0, "", -20, 30, F::SignedPercent},
        {"c_saturation", Type::Number, 0, "", -30, 40, F::SignedPercent},
        {"c_midtones", Type::Number, 1, "", 0.7f, 1.3f, F::Fixed2},
        {"c_black", Type::Number, 0, "", -0.05f, 0.05f, F::SignedFixed3},
        {"quality", Type::Choice, 0, "performance"},
        {"quality_text", Type::Text, 0, "3072 \xC3\x97 3188"},
        {"quality_note", Type::Text, 0, "applies after a restart"},
        {"headset_known", Type::Flag, 0},
        {"headset_text", Type::Text, 0, "Your headset's own eye size, known once VR starts"},
        {"upscale", Type::Choice, 0, "off"},
        {"steady_cine", Type::Flag, 1},
        // HUD and sights.
        {"hud_distance", Type::Number, 1.5f, "", 1, 3, F::Meters1},
        {"hud_size", Type::Number, 100, "", 70, 140, F::Percent},
        {"reticle", Type::Flag, 1},
        {"sight_pistol", Type::Choice, 0, "dot"},
        {"sight_rifle", Type::Choice, 0, "dot"},
        {"sight_sniper", Type::Choice, 0, "scope"},
        {"sight_assist", Type::Flag, 1},
        {"sight_converge", Type::Flag, 1},
        // Advanced: Halcyon's renderer.
        {"recenter_key", Type::Flag, 1},
        {"look_speed", Type::Number, 1, "", 0.25f, 2, F::Times2},
        {"look_smooth", Type::Number, 0.4f, "", 0, 0.9f, F::Fixed2},
        {"vr_convo", Type::Flag, 1},
        {"vr_cine", Type::Flag, 1},
        {"flat_menus", Type::Flag, 1},
        {"flat_galaxy", Type::Flag, 1},
        {"fp_on", Type::Flag, 0},
        {"fp_hide_head", Type::Flag, 1},
        {"fp_back", Type::Number, 0, "", -2, 2, F::SignedMeters2},
        {"fp_right", Type::Number, 0, "", -1, 1, F::SignedMeters2},
        {"fp_up", Type::Number, 0, "", -1, 1, F::SignedMeters2},
        {"lean", Type::Flag, 1},
        {"lean_gain", Type::Number, 1, "", 0.5f, 3, F::Times1},
        {"flat_size", Type::Number, 2.35f, "", 1, 3.5f, F::Meters2},
        {"fov_fill", Type::Flag, 1},
        {"vr_mode", Type::Choice, 0, "stereo"},
        {"mode_note", Type::Text, 0, ""},
        {"life_size", Type::Flag, 1},
        {"extra_depth", Type::Number, 1, "", 0.5f, 2, F::Times2},
        {"eye_distance", Type::Number, 3.4f, "", 0.5f, 8, F::Fixed1},
        {"convergence", Type::Number, 0.012f, "", -0.02f, 0.05f, F::Fixed3},
        {"swap_eyes", Type::Flag, 1},
        {"quest_fov", Type::Flag, 1},
        {"dof", Type::Flag, 1},
        {"mirror", Type::Choice, 0, "normal"},
        {"panel_width", Type::Number, 1.3f, "", 0.6f, 2, F::Meters2},
        {"panel_below", Type::Number, 0.25f, "", -0.3f, 0.6f, F::SignedMeters2},
        {"panel_follow", Type::Number, 70, "", 0, 120, F::DegreesOrNever},
        {"g_pchud", Type::Flag, 1},
        {"g_convo", Type::Flag, 1},
        {"g_designer", Type::Flag, 1},
        {"prof_active", Type::Choice, 0, "0"},
        {"prof0_name", Type::Text, 0, "Default"},
        {"prof1_name", Type::Text, 0, "Custom 1"},
        {"prof2_name", Type::Text, 0, "Custom 2"},
        {"prof3_name", Type::Text, 0, "Custom 3"},
        {"prof_msg", Type::Text, 0, ""},
        {"recording", Type::Flag, 0},
        {"diag_msg", Type::Text, 0, ""},
        {"perf_msg", Type::Text, 0, ""},
    };
    return fields;
}

Field const* Find(std::string_view key) {
    for (Field const& field : Schema()) {
        if (key == field.key) {
            return &field;
        }
    }
    return nullptr;
}

float Clamp(Field const& field, float value) {
    if (!std::isfinite(value)) {
        return field.number;
    }
    if (field.type == Type::Flag) {
        return value != 0.0f ? 1.0f : 0.0f;
    }
    if (field.type != Type::Number) {
        return value;
    }
    return std::clamp(value, std::min(field.min, field.max), std::max(field.min, field.max));
}

std::vector<QualityTier> const& QualityTiers() {
    static std::vector<QualityTier> const tiers = {
        {"low", 2048}, {"performance", 3072}, {"balanced", 4096}, {"sharp", 5120}, {"max", 6144}};
    return tiers;
}

char const* QualityFor(int width) {
    char const* best = QualityTiers().front().key;
    int bestDistance = 1 << 30;
    for (QualityTier const& tier : QualityTiers()) {
        int const distance = std::abs(tier.width - width);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = tier.key;
        }
    }
    return best;
}

int QualityWidth(std::string_view key) {
    for (QualityTier const& tier : QualityTiers()) {
        if (key == tier.key) {
            return tier.width;
        }
    }
    return 0;
}

int EyeHeight(int width) {
    if (width < 2) {
        return 2;
    }
    int h = static_cast<int>(static_cast<float>(width) / 0.964f + 0.5f);
    if ((h & 1) != 0) {
        ++h;
    }
    return h;
}

std::string QualityText(int width) {
    return SizeText(width, EyeHeight(width));
}

std::string SizeText(int width, int height) {
    return std::format("{} \xC3\x97 {}", width, height);
}

std::vector<HeadsetChoice> const& HeadsetChoices() {
    static std::vector<HeadsetChoice> const choices = {{"headset", 1.0f}, {"headset85", 0.85f}, {"headset75", 0.75f}};
    return choices;
}

int RoundEven(float value) {
    if (!std::isfinite(value) || value < 2.0f) {
        return 2;
    }
    return 2 * static_cast<int>(std::lround(value * 0.5f));
}

Size HeadsetSize(std::string_view key, int recommendedWidth, int recommendedHeight) {
    if (recommendedWidth <= 0 || recommendedHeight <= 0) {
        return {};
    }
    for (HeadsetChoice const& choice : HeadsetChoices()) {
        if (key == choice.key) {
            if (choice.scale == 1.0f) {
                return {recommendedWidth, recommendedHeight};  // the runtime's size exactly, even or not
            }
            return {RoundEven(static_cast<float>(recommendedWidth) * choice.scale),
                RoundEven(static_cast<float>(recommendedHeight) * choice.scale)};
        }
    }
    return {};
}

std::string ResolutionChoice(int resX, int resY, int recommendedWidth, int recommendedHeight) {
    for (HeadsetChoice const& choice : HeadsetChoices()) {
        Size const size = HeadsetSize(choice.key, recommendedWidth, recommendedHeight);
        if (size.width > 0 && size == Size{resX, resY}) {
            return choice.key;
        }
    }
    for (QualityTier const& tier : QualityTiers()) {
        if (tier.width == resX) {
            return tier.key;
        }
    }
    return "";
}

std::string HeadsetSizesText(int recommendedWidth, int recommendedHeight) {
    if (recommendedWidth <= 0 || recommendedHeight <= 0) {
        return "Your headset's own eye size, known once VR starts";
    }
    Size const full = HeadsetSize("headset", recommendedWidth, recommendedHeight);
    Size const s85 = HeadsetSize("headset85", recommendedWidth, recommendedHeight);
    Size const s75 = HeadsetSize("headset75", recommendedWidth, recommendedHeight);
    return std::format("Headset {} \xC2\xB7 \xC3\x97 0.85: {} \xC2\xB7 \xC3\x97 0.75: {}", SizeText(full.width, full.height),
        SizeText(s85.width, s85.height), SizeText(s75.width, s75.height));
}

std::string UpscaleChoice(std::string_view method) {
    // As core/vr/Upscale's Parse (kept apart: the menu library doesn't link the upscale).
    std::string const m = Lower(Trim(method));
    return m == "fsr" || m == "fsr1" || m == "easu" || m == "on" || m == "1" ? "fsr" : "off";
}

std::string SightKind(std::string_view value, std::string_view fallback) {
    std::string const text = Lower(Trim(value));
    std::string const kind = text.substr(0, text.find(' '));
    if (kind == "none" || kind == "dot" || kind == "holo" || kind == "scope") {
        return kind;
    }
    return std::string(fallback);
}

std::string SightValue(std::string_view kind, std::string_view previous) {
    if (kind != "scope") {
        return std::string(kind);
    }
    std::string const text = Lower(Trim(previous));
    float zoom = 4.0f;
    if (text.rfind("scope", 0) == 0 && text.size() > 5) {
        float const parsed = static_cast<float>(std::atof(text.c_str() + 5));
        if (parsed >= 1.0f && parsed <= 12.0f) {
            zoom = parsed;
        }
    }
    return std::format("scope {}", Plain(zoom, zoom == std::floor(zoom) ? 0 : 1, ""));
}

std::string MirrorChoice(int presentEvery) {
    return presentEvery <= 1 ? "every" : presentEvery == 2 ? "smooth" : "normal";
}

int MirrorPresentEvery(std::string_view choice) {
    return choice == "every" ? 1 : choice == "smooth" ? 2 : 8;
}

std::string SnapChoice(float degrees) {
    if (!std::isfinite(degrees)) {
        return "45";
    }
    return degrees < 37.5f ? "30" : degrees < 52.5f ? "45" : "60";
}

std::string TurnChoice(std::string_view value) {
    return Lower(Trim(value)) == "snap" ? "snap" : "smooth";
}

float ColourPercent(float value) {
    return std::round((value - 1.0f) * 100.0f);
}

float ColourFromPercent(float percent) {
    return 1.0f + percent / 100.0f;
}

std::string HeadsetLabel(std::string_view runtimeName, std::string_view systemName) {
    std::string system = Trim(systemName);
    if (system.empty()) {
        return {};
    }
    std::string const lower = Lower(system);
    if (std::size_t const colon = lower.find(" : "); lower.rfind("steamvr", 0) == 0 && colon != std::string::npos) {
        std::string const driver = Trim(std::string_view(lower).substr(colon + 3));
        struct Known {
            char const* driver;
            char const* name;
        };
        static constexpr Known kKnown[] = {{"playstation_vr2", "PlayStation VR2"}, {"oculus", "Meta Quest"},
            {"vrlink", "Quest (Steam Link)"}, {"lighthouse", "Index / Vive"}, {"holographic", "Windows Mixed Reality"},
            {"pico", "Pico"}, {"virtual_desktop", "Virtual Desktop"}};
        system.clear();
        for (Known const& k : kKnown) {
            if (driver == k.driver) {
                system = k.name;
            }
        }
        if (system.empty()) {
            bool start = true;
            for (char const c : driver) {
                system.push_back(c == '_' ? ' ' : start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
                start = c == '_';
            }
        }
    }
    std::string const runtime = Lower(Trim(runtimeName));
    char const* via = runtime.find("steamvr") != std::string::npos      ? "SteamVR"
                    : runtime.find("oculus") != std::string::npos ||
                          runtime.find("meta") != std::string::npos      ? "Link"
                    : runtime.find("virtualdesktop") != std::string::npos ? "Virtual Desktop"
                    : runtime.find("vdxr") != std::string::npos           ? "Virtual Desktop"
                                                                          : "";
    return *via != '\0' ? system + " \xC2\xB7 " + via : system;
}

std::string ControllersLabel(std::string_view calibrationLabel) {
    std::string label = Trim(calibrationLabel);
    if (std::size_t const bracket = label.find(" ("); bracket != std::string::npos) {
        label.resize(bracket);
    }
    return label;
}

void PendingWrites::Put(std::string section, std::string key, std::string value, std::uint64_t dueMs) {
    for (Edit& edit : edits_) {
        if (edit.section == section && edit.key == key) {
            edit.value = std::move(value);
            edit.dueMs = dueMs;
            return;
        }
    }
    edits_.push_back({std::move(section), std::move(key), std::move(value), dueMs});
}

std::vector<PendingWrites::Edit> PendingWrites::TakeDue(std::uint64_t nowMs, bool all) {
    std::vector<Edit> due;
    std::vector<Edit> kept;
    for (Edit& edit : edits_) {
        (all || edit.dueMs <= nowMs ? due : kept).push_back(std::move(edit));
    }
    edits_ = std::move(kept);
    return due;
}

std::optional<std::string> PendingWrites::Overlay(std::string_view section, std::string_view key) const {
    for (Edit const& edit : edits_) {
        if (edit.section == section && edit.key == key) {
            return edit.value;
        }
    }
    return std::nullopt;
}

std::map<std::string, std::string> ParseSection(std::string_view lines) {
    std::map<std::string, std::string> out;
    std::size_t start = 0;
    while (start <= lines.size()) {
        std::size_t end = lines.find('\n', start);
        if (end == std::string_view::npos) {
            end = lines.size();
        }
        std::string_view line = lines.substr(start, end - start);
        start = end + 1;
        std::string const trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }
        std::size_t const eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        out[Lower(Trim(std::string_view(trimmed).substr(0, eq)))] = Trim(std::string_view(trimmed).substr(eq + 1));
        if (end == lines.size()) {
            break;
        }
    }
    return out;
}

} // namespace mevr::vr::menu_settings
