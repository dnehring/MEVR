// SPDX-License-Identifier: MIT
#include "core/vr/CombatStance.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mevr::vr::combat_stance {
namespace {

bool Contains(std::string_view text, std::string_view part) {
    if (part.empty() || text.size() < part.size()) {
        return part.empty();
    }
    for (std::size_t i = 0; i + part.size() <= text.size(); ++i) {
        bool same = true;
        for (std::size_t j = 0; same && j < part.size(); ++j) {
            same = std::tolower(static_cast<unsigned char>(text[i + j])) == std::tolower(static_cast<unsigned char>(part[j]));
        }
        if (same) {
            return true;
        }
    }
    return false;
}

} // namespace

char const* Name(Why why) {
    switch (why) {
    case Why::Active: return "the body composes as explore";
    case Why::Off: return "off ([Hands] exploreStance = 0)";
    case Why::NoView: return "no head-tracked view (VR off, a menu or the flat screen)";
    case Why::NotOnFoot: return "not on foot";
    case Why::NoPawn: return "no player BioPawn";
    case Why::NotFirstPerson: return "not first person (or arm IK isn't placing the hands in the view)";
    case Why::Holstered: return "holstered: the game's explore stance already";
    case Why::Crouched: return "crouched: the game's combat crouch kept (the explore crouch is a static pose)";
    case Why::Cover: return "in cover";
    case Why::Scoped: return "the sniper scope";
    case Why::Storming: return "sprinting ([Hands] exploreStanceStorm = 0)";
    case Why::NoNode: return "no CombatMode node (NonCombat, Combat) in the player's AnimTree";
    case Why::NoHook: return "the UpdateSkelPose hook isn't in (arm IK never turned on)";
    case Why::OrderUnknown: return "waiting to see the AnimTree tick before UpdateSkelPose";
    case Why::OrderInside: return "the AnimTree ticks inside UpdateSkelPose: this seam can't compose as explore; stood down";
    }
    return "?";
}

Why Decide(Inputs const& in) {
    if (!in.enabled) {
        return Why::Off;
    }
    if (!in.headTrackedView) {
        return Why::NoView;
    }
    if (!in.onFoot) {
        return Why::NotOnFoot;
    }
    if (!in.pawn) {
        return Why::NoPawn;
    }
    if (!in.firstPerson) {
        return Why::NotFirstPerson;
    }
    if (!in.drawn) {
        return Why::Holstered;
    }
    if (in.crouched) {
        return Why::Crouched;
    }
    if (in.cover) {
        return Why::Cover;
    }
    if (in.scoped) {
        return Why::Scoped;
    }
    if (in.storming && !in.stanceWhileStorming) {
        return Why::Storming;
    }
    if (!in.node) {
        return Why::NoNode;
    }
    if (!in.hook) {
        return Why::NoHook;
    }
    if (!in.orderKnown) {
        return Why::OrderUnknown;
    }
    if (in.orderInside) {
        return Why::OrderInside;
    }
    return Why::Active;
}

void OrderCheck::Feed(float deltaTime, std::int32_t startTag, std::int32_t endTag) {
    if (!(deltaTime > 0.0f) || result_ == Order::Inside) {
        return;  // the attach and force-update paths (DeltaTime 0) tick nothing either way
    }
    if (endTag != startTag) {
        // The node ticked during the call: the tree's tick is inside UpdateSkelPose. Twice, so a stray tick from
        // another caller can't decide it.
        if (++inside_ >= 2) {
            result_ = Order::Inside;
        }
    } else if (havePrevious_ && startTag != previousEnd_) {
        // It ticked between the previous call and this one, and not during this one.
        if (++confirmations_ >= kNeeded && inside_ == 0) {
            result_ = Order::Before;
        }
    }
    havePrevious_ = true;
    previousEnd_ = endTag;
}

