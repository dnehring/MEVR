// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

// A controller's aim ray against a menu quad (OpenXR conventions: meters, +Y up, -Z forward; a
// quad faces along its +Z). The quad may be head-locked (submitted in VIEW space) or fixed in the
// tracking space (world-locked panels); either way it is first brought into the tracking space,
// where the controllers are. Engine-agnostic; unit-tested in tests/Main.cpp.
namespace mevr::vr::menu_pointer {

struct Quad {
    XrPosef pose{};          // the quad's center and orientation in the tracking space
    float width = 0.0f;      // meters
    float height = 0.0f;
};

// A quad submitted at `poseInSpace` in a space whose pose in the tracking space is `space`: the
// head for a VIEW-space (head-locked) quad, identity for one in the tracking space itself.
Quad InTracking(XrPosef const& space, XrPosef const& poseInSpace, float width, float height);

// A head-locked quad: `center` in VIEW space, oriented like VIEW, with VIEW at `head`.
Quad HeadLocked(XrPosef const& head, XrVector3f const& center, float width, float height);

struct Hit {
    float u = 0.0f;         // 0 at the left edge, 1 at the right
    float v = 0.0f;         // 0 at the top edge, 1 at the bottom (texture rows)
    float distance = 0.0f;  // along the ray, meters
};

// Where the ray from `origin` along `direction` (tracking space, need not be unit length) meets
// the quad's front. False if it misses, points away, or comes from behind the quad.
bool Intersect(Quad const& quad, XrVector3f const& origin, XrVector3f const& direction, Hit& out);

// The aim pose's ray: forward is its -Z.
XrVector3f AimDirection(XrPosef const& aim);

} // namespace mevr::vr::menu_pointer
