// SPDX-License-Identifier: MIT
#include "game/Player.hpp"

#include "engine/GameAdapter.hpp"
#include "engine/ProcessEvent.hpp"
#include "ue3/Handles.hpp"
#include "core/vr/CombatStance.hpp"
#include "game/BodyCalibration.hpp"
#include "game/BodyScale.hpp"
#include "game/Traversal.hpp"
#include "core/vr/ActionCamera.hpp"

#include <Windows.h>
#include <cmath>

namespace mevr::game::player {
namespace {

// kEveryGame: the reads head tracking and hand aim's aim query make on LE2 too (engine/Legacy's, moved onto our layer in
// M1, docs/le2-continuation-plan.md); the rest resolve where the ME1 modules use the layer (LE1).
ue3::ClassRef gPlayerController{"Engine.PlayerController", ue3::kEveryGame};
ue3::ClassRef gBioPlayerController{"SFXGame.BioPlayerController", ue3::kEveryGame};
ue3::ClassRef gBioPawn{"SFXGame.BioPawn", ue3::kEveryGame};
ue3::FieldRef gPawn{"Engine.Controller", "Pawn", ue3::kEveryGame};
ue3::FieldRef gGameModeManager{"SFXGame.BioPlayerController", engine::adapter::kGameModeManager, ue3::kEveryGame};
ue3::FieldRef gCurrentMode{"SFXGame.SFXGameModeManager", "CurrentMode", ue3::kEveryGame};
ue3::FieldRef gPlayerCamera{"Engine.PlayerController", "PlayerCamera", ue3::kEveryGame};  // view_anchor, hand aim (LE2: M3)
ue3::FieldRef gPcOwner{"Engine.Camera", "PCOwner", ue3::kEveryGame};
ue3::FieldRef gCameraCache{"Engine.Camera", "CameraCache", ue3::kEveryGame};
ue3::FieldRef gPov{gCameraCache, "POV"};
ue3::FieldRef gPovLocation{gPov, "Location"};
ue3::FieldRef gPovRotation{gPov, "Rotation"};
ue3::FieldRef gPovFov{gPov, "FOV"};
ue3::FieldRef gLocation{"Engine.Actor", "Location", ue3::kEveryGame};
ue3::FieldRef gRotation{"Engine.Actor", "Rotation", ue3::kEveryGame};
ue3::FieldRef gMesh{"Engine.Pawn", "Mesh", ue3::kEveryGame};
ue3::FieldRef gSkeletalMesh{"Engine.SkeletalMeshComponent", "SkeletalMesh", ue3::kEveryGame};
// The floor lock's sample (FloorLockSample, HostServices v36) on every game: the pawn's floor, cover and camera mode
// (research/le2-renderer-integration.md "Step 8: the floor lock on ME2").
ue3::FieldRef gCylinder{"Engine.Pawn", "CylinderComponent", ue3::kEveryGame};
ue3::FieldRef gCollisionHeight{"Engine.CylinderComponent", "CollisionHeight", ue3::kEveryGame};
// The game's crouch (Crouched): every game's Engine.Pawn field. It was ME1's handle only, so on the sequels body scale's
// standing reference read "crouched" for good and never settled (the ME2 and ME3 audits' finding 8, lane AU2 2026-11-18).
#if defined(MEVR_GAME_LE1)
ue3::FieldRef gCrouched{"Engine.Pawn", "bIsCrouched"};
#else
ue3::FieldRef gCrouched{"Engine.Pawn", "bIsCrouched", ue3::kEveryGame};
#endif
ue3::FieldRef gCoverType{"SFXGame.BioPawn", "CoverType", ue3::kEveryGame};
ue3::FieldRef gCoverAction{"SFXGame.BioPawn", "CoverAction", ue3::kEveryGame};
ue3::ClassRef gCameraNative{"SFXGame.SFXCameraNativeBase", ue3::kEveryGame};
ue3::ClassRef gInterpolate{"SFXGame.SFXCameraMode_Interpolate", ue3::kEveryGame};
ue3::FieldRef gCameraMode{"SFXGame.SFXCameraNativeBase", "CurrentCameraMode", ue3::kEveryGame};
ue3::FieldRef gTo{"SFXGame.SFXCameraMode_Interpolate", "To", ue3::kEveryGame};
ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera", ue3::kEveryGame};
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
    // A native vault or climb: the host holds its correction (MoveHold). Not ME3's roll: the pawn's floor doesn't move
    // there, and following the game's camera dipped the eye 0.5 m with the body (lane AU2, 2026-11-18).
    if constexpr (engine::adapter::kActionCameras) {
        out.traversalMove = traversal::FloorMoving();
    } else {
        out.traversalMove = traversal::Moving();
    }
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
            if constexpr (engine::adapter::kActionCameras) {
                // ME3's action cameras (a roll, a melee, a hit reaction) are the body's too: the floor stays locked through them.
                out.cameraOwned = out.cameraOwned || vr::IsActionCamera(reflection.NameText(reflection.ClassOf(mode)));
            }
        }
    }
    return out;
}

