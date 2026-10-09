#pragma once

#include <openxr/openxr.h>

// A controller's aim ray against a head-locked menu quad (OpenXR conventions: meters, +Y up,
// -Z forward; a quad faces along its +Z). Engine-agnostic; unit-tested in tests/Main.cpp.
namespace mevr::vr::menu_pointer {

struct Quad {
    XrPosef head{};          // the VIEW space's pose in the tracking space (the head)
    XrVector3f center{};     // the quad's center in VIEW space (its orientation is VIEW's)
    float width = 0.0f;      // meters
    float height = 0.0f;
};

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
