// SPDX-License-Identifier: MIT
#include "core/vr/MenuSettings.hpp"

#include "core/Ini.hpp"

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

std::string Upper(std::string_view text) {
    std::string out;
    for (char const c : text) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
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
    case Format::Signed0: return Signed(value, 0, "");
    case Format::Centimeters1: return Plain(value, 1, " cm");
    case Format::SignedDecibels: return Signed(value, 0, " dB");
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
        {"first_run", Type::Flag, 0, "", 0, 1, F::None, true},  // opened by itself on the controller guide (MainMenu's first run)
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
        // [Comfort] screenShake (0, 0.3, 1) and viewShake (game/CameraShake, research/71).
        {"screen_shake", Type::Choice, 0, "off"},
        {"view_shake", Type::Flag, 0},
        {"pause", Type::Flag, 1},
        // Comfort: conversations and cutscenes (research/52), each in VR (Halcyon's), on the 3D cinema screen, or the flat screen.
        {"cine_convo", Type::Choice, 0, "screen"},
        {"cine_cut", Type::Choice, 0, "screen"},
        {"cine_width", Type::Number, 2.9f, "", 1.5f, 8, F::Meters1},
        {"cine_distance", Type::Number, 3.5f, "", 1, 6, F::Meters1},
        {"cine_depth", Type::Number, 100, "", 0, 150, F::Percent},
        {"wrist_screen", Type::Choice, 0, "wrist"},  // the hacking screen: "wrist" (projected from it) or "room" (research/53)
        {"wrist_width", Type::Number, 0.7f, "", 0.4f, 1.0f, F::Meters2},  // [WorldUi] wristMenuWidthM (research/57)
        {"world_ui", Type::Flag, 1},     // [WorldUi] on: the game's screens in the room (research/53)
        {"galaxy_holo", Type::Flag, 1},  // [Galaxy] hologram: the galaxy map as a hologram over the CIC's table (research/51)
        // Controls.
        {"grip_holds", Type::Flag, 1},
        {"holsters", Type::Flag, 1},
        {"holster_buzz", Type::Number, 50, "", 0, 100, F::Percent},
        {"left_trigger", Type::Choice, 0, "lb"},
        {"throwables", Type::Flag, 1},
        {"recoil_buzz", Type::Number, 50, "", 0, 100, F::Percent},
        {"aim_assist", Type::Flag, 0},
        {"explore_stance", Type::Flag, 0},  // [Hands] exploreStance (research/38 A; off by default since 2026-10-25)
        {"constant_eye", Type::Flag, 1},    // [Hands] constantEye (B, the host)
        {"draw_rate", Type::Number, 6, "", 1, 10, F::Times1},  // [Hands] drawRate (D)
        {"gun_kick", Type::Flag, 1},       // [Hands] gunKick (research/24 section 8)
        {"fingers", Type::Flag, 1},        // [Hands] fingers (research/43)
        {"vent", Type::Flag, 1},           // [Vent] enabled (research/45)
        {"mako_controls", Type::Flag, 1},  // [Mako] controls: the lever and the stick (research/46)
        {"cockpit", Type::Flag, 1},        // [Cockpit] enabled (research/47)
        // The controller guide's texts (core/vr/MenuSettings GuideTexts; "<id>" the drawing's label, "<id>_how" the list's line).
        {"guide_lt", Type::Text, 0, ""},
        {"guide_lt_how", Type::Text, 0, ""},
        {"guide_lgrip", Type::Text, 0, ""},
        {"guide_lgrip_how", Type::Text, 0, ""},
        {"guide_lstick", Type::Text, 0, ""},
        {"guide_lstick_how", Type::Text, 0, ""},
        {"guide_x", Type::Text, 0, ""},
        {"guide_x_how", Type::Text, 0, ""},
        {"guide_y", Type::Text, 0, ""},
        {"guide_y_how", Type::Text, 0, ""},
        {"guide_menu", Type::Text, 0, ""},
        {"guide_menu_how", Type::Text, 0, ""},
        {"guide_rt", Type::Text, 0, ""},
        {"guide_rt_how", Type::Text, 0, ""},
        {"guide_rgrip", Type::Text, 0, ""},
        {"guide_rgrip_how", Type::Text, 0, ""},
        {"guide_rstick", Type::Text, 0, ""},
        {"guide_rstick_how", Type::Text, 0, ""},
        {"guide_a", Type::Text, 0, ""},
        {"guide_a_how", Type::Text, 0, ""},
        {"guide_b", Type::Text, 0, ""},
        {"guide_b_how", Type::Text, 0, ""},
        {"guide_grenade", Type::Text, 0, ""},
        {"guide_melee", Type::Text, 0, ""},
        {"guide_pick", Type::Text, 0, ""},
        {"guide_cast", Type::Text, 0, ""},
        {"guide_self", Type::Text, 0, ""},
        // Body and calibration.
        {"play_position", Type::Choice, 0, "standing"},
        {"position_known", Type::Flag, 0},
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
        {"upscale", Type::Choice, 0, "fsr"},  // FSR 1 by default (2026-11-05): it acts only when the target is below the eye
        {"steamvr_runtime", Type::Flag, 0, "", 0, 1, Format::None, true},
        {"steamvr_smoothing", Type::Choice, 0, "steamvr"},
        {"steamvr_smoothing_note", Type::Text, 0, ""},
        {"steady_cine", Type::Flag, 1},
        // Display's Performance row (core/vr/PerformanceModes, research/60): quality | balanced | performance | custom, what it
        // trades, what waits for a restart. Advanced > Performance's rows are the bundled levers one by one.
        {"perf_mode", Type::Choice, 0, "balanced"},
        {"perf_desc", Type::Text, 0, ""},
        {"perf_note", Type::Text, 0, ""},
        {"cull_px", Type::Choice, 0, "8"},                              // [Performance] minScreenRadiusPx
        {"grass", Type::Number, 50, "", 25, 100, F::Percent},         // [SystemSettings] FoliageDrawRadiusMultiplier x 100
        {"dyn_shadows", Type::Flag, 0},                                 // [SystemSettings] DynamicShadows
        {"ambient_occl", Type::Flag, 1},                                // [SystemSettings] AmbientOcclusion
        {"fsr_sharpness", Type::Number, 90, "", 0, 100, F::Percent},  // [Upscale] sharpness x 100
        // HUD and sights.
        {"hud_distance", Type::Number, 1.5f, "", 1, 3, F::Meters1},
        {"hud_size", Type::Number, 100, "", 45, 155, F::Percent},  // of 1.3 m: Halcyon's hudPanelWidthM, 0.6 to 2 m
        {"panel_below", Type::Number, 0.25f, "", -0.3f, 0.6f, F::SignedMeters2},
        {"panel_follow", Type::Number, 70, "", 0, 120, F::DegreesOrNever},
        {"reticle", Type::Flag, 1},
        {"wrist_status", Type::Flag, 1},    // [Hud] wristStatus (research/41)
        {"gun_status", Type::Flag, 1},      // [Hud] gunStatus
        {"interact_label", Type::Flag, 1},  // [Interact] label (research/40)
        {"sight_pistol", Type::Choice, 0, "dot"},
        {"sight_shotgun", Type::Choice, 0, "dot"},
        {"sight_rifle", Type::Choice, 0, "dot"},  // the assault rifle's (scope: its own, 1.5x)
        {"sight_sniper", Type::Choice, 0, "scope"},
        {"sight_assist", Type::Flag, 1},
        {"sight_converge", Type::Flag, 1},
        {"scope_grade", Type::Flag, 1},  // [Sights] scopeCapturePost (research/39)
        // Advanced: Halcyon's renderer.
        {"recenter_key", Type::Flag, 1},
        {"look_speed", Type::Number, 1, "", 0.25f, 2, F::Times2},
        {"look_smooth", Type::Number, 0.4f, "", 0, 0.9f, F::Fixed2},
        {"flat_menus", Type::Flag, 1},
        {"flat_galaxy", Type::Flag, 1},
        {"fp_on", Type::Flag, 0},
        {"fp_hide_head", Type::Flag, 1},
        {"fp_back", Type::Number, 0, "", -2, 2, F::SignedMeters2},
        {"fp_right", Type::Number, 0, "", -1, 1, F::SignedMeters2},
        {"fp_up", Type::Number, 0, "", -1, 1, F::SignedMeters2},
        {"lean", Type::Flag, 1},
        {"flat_size", Type::Number, 2.35f, "", 1, 3.5f, F::Meters2},
        {"fov_fill", Type::Flag, 1},
        {"vr_mode", Type::Choice, 0, "stereo"},
        {"mode_note", Type::Text, 0, ""},
        {"life_size", Type::Flag, 1},
        {"extra_depth", Type::Number, 1, "", 0.5f, 2, F::Times2},
        {"eye_frustum", Type::Flag, 1},
        {"eye_distance", Type::Number, 6.8f, "", 1, 16, F::Centimeters1},  // twice Halcyon's sfr2HalfEyeUU (game cm at life size)
        {"convergence", Type::Number, 0.012f, "", -0.02f, 0.05f, F::Fixed3},
        {"swap_eyes", Type::Flag, 1},
        {"quest_fov", Type::Flag, 1},
        {"dof", Type::Flag, 1},
        {"mirror", Type::Choice, 0, "smooth"},  // the deploy's mirrorPresentEvery = 2
        // Advanced > Game UI: Halcyon's size and place of the conversation wheel's and the mission overlays' movies (its HUD
        // tab's sliders), as a percentage of its tuned size and a nudge in the movie's pixels from its tuned place (up +).
        {"g_convo", Type::Flag, 1},
        {"convo_size", Type::Number, 100, "", 40, 250, F::Percent},
        {"convo_x", Type::Number, 0, "", -600, 600, F::Signed0},
        {"convo_y", Type::Number, 0, "", -600, 600, F::Signed0},
        {"g_designer", Type::Flag, 1},
        {"designer_size", Type::Number, 100, "", 40, 250, F::Percent},
        {"designer_x", Type::Number, 0, "", -900, 900, F::Signed0},
        {"designer_y", Type::Number, 0, "", -900, 900, F::Signed0},
        {"prof_active", Type::Choice, 0, "0"},
        {"prof0_name", Type::Text, 0, "Default"},
        {"prof1_name", Type::Text, 0, "Custom 1"},
        {"prof2_name", Type::Text, 0, "Custom 2"},
        {"prof3_name", Type::Text, 0, "Custom 3"},
        {"prof_msg", Type::Text, 0, ""},
        {"recording", Type::Flag, 0},
        {"diag_msg", Type::Text, 0, ""},
        {"perf_msg", Type::Text, 0, ""},
        {"content_installed", Type::Text, 0, ""},  // Display > Optional content (research/33)
        {"content_msg", Type::Text, 0, ""},
        // Report a problem (Diagnostics, research/33 section 8): what happened to the last request, the crash offer, and
        // what the zip should also hold (the menu's own choices, sent with the request).
        {"report_msg", Type::Text, 0, ""},
        {"crash_offer", Type::Flag, 0},
        {"report_snapshot", Type::Flag, 0, "", 0, 1, F::None, true},
        {"report_perf", Type::Flag, 0, "", 0, 1, F::None, true},
        // Status and per-machine texts (2026-10-26).
        {"save_msg", Type::Text, 0, ""},            // the top bar: the settings file couldn't be written
        {"throwables_note", Type::Text, 0, ""},     // grenades and melee set apart in the file
        {"recenter_key_desc", Type::Text, 0, "R on the keyboard recenters (the right stick's double-click works either way)"},
        {"classic_desc", Type::Text, 0, "The original MELE-VR settings menu, with expert settings these pages don't have (also: Ctrl + "
                                        "the menu key)"},
        {"about_build", Type::Text, 0, ""},  // Diagnostics > About: version and commit (AboutBuild)
        // Voice (research/54 "The menu's Voice page"): [Voice] in <GAME>MEVR.ini and the host's recognizer (plugin/Voice).
        {"voice_on", Type::Flag, 1},                                 // [Voice] enabled
        {"voice_status", Type::Text, 0, ""},                         // VoiceStatusText
        {"voice_ready", Type::Flag, 0},                              // the recognizer is ready (the status line's colour)
        {"voice_level", Type::Number, 0, "", 0, 100, F::Percent},    // the bar (VoiceMeterPercent), set by MainMenu each frame
        {"voice_heard", Type::Flag, 0},                              // the bar went past kVoiceHeardPercent in the last 1.5 s
        {"mic", Type::Choice, 0, "default"},                         // "default", "0".."7", or "missing"
        {"mic_count", Type::Number, 0, "", 0, kMicSlots},
        {"mic0", Type::Text, 0, ""},
        {"mic1", Type::Text, 0, ""},
        {"mic2", Type::Text, 0, ""},
        {"mic3", Type::Text, 0, ""},
        {"mic4", Type::Text, 0, ""},
        {"mic5", Type::Text, 0, ""},
        {"mic6", Type::Text, 0, ""},
        {"mic7", Type::Text, 0, ""},
        {"mic_default", Type::Text, 0, "Windows' default communications microphone"},
        {"mic_missing", Type::Text, 0, ""},                          // [Voice] device when no listed device holds it
        {"mic_note", Type::Text, 0, ""},                             // why there's no list
        {"voice_model", Type::Choice, 0, "tiny"},                    // [Voice] model
        {"voice_key", Type::Text, 0, "(none)"},                      // [Voice] key by name
        {"voice_key_wait", Type::Flag, 0},                           // waiting for the key to press
        {"ear_cue", Type::Number, 40, "", 0, 100, F::Percent},       // [Voice] earCueStrength x 100 (the mod's)
        // Sound (research/69 A3): [Audio] in <GAME>MEVR.ini. spatial and gainDb are the host's, read once when the game starts
        // (plugin/OpenAlSoft); overheard and gun the mod's, live (game/SoundPlace).
        {"audio_spatial", Type::Choice, 0, "hrtf"},                                                       // [Audio] spatial
        {"audio_gain", Type::Number, kAudioGainDefaultDb, "", kAudioGainMinDb, kAudioGainMaxDb, F::SignedDecibels},  // [Audio] gainDb
        {"audio_now", Type::Text, 0, ""},                            // what the game's audio runs on now (openal_soft::Describe)
        {"audio_note", Type::Text, 0, "Applies when the game next starts"},  // AudioNote
        {"audio_overheard", Type::Flag, 1},                          // [Audio] overheard: people near the player from where they stand
        {"audio_gun", Type::Flag, 1},                                // [Audio] gun: the player's gun from the gun
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

Size FirstStartSize(bool wanted, bool alreadySet, Size current, int recommendedWidth, int recommendedHeight) {
    if (!wanted || alreadySet) {
        return {};
    }
    // 0.85 of it (the owner, 2026-11-05: the dev settings are the defaults; theirs was 0.83), which FSR, on by default,
    // scales up to the eye.
    Size const size = HeadsetSize("headset85", recommendedWidth, recommendedHeight);
    if (size.width <= 0 || size.height <= 0 || size == current) {
        return {};
    }
    return size;
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

std::string SightValue(std::string_view kind, std::string_view previous, float fallbackZoom) {
    if (kind != "scope") {
        return std::string(kind);
    }
    std::string const text = Lower(Trim(previous));
    float zoom = fallbackZoom >= 1.0f && fallbackZoom <= 12.0f ? fallbackZoom : fallbackZoom == 0.0f ? 0.0f : 4.0f;
    if (text.rfind("scope", 0) == 0 && text.size() > 5) {
        std::string const rest = Trim(std::string_view(text).substr(5));
        if (rest == "auto" || rest == "game") {
            return "scope auto";  // the held weapon's own (research/21 section 16)
        }
        float const parsed = static_cast<float>(ini::Number(rest).value_or(0.0));
        if (parsed >= 1.0f && parsed <= 12.0f) {
            zoom = parsed;
        }
    }
    if (zoom == 0.0f) {
        return "scope auto";
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

std::vector<std::pair<std::string, std::string>> GuideTexts(GuideInput const& in) {
    std::vector<std::pair<std::string, std::string>> out;
    auto put = [&out](char const* id, std::string label, std::string how) {
        out.emplace_back(std::string("guide_") + id, std::move(label));
        out.emplace_back(std::string("guide_") + id + "_how", std::move(how));
    };
    // Where the powers are (the game's RB: hold = the power wheel, tap = the power mapped in it). The right grip is RB
    // unless it holds the gun (holsters with gripHolds); then [Holsters] powerWheel names the control that is.
    bool const gripIsRb = !(in.holsters && in.gripHolds);
    std::string const power = gripIsRb ? std::string("grip") : Lower(Trim(in.powerWheel));
    std::string const powersHow = "hold for the power wheel, tap for the power you mapped in it";
    bool const grenadeRight = in.grenades && Lower(Trim(in.grenadeHands)) != "left";
    bool const grenadeLeft = in.grenades && (Lower(Trim(in.grenadeHands)) == "left" || Lower(Trim(in.grenadeHands)) == "both");
    std::string const lt = Lower(Trim(in.leftTrigger));

    // ---- The left controller ----
    if (lt == "lt") {
        put("lt", "Aim", "The game's aim button: steadier, zoomed fire. Aimed fire also comes from holding the gun at your cheek "
                         "with both hands.");
    } else if (lt == "lb" && in.squad && Lower(Trim(in.squadStyle)) == "custom") {
        put("lt", "Squad weapons", "Pull for the squad weapon menu at your hand: the left stick picks a squadmate, a flick of the "
                                   "right stick a weapon; let go to give it, B cancels.");
    } else if (lt == "lb" && in.squad) {
        put("lt", "Weapon wheel", "Pull to open the weapon wheel at your hand. Point at a squadmate to arm them; the right stick "
                                  "picks a weapon; let go to choose it.");
    } else if (lt == "lb") {
        put("lt", "Weapon wheel", "Hold for the game's weapon wheel; tap to switch to your other weapon.");
    } else {
        put("lt", "Nothing", "Off (Controls > Left trigger). Aimed fire comes from holding the gun at your cheek with both hands.");
    }
    put("lgrip", in.vent ? "Two-handed grip, vent" : "Two-handed grip",
        std::string("With your hand on the gun: hold it with both hands; at your cheek that is aimed fire.") +
            (in.vent ? " On a hot gun, squeeze just ahead of your right hand and rack it back to vent the heat." : "") +
            (grenadeLeft ? " At the left of your chest: take a grenade." : ""));
    std::string const toward = in.walk == "hand" ? "where your left hand points" : in.walk == "body" ? "where Shepard faces"
                                                                                                   : "where you look";
    put("lstick", "Walk, click: crouch",
        "Walk toward " + toward + ". Click to crouch or stand" + (in.seated ? ". Seated play: lowering your head never crouches Shepard."
            : in.physicalCrouch ? "; or just crouch for real." : "."));
    if (in.handWheel) {
        put("x", "Power wheel", std::string("Hold for your powers around your left hand: move the hand toward one and let go of X to "
                                            "equip it (a tap keeps the wheel open).") +
                                    " X is only the power wheel on foot, for taps and holds.");
    } else if (power == "x") {
        put("x", "Powers", "Powers: " + powersHow + ".");
    } else if (in.grenadeButton) {
        put("x", "Grenade", "Throw a grenade the game's way.");
    } else {
        put("x", "Nothing", "Nothing on foot: take grenades from your chest.");
    }
    if (power == "y") {
        put("y", "Powers", "Powers: " + powersHow + ". Medi-gel is in the power wheel.");
    } else {
        put("y", "Medi-gel", "Use medi-gel: heals you and your squad.");
    }
    put("menu", "Game menu", "The game's pause menu, on controllers that send this button. The Game menu button at the top of "
                             "this menu always opens it.");

    // ---- The right controller ----
    put("rt", "Fire", "Fire the gun in your hand.");
    if (!in.holsters) {
        put("rgrip", "Powers", "Powers: " + powersHow + ".");
    } else if (!in.gripHolds) {
        put("rgrip", "Draw, powers", "At a holster on your body: squeeze to draw that weapon; squeeze and let go there to put it "
                                     "away. Anywhere else, powers: " + powersHow + ".");
    } else {
        put("rgrip", "Draw and holster", "Squeeze at a holster on your body to draw that weapon and keep squeezing to hold "
                                              "it. Let go at its holster to put it away; anywhere else it goes back by itself.");
    }
    std::string const turnHow = in.turn == "snap" ? "Turn in steps (Comfort > Turn style)." : "Turn (Comfort > Turn style).";
    if (power == "r3") {
        put("rstick", "Turn, hold: powers", turnHow + " Hold the click: the game's power wheel, steered with this stick; "
                                            "let go to close it. Double-click: recenter the view.");
    } else {
        put("rstick", "Turn", turnHow + " Double-click the stick to recenter the view.");
    }
    put("a", "Use, hold: sprint", "Use what you look at: doors, consoles, people, loot. Hold to sprint.");
    if (power == "b") {
        put("b", "Powers", "Powers: " + powersHow + ".");
    } else if (in.meleeButton) {
        put("b", "Melee", "Strike with your weapon.");
    } else {
        put("b", "Back (menus)", "Nothing on foot: melee is a real strike (below). In menus it backs out.");
    }

    // ---- Gestures ----
    std::string const grenadeHand = grenadeRight && grenadeLeft ? "Either hand" : grenadeLeft ? "Left hand" : "Right hand";
    out.emplace_back("guide_grenade", in.grenades ? grenadeHand + " to the left of your chest, squeeze the grip to take a "
                                                    "grenade, then throw: let go mid-swing. Let go slowly to put it back."
                                                  : std::string("Off (Controls > Physical grenades and melee)."));
    out.emplace_back("guide_melee", in.melee ? std::string("Strike fast at an enemy within reach, with either hand, about once a "
                                                           "second.") + (in.meleeFist ? " With your weapon away, punch with your "
                                                                                        "right fist: squeeze the grip and the trigger."
                                                                                      : "")
                                             : std::string("Off (Controls > Physical grenades and melee)."));

    // ---- Powers (research/55): the wheel at the left hand, the lock and the push, the self-cast gestures ----
    std::string const gameWheel = power == "r3" ? "hold the right stick click for the game's power wheel"
                                : power == "grip" ? "hold the right grip for the game's power wheel"
                                : power == "none" || power.empty() ? "the game's power wheel isn't on a button (Controls)"
                                                                   : "hold " + Upper(power) + " for the game's power wheel";
    out.emplace_back("guide_pick", in.handWheel ? "Hold X: your powers around your left hand. Move the hand toward one and let go of "
                                                  "X to equip it. A card then shows how to cast it. No power, the last tile, puts "
                                                  "them away."
                                                : "The power wheel at your hand is off ([PowerWheel] on): " + gameWheel + ".");
    out.emplace_back("guide_cast", in.handWheel && in.castLock
                                       ? std::string("Raise your left hand at an enemy: orange brackets lock on (or on the one you face). "
                                                     "Push your open hand toward it to cast what you equipped. Powers that land somewhere "
                                                     "go where your hand points. Your right hand can keep the gun.")
                                       : "Cast from the game's power wheel: " + gameWheel + ".");
    out.emplace_back("guide_self", !(in.handWheel && in.castLock) ? std::string("From the game's power wheel, like the others.")
                                   : in.selfGestures ? std::string("Powers on yourself are gestures of the open left hand: flat on "
                                                                   "your chest, held (Barrier, Shield Boost, Immunity, Adrenaline "
                                                                   "Burst); slapped onto the gun (Marksman, Overkill, Carnage, "
                                                                   "Assassination: each on its own gun); raised above your head, "
                                                                   "held (Unity). The glowing target shows when it's ready.")
                                                     : std::string("Pushed forward like the others."));
    return out;
}

std::string AboutBuild(std::string_view version, std::string_view commit, bool development) {
    std::string text = "MEVR " + (version.empty() ? std::string("(no version)") : std::string(version));
    if (!commit.empty() && commit != "unknown") {
        text += " \xC2\xB7 commit " + std::string(commit);
    }
    if (development) {
        text += " \xC2\xB7 development build";
    }
    return text;
}

std::string VoiceModelChoice(std::string_view value) {
    std::string const v = Lower(Trim(value));
    return v == "small" || v == "medium" ? v : std::string("tiny");
}

int MicIndex(std::vector<std::string> const& names, std::string_view setting) {
    std::string const want = Lower(setting);
    if (want.empty()) {
        return -1;
    }
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (Lower(names[i]).find(want) != std::string::npos) {
            return static_cast<int>(i);
        }
    }
    return -2;
}

float VoiceMeterPercent(float rms) {
    if (!std::isfinite(rms) || rms <= 1e-6f) {
        return 0.0f;
    }
    float const db = 20.0f * std::log10(rms);
    return std::clamp((db + 60.0f) / 60.0f * 100.0f, 0.0f, 100.0f);
}

float VoiceMeterStep(float shown, float target, float dtS, float fallPerS) {
    if (!std::isfinite(shown)) shown = 0.0f;
    if (!std::isfinite(target)) target = 0.0f;
    if (target >= shown) {
        return target;
    }
    return std::max(target, shown - std::max(dtS, 0.0f) * fallPerS);
}

std::string VoiceStatusText(VoiceStatusInput const& in) {
    if (in.restarting) {
        return "Restarting the recognizer with the new settings\xE2\x80\xA6";
    }
    if (!in.enabled) {
        return "Voice orders are off.";
    }
    switch (in.status) {
    case 1:
        return "Loading the recognizer\xE2\x80\xA6";
    case 2: {
        std::string text = in.listening ? "Listening" : "Ready";
        if (!in.device.empty()) {
            text += " \xC2\xB7 microphone: " + in.device;
        } else {
            text += " \xC2\xB7 no microphone opened";
        }
        if (!in.model.empty()) {
            text += " \xC2\xB7 " + in.model + " model";
        }
        if (!in.error.empty()) {
            text += " \xC2\xB7 " + in.error;
        }
        return text;
    }
    case 3:
        return "The recognizer failed: " + (in.error.empty() ? std::string("see LE1MEVR.log") : in.error);
    default:
        break;
    }
    if (in.error.find("MEVR-Voice.dll") != std::string::npos || in.error.find("models") != std::string::npos) {
        return "Not installed: add Voice orders in Display > Optional content (" + in.error + ")";
    }
    return "Off" + (in.error.empty() ? std::string(".") : ": " + in.error);
}

int VoiceKeyFromPress(int vk, int menuKey) {
    if (vk <= 0 || vk > 0xFE) {
        return 0;
    }
    switch (vk) {
    case 0x01: case 0x02: case 0x04: case 0x05: case 0x06:  // the mouse buttons
    case 0x1B:                                              // Escape
    case 0x23: case 0x24:                                   // End, Home: the test loop's snapshot and recording
    case 0x10: case 0x11: case 0x12:                        // Shift, Ctrl, Alt (the generic ones; their left and right are fine)
        return 0;
    default:
        break;
    }
    return vk == menuKey ? 0 : vk;
}

std::string AudioSpatialChoice(std::string_view value) {
    std::string const v = Lower(Trim(value));
    if (v == "game" || v == "off" || v == "0" || v == "creative") {
        return "game";
    }
    if (v == "soft" || v == "pan" || v == "speakers") {
        return "soft";
    }
    return "hrtf";
}

std::string AudioNote(std::string_view chosen, float chosenGainDb, std::string_view running, float runningGainDb, bool softInstalled) {
    std::string const want = AudioSpatialChoice(chosen);
    std::string const now = AudioSpatialChoice(running);
    if (want != "game" && !softInstalled) {
        return "OpenAL Soft isn't installed, so the game's own sound plays: MEVR Setup's Repair puts it back";
    }
    bool const gainMatters = want != "game";
    bool const same = want == now && (!gainMatters || std::abs(chosenGainDb - runningGainDb) < 0.05f);
    return same ? "In use now" : "Applies when the game next starts";
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
    // core/Ini's reader (the mod's too): lower-cased keys, the first occurrence wins as in Win32's profile API.
    ini::Section const section = ini::ParseKeys(lines);
    return std::map<std::string, std::string>(section.begin(), section.end());
}

} // namespace mevr::vr::menu_settings
