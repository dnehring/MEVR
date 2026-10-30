// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>

// The interaction prompt at the object (research/40). The game names what A would use in its HUD's target status
// widget: the object's name and, under it, the action ("Open", "Talk"), with the A glyph only while it's in use range,
// at the top centre of the HUD movie. On our body-locked HUD panel that is about 0.4 degrees tall and far from the
// object. The mod says where the selected object is (HostServices v21, setInteractLabel); the host cuts that part of
// the UI out of the HUD panel and shows it on a quad of its own at the object, facing the eyes, the same angular size
// at any distance. Shared by the host (plugin/HalcyonOverlays) and the mod (game/Interact); pure parts unit-tested.
namespace mevr::vr::interact {

struct Label {
    std::uint32_t size = sizeof(Label);  // the publisher's struct size: host and mod must match
    bool visible = false;      // a selection the label should show (on foot, not a combat target unless asked)
    bool combat = false;       // the selection is a combat target (its name and health bars)
    XrVector3f position{};     // tracking space (meters): where the label's centre goes
    float heightDeg = 6.0f;    // the angular height of the cut-out band ([Interact] labelHeightDeg)
    float crop[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // x, y, width, height of the frame ([Interact] crop); width 0: the default
    std::uint32_t serial = 0;  // changes when the selected object does
    std::uint64_t sequence = 0;  // set by Publish
    std::uint64_t timeMs = 0;    // GetTickCount64 when published
};

// [Interact] in ASI\LE1MEVR.ini (game/HandCalibration reads it; the mod applies it).
struct Settings {
    bool gaze = true;          // the game chooses what A uses from where the head looks (engine/SelectionGaze)
    bool label = true;         // the prompt on a quad at the object
    bool combatLabel = false;  // also a combat target's name and health bars at the target
    float heightDeg = 6.0f;    // the label band's angular height (the name's letters are about 1/7 of it)
    float crop[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // the band by hand (fractions of the HUD frame); width 0: the default
};

void Publish(Label const& label);
// The latest label; `visible` false (and size 0) if none was published within `maxAgeMs` (the mod stopped).
Label Latest(std::uint64_t maxAgeMs = 300);
void Reset();

// A rectangle as fractions of the frame: x, y (top left), width, height.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool Valid() const { return width > 0.0f && height > 0.0f; }
};

// The target status widget's part of a frame of `width` x `height` pixels. The HUD movie's 1280x720 stage is shown
// whole across the frame's width, aligned to the top (WheelCrop's "show all", top left), so the band is placed in
// stage pixels: x 384..896 (the widget is centred; the enemy health arc spans about 460..800), y 30..100 (the name
// and the action line; measured 2026-10-30 on 2344x2392 HUD frames: text at y 0.034..0.063 of the height).
Rect DefaultCrop(std::uint32_t width, std::uint32_t height);

// The crop as whole pixels of a width x height frame (at least 8x8, inside the frame).
struct Pixels {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};
Pixels ToPixels(Rect const& rect, std::uint32_t width, std::uint32_t height);

// The label's quad: centred at `position`, its front facing `head` (no roll), `heightDeg` tall as seen from there
// and as wide as the crop's `aspect` (width / height) makes it. False if the geometry is degenerate (the label at
// the head).
struct Quad {
    XrPosef pose{};
    float width = 0.0f;
    float height = 0.0f;
};
bool Place(XrVector3f const& position, XrVector3f const& head, float heightDeg, float aspect, Quad& out);

// Where the label goes for an object: above the middle of its bounds (Unreal units, Z up), `raise` units over the top
// (the top capped at `maxRise` over the centre, so a tall door's label stays in view), never more than `aboveEye`
// units over the eye height `eyeZ`.
struct Point {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};
Point LabelPoint(Point const& boundsOrigin, double boundsHalfHeight, double eyeZ, double raise = 12.0, double maxRise = 60.0,
    double aboveEye = 15.0);

} // namespace mevr::vr::interact
