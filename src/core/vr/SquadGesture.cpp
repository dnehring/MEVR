// SPDX-License-Identifier: MIT
#include "core/vr/SquadGesture.hpp"

namespace mevr::vr::squad_gesture {

char const* Name(Hit hit) {
    switch (hit) {
    case Hit::None: return "nothing";
    case Hit::Enemy: return "an enemy";
    case Hit::Ground: return "the ground";
    case Hit::Squadmate: return "a squadmate";
    }
    return "nothing";
}

void Machine::Reset() {
    holding = false;
    heldS = 0.0;
    member = -1;
    lastHit = Hit::None;
    lastGroup = 0;
}

Step Machine::Advance(Input const& in, Settings const& settings) {
    Step step;
    if (!holding) {
        if (!settings.enabled || !in.usable || !in.newSqueeze) {
            return step;
        }
        holding = true;
        heldS = 0.0;
        member = in.pointedMember;
        lastHit = in.hit;
        lastGroup = in.hitGroup;
        step.started = true;
        return step;
    }
    heldS += in.dt;
    if (!settings.enabled || !in.usable) {
        step.cancelled = true;
        step.why = !settings.enabled ? "turned off" : "no longer usable (off foot, untracked, or no squad)";
        Reset();
        return step;
    }
    if (in.squeezed) {
        if (heldS > settings.maxHoldS) {
            step.cancelled = true;
            step.why = "held too long";
            Reset();
            return step;
        }
        step.hitChanged = in.hit != lastHit || (in.hit == Hit::Enemy && in.hitGroup != lastGroup);
        lastHit = in.hit;
        lastGroup = in.hitGroup;
        return step;
    }
    // Let go: the order.
    if (heldS < settings.minHoldS) {
        step.cancelled = true;
        step.why = "a blip";
        Reset();
        return step;
    }
    Order order;
    order.given = true;
    order.member = member;
    switch (in.hit) {
    case Hit::Enemy:
        order.order = squad_context::Order::Attack;
        order.group = in.hitGroup;
        break;
    case Hit::Ground:
        if (in.hitDistanceM >= settings.followWithinM) {
            order.order = squad_context::Order::Move;
            order.toPoint = true;
        } else {
            order.order = squad_context::Order::Follow;
        }
        break;
    case Hit::Squadmate:
        order.order = squad_context::Order::Follow;
        if (order.member < 0) {
            order.member = in.pointedMember;
        }
        break;
    case Hit::None:
        order.order = squad_context::Order::Follow;
        break;
    }
    step.order = order;
    Reset();
    return step;
}

}  // namespace mevr::vr::squad_gesture
