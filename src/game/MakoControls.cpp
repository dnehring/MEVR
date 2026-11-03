// SPDX-License-Identifier: MIT
#include "game/MakoControls.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CallFunction.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/BodyCalibration.hpp"
#include "game/CockpitProbe.hpp"
#include "game/HandCalibration.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <numbers>
#include <string>

namespace mevr::game::mako_controls {
namespace {

namespace json = vr::test_request;
namespace mc = vr::mako_controls;

constexpr ULONGLONG kSetInputsFreshMs = 200;  // SetInputs not seen for this long: the mod's tick writes instead
constexpr float kRumbleHz = 60.0f;
constexpr float kRumbleSeconds = 0.1f;   // asked again each tick, so it stops by itself if the mod stalls
constexpr double kStickRayUp = 150.0;    // the thumbstick's ray starts this far above the vehicle's origin (units): about the turret
constexpr double kDegrees = 180.0 / std::numbers::pi;
// SetRBAngularVelocity takes radians a second (PhysX's units; the body is capped near 7 rad/s): degrees made both rate
// controllers 57 times too strong, and the spin swung between +200 and -164 deg/s for 60 asked (2026-11-03, the headset).
constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180.0f;

// A world angular velocity (degrees a second, about UE's axes) as SetRBAngularVelocity needs it: radians, and X and Z negated.
// The engine converts the vector to PhysX like a position (Y mirrored), but a rotation axis under a mirror also changes sign,
// so turns about world X and Z came out reversed while Y was right (2026-11-03, the headset: the "level" stabiliser rolled the
// Mako to 180 degrees and the spin ran at +166 deg/s for -17 asked; the one pitch test had happened to face along X).
ue3::Vector AngularVelocity(double x, double y, double z) {
    return ue3::Vector{static_cast<float>(-x * kRadiansPerDegree), static_cast<float>(y * kRadiansPerDegree),
        static_cast<float>(-z * kRadiansPerDegree)};
}

#if defined(MEVR_GAME_LE1)
// Our reflection layer (ue3/, research/34); every row is checked by engine/ReflectCheck.
ue3::FunctionRef gSetInputs{"SFXGame.BioVehicleBase.SetInputs"};
ue3::FieldRef gThrottle{"Engine.Vehicle", "Throttle"};
ue3::FieldRef gSteering{"Engine.Vehicle", "Steering"};
ue3::FieldRef gRotation{"Engine.Actor", "Rotation"};
ue3::FieldRef gLocation{"Engine.Actor", "Location"};
ue3::FieldRef gBehavior{"SFXGame.BioVehicleBase", "m_oBehavior"};
ue3::FieldRef gPowertrain{"SFXGame.BioVehicleBehaviorBase", "m_bPowertrainEnabled"};
ue3::FieldRef gPaused{"SFXGame.BioVehicleBehaviorBase", "m_bIsPaused"};
ue3::FieldRef gSimObj{"Engine.SVehicle", "SimObj"};
ue3::FieldRef gMaxSpeed{"Engine.SVehicle", "MaxSpeed"};
ue3::FieldRef gForwardVel{"Engine.SVehicle", "ForwardVel"};
ue3::FieldRef gOutputGas{"Engine.SVehicle", "OutputGas"};
ue3::FieldRef gOutputSteering{"Engine.SVehicle", "OutputSteering"};
ue3::FieldRef gOutputBrake{"Engine.SVehicle", "OutputBrake"};
ue3::ClassRef gSimTank{"Engine.SVehicleSimTank"};
ue3::ClassRef gBioSimTank{"SFXGame.BioSVehicleSimTank"};
ue3::ClassRef gSimCar{"Engine.SVehicleSimCar"};
ue3::FieldRef gActualSteering{"Engine.SVehicleSimCar", "ActualSteering"};
ue3::FieldRef gLeftTrackVel{"Engine.SVehicleSimTank", "LeftTrackVel"};
ue3::FieldRef gRightTrackVel{"Engine.SVehicleSimTank", "RightTrackVel"};
ue3::FieldRef gTurnInPlace{"Engine.SVehicleSimTank", "TurnInPlaceThrottle"};
ue3::FieldRef gCurrentInputYaw{"SFXGame.BioSVehicleSimTank", "m_fCurrentInputYaw"};
ue3::FieldRef gCameraRelative{"SFXGame.BioPlayerController", "bUseCameraRelativeVehicleSteering"};
// The thrusters (the user, 2026-11-03): their fuel burns slower while the player drives, and the stick's forward and back tilt
// pitches the Mako while they fire (the vehicle's rigid body, SetRBAngularVelocity added to the current).
// BioVehicleWheeled.Tick fires the jump jets while BioPlayerController.bJumpJetsOn (X) and the booster while bBoostersOn (Y), and
// its UpdateFuel burns CurrentJumpFuel / CurrentBoostFuel by the frame's time while they're on (MaxJumpFuel seconds of jets; the
// ThrusterBurnRate and BoosterBurnRate fields are 0 and unused). Longer: part of each burn given back.
ue3::FieldRef gJumpJetsOn{"SFXGame.BioPlayerController", "bJumpJetsOn"};  // `mevr vehicle jets`: held on for a test
ue3::FieldRef gJumpFuel{"SFXGame.BioVehicleWheeled", "CurrentJumpFuel"};
ue3::FieldRef gBoostFuel{"SFXGame.BioVehicleWheeled", "CurrentBoostFuel"};
ue3::FieldRef gMaxJumpFuel{"SFXGame.BioVehicleWheeled", "MaxJumpFuel"};
ue3::FieldRef gMaxBoostFuel{"SFXGame.BioVehicleWheeled", "MaxBoostFuel"};
ue3::FieldRef gJumping{"SFXGame.BioVehicleWheeled", "bJumping"};
ue3::FieldRef gBoosting{"SFXGame.BioVehicleWheeled", "bBoosting"};
ue3::FieldRef gVehicleMesh{"Engine.Pawn", "Mesh"};
ue3::FunctionRef gSetAngularVelocity{"Engine.PrimitiveComponent.SetRBAngularVelocity"};
#endif

bool gStarted = false;
bool gAvailable = false;    // the fields it writes were found
bool gInMako = false;       // this tick
mc::Settings gSettings;
mc::Controls gControls;
mc::Output gOut;
ue3::Object* gVehicle = nullptr;
ue3::Object* gVehicleBehavior = nullptr;
int gLastYaw = 0;
bool gHaveYaw = false;
float gSteeringSent = 0.0f;  // what goes into Vehicle.Steering (+ left, as the game's own)
bool gViewShown = false;
ULONGLONG gSetInputsSeenMs = 0;
bool gLoggedSim = false;
bool gLoggedSeamCall = false;
bool gLoggedSeamEvent = false;
bool gLoggedSeamTick = false;
bool gLoggedLever = false;
bool gLoggedStick = false;
bool gLoggedDrive = false;
bool gLoggedSettings = false;
std::string gSimClass;
bool gSimIsTank = false;

ULONGLONG gJetsUntilMs = 0;  // `mevr vehicle jets <seconds>`: bJumpJetsOn held on until then
ue3::Object* gJetsController = nullptr;
bool gLoggedFuel = false;

// Since the heartbeat.
struct Counts {
    unsigned handlerCalls = 0;   // SetInputs, through CallFunction
    unsigned handlerEvents = 0;  // ...through ProcessEvent
    unsigned written = 0;        // our values went in
    unsigned gated = 0;          // SetInputs had zeroed them: the powertrain off, or paused
    unsigned tickWrites = 0;     // no SetInputs seen: written from the mod's tick
    int cameraUnits = 0;         // the vehicle's yaw given to the camera
    unsigned jets = 0;           // ticks with the jump jets on
    unsigned boost = 0;          // ...with the booster on
    unsigned held = 0;           // ...of them stabilised
    float refunded = 0.0f;       // fuel given back (seconds of thrust)
    float worstPitchDeg = 0.0f;  // the largest |pitch error| and |roll| seen while stabilising
    float worstRollDeg = 0.0f;
    float targetPitchDeg = 0.0f; // the last target
    unsigned spins = 0;          // spins on the spot begun
    unsigned spinTicks = 0;      // ticks spinning
    float spinRateDeg = 0.0f;    // the last yaw rate seen, and the one asked for
    float spinTargetDeg = 0.0f;
} gCounts;

#if defined(MEVR_GAME_LE1)

int SignedUnits(int units) {
    int const u = units & 0xFFFF;
    return u >= 0x8000 ? u - 0x10000 : u;
}

// What the game's own SetInputs would let through: it zeroes the inputs with the powertrain off or the vehicle paused.
bool Powered() {
    if (gVehicleBehavior == nullptr) {
        return false;
    }
    return gPowertrain.ReadBool(gVehicleBehavior).value_or(false) && !gPaused.ReadBool(gVehicleBehavior).value_or(true);
}

// While the jets or the booster fire, (1 - scale) of the frame's burn goes back into their fuel, so they last 1/scale as long;
// their regeneration is the game's.
void RefundFuel(ue3::Object* vehicle, double deltaSeconds, float scale) {
    if (!gLoggedFuel) {
        gLoggedFuel = true;
        log::Info("mako controls: the thrusters: {:.1f} s of jump jets and {:.1f} s of booster at full fuel, {}",
            gMaxJumpFuel.Read<float>(vehicle).value_or(-1.0f), gMaxBoostFuel.Read<float>(vehicle).value_or(-1.0f),
            scale < 1.0f ? std::format("burning {:.2f} as fast: {:.1f} times as long ([Mako] thrusterBurnScale)", scale, 1.0f / scale) : std::string("as the game burns them"));
    }
    if (!(scale < 1.0f)) {
        return;
    }
    float const back = (1.0f - std::max(scale, 0.0f)) * static_cast<float>(std::clamp(deltaSeconds, 0.0, 0.1));
    auto const refund = [&](ue3::FieldRef& firingField, ue3::FieldRef& fuelField, ue3::FieldRef& maxField, char const* what) {
        if (!firingField.ReadBool(vehicle).value_or(false)) {
            return;
        }
        float* const fuel = fuelField.In<float>(vehicle);
        float const max = maxField.Read<float>(vehicle).value_or(0.0f);
        if (fuel == nullptr || !(*fuel > 0.0f)) {
            return;  // ran out: the game's own refill decides
        }
        *fuel = std::min(*fuel + back, max);
        write_journal::Wrote(what, fuel);
        gCounts.refunded += back;
    };
    refund(gJumping, gJumpFuel, gMaxJumpFuel, "MakoControls.jumpFuel");
    refund(gBoosting, gBoostFuel, gMaxBoostFuel, "MakoControls.boostFuel");
}

// `mevr vehicle jets`: the X button's bJumpJetsOn held on (the player input clears it each frame without a pad).
void HoldJets(ue3::Object* controller) {
    if (controller == nullptr || GetTickCount64() >= gJetsUntilMs) {
        return;
    }
    if (std::uint8_t* const on = gJumpJetsOn.In<std::uint8_t>(controller)) {
        *on = 1;
        write_journal::Wrote("MakoControls.bJumpJetsOn", on);
    }
}

// Spinning on the spot (the user, 2026-11-03). With the throttle at exactly 0 and steering, BioSVehicleSimTank applies its own
// TurnInPlaceThrottle (0.25) as gas with the torque split by the steering: both tracks run forward (757 and 212 at full right),
// so the Mako pivots forward instead of turning in place. While the mod drives that gas is 0 (put back when it stops), and the
// body is turned about its up axis by a rate controller as the thrusters' stabiliser: the wanted yaw rate steer x spinRateDeg
// (+ right: the yaw grows), the change toward it from the rate seen since the last tick. A body that answers the other way for
// 0.3 s flips the sign (logged): the yaw axis's sign is assumed from the pitch's and roll's, measured [verify].
struct Spin {
    ue3::Object* sim = nullptr;  // the simulation whose TurnInPlaceThrottle we set to 0, and what it was
    float turnInPlace = 0.0f;
    bool haveYaw = false;
    float yawDeg = 0.0f;
    float sign = 1.0f;
    float wrongS = 0.0f;
    bool active = false;
};
Spin gSpin;

void RestoreTurnInPlace() {
    if (gSpin.sim != nullptr && ue3::runtime::IsAlive(gSpin.sim)) {
        if (float* const field = gTurnInPlace.In<float>(gSpin.sim)) {
            *field = gSpin.turnInPlace;
            write_journal::Wrote("MakoControls.TurnInPlaceThrottle", field);
        }
    }
    gSpin = {};
}

void SpinInPlace(ue3::Object* vehicle, double deltaSeconds, float spinRateDeg) {
    ue3::Object* const sim = gSimIsTank ? gSimObj.Read<ue3::Object*>(vehicle).value_or(nullptr) : nullptr;
    if (sim == nullptr || !(spinRateDeg > 0.0f) || !gOut.driving) {
        if (gSpin.sim != nullptr) {
            RestoreTurnInPlace();
        }
        return;
    }
    if (gSpin.sim != sim) {
        RestoreTurnInPlace();
        gSpin.sim = sim;
        gSpin.turnInPlace = gTurnInPlace.Read<float>(sim).value_or(0.25f);
        log::Info("mako controls: steering at zero throttle spins the Mako on the spot (up to {:.0f} deg/s; the tank's own turn-in-place "
                  "gas {:.2f} off while you drive)", spinRateDeg, gSpin.turnInPlace);
    }
    if (float* const field = gTurnInPlace.In<float>(sim); field != nullptr && *field != 0.0f) {
        *field = 0.0f;
        write_journal::Wrote("MakoControls.TurnInPlaceThrottle", field);
    }
    bool const want = gOut.stickHeld && gOut.throttle == 0.0f && gOut.steer != 0.0f && gSetAngularVelocity.Get() != nullptr;
    auto const rotation = gRotation.Read<ue3::Rotator>(vehicle);
    ue3::Object* const mesh = gVehicleMesh.Read<ue3::Object*>(vehicle).value_or(nullptr);
    if (!want || !rotation || mesh == nullptr) {
        gSpin.haveYaw = false;
        gSpin.active = false;
        return;
    }
    float const dt = static_cast<float>(std::clamp(deltaSeconds, 0.001, 0.1));
    float const yaw = static_cast<float>(SignedUnits(rotation->yaw) * 360.0 / 65536.0);
    float const rate = gSpin.haveYaw ? std::remainder(yaw - gSpin.yawDeg, 360.0f) / dt : 0.0f;
    gSpin.haveYaw = true;
    gSpin.yawDeg = yaw;
    if (!gSpin.active) {
        gSpin.active = true;
        ++gCounts.spins;
    }
    float const target = gOut.steer * spinRateDeg;
    // The safety: turning hard the other way from the one asked for, for long, is the axis's sign.
    gSpin.wrongS = std::abs(target) > 20.0f && std::abs(rate) > 20.0f && rate * target < 0.0f ? gSpin.wrongS + dt : 0.0f;
    if (gSpin.wrongS > 0.3f) {
        gSpin.sign = -gSpin.sign;
        gSpin.wrongS = 0.0f;
        log::Warn("mako controls: the spin answered the other way ({:+.0f} deg/s for {:+.0f} asked): the up axis's sign flipped", rate, target);
    }
    constexpr float kRateGain = 0.35f;
    float const add = std::clamp((target - rate) * kRateGain, -spinRateDeg, spinRateDeg) * gSpin.sign;
    engine::Basis const axes = engine::ToBasis(engine::UnrealRotation{rotation->pitch, rotation->yaw, rotation->roll});
    ue3::Params parms(ue3::runtime::Get(), gSetAngularVelocity.Get());
    parms.Set("NewAngVel", AngularVelocity(axes.up.x * add, axes.up.y * add, axes.up.z * add));
    parms.SetBool("bAddToCurrent", true);
    ue3::runtime::Call(mesh, gSetAngularVelocity.Get(), parms);
    ++gCounts.spinTicks;
    gCounts.spinRateDeg = rate;
    gCounts.spinTargetDeg = target;
}

// The vehicle's attitude last tick while stabilising (its pitch and roll, degrees), for the rates.
struct Attitude {
    bool have = false;
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;
};
Attitude gAttitude;

// While the jets or the booster fire, the Mako is held level: roll to 0 and pitch to the stick's forward/back tilt (pushed: the
// nose down, at most `maxPitchDeg`), each turning at most `maxRateDeg` a second. A rate controller on the body's angular velocity
// (SetRBAngularVelocity, added to the current): the wanted rate from the angle's error, the change toward it from the rate seen
// since the last tick. The first version added the stick's pitch every tick with nothing holding it and flipped the Mako
// (2026-11-03, the user in the headset: +16 -> +84 degrees in one burst). A positive angular velocity about the vehicle's right
// axis raises its pitch and about its forward axis its roll (measured: pushed, the pitch fell from -0.3 to -61 degrees).
void StabilizeOnThrusters(ue3::Object* vehicle, double deltaSeconds, hand_calibration::Mako const& settings) {
    bool const jets = gJumping.ReadBool(vehicle).value_or(false);
    bool const boost = gBoosting.ReadBool(vehicle).value_or(false);
    gCounts.jets += jets ? 1u : 0u;
    gCounts.boost += boost ? 1u : 0u;
    auto const rotation = gRotation.Read<ue3::Rotator>(vehicle);
    if (!(jets || boost) || !settings.thrusterStabilize || !rotation || gSetAngularVelocity.Get() == nullptr) {
        gAttitude = {};
        return;
    }
    ue3::Object* const mesh = gVehicleMesh.Read<ue3::Object*>(vehicle).value_or(nullptr);
    float const dt = static_cast<float>(std::clamp(deltaSeconds, 0.001, 0.1));
    if (mesh == nullptr) {
        return;
    }
    float const pitch = static_cast<float>(SignedUnits(rotation->pitch) * 360.0 / 65536.0);
    float const roll = static_cast<float>(SignedUnits(rotation->roll) * 360.0 / 65536.0);
    float const pitchRate = gAttitude.have ? std::remainder(pitch - gAttitude.pitchDeg, 360.0f) / dt : 0.0f;
    float const rollRate = gAttitude.have ? std::remainder(roll - gAttitude.rollDeg, 360.0f) / dt : 0.0f;
    gAttitude = {true, pitch, roll};
    float const target = gOut.stickHeld ? -gOut.pitch * settings.thrusterPitchDeg : 0.0f;
    constexpr float kAngleGain = 3.0f;  // wanted rate per degree of error (1/s)
    constexpr float kRateGain = 0.35f;  // the share of the rate's error corrected each tick
    float const limit = settings.thrusterRateDeg;
    float const wantPitchRate = std::clamp(kAngleGain * (target - pitch), -limit, limit);
    float const wantRollRate = std::clamp(kAngleGain * (0.0f - roll), -limit, limit);
    float const addPitch = std::clamp((wantPitchRate - pitchRate) * kRateGain, -limit, limit);
    float const addRoll = std::clamp((wantRollRate - rollRate) * kRateGain, -limit, limit);
    engine::Basis const axes = engine::ToBasis(engine::UnrealRotation{rotation->pitch, rotation->yaw, rotation->roll});
    ue3::Params parms(ue3::runtime::Get(), gSetAngularVelocity.Get());
    parms.Set("NewAngVel", AngularVelocity(axes.right.x * addPitch + axes.forward.x * addRoll, axes.right.y * addPitch + axes.forward.y * addRoll,
                               axes.right.z * addPitch + axes.forward.z * addRoll));
    parms.SetBool("bAddToCurrent", true);
    ue3::runtime::Call(mesh, gSetAngularVelocity.Get(), parms);
    ++gCounts.held;
    gCounts.worstPitchDeg = std::max(gCounts.worstPitchDeg, std::abs(target - pitch));
    gCounts.worstRollDeg = std::max(gCounts.worstRollDeg, std::abs(roll));
    gCounts.targetPitchDeg = target;
}

void WriteInputs(ue3::Object* vehicle, float throttle, float steering) {
    float* const t = gThrottle.In<float>(vehicle);
    float* const s = gSteering.In<float>(vehicle);
    if (t == nullptr || s == nullptr) {
        return;
    }
    *t = throttle;
    write_journal::Wrote("MakoControls.Throttle", t);
    *s = steering;
    write_journal::Wrote("MakoControls.Steering", s);
}

void ApplyInputs(ue3::Object* context) {
    if (context == gVehicle && gVehicle != nullptr) {
        HoldJets(gJetsController);
    }
    if (!gOut.driving || context != gVehicle || gVehicle == nullptr) {
        return;
    }
    if (!Powered()) {
        ++gCounts.gated;
        return;
    }
    WriteInputs(gVehicle, gOut.throttle, gSteeringSent);
    ++gCounts.written;
}

// BioVehicleBase.SetInputs's post-handlers: right after the game's own values (from the sticks) went in.
void OnSetInputsCall(ue3::Object* context, ue3::Function* /*function*/, void* /*result*/) {
    if (context == gVehicle && gVehicle != nullptr) {
        gSetInputsSeenMs = GetTickCount64();
        ++gCounts.handlerCalls;
        if (!gLoggedSeamCall) {
            gLoggedSeamCall = true;
            log::Info("mako controls: seam: BioVehicleBase.SetInputs seen through CallFunction (script calls it each tick); while the mod "
                      "drives its post-handler overwrites Vehicle.Throttle and Vehicle.Steering");
        }
        ApplyInputs(context);
    }
}

void OnSetInputsEvent(ue3::Object* context, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    if (context == gVehicle && gVehicle != nullptr) {
        gSetInputsSeenMs = GetTickCount64();
        ++gCounts.handlerEvents;
        if (!gLoggedSeamEvent) {
            gLoggedSeamEvent = true;
            log::Info("mako controls: seam: BioVehicleBase.SetInputs seen through ProcessEvent");
        }
        ApplyInputs(context);
    }
}

// Which simulation the vehicle runs, once per vehicle: a tank turns on the spot, a plain car doesn't.
void LogSimulation(ue3::Object* vehicle, ue3::Object* controller) {
    if (gLoggedSim) {
        return;
    }
    gLoggedSim = true;
    ue3::Object* const sim = gSimObj.Read<ue3::Object*>(vehicle).value_or(nullptr);
    gSimClass = sim != nullptr ? ue3::runtime::FullName(sim) : std::string("none");
    gSimIsTank = sim != nullptr && ue3::IsA(sim, gSimTank);
    bool const car = sim != nullptr && ue3::IsA(sim, gSimCar);
    log::Info("mako controls: simulation is {} ({}{}){}; the game's camera-relative steering is {} (it applies above SetInputs, so not to us)", gSimClass,
        gSimIsTank ? "a tank: full twist with the throttle centred should spin on the spot" : car ? "a plain car: twist steers the wheels, no spin on the spot" : "neither a tank nor a car",
        sim != nullptr && ue3::IsA(sim, gBioSimTank) ? ", BioSVehicleSimTank" : "",
        gSimIsTank ? std::format(", TurnInPlaceThrottle {:.2f}", gTurnInPlace.Read<float>(sim).value_or(0.0f)) : std::string(),
        controller != nullptr ? (gCameraRelative.ReadBool(controller).value_or(false) ? "on" : "off") : "unknown");
}

float EngineSpeed(ue3::Object* vehicle) {
    float const top = gMaxSpeed.Read<float>(vehicle).value_or(0.0f);
    float const speed = gForwardVel.Read<float>(vehicle).value_or(0.0f);
    return top > 1.0f ? std::clamp(std::abs(speed) / top, 0.0f, 1.0f) : 0.0f;
}

// While the mod drives, the camera keeps its angle to the vehicle (it hangs on ControlRotation, research/46). Riding in the
// cockpit (research/47) it does so whoever drives: that camera is the centre screen's chase view, and it trails the Mako as a
// pad player keeps it behind. The ride moves the eyes' view to the cockpit itself, so the view anchor isn't told of the turn.
void FollowCamera(ue3::Object* vehicle, ue3::Object* controller) {
    std::optional<ue3::Rotator> const rotation = gRotation.Read<ue3::Rotator>(vehicle);
    if (!rotation) {
        gHaveYaw = false;
        return;
    }
    int const units = gHaveYaw ? SignedUnits(rotation->yaw - gLastYaw) : 0;
    gLastYaw = rotation->yaw;
    gHaveYaw = true;
    bool const riding = cockpit_probe::Riding() && hand_calibration::GetCockpit().followCamera;
    if (units == 0 || !(riding || (gOut.driving && gSettings.cameraFollow)) || controller == nullptr) {
        return;
    }
    ue3::Rotator* const control = gRotation.In<ue3::Rotator>(controller);
    if (control == nullptr) {
        return;
    }
    control->yaw += units;
    write_journal::Wrote("MakoControls.ControlRotation", control);
    if (!riding) {
        view_anchor::NoteTurn(units, false, 0.0, 0.0);
    }
    gCounts.cameraUnits += units;
}

engine::UnrealRotation ToRotation(ue3::Rotator const& r) {
    return {r.pitch, r.yaw, r.roll};
}

// The thumbstick's aim, relative to the vehicle, in the world.
StickAim AimFromStick(ue3::Object* vehicle) {
    StickAim aim;
    std::optional<ue3::Rotator> const rotation = gRotation.Read<ue3::Rotator>(vehicle);
    std::optional<ue3::Vector> const location = gLocation.Read<ue3::Vector>(vehicle);
    if (!rotation || !location) {
        return aim;
    }
    engine::Basis const basis = engine::ToBasis(ToRotation(*rotation));
    double const yaw = static_cast<double>(gOut.aimYawDeg) / kDegrees;
    double const pitch = static_cast<double>(gOut.aimPitchDeg) / kDegrees;
    aim.direction = engine::Apply(basis, {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)});
    aim.origin = {location->x + basis.up.x * kStickRayUp, location->y + basis.up.y * kStickRayUp, location->z + basis.up.z * kStickRayUp};
    aim.valid = true;
    return aim;
}

// Where the turret aims now, relative to the vehicle: the thumbstick starts from there.
void SeedAim(ue3::Object* vehicle, engine::UnrealVector const* currentAim) {
    std::optional<ue3::Rotator> const rotation = gRotation.Read<ue3::Rotator>(vehicle);
    if (currentAim == nullptr || !rotation) {
        gControls.SeedAim(0.0f, 0.0f);
        return;
    }
    engine::UnrealVector const local = engine::Apply(engine::Inverse(engine::ToBasis(ToRotation(*rotation))), *currentAim);
    gControls.SeedAim(static_cast<float>(std::atan2(local.y, local.x) * kDegrees),
        static_cast<float>(std::atan2(local.z, std::hypot(local.x, local.y)) * kDegrees));
}

void PublishView(Link const& link, bool visible) {
    if (link.publish == nullptr || (!visible && !gViewShown)) {
        return;
    }
    gViewShown = visible;
    mc::View view;
    view.visible = visible;
    if (visible) {
        mc::Seat const& seat = gControls.CurrentSeat();
        view.leverBase = seat.leverBase;
        view.stickBase = seat.stickBase;
        view.forward = seat.forward;
        view.lever = gOut.lever;
        view.steer = gOut.steer;
        view.travelM = gSettings.throttleTravelM;
        view.detent = gSettings.throttleDetent;
        view.leverHeld = gOut.leverHeld;
        view.stickHeld = gOut.stickHeld;
        view.leverNear = gOut.leverNear;
        view.stickNear = gOut.stickNear;
    }
    link.publish(&view);
}

#endif

} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE1)
    if (gStarted) {
        return true;
    }
    if (!ue3::runtime::Ready()) {
        return false;
    }
    gStarted = true;
    gAvailable = gThrottle.Get() != nullptr && gSteering.Get() != nullptr && gRotation.Get() != nullptr && gBehavior.Get() != nullptr &&
        gPowertrain.Get() != nullptr && gPaused.Get() != nullptr;
    if (!gAvailable) {
        log::Warn("mako controls: Vehicle.Throttle, Vehicle.Steering or the behavior's powertrain flags not found: no physical controls, "
                  "the game's own controls stay");
        return true;
    }
    if (ue3::Function* const setInputs = gSetInputs.Get()) {
        engine::call_function::AddPostHandler(setInputs, &OnSetInputsCall);
        engine::process_event::AddPostHandler(setInputs, &OnSetInputsEvent);
    } else {
        log::Warn("mako controls: BioVehicleBase.SetInputs not found: the mod's tick writes Throttle and Steering (after the game's "
                  "tick, so a frame late) while it drives");
    }
    return true;
