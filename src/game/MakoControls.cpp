// SPDX-License-Identifier: MIT
#include "game/MakoControls.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CallFunction.hpp"
#include "engine/ProcessEvent.hpp"
#include "game/BodyCalibration.hpp"
#include "game/CockpitProbe.hpp"
#include "game/HandCalibration.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
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

// Since the heartbeat.
struct Counts {
    unsigned handlerCalls = 0;   // SetInputs, through CallFunction
    unsigned handlerEvents = 0;  // ...through ProcessEvent
    unsigned written = 0;        // our values went in
    unsigned gated = 0;          // SetInputs had zeroed them: the powertrain off, or paused
    unsigned tickWrites = 0;     // no SetInputs seen: written from the mod's tick
    int cameraUnits = 0;         // the vehicle's yaw given to the camera
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
            log::Info("mako controls: first grab of the stick: twist steers, the trigger fires the machine gun, A the cannon, the thumbstick aims the turret");
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

Pose CurrentPose() {
    Pose pose;
    pose.travelM = gSettings.throttleTravelM;
    pose.lockDeg = gSettings.stickTwistDeg;
    if (gInMako) {
        pose.lever = gOut.lever;
        pose.steer = gOut.steer;
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
    gCounts = Counts{};
#endif
}

void Shutdown(bool exiting, void (*publish)(vr::mako_controls::View const* view)) {
#if defined(MEVR_GAME_LE1)
    if (!exiting && gVehicle != nullptr && gOut.driving && ue3::runtime::IsAlive(gVehicle)) {
        WriteInputs(gVehicle, 0.0f, 0.0f);
    }
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
