// SPDX-License-Identifier: MIT
// The hand reticle (plugin/HandReticle.hpp), moved out of plugin/HalcyonOverlays.cpp.

#include "plugin/HandReticle.hpp"

#include "core/Log.hpp"
#include "core/vr/AimRay.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/TestLoop.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace mevr::host::hand_reticle {
namespace {

using Microsoft::WRL::ComPtr;
namespace overlay = mevr::vr::overlay;

constexpr float kDegToRad = 3.14159265358979f / 180.0f;
constexpr UINT kReticleSize = 128;

XrSession gSession = XR_NULL_HANDLE;
QuadSwapchain gReticleSwapchain;
XrCompositionLayerQuad gReticleQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
bool gLoggedReticle = false;
bool gLoggedFallback = false;
int gReticleSource = -1;  // last logged: 0 none, 1 aim ray, 2 right controller

// A ring with a centre dot, light cyan with a dark rim so it reads on bright and dark backgrounds.
// Premultiplied, sRGB-encoded (the swapchain's format).
std::vector<std::uint32_t> ReticlePixels() {
    std::vector<std::uint32_t> pixels(static_cast<size_t>(kReticleSize) * kReticleSize);
    auto encode = [](float linear) {
        float const c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint32_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    float const px = 2.0f / static_cast<float>(kReticleSize);  // one pixel in [-1, 1] units
    auto band = [px](float d, float inner, float outer) {     // anti-aliased coverage of inner <= d <= outer
        return std::clamp((d - inner) / px + 0.5f, 0.0f, 1.0f) * std::clamp((outer - d) / px + 0.5f, 0.0f, 1.0f);
    };
    for (UINT y = 0; y < kReticleSize; ++y) {
        for (UINT x = 0; x < kReticleSize; ++x) {
            float const u = (static_cast<float>(x) + 0.5f) * px - 1.0f;
            float const v = (static_cast<float>(y) + 0.5f) * px - 1.0f;
            float const d = std::sqrt(u * u + v * v);
            float const light = std::max(band(d, 0.64f, 0.78f), band(d, -1.0f, 0.11f));
            float const rim = std::max(band(d, 0.58f, 0.84f), band(d, -1.0f, 0.17f));
            // Light over a half-transparent dark rim, premultiplied.
            float const rimAlpha = 0.55f * rim;
            float const a = light + rimAlpha * (1.0f - light);
            float const r = light * 0.55f, g = light * 0.95f, b = light * 1.0f;  // over black rim
            pixels[static_cast<size_t>(y) * kReticleSize + x] =
                encode(r) | (encode(g) << 8) | (encode(b) << 16) | (static_cast<std::uint32_t>(a * 255.0f + 0.5f) << 24);
        }
    }
    return pixels;
}

bool EnsureReticle(ID3D11DeviceContext* context, ID3D11Device* device) {
    if (gReticleSwapchain.handle != XR_NULL_HANDLE) {
        return true;
    }
    if (gReticleSwapchain.Refused() || !quad_swapchain::Create(gSession, kReticleSize, kReticleSize, gReticleSwapchain, "reticle")) {
        return false;
    }
    std::vector<std::uint32_t> const pixels = ReticlePixels();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kReticleSize;
    td.Height = kReticleSize;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(), kReticleSize * 4, 0};
    ComPtr<ID3D11Texture2D> source;
    if (FAILED(device->CreateTexture2D(&td, &data, source.GetAddressOf()))) {
        mevr::log::Error("overlays: reticle texture creation failed");
        gReticleSwapchain.Destroy();
        gReticleSwapchain.Refuse();
        return false;
    }
    // The content never changes: fill every image once; the runtime keeps showing the last released.
    for (size_t i = 0; i < gReticleSwapchain.images.size(); ++i) {
        std::uint32_t index = 0;
        if (!quad_swapchain::Acquire(gReticleSwapchain, index)) {
            mevr::log::Error("overlays: reticle swapchain image acquire failed");
            gReticleSwapchain.Destroy();
            gReticleSwapchain.Refuse();
            return false;
        }
        context->CopyResource(gReticleSwapchain.images[index], source.Get());
        quad_swapchain::Release(gReticleSwapchain);
    }
    // Snapshots draw it from these pixels: its images are never released again to be copied.
    mevr::host::testloop::NameSwapchain(gReticleSwapchain.handle, "reticle");
    mevr::host::testloop::SetStaticImage(gReticleSwapchain.handle, kReticleSize, kReticleSize, pixels);
    return true;
}

// Logs a line to our log and to the renderer's.
void LogBoth(std::string const& line) {
    mevr::log::Info("{}", line);
    mevr::host::renderer::LogLine(line);
}

} // namespace