#else
    return false;
#endif
}

StickAim Tick(double deltaSeconds, Link const& link, ue3::Object* vehicle, ue3::Object* controller, engine::UnrealVector const* currentAim) {
    StickAim aim;
    hand_calibration::Mako const settings = hand_calibration::GetMako();
    gSettings = settings.controlSettings;
#if defined(MEVR_GAME_LE1)
    if (!gLoggedSettings) {
        gLoggedSettings = true;
        log::Info("mako controls: {}", mc::Describe(gSettings));
    }
    bool const on = gAvailable && gSettings.controls && link.supported;
    if (vehicle != nullptr && !ue3::runtime::IsAlive(vehicle)) {
        vehicle = nullptr;  // going with the level: never read
    }
    vr::controllers::State const controllers = vr::controllers::Latest();
    // The vehicle we were driving: gone with the level, left, or turned off in the file. Everything back.
    if (gVehicle != nullptr) {
        bool const alive = ue3::runtime::IsAlive(gVehicle);
        if (!alive || vehicle != gVehicle || !on) {
            mc::Input leaving;
            leaving.stop = alive ? mc::Stop::LeftMako : mc::Stop::Level;
            leaving.left.squeeze = controllers.left.squeeze;  // a grip held through it is no new squeeze afterwards
            leaving.right.squeeze = controllers.right.squeeze;
            mc::Output const left = gControls.Step(leaving, gSettings);
            if (left.stopped != mc::Stop::None) {
                if (alive) {
                    WriteInputs(gVehicle, 0.0f, 0.0f);  // the game's next SetInputs takes over from zero
                }
                log::Info("mako controls: stopped ({}): throttle and steering zero, the game's own controls are back until the next grab",
                    mc::Name(left.stopped));
            }
            RestoreTurnInPlace();
            gVehicle = nullptr;
            gVehicleBehavior = nullptr;
            gHaveYaw = false;
            gLoggedSim = false;
            gLoggedDrive = false;
        }
    }
    if (vehicle == nullptr || !on) {
        gInMako = false;
        gOut = {};
        gSteeringSent = 0.0f;
        PublishView(link, false);
        return aim;
    }
    if (gVehicle != vehicle) {
        gVehicle = vehicle;
        gVehicleBehavior = gBehavior.Read<ue3::Object*>(vehicle).value_or(nullptr);
    }
    gInMako = true;
    gJetsController = controller;
    HoldJets(controller);
    RefundFuel(vehicle, deltaSeconds, settings.thrusterBurnScale);

    bool const calibrating = body_calibration::Calibrating();
    mc::Input in;
    in.deltaSeconds = deltaSeconds;
    in.left = {controllers.active && controllers.left.gripValid, controllers.left.grip, controllers.left.squeeze};
    in.right = {controllers.active && controllers.right.gripValid, controllers.right.grip, controllers.right.squeeze};
    in.stickX = controllers.right.stickX;
    in.stickY = controllers.right.stickY;
    if (auto const anchor = view_anchor::Current()) {
        in.headValid = true;
        in.head = anchor->head;
    }
    if (link.floor) {
        in.haveRecenters = true;
        in.recenters = link.floor->recenters;
    }
    in.engineSpeed = EngineSpeed(vehicle);
    in.stop = link.mainMenuOpen ? mc::Stop::Menu : calibrating ? mc::Stop::Calibration : !link.headTrackedView ? mc::Stop::Tracking : mc::Stop::None;
    gOut = gControls.Step(in, gSettings);
    if (gOut.stopped != mc::Stop::None) {
        WriteInputs(vehicle, 0.0f, 0.0f);  // the game's next SetInputs takes over from zero
        log::Info("mako controls: stopped ({}): throttle and steering zero, the game's own controls are back until the next grab", mc::Name(gOut.stopped));
        gLoggedDrive = false;
    }
    LogSimulation(vehicle, controller);
    StabilizeOnThrusters(vehicle, deltaSeconds, settings);
    SpinInPlace(vehicle, deltaSeconds, settings.spinRateDeg);

    // The physics' values for this tick. Steering: the game's is + left (PlayerDriving's ProcessDrive passes -InStrafe)
    // [verify the sign in the headset; [Mako] stickInvert flips the twist].
    gSteeringSent = -gOut.steer;
    FollowCamera(vehicle, controller);

    if (gOut.replaced) {
        mc::Seat const& seat = gControls.CurrentSeat();
        log::Info("mako controls: placed from the head: lever at ({:.2f}, {:.2f}, {:.2f}), stick at ({:.2f}, {:.2f}, {:.2f}) m in tracking space, "
                  "the seat faces ({:.2f}, {:.2f})",
            seat.leverBase.x, seat.leverBase.y, seat.leverBase.z, seat.stickBase.x, seat.stickBase.y, seat.stickBase.z, seat.forward.x, seat.forward.z);
    }
    if (gOut.leverGrabbed && !gLoggedLever) {
        gLoggedLever = true;
        log::Info("mako controls: first grab of the lever: the left grip's squeeze holds it; X is jump and upright, Y is boost while held");
    }
    if (gOut.stickGrabbed) {
        SeedAim(vehicle, currentAim);
        gOut.aimYawDeg = gControls.AimYaw();
        gOut.aimPitchDeg = gControls.AimPitch();
        if (!gLoggedStick) {
            gLoggedStick = true;
            log::Info("mako controls: first grab of the stick: {}, the trigger fires the machine gun, A the cannon, the thumbstick aims the turret",
                gSettings.stickTwist ? "twist steers" : "tilting it left and right steers, forward and back pitches the Mako while its thrusters fire");
        }
    }
    if (gOut.driving && !gLoggedDrive) {
        gLoggedDrive = true;
        log::Info("mako controls: the mod drives: Throttle and Steering are ours, the game sees no left stick from the controllers");
    }
    // The fallback: no SetInputs seen lately (the handler isn't in, or something else drives the vehicle).
    if (gOut.driving && GetTickCount64() - gSetInputsSeenMs > kSetInputsFreshMs) {
        if (!gLoggedSeamTick) {
            gLoggedSeamTick = true;
            log::Warn("mako controls: BioVehicleBase.SetInputs not seen in {} ms while driving: writing Throttle and Steering from the mod's tick [verify]",
                kSetInputsFreshMs);
        }
        if (Powered()) {
            WriteInputs(vehicle, gOut.throttle, gSteeringSent);
            ++gCounts.tickWrites;
        }
    }

    for (int i = 0; i < gOut.hapticCount; ++i) {
        if (link.hapticPulse != nullptr) {
            link.hapticPulse(gOut.haptics[i].hand, gOut.haptics[i].seconds, gOut.haptics[i].amplitude);
        }
    }
    if (link.hapticContinuous != nullptr) {
        if (gOut.rumbleLeft > 0.005f) {
            link.hapticContinuous(0, gOut.rumbleLeft, kRumbleHz, kRumbleSeconds);
        }
        if (gOut.rumbleRight > 0.005f) {
            link.hapticContinuous(1, gOut.rumbleRight, kRumbleHz, kRumbleSeconds);
        }
    }

    PublishView(link, gOut.placed && link.headTrackedView && !link.mainMenuOpen && !calibrating);
    if (gOut.stickHeld) {
        aim = AimFromStick(vehicle);
    }
#else
    (void)deltaSeconds;
    (void)link;
    (void)vehicle;
    (void)controller;
    (void)currentAim;
#endif
    return aim;
}

