#include "core/vr/MenuPointer.hpp"

#include "core/vr/OverlayPlacement.hpp"

#include <cmath>

namespace mevr::vr::menu_pointer {

bool Intersect(Quad const& quad, XrVector3f const& origin, XrVector3f const& direction, Hit& out) {
    if (!(quad.width > 0.0f) || !(quad.height > 0.0f)) {
        return false;
    }
    // Into VIEW space: the inverse head rotation of the offset from the head.
    XrQuaternionf const q = quad.head.orientation;
    XrQuaternionf const inverse{-q.x, -q.y, -q.z, q.w};
    XrVector3f const rel{origin.x - quad.head.position.x, origin.y - quad.head.position.y,
        origin.z - quad.head.position.z};
    XrVector3f const o = overlay::Rotate(inverse, rel);
    XrVector3f const d = overlay::Rotate(inverse, direction);
    float const length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (!(length > 1e-6f) || !std::isfinite(length)) {
        return false;
    }
    // The quad's front faces +Z (toward the eye): the ray must start in front and point at it.
    if (!(o.z > quad.center.z) || !(d.z < -1e-6f)) {
        return false;
    }
    float const t = (quad.center.z - o.z) / d.z;
    float const x = o.x + d.x * t - quad.center.x;
    float const y = o.y + d.y * t - quad.center.y;
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
