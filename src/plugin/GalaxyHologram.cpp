// SPDX-License-Identifier: MIT
// The galaxy map as a hologram we draw: the host's side (plugin/GalaxyHologram.hpp).

#include "plugin/GalaxyHologram.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "core/vr/TestRequest.hpp"
#include "plugin/GalaxyHologramDraw.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/MevrMenu.hpp"
#include "plugin/ScopeCaptureSource.hpp"
#include "plugin/TestLoop.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
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
#include <cstring>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <mutex>
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
std::atomic<bool> gInWorld{false};   // over the CIC's projector (the game's UI goes to a panel over the table)
std::atomic<bool> gAsking{false};    // the game's travel question is up (its own box; no picks on the hologram)
int gPicks = 0;
unsigned gFrames = 0;
double gDrawMsSum = 0.0;
std::uint64_t gLastCostLogMs = 0;
std::mutex gStatusMutex;  // the snapshot's JSON may be asked for from another thread
std::string gStatus = "{}";

void SetStatus(std::string text) {
    std::lock_guard lock(gStatusMutex);
    gStatus = std::move(text);
}

std::string JVec3(XrVector3f const& v) {
    return std::format("[{:.4f}, {:.4f}, {:.4f}]", v.x, v.y, v.z);
}

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
    gInWorld.store(false, std::memory_order_release);
    gAsking.store(false, std::memory_order_release);
    SetStatus(std::format("{{\"up\": false, \"picks\": {}}}", gPicks));
}

} // namespace

