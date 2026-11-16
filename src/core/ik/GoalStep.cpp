// SPDX-License-Identifier: MIT
#include "core/ik/GoalStep.hpp"

#include <cmath>

namespace mevr::ik {

Step FindStep(StepInput const& in) {
    Step out;
    out.componentMove = Vec3{in.componentNow.m[3][0] - in.componentThen.m[3][0], in.componentNow.m[3][1] - in.componentThen.m[3][1],
        in.componentNow.m[3][2] - in.componentThen.m[3][2]};
    out.anchorMove = in.anchorKnown ? in.anchorNow - in.anchorThen : Vec3{};
    out.componentUnits = Length(out.componentMove);
    out.anchorUnits = Length(out.anchorMove);
    double const units = std::isfinite(in.units) && in.units > 0.0 ? in.units : 1.0;
    out.seen = in.anchorKnown && std::isfinite(out.anchorUnits) && out.anchorUnits > units;
    // Only while the view hangs on the anchor: a held view (a conversation shown in the world, a scripted walk) doesn't go where
    // the bone goes, and an animation's jump of the bone there must not move the hands.
    if (out.seen && in.rule == StepRule::Camera && in.viewOnAnchor) {
        out.toNow = Transform{{}, out.anchorMove, 1.0};
    }
    return out;
}

} // namespace mevr::ik
