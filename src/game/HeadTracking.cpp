#include "game/HeadTracking.hpp"

#include "core/Log.hpp"
#include "core/vr/Output.hpp"
#include "core/vr/ViewSync.hpp"
#include "engine/Objects.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/SceneView.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HeadSim.hpp"

#include <LESDK/Headers.hpp>

#include <atomic>
#include <optional>

namespace mevr::game::head_tracking {
namespace {

constexpr char const* kUpdateCamera = "Function Engine.Camera.UpdateCamera";
constexpr char const* kGetPlayerViewPoint = "Function Engine.PlayerController.GetPlayerViewPoint";
constexpr char const* kGetFovAngle = "Function Engine.PlayerController.GetFOVAngle";
constexpr float kDefaultHalfIpdMeters = 0.032f;

// Game thread only.
Path gPath = Path::Render;
Stereo gStereo = Stereo::Mono;
bool gPositional = false;
bool gRenderPathAvailable = false;
bool gActive = false;  // gHead is valid for the view being built
bool gOverrideFov = false;  // render at the headset's FOV
std::optional<XrPosef> gHeadPose;
engine::UnrealRotation gHead;
int gEye = -1;  // eye of the view being built: -1 mono, 0 left, 1 right
unsigned gEyeParity = 0;
float gHalfIpdMeters = kDefaultHalfIpdMeters;
float gCameraFov = 0.0f;  // the camera's FOV this frame (before any override)
float gRenderedFov = 0.0f;  // the FOV the player view was built with
// Render path, if CalcSceneView turns out not to ask GetFOVAngle: override the camera's FOV
// instead (it then also reaches game logic, as on the camera path).
bool gFovViaCamera = false;

std::atomic<int> gRenderYawOffset{0};

std::atomic<unsigned> gTaggedFrames{0};
std::atomic<unsigned> gAdjustedViews{0};
std::atomic<unsigned> gFovOverrides{0};
std::atomic<unsigned> gViewPointsInPrimary{0};
std::atomic<unsigned> gFovCallsInPrimary{0};
std::atomic<unsigned> gPrimaryViews{0};
std::atomic<unsigned> gEyeViews[2]{};

// The headset when VR runs head-tracked, else the simulator. Returns whether it's the headset.
bool BeginView() {
    bool const forVr = vr::output::WantsHeadTracking();
    gHeadPose = forVr ? vr::view_sync::LatestHead() : head_sim::CurrentPose();
    gActive = gHeadPose.has_value();
    if (gActive) {
        gHead = engine::ToUnrealRotation(gHeadPose->orientation);
    }
    gOverrideFov = gActive && forVr && vr::view_sync::HeadsetFov().has_value();
    return forVr;
}

// Tagging only matters for VR: it tells the output which pose (and eye) the image has.
void TagFrame(XrPosef const& pose, float fov, int eye) {
    if (vr::output::WantsHeadTracking()) {
        vr::view_sync::PushRendered({pose, fov, eye});
        gTaggedFrames.fetch_add(1, std::memory_order_relaxed);
    }
}

void ApplyRotation(FRotator& rotation) {
    rotation.Yaw += gHead.yaw;
    rotation.Pitch = gHead.pitch;
    rotation.Roll = gHead.roll;
    gAdjustedViews.fetch_add(1, std::memory_order_relaxed);
}

// Render path only: head position (6DOF) and the stereo eye offset, in world units.
void ApplyTranslation(FVector& location, int gameYaw, FRotator const& rotation) {
    engine::UnrealVector offset{};
    if (gPositional && gHeadPose) {
        offset = engine::ToUnrealOffset(gHeadPose->position, gameYaw);
    }
    if (gEye >= 0) {
        engine::UnrealVector const right = engine::RightAxis({rotation.Pitch, rotation.Yaw, rotation.Roll});
        double const meters = (gEye == 0 ? -1.0 : 1.0) * gHalfIpdMeters;
        offset.x += right.x * meters * engine::kUnitsPerMeter;
        offset.y += right.y * meters * engine::kUnitsPerMeter;
        offset.z += right.z * meters * engine::kUnitsPerMeter;
    }
    location.X += static_cast<float>(offset.x);
    location.Y += static_cast<float>(offset.y);
    location.Z += static_cast<float>(offset.z);
}

// Once per frame, after the camera computed this frame's view (before the player view is built).
void OnUpdateCamera(UObject* context, UFunction* /*function*/, void* /*parms*/, void* /*result*/) {
    auto* const camera = static_cast<ACamera*>(context);
    if (camera == nullptr || camera->PCOwner == nullptr) {
        return;
    }
    gCameraFov = camera->CameraCache.POV.FOV;
    if (gPath == Path::Render) {
        // The head is sampled at the player view instead (as late as possible).
        auto const headsetFov = vr::view_sync::HeadsetFov();
        if (gFovViaCamera && vr::output::WantsHeadTracking() && headsetFov) {
            camera->CameraCache.POV.FOV = *headsetFov;
        }
        return;
    }
    BeginView();
    gEye = -1;
    if (gPath == Path::Camera && gOverrideFov) {
        // Set in the camera cache (read by script) and returned from GetFOVAngle.
        camera->CameraCache.POV.FOV = *vr::view_sync::HeadsetFov();
    }
    if (gHeadPose) {
        TagFrame(*gHeadPose, camera->CameraCache.POV.FOV, -1);
    }
    if (gPath == Path::Off) {
        gActive = false;  // tag only
    }
}

void OnGetPlayerViewPoint(UObject* context, UFunction* /*function*/, void* parms, void* /*result*/) {
    if (context == nullptr || !context->IsA(APlayerController::StaticClass())) {
        return;
    }
    bool const inView = gPath == Path::Render && engine::scene_view::InPrimaryView();
    if (inView) {
        gViewPointsInPrimary.fetch_add(1, std::memory_order_relaxed);
    }
    if (!gActive || !(inView || gPath == Path::Camera)) {
        return;
    }
    auto* const p = static_cast<AController_eventGetPlayerViewPoint_Parms*>(parms);
    int const gameYaw = p->out_Rotation.Yaw;
    ApplyRotation(p->out_Rotation);
    if (inView) {
        ApplyTranslation(p->out_Location, gameYaw, p->out_Rotation);
    }
}

void OnGetFovAngle(UObject* context, UFunction* /*function*/, void* parms, void* /*result*/) {
    if (context == nullptr || !context->IsA(APlayerController::StaticClass())) {
        return;
    }
    auto* const p = static_cast<APlayerController_eventGetFOVAngle_Parms*>(parms);
    bool const inView = gPath == Path::Render && engine::scene_view::InPrimaryView();
    if (gOverrideFov && (inView || gPath == Path::Camera)) {
        p->ReturnValue = *vr::view_sync::HeadsetFov();
        gFovOverrides.fetch_add(1, std::memory_order_relaxed);
    }
    if (inView) {
        gFovCallsInPrimary.fetch_add(1, std::memory_order_relaxed);
        gRenderedFov = p->ReturnValue;
    }
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

void OnBeforePrimaryView() {
    gPrimaryViews.fetch_add(1, std::memory_order_relaxed);
    if (gPath != Path::Render) {
        return;
    }
    bool const forVr = BeginView();
    gRenderYawOffset = gActive ? gHead.yaw : 0;
    gEye = -1;
    if (!gActive) {
        return;
    }
    XrPosef pose = *gHeadPose;
    if (forVr && gStereo == Stereo::AlternateEye) {
        gEye = static_cast<int>(gEyeParity++ & 1u);
        gHalfIpdMeters = vr::view_sync::HalfIpd().value_or(kDefaultHalfIpdMeters);
        pose = engine::OffsetRight(pose, gEye == 0 ? -gHalfIpdMeters : gHalfIpdMeters);
        gEyeViews[gEye].fetch_add(1, std::memory_order_relaxed);
    }
    // The FOV this view renders with isn't known until the engine asks for it, so the frame is
    // tagged with the last view's (the FOV rarely changes between frames).
    TagFrame(pose, gRenderedFov > 0.0f && !gFovViaCamera ? gRenderedFov : gCameraFov, gEye);
}

bool TryStartup(bool renderPathAvailable) {
    UFunction* const updateCamera = FindFunction(L"UpdateCamera", kUpdateCamera);
    UFunction* const viewPoint = FindFunction(L"GetPlayerViewPoint", kGetPlayerViewPoint);
    UFunction* const fovAngle = FindFunction(L"GetFOVAngle", kGetFovAngle);
    if (updateCamera == nullptr || viewPoint == nullptr || fovAngle == nullptr) {
        return false;  // script packages not loaded yet
    }
    engine::process_event::AddPostHandler(updateCamera, &OnUpdateCamera);
    engine::process_event::AddPostHandler(viewPoint, &OnGetPlayerViewPoint);
    engine::process_event::AddPostHandler(fovAngle, &OnGetFovAngle);
    gRenderPathAvailable = renderPathAvailable;
    gPath = renderPathAvailable ? Path::Render : Path::Camera;
    log::Info("head tracking ready ({}, {}, {}); path {}", kUpdateCamera, kGetPlayerViewPoint, kGetFovAngle, Name(gPath));
    return true;
}

void LogStats() {
    unsigned const frames = gTaggedFrames.exchange(0);
    unsigned const views = gAdjustedViews.exchange(0);
    unsigned const fovs = gFovOverrides.exchange(0);
    unsigned const primary = gPrimaryViews.exchange(0);
    unsigned const viewPoints = gViewPointsInPrimary.exchange(0);
    unsigned const fovCalls = gFovCallsInPrimary.exchange(0);
    unsigned const leftViews = gEyeViews[0].exchange(0);
    unsigned const rightViews = gEyeViews[1].exchange(0);
    char const* const source = vr::output::WantsHeadTracking() ? "headset"
        : head_sim::Current() != head_sim::Pattern::Off   ? head_sim::Name(head_sim::Current())
                                                           : "none";
    log::Info("    head tracking: path {}, source {}, {} frames tagged, {} views adjusted, FOV {:.1f} ({} overrides), head yaw {} pitch {} roll {}",
        Name(gPath), source, frames, views, gRenderedFov > 0.0f ? gRenderedFov : gCameraFov, fovs, gHead.yaw, gHead.pitch, gHead.roll);
    if (gPath == Path::Render) {
        log::Info("    render path: {} player views, {} GetPlayerViewPoint and {} GetFOVAngle calls inside them; 6DOF {}, stereo {} (L {} / R {}, half IPD {:.1f} mm)",
            primary, viewPoints, fovCalls, gPositional ? "on" : "off", Name(gStereo), leftViews, rightViews,
            gHalfIpdMeters * 1000.0f);
        // The render path relies on CalcSceneView asking script for the view point (stock UE3).
        // If LE1 doesn't, nothing rotates: fall back to the camera path rather than do nothing.
        if (primary > 0 && viewPoints == 0) {
            gPath = Path::Camera;
            gRenderPathAvailable = false;
            gRenderYawOffset = 0;
            log::Warn("render path: CalcSceneView never asked for the view point; falling back to the camera path");
        } else if (primary > 0 && fovCalls == 0 && !gFovViaCamera) {
            gFovViaCamera = true;
            log::Warn("render path: CalcSceneView never asked for the FOV; overriding the camera FOV instead");
        }
    }
}

void Shutdown() {
    gActive = false;
    gHeadPose.reset();
    gEye = -1;
    gRenderYawOffset = 0;
}

Path CyclePath() {
    gPath = gPath == Path::Render ? Path::Camera
        : gPath == Path::Camera   ? Path::Off
                                  : (gRenderPathAvailable ? Path::Render : Path::Camera);
    gActive = false;
    gEye = -1;
    gRenderYawOffset = 0;
    return gPath;
}

char const* Name(Path path) {
    switch (path) {
    case Path::Render:
        return "render";
    case Path::Camera:
        return "camera";
    default:
        return "off";
    }
}

Stereo CycleStereo() {
    gStereo = gStereo == Stereo::Mono ? Stereo::AlternateEye : Stereo::Mono;
    return gStereo;
}

char const* Name(Stereo stereo) {
    return stereo == Stereo::AlternateEye ? "alternate-eye" : "mono";
}

bool TogglePositional() {
    gPositional = !gPositional;
    return gPositional;
}

int RenderYawOffset() {
    return gRenderYawOffset.load(std::memory_order_relaxed);
}

} // namespace mevr::game::head_tracking
