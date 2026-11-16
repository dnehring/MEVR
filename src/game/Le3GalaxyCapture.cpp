// SPDX-License-Identifier: MIT
#include "game/Le3GalaxyCapture.hpp"
#if defined(MEVR_GAME_LE3)
#include "game/Le3GalaxyProjector.hpp"
#include "game/Le3GalaxyRetention.hpp"
#include "game/SceneCaptureRig.hpp"
#include "core/vr/CineScreen.hpp"
#include "core/Log.hpp"
#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace mevr::game::le3_galaxy_capture {
namespace {
using namespace le3_galaxy;
capture_rig::Rig eyes[2];
Publish publishScene = nullptr;
std::optional<vr::floor::State> currentFloor;
vr::floor::State epoch;
vr::galaxy_holo::Scene scene;
bool armed = false;
bool postProcess = true;
std::string reason = "off";
unsigned frames = 0;
float convergence = 0, baseline = 0;

void Stop(char const* why, bool exiting = false) {
    if (publishScene) publishScene(nullptr);
    for (auto& eye : eyes) {
        if (exiting) eye.Forget(); else eye.Destroy(why);
    }
    if (armed) log::Info("LE3 galaxy captures: stopped ({})", why);
    armed = false; reason = why;
}
bool Vector(json::Value const& v, float out[3]) {
    if (!v.IsArray() || v.Items().size() != 3) return false;
    for (int i = 0; i < 3; ++i) {
        out[i] = static_cast<float>(v.Items()[i].AsNumber(NAN));
        if (!std::isfinite(out[i])) return false;
    }
    return true;
}
}

void Tick(Publish publish, std::optional<vr::floor::State> const& floor) {
    publishScene = publish; currentFloor = floor;
    if (!armed) return;
    auto* pc = player::Controller();
    if (!publish || !floor || !floor->valid || floor->sessions != epoch.sessions || floor->recenters != epoch.recenters ||
        !le3_galaxy_retention::Armed()) { Stop("lease-or-space-ended"); return; }
    if (!le3_galaxy_retention::Active()) return; // waiting for normal entry
    auto const context = Capture(pc);
    auto* cameraObject = player::Camera(pc);
    auto const pov = player::CameraPov(cameraObject);
    if (!context || !pov || player::CameraOwner(cameraObject) != pc) { Stop("native-camera-unavailable"); return; }
    auto const centre = player::Location(Object(context->camera, kCamera, "m_pCenterObject"));
    if (!centre || !std::isfinite(*pov->fov) || *pov->fov <= 1 || *pov->fov >= 179) {
        Stop("native-map-centre-or-lens-unavailable"); return;
    }
    auto const basis = engine::ToBasis(engine::UnrealRotation{pov->rotation->pitch, pov->rotation->yaw, pov->rotation->roll});
    convergence = static_cast<float>((centre->x - pov->location->x) * basis.forward.x +
        (centre->y - pov->location->y) * basis.forward.y + (centre->z - pov->location->z) * basis.forward.z);
    if (!std::isfinite(convergence) || convergence < 100) { Stop("native-map-centre-behind-camera"); return; }
    // Same parallel-camera/crop policy as ME1 StepWindow. No guessed centre distance.
    float const depth = std::min(0.01f, vr::cine_screen::MaxDepth(scene.windowWidthM, vr::cine_screen::kDefaultIpdM));
    scene.windowCrop = vr::cine_screen::CropFraction(depth);
    baseline = vr::cine_screen::BaselineUU(scene.windowCrop, convergence, *pov->fov * 0.00872664626f);
    for (int e = 0; e < 2; ++e) {
        auto& eye = eyes[e];
        if (!eye.Alive()) {
            capture_rig::Options options;
            options.width = 1024; options.height = 576; options.fovDeg = *pov->fov;
            options.postProcess = postProcess;
            if (!eye.Create(e == 0 ? "LE3 galaxy left" : "LE3 galaxy right", options, *pov->location)) {
                log::Warn("LE3 galaxy capture creation: {}", eye.Failure());
                Stop("capture-create-failed"); return;
            }
        }
        float const side = (e == 0 ? -0.5f : 0.5f) * baseline;
        ue3::Vector const at{static_cast<float>(pov->location->x + basis.right.x * side),
            static_cast<float>(pov->location->y + basis.right.y * side),
            static_cast<float>(pov->location->z + basis.right.z * side)};
        if (!eye.SetFov(*pov->fov) || (!eye.Enabled() && !eye.SetEnabled(true)) || !eye.Point(at, *pov->rotation)) {
            Stop("capture-point-failed"); return;
        }
        eye.Tick();
        scene.windowResource[e] = eye.Resource();
    }
    ++frames;
    scene.sequence = frames;
    scene.windowTicks = std::min(eyes[0].EnabledTicks(), eyes[1].EnabledTicks());
    scene.windowWidth = eyes[0].Width(); scene.windowHeight = eyes[0].Height();
    scene.windowGeneration = std::max(eyes[0].Generation(), eyes[1].Generation());
    publish(&scene);
    reason = "capture-window-published-room-view-pending";
}

void Shutdown(bool exiting) { Stop("shutdown", exiting); publishScene = nullptr; currentFloor.reset(); }

std::string Command(ue3::Object* pc, std::string const& action) {
    if (action == "off") { Stop("command-off"); le3_galaxy_retention::Command(pc, "off"); }
    else if (action == "on" || action == "on-raw") {
        if (armed || le3_galaxy_retention::Armed()) return "galaxy capture: use capture off before rearming";
        auto const probe = ObserveProjector(pc);
        if (!publishScene || !currentFloor || !currentFloor->valid || !probe.Bool("trackingAvailable"))
            return "galaxy capture: requires host publication, floor epoch and measured on-foot projector";
        scene = {};
        if (!Vector(probe.Get("trackingCentreM"), scene.discCentre) || !Vector(probe.Get("trackingNormal"), scene.discNormal))
            return "galaxy capture: invalid measured projector";
        scene.discRadiusM = static_cast<float>(probe.Number("trackingRadiusM"));
        if (!(scene.discRadiusM > 0.1f && scene.discRadiusM < 10)) return "galaxy capture: invalid radius";
        le3_galaxy_retention::Command(pc, "on");
        if (!le3_galaxy_retention::Armed()) return "galaxy capture: retention admission failed";
        scene.inWorld = scene.anchorValid = scene.windowOn = scene.windowDisc = scene.windowRound = 1;
        scene.windowWidthM = 2 * scene.discRadiusM;
        postProcess = action != "on-raw"; // Diagnostic isolation of the native post-process target path.
        epoch = *currentFloor; armed = true; frames = 0; reason = "waiting-for-native-map";
    } else if (action != "state" && !action.empty()) return "usage: mevr galaxy capture on|on-raw|off|state";
    auto out = json::Value::Object();
    out["armed"] = armed; out["reason"] = reason; out["frames"] = static_cast<int>(frames);
    out["leftResource"] = std::format("0x{:X}", eyes[0].Resource());
    out["rightResource"] = std::format("0x{:X}", eyes[1].Resource());
    out["convergenceUU"] = convergence; out["baselineUU"] = baseline;
    out["postProcess"] = postProcess;
    out["presentationVerified"] = false; out["roomViewHeld"] = false;
    return "galaxy capture: " + json::Write(out, 0);
}
}
#else
namespace mevr::game::le3_galaxy_capture {
void Tick(Publish, std::optional<vr::floor::State> const&) {}
void Shutdown(bool) {}
std::string Command(ue3::Object*, std::string const&) { return "galaxy capture: LE3 only"; }
}
#endif
