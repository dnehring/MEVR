// SPDX-License-Identifier: MIT
// What each row of our main menu drives (plugin/MainMenuSettings.hpp, research/28 section 4).

#include "plugin/MainMenuSettings.hpp"

#include "halcyon/le1/d3d_capture.h"
#include "halcyon/le1/mevr_bridge.h"
#include "halcyon/le1/vr_config.h"
#include "halcyon/le1/vr_menu.h"
#include "halcyon/le1/xr_session.h"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/CineScreen.hpp"
#include "core/vr/PerformanceModes.hpp"
#include "core/vr/WorldUi.hpp"
#include "plugin/Calibration.hpp"
#include "plugin/Colour.hpp"
#include "plugin/MevrMenu.hpp"
#include "plugin/PerfProbe.hpp"
#include "plugin/ReportRequest.hpp"
#include "plugin/SetupRequest.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/Voice.hpp"

#include "mevr/BuildInfo.hpp"  // MEVR_GIT_COMMIT, About (generated: tools/cmake/BuildInfo.cmake)
#include "mevr/Version.hpp"    // MEVR_VERSION, About (generated from CMake's project(VERSION))

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
constexpr float kCineDepthFull = 0.010f;          // [Cinematics] depth at the menu's 100% (core/vr/CineScreen's default; 150% is
                                                   // the first default, 1.5%, which the owner found cross-eyed)
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
    if (!text) {
        return std::nullopt;
    }
    std::optional<double> const v = ini::Number(*text);  // locale-independent
    return v ? std::optional<float>(static_cast<float>(*v)) : std::nullopt;
}

// A key's name on this keyboard layout (GetKeyNameText): the menu key is \ in our deploy, which is ^ on a German one.
std::string KeyName(int vk) {
    if (vk <= 0) {
        return "(none)";
    }
    UINT const scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    LONG param = static_cast<LONG>(scan << 16);
    switch (vk) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_NUMLOCK:
        param |= 1 << 24;  // extended keys
        break;
    default: break;
    }
    wchar_t name[64]{};
    int const n = scan != 0 ? GetKeyNameTextW(param, name, 64) : 0;
    if (n <= 0) {
        return std::format("key {}", vk);
    }
    char utf8[192]{};
    int const bytes = WideCharToMultiByte(CP_UTF8, 0, name, n, utf8, static_cast<int>(sizeof utf8) - 1, nullptr, nullptr);
    return bytes > 0 ? std::string(utf8, static_cast<std::size_t>(bytes)) : std::format("key {}", vk);
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

// A game UI movie's size and place in Halcyon's config (its HUD tab's transform; Advanced > Game UI), the set the renderer
// uses now (xr_session's useDibrUi / useStereoUi choice: DIBR's own, the old side-by-side stereo's own, else the normal one),
// with Halcyon's tuned values for it.
struct UiMovie {
    float* scaleX;
    float* scaleY;
    float* offsetX;
    float* offsetY;
    float defScaleX, defScaleY, defOffsetX, defOffsetY;
};

UiMovie Movie(MELEVR::Config::VrConfig& c, bool designer) {
    static MELEVR::Config::VrConfig const d{};
    if (designer) {  // one set for every mode
        return {&c.designerUiScaleX, &c.designerUiScaleY, &c.designerUiOffsetX, &c.designerUiOffsetY, d.designerUiScaleX,
            d.designerUiScaleY, d.designerUiOffsetX, d.designerUiOffsetY};
    }
    if (c.dibrEnabled) {
        return {&c.convoScaleXDibr, &c.convoScaleYDibr, &c.convoOffsetXDibr, &c.convoOffsetYDibr, d.convoScaleXDibr, d.convoScaleYDibr,
            d.convoOffsetXDibr, d.convoOffsetYDibr};
    }
    if (c.stereoEnabled) {
        return {&c.convoScaleXStereo, &c.convoScaleYStereo, &c.convoOffsetXStereo, &c.convoOffsetYStereo, d.convoScaleXStereo,
            d.convoScaleYStereo, d.convoOffsetXStereo, d.convoOffsetYStereo};
    }
    return {&c.convoScaleX, &c.convoScaleY, &c.convoOffsetX, &c.convoOffsetY, d.convoScaleX, d.convoScaleY, d.convoOffsetX, d.convoOffsetY};
}

// The rows: size as a percentage of the tuned size, a nudge from the tuned place in the movie's pixels (up is +: Flash's y
// grows downward).
void ReadMovie(menu::Values& v, MELEVR::Config::VrConfig& c, bool designer, char const* prefix) {
    UiMovie const m = Movie(c, designer);
    v.numbers[std::string(prefix) + "_size"] = m.defScaleX > 0.0f ? std::round(*m.scaleX / m.defScaleX * 100.0f) : 100.0f;
    v.numbers[std::string(prefix) + "_x"] = std::round(*m.offsetX - m.defOffsetX);
    v.numbers[std::string(prefix) + "_y"] = std::round(m.defOffsetY - *m.offsetY);
}

