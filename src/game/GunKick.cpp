// SPDX-License-Identifier: MIT
#include "game/GunKick.hpp"

#include "core/Log.hpp"
#include "core/ik/GunKick.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/BonePose.hpp"
#include "game/ArmIk.hpp"
#include "game/HandCalibration.hpp"
#include "game/Sights.hpp"
#include "game/SupportGrip.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <sstream>
#include <string>

namespace mevr::game::gun_kick {
namespace {

namespace gk = ik::gun_kick;

gk::Tuning const kTuning{};
// Shots between two ticks that each get their own push (more are counted; the cap holds the kick anyway).
constexpr int kMaxPending = 16;

enum class Source { Queued, GetRecoil, Remembered, Synthetic, Count };

char const* SourceName(Source source) {
    switch (source) {
    case Source::Queued: return "the kick the game queued for the view (SetWeaponRecoil)";
    case Source::GetRecoil: return "BioWeaponRanged.GetRecoil";
    case Source::Remembered: return "the last value this weapon had";
    case Source::Synthetic: return "mevr kick";
    default: return "?";
    }
}

char const* SourceKey(Source source) {
    switch (source) {
    case Source::Queued: return "queued";
    case Source::GetRecoil: return "getRecoil";
    case Source::Remembered: return "remembered";
    case Source::Synthetic: return "synthetic";
    default: return "none";
    }
}

char const* TypeName(weapon::RangedType type) {
    switch (type) {
    case weapon::RangedType::Pistol: return "pistol";
    case weapon::RangedType::Shotgun: return "shotgun";
    case weapon::RangedType::AssaultRifle: return "assault rifle";
    case weapon::RangedType::Sniper: return "sniper rifle";
    default: return "gun";
    }
}

struct Pending {
    double recoilDeg = 0.0;
    bool synthetic = false;
};
std::array<Pending, kMaxPending> gPending{};
int gPendingCount = 0;

gk::Spring gSpring;
Kick gCurrent;
double gTwoHand = 0.0;  // this tick's shaping inputs
double gRaised = 0.0;
std::uintptr_t gWeapon = 0;  // the held weapon the remembered recoil is for
double gRemembered = -1.0;

// `mevr kick`: synthetic shots, one every gSyntheticInterval seconds.
int gSyntheticLeft = 0;
double gSyntheticRecoil = 1.0;
double gSyntheticInterval = 0.1;
double gSyntheticWait = 0.0;

#if defined(MEVR_GAME_LE1)
// Our reflection layer (research/34): the weapon's own recoil for a shot (research/24 section 2: Stats.Recoil or ZoomRecoil,
// times the talent curves), a native with a plain float return, which ProcessEvent passes back.
ue3::FunctionRef gGetRecoil{"SFXGame.BioWeaponRanged.GetRecoil"};
#endif

bool gToldFirst = false;
bool gToldNoRecoil = false;
bool gToldNoFunction = false;
double gLastRecoil = -1.0;
Source gLastSource = Source::Count;

// Heartbeat window.
unsigned gShots = 0;
unsigned gKicked = 0;
unsigned gNotAimed = 0;
unsigned gNoRecoil = 0;
unsigned gOverflow = 0;
std::array<unsigned, static_cast<std::size_t>(Source::Count)> gBySource{};
double gRecoilMin = 0.0;
double gRecoilMax = 0.0;
double gPeakAngle = 0.0;
double gPeakBack = 0.0;
double gPeakTwoHand = 0.0;
std::uint64_t gKickedWritesAtBeat = 0;
// Totals.
unsigned long long gShotsTotal = 0;
unsigned long long gKickedTotal = 0;

std::optional<double> GameRecoil() {
#if defined(MEVR_GAME_LE1)
    ue3::Object* const held = weapon::HeldObject();
    if (held == nullptr) {
        return std::nullopt;
    }
    ue3::Function* const function = gGetRecoil.Get();
    if (function == nullptr) {
        if (!gToldNoFunction) {
            gToldNoFunction = true;
            log::Warn("gun kick: BioWeaponRanged.GetRecoil not found: a shot whose queued kick wasn't measured kicks by the "
                      "weapon's last value, or not at all");
        }
        return std::nullopt;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(held, function, parms);  // directly: never a generated wrapper (CLAUDE.md)
    std::optional<float> const value = parms.Return<float>();
    if (value && std::isfinite(*value) && *value >= 0.0f && *value < 90.0f) {
        return static_cast<double>(*value);
    }
#endif
    return std::nullopt;
}

void NoteRecoil(double recoil) {
    gRecoilMin = gKicked == 0 ? recoil : std::min(gRecoilMin, recoil);
    gRecoilMax = gKicked == 0 ? recoil : std::max(gRecoilMax, recoil);
}

// One shot while the gun is aimed: its recoil (from the best source there is), then its push.
void Push(Pending const& shot, double scale) {
    double recoil = 0.0;
    Source source = Source::Count;
    if (shot.synthetic) {
        recoil = shot.recoilDeg;
        source = Source::Synthetic;
    } else if (shot.recoilDeg > 1e-4) {
        recoil = shot.recoilDeg;
        source = Source::Queued;
    } else if (auto const game = GameRecoil()) {
        recoil = *game;
        source = Source::GetRecoil;
    } else if (gRemembered >= 0.0 && gWeapon != 0 && gWeapon == weapon::Key()) {
        recoil = gRemembered;
        source = Source::Remembered;
    } else {
        ++gNoRecoil;
        if (!gToldNoRecoil) {
            gToldNoRecoil = true;
            log::Warn("gun kick: a shot without a recoil value (the game queued no kick for the view, GetRecoil gave none and the "
                      "weapon had none before): it doesn't kick");
        }
        return;
    }
    if (!shot.synthetic) {
        gRemembered = recoil;
    }
    gk::Offset const peak = gk::ShotKick(recoil, scale, kTuning);
    gSpring.Kick(peak, kTuning);
    NoteRecoil(recoil);
    ++gKicked;
    ++gKickedTotal;
    ++gBySource[static_cast<std::size_t>(source)];
    gLastRecoil = recoil;
    gLastSource = source;
    if (!gToldFirst) {
        gToldFirst = true;
        gk::Offset const drawn = gk::Shaped(peak, support_grip::Blend(), sights::RaisedWeight(), kTuning);
        log::Info("gun kick: first kick: the {}'s recoil {:.2f} degrees ({}) -> {:.2f} degrees muzzle up and {:.1f} cm back at its "
                  "peak ({:.0f} ms after the shot{}), back to a tenth in about {:.0f} ms",
            TypeName(weapon::Type()), recoil, SourceName(source), drawn.angleDeg, drawn.backM * 100.0, 1000.0 / kTuning.omega,
            support_grip::Blend() > 0.0 ? ", both hands on the gun" : "", 5.3 * 1000.0 / kTuning.omega);
    }
}

} // namespace

void NoteShot(double queuedRecoilDeg) {
    ++gShots;
    ++gShotsTotal;
    if (gPendingCount >= kMaxPending) {
        ++gOverflow;
        return;
    }
    gPending[static_cast<std::size_t>(gPendingCount++)] = {std::isfinite(queuedRecoilDeg) ? queuedRecoilDeg : 0.0, false};
}

void Tick(double deltaSeconds) {
    double const dt = std::isfinite(deltaSeconds) ? std::clamp(deltaSeconds, 0.0, 0.25) : 0.0;
    hand_calibration::GunKick const settings = hand_calibration::GetGunKick();
    bool const on = settings.on && settings.scale > 0.0;
    std::uintptr_t const key = weapon::Key();
    bool const aimed = key != 0 && arm_ik::RightHandAimsGun();
    if (key != gWeapon) {
        gWeapon = key;
        gRemembered = -1.0;
        gSpring.Reset();  // another gun (or none) in the hand: its kick isn't this one's
    }
    // `mevr kick`'s shots, on their own clock.
    if (gSyntheticLeft > 0) {
        gSyntheticWait -= dt;
        while (gSyntheticLeft > 0 && gSyntheticWait <= 0.0 && gPendingCount < kMaxPending) {
            gPending[static_cast<std::size_t>(gPendingCount++)] = {gSyntheticRecoil, true};
            ++gShots;
            ++gShotsTotal;
            --gSyntheticLeft;
            gSyntheticWait += gSyntheticInterval;
        }
    }
    for (int i = 0; i < gPendingCount; ++i) {
        Pending const& shot = gPending[static_cast<std::size_t>(i)];
        if (on && aimed) {
            Push(shot, settings.scale);
        } else if (on) {
            ++gNotAimed;
        }
    }
    gPendingCount = 0;
    if (!on || !aimed) {
        gSpring.Reset();
    }
    gSpring.Step(dt, kTuning);
    gTwoHand = support_grip::Blend();
    gRaised = sights::RaisedWeight();
    gk::Offset const shaped = gk::Shaped(gSpring.Value(kTuning), gTwoHand, gRaised, kTuning);
    gCurrent = {};
    if (on && aimed && gSpring.Moving() && (shaped.angleDeg > 1e-4 || shaped.backM > 1e-6)) {
        gCurrent = {true, shaped.angleDeg, shaped.backM};
        if (shaped.angleDeg > gPeakAngle) {
            gPeakAngle = shaped.angleDeg;
        }
        gPeakBack = std::max(gPeakBack, shaped.backM);
        gPeakTwoHand = std::max(gPeakTwoHand, gTwoHand);
    }
}

Kick Current() {
    return gCurrent;
}

std::string Command(std::string const& args) {
    std::istringstream words(args);
    double recoil = 1.0;
    int shots = 1;
    int ms = 100;
    words >> recoil >> shots >> ms;
    if (!std::isfinite(recoil)) {
        recoil = 1.0;
    }
    gSyntheticRecoil = std::clamp(recoil, 0.0, 10.0);
    gSyntheticLeft = std::clamp(shots, 1, 60);
    gSyntheticInterval = std::clamp(ms, 20, 2000) / 1000.0;
    gSyntheticWait = 0.0;
    gk::Offset const peak = gk::ShotKick(gSyntheticRecoil, hand_calibration::GetGunKick().scale, kTuning);
    return std::format("gun kick: {} synthetic shot{} of {:.2f} degrees of recoil, {} ms apart (each {:.2f} degrees muzzle up, "
                       "{:.1f} cm back at its peak, one-handed); they kick only while arm IK aims a gun in the right hand "
                       "(`mevr weapon assault` draws one) and [Hands] gunKick is on (now {})",
        gSyntheticLeft, gSyntheticLeft == 1 ? "" : "s", gSyntheticRecoil, static_cast<int>(gSyntheticInterval * 1000.0), peak.angleDeg,
        peak.backM * 100.0, hand_calibration::GetGunKick().on ? "on" : "off");
}

std::string StatusJson() {
    namespace json = vr::test_request;
    hand_calibration::GunKick const settings = hand_calibration::GetGunKick();
    gk::Offset const raw = gSpring.Raw();
    engine::bone_pose::KickSamples const written = engine::bone_pose::WrittenGunKicks();
    return std::format("{{\"on\": {}, \"scale\": {}, \"active\": {}, \"angleDeg\": {}, \"backM\": {}, \"rawAngleDeg\": {}, \"rawBackM\": {}, "
                       "\"twoHandBlend\": {}, \"raisedWeight\": {}, \"shots\": {}, \"kicked\": {}, \"lastRecoilDeg\": {}, \"lastSource\": {}, "
                       "\"drawn\": {{\"kicked\": {}, \"angleDeg\": {}, \"moveUnits\": {}, \"kickedWrites\": {}}}}}",
        settings.on ? "true" : "false", json::JsonNumber(settings.scale), gCurrent.active ? "true" : "false",
        json::JsonNumber(gCurrent.angleDeg), json::JsonNumber(gCurrent.backM), json::JsonNumber(raw.angleDeg), json::JsonNumber(raw.backM),
        json::JsonNumber(gTwoHand), json::JsonNumber(gRaised), gShotsTotal, gKickedTotal, json::JsonNumber(gLastRecoil),
        json::JsonString(SourceKey(gLastSource)), written.latest.kicked ? "true" : "false",
        json::JsonNumber(written.latest.kicked ? gk::AngleDeg(written.latest.kick) : 0.0),
        json::JsonNumber(written.latest.kicked ? gk::MoveLength(written.latest.kick) : 0.0), written.kickedWrites);
}

void LogStats() {
    hand_calibration::GunKick const settings = hand_calibration::GetGunKick();
    std::uint64_t const kickedWrites = engine::bone_pose::WrittenGunKicks().kickedWrites;
    std::uint64_t const drawn = kickedWrites - std::min(kickedWrites, gKickedWritesAtBeat);
    gKickedWritesAtBeat = kickedWrites;
    if (!arm_ik::Enabled() && gShots == 0 && drawn == 0) {
        // Not in VR (or arm IK off): nothing to say.
    } else if (!settings.on || !(settings.scale > 0.0)) {
        log::Info("    gun kick: off ([Hands] gunKick = {}, gunKickScale {:.2f}){}", settings.on ? 1 : 0, settings.scale,
            gShots > 0 ? std::format("; {} shots not kicked", gShots) : std::string());
    } else if (gShots == 0 && gKicked == 0 && drawn == 0) {
        log::Info("    gun kick: on (scale {:.2f}), no shots", settings.scale);
    } else {
        std::string sources;
        for (std::size_t s = 0; s < gBySource.size(); ++s) {
            if (gBySource[s] > 0) {
                sources += std::format("{}{} {}", sources.empty() ? "" : ", ", gBySource[s], SourceKey(static_cast<Source>(s)));
            }
        }
        log::Info("    gun kick: on (scale {:.2f}); {} shots: {} kicked ({}), {} not aimed by arm IK, {} without a recoil value{}; recoil "
                  "{:.2f}..{:.2f} degrees a shot; peak as drawn {:.2f} degrees muzzle up and {:.1f} cm back (both hands on the gun "
                  "{:.0f}% at most); the writer drew {} kicked poses",
            settings.scale, gShots, gKicked, sources.empty() ? std::string("none") : sources, gNotAimed, gNoRecoil,
            gOverflow > 0 ? std::format(", {} past {} a tick", gOverflow, kMaxPending) : std::string(), gRecoilMin, gRecoilMax, gPeakAngle,
            gPeakBack * 100.0, gPeakTwoHand * 100.0, drawn);
    }
    gShots = gKicked = gNotAimed = gNoRecoil = gOverflow = 0;
    gBySource = {};
    gRecoilMin = gRecoilMax = 0.0;
    gPeakAngle = gPeakBack = gPeakTwoHand = 0.0;
}

void Shutdown() {
    gPendingCount = 0;
    gSpring.Reset();
    gCurrent = {};
    gTwoHand = gRaised = 0.0;
    gWeapon = 0;
    gRemembered = -1.0;
    gSyntheticLeft = 0;
    gSyntheticWait = 0.0;
#if defined(MEVR_GAME_LE1)
    gGetRecoil.Reset();
#endif
    gToldFirst = gToldNoRecoil = gToldNoFunction = false;
    gLastRecoil = -1.0;
    gLastSource = Source::Count;
    gShots = gKicked = gNotAimed = gNoRecoil = gOverflow = 0;
    gBySource = {};
    gRecoilMin = gRecoilMax = 0.0;
    gPeakAngle = gPeakBack = gPeakTwoHand = 0.0;
    gKickedWritesAtBeat = 0;
    gShotsTotal = gKickedTotal = 0;
}

} // namespace mevr::game::gun_kick
