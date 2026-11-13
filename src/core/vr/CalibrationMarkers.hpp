// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/CalibrationWeapon.hpp"
#include "core/vr/Controllers.hpp"

namespace mevr::vr::calibration_markers {

// Original generic controller guide, not a device-accurate render model. Raw grip origin and forward are exact;
// the rounded shell is deliberately faint so it cannot be mistaken for a target Shepard's hand must fit.
inline std::vector<calibration_weapon::Face> Build(controllers::State const& raw, int selected) {
    namespace w = calibration_weapon;
    std::vector<w::Face> faces;
    if (!raw.active) return faces;
    constexpr float pi = 3.14159265359f;
    constexpr int n = 16;
    auto scale = [](XrVector3f p, float s) { return XrVector3f{p.x*s, p.y*s, p.z*s}; };
    auto cross = [](XrVector3f a, XrVector3f b) { return XrVector3f{a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; };
    auto length = [](XrVector3f p) { return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z); };
    for (int side : {0, 1}) {
        auto const& hand = side == 0 ? raw.left : raw.right;
        auto const& q = hand.grip.orientation;
        float const norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
        if (!hand.gripValid || !w::Finite(hand.grip.position) || !std::isfinite(norm) || norm < 0.001f) continue;
        std::uint32_t const rgb = side == selected ? 0x003F8FFFu : 0x00F2CB7Cu; // orange / cyan
        auto face = [&](std::array<XrVector3f,4> points, unsigned alpha) {
            for (auto& p : points) p = w::Add(hand.grip.position, calibration::Rotate(q, p));
            faces.push_back({points, rgb | (alpha << 24)});
        };
        // Crossed ribbons give thin strokes thickness from either eye without a dense tube mesh.
        auto line = [&](XrVector3f a, XrVector3f b, float radius = 0.00065f, unsigned alpha = 230) {
            auto const d = w::Sub(b,a);
            auto u = cross(d, std::abs(d.y) < length(d)*0.9f ? XrVector3f{0,1,0} : XrVector3f{1,0,0});
            if (length(u) < 0.000001f) return;
            u = scale(u, radius/length(u));
            auto v = cross(d,u);
            v = scale(v, radius/length(v));
            for (auto axis : {u,v}) face({w::Sub(a,axis), w::Add(a,axis), w::Add(b,axis), w::Sub(b,axis)}, alpha);
        };
        auto ring = [&](float y, float z, float rx, float rz, float x = 0.0f) {
            std::array<XrVector3f,n> points;
            for (int i = 0; i < n; ++i) {
                float const a = 2*pi*static_cast<float>(i)/n;
                points[i] = {x+rx*std::cos(a), y, z+rz*std::sin(a)};
            }
            return points;
        };
        std::array<std::array<XrVector3f,n>,5> const shell{{
            ring(-0.067f,0.012f,0.009f,0.01f), ring(-0.055f,0.012f,0.015f,0.017f),
            ring(0,0,0.019f,0.021f), ring(0.034f,-0.009f,0.027f,0.031f), ring(0.045f,-0.009f,0.025f,0.029f)}};
        for (std::size_t r = 0; r < shell.size(); ++r) {
            for (int i = 0; i < n; ++i) {
                int const j = (i+1)%n;
                if (r == 0 || r >= 3) line(shell[r][i],shell[r][j]);
                if (r > 0) {
                    face({shell[r-1][i],shell[r-1][j],shell[r][j],shell[r][i]}, 18);
                    if (i%4 == 0) line(shell[r-1][i],shell[r][i],0.00055f,160);
                }
            }
        }
        // Thumb stick and one button distinguish the top and the mirrored outward side.
        float const mirror = side == 0 ? -1.0f : 1.0f;
        for (int control = 0; control < 2; ++control) {
            float const x = mirror*(control == 0 ? -0.009f : 0.012f);
            float const y = control == 0 ? 0.052f : 0.047f;
            float const radius = control == 0 ? 0.007f : 0.004f;
            auto const top = ring(y,-0.013f,radius,radius,x);
            for (int i = 0; i < n; ++i) line(top[i],top[(i+1)%n],0.00055f);
            if (control == 0) line({x,0.045f,-0.013f},{x,y,-0.013f},0.001f);
        }
        // Curved trigger on the forward side.
        XrVector3f previous{-0.011f,0.021f,-0.035f};
        for (int i = 1; i <= 8; ++i) {
            float const a = pi*static_cast<float>(i)/8;
            XrVector3f const p{-0.011f*std::cos(a),0.021f-0.006f*std::sin(a),-0.035f-0.005f*std::sin(a)};
            line(previous,p,0.0008f);
            previous = p;
        }
        // Precise origin cross and a short forward arrow; no aim target or calibrated transform enters this guide.
        line({-0.006f,0,0},{0.006f,0,0},0.0008f,255);
        line({0,-0.006f,0},{0,0.006f,0},0.0008f,255);
        line({0,0,0},{0,0,-0.09f},0.0008f,255);
        line({0,0,-0.09f},{-0.007f,0,-0.078f},0.0008f,255);
        line({0,0,-0.09f},{0.007f,0,-0.078f},0.0008f,255);
    }
    return faces;
}

} // namespace mevr::vr::calibration_markers
