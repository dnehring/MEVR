// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Calibration.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mevr::vr::calibration_weapon {

// Original, sightless training pistol. No target or solved sight line is an input to the capture preview.
// Coordinates are meters: grip at origin, -Z along the barrel, +Y up. The orange grips are pickup cues.
struct Model {
    XrPosef pose{{0, 0, 0, 1}, {0, 0, 0}};
    bool rightHeld = false;
};
struct Face {
    std::array<XrVector3f, 4> points;
    std::uint32_t colour;
};
inline constexpr unsigned kPixels = 1024;
inline constexpr float kPlaneM = 1.0f;
inline constexpr float kHalfSizeM = 1.2f;
inline constexpr float kNearM = 0.12f;

inline XrVector3f Add(XrVector3f a, XrVector3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline XrVector3f Sub(XrVector3f a, XrVector3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline bool Finite(XrVector3f p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
inline std::uint32_t Shade(std::uint32_t rgb, float light) {
    auto channel = [&](unsigned shift) {
        return static_cast<std::uint32_t>(static_cast<float>((rgb >> shift) & 255u) * light);
    };
    return channel(0) | (channel(8) << 8) | (channel(16) << 16) | 0xFF000000u;
}

inline std::vector<Face> Build(Model const& model) {
    std::vector<Face> out;
    if (!Finite(model.pose.position)) return out;
    constexpr std::uint32_t shell = 0x00B5A89Cu; // cool grey, RGBA byte order
    constexpr std::uint32_t dark = 0x005E5146u;
    constexpr std::uint32_t orange = 0x003F8FFFu;
    constexpr std::uint32_t held = 0x00D7C075u;
    auto box = [&](XrVector3f low, XrVector3f high, std::uint32_t colour) {
        std::array<XrVector3f, 8> v;
        for (unsigned i = 0; i < v.size(); ++i) {
            XrVector3f p{(i & 1u) ? high.x : low.x, (i & 2u) ? high.y : low.y, (i & 4u) ? high.z : low.z};
            v[i] = Add(model.pose.position, calibration::Rotate(model.pose.orientation, p));
        }
        constexpr unsigned faces[6][4] = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};
        constexpr float light[6] = {0.65f, 0.82f, 0.45f, 1.0f, 0.58f, 0.72f};
        for (unsigned f = 0; f < 6; ++f) {
            out.push_back({{v[faces[f][0]], v[faces[f][1]], v[faces[f][2]], v[faces[f][3]]}, Shade(colour, light[f])});
        }
    };
    // The hand holds this grip, below the receiver. No raised sights, scope, barrel stripe or reticle.
    box({-0.022f, -0.065f, -0.024f}, {0.022f, 0.038f, 0.027f}, model.rightHeld ? held : orange);
    box({-0.033f, 0.038f, -0.19f}, {0.033f, 0.091f, 0.052f}, shell); // slide
    box({-0.026f, 0.009f, -0.16f}, {0.026f, 0.038f, -0.024f}, dark); // frame
    box({-0.019f, 0.047f, -0.218f}, {0.019f, 0.078f, -0.19f}, dark); // muzzle
    box({-0.016f, -0.021f, -0.092f}, {0.016f, -0.009f, -0.024f}, dark); // trigger guard bottom
    box({-0.016f, -0.009f, -0.092f}, {0.016f, 0.009f, -0.08f}, dark);
    return out;
}

// Render each eye separately into a transparent eye-only quad. The projection is the perspective of THAT eye
// through the quad, so hands and barrel have stereo depth instead of being a gun-shaped flat billboard.
inline XrPosef CanvasPose(XrPosef const& eye) {
    return {eye.orientation, Add(eye.position, calibration::Rotate(eye.orientation, {0, 0, -kPlaneM}))};
}
struct Polygon {
    std::vector<XrVector2f> points; // 0..1 on the canvas, Y downward
    float depth = 0;
    std::uint32_t colour = 0;
};
inline std::vector<Polygon> Project(std::vector<Face> const& faces, XrPosef const& eye) {
    std::vector<Polygon> out;
    if (!Finite(eye.position)) return out;
    auto const inverse = calibration::Conjugate(calibration::Normalize(eye.orientation));
    for (auto const& face : faces) {
        std::vector<XrVector3f> points;
        bool valid = true;
        for (auto p : face.points) {
            p = calibration::Rotate(inverse, Sub(p, eye.position));
            valid = valid && Finite(p);
            points.push_back(p);
        }
        if (!valid) continue;
        // Clip near and all four canvas edges before division; no giant triangles when the pistol crosses an eye.
        for (int plane = 0; plane < 5 && !points.empty(); ++plane) {
            auto distance = [&](XrVector3f p) {
                float const extent = -p.z * kHalfSizeM / kPlaneM;
                switch (plane) {
                case 0: return -p.z - kNearM;
                case 1: return extent + p.x;
                case 2: return extent - p.x;
                case 3: return extent + p.y;
                default: return extent - p.y;
                }
            };
            std::vector<XrVector3f> clipped;
            auto a = points.back();
            float da = distance(a);
            for (auto b : points) {
                float const db = distance(b);
                if ((da >= 0) != (db >= 0)) {
                    float const t = da / (da - db);
                    clipped.push_back({a.x + t * (b.x - a.x), a.y + t * (b.y - a.y), a.z + t * (b.z - a.z)});
                }
                if (db >= 0) clipped.push_back(b);
                a = b;
                da = db;
            }
            points = std::move(clipped);
        }
        if (points.size() < 3) continue;
        Polygon poly;
        poly.colour = face.colour;
        float nearest = 1000;
        for (auto p : points) {
            float const scale = kPlaneM / (-p.z * 2 * kHalfSizeM);
            poly.points.push_back({std::clamp(0.5f + p.x * scale, 0.0f, 1.0f), std::clamp(0.5f - p.y * scale, 0.0f, 1.0f)});
            poly.depth -= p.z / static_cast<float>(points.size());
            nearest = std::min(nearest, -p.z);
        }
        // Fade surfaces close to the eye so a clipped pistol does not cover the view.
        float const fade = std::clamp((nearest - kNearM) / 0.12f, 0.0f, 1.0f);
        auto const alpha = static_cast<std::uint32_t>(static_cast<float>(face.colour >> 24) * fade);
        if (alpha == 0) continue;
        poly.colour = (poly.colour & 0x00FFFFFFu) | (alpha << 24);
        out.push_back(std::move(poly));
    }
    std::stable_sort(out.begin(), out.end(), [](auto const& a, auto const& b) { return a.depth > b.depth; });
    return out;
}

} // namespace mevr::vr::calibration_weapon
