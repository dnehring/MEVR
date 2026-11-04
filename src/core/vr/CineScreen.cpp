// SPDX-License-Identifier: MIT
#include "core/vr/CineScreen.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::cine_screen {
namespace {

float Within(float value, float low, float high, float fallback) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

} // namespace

Settings Clamped(Settings const& s) {
    Settings const d;
    Settings out = s;
    out.distanceM = Within(s.distanceM, kMinDistanceM, kMaxDistanceM, d.distanceM);
    out.widthM = Within(s.widthM, kMinWidthM, kMaxWidthM, d.widthM);
    out.depth = Within(s.depth, 0.0f, kMaxDepth, d.depth);
    out.convergeM = Within(s.convergeM, kMinConvergeM, kMaxConvergeM, d.convergeM);
    return out;
}

float MaxDepth(float widthM, float ipdM) {
    float const ipd = ipdM > 0.0f && std::isfinite(ipdM) ? ipdM : kDefaultIpdM;
    if (!(widthM > 0.0f) || !std::isfinite(widthM)) {
        return 0.0f;
    }
    return std::min(kMaxDepth, kDivergenceShare * ipd / widthM);
}

float CropFraction(float depth) {
    if (!(depth > 0.0f) || !std::isfinite(depth)) {
        return 0.0f;
    }
    return depth / (1.0f + depth);
}

EyeRects Rects(int width, int height, float crop) {
    EyeRects r;
    if (width <= 0 || height <= 0) {
        return r;
    }
    float const c = std::isfinite(crop) ? std::clamp(crop, 0.0f, 0.25f) : 0.0f;
    int const drop = static_cast<int>(std::lround(c * static_cast<float>(width)));
    int const shown = width - drop;
    r.left = {drop, 0, shown, height};
    r.right = {0, 0, shown, height};
    return r;
}

float HeightM(EyeRects const& rects, float widthM) {
    if (rects.left.width <= 0 || rects.left.height <= 0) {
        return 0.0f;
    }
    return widthM * static_cast<float>(rects.left.height) / static_cast<float>(rects.left.width);
}

float BaselineUU(float crop, float convergeUU, float halfH) {
    if (!(crop > 0.0f) || !(convergeUU > 0.0f) || !(halfH > 0.0f) || !(halfH < 1.5f) || !std::isfinite(crop) ||
        !std::isfinite(convergeUU)) {
        return 0.0f;
    }
    return crop * convergeUU * 2.0f * std::tan(halfH);
}

Frame Plan(Settings const& s, float ipdM, float unitsPerMeter, float halfH) {
    Settings const c = Clamped(s);
    Frame f;
    float const cap = MaxDepth(c.widthM, ipdM);
    f.depthCapped = c.depth > cap;
    f.depth = std::min(c.depth, cap);
    f.crop = CropFraction(f.depth);
    float const upm = unitsPerMeter > 0.0f && std::isfinite(unitsPerMeter) ? unitsPerMeter : 0.0f;
    f.baselineUU = BaselineUU(f.crop, c.convergeM * upm, halfH);
    return f;
}

} // namespace mevr::vr::cine_screen
