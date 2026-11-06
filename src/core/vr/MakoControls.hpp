// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <string>
#include <vector>

// Physical Mako controls, stage 1 (research/46; LE1, game mode 1, the current chase camera). Two controls fixed in tracking
// space relative to the seat, driven by real hand movement. The left hand grips a throttle lever and pushes it forward or
// pulls it back (let go, it springs back to neutral: leverReturns, the user's choice 2026-11-03 and 2026-11-04); the right hand grips a drive stick and tilts it like a flight stick (it springs
// back to centre): side to side steers, forward and back pitches the Mako while its thrusters fire (game/MakoControls; the user,
// 2026-11-03). `stickTwist` keeps the first design, a twist about the stick's own axis. Movement counts from where the hand grabbed, so a grab never jerks the Mako and no exact
// grab spot is needed. Pure parts here, unit-tested (tests/Main.cpp TestMakoControls); game/MakoControls feeds the
// physics and the pad; the host (plugin/HalcyonOverlays) draws the two from View (HostServices v23, setMakoControls).
namespace mevr::vr::mako_controls {

// The pad mapping's grip latch (game/GamepadMapping), so a squeeze here is the same squeeze there.
inline constexpr float kGripOn = 0.6f;
inline constexpr float kGripOff = 0.4f;
// A control's hand may lose tracking this long before the drive stops.
inline constexpr float kTrackingGraceS = 0.5f;
// How fast the steering may move, per second: while held (full lock in about 0.1 s) and springing back on release.
inline constexpr float kSteerRateHeld = 12.0f;
inline constexpr float kSteerRateReturn = 6.0f;
// After a grab of the stick its zero follows the hand this long (a squeeze shifts the hand: the user's first drive started each
// grab tilted forward), and a released lever goes back to neutral at this many travels a second.
inline constexpr float kGrabSettleS = 0.2f;
inline constexpr float kLeverReturnRate = 4.0f;
// The right stick's dead zone for the turret, and the turret's pitch range relative to the vehicle (degrees).
inline constexpr float kAimStickDeadZone = 0.15f;
inline constexpr float kAimPitchMinDeg = -45.0f;
inline constexpr float kAimPitchMaxDeg = 60.0f;

// [Mako] in ASI\LE1MEVR.ini (read by game/HandCalibration).
struct Settings {
    bool controls = true;          // 0: exactly the game's own controls, nothing here runs
    float grabRadiusM = 0.15f;     // a hand this close to a control can grab it
    bool stickTwist = false;       // 1: steer by twisting the stick about its axis (stage 1); 0: by tilting it
    float stickTiltDeg = 20.0f;    // tilt for full lock (and for a full pitch)
    float stickHeightM = 0.20f;    // the grip's height over the stick's pivot: the hand's travel from the grab, as a tilt (0.20: about
                                   // 7 cm of travel for full lock; 0.10 made 2 cm of drift a 10-degree tilt, 2026-11-03)
    float stickTwistDeg = 42.0f;   // twist for full lock
    float stickDeadZoneDeg = 4.0f; // twist or tilt that steers nothing
    float stickPitchDeadZoneDeg = 8.0f;  // forward/back tilt that pitches nothing (a hand drifts while it steers; the user, 2026-11-03)
    // Below this throttle the stick's steering turns the Mako on the spot: the throttle sent is exactly 0, which is when the tank
    // simulation counter-rotates its tracks; anything above drives the outer track and the Mako surges (the user, 2026-11-03).
    float steerInPlaceBelow = 0.2f;
    // Tilt and twist (the user, 2026-11-03): the tilt steers, limited to this share of full lock; the twist adds to it (up to full
    // lock, or against it) and alone turns the Mako on the spot with the lever at neutral (Output::spin).
    float tiltSteerMax = 0.5f;
    bool stickInvert = false;      // twist the other way
    float throttleTravelM = 0.055f; // hand travel from centre to full, along the seat's forward (the cockpit model's slot)
    bool leverReturns = true;      // let go, the lever goes back to neutral (the user, 2026-11-03); 0: it stays where it's left
    float throttleDetent = 0.15f;  // the lever's neutral notch, as a fraction of its travel
    // Where each control's grip sits from the head at placement, along the seat's axes (meters: right, up, forward): the
    // cockpit models' grips from the pilot's eye (research/48 section 22, the manifest's pilot.fromEyeMetres).
    XrVector3f leverOffset{-0.362f, -0.446f, 0.286f};
    XrVector3f stickOffset{0.375f, -0.415f, 0.314f};
    float stickAimRate = 90.0f;    // degrees a second at a full thumbstick, for the turret while the stick is held
    float rumble = 0.12f;          // the engine's rumble at full speed, in the holding hand (0: none)
    bool cameraFollow = true;      // while the mod drives, the chase camera keeps its angle to the vehicle (game/MakoControls)
};

// Why a drive ended: throttle and steering to zero, the holds dropped, the game's own controls back until the next grab.
enum class Stop : std::uint8_t {
    None,
    LeftMako,     // game mode != 1: on foot, a game menu, a cutscene, another pawn
    Menu,         // our VR main menu
    Calibration,  // the guided calibration
    Tracking,     // a controller lost tracking for over kTrackingGraceS
    Level,        // the vehicle went away with the level
    Unload,       // the mod is unloading
};
char const* Name(Stop stop);

struct HandInput {
    bool tracked = false;
    XrPosef grip{};
    float squeeze = 0.0f;
};

struct Seat {
    XrVector3f leverBase{};  // the lever at neutral
    XrVector3f stickBase{};
    XrVector3f forward{0.0f, 0.0f, -1.0f};  // flat, unit
    XrVector3f right{1.0f, 0.0f, 0.0f};     // flat, unit
};

struct Input {
    double deltaSeconds = 0.0;
    Stop stop = Stop::None;      // not None: stop now, nothing else counts
    HandInput left;
    HandInput right;
    bool headValid = false;
    XrPosef head{};
    // The cockpit's model is up (research/47): its grips, in tracking space this tick, are the seat instead of a placement from the
    // head (the hands grab what the eyes see; the user, 2026-11-03: "the holding visual looks completely off").
    bool haveModelSeat = false;
    Seat modelSeat{};
    bool haveRecenters = false;  // the host's recenter count below is real
    std::uint32_t recenters = 0;
    float engineSpeed = 0.0f;    // 0..1: for the rumble
    float stickX = 0.0f;         // the right thumbstick (the turret, while the stick is held)
    float stickY = 0.0f;
    // The jump jets fire: the stick's sideways tilt tips the Mako (Output::roll) instead of steering; the twist still steers
    // (research/47 "Flying on the jets": a tilt that also steered turned the Mako 88 degrees in a burst, its push swung behind).
    bool jets = false;
};

struct Haptic {
    int hand = 0;  // 0 left, 1 right
    float seconds = 0.0f;
    float amplitude = 0.0f;
};

struct Output {
    float throttle = 0.0f;   // -1 (full reverse) .. 1 (full forward), after the detent
    float lever = 0.0f;      // where the lever physically is, -1..1 (before the detent)
    float steer = 0.0f;      // -1 (full left) .. 1 (full right), + turns to the player's right
    float twistDeg = 0.0f;   // the stick's twist from the grab, unwrapped (+: the right-hand rule about the stick's axis)
    // The stick model's turn about its own axis (tilt mode; the user, 2026-11-05: the twist steered but the model never turned):
    // the hand's twist while held, held at stickTwistDeg (its full lock), springing back to 0 when let go. + counter-clockwise
    // from above, as twistDeg.
    float twistShownDeg = 0.0f;
    float tiltRightDeg = 0.0f;    // the stick's tilt (tilt mode): its top to the player's right
    float tiltForwardDeg = 0.0f;  // ... and away from the player
    float pitch = 0.0f;           // -1..1 from the forward tilt (+: pushed, the nose down), past the dead zone; 0 in twist mode
    float roll = 0.0f;            // -1..1 from the tilt to the right (+: the right side down) for the jump jets, past the same dead
                                  // zone as the pitch; 0 in twist mode
    float spin = 0.0f;            // -1..1 (+ right): the twist alone, for turning on the spot at zero throttle
    bool driving = false;    // a control has been grabbed in this drive: the mod's values go to the physics
    bool placed = false;     // the controls have a place
    bool leverHeld = false;
    bool stickHeld = false;
    bool leverNear = false;  // the left hand is within reach of the lever
    bool stickNear = false;  // the right hand is within reach of the stick
    bool leverGrabbed = false;  // this step
    bool stickGrabbed = false;  // this step
    bool replaced = false;      // this step: placed (again), from the head
    Stop stopped = Stop::None;  // this step: a drive ended, and why
    float aimYawDeg = 0.0f;     // the turret relative to the vehicle while the stick is held (+ right, degrees)
    float aimPitchDeg = 0.0f;   // (+ up)
    float rumbleLeft = 0.0f;
    float rumbleRight = 0.0f;
    Haptic haptics[6];
    int hapticCount = 0;
};

// The controls' place in tracking space, relative to the seat.
// Placed from the head's position and flat yaw. False (`out` untouched) for a head looking straight up or down.
bool PlaceSeat(XrPosef const& head, Settings const& settings, Seat& out);

// The hand's turn from `grab` to `now` about `axis` (unit, tracking space), degrees in (-180, 180]; anything else the hand
// did (the swing) doesn't count. The right-hand rule: + is counter-clockwise looking down the axis at the hand.
float TwistDegrees(XrQuaternionf const& grab, XrQuaternionf const& now, XrVector3f const& axis);
// The stick's axis at a grab: the tube the fingers close around (the grip pose's -Z: little finger to thumb).
XrVector3f StickAxis(XrQuaternionf const& grip);
// -1..1 (+ the player's right) from an unwrapped twist.
float SteerFromTwist(float twistDeg, Settings const& settings);
// -1..1 (+ the player's right, or pushed away) from a tilt in degrees: nothing inside `deadZoneDeg` (stickDeadZoneDeg by default),
// full at stickTiltDeg.
float FromTilt(float tiltDeg, Settings const& settings);
float FromTilt(float tiltDeg, Settings const& settings, float deadZoneDeg);

// The attitude the jump jets hold the Mako at while they fire (research/47 "Flying on the jets", 2026-11-04). They push along the
// Mako's own up (BioVehicleWheeled.Tick: its Z axis times ThrusterForce), so a tilted Mako drifts that way: the stick pushed tips
// the nose down (forward), pulled lifts it (back), tilted right drops the right side (right). Degrees in UE's signs (pitch + nose
// up, roll + right side down); level while the stick isn't held. The booster gets none of it: the game's own (the user).
struct JetAttitude {
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;
};
JetAttitude JetTarget(Output const& out, float maxPitchDeg, float maxRollDeg);
// The stick's tilt (degrees: to the right, forward) for the hand moved `travel` from where it grabbed, the grip `heightM` over the
// pivot: atan(travel / height) along the seat's right and forward.
void TiltFromTravel(XrVector3f const& travel, XrVector3f const& right, XrVector3f const& forward, float heightM, float& rightDeg, float& forwardDeg);
// -1..1 from the lever's position: nothing inside the detent, full at the ends.
float ThrottleFromLever(float lever, float detent);
// The turret's yaw and pitch (degrees) moved by the thumbstick for `seconds`: pitch clamped, yaw wrapped to (-180, 180].
void StepAim(float& yawDeg, float& pitchDeg, float stickX, float stickY, float degreesPerSecond, double seconds);

class Controls {
public:
    Output Step(Input const& input, Settings const& settings);
    // Where the turret starts when the stick is grabbed (the current aim relative to the vehicle): no jerk.
    void SeedAim(float yawDeg, float pitchDeg);
    float AimYaw() const { return aimYaw_; }
    float AimPitch() const { return aimPitch_; }
    Seat const& CurrentSeat() const { return seat_; }

private:
    void StopAll(Stop stop, Output& out);