void WriteSize(UiMovie const& m, float percent) {
    float const scaleX = m.defScaleX * percent / 100.0f;
    // Both axes together, keeping the shape the movie has (a different height set in the classic menu stays in proportion).
    float const ratio = *m.scaleX > 1e-4f ? *m.scaleY / *m.scaleX : (m.defScaleX > 1e-4f ? m.defScaleY / m.defScaleX : 1.0f);
    *m.scaleX = scaleX;
    *m.scaleY = scaleX * ratio;
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
        bool ok = WritePrivateProfileStringW(L"Menu", L"pause", L"1", path.c_str()) != 0;
        ok = WritePrivateProfileStringW(L"Menu", L"distanceM", L"1.30", path.c_str()) != 0 && ok;
        ok = WritePrivateProfileStringW(L"Menu", L"widthM", L"1.45", path.c_str()) != 0 && ok;
        ok = WritePrivateProfileStringW(L"Menu", L"assets", L"", path.c_str()) != 0 && ok;
        Wrote(ok, "[Menu]'s defaults");
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
        // Every section a row below reads (WorldUi was missing: the Hacking screen row fell back to its default after a write).
        // A section missing here makes its rows snap back to their defaults after a write (the Hacking row's bug, f1f8d8b).
        for (wchar_t const* s : {L"Hands", L"Holsters", L"Controls", L"Throwables", L"Sights", L"Camera", L"Arms", L"Menu", L"Comfort",
                                 L"Upscale", L"Cinematics", L"WorldUi", L"Galaxy", L"Hud", L"Interact", L"Vent", L"Mako", L"Cockpit",
                                 L"Squad", L"Performance", L"SystemSettings", L"Voice"}) {
            ini.sections[Narrow(s)] = ReadSection(path, s);
        }
    }
    auto flag = [&](char const* key, bool value) { v.numbers[key] = value ? 1.0f : 0.0f; };

    // Comfort.
    MELEVR::Config::VrConfig const& c = MELEVR::Config::Get();
    // [Comfort] (research/31; the mod reads it: game/comfort). Smooth is the game's own turn ("turn = game"); the
    // file-only "turn = smooth" (the mod's steady turn) shows as Smooth too.
    v.texts["turn"] = ms::TurnChoice(ini.Text("Comfort", "turn", "game"));
    v.texts["snap"] = ms::SnapChoice(ini.Num("Comfort", "snapDeg", 45.0f));
    flag("vignette", ini.Num("Comfort", "vignette", 0.0f) != 0.0f);
    v.numbers["vignette_strength"] = std::clamp(std::round(ini.Num("Comfort", "vignetteStrength", 0.6f) * 100.0f), 10.0f, 100.0f);
    v.texts["vignette_when"] = Lower(ini.Text("Comfort", "vignetteWhen", "turn")) == "move" ? "move" : "turn";
    // "body": Halcyon's moveFollowsHead off (the stick walks where Shepard faces). Not offered since 2026-11-04 (the owner: it
    // brings back Halcyon's "look left, move right" bug), so a file with it off shows no chip until Where I look is picked.
    v.texts["walk"] = Lower(ini.Text("Comfort", "walk", "head")) == "hand" ? "hand" : c.moveFollowsHead ? "head" : "body";
    flag("crouch", ini.Num("Hands", "physicalCrouch", 1.0f) != 0.0f);
    // Movement is always life-size (the owner, 2026-11-06: Halcyon's lean gain is 2 whatever a profile says, vr_config's
    // [MEVR] seam); no row for it.
    flag("pause", ini.Num("Menu", "pause", 1.0f) != 0.0f);
    // Conversations and cutscenes (research/52), each on its own row: flat with Halcyon's VR conversations (cutscenes) off, on the
    // 3D cinema screen with [Cinematics] conversations (cutscenes) on, else in VR.
    mevr::vr::cine_screen::Settings const cineDefaults;
    auto const cineChoice = [&](bool inVr, char const* key, bool fallback) {
        return !inVr ? std::string("flat") : ini.Num("Cinematics", key, fallback ? 1.0f : 0.0f) != 0.0f ? std::string("screen")
                                                                                                       : std::string("vr");
    };
    v.texts["cine_convo"] = cineChoice(c.cineVrConvo, "conversations", cineDefaults.conversations);
    v.texts["cine_cut"] = cineChoice(c.cineVrCutscene, "cutscenes", cineDefaults.cutscenes);
    v.numbers["cine_width"] = std::clamp(ini.Num("Cinematics", "widthM", 2.9f), 1.5f, 8.0f);
    v.numbers["cine_distance"] = std::clamp(ini.Num("Cinematics", "distanceM", 3.5f), 1.0f, 6.0f);
    v.numbers["cine_depth"] = std::clamp(std::round(ini.Num("Cinematics", "depth", kCineDepthFull) / kCineDepthFull * 100.0f), 0.0f, 150.0f);
    {
        // The omni-tool's screens (research/53 section 6, research/57): projected from the wrist unless [WorldUi] wrist is none.
        std::string const wrist = Lower(ini.Text("WorldUi", "wrist", "all"));
        v.texts["wrist_screen"] = wrist.empty() || wrist == "none" ? "room" : "wrist";
        v.numbers["wrist_width"] =
            std::clamp(ini.Num("WorldUi", "wristMenuWidthM", vr::world_ui::Settings{}.wristMenuWidthM), 0.4f, 1.0f);
    }
    // The game's screens in the room (research/53) and the galaxy map's hologram (research/51): the mod's defaults are on.
    flag("world_ui", ini.Num("WorldUi", "on", 1.0f) != 0.0f);
    flag("galaxy_holo", ini.Num("Galaxy", "hologram", 1.0f) != 0.0f);
    // Controls.
    flag("grip_holds", ini.Num("Holsters", "gripHolds", 1.0f) != 0.0f);
    flag("holsters", ini.Num("Holsters", "enabled", 1.0f) != 0.0f);
    v.numbers["holster_buzz"] = std::round(std::clamp(ini.Num("Holsters", "hapticAmplitude", 0.5f), 0.0f, 1.0f) * 100.0f);
    {
        std::string const lt = Lower(ini.Text("Controls", "leftTrigger", "lb"));
        v.texts["left_trigger"] = lt == "lt" ? "lt" : lt == "lb" ? "lb" : "none";
    }
    {
        // One row for both: on when both are; set apart in the file, the row says so.
        bool const grenades = ini.Num("Throwables", "enabled", 1.0f) != 0.0f;
        bool const melee = ini.Num("Throwables", "melee", 1.0f) != 0.0f;
        flag("throwables", grenades && melee);
        v.texts["throwables_note"] = grenades == melee ? std::string()
                                   : grenades           ? std::string("Grenades on, melee off (set in the file): switching sets both")
                                                        : std::string("Melee on, grenades off (set in the file): switching sets both");
    }
    v.numbers["recoil_buzz"] = std::round(std::clamp(ini.Num("Hands", "recoilHaptic", 0.5f), 0.0f, 1.0f) * 100.0f);
    flag("aim_assist", ini.Num("Hands", "aimAssist", 0.0f) != 0.0f);
    // The weapon drawn, like exploring (research/38): the mod reads exploreStance and drawRate, the host constantEye.
    flag("explore_stance", ini.Num("Hands", "exploreStance", 0.0f) != 0.0f);
    flag("constant_eye", ini.Num("Hands", "constantEye", 1.0f) != 0.0f);
    v.numbers["draw_rate"] = std::clamp(ini.Num("Hands", "drawRate", 6.0f), 1.0f, 10.0f);
    flag("gun_kick", ini.Num("Hands", "gunKick", 1.0f) != 0.0f);  // the mod reads it (game/gun_kick, research/24 section 8)
    // Features on by default with no row until 2026-11-04 (the release checklist's file-only list): the mod's defaults are on.
    flag("fingers", ini.Num("Hands", "fingers", 1.0f) != 0.0f);
    flag("vent", ini.Num("Vent", "enabled", 1.0f) != 0.0f);
    flag("mako_controls", ini.Num("Mako", "controls", 1.0f) != 0.0f);
    flag("cockpit", ini.Num("Cockpit", "enabled", 1.0f) != 0.0f);
    {
        // The controller guide: what each button does with these settings (core/vr/MenuSettings GuideTexts).
        ms::GuideInput g;
        g.leftTrigger = v.Text("left_trigger");
        g.squad = ini.Num("Squad", "enabled", 1.0f) != 0.0f;
        g.squadStyle = Lower(ini.Text("Squad", "style", "native"));
        g.holsters = v.Flag("holsters");
        g.gripHolds = v.Flag("grip_holds");
        g.powerWheel = Lower(ini.Text("Holsters", "powerWheel", "r3"));
        g.grenades = ini.Num("Throwables", "enabled", 1.0f) != 0.0f;
        g.melee = ini.Num("Throwables", "melee", 1.0f) != 0.0f;
        g.grenadeHands = Lower(ini.Text("Throwables", "hands", "right"));
        g.grenadeButton = ini.Num("Throwables", "grenadeButton", 1.0f) != 0.0f;
        g.meleeButton = ini.Num("Throwables", "meleeButton", 0.0f) != 0.0f;
        g.meleeFist = ini.Num("Throwables", "meleeFist", 1.0f) != 0.0f;
        g.vent = v.Flag("vent");
        g.turn = v.Text("turn");
        g.walk = v.Text("walk");
        g.physicalCrouch = v.Flag("crouch");
        for (auto const& [key, text] : ms::GuideTexts(g)) {
            v.texts[key] = text;
        }
    }
    // Body.
    // The row is the trim around the hand model's 90 degrees (ms::kHandPitchDefault), the file the whole angle.
    v.numbers["trim_pitch"] = ini.Num("Hands", "handPitch", ms::kHandPitchDefault) - ms::kHandPitchDefault;
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
    // The render target: the headset's own eye size (and 0.85 / 0.75 of it) once VR has started, or Halcyon's ladder.
    MELEVR::XrSession::ResolutionStats resolution{};
    MELEVR::XrSession::GetResolutionStats(&resolution);
    int const recommendedW = static_cast<int>(resolution.recommendedEyeWidth);
    int const recommendedH = static_cast<int>(resolution.recommendedEyeHeight);
    v.texts["quality"] = ms::ResolutionChoice(c.sfr2ResX, c.sfr2ResY, recommendedW, recommendedH);
    v.texts["quality_text"] = ms::SizeText(c.sfr2ResX, c.sfr2ResY);
    v.texts["quality_note"] = noSave_ ? "not saved: the VR settings were reset this session (Advanced > Profiles > Save keeps it)"
                                      : "applies after a restart";
    flag("headset_known", recommendedW > 0 && recommendedH > 0);
    v.texts["headset_text"] = ms::HeadsetSizesText(recommendedW, recommendedH);
    v.texts["upscale"] = ms::UpscaleChoice(ini.Text("Upscale", "method", "fsr"));
    flag("steady_cine", c.cineFpsCap);
    // The Performance row (research/60): which mode the files are now, what it trades, what waits for a restart; and the
    // bundled levers one by one (Advanced > Performance).
    {
        namespace pm = vr::perf_modes;
        pm::Levers const levers = pm::Current(
            [&](std::string_view section, std::string_view key) { return ini.Get(std::string(section).c_str(), std::string(key).c_str()); },
            v.texts["quality"]);
        v.texts["perf_mode"] = pm::ModeFor(levers);
        v.texts["perf_desc"] = pm::Description(levers);
        bool const live = resolution.renderWidth > 0 && resolution.renderHeight > 0;
        bool const pendingSize = live && (static_cast<int>(resolution.renderWidth) != c.sfr2ResX ||
                                             static_cast<int>(resolution.renderHeight) != c.sfr2ResY);
        v.texts["perf_note"] = pm::RestartNote(pendingSize);
        v.texts["cull_px"] = pm::CullChoice(levers.minScreenRadiusPx);
        v.numbers["grass"] = std::clamp(pm::FoliagePercent(levers.foliage), 25.0f, 100.0f);
        flag("dyn_shadows", levers.dynamicShadows);
        flag("ambient_occl", levers.ambientOcclusion);
        v.numbers["fsr_sharpness"] = std::clamp(std::round(ini.Num("Upscale", "sharpness", 0.9f) * 100.0f), 0.0f, 100.0f);
    }
    // HUD and sights.
    v.numbers["hud_distance"] = c.hudPanelDistanceM;
    v.numbers["hud_size"] = std::round(c.hudPanelWidthM / 1.3f * 100.0f);
    v.numbers["panel_below"] = c.hudPanelDropM;
    v.numbers["panel_follow"] = c.hudPanelFollowDeg;
    flag("reticle", c.handReticle);
    flag("wrist_status", ini.Num("Hud", "wristStatus", 1.0f) != 0.0f);
    flag("gun_status", ini.Num("Hud", "gunStatus", 1.0f) != 0.0f);
    flag("interact_label", ini.Num("Interact", "label", 1.0f) != 0.0f);
    v.texts["sight_pistol"] = ms::SightKind(ini.Text("Sights", "pistol", "dot"), "dot");
    v.texts["sight_shotgun"] = ms::SightKind(ini.Text("Sights", "shotgun", "dot"), "dot");
    v.texts["sight_rifle"] = ms::SightKind(ini.Text("Sights", "assaultRifle", "dot"), "dot");
    v.texts["sight_sniper"] = ms::SightKind(ini.Text("Sights", "sniperRifle", "scope auto"), "scope");
    flag("sight_assist", ini.Num("Sights", "assist", 1.0f) != 0.0f);
    flag("sight_converge", ini.Num("Sights", "converge", 1.0f) != 0.0f);
    flag("scope_grade", ini.Num("Sights", "scopeCapturePost", 1.0f) != 0.0f);
    // Advanced: Halcyon's.
    flag("recenter_key", c.recenterKeyEnabled);
    v.texts["recenter_key_desc"] = std::format("{} on the keyboard recenters, with the game window in front (the right stick's "
                                               "double-click works either way)", KeyName(c.recenterKey));
    v.texts["classic_desc"] = std::format("The original MELE-VR settings menu, with expert settings these pages don't have (also: "
                                          "Ctrl + {})", KeyName(c.menuKey));
    v.numbers["look_speed"] = c.lookSensitivity;
    v.numbers["look_smooth"] = c.headLookSmoothing;
    flag("flat_menus", c.menuFlat);
    flag("flat_galaxy", c.galaxyFlat);
    flag("fp_on", c.fpEnabled);
    flag("fp_hide_head", c.fpHideHead);
    // What's in effect: Halcyon applies the distance and the offset only while their switches are on.
    v.numbers["fp_back"] = c.screenDistanceEnabled ? c.flatScreenDistance : 0.0f;
    v.numbers["fp_right"] = c.cameraOffsetEnabled ? c.cameraOffsetRight : 0.0f;
    v.numbers["fp_up"] = c.cameraOffsetEnabled ? c.cameraOffsetUp : 0.0f;
    flag("lean", c.leanEnabled);
    v.numbers["flat_size"] = c.menuMonoQuadWidth;
    flag("fov_fill", c.vrFovFillEnabled);
    v.texts["vr_mode"] = ModeChoice(c);
    v.texts["mode_note"] = modeNote_;
    flag("life_size", c.sfr2Metric);
    v.numbers["extra_depth"] = c.sfr2MetricDepth;
    flag("eye_frustum", c.sfr2MetricEyeFrustum);
    v.numbers["eye_distance"] = 2.0f * c.sfr2HalfEyeUU;  // both eyes apart, in game centimetres at life size (100 units/m)
    v.numbers["convergence"] = c.sfr2Convergence;
    flag("swap_eyes", c.sfr2SwapEyes);
    flag("quest_fov", c.questFovMatch);
    flag("dof", !c.disableDof);
    v.texts["mirror"] = ms::MirrorChoice(c.mirrorPresentEvery);
    // Advanced > Game UI (Halcyon's HUD tab's movie transforms; the combat HUD's isn't there: the HUD panel shows that movie
    // at its authored layout, research/15).
    flag("g_convo", c.convoEnabled);
    ReadMovie(v, MELEVR::Config::Get(), false, "convo");  // read only
    flag("g_designer", c.designerUiEnabled);
    ReadMovie(v, MELEVR::Config::Get(), true, "designer");
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
    v.texts["content_installed"] = setup_request::InstalledPacksText();
    v.texts["content_msg"] = contentMsg_;
    // Report a problem (research/33 section 8): the last request's outcome, and the offer after a crash.
    std::string const reportStatus = report_request::StatusText();
    v.texts["report_msg"] = reportStatus.empty() ? reportMsg_ : reportStatus;
    flag("crash_offer", report_request::CrashOffer());
    // The top bar.
    v.texts["chip_headset"] = headset.empty() ? std::string("Headset not started") : headset;
    v.texts["chip_controllers"] = controllers.empty() ? std::string("No controllers yet") : controllers;
    v.texts["chip_calibrated"] = cal.calibrated ? "Calibrated" : "Not calibrated";
    v.texts["save_msg"] = saveFailed_ ? "Can't save settings: the game's ASI folder is read-only" : "";
    // Diagnostics > About: what a report needs (the log's attach line says the same).
