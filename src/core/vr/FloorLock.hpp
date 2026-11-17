// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Floor.hpp"

#include <cstdint>
#include <optional>

namespace mevr::vr::floor_lock {

// Copied on the game thread through reflection; no engine pointers cross into the renderer.
struct PawnSample {
    bool onFoot = false;
    bool pawn = false;
    bool cover = true;       // failed reflection stands down
    bool cameraOwned = false;
    bool floorValid = false;
    double floorZ = 0.0;     // Actor.Location.z - CylinderComponent.CollisionHeight
    std::uint64_t timeMs = 0;
    // Extended size-checked payload, no new HostServices callback. Never the measured seated height.
    bool seated = false;
    // A native traversal move plays (ME2's vault: game/Traversal's Moving()): the pawn's place steps while the game's
    // camera stays smooth, so the host holds its correction (MoveHold). In the padding after `seated`: the size is the same.
    bool traversalMove = false;
    double standingEyeM = 0.0; // calibrated standing eye, else frozen standing Shepard; 0 while learning
};

struct Context {
    bool enabled = true;
    bool world = false;
    bool firstPerson = false;
    bool cutscene = false;
    bool screen = false;
    bool frontEnd = false;
    bool galaxyCamera = false;
    bool menu = false;
    PawnSample pawn;
    double heightOffsetM = 0.0; // manual view delta, applied after the standing/seated height solve
};

bool Applies(Context const& context);
bool Fresh(std::uint64_t sampleMs, std::uint64_t nowMs);
bool ValidFloor(floor::State const& sample, std::uint64_t nowMs);

struct Result {
    bool active = false;
    double correction = 0.0;
    double centreZ = 0.0;
};
// rawCameraZ is BEFORE any common or per-eye translation. commonWorldZ includes ALL framing/lean terms.
Result Correct(Context const& context, floor::State const& sample, std::uint64_t nowMs,
    double rawCameraZ, double commonWorldZ, double unitsPerMeter, std::optional<double> seatedEyeM = std::nullopt);

// While a native traversal move plays (PawnSample::traversalMove) the pawn's floor is held where it stood relative to the
// game's camera on the tick before the move began, and the eye is solved from that (the headset's live eye height still
// counts): the animation steps the pawn's place (0.8-0.9 m at a time in ME2's vault) while the game's camera rides smoothly on
// its bone, so a correction solved from the pawn's floor would move the eye in those steps and away from Shepard's head (arm
// IK then took the view for third person: gotchas/floor-lock-through-a-traversal-move). When the move ends the game's camera
// is still settling (ME2's vault: 52 units high, at rest 0.8 s later), so the hold lasts until the pawn's floor is back within
// `releaseM` of the held one (the eye then joins the fresh solve with a step of at most that), and past `waitS` it eases there
// at `easeMS`. No move, no change (ME1). `eligible`: the lock is enabled in an ordinary first-person world with a pawn (the
// move itself may fail other gates, e.g. its camera mode); not eligible releases at once.
struct MoveHoldSettings {
    double releaseM = 0.02;
    double waitS = 1.5;
    double easeMS = 0.5;
};
struct MoveInput {
    Result fresh;               // Correct's result this frame
    bool moving = false;        // PawnSample::traversalMove
    bool eligible = false;
    double rawCameraZ = 0.0;
    double commonWorldZ = 0.0;
    double floorZ = 0.0;        // the pawn's floor Correct used (Context::pawn.floorZ)
    double unitsPerMeter = 100.0;
    std::uint64_t nowMs = 0;
};
class MoveHold {
public:
    enum class Phase { None, Moving, Settling };
    Result Step(MoveInput const& in, MoveHoldSettings const& settings = {});
    Phase State() const { return phase_; }
    bool Holding() const { return phase_ != Phase::None; }
    double Held() const { return correction_; }          // the correction last answered while holding
    double FloorGap() const { return gap_; }             // the held floor below the game's camera (units, negative)
    double SettleGap() const { return settleGap_; }      // while settling: the fresh floor's distance from the held one
    bool Eased() const { return eased_; }                // the settle went past waitS and eased
    std::uint64_t EndedMs() const { return endedMs_; }   // the move's end (Settling)
    void Reset() { *this = {}; }
private:
    Phase phase_ = Phase::None;
    bool haveLast_ = false;
    double gap_ = 0.0;         // floorZ - rawCameraZ, held (outside a move: the last active one)
    double eye_ = 0.0;         // fresh centreZ - floorZ: the eye's height over the floor, live while the fresh solve is active
    double correction_ = 0.0;
    double settleGap_ = 0.0;
    bool eased_ = false;
    bool wasMoving_ = false;
    std::uint64_t endedMs_ = 0;
    std::uint64_t lastMs_ = 0;
};

// A standing reference settles once per skeleton. Neither kind of crouch can contribute; the live eye is never a target.
class StandingReference {
public:
    void Feed(std::uintptr_t skeleton, bool physicalCrouch, bool gameCrouch, bool holstered,
        double stageEyeM, double shepardEyeM, double dt, bool seated = false);
    // Shepard's standing eye height saved from an earlier run for this skeleton's mesh: Shepard() answers it at once, while
    // Feed still measures; Measured() is this run's own value once settled (it then replaces the saved one). A seed for
    // another skeleton than the next Feed's is dropped with the rest. Outside 0.8..2.5 m: ignored.
    void Seed(std::uintptr_t skeleton, double shepardEyeM);
    std::optional<double> Player() const { return player_; }
    std::optional<double> Shepard() const { return shepard_; }
    std::optional<double> Measured() const { return measured_; }
    bool Seeded() const { return seeded_; }
private:
    std::uintptr_t skeleton_ = 0;
    bool seeded_ = false;
    std::optional<double> measured_;
    double quietSeconds_ = 0.0;
    double seconds_ = 0.0;
    double playerSum_ = 0.0;
    double shepardSum_ = 0.0;
    std::optional<double> player_;
    std::optional<double> shepard_;
};

} // namespace mevr::vr::floor_lock
