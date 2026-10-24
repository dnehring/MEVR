// SPDX-License-Identifier: MIT
#include "game/Player.hpp"

#include "engine/GameAdapter.hpp"
#include "ue3/Handles.hpp"

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

ue3::Object* Mesh(ue3::Object const* pawn) {
    return ObjectField(gMesh, pawn);
}

ue3::Object* SkeletalMesh(ue3::Object const* mesh) {
    return ObjectField(gSkeletalMesh, mesh);
}

} // namespace mevr::game::player
