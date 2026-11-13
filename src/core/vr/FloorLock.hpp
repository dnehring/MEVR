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
