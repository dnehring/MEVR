#include "game/CameraProbe.hpp"

#include "core/Log.hpp"
#include "core/ik/CameraHook.hpp"
#include "engine/GameAdapterSdk.hpp"
#include "engine/Objects.hpp"
#include "engine/Skeleton.hpp"
#include "game/HeadTracking.hpp"

#include <LESDK/Headers.hpp>

#include <Windows.h>

#include <cstddef>
#include <cstring>
#include <format>
#include <numbers>
#include <string>

namespace mevr::game::camera_probe {
namespace {

#if defined(SDK_TARGET_LE1)

// The SDK's layouts, as research/18 read them from the code (CalculateOffsetHook 0xCAEF50,
// GetCameraLocation 0xCB2240).
static_assert(offsetof(APlayerController, PlayerCamera) == 0x6B0);
static_assert(offsetof(ASFXCameraNativeBase, CurrentCameraMode) == 0x568);
static_assert(offsetof(USFXCameraMode, m_pov) == 0x98);
static_assert(offsetof(USFXCameraMode, Offset) == 0xB4);
static_assert(offsetof(USFXCameraMode, HookOffset) == 0xC0);
static_assert(offsetof(USFXCameraMode, LastHookPos) == 0xCC);
static_assert(offsetof(USFXCameraMode, HookName) == 0xE4);
static_assert(offsetof(USFXCameraMode_Interpolate, To) == 0x168);
static_assert(sizeof(SFXName) == sizeof(std::uint64_t));

constexpr ULONGLONG kMinIntervalMs = 250;
constexpr double kRotatorToRadians = std::numbers::pi / 32768.0;

ik::CameraModeKey gLogged;
bool gHaveLogged = false;
std::string gLoggedMode;
ULONGLONG gLastLogMs = 0;

std::uint64_t Key(SFXName const& name) {
    std::uint64_t key = 0;
    std::memcpy(&key, &name, sizeof key);
    return key;
}

ik::Vec3 V(FVector const& v) {
    return {v.X, v.Y, v.Z};
}

std::string Text(ik::Vec3 const& v) {
    return std::format("({:.1f}, {:.1f}, {:.1f})", v.x, v.y, v.z);
}

std::string ModeName(UObject* mode) {
    return mode != nullptr ? std::format("{} '{}'", engine::FullName(mode->Class), engine::FullName(mode)) : std::string("none");
}

// The hook bone against the game camera and the formula's camera against the real one.
std::string HookPlacement(USFXCameraMode const* mode, ASFXCameraNativeBase const* camera, std::uint64_t hookName) {
    APlayerController* const controller = engine::LocalPlayerController();
    APawn* const pawn = controller != nullptr ? controller->Pawn : nullptr;
    if (pawn == nullptr || !pawn->IsA(ABioPawn::StaticClass()) || pawn->Mesh == nullptr || pawn->Mesh->SkeletalMesh == nullptr) {
        return "no player body";
    }
    USkeletalMeshComponent* const mesh = pawn->Mesh;
    int const bone = engine::skeleton::FindBone(mesh, hookName);
    if (bone < 0) {
        return std::format("the hook isn't a bone of {} (None: the feet; CoverSlot: the cover slot)", engine::FullName(mesh->SkeletalMesh));
    }
    auto const at = engine::skeleton::BoneLocation(mesh, bone, engine::skeleton::Space::World);
    if (!at) {
        return std::format("hook bone {} not posed", bone);
    }
    ik::Vec3 const hook{at->x, at->y, at->z};
    FTPOV const& pov = camera->CameraCache.POV;
    ik::Vec3 const game = V(engine::adapter::Location(pov));
    double const gameYaw = pov.Rotation.Yaw * kRotatorToRadians;
    ik::Vec3 const fromView = ik::InViewAxes(hook - game, gameYaw);
    // GetCameraLocation's formula, with the mode's own view rotation (what it used).
    FTPOV const& own = mode->m_pov;
    ik::Vec3 const predicted = ik::CameraFromHook(hook, V(mode->HookOffset), V(mode->Offset), own.Rotation.Pitch * kRotatorToRadians,
        own.Rotation.Yaw * kRotatorToRadians);
    ik::Vec3 const modeAt = V(engine::adapter::Location(own));
    return std::format("hook bone {}({}) at {} world, {:.1f} forward, {:.1f} right, {:.1f} up of the game camera {} (yaw {}); "
                       "the formula from it puts the camera at {}: {:.1f} units from the mode's m_pov {}, {:.1f} from the "
                       "game camera; LastHookPos {} is {:.1f} from the bone",
        engine::ToString(engine::skeleton::NameOf(hookName)), bone, Text(hook), fromView.x, fromView.y, fromView.z, Text(game),
        pov.Rotation.Yaw, Text(predicted), ik::Length(predicted - modeAt), Text(modeAt), ik::Length(predicted - game),
        Text(V(mode->LastHookPos)), ik::Length(V(mode->LastHookPos) - hook));
}

void TickLe1() {
    if (!head_tracking::OnFoot()) {
        return;  // conversations and cutscenes cut between their own cameras
    }
    APlayerController* const controller = engine::LocalPlayerController();
    ACamera* const base = controller != nullptr ? controller->PlayerCamera : nullptr;
    if (base == nullptr || !base->IsA(ASFXCameraNativeBase::StaticClass())) {
        return;
    }
    auto* const camera = static_cast<ASFXCameraNativeBase*>(base);
    USFXCameraMode* const mode = camera->CurrentCameraMode;
    if (mode == nullptr || !engine::IsAlive(mode)) {
        return;
    }
    // The mode whose hook is logged: an interpolation's target (it blends from one mode to it).
    USFXCameraMode* placing = mode;
    std::string to;
    if (mode->IsA(USFXCameraMode_Interpolate::StaticClass())) {
        USFXCameraMode* const target = static_cast<USFXCameraMode_Interpolate*>(mode)->To;
        if (target != nullptr && engine::IsAlive(target)) {
            to = std::format(", interpolating to {} (its hook below)", ModeName(target));
            placing = target;
        }
    }
    ik::CameraModeKey const key{reinterpret_cast<std::uintptr_t>(mode), Key(placing->HookName), V(placing->HookOffset), V(placing->Offset)};
    if (gHaveLogged && !ik::CameraModeChanged(gLogged, key)) {
        return;
    }
    ULONGLONG const now = GetTickCount64();
    if (gHaveLogged && now - gLastLogMs < kMinIntervalMs) {
        return;  // seen again next tick
    }
    std::string const name = ModeName(mode);
    bool const sameMode = gHaveLogged && gLogged.mode == key.mode;
    log::Info("camera: mode {}{}{}: HookName {}, HookOffset {}, Offset {}{}; {}", name, to,
        sameMode ? " (its hook or offset changed)" : gHaveLogged ? std::format(" (was {})", gLoggedMode) : std::string(),
        engine::ToString(placing->HookName), Text(key.hookOffset), Text(key.offset), placing->bFirstPerson ? ", bFirstPerson" : "",
        HookPlacement(placing, camera, key.hookName));
    gLogged = key;
    gLoggedMode = name;
    gHaveLogged = true;
    gLastLogMs = now;
}

#endif

} // namespace

void Tick() {
#if defined(SDK_TARGET_LE1)
    TickLe1();
#endif
}

void Shutdown() {
#if defined(SDK_TARGET_LE1)
    gLogged = {};
    gHaveLogged = false;
    gLoggedMode.clear();
    gLastLogMs = 0;
#endif
}

} // namespace mevr::game::camera_probe
