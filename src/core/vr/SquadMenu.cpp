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

float WrapDegrees(float degrees) {
    while (degrees > 180.0f) {
        degrees -= 360.0f;
    }
    while (degrees <= -180.0f) {
        degrees += 360.0f;
    }
    return degrees;
}

// The stick angle each tile sits at: degrees clockwise from up.
float CenterOf(Direction direction) {
    switch (direction) {
    case Direction::Up: return 0.0f;
    case Direction::Right: return 90.0f;
    case Direction::Down: return 180.0f;
    case Direction::Left: return -90.0f;
    case Direction::None: break;
    }
    return 0.0f;
}

// The card side a left stick points to: -1 left, +1 right, 0 neither.
int SideOf(float x, float y, float push) {
    if (std::abs(x) < push || std::abs(x) < std::abs(y)) {
        return 0;
    }
    return x < 0.0f ? -1 : 1;
}

std::mutex gMutex;
View gLatest;
std::uint64_t gSequence = 0;

} // namespace

int WeaponAt(Direction direction) {
    switch (direction) {
    case Direction::Up: return 2;     // assault rifle
    case Direction::Left: return 1;   // shotgun
    case Direction::Right: return 3;  // sniper rifle
    case Direction::Down: return 0;   // pistol
    case Direction::None: break;
    }
    return -1;
}

Direction DirectionOf(int weapon) {
    switch (weapon) {
    case 0: return Direction::Down;
    case 1: return Direction::Left;
    case 2: return Direction::Up;
    case 3: return Direction::Right;
    default: return Direction::None;
    }
}

char const* WeaponName(int weapon) {
    switch (weapon) {
    case 0: return "pistol";
    case 1: return "shotgun";
    case 2: return "assault rifle";
    case 3: return "sniper rifle";
    default: return "none";
    }
}

char const* DirectionName(Direction direction) {
    switch (direction) {
    case Direction::Up: return "up";
    case Direction::Left: return "left";
    case Direction::Right: return "right";
    case Direction::Down: return "down";
    case Direction::None: break;
    }
    return "centre";
}

Direction Radial(float x, float y, Direction held, RadialSettings const& settings) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return Direction::None;
    }
    float const magnitude = std::sqrt(x * x + y * y);
    if (magnitude < (held != Direction::None ? settings.release : settings.deadZone)) {
        return Direction::None;
    }
    float const angle = std::atan2(x, y) / kDeg;  // clockwise from up
    if (held != Direction::None && std::abs(WrapDegrees(angle - CenterOf(held))) <= 45.0f + settings.hysteresisDeg) {
        return held;
    }
    Direction best = Direction::Up;
    float bestDiff = 1e9f;
    for (Direction d : {Direction::Up, Direction::Left, Direction::Right, Direction::Down}) {
        float const diff = std::abs(WrapDegrees(angle - CenterOf(d)));
        if (diff < bestDiff) {
            bestDiff = diff;
            best = d;
        }
    }
    return best;
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

char const* CloseName(Close close) {
    switch (close) {
    case Close::Confirmed: return "confirmed";
    case Close::Released: return "released with nothing chosen";
    case Close::CancelButton: return "cancelled (B)";
    case Close::Resqueezed: return "cancelled (grip squeezed again)";
    case Close::Unusable: return "closed (it can't stay open here)";
    case Close::Idle: return "closed (untouched)";
    case Close::None: break;
    }
    return "-";
}

void Machine::Reset() {
    *this = Machine{};
}

void Machine::OpenMenu(Input const& input, int member, Step& step) {
    phase_ = Phase::Open;
    selected_ = std::clamp(member, 0, std::max(input.memberCount - 1, 0));
    highlighted_ = -1;
    // A stick already pushed when it opens (walking, turning) chooses nothing until it moves on.
    rightHeld_ = Radial(input.rightX, input.rightY, Direction::None, input.radial);
    flickRefused_ = true;
    leftSide_ = SideOf(input.leftX, input.leftY, input.cardPush);
    idleFor_ = 0.0f;
    armedFor_ = 0.0f;
    step.opened = true;
}

void Machine::CloseMenu(Close why, Step& step) {
    if (why == Close::Confirmed) {
        step.member = selected_;
        step.weapon = highlighted_;
    }
    step.closed = why;
    phase_ = Phase::Idle;
    highlighted_ = -1;
    rightHeld_ = Direction::None;
    flickRefused_ = false;
    armedFor_ = 0.0f;
    idleFor_ = 0.0f;
}

