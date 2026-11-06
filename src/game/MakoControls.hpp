// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Floor.hpp"
#include "core/vr/MakoControls.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/GamepadMapping.hpp"
#include "ue3/Types.hpp"

#include <optional>
#include <string>

// Physical Mako controls, stage 1 (research/46; LE1, game mode 1, the current chase camera; [Mako] controls in
// ASI\LE1MEVR.ini). The left hand on a throttle lever, the right on a drive stick, both fixed in tracking space at the seat;
// the rules are core/vr/MakoControls'. This is the game's side of them, called from game/mako's tick:
// - Physics: BioPlayerController.PlayerDriving.PlayerMove asks ProcessDrive, which reaches BioVehicleBase.SetInputs, a script
//   function, every tick. A post-handler of it (CallFunction, and ProcessEvent should anything native call it) overwrites
//   Vehicle.Throttle and Vehicle.Steering with ours while the mod drives, whatever the pad said: the camera-relative steering
//   happens above SetInputs, so looking around never steers. It leaves them alone where SetInputs zeroed them (the powertrain
//   off, the vehicle paused). No SetInputs seen for 200 ms: the same write from the mod's tick.
// - The pad (AdjustPad, the host's XInput hook): the game's own bindings for jump, boost and fire, through the buttons they
//   already read (gamepad::Options::makoLever, makoStick); no left stick from the controllers while the mod drives.
// - The turret: while the right hand holds the stick its thumbstick sets a yaw and pitch relative to the vehicle, which replaces
//   the hand's ray in game/mako's aim; let go, the hand's ray takes over again.
// - The chase camera: it doesn't follow the vehicle by itself (it hangs on ControlRotation, which only the right stick turns),
//   so while the mod drives the vehicle's yaw change goes into ControlRotation ([Mako] cameraFollow).
// - The picture: the View to the host each tick. Game thread. Until a control has been grabbed in this drive, nothing here
//   changes what the game does; a lookup that fails stands the feature down with a line in the log.
namespace mevr::game::mako_controls {

struct Link {
    bool headTrackedView = false;
    bool mainMenuOpen = false;
    bool supported = false;  // the host (v23) takes the pad flags: without them the game would see both
    std::optional<vr::floor::State> floor;  // the host's recenter counter
    void (*hapticPulse)(int hand, float seconds, float amplitude) = nullptr;
    void (*hapticContinuous)(int hand, float amplitude, float frequencyHz, float seconds) = nullptr;
    // HostServices v23 setMakoControls; null: an older host (no picture; the controls still work).
    void (*publish)(vr::mako_controls::View const* view) = nullptr;
};

// Registers the SetInputs post-handlers; call before the hooks go in. False until the script packages are loaded.
bool TryStartup();

// While the right hand holds the stick: the turret's ray in the world, set by the thumbstick.
struct StickAim {
    bool valid = false;
    engine::UnrealVector origin;
    engine::UnrealVector direction;  // unit
};

// Each mod tick, from game/mako: `vehicle` is the driven BioVehicleBase, null when not in the Mako. `currentAim` is the world
// direction the turret aims now (null: not aiming), where the thumbstick's aim starts from when the stick is grabbed.
StickAim Tick(double deltaSeconds, Link const& link, ue3::Object* vehicle, ue3::Object* controller, engine::UnrealVector const* currentAim);

// While the gunner aims (game/mako_gunner): the thumbstick's turret aim follows it, so a thumb that takes the gun starts from
// where the turret points.
void FollowAim(ue3::Object* vehicle, engine::UnrealVector const& direction);

// The pad flags for this tick: a squeeze at a control isn't RB or LB, what the held controls' buttons are.
void AdjustPad(gamepad::Options& options);
// The mod drives: a control has been grabbed in this drive.
bool Driving();
// `mevr vehicle jets <seconds>`: the jump jets' button (BioPlayerController.bJumpJetsOn) held on, to fire them without the pad
// (tests on the fake headset: the pad reaches the game only with its window in front). Game thread.
std::string TestJets(double seconds);

// Where the controls are now, for the cockpit's models (research/47): the lever -1..1 of its travel (meters from centre to
// full), the steering -1..1 (+ right) of its full lock (degrees of twist). Zero when not in the Mako.
struct Pose {
    float lever = 0.0f;
    float travelM = 0.055f;
    float steer = 0.0f;
    float lockDeg = 42.0f;
    bool tilt = false;          // the stick tilts (else it twists by `steer` x `lockDeg`)
    float tiltRightDeg = 0.0f;  // its tilt: the top to the right, and away from the player
    float tiltForwardDeg = 0.0f;
    float twistDeg = 0.0f;      // tilting, its turn about its own (tilted) axis: + counter-clockwise from above (the hand's twist)
    bool leverHeld = false;  // the left hand holds the lever (Shepard's left hand goes on it)
    bool stickHeld = false;  // the right hand holds the stick
};
Pose CurrentPose();

std::string StatusJson();
void LogStats();
// Throttle and steering to zero unless the process is exiting, the host's picture gone.
void Shutdown(bool exiting, void (*publish)(vr::mako_controls::View const* view));

} // namespace mevr::game::mako_controls