XrCompositionLayerBaseHeader const* Layer(Frame const& frame) {
    auto scene = gh::Latest(kFreshMs);
    // In the CIC ([Galaxy] inWorld) the table stands over the projector on foot (0), through the map's opening (6) and with the
    // map open (9); otherwise only with the map open.
    bool const inWorld = scene && scene->inWorld != 0 && scene->anchorValid != 0;
    bool const modeOk = scene && (inWorld ? (frame.gameMode == 0 || frame.gameMode == 6 || frame.gameMode == 9) : frame.gameMode == 9);
    if (!scene || !modeOk || frame.menuOpen || frame.session == XR_NULL_HANDLE || frame.context == nullptr || frame.device == nullptr) {
        Down(!scene ? "the mod sends no map" : !modeOk ? "not the galaxy map" : "a menu is open");
        return nullptr;
    }
    std::uint64_t const now = GetTickCount64();
    gh::TableSettings settings;
    settings.distanceM = scene->tableDistanceM;
    settings.belowM = scene->tableBelowM;
    settings.radiusM = scene->tableRadiusM;
    if (inWorld) {
        // Over the projector: where the mod put it this tick (it follows the world as the player turns or moves with the stick).
        gTable.pose.orientation = gh::TableOrientation(scene->anchorYaw, scene->tiltDeg * 0.01745329f);
        gTable.pose.position = {scene->anchor[0], scene->anchor[1], scene->anchor[2]};
        gTable.radiusM = scene->tableRadiusM;
    }
    if (!gUp) {
        if (!frame.headValid && !inWorld) {
            return nullptr;
        }
        if (!inWorld) {
            gTable = gh::PlaceTable(frame.head, settings);
        }
        gUp = true;
        gUpMs = now;
        gLastLevel = 0;
        log::Info("galaxy hologram: up: the table at ({:.2f}, {:.2f}, {:.2f}) m, {:.2f} m in radius, facing heading {:.0f} deg",
                  gTable.pose.position.x, gTable.pose.position.y, gTable.pose.position.z, gTable.radiusM,
                  vr::overlay::HeadingOf(gTable.pose.orientation) * 57.29578f);
    }
    gActive.store(true, std::memory_order_release);
    gInWorld.store(inWorld, std::memory_order_release);
    if (inWorld && scene->windowOn != 0) {
        // The game's own map is on the window over the table (WindowLayers): no table of ours and no picks; the game's cursor
        // (the game pointer on the window) does the picking.
        gAsking.store(scene->askTravel != 0, std::memory_order_release);
        gHovering.store(false, std::memory_order_release);
        gPointer.Reset();
        gHovered = -1;
        SetStatus(std::format("{{\"up\": true, \"window\": true, \"level\": {}, \"askTravel\": {}, \"table\": {}}}", scene->level,
            scene->askTravel != 0 ? "true" : "false", JVec3(gTable.pose.position)));
        return nullptr;
    }
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
    // The game's travel question is its own box (the game's UI on a panel over the table, HalcyonOverlays, answered by the
    // game pointer): the hologram takes no picks and no trigger meanwhile.
    bool const asking = inWorld && scene->askTravel != 0;
    if (asking != gAsking.load(std::memory_order_relaxed)) {
        log::Info("galaxy hologram: {}", asking ? "the game asks whether to travel: its own box over the table, the hologram waits"
                                                : "the game's travel question is answered");
    }
    gAsking.store(asking, std::memory_order_release);
    int candidate = -1;
    if (valid && !asking) {
        candidate = gh::Pick(origin, dir, placed, *scene);
    }
    bool const onTable = valid && !asking && (candidate >= 0 || RayOnTable(origin, dir));
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
            testloop::OnImageWritten(c.handle, c.images[index]);  // snapshots see our layer as the runtime does
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
                gFrames, gDrawMsSum / gFrames, scene->objectCount, gHovered >= 0 && gHovered < scene->objectCount ? scene->objects[gHovered].label : "", gPicks);
        }
        gLastCostLogMs = now;
        gFrames = 0;
        gDrawMsSum = 0.0;
    }
    // Where each object and button is in the app space (tools/make-galaxy-pick-recording.py --snapshot aims at them).
    std::string objects;
    for (gh::Placed const& p : placed) {
        if (p.index < 0 || p.index >= scene->objectCount) {
            continue;
        }
        gh::Object const& o = scene->objects[p.index];
        objects += std::format("{}{{\"label\": {}, \"id\": {}, \"at\": {}, \"radiusM\": {:.4f}}}", objects.empty() ? "" : ", ",
            vr::test_request::JsonString(std::string_view(o.label, strnlen(o.label, gh::kLabelBytes))), o.id, JVec3(p.position), p.radiusM);
    }
    SetStatus(std::format(
        "{{\"up\": true, \"level\": {}, \"objectCount\": {}, \"hovered\": {}, \"onTable\": {}, \"picks\": {}, "
        "\"askTravel\": {}, \"table\": {}, \"radiusM\": {:.3f}, \"tiltDeg\": {:.1f}, \"objects\": [{}]}}",
        scene->level, scene->objectCount, gHovered, onTable ? "true" : "false", gPicks, scene->askTravel != 0 ? "true" : "false",
        JVec3(gTable.pose.position), gTable.radiusM, scene->tiltDeg, objects));
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

bool InWorld() {
    return gActive.load(std::memory_order_acquire) && gInWorld.load(std::memory_order_acquire);
}

bool AskingTravel() {
    return InWorld() && gAsking.load(std::memory_order_acquire);
}

