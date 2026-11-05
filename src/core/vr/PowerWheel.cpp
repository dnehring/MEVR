// SPDX-License-Identifier: MIT
#include "core/vr/PowerWheel.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mevr::vr::power_wheel {
namespace {

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

}  // namespace mevr::vr::power_wheel
