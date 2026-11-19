// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/GalaxyHologram.hpp"

#include <openxr/openxr.h>

#include <array>
#include <optional>
#include <span>

// The game's own galaxy map picked with a controller's ray on a sequel (lane GXR, 2026-11-18; the owner: "whichever is closest to
// ME1's experience"): the map lies on the CIC's projector as a stereo window with the game's UI on a panel over it (the sequels'
// flow, game/GalaxyProjectorFlow), and ME1's way of picking it is a ray and the trigger on what the map shows (ME1: the game
// pointer on the window, research/51 section 8.1). The ray's point on the panel is a point of the game's own screen; the map's
// objects are put on that screen through the game's map camera (galaxy_holo::ToGameScreen), and the nearest to the ray is the one
// pointed at. In orbit (ME2's planet scan) the ray's point on the screen is a ray in the map's world through the map camera, and
// where it meets the planet is where the scanner's reticle goes. Game-independent; unit-tested (tests/GalaxyRayTests.cpp).
namespace mevr::vr::galaxy_ray {

// The UI panel over the window (the game's whole frame), as the host draws it: centred on the disc, its front (+Z) toward the
// head's side (galaxy_holo::DiscPose), widthM across and heightM down.
struct Panel {
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    float widthM = 0.0f;
    float heightM = 0.0f;
};

// The panel of a window on a disc (the flow's scene): width = the window's / (1 - crop) (the window shows the captures' crop, the
// panel the game's whole frame: plugin/GalaxyHologram's UiPanelWidthM), height = width / the game window's aspect.
Panel PanelOnDisc(XrVector3f const& discCentre, XrVector3f const& discNormal, XrVector3f const& head, float windowWidthM,
    float windowCrop, float aspect);

struct Hit {
    float u = 0.0f, v = 0.0f;  // the game's screen, 0..1 from its top left
    float distanceM = 0.0f;    // from the ray's origin
    XrVector3f point{};
};
// Where a ray meets the panel; nullopt off it, parallel to it, or behind the ray's origin.
std::optional<Hit> PanelHit(XrVector3f const& origin, XrVector3f const& direction, Panel const& panel);

// A ray in the map's world (game units, UE3's axes) through the game's screen at (u, v) (y down), from the map camera.
struct WorldRay {
    std::array<float, 3> origin{};
    std::array<float, 3> direction{};  // unit
};
std::optional<WorldRay> ScreenRay(galaxy_holo::GameCamera const& camera, float u, float v);

// Where a world ray meets a sphere (the planet in orbit), the nearer side; nullopt if it misses. `grazeScale` > 1 widens the
// sphere for the miss test only: a ray just past the rim lands on the rim (the reticle stays on the planet's edge).
std::optional<std::array<float, 3>> SphereHit(WorldRay const& ray, std::array<float, 3> const& centre, float radius,
    float grazeScale = 1.0f);

// One of the map's objects as the game's screen shows it.
struct Candidate {
    float u = 0.0f, v = 0.0f;
    bool valid = false;  // on the screen (in front of the camera)
};
// The candidate nearest (u, v) within `toleranceH` (a fraction of the screen's height; distances aspect-correct), or -1.
int Nearest(std::span<Candidate const> candidates, float u, float v, float aspect, float toleranceH);

// A direction as a UE3 rotator's yaw and pitch (65536 units a turn), the inverse of Vector(Rotator).
struct YawPitch {
    int yaw = 0;
    int pitch = 0;
};
YawPitch ToRotator(std::array<float, 3> const& direction);
// A direction turned by the inverse of a UE3 rotator (pitch, yaw, roll in rotator units): world to the rotator's local frame.
std::array<float, 3> ToLocal(std::array<float, 3> const& direction, int pitch, int yaw, int roll);

} // namespace mevr::vr::galaxy_ray
