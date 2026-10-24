// SPDX-License-Identifier: MIT
#pragma once

#include "ue3/Types.hpp"

#include <cstdint>
#include <optional>

// The local player's objects and the fields most modules read, through our reflection layer
// (ue3/, research/34): one set of handles instead of one per module. Every accessor answers
// nullptr / nullopt when the layer is off, a lookup failed (the handle logs it once) or the object
// isn't there, so callers stand down. LE1 (LE2/LE3 have no reflection layout yet: nothing there).
// Game thread only.
namespace mevr::game::player {

// GameEngine.GamePlayers[0].Actor.
ue3::Object* Controller();
// Controller.Pawn.
ue3::Object* Pawn(ue3::Object const* controller);
// The controller's pawn if it is an SFXGame.BioPawn (Shepard on foot; not the Mako).
ue3::Object* BioPawn(ue3::Object const* controller);
bool IsBioPawn(ue3::Object const* object);
bool IsPlayerController(ue3::Object const* object);
bool IsBioPlayerController(ue3::Object const* object);
// BioPlayerController's SFXGameModeManager.CurrentMode (EGameModes: 0 = on foot), if it has a manager.
std::optional<std::uint8_t> GameMode(ue3::Object const* controller);

// PlayerController.PlayerCamera.
ue3::Object* Camera(ue3::Object const* controller);
// Camera.PCOwner.
ue3::Object* CameraOwner(ue3::Object const* camera);
// Camera.CameraCache.POV, in place (writable).
struct Pov {
    ue3::Vector* location = nullptr;
    ue3::Rotator* rotation = nullptr;
    float* fov = nullptr;
};
std::optional<Pov> CameraPov(ue3::Object* camera);

// Actor.Location / Actor.Rotation.
std::optional<ue3::Vector> Location(ue3::Object const* actor);
std::optional<ue3::Rotator> Rotation(ue3::Object const* actor);
// Pawn.Mesh, and a SkeletalMeshComponent's SkeletalMesh (its asset).
ue3::Object* Mesh(ue3::Object const* pawn);
ue3::Object* SkeletalMesh(ue3::Object const* mesh);

} // namespace mevr::game::player
