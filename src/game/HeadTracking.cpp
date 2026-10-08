#include "game/HeadTracking.hpp"

#include "core/Log.hpp"
#include "core/vr/Output.hpp"
#include "core/vr/ViewSync.hpp"
#include "engine/Objects.hpp"
#include "engine/GameAdapter.hpp"
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
constexpr unsigned char kGameModeDefault = 0;  // SFXGameModeManager.EGameModes: on foot, no GUI
// Self-checks decide only after this many heartbeat windows in a row agree.
constexpr unsigned kWindowsToDecide = 2;

// Game thread only.
bool gReady = false;  // handlers registered and the ProcessEvent hook live
Path gPath = Path::Render;
Stereo gStereo = Stereo::Mono;
bool gPositional = false;
bool gRenderPathAvailable = false;
bool gActive = false;  // gHead is valid for the view being built
std::optional<float> gHeadsetFov;  // snapshot for this view when rendering at the headset's FOV
std::optional<XrPosef> gHeadPose;
engine::UnrealRotation gHead;
int gEye = -1;  // eye of the view being built: -1 mono, 0 left, 1 right
unsigned gEyeParity = 0;
int gTickEye = 0;  // the eye this tick renders (alternate-eye stereo)
float gHalfIpdMeters = kDefaultHalfIpdMeters;
float gCameraFov = 0.0f;  // the camera's FOV this frame, including any override written to it
float gRenderedFov = 0.0f;  // the FOV the last player view was built with (stats)
bool gOnFoot = false;  // on-foot gameplay (no conversation, GUI, vehicle, wheels)
std::optional<unsigned char> gGameMode;  // EGameModes as of the last camera update
std::optional<int> gGameYaw;  // the game's yaw under the head rotation, from the last rotated view
std::optional<ViewAnchor> gAnchor;  // render path: the last rotated player view's head placement
bool gTaggedThisTick = false;
bool gPrimaryThisTick = false;
// Render path, if CalcSceneView turns out not to ask GetFOVAngle: override the camera's FOV
// instead (it then also reaches game logic, as on the camera path).
bool gFovViaCamera = false;
unsigned gNoViewPointWindows = 0;
unsigned gNoFovWindows = 0;
unsigned gMissedViewWindows = 0;

std::atomic<int> gRenderYawOffset{0};

std::atomic<unsigned> gTaggedFrames{0};
std::atomic<unsigned> gTickTags{0};
std::atomic<unsigned> gAdjustedViews{0};
std::atomic<unsigned> gFovOverrides{0};
std::atomic<unsigned> gViewPointsInPrimary{0};
std::atomic<unsigned> gFovCallsInPrimary{0};
std::atomic<unsigned> gPrimaryViews{0};
std::atomic<unsigned> gEyeViews[2]{};

// The headset when VR runs head-tracked, else the simulator. Returns whether it's the headset.
// The headset FOV is read once here, so the override and the frame's tag always agree.
bool BeginView() {
    bool const forVr = vr::output::WantsHeadTracking();
    gHeadPose = forVr ? vr::view_sync::LatestHead() : head_sim::CurrentPose();
    gActive = gHeadPose.has_value();
    if (gActive) {
        gHead = engine::ToUnrealRotation(gHeadPose->orientation);
    }
    gHeadsetFov = gActive && forVr ? vr::view_sync::HeadsetFov() : std::nullopt;
    return forVr;
}

// Tagging only matters for VR: it tells the output which pose (and eye) the image has. One
// queue entry per tick: a second view in the same tick replaces the first, so the tick's single
// Present consumes exactly one (otherwise the lockstep would wait for a Present that never comes).
void TagFrame(XrPosef const& pose, float fov, int eye) {
    if (!vr::output::WantsHeadTracking()) {
        return;
    }
    if (gTaggedThisTick) {
        vr::view_sync::ReplaceNewest({pose, fov, eye});
    } else {
        vr::view_sync::PushRendered({pose, fov, eye});
        gTaggedFrames.fetch_add(1, std::memory_order_relaxed);
    }
    gTaggedThisTick = true;
}

void ApplyRotation(FRotator& rotation) {
    rotation.Yaw += gHead.yaw;
    rotation.Pitch = gHead.pitch;
    rotation.Roll = gHead.roll;
    gAdjustedViews.fetch_add(1, std::memory_order_relaxed);
}

