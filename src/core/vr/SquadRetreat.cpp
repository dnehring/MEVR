// SPDX-License-Identifier: MIT
#include "core/vr/SquadRetreat.hpp"

#include <cmath>

namespace mevr::vr::squad_retreat {

namespace sc = squad_context;

namespace {

sc::Vec3 Sub(sc::Vec3 const& a, sc::Vec3 const& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
sc::Vec3 Add(sc::Vec3 const& a, sc::Vec3 const& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
sc::Vec3 Times(sc::Vec3 const& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Flat(sc::Vec3 const& v) { return std::sqrt(v.x * v.x + v.y * v.y); }
sc::Vec3 Unit(sc::Vec3 const& v) {
    float const l = Flat(v);
    return l > 1e-4f ? sc::Vec3{v.x / l, v.y / l, 0.0f} : sc::Vec3{};
}

}  // namespace

Choice Pick(sc::Vec3 const& member, sc::Vec3 const& shepard, std::vector<sc::Vec3> const& threats, std::vector<sc::Vec3> const& cover,
    Settings const& s) {
    Choice choice;
    // The pull: toward Shepard, and away from the threats' centre (flat: the floor decides heights).
    sc::Vec3 pull = Times(Unit(Sub(shepard, member)), s.towardShepard);
    sc::Vec3 centre;
    if (!threats.empty()) {
        for (sc::Vec3 const& t : threats) centre = Add(centre, t);
        centre = Times(centre, 1.0f / static_cast<float>(threats.size()));
        pull = Add(pull, Times(Unit(Sub(member, centre)), s.awayFromThreat));
    }
    sc::Vec3 direction = Unit(pull);
    if (Flat(direction) < 0.5f) direction = Unit(Sub(shepard, member));  // the pulls cancelled: toward Shepard
    if (Flat(direction) < 0.5f) direction = {-1.0f, 0.0f, 0.0f};         // on Shepard's spot with no threat: anywhere back
    // The ideal point, never past Shepard.
    float distance = s.retreatM;
    float const toShepard = Flat(Sub(shepard, member));
    if (toShepard > 0.0f) {
        float const along = direction.x * (shepard.x - member.x) + direction.y * (shepard.y - member.y);
        if (along > 0.0f) distance = std::fmin(distance, std::fmax(0.0f, along - s.stopShortM));
    }
    sc::Vec3 ideal = Add(member, Times(direction, distance));
    ideal.z = member.z;
    choice.point = ideal;
    // Cover near it, behind the squadmate as the threat sees it.
    float best = s.coverSearchM;
    for (std::size_t i = 0; i < cover.size(); ++i) {
        sc::Vec3 const& c = cover[i];
        float const fromIdeal = Flat(Sub(c, ideal));
        if (fromIdeal > best) continue;
        if (!threats.empty() && Flat(Sub(c, centre)) < Flat(Sub(member, centre)) + s.coverBehindM) continue;
        if (Flat(Sub(c, shepard)) < s.stopShortM) continue;
        best = fromIdeal;
        choice.point = c;
        choice.cover = true;
        choice.coverIndex = static_cast<int>(i);
    }
    return choice;
}

}  // namespace mevr::vr::squad_retreat
