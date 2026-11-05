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
        {"TK_Throw", "Throw", 0, 1},
        {"TK_Lift", "Lift", 0, 2},
        {"SD_Warp", "Warp", 0, 3},
        {"SD_Singularity", "Singularity", 0, 4},
        {"KF_Stasis", "Stasis", 0, 5},
        {"KF_Barrier", "Barrier", 0, 6},
        {"EL_EMP_Delivery", "Overload", 1, 7},
        {"EL_Damping_Delivery", "Damping", 1, 8},
        {"DC_Sabotage_Delivery", "Sabotage", 1, 9},
        {"DC_Hacking", "AI Hacking", 1, 10},
        {"MD_Shock", "Neural Shock", 1, 11},
        {"AR_ShieldBoost", "Shield Boost", 1, 12},
        {"FT_Adrenaline", "Adrenaline Burst", 2, 13},
        {"AT_Immunity", "Immunity", 2, 14},
        {"TC_TakeDown", "Unity", 2, 15},
        {"LW_Marksman", "Marksman", 2, 16},
        {"AW_Suppression", "Overkill", 2, 17},
        {"BW_CarnageSetup", "Carnage", 2, 18},
        {"SW_Critical", "Assassination", 2, 19},
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
    }
    return out;
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

}  // namespace mevr::vr::power_wheel
