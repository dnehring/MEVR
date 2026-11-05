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

bool GameCone(Vec3 const& coverNormal, Vec3 const& hitLocation, Vec3 const& instigatorLocation) {
    Vec3 const shot = Minus(hitLocation, instigatorLocation);
    double const distance = Length(shot);
    if (distance <= kGameMinUnits) {
        return false;
    }
    Vec3 const reversed{-coverNormal.x, -coverNormal.y, -coverNormal.z};
    return Dot(shot, reversed) / distance < kGameConeDot;
}

} // namespace mevr::vr::cover
