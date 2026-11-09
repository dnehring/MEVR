// SPDX-License-Identifier: MIT
// The dip from black between the world and a cinematic (core/vr/TransitionFade; the seam: MevrBridge::TransitionFadeLayer in
// src/halcyon/le1/xr_session.cpp, the frame's last layer): a 16 x 16 black swapchain image cleared to the fade's opacity each
// frame it runs, on a quad 8 m across 0.5 m in front of the eyes in VIEW space (it covers the whole view), alpha-blended over
// everything. Nothing at all, not even a swapchain, until the first fade; freed for room like our other quads (plugin/Swapchains).
// [Cinematics] fadeS (0.35; 0 off). Log `transition fade:`. Present thread.

#include "core/Log.hpp"
#include "core/vr/TransitionFade.hpp"
#include "halcyon/le1/mevr_bridge.h"
#include "plugin/CineScreen.hpp"
#include "plugin/ConversationWorld.hpp"
#include "plugin/Swapchains.hpp"
#include "plugin/TestLoop.hpp"

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

namespace {

using Microsoft::WRL::ComPtr;
namespace tf = mevr::vr::transition_fade;

constexpr int kSize = 16;
constexpr unsigned kMaxLogs = 60;

tf::Fade gFade;
XrSession gSession = XR_NULL_HANDLE;
XrSwapchain gHandle = XR_NULL_HANDLE;
std::vector<ID3D11Texture2D*> gImages;
std::vector<ComPtr<ID3D11RenderTargetView>> gTargets;
bool gFailed = false;
std::uint64_t gFailedMs = 0;
unsigned gLogs = 0;
XrCompositionLayerQuad gQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};

char const* Name(tf::View v) {
    switch (v) {
    case tf::View::World: return "the world";
    case tf::View::Screen: return "the 3D cinema screen";
    case tf::View::CinemaFlat: return "a cinematic's flat panel";
    case tf::View::Other: return "another flat panel";
    case tf::View::Unknown: return "nothing yet";
    }
    return "?";
}

void Destroy() {
    mevr::host::swapchains::Destroy(gHandle);
    gHandle = XR_NULL_HANDLE;
    gImages.clear();
    gTargets.clear();
}

bool Ensure(ID3D11Device* device, XrSession session) {
    if (session != gSession) {
        mevr::host::swapchains::Forget(gHandle);
        gHandle = XR_NULL_HANDLE;
        gImages.clear();
        gTargets.clear();
        gFailed = false;
        gSession = session;
    }
    if (gHandle != XR_NULL_HANDLE) return true;
    if (gFailed && !mevr::host::swapchains::RetryDue(gFailedMs)) return false;
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    info.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;   // SteamVR refuses the plain UNORM format for swapchains (XrResult -26)
    info.sampleCount = 1;
    info.width = kSize;
    info.height = kSize;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (XR_FAILED(mevr::host::swapchains::Create(session, info, "fade", [] { Destroy(); }, gHandle, gImages))) {
        gFailed = true;
        gFailedMs = GetTickCount64();
        return false;
    }
    gFailed = false;
    gTargets.resize(gImages.size());
    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;   // the runtime's images are typeless: the view names the format
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    for (size_t i = 0; i < gImages.size(); ++i) {
        if (FAILED(device->CreateRenderTargetView(gImages[i], &rd, gTargets[i].GetAddressOf()))) {
            mevr::log::Error("transition fade: no render target on the swapchain's image {}; no fades", i);
            Destroy();
            gFailed = true;
            gFailedMs = GetTickCount64();
            return false;
        }
    }
    mevr::host::testloop::NameSwapchain(gHandle, "fade");
    return true;
}

} // namespace

namespace MELEVR::MevrBridge {

const void* TransitionFadeLayer(void* session, void* viewSpace, ID3D11DeviceContext* context, int view) noexcept {
    tf::View const v = view == 0 ? tf::View::World
                     : view == 1 ? tf::View::Screen
                     : view == 2 ? tf::View::CinemaFlat
                     : view == 3 ? tf::View::Other
                                 : tf::View::Unknown;
    tf::View const before = gFade.Last();
    unsigned const countBefore = gFade.Count();
    float const fadeS = mevr::host::cine_screen::FadeSeconds();
    double const nowS = static_cast<double>(GetTickCount64()) / 1000.0;
    // A cut inside a world conversation (the player re-placed at Shepard's mark after the opening, research/59 "Part A").
    static std::uint32_t lastCut = 0;
    if (auto const convo = mevr::host::conversation_world::Current(); convo && convo->cut != lastCut) {
        if (lastCut != 0 || convo->cut != 0) {
            gFade.Cut(nowS, fadeS);
        }
        lastCut = convo->cut;
    }
    float const alpha = gFade.Update(v, nowS, fadeS);
    if (gFade.Count() != countBefore && gLogs < kMaxLogs) {
        ++gLogs;
        mevr::log::Info("transition fade: {} -> {}: from black over {:.2f} s", Name(before), Name(v), fadeS);
    }
    XrSession const xrSession = static_cast<XrSession>(session);
    if (alpha <= 0.002f || xrSession == XR_NULL_HANDLE || viewSpace == nullptr || context == nullptr) {
        return nullptr;
    }
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    if (!device || !Ensure(device.Get(), xrSession)) {
        return nullptr;
    }
    std::uint32_t index = 0;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(gHandle, &acquire, &index))) return nullptr;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (XR_FAILED(xrWaitSwapchainImage(gHandle, &wait)) || index >= gTargets.size()) {
        xrReleaseSwapchainImage(gHandle, &release);
        return nullptr;
    }
    float const black[4] = {0.0f, 0.0f, 0.0f, alpha};   // premultiplied: black at any opacity
    context->ClearRenderTargetView(gTargets[index].Get(), black);
    mevr::host::testloop::OnImageWritten(gHandle, gImages[index]);
    xrReleaseSwapchainImage(gHandle, &release);
    gQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    gQuad.space = static_cast<XrSpace>(viewSpace);
    gQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    gQuad.subImage.swapchain = gHandle;
    gQuad.subImage.imageRect = {{0, 0}, {kSize, kSize}};
    gQuad.subImage.imageArrayIndex = 0;
    gQuad.pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -0.5f}};
    gQuad.size = {8.0f, 8.0f};
    return &gQuad;
}

} // namespace MELEVR::MevrBridge