    Seat seat_;
    bool placed_ = false;
    bool modelSeat_ = false;  // seat_ is the cockpit model's (Input::modelSeat)
    bool haveRecenters_ = false;
    std::uint32_t recenters_ = 0;
    bool driving_ = false;
    float lever_ = 0.0f;
    float steer_ = 0.0f;
    float spin_ = 0.0f;
    float twist_ = 0.0f;
    float twistRaw_ = 0.0f;
    float twistShown_ = 0.0f;
    XrQuaternionf grabOrientation_{0.0f, 0.0f, 0.0f, 1.0f};
    XrVector3f axis_{0.0f, 1.0f, 0.0f};
    XrVector3f stickGrab_{};
    float stickSettle_ = 0.0f;  // seconds left in which the stick's zero follows the hand
    float tiltRight_ = 0.0f;
    float tiltForward_ = 0.0f;
    float pitch_ = 0.0f;
    float roll_ = 0.0f;
    XrVector3f leverLast_{};
    bool leverHeld_ = false;
    bool stickHeld_ = false;
    bool leftDown_ = false;
    bool rightDown_ = false;
    bool leverInDetent_ = true;
    bool stickCentred_ = true;
    bool stickAtLock_ = false;
    float untrackedFor_ = 0.0f;
    float aimYaw_ = 0.0f;
    float aimPitch_ = 0.0f;
};

// ---- The picture (host) ----------------------------------------------------------------------------------------------

// What the mod publishes each tick (mod -> host, HostServices::setMakoControls).
struct View {
    std::uint32_t size = sizeof(View);  // the publisher's struct size: host and mod must match
    bool visible = false;               // in the Mako, the controls placed
    XrVector3f leverBase{};
    XrVector3f stickBase{};
    XrVector3f forward{0.0f, 0.0f, -1.0f};
    float lever = 0.0f;     // -1..1, where the lever is
    float steer = 0.0f;     // -1..1
    float travelM = 0.15f;  // the lever's travel from centre to full
    float detent = 0.08f;
    bool leverHeld = false;
    bool stickHeld = false;
    bool leverNear = false;
    bool stickNear = false;
    std::uint64_t sequence = 0;  // set by Publish
    std::uint64_t timeMs = 0;    // GetTickCount64 when published
};

void Publish(View const& view);
// The latest; `visible` false (and size 0) if none was published within `maxAgeMs` (the mod stopped).
View Latest(std::uint64_t maxAgeMs = 300);
void Reset();

// Their sizes in the room, lying flat at the seat (a slot and a dial on a console seen from above): the lever's quad long
// along the seat's forward, the stick's a square.
inline constexpr float kLeverWidthM = 0.06f;
inline constexpr float kLeverLengthM = 0.40f;
inline constexpr float kStickSizeM = 0.13f;
inline constexpr std::uint32_t kLeverPixelsWide = 48;
inline constexpr std::uint32_t kLeverPixelsLong = 320;
inline constexpr std::uint32_t kStickPixels = 128;

// A quad lying flat at `at`: its top edge toward `forward`, its front facing up.
XrPosef FlatPose(XrVector3f const& at, XrVector3f const& forward);

// Premultiplied, sRGB-encoded RGBA (the swapchain's format), row 0 toward the seat's forward.
std::vector<std::uint32_t> LeverPixels(float lever, float travelM, float detent, bool inReach, bool held);
std::vector<std::uint32_t> StickPixels(float steer, bool inReach, bool held);
// A number that changes when the picture would (the host uploads only then).
std::uint32_t LeverKey(float lever, float detent, bool inReach, bool held);
std::uint32_t StickKey(float steer, bool inReach, bool held);

// A short line for the log.
std::string Describe(Settings const& settings);

} // namespace mevr::vr::mako_controls
