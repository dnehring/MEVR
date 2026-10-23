// SPDX-License-Identifier: MIT
// The squad weapon menu's quad (plugin/SquadPanel.hpp). Drawn (plugin/SquadPanelDraw) with ImGui in a
// context of our own: Halcyon's menu keeps the default one; both run on the Present thread, and the
// current context is always put back.

#include "plugin/SquadPanel.hpp"
#include "plugin/SquadPanelDraw.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/SquadMenu.hpp"
#include "plugin/TestLoop.hpp"

#include "imgui.h"
#include "imgui_impl_dx11.h"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

namespace mevr::host::squad_panel {
namespace {

using Microsoft::WRL::ComPtr;
namespace menu = vr::squad_menu;

constexpr std::int64_t kSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // the bytes are sRGB already

// ---- GPU side -----------------------------------------------------------------------------------------
struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    ImGuiContext* imgui = nullptr;
    ImFont* font = nullptr;
    bool failed = false;
};
Gpu gGpu;

struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;  // owned by the runtime
    bool failed = false;
};
Swapchain gSwapchain;
XrSession gSession = XR_NULL_HANDLE;

// The placement: fixed while one opening lasts.
std::uint32_t gPlacedSerial = 0;
bool gPlaced = false;
XrPosef gPose{};
std::atomic<bool> gRecentered{false};
XrCompositionLayerQuad gQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
bool gLoggedSize = false;
std::uint64_t gLastDrawMs = 0;

// Our ImGui context and the canvas texture, made once (the game's device lives as long as the process).
bool EnsureGpu(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (gGpu.failed) {
        return false;
    }
    if (gGpu.imgui != nullptr) {
        return gGpu.device.Get() == device;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kWidth;
    td.Height = kHeight;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // ImGui's colours are sRGB-encoded bytes; copied raw into the SRGB swapchain
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&td, nullptr, gGpu.texture.GetAddressOf())) ||
        FAILED(device->CreateRenderTargetView(gGpu.texture.Get(), nullptr, gGpu.rtv.GetAddressOf()))) {
        log::Error("squad panel: canvas texture {}x{} failed; no squad menu in the headset", kWidth, kHeight);
        gGpu.failed = true;
        return false;
    }
    gGpu.device = device;
    ImGuiContext* const previous = ImGui::GetCurrentContext();
    gGpu.imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(gGpu.imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight));
    std::string chosen;
    gGpu.font = LoadFont(io, chosen);
    bool const ok = ImGui_ImplDX11_Init(device, context);
    ImGui::SetCurrentContext(previous);  // Halcyon's menu keeps its own (or none)
    if (!ok) {
        log::Error("squad panel: ImGui's D3D11 backend didn't start; no squad menu in the headset");
        gGpu.failed = true;
        return false;
    }
    log::Info("squad panel: ImGui context of its own (Halcyon's menu keeps the default), canvas {}x{}, font {}", kWidth, kHeight,
        chosen);
    return true;
}

// Draws the menu into the canvas (the game's context; render targets and viewports put back).
void Render(ID3D11DeviceContext* context, menu::View const& view, float dt) {
    ImGuiContext* const previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(gGpu.imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(kWidth), static_cast<float>(kHeight));
    io.DeltaTime = std::clamp(dt, 1.0f / 500.0f, 0.25f);
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    Draw(ImGui::GetBackgroundDrawList(), gGpu.font, view);
    ImGui::Render();

    ID3D11RenderTargetView* savedRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* savedDsv = nullptr;
    D3D11_VIEWPORT savedViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT savedViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRtvs, &savedDsv);
    context->RSGetViewports(&savedViewportCount, savedViewports);
    ID3D11RenderTargetView* target = gGpu.rtv.Get();
    context->OMSetRenderTargets(1, &target, nullptr);
    float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->ClearRenderTargetView(target, clear);
    D3D11_VIEWPORT const viewport{0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight), 0.0f, 1.0f};
    context->RSSetViewports(1, &viewport);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());  // backs up and restores the rest of the pipeline
    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRtvs, savedDsv);
    context->RSSetViewports(savedViewportCount, savedViewports);
    for (auto*& r : savedRtvs) {
        if (r != nullptr) {
            r->Release();
        }
    }
    if (savedDsv != nullptr) {
        savedDsv->Release();
    }
    ImGui::SetCurrentContext(previous);
}

