// SPDX-License-Identifier: MIT
// The game's own galaxy map over the CIC's projector, every game's part (game/GalaxyProjector.hpp).
#include "game/GalaxyProjector.hpp"

#include "core/Log.hpp"
#include "core/vr/CineScreen.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mevr::game::galaxy_projector {

HWND GameWindow() {
    struct Search {
        DWORD process = 0;
        HWND best = nullptr;
        long area = 0;
    } search{GetCurrentProcessId(), nullptr, 0};
    EnumWindows(
        [](HWND window, LPARAM context) -> BOOL {
            auto* const s = reinterpret_cast<Search*>(context);
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            RECT rc{};
            if (process == s->process && IsWindowVisible(window) && GetClientRect(window, &rc)) {
                long const area = (rc.right - rc.left) * (rc.bottom - rc.top);
                if (area > s->area) {
                    s->area = area;
                    s->best = window;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.best;
}

void Remember(HeldView& held, ue3::Vector const& location, ue3::Rotator const& rotation, float fov) {
    held.location = location;
    held.rotation = rotation;
    // Carry the corrected on-foot base into the hold; the renderer still adds its common offsets once.
    auto const view = vr::host_view::Latest();
    held.rawZ = location.z;
    held.floorView = view;
    auto const basis = engine::ToBasis(engine::UnrealRotation{held.rotation.pitch, held.rotation.yaw, held.rotation.roll});
    held.location.z = static_cast<float>(vr::host_view::HoldBaseZ(view, location.z, basis.right.z, basis.up.z, basis.forward.z));
    held.fov = fov;
    held.haveView = true;
}

void PutRenderHold(HeldView const& held, RenderHold const& render, bool writes, vr::galaxy_holo::Scene& scene) {
    if (!held.haveView || !render.on || !writes || GetTickCount64() - render.ms >= 500) return;
    scene.holdRender = 1;
    scene.holdLocation[0] = held.location.x;
    scene.holdLocation[1] = held.location.y;
    scene.holdLocation[2] = held.location.z;
    scene.holdRotation[0] = held.rotation.pitch;
    scene.holdRotation[1] = held.rotation.yaw;
    scene.holdRotation[2] = held.rotation.roll;
    scene.holdFov = held.fov;
    scene.povLocationAddress = reinterpret_cast<std::uint64_t>(render.location);
    scene.povRotationAddress = reinterpret_cast<std::uint64_t>(render.rotation);
    scene.povFovAddress = reinterpret_cast<std::uint64_t>(render.fov);
}

void StereoWindow::Pause() {
    for (capture_rig::Rig& eye : eyes_) {
        if (eye.Alive() && eye.Enabled()) {
            eye.SetEnabled(false);
        }
    }
    if (on_) {
        log::Info("galaxy window: paused ({} ticks pointed)", pointed_);
    }
    on_ = false;
    pointed_ = 0;
}

void StereoWindow::Reset(char const* why) {
    for (auto& eye : eyes_) {
        if (eye.Alive()) eye.Destroy(why);  // same-world controller replacement
        eye.Forget();                       // otherwise the actors went with the old world
    }
    on_ = false;
    failedMs_ = 0;
}

void StereoWindow::Shutdown(bool exiting) {
    for (capture_rig::Rig& eye : eyes_) {
        if (!exiting) {
            eye.Destroy("the mod is unloading");
        }
        eye.Forget();
    }
    on_ = false;
}

bool StereoWindow::Step(MapPov const& pov, std::optional<ue3::Vector> const& centre, int clientWidth, int clientHeight, WindowLook const& look,
    vr::galaxy_holo::Scene& scene) {
    std::uint64_t const now = GetTickCount64();
    for (int e = 0; e < 2; ++e) {
        capture_rig::Rig& eye = eyes_[e];
        if (eye.Alive()) {
            continue;
        }
        eye.Forget();
        if (failedMs_ != 0 && now - failedMs_ < 5000) {
            Pause();
            return false;
        }
        // About the UI panel's resolution (half the window) at the window's aspect, both multiples of 8 (a 1172x1196 target's
        // texture wasn't found behind its resource; the spectator's 1920x1080 is).
        capture_rig::Options options;
        double const aspect = static_cast<double>(clientWidth) / static_cast<double>(clientHeight);
        int bestW = 1024, bestH = 1024;
        double bestError = 1e9;
        for (int w = 512; w <= 2048; w += 8) {
            int const h = static_cast<int>(std::lround(w / aspect / 8.0)) * 8;
            double const error = std::fabs(static_cast<double>(w) / h - aspect) + 1e-5 * std::abs(w - clientWidth / 2);
            if (h >= 256 && h <= 2048 && error < bestError) {
                bestError = error;
                bestW = w;
                bestH = h;
            }
        }
        options.width = bestW;
        options.height = bestH;
        options.fovDeg = pov.fovDeg;
        options.nearUnits = 10.0f;
        options.postProcess = look.postProcess;
        options.postProcessChain = look.postProcessChain;
        if (!eye.Create(e == 0 ? "galaxy left" : "galaxy right", options, pov.location)) {
            log::Warn("galaxy window: the {} capture failed ({}); the table UI stays, retry in 5 s", e == 0 ? "left" : "right", eye.Failure());
            failedMs_ = now;
            Pause();
            return false;
        }
    }
    // The stereo: parallel cameras on the game's, the map's centre on the window's plane, infinity `depth` of the width behind.
    float const yaw = static_cast<float>(pov.rotation.yaw) * 6.2831853f / 65536.0f;
    float const pitch = static_cast<float>(pov.rotation.pitch) * 6.2831853f / 65536.0f;
    ue3::Vector const forward{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    ue3::Vector const right{-std::sin(yaw), std::cos(yaw), 0.0f};
    float converge = 5000.0f;
    if (centre) {
        float const along = (centre->x - pov.location.x) * forward.x + (centre->y - pov.location.y) * forward.y +
            (centre->z - pov.location.z) * forward.z;
        if (along > 100.0f) {
            converge = along;
        }
    }
    float const widthM = look.widthM;
    float const depth = std::min(look.depth, vr::cine_screen::MaxDepth(widthM, vr::cine_screen::kDefaultIpdM));
    float const crop = vr::cine_screen::CropFraction(depth);
    float const halfH = pov.fovDeg * 0.5f * 0.01745329f;
    float const baseline = vr::cine_screen::BaselineUU(crop, converge, halfH);
    for (int e = 0; e < 2; ++e) {
        capture_rig::Rig& eye = eyes_[e];
        float const side = (e == 0 ? -0.5f : 0.5f) * baseline;
        ue3::Vector const at{pov.location.x + right.x * side, pov.location.y + right.y * side, pov.location.z + right.z * side};
        if (!eye.SetFov(pov.fovDeg) || (!eye.Enabled() && !eye.SetEnabled(true)) || !eye.Point(at, pov.rotation)) {
            log::Warn("galaxy window: pointing the {} capture failed; the table UI stays while it retries", e == 0 ? "left" : "right");
            failedMs_ = now;
            Pause();
            return false;
        }
        eye.Tick();
    }
    if (!on_) {
        log::Info("galaxy window: the game's own map over the table: two {}x{} captures from the map camera ({:.1f} degrees), {:.0f} units "
                  "apart, the map's centre {:.0f} units away on the window, infinity {:.1f}% of its width behind",
            eyes_[0].Width(), eyes_[0].Height(), pov.fovDeg, baseline, converge, depth * 100.0f);
    }
    on_ = true;
    baselineUU_ = baseline;
    crop_ = crop;
    convergeUU_ = converge;
    ++pointed_;
    // The host's label-projecting CalcSceneView uses exactly the POV just sent to these captures. The ordinary
    // player POV can advance before the next end-of-tick Point (especially while easing/zooming).
    constexpr float kUnitToRad = 6.2831853f / 65536.0f;
    scene.camera.location[0] = pov.location.x;
    scene.camera.location[1] = pov.location.y;
    scene.camera.location[2] = pov.location.z;
    scene.camera.pitch = static_cast<float>(pov.rotation.pitch) * kUnitToRad;
    scene.camera.yaw = static_cast<float>(pov.rotation.yaw) * kUnitToRad;
    scene.camera.roll = static_cast<float>(pov.rotation.roll) * kUnitToRad;
    scene.camera.fovXDeg = pov.fovDeg;
    scene.camera.aspect = static_cast<float>(eyes_[0].Width()) / static_cast<float>(eyes_[0].Height());
    scene.camera.valid = 1;
    scene.windowOn = 1;
    scene.windowFloat = 0;
    scene.windowRound = look.round ? 1 : 0;
    scene.windowBackdrop = static_cast<std::uint8_t>(std::lround(std::clamp(look.backdrop, 0.0f, 1.0f) * 100.0f));
    scene.windowWidth = static_cast<std::uint32_t>(eyes_[0].Width());
    scene.windowHeight = static_cast<std::uint32_t>(eyes_[0].Height());
    scene.windowTicks = std::min(eyes_[0].EnabledTicks(), eyes_[1].EnabledTicks());
    scene.windowResource[0] = eyes_[0].Resource();
    scene.windowResource[1] = eyes_[1].Resource();
    scene.windowGeneration = eyes_[0].Generation() * 1000003ull + eyes_[1].Generation();
    scene.windowCrop = crop;
    scene.windowWidthM = widthM;
    return true;
}

} // namespace mevr::game::galaxy_projector
