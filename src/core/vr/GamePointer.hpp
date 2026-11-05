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
    // The index fingertip (tracking space): touching the quad with it is the mouse, Half-Life: Alyx's way (research/53
    // section 6.2). Near the quad's touchable part it takes over from the rays.
    bool tipValid = false;
    XrVector3f tip{};
};

struct Input {
    bool active = false;       // the controllers are tracked (false: everything is released)
    HandInput hands[2];        // 0 left, 1 right
    float rightStickY = 0.0f;  // -1..1, + forward: the wheel
    int width = 0;             // the game window's client area, pixels
    int height = 0;
    float dt = 0.0f;           // seconds since the last Step
    // The part of the quad a fingertip can touch (quad UV): what's visible of it (the world UI shows a crop of the whole window
    // the quad stands for). The rays reach the whole quad as before.
    float touchU0 = 0.0f;
    float touchV0 = 0.0f;
    float touchU1 = 1.0f;
    float touchV1 = 1.0f;
};

struct Settings {
    float triggerOn = 0.6f;       // the latch closes at or above this
    float triggerOff = 0.4f;      // and opens below this
    float stickDeadZone = 0.2f;
    float wheelPerSecond = 6.0f;  // notches (WHEEL_DELTA) per second at full deflection
    float smoothing = 0.5f;       // per step, of the way to the new point (hand tremor); 1 = none
    float deadBandPx = 1.0f;      // a move smaller than this (both axes) isn't reported
    // Touch: a fingertip within touchHoverM in front of the touchable part moves the pointer there; at touchPressM (or through
    // the surface) it presses, and the press lets go once the tip is back past touchReleaseM. Only a tip that came from the
    // front presses (a hand coming through the quad from behind, or resting in it, doesn't). touchMarginM around the part
    // still counts, so the edges' buttons take a touch.
    bool touch = true;
    float touchHoverM = 0.06f;
    float touchPressM = 0.005f;
    float touchReleaseM = 0.015f;
    float touchBehindM = 0.06f;   // how far behind the surface a pressing tip still holds the press
    float touchMarginM = 0.01f;
    float touchSmoothing = 0.8f;  // a lighter smoothing than the ray's: the fingertip is steady
    // Scrolling by touch (research/56, the user 2026-11-04: "we'd need scrolling support when we're going through inventory"):
    // a fingertip at the surface waits before it presses. Let go within touchHoldS without moving: a click (the button down,
    // and up on the next step). Held still past touchHoldS: a press as before (holding, dragging a slider). Moved more than
    // touchDragM along the surface first: a drag that scrolls (a wheel notch per touchScrollNotchM, the content following the
    // finger) and never clicks. touchScroll = false presses at once, as before.
    bool touchScroll = true;
    float touchHoldS = 0.15f;
    float touchDragM = 0.012f;
    float touchScrollNotchM = 0.013f;
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
    bool touch = false;        // the point is a fingertip's (near the quad), not a ray's
    float touchDistance = 0.0f;  // the fingertip in front of the surface, meters (< 0 behind it)
    bool touchWaiting = false;   // a fingertip is at the surface, not yet a click, a press or a drag
    bool dragging = false;       // a fingertip drags along the surface: it scrolls (wheelNotches), nothing pressed
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
    bool touchArmed_[2] = {false, false};    // the tip was in front of the surface, in reach, since it last left
    bool touchLatched_[2] = {false, false};  // the tip pressed and hasn't pulled back past touchReleaseM
    bool pressByTouch_ = false;              // the open press is a touch's (else a trigger's)
    bool touchWaiting_ = false;              // a touch reached the surface: click, press or drag not decided yet
    bool dragging_ = false;                  // the touch is a drag: it scrolls
    bool clickUpNext_ = false;               // a tap's button down was sent: its up goes on the next step
    int touchHand_ = 1;                      // the hand of the waiting or dragging touch
    float waitS_ = 0.0f;
    float waitU_ = 0.0f;                     // where the touch reached the surface (quad UV)
    float waitV_ = 0.0f;
    float dragV_ = 0.0f;                     // the drag's last V
    float dragAccum_ = 0.0f;                 // notches not yet whole
    bool lastTouch_ = false;                 // the last step's point was a fingertip's
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
