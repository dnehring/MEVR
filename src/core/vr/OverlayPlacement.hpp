#pragma once

#include <openxr/openxr.h>

#include <cstdint>

// Where the renderer's own quads go in tracking space (OpenXR conventions: meters, +Y up, -Z
// forward, a quad faces along its +Z): the body-locked HUD panel, the world-locked flat panels
// (Halcyon's flat screen and menu) and the reticle on the weapon's aim ray. Engine-agnostic;
// unit-tested in tests/Main.cpp.
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
    float followAfter = 1.2217f;  // head turned further than this (radians, 70 degrees) ...
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

// ---- World-locked flat panels -------------------------------------------------------------------
// A flat panel (a virtual screen or a menu) fixed in tracking space where the head was when it
// appeared, instead of riding the head: level (yaw only), so leaning gives parallax and looking
// around leaves it in place.

struct WorldPanelSettings {
    float followAfter = 70.0f * 3.14159265358979f / 180.0f;  // head turned further than this (radians) ...
    float followDelay = 1.0f;  // ... for this long (seconds): it eases round in front of the head; followAfter <= 0: never
    std::uint64_t gapFrames = 5;  // not shown for more frames than this: it appeared afresh
    // A re-placement (appeared, kind changed) that would move it less than this keeps it where it
    // is, so a kind that flickers or a panel that blinks doesn't nudge it about.
    float keepYaw = 5.0f * 3.14159265358979f / 180.0f;  // radians
    float keepDistance = 0.05f;                           // meters
};

enum class Placement {
    Kept,         // stays where it was
    Appeared,     // shown afresh (or for the first time): placed in front of the head
    KindChanged,  // another kind of panel took its place: placed in front of the head
    Recentered,   // the tracking space was re-origined: placed in front of the head
    HeadFound,    // it was placed blind (no head yet); now in front of the head
    Followed,     // the head stayed turned away: it starts easing round
};

char const* PlacementName(Placement placement);

class WorldPanel {
public:
    // Recentering re-origined the tracking space: the next Update places it afresh.
    void Recenter() { recenter_ = true; }

    // Each frame the panel is shown. `frame` counts frames (consecutive frames differ by 1), `kind`
    // tells panels sharing this slot apart, `head` is the eyes' pose in tracking space, `dt` in
    // seconds. It is placed in front of the head when it appears (not shown for more than
    // gapFrames), when its kind changes, after Recenter, or once the head is found after it was
    // placed blind (at the origin, facing -Z); and it eases round once the head has stayed turned
    // away past followAfter for followDelay. Returns why it moved this frame.
    Placement Update(int kind, std::uint64_t frame, XrPosef const& head, bool headValid,
        WorldPanelSettings const& settings, float dt);

    // Where a quad goes that sits at `local` in the panel's level frame (meters: x right, y up,
    // -z ahead, from the eyes when it was placed): its pose in tracking space, upright, its front
    // (+Z) facing back toward the eyes.
    XrPosef PoseOf(XrVector3f const& local) const;

    XrVector3f Anchor() const { return anchor_; }
    float Yaw() const { return heading_.Yaw(); }
    bool Following() const { return heading_.Following(); }
    bool Placed() const { return placed_; }

private:
    void PlaceAt(XrPosef const& head, bool headValid);

    Panel heading_;
    XrVector3f anchor_{};
    std::uint64_t lastFrame_ = 0;
    int kind_ = -1;
    bool placed_ = false;
    bool blind_ = false;
    bool recenter_ = false;
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
