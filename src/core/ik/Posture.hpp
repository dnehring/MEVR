// SPDX-License-Identifier: MIT
#pragma once

#include <optional>

// Physical crouch (research/23): the player's real posture decides whether Shepard crouches. Pure
// state machines, testable offline; game/posture ties them to ME1 (BioPawn.ShouldCrouch).
//
// - How low the head is: the eyes' height as a fraction of the standing eye height. From the floor
//   (the host's STAGE / LOCAL_FLOOR measurement over the standing height learned from it), or without
//   a floor from how far the head dropped below where it was at the recenter (tracking space), over an
//   assumed standing eye height.
// - CrouchDetector: crouched below `crouchBelow` of the standing height, standing again above
//   `standAbove` (hysteresis), each only after the head stayed past the threshold for `dwellSeconds`.
// - CrouchArbiter: the physical posture acts on its changes only (edges), so the crouch button (L3, a
//   toggle) and the game's own changes in between are kept until the next physical change. Changes the
//   game can't take now (sprinting) wait; the game crouched by us is "owned", so a stand releases it.
namespace mevr::ik::posture {

// ---- How low the head is -----------------------------------------------------------------------

// The eyes' height above the floor over the standing eye height (both meters). Nullopt: no usable
// measurement (a standing height that isn't positive, or a value that isn't finite).
std::optional<double> FractionFromFloor(double eyeAboveFloor, double standingEyeHeight);
// Without a floor: the head's tracking-space height now and when the player stood (the recenter, or the
// highest it has been since), over an assumed standing eye height (meters).
std::optional<double> FractionFromRecenter(double headY, double standingHeadY, double assumedStandingEyeHeight);

// ---- Crouched or standing ------------------------------------------------------------------------

struct CrouchParams {
    double crouchBelow = 0.75;   // crouch when the eyes are below this fraction of the standing height
    double standAbove = 0.85;    // stand again above this fraction
    double dwellSeconds = 0.2;   // ...once the head has stayed past the threshold this long
};

// Clamped to sane values: crouchBelow 0.3..0.95, standAbove at least crouchBelow + 0.02 (at most 0.99),
// dwell 0..2 s.
CrouchParams Sanitize(CrouchParams params);

// A margin under standing ([Hands] crouchMinDropM, research/23 "A lean is not a crouch", lane M2W 2026-11-18; 0: off, the
// thresholds as given). The calibration's threshold sits half way down to the measured crouch: with the owner's shallow
// calibrated crouch (eyes 1.87 m standing, 1.56 m crouched) that is 0.92 of standing, 15 cm, and a reach toward someone
// (eyes 1.69 m) crouched Shepard. The crouch is moved down until the eyes must be at least `minDropMeters` under the standing
// height (`standingEye`, meters), but never below `crouchedFraction` (the calibrated crouch as a fraction of standing, when
// measured) plus kCrouchedReachMeters, so the calibrated crouch still crouches; the stand moves down with it, keeping the
// hysteresis gap it had. Then Sanitize.
inline constexpr double kCrouchedReachMeters = 0.03;
CrouchParams WithMinimumDrop(CrouchParams params, double standingEye, double minDropMeters,
    std::optional<double> crouchedFraction = std::nullopt);

// A lean is not a crouch ([Hands] crouchLeanRatio, research/23 "A lean is not a crouch"; 0: off, every drop counts). A real
// crouch takes the head down nearly straight; a reach toward someone takes it forward as far as down or further. The guard
// keeps where the head stood (horizontally, tracking space) while the eyes were in the standing band (at or above
// standAbove, or before a first sample), and calls the drop since then a lean while the head has moved horizontally at least
// `ratio` times as far as it dropped from the height it stood at. The detector's crouch then waits (its dwell starts over);
// standing up is never held back.
struct LeanSample {
    double headX = 0.0;   // the head in tracking space (meters): x and z horizontal
    double headZ = 0.0;
    std::optional<double> fraction;  // the eyes over the standing height (nullopt: no measurement)
    double standingEye = 0.0;        // meters
};
class LeanGuard {
public:
    // One sample. True while this drop is a lean (only with ratio > 0).
    bool Update(LeanSample const& sample, CrouchParams const& params, double ratio);
    // A recenter, a new session or pawn: the next sample anchors again.
    void Reset() { anchored_ = false; lean_ = false; }
    bool Lean() const { return lean_; }
    double Horizontal() const { return horizontal_; }  // meters from where the head stood, last sample
    double Drop() const { return drop_; }              // meters below the height it stood at, last sample

private:
    bool anchored_ = false;
    bool lean_ = false;
    double x_ = 0.0;
    double z_ = 0.0;
    double eye_ = 0.0;  // meters above the floor where it stood
    double horizontal_ = 0.0;
    double drop_ = 0.0;
};

enum class CrouchEvent { None, Crouch, Stand };

class CrouchDetector {
public:
    // One sample: the eyes' height as a fraction of the standing eye height. Nullopt: nothing measured
    // this tick (the state holds; the dwell starts over).
    CrouchEvent Update(std::optional<double> fraction, double deltaSeconds, CrouchParams const& params);
    bool Crouched() const { return crouched_; }
    // How long the head has been past the threshold that would change the state (seconds).
    double Dwell() const { return dwell_; }
    void Reset(bool crouched = false) {
        crouched_ = crouched;
        dwell_ = 0.0;
    }

private:
    bool crouched_ = false;
    double dwell_ = 0.0;
};

// ---- The physical posture vs the crouch button ---------------------------------------------------

enum class CrouchAction { None, Crouch, Stand };

struct ArbiterInput {
    CrouchEvent physical = CrouchEvent::None;  // this tick's physical change
    bool gameWants = false;                    // the pawn wants to crouch now (bWantsToCrouch)
    bool canApply = true;                      // the game takes a change now (not sprinting, ...)
    // Follow the player's posture ([Hands] crouchFollow; the owner, 2026-11-18: "the cover system needs to follow Shepard as
    // best we can"; research/23 "The crouch follows you"): a change the game makes without us that goes against the player's
    // real posture (physicalCrouched, the detector's state after this tick's sample) is put back, unless the crouch button
    // made it (button: L3 pressed within the caller's window; ME1's L3 is the game's own toggle). A put-back waits for
    // canApply and canRestore (the game's reason gone: the pawn standing still, no vault); the game undoing it again within
    // kInsistSeconds kInsistTimes times in a row wins (a script, a rule of the game's) until the next physical change.
    bool follow = false;
    bool physicalCrouched = false;
    bool button = false;
    bool canRestore = true;
    bool explained = false;   // the game's change has a passing reason the caller sees (moving, sprinting, a vault): no insist
    double nowSeconds = 0.0;  // a clock for the insist rule
};

struct ArbiterStep {
    CrouchAction action = CrouchAction::None;  // ask the game for this now, then call Applied
    bool external = false;      // the game's crouch changed without us since the last look (L3, a script)
    bool deferred = false;      // a physical change (or a put-back) waits until the game takes it
    bool alreadyThere = false;  // a physical change found the game already in that posture
    bool gaveUp = false;        // a change the game didn't take after kMaxTries asks was dropped
    bool restoring = false;     // this tick's external change went against the player's posture: it is put back (pending)
    bool insisted = false;      // ... but the game undid the put-back again: it wins until the next physical change
};

class CrouchArbiter {
public:
    static constexpr int kMaxTries = 3;
    static constexpr double kInsistSeconds = 1.5;
    static constexpr int kInsistTimes = 2;
    // Each tick while active.
    ArbiterStep Update(ArbiterInput const& input);
    // The crouch button changed the game's crouch for the player (the sequels' L3, made by the mod): kept until the next
    // physical change, as ME1's L3 (nothing pending, nothing ours, never put back).
    void Pressed(bool gameWantsNow);
    // The pending change is a put-back of the game's own change (ArbiterStep::restoring), not a physical one.
    bool Restoring() const { return restore_; }
    // The game's crouch as last seen (nullopt: nothing seen since Forget): a change this tick is one against it.
    std::optional<bool> LastSeen() const { return seen_ ? std::optional<bool>(last_) : std::nullopt; }
    // After the caller asked the game for the step's action (or released a crouch of ours): what the
    // pawn wants now.
    void Applied(bool gameWantsNow);
    // While inactive (a menu, a conversation, the Mako): only watches the game's crouch for changes made
    // without us; a waiting change is dropped.
    void Observe(bool gameWants);
    // The game's crouch is ours: we asked for it and nothing changed it since.
    bool Owns() const { return owns_; }
    std::optional<bool> Pending() const { return pending_; }
    // A new pawn (a level load): nothing is ours, nothing seen.
    void Forget();
    // A menu/recenter/focus transition cancels pending gestures, never the current
    // pawn's owned crouch. Reset the detector to that native baseline as one operation.
    void ResetTracking(CrouchDetector& detector, std::optional<bool> gameWants);
    // An owned release is complete only after the native bit is observed false.
    // Missing/true observations retain ownership for a later tick or unload retry.
    bool FinishRelease(std::optional<bool> gameWants);

private:
    bool seen_ = false;
    bool last_ = false;
    bool owns_ = false;
    std::optional<bool> pending_;
    int tries_ = 0;
    bool restore_ = false;       // pending_ puts back a change of the game's
    double restoredAt_ = -1.0;   // when the last put-back was asked (nowSeconds; < 0: none)
    int undone_ = 0;             // put-backs the game undid within kInsistSeconds, in a row
    bool givenUp_ = false;       // the game insisted: no put-backs until the next physical change
};

// ---- No cover snapping ----------------------------------------------------------------------------

// One rule for the three games (research/23; [Hands] noCover, the owner 2026-11-14: "We won't snap to cover for VR"): the
// game's cover acquisition is refused for the player whenever VR is on foot, decided once a game tick and kept until the next
// (ME1's native cover test, ME2's FindPlayerCover and ME3's FindCover answers read it). Nothing else lifts it: not a hand
// wheel, a screen on the wrist, a held view, the end of a roll or a cast, the host's controls or a late tick (ME3's refusal
// had asked for those and failed open: a cover press then snapped Shepard in, 2026-11-17).
struct CoverSnapInput {
    bool noCover = false;      // [Hands] noCover (or `mevr cover snap`)
    bool onFoot = false;       // the game's on-foot mode (0) with the player's pawn
    bool headTracked = false;  // a head-tracked world view: VR is on
    bool calibrating = false;  // the guided calibration runs (the game sees a neutral pad)
};
bool RefuseCoverSnap(CoverSnapInput const& in);

} // namespace mevr::ik::posture
