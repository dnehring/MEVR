// SPDX-License-Identifier: MIT
#include "game/BodyCalibration.hpp"

#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"

#include "engine/GameAdapter.hpp"

#include <Windows.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>

namespace mevr::game::body_calibration {
namespace {

namespace fs = std::filesystem;
namespace cal = vr::calibration;

struct Values {
    bool loaded = false;        // a section with a time
    std::string key;
    std::string when;
    std::string label;
    std::optional<double> standEye;
    std::optional<double> crouchEye;
    std::optional<double> armLength;
    std::optional<double> shoulderWidth;
    std::optional<cal::Stance> pistol;
    std::optional<cal::Stance> rifle;
};

bool gUse = true;
Values gValues;
bool gCalibrating = false;
bool gLoggedCalibrating = false;
std::string gHostKey;            // from the host (v15); empty: none
std::uint32_t gHostSaves = 0;
bool gHaveHost = false;
ULONGLONG gLastCheckMs = 0;
fs::file_time_type gStamp{};
bool gHaveStamp = false;
std::string gLoggedText;
bool gToldNoHost = false;

fs::path FilePath() {
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return fs::path(buffer).parent_path() / L"ASI" / (MEVR_GAME_NAME_W L"MEVR.ini");
}

std::string Read(fs::path const& path, std::string const& section, char const* key) {
    wchar_t text[256]{};
    std::wstring const s(section.begin(), section.end());
    std::string const k(key);
    std::wstring const wk(k.begin(), k.end());
    GetPrivateProfileStringW(s.c_str(), wk.c_str(), L"", text, 256, path.wstring().c_str());
    std::string out;
    for (wchar_t const* c = text; *c != L'\0'; ++c) {
        out.push_back(*c < 128 ? static_cast<char>(*c) : '?');
    }
    return out;
}

std::optional<double> Number(fs::path const& path, std::string const& section, char const* key, double lo, double hi) {
    std::string const text = Read(path, section, key);
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    double const value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value) || value < lo || value > hi) {
        return std::nullopt;
    }
    return value;
}

// A stance whose three aims disagreed by more than this isn't used (a half-finished or confused step).
constexpr double kMaxSpreadDeg = 10.0;

// A stance's keys ("pistol" / "rifle" + Trim, Line, ...). Nullopt unless its trim and line read and its aims agreed.
std::optional<cal::Stance> ReadStance(fs::path const& path, std::string const& section, std::string const& prefix, int eye) {
    auto const trim = cal::ParseRotation(Read(path, section, (prefix + "Trim").c_str()));
    auto const line = cal::ParseVector(Read(path, section, (prefix + "Line").c_str()));
    if (!trim || !line || std::sqrt(line->x * line->x + line->y * line->y + line->z * line->z) > 0.6f) {
        return std::nullopt;
    }
    if (auto const spread = Number(path, section, (prefix + "SpreadDeg").c_str(), 0.0, 180.0); spread && *spread > kMaxSpreadDeg) {
        log::Warn("calibration: the {} stance in [{}] isn't used: its aims disagreed by {:.1f} degrees (more than {:.0f})", prefix, section,
            *spread, kMaxSpreadDeg);
        return std::nullopt;
    }
    cal::Stance s;
    s.valid = true;
    s.eye = eye;
    s.samples = 3;
    s.trim = *trim;
    s.line = *line;
    s.spreadDeg = static_cast<float>(Number(path, section, (prefix + "SpreadDeg").c_str(), 0.0, 90.0).value_or(0.0));
    s.eyeRelief = static_cast<float>(Number(path, section, (prefix + "EyeReliefM").c_str(), -2.0, 2.0).value_or(0.0));
    auto const foregrip = cal::ParseVector(Read(path, section, (prefix + "Foregrip").c_str()));
    auto const leftTrim = cal::ParseRotation(Read(path, section, (prefix + "LeftTrim").c_str()));
    auto const hands = cal::ParseVector(Read(path, section, (prefix + "HandsLine").c_str()));
    if (foregrip && leftTrim && hands && std::sqrt(foregrip->x * foregrip->x + foregrip->y * foregrip->y + foregrip->z * foregrip->z) < 1.2f) {
        s.hasLeft = true;
        s.foregrip = *foregrip;
        s.leftTrim = *leftTrim;
        s.handsLine = *hands;
        s.handsSpreadDeg = static_cast<float>(Number(path, section, (prefix + "HandsSpreadDeg").c_str(), 0.0, 90.0).value_or(0.0));
    }
    if (auto const stock = cal::ParseVector(Read(path, section, (prefix + "Stock").c_str()))) {
        s.stock = *stock;
    }
    return s;
}

