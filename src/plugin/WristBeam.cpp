// SPDX-License-Identifier: MIT
// The fan of light from the wrist to a screen projected from it (plugin/WristBeam.hpp, research/53 section 6).

#include "plugin/WristBeam.hpp"

#include "core/Log.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/Swapchains.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdint>
#include <vector>

namespace mevr::host::wrist_beam {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::int64_t kSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // as our other quads
constexpr int kSize = 128;  // a soft gradient: bilinear filtering does the rest
// The light's colour: the omni-tool's orange (its hologram on the forearm, game/OmniTool, measured from the eye image
// 2026-11-04: mid tones (233, 147, 81), the bright core (255, 205, 103) in sRGB), so the fan reads as the omni-tool's projection.
constexpr float kRed = 1.0f;
constexpr float kGreen = 0.63f;
constexpr float kBlue = 0.35f;

// Present thread.
XrSession gSession = XR_NULL_HANDLE;
XrSwapchain gSwapchain = XR_NULL_HANDLE;
std::vector<ID3D11Texture2D*> gImages;  // the runtime's
bool gSwapchainFailed = false;  // refused at gSwapchainFailedMs
std::uint64_t gSwapchainFailedMs = 0;
bool gFilled = false;
XrCompositionLayerQuad gQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
bool gLoggedFirst = false;

void ForgetSession() {
    // The session's swapchains went with it.
    swapchains::Forget(gSwapchain);
    gSwapchain = XR_NULL_HANDLE;
    gImages.clear();
    gSwapchainFailed = false;
    gFilled = false;
}

// The swapchain, its picture copied into every image once (it never changes).
bool Ensure(Frame const& frame) {
    if (gSwapchain != XR_NULL_HANDLE && gFilled) {
        return true;
    }
    if ((gSwapchainFailed && !swapchains::RetryDue(gSwapchainFailedMs)) || frame.device == nullptr || frame.context == nullptr) {
        return false;  // refused just now (plugin/Swapchains: tried again in 2 s, or once room was freed)
    }
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = kSwapchainFormat;
    info.sampleCount = 1;
    info.width = kSize;
    info.height = kSize;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    // Freed for room (plugin/Swapchains) when no frame has shown it for a second; made and filled again when next wanted.
    auto evict = [] {
        swapchains::Destroy(gSwapchain);
        gSwapchain = XR_NULL_HANDLE;
        gImages.clear();
        gFilled = false;
    };
    XrResult r = gSwapchain == XR_NULL_HANDLE ? swapchains::Create(frame.session, info, "wrist fan", evict, gSwapchain, gImages) : XR_SUCCESS;
    std::uint32_t const count = static_cast<std::uint32_t>(gImages.size());
    std::vector<std::uint32_t> const pixels = vr::world_ui::BeamPixels(kSize, kRed, kGreen, kBlue);
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kSize;
    td.Height = kSize;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(), kSize * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    if (XR_FAILED(r) || count == 0 || FAILED(frame.device->CreateTexture2D(&td, &data, texture.GetAddressOf()))) {
        if (XR_SUCCEEDED(r)) {
            log::Error("world ui: the wrist's fan of light: its texture couldn't be made; no fan until it can");
        }  // a refused swapchain: plugin/Swapchains logged it
        swapchains::Destroy(gSwapchain);
        gSwapchain = XR_NULL_HANDLE;
        gImages.clear();
        gSwapchainFailed = true;
        gSwapchainFailedMs = GetTickCount64();
        return false;
    }
    gSwapchainFailed = false;
    // Every image gets the picture: the runtime may show any of them.
    for (std::uint32_t i = 0; i < count; ++i) {
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        std::uint32_t index = 0;
        if (XR_FAILED(xrAcquireSwapchainImage(gSwapchain, &acquire, &index))) {
            return false;
        }
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = XR_INFINITE_DURATION;
        if (XR_SUCCEEDED(xrWaitSwapchainImage(gSwapchain, &wait)) && index < gImages.size()) {
            frame.context->CopyResource(gImages[index], texture.Get());
        }
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(gSwapchain, &release);
    }
    testloop::NameSwapchain(gSwapchain, "beam");
    testloop::SetStaticImage(gSwapchain, kSize, kSize, pixels);  // snapshots draw it from these
    gFilled = true;
    log::Info("world ui: the wrist's fan of light: a {}x{} picture in {} swapchain images", kSize, kSize, count);
    return true;
}

} // namespace

XrCompositionLayerQuad const* Layer(Frame const& frame, vr::world_ui::Beam const& beam) {
    if (frame.session == XR_NULL_HANDLE || frame.appSpace == XR_NULL_HANDLE || !(beam.heightM > 0.0f) || !(beam.widthM > 0.0f)) {
        return nullptr;
    }
    if (frame.session != gSession) {
        ForgetSession();
        gSession = frame.session;
    }
    if (!Ensure(frame)) {
        return nullptr;
    }
    gQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    gQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
    gQuad.space = frame.appSpace;
    gQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    gQuad.subImage.swapchain = gSwapchain;
    gQuad.subImage.imageRect = {{0, 0}, {kSize, kSize}};
    gQuad.pose = beam.pose;
    gQuad.size = {beam.widthM, beam.heightM};
    if (!gLoggedFirst) {
        gLoggedFirst = true;
        log::Info("world ui: the wrist's fan of light first shown: {:.2f} x {:.2f} m from the wrist to the screen's bottom edge",
            beam.widthM, beam.heightM);
    }
    return &gQuad;
}

} // namespace mevr::host::wrist_beam
