#include "game/HeadTracking.hpp"

#include "core/Log.hpp"
#include "core/vr/Output.hpp"
#include "core/vr/ViewSync.hpp"
#include "engine/Objects.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/XrToUnreal.hpp"

#include <LESDK/Headers.hpp>

#include <atomic>

namespace mevr::game::head_tracking {
namespace {

constexpr char const* kUpdateCamera = "Function Engine.Camera.UpdateCamera";
constexpr char const* kGetPlayerViewPoint = "Function Engine.PlayerController.GetPlayerViewPoint";
constexpr char const* kGetFovAngle = "Function Engine.PlayerController.GetFOVAngle";

// Game thread only.
bool gActive = false;  // this frame's head rotation is valid
bool gOverrideFov = false;  // this frame renders at the headset's FOV
engine::UnrealRotation gHead;
float gLastFov = 0.0f;
std::atomic<bool> gApplyToCamera{true};

std::atomic<unsigned> gTaggedFrames{0};
std::atomic<unsigned> gAdjustedViews{0};
std::atomic<unsigned> gFovOverrides{0};

// Once per frame, after the camera computed this frame's view.
void OnUpdateCamera(UObject* context, UFunction* /*function*/, void* /*parms*/, void* /*result*/) {
    gActive = false;
    if (!vr::output::WantsHeadTracking()) {
        return;
    }
    auto* const camera = static_cast<ACamera*>(context);
    if (camera == nullptr || camera->PCOwner == nullptr) {
        return;
    }
    auto const head = vr::view_sync::LatestHead();
    if (!head) {
        return;
    }
    gHead = engine::ToUnrealRotation(head->orientation);
    gActive = true;

    // Render as wide as the headset shows, so the image fills the view instead of a 70 degree
    // window. Set in the camera cache (read by script) and returned from GetFOVAngle (read by
    // the renderer); the frame is tagged with whatever FOV it actually renders at.
    auto const headsetFov = vr::view_sync::HeadsetFov();
    gOverrideFov = gApplyToCamera && headsetFov.has_value();
    if (gOverrideFov) {
        camera->CameraCache.POV.FOV = *headsetFov;
    }
    gLastFov = camera->CameraCache.POV.FOV;
    vr::view_sync::PushRendered({*head, gLastFov});
    gTaggedFrames.fetch_add(1, std::memory_order_relaxed);
}

void OnGetPlayerViewPoint(UObject* context, UFunction* /*function*/, void* parms, void* /*result*/) {
    if (!gActive || !gApplyToCamera || context == nullptr || !context->IsA(APlayerController::StaticClass())) {
        return;
    }
    auto* const p = static_cast<AController_eventGetPlayerViewPoint_Parms*>(parms);
    p->out_Rotation.Yaw += gHead.yaw;
    p->out_Rotation.Pitch = gHead.pitch;
    p->out_Rotation.Roll = gHead.roll;
    gAdjustedViews.fetch_add(1, std::memory_order_relaxed);
}

void OnGetFovAngle(UObject* context, UFunction* /*function*/, void* parms, void* /*result*/) {
    if (!gActive || !gOverrideFov || context == nullptr || !context->IsA(APlayerController::StaticClass())) {
        return;
    }
    static_cast<APlayerController_eventGetFOVAngle_Parms*>(parms)->ReturnValue = gLastFov;
    gFovOverrides.fetch_add(1, std::memory_order_relaxed);
}

UFunction* FindFunction(wchar_t const* name, char const* fullName) {
    for (UFunction* function : engine::FindFunctionsNamed(name)) {
        if (engine::FullName(function) == fullName) {
            return function;
        }
    }
    return nullptr;
}

} // namespace

bool TryStartup() {
    UFunction* const updateCamera = FindFunction(L"UpdateCamera", kUpdateCamera);
    UFunction* const viewPoint = FindFunction(L"GetPlayerViewPoint", kGetPlayerViewPoint);
    UFunction* const fovAngle = FindFunction(L"GetFOVAngle", kGetFovAngle);
    if (updateCamera == nullptr || viewPoint == nullptr || fovAngle == nullptr) {
        return false;  // script packages not loaded yet
    }
    engine::process_event::AddPostHandler(updateCamera, &OnUpdateCamera);
    engine::process_event::AddPostHandler(viewPoint, &OnGetPlayerViewPoint);
    engine::process_event::AddPostHandler(fovAngle, &OnGetFovAngle);
    log::Info("head tracking ready ({}, {}, {})", kUpdateCamera, kGetPlayerViewPoint, kGetFovAngle);
    return true;
}

void LogStats() {
    unsigned const frames = gTaggedFrames.exchange(0);
    unsigned const views = gAdjustedViews.exchange(0);
    unsigned const fovs = gFovOverrides.exchange(0);
    if (frames > 0 || views > 0) {
        log::Info("    head tracking: {} frames tagged, {} views adjusted (camera {}), FOV {:.1f} ({} overrides), head yaw {} pitch {} roll {}",
            frames, views, gApplyToCamera ? "on" : "OFF", gLastFov, fovs, gHead.yaw, gHead.pitch, gHead.roll);
    } else {
        log::Info("    head tracking: idle ({})", vr::output::WantsHeadTracking() ? "waiting for a head pose" : "VR off or screen mode");
    }
}

void Shutdown() {
    gActive = false;
}

void SetApplyToCamera(bool apply) {
    gApplyToCamera = apply;
}

bool AppliesToCamera() {
    return gApplyToCamera;
}

} // namespace mevr::game::head_tracking
