// SPDX-License-Identifier: MIT
#pragma once

#include "ue3/Types.hpp"
#include "core/vr/FloorLock.hpp"

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
// Stable cylinder floor and actual on-foot camera ownership; reflection failures stand down.
vr::floor_lock::PawnSample FloorLockSample();
// Register before installing ProcessEvent handlers; refresh at the camera update as well as each mod tick.
void PublishFloorLockOnCamera(void (*publish)(vr::floor_lock::PawnSample const*));
std::optional<bool> Crouched(ue3::Object const* pawn);

#if defined(MEVR_GAME_LE3)
// ME3's movement admission, read in one place (docs/trilogy-parity-ledger.md Forks 11; until 2026-11-16 Comfort, ArmIk,
// Posture, Sprint, Melee and Le3Traversal each had their own copy of these reads). Game mode 0 alone doesn't mean the
// player moves Shepard: an authored scene keeps it while a custom action, ignored input, death, another physics or cover
// owns the pawn. Each fact is what the game states now; a missing object, an unreadable field or a failed call means the
// fact does NOT hold (every caller stands down on doubt).
namespace admission {
using Facts = std::uint32_t;
inline constexpr Facts kOnFoot = 1u << 0;         // BioPlayerController's game mode is 0
inline constexpr Facts kNoAction = 1u << 1;       // BioPawn.CurrentCustomAction == 0
inline constexpr Facts kPhysicsKnown = 1u << 2;   // Actor.Physics is readable (any physics)
inline constexpr Facts kWalking = 1u << 3;        // Actor.Physics == PHYS_Walking (1)
inline constexpr Facts kCoverKnown = 1u << 4;     // BioPawn.CoverType and CoverAction are readable (in cover or not)
inline constexpr Facts kOutOfCover = 1u << 5;     // BioPawn.CoverType == 0
inline constexpr Facts kNoCoverAction = 1u << 6;  // BioPawn.CoverAction == 0
inline constexpr Facts kNotCrouched = 1u << 7;    // Pawn.bIsCrouched is false
inline constexpr Facts kNotStorming = 1u << 8;    // BioPawn.bStorming is false
inline constexpr Facts kMoveInput = 1u << 9;      // PlayerController.IsMoveInputIgnored() answers false (a script call)
inline constexpr Facts kAlive = 1u << 10;         // Pawn.IsDead() answers false (a script call)
} // namespace admission
// The first fact of `wanted` that does not hold for this controller and its pawn, in the order above; 0: all of them hold;
// all of `wanted` when the controller or the pawn isn't a live object. Only wanted facts are read, and nothing after the
// first that fails (the fields come before the two script calls, as in every copy's short-circuit). A caller that reports
// reasons in an order of its own asks in that order, a few facts at a time. Game thread.
admission::Facts MovementRefused(ue3::Object* controller, ue3::Object* pawn, admission::Facts wanted);
inline bool MovementAdmitted(ue3::Object* controller, ue3::Object* pawn, admission::Facts wanted) {
    return MovementRefused(controller, pawn, wanted) == 0;
}
// Every name MovementRefused reads resolves with the shape it expects (a 4-byte Int action, Byte physics and cover, Bool
// crouch and storm, two functions that take nothing and return a bool): a module's startup asks once before it relies on
// the facts. False until the script packages have loaded.
bool MovementAdmissionReady();
#endif

// Pawn.Mesh, and a SkeletalMeshComponent's SkeletalMesh (its asset).
ue3::Object* Mesh(ue3::Object const* pawn);
ue3::Object* SkeletalMesh(ue3::Object const* mesh);

} // namespace mevr::game::player