Values ReadValues(fs::path const& path, std::string const& key) {
    Values v;
    v.key = key;
    if (key.empty()) {
        return v;
    }
    std::string const section = "Calibration." + key;
    v.when = Read(path, section, "time");
    if (v.when.empty()) {
        return v;
    }
    v.loaded = true;
    v.label = Read(path, section, "label");
    v.standEye = Number(path, section, "standEyeM", 0.8, 2.3);
    v.crouchEye = Number(path, section, "crouchEyeM", 0.2, 2.0);
    v.armLength = Number(path, section, "armLengthM", 0.3, 1.2);
    v.shoulderWidth = Number(path, section, "shoulderWidthM", 0.15, 0.75);
    int const eye = Read(path, section, "eye") == "left" ? 0 : 1;
    v.pistol = ReadStance(path, section, "pistol", eye);
    v.rifle = ReadStance(path, section, "rifle", eye);
    return v;
}

std::string Describe(Values const& v) {
    if (!gUse) {
        return "not used ([Calibration] use = 0): the guns follow the controller's aim pose";
    }
    if (v.key.empty()) {
        return "no controller type known yet";
    }
    if (!v.loaded) {
        return std::format("none for [Calibration.{}]: the guns follow the controller's aim pose", v.key);
    }
    auto const stance = [](char const* name, std::optional<cal::Stance> const& s) {
        if (!s) {
            return std::format("{} not measured", name);
        }
        return std::format("{} trim ({}), sight line {} m from the grip, spread {:.1f} deg{}", name, cal::FormatRotation(s->trim),
            cal::FormatVector(s->line), s->spreadDeg, s->hasLeft ? std::format(", foregrip {} m", cal::FormatVector(s->foregrip)) : "");
    };
    auto const meters = [](std::optional<double> const& m) { return m ? std::format("{:.2f} m", *m) : std::string("-"); };
    return std::format("[Calibration.{}] ({}), {}: {} eye; {}; {}{}; eyes {} standing, {} crouched; arms {}, shoulders {}", v.key,
        v.label.empty() ? "?" : v.label, v.when, v.pistol || v.rifle ? ((v.pistol ? v.pistol : v.rifle)->eye == 0 ? "left" : "right") : "-",
        stance("pistol", v.pistol), stance("rifle", v.rifle),
        !v.rifle && v.pistol ? " (the long guns take the pistol's grip frame, not its sight line)"
            : !v.rifle       ? " (the long guns follow the controller's aim pose)"
                             : "",
        meters(v.standEye), meters(v.crouchEye), meters(v.armLength), meters(v.shoulderWidth));
}

void Load(char const* why) {
    fs::path const path = FilePath();
    std::string const use = Read(path, "Calibration", "use");
    gUse = use.empty() || use != "0";
    std::string key = gHostKey;
    if (key.empty() && !gHaveHost) {
        key = Read(path, "Calibration", "last");  // an older host doesn't say which controllers: the last calibrated
    }
    gValues = ReadValues(path, key);
    std::string const text = Describe(gValues);
    if (text != gLoggedText) {
        gLoggedText = text;
        log::Info("calibration: {} ({})", text, why);
    }
}

bool Usable() {
    return gUse && gValues.loaded;
}

} // namespace

void Tick(Link const& link) {
    bool reload = false;
    char const* why = "the file changed";
    if (link.state != nullptr) {
        cal::HostState state;
        link.state(&state);
        if (state.size == sizeof(cal::HostState)) {
            gHaveHost = true;
            state.key[sizeof state.key - 1] = '\0';
            std::string const key = state.key;
            if (key != gHostKey) {
                gHostKey = key;
                reload = true;
                why = "the controllers changed";
            }
            if (state.saves != gHostSaves) {
                gHostSaves = state.saves;
                reload = true;
                why = "a calibration was saved";
            }
            gCalibrating = (state.flags & cal::kCalibrating) != 0;
        }
    } else if (!gToldNoHost) {
        gToldNoHost = true;
        log::Info("calibration: the host (services older than v15) doesn't run the guided calibration; [Calibration] last is used "
                  "if there is one");
    }
    if (gCalibrating != gLoggedCalibrating) {
        gLoggedCalibrating = gCalibrating;
        log::Info("calibration: {}", gCalibrating ? "the player is calibrating: holsters, grenades, melee, the squad trigger, the "
                                                    "support grip and the physical crouch stand down"
                                                  : "calibration over: everything back");
    }
    ULONGLONG const now = GetTickCount64();
    if (!reload && gHaveStamp && now - gLastCheckMs < 1000) {
        return;
    }
    gLastCheckMs = now;
    std::error_code ec;
    auto const stamp = fs::last_write_time(FilePath(), ec);
    if (ec) {
        return;
    }
    if (reload || !gHaveStamp || stamp != gStamp) {
        gStamp = stamp;
        gHaveStamp = true;
        Load(why);
    }
}

