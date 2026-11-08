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
    std::optional<double> Player() const { return player_; }
    std::optional<double> Shepard() const { return shepard_; }
private:
    std::uintptr_t skeleton_ = 0;
    double quietSeconds_ = 0.0;
    double seconds_ = 0.0;
    double playerSum_ = 0.0;
    double shepardSum_ = 0.0;
    std::optional<double> player_;
    std::optional<double> shepard_;
};

} // namespace mevr::vr::floor_lock
