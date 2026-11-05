// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/SquadContext.hpp"

#include <cstdint>

// Standing orders: the memory above the game's one-shot squad primitives (research/54 "Standing orders"). "Hold the
// bridge" is not a move: it is an intent kept per squadmate until a condition ends it, re-asserted through the game's
// own orders (LE1's henchman AI drifts back to Shepard and drops targets on its own), and given up when the squadmate
// is overwhelmed or hurt. The rules only, unit-tested in tests/Main.cpp; game/SquadOrders observes the scene, keeps
// one Standing per squadmate and makes the game calls each Issue asks for.
namespace mevr::vr::squad_standing {

enum class Kind : std::uint8_t {
    HoldAt,       // hold a point: back to it whenever they drift
    AttackGroup,  // keep after a group while it lives
};
enum class Until : std::uint8_t {
    Calm,      // no hostile near the point and no combat for calmS (the fighting stopped)
    GroupGone, // the group has no living member
    Seconds,   // untilS from the order
    Relieved,  // only a new order ends it
};
char const* Name(Kind kind);
char const* Name(Until until);

struct Settings {
    float reassertS = 4.0f;         // a drifted hold or a dropped target is re-ordered this often at most
    float holdRadiusM = 4.0f;       // this far from the point is "drifted"
    float calmRadiusM = 25.0f;      // hostiles within this of the point keep a hold alive
    float calmS = 8.0f;             // Calm: this long without them (and without combat)
    float retreatHealth = 0.3f;     // below this the squadmate gives up and falls back...
    int overwhelmCount = 3;         // ...or with this many hostiles within overwhelmRadiusM
    float overwhelmRadiusM = 8.0f;
    float maxAgeS = 180.0f;         // a standing older than this is stale: they come back
};

struct Intent {
    char who = 0;                 // the squadmate's letter
    Kind kind = Kind::HoldAt;
    squad_context::Vec3 point;    // HoldAt
    int group = 0;                // AttackGroup
    Until until = Until::Calm;
    float untilS = 0.0f;          // Until::Seconds
    bool mayRetreat = true;       // "no matter what" turns it off
    bool returnAfter = true;      // when it ends by itself: told to follow ("then come back"); "stay there" keeps them
};

// What the mod sees of the squadmate and the point this tick.
struct Observation {
    double dt = 0.0;
    bool alive = true;
    float health = 1.0f;          // 0..1 (unknown: 1)
    squad_context::Vec3 position; // the squadmate
    int hostilesNearPoint = 0;    // living hostiles within calmRadiusM of the point
    int hostilesNearMember = 0;   // ...within overwhelmRadiusM of the squadmate
    bool groupAlive = false;      // AttackGroup: any member left
    bool combat = false;          // the player's combat state
};

enum class Phase : std::uint8_t { Active, Returning, Retreating, Done };
char const* Name(Phase phase);

struct Issue {
    enum Kind : std::uint8_t { None, MoveTo, Follow, Attack } kind = None;
    squad_context::Vec3 point;  // MoveTo
    int group = 0;              // Attack
    char const* why = "";
};

struct Standing {
    Intent intent;
    Phase phase = Phase::Active;
    double ageS = 0.0;
    double sinceIssueS = 1e9;   // a fresh standing issues at once
    double calmForS = 0.0;
    bool engaged = false;       // hostiles were ever near the point (a hold at a quiet spot still ends on Calm)
    // One tick: what to tell the game now (None most ticks). After Done nothing more.
    Issue Advance(Observation const& seen, Settings const& settings);
    bool Done() const { return phase == Phase::Done; }
};

}  // namespace mevr::vr::squad_standing
