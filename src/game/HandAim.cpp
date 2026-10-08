#include "game/HandAim.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "engine/CallFunction.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/Objects.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HeadTracking.hpp"
#include "game/ViewAnchor.hpp"

#include <LESDK/Headers.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace mevr::game::hand_aim {
namespace {

constexpr char const* kGetAdjustedAimFor = "Function SFXGame.BioPlayerController.GetAdjustedAimFor";
#if defined(SDK_TARGET_LE1)
// Diagnostics only: the weapon's own aim query, which gets the shot's start (StartFireLoc).
constexpr char const* kWeaponGetAdjustedAim = "Function SFXGame.BioWeaponRanged.GetAdjustedAim";
#endif

UFunction* gGetAdjustedAimFor = nullptr;
bool gEnabled = false;  // game thread
std::atomic<unsigned> gAimCalls{0};
std::atomic<unsigned> gOverrides{0};
std::atomic<unsigned> gNoAnchor{0};
bool gLoggedOverride = false;
bool gLoggedShotStart = false;
// The last override's ray (world), for the shot-start diagnostic. Game thread.
bool gLastRayValid = false;
engine::UnrealVector gLastRayStart{};
engine::UnrealVector gLastRayDirection{};

void LogFirstOverride(engine::TrackingFrame const& frame, FRotator const& game, FRotator const& hand,
    engine::UnrealVector const& handWorld) {
    gLoggedOverride = true;
    double distanceToPawn = -1.0;
    APlayerController* const controller = engine::LocalPlayerController();
    if (controller != nullptr && controller->Pawn != nullptr) {
        FVector const& at = engine::adapter::Location(controller->Pawn);
        distanceToPawn = std::hypot(handWorld.x - at.X, handWorld.y - at.Y, handWorld.z - at.Z);
    }
    log::Info("hand aim: first shot aimed by the right controller: game aim pitch {} yaw {} -> controller pitch {} yaw {}; "
              "hand at ({:.0f}, {:.0f}, {:.0f}), {:.0f} units from the head and {:.0f} from the pawn",
        game.Pitch, game.Yaw, hand.Pitch, hand.Yaw, handWorld.x, handWorld.y, handWorld.z,
        std::hypot(handWorld.x - frame.headWorld.x, handWorld.y - frame.headWorld.y, handWorld.z - frame.headWorld.z),
        distanceToPawn);
}

// The right controller's aim ray as a world rotation, unwound next to the game's own aim. False
// (the game's aim stays) unless on foot with a tracked controller and a valid view anchor.
bool HandRotation(FRotator const& game, FRotator& out) {
    if (!gEnabled || !head_tracking::OnFoot()) {
        return false;
    }
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!controllers.active || !controllers.right.aimValid) {
        return false;
    }
    auto const frame = view_anchor::Current();
    if (!frame) {
        gNoAnchor.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    XrPosef const& aim = controllers.right.aim;
    engine::Basis const ray = engine::OrientationToWorld(*frame, aim.orientation);
    engine::UnrealRotation const rotation = engine::ToRotation(ray.forward);
    out.Pitch = engine::Unwind(rotation.pitch, game.Pitch);
    out.Yaw = engine::Unwind(rotation.yaw, game.Yaw);
    out.Roll = 0;
    gLastRayStart = engine::ToWorld(*frame, aim.position);
    gLastRayDirection = ray.forward;
    gLastRayValid = true;
    if (!gLoggedOverride) {
        LogFirstOverride(*frame, game, out, gLastRayStart);
    }
    return true;
}

bool IsLocalPlayer(UObject* context) {
    return context != nullptr && context == engine::LocalPlayerController();
}

void Apply(UObject* context, FRotator* result) {
    gAimCalls.fetch_add(1, std::memory_order_relaxed);
    gLastRayValid = false;
    FRotator rotation{};
    if (result != nullptr && IsLocalPlayer(context) && HandRotation(*result, rotation)) {
        *result = rotation;
        gOverrides.fetch_add(1, std::memory_order_relaxed);
    }
}

// Script-to-script calls (the weapon's fire code): `result` is the return value.
void OnCallFunction(UObject* context, UFunction* /*function*/, void* result) {
    Apply(context, static_cast<FRotator*>(result));
}

// Native-to-script calls: the return value lives in the parms.
void OnProcessEvent(UObject* context, UFunction* /*function*/, void* parms, void* /*result*/) {
    Apply(context, &static_cast<ABioPlayerController_execGetAdjustedAimFor_Parms*>(parms)->ReturnValue);
}

#if defined(SDK_TARGET_LE1)
// Once: where the shot starts relative to the controller's ray. The override sets a direction
// only, so a shot leaving StartFireLoc runs parallel to the ray, this far off it.
void OnWeaponAim(UObject* /*context*/, UFunction* /*function*/, void* parms, void* /*result*/) {
    if (gLoggedShotStart || !gLastRayValid) {
        return;
    }
    gLoggedShotStart = true;
    FVector const& start = static_cast<ABioWeaponRanged_execGetAdjustedAim_Parms*>(parms)->StartFireLoc;
    engine::UnrealVector const d = gLastRayDirection;
    double const length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    engine::UnrealVector const v{start.X - gLastRayStart.x, start.Y - gLastRayStart.y, start.Z - gLastRayStart.z};
    double const along = length > 0.0 ? (v.x * d.x + v.y * d.y + v.z * d.z) / length : 0.0;
    double const total = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    log::Info("hand aim: shots start at ({:.0f}, {:.0f}, {:.0f}), {:.0f} units from the hand: {:.0f} along the "
              "controller's ray and {:.0f} off it (how far shots pass beside where it points)",
        start.X, start.Y, start.Z, total, along, std::sqrt(std::max(0.0, total * total - along * along)));
}
#endif

} // namespace

bool TryStartup() {
    gGetAdjustedAimFor = engine::FindFunction(L"GetAdjustedAimFor", kGetAdjustedAimFor);
    if (gGetAdjustedAimFor == nullptr) {
        return false;
    }
    engine::call_function::AddPostHandler(gGetAdjustedAimFor, &OnCallFunction);
    engine::process_event::AddPostHandler(gGetAdjustedAimFor, &OnProcessEvent);
#if defined(SDK_TARGET_LE1)
    if (UFunction* const weaponAim = engine::FindFunction(L"GetAdjustedAim", kWeaponGetAdjustedAim)) {
        engine::process_event::AddPostHandler(weaponAim, &OnWeaponAim);
    }
#endif
    return true;
}

bool Toggle() {
    gEnabled = !gEnabled;
    return gEnabled;
}

void SetEnabled(bool on) {
    gEnabled = on;
}

bool Enabled() {
    return gEnabled;
}

void LogStats() {
    if (gGetAdjustedAimFor == nullptr) {
        return;
    }
    unsigned const calls = gAimCalls.exchange(0);
    unsigned const overrides = gOverrides.exchange(0);
    unsigned const noAnchor = gNoAnchor.exchange(0);
    if (gEnabled || calls > 0) {
        log::Info("    hand aim: {}, {} aim queries, {} aimed by the right controller, {} kept the game's aim (no view anchor)",
            gEnabled ? "on" : "off", calls, overrides, noAnchor);
    }
}

void Shutdown() {
    gGetAdjustedAimFor = nullptr;
    gEnabled = false;
    gLoggedOverride = false;
    gLoggedShotStart = false;
    gLastRayValid = false;
}

} // namespace mevr::game::hand_aim
