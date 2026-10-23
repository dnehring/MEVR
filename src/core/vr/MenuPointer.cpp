// SPDX-License-Identifier: MIT
#include "core/vr/MenuPointer.hpp"

#include "core/vr/OverlayPlacement.hpp"

#include <cmath>

namespace mevr::vr::menu_pointer {
namespace {

XrQuaternionf Multiply(XrQuaternionf const& a, XrQuaternionf const& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

} // namespace

Quad InTracking(XrPosef const& space, XrPosef const& poseInSpace, float width, float height) {
    Quad quad;
    XrVector3f const offset = overlay::Rotate(space.orientation, poseInSpace.position);
    quad.pose.position = {space.position.x + offset.x, space.position.y + offset.y, space.position.z + offset.z};
    quad.pose.orientation = Multiply(space.orientation, poseInSpace.orientation);
    quad.width = width;
    quad.height = height;
    return quad;
}

Quad HeadLocked(XrPosef const& head, XrVector3f const& center, float width, float height) {
    XrPosef local{};
    local.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    local.position = center;
    return InTracking(head, local, width, height);
}

bool Intersect(Quad const& quad, XrVector3f const& origin, XrVector3f const& direction, Hit& out) {
    if (!(quad.width > 0.0f) || !(quad.height > 0.0f)) {
        return false;
    }
    // Into the quad's frame: the inverse of its rotation, about its center.
    XrQuaternionf const q = quad.pose.orientation;
    XrQuaternionf const inverse{-q.x, -q.y, -q.z, q.w};
    XrVector3f const rel{origin.x - quad.pose.position.x, origin.y - quad.pose.position.y,
        origin.z - quad.pose.position.z};
    XrVector3f const o = overlay::Rotate(inverse, rel);
    XrVector3f const d = overlay::Rotate(inverse, direction);
    float const length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (!(length > 1e-6f) || !std::isfinite(length)) {
        return false;
    }
    // The quad's front faces +Z (toward the eye): the ray must start in front and point at it.
    if (!(o.z > 0.0f) || !(d.z < -1e-6f)) {
        return false;
    }
    float const t = -o.z / d.z;
    float const x = o.x + d.x * t;
    float const y = o.y + d.y * t;
    float const u = x / quad.width + 0.5f;
    float const v = 0.5f - y / quad.height;
    if (!(u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f)) {
        return false;
    }
    out.u = u;
    out.v = v;
    out.distance = t * length;
    return true;
}

XrVector3f AimDirection(XrPosef const& aim) {
    return overlay::Rotate(aim.orientation, {0.0f, 0.0f, -1.0f});
}

} // namespace mevr::vr::menu_pointer
