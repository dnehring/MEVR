#pragma once

#include <openxr/openxr.h>

// Where the renderer's own quads go in tracking space (OpenXR conventions: meters, +Y up, -Z
// forward, a quad faces along its +Z): the body-locked HUD panel and the reticle on the weapon's
// aim ray. Engine-agnostic; unit-tested in tests/Main.cpp.
namespace mevr::vr::overlay {

XrVector3f Rotate(XrQuaternionf const& q, XrVector3f const& v);

// The heading of `orientation`'s forward (-Z) about +Y, radians, positive = turned left, 0 when
// facing -Z. Looking straight up or down uses the up vector's heading instead.
float HeadingOf(XrQuaternionf const& orientation);

XrQuaternionf YawRotation(float yaw);

// Wraps to (-pi, pi].
float WrapAngle(float radians);

// A quad orientation whose front (+Z) points along `normal` (unit) and whose +Y leans toward `up`.
XrQuaternionf Facing(XrVector3f const& normal, XrVector3f const& up = {0.0f, 1.0f, 0.0f});

struct PanelSettings {
    float distance = 1.5f;      // from the anchor (eye height at recenter), meters
    float drop = 0.25f;         // center below eye height, meters
    float followAfter = 0.87f;  // head turned further than this (radians) ...
    float followDelay = 1.0f;   // ... for this long (seconds): the panel swings round; followAfter <= 0: never
};

// A panel fixed in tracking space in front of the body: tracking space doesn't turn when the
// player turns with the stick, so the panel stays in front of the physical body while the world
// turns. Recentering re-origins the space at the head, so Reset puts it straight ahead again.
class Panel {
public:
    void Reset();
    // Lazy follow: once the head has stayed turned away past followAfter for followDelay, the
    // panel eases round to the head's heading. `dt` in seconds.
    void Update(XrQuaternionf const& head, bool headValid, PanelSettings const& settings, float dt);
    // Re-place straight ahead of `head` now.
    void FaceHeading(XrQuaternionf const& head);
    // Center and orientation: `distance` ahead along the panel's heading, `drop` below eye
    // height, tilted to face the eye.
    XrPosef Pose(PanelSettings const& settings) const;
    float Yaw() const { return yaw_; }
    bool Following() const { return following_; }

private:
    float yaw_ = 0.0f;
    float turnedFor_ = 0.0f;
    bool following_ = false;
};

struct Billboard {
    XrPosef pose{};
    float size = 0.0f;  // edge length, meters
};

// A square quad `distance` along the ray (`direction` need not be unit length), facing `eye`,
// `angularSize` radians across as seen from `eye`. False if the ray or the geometry is degenerate.
bool PlaceOnRay(XrVector3f const& origin, XrVector3f const& direction, float distance, XrVector3f const& eye,
    float angularSize, Billboard& out);

} // namespace mevr::vr::overlay
