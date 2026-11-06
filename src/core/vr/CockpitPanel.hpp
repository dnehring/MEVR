// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>

// The Mako cockpit's two controls (research/48 section 27; LE1, riding with the cockpit up): the repair button and the
// extraction lever, models in the cabin the hands work. Pure rules here, unit-tested (tests/CockpitPanelTests.cpp);
// game/CockpitPanel feeds them the hands and the controls' places (tracking space, from the cockpit's models), moves the
// models, lights the lamps and calls the game (SFXGameModeVehicle.RepairVehicle and .LeaveWorld).
//
// The button: a fingertip pushes its cap in, and the cap follows up to its travel. Pushed past pressShare of the travel it is
// pressed; held pressed for holdS, the repair runs once. Lifting the finger before that empties the progress. A press needs a
// tip that came from in front of the cap (one resting beside it, or coming through it from below, doesn't). While the game's
// repair can't run (the user, 2026-11-05) the press fills nothing and runs nothing: with no omni-gel the plate's squares show
// solid red, while the repair cools down they flash red every refusalFlashS (lit its first half).
// The lever: a new grip squeeze within grabRadiusM of the handle's grip takes it, and the hand's travel about the hinge swings
// the handle out, from 0 to the model's pull. Held at the end (pullShare of it) for pullHoldS, the extraction runs once. Let go,
// it swings back. Each fires once per press or pull: the button must be let go, the handle brought back below half its pull.
namespace mevr::vr::cockpit_panel {

// [Cockpit] in ASI\LE1MEVR.ini (game/HandCalibration reads them; game/CockpitPanel the rest).
struct Settings {
    bool on = true;
    float holdS = 2.0f;            // the repair's hold: the plate's eight squares fill over it
    float buttonRadiusM = 0.03f;   // a fingertip this close to the button's axis presses it (its cap is 4.3 cm across)
    float hoverM = 0.05f;          // and this far in front of the cap it is coming from in front
    float behindM = 0.06f;         // a press holds while the tip is no deeper than the travel plus this (a finger through the cap)
    float pressShare = 0.6f;       // pushed in this share of the travel: pressed
    float releaseShare = 0.3f;     // back out past this share: let go
    float grabRadiusM = 0.1f;      // a hand's grip this close to the handle's grip takes it with a new squeeze
    float pullShare = 0.85f;       // the handle out this share of its pull: at the end
    float pullHoldS = 0.4f;        // held at the end this long: the extraction
    float returnDegPerS = 240.0f;  // a handle let go swings back this fast
    float refusalFlashS = 1.2f;    // pressed while the repair cools down: the squares flash red once this often
    bool throttleClick = true;     // the left thumbstick's click on the held throttle lever is the repair too (ThrottleClick)
    float clickRefusalS = 2.4f;    // a refused click shows red this long (two of the cooldown's flashes), or while it's held
};

// Why the game's repair can't run now (game/CockpitPanel asks the vehicle's VehicleRepair power while the button is pushed).
enum class Refusal : std::uint8_t { None, NoGel, Cooling };

// The pad mapping's grip latch (game/GamepadMapping, vr::mako_controls), so a squeeze here is the same squeeze there.
inline constexpr float kGripOn = 0.6f;
inline constexpr float kGripOff = 0.4f;
// The models' lamps (tools/cockpit/panel_controls.py): the ring's health segments and the plate's progress squares.
inline constexpr int kHealthLamps = 9;
inline constexpr int kProgressLamps = 8;

// The controls in tracking space (metres), this tick.
struct Layout {
    XrVector3f buttonTop{};               // the cap's top middle at rest
    XrVector3f buttonIn{0.0f, -1.0f, 0.0f};  // unit: the press, into the button
    float travelM = 0.0028f;              // how far the cap goes in
    XrVector3f hinge{};                   // a point on the handle's hinge
    XrVector3f hingeAxis{1.0f, 0.0f, 0.0f};  // unit: a turn about it by + (the right-hand rule) swings the handle out
    XrVector3f grip{};                    // the handle's grip at rest
    float pullDeg = 30.0f;                // how far the handle swings out
};

struct Hand {
    bool tracked = false;
    XrVector3f tip{};    // the index fingertip
    XrVector3f grip{};   // the grip pose's position
    float squeeze = 0.0f;
};

struct Input {
    double deltaSeconds = 0.0;
    bool active = false;  // riding with the cockpit and its layout: else everything lets go and nothing fires
    Layout layout;
    Hand hands[2];        // 0 left, 1 right
    Refusal refusal = Refusal::None;  // the repair can't run: no omni-gel wins over a cooldown
};

struct Haptic {
    int hand = 0;
    float seconds = 0.0f;
    float amplitude = 0.0f;
};

struct Output {
    float pressM = 0.0f;     // how far the cap is in (0..travel)
    bool pressed = false;
    int pressHand = -1;      // the hand pressing (or pushing the cap), -1 none
    float progress = 0.0f;   // 0..1: the hold, the plate's squares
    bool repair = false;     // this step: run the repair
    Refusal refused = Refusal::None;  // pressed while the repair can't run: the squares show red instead of the progress...
    bool refusedLit = false;          // ...lit now (solid with no gel; the flash's on half while cooling down)
    float pullDeg = 0.0f;    // the handle's swing out (0..pull)
    bool held = false;
    int holdHand = -1;
    bool inReach[2]{};          // the hand is within reach of the handle (its squeeze is the handle's, not a button)
    bool extract = false;    // this step: run the extraction
    Haptic haptics[6];
    int hapticCount = 0;
};

class Panel {
public:
    Output Step(Input const& input, Settings const& settings);
    // Everything let go (a level change, the cockpit gone).
    void Reset();

private:
    void Haptic(Output& out, int hand, float seconds, float amplitude);
    // the button
    bool front_[2]{};        // the tip was in front of the cap last step
    int pressHand_ = -1;
    bool pressed_ = false;
    double hold_ = 0.0;
    bool fired_ = false;     // the repair ran in this press
    int squares_ = 0;        // the progress squares lit (a tick each)
    Refusal refused_ = Refusal::None;
    double refusedFor_ = 0.0;  // how long this refusal has shown (the flash starts lit)
    // the lever
    bool down_[2]{};         // each hand's grip latch
    int holdHand_ = -1;
    XrVector3f grabFrom_{};  // the hand's grip at the grab, square to the hinge
    float grabDeg_ = 0.0f;   // the handle's swing at the grab
    float pull_ = 0.0f;
    double atEnd_ = 0.0;
    bool armed_ = true;      // the extraction may run (back below half the pull since it last did)
    bool endTicked_ = false;
};

// The repair on the throttle (the user, 2026-11-05, the release's first QA pass: the shelf's button was too far away): while the
// left hand holds the throttle lever, its thumbstick's click runs the repair at once, no hold, with the cockpit up or not. One
// click, one repair; a click that went down off the lever counts for nothing until it's let go. While the repair can't run the
// click runs nothing: the plate's squares show why as a press of the button would (solid red with no omni-gel, the flash while
// cooling down) for clickRefusalS or while the click is held, and the left hand buzzes twice.
struct ClickInput {
    double deltaSeconds = 0.0;
    bool leverHeld = false;  // the left hand holds the throttle lever (vr::mako_controls), our menu not up
    bool click = false;      // the left thumbstick's click
    Refusal refusal = Refusal::None;  // the repair can't run (asked while the click is down on the lever)
};

struct ClickOutput {
    bool repair = false;     // this step: run the repair
    Refusal refused = Refusal::None;  // a refused click: the squares show red...
    bool refusedLit = false;          // ...lit now
    Haptic haptics[3];
    int hapticCount = 0;
};

class ThrottleClick {
public:
    ClickOutput Step(ClickInput const& input, Settings const& settings);
    void Reset();

private:
    bool down_ = false;
    bool onLever_ = false;     // this click went down on the lever
    Refusal refused_ = Refusal::None;
    double refusedFor_ = 0.0;
    double secondBuzzIn_ = -1.0;  // a refusal's second buzz, this many seconds on (< 0 none)
};

// The lamps. Health: how many of `count` light for a hull at `health` (0..1): none at 0, all at full, each one a share of it.
int HealthLamps(float health, int count);
enum class Colour : std::uint8_t { Off, Green, Amber, Red, White, Blue };
// The health lamps' colour: green over 0.6, amber over 0.3, red below.
Colour HealthColour(float health);
// The progress squares lit for a hold at `progress` (0..1).
int ProgressLamps(float progress, int count);
char const* Name(Colour colour);
char const* Name(Refusal refusal);

}  // namespace mevr::vr::cockpit_panel
