// SPDX-License-Identifier: MIT
#include "core/vr/LayerComposite.hpp"

#include "core/vr/ImageConvert.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::composite {
namespace {

XrVector3f Add(XrVector3f const& a, XrVector3f const& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
XrVector3f Sub(XrVector3f const& a, XrVector3f const& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XrVector3f Scale(XrVector3f const& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(XrVector3f const& a, XrVector3f const& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
XrVector3f Cross(XrVector3f const& a, XrVector3f const& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

XrVector3f Rotate(XrQuaternionf const& q, XrVector3f const& v) {
    XrVector3f const u{q.x, q.y, q.z};
    XrVector3f const t = Scale(Cross(u, v), 2.0f);
    return Add(Add(v, Scale(t, q.w)), Cross(u, t));
}

XrQuaternionf Conjugate(XrQuaternionf const& q) { return {-q.x, -q.y, -q.z, q.w}; }

// The quad's frame, for many rays.
struct Plane {
    XrVector3f center;
    XrVector3f normal;  // +Z, the front
    XrVector3f right;   // +X
    XrVector3f up;      // +Y
    float width;
    float height;
};

Plane PlaneOf(Quad const& quad) {
    XrQuaternionf const& q = quad.pose.orientation;
    return {quad.pose.position, Rotate(q, {0.0f, 0.0f, 1.0f}), Rotate(q, {1.0f, 0.0f, 0.0f}), Rotate(q, {0.0f, 1.0f, 0.0f}),
        quad.width, quad.height};
}

bool Hit(Plane const& plane, XrVector3f const& origin, XrVector3f const& direction, float& u, float& v) {
    if (!(plane.width > 0.0f) || !(plane.height > 0.0f)) {
        return false;
    }
    float const denominator = Dot(direction, plane.normal);
    if (!(denominator < -1e-9f)) {
        return false;  // parallel, or reaching the back
    }
    XrVector3f const toCenter = Sub(plane.center, origin);
    float const t = Dot(toCenter, plane.normal) / denominator;
    if (!(t > 0.0f)) {
        return false;
    }
    XrVector3f const local = Sub(Scale(direction, t), toCenter);  // hit point minus the center
    u = Dot(local, plane.right) / plane.width + 0.5f;
    v = 0.5f - Dot(local, plane.up) / plane.height;
    return u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
}

} // namespace

XrVector3f EyeRay(XrFovf const& fov, float u, float v) {
    float const left = std::tan(fov.angleLeft);
    float const right = std::tan(fov.angleRight);
    float const up = std::tan(fov.angleUp);
    float const down = std::tan(fov.angleDown);
    return {left + (right - left) * u, up + (down - up) * v, -1.0f};
}

bool HitQuad(Quad const& quad, XrVector3f const& origin, XrVector3f const& direction, float& u, float& v) {
    return Hit(PlaneOf(quad), origin, direction, u, v);
}

std::size_t Draw(Image& target, Eye const& eye, Quad const& quad) {
    if (quad.image == nullptr || quad.image->width <= 0 || quad.image->height <= 0 || target.width <= 0 ||
        target.height <= 0 || target.pixels.size() < static_cast<size_t>(target.width) * target.height) {
        return 0;
    }
    Rect sub = quad.sub;
    if (sub.width <= 0 || sub.height <= 0) {
        sub = {0, 0, quad.image->width, quad.image->height};
    }
    sub.x = std::clamp(sub.x, 0, quad.image->width - 1);
    sub.y = std::clamp(sub.y, 0, quad.image->height - 1);
    sub.width = std::clamp(sub.width, 1, quad.image->width - sub.x);
    sub.height = std::clamp(sub.height, 1, quad.image->height - sub.y);

    Plane const plane = PlaneOf(quad);
    XrQuaternionf const& r = eye.pose.orientation;
    XrVector3f const origin = eye.pose.position;

    // The pixels the quad can cover: its corners projected into the image, if all are in front.
    int x0 = 0, y0 = 0, x1 = target.width, y1 = target.height;
    {
        float const tl = std::tan(eye.fov.angleLeft), tr = std::tan(eye.fov.angleRight);
        float const tu = std::tan(eye.fov.angleUp), td = std::tan(eye.fov.angleDown);
        float minU = 1e30f, maxU = -1e30f, minV = 1e30f, maxV = -1e30f;
        bool allInFront = (tr - tl) > 0.0f && (tu - td) > 0.0f;
        XrQuaternionf const inverse = Conjugate(r);
        for (int c = 0; c < 4 && allInFront; ++c) {
            float const sx = (c & 1) ? 0.5f : -0.5f;
            float const sy = (c & 2) ? 0.5f : -0.5f;
            XrVector3f const corner =
                Add(plane.center, Add(Scale(plane.right, sx * plane.width), Scale(plane.up, sy * plane.height)));
            XrVector3f const local = Rotate(inverse, Sub(corner, origin));
            if (!(local.z < -1e-4f)) {
                allInFront = false;
                break;
            }
            float const px = local.x / -local.z;
            float const py = local.y / -local.z;
            float const u = (px - tl) / (tr - tl);
            float const v = (tu - py) / (tu - td);
            minU = std::min(minU, u);
            maxU = std::max(maxU, u);
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
        }
        if (allInFront) {
            if (maxU < 0.0f || minU > 1.0f || maxV < 0.0f || minV > 1.0f) {
                return 0;
            }
            x0 = std::clamp(static_cast<int>(std::floor(minU * target.width)) - 1, 0, target.width);
            x1 = std::clamp(static_cast<int>(std::ceil(maxU * target.width)) + 1, 0, target.width);
            y0 = std::clamp(static_cast<int>(std::floor(minV * target.height)) - 1, 0, target.height);
            y1 = std::clamp(static_cast<int>(std::ceil(maxV * target.height)) + 1, 0, target.height);
        }
    }

    // The ray direction is linear in (u, v): corner plus steps, rotated into the quads' space once.
    XrVector3f const base = Rotate(r, EyeRay(eye.fov, 0.0f, 0.0f));
    XrVector3f const stepU = Sub(Rotate(r, EyeRay(eye.fov, 1.0f, 0.0f)), base);
    XrVector3f const stepV = Sub(Rotate(r, EyeRay(eye.fov, 0.0f, 1.0f)), base);
    float const invW = 1.0f / static_cast<float>(target.width);
    float const invH = 1.0f / static_cast<float>(target.height);
    std::size_t covered = 0;
    for (int y = y0; y < y1; ++y) {
        float const v = (static_cast<float>(y) + 0.5f) * invH;
        XrVector3f const row = Add(base, Scale(stepV, v));
        std::uint32_t* out = target.pixels.data() + static_cast<size_t>(y) * target.width;
        for (int x = x0; x < x1; ++x) {
            float const u = (static_cast<float>(x) + 0.5f) * invW;
            XrVector3f const direction = Add(row, Scale(stepU, u));
            float qu = 0.0f, qv = 0.0f;
            if (!Hit(plane, origin, direction, qu, qv)) {
                continue;
            }
            int const sx = sub.x + std::min(static_cast<int>(qu * sub.width), sub.width - 1);
            int const sy = sub.y + std::min(static_cast<int>(qv * sub.height), sub.height - 1);
            std::uint32_t source = quad.image->At(sx, sy);
            if (!quad.blend) {
                source |= 0xFF000000u;
            }
            out[x] = image::Over(out[x], source);
            ++covered;
        }
    }
    return covered;
}

} // namespace mevr::vr::composite
