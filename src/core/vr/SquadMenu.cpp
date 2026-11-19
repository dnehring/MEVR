// SPDX-License-Identifier: MIT
#include "core/vr/SquadMenu.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace mevr::vr::squad_menu {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDeg = kPi / 180.0f;

XrVector3f Sub(XrVector3f const& a, XrVector3f const& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XrVector3f Add(XrVector3f const& a, XrVector3f const& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
XrVector3f Scale(XrVector3f const& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
float Dot(XrVector3f const& a, XrVector3f const& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
XrVector3f Cross(XrVector3f const& a, XrVector3f const& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Length(XrVector3f const& v) { return std::sqrt(Dot(v, v)); }
XrVector3f Normalize(XrVector3f const& v, XrVector3f const& fallback) {
    float const length = Length(v);
    return length > 1e-6f && std::isfinite(length) ? Scale(v, 1.0f / length) : fallback;
}

// The rotation whose axes are x, y, z (orthonormal, right-handed), as a quaternion.
XrQuaternionf FromAxes(XrVector3f const& x, XrVector3f const& y, XrVector3f const& z) {
    float const m00 = x.x, m01 = y.x, m02 = z.x;
    float const m10 = x.y, m11 = y.y, m12 = z.y;
    float const m20 = x.z, m21 = y.z, m22 = z.z;
    float const trace = m00 + m11 + m22;
    XrQuaternionf q{};
    if (trace > 0.0f) {
        float const s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float const s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        float const s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        float const s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    float const n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 0.0f ? XrQuaternionf{q.x / n, q.y / n, q.z / n, q.w / n} : XrQuaternionf{0.0f, 0.0f, 0.0f, 1.0f};
}

} // namespace

char const* WeaponName(int weapon) {
    switch (weapon) {
    case 0: return "pistol";
    case 1: return "shotgun";
    case 2: return "assault rifle";
    case 3: return "sniper rifle";
    default: return "none";
    }
}

float AngleToRay(XrVector3f const& origin, XrVector3f const& direction, XrVector3f const& point) {
    XrVector3f const to = Sub(point, origin);
    float const toLength = Length(to);
    float const dirLength = Length(direction);
    if (!(toLength > 1e-6f) || !(dirLength > 1e-6f) || !std::isfinite(toLength) || !std::isfinite(dirLength)) {
        return kPi;
    }
    float const c = std::clamp(Dot(to, direction) / (toLength * dirLength), -1.0f, 1.0f);
    return std::acos(c);
}

int PickPointed(XrVector3f const& origin, XrVector3f const& direction, Target const* targets, int count, float cone,
    float maxDistance) {
    int best = -1;
    float bestAngle = 0.0f;
    float bestDistance = 0.0f;
    for (int i = 0; i < count; ++i) {
        if (!targets[i].valid) {
            continue;
        }
        float const distance = Length(Sub(targets[i].position, origin));
        float const angle = AngleToRay(origin, direction, targets[i].position);
        if (!(angle <= cone) || !(distance <= maxDistance)) {
            continue;
        }
        bool const better = best < 0 || angle < bestAngle - kDeg ||
            (angle <= bestAngle + kDeg && distance < bestDistance);
        if (better) {
            best = i;
            bestAngle = angle;
            bestDistance = distance;
        }
    }
    return best;
}

int NearestToRay(XrVector3f const& origin, XrVector3f const& direction, Target const* targets, int count) {
    int best = -1;
    float bestAngle = 0.0f;
    for (int i = 0; i < count; ++i) {
        if (!targets[i].valid) {
            continue;
        }
        float const angle = AngleToRay(origin, direction, targets[i].position);
        if (best < 0 || angle < bestAngle) {
            best = i;
            bestAngle = angle;
        }
    }
    return best;
}

XrPosef PlaceAtHand(XrVector3f const& hand, XrVector3f const& head, float distance) {
    XrVector3f const up{0.0f, 1.0f, 0.0f};
    XrVector3f const toHand = Sub(hand, head);
    XrVector3f const out = Normalize({toHand.x, 0.0f, toHand.z}, {0.0f, 0.0f, -1.0f});
    // Mostly up, a little further out: |(0.86, 0.51)| = 1.
    XrVector3f const center = Add(hand, Add(Scale(up, 0.86f * distance), Scale(out, 0.51f * distance)));
    // Front (+Z) toward the eyes, +X level (no roll), +Y completing the frame.
    XrVector3f const z = Normalize(Sub(head, center), Scale(out, -1.0f));
    XrVector3f x = Cross(up, z);
    x = Normalize(x, {1.0f, 0.0f, 0.0f});
    XrVector3f const y = Cross(z, x);
    XrPosef pose{};
    pose.orientation = FromAxes(x, y, z);
    pose.position = center;
    return pose;
}

// ---- The game's own weapon wheel ---------------------------------------------------------------------
namespace wheel {
namespace {

// GUI_SF_HUD.ME_HUD's tables (InitializeHud, `new Array(...)`): the arrow angle (361 - nAngle) belongs to
// the first boundary above it; each boundary index names a slot and a member.
constexpr int kBoundaries[] = {1, 12, 51, 90, 129, 169, 193, 214, 238, 270, 302, 326, 347, 361};
constexpr int kSlotOf[] = {0, 1, 4, 3, 2, 1, 4, 3, 2, 1, 4, 3, 2, 1};
constexpr int kMemberOf[] = {-1, 2, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2};
constexpr int kBoundaryCount = static_cast<int>(sizeof(kBoundaries) / sizeof(kBoundaries[0]));

std::mutex gPanelMutex;
Panel gPanel{0};
std::uint64_t gPanelSequence = 0;

} // namespace

Entry EntryAtStickAngle(int nAngle) {
    if (nAngle < 0) {
        return {};
    }
    int const arrow = 361 - std::clamp(nAngle, 1, 360);  // the movie's UpdateHUDMode
    int i = 0;
    while (i < kBoundaryCount - 1 && arrow >= kBoundaries[i]) {
        ++i;
    }
    if (arrow >= kBoundaries[i] || kSlotOf[i] <= 0 || kMemberOf[i] < 0) {
        return {};
    }
    return {kMemberOf[i], kSlotOf[i]};
}

int StickAngleOf(Entry entry) {
    if (!entry.Valid() || entry.member >= kMembers || entry.slot > kSlots) {
        return -1;
    }
    if (entry.member == 2 && entry.slot == 1) {
        return 2;  // its arc wraps ([347, 361) and [1, 12), 25 degrees about 359.5): arrow 359
    }
    for (int i = 1; i < kBoundaryCount; ++i) {
        if (kMemberOf[i] == entry.member && kSlotOf[i] == entry.slot) {
            int const arrow = (kBoundaries[i - 1] + kBoundaries[i]) / 2;
            return std::clamp(361 - arrow, 0, 359);
        }
    }
    return -1;
}

int WeaponOfSlot(int slot) {
    switch (slot) {
    case 1: return 3;  // sniper rifle
    case 2: return 0;  // pistol
    case 3: return 2;  // assault rifle
    case 4: return 1;  // shotgun
    default: return -1;
    }
}

int SlotOfWeapon(int weapon) {
    switch (weapon) {
    case 3: return 1;
    case 0: return 2;
    case 2: return 3;
    case 1: return 4;
    default: return 0;
    }
}

char const* PhaseName(Phase phase) {
    switch (phase) {
    case Phase::Idle: return "idle";
    case Phase::Opening: return "opening";
    case Phase::Open: return "open";
    case Phase::Confirming: return "confirming";
    case Phase::Closing: return "closing";
    }
    return "?";
}

void Machine::BeginClose(Step& step, bool confirm, Input const& input) {
    if (confirm && input.highlightValid && input.highlightCarried && !input.choiceRecorded) {
        phase_ = Phase::Confirming;
        timer_ = 0.0f;
        step.holdA = true;
        step.confirmed = true;
        return;
    }
    if (!(input.highlightValid && input.choiceRecorded)) {
        step.cancelled = true;  // nothing queued now (an earlier A may have queued another entry)
    }
    phase_ = Phase::Closing;
    timer_ = 0.0f;
    exitTries_ = 1;
    step.exit = true;
}

Step Machine::Update(Input const& in) {
    Step step;
    float const dt = std::max(in.dt, 0.0f);
    bool const triggerReleased = !in.triggerDown && triggerWasDown_;
    triggerWasDown_ = in.triggerDown;
    bool const bPressed = in.b && !bWasDown_;
    bWasDown_ = in.b;

    switch (phase_) {
    case Phase::Idle:
        lateLeft_ = std::max(lateLeft_ - dt, 0.0f);
        // A weapon wheel up that we don't hold: our open that came late, or (a new pull) any wheel up with nobody
        // holding it. Taken over, so letting go of the trigger closes it and the game doesn't stay paused in it.
        if (in.wheelActive && in.usable && (lateLeft_ > 0.0f || in.newPull)) {
            phase_ = Phase::Open;
            timer_ = 0.0f;
            lateLeft_ = 0.0f;
            step.opened = true;
            step.adopted = true;
            bWasDown_ = true;  // a B held from before isn't a cancel
            if (!in.triggerDown) {
                BeginClose(step, false, in);  // the trigger's already up: shut it, give nothing
            }
            break;
        }
        // A new pull opens it at once (pointing only preselects).
        if (!in.newPull || !in.usable || !in.onFoot) {
            break;
        }
        phase_ = Phase::Opening;
        timer_ = 0.0f;
        releasedEarly_ = !in.triggerDown;
        step.enter = true;
        step.openedPointing = in.pointed;
        break;

    case Phase::Opening:
        timer_ += dt;
        releasedEarly_ = releasedEarly_ || !in.triggerDown;
        if (in.wheelActive) {
            phase_ = Phase::Open;
            timer_ = 0.0f;
            step.opened = true;
            bWasDown_ = true;  // a B held from before isn't a cancel
            if (releasedEarly_) {
                BeginClose(step, false, in);  // a quick pull-and-let-go: open and shut, give nothing
            }
        } else if (timer_ >= kOpenTimeout || !in.usable) {
            phase_ = Phase::Idle;
            step.openFailed = true;
            lateLeft_ = kLateOpenSeconds;  // EnterWeaponWheel may still take: adopt the wheel if it does
        }
        break;

    case Phase::Open:
        if (!in.wheelActive) {
            phase_ = Phase::Idle;
            step.closedByGame = true;
        } else if (bPressed) {
            step.cancelled = true;
            phase_ = Phase::Closing;
            timer_ = 0.0f;
            exitTries_ = 1;
            step.exit = true;
        } else if (triggerReleased || !in.triggerDown || !in.usable) {
            BeginClose(step, in.confirmOnRelease && in.usable, in);
        }
        break;

    case Phase::Confirming:
        if (!in.wheelActive) {
            phase_ = Phase::Idle;
            step.closedByGame = true;
            break;
        }
        timer_ += dt;
        if (timer_ < kPressASeconds) {
            step.holdA = true;
        } else if (in.choiceRecorded || timer_ >= kPressASeconds + kQueueTimeout) {
            phase_ = Phase::Closing;
            timer_ = 0.0f;
            exitTries_ = 1;
            step.exit = true;
        }
        break;

    case Phase::Closing:
        if (!in.wheelActive) {
            phase_ = Phase::Idle;
            step.closed = true;
            break;
        }
        timer_ += dt;
        if (timer_ >= kCloseTimeout) {
            if (exitTries_ < 2) {
                ++exitTries_;
                timer_ = 0.0f;
                step.exit = true;
            } else {
                phase_ = Phase::Idle;
                step.exitStuck = true;
            }
        }
        break;
    }
    return step;
}

void PublishPanel(Panel const& panel) {
    std::lock_guard lock(gPanelMutex);
    gPanel = panel;
    gPanel.size = sizeof(Panel);
    gPanel.sequence = ++gPanelSequence;
    gPanel.timeMs = GetTickCount64();
}

Panel LatestPanel(std::uint64_t maxAgeMs) {
    Panel copy{0};
    {
        std::lock_guard lock(gPanelMutex);
        copy = gPanel;
    }
    if (copy.timeMs == 0 || GetTickCount64() - copy.timeMs > maxAgeMs) {
        copy.size = 0;
        copy.modOpen = false;
    }
    return copy;
}

void ResetPanel() {
    std::lock_guard lock(gPanelMutex);
    gPanel = Panel{0};
}

} // namespace wheel

char const* RouteName(TriggerRoute route) {
    switch (route) {
    case TriggerRoute::OpensWheel: return "the squad opens the game's weapon wheel";
    case TriggerRoute::GameButton: break;
    }
    return "the game's own button";
}

TriggerRoute RouteTrigger(TriggerFacts const& f) {
    bool const squads = f.enabled && f.leftTriggerLB && f.opensWheel && f.modeTakesIt && !f.menuOpen && !f.powerWheel &&
        !f.supportGrip;
    return squads ? TriggerRoute::OpensWheel : TriggerRoute::GameButton;
}

bool OwnTrigger(TriggerOwner& owner, bool pulled, bool wanted) {
    if (!pulled || !owner.down) {
        owner.owned = wanted;  // up, or the press itself: decided now, then kept until it's let go
    }
    owner.down = pulled;
    return owner.owned;
}

} // namespace mevr::vr::squad_menu
