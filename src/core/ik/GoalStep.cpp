// SPDX-License-Identifier: MIT
#include "core/ik/GoalStep.hpp"

#include <cmath>

namespace mevr::ik {

std::optional<Transform> TransformOfAffine(Affine const& a) {
    std::optional<Quat> const rotation = RotationOf(a);
    double const determinant = Determinant3(a);
    if (!rotation || !std::isfinite(determinant) || !(determinant > 1e-12)) {
        return std::nullopt;
    }
    return Transform{*rotation, {a.m[3][0], a.m[3][1], a.m[3][2]}, std::cbrt(determinant)};
}

Step FindStep(StepInput const& in) {
    Step out;
    out.componentMove = Vec3{in.componentNow.m[3][0] - in.componentThen.m[3][0], in.componentNow.m[3][1] - in.componentThen.m[3][1],
        in.componentNow.m[3][2] - in.componentThen.m[3][2]};
    out.anchorMove = in.anchorKnown ? in.anchorNow - in.anchorThen : Vec3{};
    out.componentUnits = Length(out.componentMove);
    out.anchorUnits = Length(out.anchorMove);
    bool const finite = std::isfinite(out.componentUnits) && std::isfinite(out.anchorUnits);
    double const units = std::isfinite(in.units) && in.units > 0.0 ? in.units : 20.0;
    out.seen = finite && (out.componentUnits > units || out.anchorUnits > units);
    if (!out.seen) {
        return out;
    }
    // The camera rule only where the engine moved the skeleton under a moving component (both past `units`): a teleport moves the
    // component alone (the camera goes with the pawn), an animation can move the camera's bone alone (a held view doesn't).
    if (in.rule == StepRule::Camera && in.anchorKnown && out.anchorUnits > units && out.componentUnits > units) {
        out.toNow = Transform{{}, out.anchorMove, 1.0};
    } else if (in.rule == StepRule::World && out.componentUnits > units) {
        std::optional<Transform> const then = TransformOfAffine(in.componentThen);
        std::optional<Transform> const now = TransformOfAffine(in.componentNow);
        if (then && now) {
            Transform d = Compose(*then, Inverse(*now));
            d.scale = 1.0;  // one mesh: its scale doesn't change between a tick and the next call
            out.toNow = d;
        }
    }
    return out;
}

} // namespace mevr::ik
