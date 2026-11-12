// SPDX-License-Identifier: MIT
#include "game/CameraCollision.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/HostView.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace mevr::game::camera_collision {

#if defined(MEVR_GAME_LE1)

namespace {

// Our reflection layer (research/34); each has a row in engine/ReflectCheck.
ue3::ClassRef gCameraNativeBase{"SFXGame.SFXCameraNativeBase"};
ue3::ClassRef gCameraModeInterpolate{"SFXGame.SFXCameraMode_Interpolate"};
ue3::FieldRef gCurrentCameraMode{"SFXGame.SFXCameraNativeBase", "CurrentCameraMode"};
ue3::FieldRef gInterpolateTo{"SFXGame.SFXCameraMode_Interpolate", "To"};
ue3::FieldRef gCollisionEnabled{"SFXGame.SFXCameraMode", "bCollisionEnabled"};

// The modes whose collision we cleared (to put it back), and counters for the status.
std::vector<ue3::Object*> gCleared;
std::uint64_t gClears = 0;
std::uint64_t gRestores = 0;
bool gActive = false;
std::string gWhy = "not started";

// Halcyon's first-person classes (head_aim.cpp FpStateForClass): never the Mako, the scope or blind fire from cover.
bool FirstPersonClass(std::string_view name) {
    if (name.find("Vehicle") != std::string_view::npos || name.find("Sniper") != std::string_view::npos ||
        name.find("HipAim") != std::string_view::npos) {
        return false;
    }
    return name.find("Explore") != std::string_view::npos || name.find("Combat") != std::string_view::npos ||
           name.find("TightAim") != std::string_view::npos;
}

void Clear(ue3::Object* mode) {
    if (mode == nullptr || !ue3::runtime::IsAlive(mode)) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    std::string const cls = r.NameText(r.ClassOf(mode));
    if (!FirstPersonClass(cls) || !gCollisionEnabled.ReadBool(mode).value_or(false)) {
        return;
    }
    if (!gCollisionEnabled.WriteBool(mode, false)) {
        return;
    }
    if (void* const field = gCollisionEnabled.Address(mode)) {
        write_journal::Wrote("CameraCollision.bCollisionEnabled", field, 4);
    }
    gCleared.push_back(mode);
    if (gClears++ == 0) {
        log::Info("camera collision: {} pulled the camera to its hook when anything stood between them; cleared in first person "
                  "([Game] cameraCollision = 0)", cls);
    }
}

void RestoreAll(char const* why) {
    int restored = 0;
    for (ue3::Object* const mode : gCleared) {
        if (ue3::runtime::IsAlive(mode) && gCollisionEnabled.WriteBool(mode, true)) {
            if (void* const field = gCollisionEnabled.Address(mode)) {
                write_journal::Wrote("CameraCollision.bCollisionEnabled", field, 4);
            }
            ++restored;
        }
    }
    if (!gCleared.empty()) {
        log::Info("camera collision: put back on {} of {} camera modes ({})", restored, gCleared.size(), why);
    }
    gRestores += static_cast<std::uint64_t>(restored);
    gCleared.clear();
}

} // namespace

bool TryStartup() {
    if (!ue3::runtime::Ready() || gCollisionEnabled.Get() == nullptr || gCurrentCameraMode.Get() == nullptr) {
        log::Warn("camera collision: SFXCameraMode.bCollisionEnabled or the camera's current mode not found: the game's camera "
                  "collision stays on in first person");
        return false;
    }
    return true;
}

void Tick() {
    bool const wanted = !hand_calibration::GetGame().cameraCollision;
    auto const mode = head_tracking::GameMode();
    bool const onFoot = mode && *mode == 0;
    bool const tracked = vr::host_view::Latest().valid;
    bool const active = wanted && onFoot && tracked;
    if (!active) {
        // Not on foot (a conversation, a menu, the Mako) keeps what was cleared: the same modes come back on foot. Only the
        // setting or a view that isn't head-tracked (flat, VR off) gives the game its collision back.
        if ((!wanted || (onFoot && !tracked)) && !gCleared.empty()) {
            RestoreAll(!wanted ? "[Game] cameraCollision = 1" : "the view isn't head-tracked");
        }
        gActive = false;
        gWhy = !wanted ? "[Game] cameraCollision = 1" : !onFoot ? "not on foot" : "no head-tracked world view";
        return;
    }
    gActive = true;
    gWhy = "first person on foot";
    // Modes that died with a level are dropped (never written: the level freed them).
    std::erase_if(gCleared, [](ue3::Object* m) { return !ue3::runtime::IsAlive(m); });
    ue3::Object* const camera = player::Camera(player::Controller());
    if (camera == nullptr || !ue3::IsA(camera, gCameraNativeBase)) {
        return;
    }
    ue3::Object* const current = gCurrentCameraMode.Read<ue3::Object*>(camera).value_or(nullptr);
    if (current == nullptr || !ue3::runtime::IsAlive(current)) {
        return;
    }
    Clear(current);
    if (ue3::IsA(current, gCameraModeInterpolate)) {
        Clear(gInterpolateTo.Read<ue3::Object*>(current).value_or(nullptr));  // the blend's destination
    }
}

std::string StatusJson() {
    return std::format("{{\"active\": {}, \"why\": \"{}\", \"cleared\": {}, \"clears\": {}, \"restores\": {}}}", gActive ? "true" : "false",
        gWhy, gCleared.size(), gClears, gRestores);
}

void Shutdown() {
    RestoreAll("the mod unloads");
    gClears = gRestores = 0;
    gActive = false;
    gWhy = "not started";
    gCameraNativeBase.Reset();
    gCameraModeInterpolate.Reset();
    gCurrentCameraMode.Reset();
    gInterpolateTo.Reset();
    gCollisionEnabled.Reset();
}

#else

bool TryStartup() {
    return false;
}

void Tick() {}

std::string StatusJson() {
    return "null";
}

void Shutdown() {}

#endif

} // namespace mevr::game::camera_collision
