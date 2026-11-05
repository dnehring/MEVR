// SPDX-License-Identifier: MIT
// The power wheel's quads (plugin/PowerWheel.hpp). Drawn with ImGui in a context of our own: Halcyon's menu keeps the
// default one; both run on the Present thread, and the current context is always put back.

#include "plugin/PowerWheel.hpp"

#include "core/Log.hpp"
#include "core/vr/PowerWheel.hpp"
#include "plugin/Glow.hpp"
#include "plugin/MevrMenu.hpp"
#include "plugin/PowerIcons.hpp"
#include "plugin/PowerWheelDraw.hpp"
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
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mevr::host::power_wheel {
namespace {

using Microsoft::WRL::ComPtr;
namespace pw = vr::power_wheel;
namespace draw = power_wheel_draw;
namespace icons = power_icons;

constexpr std::int64_t kSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // the bytes are sRGB already
constexpr float kHintWidthM = 0.30f;

// A canvas: ImGui draws into `canvas`, the glow writes `result`, which is copied into the swapchain's image.
struct Canvas {
    unsigned width = 0;
    unsigned height = 0;
    ComPtr<ID3D11Texture2D> canvas;
    ComPtr<ID3D11RenderTargetView> canvasRtv;
    ComPtr<ID3D11ShaderResourceView> canvasSrv;
    ComPtr<ID3D11Texture2D> result;
    ComPtr<ID3D11RenderTargetView> resultRtv;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;  // owned by the runtime
    bool swapchainFailed = false;
    char const* name = "";
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
};

struct Gpu {
    ComPtr<ID3D11Device> device;
    ImGuiContext* imgui = nullptr;
    draw::Fonts fonts;
    glow::Glow glow;
    std::map<int, ComPtr<ID3D11ShaderResourceView>> icons;
    bool failed = false;
};
Gpu gGpu;
Canvas gWheel{draw::kCanvas, draw::kCanvas};
Canvas gHint{draw::kHintWidth, draw::kHintHeight};
XrSession gSession = XR_NULL_HANDLE;
std::uint64_t gLastDrawMs = 0;
std::uint32_t gLoggedSerial = 0;
bool gLoggedSize = false;
bool gLoggedFirst = false;

bool MakeCanvas(ID3D11Device* device, Canvas& c) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = c.width;
    td.Height = c.height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // ImGui's colours are sRGB-encoded bytes; copied raw into the SRGB swapchain
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    return SUCCEEDED(device->CreateTexture2D(&td, nullptr, c.canvas.GetAddressOf())) &&
        SUCCEEDED(device->CreateRenderTargetView(c.canvas.Get(), nullptr, c.canvasRtv.GetAddressOf())) &&
        SUCCEEDED(device->CreateShaderResourceView(c.canvas.Get(), nullptr, c.canvasSrv.GetAddressOf())) &&
        SUCCEEDED(device->CreateTexture2D(&td, nullptr, c.result.GetAddressOf())) &&
        SUCCEEDED(device->CreateRenderTargetView(c.result.Get(), nullptr, c.resultRtv.GetAddressOf()));
}

// Our ImGui context, the canvases and the glow, made once (the game's device lives as long as the process).
bool EnsureGpu(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (gGpu.failed) {
        return false;
    }
    if (gGpu.imgui != nullptr) {
        return gGpu.device.Get() == device;
    }
    if (!MakeCanvas(device, gWheel) || !MakeCanvas(device, gHint)) {
        log::Error("power wheel: canvas textures failed; no power wheel in the headset");
        gGpu.failed = true;
        return false;
    }
    std::string error;
    if (!gGpu.glow.Initialize(device, &error)) {
        log::Warn("power wheel: {}; drawn without its glow", error);
    }
    gGpu.device = device;
    ImGuiContext* const previous = ImGui::GetCurrentContext();
    gGpu.imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(gGpu.imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(draw::kCanvas), static_cast<float>(draw::kCanvas));
    std::vector<std::filesystem::path> folders;
    if (!MELEVR::MevrBridge::SettingsPath().empty()) {
        folders.push_back(std::filesystem::path(MELEVR::MevrBridge::SettingsPath()).parent_path() / L"fonts");
    }
    std::string chosen;
    gGpu.fonts = draw::LoadFonts(io, folders, chosen);
    bool const ok = ImGui_ImplDX11_Init(device, context);
    ImGui::SetCurrentContext(previous);  // Halcyon's menu keeps its own (or none)
    if (!ok) {
        log::Error("power wheel: ImGui's D3D11 backend didn't start; no power wheel in the headset");
        gGpu.failed = true;
        return false;
    }
    log::Info("power wheel: ImGui context of its own, canvas {}x{}, hint {}x{}, fonts: {}; glow {}", draw::kCanvas, draw::kCanvas,
        draw::kHintWidth, draw::kHintHeight, chosen, gGpu.glow.Ready() ? "on" : "off");
    return true;
}

ImTextureID Icon(int id) {
    if (id < 0 || id >= static_cast<int>(icons::Icon::Count)) {
        id = 0;
    }
    auto it = gGpu.icons.find(id);
    if (it == gGpu.icons.end()) {
        std::vector<std::uint32_t> const pixels = icons::Picture(static_cast<icons::Icon>(id));
        D3D11_TEXTURE2D_DESC td{};
        td.Width = icons::kPixels;
        td.Height = icons::kPixels;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(), icons::kPixels * 4, 0};
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        if (FAILED(gGpu.device->CreateTexture2D(&td, &data, texture.GetAddressOf())) ||
            FAILED(gGpu.device->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf()))) {
            srv.Reset();
        }
        it = gGpu.icons.emplace(id, srv).first;
    }
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(it->second.Get()));
}

