// SPDX-License-Identifier: MIT
#include "core/vr/VrCover.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::cover {
namespace {

Vec3 Minus(Vec3 const& a, Vec3 const& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
double Dot(Vec3 const& a, Vec3 const& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
double Length(Vec3 const& v) {
    return std::sqrt(Dot(v, v));
}
// The horizontal part, unit length; nullopt if (nearly) vertical.
std::optional<Vec3> Flat(Vec3 const& v) {
    double const h = std::hypot(v.x, v.y);
    if (h < 1e-3 * std::max(1.0, Length(v))) {
        return std::nullopt;
    }
    return Vec3{v.x / h, v.y / h, 0.0};
}

} // namespace

char const* Name(Why why) {
    switch (why) {
    case Why::Off: return "off";
    case Why::NoShooter: return "no shooter";
    case Why::Exposed: return "exposed";
    case Why::CoverTooFar: return "cover too far";
    case Why::Overhead: return "overhead";
    case Why::Covered: return "covered";
    }
    return "?";
}

Verdict Judge(Vec3 const& eyes, std::optional<Vec3> const& shooter, std::optional<Block> const& block, Settings const& settings) {
    Verdict v;
    if (!settings.on) {
        v.why = Why::Off;
        return v;
    }
    if (!shooter) {
        v.why = Why::NoShooter;
        return v;
    }
    if (!block) {
        v.why = Why::Exposed;
        return v;
    }
    double const perMeter = settings.unitsPerMeter > 0.0 ? settings.unitsPerMeter : 100.0;
    v.coverM = Length(Minus(block->point, eyes)) / perMeter;
    if (v.coverM > settings.reachM) {
        v.why = Why::CoverTooFar;
        return v;
    }
    // The shot's way in, flattened: from the shooter toward the player.
    std::optional<Vec3> const inward = Flat(Minus(eyes, *shooter));
    if (!inward) {
        v.why = Why::Overhead;
        return v;
    }
    Vec3 facing = *inward;
    double const normalLength = Length(block->normal);
    if (normalLength > 0.0 && std::abs(block->normal.z) / normalLength <= settings.topNormalZ) {
        // A wall: its own facing, turned toward the player if the surface reported its back.
        if (std::optional<Vec3> const wall = Flat(block->normal)) {
            facing = Dot(*wall, *inward) >= 0.0 ? *wall : Vec3{-wall->x, -wall->y, 0.0};
        }
    }
    v.coverNormal = facing;
    v.why = Why::Covered;
    return v;
}

// The level look is the sequels' (ME1 keeps the eye line): left out of ME1's build so its compiled code stays the same (the
// linker keeps unreferenced functions there, /DEBUG's /OPT:NOREF). The offline tests (no game) build it.
#if !defined(MEVR_GAME_LE1)
char const* Name(Rule rule) {
    switch (rule) {
    case Rule::Line: return "line";
    case Rule::Level: return "level";
    case Rule::Either: return "either";
    }
    return "?";
}

std::optional<Rule> ParseRule(std::string_view text) {
    if (text == "line" || text == "eyes") return Rule::Line;
    if (text == "level") return Rule::Level;
    if (text == "either" || text == "both") return Rule::Either;
    return std::nullopt;
}

std::optional<LevelLine> Level(Vec3 const& eyes, std::optional<Vec3> const& shooter, Settings const& settings, double dropUnits) {
    if (!shooter) {
        return std::nullopt;
    }
    std::optional<Vec3> const toward = Flat(Minus(*shooter, eyes));
    if (!toward) {
        return std::nullopt;
    }
    double const perMeter = settings.unitsPerMeter > 0.0 ? settings.unitsPerMeter : 100.0;
    double const reach = settings.reachM * perMeter;
    Vec3 const from{eyes.x, eyes.y, eyes.z - std::max(dropUnits, 0.0)};
    return LevelLine{from, {from.x + toward->x * reach, from.y + toward->y * reach, from.z}};
}

Verdict Choose(Rule rule, Verdict const& line, Verdict const& level) {
    switch (rule) {
    case Rule::Line: return line;
    case Rule::Level: return level;
    case Rule::Either: return line.Covered() || !level.Covered() ? line : level;
    }
    return line;
}
#endif

bool GameCone(Vec3 const& coverNormal, Vec3 const& hitLocation, Vec3 const& instigatorLocation) {
    Vec3 const shot = Minus(hitLocation, instigatorLocation);
    double const distance = Length(shot);
    if (distance <= kGameMinUnits) {
        return false;
    }
    Vec3 const reversed{-coverNormal.x, -coverNormal.y, -coverNormal.z};
    return Dot(shot, reversed) / distance < kGameConeDot;
}

void Add(ShelterCount& count, Why why) {
    switch (why) {
    case Why::Covered: ++count.hostiles; ++count.covered; break;
    case Why::Exposed: ++count.hostiles; ++count.exposed; break;
    case Why::CoverTooFar: ++count.hostiles; ++count.blockedFar; break;
    case Why::Overhead: ++count.hostiles; ++count.overhead; break;
    case Why::Off:
    case Why::NoShooter: break;  // nobody to judge
    }
}

char const* Name(ShelterWhy why) {
    switch (why) {
    case ShelterWhy::NoHostile: return "no awake hostile";
    case ShelterWhy::Exposed: return "in the open to a hostile";
    case ShelterWhy::NoCloseCover: return "hidden only by things far away";
    case ShelterWhy::Sheltered: return "behind cover";
    }
    return "?";
}

ShelterWhy Shelter(ShelterCount const& count) {
    if (count.hostiles <= 0) {
        return ShelterWhy::NoHostile;
    }
    if (count.exposed > 0 || count.overhead > 0) {
        return ShelterWhy::Exposed;
    }
    return count.covered > 0 ? ShelterWhy::Sheltered : ShelterWhy::NoCloseCover;
}

std::optional<RegenWaitDone> Step(RegenWait& w, double nowS, float timerIn, bool sheltered) {
    float const timer = std::isfinite(timerIn) ? timerIn : 0.0f;
    double const dt = w.lastS > 0.0 && nowS > w.lastS ? std::min(nowS - w.lastS, 0.25) : 0.0;
    std::optional<RegenWaitDone> done;
    if (timer > w.lastTimer + 0.05f) {
        // A hit set the timer (again): a new wait; one cut short by another hit isn't reported.
        w.waiting = true;
        w.startS = nowS;
        w.startTimer = timer;
        w.shelteredS = 0.0;
    } else if (w.waiting) {
        if (sheltered) {
            w.shelteredS += dt;
        }
        if (timer <= 0.0f) {
            done = RegenWaitDone{nowS - w.startS, w.shelteredS, w.startTimer};
            w.waiting = false;
        }
    }
    w.lastS = nowS;
    w.lastTimer = timer;
    return done;
}

} // namespace mevr::vr::cover