void AdjustPad(gamepad::Options& options) {
    if (!gInMako || !gAvailable || !gSettings.controls) {
        return;
    }
    if (gOut.stickNear || gOut.stickHeld) {
        options.rightGripFree = true;  // a new squeeze here takes the stick, it isn't the cannon's RB
    }
    if (gOut.leverNear || gOut.leverHeld) {
        options.gripShoulder = false;  // and here the lever, never LB
    }
    options.makoDrive = gOut.driving;
    options.makoLever = gOut.leverHeld;
    options.makoStick = gOut.stickHeld;
}

bool Driving() {
    return gInMako && gOut.driving;
}

std::string TestJets(double seconds) {
    if (!gInMako || gVehicle == nullptr) {
        return "not driving the Mako";
    }
    gJetsUntilMs = GetTickCount64() + static_cast<ULONGLONG>(std::clamp(seconds, 0.0, 30.0) * 1000.0);
    log::Info("mako controls: test: the jump jets' button (bJumpJetsOn) held for {:.1f} s", seconds);
    return std::format("bJumpJetsOn held for {:.1f} s (jump fuel {:.2f} of {:.2f}): the heartbeat's `thrusters on` line says what happened", seconds,
        gJumpFuel.Read<float>(gVehicle).value_or(-1.0f), gMaxJumpFuel.Read<float>(gVehicle).value_or(-1.0f));
}