XrPosef UiPanelPose(XrVector3f const& head) {
    // With the game's map on the projector's hologram, the UI lies on it too (its labels belong to the map's picture there).
    if (auto const scene = gh::Latest(kFreshMs); scene && scene->windowOn != 0 && scene->windowDisc != 0 && scene->discRadiusM > 0.1f) {
        return gh::DiscPose({scene->discCentre[0], scene->discCentre[1], scene->discCentre[2]},
            {scene->discNormal[0], scene->discNormal[1], scene->discNormal[2]}, head);
    }
    // Over the table's centre at about the eyes' height, brought toward the player so it stands clear of the raised far edge,
    // upright and turned to face the eyes.
    XrVector3f centre = gTable.pose.position;
    XrVector3f toHead = gh::Sub(head, centre);
    toHead.y = 0.0f;
    float const flat = gh::Length(toHead);
    XrVector3f const toward = flat > 1e-3f ? gh::Mul(toHead, 1.0f / flat) : XrVector3f{0.0f, 0.0f, 1.0f};
    float const forward = std::min(gTable.radiusM * 0.6f, std::max(0.0f, flat - 1.2f));
    XrPosef pose{};
    pose.position = gh::Add(centre, gh::Mul(toward, forward));
    pose.position.y = head.y - 0.10f;
    float const yaw = std::atan2(toward.x, toward.z);  // the quad's +Z (its front) toward the player
    pose.orientation = {0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
    return pose;
}

// ---- The game's own map as a stereo window over the table (scene->windowOn; research/51 section 8) --------------------------
namespace {

constexpr char kWindowShader[] = R"(
Texture2D picture : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Params : register(b0) {
    float4 rect;  // the capture's shown part: u0, v0, du, dv
    float4 misc;  // x: the decode gamma (1: an sRGB view decodes it), y: the edge's fade width (a share of the window),
                  // z: 1 a disc inscribed in the window (the projector's round hologram), w: the window's width over its height
    float4 look;  // x: the backdrop's opacity (the map's space, 0..1)
};
struct VsOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
VsOut VSMain(uint id : SV_VertexID) {
    VsOut o;
    float2 t = float2((id << 1) & 2, id & 2);
    o.uv = t;
    o.pos = float4(t * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
float4 PSMain(VsOut i) : SV_Target {
    float3 c = picture.SampleLevel(linearClamp, rect.xy + i.uv * rect.zw, 0).rgb;
    c = pow(max(c, 0.0), misc.x);
    float fade;
    if (misc.z > 0.5) {
        // The disc in the window's own coordinates (the same for both eyes: its edge on the window's plane), 0.5 its radius.
        float2 p = (i.uv - 0.5) * float2(misc.w, 1.0) / min(misc.w, 1.0);
        fade = saturate((0.5 - length(p)) / max(misc.y, 1e-4));
    } else {
        float2 e = min(i.uv, 1.0 - i.uv) / max(misc.y, 1e-4);
        fade = saturate(min(e.x, e.y));
    }
    fade = fade * fade * (3.0 - 2.0 * fade);
    // Premultiplied, its alpha its brightness but at least the backdrop's opacity: the map's space (its starfield's black)
    // dims the room behind it as the full screen's did, and below 1 the room still shows faintly through (a hologram's look);
    // the bright galaxy covers it (pure added light with no alpha drops out of the snapshots' straight-alpha composite).
    float a = max(saturate(max(c.r, max(c.g, c.b))), look.x) * fade;
    return float4(min(c * fade, a), a);
}
)";

struct WindowParams {
    float rect[4];
    float misc[4];
    float look[4];
};

struct WindowEye {
    EyeChain chain;
    ComPtr<ID3D11Texture2D> texture;  // the capture's (a reference of ours)
    ComPtr<ID3D11ShaderResourceView> srv;
    std::uint64_t resource = 0;
    std::uint64_t generation = 0;
    vr::scope_capture::TexturePath hint;
    float decode = 1.0f;
    std::uint64_t triedMs = 0;
    int described = 0;
};

struct WindowState {
    WindowEye eyes[2];
    XrSession session = XR_NULL_HANDLE;
    bool pipelineTried = false;
    bool pipelineReady = false;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> params;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> depth;
    XrCompositionLayerQuad quads[2] = {{XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD}};
    bool up = false;
    std::atomic<bool> on{false};
    std::atomic<float> panelWidthM{0.0f};
    std::uint64_t failedGeneration = 0;
    unsigned frames = 0;
};
WindowState gWindow;

bool EnsureWindowPipeline(ID3D11Device* device) {
    if (gWindow.pipelineTried) {
        return gWindow.pipelineReady;
    }
    gWindow.pipelineTried = true;
    ComPtr<ID3DBlob> vsBlob, psBlob, errors;
    UINT const flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if (FAILED(D3DCompile(kWindowShader, sizeof(kWindowShader) - 1, "galaxy-window", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0,
            vsBlob.GetAddressOf(), errors.ReleaseAndGetAddressOf())) ||
        FAILED(D3DCompile(kWindowShader, sizeof(kWindowShader) - 1, "galaxy-window", nullptr, nullptr, "PSMain", "ps_5_0", flags, 0,
            psBlob.GetAddressOf(), errors.ReleaseAndGetAddressOf()))) {
        log::Error("galaxy window: its shader didn't compile ({})",
            errors ? std::string(static_cast<char const*>(errors->GetBufferPointer()), errors->GetBufferSize()) : std::string("?"));
        return false;
    }
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(WindowParams);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;  // replaces
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    gWindow.pipelineReady =
        SUCCEEDED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, gWindow.vs.GetAddressOf())) &&
        SUCCEEDED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, gWindow.ps.GetAddressOf())) &&
        SUCCEEDED(device->CreateBuffer(&cb, nullptr, gWindow.params.GetAddressOf())) &&
        SUCCEEDED(device->CreateSamplerState(&sd, gWindow.sampler.GetAddressOf())) &&
        SUCCEEDED(device->CreateBlendState(&bd, gWindow.blend.GetAddressOf())) &&
        SUCCEEDED(device->CreateRasterizerState(&rd, gWindow.raster.GetAddressOf())) &&
        SUCCEEDED(device->CreateDepthStencilState(&dd, gWindow.depth.GetAddressOf()));
    if (!gWindow.pipelineReady) {
        log::Error("galaxy window: its pipeline objects failed");
    }
    return gWindow.pipelineReady;
}

