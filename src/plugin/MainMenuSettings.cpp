// What each row of our main menu drives (plugin/MainMenuSettings.hpp, research/28 section 4).

#include "plugin/MainMenuSettings.hpp"

#include "halcyon/le1/d3d_capture.h"
#include "halcyon/le1/mevr_bridge.h"
#include "halcyon/le1/vr_config.h"
#include "halcyon/le1/vr_menu.h"

#include "core/Log.hpp"
#include "plugin/Calibration.hpp"
#include "plugin/Colour.hpp"
#include "plugin/MevrMenu.hpp"
#include "plugin/PerfProbe.hpp"
#include "plugin/TestLoop.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <map>
#include <optional>
#include <vector>

namespace mevr::host::main_menu {
namespace {

namespace ms = vr::menu_settings;
namespace cv = vr::colour;

constexpr std::uint64_t kSliderRestMs = 300;    // a slider's value is written once it has rested this long
constexpr std::uint64_t kColourRestMs = 400;
constexpr std::uint64_t kHalcyonRestMs = 2000;  // Halcyon's profile saved this long after the last change
constexpr std::uint64_t kResetArmMs = 4000;
constexpr std::uint64_t kMeasureTimeoutMs = 30000;

std::uint64_t Now() {
    return GetTickCount64();
}

std::wstring Wide(std::string_view text) {
    std::wstring out;
    for (char const c : text) {
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string Narrow(std::wstring_view text) {
    std::string out;
    for (wchar_t const c : text) {
        out.push_back(c < 128 ? static_cast<char>(c) : '?');
    }
    return out;
}

std::map<std::string, std::string> ReadSection(std::wstring const& path, wchar_t const* section) {
    std::vector<wchar_t> buffer(16384);
    DWORD const n = GetPrivateProfileSectionW(section, buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    std::string text;
    text.reserve(n);
    for (DWORD i = 0; i < n; ++i) {
        wchar_t const c = buffer[i];
        text.push_back(c == L'\0' ? '\n' : (c < 128 ? static_cast<char>(c) : '?'));
    }
    return ms::ParseSection(text);
}

std::string Lower(std::string_view text) {
    std::string out;
    for (char const c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::optional<float> Number(std::optional<std::string> const& text) {
    if (!text || text->empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    double const v = std::strtod(text->c_str(), &end);
    if (end == text->c_str() || !std::isfinite(v)) {
        return std::nullopt;
    }
    return static_cast<float>(v);
}

std::string Fixed(std::string const& text, int decimals) {
    std::optional<float> const v = Number(text);
    return v ? std::format("{:.{}f}", *v, decimals) : std::string("\xE2\x80\x93");
}

// The settings file's sections as they are now, with the edits not yet written on top.
struct Ini {
    std::map<std::string, std::map<std::string, std::string>> sections;
    ms::PendingWrites const* pending = nullptr;

    std::optional<std::string> Get(char const* section, char const* key) const {
        if (pending != nullptr) {
            if (auto v = pending->Overlay(section, key)) {
                return v;
            }
        }
        auto const s = sections.find(section);
        if (s == sections.end()) {
            return std::nullopt;
        }
        auto const k = s->second.find(Lower(key));
        return k != s->second.end() ? std::optional<std::string>(k->second) : std::nullopt;
    }
    float Num(char const* section, char const* key, float fallback) const {
        return Number(Get(section, key)).value_or(fallback);
    }
    std::string Text(char const* section, char const* key, char const* fallback) const {
        return Get(section, key).value_or(fallback);
    }
};

char const* ModeChoice(MELEVR::Config::VrConfig const& c) {
    return c.sfr2Enabled ? "stereo" : c.dibrEnabled ? "dibr" : c.aerEnabled ? "aer" : "mono";
}

} // namespace

Settings::Settings() {
    ReadMenuSection();
}

std::filesystem::path Settings::File() const {
    return std::filesystem::path(MELEVR::MevrBridge::SettingsPath());
}

std::filesystem::path Settings::Assets() const {
    std::error_code ec;
    if (!assets_.empty() && std::filesystem::exists(std::filesystem::path(assets_) / L"main.rml", ec)) {
        return assets_;
    }
    return File().parent_path() / L"menu";
}

std::filesystem::path Settings::Fonts() const {
    return File().parent_path() / L"fonts";
}

void Settings::ReadMenuSection() {
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    if (path.empty()) {
        return;
    }
    nextMenuReadMs_ = Now() + 1000;
    auto const menu = ReadSection(path, L"Menu");
    if (menu.find("pause") == menu.end()) {
        // The section the player may edit by hand: its keys written once, each on its own (never the whole file).
        WritePrivateProfileStringW(L"Menu", L"pause", L"1", path.c_str());
        WritePrivateProfileStringW(L"Menu", L"distanceM", L"1.30", path.c_str());
        WritePrivateProfileStringW(L"Menu", L"widthM", L"1.45", path.c_str());
        WritePrivateProfileStringW(L"Menu", L"assets", L"", path.c_str());
    }
    Ini ini;
    ini.sections["Menu"] = menu;
    ini.pending = &pending_;
    pause_ = ini.Num("Menu", "pause", 1.0f) != 0.0f;
    distance_ = std::clamp(ini.Num("Menu", "distanceM", 1.3f), 0.6f, 3.0f);
    width_ = std::clamp(ini.Num("Menu", "widthM", 1.45f), 0.6f, 3.0f);
    assets_ = Wide(ini.Text("Menu", "assets", ""));
}

void Settings::Read(menu::Values& v) {
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    Ini ini;
    ini.pending = &pending_;
    if (!path.empty()) {
        for (wchar_t const* s : {L"Hands", L"Holsters", L"Controls", L"Throwables", L"Sights", L"Camera", L"Arms", L"Menu"}) {
            ini.sections[Narrow(s)] = ReadSection(path, s);
        }
    }
    auto flag = [&](char const* key, bool value) { v.numbers[key] = value ? 1.0f : 0.0f; };

    // Comfort.
    MELEVR::Config::VrConfig const& c = MELEVR::Config::Get();
    v.texts["walk"] = c.moveFollowsHead ? "head" : "body";
    flag("crouch", ini.Num("Hands", "physicalCrouch", 1.0f) != 0.0f);
    flag("lifesize", ini.Num("Camera", "lifeSize", 0.0f) != 0.0f);
    flag("pause", ini.Num("Menu", "pause", 1.0f) != 0.0f);
    // Controls.
    flag("grip_holds", ini.Num("Holsters", "gripHolds", 1.0f) != 0.0f);
    flag("holsters", ini.Num("Holsters", "enabled", 1.0f) != 0.0f);
    v.numbers["holster_buzz"] = std::round(std::clamp(ini.Num("Holsters", "hapticAmplitude", 0.5f), 0.0f, 1.0f) * 100.0f);
    {
        std::string const lt = Lower(ini.Text("Controls", "leftTrigger", "lb"));
        v.texts["left_trigger"] = lt == "lt" ? "lt" : lt == "lb" ? "lb" : "none";
    }
    flag("throwables", ini.Num("Throwables", "enabled", 1.0f) != 0.0f);
    v.numbers["recoil_buzz"] = std::round(std::clamp(ini.Num("Hands", "recoilHaptic", 0.5f), 0.0f, 1.0f) * 100.0f);
    flag("aim_assist", ini.Num("Hands", "aimAssist", 0.0f) != 0.0f);
    // Body.
    v.numbers["trim_pitch"] = ini.Num("Hands", "handPitch", 0.0f);
    v.numbers["trim_yaw"] = ini.Num("Hands", "handYaw", 0.0f);
    v.numbers["trim_roll"] = ini.Num("Hands", "handRoll", 0.0f);
    calibration::Summary const cal = calibration::GetSummary();
    flag("cal_running", cal.running);
    flag("calibrated", cal.calibrated);
    std::string const controllers = ms::ControllersLabel(cal.device);
    v.texts["cal_status"] = cal.running      ? std::format("Calibrating now (step: {})", cal.step)
                          : cal.calibrated   ? std::format("Calibrated {} \xC2\xB7 {}", cal.when, controllers)
                          : controllers.empty() ? std::string("The controllers aren't known yet: calibrate once they're tracked")
                                                : std::format("Not calibrated for {} yet: the guns follow the controller's aim pose", controllers);
    v.texts["cal_action"] = cal.calibrated ? "Calibrate again" : "Start calibration";
    v.texts["t_stand"] = Fixed(cal.standEyeM, 2);
    v.texts["t_crouch"] = Fixed(cal.crouchEyeM, 2);
    v.texts["t_arm"] = Fixed(ini.Text("Arms", "armLengthM", ""), 2);
    v.texts["t_eye"] = cal.eye == "left" ? "Left" : cal.eye == "right" ? "Right" : "\xE2\x80\x93";
    v.texts["t_pistol"] = Fixed(cal.pistolSpreadDeg, 1);
    v.texts["t_rifle"] = Fixed(cal.rifleSpreadDeg, 1);
    v.texts["measure_msg"] = measureMsg_;
    std::string const align = MELEVR::MevrBridge::CameraAlignResult();
    v.texts["align_msg"] = MELEVR::MevrBridge::CameraAlignBusy() ? std::string("Armed: stand naturally and pull the left trigger") : align;
    // Display.
    colour::MenuState const colour = colour::GetMenuState();
    v.texts["profile"] = cv::Name(colour.chosen);
    std::string const headset = ms::HeadsetLabel(colour.runtime, colour.system);
    switch (colour.chosen) {
    case cv::Profile::Auto:
        v.texts["profile_info"] = colour.effective == cv::Profile::Quest
            ? std::format("Detected {}: its profile deepens the mid-tones and lifts saturation, offsetting the flat look of the "
                          "streamed picture.", headset.empty() ? std::string("a streamed Quest") : headset)
            : std::format("Detected {}: no correction, the game's colours exactly as rendered.",
                  headset.empty() ? std::string("no headset yet") : headset);
        break;
    case cv::Profile::Neutral: v.texts["profile_info"] = "No correction: the game's colours exactly as rendered. Right for OLED headsets such as PS VR2."; break;
    case cv::Profile::Vivid: v.texts["profile_info"] = "Stronger contrast and colour than the headset profile."; break;
    case cv::Profile::Quest: v.texts["profile_info"] = "The streamed-Quest profile (chosen in the settings file)."; break;
    case cv::Profile::Custom: v.texts["profile_info"] = "Your own values, saved for this headset. Moving a slider makes them yours."; break;
    }
    if (colour.compareInFile) {
        v.texts["profile_info"] += " ([Colour] compare = 1 in the file: shown uncorrected.)";
    }
    cv::Settings const shown = colourDirty_ ? colour_ : colour.settings;
    if (!colourDirty_) {
        colour_ = colour.settings;
    }
    v.numbers["c_brightness"] = ms::ColourPercent(shown.brightness);
    v.numbers["c_contrast"] = ms::ColourPercent(shown.contrast);
    v.numbers["c_saturation"] = ms::ColourPercent(shown.saturation);
    v.numbers["c_midtones"] = shown.gamma;
    v.numbers["c_black"] = shown.black;
    v.texts["quality"] = ms::QualityFor(c.sfr2ResX);
    v.texts["quality_text"] = ms::QualityText(c.sfr2ResX);
    v.texts["quality_note"] = "applies after a restart";
    flag("steady_cine", c.cineFpsCap);
    // HUD and sights.
    v.numbers["hud_distance"] = c.hudPanelDistanceM;
    v.numbers["hud_size"] = std::round(c.hudPanelWidthM / 1.3f * 100.0f);
    flag("reticle", c.handReticle);
    v.texts["sight_pistol"] = ms::SightKind(ini.Text("Sights", "pistol", "dot"), "dot");
    v.texts["sight_rifle"] = ms::SightKind(ini.Text("Sights", "assaultRifle", "dot"), "dot");
    v.texts["sight_sniper"] = ms::SightKind(ini.Text("Sights", "sniperRifle", "scope 4"), "scope");
    flag("sight_assist", ini.Num("Sights", "assist", 1.0f) != 0.0f);
    flag("sight_converge", ini.Num("Sights", "converge", 1.0f) != 0.0f);
    // Advanced: Halcyon's.
    flag("recenter_key", c.recenterKeyEnabled);
    v.numbers["look_speed"] = c.lookSensitivity;
    v.numbers["look_smooth"] = c.headLookSmoothing;
    flag("vr_convo", c.cineVrConvo);
    flag("vr_cine", c.cineVrCutscene);
    flag("flat_menus", c.menuFlat);
    flag("flat_galaxy", c.galaxyFlat);
    flag("fp_on", c.fpEnabled);
    flag("fp_hide_head", c.fpHideHead);
    v.numbers["fp_back"] = c.flatScreenDistance;
    v.numbers["fp_right"] = c.cameraOffsetRight;
    v.numbers["fp_up"] = c.cameraOffsetUp;
    flag("lean", c.leanEnabled);
    v.numbers["lean_gain"] = c.leanGain;
    v.numbers["flat_size"] = c.menuMonoQuadWidth;
    flag("fov_fill", c.vrFovFillEnabled);
    v.texts["vr_mode"] = ModeChoice(c);
    v.texts["mode_note"] = modeNote_;
    flag("life_size", c.sfr2Metric);
    v.numbers["extra_depth"] = c.sfr2MetricDepth;
    v.numbers["eye_distance"] = c.sfr2HalfEyeUU;
    v.numbers["convergence"] = c.sfr2Convergence;
    flag("swap_eyes", c.sfr2SwapEyes);
    flag("quest_fov", c.questFovMatch);
    flag("dof", !c.disableDof);
    v.texts["mirror"] = ms::MirrorChoice(c.mirrorPresentEvery);
    v.numbers["panel_width"] = c.hudPanelWidthM;
    v.numbers["panel_below"] = c.hudPanelDropM;
    v.numbers["panel_follow"] = c.hudPanelFollowDeg;
    flag("g_pchud", c.pchudEnabled);
    flag("g_convo", c.convoEnabled);
    flag("g_designer", c.designerUiEnabled);
    v.texts["prof_active"] = std::to_string(MELEVR::Config::ActiveProfileIndex());
    for (int i = 0; i < 4; ++i) {
        v.texts[std::format("prof{}_name", i)] = MELEVR::Config::ProfileName(i);
    }
    v.texts["prof_msg"] = profMsg_;
    flag("recording", testloop::GetStatus().recording);
    flag("reset_armed", resetArmedMs_ != 0);
    v.texts["diag_msg"] = diagMsg_;
    perf::Status const p = perf::GetStatus();
    v.texts["perf_msg"] = p.pending ? std::format("Measuring: {:.0f} s of 20", p.seconds) : perfMsg_;
    // The top bar.
    v.texts["chip_headset"] = headset.empty() ? std::string("Headset not started") : headset;
    v.texts["chip_controllers"] = controllers.empty() ? std::string("No controllers yet") : controllers;
    v.texts["chip_calibrated"] = cal.calibrated ? "Calibrated" : "Not calibrated";
}

void Settings::Put(char const* section, char const* key, std::string const& value, bool now) {
    pending_.Put(section, key, value, Now() + (now ? 0 : kSliderRestMs));
    if (now) {
        Flush(false);
    }
}

void Settings::Flush(bool all) {
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    std::uint64_t const now = Now();
    for (ms::PendingWrites::Edit const& edit : pending_.TakeDue(now, all)) {
        if (path.empty()) {
            continue;
        }
        WritePrivateProfileStringW(Wide(edit.section).c_str(), Wide(edit.key).c_str(), Wide(edit.value).c_str(), path.c_str());
        log::Info("main menu: [{}] {} = {}", edit.section, edit.key, edit.value);
    }
    if (colourDirty_ && (all || now - colourChangedMs_ >= kColourRestMs)) {
        colourDirty_ = false;
        colour::SetCustom(colour_, true);
    }
    if (halcyonDirty_ && (all || now - halcyonChangedMs_ >= kHalcyonRestMs)) {
        SaveHalcyon(all ? "the menu closed" : "settled");
    }
}

void Settings::HalcyonChanged(bool saveNow) {
    halcyonDirty_ = true;
    halcyonChangedMs_ = Now();
    if (saveNow) {
        SaveHalcyon("a setting its own menu saves at once");
    }
}

void Settings::SaveHalcyon(char const* why) {
    halcyonDirty_ = false;
    if (noSave_) {
        log::Info("main menu: renderer settings not saved ({}): they were reset this session", why);
        return;
    }
    int const active = MELEVR::Config::ActiveProfileIndex();
    MELEVR::Config::SaveProfile(active);  // Halcyon's own profile save: the live config into the active slot
    log::Info("main menu: renderer settings saved to profile {} ({})", MELEVR::Config::ProfileName(active), why);
}

void Settings::Write(std::string const& key, menu::Values const& v, bool final) {
    auto onOff = [&](char const* k) { return std::string(v.Flag(k) ? "1" : "0"); };
    auto fixed = [&](float value, int decimals) { return std::format("{:.{}f}", value, decimals); };
    MELEVR::Config::VrConfig& c = MELEVR::Config::Get();
    // ---- Our settings file ----
    if (key == "crouch") {
        Put("Hands", "physicalCrouch", onOff("crouch"), true);
    } else if (key == "lifesize") {
        // As the VR hands tab's checkbox: Halcyon's lean gain follows (2: one to one with Shepard's skeleton).
        bool const on = v.Flag("lifesize");
        Put("Camera", "lifeSize", on ? "1" : "0", true);
        c.leanGain = on ? 2.0f : 1.0f;
        HalcyonChanged(false);
    } else if (key == "pause") {
        Put("Menu", "pause", onOff("pause"), true);
        pause_ = v.Flag("pause");
    } else if (key == "grip_holds") {
        Put("Holsters", "gripHolds", onOff("grip_holds"), true);
    } else if (key == "holsters") {
        Put("Holsters", "enabled", onOff("holsters"), true);
    } else if (key == "holster_buzz") {
        Put("Holsters", "hapticAmplitude", fixed(v.Number("holster_buzz") / 100.0f, 2), final);
    } else if (key == "left_trigger") {
        Put("Controls", "leftTrigger", v.Text("left_trigger"), true);
    } else if (key == "throwables") {
        Put("Throwables", "enabled", onOff("throwables"), true);
        Put("Throwables", "melee", onOff("throwables"), true);
    } else if (key == "recoil_buzz") {
        Put("Hands", "recoilHaptic", fixed(v.Number("recoil_buzz") / 100.0f, 2), final);
    } else if (key == "aim_assist") {
        Put("Hands", "aimAssist", onOff("aim_assist"), true);
    } else if (key == "trim_pitch" || key == "trim_yaw" || key == "trim_roll") {
        char const* name = key == "trim_pitch" ? "handPitch" : key == "trim_yaw" ? "handYaw" : "handRoll";
        Put("Hands", name, fixed(v.Number(key), 1), final);
    } else if (key == "sight_pistol") {
        Put("Sights", "pistol", ms::SightValue(v.Text(key), ""), true);
    } else if (key == "sight_rifle") {
        Put("Sights", "shotgun", ms::SightValue(v.Text(key), ""), true);
        Put("Sights", "assaultRifle", ms::SightValue(v.Text(key), ""), true);
    } else if (key == "sight_sniper") {
        std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
        wchar_t previous[64]{};
        GetPrivateProfileStringW(L"Sights", L"sniperRifle", L"scope 4", previous, 64, path.c_str());
        Put("Sights", "sniperRifle", ms::SightValue(v.Text(key), Narrow(previous)), true);
    } else if (key == "sight_assist") {
        Put("Sights", "assist", onOff("sight_assist"), true);
    } else if (key == "sight_converge") {
        Put("Sights", "converge", onOff("sight_converge"), true);
    }
    // ---- Colour ----
    else if (key == "profile") {
        cv::Profile const p = cv::ParseProfile(v.Text("profile")).value_or(cv::Profile::Auto);
        colourDirty_ = false;
        colour::SelectProfile(p);
    } else if (key == "c_brightness" || key == "c_contrast" || key == "c_saturation" || key == "c_midtones" || key == "c_black") {
        colour_.brightness = ms::ColourFromPercent(v.Number("c_brightness"));
        colour_.contrast = ms::ColourFromPercent(v.Number("c_contrast"));
        colour_.saturation = ms::ColourFromPercent(v.Number("c_saturation"));
        colour_.gamma = v.Number("c_midtones");
        colour_.black = v.Number("c_black");
        colour::SetCustom(colour_, false);  // live; written once the slider rests
        colourDirty_ = true;
        colourChangedMs_ = Now();
    }
    // ---- Halcyon's config ----
    else if (key == "walk") {
        if (v.Text("walk") == "head") {
            c.moveFollowsHead = true;
            HalcyonChanged(false);
        }
    } else if (key == "quality") {
        int const width = ms::QualityWidth(v.Text("quality"));
        if (width > 0) {
            c.sfr2ResX = width;
            c.sfr2ResY = MELEVR::Config::EyeHeightForWidth(width);
            HalcyonChanged(true);  // as Halcyon's slider: saved, and GamerSettings.ini written for the next launch
            int wx = 0, wy = 0;
            MELEVR::Config::ApplyResolutionForCurrentMode(&wx, &wy);
            log::Info("main menu: render resolution {} ({}x{}), GamerSettings.ini {}", v.Text("quality"), c.sfr2ResX, c.sfr2ResY,
                wx > 0 ? std::format("set to {}x{}", wx, wy) : std::string("unchanged (auto-set per mode is off)"));
        }
    } else if (key == "steady_cine") {
        c.cineFpsCap = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "hud_distance") {
        c.hudPanelDistanceM = v.Number(key);
        HalcyonChanged(false);
    } else if (key == "hud_size") {
        c.hudPanelWidthM = 1.3f * v.Number(key) / 100.0f;
        HalcyonChanged(false);
    } else if (key == "reticle") {
        c.handReticle = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "recenter_key") {
        c.recenterKeyEnabled = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "look_speed") {
        c.lookSensitivity = v.Number(key);
        HalcyonChanged(false);
    } else if (key == "look_smooth") {
        c.headLookSmoothing = v.Number(key);
        HalcyonChanged(false);
    } else if (key == "vr_convo") {
        c.cineVrConvo = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "vr_cine") {
        c.cineVrCutscene = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "flat_menus") {
        c.menuFlat = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "flat_galaxy") {
        c.galaxyFlat = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "fp_on") {
        c.fpEnabled = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "fp_hide_head") {
        c.fpHideHead = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "fp_back") {
        c.flatScreenDistance = v.Number(key);
        c.screenDistanceEnabled = true;
        HalcyonChanged(false);
    } else if (key == "fp_right" || key == "fp_up") {
        (key == "fp_right" ? c.cameraOffsetRight : c.cameraOffsetUp) = v.Number(key);
        c.cameraOffsetEnabled = true;
        HalcyonChanged(false);
    } else if (key == "lean") {
        c.leanEnabled = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "lean_gain") {
        c.leanGain = v.Number(key);
        HalcyonChanged(false);
    } else if (key == "flat_size") {
        float const w = v.Number(key);
        c.menuMonoQuadWidth = c.galaxyMonoQuadWidth = c.convoMonoQuadWidth = c.cutsceneMonoQuadWidth = c.movieMonoQuadWidth = w;
        HalcyonChanged(false);
    } else if (key == "fov_fill") {
        c.vrFovFillEnabled = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "vr_mode") {
        std::string const mode = v.Text(key);
        // As Halcyon's mode radio: the modes are exclusive, the old side-by-side stereo is never on, the choice is saved,
        // and the resolution the mode needs is written to GamerSettings.ini for the next launch.
        c.sfr2Enabled = mode == "stereo";
        c.aerEnabled = false;
        c.dibrEnabled = mode == "dibr";
        c.stereoEnabled = false;
        HalcyonChanged(true);
        int wx = 0, wy = 0;
        MELEVR::Config::ApplyResolutionForCurrentMode(&wx, &wy);
        modeNote_ = wx > 0 ? std::format("Resolution set to {} \xC3\x97 {}: restart the game to apply this mode.", wx, wy)
                           : std::string("Restart the game to apply this mode.");
        log::Info("main menu: VR mode {} ({})", mode, modeNote_);
    } else if (key == "life_size") {
        c.sfr2Metric = v.Flag(key);  // life-size stereo (research/30)
        HalcyonChanged(false);
        log::Info("main menu: life-size stereo {}", c.sfr2Metric ? "on" : "off");
    } else if (key == "extra_depth") {
        c.sfr2MetricDepth = v.Number(key);
        HalcyonChanged(false);
    } else if (key == "eye_distance") {
        c.sfr2HalfEyeUU = v.Number(key);
        HalcyonChanged(false);
    } else if (key == "convergence") {
        c.sfr2Convergence = v.Number(key);
        HalcyonChanged(false);
    } else if (key == "swap_eyes") {
        c.sfr2SwapEyes = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "quest_fov") {
        c.questFovMatch = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "dof") {
        c.disableDof = !v.Flag(key);
        MELEVR::D3DCapture::EnsureDepthOfFieldSetting(c.disableDof);  // GamerSettings.ini, for the next launch
        HalcyonChanged(true);
    } else if (key == "mirror") {
        c.mirrorPresentEvery = ms::MirrorPresentEvery(v.Text(key));
        HalcyonChanged(false);
    } else if (key == "panel_width") {
        c.hudPanelWidthM = v.Number(key);
        HalcyonChanged(false);
    } else if (key == "panel_below") {
        c.hudPanelDropM = v.Number(key);
        HalcyonChanged(false);
    } else if (key == "panel_follow") {
        c.hudPanelFollowDeg = v.Number(key);
        HalcyonChanged(false);
    } else if (key == "g_pchud") {
        c.pchudEnabled = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "g_convo") {
        c.convoEnabled = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "g_designer") {
        c.designerUiEnabled = v.Flag(key);
        HalcyonChanged(false);
    } else {
        log::Warn("main menu: no setting behind '{}'", key);
    }
}

void Settings::Command(std::string const& name, std::string const& argument, menu::Values& v) {
    log::Info("main menu: {}{}{}", name, argument.empty() ? "" : " ", argument);
    if (name == "resume") {
        after_ = After::Close;
    } else if (name == "calibrate") {
        after_ = After::Calibrate;
    } else if (name == "recenter") {
        MELEVR::Menu::RequestRecenter();
    } else if (name == "align") {
        after_ = After::Align;
    } else if (name == "measure") {
        measure_ = Measure::WaitRelease;  // the click itself may be a trigger pull
        measureSinceMs_ = Now();
        measureMsg_ = "Hold both arms straight out to the sides (a T) and pull either trigger.";
    } else if (name == "place_hud") {
        MELEVR::MevrBridge::RequestHudPanelReplace();
        profMsg_.clear();
    } else if (name == "classic") {
        after_ = After::Classic;
    } else if (name == "prof_load" || name == "prof_save") {
        int const slot = std::atoi(argument.c_str());
        if (slot >= 0 && slot < MELEVR::Config::ProfileCount()) {
            if (name == "prof_load") {
                Flush(true);
                MELEVR::Config::LoadProfile(slot);  // Halcyon's own: applies the slot and remembers it as active
                halcyonDirty_ = false;
                noSave_ = false;
                profMsg_ = std::format("Loaded {}.", MELEVR::Config::ProfileName(slot));
            } else {
                MELEVR::Config::SaveProfile(slot);
                halcyonDirty_ = false;
                profMsg_ = std::format("Saved into {}.", MELEVR::Config::ProfileName(slot));
            }
            log::Info("main menu: {}", profMsg_);
        }
    } else if (name == "snapshot") {
        after_ = After::Snapshot;
    } else if (name == "record") {
        testloop::RequestRecording(testloop::GetStatus().recording ? 0 : 1);
    } else if (name == "perf") {
        after_ = After::Perf;
    } else if (name == "reset") {
        if (resetArmedMs_ == 0) {
            resetArmedMs_ = Now();
            diagMsg_ = "Press Confirm to reset the renderer settings for this session.";
        } else {
            // Halcyon's defaults, live, but what our deploy and the player chose stays: the menu key, no head aim, the
            // render resolution, the first-run notice. Not saved over the profile (Profiles > Save keeps it).
            MELEVR::Config::VrConfig const before = MELEVR::Config::Get();
            MELEVR::Config::ResetDefaults();
            MELEVR::Config::VrConfig& c = MELEVR::Config::Get();
            c.menuKey = before.menuKey;
            c.combatHeadAim = before.combatHeadAim;
            c.makoHeadAim = before.makoHeadAim;
            c.sfr2ResX = before.sfr2ResX;
            c.sfr2ResY = before.sfr2ResY;
            c.expectedResX = before.expectedResX;
            c.expectedResY = before.expectedResY;
            c.firstRunDone = before.firstRunDone;
            c.hdrPaperWhiteNits = before.hdrPaperWhiteNits;
            c.fpEnabled = before.fpEnabled;
            resetArmedMs_ = 0;
            noSave_ = true;
            halcyonDirty_ = false;
            diagMsg_ = "Reset for this session. Not saved: Profiles > Save keeps it; otherwise your profile comes back next launch.";
            log::Info("main menu: renderer settings reset to Halcyon's defaults for this session (menu key, head aim, resolution kept)");
        }
    }
    (void)v;
}

void Settings::Tick(vr::controllers::State const& live, bool headValid, XrVector3f const& head) {
    std::uint64_t const now = Now();
    Flush(false);
    if (now >= nextMenuReadMs_) {
        ReadMenuSection();
    }
    if (resetArmedMs_ != 0 && now - resetArmedMs_ > kResetArmMs) {
        resetArmedMs_ = 0;
        diagMsg_.clear();
    }
    if (measure_ != Measure::Idle) {
        bool const anyTrigger = live.left.trigger > 0.6f || live.right.trigger > 0.6f;
        bool const noTrigger = live.left.trigger < 0.4f && live.right.trigger < 0.4f;
        if (now - measureSinceMs_ > kMeasureTimeoutMs) {
            measure_ = Measure::Idle;
            measureMsg_ = "Timed out: no trigger pull within 30 seconds.";
        } else if (measure_ == Measure::WaitRelease && noTrigger) {
            measure_ = Measure::WaitPull;
        } else if (measure_ == Measure::WaitPull && anyTrigger && live.active && live.left.gripValid && live.right.gripValid) {
            MELEVR::MevrBridge::TakeArmSample(live.left.grip.position, live.right.grip.position, headValid ? &head : nullptr,
                "the main menu");
            XrVector3f const d{live.right.grip.position.x - live.left.grip.position.x, live.right.grip.position.y - live.left.grip.position.y,
                live.right.grip.position.z - live.left.grip.position.z};
            measureMsg_ = std::format("Measured: {:.2f} m between the controllers. The arm length updates in a few seconds.",
                std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z));
            measure_ = Measure::Idle;
        }
    }
    perf::Status const p = perf::GetStatus();
    if (!p.pending && !p.lastCapture.empty()) {
        perfMsg_ = "Last measurement saved (perf folder).";
    }
}

bool Settings::CancelPending() {
    bool cancelled = false;
    if (resetArmedMs_ != 0) {
        resetArmedMs_ = 0;
        diagMsg_.clear();
        cancelled = true;
    }
    if (measure_ != Measure::Idle) {
        measure_ = Measure::Idle;
        measureMsg_ = "Measurement cancelled.";
        cancelled = true;
    }
    return cancelled;
}

void Settings::Closed() {
    measure_ = Measure::Idle;
    resetArmedMs_ = 0;
    diagMsg_.clear();
    Flush(true);
}

After Settings::TakeAfter() {
    After const a = after_;
    after_ = After::Nothing;
    return a;
}

} // namespace mevr::host::main_menu
