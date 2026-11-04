// SPDX-License-Identifier: MIT
#include "game/GalaxyMap.hpp"

#include "core/Log.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace mevr::game::galaxy_map {

namespace {

namespace gm = vr::galaxy_map;

constexpr std::uint8_t kGalaxyMode = 9;  // SFXGameModeManager's GameMode_Galaxy

ue3::ClassRef gCameraGalaxy{"SFXGame.BioCameraBehaviorGalaxy"};
ue3::FieldRef gCenterObject{"SFXGame.BioCameraBehaviorGalaxy", "m_pCenterObject"};
ue3::FieldRef gZoomLocation{"SFXGame.BioCameraBehaviorGalaxy", "m_vZoomLocation"};
ue3::FieldRef gSelectedObject{"SFXGame.BioCameraBehaviorGalaxy", "m_pSelectedObject"};
ue3::FieldRef gState{"SFXGame.BioCameraBehaviorGalaxy", "m_nCurrentState"};

struct State {
    bool published = false;      // the host was given an active view (and not yet told it's gone)
    ue3::Object* behavior = nullptr;
    bool loggedOpen = false;     // this opening's first line
    std::int32_t lastState = -1;
    bool loggedMissing = false;
    // The last measure, for the snapshot and the heartbeat.
    float distanceUnits = 0.0f;
    float unitsPerMeter = 0.0f;
    std::string centreFrom = "none";
    std::uint64_t ticks = 0;
};
State gState2;

// The map's camera behaviour: the live, non-default BioCameraBehaviorGalaxy (one per map; looked up again when it goes).
ue3::Object* FindBehavior() {
    if (gState2.behavior != nullptr && ue3::runtime::IsAlive(gState2.behavior)) {
        return gState2.behavior;
    }
    gState2.behavior = nullptr;
    ue3::Reflection& r = ue3::runtime::Get();
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !ue3::IsA(o, gCameraGalaxy) || !r.IsAlive(o) || r.NameText(o).starts_with("Default__")) {
            continue;
        }
        gState2.behavior = o;
        break;
    }
    return gState2.behavior;
}

double Distance(ue3::Vector const& a, ue3::Vector const& b) {
    double const dx = a.x - b.x;
    double const dy = a.y - b.y;
    double const dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void Forget(Link const& link, char const* why) {
    if (gState2.published && link.publish != nullptr) {
        link.publish(nullptr);
        log::Info("galaxy map: the hologram is down ({})", why);
    }
    gState2.published = false;
    gState2.loggedOpen = false;
    gState2.lastState = -1;
}

} // namespace

void Tick(Link const& link) {
    gm::Settings const settings = hand_calibration::GetGalaxy();
    ue3::Object* const controller = player::Controller();
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    if (link.publish == nullptr || !settings.hologram || !mode || *mode != kGalaxyMode) {
        Forget(link, !settings.hologram ? "[Galaxy] hologram = 0" : "the map closed");
        return;
    }
    if (gCameraGalaxy.Get() == nullptr) {
        if (!gState2.loggedMissing) {
            gState2.loggedMissing = true;
            log::Warn("galaxy map: SFXGame.BioCameraBehaviorGalaxy not found through our reflection layer: the map stays on Halcyon's panel");
        }
        return;
    }
    ue3::Object* const behavior = FindBehavior();
    auto const pov = player::CameraPov(player::Camera(controller));
    if (behavior == nullptr || !pov || pov->location == nullptr) {
        Forget(link, behavior == nullptr ? "no map camera" : "no camera view");
        return;
    }
    // The map's centre: its centre object, else the camera's zoom target.
    std::optional<ue3::Vector> centre;
    ue3::Object* const centreObject = gCenterObject.Read<ue3::Object*>(behavior).value_or(nullptr);
    if (centreObject != nullptr) {
        centre = player::Location(centreObject);
        gState2.centreFrom = "the centre object";
    }
    if (!centre) {
        centre = gZoomLocation.Read<ue3::Vector>(behavior);
        gState2.centreFrom = "the zoom location";
    }
    ue3::Vector const camera = *pov->location;
    double const distance = centre ? Distance(camera, *centre) : 0.0;
    auto const upm = gm::UnitsPerMeter(static_cast<float>(distance), settings.distanceM);
    std::int32_t const state = gState.Read<std::int32_t>(behavior).value_or(-1);
    if (!upm) {
        Forget(link, "the map's centre is at the camera");
        return;
    }
    gState2.distanceUnits = static_cast<float>(distance);
    gState2.unitsPerMeter = *upm;
    ++gState2.ticks;
    if (!gState2.loggedOpen || state != gState2.lastState) {
        ue3::Object* const selected = gSelectedObject.Read<ue3::Object*>(behavior).value_or(nullptr);
        log::Info("galaxy map: {} (map state {}): the camera at ({:.0f}, {:.0f}, {:.0f}), the centre {:.0f} units ahead from {} ({}), selected {}: "
                  "{:.0f} units a metre, the centre {:.2f} m ahead and {:.2f} m below the eyes",
            gState2.loggedOpen ? "the view changed" : "the hologram is up", state, camera.x, camera.y, camera.z, distance, gState2.centreFrom,
            ue3::runtime::FullName(centreObject), ue3::runtime::FullName(selected), *upm, settings.distanceM, settings.belowM);
        gState2.loggedOpen = true;
        gState2.lastState = state;
    }
    gm::View view;
    view.active = true;
    view.unitsPerMeter = *upm;
    view.distanceM = settings.distanceM;
    view.belowM = settings.belowM;
    link.publish(&view);
    gState2.published = true;
}

std::string StatusJson() {
    return std::format("{{\"published\": {}, \"distanceUnits\": {:.1f}, \"unitsPerMeter\": {:.1f}, \"centreFrom\": \"{}\", \"ticks\": {}}}",
        gState2.published ? "true" : "false", gState2.distanceUnits, gState2.unitsPerMeter, gState2.centreFrom, gState2.ticks);
}

void Shutdown(Link const& link) {
    Forget(link, "the mod is unloading");
    gState2 = State{};
}

} // namespace mevr::game::galaxy_map
