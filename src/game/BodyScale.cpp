// SPDX-License-Identifier: MIT
#include "game/BodyScale.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/ik/BodyScale.hpp"
#include "core/vr/FloorLock.hpp"
#include "core/vr/HostView.hpp"
#include "engine/BonePose.hpp"
#include "engine/SettingsFile.hpp"
#include "game/Posture.hpp"
#include "game/BodyCalibration.hpp"
#include "game/Player.hpp"
#include "game/Weapon.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <sstream>
#include <string>

namespace mevr::game::body_scale {
namespace {

namespace bs = ik::body_scale;

enum class Mode { Auto, Off, Fixed };

struct Settings {
    Mode mode = Mode::Auto;
    double fixed = 1.0;
};

constexpr double kRatePerSecond = 0.1;  // the factor's largest change per second
constexpr double kRiseS = 0.5;          // the estimates rise to a higher sample this fast...
constexpr double kFallS = 30.0;         // ...and fall to a lower one this slowly (a dip or a lean doesn't shrink him)

struct State {
    Settings settings;
    std::uint64_t settingsMs = 0;
    std::optional<double> override;  // the console's (`mevr body scale <s>`), over the file's
    bool overrideOff = false;
    double s = 1.0;
    double target = 1.0;
    std::optional<double> shepardEye;  // component units, unscaled
    std::optional<double> viewEye;     // component units above the floor
    double unitsPerMeter = 100.0;
    std::string why = "not measured yet";
    std::string loggedWhy;
    unsigned steps = 0;
    vr::floor_lock::StandingReference standing;
    std::string reference = "legacy filtered view / lowest bone";
    std::optional<double> referencePlayer;
    std::optional<double> referenceShepard;
    bool floorReference = false;
};
State g;

void ReadSettings() {
    std::uint64_t const now = GetTickCount64();
    if (g.settingsMs != 0 && now - g.settingsMs < 2000) {
        return;
    }
    g.settingsMs = now;
    ini::Loaded const loaded = ini::Read(engine::SettingsFile());
    Settings s;
    if (auto const text = ini::Get(loaded.file, "Body", "scale")) {
        std::string const value = ini::Lower(ini::Trim(*text));
        if (value == "off" || value == "0") {
            s.mode = Mode::Off;
        } else if (auto const number = ini::Number(value); number && value != "auto") {
            s.mode = Mode::Fixed;
            s.fixed = std::clamp(*number, bs::kMin, bs::kMax);
        }
    }
    if (s.mode != g.settings.mode || s.fixed != g.settings.fixed || g.steps == 0) {
        log::Info("body scale: [Body] scale = {}", s.mode == Mode::Auto ? "auto (Shepard's eyes where the view is)"
                                                   : s.mode == Mode::Off ? "off"
                                                                         : std::format("{:.3f}", s.fixed));
    }
    g.settings = s;
}

void Track(std::optional<double>& estimate, double sample, double dt) {
    if (!estimate) {
        estimate = sample;
        return;
    }
    double const tau = sample > *estimate ? kRiseS : kFallS;
    *estimate += (sample - *estimate) * std::min(1.0, std::max(0.0, dt) / tau);
}

void NoteWhy(std::string why) {
    g.why = std::move(why);
    if (g.why != g.loggedWhy) {
        g.loggedWhy = g.why;
        log::Info("body scale: {}", g.why);
    }
}

} // namespace

double Step(double viewAboveFloor, double unitsPerMeter, double deltaSeconds,
    std::optional<double> shepardPawnEyeM, std::uintptr_t skeleton) {
    ReadSettings();
    ++g.steps;
    g.unitsPerMeter = unitsPerMeter > 0.0 ? unitsPerMeter : g.unitsPerMeter;
    bool const crouched = posture::PhysicallyCrouched();
    auto const view = vr::host_view::Latest();
    bool const locked = view.valid && view.floorLocked;
    g.floorReference = locked;
    if (locked) {
        bool const gameCrouch = player::Crouched(player::BioPawn(player::Controller())).value_or(true);
        auto const calibrated = body_calibration::StandingEyeHeight();
        bool const headLow = calibrated && view.stageEyeM < *calibrated * 0.9;
        g.standing.Feed(skeleton, crouched || headLow, gameCrouch, weapon::Key() == 0,
            view.stageEyeM, shepardPawnEyeM.value_or(0.0), deltaSeconds);
        g.referencePlayer = calibrated ? calibrated : g.standing.Player();
        g.referenceShepard = g.standing.Shepard();
        g.reference = calibrated ? "calibrated standing / frozen Shepard cylinder floor"
                                 : "learned standing STAGE / frozen Shepard cylinder floor";
    } else {
        g.reference = "legacy filtered view / lowest bone";
    }
    // Shepard standing holstered (the combat stance carries his head 16 cm lower), unscaled, from the writer's last pose.
    // Only heights a standing person can have count: a floor read far below (a trace through a gap) said 50 m once and took
    // the scale to its minimum for good.
    if (auto const body = engine::bone_pose::UnscaledBody(); !locked &&
        body && weapon::Key() == 0 && !crouched && bs::Plausible(body->eyeHeight, g.unitsPerMeter)) {
        Track(g.shepardEye, body->eyeHeight, deltaSeconds);
    }
    if (!locked && !crouched && bs::Plausible(viewAboveFloor, g.unitsPerMeter)) {
        Track(g.viewEye, viewAboveFloor, deltaSeconds);
    }
    Mode const mode = g.overrideOff ? Mode::Off : g.override ? Mode::Fixed : g.settings.mode;
    std::optional<double> const playerEye = locked ? g.referencePlayer : g.viewEye;
    std::optional<double> const shepardEye = locked ? g.referenceShepard : g.shepardEye;
    double const referenceUnits = locked ? 1.0 : g.unitsPerMeter;
    if (mode == Mode::Off) {
        g.target = 1.0;
        NoteWhy("off");
    } else if (mode == Mode::Fixed) {
        g.target = g.override.value_or(g.settings.fixed);
        NoteWhy(std::format("fixed at {:.3f}{}", g.target, g.override ? " (console)" : ""));
    } else if (auto const f = playerEye && shepardEye ? bs::Factor(*playerEye, *shepardEye, referenceUnits) : std::nullopt) {
        g.target = *f;
        NoteWhy(locked ? "auto: fixed standing references" : "auto: Shepard's eyes where the view is");
    } else {
        g.target = 1.0;
        NoteWhy(!shepardEye ? "auto: waiting for Shepard standing holstered (his eye height)" : "auto: waiting for the standing eye height");
    }
    g.s = bs::Approach(g.s, g.target, kRatePerSecond, deltaSeconds);
    return g.s;
}

double Idle(double deltaSeconds) {
    g.s = bs::Approach(g.s, 1.0, kRatePerSecond * 3.0, deltaSeconds);
    return g.s;
}

std::string Command(std::string const& args) {
    std::istringstream words(args);
    std::string value;
    words >> value;
    if (value == "auto") {
        g.override.reset();
        g.overrideOff = false;
        return "body scale: auto ([Body] scale's own setting)";
    }
    if (value == "off") {
        g.override.reset();
        g.overrideOff = true;
        return "body scale: off (console)";
    }
    if (auto const number = ini::Number(value); number && *number >= bs::kMin && *number <= bs::kMax) {
        g.override = *number;
        g.overrideOff = false;
        return std::format("body scale: fixed at {:.3f} (console)", *number);
    }
    return std::format("body scale: {:.3f} now (target {:.3f}; {}); give auto, off or {:.2f}..{:.2f}", g.s, g.target, g.why, bs::kMin,
        bs::kMax);
}

std::string StatusJson() {
    auto meters = [](std::optional<double> v) { return v ? std::format("{:.3f}", *v / g.unitsPerMeter) : std::string("null"); };
    auto standing = [](std::optional<double> v) { return v ? std::format("{:.3f}", *v) : std::string("null"); };
    return std::format("{{\"scale\": {:.4f}, \"target\": {:.4f}, \"why\": \"{}\", \"shepardEyeM\": {}, \"viewEyeM\": {}, \"reference\": \"{}\", "
        "\"standingPlayerM\": {}, \"standingShepardM\": {}}}", g.s, g.target,
        g.why, g.floorReference ? standing(g.referenceShepard) : meters(g.shepardEye),
        g.floorReference ? standing(g.referencePlayer) : meters(g.viewEye), g.reference,
        g.referencePlayer ? std::format("{:.4f}", *g.referencePlayer) : "null",
        g.referenceShepard ? std::format("{:.4f}", *g.referenceShepard) : "null");
}

void LogStats() {
    if (g.steps == 0) {
        return;
    }
    auto const shepard = g.floorReference ? g.referenceShepard : g.shepardEye;
    auto const player = g.floorReference ? g.referencePlayer : g.viewEye;
    double const units = g.floorReference ? 1.0 : g.unitsPerMeter;
    log::Info("    body scale: {:.3f} (target {:.3f}; {}); reference: {}; standing Shepard {} m, player {} m", g.s, g.target,
        g.why, g.reference, shepard ? std::format("{:.3f}", *shepard / units) : std::string("?"),
        player ? std::format("{:.3f}", *player / units) : std::string("?"));
}

void Shutdown() {
    g = {};
}

} // namespace mevr::game::body_scale