#if defined(MEVR_MONOLITHIC)
    v.texts["about_build"] = ms::AboutBuild(MEVR_VERSION, MEVR_GIT_COMMIT, false);
#else
    v.texts["about_build"] = ms::AboutBuild(MEVR_VERSION, MEVR_GIT_COMMIT, true);
#endif
    ReadVoice(v, ini.Num("Voice", "enabled", 1.0f) != 0.0f, ini.Text("Voice", "model", "tiny"), ini.Num("Voice", "earCueStrength", 0.4f));
}

// The Voice page (research/54 "The menu's Voice page"): the recognizer's state from the host (plugin/Voice), the microphones it
// lists, and [Voice]'s keys. The level bar is MainMenu's (every frame while the page is open).
void Settings::ReadVoice(menu::Values& v, bool enabled, std::string const& model, float earCue) {
    std::uint64_t const now = Now();
    bool const onPage = v.Text("section") == "voice";
    if (onPage && now >= nextDeviceListMs_) {
        nextDeviceListMs_ = now + 3000;  // a headset plugged in while the page is open shows up
        voice::ListDevices();
    }
    voice::MenuView const view = voice::Menu();
    v.numbers["voice_on"] = enabled ? 1.0f : 0.0f;
    v.texts["voice_model"] = ms::VoiceModelChoice(model);
    v.numbers["ear_cue"] = std::clamp(std::round(earCue * 100.0f), 0.0f, 100.0f);
    ms::VoiceStatusInput status;
    status.enabled = enabled;
    status.restarting = view.restarting;
    status.status = view.state.status;
    status.device = view.state.device;
    status.model = view.state.model;
    status.error = view.state.error;
    status.listening = view.state.listening != 0;
    v.texts["voice_status"] = ms::VoiceStatusText(status);
    v.numbers["voice_ready"] = !view.restarting && enabled && view.state.status == 2 ? 1.0f : 0.0f;
    // The device: the one just picked until the restart has read it, else what the recognizer runs with.
    std::string const device = now - micPickedMs_ < 3000 ? micPicked_ : view.device;
    int const count = std::min(static_cast<int>(view.devices.size()), ms::kMicSlots);
    v.numbers["mic_count"] = static_cast<float>(count);
    for (int i = 0; i < ms::kMicSlots; ++i) {
        v.texts[std::format("mic{}", i)] = i < count ? view.devices[static_cast<std::size_t>(i)] : std::string();
    }
    int const index = ms::MicIndex(view.devices, device);
    v.texts["mic"] = index == -1 ? "default" : index >= 0 && index < count ? std::to_string(index) : "missing";
    v.texts["mic_missing"] = index == -2 && view.listed ? device : std::string();
    v.texts["mic_default"] = view.defaultDevice >= 0 && view.defaultDevice < static_cast<int>(view.devices.size())
                                 ? "Windows' default communications microphone: " + view.devices[static_cast<std::size_t>(view.defaultDevice)]
                                 : std::string("Windows' default communications microphone");
    v.texts["mic_note"] = view.listed ? (view.devices.empty() ? std::string("No microphone found: is the headset connected?") : std::string())
                                      : view.listNote.empty() ? std::string("Listing the microphones\xE2\x80\xA6") : view.listNote;
    v.numbers["voice_key_wait"] = keyWaitMs_ != 0 ? 1.0f : 0.0f;
    v.texts["voice_key"] = keyWaitMs_ != 0 ? std::string("Press a key\xE2\x80\xA6") : view.key != 0 ? KeyName(view.key) : std::string("None");
}

