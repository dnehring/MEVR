// SPDX-License-Identifier: MIT
// The sequels' side of the frame-breakdown probe (plugin/PerfProbe.cpp is the shared module; plugin/PerfProbeHost.hpp
// the adapter it asks; research/sequel-render-levers.md "Shared module"). Halcyon's ME2 and ME3 renderers have no seam
// for the probe in their Present hooks yet, so a frame is bounded by xrEndFrame (PerfProbe.cpp's
// EndFrameAsFrameBoundary), the game's immediate context is found from the frame's own projection layer, and the
// draw counter is ours (perf::NoteDraws: 0 until the renderers' D3D draw hooks call it).

#include "plugin/PerfProbe.hpp"
#include "plugin/PerfProbeHost.hpp"

#include "plugin/RenderLevers.hpp"
#include "plugin/Upscale.hpp"

#include "core/Log.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <atomic>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace mevr::host::perf {

namespace {

using Microsoft::WRL::ComPtr;

std::atomic<std::uint32_t> gDraws{0};

// Render thread: the context found, the swapchain it was found from, and the eye image's size as that frame submitted it.
ComPtr<ID3D11DeviceContext> gFrameContext;
XrSwapchain gContextFrom = XR_NULL_HANDLE;
std::atomic<std::int32_t> gEyeWidth{0}, gEyeHeight{0};
std::atomic<std::uint32_t> gEyeViews{0};

} // namespace

void NoteDraws(std::uint32_t count) noexcept {
    gDraws.fetch_add(count, std::memory_order_relaxed);
}

namespace adapter {

std::uint32_t Draws() noexcept {
    return gDraws.load(std::memory_order_relaxed);
}

ID3D11DeviceContext* FrameContext(XrFrameEndInfo const* info) noexcept {
    if (info == nullptr || info->layers == nullptr) {
        return gFrameContext.Get();
    }
    XrSwapchain swapchain = XR_NULL_HANDLE;
    for (std::uint32_t i = 0; i < info->layerCount && swapchain == XR_NULL_HANDLE; ++i) {
        XrCompositionLayerBaseHeader const* const layer = info->layers[i];
        if (layer == nullptr || layer->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            continue;
        }
        auto const* const projection = reinterpret_cast<XrCompositionLayerProjection const*>(layer);
        if (projection->viewCount == 0 || projection->views == nullptr) {
            continue;
        }
        swapchain = projection->views[0].subImage.swapchain;
        gEyeWidth.store(projection->views[0].subImage.imageRect.extent.width, std::memory_order_relaxed);
        gEyeHeight.store(projection->views[0].subImage.imageRect.extent.height, std::memory_order_relaxed);
        gEyeViews.store(projection->viewCount, std::memory_order_relaxed);
    }
    if (swapchain == XR_NULL_HANDLE || swapchain == gContextFrom) {
        return gFrameContext.Get();  // no eye layer this frame (a flat panel alone), or the swapchain already read
    }
    std::uint32_t count = 0;
    if (XR_FAILED(xrEnumerateSwapchainImages(swapchain, 0, &count, nullptr)) || count == 0) {
        return gFrameContext.Get();
    }
    try {
        std::vector<XrSwapchainImageD3D11KHR> images(count, XrSwapchainImageD3D11KHR{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        if (XR_FAILED(xrEnumerateSwapchainImages(swapchain, count, &count,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()))) ||
            count == 0 || images[0].texture == nullptr) {
            return gFrameContext.Get();
        }
        ComPtr<ID3D11Device> device;
        images[0].texture->GetDevice(device.GetAddressOf());
        if (device == nullptr) {
            return gFrameContext.Get();
        }
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(context.GetAddressOf());
        if (context != gFrameContext) {
            log::Info("perf probe: the game's device context found from the frame's eye swapchain ({} images)", count);
        }
        gFrameContext = context;
        gContextFrom = swapchain;
    } catch (...) {
        // an allocation failure: keep what we had
    }
    return gFrameContext.Get();
}

void FrameClosed(ID3D11DeviceContext* context) noexcept {
    render_levers::OnPresentEnd(context != nullptr ? context : gFrameContext.Get());
}

std::string RendererJson() {
#if defined(MEVR_GAME_LE2)
    char const* const renderer = "halcyon-le2";
#else
    char const* const renderer = "halcyon-le3";
#endif
    // The eye copy's upscale (plugin/Upscale: the method, its source and target sizes) goes with every capture, so captures
    // taken before and after a render size or FSR change stay comparable (research/sequel-render-levers.md "Performance
    // research").
    return std::format(
        "  \"renderer\": \"{}\",\n  \"frameBounds\": \"{}\",\n  \"renderLevers\": {},\n  \"upscale\": {},\n"
        "  \"resolution\": {{\"eye\": [{}, {}], \"eyeViews\": {}}}",
        renderer, PresentSeamSeen() ? "Present" : "xrEndFrame", render_levers::SettingsJson(), upscale::StatusJson(),
        gEyeWidth.load(std::memory_order_relaxed), gEyeHeight.load(std::memory_order_relaxed), gEyeViews.load(std::memory_order_relaxed));
}

} // namespace adapter

} // namespace mevr::host::perf
