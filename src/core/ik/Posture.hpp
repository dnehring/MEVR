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
};

struct ArbiterStep {
    CrouchAction action = CrouchAction::None;  // ask the game for this now, then call Applied
    bool external = false;      // the game's crouch changed without us since the last look (L3, a script)
    bool deferred = false;      // a physical change waits until the game takes it
    bool alreadyThere = false;  // a physical change found the game already in that posture
    bool gaveUp = false;        // a change the game didn't take after kMaxTries asks was dropped
};

class CrouchArbiter {
public:
    static constexpr int kMaxTries = 3;
    // Each tick while active.
    ArbiterStep Update(ArbiterInput const& input);
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

private:
    bool seen_ = false;
    bool last_ = false;
    bool owns_ = false;
    std::optional<bool> pending_;
    int tries_ = 0;
};

} // namespace mevr::ik::posture
