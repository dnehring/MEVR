#include "core/ik/BodySlots.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>

namespace mevr::ik::body_slots {
namespace {

bool On(Input const& input, int slot) {
    double const d = input.distance[slot];
    return std::isfinite(d) && d >= 0.0 && input.radius[slot] > 0.0 && input.weapon[slot] != Weapon::None;
}

} // namespace

char const* Name(Weapon weapon) {
    switch (weapon) {
    case Weapon::Pistol: return "pistol";
    case Weapon::Shotgun: return "shotgun";
    case Weapon::AssaultRifle: return "assault rifle";
    case Weapon::Sniper: return "sniper rifle";
    case Weapon::None: break;
    }
    return "none";
}

std::optional<Weapon> ParseWeapon(std::string_view text) {
    std::string lower;
    for (char c : text) {
        if (c != ' ' && c != '\t' && c != '_' && c != '-') {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (lower == "pistol") return Weapon::Pistol;
    if (lower == "shotgun") return Weapon::Shotgun;
    if (lower == "rifle" || lower == "assault" || lower == "assaultrifle" || lower == "ar") return Weapon::AssaultRifle;
    if (lower == "sniper" || lower == "sniperrifle") return Weapon::Sniper;
    if (lower == "none" || lower == "off") return Weapon::None;
    return std::nullopt;
}

char const* Name(Action action) {
    switch (action) {
    case Action::None: return "none";
    case Action::Draw: return "draw";
    case Action::Holster: return "holster";
    case Action::Unavailable: return "unavailable";
    case Action::AlreadyHeld: return "already held";
    case Action::HolsterArmed: return "holster armed";
    case Action::HolsterCancelled: return "holster cancelled";
    case Action::Missed: return "missed";
    }
    return "?";
}

Layout DefaultLayout() {
    Layout layout;
    layout.slots[0] = {"hipRight", Weapon::Pistol, {0.20, -0.62, -0.05}, 0.14};
    layout.slots[1] = {"shoulderRight", Weapon::AssaultRifle, {0.18, -0.08, -0.20}, 0.20};
    layout.slots[2] = {"belly", Weapon::Shotgun, {0.00, -0.50, 0.12}, 0.13};
    layout.slots[3] = {"shoulderLeft", Weapon::Sniper, {-0.18, -0.08, -0.20}, 0.20};
    return layout;
}

double Scale(Layout const& layout) {
    double const scale = layout.eyeHeight / kReferenceEyeHeight;
    return std::isfinite(scale) ? std::clamp(scale, 0.5, 1.5) : 1.0;
}

Vec3 YawForward(double yaw) {
    return {-std::sin(yaw), 0.0, -std::cos(yaw)};
}

Vec3 YawRight(double yaw) {
    return {std::cos(yaw), 0.0, -std::sin(yaw)};
}

Vec3 NeutralEye(Vec3 headPosition, Quat headOrientation, double neckBack, double neckDown) {
    // The pivot in the head's own axes: behind (+Z) and below the eyes.
    Vec3 const neck = headPosition + Rotate(headOrientation, Vec3{0.0, -neckDown, neckBack});
    double const yaw = YawOf(headOrientation.x, headOrientation.y, headOrientation.z, headOrientation.w);
    return neck + YawForward(yaw) * neckBack + Vec3{0.0, neckDown, 0.0};
}

Vec3 SlotCenter(Frame const& frame, Slot const& slot, double scale) {
    Vec3 const o = slot.offset * scale;
    return frame.anchor + YawRight(frame.yaw) * o.x + Vec3{0.0, o.y, 0.0} + YawForward(frame.yaw) * o.z;
}

Vec3 Anchor::Update(Vec3 neutralEye, double deltaSeconds) {
    if (!initialized_ || !std::isfinite(y_)) {
        y_ = neutralEye.y;
        initialized_ = true;
    } else if (heightSeconds_ > 0.0) {
        double const k = 1.0 - std::exp(-std::max(deltaSeconds, 0.0) / heightSeconds_);
        y_ += (neutralEye.y - y_) * k;
    } else {
        y_ = neutralEye.y;
    }
    return {neutralEye.x, y_, neutralEye.z};
}

void Machine::Reset() {
    inSlot_ = -1;
    phase_ = Phase::Idle;
    armedSlot_ = -1;
    armedWeapon_ = Weapon::None;
}

Step Machine::Update(Input const& input) {
    Step step;

    // Which slot the hand is in: stays in its slot until past radius + margin; otherwise the one
    // it's inside with the smallest distance / radius.
    int const before = inSlot_;
    if (!input.tracked) {
        inSlot_ = -1;
    } else {
        if (inSlot_ >= 0 && !(On(input, inSlot_) && input.distance[inSlot_] <= input.radius[inSlot_] + input.exitMargin)) {
            inSlot_ = -1;
        }
        if (inSlot_ < 0) {
            double best = std::numeric_limits<double>::infinity();
            for (int i = 0; i < kSlots; ++i) {
                if (On(input, i) && input.distance[i] <= input.radius[i]) {
                    double const ratio = input.distance[i] / input.radius[i];
                    if (ratio < best) {
                        best = ratio;
                        inSlot_ = i;
                    }
                }
            }
        }
    }
    if (inSlot_ != before) {
        step.left = before;
        step.entered = inSlot_;
    }
    step.nearby = inSlot_ >= 0;
    for (int i = 0; input.tracked && i < kSlots; ++i) {
        step.nearby = step.nearby || (On(input, i) && input.distance[i] <= input.radius[i] + input.exitMargin);
    }

    auto const act = [&step](Action action, int slot, Weapon weapon) {
        step.action = action;
        step.slot = slot;
        step.weapon = weapon;
    };

    // A new squeeze: whatever came before is over (its release may have fallen between steps).
    if (input.newSqueeze) {
        phase_ = Phase::Idle;
        armedSlot_ = -1;
        armedWeapon_ = Weapon::None;
        if (inSlot_ >= 0) {
            Weapon const weapon = input.weapon[inSlot_];
            bool const ownSlot = input.held != Weapon::None && input.held == weapon;
            bool const emptySlot = input.held != Weapon::None && !input.available[inSlot_] &&
                input.holster == Holster::OwnOrEmptySlot;
            if (ownSlot || emptySlot) {
                if (input.holster == Holster::Off) {
                    act(Action::AlreadyHeld, inSlot_, weapon);
                    phase_ = Phase::Grab;
                } else {
                    act(Action::HolsterArmed, inSlot_, input.held);
                    phase_ = Phase::HolsterArmed;
                    armedSlot_ = inSlot_;
                    armedWeapon_ = input.held;
                }
            } else if (input.available[inSlot_]) {
                act(Action::Draw, inSlot_, weapon);
                phase_ = Phase::Grab;
            } else {
                act(Action::Unavailable, inSlot_, weapon);
                phase_ = Phase::Grab;
            }
        } else if (step.nearby) {
            act(Action::Missed, -1, Weapon::None);
        }
    }

    // An armed holster is called off by leaving its slot, the trigger, or the gun changing.
    if (phase_ == Phase::HolsterArmed && (inSlot_ != armedSlot_ || input.trigger || input.held != armedWeapon_)) {
        if (step.action == Action::None) {
            act(Action::HolsterCancelled, armedSlot_, armedWeapon_);
        }
        phase_ = Phase::Grab;
    }

    if (!input.gripDown) {
        if (phase_ == Phase::HolsterArmed) {
            act(Action::Holster, armedSlot_, armedWeapon_);
        }
        phase_ = Phase::Idle;
        armedSlot_ = -1;
        armedWeapon_ = Weapon::None;
    }
    return step;
}

} // namespace mevr::ik::body_slots
