// SPDX-License-Identifier: MIT
// The galaxy map as a hologram we draw: the host's side (plugin/GalaxyHologram.hpp).

#include "plugin/GalaxyHologram.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "plugin/GalaxyHologramDraw.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/MevrMenu.hpp"
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
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace mevr::host::galaxy_hologram {

namespace {

namespace gh = vr::galaxy_holo;
namespace gd = galaxy_draw;
namespace controllers = vr::controllers;
using Microsoft::WRL::ComPtr;

constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
constexpr std::uint64_t kFreshMs = 400;  // the mod sends every tick while the map is up

struct EyeChain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;
    int width = 0;
    int height = 0;
    bool failed = false;
    void Destroy() {
        if (handle != XR_NULL_HANDLE) {
            testloop::ForgetSwapchain(handle);
            xrDestroySwapchain(handle);
        }
        handle = XR_NULL_HANDLE;
        images.clear();
        width = height = 0;
    }
};

XrSession gSession = XR_NULL_HANDLE;
EyeChain gChains[2];
ComPtr<ID3D11Texture2D> gMsaa;
ComPtr<ID3D11RenderTargetView> gMsaaRtv;
int gMsaaWidth = 0;
int gMsaaHeight = 0;
gd::Renderer gRenderer;
bool gRendererTried = false;
XrCompositionLayerProjection gLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
XrCompositionLayerProjectionView gViews[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};

bool gUp = false;
gh::Table gTable;
gh::Pointer gPointer;
int gHovered = -1;
std::uint64_t gUpMs = 0;
std::uint32_t gLastLevel = 0;
float gForward[3] = {1.0f, 0.0f, 0.0f};
std::atomic<bool> gActive{false};
std::atomic<bool> gHovering{false};
int gPicks = 0;
unsigned gFrames = 0;
double gDrawMsSum = 0.0;
std::uint64_t gLastCostLogMs = 0;
char gStatus[384] = "{}";

// Saves and restores what drawing touches on the game's context (UE3 caches its own state).
class StateGuard {
public:
    explicit StateGuard(ID3D11DeviceContext* c) : c_(c) {
        c_->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_, &dsv_);
        c_->OMGetBlendState(&blend_, blendFactor_, &sampleMask_);
        c_->OMGetDepthStencilState(&depth_, &stencilRef_);
        c_->RSGetState(&raster_);
        c_->RSGetViewports(&viewportCount_, viewports_);
        c_->IAGetPrimitiveTopology(&topology_);
        c_->IAGetInputLayout(&layout_);
        c_->IAGetVertexBuffers(0, 1, &vb_, &vbStride_, &vbOffset_);
        c_->VSGetShader(&vs_, nullptr, nullptr);
        c_->PSGetShader(&ps_, nullptr, nullptr);
        c_->GSGetShader(&gs_, nullptr, nullptr);
        c_->HSGetShader(&hs_, nullptr, nullptr);
        c_->DSGetShader(&ds_, nullptr, nullptr);
        c_->VSGetConstantBuffers(0, 1, &vsCb_);
        c_->PSGetShaderResources(0, 1, &srv_);
        c_->PSGetConstantBuffers(0, 1, &psCb_);
        c_->PSGetSamplers(0, 1, &sampler_);
    }
    ~StateGuard() {
        ID3D11ShaderResourceView* none = nullptr;
        c_->PSSetShaderResources(0, 1, &none);
        c_->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_, dsv_);
        c_->OMSetBlendState(blend_, blendFactor_, sampleMask_);
        c_->OMSetDepthStencilState(depth_, stencilRef_);
        c_->RSSetState(raster_);
        c_->RSSetViewports(viewportCount_, viewports_);
        c_->IASetPrimitiveTopology(topology_);
        c_->IASetInputLayout(layout_);
        c_->IASetVertexBuffers(0, 1, &vb_, &vbStride_, &vbOffset_);
        c_->VSSetShader(vs_, nullptr, 0);
        c_->PSSetShader(ps_, nullptr, 0);
        c_->GSSetShader(gs_, nullptr, 0);
        c_->HSSetShader(hs_, nullptr, 0);
        c_->DSSetShader(ds_, nullptr, 0);
        c_->VSSetConstantBuffers(0, 1, &vsCb_);
        c_->PSSetShaderResources(0, 1, &srv_);
        c_->PSSetConstantBuffers(0, 1, &psCb_);
        c_->PSSetSamplers(0, 1, &sampler_);
        for (auto*& r : rtvs_) {
            Drop(r);
        }
        Drop(dsv_);
        Drop(blend_);
        Drop(depth_);
        Drop(raster_);
        Drop(layout_);
        Drop(vb_);
        Drop(vs_);
        Drop(ps_);
        Drop(gs_);
        Drop(hs_);
        Drop(ds_);
        Drop(vsCb_);
        Drop(srv_);
        Drop(psCb_);
        Drop(sampler_);
    }
    StateGuard(StateGuard const&) = delete;
    StateGuard& operator=(StateGuard const&) = delete;

