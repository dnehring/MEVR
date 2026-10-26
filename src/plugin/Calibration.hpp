// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Calibration.hpp"
#include "core/vr/Controllers.hpp"

#include <openxr/openxr.h>

#include <cstdint>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;

// The guided calibration in the headset (research/25; the math in core/vr/Calibration, the look in
// plugin/CalibrationDraw). About a minute, started from our main menu (hold both sticks > Body & calibration > Start
// calibration), the VR hands tab's Calibrate button, or the one-time prompt the first time VR runs with controllers
// that have no calibration:
//   1 stand tall, look ahead (standing eye height; the heading the rest is laid out on)
//   2 arms out to the sides, then straight ahead (the arm-length T-pose sample [Arms] measure* as the tab
//     takes it, and the shoulder width)
//   3 crouch (crouched eye height)
//   4 take the pistol orb with the right grip, aim at three targets
//   5 take the two rifle orbs (right grip, left grip), shoulder, aim at three targets
//   6 check: a red dot along each new sight line from the hand; save, switch eye, or redo a part
// A sample is taken when the head and hands hold still for a second while it fits the step (no buttons).
// The panel's buttons (skip, start again, cancel; save, redo) answer a controller's aim ray and its trigger.
//
// While it runs (and Halcyon's menu is closed, the world shown): the game sees a neutral pad and the mod
// sees no controllers (HalcyonBridge publishes them hidden), and HostServices v15 says so, so holsters,
// grenades, melee, the squad trigger, the support grip and the physical crouch stand down. Its quads replace
// ours (HUD panel, reticle, sights, squad menu, wheel) and are laid out world-locked in the app space.
//
// Results: [Calibration.<key>] in <GAME>MEVR.ini, one section per controller type (interaction profile and
// headset: vr::calibration::ProfileKey), read by the mod (game/body_calibration). Present thread unless noted.
namespace mevr::host::calibration {

// ---- Any thread -----------------------------------------------------------------------------------------
// The sequence runs and isn't paused (Halcyon's menu closed): the game must see no pad, the mod no controllers.
bool Active();
// The first-run prompt is up with a controller pointing at it: the triggers aren't the game's.
bool OwnsTriggers();
// HostServices v15.
void HostStateOut(vr::calibration::HostState* out);

// ---- Present thread ---------------------------------------------------------------------------------------
void Start(char const* why);
void Cancel(char const* why);
bool Running();
// The headset (xrGetSystemProperties) and the controllers' interaction profile (xrGetCurrentInteractionProfile):
// the calibration is kept per both. Empty: not known (yet).
void SetSystem(std::string const& systemName);
void SetProfile(std::string const& interactionProfile);
// Each frame, after the controller sync and the test loop (a replay's controllers stand in for live ones).
void NoteControllers(vr::controllers::State const& state);
// Each frame: the session has input focus (the headset is worn; or a replay drives the inputs). The first-run prompt's
// delay and its timeout count only then, so a headset left on a desk doesn't use them up.
void NoteFocused(bool focused);
// What everything else sees while Active(): the same sample with no controller in it.
vr::controllers::State Hidden(vr::controllers::State const& state);

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    bool worldFrame = false;   // the head-tracked world is submitted
    bool menuOpen = false;     // Halcyon's menu is open (the sequence pauses)
    int gameMode = -1;         // 0 on foot (the prompt waits for it)
    bool headValid = false;
    XrPosef head{};            // the eyes' centre and the head's orientation, app space
    XrVector3f eyes[2]{};      // the runtime's eye positions, app space
};
// OverlayLayers, every frame: the sequence's quads (or the prompt's), at most `capacity`. While Active() they
// are the only MEVR quads.
unsigned Layers(Frame const& frame, XrCompositionLayerQuad const** out, unsigned capacity);
// Recentering re-origined the app space: the layout is placed afresh in front of the head.
void Recentered();
// For snapshots (plugin/TestLoop).
std::string StatusJson();

// The VR hands tab.
struct Summary {
    bool running = false;
    std::string step;          // what it's doing now
    std::string device;        // the controllers it calibrates for (label)
    std::string key;           // their section key
    bool calibrated = false;   // [Calibration.<key>] has a saved calibration
    std::string when;          // its time
    std::string results;       // one line: eye, spreads, heights
    // The saved values as written ("" when not saved): the main menu's tiles.
    std::string eye;           // "left" / "right"
    std::string pistolSpreadDeg;
    std::string rifleSpreadDeg;
    std::string standEyeM;
    std::string crouchEyeM;
};
Summary GetSummary();

} // namespace mevr::host::calibration