bool EnsureSwapchain(XrSession session) {
    if (gSwapchain.handle != XR_NULL_HANDLE) {
        return true;
    }
    if (gSwapchain.failed) {
        return false;
    }
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = kSwapchainFormat;
    info.sampleCount = 1;
    info.width = kWidth;
    info.height = kHeight;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    XrResult r = xrCreateSwapchain(session, &info, &gSwapchain.handle);
    std::uint32_t count = 0;
    if (XR_SUCCEEDED(r)) {
        r = xrEnumerateSwapchainImages(gSwapchain.handle, 0, &count, nullptr);
    }
    std::vector<XrSwapchainImageD3D11KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if (XR_SUCCEEDED(r) && count > 0) {
        r = xrEnumerateSwapchainImages(gSwapchain.handle, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    }
    if (XR_FAILED(r) || count == 0) {
        log::Error("squad panel: swapchain {}x{} failed (XrResult {}); no squad menu in the headset", kWidth, kHeight,
            static_cast<int>(r));
        if (gSwapchain.handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(gSwapchain.handle);
        }
        gSwapchain = {};
        gSwapchain.failed = true;
        return false;
    }
    for (auto const& image : images) {
        gSwapchain.images.push_back(image.texture);
    }
    testloop::NameSwapchain(gSwapchain.handle, "squad");  // snapshots: <stamp>-squad.png, and in the eyes' composites
    log::Info("squad panel: swapchain {}x{}, {} images", kWidth, kHeight, count);
    return true;
}

// Acquire, copy the canvas in, let the test loop copy it for a snapshot, release.
bool Submit(ID3D11DeviceContext* context) {
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    std::uint32_t index = 0;
    if (XR_FAILED(xrAcquireSwapchainImage(gSwapchain.handle, &acquire, &index))) {
        return false;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    bool const ok = XR_SUCCEEDED(xrWaitSwapchainImage(gSwapchain.handle, &wait)) && index < gSwapchain.images.size();
    if (ok) {
        context->CopyResource(gSwapchain.images[index], gGpu.texture.Get());
        testloop::OnImageWritten(gSwapchain.handle, gSwapchain.images[index]);
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(gSwapchain.handle, &release);
    return ok;
}

// Where it goes when it opens: above the left hand, facing the eyes (squad_menu::PlaceAtHand). Without
// a tracked left hand, in front of the head and a little down.
XrPosef Place(menu::View const& view, Frame const& frame, char const*& from) {
    vr::controllers::State const controllers = vr::controllers::Latest();
    XrVector3f const head = frame.head.position;
    if (controllers.active && (controllers.left.gripValid || controllers.left.aimValid)) {
        from = controllers.left.gripValid ? "the left grip" : "the left aim pose";
        XrVector3f const hand = controllers.left.gripValid ? controllers.left.grip.position : controllers.left.aim.position;
        return menu::PlaceAtHand(hand, head, std::clamp(view.distance, 0.1f, 0.8f));
    }
    from = "in front of the head (no left hand)";
    XrQuaternionf const& q = frame.head.orientation;
    XrVector3f const forward{-2.0f * (q.x * q.z + q.w * q.y), 0.0f, -(1.0f - 2.0f * (q.x * q.x + q.y * q.y))};
    float const length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    XrVector3f const f = length > 1e-4f ? XrVector3f{forward.x / length, 0.0f, forward.z / length} : XrVector3f{0.0f, 0.0f, -1.0f};
    XrVector3f const hand{head.x + f.x * 0.45f, head.y - 0.35f, head.z + f.z * 0.45f};
    return menu::PlaceAtHand(hand, head, std::clamp(view.distance, 0.1f, 0.8f));
}

} // namespace

void Recentered() {
    gRecentered.store(true, std::memory_order_release);
}

XrCompositionLayerQuad const* Layer(Frame const& frame) {
    if (frame.session != gSession) {
        gSwapchain = {};  // the old session's swapchain died with it
        gSession = frame.session;
        gPlaced = false;
    }
    bool const recentered = gRecentered.exchange(false, std::memory_order_acq_rel);
    menu::View const view = menu::Latest();
    if (!view.open) {
        gPlaced = false;  // the next opening is placed afresh
        return nullptr;
    }
    if (view.size != sizeof(menu::View)) {
        if (!gLoggedSize) {
            gLoggedSize = true;
            log::Warn("squad panel: the mod's menu is {} bytes, the host's {}: not drawn; rebuild both", view.size,
                sizeof(menu::View));
        }
        return nullptr;
    }
    if (!frame.worldFrame || frame.menuOpen || !frame.headValid || frame.session == XR_NULL_HANDLE || frame.context == nullptr ||
        frame.device == nullptr) {
        return nullptr;
    }
    if (!EnsureGpu(frame.device, frame.context) || !EnsureSwapchain(frame.session)) {
        return nullptr;
    }
    if (!gPlaced || view.serial != gPlacedSerial || recentered) {
        char const* from = "";
        gPose = Place(view, frame, from);
        gPlaced = true;
        gPlacedSerial = view.serial;
        log::Info("squad panel: menu #{} placed{} {:.2f} m from {}, at ({:.2f}, {:.2f}, {:.2f}) m, {:.2f} m wide; head at "
                  "({:.2f}, {:.2f}, {:.2f})", view.serial, recentered ? " again (recentered)" : "", view.distance, from,
            gPose.position.x, gPose.position.y, gPose.position.z, view.width, frame.head.position.x, frame.head.position.y,
            frame.head.position.z);
    }
    std::uint64_t const now = GetTickCount64();
    float const dt = gLastDrawMs == 0 ? 1.0f / 72.0f : static_cast<float>(now - gLastDrawMs) / 1000.0f;
    gLastDrawMs = now;
    Render(frame.context, view, dt);
    if (!Submit(frame.context)) {
        return nullptr;
    }
    float const width = std::clamp(view.width, 0.15f, 1.0f);
    gQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    gQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied (ImGui over a clear canvas)
    gQuad.space = frame.appSpace;
    gQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    gQuad.subImage.swapchain = gSwapchain.handle;
    gQuad.subImage.imageRect = {{0, 0}, {static_cast<std::int32_t>(kWidth), static_cast<std::int32_t>(kHeight)}};
    gQuad.pose = gPose;
    gQuad.size = {width, width * static_cast<float>(kHeight) / static_cast<float>(kWidth)};
    return &gQuad;
}

} // namespace mevr::host::squad_panel