Step Machine::Update(Input const& in) {
    Step step;
    bool const gripReleased = !in.gripDown && gripWasDown_;
    gripWasDown_ = in.gripDown;
    bool const aPressed = in.a && !aWasDown_;
    bool const bPressed = in.b && !bWasDown_;
    aWasDown_ = in.a;
    bWasDown_ = in.b;
    int const count = std::clamp(in.memberCount, 0, kMaxMembers);
    bool const pointed = in.pointed >= 0 && in.pointed < count;

    switch (phase_) {
    case Phase::Idle:
        if (!in.newAwaySqueeze || !in.usable) {
            break;
        }
        if (count == 0) {
            step.refused = true;
        } else if (pointed) {
            OpenMenu(in, in.pointed, step);
            step.openedPointing = true;
        } else if (in.nearGun) {
            step.missedGrab = true;
        } else {
            phase_ = Phase::Arming;
            armedFor_ = 0.0f;
            step.armed = true;
        }
        break;

    case Phase::Arming:
        if (!in.gripDown || !in.usable || count == 0) {
            phase_ = Phase::Idle;  // let go early: nothing
            armedFor_ = 0.0f;
            break;
        }
        if (pointed) {
            OpenMenu(in, in.pointed, step);
            step.openedPointing = true;
            break;
        }
        armedFor_ += std::max(in.dt, 0.0f);
        if (armedFor_ >= in.openHoldSeconds) {
            OpenMenu(in, in.preferred >= 0 && in.preferred < count ? in.preferred : 0, step);
        }
        break;

    case Phase::Open: {
        if (!in.usable || count == 0 || selected_ >= count) {
            CloseMenu(Close::Unusable, step);
            break;
        }
        // The card: the left stick pushed left or right (a new push, not one held since it opened).
        int const side = count > 1 ? SideOf(in.leftX, in.leftY, in.cardPush) : 0;
        if (side != leftSide_) {
            leftSide_ = side;
            int const want = side < 0 ? 0 : side > 0 ? 1 : selected_;
            if (want != selected_) {
                selected_ = want;
                step.cardChanged = true;
                if (highlighted_ >= 0 && (in.members[selected_].carried & (1u << highlighted_)) == 0) {
                    highlighted_ = -1;  // the new squadmate doesn't carry it
                    step.tileChanged = true;
                }
            }
        }
        // The tile: a flick of the right stick into a new sector; centred, the choice stays.
        Direction const d = Radial(in.rightX, in.rightY, rightHeld_, in.radial);
        if (d != rightHeld_) {
            rightHeld_ = d;
            flickRefused_ = false;
            if (d != Direction::None) {
                int const weapon = WeaponAt(d);
                if ((in.members[selected_].carried & (1u << weapon)) != 0) {
                    if (weapon != highlighted_) {
                        highlighted_ = weapon;
                        step.tileChanged = true;
                    }
                } else {
                    flickRefused_ = true;
                    step.tileRefused = true;
                }
            }
        }
        bool const touched = d != Direction::None || side != 0 || in.gripDown || aPressed || bPressed;
        idleFor_ = touched ? 0.0f : idleFor_ + std::max(in.dt, 0.0f);
        if (bPressed) {
            CloseMenu(Close::CancelButton, step);
        } else if (aPressed && highlighted_ >= 0) {
            CloseMenu(Close::Confirmed, step);
        } else if (gripReleased && in.confirmOnRelease) {
            CloseMenu(highlighted_ >= 0 ? Close::Confirmed : Close::Released, step);
        } else if (!in.confirmOnRelease && in.newAwaySqueeze) {
            CloseMenu(Close::Resqueezed, step);
        } else if (!in.confirmOnRelease && idleFor_ >= in.idleCloseSeconds) {
            CloseMenu(Close::Idle, step);
        }
        break;
    }
    }
    return step;
}

void Publish(View const& view) {
    std::lock_guard lock(gMutex);
    gLatest = view;
    gLatest.size = sizeof(View);
    gLatest.sequence = ++gSequence;
    gLatest.timeMs = GetTickCount64();
}

View Latest(std::uint64_t maxAgeMs) {
    View copy;
    {
        std::lock_guard lock(gMutex);
        copy = gLatest;
    }
    if (copy.timeMs == 0 || GetTickCount64() - copy.timeMs > maxAgeMs) {
        copy.open = false;
    }
    return copy;
}

void Reset() {
    std::lock_guard lock(gMutex);
    gLatest = View{};
}

} // namespace mevr::vr::squad_menu