// The capture's texture behind its FTextureResource (plugin/ScopeCaptureSource's guarded search), once per render target.
bool FindWindowTexture(WindowEye& eye, std::uint64_t resource, std::uint64_t generation, unsigned width, unsigned height, ID3D11Device* device,
    char const* name) {
    if (eye.srv != nullptr && eye.resource == resource && eye.generation == generation) {
        return true;
    }
    std::uint64_t const now = GetTickCount64();
    if (eye.resource == resource && eye.generation == generation && now - eye.triedMs < 1000) {
        return false;  // tried this one less than a second ago
    }
    eye.triedMs = now;
    bool const again = eye.resource == resource && eye.generation == generation;
    eye.texture.Reset();
    eye.srv.Reset();
    eye.resource = resource;
    eye.generation = generation;
    D3D11_TEXTURE2D_DESC desc{};
    scope_capture_source::NoteDevice(device);
    eye.texture.Attach(scope_capture_source::FindTargetTexture(resource, width, height, eye.hint, &desc));
    DXGI_FORMAT view = DXGI_FORMAT_UNKNOWN;
    float decode = 1.0f;
    if (eye.texture == nullptr || !scope_capture_source::CaptureViewFormat(desc.Format, view, decode)) {
        if (!again) {
            log::Warn("galaxy window: the {} capture's texture wasn't found behind 0x{:X} ({}x{}); trying once a second", name, resource,
                width, height);
        } else if (++eye.described <= 3) {
            log::Info("galaxy window: textures behind the {} capture's resource:{}; the search: {}; texture {}, format {}", name,
                scope_capture_source::DescribeTextures(resource), scope_capture_source::LastSearch(), eye.texture != nullptr ? "found" : "none",
                static_cast<int>(desc.Format));
        }
        eye.texture.Reset();
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = view;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(eye.texture.Get(), &sv, eye.srv.GetAddressOf()))) {
        log::Warn("galaxy window: no view of the {} capture's texture (format {})", name, static_cast<int>(desc.Format));
        eye.texture.Reset();
        return false;
    }
    eye.decode = decode;
    log::Info("galaxy window: the {} capture's texture {}x{} format {} (decoded with gamma {:.1f})", name, desc.Width, desc.Height,
        static_cast<int>(desc.Format), decode);
    return true;
}

