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
    out.backdropBrightness = Within(s.backdropBrightness, 0.0f, 3.0f, d.backdropBrightness);
    out.fadeS = Within(s.fadeS, 0.0f, 2.0f, d.fadeS);
    out.uiForwardM = Within(s.uiForwardM, 0.0f, kMaxUiForwardM, d.uiForwardM);
    if (!std::isfinite(s.aspect) || s.aspect < 0.0f) {
        out.aspect = d.aspect;
    } else if (s.aspect > 0.0f) {
        out.aspect = std::clamp(s.aspect, kMinAspect, kMaxAspect);
    }
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

EyeRects Rects(int width, int height, float crop, float aspect) {
    EyeRects r;
    if (width <= 0 || height <= 0) {
        return r;
    }
    float const c = std::isfinite(crop) ? std::clamp(crop, 0.0f, 0.25f) : 0.0f;
    int const drop = static_cast<int>(std::lround(c * static_cast<float>(width)));
    int const shown = width - drop;
    int y = 0;
    int rows = height;
    if (std::isfinite(aspect) && aspect > 0.0f) {
        int const band = static_cast<int>(std::lround(static_cast<float>(shown) / std::clamp(aspect, kMinAspect, kMaxAspect)));
        if (band > 0 && band < height) {
            y = (height - band) / 2;
            rows = band;
        }
    }
    r.left = {drop, y, shown, rows};
    r.right = {0, y, shown, rows};
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
    return crop * convergeUU * 2.0f * std::tan(std::min(halfH, kMaxLensHalfH));
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

UiPlace PlaceUi(float const screen[3], float const eyes[3], float forwardM) {
    UiPlace out;
    float to[3] = {eyes[0] - screen[0], eyes[1] - screen[1], eyes[2] - screen[2]};
    float const d = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
    float const f = std::isfinite(forwardM) ? std::clamp(forwardM, 0.0f, kMaxUiForwardM) : 0.0f;
    if (!std::isfinite(d) || d < f + 0.25f) {
        return out;
    }
    for (int i = 0; i < 3; ++i) {
        out.position[i] = screen[i] + to[i] / d * f;
    }
    out.scale = (d - f) / d;
    out.valid = true;
    return out;
}

UiPart UiPartShown(float crop, float v0, float v1) {
    UiPart part;
    float const c = std::isfinite(crop) ? std::clamp(crop, 0.0f, 0.25f) : 0.0f;
    part.u0 = 0.5f * c;
    part.u1 = 1.0f - 0.5f * c;
    if (std::isfinite(v0) && std::isfinite(v1) && v0 >= 0.0f && v1 <= 1.0f && v1 - v0 > 0.01f) {
        part.v0 = v0;
        part.v1 = v1;
    }
    return part;
}

} // namespace mevr::vr::cine_screen
