// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/SquadContext.hpp"

#include <vector>

// Where a squadmate who gives up a standing order goes (research/54 "Retreat to cover"): not back to Shepard's heels but
// to cover between them and the threat, where they hold until it's calm and then come back. Pure math, unit-tested in
// tests/Main.cpp; game/SquadOrders supplies the cover candidates (the level's CoverLink slots near the squadmate).
namespace mevr::vr::squad_retreat {

struct Settings {
    float retreatM = 8.0f;        // how far back the ideal point lies
    float towardShepard = 1.0f;   // the pull toward Shepard...
    float awayFromThreat = 1.0f;  // ...and the push away from the hostiles' centre, as weights
    float stopShortM = 2.5f;      // never closer to Shepard than this
    float coverSearchM = 10.0f;   // cover within this of the ideal point is taken...
    float coverBehindM = 1.0f;    // ...if it is at least this much farther from the threat than the squadmate is
};

struct Choice {
    squad_context::Vec3 point;
    bool cover = false;   // a cover slot, not the open ideal point
    int coverIndex = -1;  // which candidate
};

// The point to fall back to: the ideal point `retreatM` from the squadmate along the pull toward Shepard and away from the
// threats' centre (with no threats: straight toward Shepard), clamped short of Shepard; then the best cover candidate near
// it (nearest to the ideal, behind the squadmate as seen from the threat), else the ideal point itself.
Choice Pick(squad_context::Vec3 const& member, squad_context::Vec3 const& shepard, std::vector<squad_context::Vec3> const& threats,
    std::vector<squad_context::Vec3> const& cover, Settings const& settings);

}  // namespace mevr::vr::squad_retreat
