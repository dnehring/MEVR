#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The guided calibration (research/25): about a minute in the headset, started from the VR hands tab
// (plugin/Calibration draws it and captures the samples; the mod applies the results). Everything here
// is pure and unit-tested in tests/Main.cpp: the solve, the capture's stillness test, the step criteria,
// the layout of the panel, orbs and targets, and the text of the [Calibration] section.
//
// Tracking space: OpenXR conventions (meters, +X right, +Y up, -Z forward), Halcyon's recentered app
// space (the eyes' and controllers' space). "Frame" orientations follow OpenXR too: an object's forward
// is its -Z, its up +Y, its right +X.
//
// The idea: the player grabs an orb that stands for a gun and aims it naturally at three targets. Where
// they aim is the sight line: from the aiming eye to the target. The gun frame is that line (forward)
// with the world's up made perpendicular to it (no cant: roll can't be measured, the player holds the
// orb level). Expressed in the right controller's GRIP frame (not its aim pose, which differs per
// controller and runtime), it is what the gun's barrel should follow: grip * trim. The line's nearest
// point to the grip says where the gun's sight belongs relative to the hand. Stored per controller type,
// relative to the body and the controllers, never per gun: any gun is placed by its own sight geometry.
namespace mevr::vr::calibration {

// ---- Small rotation helpers (OpenXR quaternions: x, y, z, w; a * b applies b first) --------------------

XrQuaternionf Multiply(XrQuaternionf const& a, XrQuaternionf const& b);
XrQuaternionf Conjugate(XrQuaternionf const& q);
// Unit length; identity for a (near) zero or non-finite quaternion.
XrQuaternionf Normalize(XrQuaternionf const& q);
XrVector3f Rotate(XrQuaternionf const& q, XrVector3f const& v);
// The angle between two rotations, degrees (0..180).
float AngleDeg(XrQuaternionf const& a, XrQuaternionf const& b);
// The angle between two directions, degrees (0..180); 180 if either is (nearly) zero.
float DirectionAngleDeg(XrVector3f const& a, XrVector3f const& b);
// The rotation whose -Z is `forward` and whose +Y leans toward `up` (a gun held level). Nullopt if
// `forward` is (nearly) zero or runs along `up`.
std::optional<XrQuaternionf> LookRotation(XrVector3f const& forward, XrVector3f const& up = {0.0f, 1.0f, 0.0f});
// About +Y by `yaw` radians (positive turns -Z toward -X: left).
XrQuaternionf YawRotation(float yaw);
// The heading of an orientation's forward about +Y (radians, + left, 0 facing -Z).
float HeadingOf(XrQuaternionf const& orientation);
// The mean of rotations (each sign-aligned to the first, summed, normalized): close to the true mean for
// rotations a few degrees apart. Nullopt for none.
std::optional<XrQuaternionf> Mean(std::span<XrQuaternionf const> rotations);

// ---- The aim solve ------------------------------------------------------------------------------------

// One aim held still at a target. `right` (and `left` for the rifle) are GRIP poses.
struct AimSample {
    XrPosef right{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    bool hasLeft = false;
    XrPosef left{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrPosef head{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};  // the eyes' centre and the head's orientation
    XrVector3f eyes[2]{};    // the left and right eye
    XrVector3f target{};     // the target's centre
};

// Which eye aims: the one whose line to the target passes closest (sideways, perpendicular to the line
// and to up) over the right grip, on average over the samples.
struct EyeChoice {
    int eye = 1;              // 0 left, 1 right
    float lateral[2] = {};    // each eye's mean sideways distance from the grip (meters)
};
EyeChoice ChooseEye(std::span<AimSample const> samples);

// One stance: the pistol (the right hand alone) or the rifle (both hands, shouldered).
struct Stance {
    bool valid = false;
    int eye = 1;             // the eye the sight line runs from (0 left, 1 right)
    bool eyeMeasured = true; // false: forced by the caller (the [Calibration] eye override)
    int samples = 0;
    // The gun frame (-Z along the sight line, +Y the world's up made perpendicular to it) in the right
    // grip's frame: grip * trim is the gun's orientation.
    XrQuaternionf trim{0.0f, 0.0f, 0.0f, 1.0f};
    float spreadDeg = 0.0f;  // the largest angle of one sample's trim from the mean
    // The sight line's point nearest the right grip's origin, in the grip's frame (meters): where the
    // gun's sight belongs relative to the hand.
    XrVector3f line{};
    float eyeRelief = 0.0f;  // how far ahead of the aiming eye the grip's origin is, along the line (meters)
    // The rifle (both hands). In the gun frame (x right, y up, -z along the line), origin the right grip:
    bool hasLeft = false;
    XrVector3f foregrip{};                        // the left grip's position (meters)
    XrQuaternionf leftTrim{0.0f, 0.0f, 0.0f, 1.0f};  // the left grip's orientation in the gun frame
    XrVector3f handsLine{};                       // unit: from the right grip to the left grip
    float handsSpreadDeg = 0.0f;                  // the largest angle of one sample's hands line from the mean
    // The right grip from the eyes' centre, in the head's level frame (x right, y up, z back; meters):
    // where the stock sits.
    XrVector3f stock{};
};

// Solves a stance from its samples (normally three). `eye`: 0 or 1 forces that eye, -1 picks it
// (ChooseEye). Invalid without samples or when a sample's sight line is degenerate.
Stance Solve(std::span<AimSample const> samples, int eye = -1);

// The three aims agree: the spread stays within `maxSpreadDeg`. The pistol's: the controller is the gun. The
// rifle's is wider: two hands hold an imaginary gun, so the right wrist (whose trim this measures) has nothing
// rigid to hold it the same way at each target.
inline constexpr float kMaxSpreadDeg = 3.0f;
inline constexpr float kMaxRifleSpreadDeg = 4.5f;

// ---- A step's three aims, with the redo rules (research/25 section 2) -------------------------------------
//
// The aims are kept per target. When all three are in, Finish() judges them:
//  - within the spread: accepted;
//  - otherwise the aim farthest from the other two (the one whose removal leaves the most consistent pair) is
//    the outlier: if the other two agree, only its target is aimed again; if they don't either, all three;
//  - from the second failed judgement on, a pair that agrees is accepted (the outlier left out, with a note),
//    so the step can't go round forever.
struct Review {
    enum class Result { Accepted, AcceptedPair, RedoOne, RedoAll };
    Result result = Result::RedoAll;
    Stance stance;           // Accepted / AcceptedPair: the stance to keep
    float spreadDeg = 0.0f;  // all three aims' spread
    int outlier = -1;        // the target whose aim is farthest from the other two (-1: not judged)
    float outlierDeg = 0.0f; // its trim's angle from the other two's mean
    float pairSpreadDeg = 0.0f;  // the other two's spread
    int failures = 0;        // failed judgements in this step so far, this one included
};
char const* ResultName(Review::Result result);  // "accepted", "accepted-pair", "redo-one", "redo-all"

class AimSet {
public:
    void Reset();                    // a new step: every target open, no failures
    bool Has(int target) const;
    int Count() const;
    int Next() const;                // the first open target (0..2), -1 when all are taken
    void Add(int target, AimSample const& sample);
    int Back();                      // removes the aim taken last; its target, or -1 if none
    std::vector<AimSample> Samples() const;  // in target order
    int Failures() const { return failures_; }
    // All three taken: judged against `maxSpreadDeg` (eye as Solve's). Clears what has to be aimed again.
    Review Finish(int eye, float maxSpreadDeg);

private:
    std::optional<AimSample> slots_[3];
    int order_[3] = {-1, -1, -1};  // targets in the order taken
    int taken_ = 0;
    int failures_ = 0;
};

// ---- Applying a stance (the mod; the check step) --------------------------------------------------------

struct Line {
    XrVector3f point{};
    XrVector3f direction{0.0f, 0.0f, -1.0f};  // unit
};
// The calibrated sight line for a right grip pose: through grip * line, along the gun's forward.
Line SightLine(XrPosef const& grip, Stance const& stance);
// The gun's orientation for a grip orientation: grip * trim.
XrQuaternionf GunOrientation(XrQuaternionf const& grip, Stance const& stance);
// The move (perpendicular to the line) that puts `point` on `line`, at most `maxLength` long.
XrVector3f ShiftOntoLine(XrVector3f const& point, Line const& line, float maxLength);
// Where `line` meets the plane through `planePoint` with normal `planeNormal`, if at least `minDistance`
// ahead; else `fallback` meters along it.
XrVector3f OnPlane(Line const& line, XrVector3f const& planePoint, XrVector3f const& planeNormal, float minDistance,
    float fallback);

// ---- Body measures --------------------------------------------------------------------------------------

// Shoulder width from both arms held straight ahead: the grips' distance across the body (perpendicular
// to `forward`, horizontal). Arm length from a T pose's grip span and that width: (span - width) / 2.
float ShoulderWidth(XrVector3f const& left, XrVector3f const& right, XrVector3f const& forward);
std::optional<float> ArmLength(float tPoseSpan, float shoulderWidth);

// Physical crouch thresholds from the measured standing and crouched eye heights (meters): crouch below
// the point `crouchAt` of the way down from standing to crouched, stand again above `standAt` of the way
// (fractions of the standing height, as core/ik/Posture's CrouchParams wants them). Nullopt unless the
// crouch is at least 0.15 m below standing. Clamped like ik::posture::Sanitize (crouch 0.3..0.92,
// stand at least 0.02 above).
struct CrouchFractions {
    double crouchBelow = 0.75;
    double standAbove = 0.85;
};
std::optional<CrouchFractions> CrouchFromHeights(double standingEye, double crouchedEye, double crouchAt = 0.5,
    double standAt = 0.35);

// ---- Capture: hold still ------------------------------------------------------------------------------------

struct StillSettings {
    float position = 0.025f;  // every pose within this of where it was when the hold began (meters)...
    float angleDeg = 4.0f;    // ...and this much rotation
    float seconds = 1.0f;     // held this long: captured
};
// A shouldered rifle held with no rifle: two arms and the head, the left arm out, sway more than a pistol.
inline constexpr StillSettings kRifleStill{0.04f, 6.0f, 1.0f};

// Up to three poses (head, right grip, left grip) held still. A pose that moves too far starts the hold
// over from where everything is now. Averages each pose over the hold (positions mean, rotations
// sign-aligned mean), so the sample is steadier than any one frame.
class Stillness {
public:
    static constexpr int kMaxPoses = 3;
    void Reset();
    // Each frame. `poses[i]` with `used[i]` false is ignored. Returns the seconds held so far.
    float Update(XrPosef const poses[kMaxPoses], bool const used[kMaxPoses], float dt, StillSettings const& settings);
    float Held() const { return held_; }
    // Which pose broke the last hold (0 head, 1 right, 2 left), -1 if none did (the first frame, or a pose
    // joining or leaving); and how long the broken hold had lasted.
    int Moved() const { return moved_; }
    float Broke() const { return broke_; }
    // The average of pose `i` since the hold began (the latest pose if none).
    XrPosef Average(int i) const;

private:
    int moved_ = -1;
    float broke_ = 0.0f;
    bool started_ = false;
    bool used_[kMaxPoses] = {};
    XrPosef reference_[kMaxPoses]{};
    double positionSum_[kMaxPoses][3] = {};
    double rotationSum_[kMaxPoses][4] = {};
    int frames_ = 0;
    float held_ = 0.0f;
};

// ---- Step criteria (each checked against the latest poses) ------------------------------------------

// Looking ahead: the head's pitch within `maxPitchDeg` of level.
bool LookingAhead(XrQuaternionf const& head, float maxPitchDeg = 15.0f);
// A T pose: the grips at least `minSpan` apart, the head between them (with room for the shoulders), each
// grip within 0.35 m of the head's height.
bool TPose(XrVector3f const& head, XrVector3f const& left, XrVector3f const& right, float minSpan = 1.0f);
// Both arms straight ahead: each grip at least `minAhead` in front of the head along `forward`
// (horizontal), between 0.6 m below and 0.15 m above the head, and the grips 0.1-0.8 m apart.
bool ArmsForward(XrVector3f const& head, XrVector3f const& left, XrVector3f const& right, XrVector3f const& forward,
    float minAhead = 0.45f);
// Crouched: the eyes at least `minDrop` below the standing height.
bool Crouched(float standingHeight, float height, float minDrop = 0.25f);
// Aiming at a target (any gun frame is unknown yet): the head looks within `maxGazeDeg` of the target,
// and the right grip sits on the line of sight, within `maxOffLine` of it and at least `minAhead` ahead
// of the eyes along it.
bool OnTarget(XrPosef const& head, XrVector3f const& grip, XrVector3f const& target, float maxGazeDeg = 20.0f,
    float maxOffLine = 0.18f, float minAhead = 0.12f);

// Why an aim isn't being captured (the panel says it, the log records it). Checked in this order.
enum class AimProblem {
    None,
    LookAtTarget,   // the head doesn't look at the lit target
    PistolOffLine,  // pistol: the hand isn't on the line of sight
    PistolTooClose, // pistol: the hand isn't held out ahead of the eyes
    NotShouldered,  // rifle: the right hand isn't at the shoulder, by the cheek
    HandsTooClose,  // rifle: the left hand isn't out along the rifle
    NotPointing,    // rifle: the line from the right hand through the left doesn't point at the target
};
char const* ProblemName(AimProblem problem);  // for the log: "look-at-target", ...
struct AimCheck {
    AimProblem problem = AimProblem::None;
    float gazeDeg = 0.0f;   // the head's forward from the eyes-to-target line
    float offLine = 0.0f;   // pistol: the grip's distance from the line of sight (meters)
    float ahead = 0.0f;     // pistol: the grip ahead of the eyes along it (meters)
    float stock = 0.0f;     // rifle: the right grip's distance from the eyes (meters)
    float span = 0.0f;      // rifle: the left grip's distance from the right (meters)
    float pointDeg = 0.0f;  // rifle: the right-to-left line's angle from the right grip's line to the target
};
struct PistolLimits {
    float maxGazeDeg = 20.0f;
    float maxOffLine = 0.18f;
    float minAhead = 0.12f;
};
// The rifle's stance as a rifle is really held (research/25 section 2): looking at the target, the right hand at
// the shoulder by the cheek, the right-to-left line pointing roughly at the target. The left hand sits wherever
// the player holds a foregrip: below the sight line and to the side is fine (it isn't on the line of sight).
struct RifleLimits {
    float maxGazeDeg = 22.0f;
    float maxStock = 0.40f;   // the right grip within this of the eyes' centre
    float minSpan = 0.12f;    // the left grip at least this far from the right
    float maxPointDeg = 22.0f;
};
AimCheck CheckPistolAim(XrPosef const& head, XrVector3f const& grip, XrVector3f const& target, PistolLimits const& limits = {});
AimCheck CheckRifleAim(XrPosef const& head, XrVector3f const& right, XrVector3f const& left, XrVector3f const& target,
    RifleLimits const& limits = {});

// A grip near an orb takes it (meters): generous, a reach in the dark (the player's hand isn't drawn while the
// calibration runs, only a marker at the grab point). The foregrip's more so.
inline constexpr float kGrabReach = 0.16f;
inline constexpr float kForegripGrabReach = 0.22f;
// The point a hand grabs with: `offset` (meters, in the right grip's frame; mirrored across x for the left hand)
// from the grip pose's origin. OpenXR puts that origin in the palm; [Calibration] grabOffset moves it if a runtime
// doesn't (the marker shows where it is).
XrVector3f GrabPoint(XrPosef const& grip, XrVector3f const& offset, bool leftHand);
inline constexpr float kSqueezeOn = 0.6f;
inline constexpr float kSqueezeOff = 0.35f;

// ---- Layout (world-locked from where the player stood at step 1) ---------------------------------------

struct Anchor {
    XrVector3f eyes{};  // the eyes' centre
    float yaw = 0.0f;   // the heading the sequence is laid out on (radians, + left, 0 facing -Z)
};
// A point `right`, `up`, `forward` meters from the anchor's eyes, in its level frame.
XrVector3f AnchorPoint(Anchor const& anchor, float right, float up, float forward);
XrVector3f AnchorForward(Anchor const& anchor);
XrVector3f AnchorRight(Anchor const& anchor);

inline constexpr int kTargets = 3;
inline constexpr float kBoardDistance = 5.0f;  // meters ahead
inline constexpr float kBoardSize = 4.4f;       // the target board's quad, square (meters)
// The targets on the board (meters from its centre: x right, y up): centre, upper left, lower right,
// about 17 and 15 degrees off.
inline constexpr float kTargetX[kTargets] = {0.0f, -1.5f, 1.5f};
inline constexpr float kTargetY[kTargets] = {0.0f, 1.35f, -1.35f};
inline constexpr float kTargetRadius = 0.22f;   // the outer ring (meters): 5 degrees across at 5 m
inline constexpr float kDotSize = 0.24f;        // the check dot's quad on the board (meters; its core is 0.8 degrees)
// The board's centre and orientation (facing back toward the anchor).
XrPosef BoardPose(Anchor const& anchor);
XrVector3f TargetPosition(Anchor const& anchor, int index);

// Where the instruction panel goes: straight ahead and a little down for the body steps, or to the lower
// left (out of the targets' way) for the aiming steps. Facing the anchor's eyes, upright.
XrPosef PanelPose(Anchor const& anchor, bool aiming);
inline constexpr float kPanelWidth = 0.84f;  // meters (the height follows the canvas' aspect)

// The orbs, relative to where the eyes were when the grab step began (on the anchor's heading). The rifle's: the
// rear one where the right hand carries a rifle at low ready, the front one a foregrip's distance ahead of it along
// the bore, a little lower and inboard, where the support hand sits; the distance from the player's arms when steps
// 2-3 measured them (ForegripDistance).
enum class Orb { Pistol = 0, RifleRear = 1, RifleFront = 2 };
struct BodyMeasures {
    std::optional<float> armLength;      // shoulder to grip (meters)
    std::optional<float> shoulderWidth;  // across the body (meters)
};
// How far ahead of the right hand the left one holds a rifle: 0.29 m unmeasured, else 0.40 of the arm (0.24-0.32 m),
// shortened until the left hand reaches it with its arm bent (0.85 of the arm from the left shoulder); at least 0.2 m.
float ForegripDistance(BodyMeasures const& body);
XrVector3f OrbRest(Anchor const& stepAnchor, Orb orb, BodyMeasures const& body = {});
inline constexpr float kOrbSize = 0.085f;      // the orb's quad (meters; the ball is 0.58 of it, 5 cm, with a glow)
inline constexpr float kHeldOrbSize = 0.05f;   // ...once in the hand: small and pale, so it isn't a sight to align
inline constexpr float kHandMarkerSize = 0.045f;  // the ring at each hand's grab point (meters)

// A square quad facing `eye`, `size` meters across, centred on `center`.
XrPosef Billboard(XrVector3f const& center, XrVector3f const& eye);

// ---- The [Calibration] text -----------------------------------------------------------------------------

std::string FormatVector(XrVector3f const& v);      // "0.0123, -0.0456, 0.0789"
std::string FormatRotation(XrQuaternionf const& q);  // "x, y, z, w"
std::optional<XrVector3f> ParseVector(std::string_view text);
std::optional<XrQuaternionf> ParseRotation(std::string_view text);  // normalized
// The section key for a controller type: the interaction profile's vendor and name, and the system's name
// (headset), lower case, anything but letters and digits as '_', e.g. "oculus_touch_controller__meta_quest_3".
std::string ProfileKey(std::string_view interactionProfile, std::string_view systemName);
// A short readable name for the panel: "Touch controller (Meta Quest 3)".
std::string ProfileLabel(std::string_view interactionProfile, std::string_view systemName);
// A comma-separated list (the [Calibration] dismissed keys) contains `key`.
bool ListHas(std::string_view list, std::string_view key);

// ---- Host -> mod (HostServices v15) ---------------------------------------------------------------------

enum HostFlags : std::uint32_t {
    kCalibrating = 1u,  // the sequence runs: the mod stands down (no holsters, grenades, melee, squad, grip, crouch)
};
// Plain data across the host/mod boundary: fields only ever appended (HostServices::calibrationStateSize).
struct HostState {
    std::uint32_t size = sizeof(HostState);
    std::uint32_t flags = 0;
    std::uint32_t saves = 0;     // counts saved calibrations: the mod re-reads [Calibration] when it changes
    std::uint32_t reserved = 0;
    char key[96] = {};           // the current controller type's section key (ProfileKey); "" unknown
};

} // namespace mevr::vr::calibration
