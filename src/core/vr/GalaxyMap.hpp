// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>

// The galaxy map as a hologram (research/51 stage B): LE1's map is a 3D scene the engine renders from a camera mode of its
// own (BioCameraBehaviorGalaxy), with a Flash layer on top. Instead of Halcyon's flat panel, the map renders in stereo at a
// scale that makes it a table-sized model in front of the player: while the map is up, the stereo's units per metre (the eye
// separation and the head's movement, Halcyon's lean) are those that put the map's centre `distanceM` ahead of the player,
// and the view is raised `belowM` so the model sits below the eyes.
//
// The mod (game/GalaxyMap) measures the map's camera against the map's centre each tick and publishes a View (HostServices
// v25, setGalaxyMap); the host (Halcyon's galaxy branch, the [MEVR] galaxy seams) uses it while the game is in its galaxy
// mode. Pointing: rendered from a camera the game takes to stand at a fixed place in the room, the game's screen is a quad in
// the room through the map's centre (VirtualScreen), so the game pointer (research/42) turns a controller's ray on it into the
// game's own mouse. This file: engine-agnostic and unit-tested (tests/Main.cpp TestGalaxyMap).
namespace mevr::vr::galaxy_map {

// ---- Settings ([Galaxy] in <GAME>MEVR.ini, parsed by the mod) -------------------------------------------------------
struct Settings {
    bool hologram = true;     // hologram: our own hologram of the map (core/vr/GalaxyHologram, research/51 section 8) on a table
                              // in front of the player; 0 = the game's map on Halcyon's flat panel only
    float distanceM = 0.9f;   // (the camera-based experiment's, unused since research/51 section 7)
    float belowM = 0.30f;
    float tableDistanceM = 0.70f;  // tableDistanceM: the table's centre ahead of the head when the map opens
    float tableBelowM = 0.45f;     // tableBelowM: ... and below the eyes
    float tableRadiusM = 0.48f;    // tableRadiusM: the map's rim
    bool inWorld = true;           // inWorld: the hologram stands over the CIC's projector and you use it on foot; while the game
                                   // is in its map mode the view stays in the CIC (the game's own map hidden); 0: the table in
                                   // front of you once the map is open, the game's map as a screen behind it
    float worldRadiusM = 1.40f;    // worldRadiusM: the CIC table's radius
    float worldTiltDeg = 35.0f;    // worldTiltDeg: its far edge raised toward you (read from the console, a few metres away)
    float worldLiftM = 0.0f;       // worldLiftM: its centre above the galaxy map console's use point (about 1 m above the floor)
    bool window = true;            // window: with the map open in the CIC, the game's own map (its 3D scene in stereo, its Flash
                                   // layer) on a window over the table instead of our hologram (research/51 section 8)
    float windowWidthM = 2.0f;     // windowWidthM: that window's width
    float windowDepth = 0.03f;     // windowDepth: infinity's disparity, a share of the window's width (core/vr/CineScreen's depth)
    bool windowRound = true;       // windowRound: the window is a disc with a soft edge, like the projector's own round hologram
                                   // (0: the captures' rectangle)
};

// ---- What the mod publishes ------------------------------------------------------------------------------------------
struct View {
    bool active = false;          // the map is up and measured: the host shows it as a hologram
    float unitsPerMeter = 0.0f;   // game units a metre of the room is, while the map is up
    float distanceM = 0.9f;
    float belowM = 0.30f;
    std::uint64_t sequence = 0;   // set by Publish
    std::uint64_t timeMs = 0;     // GetTickCount64 when published (set by Publish)
};

void Publish(View const& view);
void Reset();
// The latest view if one was published within maxAgeMs and is active; else nullopt.
std::optional<View> Latest(std::uint64_t maxAgeMs);

// ---- The math --------------------------------------------------------------------------------------------------------
// The units per metre that put a point `cameraToCentreUnits` ahead of the map's camera at `distanceM` in the room; nullopt for
// a distance that can't be (zero, negative, not finite). Clamped to [kMinUnitsPerMeter, kMaxUnitsPerMeter].
inline constexpr float kMinUnitsPerMeter = 1.0f;
inline constexpr float kMaxUnitsPerMeter = 10000000.0f;
std::optional<float> UnitsPerMeter(float cameraToCentreUnits, float distanceM);

// The game's screen as a quad in the room (tracking space, OpenXR axes: -Z ahead, +Y up), for a view rendered from a camera
// the game takes to stand `belowM` below the tracking origin, looking along -Z, with a symmetric window of tan half angles
// tanHalfX, tanHalfY: the quad through the point `distanceM` ahead, facing the camera, covering the whole window (the
// backbuffer's corners on its corners). A ray's hit on it is the window pixel the game's projection puts that point at.
struct Screen {
    XrPosef pose{};   // its centre; it faces +Z (toward the camera)
    float width = 0.0f;
    float height = 0.0f;
};
std::optional<Screen> VirtualScreen(float distanceM, float belowM, float tanHalfX, float tanHalfY);

// Where the game's projection puts a point of the room (the same camera): the window's UV (0,0 top left, 1,1 bottom right);
// nullopt behind the camera. VirtualScreen's quad maps its points to the same UV (the test checks it).
std::optional<XrVector2f> ProjectToWindow(XrVector3f const& point, float belowM, float tanHalfX, float tanHalfY);

} // namespace mevr::vr::galaxy_map