// Draws into the canvas (the game's context; render targets and viewports put back), then the glow into `result`.
template <class Paint>
void Render(ID3D11DeviceContext* context, Canvas& c, float dt, Paint const& paint) {
    ImGuiContext* const previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(gGpu.imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(c.width), static_cast<float>(c.height));
    io.DeltaTime = std::clamp(dt, 1.0f / 500.0f, 0.25f);
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    paint(ImGui::GetBackgroundDrawList());
    ImGui::Render();

    ID3D11RenderTargetView* savedRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* savedDsv = nullptr;
    D3D11_VIEWPORT savedViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT savedViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRtvs, &savedDsv);
    context->RSGetViewports(&savedViewportCount, savedViewports);
    ID3D11RenderTargetView* target = c.canvasRtv.Get();
    context->OMSetRenderTargets(1, &target, nullptr);
    float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->ClearRenderTargetView(target, clear);
    D3D11_VIEWPORT const viewport{0.0f, 0.0f, static_cast<float>(c.width), static_cast<float>(c.height), 0.0f, 1.0f};
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
    // The glow runs in a device context state of its own (the game's untouched); without it the plain canvas is copied.
    if (!gGpu.glow.Ready() || !gGpu.glow.Apply(context, c.canvasSrv.Get(), c.width, c.height, c.resultRtv.Get())) {
        context->CopyResource(c.result.Get(), c.canvas.Get());
    }
}