// Render path only: head position (6DOF) and the stereo eye offset, in world units. Records
// where the head (without the eye offset) ended up, for mapping controllers into the world.
void ApplyTranslation(FVector& location, int gameYaw, FRotator const& rotation) {
    engine::UnrealVector offset{};
    if (gPositional && gHeadPose) {
        offset = engine::ToUnrealOffset(gHeadPose->position, gameYaw);
    }
    if (gHeadPose) {
        // Without 6DOF the camera stands for the head wherever it is in LOCAL space.
        gAnchor = ViewAnchor{{location.X + offset.x, location.Y + offset.y, location.Z + offset.z}, gameYaw, *gHeadPose};
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

// The player's game mode (EGameModes), if the controller has a mode manager.
std::optional<unsigned char> ReadGameMode(APlayerController* controller) {
    if (controller == nullptr || !controller->IsA(ABioPlayerController::StaticClass())) {
        return std::nullopt;
    }
    USFXGameModeManager const* const modes = engine::adapter::GameModes(static_cast<ABioPlayerController*>(controller));
    if (modes == nullptr) {
        return std::nullopt;
    }
    return modes->CurrentMode;
}

// Once per frame, after the camera computed this frame's view (before the player view is built).
void OnUpdateCamera(UObject* context, UFunction* /*function*/, void* /*parms*/, void* /*result*/) {
    auto* const camera = static_cast<ACamera*>(context);
    if (camera == nullptr || camera->PCOwner == nullptr) {
        return;
    }
    gGameMode = ReadGameMode(camera->PCOwner);
    // On foot is the only mode where the move stick should follow the head (not the
    // conversation wheel, galaxy map, menus, power/weapon wheels or the Mako).
    gOnFoot = gGameMode == kGameModeDefault;
    if (!gOnFoot) {
        gRenderYawOffset = 0;  // menus, conversations, wheels: the stick isn't for walking
    }
    if (gPath == Path::Render) {
        // The head is sampled at the player view instead (as late as possible).
        auto const headsetFov = vr::view_sync::HeadsetFov();
        if (gFovViaCamera && vr::output::WantsHeadTracking() && headsetFov) {
            camera->CameraCache.POV.FOV = *headsetFov;
        }
        gCameraFov = camera->CameraCache.POV.FOV;
        return;
    }
    BeginView();
    gEye = -1;
    gGameYaw.reset();  // set again if this frame's views get rotated
    if (gPath == Path::Camera && gHeadsetFov) {
        // Set in the camera cache (read by script) and returned from GetFOVAngle.
        camera->CameraCache.POV.FOV = *gHeadsetFov;
    }
    gCameraFov = camera->CameraCache.POV.FOV;
    if (gHeadPose) {
        TagFrame(*gHeadPose, gCameraFov, -1);
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
    FRotator& rotation = engine::adapter::ViewPointRotation(*p);
    int const gameYaw = rotation.Yaw;
    gGameYaw = gameYaw;
    ApplyRotation(rotation);
    if (inView) {
        ApplyTranslation(p->out_Location, gameYaw, rotation);
    }
}

void OnGetFovAngle(UObject* context, UFunction* /*function*/, void* parms, void* /*result*/) {
    if (context == nullptr || !context->IsA(APlayerController::StaticClass())) {
        return;
    }
    auto* const p = static_cast<APlayerController_eventGetFOVAngle_Parms*>(parms);
    bool const inView = gPath == Path::Render && engine::scene_view::InPrimaryView();
    if (gHeadsetFov && (inView || gPath == Path::Camera)) {
        p->ReturnValue = *gHeadsetFov;
        gFovOverrides.fetch_add(1, std::memory_order_relaxed);
    }
    if (inView) {
        gFovCallsInPrimary.fetch_add(1, std::memory_order_relaxed);
        gRenderedFov = p->ReturnValue;
    }
}

// Render-path self-checks, once per heartbeat window. Each needs kWindowsToDecide windows in a
// row so a single odd window (startup, a level load) can't flip anything.
void CheckRenderPath(unsigned primary, unsigned viewPoints, unsigned fovCalls) {
    // Stock UE3's CalcSceneView asks script for the view point. If LE1's doesn't, nothing would
    // rotate: fall back to the camera path rather than do nothing.
    gNoViewPointWindows = primary > 0 && viewPoints == 0 ? gNoViewPointWindows + 1 : 0;
    if (gNoViewPointWindows >= kWindowsToDecide) {
        gPath = Path::Camera;
        gRenderPathAvailable = false;
        gRenderYawOffset = 0;
        log::Warn("render path: CalcSceneView never asked for the view point; falling back to the camera path");
        return;
    }
    gNoFovWindows = primary > 0 && fovCalls == 0 ? gNoFovWindows + 1 : 0;
    if (gNoFovWindows >= kWindowsToDecide && !gFovViaCamera) {
        gFovViaCamera = true;
        log::Warn("render path: CalcSceneView never asked for the FOV; overriding the camera FOV instead");
    }
    // The scene is drawn but never from the call site we know: wrong return address for this
    // build. Frames are still tagged once per tick (head-locked); \ switches to the camera path.
    gMissedViewWindows = primary == 0 && engine::scene_view::OtherCalls() > 0 ? gMissedViewWindows + 1 : 0;
    if (gMissedViewWindows == kWindowsToDecide) {
        log::Warn("render path: scene views are built, but not from the known player-view call site; "
                  "the view won't follow the head. Press \\ for the camera path");
    }
}

} // namespace

void OnBeforePrimaryView() {
    if (!gReady) {
        return;  // the ProcessEvent handlers aren't live yet
    }
    gPrimaryViews.fetch_add(1, std::memory_order_relaxed);
    gPrimaryThisTick = true;
    if (gPath != Path::Render) {
        return;
    }
    bool const forVr = BeginView();
    gRenderYawOffset = gActive && gOnFoot ? gHead.yaw : 0;
    gGameYaw.reset();  // set again below if this view gets rotated
    gAnchor.reset();
    gEye = -1;
    if (!gActive) {
        return;
    }
    XrPosef pose = *gHeadPose;
    if (forVr && gStereo == Stereo::AlternateEye && vr::output::StereoAvailable()) {
        // One eye per tick: a second player view in the same tick replaces the first in the
        // queue (TagFrame), so it must render the same eye or that eye would never alternate.
        if (!gTaggedThisTick) {
            gTickEye = static_cast<int>(gEyeParity++ & 1u);
            gEyeViews[gTickEye].fetch_add(1, std::memory_order_relaxed);
        }
        gEye = gTickEye;
        gHalfIpdMeters = vr::view_sync::HalfIpd().value_or(kDefaultHalfIpdMeters);
        pose = engine::OffsetRight(pose, gEye == 0 ? -gHalfIpdMeters : gHalfIpdMeters);
    }
    // The headset FOV snapshot is what GetFOVAngle will return inside this view; without an
    // override the camera's own FOV (this frame's) is what gets rendered.
    TagFrame(pose, gHeadsetFov && !gFovViaCamera ? *gHeadsetFov : gCameraFov, gEye);
}

void OnTickEnd() {
    // Render path: a tick that drew no player view (loading screen, movie, or an unknown call
    // site) still gives the output a pose, the current head pose, so the image is head-locked
    // instead of stuck at an old pose and VR never goes black. It's published as a fallback, not
    // queued: this late in the tick its Present may already have happened.
    if (gReady && gPath == Path::Render && !gTaggedThisTick && vr::output::WantsHeadTracking()) {
        BeginView();
        gActive = false;  // nothing to rotate: the tick's views (if any) are done
        if (gHeadPose) {
            vr::view_sync::PublishFallback({*gHeadPose, gCameraFov, -1});
            gTickTags.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (!gPrimaryThisTick) {
        gRenderYawOffset = 0;  // no player view (full-screen GUI, pause): the stick isn't for walking
        if (gPath == Path::Render) {
            gGameYaw.reset();
            gAnchor.reset();
        }
    }
    gTaggedThisTick = false;
    gPrimaryThisTick = false;
}

bool TryStartup(bool renderPathAvailable) {
    UFunction* const updateCamera = engine::FindFunction(L"UpdateCamera", kUpdateCamera);
    UFunction* const viewPoint = engine::FindFunction(L"GetPlayerViewPoint", kGetPlayerViewPoint);
    UFunction* const fovAngle = engine::FindFunction(L"GetFOVAngle", kGetFovAngle);
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

void SetReady() {
    gReady = true;
}

void LogStats() {
    unsigned const frames = gTaggedFrames.exchange(0);
    unsigned const tickTags = gTickTags.exchange(0);
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
    log::Info("    head tracking: path {}, source {}, {} frames tagged ({} without a player view), {} views adjusted, FOV {:.1f} ({} overrides), head yaw {} pitch {} roll {}, on foot {}",
        Name(gPath), source, frames, tickTags, views, gRenderedFov > 0.0f ? gRenderedFov : gCameraFov, fovs, gHead.yaw,
        gHead.pitch, gHead.roll, gOnFoot ? "yes" : "no");
    if (gPath == Path::Render) {
        log::Info("    render path: {} player views, {} GetPlayerViewPoint and {} GetFOVAngle calls inside them; 6DOF {}, stereo {} (L {} / R {}, half IPD {:.1f} mm)",
            primary, viewPoints, fovCalls, gPositional ? "on" : "off", Name(gStereo), leftViews, rightViews,
            gHalfIpdMeters * 1000.0f);
        CheckRenderPath(primary, viewPoints, fovCalls);
    }
}

void Shutdown() {
    gReady = false;
    gGameYaw.reset();
    gAnchor.reset();
    gOnFoot = false;
    gGameMode.reset();
    gActive = false;
    gHeadPose.reset();
    gHeadsetFov.reset();
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
    gGameYaw.reset();
    gAnchor.reset();
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

std::optional<int> GameYaw() {
    return gGameYaw;
}

std::optional<ViewAnchor> Anchor() {
    return gAnchor;
}

bool OnFoot() {
    return gOnFoot;
}

std::optional<unsigned char> GameMode() {
    return gGameMode;
}

void RefreshGameMode() {
    gGameMode = ReadGameMode(engine::LocalPlayerController());
    gOnFoot = gGameMode == kGameModeDefault;
}

} // namespace mevr::game::head_tracking
