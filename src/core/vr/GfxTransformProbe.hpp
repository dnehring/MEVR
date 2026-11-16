// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cmath>

namespace mevr::vr::gfx_transform_probe {
enum class Result { Consistent, NotDisplayObject, NonFinite, OutOfRange, NonAffine, NonPlanar, Degenerate, PositionMismatch };
inline char const* Name(Result r) {
    switch (r) {
    case Result::Consistent: return "consistent-unverified";
    case Result::NotDisplayObject: return "GetPosition-failed";
    case Result::NonFinite: return "non-finite";
    case Result::OutOfRange: return "outside-diagnostic-bounds";
    case Result::NonAffine: return "not-affine";
    case Result::NonPlanar: return "not-2D";
    case Result::Degenerate: return "degenerate";
    case Result::PositionMismatch: return "translation-disagrees-with-GetPosition";
    }
    return "unknown";
}
// Diagnostic rejection only, never crop approval. GetDisplayMatrix has no success return:
// a live GFx wrapper can wrap a non-display object and leave a meaningless matrix behind.
inline Result Check(bool positioned, float x, float y, std::array<float, 16> const& m) {
    if (!positioned) return Result::NotDisplayObject;
    if (!std::isfinite(x) || !std::isfinite(y)) return Result::NonFinite;
    for (float v : m) if (!std::isfinite(v)) return Result::NonFinite;
    constexpr float bound = 1.0e7f; // generous diagnostic bounds, not HUD coordinate normalization
    if (std::abs(x) > bound || std::abs(y) > bound) return Result::OutOfRange;
    for (float v : m) if (std::abs(v) > bound) return Result::OutOfRange;
    auto closeEnough = [](float a, float b) { return std::abs(a - b) <= 0.0001f; };
    if (!closeEnough(m[3], 0) || !closeEnough(m[7], 0) || !closeEnough(m[11], 0) || !closeEnough(m[15], 1)) return Result::NonAffine;
    if (!closeEnough(m[2], 0) || !closeEnough(m[6], 0) || !closeEnough(m[8], 0) || !closeEnough(m[9], 0) ||
        !closeEnough(m[10], 1) || !closeEnough(m[14], 0)) return Result::NonPlanar;
    if (std::abs(m[0] * m[5] - m[1] * m[4]) < 1.0e-8f) return Result::Degenerate;
    // UE Object.Matrix row-vector translation. A different native convention is unverified,
    // not permission to repair/transplant elements and derive a crop.
    if (std::abs(m[12] - x) > 0.5f || std::abs(m[13] - y) > 0.5f) return Result::PositionMismatch;
    return Result::Consistent;
}
}