bool Calibrating() {
    return gCalibrating;
}

std::optional<cal::Stance> StanceFor(int weaponType) {
    if (!Usable() || weaponType < 0 || weaponType > 3) {
        return std::nullopt;
    }
    if (weaponType == 0) {
        return gValues.pistol;
    }
    // No rifle stance (the rifle step not finished, 2026-10-22): the pistol's grip frame, so a long gun points where
    // this controller's grip points for the player rather than along the runtime's aim pose (9 degrees apart on the
    // PS VR2 under SteamVR's Touch emulation). StanceBorrowed tells arm IK not to use its sight line.
    return gValues.rifle ? gValues.rifle : gValues.pistol;
}

bool StanceBorrowed(int weaponType) {
    return Usable() && weaponType >= 1 && weaponType <= 3 && !gValues.rifle && gValues.pistol;
}

std::optional<cal::Stance> TwoHanded() {
    if (!Usable() || !gValues.rifle || !gValues.rifle->hasLeft) {
        return std::nullopt;
    }
    return gValues.rifle;
}

std::optional<double> StandingEyeHeight() {
    return Usable() ? gValues.standEye : std::nullopt;
}

std::optional<double> ArmLength() {
    return Usable() ? gValues.armLength : std::nullopt;
}

std::optional<ik::posture::CrouchParams> CrouchParams(ik::posture::CrouchParams const& base) {
    if (!Usable() || !gValues.standEye || !gValues.crouchEye) {
        return std::nullopt;
    }
    auto const fractions = cal::CrouchFromHeights(*gValues.standEye, *gValues.crouchEye);
    if (!fractions) {
        return std::nullopt;
    }
    ik::posture::CrouchParams out = base;
    out.crouchBelow = fractions->crouchBelow;
    out.standAbove = fractions->standAbove;
    return ik::posture::Sanitize(out);
}

std::optional<ik::ShoulderZone> ShoulderZone() {
    if (!Usable() || !gValues.rifle) {
        return std::nullopt;
    }
    XrVector3f const s = gValues.rifle->stock;
    if (s.x == 0.0f && s.y == 0.0f && s.z == 0.0f) {
        return std::nullopt;
    }
    return ik::ShoulderZoneAround({s.x, s.y, s.z});
}

std::string StatusJson() {
    namespace json = vr::test_request;
    return std::format("{{\"calibrating\": {}, \"use\": {}, \"key\": {}, \"loaded\": {}, \"time\": {}, \"pistol\": {}, \"rifle\": {}, "
                       "\"twoHanded\": {}, \"standEyeM\": {}, \"crouchEyeM\": {}, \"armLengthM\": {}}}",
        gCalibrating ? "true" : "false", gUse ? "true" : "false", json::JsonString(gValues.key), gValues.loaded ? "true" : "false",
        json::JsonString(gValues.when), gValues.pistol ? "true" : "false", gValues.rifle ? "true" : "false",
        gValues.rifle && gValues.rifle->hasLeft ? "true" : "false", json::JsonNumber(gValues.standEye.value_or(-1.0)),
        json::JsonNumber(gValues.crouchEye.value_or(-1.0)), json::JsonNumber(gValues.armLength.value_or(-1.0)));
}

void LogStats() {
    if (gCalibrating) {
        log::Info("    calibration: the player is calibrating (the mod stands down)");
    }
}

void Shutdown() {
    gUse = true;
    gValues = {};
    gCalibrating = false;
    gLoggedCalibrating = false;
    gHostKey.clear();
    gHostSaves = 0;
    gHaveHost = false;
    gLastCheckMs = 0;
    gHaveStamp = false;
    gLoggedText.clear();
    gToldNoHost = false;
}

} // namespace mevr::game::body_calibration