#if defined(MEVR_GAME_LE3)
namespace {
// ME3's names, all in engine/ReflectCheck's LE3 tables (the six modules that had copies of these put them there).
ue3::FieldRef gAdmitAction{"SFXGame.BioPawn", "CurrentCustomAction", ue3::kEveryGame};
ue3::FieldRef gAdmitPhysics{"Engine.Actor", "Physics", ue3::kEveryGame};
ue3::FieldRef gAdmitCrouched{"Engine.Pawn", "bIsCrouched", ue3::kEveryGame};
ue3::FieldRef gAdmitStorming{"SFXGame.BioPawn", "bStorming", ue3::kEveryGame};
ue3::FunctionRef gAdmitMoveIgnored{"Engine.PlayerController.IsMoveInputIgnored", ue3::kEveryGame};
ue3::FunctionRef gAdmitDead{"Engine.Pawn.IsDead", ue3::kEveryGame};
constexpr std::uint8_t kPhysWalking = 1;

// A script function without parameters that returns a bool (a 4-byte block): true only if it answered false.
bool AnswersFalse(ue3::Object* object, ue3::FunctionRef& ref) {
    ue3::Params params(ue3::runtime::Get(), ref.Get());
    return params.Valid() && params.Size() == 4 && ue3::runtime::Call(object, ref.Get(), params) && params.ReturnBool() == false;
}
} // namespace

admission::Facts MovementRefused(ue3::Object* controller, ue3::Object* pawn, admission::Facts wanted) {
    namespace a = admission;
    if (!ue3::runtime::IsAlive(controller) || !ue3::runtime::IsAlive(pawn)) return wanted;
    auto const asked = [wanted](a::Facts fact) { return (wanted & fact) != 0; };
    if (asked(a::kOnFoot) && GameMode(controller) != 0) return a::kOnFoot;
    if (asked(a::kNoAction) && gAdmitAction.Read<std::int32_t>(pawn) != 0) return a::kNoAction;
    if (asked(a::kPhysicsKnown | a::kWalking)) {
        auto const physics = gAdmitPhysics.Read<std::uint8_t>(pawn);
        if (asked(a::kPhysicsKnown) && !physics) return a::kPhysicsKnown;
        if (asked(a::kWalking) && physics != kPhysWalking) return a::kWalking;
    }
    if (asked(a::kCoverKnown | a::kOutOfCover | a::kNoCoverAction)) {
        auto const type = gCoverType.Read<std::uint8_t>(pawn);
        auto const action = asked(a::kCoverKnown | a::kNoCoverAction) ? gCoverAction.Read<std::uint8_t>(pawn) : std::nullopt;
        if (asked(a::kCoverKnown) && (!type || !action)) return a::kCoverKnown;
        if (asked(a::kOutOfCover) && type != 0) return a::kOutOfCover;
        if (asked(a::kNoCoverAction) && action != 0) return a::kNoCoverAction;
    }
    if (asked(a::kNotCrouched) && gAdmitCrouched.ReadBool(pawn) != false) return a::kNotCrouched;
    if (asked(a::kNotStorming) && gAdmitStorming.ReadBool(pawn) != false) return a::kNotStorming;
    if (asked(a::kMoveInput) && !AnswersFalse(controller, gAdmitMoveIgnored)) return a::kMoveInput;
    if (asked(a::kAlive) && !AnswersFalse(pawn, gAdmitDead)) return a::kAlive;
    return 0;
}

bool MovementAdmissionReady() {
    if (!ue3::runtime::Up()) return false;
    auto const field = [](ue3::FieldRef& ref, ue3::Kind kind, int bytes) {
        auto const* const info = ref.Get();
        return info != nullptr && info->kind == kind && info->elementSize == bytes && info->arrayDim == 1;
    };
    auto const answersBool = [](ue3::FunctionRef& ref) {
        ue3::Params params(ue3::runtime::Get(), ref.Get());
        auto const* const value = params.ReturnSpec();
        return params.Valid() && params.Size() == 4 && params.Specs().size() == 1 && value != nullptr &&
            value->kind == ue3::Kind::Bool && value->size == 4 && value->offset == 0;
    };
    return field(gAdmitAction, ue3::Kind::Int, 4) && field(gAdmitPhysics, ue3::Kind::Byte, 1) &&
        field(gCoverType, ue3::Kind::Byte, 1) && field(gCoverAction, ue3::Kind::Byte, 1) &&
        field(gAdmitCrouched, ue3::Kind::Bool, 4) && field(gAdmitStorming, ue3::Kind::Bool, 4) &&
        answersBool(gAdmitMoveIgnored) && answersBool(gAdmitDead);
}
#endif

ue3::Object* Mesh(ue3::Object const* pawn) {
    return ObjectField(gMesh, pawn);
}

ue3::Object* SkeletalMesh(ue3::Object const* mesh) {
    return ObjectField(gSkeletalMesh, mesh);
}

} // namespace mevr::game::player
