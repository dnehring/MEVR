// SPDX-License-Identifier: MIT
#include "core/vr/PowerWheel.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <numbers>

namespace mevr::vr::power_wheel {
namespace {

// ---- 3D helpers --------------------------------------------------------------------------------------------------------
XrVector3f Sub(XrVector3f a, XrVector3f b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
XrVector3f Cross(XrVector3f a, XrVector3f b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Length(XrVector3f a) {
    return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}
XrVector3f Normalized(XrVector3f a, XrVector3f fallback) {
    float const l = Length(a);
    return l > 1e-6f ? XrVector3f{a.x / l, a.y / l, a.z / l} : fallback;
}
// q v q*.
XrVector3f Rotate(XrQuaternionf q, XrVector3f v) {
    XrVector3f const u{q.x, q.y, q.z};
    XrVector3f const t = Cross(u, v);
    XrVector3f const t2{2.0f * t.x, 2.0f * t.y, 2.0f * t.z};
    XrVector3f const c = Cross(u, t2);
    return {v.x + q.w * t2.x + c.x, v.y + q.w * t2.y + c.y, v.z + q.w * t2.z + c.z};
}
// The rotation whose columns are the axes x, y, z.
XrQuaternionf FromAxes(XrVector3f x, XrVector3f y, XrVector3f z) {
    float const trace = x.x + y.y + z.z;
    XrQuaternionf q{};
    if (trace > 0.0f) {
        float const s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (y.z - z.y) / s;
        q.y = (z.x - x.z) / s;
        q.z = (x.y - y.x) / s;
    } else if (x.x > y.y && x.x > z.z) {
        float const s = std::sqrt(1.0f + x.x - y.y - z.z) * 2.0f;
        q.w = (y.z - z.y) / s;
        q.x = 0.25f * s;
        q.y = (y.x + x.y) / s;
        q.z = (z.x + x.z) / s;
    } else if (y.y > z.z) {
        float const s = std::sqrt(1.0f + y.y - x.x - z.z) * 2.0f;
        q.w = (z.x - x.z) / s;
        q.x = (y.x + x.y) / s;
        q.y = 0.25f * s;
        q.z = (z.y + y.z) / s;
    } else {
        float const s = std::sqrt(1.0f + z.z - x.x - y.y) * 2.0f;
        q.w = (x.y - y.x) / s;
        q.x = (z.x + x.z) / s;
        q.y = (z.y + y.z) / s;
        q.z = 0.25f * s;
    }
    return q;
}

std::mutex gLatestMutex;
View gLatest;
std::chrono::steady_clock::time_point gLatestAt{};
LockView gLatestLock;
std::chrono::steady_clock::time_point gLatestLockAt{};

constexpr float kRadPerDeg = std::numbers::pi_v<float> / 180.0f;

Vec2 Direction(float deg) {
    return {std::cos(deg * kRadPerDeg), std::sin(deg * kRadPerDeg)};
}

float Cross(Vec2 a, Vec2 b) {
    return a.x * b.y - a.y * b.x;
}

// How far along the ray from the origin in direction `d` it meets the line through p and q (the ray's own length if
// they're parallel: the caller then keeps its arc).
float RayToLine(Vec2 d, Vec2 p, Vec2 q, float fallback) {
    Vec2 const e{q.x - p.x, q.y - p.y};
    float const denominator = Cross(d, e);
    if (std::abs(denominator) < 1e-9f) {
        return fallback;
    }
    return Cross(p, e) / denominator;
}

float Normalized(float deg) {
    float d = std::fmod(deg, 360.0f);
    if (d <= -180.0f) {
        d += 360.0f;
    } else if (d > 180.0f) {
        d -= 360.0f;
    }
    return d;
}

}  // namespace

float AngleDiff(float a, float b) {
    return Normalized(a - b);
}

float StepDeg(int count, LayoutSettings const& settings) {
    if (count <= 1) {
        return 0.0f;
    }
    float step = (360.0f - settings.gapDeg) / static_cast<float>(count - 1);
    step = std::min(step, settings.maxStepDeg);
    step = std::max(step, settings.minStepDeg);
    if (step * static_cast<float>(count) > 360.0f) {
        step = 360.0f / static_cast<float>(count);  // a full ring, no gap left
    }
    return step;
}

std::vector<float> Angles(int count, LayoutSettings const& settings) {
    std::vector<float> out;
    if (count <= 0) {
        return out;
    }
    float const centre = settings.gapCentreDeg + 180.0f;
    float const step = StepDeg(count, settings);
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        out.push_back(Normalized(centre + step * (static_cast<float>(count - 1) * 0.5f - static_cast<float>(i))));
    }
    return out;
}

float HalfWidthDeg(int count, TileSettings const& tile, LayoutSettings const& layout, float spacingDeg) {
    float const step = StepDeg(count, layout);
    if (step <= 0.0f) {
        return tile.halfWidthDeg;
    }
    return std::max(1.0f, std::min(tile.halfWidthDeg, (step - spacingDeg) * 0.5f));
}

Strip TileStrip(float angleDeg, TileSettings const& tile) {
    float const hw = tile.halfWidthDeg;
    float const r = tile.outerRadius;
    float const rIn = tile.innerRadius;
    float const ci = tile.innerChamfer;
    // Where each cut starts on its edge (degrees from the tile's middle), and where it ends on the side (radius).
    float const outerKink = std::max(0.0f, hw - tile.outerChamfer / std::max(r, 1e-3f) / kRadPerDeg);
    float innerKink = std::max(0.0f, hw - ci / std::max(rIn, 1e-3f) / kRadPerDeg);
    float innerCorner = rIn;  // the inner edge's radius at the side
    if (tile.straightInner) {
        float const halfChord = rIn * std::tan(hw * kRadPerDeg);
        innerKink = std::atan2(std::max(halfChord - ci, 0.0f), rIn) / kRadPerDeg;
        innerCorner = rIn / std::cos(hw * kRadPerDeg);
    }

    std::vector<float> ts;
    int const n = std::max(tile.arcSegments, 2);
    for (int i = 0; i <= n; ++i) {
        ts.push_back(-hw + 2.0f * hw * static_cast<float>(i) / static_cast<float>(n));
    }
    for (float k : {outerKink, innerKink}) {
        ts.push_back(k);
        ts.push_back(-k);
    }
    std::sort(ts.begin(), ts.end());
    ts.erase(std::unique(ts.begin(), ts.end(), [](float a, float b) { return std::abs(a - b) < 1e-4f; }), ts.end());

    Strip strip;
    strip.outer.reserve(ts.size());
    strip.inner.reserve(ts.size());
    for (float t : ts) {
        float const side = t < 0.0f ? -1.0f : 1.0f;
        Vec2 const d = Direction(t);  // in the tile's own frame (its middle along +x)
        float outer = r;
        if (std::abs(t) > outerKink) {
            Vec2 const p = Direction(side * outerKink);
            Vec2 const q = Direction(side * hw);
            outer = RayToLine(d, {p.x * r, p.y * r}, {q.x * (r - tile.outerChamfer), q.y * (r - tile.outerChamfer)}, r);
        }
        float const edge = tile.straightInner ? rIn / std::cos(t * kRadPerDeg) : rIn;  // the inner edge on this ray
        float inner = edge;
        if (std::abs(t) > innerKink) {
            Vec2 const pd = Direction(side * innerKink);
            float const pr = tile.straightInner ? rIn / std::cos(innerKink * kRadPerDeg) : rIn;
            Vec2 const q = Direction(side * hw);
            inner = RayToLine(d, {pd.x * pr, pd.y * pr}, {q.x * (innerCorner + ci), q.y * (innerCorner + ci)}, edge);
        }
        Vec2 const w = Direction(angleDeg + t);
        strip.outer.push_back({w.x * outer, w.y * outer});
        strip.inner.push_back({w.x * inner, w.y * inner});
    }
    return strip;
}

std::vector<Vec2> Outline(Strip const& strip) {
    std::vector<Vec2> out(strip.outer.begin(), strip.outer.end());
    out.insert(out.end(), strip.inner.rbegin(), strip.inner.rend());
    return out;
}

Strip Transformed(Strip const& strip, float angleDeg, float middleRadius, float scale, float pushOut) {
    Vec2 const d = Direction(angleDeg);
    Vec2 const pivot{d.x * middleRadius, d.y * middleRadius};
    auto move = [&](Vec2 p) {
        return Vec2{pivot.x + (p.x - pivot.x) * scale + d.x * pushOut, pivot.y + (p.y - pivot.y) * scale + d.y * pushOut};
    };
    Strip out;
    for (Vec2 p : strip.outer) {
        out.outer.push_back(move(p));
    }
    for (Vec2 p : strip.inner) {
        out.inner.push_back(move(p));
    }
    return out;
}

int Pick(Vec2 offset, std::vector<float> const& angles, int current, PickSettings const& settings) {
    int const n = static_cast<int>(angles.size());
    if (n == 0) {
        return -1;
    }
    bool const valid = current >= 0 && current < n;
    if (std::hypot(offset.x, offset.y) < settings.deadZone) {
        return valid ? current : -1;
    }
    float const direction = std::atan2(offset.y, offset.x) / kRadPerDeg;
    int best = 0;
    float bestDiff = 1e9f;
    for (int i = 0; i < n; ++i) {
        float const diff = std::abs(AngleDiff(direction, angles[static_cast<std::size_t>(i)]));
        if (diff < bestDiff) {
            bestDiff = diff;
            best = i;
        }
    }
    if (valid && best != current) {
        float const currentDiff = std::abs(AngleDiff(direction, angles[static_cast<std::size_t>(current)]));
        if (currentDiff - bestDiff < settings.keepDeg) {
            return current;
        }
    }
    return best;
}

// ---- In the game -------------------------------------------------------------------------------------------------------

PowerInfo const* FindPower(char const* label) {
    // The icons' order is plugin/PowerIcons' Icon: 1 Throw .. 19 Assassination (0 the generic one).
    static constexpr PowerInfo kPowers[] = {
        {"TK_Throw", "Throw", 0, 1, Aim::Target},
        {"TK_Lift", "Lift", 0, 2, Aim::Target},
        {"SD_Warp", "Warp", 0, 3, Aim::Target},
        {"SD_Singularity", "Singularity", 0, 4, Aim::Place},
        {"KF_Stasis", "Stasis", 0, 5, Aim::Place},
        {"KF_Barrier", "Barrier", 0, 6, Aim::None},
        {"EL_EMP_Delivery", "Overload", 1, 7, Aim::Place},
        {"EL_Damping_Delivery", "Damping", 1, 8, Aim::Place},
        {"DC_Sabotage_Delivery", "Sabotage", 1, 9, Aim::Place},
        {"DC_Hacking", "AI Hacking", 1, 10, Aim::Target},
        {"MD_Shock", "Neural Shock", 1, 11, Aim::Target},
        {"AR_ShieldBoost", "Shield Boost", 1, 12, Aim::None},
        {"FT_Adrenaline", "Adrenaline Burst", 2, 13, Aim::None},
        {"AT_Immunity", "Immunity", 2, 14, Aim::None},
        {"TC_TakeDown", "Unity", 2, 15, Aim::None},
        {"LW_Marksman", "Marksman", 2, 16, Aim::None},
        {"AW_Suppression", "Overkill", 2, 17, Aim::None},
        {"BW_CarnageSetup", "Carnage", 2, 18, Aim::None},
        {"SW_Critical", "Assassination", 2, 19, Aim::None},
    };
    if (label == nullptr) {
        return nullptr;
    }
    for (PowerInfo const& power : kPowers) {
        if (std::strcmp(power.label, label) == 0) {
            return &power;
        }
    }
    return nullptr;
}

void Publish(View const& view) {
    std::lock_guard lock(gLatestMutex);
    gLatest = view;
    gLatestAt = std::chrono::steady_clock::now();
}

View Latest(std::uint64_t maxAgeMs) {
    std::lock_guard lock(gLatestMutex);
    View out = gLatest;
    auto const age = std::chrono::steady_clock::now() - gLatestAt;
    if (gLatestAt == std::chrono::steady_clock::time_point{} || age > std::chrono::milliseconds(maxAgeMs)) {
        out.open = 0;
        out.coach = 0;  // the coach card goes when the mod stops sending it
        out.self = {};
    }
    return out;
}

void PublishLock(LockView const& view) {
    std::lock_guard lock(gLatestMutex);
    gLatestLock = view;
    gLatestLockAt = std::chrono::steady_clock::now();
}

LockView LatestLock(std::uint64_t maxAgeMs) {
    std::lock_guard lock(gLatestMutex);
    LockView out = gLatestLock;
    auto const age = std::chrono::steady_clock::now() - gLatestLockAt;
    if (gLatestLockAt == std::chrono::steady_clock::time_point{} || age > std::chrono::milliseconds(maxAgeMs)) {
        out.active = 0;
    }
    return out;
}

std::uint64_t UpdateLock(LockState& state, std::vector<LockCandidate> const& candidates, double time, LockSettings const& settings) {
    std::uint64_t const previous = state.target;
    double const previousSince = state.since;
    auto pointed = [&](LockCandidate const& c) {
        return c.visible && c.angleDeg <= settings.acquireDeg && c.distanceM <= settings.rangeM;
    };
    // The current target, while it stays in its cone and range and in sight (or blocked less than graceS).
    if (state.target != 0) {
        LockCandidate const* current = nullptr;
        bool otherPointed = false;
        for (LockCandidate const& c : candidates) {
            if (c.id == state.target) {
                current = &c;
            } else if (pointed(c)) {
                otherPointed = true;
            }
        }
        bool keep = current != nullptr && current->distanceM <= settings.rangeM;
        if (keep && state.automatic) {
            keep = settings.autoLock && !otherPointed && !pointed(*current) &&
                   (current->angleDeg <= settings.autoHandDeg + kAutoKeepMarginDeg || current->gazeDeg <= settings.autoGazeDeg + kAutoKeepMarginDeg);
            if (current != nullptr && pointed(*current)) {
                state.automatic = false;  // now pointed at: a pointed lock from here on, the same target
                keep = true;
            }
        } else if (keep) {
            keep = current->angleDeg <= settings.keepDeg;
        }
        if (keep && !current->visible) {
            if (state.blockedSince < 0.0) {
                state.blockedSince = time;
            }
            keep = time - state.blockedSince <= settings.graceS;
        } else if (keep) {
            state.blockedSince = -1.0;
        }
        if (keep) {
            return state.target;
        }
        state.target = 0;
        state.blockedSince = -1.0;
    }
    // The best one pointed at.
    float bestScore = 1e9f;
    state.automatic = false;
    for (LockCandidate const& c : candidates) {
        if (!pointed(c)) {
            continue;
        }
        float const score = c.angleDeg + 0.05f * c.distanceM;
        if (score < bestScore) {
            bestScore = score;
            state.target = c.id;
        }
    }
    // Nothing pointed at: the automatic resolver.
    if (state.target == 0 && settings.autoLock) {
        for (LockCandidate const& c : candidates) {
            if (!c.visible || c.distanceM > settings.rangeM || (c.angleDeg > settings.autoHandDeg && c.gazeDeg > settings.autoGazeDeg)) {
                continue;
            }
            float const score = c.angleDeg + c.gazeDeg + 0.1f * c.distanceM;
            if (score < bestScore) {
                bestScore = score;
                state.target = c.id;
                state.automatic = true;
            }
        }
    }
    if (state.target != 0) {
        state.since = state.target == previous ? previousSince : time;
        state.blockedSince = -1.0;
    }
    return state.target;
}

bool StepPush(Push& push, double time, XrVector3f hand, XrVector3f forward, LockSettings const& settings, bool canFire) {
    constexpr double kWindowS = 0.12;
    constexpr int kMax = 16;
    // Keep the samples of the last window (and at most kMax), newest last.
    if (push.count == kMax) {
        for (int i = 1; i < kMax; ++i) {
            push.times[i - 1] = push.times[i];
            push.hands[i - 1] = push.hands[i];
        }
        --push.count;
    }
    push.times[push.count] = time;
    push.hands[push.count] = hand;
    ++push.count;
    int first = 0;
    while (first < push.count - 1 && time - push.times[first] > kWindowS) {
        ++first;
    }
    if (first > 0) {
        for (int i = first; i < push.count; ++i) {
            push.times[i - first] = push.times[i];
            push.hands[i - first] = push.hands[i];
        }
        push.count -= first;
    }
    XrVector3f const f = Normalized(forward, {0.0f, 0.0f, -1.0f});
    double const dt = time - push.times[0];
    if (push.count < 2 || dt < 0.03) {
        return false;
    }
    XrVector3f const moved = Sub(hand, push.hands[0]);
    float const travel = moved.x * f.x + moved.y * f.y + moved.z * f.z;
    float const speed = static_cast<float>(travel / dt);
    if (!push.armed) {
        if (speed < settings.pushSpeedMS * 0.4f) {
            push.armed = true;
        }
        return false;
    }
    if (speed >= settings.pushSpeedMS && travel >= settings.pushTravelM && time - push.lastFire >= settings.cooldownS) {
        if (!canFire) {
            push.held = true;  // not spent: the same push casts once it may
            return false;
        }
        push.armed = false;
        push.held = false;
        push.lastFire = time;
        return true;
    }
    if (push.held && speed < settings.pushSpeedMS * 0.4f) {
        push.held = false;  // it slowed down without ever being allowed: a miss
        ++push.heldMissed;
    }
    return false;
}

XrPosef Place(XrVector3f palm, XrVector3f eye, float towardM) {
    XrVector3f const z = Normalized(Sub(eye, palm), {0.0f, 0.0f, 1.0f});  // toward the eye
    XrVector3f x = Cross({0.0f, 1.0f, 0.0f}, z);                         // right, level
    if (Length(x) < 1e-3f) {
        x = {1.0f, 0.0f, 0.0f};  // looking straight down or up at it
    }
    x = Normalized(x, {1.0f, 0.0f, 0.0f});
    XrVector3f const y = Cross(z, x);
    XrPosef pose;
    pose.orientation = FromAxes(x, y, z);
    pose.position = {palm.x + z.x * towardM, palm.y + z.y * towardM, palm.z + z.z * towardM};
    return pose;
}

Vec2 InPlane(XrPosef const& wheel, XrVector3f point) {
    XrQuaternionf const inverse{-wheel.orientation.x, -wheel.orientation.y, -wheel.orientation.z, wheel.orientation.w};
    XrVector3f const local = Rotate(inverse, Sub(point, wheel.position));
    return {local.x, local.y};
}

XrVector3f ToFrame(XrPosef const& frame, XrVector3f point) {
    XrQuaternionf const inverse{-frame.orientation.x, -frame.orientation.y, -frame.orientation.z, frame.orientation.w};
    return Rotate(inverse, Sub(point, frame.position));
}

XrVector3f FromFrame(XrPosef const& frame, XrVector3f local) {
    XrVector3f const r = Rotate(frame.orientation, local);
    return {frame.position.x + r.x, frame.position.y + r.y, frame.position.z + r.z};
}

Vec2 SeenInPlane(XrPosef const& wheel, XrVector3f eye, XrVector3f source) {
    XrVector3f const normal = Rotate(wheel.orientation, {0.0f, 0.0f, 1.0f});
    XrVector3f const d = Sub(source, eye);
    float const along = d.x * normal.x + d.y * normal.y + d.z * normal.z;
    float const length = Length(d);
    if (length > 1e-6f && std::abs(along) > length * 0.0872f) {  // sin 5 degrees
        XrVector3f const toCentre = Sub(wheel.position, eye);
        float const t = (toCentre.x * normal.x + toCentre.y * normal.y + toCentre.z * normal.z) / along;
        if (t > 0.0f) {
            return InPlane(wheel, {eye.x + d.x * t, eye.y + d.y * t, eye.z + d.z * t});
        }
    }
    return InPlane(wheel, source);
}

char const* Name(Event event) {
    switch (event) {
    case Event::None: return "none";
    case Event::Opened: return "opened";
    case Event::Equipped: return "equipped";
    case Event::Cancelled: return "cancelled";
    }
    return "?";
}

Event Step(Machine& m, MachineInput const& in, std::vector<float> const& angles, Settings const& settings) {
    bool const pressed = in.button && !m.buttonWas;
    bool const released = !in.button && m.buttonWas;
    m.buttonWas = in.button;
    // The left trigger's pinch and the grip's squeeze, latched (a half-pressed one doesn't chatter).
    bool const triggerWas = m.triggerDown;
    bool const gripWas = m.gripDown;
    m.triggerDown = m.triggerDown ? in.trigger > 0.35f : in.trigger > 0.6f;
    m.gripDown = m.gripDown ? in.grip > 0.35f : in.grip > 0.6f;
    bool const pinch = m.triggerDown && !triggerWas && m.triggerArmed;
    bool const squeeze = m.gripDown && !gripWas && m.gripArmed;
    if (!m.triggerDown) {
        m.triggerArmed = true;
    }
    if (!m.gripDown) {
        m.gripArmed = true;
    }
    auto close = [&m](Event event) {
        m.open = false;
        m.toggled = false;
        return event;
    };

    if (!m.open) {
        if (pressed && in.allowed && !angles.empty()) {
            m.open = true;
            m.toggled = false;
            m.openedAt = in.time;
            m.selected = -1;
            m.selectedAt = in.time;
            m.stickPicked = false;
            m.triggerArmed = !m.triggerDown;  // one already held when it opened confirms nothing
            m.gripArmed = !m.gripDown;
            ++m.serial;
            return Event::Opened;
        }
        return Event::None;
    }
    if (!in.allowed || angles.empty()) {
        return close(Event::Cancelled);
    }
    if (m.selected >= static_cast<int>(angles.size())) {
        m.selected = -1;
    }

    // The pick: the stick's direction while it's pushed, else the hand's (once it moves on from where the stick left it).
    int pick = m.selected;
    PickSettings const byStick{0.0f, settings.keepDeg};
    PickSettings const byHand{settings.deadZoneM, settings.keepDeg};
    if (std::hypot(in.stickX, in.stickY) > settings.stickPick) {
        pick = Pick({in.stickX, in.stickY}, angles, m.selected, byStick);
        m.stickPicked = true;
        m.stickHand = in.hand;
    } else if (in.handValid) {
        bool const moved = std::hypot(in.hand.x - m.stickHand.x, in.hand.y - m.stickHand.y) > settings.deadZoneM;
        if (!m.stickPicked || moved) {
            m.stickPicked = false;
            pick = Pick(in.hand, angles, m.selected, byHand);
        }
    }
    if (pick != m.selected) {
        m.selected = pick;
        m.selectedAt = in.time;
    }

    auto equip = [&]() {
        if (m.selected >= 0) {
            m.equipped = m.selected;
            return close(Event::Equipped);
        }
        return close(Event::Cancelled);
    };
    if ((pinch || squeeze) && m.selected >= 0) {
        return equip();
    }
    if (released && !m.toggled) {
        if (in.time - m.openedAt < settings.tapS) {
            m.toggled = true;  // a tap: it stays open
            return Event::None;
        }
        return equip();
    }
    if (pressed && m.toggled) {
        return equip();
    }
    return Event::None;
}

SelfGesture SelfGestureFor(char const* label) {
    if (label == nullptr) {
        return SelfGesture::None;
    }
    static constexpr struct {
        char const* label;
        SelfGesture gesture;
    } kSelf[] = {
        {"KF_Barrier", SelfGesture::Chest},      {"AR_ShieldBoost", SelfGesture::Chest}, {"AT_Immunity", SelfGesture::Chest},
        {"FT_Adrenaline", SelfGesture::Chest},   {"LW_Marksman", SelfGesture::Gun},      {"AW_Suppression", SelfGesture::Gun},
        {"BW_CarnageSetup", SelfGesture::Gun},   {"SW_Critical", SelfGesture::Gun},      {"TC_TakeDown", SelfGesture::Raise},
    };
    for (auto const& s : kSelf) {
        if (std::strcmp(s.label, label) == 0) {
            return s.gesture;
        }
    }
    return SelfGesture::None;
}

char const* Name(SelfGesture gesture) {
    switch (gesture) {
    case SelfGesture::Chest: return "the open hand on the chest";
    case SelfGesture::Gun: return "the open hand slapped onto the gun";
    case SelfGesture::Raise: return "the open hand raised above the head";
    case SelfGesture::None: break;
    }
    return "none";
}

SelfRefusal RefusalFor(SelfGesture gesture, bool gunDrawn, bool available, float cooldown) {
    if (gesture == SelfGesture::Gun && !gunDrawn) {
        return SelfRefusal::DrawWeapon;
    }
    if (cooldown > 0.0f) {
        return SelfRefusal::CoolingDown;
    }
    return available ? SelfRefusal::None : SelfRefusal::Unavailable;
}

SelfZone GunZone(XrVector3f grip, XrVector3f muzzle, XrVector3f up, SelfCastSettings const& settings) {
    up = Normalized(up, {0.0f, 1.0f, 0.0f});
    auto lift = [&](XrVector3f p) {
        return XrVector3f{p.x + up.x * settings.gunUpM, p.y + up.y * settings.gunUpM, p.z + up.z * settings.gunUpM};
    };
    return {lift(grip), lift(muzzle), settings.gunRadiusM};
}

XrVector3f ClosestPoint(SelfZone const& zone, XrVector3f hand) {
    XrVector3f const axis = Sub(zone.end, zone.start);
    XrVector3f const rel = Sub(hand, zone.start);
    float const length2 = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
    float const t = length2 > 1e-8f ? std::clamp((rel.x * axis.x + rel.y * axis.y + rel.z * axis.z) / length2, 0.0f, 1.0f) : 0.0f;
    return {zone.start.x + t * axis.x, zone.start.y + t * axis.y, zone.start.z + t * axis.z};
}

SelfZone ZoneFor(SelfGesture gesture, SelfCastInput const& in, SelfCastSettings const& settings) {
    if (gesture == SelfGesture::Gun) {
        if (in.gunValid) {
            return in.gunZone;
        }
        XrVector3f const ahead = Rotate(in.gun.orientation, {0.0f, 0.0f, -0.35f});
        return GunZone(in.gun.position, {in.gun.position.x + ahead.x, in.gun.position.y + ahead.y, in.gun.position.z + ahead.z},
            Rotate(in.gun.orientation, {0.0f, 1.0f, 0.0f}), settings);
    }
    XrVector3f f = Rotate(in.head.orientation, {0.0f, 0.0f, -1.0f});
    f.y = 0.0f;
    f = Normalized(f, {0.0f, 0.0f, -1.0f});
    XrVector3f point{in.head.position.x + f.x * settings.chestAheadM, in.head.position.y - settings.chestBelowM,
        in.head.position.z + f.z * settings.chestAheadM};
    if (gesture == SelfGesture::Raise) {
        point = {in.hand.x, in.head.position.y + settings.raiseAboveM, in.hand.z};
    }
    return {point, point, gesture == SelfGesture::Chest ? settings.chestRadiusM : 0.0f};
}

SelfLook LookFor(SelfGesture gesture, SelfCastInput const& in, SelfZone const& zone) {
    if (!in.tracked || gesture == SelfGesture::None || (gesture == SelfGesture::Gun && !in.gunValid)) {
        return SelfLook::Hidden;
    }
    float const distance = Length(Sub(in.hand, ClosestPoint(zone, in.hand)));
    bool const inside = gesture == SelfGesture::Raise ? in.hand.y >= zone.start.y : distance <= zone.radiusM;
    if (in.open && inside) {
        return SelfLook::InRange;
    }
    if (distance <= zone.radiusM + 0.15f) {
        return SelfLook::Near;
    }
    return gesture == SelfGesture::Gun || distance <= 0.30f ? SelfLook::Faint : SelfLook::Hidden;
}

SelfCastStep StepSelfCast(SelfCastState& s, SelfGesture gesture, SelfCastInput const& in, SelfCastSettings const& settings) {
    SelfCastStep step;
    SelfZone const zone = ZoneFor(gesture, in, settings);
    XrVector3f const point = ClosestPoint(zone, in.hand);
    float const distance = Length(Sub(in.hand, point));
    bool const valid = gesture != SelfGesture::None && in.tracked &&
        (gesture != SelfGesture::Gun || in.gunValid || in.gunProbe);
    bool const inside = valid && (gesture == SelfGesture::Raise ? in.hand.y >= point.y : distance <= zone.radiusM);
    bool const close = valid && (gesture == SelfGesture::Raise ? in.hand.y >= point.y - 0.30f : distance <= zone.radiusM + 0.30f);
    if (!close) {
        if (s.reaching && !s.reported) {
            step.missed = true;
            step.attempt = s.attempt;
        }
        s.reaching = false;
        s.reported = false;
    } else {
        if (!s.reaching) {
            s.reaching = true;
            s.attempt = {};
        }
        if (distance < s.attempt.closestM) {
            s.attempt.closestM = distance;
            s.attempt.open = in.open;
            s.attempt.gunDrawn = in.gunDrawn || in.gunValid;
        }
    }
    float approach = 0.0f;
    if (s.hasLast && in.time > s.lastTime && in.time - s.lastTime <= 0.15) {
        // Same-frame geometry: gun motion onto a still hand cannot be a slap.
        float const previous = Length(Sub(s.last, ClosestPoint(zone, s.last)));
        approach = (previous - distance) / static_cast<float>(in.time - s.lastTime);
    }
    s.last = in.hand;
    s.lastTime = in.time;
    s.hasLast = valid;
    if (!inside) {
        s.inZone = false;
        s.spent = false;
        s.closedInStay = false;
        return step;
    }
    if (!s.inZone) {
        s.inZone = true;
        s.since = in.time;
        step.entrySpeedMS = approach;
        s.attempt.entrySpeedMS = approach;
        step.entered = in.open;
    }
    // A hand closed in the zone (the support grip on the foregrip, a grenade at the chest) must leave it before the gesture
    // counts: opening it in place never casts (else letting go of the foregrip cast Overkill a third of a second later).
    if (!in.open) {
        s.since = in.time;
        s.closedInStay = true;
        return step;
    }
    if (s.spent) {
        step.progress = 1.0f;
        return step;
    }
    if (s.closedInStay) {
        return step;
    }
    float const held = static_cast<float>(in.time - s.since);
    step.progress = settings.dwellS > 0.0f ? std::clamp(held / settings.dwellS, 0.0f, 1.0f) : 1.0f;
    bool const slap = gesture == SelfGesture::Gun && step.entered && approach >= settings.slapSpeedMS;
    // With no gun drawn only a real slap is answered ("Draw your weapon"): a resting or slow hand near the right controller (the
    // omni-tool's wrist tap, hands held together) says nothing.
    if (gesture == SelfGesture::Gun && !in.gunValid && !slap) {
        step.entered = false;  // no tick, no ring
        step.progress = 0.0f;
        return step;
    }
    if (slap || held >= settings.dwellS) {
        s.spent = true;
        s.reported = true;
        step.weaponMissing = gesture == SelfGesture::Gun && !in.gunValid;
        step.cast = !step.weaponMissing;
        step.progress = 1.0f;
        s.attempt.open = in.open;
        s.attempt.gunDrawn = in.gunDrawn || in.gunValid;
        step.attempt = s.attempt;
    }
    return step;
}

void BeginConfirmation(ConfirmationState& state, Confirmation kind, double time) {
    state = {kind, time, false};
}

ConfirmationStep StepConfirmation(ConfirmationState& state, double time) {
    ConfirmationStep out;
    if (state.kind == Confirmation::None) {
        return out;
    }
    out.age = static_cast<float>(time - state.since);
    out.visible = out.age >= 0.0f && out.age < kConfirmationS;
    bool const success = state.kind == Confirmation::Success;
    float const delay = success ? 0.16f : 0.22f;
    if (!state.secondBeat && out.age >= delay) {
        state.secondBeat = true;
        out.beat = out.visible;
    }
    out.seconds = success ? 0.055f : 0.085f;
    out.strength = success ? 2.2f : 1.4f;
    return out;
}

Coach CoachFor(Aim aim, SelfGesture gesture) {
    switch (gesture) {
    case SelfGesture::Chest: return Coach::Chest;
    case SelfGesture::Gun: return Coach::Gun;
    case SelfGesture::Raise: return Coach::Raise;
    case SelfGesture::None: break;
    }
    switch (aim) {
    case Aim::Target: return Coach::Target;
    case Aim::Place: return Coach::Place;
    case Aim::None: break;
    }
    return Coach::Forward;
}

CoachText CoachLines(Coach coach) {
    switch (coach) {
    case Coach::Target: return {"Raise your left hand at an enemy until it locks on,", "then push your open hand toward it."};
    case Coach::Place: return {"Point your left hand at an enemy or a spot,", "then push your open hand toward it."};
    case Coach::Forward: return {"Push your open left hand forward to cast it.", "Your right hand can keep the gun."};
    case Coach::Chest: return {"Hold your open left hand flat on your chest", "for a moment to cast it."};
    case Coach::Gun: return {"Slap your open left hand onto the top", "of the gun in your right hand."};
    case Coach::Raise: return {"Raise your open left hand above your head", "and hold it there for a moment."};
    case Coach::None: break;
    }
    return {"", ""};
}

char const* Name(Coach coach) {
    switch (coach) {
    case Coach::Target: return "target";
    case Coach::Place: return "place";
    case Coach::Forward: return "forward";
    case Coach::Chest: return "chest";
    case Coach::Gun: return "gun";
    case Coach::Raise: return "raise";
    case Coach::None: break;
    }
    return "none";
}

}  // namespace mevr::vr::power_wheel
