#include "game/HandAim.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/Output.hpp"
#include "engine/CallFunction.hpp"
#include "engine/Objects.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HeadTracking.hpp"

#include <LESDK/Headers.hpp>

#include <atomic>

namespace mevr::game::hand_aim {
namespace {

constexpr char const* kGetAdjustedAimFor = "Function SFXGame.BioPlayerController.GetAdjustedAimFor";

UFunction* gGetAdjustedAimFor = nullptr;
bool gEnabled = false;  // game thread
std::atomic<unsigned> gAimCalls{0};
std::atomic<unsigned> gOverrides{0};

// The controller's aim ray as a world rotation: its yaw is relative to LOCAL forward, which the
// rendered view maps to the game's own (body) yaw.
bool HandRotation(FRotator& out) {
    auto const yaw = head_tracking::GameYaw();
    if (!gEnabled || !yaw || !head_tracking::OnFoot() || !vr::output::WantsHeadTracking()) {
        return false;
    }
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!controllers.active || !controllers.right.aimValid) {
        return false;
    }
    engine::UnrealRotation const aim = engine::ToUnrealRotation(controllers.right.aim.orientation);
    out.Pitch = aim.pitch;
    out.Yaw = *yaw + aim.yaw;
    out.Roll = 0;
    return true;
}

bool IsLocalPlayer(UObject* context) {
    return context != nullptr && context == engine::LocalPlayerController();
}

void Apply(UObject* context, FRotator* result) {
    gAimCalls.fetch_add(1, std::memory_order_relaxed);
    FRotator rotation{};
    if (result != nullptr && IsLocalPlayer(context) && HandRotation(rotation)) {
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

} // namespace

bool TryStartup() {
    gGetAdjustedAimFor = engine::FindFunction(L"GetAdjustedAimFor", kGetAdjustedAimFor);
    if (gGetAdjustedAimFor == nullptr) {
        return false;
    }
    engine::call_function::AddPostHandler(gGetAdjustedAimFor, &OnCallFunction);
    engine::process_event::AddPostHandler(gGetAdjustedAimFor, &OnProcessEvent);
    return true;
}

bool Toggle() {
    gEnabled = !gEnabled;
    return gEnabled;
}

void SetEnabled(bool on) {
    gEnabled = on;
}

void LogStats() {
    if (gGetAdjustedAimFor == nullptr) {
        return;
    }
    unsigned const calls = gAimCalls.exchange(0);
    unsigned const overrides = gOverrides.exchange(0);
    if (gEnabled || calls > 0) {
        log::Info("    hand aim: {}, {} aim queries, {} aimed by the right controller", gEnabled ? "on" : "off", calls, overrides);
    }
}

void Shutdown() {
    gGetAdjustedAimFor = nullptr;
    gEnabled = false;
}

} // namespace mevr::game::hand_aim