XrCompositionLayerQuad const* Layer(Frame const& frame) noexcept {
    if (frame.session != gSession) {
        gReticleSwapchain.Forget();  // the session that owned it is gone
        gSession = frame.session;
    }
    // Where the weapon points: the mod's aim ray (muzzle and barrel) once it publishes one, else the
    // right controller's aim pose. Game-reticle suppression lasts throughout VR gameplay.
    // On foot; in the Mako (game mode 1, research/44) only on a ray the mod publishes (the controller's, while it aims the
    // turret), never the controller fallback.
    std::uint64_t const now = GetTickCount64();
    mevr::vr::aim_ray::Ray const ray = mevr::vr::aim_ray::Latest();
    bool const producer = ray.sequence != 0 && now - ray.timeMs < 1000;
    bool const makoRay = frame.gameMode == 1 && producer && ray.valid;
    bool const reticleMode = frame.enabled && frame.handAim && (frame.gameMode == 0 || makoRay);
    XrVector3f origin{};
    XrVector3f direction{};
    int source = 0;
    if (reticleMode) {
        if (producer) {
            if (ray.valid) {
                origin = ray.origin;
                direction = ray.direction;
                source = 1;
            }
        } else {
            mevr::vr::controllers::State const controllers = mevr::vr::controllers::Latest();
            if (controllers.active && controllers.right.aimValid) {
                origin = controllers.right.aim.position;
                direction = overlay::Rotate(controllers.right.aim.orientation, {0.0f, 0.0f, -1.0f});
                source = 2;
                if (!gLoggedFallback) {
                    gLoggedFallback = true;
                    mevr::log::Info("overlays: no aim ray from the mod yet: the reticle follows the right controller's aim pose");
                }
            }
        }
        if (source != gReticleSource) {
            gReticleSource = source;
            mevr::log::Info("overlays: reticle {}", source == 1 ? "on the mod's aim ray"
                                                    : source == 2 ? "on the right controller (fallback)"
                                                                  : "hidden (the mod reports no aim ray)");
        }
    }
    if (source == 0 || frame.sightHidesRing || !frame.worldFrame || frame.menuOpen || !frame.headValid || frame.device == nullptr ||
        frame.context == nullptr) {
        return nullptr;
    }
    overlay::Billboard reticle;
    float const distance = std::clamp(frame.distanceM, 1.0f, 100.0f);
    float const size = std::clamp(frame.sizeDeg, 0.2f, 10.0f) * kDegToRad;
    if (!overlay::PlaceOnRay(origin, direction, distance, frame.headPosition, size, reticle) ||
        !EnsureReticle(frame.context, frame.device)) {
        return nullptr;
    }
    gReticleQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    gReticleQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
    gReticleQuad.space = frame.appSpace;
    gReticleQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    gReticleQuad.subImage.swapchain = gReticleSwapchain.handle;
    gReticleQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(kReticleSize), static_cast<int32_t>(kReticleSize)}};
    gReticleQuad.pose = reticle.pose;
    gReticleQuad.size = {reticle.size, reticle.size};
    if (!gLoggedReticle) {
        gLoggedReticle = true;
        char line[256];
        std::snprintf(line, sizeof line,
            "[MEVR-HUD] first hand reticle: %s, %.1f m along the ray at (%.2f, %.2f, %.2f), %.3f m across (%.1f deg)",
            source == 1 ? "the mod's aim ray" : "the right controller's aim pose", distance,
            reticle.pose.position.x, reticle.pose.position.y, reticle.pose.position.z, reticle.size,
            size / kDegToRad);
        LogBoth(line);
    }
    return &gReticleQuad;
}

} // namespace mevr::host::hand_reticle