Pose CurrentPose() {
    Pose pose;
    pose.travelM = gSettings.throttleTravelM;
    pose.lockDeg = gSettings.stickTwistDeg;
    pose.tilt = !gSettings.stickTwist;
    if (gInMako) {
        pose.lever = gOut.lever;
        pose.steer = gOut.steer;
        pose.tiltRightDeg = gOut.tiltRightDeg;
        pose.tiltForwardDeg = gOut.tiltForwardDeg;
        pose.leverHeld = gOut.leverHeld;
        pose.stickHeld = gOut.stickHeld;
    }
    return pose;
}

std::string StatusJson() {
    mc::Seat const& seat = gControls.CurrentSeat();
    return std::format(
        "{{\"available\": {}, \"on\": {}, \"inMako\": {}, \"driving\": {}, \"placed\": {}, \"throttle\": {}, \"lever\": {}, \"steer\": {}, \"twistDeg\": {}, "
        "\"leverHeld\": {}, \"stickHeld\": {}, \"leverNear\": {}, \"stickNear\": {}, \"leverBase\": [{}, {}, {}], \"stickBase\": [{}, {}, {}], "
        "\"forward\": [{}, {}, {}], \"aimYawDeg\": {}, \"aimPitchDeg\": {}, \"simulation\": {}, \"setInputsCalls\": {}, \"setInputsEvents\": {}, "
        "\"written\": {}, \"gated\": {}, \"tickWrites\": {}}}",
        gAvailable ? "true" : "false", gSettings.controls ? "true" : "false", gInMako ? "true" : "false", gOut.driving ? "true" : "false",
        gOut.placed ? "true" : "false", json::JsonNumber(gOut.throttle), json::JsonNumber(gOut.lever), json::JsonNumber(gOut.steer),
        json::JsonNumber(gOut.twistDeg), gOut.leverHeld ? "true" : "false", gOut.stickHeld ? "true" : "false", gOut.leverNear ? "true" : "false",
        gOut.stickNear ? "true" : "false", json::JsonNumber(seat.leverBase.x), json::JsonNumber(seat.leverBase.y), json::JsonNumber(seat.leverBase.z),
        json::JsonNumber(seat.stickBase.x), json::JsonNumber(seat.stickBase.y), json::JsonNumber(seat.stickBase.z), json::JsonNumber(seat.forward.x),
        json::JsonNumber(seat.forward.y), json::JsonNumber(seat.forward.z), json::JsonNumber(gOut.aimYawDeg), json::JsonNumber(gOut.aimPitchDeg),
        json::JsonString(gSimClass), gCounts.handlerCalls, gCounts.handlerEvents, gCounts.written, gCounts.gated, gCounts.tickWrites);
}