bool EnsureSwapchain(XrSession session, Canvas& c) {
    if (c.swapchain != XR_NULL_HANDLE) {
        return true;
    }
    if (c.swapchainFailed) {
        return false;
    }
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = kSwapchainFormat;
    info.sampleCount = 1;
    info.width = c.width;
    info.height = c.height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    XrResult r = xrCreateSwapchain(session, &info, &c.swapchain);
    std::uint32_t count = 0;
    if (XR_SUCCEEDED(r)) {
        r = xrEnumerateSwapchainImages(c.swapchain, 0, &count, nullptr);
    }
    std::vector<XrSwapchainImageD3D11KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if (XR_SUCCEEDED(r) && count > 0) {
        r = xrEnumerateSwapchainImages(c.swapchain, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    }
    if (XR_FAILED(r) || count == 0) {
        log::Error("power wheel: {} swapchain {}x{} failed (XrResult {})", c.name, c.width, c.height, static_cast<int>(r));
        if (c.swapchain != XR_NULL_HANDLE) {
            xrDestroySwapchain(c.swapchain);
        }
        c.swapchain = XR_NULL_HANDLE;
        c.images.clear();
        c.swapchainFailed = true;
        return false;
    }
    c.images.clear();
    for (auto const& image : images) {
        c.images.push_back(image.texture);
    }
    testloop::NameSwapchain(c.swapchain, c.name);  // snapshots: <stamp>-powerwheel.png, and in the eyes' composites
    log::Info("power wheel: {} swapchain {}x{}, {} images", c.name, c.width, c.height, count);
    return true;
}

// Acquire, copy the result in, let the test loop copy it for a snapshot, release.
bool Submit(ID3D11DeviceContext* context, Canvas& c) {
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    std::uint32_t index = 0;
    if (XR_FAILED(xrAcquireSwapchainImage(c.swapchain, &acquire, &index))) {
        return false;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    bool const ok = XR_SUCCEEDED(xrWaitSwapchainImage(c.swapchain, &wait)) && index < c.images.size();
    if (ok) {
        context->CopyResource(c.images[index], c.result.Get());
        testloop::OnImageWritten(c.swapchain, c.images[index]);
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(c.swapchain, &release);
    return ok;
}

XrVector3f Rotate(XrQuaternionf q, XrVector3f v) {
    XrVector3f const u{q.x, q.y, q.z};
    auto cross = [](XrVector3f a, XrVector3f b) { return XrVector3f{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
    XrVector3f const t = cross(u, v);
    XrVector3f const t2{2.0f * t.x, 2.0f * t.y, 2.0f * t.z};
    XrVector3f const c = cross(u, t2);
    return {v.x + q.w * t2.x + c.x, v.y + q.w * t2.y + c.y, v.z + q.w * t2.z + c.z};
}

// A quad at `pose` moved by (right, up) metres in its own plane.
XrCompositionLayerQuad Quad(Frame const& frame, Canvas const& c, XrPosef pose, float right, float up, float widthM) {
    XrVector3f const shift = Rotate(pose.orientation, {right, up, 0.0f});
    pose.position = {pose.position.x + shift.x, pose.position.y + shift.y, pose.position.z + shift.z};
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied (ImGui over a clear canvas, glow as light)
    quad.space = frame.appSpace;
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = c.swapchain;
    quad.subImage.imageRect = {{0, 0}, {static_cast<std::int32_t>(c.width), static_cast<std::int32_t>(c.height)}};
    quad.pose = pose;
    quad.size = {widthM, widthM * static_cast<float>(c.height) / static_cast<float>(c.width)};
    return quad;
}

}  // namespace

int Layers(Frame const& frame, XrCompositionLayerQuad const** out, int capacity) {
    if (frame.session != gSession) {
        gWheel.swapchain = XR_NULL_HANDLE;  // the old session's swapchains died with it
        gWheel.images.clear();
        gHint.swapchain = XR_NULL_HANDLE;
        gHint.images.clear();
        gSession = frame.session;
    }
    gWheel.name = "powerwheel";
    gHint.name = "powerhint";
    pw::View const view = pw::Latest();
    if (view.open == 0 || capacity <= 0) {
        return 0;
    }
    if (view.size != sizeof(pw::View)) {
        if (!gLoggedSize) {
            gLoggedSize = true;
            log::Warn("power wheel: the mod's view is {} bytes, the host's {}: not drawn; rebuild both", view.size, sizeof(pw::View));
        }
        return 0;
    }
    if (!frame.worldFrame || frame.menuOpen || frame.session == XR_NULL_HANDLE || frame.context == nullptr || frame.device == nullptr) {
        return 0;
    }
    if (!EnsureGpu(frame.device, frame.context) || !EnsureSwapchain(frame.session, gWheel)) {
        return 0;
    }
    std::uint64_t const now = GetTickCount64();
    float const dt = gLastDrawMs == 0 ? 1.0f / 72.0f : static_cast<float>(now - gLastDrawMs) / 1000.0f;
    gLastDrawMs = now;

    // The wheel, as the mod sent it.
    float const widthM = std::clamp(view.widthM, 0.15f, 1.0f);
    float const pixelsPerM = static_cast<float>(draw::kCanvas) / widthM;
    draw::WheelView wheel;
    int const count = std::clamp(view.count, 0, pw::kMaxEntries);
    for (int i = 0; i < count; ++i) {
        pw::ViewEntry const& e = view.entries[i];
        draw::Entry entry;
        entry.label.assign(e.label, strnlen(e.label, sizeof(e.label)));
        entry.icon = Icon(e.icon);
        entry.discipline = e.discipline == 1 ? draw::Discipline::Tech : e.discipline == 2 ? draw::Discipline::Combat : draw::Discipline::Biotic;
        entry.cooldown = std::clamp(e.cooldown, 0.0f, 1.0f);
        entry.enabled = e.enabled != 0;
        wheel.entries.push_back(std::move(entry));
    }
    wheel.selected = view.selected;
    wheel.selectedFor = view.selectedFor;
    wheel.open = std::clamp(view.openFor / 0.18f, 0.0f, 1.0f);
    wheel.time = view.openFor;
    wheel.emblem = view.emblem != 0 ? draw::Emblem::N7 : draw::Emblem::None;
    wheel.upperCase = view.upperCase != 0;
    // The palm's offset, in canvas pixels, kept within the hub.
    float const hx = view.hand.x * pixelsPerM;
    float const hy = view.hand.y * pixelsPerM;
    float const hl = std::hypot(hx, hy);
    float const hubPx = wheel.tile.innerRadius * 0.8f;
    wheel.hand = hl > hubPx ? vr::power_wheel::Vec2{hx * hubPx / hl, hy * hubPx / hl} : vr::power_wheel::Vec2{hx, hy};
    Render(frame.context, gWheel, dt, [&](ImDrawList* dl) { draw::DrawWheel(dl, gGpu.fonts, wheel); });
    if (!Submit(frame.context, gWheel)) {
        return 0;
    }
    // The canvas's centre sits above the wheel's (kCentreY is below the middle): the quad moves up by that much.
    float const up = (draw::kCentreY - static_cast<float>(draw::kCanvas) * 0.5f) / pixelsPerM;
    gWheel.quad = Quad(frame, gWheel, view.pose, 0.0f, up, widthM);
    int n = 0;
    out[n++] = &gWheel.quad;
    if (view.serial != gLoggedSerial || !gLoggedFirst) {
        gLoggedSerial = view.serial;
        gLoggedFirst = true;
        log::Info("power wheel: #{} drawn, {} tiles, {:.2f} m wide at ({:.2f}, {:.2f}, {:.2f}) m{}", view.serial, count, widthM,
            view.pose.position.x, view.pose.position.y, view.pose.position.z, view.hint != 0 ? ", with the hint card" : "");
    }

    // The hint card beside it, to the right as seen, a little high.
    if (view.hint != 0 && n < capacity && EnsureSwapchain(frame.session, gHint)) {
        draw::HintView hint;
        hint.open = std::clamp((view.openFor - 0.15f) / 0.25f, 0.0f, 1.0f);
        Render(frame.context, gHint, dt, [&](ImDrawList* dl) { draw::DrawHint(dl, gGpu.fonts, hint); });
        if (Submit(frame.context, gHint)) {
            gHint.quad = Quad(frame, gHint, view.pose, widthM * 0.5f + kHintWidthM * 0.5f + 0.02f, widthM * 0.30f, kHintWidthM);
            out[n++] = &gHint.quad;
        }
    }
    return n;
}

}  // namespace mevr::host::power_wheel
