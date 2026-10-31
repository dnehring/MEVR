// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/MenuPointer.hpp"

#include <openxr/openxr.h>

#include <cstdint>

// The game pointer (research/42): a controller's aim ray on the flat screen Halcyon shows the game on (the
// game's own menus, the galaxy map) becomes the game's mouse. This is the game-independent part: which hand
// points, where its ray meets the quad, the quad's UV as a pixel of the game window's client area, the
// trigger as the left button (a latch with a dead zone, a press that outlives the ray leaving the quad), the
// right stick as wheel notches, and when a mouse move is worth sending (the pixel changed by more than the dead
// band). No Win32 here: the host (plugin/GamePointer) sends what Step says. Unit-tested in tests/Main.cpp.
namespace mevr::vr::game_pointer {

using Quad = menu_pointer::Quad;   // the flat screen in the tracking space (menu_pointer::InTracking)

struct HandInput {
    bool aimValid = false;
    XrPosef aim{};           // tracking space; forward is -Z
    float trigger = 0.0f;    // 0..1
};

struct Input {
    bool active = false;       // the controllers are tracked (false: everything is released)
    HandInput hands[2];        // 0 left, 1 right
    float rightStickY = 0.0f;  // -1..1, + forward: the wheel
    int width = 0;             // the game window's client area, pixels
    int height = 0;
    float dt = 0.0f;           // seconds since the last Step
};

struct Settings {
    float triggerOn = 0.6f;       // the latch closes at or above this
    float triggerOff = 0.4f;      // and opens below this
    float stickDeadZone = 0.2f;
    float wheelPerSecond = 6.0f;  // notches (WHEEL_DELTA) per second at full deflection
    float smoothing = 0.5f;       // per step, of the way to the new point (hand tremor); 1 = none
    float deadBandPx = 1.0f;      // a move smaller than this (both axes) isn't reported
};

struct Output {
    bool hit = false;          // the pointing hand's ray is on the quad, or a press begun there is held
    bool onQuad = false;       // the ray itself is on the quad this step
    bool left = false;         // not hit, but the last step was: the pointer just left the quad
    int hand = 1;              // 0 left, 1 right: the hand that points
    int x = 0;                 // client pixels (clamped to the area); the last point while off the quad in a press
    int y = 0;
    bool moved = false;        // (x, y) differs from the last reported point by more than the dead band
    bool pressed = false;      // the trigger is latched (the left button is down)
    bool pressEdge = false;    // pressed changed this step: a button down (pressed) or up (!pressed) to send
    int wheelNotches = 0;      // whole notches to send this step (+ away from the player = scroll up)
    float u = 0.0f;            // the hit on the quad, 0..1 from the top-left
    float v = 0.0f;
    float distance = 0.0f;     // along the ray, meters
    XrVector3f point{};        // the hit in the tracking space (the dot)
};

class Pointer {
public:
    explicit Pointer(Settings const& settings = Settings{});
    // One step: the quad as submitted this frame (or any quad with width 0 for "none": releases a press).
    Output Step(Quad const& quad, Input const& input);
    // Everything released (the quad went away, a menu of ours opened): the Output says whether a press was
    // open (pressEdge with !pressed, at the last point), so the host can send the button up.
    Output Release();
    int Hand() const { return hand_; }
    bool Pressed() const { return pressing_; }
    Settings const& GetSettings() const { return settings_; }

private:
    bool Latch(int hand, float value);

    Settings settings_;
    int hand_ = 1;
    bool latched_[2] = {false, false};
    bool pressing_ = false;
    bool wasHit_ = false;
    float smoothU_ = 0.0f;
    float smoothV_ = 0.0f;
    float reportedX_ = 0.0f;  // the last point reported with moved = true
    float reportedY_ = 0.0f;
    bool reported_ = false;
    float wheelAccum_ = 0.0f;
};

// UV (0..1 from the top-left) as a pixel of a `width` x `height` client area, clamped to it.
void ToPixel(float u, float v, int width, int height, int& x, int& y);

// The point on the quad at (u, v), tracking space.
XrVector3f PointOnQuad(Quad const& quad, float u, float v);

// The size of a dot at `point` that looks `angularSize` radians across from `eye`.
float DotSize(XrVector3f const& eye, XrVector3f const& point, float angularSize);

} // namespace mevr::vr::game_pointer
