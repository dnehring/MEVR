// SPDX-License-Identifier: MIT
#pragma once

#include <vector>

// The power wheel at the hand (research/55, the user's concept of 2026-11-04, concept/power-wheel.png): tiles in a ring
// around the palm, the selected one pulled out, a gap where the forearm comes in. This is the game-independent part:
// where the tiles go, their outline, and which one a hand's offset from the centre picks. The look is
// plugin/PowerWheelDraw's. Unit-tested in tests/Main.cpp (TestPowerWheel).
//
// Conventions: the wheel's plane with x right and y up, angles in degrees counter-clockwise from +x (90 is up), lengths
// in whatever unit the caller uses (canvas pixels for the drawing, metres for the hand).
namespace mevr::vr::power_wheel {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// The concept's six tiles sit at 184, 139, 90, 37, -7 and -69 degrees around a left palm: an arc of 253 degrees,
// evenly spaced to within its perspective, with the empty part toward the bottom left, where the forearm comes in.
struct LayoutSettings {
    float gapCentreDeg = -122.5f;  // the middle of the empty part (toward the forearm)
    float gapDeg = 107.0f;         // from the last tile's centre to the first's, across the gap
    float minStepDeg = 34.0f;      // more entries close the gap before the tiles come closer than this
    float maxStepDeg = 52.0f;      // fewer entries keep this spacing, centred opposite the gap
};
// The spacing between neighbouring tiles for `count` entries.
float StepDeg(int count, LayoutSettings const& settings = {});
// Each entry's centre angle, in (-180, 180], entry 0 at the arc's counter-clockwise end, then clockwise.
std::vector<float> Angles(int count, LayoutSettings const& settings = {});

// A tile: its outer edge an arc, its sides along radii, its inner edge straight (the concept's: the spoke meets it in its
// middle) or an arc, its corners cut.
struct TileSettings {
    float innerRadius = 222.0f;  // the inner edge's distance from the centre (at its middle)
    float outerRadius = 410.0f;
    float halfWidthDeg = 22.0f;
    float outerChamfer = 30.0f;  // the outer corners' cut, along each edge
    float innerChamfer = 12.0f;  // the inner corners'
    bool straightInner = true;
    int arcSegments = 24;        // points along each edge (the cuts' corners are added)
};
// The tile's half-width for `count` entries: the settings' own, narrowed so neighbours keep a gap of `spacingDeg`.
float HalfWidthDeg(int count, TileSettings const& tile, LayoutSettings const& layout = {}, float spacingDeg = 5.0f);

// The tile's boundary as matching points along its outer and inner edge, from its clockwise side to its
// counter-clockwise side: a strip whose quads fill it (each inner point under its outer point), and whose outline is
// the outer points, then the inner points backwards.
struct Strip {
    std::vector<Vec2> outer;
    std::vector<Vec2> inner;
};
Strip TileStrip(float angleDeg, TileSettings const& tile);
std::vector<Vec2> Outline(Strip const& strip);
// The same tile scaled about its own middle (the selected tile grows) and moved out along its angle.
Strip Transformed(Strip const& strip, float angleDeg, float middleRadius, float scale, float pushOut);

// Which entry a hand picks from its offset from the wheel's centre (in the wheel's plane): the one whose angle is
// nearest the offset's direction, once the offset leaves the dead zone. Inside the dead zone the current pick stays
// (it's the way back to the centre, not a choice). A pick changes only when another entry is nearer by `keepDeg`, so
// a hand on the border between two doesn't flicker. Directions in the gap go to the nearer end. -1: nothing picked.
struct PickSettings {
    float deadZone = 0.025f;  // the offset's length below which nothing changes (the caller's unit: metres)
    float keepDeg = 6.0f;
};
int Pick(Vec2 offset, std::vector<float> const& angles, int current, PickSettings const& settings = {});

// The signed difference a - b in degrees, in (-180, 180].
float AngleDiff(float a, float b);

}  // namespace mevr::vr::power_wheel
