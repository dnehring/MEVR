// SPDX-License-Identifier: MIT
#pragma once

#include <optional>

// VR cover (research/23 section 6): the protection all three games give a player in cover, for a VR player who takes
// cover with their body. In each game the damage code cuts a hit on the player while they're in cover and not leaning out,
// if the shot comes from the cover's front (its direction within about 72 degrees of the cover's normal) and from more than
// 300 units away: LE1 by `CoverDamageReduction` (all of it on Casual and Normal, 90% above), LE2 to zero, LE3 by its own
// `CoverDamageReduction` (and LE3 adds 40% out of cover from Normal up). A VR player never takes the game's cover (no
// snapping: research/23 section 2), so this decides, for one hit, whether they're in cover against that shooter: the head
// plays the game's "not leaning out". Covered when the line from the eyes to the shooter meets a surface within `reachM` of
// the eyes; then `coverNormal` is the wall's facing (from the cover toward the player) for the game's own cone test. The
// line is traced from the eyes so the surface found is the player's own cover, not one near the shooter. Pure math, game
// independent (world units, Z up); unit-tested in tests/VrCoverTests.cpp. Each game's module traces the line and makes its
// damage code see the verdict.
namespace mevr::vr::cover {

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

struct Settings {
    bool on = true;
    double reachM = 1.2;          // the cover must stand within this of the eyes (crouched behind a crate, about 0.4-0.9)
    double unitsPerMeter = 100.0;
    double topNormalZ = 0.7;      // a surface whose normal is steeper than this (a crate's top, an overhang) gets its facing from the shot
};

// The first surface the line from the eyes toward the shooter meets.
struct Block {
    Vec3 point;
    Vec3 normal;  // the surface's normal there (facing back toward the eyes)
};

enum class Why {
    Off,          // the setting is off
    NoShooter,    // no shooter's place (the game's code then has none either)
    Exposed,      // nothing between the eyes and the shooter: the head is in the open
    CoverTooFar,  // something blocks the line, but farther than reachM from the eyes (not the player's cover)
    Overhead,     // the shooter is (nearly) straight above or below: no cover's facing
    Covered,      // the head is hidden behind cover from this shooter
};
char const* Name(Why why);

struct Verdict {
    Why why = Why::Off;
    Vec3 coverNormal;    // when covered: horizontal, unit, from the cover toward the player (the game's CoverNormal)
    double coverM = 0.0; // when blocked: the eyes' distance to the surface
    bool Covered() const {
        return why == Why::Covered;
    }
};

Verdict Judge(Vec3 const& eyes, std::optional<Vec3> const& shooter, std::optional<Block> const& block, Settings const& settings);

// The games' own cone and distance (LE1 BioPawnBehavior.ProxyTakeDamage, LE2 SFXGame.ModifyDamage, LE3
// SFXPawn_PlayerParty.ModifyDamage): the hit is cut when the shot's direction, from the instigator to the hit, against the
// cover's normal is below -0.3 (dot with the reversed normal) and the hit is more than 300 units from the instigator.
inline constexpr double kGameConeDot = -0.3;
inline constexpr double kGameMinUnits = 300.0;
bool GameCone(Vec3 const& coverNormal, Vec3 const& hitLocation, Vec3 const& instigatorLocation);

} // namespace mevr::vr::cover