void WindowDown(char const* why) {
    if (gWindow.up) {
        log::Info("galaxy window: down ({}; {} frames)", why, gWindow.frames);
    }
    gWindow.up = false;
    gWindow.frames = 0;
    gWindow.on.store(false, std::memory_order_release);
}

} // namespace

int WindowLayers(Frame const& frame, XrCompositionLayerBaseHeader const** out) {
    auto const scene = gh::Latest(kFreshMs);
    bool const wanted = scene && scene->windowOn != 0 && scene->inWorld != 0 && scene->anchorValid != 0 && frame.gameMode == 9 &&
        !frame.menuOpen && frame.session != XR_NULL_HANDLE && frame.context != nullptr && frame.device != nullptr && frame.headValid;
    if (!wanted) {
        WindowDown(!scene || scene->windowOn == 0 ? "the mod sends no window" : "not the map's mode");
        return 0;
    }
    float const crop = std::clamp(scene->windowCrop, 0.0f, 0.2f);
    float const widthM = std::clamp(scene->windowWidthM, 0.5f, 10.0f);  // on the projector's hologram, its diameter (about 6 m)
    gWindow.panelWidthM.store(widthM / (1.0f - crop), std::memory_order_release);  // the UI frame: the captures' whole width
    gWindow.on.store(true, std::memory_order_release);
    if (scene->windowTicks < 2 || scene->windowWidth < 64 || scene->windowHeight < 64) {
        return 0;  // its first renders aren't there yet
    }
    if (frame.session != gWindow.session) {
        for (WindowEye& eye : gWindow.eyes) {
            eye.chain.Destroy();
            eye.chain.failed = false;
        }
        gWindow.session = frame.session;
    }
    if (!EnsureWindowPipeline(frame.device)) {
        return 0;
    }
    int const shownW = std::max(64, static_cast<int>(std::lround(static_cast<double>(scene->windowWidth) * (1.0 - crop))));
    int const height = static_cast<int>(scene->windowHeight);
    for (int e = 0; e < 2; ++e) {
        WindowEye& eye = gWindow.eyes[e];
        char const* const name = e == 0 ? "left" : "right";
        if (!FindWindowTexture(eye, scene->windowResource[e], scene->windowGeneration, scene->windowWidth, scene->windowHeight, frame.device,
                name)) {
            return 0;
        }
        if (eye.chain.failed) {
            return 0;
        }
        if (eye.chain.handle == XR_NULL_HANDLE || eye.chain.width != shownW || eye.chain.height != height) {
            eye.chain.Destroy();
            if (!MakeChain(frame.session, shownW, height, eye.chain, e == 0 ? "galaxy-window-left" : "galaxy-window-right")) {
                return 0;
            }
        }
    }
    XrPosef const pose = UiPanelPose(frame.head.position);
    float const heightM = widthM * static_cast<float>(height) / static_cast<float>(shownW);
    {
        StateGuard guard(frame.context);
        ID3D11DeviceContext* const c = frame.context;
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->IASetInputLayout(nullptr);
        c->VSSetShader(gWindow.vs.Get(), nullptr, 0);
        c->PSSetShader(gWindow.ps.Get(), nullptr, 0);
        c->GSSetShader(nullptr, nullptr, 0);
        c->HSSetShader(nullptr, nullptr, 0);
        c->DSSetShader(nullptr, nullptr, 0);
        c->PSSetConstantBuffers(0, 1, gWindow.params.GetAddressOf());
        c->PSSetSamplers(0, 1, gWindow.sampler.GetAddressOf());
        float const factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        c->OMSetBlendState(gWindow.blend.Get(), factor, 0xFFFFFFFFu);
        c->OMSetDepthStencilState(gWindow.depth.Get(), 0);
        c->RSSetState(gWindow.raster.Get());
        D3D11_VIEWPORT const viewport{0.0f, 0.0f, static_cast<float>(shownW), static_cast<float>(height), 0.0f, 1.0f};
        c->RSSetViewports(1, &viewport);
        for (int e = 0; e < 2; ++e) {
            WindowEye& eye = gWindow.eyes[e];
            std::uint32_t index = 0;
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            if (XR_FAILED(xrAcquireSwapchainImage(eye.chain.handle, &acquire, &index))) {
                return 0;
            }
            XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wait.timeout = XR_INFINITE_DURATION;
            bool drawn = false;
            if (XR_SUCCEEDED(xrWaitSwapchainImage(eye.chain.handle, &wait)) && index < eye.chain.images.size()) {
                D3D11_RENDER_TARGET_VIEW_DESC rv{};
                rv.Format = kFormat;
                rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                ComPtr<ID3D11RenderTargetView> rtv;
                if (SUCCEEDED(frame.device->CreateRenderTargetView(eye.chain.images[index], &rv, rtv.GetAddressOf()))) {
                    // The left eye drops its left edge, the right eye its right (core/vr/CineScreen's crop).
                    WindowParams params{};
                    params.rect[0] = e == 0 ? crop : 0.0f;
                    params.rect[1] = 0.0f;
                    params.rect[2] = 1.0f - crop;
                    params.rect[3] = 1.0f;
                    params.misc[0] = eye.decode;
                    bool const round = scene->windowRound != 0;
                    params.misc[1] = round ? 0.05f : 0.04f;
                    params.misc[2] = round ? 1.0f : 0.0f;
                    params.misc[3] = static_cast<float>(shownW) / static_cast<float>(std::max(height, 1));
                    params.look[0] = std::min(static_cast<float>(scene->windowBackdrop), 100.0f) / 100.0f;
                    c->UpdateSubresource(gWindow.params.Get(), 0, nullptr, &params, 0, 0);
                    ID3D11RenderTargetView* const target = rtv.Get();
                    c->OMSetRenderTargets(1, &target, nullptr);
                    c->PSSetShaderResources(0, 1, eye.srv.GetAddressOf());
                    c->Draw(3, 0);
                    ID3D11ShaderResourceView* none = nullptr;
                    c->PSSetShaderResources(0, 1, &none);
                    testloop::OnImageWritten(eye.chain.handle, eye.chain.images[index]);
                    drawn = true;
                }
            }
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            xrReleaseSwapchainImage(eye.chain.handle, &release);
            if (!drawn) {
                return 0;
            }
            XrCompositionLayerQuad& q = gWindow.quads[e];
            q = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied, alpha = brightness
            q.space = frame.appSpace;
            q.eyeVisibility = e == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
            q.subImage.swapchain = eye.chain.handle;
            q.subImage.imageRect = {{0, 0}, {shownW, height}};
            q.pose = pose;
            q.size = {widthM, heightM};
            out[e] = reinterpret_cast<XrCompositionLayerBaseHeader const*>(&q);
        }
    }
    if (!gWindow.up) {
        XrVector3f const facing = vr::overlay::Rotate(pose.orientation, {0.0f, 0.0f, 1.0f});
        log::Info("galaxy window: up: the game's map {:.2f} x {:.2f} m{} at ({:.2f}, {:.2f}, {:.2f}), {}, facing ({:.2f}, {:.2f}, {:.2f}), each "
                  "eye {}x{} of its capture (crop {:.1f}%)",
            widthM, heightM, scene->windowRound != 0 ? " (a disc)" : "", pose.position.x, pose.position.y, pose.position.z,
            scene->windowDisc != 0 ? "on the projector's hologram" : "upright over the table", facing.x, facing.y, facing.z, shownW, height,
            crop * 100.0f);
        gWindow.up = true;
    }
    ++gWindow.frames;
    return 2;
}

bool WindowOn() {
    return gWindow.on.load(std::memory_order_acquire);
}

float UiPanelWidthM(float fallbackM) {
    float const w = gWindow.panelWidthM.load(std::memory_order_acquire);
    return WindowOn() && w > 0.0f ? w : fallbackM;
}

std::string StatusJson() {
    std::lock_guard lock(gStatusMutex);
    return gStatus;
}

} // namespace mevr::host::galaxy_hologram