void Settings::WriteVoiceDevice(std::string const& name) {
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    if (path.empty()) {
        return;
    }
    // The name as UTF-16 (Put's narrow path would turn a device's "ö" into '?'): plugin/Voice reads it back the same way.
    std::wstring wide;
    if (!name.empty()) {
        int const n = MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
        wide.resize(static_cast<std::size_t>(std::max(n, 0)));
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), wide.data(), n);
    }
    bool const ok = WritePrivateProfileStringW(L"Voice", L"device", wide.c_str(), path.c_str()) != 0;
    Wrote(ok, "[Voice] device");
    log::Info("main menu: [Voice] device = {}{}", name.empty() ? "(the default communications device)" : name, ok ? "" : " (NOT saved)");
    micPicked_ = name;
    micPickedMs_ = Now();
}

void Settings::Wrote(bool ok, std::string_view what) {
    DWORD const error = ok ? 0 : GetLastError();
    if (ok == !saveFailed_) {
        return;
    }
    saveFailed_ = !ok;
    if (ok) {
        log::Info("main menu: the settings file is writable again ({})", what);
    } else {
        log::Warn("main menu: can't write {} to {} (error {}): the game's ASI folder or the file is read-only; the menu's "
                  "changes apply for this session only", what, ini::Utf8(File()), error);
    }
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
        bool const ok =
            WritePrivateProfileStringW(Wide(edit.section).c_str(), Wide(edit.key).c_str(), Wide(edit.value).c_str(), path.c_str()) != 0;
        Wrote(ok, std::format("[{}] {}", edit.section, edit.key));
        log::Info("main menu: [{}] {} = {}{}", edit.section, edit.key, edit.value, ok ? "" : " (NOT saved)");
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

bool Settings::SetResolution(std::string const& choice) {
    // A headset chip (the runtime's recommended eye size, or 0.85 / 0.75 of it) or a ladder tier (Halcyon's near-square
    // height). Either way written as tools/vr-game-settings.ps1 writes a target: sfr2ResX/Y and the watchdog's
    // expectedResX/Y in MELEVR.ini (the active profile), GamerSettings.ini's ResX/ResY for the next launch.
    MELEVR::Config::VrConfig& c = MELEVR::Config::Get();
    MELEVR::XrSession::ResolutionStats resolution{};
    MELEVR::XrSession::GetResolutionStats(&resolution);
    ms::Size size = ms::HeadsetSize(choice, static_cast<int>(resolution.recommendedEyeWidth),
        static_cast<int>(resolution.recommendedEyeHeight));
    if (size.width == 0) {
        int const width = ms::QualityWidth(choice);
        if (width > 0) {
            size = {width, MELEVR::Config::EyeHeightForWidth(width)};
        }
    }
    if (size.width <= 0) {
        log::Warn("main menu: render resolution {}: no size for it (the headset's isn't known before VR starts)", choice);
        return false;
    }
    if (c.sfr2ResX == size.width && c.sfr2ResY == size.height && c.expectedResX == size.width && c.expectedResY == size.height) {
        return true;  // the target already (a mode picked with the size it has): no profile save, no GamerSettings.ini write
    }
    c.sfr2ResX = size.width;
    c.sfr2ResY = size.height;
    c.expectedResX = size.width;
    c.expectedResY = size.height;
    HalcyonChanged(true);  // as Halcyon's slider: saved, and GamerSettings.ini written for the next launch
    int wx = 0, wy = 0;
    if (!noSave_) {  // after a Reset the profile isn't saved: GamerSettings.ini stays with it (Profiles > Save writes both)
        MELEVR::Config::ApplyResolutionForCurrentMode(&wx, &wy);
    }
    log::Info("main menu: render resolution {} ({}x{}), GamerSettings.ini {}", choice, c.sfr2ResX, c.sfr2ResY,
        wx > 0 ? std::format("set to {}x{}", wx, wy) : std::string("unchanged (auto-set per mode is off)"));
    return true;
}

void Settings::ClearCullTenths() {
    // plugin/RenderLevers reads minScreenRadiusTenths before minScreenRadiusPx: a file that has it would keep its cull whatever
    // the row or a mode writes. Emptied (an empty value is the default, so minScreenRadiusPx counts), never added.
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    if (path.empty()) {
        return;
    }
    wchar_t buffer[32]{};
    if (GetPrivateProfileStringW(L"Performance", L"minScreenRadiusTenths", L"", buffer, 32, path.c_str()) > 0) {
        Put("Performance", "minScreenRadiusTenths", "", true);
    }
}

void Settings::Write(std::string const& key, menu::Values const& v, bool final) {
    auto onOff = [&](char const* k) { return std::string(v.Flag(k) ? "1" : "0"); };
    auto fixed = [&](float value, int decimals) { return std::format("{:.{}f}", value, decimals); };
    MELEVR::Config::VrConfig& c = MELEVR::Config::Get();
    // ---- Our settings file ----
    if (key == "cine_convo" || key == "cine_cut") {
        // One context each: Halcyon's VR conversations (cutscenes) off for the flat screen, else [Cinematics] picks the 3D
        // screen or the director's camera in VR.
        bool const convo = key == "cine_convo";
        std::string const view = v.Text(key);
        (convo ? c.cineVrConvo : c.cineVrCutscene) = view != "flat";
        HalcyonChanged(false);
        if (view != "flat") {
            Put("Cinematics", convo ? "conversations" : "cutscenes", view == "screen" ? "1" : "0", true);
        }
    } else if (key == "cine_width") {
        Put("Cinematics", "widthM", fixed(v.Number("cine_width"), 2), final);
    } else if (key == "cine_distance") {
        Put("Cinematics", "distanceM", fixed(v.Number("cine_distance"), 2), final);
    } else if (key == "cine_depth") {
        Put("Cinematics", "depth", fixed(v.Number("cine_depth") / 100.0f * kCineDepthFull, 4), final);
    } else if (key == "wrist_screen") {
        Put("WorldUi", "wrist", v.Text("wrist_screen") == "room" ? std::string("none")
            : vr::world_ui::ScreenList(vr::world_ui::Settings{}.wristScreens), true);  // the omni-tool's screens (research/57)
    } else if (key == "wrist_width") {
        Put("WorldUi", "wristMenuWidthM", fixed(v.Number("wrist_width"), 2), final);  // their size on the wrist (research/57)
    } else if (key == "world_ui") {
        Put("WorldUi", "on", onOff("world_ui"), true);
    } else if (key == "galaxy_holo") {
        Put("Galaxy", "hologram", onOff("galaxy_holo"), true);
    } else if (key == "voice_on") {
        // Voice (research/54 "The menu's Voice page"): the recognizer restarts with the file's [Voice] (plugin/Voice).
        Put("Voice", "enabled", onOff("voice_on"), true);
        voice::Restart(v.Flag("voice_on") ? "the menu: voice orders on" : "the menu: voice orders off");
    } else if (key == "voice_model") {
        std::string const model = ms::VoiceModelChoice(v.Text("voice_model"));
        Put("Voice", "model", model, true);
        voice::Restart("the menu: the " + model + " model");
    } else if (key == "mic") {
        std::string const pick = v.Text("mic");
        voice::MenuView const view = voice::Menu();
        if (pick == "default") {
            WriteVoiceDevice({});
        } else if (pick != "missing") {
            int const i = std::atoi(pick.c_str());
            if (i < 0 || i >= static_cast<int>(view.devices.size())) {
                return;
            }
            WriteVoiceDevice(view.devices[static_cast<std::size_t>(i)]);  // the whole name: a part of it that is unique
        } else {
            return;
        }
        voice::Restart(pick == "default" ? std::string("the menu: the default microphone")
                                         : "the menu: microphone " + view.devices[static_cast<std::size_t>(std::atoi(pick.c_str()))]);
    } else if (key == "ear_cue") {
        Put("Voice", "earCueStrength", fixed(v.Number("ear_cue") / 100.0f, 2), final);  // the mod re-reads it
    } else if (key == "fingers") {
        Put("Hands", "fingers", onOff("fingers"), true);
    } else if (key == "vent") {
        Put("Vent", "enabled", onOff("vent"), true);
    } else if (key == "mako_controls") {
        Put("Mako", "controls", onOff("mako_controls"), true);
    } else if (key == "cockpit") {
        Put("Cockpit", "enabled", onOff("cockpit"), true);
    } else if (key == "wrist_status") {
        Put("Hud", "wristStatus", onOff("wrist_status"), true);
    } else if (key == "gun_status") {
        Put("Hud", "gunStatus", onOff("gun_status"), true);
    } else if (key == "interact_label") {
        Put("Interact", "label", onOff("interact_label"), true);
    } else if (key == "scope_grade") {
        Put("Sights", "scopeCapturePost", onOff("scope_grade"), true);  // the mod applies it to the next scope capture it makes
    } else if (key == "turn") {
        Put("Comfort", "turn", v.Text("turn") == "snap" ? "snap" : "game", true);
    } else if (key == "snap") {
        Put("Comfort", "snapDeg", v.Text("snap"), true);
    } else if (key == "vignette") {
        Put("Comfort", "vignette", onOff("vignette"), true);
    } else if (key == "vignette_strength") {
        Put("Comfort", "vignetteStrength", fixed(v.Number("vignette_strength") / 100.0f, 2), final);
    } else if (key == "vignette_when") {
        Put("Comfort", "vignetteWhen", v.Text("vignette_when") == "move" ? "move" : "turn", true);
    } else if (key == "crouch") {
        Put("Hands", "physicalCrouch", onOff("crouch"), true);
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
    } else if (key == "explore_stance") {
        Put("Hands", "exploreStance", onOff("explore_stance"), true);
    } else if (key == "constant_eye") {
        Put("Hands", "constantEye", onOff("constant_eye"), true);
    } else if (key == "draw_rate") {
        Put("Hands", "drawRate", fixed(v.Number("draw_rate"), 1), final);
    } else if (key == "gun_kick") {
        Put("Hands", "gunKick", onOff("gun_kick"), true);
    } else if (key == "trim_pitch" || key == "trim_yaw" || key == "trim_roll") {
        char const* name = key == "trim_pitch" ? "handPitch" : key == "trim_yaw" ? "handYaw" : "handRoll";
        float const value = key == "trim_pitch" ? v.Number(key) + ms::kHandPitchDefault : v.Number(key);
        Put("Hands", name, fixed(value, 1), final);
    } else if (key == "sight_pistol") {
        Put("Sights", "pistol", ms::SightValue(v.Text(key), ""), true);
    } else if (key == "sight_shotgun") {
        Put("Sights", "shotgun", ms::SightValue(v.Text(key), ""), true);  // no scope chip: the shotgun's model has none
    } else if (key == "sight_rifle") {
        // A red dot by default (research/21 section 15). A scope is the assault rifle's own (its model has one: section 14),
        // 1.5x unless it had another zoom.
        std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
        wchar_t previous[64]{};
        GetPrivateProfileStringW(L"Sights", L"assaultRifle", L"dot", previous, 64, path.c_str());
        Put("Sights", "assaultRifle", ms::SightValue(v.Text(key), Narrow(previous), 1.5f), true);
    } else if (key == "sight_sniper") {
        std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
        wchar_t previous[64]{};
        GetPrivateProfileStringW(L"Sights", L"sniperRifle", L"scope auto", previous, 64, path.c_str());
        Put("Sights", "sniperRifle", ms::SightValue(v.Text(key), Narrow(previous), 0.0f), true);
    } else if (key == "upscale") {
        Put("Upscale", "method", v.Text("upscale") == "fsr" ? "fsr" : "off", true);  // plugin/Upscale re-reads it within a second
    } else if (key == "fsr_sharpness") {
        Put("Upscale", "sharpness", fixed(v.Number("fsr_sharpness") / 100.0f, 2), final);
    } else if (key == "cull_px") {
        Put("Performance", "minScreenRadiusPx", v.Text("cull_px"), true);  // plugin/RenderLevers re-reads it within a second
        ClearCullTenths();
    } else if (key == "grass") {
        // game/SystemSettings applies the section by `scale set` within a second of the file's change.
        Put("SystemSettings", "FoliageDrawRadiusMultiplier", fixed(vr::perf_modes::FoliageFromPercent(v.Number("grass")), 2), final);
    } else if (key == "dyn_shadows") {
        Put("SystemSettings", "DynamicShadows", v.Flag("dyn_shadows") ? "True" : "False", true);
    } else if (key == "ambient_occl") {
        Put("SystemSettings", "AmbientOcclusion", v.Flag("ambient_occl") ? "True" : "False", true);
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
        // Where I look: Halcyon's moveFollowsHead (as before). Left hand: [Comfort] walk = hand, the mod's flag and the
        // host's stick turn (research/31); moveFollowsHead stays as it is (the host takes Halcyon's turn off first).
        // Shepard's body: moveFollowsHead off (Halcyon's own), [Comfort] walk = head; no longer a chip (2026-11-04), so picking
        // Where I look turns moveFollowsHead back on.
        std::string const walk = v.Text("walk");
        bool const hand = walk == "hand";
        bool const body = walk == "body";
        Put("Comfort", "walk", hand ? "hand" : "head", true);
        if (!hand && c.moveFollowsHead == body) {
            c.moveFollowsHead = !body;
            HalcyonChanged(false);
        }
    } else if (key == "quality") {
        SetResolution(v.Text("quality"));
    } else if (key == "perf_mode") {
        // A mode's bundle (research/60): its live levers key by key, then its render resolution as the row above sets one. The
        // Custom chip only shows; it sets nothing.
        namespace pm = vr::perf_modes;
        if (pm::Mode const* mode = pm::Find(v.Text("perf_mode"))) {
            for (pm::Edit const& edit : pm::Edits(mode->levers)) {
                Put(edit.section.c_str(), edit.key.c_str(), edit.value, true);
            }
            ClearCullTenths();
            bool const sized = SetResolution(mode->levers.resolution);
            std::string const against = pm::Differences(mode->levers, pm::Find(pm::kDefault)->levers);
            log::Info("main menu: performance mode {}{}{}", mode->key, against.empty() ? std::string() : ": " + against + " (against Balanced)",
                sized ? "" : "; the render resolution waits until the headset's size is known");
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
        if (!noSave_) {  // as the resolution: GamerSettings.ini only with the profile
            MELEVR::Config::ApplyResolutionForCurrentMode(&wx, &wy);
        }
        modeNote_ = noSave_ ? std::string("Not saved: the VR settings were reset this session (Advanced > Profiles > Save keeps it).")
                  : wx > 0 ? std::format("Resolution set to {} \xC3\x97 {}: restart the game to apply this mode.", wx, wy)
                           : std::string("Restart the game to apply this mode.");
        log::Info("main menu: VR mode {} ({})", mode, modeNote_);
    } else if (key == "life_size") {
        c.sfr2Metric = v.Flag(key);  // life-size stereo (research/30)
        HalcyonChanged(false);
        log::Info("main menu: life-size stereo {}", c.sfr2Metric ? "on" : "off");
    } else if (key == "extra_depth") {
        c.sfr2MetricDepth = v.Number(key);
        HalcyonChanged(false);
    } else if (key == "eye_frustum") {
        c.sfr2MetricEyeFrustum = v.Flag(key);  // mode 2 (each eye's own frustum) or mode 1 (the symmetric window)
        HalcyonChanged(false);
        log::Info("main menu: each eye's own field of view {}", c.sfr2MetricEyeFrustum ? "on" : "off");
    } else if (key == "eye_distance") {
        c.sfr2HalfEyeUU = v.Number(key) * 0.5f;  // the row shows both eyes apart
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
    } else if (key == "panel_below") {
        c.hudPanelDropM = v.Number(key);
        HalcyonChanged(false);
    } else if (key == "panel_follow") {
        c.hudPanelFollowDeg = v.Number(key);
        HalcyonChanged(false);
    } else if (key == "g_convo") {
        c.convoEnabled = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "g_designer") {
        c.designerUiEnabled = v.Flag(key);
        HalcyonChanged(false);
    } else if (key == "convo_size" || key == "designer_size") {
        WriteSize(Movie(c, key == "designer_size"), v.Number(key));
        HalcyonChanged(false);
    } else if (key == "convo_x" || key == "designer_x") {
        UiMovie const m = Movie(c, key == "designer_x");
        *m.offsetX = m.defOffsetX + v.Number(key);
        HalcyonChanged(false);
    } else if (key == "convo_y" || key == "designer_y") {
        UiMovie const m = Movie(c, key == "designer_y");
        *m.offsetY = m.defOffsetY - v.Number(key);  // up is +; Flash's y grows downward
        HalcyonChanged(false);
    } else {
        log::Warn("main menu: no setting behind '{}'", key);
    }
}

void Settings::Command(std::string const& name, std::string const& argument, menu::Values& v) {
    log::Info("main menu: {}{}{}", name, argument.empty() ? "" : " ", argument);
    if (name == "voice_key_set") {
        // The next key pressed on the keyboard (Tick), within 10 s; keys down now don't count until let go.
        keyWaitMs_ = Now();
        keysDown_.fill(false);
        for (int vk = 8; vk <= 0xFE; ++vk) {
            keysDown_[static_cast<std::size_t>(vk)] = MELEVR::MevrBridge::KeyDown(vk);
        }
    } else if (name == "voice_key_clear") {
        keyWaitMs_ = 0;
        Put("Voice", "key", "0", true);
        voice::SetKey(0);
    } else if (name == "voice_restart") {
        voice::Restart("the menu's Restart");
    } else if (name == "resume") {
        after_ = After::Close;
    } else if (name == "game_menu") {
        after_ = After::GameStart;
    } else if (name == "game_back") {
        after_ = After::GameBack;
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
    } else if (name == "ui_reset") {
        // Advanced > Game UI's Standard: the movie back to Halcyon's tuned size and place (as its HUD tab's Reset buttons).
        UiMovie const m = Movie(MELEVR::Config::Get(), argument == "designer");
        *m.scaleX = m.defScaleX;
        *m.scaleY = m.defScaleY;
        *m.offsetX = m.defOffsetX;
        *m.offsetY = m.defOffsetY;
        HalcyonChanged(false);
        log::Info("main menu: the {} back to its standard size and place", argument == "designer" ? "mission overlays" : "conversation wheel");
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
                if (slot == MELEVR::Config::ActiveProfileIndex()) {
                    noSave_ = false;  // saved by hand: the session's settings are the profile's now
                    int wx = 0, wy = 0;
                    MELEVR::Config::ApplyResolutionForCurrentMode(&wx, &wy);  // GamerSettings.ini follows the saved target
                }
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
    } else if (name == "content_request") {
        setup_request::Request(contentMsg_);  // MEVR Setup opens on Optional content after the game exits (research/33)
    } else if (name == "report") {
        // MEVR Setup writes the zip in its own process, now (plugin/ReportRequest): the menu stays open.
        report_request::Request(v.Flag("report_snapshot"), v.Flag("report_perf"), reportMsg_);
    } else if (name == "crash_dismiss") {
        report_request::DismissCrash();
    } else if (name == "reset") {
        if (resetArmedMs_ == 0) {
            resetArmedMs_ = Now();
            diagMsg_ = "Press Confirm to put the VR view, stereo and HUD settings back to standard for this session.";
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
    if (keyWaitMs_ != 0) {
        if (now - keyWaitMs_ > 10000) {
            keyWaitMs_ = 0;
        } else {
            for (int vk = 8; vk <= 0xFE; ++vk) {
                bool const down = MELEVR::MevrBridge::KeyDown(vk);
                bool const was = keysDown_[static_cast<std::size_t>(vk)];
                keysDown_[static_cast<std::size_t>(vk)] = down;
                int const key = down && !was ? ms::VoiceKeyFromPress(vk, MELEVR::Config::Get().menuKey) : 0;
                if (key != 0) {
                    keyWaitMs_ = 0;
                    Put("Voice", "key", std::to_string(key), true);
                    voice::SetKey(key);
                    log::Info("main menu: the voice orders' hold-to-talk key is {} ({})", KeyName(key), key);
                    break;
                }
            }
        }
    }
    perf::Status const p = perf::GetStatus();
    if (!p.pending && !p.lastCapture.empty()) {
        perfMsg_ = "Saved in AppData\\Local\\MEVR\\perf. Switch on \"With the latest frame timing\" to put it in a report.";
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
    if (keyWaitMs_ != 0) {
        keyWaitMs_ = 0;
        cancelled = true;
    }
    return cancelled;
}

void Settings::Closed() {
    measure_ = Measure::Idle;
    keyWaitMs_ = 0;
    resetArmedMs_ = 0;
    diagMsg_.clear();
    Flush(true);
}

After Settings::TakeAfter() {
    After const a = after_;
    after_ = After::Nothing;
    return a;
}

void FirstStartEyeSize() {
    static bool done = false;
    if (done) {
        return;
    }
    MELEVR::XrSession::ResolutionStats resolution{};
    MELEVR::XrSession::GetResolutionStats(&resolution);
    int const recommendedW = static_cast<int>(resolution.recommendedEyeWidth);
    int const recommendedH = static_cast<int>(resolution.recommendedEyeHeight);
    if (recommendedW <= 0 || recommendedH <= 0) {
        return;  // VR hasn't started yet: the runtime's recommendation comes with the session
    }
    done = true;
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    bool const already = GetPrivateProfileIntW(L"Display", L"eyeSizeSet", 0, path.c_str()) != 0;
    bool const wanted = setup_request::EyeSizeAtFirstStart();
    if (!wanted || already) {
        return;  // a developer deploy, a size the player chose in the setup, or done before
    }
    MELEVR::Config::VrConfig& c = MELEVR::Config::Get();
    int const oldW = c.sfr2ResX;
    int const oldH = c.sfr2ResY;
    ms::Size const size = ms::FirstStartSize(wanted, already, {oldW, oldH}, recommendedW, recommendedH);
    if (size.width > 0) {
        c.sfr2ResX = size.width;
        c.sfr2ResY = size.height;
        c.expectedResX = size.width;
        c.expectedResY = size.height;
        int const active = MELEVR::Config::ActiveProfileIndex();
        MELEVR::Config::SaveProfile(active);
        int wx = 0, wy = 0;
        MELEVR::Config::ApplyResolutionForCurrentMode(&wx, &wy);
        log::Info("first VR start: the render resolution is now 0.85 of your headset's eye size, {}x{}, which FSR scales up to it "
                  "(MEVR Setup's stand-in was {}x{}); "
                  "profile {} saved, GamerSettings.ini {}: it applies from the next launch ([Display] eyeSizeSet = 1)",
            size.width, size.height, oldW, oldH, MELEVR::Config::ProfileName(active),
            wx > 0 ? std::format("set to {}x{}", wx, wy) : std::string("unchanged (auto-set per mode is off)"));
    } else {
        log::Info("first VR start: the render resolution {}x{} is already 0.85 of your headset's eye size ([Display] eyeSizeSet = 1)", oldW, oldH);
    }
    if (WritePrivateProfileStringW(L"Display", L"eyeSizeSet", L"1", path.c_str()) == 0) {
        log::Warn("first VR start: couldn't write [Display] eyeSizeSet to {}: the next VR start checks again", ini::Utf8(std::filesystem::path(path)));
    }
}

} // namespace mevr::host::main_menu