private:
    template <typename T>
    static void Drop(T*& p) {
        if (p != nullptr) {
            p->Release();
            p = nullptr;
        }
    }

    ID3D11DeviceContext* c_;
    ID3D11RenderTargetView* rtvs_[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* dsv_ = nullptr;
    ID3D11BlendState* blend_ = nullptr;
    FLOAT blendFactor_[4] = {};
    UINT sampleMask_ = 0xFFFFFFFFu;
    ID3D11DepthStencilState* depth_ = nullptr;
    UINT stencilRef_ = 0;
    ID3D11RasterizerState* raster_ = nullptr;
    D3D11_VIEWPORT viewports_[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT viewportCount_ = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_PRIMITIVE_TOPOLOGY topology_ = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11Buffer* vb_ = nullptr;
    UINT vbStride_ = 0;
    UINT vbOffset_ = 0;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11GeometryShader* gs_ = nullptr;
    ID3D11HullShader* hs_ = nullptr;
    ID3D11DomainShader* ds_ = nullptr;
    ID3D11Buffer* vsCb_ = nullptr;
    ID3D11ShaderResourceView* srv_ = nullptr;
    ID3D11Buffer* psCb_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
};

bool MakeChain(XrSession session, int width, int height, EyeChain& out, char const* name) {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    info.format = kFormat;
    info.sampleCount = 1;
    info.width = static_cast<std::uint32_t>(width);
    info.height = static_cast<std::uint32_t>(height);
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    XrResult r = xrCreateSwapchain(session, &info, &out.handle);
    std::uint32_t count = 0;
    if (XR_SUCCEEDED(r)) {
        r = xrEnumerateSwapchainImages(out.handle, 0, &count, nullptr);
    }
    std::vector<XrSwapchainImageD3D11KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if (XR_SUCCEEDED(r) && count > 0) {
        r = xrEnumerateSwapchainImages(out.handle, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    }
    if (XR_FAILED(r) || count == 0) {
        log::Error("galaxy hologram: the {} eye's swapchain {}x{} failed (XrResult {}); no hologram", name, width, height, static_cast<int>(r));
        out.Destroy();
        out.failed = true;
        return false;
    }
    for (auto const& image : images) {
        out.images.push_back(image.texture);
    }
    out.width = width;
    out.height = height;
    testloop::NameSwapchain(out.handle, name);
    return true;
}

bool EnsureTargets(ID3D11Device* device, XrSession session, int width, int height) {
    if (session != gSession) {
        for (EyeChain& c : gChains) {
            c.Destroy();
            c.failed = false;
        }
        gSession = session;
    }
    for (int e = 0; e < 2; ++e) {
        EyeChain& c = gChains[e];
        if (c.failed) {
            return false;
        }
        if (c.handle == XR_NULL_HANDLE || c.width != width || c.height != height) {
            c.Destroy();
            if (!MakeChain(session, width, height, c, e == 0 ? "galaxy-left" : "galaxy-right")) {
                return false;
            }
            log::Info("galaxy hologram: the {} eye's swapchain {}x{}, {} images", e == 0 ? "left" : "right", width, height, c.images.size());
        }
    }
    if (gMsaa == nullptr || gMsaaWidth != width || gMsaaHeight != height) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(width);
        td.Height = static_cast<UINT>(height);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = kFormat;
        td.SampleDesc.Count = 4;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        gMsaa.Reset();
        gMsaaRtv.Reset();
        if (FAILED(device->CreateTexture2D(&td, nullptr, gMsaa.GetAddressOf())) ||
            FAILED(device->CreateRenderTargetView(gMsaa.Get(), nullptr, gMsaaRtv.GetAddressOf()))) {
            log::Error("galaxy hologram: the {}x{} 4x MSAA target failed; no hologram", width, height);
            for (EyeChain& c : gChains) {
                c.failed = true;
            }
            return false;
        }
        gMsaaWidth = width;
        gMsaaHeight = height;
    }
    return true;
}

bool EnsureRenderer(ID3D11Device* device) {
    if (gRendererTried) {
        return gRenderer.Ready();
    }
    gRendererTried = true;
    std::filesystem::path font;
    std::wstring const& settings = MELEVR::MevrBridge::SettingsPath();
    if (!settings.empty()) {
        font = std::filesystem::path(settings).parent_path() / L"fonts" / L"Rajdhani-SemiBold.ttf";
    }
    std::string error;
    bool const ok = gRenderer.Init(device, font, &error);
    if (!ok) {
        log::Error("galaxy hologram: the renderer failed ({}); no hologram", error);
    } else {
        log::Info("galaxy hologram: renderer ready{}", error.empty() ? std::string() : " (" + error + ")");
    }
    return ok;
}

// Where a ray meets the table's plane, if it does within the table (with a margin).
bool RayOnTable(XrVector3f const& origin, XrVector3f const& dir) {
    XrVector3f const up = vr::overlay::Rotate(gTable.pose.orientation, {0.0f, 1.0f, 0.0f});
    float const denom = gh::Dot(dir, up);
    if (std::abs(denom) < 1e-4f) {
        return false;
    }
    float const t = gh::Dot(gh::Sub(gTable.pose.position, origin), up) / denom;
    if (t <= 0.0f || t > 4.0f) {
        return false;
    }
    XrVector3f const hit = gh::Add(origin, gh::Mul(dir, t));
    return gh::Length(gh::Sub(hit, gTable.pose.position)) < gTable.radiusM * 1.2f;
}

void Down(char const* why) {
    if (gUp) {
        log::Info("galaxy hologram: down ({}); {} picks", why, gPicks);
    }
    gUp = false;
    gHovered = -1;
    gPointer.Reset();
    gActive.store(false, std::memory_order_release);
    gHovering.store(false, std::memory_order_release);
    std::snprintf(gStatus, sizeof(gStatus), "{\"up\": false, \"picks\": %d}", gPicks);
}

} // namespace

