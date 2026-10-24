// SPDX-License-Identifier: MIT
#include "game/ViewAnchor.hpp"

#include "core/Log.hpp"
#include "core/vr/Comfort.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/Output.hpp"
#include "game/HeadTracking.hpp"
#include "game/Player.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mevr::game::view_anchor {
namespace {

bool gUseHostView = false;
bool gLogged = false;

// The mod's own turn since the game camera last updated (NoteTurn): the camera as it was then, and the turn.
struct PendingTurn {
    bool active = false;
    int cameraYaw = 0;
    double cameraX = 0.0;
    double cameraY = 0.0;
    double cameraZ = 0.0;
    int units = 0;
    bool pawnTurned = false;
    double pivotX = 0.0;
    double pivotY = 0.0;
};
PendingTurn gTurn;

// The local player's game camera (PlayerController.PlayerCamera's CameraCache.POV), through our
// reflection layer (game/player). Copied: the camera moves on its own update.
struct CameraView {
    ue3::Vector location;
    ue3::Rotator rotation;
};

std::optional<CameraView> GameCamera() {
    auto const pov = player::CameraPov(player::Camera(player::Controller()));
    if (!pov) {
        return std::nullopt;
    }
    return CameraView{*pov->location, *pov->rotation};
}

// Whether the camera is still the one the pending turn was noted against (it hasn't updated since).
bool SameCamera(CameraView const& pov) {
    ue3::Vector const& at = pov.location;
    return gTurn.active && pov.rotation.yaw == gTurn.cameraYaw && at.x == gTurn.cameraX && at.y == gTurn.cameraY &&
        at.z == gTurn.cameraZ;
}

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
    auto const game = GameCamera();
    if (!game) {
        return std::nullopt;
    }
    CameraView const& pov = *game;
    ue3::Vector const& at = pov.location;
    engine::UnrealRotation camera{pov.rotation.pitch, pov.rotation.yaw, pov.rotation.roll};
    engine::UnrealVector location{at.x, at.y, at.z};
    if (gTurn.active) {
        if (SameCamera(pov)) {
            // Our turn isn't in the camera yet: map the view the next frame will show.
            vr::comfort::CameraGuess const guess =
                vr::comfort::TurnCamera(at.x, at.y, pov.rotation.yaw, gTurn.units, gTurn.pawnTurned, gTurn.pivotX, gTurn.pivotY);
            camera.yaw = guess.yaw;
            location.x = guess.x;
            location.y = guess.y;
        } else {
            gTurn = {};  // the camera has updated: the turn is in it
        }
    }
    engine::TrackingFrame const frame = engine::FromCameraView(location, camera,
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

void NoteTurn(int units, bool pawnTurned, double pivotX, double pivotY) {
    if (units == 0) {
        return;
    }
    auto const game = GameCamera();
    if (!game) {
        return;
    }
    CameraView const& pov = *game;
    if (!SameCamera(pov)) {
        ue3::Vector const& at = pov.location;
        gTurn = {};
        gTurn.active = true;
        gTurn.cameraYaw = pov.rotation.yaw;
        gTurn.cameraX = at.x;
        gTurn.cameraY = at.y;
        gTurn.cameraZ = at.z;
    }
    gTurn.units += units;
    gTurn.pawnTurned = gTurn.pawnTurned || pawnTurned;
    if (pawnTurned) {
        gTurn.pivotX = pivotX;
        gTurn.pivotY = pivotY;
    }
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
    gTurn = {};
    vr::host_view::Reset();
}

} // namespace mevr::game::view_anchor
