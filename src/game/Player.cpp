// SPDX-License-Identifier: MIT
#include "game/Player.hpp"

#include "engine/GameAdapter.hpp"
#include "engine/ProcessEvent.hpp"
#include "ue3/Handles.hpp"
#include "core/vr/CombatStance.hpp"
#include "game/BodyCalibration.hpp"
#include "game/BodyScale.hpp"

#include <Windows.h>
#include <cmath>

namespace mevr::game::player {
namespace {

ue3::ClassRef gPlayerController{"Engine.PlayerController"};
ue3::ClassRef gBioPlayerController{"SFXGame.BioPlayerController"};
ue3::ClassRef gBioPawn{"SFXGame.BioPawn"};
ue3::FieldRef gPawn{"Engine.Controller", "Pawn"};
ue3::FieldRef gGameModeManager{"SFXGame.BioPlayerController", engine::adapter::kGameModeManager};
ue3::FieldRef gCurrentMode{"SFXGame.SFXGameModeManager", "CurrentMode"};
ue3::FieldRef gPlayerCamera{"Engine.PlayerController", "PlayerCamera"};
ue3::FieldRef gPcOwner{"Engine.Camera", "PCOwner"};
ue3::FieldRef gCameraCache{"Engine.Camera", "CameraCache"};
ue3::FieldRef gPov{gCameraCache, "POV"};
ue3::FieldRef gPovLocation{gPov, "Location"};
ue3::FieldRef gPovRotation{gPov, "Rotation"};
ue3::FieldRef gPovFov{gPov, "FOV"};
ue3::FieldRef gLocation{"Engine.Actor", "Location"};
ue3::FieldRef gRotation{"Engine.Actor", "Rotation"};
ue3::FieldRef gMesh{"Engine.Pawn", "Mesh"};
ue3::FieldRef gSkeletalMesh{"Engine.SkeletalMeshComponent", "SkeletalMesh"};
ue3::FieldRef gCylinder{"Engine.Pawn", "CylinderComponent"};
ue3::FieldRef gCollisionHeight{"Engine.CylinderComponent", "CollisionHeight"};
ue3::FieldRef gCrouched{"Engine.Pawn", "bIsCrouched"};
ue3::FieldRef gCoverType{"SFXGame.BioPawn", "CoverType"};
ue3::FieldRef gCoverAction{"SFXGame.BioPawn", "CoverAction"};
ue3::ClassRef gCameraNative{"SFXGame.SFXCameraNativeBase"};
ue3::ClassRef gInterpolate{"SFXGame.SFXCameraMode_Interpolate"};
ue3::FieldRef gCameraMode{"SFXGame.SFXCameraNativeBase", "CurrentCameraMode"};
ue3::FieldRef gTo{"SFXGame.SFXCameraMode_Interpolate", "To"};
ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera"};
void (*gPublishFloor)(vr::floor_lock::PawnSample const*) = nullptr;

void OnFloorCamera(ue3::Object* camera, ue3::Function*, void*, void*) {
    auto* const pc = Controller();
    if (gPublishFloor != nullptr && pc != nullptr && CameraOwner(camera) == pc) {
        auto const sample = FloorLockSample();
        gPublishFloor(&sample);
    }
}

ue3::Object* ObjectField(ue3::FieldRef& field, ue3::Object const* object) {
    return object != nullptr ? field.Read<ue3::Object*>(object).value_or(nullptr) : nullptr;
}

} // namespace

ue3::Object* Controller() {
    return ue3::runtime::LocalPlayerController();
}

ue3::Object* Pawn(ue3::Object const* controller) {
    return ObjectField(gPawn, controller);
}

ue3::Object* BioPawn(ue3::Object const* controller) {
    ue3::Object* const pawn = Pawn(controller);
    return IsBioPawn(pawn) ? pawn : nullptr;
}

bool IsBioPawn(ue3::Object const* object) {
    return ue3::IsA(object, gBioPawn);
}

bool IsPlayerController(ue3::Object const* object) {
    return ue3::IsA(object, gPlayerController);
}

bool IsBioPlayerController(ue3::Object const* object) {
    return ue3::IsA(object, gBioPlayerController);
}

std::optional<std::uint8_t> GameMode(ue3::Object const* controller) {
    if (!IsBioPlayerController(controller)) {
        return std::nullopt;
    }
    ue3::Object* const manager = ObjectField(gGameModeManager, controller);
    if (manager == nullptr) {
        return std::nullopt;
    }
    return gCurrentMode.Read<std::uint8_t>(manager);
}

ue3::Object* Camera(ue3::Object const* controller) {
    return ObjectField(gPlayerCamera, controller);
}

ue3::Object* CameraOwner(ue3::Object const* camera) {
    return ObjectField(gPcOwner, camera);
}

std::optional<Pov> CameraPov(ue3::Object* camera) {
    void* const pov = camera != nullptr ? gPov.Address(gCameraCache.Address(camera)) : nullptr;
    Pov out{gPovLocation.In<ue3::Vector>(pov), gPovRotation.In<ue3::Rotator>(pov), gPovFov.In<float>(pov)};
    if (out.location == nullptr || out.rotation == nullptr || out.fov == nullptr) {
        return std::nullopt;
    }
    return out;
}

std::optional<ue3::Vector> Location(ue3::Object const* actor) {
    return gLocation.Read<ue3::Vector>(actor);
}

std::optional<ue3::Rotator> Rotation(ue3::Object const* actor) {
    return gRotation.Read<ue3::Rotator>(actor);
}

std::optional<bool> Crouched(ue3::Object const* pawn) {
    return pawn != nullptr ? gCrouched.ReadBool(pawn) : std::nullopt;
}

void PublishFloorLockOnCamera(void (*publish)(vr::floor_lock::PawnSample const*)) {
    gPublishFloor = publish;
    if (publish != nullptr) {
        if (auto* const fn = gUpdateCamera.Get()) engine::process_event::AddPostHandler(fn, &OnFloorCamera);
    }
}

vr::floor_lock::PawnSample FloorLockSample() {
    vr::floor_lock::PawnSample out;
    out.timeMs = GetTickCount64();
    out.seated = body_calibration::Position() == vr::calibration::PlayPosition::Seated;
    out.standingEyeM = out.seated ? body_scale::StandingEyeHeight().value_or(0.0) : 0.0;
    auto* const pc = Controller();
    if (pc == nullptr) return out;
    out.onFoot = GameMode(pc) == 0;
    auto* const pawn = BioPawn(pc);
    out.pawn = pawn != nullptr;
    if (!out.onFoot || !out.pawn) return out;
    auto const type = gCoverType.Read<std::uint8_t>(pawn);
    auto const action = gCoverAction.Read<std::uint8_t>(pawn);
    out.cover = !type || !action || *type != 0 || *action != 0;
    auto* const cylinder = ObjectField(gCylinder, pawn);
    auto const at = Location(pawn);
    auto const height = cylinder != nullptr ? gCollisionHeight.Read<float>(cylinder) : std::nullopt;
    out.floorValid = at && height && std::isfinite(at->z) && std::isfinite(*height) && *height > 0.0f && *height < 300.0f;
    if (out.floorValid) out.floorZ = static_cast<double>(at->z) - *height;
    auto* const camera = Camera(pc);
    if (camera != nullptr && ue3::IsA(camera, gCameraNative)) {
        auto* mode = ObjectField(gCameraMode, camera);
        if (mode != nullptr && ue3::IsA(mode, gInterpolate)) mode = ObjectField(gTo, mode);
        if (mode != nullptr && ue3::runtime::IsAlive(mode)) {
            auto& reflection = ue3::runtime::Get();
            out.cameraOwned = vr::combat_stance::ConstantEyeApplies(reflection.NameText(reflection.ClassOf(mode)),
                reflection.NameText(mode), true); // both crouch modes qualify; no synthetic extra drop
        }
    }
    return out;
}

ue3::Object* Mesh(ue3::Object const* pawn) {
    return ObjectField(gMesh, pawn);
}

ue3::Object* SkeletalMesh(ue3::Object const* mesh) {
    return ObjectField(gSkeletalMesh, mesh);
}

} // namespace mevr::game::player