XrCompositionLayerBaseHeader const* Layer(Frame const& frame) {
    auto scene = gh::Latest(kFreshMs);
    if (!scene || frame.gameMode != 9 || frame.menuOpen || frame.session == XR_NULL_HANDLE || frame.context == nullptr ||
        frame.device == nullptr) {
        Down(!scene ? "the mod sends no map" : frame.gameMode != 9 ? "not the galaxy map" : "a menu is open");
        return nullptr;
    }
    std::uint64_t const now = GetTickCount64();
    gh::TableSettings settings;
    settings.distanceM = scene->tableDistanceM;
    settings.belowM = scene->tableBelowM;
    settings.radiusM = scene->tableRadiusM;
    if (!gUp) {
        if (!frame.headValid) {
            return nullptr;
        }
        gTable = gh::PlaceTable(frame.head, settings);
        gUp = true;
        gUpMs = now;
        gLastLevel = 0;
        log::Info("galaxy hologram: up: the table at ({:.2f}, {:.2f}, {:.2f}) m, {:.2f} m in radius, facing heading {:.0f} deg",
                  gTable.pose.position.x, gTable.pose.position.y, gTable.pose.position.z, gTable.radiusM,
                  vr::overlay::HeadingOf(gTable.pose.orientation) * 57.29578f);
    }
    gActive.store(true, std::memory_order_release);
    if (scene->level != gLastLevel) {
        // The table's 'away' is the game camera's heading when the level appears, held while the game's camera flies to
        // what's selected (else the hologram would swing round under the player).
        gLastLevel = scene->level;
        std::copy(std::begin(scene->forward), std::end(scene->forward), std::begin(gForward));
        log::Info("galaxy hologram: {}", gh::Describe(*scene));
        gPointer.Reset();
    }
    std::copy(std::begin(gForward), std::end(gForward), std::begin(scene->forward));
    float const timeS = static_cast<float>(now - gUpMs) / 1000.0f;
    auto const placed = gh::PlaceObjects(*scene, gTable, settings);

    // The pointer: the right controller's aim ray (the left one if the right isn't tracked).
    controllers::State const state = controllers::Latest();
    int hand = state.right.aimValid ? 1 : state.left.aimValid ? 0 : -1;
    controllers::Hand const& h = hand == 0 ? state.left : state.right;
    XrVector3f const origin = h.aim.position;
    XrVector3f const dir = gh::Normalize(vr::overlay::Rotate(h.aim.orientation, {0.0f, 0.0f, -1.0f}));
    bool const valid = state.active && hand >= 0;
    int const candidate = valid ? gh::Pick(origin, dir, placed, *scene) : -1;
    bool const onTable = valid && (candidate >= 0 || RayOnTable(origin, dir));
    auto const selected = gPointer.Step(candidate, onTable ? h.trigger : 0.0f, valid);
    if (candidate != gHovered) {
        if (candidate >= 0) {
            halcyon::RequestHaptic(hand, 0.012f, 0.18f);
        }
        gHovered = candidate;
    }
    if (selected && *selected >= 0 && *selected < scene->objectCount) {
        gh::Object const& o = scene->objects[*selected];
        gh::PostPick(o.id);
        ++gPicks;
        halcyon::RequestHaptic(hand, 0.04f, 0.6f);
        log::Info("galaxy hologram: picked '{}' (id {}) with the {} controller", o.label, o.id, hand == 1 ? "right" : "left");
    }
    gHovering.store(onTable, std::memory_order_release);

    gh::DrawList list = gh::Build(*scene, gTable, settings, placed, gHovered, timeS);
    if (onTable) {
        // The beam: from the controller to what it hovers, or to where it meets the table.
        XrVector3f end = gh::Add(origin, gh::Mul(dir, 0.6f));
        if (gHovered >= 0) {
            for (gh::Placed const& p : placed) {
                if (p.index == gHovered) {
                    end = p.position;
                }
            }
        } else {
            XrVector3f const up = vr::overlay::Rotate(gTable.pose.orientation, {0.0f, 1.0f, 0.0f});
            float const t = gh::Dot(gh::Sub(gTable.pose.position, origin), up) / gh::Dot(dir, up);
            end = gh::Add(origin, gh::Mul(dir, t));
        }
        gh::Colour const beam = gHovered >= 0 ? gh::Colour{1.0f, 0.62f, 0.25f, 0.8f} : gh::Colour{0.35f, 0.75f, 1.0f, 0.45f};
        list.lines.push_back({gh::Add(origin, gh::Mul(dir, 0.03f)), end, 0.0018f, beam});
        list.sprites.push_back({end, 0.004f, beam, gh::SpriteStyle::Star, {}});
    }

    if (!EnsureRenderer(frame.device)) {
        return nullptr;
    }
    int const width = std::clamp(frame.eyeWidth > 0 ? frame.eyeWidth * 4 / 5 : 1600, 512, 2400);
    int const height = std::clamp(frame.eyeHeight > 0 ? frame.eyeHeight * 4 / 5 : 1600, 512, 2400);
    if (!EnsureTargets(frame.device, frame.session, width, height)) {
        return nullptr;
    }
    LARGE_INTEGER t0{}, t1{}, freq{};
    QueryPerformanceCounter(&t0);
    gRenderer.Prepare(list, frame.head);
    {
        StateGuard guard(frame.context);
        for (int e = 0; e < 2; ++e) {
            EyeChain& c = gChains[e];
            std::uint32_t index = 0;
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (XR_FAILED(xrAcquireSwapchainImage(c.handle, &acquire, &index))) {
                return nullptr;
            }
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout = XR_INFINITE_DURATION;
            if (XR_FAILED(xrWaitSwapchainImage(c.handle, &wait)) || index >= c.images.size()) {
                XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrReleaseSwapchainImage(c.handle, &release);
                return nullptr;
            }
            float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            frame.context->ClearRenderTargetView(gMsaaRtv.Get(), clear);
            gd::Eye const eye{frame.eyes[e], frame.fov[e]};
            gRenderer.Draw(frame.context, gMsaaRtv.Get(), width, height, eye, timeS);
            frame.context->ResolveSubresource(c.images[index], 0, gMsaa.Get(), 0, kFormat);
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            xrReleaseSwapchainImage(c.handle, &release);
            gViews[e].pose = frame.eyes[e];
            gViews[e].fov = frame.fov[e];
            gViews[e].subImage.swapchain = c.handle;
            gViews[e].subImage.imageRect = {{0, 0}, {width, height}};
            gViews[e].subImage.imageArrayIndex = 0;
        }
    }
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&freq);
    gDrawMsSum += static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
    ++gFrames;
    if (now - gLastCostLogMs > 10000) {
        if (gLastCostLogMs != 0 && gFrames > 0) {
            log::Info("galaxy hologram: last 10 s: {} frames, {:.3f} ms a frame on the CPU (both eyes' drawing submitted), {} objects, "
                      "hovered '{}', {} picks",
                gFrames, gDrawMsSum / gFrames, scene->objectCount, gHovered >= 0 ? scene->objects[gHovered].label : "", gPicks);
        }
        gLastCostLogMs = now;
        gFrames = 0;
        gDrawMsSum = 0.0;
    }
    std::snprintf(gStatus, sizeof(gStatus),
        "{\"up\": true, \"level\": %d, \"objects\": %d, \"hovered\": %d, \"onTable\": %s, \"picks\": %d, \"table\": [%.3f, %.3f, %.3f], "
        "\"radiusM\": %.3f}",
        scene->level, scene->objectCount, gHovered, onTable ? "true" : "false", gPicks, gTable.pose.position.x, gTable.pose.position.y,
        gTable.pose.position.z, gTable.radiusM);
    gLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    gLayer.space = frame.appSpace;
    gLayer.viewCount = 2;
    gLayer.views = gViews;
    return reinterpret_cast<XrCompositionLayerBaseHeader const*>(&gLayer);
}

bool Active() {
    return gActive.load(std::memory_order_acquire);
}

bool Hovering() {
    return gHovering.load(std::memory_order_acquire);
}

char const* StatusJson() {
    return gStatus;
}

} // namespace mevr::host::galaxy_hologram
