#include "game/ViewAnchor.hpp"

#include "core/Log.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/Output.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/Objects.hpp"
#include "game/HeadTracking.hpp"

#include <LESDK/Headers.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mevr::game::view_anchor {
namespace {

bool gUseHostView = false;
bool gLogged = false;

// Degrees between two directions (any length).
double AngleDegrees(engine::UnrealVector const& a, engine::UnrealVector const& b) {
    double const dot = a.x * b.x + a.y * b.y + a.z * b.z;
    double const lengths = std::sqrt((a.x * a.x + a.y * a.y + a.z * a.z) * (b.x * b.x + b.y * b.y + b.z * b.z));
    if (lengths <= 0.0) {
        return -1.0;
    }
    return std::acos(std::clamp(dot / lengths, -1.0, 1.0)) * 180.0 / std::numbers::pi;
}

void LogHostFrame(engine::TrackingFrame const& frame, engine::UnrealRotation const& camera,
    vr::host_view::State const& view) {
    gLogged = true;
    engine::UnrealRotation const forward = engine::ToRotation(frame.rotation.forward);
    log::Info("view anchor: host renderer's view on the game camera (rotation {} {} {}): head look yaw {} pitch {}, "
              "head at ({:.0f}, {:.0f}, {:.0f}), tracking forward faces pitch {} yaw {}, {:.1f} units per meter",
        camera.pitch, camera.yaw, camera.roll, view.lookYaw, view.lookPitch, frame.headWorld.x, frame.headWorld.y,
        frame.headWorld.z, forward.pitch, forward.yaw, frame.unitsPerMeter);
    if (view.baseRightValid) {
        // The renderer's last view before head look should be the game camera: if this is more
        // than a degree or two, CameraCache.POV isn't what the view is built from.
        engine::UnrealVector const rendered{view.baseRight[0], view.baseRight[1], view.baseRight[2]};
        log::Info("view anchor: game camera's right axis vs the renderer's view: {:.1f} degrees apart",
            AngleDegrees(engine::ToBasis(camera).right, rendered));
    }
}

std::optional<engine::TrackingFrame> FromHost() {
    vr::host_view::State const view = vr::host_view::Latest();
    if (!view.valid) {
        return std::nullopt;
    }
    APlayerController* const controller = engine::LocalPlayerController();
    if (controller == nullptr || controller->PlayerCamera == nullptr) {
        return std::nullopt;
    }
    FTPOV const& pov = controller->PlayerCamera->CameraCache.POV;
    FVector const& at = engine::adapter::Location(pov);
    engine::UnrealRotation const camera{pov.Rotation.Pitch, pov.Rotation.Yaw, pov.Rotation.Roll};
    engine::TrackingFrame const frame = engine::FromCameraView({at.X, at.Y, at.Z}, camera,
        {view.lookPitch, view.lookYaw, 0}, {view.offsetForward, view.offsetRight, view.offsetUp}, view.head,
        view.unitsPerMeter);
    if (!gLogged) {
        LogHostFrame(frame, camera, view);
    }
    return frame;
}

std::optional<engine::TrackingFrame> FromOurRenderer() {
    if (!vr::output::WantsHeadTracking()) {
        return std::nullopt;
    }
    auto const anchor = head_tracking::Anchor();
    if (!anchor) {
        return std::nullopt;
    }
    return engine::FromBodyYaw(anchor->headWorld, anchor->gameYaw, anchor->head);
}

} // namespace

void UseHostView(bool on) {
    gUseHostView = on;
}

std::optional<engine::TrackingFrame> Current() {
    return gUseHostView ? FromHost() : FromOurRenderer();
}

void LogStats() {
    if (!gUseHostView) {
        return;
    }
    vr::host_view::State const view = vr::host_view::Latest();
    log::Info("    host view: {}, head look {} (yaw {} pitch {}), head aim {}, view offset fwd {:.1f} right {:.1f} up {:.1f}",
        view.valid ? "valid" : (view.sequence == 0 ? "none yet" : "not a head-tracked world view"),
        view.headLook ? "on" : "off", view.lookYaw, view.lookPitch, view.headAim ? "on" : "off", view.offsetForward,
        view.offsetRight, view.offsetUp);
}

void Shutdown() {
    gUseHostView = false;
    gLogged = false;
    vr::host_view::Reset();
}

} // namespace mevr::game::view_anchor