void LogStats() {
#if defined(MEVR_GAME_LE1)
    if (!gInMako || gVehicle == nullptr) {
        gCounts = Counts{};
        return;
    }
    ue3::Object* const sim = gSimObj.Read<ue3::Object*>(gVehicle).value_or(nullptr);
    std::string tank;
    if (sim != nullptr && gSimIsTank) {
        tank = std::format(", tracks L {:.0f} R {:.0f}, input yaw {:.2f}", gLeftTrackVel.Read<float>(sim).value_or(0.0f), gRightTrackVel.Read<float>(sim).value_or(0.0f),
            gCurrentInputYaw.Read<float>(sim).value_or(0.0f));
    }
    log::Info("    mako controls: {}; ours throttle {:+.2f} steer {:+.2f} (lever {:+.2f}{}{}); the vehicle's Throttle {:+.2f} Steering {:+.2f}, OutputGas {:+.2f} "
              "OutputSteering {:+.2f} OutputBrake {:.2f}, ForwardVel {:.0f}, ActualSteering {:+.2f}{}; last 5 s: SetInputs seen {} (script) {} (native), written {}, "
              "left alone {} (powertrain off or paused), from the tick {}; camera followed {:+.1f} deg",
        gOut.driving ? "the mod drives" : "the game drives", gOut.throttle, gOut.steer, gOut.lever, gOut.leverHeld ? ", lever held" : "", gOut.stickHeld ? ", stick held" : "",
        gThrottle.Read<float>(gVehicle).value_or(0.0f), gSteering.Read<float>(gVehicle).value_or(0.0f), gOutputGas.Read<float>(gVehicle).value_or(0.0f),
        gOutputSteering.Read<float>(gVehicle).value_or(0.0f), gOutputBrake.Read<float>(gVehicle).value_or(0.0f), gForwardVel.Read<float>(gVehicle).value_or(0.0f),
        sim != nullptr ? gActualSteering.Read<float>(sim).value_or(0.0f) : 0.0f, tank, gCounts.handlerCalls, gCounts.handlerEvents, gCounts.written, gCounts.gated,
        gCounts.tickWrites, static_cast<double>(gCounts.cameraUnits) * 360.0 / 65536.0);
    if (gCounts.spinTicks > 0) {
        log::Info("    mako controls: spun on the spot {} times ({} ticks); the last yaw rate {:+.0f} deg/s for {:+.0f} asked", gCounts.spins,
            gCounts.spinTicks, gCounts.spinRateDeg, gCounts.spinTargetDeg);
    }
    if (gCounts.jets + gCounts.boost > 0) {
        log::Info("    mako controls: thrusters: jump jets on {} ticks, booster {}, {:.2f} s of fuel given back; held level on {} (the worst pitch error "
                  "{:.1f} deg, roll {:.1f} deg; the last target pitch {:+.1f})",
            gCounts.jets, gCounts.boost, gCounts.refunded, gCounts.held, gCounts.worstPitchDeg, gCounts.worstRollDeg, gCounts.targetPitchDeg);
    }
    gCounts = Counts{};
#endif
}

