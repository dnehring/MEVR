// SPDX-License-Identifier: MIT
// Extracted from the LE2 bridge; draw modules remain the same ones used by LE1.
#include "plugin/SequelWeaponLayers.hpp"
#include "plugin/SightLayers.hpp"
#include "core/vr/AimRay.hpp"
#include "plugin/HandReticle.hpp"
#include "plugin/Renderer.hpp"
#include <Windows.h>
namespace mevr::host::sequel_weapon_layers {
unsigned Layers(ME2VR::MevrBridge::OverlayFrame const& frame, ID3D11Device* device, int reticleMode,
    void const** layersOut, unsigned count, unsigned capacity, bool requirePublishedAim) noexcept {
    if (!layersOut || count >= capacity) return count;
    // The gun's sights (plugin/SightLayers, LE1's): worked out before the ring, which hides while an eye looks through the
    // sight (held 0.3 s, as LE1's HalcyonOverlays); their quads go on top of the ring's.
    mevr::host::sight_layers::Frame sights;
    sights.session = static_cast<XrSession>(frame.session);
    sights.appSpace = static_cast<XrSpace>(frame.appSpace);
    sights.context = frame.context;
    sights.device = device;
    sights.worldFrame = frame.worldFrame;
    sights.menuOpen = frame.menuOpen;
    sights.onFoot = frame.gameMode == 0;
    sights.eyesValid = frame.eyesValid;
    sights.projectionValid = frame.projectionValid;
    for (int e = 0; e < 2; ++e) {
        auto const& eye = frame.eyePosition[e];
        sights.eyes[e].position = {eye[0], eye[1], eye[2]};
        if (frame.projectionValid) {
            auto const& rect = frame.projectionRect[e];
            auto const& orientation = frame.projectionOrientation[e];
            auto const& position = frame.projectionPosition[e];
            auto const& fov = frame.projectionFov[e];
            sights.projectionSwapchain[e] = static_cast<XrSwapchain>(frame.projectionSwapchain[e]);
            sights.projectionRect[e] = {{rect[0], rect[1]}, {rect[2], rect[3]}};
            sights.projectionPose[e] = {{orientation[0], orientation[1], orientation[2], orientation[3]},
                                       {position[0], position[1], position[2]}};
            sights.projectionFov[e] = {fov[0], fov[1], fov[2], fov[3]};
        }
    }
    auto const sightLayers = mevr::host::sight_layers::Layers(sights);
    static std::uint64_t sightSeenMs = 0;  // Present thread
    std::uint64_t const nowMs = GetTickCount64();
    if (sightLayers.looking && sightLayers.replaceRing) {
        sightSeenMs = nowMs;
    }
    // The hand reticle (plugin/HandReticle, LE1's): a ring on the mod's aim ray (game/HandAim's setAimRay) while the mod
    // aims with the right controller, on foot (game mode 0; the last readable one).
    {
        mevr::host::renderer::HudSettings const cfg = mevr::host::renderer::Hud();
        mevr::host::hand_reticle::Frame reticle;
        reticle.session = static_cast<XrSession>(frame.session);
        reticle.appSpace = static_cast<XrSpace>(frame.appSpace);
        reticle.context = frame.context;
        reticle.device = device;
        reticle.worldFrame = frame.worldFrame;
        reticle.menuOpen = frame.menuOpen;
        reticle.headValid = frame.headValid;
        reticle.headPosition = {frame.headPosition[0], frame.headPosition[1], frame.headPosition[2]};
        reticle.gameMode = reticleMode;
        auto const ray = mevr::vr::aim_ray::Latest();
        bool const liveAim = ray.sequence != 0 && ray.valid && nowMs >= ray.timeMs && nowMs - ray.timeMs < 1000;
        reticle.handAim = mevr::host::renderer::ModHandAim() && (!requirePublishedAim || liveAim);
        reticle.sightHidesRing = sightSeenMs != 0 && nowMs - sightSeenMs < 300;
        reticle.enabled = cfg.handReticle;
        reticle.distanceM = cfg.handReticleDistanceM;
        reticle.sizeDeg = cfg.handReticleSizeDeg;
        XrCompositionLayerQuad const* const ring = mevr::host::hand_reticle::Layer(reticle);
        if (ring != nullptr && count < capacity) {
            layersOut[count++] = ring;
        }
    }
    for (unsigned i = 0; i < sightLayers.count && count < capacity; ++i) {
        layersOut[count++] = sightLayers.layers[i];
    }
    return count;
}
}
