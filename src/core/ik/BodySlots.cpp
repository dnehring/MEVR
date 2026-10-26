// SPDX-License-Identifier: MIT
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
    case Action::ReturnArmed: return "return armed";
    case Action::ReturnCancelled: return "return cancelled";
    case Action::Return: return "return";
    }
    return "?";
}

Layout DefaultLayout() {
    Layout layout;
    layout.slots[0] = {"hipRight", Weapon::Pistol, {0.20, -0.62, -0.05}, 0.14};
    layout.slots[1] = {"shoulderRight", Weapon::AssaultRifle, {0.18, -0.08, -0.20}, 0.20};
    // A cross-draw at the left hip (2026-10-25): the belly slot (0, -0.50, +0.12) sat in front of the right hand's rest,
    // about 0.33 m from the pistol, and was entered a dozen times a session on the way to the hip.
    layout.slots[2] = {"hipLeft", Weapon::Shotgun, {-0.20, -0.62, 0.00}, 0.14};
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
    drawn_ = Weapon::None;
    returning_ = Weapon::None;
    returnLeft_ = 0.0;
    returnWaited_ = 0.0;
    returnAwaitsDraw_ = false;
}

void Machine::UpdateHolding(Input const& input, Step& step) {
    auto const act = [&step](Action action, int slot, Weapon weapon) {
        step.action = action;
        step.slot = slot;
        step.weapon = weapon;
    };

    // A new squeeze: in a slot it takes that slot's gun (draws it, or takes the one that's out of
    // it); anywhere else it takes hold of whatever is out. Either way a return in progress is over.
    if (input.newSqueeze) {
        bool const wasReturning = phase_ == Phase::Returning;
        Weapon const wasReturningGun = returning_;
        phase_ = Phase::Grab;
        drawn_ = Weapon::None;
        returning_ = Weapon::None;
        if (inSlot_ >= 0) {
            Weapon const weapon = input.weapon[inSlot_];
            if (input.held != Weapon::None && input.held == weapon) {
                act(Action::AlreadyHeld, inSlot_, weapon);
            } else if (input.available[inSlot_]) {
                act(Action::Draw, inSlot_, weapon);
                drawn_ = weapon;
            } else {
                act(Action::Unavailable, inSlot_, weapon);
            }
        } else if (wasReturning) {
            act(Action::ReturnCancelled, -1, wasReturningGun);
        } else if (step.nearby) {
            act(Action::Missed, -1, Weapon::None);
        }
    }

    // A squeeze that began while we weren't running, holding a gun that's out: it's held.
    if (phase_ == Phase::Idle && input.gripDown && input.gripKept && !input.newSqueeze && input.held != Weapon::None) {
        phase_ = Phase::Grab;
    }

    // The grip opens on a squeeze we saw: let go of the gun. At its own slot (or an empty one with
    // OwnOrEmptySlot) it's holstered now; elsewhere it goes back after the grace. A squeeze and
    // release between two steps acts on the squeeze first and on the release next step.
    if (!input.gripDown && phase_ == Phase::Grab && step.action == Action::None) {
        Weapon const gun = input.held != Weapon::None ? input.held : drawn_;  // a draw on its way counts
        phase_ = Phase::Idle;
        if (gun != Weapon::None && input.holster != Holster::Off) {
            bool const ownSlot = inSlot_ >= 0 && input.weapon[inSlot_] == gun;
            bool const emptySlot = inSlot_ >= 0 && !input.available[inSlot_] && input.holster == Holster::OwnOrEmptySlot;
            if ((ownSlot || emptySlot) && input.held == gun && !input.trigger && !input.switching) {
                act(Action::Holster, inSlot_, gun);
            } else if (input.releaseGrace >= 0.0 || ownSlot || emptySlot) {
                // Let go at the gun's slot while drawing (the gun not out yet, or the switch still animating:
                // the game ignores TryHolster then) or firing: it goes back once that's over, with no grace
                // (where it was let go says what was meant). A new squeeze before then keeps it.
                phase_ = Phase::Returning;
                returning_ = gun;
                returnLeft_ = ownSlot || emptySlot ? 0.0 : std::max(input.releaseGrace, 0.0);
                returnWaited_ = 0.0;
                returnAwaitsDraw_ = input.held == Weapon::None;
                act(Action::ReturnArmed, inSlot_, gun);
            }
        }
        drawn_ = Weapon::None;
        return;
    }

    // The grace: it runs while the gun is out and the trigger isn't held.
    if (phase_ == Phase::Returning && step.action == Action::None) {
        if (input.held != Weapon::None && input.held != returning_) {
            act(Action::ReturnCancelled, -1, returning_);  // the gun changed
            phase_ = Phase::Idle;
        } else if (input.held == Weapon::None) {
            returnWaited_ += std::max(input.deltaSeconds, 0.0);
            if (!returnAwaitsDraw_ || returnWaited_ > kReturnWaitForDrawSeconds) {
                act(Action::ReturnCancelled, -1, returning_);  // put away already, or never came out
                phase_ = Phase::Idle;
            }
        } else if (input.trigger) {
            returnAwaitsDraw_ = false;
            step.returnWaitsForTrigger = true;  // never in the middle of a burst
        } else if (input.switching) {
            returnAwaitsDraw_ = false;
            step.returnWaitsForSwitch = true;  // TryHolster would be ignored mid-switch: wait for its end
        } else {
            returnAwaitsDraw_ = false;
            returnLeft_ -= std::max(input.deltaSeconds, 0.0);
            if (returnLeft_ <= 0.0) {
                act(Action::Return, -1, returning_);
                phase_ = Phase::Idle;
            }
        }
        if (phase_ == Phase::Idle) {
            returning_ = Weapon::None;
        }
    }
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

    // Switching between the two grip modes drops whatever was in progress.
    if (input.gripHolds != holds_) {
        int const keep = inSlot_;
        Reset();
        inSlot_ = keep;
        holds_ = input.gripHolds;
    }
    if (input.gripHolds) {
        UpdateHolding(input, step);
        return step;
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

namespace {

// `strength` 0.5 is the designed level; 1 doubles it (amplitudes clamp at 1).
float Scaled(double strength, double level) {
    if (!(strength > 0.0)) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp(strength * 2.0 * level, 0.0, 1.0));
}

} // namespace

double Depth(double distance, double radius) {
    if (!(radius > 0.0) || !std::isfinite(distance) || distance < 0.0) {
        return 0.0;
    }
    return std::clamp(1.0 - distance / radius, 0.0, 1.0);
}

Buzz Contact(double depth, double strength, bool holding) {
    double const d = std::isfinite(depth) ? std::clamp(depth, 0.0, 1.0) : 0.0;
    Buzz buzz;
    // Faint at the edge (and in the hysteresis band), firm at the center; the frequency rises with it.
    buzz.amplitude = Scaled(strength, (0.08 + 0.27 * d) * (holding ? 0.5 : 1.0));
    buzz.frequency = static_cast<float>(70.0 + 130.0 * d);
    return buzz;
}

Buzz EnterEdge(double strength) {
    return {Scaled(strength, 0.55), 160.0f, 0.035f};
}

Buzz DrawClick(double strength) {
    return {Scaled(strength, 0.9), 320.0f, 0.02f};
}

Buzz HolsterClick(double strength) {
    return {Scaled(strength, 0.65), 200.0f, 0.02f};
}

Buzz Refused(double strength) {
    return {Scaled(strength, 0.3), 60.0f, 0.12f};
}

void StandingHeight::Update(double eyeHeight, double deltaSeconds) {
    if (!(eyeHeight >= kMin && eyeHeight <= kMax)) {
        return;  // no floor, or not this player's eyes above it
    }
    if (!(value_ > 0.0)) {
        value_ = eyeHeight;
    } else if (eyeHeight > value_) {
        value_ += (eyeHeight - value_) * (1.0 - std::exp(-std::max(deltaSeconds, 0.0) / kRiseSeconds));
    }
}

void StandingHeight::Recenter(double eyeHeight) {
    if (eyeHeight >= kMin && eyeHeight <= kMax) {
        value_ = eyeHeight;
    }
}

} // namespace mevr::ik::body_slots