void Shutdown(bool exiting, void (*publish)(vr::mako_controls::View const* view)) {
#if defined(MEVR_GAME_LE1)
    if (!exiting && gVehicle != nullptr && gOut.driving && ue3::runtime::IsAlive(gVehicle)) {
        WriteInputs(gVehicle, 0.0f, 0.0f);
    }
    gJetsUntilMs = 0;
    gJetsController = nullptr;
    gAttitude = {};
    if (!exiting) {
        RestoreTurnInPlace();
    }
    gSpin = {};
    gLoggedFuel = false;
    if (publish != nullptr) {
        publish(nullptr);
    }
#else
    (void)exiting;
    (void)publish;
#endif
    gStarted = false;
    gAvailable = false;
    gInMako = false;
    gSettings = {};
    gControls = {};
    gOut = {};
    gVehicle = nullptr;
    gVehicleBehavior = nullptr;
    gHaveYaw = false;
    gSteeringSent = 0.0f;
    gViewShown = false;
    gSetInputsSeenMs = 0;
    gLoggedSim = gLoggedSeamCall = gLoggedSeamEvent = gLoggedSeamTick = false;
    gLoggedLever = gLoggedStick = gLoggedDrive = gLoggedSettings = false;
    gSimClass.clear();
    gSimIsTank = false;
    gCounts = Counts{};
}

} // namespace mevr::game::mako_controls