char const* Name(OrderCheck::Order order) {
    switch (order) {
    case OrderCheck::Order::Unknown: return "unknown";
    case OrderCheck::Order::Before: return "the tree ticks before UpdateSkelPose composes";
    case OrderCheck::Order::Inside: return "the tree ticks inside UpdateSkelPose";
    }
    return "?";
}

Vec ExploreEye(Vec exploreHook, Vec exploreOffset) {
    return {exploreHook.x - exploreOffset.x, exploreHook.y - exploreOffset.y, exploreHook.z - exploreOffset.z};
}

Vec ConstantEyeOffset(Vec modeHook, Vec eye) {
    return {modeHook.x - eye.x, modeHook.y - eye.y, modeHook.z - eye.z};
}

Vec RotateToWorld(std::int32_t pitch, std::int32_t yaw, Vec local) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kUnit = 2.0 * kPi / 65536.0;
    double const p = static_cast<double>(pitch % 65536) * kUnit;
    double const y = static_cast<double>(yaw % 65536) * kUnit;
    double const sp = std::sin(p);
    double const cp = std::cos(p);
    double const sy = std::sin(y);
    double const cy = std::cos(y);
    return {local.x * cp * cy - local.y * sy - local.z * sp * cy,
            local.x * cp * sy + local.y * cy - local.z * sp * sy,
            local.x * sp + local.z * cp};
}

Vec BlendLocationOffset(Vec locationOffset, std::int32_t pitch, std::int32_t yaw, Vec oldOffset, Vec newOffset) {
    // To moves by -R * (new - old); the stored offset (To - From) moves with it.
    Vec const moved = RotateToWorld(pitch, yaw, {newOffset.x - oldOffset.x, newOffset.y - oldOffset.y, newOffset.z - oldOffset.z});
    return {locationOffset.x - moved.x, locationOffset.y - moved.y, locationOffset.z - moved.z};
}

bool OffsetMoved(Vec oldOffset, Vec newOffset) {
    return std::fabs(newOffset.x - oldOffset.x) > 0.01 || std::fabs(newOffset.y - oldOffset.y) > 0.01 ||
           std::fabs(newOffset.z - oldOffset.z) > 0.01;
}

bool ConstantEyeApplies(std::string_view className, std::string_view objectName) {
    if (Contains(className, "Crouch") || Contains(objectName, "Crouch")) {
        return false;
    }
    if (Contains(className, "Vehicle") || Contains(className, "Sniper") || Contains(className, "HipAim") || Contains(className, "Cover")) {
        return false;  // Halcyon never hands these over; belt and braces
    }
    return Contains(className, "Explore") || Contains(className, "Combat") || Contains(className, "TightAim");
}

bool IsExploreMode(std::string_view className, std::string_view objectName) {
    return Contains(className, "Explore") && !Contains(className, "Storm") && !Contains(className, "Crouch") &&
        !Contains(objectName, "Crouch") && !Contains(objectName, "Storm");
}

bool PlausibleHook(Vec hook) {
    for (double v : {hook.x, hook.y, hook.z}) {
        if (!std::isfinite(v) || std::fabs(v) > 1000.0) {
            return false;
        }
    }
    return true;
}

bool UsableRate(float rate) {
    return std::isfinite(rate) && rate > 0.0f && rate <= 100.0f;
}

bool RampNeedsWrite(float tacAccel, float tacDecel, float exploreAccel, float exploreDecel) {
    if (!UsableRate(exploreAccel) || !UsableRate(exploreDecel)) {
        return false;
    }
    return std::fabs(tacAccel - exploreAccel) > 1e-4f || std::fabs(tacDecel - exploreDecel) > 1e-4f;
}

double ClampDrawRate(double rate) {
    return std::isfinite(rate) ? std::clamp(rate, 1.0, kMaxDrawRate) : 1.0;
}

bool RateNeedsWrite(float current, double wanted) {
    return !std::isfinite(current) || std::fabs(static_cast<double>(current) - wanted) > 1e-3;
}

} // namespace mevr::vr::combat_stance
