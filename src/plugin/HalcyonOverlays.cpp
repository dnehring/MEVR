// Our quads on top of Halcyon's world layer (seams in src/halcyon/le1/mevr_bridge.h):
// - the body-locked HUD panel: d3d_capture.cpp's draw hooks send the game's UI draws of a
//   head-tracked frame into a panel texture instead of the eye images; here that texture goes into a
//   swapchain and becomes a quad fixed in the tracking space in front of the player's body;
// - the hand reticle: a small ring on the weapon's aim ray (or the right controller's aim pose
//   until the mod publishes the ray), replacing the game's screen-centre crosshair;
// - the squad weapon menu at the left hand while the mod has it open (plugin/SquadPanel.cpp);
// - the game's own weapon or power wheel on a panel of its own at a hand while one is up: its part of
//   the HUD panel texture (core/vr/WheelCrop: a default, then measured the first time a wheel opens),
//   cut out of the HUD panel meanwhile so it shows once (docs/squad-weapons-design.md, "Native wheel");
// - [MENUVIS] readbacks that tell whether Halcyon's Insert menu texture has visible pixels.
//
// The panel texture holds premultiplied colour and coverage from the UI's "over" draws; Scaleform's other
// blend modes (multiply, screen) add a tint texture (white = the world shows unchanged) that the panel's
// alpha is made from, so they reach the panel too instead of staying in the eyes.

#include "halcyon/le1/mevr_bridge.h"
#include "halcyon/le1/logger.h"
#include "halcyon/le1/pchud.h"
#include "halcyon/le1/vr_config.h"

#include "core/Log.hpp"
#include "core/vr/AimRay.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/FrameProbe.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "core/vr/SquadMenu.hpp"
#include "core/vr/WheelCrop.hpp"
#include "plugin/Calibration.hpp"
#include "plugin/Colour.hpp"
#include "plugin/Halcyon.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/MainMenu.hpp"
#endif
#include "plugin/PerfProbe.hpp"
#include "plugin/RecenterTilt.hpp"
#include "plugin/SightLayers.hpp"
#include "plugin/SquadPanel.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/WorldPanels.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <vector>

namespace MELEVR::MevrBridge {
namespace {

using Microsoft::WRL::ComPtr;
namespace overlay = mevr::vr::overlay;

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr int64_t kSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // the menu's family: every runtime has it
constexpr UINT kReticleSize = 128;

// Logs a line to our log and to Halcyon's (one-shots that belong next to Halcyon's [XRSUBMIT] lines).
void LogBoth(std::string const& line) {
    mevr::log::Info("{}", line);
    MELEVR::Logger::LogLine(line);
}

// ---- The panel texture: the game's UI draws land here (render thread) -------------------------------
struct PanelTexture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
    // The tint: what Scaleform's non-"over" draws do to whatever is behind the UI (white: nothing).
    ComPtr<ID3D11Texture2D> tint;
    ComPtr<ID3D11RenderTargetView> tintRtv;
    ComPtr<ID3D11ShaderResourceView> tintSrv;
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool failed = false;  // creation failed for this size/format: stop retrying every draw
};
PanelTexture gPanel;
std::atomic<bool> gArmed{false};
std::atomic<unsigned> gPanelDraws{0};
std::atomic<unsigned> gPanelDrawsLastFrame{0};
std::atomic<unsigned> gTintDraws{0};
std::atomic<bool> gPanelIsFloat{false};
bool gPanelNeedsClear = false;
bool gTintDirty = false;  // Present thread: the tint texture has draws in it (cleared to white with the panel)
bool gLoggedTint = false;

struct BlendEntry {
    ID3D11BlendState* game = nullptr;  // AddRef'd, so the address can't be reused for another state
    ComPtr<ID3D11BlendState> ours;
};
std::vector<BlendEntry> gBlends;  // render thread only; game blend states are few and long-lived

// Scaleform's non-"over" blend modes: the colour pass (alpha untouched) and the tint pass (nullptr: the
// mode leaves the world as is).
struct TintEntry {
    ID3D11BlendState* game = nullptr;  // AddRef'd
    ComPtr<ID3D11BlendState> colour;
    ComPtr<ID3D11BlendState> tint;
};
std::vector<TintEntry> gTintBlends;  // render thread only

struct DepthEntry {
    ID3D11DepthStencilState* game = nullptr;  // AddRef'd
    ComPtr<ID3D11DepthStencilState> ours;
};
std::vector<DepthEntry> gNoWrites;  // render thread only

// What a blend leaves of the destination, as a factor of the source alone: the tint pass's DestBlend
// (its SrcBlend is ZERO). False when it can't be said that way (MIN/MAX, a destination-dependent pair)
// or it's ONE (the world unchanged: no tint pass).
bool TintFactor(D3D11_RENDER_TARGET_BLEND_DESC const& rt, D3D11_BLEND& out) {
    if (rt.BlendOp != D3D11_BLEND_OP_ADD && rt.BlendOp != D3D11_BLEND_OP_REV_SUBTRACT) {
        return false;  // MIN/MAX don't scale the destination; SUBTRACT negates it
    }
    D3D11_BLEND const s = rt.SrcBlend;
    D3D11_BLEND const d = rt.DestBlend;
    if (s == D3D11_BLEND_DEST_COLOR && d == D3D11_BLEND_ZERO) {
        out = D3D11_BLEND_SRC_COLOR;  // multiply: dst * c
        return true;
    }
    if (s == D3D11_BLEND_INV_DEST_COLOR && d == D3D11_BLEND_ONE) {
        out = D3D11_BLEND_INV_SRC_COLOR;  // screen: dst * (1 - c) + c
        return true;
    }
    bool const sourceOnly = s != D3D11_BLEND_DEST_COLOR && s != D3D11_BLEND_INV_DEST_COLOR && s != D3D11_BLEND_DEST_ALPHA &&
        s != D3D11_BLEND_INV_DEST_ALPHA && s != D3D11_BLEND_SRC_ALPHA_SAT;
    if (!sourceOnly) {
        return false;
    }
    switch (d) {
    case D3D11_BLEND_ZERO:
    case D3D11_BLEND_SRC_COLOR:
    case D3D11_BLEND_INV_SRC_COLOR:
    case D3D11_BLEND_SRC_ALPHA:
    case D3D11_BLEND_INV_SRC_ALPHA:
        out = d;
        return true;
    default:
        return false;  // ONE (add) and the rest: the world stays as it is behind it
    }
}

bool IsFloatFormat(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R32G32B32A32_FLOAT ||
        f == DXGI_FORMAT_R11G11B10_FLOAT;
}
bool SamplesLinear(DXGI_FORMAT f) {
    return IsFloatFormat(f) || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}

// ---- Shaders ------------------------------------------------------------------------------------
// The panel blit: box-filters a window of the panel texture down by an integer factor (premultiplied,
// so the filter is exact), then writes linear premultiplied colour through the swapchain's sRGB view.
// The alpha is the coverage of the "over" draws combined with the tint (what the other blend modes left
// of the world behind): 1 - (1 - coverage) * tint. 8-bit UNORM panels hold the game's gamma-encoded
// blend: un-premultiply, decode, re-premultiply. Float (scRGB) panels: scale so SDR white is 1.0 and
// clip keeping the hue, like the eye blit. Pixels inside the mask rectangle come out transparent (the
// HUD panel while a wheel has a panel of its own).
// GridPS: the most opaque pixel (that same alpha) in each cell of a coarse grid over the whole texture,
// every 4th pixel each way: where the UI is (core/vr/WheelCrop).
// MEVR_PANEL_OUT: the colour correction's hook (plugin/Colour, research/27), the identity here; the corrected
// variant of PSMain is compiled with core/vr/Colour's prelude in front.
constexpr char kPanelShader[] = R"(
#ifndef MEVR_PANEL_OUT
#define MEVR_PANEL_OUT(c, pos) (c)
#endif
Texture2D<float4> src : register(t0);
Texture2D<float4> tint : register(t1);
cbuffer Params : register(b0) { uint4 info; float4 params; int4 window; int4 mask; };
struct VSOut { float4 pos : SV_Position; };
VSOut VSMain(uint id : SV_VertexID) {
    float2 p = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);
    VSOut o; o.pos = float4(p, 0.0, 1.0); return o;
}
float Behind(int2 q) {
    return saturate(dot(tint.Load(int3(q, 0)).rgb, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0)));
}
float4 PSMain(VSOut i) : SV_Target {
    uint f = max(info.x, 1u);
    int2 base = window.xy + int2(i.pos.xy) * int(f);
    if (mask.z > mask.x && base.x >= mask.x && base.x < mask.z && base.y >= mask.y && base.y < mask.w)
        return float4(0, 0, 0, 0);
    float4 sum = 0;
    float behind = 0;
    [loop] for (uint y = 0; y < f; ++y)
        [loop] for (uint x = 0; x < f; ++x) {
            int2 q = min(base + int2(x, y), int2(info.yz) - 1);
            sum += src.Load(int3(q, 0));
            behind += Behind(q);
        }
    float4 s = sum / float(f * f);
    float a = 1.0 - (1.0 - saturate(s.a)) * (behind / float(f * f));
    if (a < 1.0 / 1024.0) return float4(0, 0, 0, 0);
    float3 c = max(s.rgb, 0.0) / a;
    if (params.x > 0.5) {
        c *= params.y;
        c /= max(1.0, max(c.r, max(c.g, c.b)));
    } else {
        c = saturate(c);
        c = (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
    }
    c = MEVR_PANEL_OUT(c, i.pos);
    return float4(c * a, a);
}
float4 GridPS(VSOut i) : SV_Target {
    int2 o = int2(i.pos.xy) * int2(window.zw);
    float m = 0;
    [loop] for (int y = 0; y < window.w; y += 4)
        [loop] for (int x = 0; x < window.z; x += 4) {
            int2 q = min(o + int2(x, y), int2(info.yz) - 1);
            m = max(m, 1.0 - (1.0 - saturate(src.Load(int3(q, 0)).a)) * Behind(q));
        }
    return float4(m, m, m, m);
}
)";

struct PanelParams {
    std::uint32_t info[4];  // factor, source width, source height, 0
    float params[4];        // linear source (1/0), white scale, 0, 0
    std::int32_t window[4]; // blit: the window's origin (source pixels), 0, 0; grid: 0, 0, cell width, cell height
    std::int32_t mask[4];   // x0, y0, x1, y1 (source pixels); x1 <= x0: none
};
static_assert(sizeof(PanelParams) == 64);

using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR,
    LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

// `source`: kPanelShader unless given (the colour-corrected variant).
bool Compile(char const* entry, char const* profile, ComPtr<ID3DBlob>& out, std::string const* source = nullptr) {
    static D3DCompileFn compile = [] {
        HMODULE module = LoadLibraryW(L"d3dcompiler_47.dll");
        return module != nullptr ? reinterpret_cast<D3DCompileFn>(GetProcAddress(module, "D3DCompile")) : nullptr;
    }();
    if (compile == nullptr) {
        mevr::log::Error("overlays: d3dcompiler_47.dll unavailable; no HUD panel");
        return false;
    }
    ComPtr<ID3DBlob> errors;
    HRESULT const hr = compile(source != nullptr ? source->data() : kPanelShader,
        source != nullptr ? source->size() : sizeof(kPanelShader) - 1, "MEVR_HudPanel", nullptr, nullptr, entry,
        profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out.ReleaseAndGetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        mevr::log::Error("overlays: shader {} failed to compile: {}", entry,
            errors ? static_cast<char const*>(errors->GetBufferPointer()) : "(no message)");
        return false;
    }
    return true;
}

struct Pipeline {
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11PixelShader> colourPs;  // ps with the colour correction (plugin/Colour); nullptr: never corrected
    ComPtr<ID3D11PixelShader> gridPs;
    ComPtr<ID3D11Buffer> params;
    ComPtr<ID3D11BlendState> opaque;
    ComPtr<ID3D11DepthStencilState> noDepth;
    ComPtr<ID3D11RasterizerState> raster;
    bool tried = false;
    bool ready = false;
};
Pipeline gPipeline;

bool EnsurePipeline(ID3D11Device* device) {
    if (gPipeline.tried) {
        return gPipeline.ready;
    }
    gPipeline.tried = true;
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> gridBlob;
    if (!Compile("VSMain", "vs_5_0", vsBlob) || !Compile("PSMain", "ps_5_0", psBlob) || !Compile("GridPS", "ps_5_0", gridBlob)) {
        return false;
    }
    bool ok = SUCCEEDED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
                  gPipeline.vs.GetAddressOf())) &&
        SUCCEEDED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
            gPipeline.ps.GetAddressOf())) &&
        SUCCEEDED(device->CreatePixelShader(gridBlob->GetBufferPointer(), gridBlob->GetBufferSize(), nullptr,
            gPipeline.gridPs.GetAddressOf()));
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(PanelParams);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ok = ok && SUCCEEDED(device->CreateBuffer(&cb, nullptr, gPipeline.params.GetAddressOf()));
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(device->CreateBlendState(&bd, gPipeline.opaque.GetAddressOf()));
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    ok = ok && SUCCEEDED(device->CreateDepthStencilState(&dd, gPipeline.noDepth.GetAddressOf()));
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(device->CreateRasterizerState(&rd, gPipeline.raster.GetAddressOf()));
    gPipeline.ready = ok;
    if (!ok) {
        mevr::log::Error("overlays: HUD panel pipeline creation failed");
    }
    if (ok) {
        std::string const corrected = std::string(mevr::vr::colour::ShaderPrelude()) + kPanelShader;
        ComPtr<ID3DBlob> colourBlob;
        if (!Compile("PSMain", "ps_5_0", colourBlob, &corrected) ||
            FAILED(device->CreatePixelShader(colourBlob->GetBufferPointer(), colourBlob->GetBufferSize(), nullptr,
                gPipeline.colourPs.ReleaseAndGetAddressOf()))) {
            gPipeline.colourPs.Reset();
            mevr::log::Warn("overlays: the HUD panel's colour-corrected shader failed: the HUD stays uncorrected");
        }
    }
    return ok;
}

// Saves and restores everything the panel blit touches on the game's context: UE3 caches its own
// pipeline state, so whatever was bound has to be bound again afterwards.
class StateGuard {
public:
    explicit StateGuard(ID3D11DeviceContext* c) : c_(c) {
        c_->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_, &dsv_);
        c_->OMGetBlendState(&blend_, blendFactor_, &sampleMask_);
        c_->OMGetDepthStencilState(&depth_, &stencilRef_);
        c_->RSGetState(&raster_);
        c_->RSGetViewports(&viewportCount_, viewports_);
        c_->RSGetScissorRects(&scissorCount_, scissors_);
        c_->IAGetPrimitiveTopology(&topology_);
        c_->IAGetInputLayout(&layout_);
        c_->VSGetShader(&vs_, nullptr, nullptr);
        c_->PSGetShader(&ps_, nullptr, nullptr);
        c_->GSGetShader(&gs_, nullptr, nullptr);
        c_->HSGetShader(&hs_, nullptr, nullptr);
        c_->DSGetShader(&ds_, nullptr, nullptr);
        c_->PSGetShaderResources(0, 2, srv_);
        c_->PSGetConstantBuffers(0, 1, &cb_);
    }
    ~StateGuard() {
        ID3D11ShaderResourceView* none[2] = {nullptr, nullptr};
        c_->PSSetShaderResources(0, 2, none);
        c_->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_, dsv_);
        c_->OMSetBlendState(blend_, blendFactor_, sampleMask_);
        c_->OMSetDepthStencilState(depth_, stencilRef_);
        c_->RSSetState(raster_);
        c_->RSSetViewports(viewportCount_, viewports_);
        c_->RSSetScissorRects(scissorCount_, scissors_);
        c_->IASetPrimitiveTopology(topology_);
        c_->IASetInputLayout(layout_);
        c_->VSSetShader(vs_, nullptr, 0);
        c_->PSSetShader(ps_, nullptr, 0);
        c_->GSSetShader(gs_, nullptr, 0);
        c_->HSSetShader(hs_, nullptr, 0);
        c_->DSSetShader(ds_, nullptr, 0);
        c_->PSSetShaderResources(0, 2, srv_);
        c_->PSSetConstantBuffers(0, 1, &cb_);
        for (auto*& r : rtvs_) {
            Release(r);
        }
        Release(dsv_);
        Release(blend_);
        Release(depth_);
        Release(raster_);
        Release(layout_);
        Release(vs_);
        Release(ps_);
        Release(gs_);
        Release(hs_);
        Release(ds_);
        Release(srv_[0]);
        Release(srv_[1]);
        Release(cb_);
    }
    StateGuard(StateGuard const&) = delete;
    StateGuard& operator=(StateGuard const&) = delete;

private:
    template <typename T>
    static void Release(T*& p) {
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
    D3D11_RECT scissors_[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT scissorCount_ = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_PRIMITIVE_TOPOLOGY topology_ = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11GeometryShader* gs_ = nullptr;
    ID3D11HullShader* hs_ = nullptr;
    ID3D11DomainShader* ds_ = nullptr;
    ID3D11ShaderResourceView* srv_[2] = {};
    ID3D11Buffer* cb_ = nullptr;
};

// ---- Swapchains (Present thread) ---------------------------------------------------------------
struct QuadSwapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;  // owned by the runtime
    std::vector<ComPtr<ID3D11RenderTargetView>> rtvs;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool failed = false;

    void Forget() {  // the session that owned it is gone: its swapchains went with it
        handle = XR_NULL_HANDLE;
        images.clear();
        rtvs.clear();
        width = height = 0;
        failed = false;
    }
    void Destroy() {
        if (handle != XR_NULL_HANDLE) {
            mevr::host::testloop::ForgetSwapchain(handle);
            xrDestroySwapchain(handle);
        }
        Forget();
    }
};

XrSession gSession = XR_NULL_HANDLE;
QuadSwapchain gPanelSwapchain;
QuadSwapchain gReticleSwapchain;
UINT gPanelFactor = 2;

bool CreateSwapchain(XrSession session, std::uint32_t width, std::uint32_t height, QuadSwapchain& out,
    char const* what) {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = kSwapchainFormat;
    info.sampleCount = 1;
    info.width = width;
    info.height = height;
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
        r = xrEnumerateSwapchainImages(
            out.handle, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    }
    if (XR_FAILED(r) || count == 0) {
        mevr::log::Error("overlays: {} swapchain {}x{} failed (XrResult {})", what, width, height, static_cast<int>(r));
        out.Destroy();
        out.failed = true;
        return false;
    }
    for (auto const& image : images) {
        out.images.push_back(image.texture);
    }
    out.rtvs.resize(count);
    out.width = width;
    out.height = height;
    mevr::log::Info("overlays: {} swapchain {}x{}, {} images", what, width, height, count);
    return true;
}

// Acquire + wait; false (and nothing held) if either fails.
bool AcquireImage(QuadSwapchain& swapchain, std::uint32_t& index) {
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(swapchain.handle, &acquire, &index))) {
        return false;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(swapchain.handle, &wait)) || index >= swapchain.images.size()) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(swapchain.handle, &release);
        return false;
    }
    return true;
}

void ReleaseImage(QuadSwapchain& swapchain) {
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(swapchain.handle, &release);
}

// ---- [MENUVIS] readback probes (Present thread) ---------------------------------------------------
struct Probe {
    ComPtr<ID3D11Texture2D> staging;
    std::string tag;
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    int frames = 0;
};
std::vector<Probe> gProbes;

int AlphaOffset(DXGI_FORMAT f, int& stride) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        stride = 4;
        return 3;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        stride = 8;
        return 6;
    default:
        stride = 0;
        return -1;
    }
}

float AlphaAt(BYTE const* px, DXGI_FORMAT f) {
    if (f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R16G16B16A16_TYPELESS) {
        std::uint16_t h = 0;
        std::memcpy(&h, px, 2);
        std::uint32_t const exp = (h >> 10) & 0x1F;
        float const mant = static_cast<float>(h & 0x3FF) / 1024.0f;
        float const v = exp == 0 ? mant * std::ldexp(1.0f, -14) : (1.0f + mant) * std::ldexp(1.0f, static_cast<int>(exp) - 15);
        return (h & 0x8000) ? -v : v;
    }
    return static_cast<float>(*px) / 255.0f;
}

void PumpProbes(ID3D11DeviceContext* context) {
    for (auto it = gProbes.begin(); it != gProbes.end();) {
        Probe& probe = *it;
        if (++probe.frames < 3) {  // let the GPU finish the copy first
            ++it;
            continue;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT const hr = context->Map(probe.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING && probe.frames < 120) {
            ++it;
            continue;
        }
        if (FAILED(hr)) {
            LogBoth("[MENUVIS] " + probe.tag + ": readback failed hr=" + std::to_string(static_cast<long>(hr)));
            it = gProbes.erase(it);
            continue;
        }
        int stride = 0;
        int const alphaOffset = AlphaOffset(probe.format, stride);
        std::uint64_t visible = 0;
        double alphaSum = 0.0;
        std::uint64_t samples = 0;
        if (alphaOffset >= 0) {
            for (UINT y = 0; y < probe.height; y += 2) {
                BYTE const* row = static_cast<BYTE const*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
                for (UINT x = 0; x < probe.width; x += 2) {
                    float const a = AlphaAt(row + static_cast<size_t>(x) * stride + alphaOffset, probe.format);
                    alphaSum += a;
                    visible += a > 0.02f ? 1 : 0;
                    ++samples;
                }
            }
        }
        BYTE const* center = static_cast<BYTE const*>(mapped.pData) +
            static_cast<size_t>(probe.height / 2) * mapped.RowPitch + static_cast<size_t>(probe.width / 2) * (stride > 0 ? stride : 4);
        char line[320];
        std::snprintf(line, sizeof line,
            "[MENUVIS] %s %ux%u fmt=%d: %.1f%% of pixels visible (alpha > 0.02), mean alpha %.3f, centre bytes "
            "%02X %02X %02X %02X%s",
            probe.tag.c_str(), probe.width, probe.height, static_cast<int>(probe.format),
            samples > 0 ? 100.0 * static_cast<double>(visible) / static_cast<double>(samples) : 0.0,
            samples > 0 ? alphaSum / static_cast<double>(samples) : 0.0, center[0], center[1], center[2], center[3],
            alphaOffset < 0 ? " (format not decoded)" : "");
        context->Unmap(probe.staging.Get(), 0);
        LogBoth(line);
        it = gProbes.erase(it);
    }
}

// ---- Placement and layers (Present thread) ---------------------------------------------------------
overlay::Panel gPlacement;
std::atomic<bool> gRecentered{false};
std::atomic<bool> gReplaceRequested{false};
std::uint64_t gLastFrameMs = 0;
XrCompositionLayerQuad gPanelQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
XrCompositionLayerQuad gReticleQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
bool gLoggedPanel = false;
bool gLoggedReticle = false;
bool gLoggedFallback = false;
bool gProbedPanel = false;
int gReticleSource = -1;  // last logged: 0 none, 1 aim ray, 2 right controller
int gLastGameMode = -1;   // the last readable USFXGameModeManager mode
std::uint64_t gReticleSeenMs = 0;  // the reticle last had a ray
std::uint64_t gSightSeenMs = 0;    // an eye last looked through the gun's sight (the ring hides meanwhile)
std::uint64_t gNextPanelLogMs = 0;

// One blit of a window of the panel texture (and its tint) into a swapchain image, `factor` times
// smaller; pixels inside `mask` come out transparent. Returns the image written (released), or nullptr.
struct Window {
    UINT x = 0;
    UINT y = 0;
    UINT width = 0;
    UINT height = 0;
};
struct Mask {
    INT x0 = 0;
    INT y0 = 0;
    INT x1 = 0;  // x1 <= x0: none
    INT y1 = 0;
};

ID3D11Texture2D* Blit(ID3D11DeviceContext* context, ID3D11Device* device, QuadSwapchain& swapchain, Window const& window,
    UINT factor, Mask const& mask, float whiteScale, bool probe = false) {
    if (!EnsurePipeline(device) || gPanel.srv == nullptr || gPanel.tintSrv == nullptr) {
        return nullptr;
    }
    std::uint32_t index = 0;
    if (!AcquireImage(swapchain, index)) {
        return nullptr;
    }
    bool drew = false;
    ComPtr<ID3D11RenderTargetView>& rtv = swapchain.rtvs[index];
    if (rtv == nullptr) {
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = static_cast<DXGI_FORMAT>(kSwapchainFormat);
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(swapchain.images[index], &rd, rtv.GetAddressOf());
    }
    if (rtv != nullptr) {
        PanelParams params{{factor, gPanel.width, gPanel.height, 0},
            {SamplesLinear(gPanel.format) ? 1.0f : 0.0f, IsFloatFormat(gPanel.format) ? whiteScale : 1.0f, 0.0f, 0.0f},
            {static_cast<std::int32_t>(window.x), static_cast<std::int32_t>(window.y), 0, 0}, {mask.x0, mask.y0, mask.x1, mask.y1}};
        context->UpdateSubresource(gPipeline.params.Get(), 0, nullptr, &params, 0, 0);
        StateGuard guard(context);
        D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(swapchain.width), static_cast<float>(swapchain.height), 0.0f, 1.0f};
        float const blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        ID3D11RenderTargetView* target = rtv.Get();
        ID3D11ShaderResourceView* sources[2] = {gPanel.srv.Get(), gPanel.tintSrv.Get()};
        ID3D11Buffer* constants = gPipeline.params.Get();
        context->OMSetRenderTargets(1, &target, nullptr);
        context->OMSetBlendState(gPipeline.opaque.Get(), blendFactor, 0xFFFFFFFFu);
        context->OMSetDepthStencilState(gPipeline.noDepth.Get(), 0);
        context->RSSetState(gPipeline.raster.Get());
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        // The game's UI gets the eyes' colour correction (plugin/Colour), each panel its own dither pattern.
        bool const corrected = gPipeline.colourPs != nullptr &&
            mevr::host::colour::BeginPanel(context, &swapchain == &gPanelSwapchain ? 2u : 3u);
        context->VSSetShader(gPipeline.vs.Get(), nullptr, 0);
        context->PSSetShader(corrected ? gPipeline.colourPs.Get() : gPipeline.ps.Get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->PSSetShaderResources(0, 2, sources);
        context->PSSetConstantBuffers(0, 1, &constants);
        context->Draw(3, 0);
        if (corrected) {
            mevr::host::colour::EndPanel(context);
        }
        drew = true;
    }
    ID3D11Texture2D* const image = swapchain.images[index];
    if (drew && probe) {  // one look at what the headset gets: the panel image after the blit
        ProbeTextureAlpha(context, image, "HUD panel swapchain image (first frame with UI)");
        ProbeTextureAlpha(context, gPanel.texture.Get(), "HUD panel texture (the game's UI draws)");
    }
    mevr::host::testloop::OnImageWritten(swapchain.handle, image);  // snapshots
    ReleaseImage(swapchain);
    return drew ? image : nullptr;
}

// (Re)makes `swapchain` at width x height; false if it can't.
bool EnsureSwapchain(QuadSwapchain& swapchain, std::uint32_t width, std::uint32_t height, char const* what, char const* name) {
    if (swapchain.handle != XR_NULL_HANDLE && (swapchain.width != width || swapchain.height != height)) {
        swapchain.Destroy();
    }
    if (swapchain.handle == XR_NULL_HANDLE) {
        if (swapchain.failed || !CreateSwapchain(gSession, width, height, swapchain, what)) {
            return false;
        }
        mevr::host::testloop::NameSwapchain(swapchain.handle, name);
    }
    return true;
}

bool RenderPanel(ID3D11DeviceContext* context, ID3D11Device* device, float whiteScale, Mask const& mask) {
    UINT const factor = std::clamp<UINT>(static_cast<UINT>(std::lround(gPanel.width / 1400.0)), 2u, 4u);
    std::uint32_t const width = std::max<std::uint32_t>(gPanel.width / factor, 16);
    std::uint32_t const height = std::max<std::uint32_t>(gPanel.height / factor, 16);
    if (!EnsureSwapchain(gPanelSwapchain, width, height, "HUD panel", "hud")) {
        return false;
    }
    gPanelFactor = factor;
    bool const probe = !gProbedPanel && gPanelDrawsLastFrame.load(std::memory_order_relaxed) > 0;
    ID3D11Texture2D* const image =
        Blit(context, device, gPanelSwapchain, {0, 0, gPanel.width, gPanel.height}, factor, mask, whiteScale, probe);
    gProbedPanel = gProbedPanel || (probe && image != nullptr);
    return image != nullptr;
}

// ---- The game's wheels on a panel of their own (Present thread) -------------------------------------
namespace wheel = mevr::vr::squad_menu::wheel;
namespace crop = mevr::vr::wheel_crop;

constexpr std::uint32_t kGridColumns = 48;
constexpr std::uint32_t kGridRows = 50;
constexpr std::uint64_t kBeforeEveryMs = 1000;  // a HUD-only grid this often while on foot (until measured)
constexpr std::uint64_t kWheelGridAfterMs = 600; // the wheel's grid this long after it opened (past its grow animation)

QuadSwapchain gWheelSwapchain;
XrCompositionLayerQuad gWheelQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};

struct WheelState {
    int mode = -1;                // 2 the power wheel, 3 the weapon wheel, -1 none (as last shown)
    std::uint32_t serial = 0;     // the mod's opening it was placed for
    bool placed = false;
    XrPosef pose{};
    std::uint64_t openedMs = 0;
    bool gridTaken = false;       // this opening's wheel grid
    bool loggedThisOpening = false;
    bool loggedSettings = false;
    std::uint32_t lastPanelSize = 0;  // the mod's Panel size last seen (to log a mismatch once)
};
WheelState gWheel;
std::atomic<bool> gWheelRecentered{false};

// The measured part of a frame of this size (once per session and size).
struct Measurement {
    UINT width = 0;
    UINT height = 0;
    crop::Rect rect;
    bool Valid(UINT w, UINT h) const { return width == w && height == h && rect.Valid(); }
};
Measurement gMeasured;

// Grid readbacks in flight, and the last HUD-only grid.
struct GridShot {
    ComPtr<ID3D11Texture2D> staging;
    bool wheel = false;
    int frames = 0;
    UINT width = 0;
    UINT height = 0;
};
std::vector<GridShot> gGridShots;
ComPtr<ID3D11Texture2D> gGridTarget;
ComPtr<ID3D11RenderTargetView> gGridRtv;
crop::Grid gBeforeGrid;
UINT gBeforeWidth = 0;
UINT gBeforeHeight = 0;
std::uint64_t gNextBeforeMs = 0;
bool gGridFailed = false;

// Draws the coverage grid of the panel texture and copies it for a readback a few frames later.
void CaptureGrid(ID3D11DeviceContext* context, ID3D11Device* device, bool wheelShot) {
    if (gGridFailed || gGridShots.size() >= 4 || !EnsurePipeline(device) || gPanel.srv == nullptr || gPanel.tintSrv == nullptr) {
        return;
    }
    if (gGridTarget == nullptr) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = kGridColumns;
        td.Height = kGridRows;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(device->CreateTexture2D(&td, nullptr, gGridTarget.GetAddressOf())) ||
            FAILED(device->CreateRenderTargetView(gGridTarget.Get(), nullptr, gGridRtv.GetAddressOf()))) {
            mevr::log::Error("overlays: the wheel's coverage grid target failed; the wheel panel keeps its default crop");
            gGridFailed = true;
            return;
        }
    }
    GridShot shot;
    D3D11_TEXTURE2D_DESC sd{};
    gGridTarget->GetDesc(&sd);
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&sd, nullptr, shot.staging.GetAddressOf()))) {
        gGridFailed = true;
        return;
    }
    std::int32_t const cellW = static_cast<std::int32_t>((gPanel.width + kGridColumns - 1) / kGridColumns);
    std::int32_t const cellH = static_cast<std::int32_t>((gPanel.height + kGridRows - 1) / kGridRows);
    PanelParams params{{1, gPanel.width, gPanel.height, 0}, {0.0f, 1.0f, 0.0f, 0.0f}, {0, 0, cellW, cellH}, {0, 0, 0, 0}};
    context->UpdateSubresource(gPipeline.params.Get(), 0, nullptr, &params, 0, 0);
    {
        StateGuard guard(context);
        D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(kGridColumns), static_cast<float>(kGridRows), 0.0f, 1.0f};
        float const blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        ID3D11RenderTargetView* target = gGridRtv.Get();
        ID3D11ShaderResourceView* sources[2] = {gPanel.srv.Get(), gPanel.tintSrv.Get()};
        ID3D11Buffer* constants = gPipeline.params.Get();
        context->OMSetRenderTargets(1, &target, nullptr);
        context->OMSetBlendState(gPipeline.opaque.Get(), blendFactor, 0xFFFFFFFFu);
        context->OMSetDepthStencilState(gPipeline.noDepth.Get(), 0);
        context->RSSetState(gPipeline.raster.Get());
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(gPipeline.vs.Get(), nullptr, 0);
        context->PSSetShader(gPipeline.gridPs.Get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->PSSetShaderResources(0, 2, sources);
        context->PSSetConstantBuffers(0, 1, &constants);
        context->Draw(3, 0);
    }
    context->CopyResource(shot.staging.Get(), gGridTarget.Get());
    shot.wheel = wheelShot;
    shot.width = gPanel.width;
    shot.height = gPanel.height;
    gGridShots.push_back(std::move(shot));
}

// Reads finished grids: a HUD-only one is kept; a wheel one is compared with it (crop::MeasureCrop).
void PumpGrids(ID3D11DeviceContext* context) {
    for (auto it = gGridShots.begin(); it != gGridShots.end();) {
        GridShot& shot = *it;
        if (++shot.frames < 3) {
            ++it;
            continue;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT const hr = context->Map(shot.staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING && shot.frames < 120) {
            ++it;
            continue;
        }
        if (FAILED(hr)) {
            it = gGridShots.erase(it);
            continue;
        }
        crop::Grid grid;
        grid.columns = kGridColumns;
        grid.rows = kGridRows;
        grid.cells.resize(static_cast<size_t>(kGridColumns) * kGridRows);
        for (std::uint32_t y = 0; y < kGridRows; ++y) {
            std::memcpy(grid.cells.data() + static_cast<size_t>(y) * kGridColumns,
                static_cast<BYTE const*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, kGridColumns);
        }
        context->Unmap(shot.staging.Get(), 0);
        if (!shot.wheel) {
            gBeforeGrid = std::move(grid);
            gBeforeWidth = shot.width;
            gBeforeHeight = shot.height;
        } else if (gBeforeWidth != shot.width || gBeforeHeight != shot.height || !gBeforeGrid.Valid()) {
            LogBoth("[MEVR-WHEEL] the wheel's place wasn't measured: no HUD-only picture of this size yet (the default crop stays)");
        } else if (auto const measured = crop::MeasureCrop(gBeforeGrid, grid); !measured) {
            LogBoth("[MEVR-WHEEL] the wheel's place wasn't measured: too little appeared when it opened (the default crop stays)");
        } else if (measured->rect.width < 0.1f || measured->rect.height < 0.05f || (measured->rect.width > 0.95f && measured->rect.height > 0.95f)) {
            char line[256];
            std::snprintf(line, sizeof line,
                "[MEVR-WHEEL] measured wheel area (%.3f, %.3f) %.3f x %.3f of the frame looks wrong: ignored (the default crop stays)",
                measured->rect.x, measured->rect.y, measured->rect.width, measured->rect.height);
            LogBoth(line);
        } else {
            gMeasured = {shot.width, shot.height, measured->rect};
            crop::Rect const def = crop::DefaultCrop(shot.width, shot.height);
            char line[384];
            std::snprintf(line, sizeof line,
                "[MEVR-WHEEL] measured: the wheel is at (%.3f, %.3f), %.3f x %.3f of the %ux%u frame (%u of %u changed cells); the "
                "default guess was (%.3f, %.3f), %.3f x %.3f. Used from now on ([Squad] wheelCrop sets one by hand)",
                measured->rect.x, measured->rect.y, measured->rect.width, measured->rect.height, shot.width, shot.height,
                measured->cells, measured->changed, def.x, def.y, def.width, def.height);
            LogBoth(line);
        }
        it = gGridShots.erase(it);
    }
}

// The settings the mod published (HostServices::setWheelPanel), or the defaults.
wheel::Panel WheelSettings() {
    wheel::Panel latest = wheel::LatestPanel();
    if (latest.size == sizeof(wheel::Panel)) {
        return latest;
    }
    if (latest.size != 0 && latest.size != gWheel.lastPanelSize) {
        gWheel.lastPanelSize = latest.size;
        mevr::log::Warn("overlays: the mod's wheel panel settings are {} bytes, the host's {}: defaults used; rebuild both",
            latest.size, sizeof(wheel::Panel));
    }
    return wheel::Panel{};
}

// Where the wheel's panel goes when it opens: above a hand, facing the eyes, or in front of the head.
XrPosef PlaceWheel(int place, float distance, XrPosef const& head, char const*& from) {
    mevr::vr::controllers::State const controllers = mevr::vr::controllers::Latest();
    if (place == 0 || place == 1) {
        mevr::vr::controllers::Hand const& hand = place == 0 ? controllers.left : controllers.right;
        if (controllers.active && (hand.gripValid || hand.aimValid)) {
            from = place == 0 ? "the left hand" : "the right hand";
            return mevr::vr::squad_menu::PlaceAtHand(hand.gripValid ? hand.grip.position : hand.aim.position, head.position, distance);
        }
    }
    from = place == 2 ? "in front of the head" : "in front of the head (no hand)";
    XrQuaternionf const& q = head.orientation;
    XrVector3f const forward{-2.0f * (q.x * q.z + q.w * q.y), 0.0f, -(1.0f - 2.0f * (q.x * q.x + q.y * q.y))};
    float const length = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    XrVector3f const f = length > 1e-4f ? XrVector3f{forward.x / length, 0.0f, forward.z / length} : XrVector3f{0.0f, 0.0f, -1.0f};
    XrVector3f const spot{head.position.x + f.x * 0.45f, head.position.y - 0.35f, head.position.z + f.z * 0.45f};
    return mevr::vr::squad_menu::PlaceAtHand(spot, head.position, distance);
}

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
    if (gReticleSwapchain.failed || !CreateSwapchain(gSession, kReticleSize, kReticleSize, gReticleSwapchain, "reticle")) {
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
        gReticleSwapchain.failed = true;
        return false;
    }
    // The content never changes: fill every image once; the runtime keeps showing the last released.
    for (size_t i = 0; i < gReticleSwapchain.images.size(); ++i) {
        std::uint32_t index = 0;
        if (!AcquireImage(gReticleSwapchain, index)) {
            mevr::log::Error("overlays: reticle swapchain image acquire failed");
            gReticleSwapchain.Destroy();
            gReticleSwapchain.failed = true;
            return false;
        }
        context->CopyResource(gReticleSwapchain.images[index], source.Get());
        ReleaseImage(gReticleSwapchain);
    }
    // Snapshots draw it from these pixels: its images are never released again to be copied.
    mevr::host::testloop::NameSwapchain(gReticleSwapchain.handle, "reticle");
    mevr::host::testloop::SetStaticImage(gReticleSwapchain.handle, kReticleSize, kReticleSize, pixels);
    return true;
}

XrVector3f ToVec(float const v[3]) { return {v[0], v[1], v[2]}; }
XrQuaternionf ToQuat(float const q[4]) { return {q[0], q[1], q[2], q[3]}; }

} // namespace

bool HudPanelArmed() noexcept {
    return gArmed.load(std::memory_order_acquire);
}

ID3D11RenderTargetView* HudPanelTarget(ID3D11DeviceContext* context, unsigned width, unsigned height, int format) noexcept {
    DXGI_FORMAT const fmt = static_cast<DXGI_FORMAT>(format);
    if (gPanel.texture != nullptr && gPanel.width == width && gPanel.height == height && gPanel.format == fmt) {
        return gPanel.rtv.Get();
    }
    if (gPanel.failed && gPanel.width == width && gPanel.height == height && gPanel.format == fmt) {
        return nullptr;
    }
    if (gPanel.texture != nullptr && gPanelDraws.load(std::memory_order_relaxed) > 0) {
        return nullptr;  // a second kind of target in one frame: keep this frame's panel, that draw stays in the frame
    }
    gPanel = PanelTexture{};
    gPanel.width = width;
    gPanel.height = height;
    gPanel.format = fmt;
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    D3D11_TEXTURE2D_DESC tintDesc = td;
    tintDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // a 0..1 factor per channel
    if (device == nullptr || FAILED(device->CreateTexture2D(&td, nullptr, gPanel.texture.GetAddressOf())) ||
        FAILED(device->CreateRenderTargetView(gPanel.texture.Get(), nullptr, gPanel.rtv.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(gPanel.texture.Get(), nullptr, gPanel.srv.GetAddressOf())) ||
        FAILED(device->CreateTexture2D(&tintDesc, nullptr, gPanel.tint.GetAddressOf())) ||
        FAILED(device->CreateRenderTargetView(gPanel.tint.Get(), nullptr, gPanel.tintRtv.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(gPanel.tint.Get(), nullptr, gPanel.tintSrv.GetAddressOf()))) {
        mevr::log::Error("overlays: HUD panel texture {}x{} format {} failed; the HUD stays in the frame", width,
            height, format);
        PanelTexture failed;
        failed.width = width;
        failed.height = height;
        failed.format = fmt;
        failed.failed = true;
        gPanel = failed;
        return nullptr;
    }
    float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float const white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    context->ClearRenderTargetView(gPanel.rtv.Get(), clear);
    context->ClearRenderTargetView(gPanel.tintRtv.Get(), white);
    gPanelIsFloat.store(IsFloatFormat(fmt), std::memory_order_relaxed);
    LogBoth("[MEVR-HUD] HUD panel texture " + std::to_string(width) + "x" + std::to_string(height) + " fmt=" +
        std::to_string(format) + (IsFloatFormat(fmt) ? " (float scRGB: scaled to SDR white)" : "") +
        ": the game's UI draws now go to the body-locked panel instead of the eyes");
    return gPanel.rtv.Get();
}

ID3D11BlendState* HudPanelBlend(ID3D11DeviceContext* context, ID3D11BlendState* game) noexcept {
    if (game == nullptr) {
        return nullptr;
    }
    for (auto const& entry : gBlends) {
        if (entry.game == game) {
            return entry.ours.Get();
        }
    }
    D3D11_BLEND_DESC desc{};
    game->GetDesc(&desc);
    auto& rt = desc.RenderTarget[0];
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    if ((rt.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_ALL) != 0) {  // stencil-only mask draws stay colourless
        rt.RenderTargetWriteMask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
    }
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    BlendEntry entry;
    if (device == nullptr || FAILED(device->CreateBlendState(&desc, entry.ours.GetAddressOf()))) {
        return nullptr;
    }
    game->AddRef();
    entry.game = game;
    gBlends.push_back(std::move(entry));
    return gBlends.back().ours.Get();
}

void NoteHudPanelDraw() noexcept {
    gPanelDraws.fetch_add(1, std::memory_order_relaxed);
}

void HudPanelTintBlends(ID3D11DeviceContext* context, ID3D11BlendState* game, ID3D11BlendState** colour,
    ID3D11BlendState** tint) noexcept {
    *colour = nullptr;
    *tint = nullptr;
    if (game == nullptr) {
        return;
    }
    for (auto const& entry : gTintBlends) {
        if (entry.game == game) {
            *colour = entry.colour.Get();
            *tint = entry.tint.Get();
            return;
        }
    }
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    if (device == nullptr) {
        return;
    }
    D3D11_BLEND_DESC desc{};
    game->GetDesc(&desc);
    TintEntry entry;
    // The colour: the game's own blend into the panel's premultiplied colour; its alpha (the "over"
    // coverage) is left as it is.
    D3D11_BLEND_DESC colourDesc = desc;
    colourDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
    colourDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    colourDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    device->CreateBlendState(&colourDesc, entry.colour.GetAddressOf());
    // The tint: what the mode leaves of whatever is behind (dest *= factor of the source).
    D3D11_BLEND factor = D3D11_BLEND_ONE;
    if (TintFactor(desc.RenderTarget[0], factor)) {
        D3D11_BLEND_DESC tintDesc{};
        tintDesc.RenderTarget[0].BlendEnable = TRUE;
        tintDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
        tintDesc.RenderTarget[0].DestBlend = factor;
        tintDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        tintDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
        tintDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
        tintDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        tintDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN |
            D3D11_COLOR_WRITE_ENABLE_BLUE;
        device->CreateBlendState(&tintDesc, entry.tint.GetAddressOf());
    }
    char line[224];
    std::snprintf(line, sizeof line,
        "[MEVR-HUD] Scaleform blend mode src=%d dst=%d op=%d: colour to the HUD panel, %s", static_cast<int>(desc.RenderTarget[0].SrcBlend),
        static_cast<int>(desc.RenderTarget[0].DestBlend), static_cast<int>(desc.RenderTarget[0].BlendOp),
        entry.tint != nullptr ? "and its effect on what's behind as the panel's tint (its alpha)" : "the world behind unchanged (no tint)");
    LogBoth(line);
    game->AddRef();
    entry.game = game;
    gTintBlends.push_back(std::move(entry));
    *colour = gTintBlends.back().colour.Get();
    *tint = gTintBlends.back().tint.Get();
}

ID3D11RenderTargetView* HudPanelTintTarget() noexcept {
    return gPanel.tintRtv.Get();
}

ID3D11DepthStencilState* HudPanelNoWrites(ID3D11DeviceContext* context, ID3D11DepthStencilState* game) noexcept {
    if (game == nullptr) {
        return nullptr;  // the default state writes depth, but the UI's target has none bound for it to matter
    }
    for (auto const& entry : gNoWrites) {
        if (entry.game == game) {
            return entry.ours.Get();
        }
    }
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    D3D11_DEPTH_STENCIL_DESC desc{};
    game->GetDesc(&desc);
    desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    desc.StencilWriteMask = 0;
    DepthEntry entry;
    if (device == nullptr || FAILED(device->CreateDepthStencilState(&desc, entry.ours.GetAddressOf()))) {
        return nullptr;
    }
    game->AddRef();
    entry.game = game;
    gNoWrites.push_back(std::move(entry));
    return gNoWrites.back().ours.Get();
}

void NoteHudPanelTint() noexcept {
    gTintDraws.fetch_add(1, std::memory_order_relaxed);
}

bool HudPanelIsFloat() noexcept {
    return gPanelIsFloat.load(std::memory_order_relaxed);
}

void OnRecentered() noexcept {
    mevr::host::recenter_tilt::Committed();  // the orientation RecenterOrientation chose is the app space's now
    gRecentered.store(true, std::memory_order_release);
    gWheelRecentered.store(true, std::memory_order_release);  // an open wheel's panel is placed again
    RecenterWorldPanels();  // the world-locked flat panels too (plugin/WorldPanels.cpp)
    mevr::host::squad_panel::Recentered();  // and an open squad menu
    mevr::host::calibration::Recentered();  // and the guided calibration's layout
#if defined(MEVR_WITH_MAIN_MENU)
    mevr::host::main_menu::Recentered();  // and our open main menu
#endif
    mevr::host::halcyon::NoteRecentered();  // the holsters take the standing eye height there
}

void RequestHudPanelReplace() noexcept {
    gReplaceRequested.store(true, std::memory_order_release);
}

unsigned HudPanelDrawsLastFrame() noexcept {
    return gPanelDrawsLastFrame.load(std::memory_order_relaxed);
}

void ProbeTextureAlpha(ID3D11DeviceContext* context, ID3D11Texture2D* texture, char const* tag) noexcept {
    if (context == nullptr || texture == nullptr || gProbes.size() >= 8) {
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1) {
        return;
    }
    D3D11_TEXTURE2D_DESC sd = desc;
    sd.MipLevels = 1;
    sd.ArraySize = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    Probe probe;
    if (device == nullptr || FAILED(device->CreateTexture2D(&sd, nullptr, probe.staging.GetAddressOf()))) {
        LogBoth(std::string("[MENUVIS] ") + tag + ": no staging copy (fmt=" + std::to_string(static_cast<int>(desc.Format)) + ")");
        return;
    }
    context->CopySubresourceRegion(probe.staging.Get(), 0, 0, 0, 0, texture, 0, nullptr);
    probe.tag = tag;
    probe.width = desc.Width;
    probe.height = desc.Height;
    probe.format = desc.Format;
    gProbes.push_back(std::move(probe));
}

unsigned OverlayLayers(OverlayFrame const& frame, void const** layersOut, unsigned capacity) noexcept {
    mevr::host::perf::ScopedMarks const timed(mevr::vr::frame_probe::kOverlayBegin, mevr::vr::frame_probe::kOverlayEnd);
    ID3D11DeviceContext* const context = frame.context;
    if (context == nullptr || frame.session == nullptr) {
        gArmed.store(false, std::memory_order_release);
        return 0;
    }
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    PumpProbes(context);

    XrSession const session = static_cast<XrSession>(frame.session);
    if (session != gSession) {
        gPanelSwapchain.Forget();
        gReticleSwapchain.Forget();
        gWheelSwapchain.Forget();
        gSession = session;
    }
    auto const& cfg = MELEVR::Config::Get();
    std::uint64_t const now = GetTickCount64();
    float const dt = gLastFrameMs == 0 ? 0.0f : static_cast<float>(now - gLastFrameMs) / 1000.0f;
    gLastFrameMs = now;
    mevr::host::recenter_tilt::NoteHead(frame.headOrientation, frame.headValid);  // the tilt-correcting recenter's watch

    // The panel is for gameplay (modes 0-4: on foot, Mako, wheels, squad command). Conversations and
    // cutscenes in VR keep Halcyon's presentation: head tracking is parked there and their UI stays in
    // the eye images, with Halcyon's conversation layout. An unreadable mode (-1) keeps the last one.
    if (frame.gameMode >= 0) {
        gLastGameMode = frame.gameMode;
    }
    bool const gameplay = gLastGameMode >= 0 && gLastGameMode <= 4;

    // The game's own crosshair marks the screen centre (the head), not the gun: hidden while our
    // hand reticle stands in for it. The HUD movie keeps its authored layout on the panel (Halcyon's
    // head-locked shrink-and-centre would only make it small there).
    bool const handAim = ControllerAimActive();
    bool const panelUsable = !gPanelSwapchain.failed && !gPanel.failed && !(gPipeline.tried && !gPipeline.ready);
    bool const engaged = cfg.hudPanel && frame.sfr2 && panelUsable;
    MELEVR::Pchud::SetAuthoredLayout(engaged);

    unsigned count = 0;
    unsigned const draws = gPanelDraws.exchange(0, std::memory_order_relaxed);

    // ---- HUD panel ----
    overlay::PanelSettings settings;
    settings.distance = std::clamp(cfg.hudPanelDistanceM, 0.5f, 5.0f);
    settings.drop = std::clamp(cfg.hudPanelDropM, -1.0f, 1.5f);
    settings.followAfter = cfg.hudPanelFollowDeg > 0.0f ? std::clamp(cfg.hudPanelFollowDeg, 10.0f, 170.0f) * kDegToRad : 0.0f;
    XrQuaternionf const head = ToQuat(frame.headOrientation);
    if (gRecentered.exchange(false, std::memory_order_acq_rel)) {
        gPlacement.Reset();
        mevr::log::Info("overlays: recentered, HUD panel straight ahead");
    }
    if (frame.headValid && gReplaceRequested.exchange(false, std::memory_order_acq_rel)) {
        gPlacement.FaceHeading(head);
        mevr::log::Info("overlays: HUD panel re-placed at heading {:.0f} deg", gPlacement.Yaw() / kDegToRad);
    }
    bool const wasFollowing = gPlacement.Following();
    gPlacement.Update(head, frame.headValid, settings, dt);
    if (gPlacement.Following() && !wasFollowing) {
        mevr::log::Info("overlays: head turned past {:.0f} deg: HUD panel follows", cfg.hudPanelFollowDeg);
    }

    // ---- The game's wheels (power: mode 2, weapon: mode 3) on a panel of their own ----
    PumpGrids(context);
    wheel::Panel const wheelSettings = WheelSettings();
    if (!gWheel.loggedSettings && wheelSettings.size == sizeof(wheel::Panel)) {
        gWheel.loggedSettings = true;
        mevr::log::Info("overlays: wheel panel settings from the mod: {}, weapon wheel {}, power wheel {}, {:.2f} m wide, {:.2f} m from "
                        "the hand, crop {}", wheelSettings.enabled ? "on" : "off",
            wheelSettings.weaponPlace == 0 ? "at the left hand" : wheelSettings.weaponPlace == 1 ? "at the right hand" : "in front",
            wheelSettings.powerPlace == 0 ? "at the left hand" : wheelSettings.powerPlace == 1 ? "at the right hand" : "in front",
            wheelSettings.width, wheelSettings.distance,
            wheelSettings.crop[2] > 0.0f ? std::format("({:.3f}, {:.3f}) {:.3f} x {:.3f}", wheelSettings.crop[0], wheelSettings.crop[1],
                                               wheelSettings.crop[2], wheelSettings.crop[3])
                                         : std::string("measured (auto)"));
    }
    int const wheelMode = frame.gameMode == 2 || frame.gameMode == 3 ? frame.gameMode : -1;
    int const wheelPlace = wheelMode == 3 ? wheelSettings.weaponPlace : wheelSettings.powerPlace;  // -1: stays on the HUD panel
    bool const wheelWanted = wheelSettings.enabled && wheelPlace >= 0 && engaged && gameplay && frame.worldFrame && wheelMode >= 0 &&
        gPanel.texture != nullptr && gPanel.tintSrv != nullptr && !gWheelSwapchain.failed && device != nullptr;
    char const* cropFrom = "the default guess";
    crop::Rect cropRect;
    if (wheelSettings.crop[2] > 0.0f && wheelSettings.crop[3] > 0.0f) {
        cropRect = {wheelSettings.crop[0], wheelSettings.crop[1], wheelSettings.crop[2], wheelSettings.crop[3]};
        cropFrom = "[Squad] wheelCrop";
    } else if (gMeasured.Valid(gPanel.width, gPanel.height)) {
        cropRect = gMeasured.rect;
        cropFrom = "measured";
    } else {
        cropRect = crop::DefaultCrop(gPanel.width, gPanel.height);
    }
    crop::Pixels const cropPx = crop::ToPixels(cropRect, gPanel.width, gPanel.height);
    Mask hudMask;
    if (wheelWanted) {
        hudMask = {static_cast<INT>(cropPx.x), static_cast<INT>(cropPx.y), static_cast<INT>(cropPx.x + cropPx.width),
            static_cast<INT>(cropPx.y + cropPx.height)};
    }

    bool panelShown = false;
    if (frame.worldFrame && engaged && gameplay && gPanel.texture != nullptr && count < capacity && device != nullptr) {
        gPanelDrawsLastFrame.store(draws, std::memory_order_relaxed);
        if (RenderPanel(context, device.Get(), frame.floatWhiteScale, hudMask)) {
            float const width = std::clamp(cfg.hudPanelWidthM, 0.3f, 4.0f);
            gPanelQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gPanelQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gPanelQuad.space = static_cast<XrSpace>(frame.appSpace);
            gPanelQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gPanelQuad.subImage.swapchain = gPanelSwapchain.handle;
            gPanelQuad.subImage.imageRect = {{0, 0},
                {static_cast<int32_t>(gPanelSwapchain.width), static_cast<int32_t>(gPanelSwapchain.height)}};
            gPanelQuad.pose = gPlacement.Pose(settings);
            gPanelQuad.size = {width, width * static_cast<float>(gPanel.height) / static_cast<float>(gPanel.width)};
            layersOut[count++] = &gPanelQuad;
            panelShown = true;
            if (!gLoggedPanel || now >= gNextPanelLogMs) {
                char line[288];
                std::snprintf(line, sizeof line,
                    "[MEVR-HUD] panel layer: %u UI draws this frame, %ux%u (1/%u of %ux%u), %.2f x %.2f m at "
                    "(%.2f, %.2f, %.2f) m, heading %.0f deg",
                    draws, gPanelSwapchain.width, gPanelSwapchain.height, gPanelFactor, gPanel.width, gPanel.height,
                    gPanelQuad.size.width, gPanelQuad.size.height, gPanelQuad.pose.position.x,
                    gPanelQuad.pose.position.y, gPanelQuad.pose.position.z, gPlacement.Yaw() / kDegToRad);
                LogBoth(line);
                gLoggedPanel = true;
                gNextPanelLogMs = now + 30000;
            }
        }
    }
    // A HUD-only picture of where the UI is, now and then, until the wheel's place has been measured.
    bool const autoCrop = !(wheelSettings.crop[2] > 0.0f && wheelSettings.crop[3] > 0.0f);
    if (panelShown && wheelMode < 0 && frame.gameMode == 0 && autoCrop && !gMeasured.Valid(gPanel.width, gPanel.height) &&
        now >= gNextBeforeMs) {
        gNextBeforeMs = now + kBeforeEveryMs;
        CaptureGrid(context, device.Get(), false);
    }
    // The wheel's panel: placed when a wheel opens (and when the mod opens it again, or on recenter).
    bool const wheelRecentered = gWheelRecentered.exchange(false, std::memory_order_acq_rel);
    XrCompositionLayerQuad const* wheelLayer = nullptr;
    if (wheelWanted && panelShown) {
        bool const fresh = wheelMode != gWheel.mode;
        bool const reopened = wheelMode == 3 && wheelSettings.modOpen && wheelSettings.serial != gWheel.serial;
        if (fresh) {
            gWheel.openedMs = now;
            gWheel.gridTaken = false;
            gWheel.loggedThisOpening = false;
        }
        if (fresh || reopened || wheelRecentered || !gWheel.placed) {
            XrPosef headPose{};
            headPose.orientation = head;
            headPose.position = ToVec(frame.headPosition);
            char const* from = "";
            gWheel.pose = PlaceWheel(wheelPlace, std::clamp(wheelSettings.distance, 0.1f, 0.8f), headPose, from);
            gWheel.placed = true;
            char line[320];
            std::snprintf(line, sizeof line,
                "[MEVR-WHEEL] the %s wheel on its own panel%s: %.2f m from %s, at (%.2f, %.2f, %.2f) m, %.2f m wide; the UI's part "
                "(%u, %u) %ux%u of %ux%u (%s)", wheelMode == 3 ? "weapon" : "power", wheelRecentered ? " again (recentered)" : "",
                wheelSettings.distance, from, gWheel.pose.position.x, gWheel.pose.position.y, gWheel.pose.position.z,
                wheelSettings.width, cropPx.x, cropPx.y, cropPx.width, cropPx.height, gPanel.width, gPanel.height, cropFrom);
            LogBoth(line);
        }
        gWheel.mode = wheelMode;
        gWheel.serial = wheelSettings.serial;
        UINT const factor = std::clamp<UINT>(static_cast<UINT>(std::lround(cropPx.width / 1024.0)), 1u, 4u);
        std::uint32_t const width = std::max<std::uint32_t>(cropPx.width / factor, 16);
        std::uint32_t const height = std::max<std::uint32_t>(cropPx.height / factor, 16);
        if (EnsureSwapchain(gWheelSwapchain, width, height, "wheel panel", "wheel") &&
            Blit(context, device.Get(), gWheelSwapchain, {cropPx.x, cropPx.y, cropPx.width, cropPx.height}, factor, Mask{},
                frame.floatWhiteScale) != nullptr) {
            float const quadWidth = std::clamp(wheelSettings.width, 0.15f, 1.5f);
            gWheelQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gWheelQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gWheelQuad.space = static_cast<XrSpace>(frame.appSpace);
            gWheelQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gWheelQuad.subImage.swapchain = gWheelSwapchain.handle;
            gWheelQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(width), static_cast<int32_t>(height)}};
            gWheelQuad.pose = gWheel.pose;
            gWheelQuad.size = {quadWidth, quadWidth * static_cast<float>(cropPx.height) / static_cast<float>(cropPx.width)};
            wheelLayer = &gWheelQuad;
        }
        // Where the wheel really is: once per opening, after its grow animation, until measured.
        if (autoCrop && !gWheel.gridTaken && !gMeasured.Valid(gPanel.width, gPanel.height) && now - gWheel.openedMs >= kWheelGridAfterMs) {
            gWheel.gridTaken = true;
            CaptureGrid(context, device.Get(), true);
        }
    } else if (gWheel.mode >= 0 && wheelMode < 0) {
        LogBoth(std::string("[MEVR-WHEEL] the ") + (gWheel.mode == 3 ? "weapon" : "power") + " wheel closed; its panel is gone");
        gWheel.mode = -1;
        gWheel.placed = false;
    }

    std::uint32_t const tintDraws = gTintDraws.exchange(0, std::memory_order_relaxed);
    if (tintDraws > 0 && !gLoggedTint) {
        gLoggedTint = true;
        mevr::log::Info("overlays: first frame with Scaleform blend-mode draws in the panel's tint ({} draws)", tintDraws);
    }
    gTintDirty = gTintDirty || tintDraws > 0;
    if (gPanel.rtv != nullptr && (draws > 0 || gPanelNeedsClear)) {
        float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->ClearRenderTargetView(gPanel.rtv.Get(), clear);
        if (gTintDirty && gPanel.tintRtv != nullptr) {
            float const white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            context->ClearRenderTargetView(gPanel.tintRtv.Get(), white);
            gTintDirty = false;
        }
        gPanelNeedsClear = false;
    }
    // Next frame's UI goes to the panel only if this one showed it (or nothing went there yet, so
    // the texture can be made), and the panel works.
    bool const armNext = engaged && gameplay && frame.worldFrame && (panelShown || gPanel.texture == nullptr);
    if (armNext != gArmed.load(std::memory_order_relaxed)) {
        mevr::log::Info("overlays: game UI goes to {}", armNext ? "the body-locked HUD panel" : "the eye images");
        gPanelNeedsClear = true;
    }
    gArmed.store(armNext, std::memory_order_release);

    // ---- The gun's sight (plugin/SightLayers): a red dot or holo reticle, or a scope's picture ----
    // Worked out before the ring reticle, which hides while an eye looks through the sight (held 0.3 s so
    // the edge of the window doesn't make it blink); its quads go on top of the ring's.
    mevr::host::sight_layers::Frame sightFrame;
    sightFrame.session = session;
    sightFrame.appSpace = static_cast<XrSpace>(frame.appSpace);
    sightFrame.context = context;
    sightFrame.device = device.Get();
    sightFrame.worldFrame = frame.worldFrame;
    sightFrame.menuOpen = frame.menuOpen;
    sightFrame.onFoot = gLastGameMode == 0;
    sightFrame.eyesValid = frame.headValid;
    sightFrame.projectionValid = frame.projectionValid;
    for (int e = 0; e < 2; ++e) {
        sightFrame.eyes[e].orientation = ToQuat(frame.eyeOrientation[e]);
        sightFrame.eyes[e].position = ToVec(frame.eyePosition[e]);
        sightFrame.projectionSwapchain[e] = static_cast<XrSwapchain>(frame.projectionSwapchain[e]);
        sightFrame.projectionRect[e] = {{frame.projectionRect[e][0], frame.projectionRect[e][1]},
            {frame.projectionRect[e][2], frame.projectionRect[e][3]}};
        sightFrame.projectionPose[e].orientation = ToQuat(frame.projectionOrientation[e]);
        sightFrame.projectionPose[e].position = ToVec(frame.projectionPosition[e]);
        sightFrame.projectionFov[e] = {frame.projectionFov[e][0], frame.projectionFov[e][1], frame.projectionFov[e][2],
            frame.projectionFov[e][3]};
        sightFrame.imageShiftTan[e] = frame.imageShiftTan[e];
    }
    // How Stereo 2 drew the eyes (research/21 section 9): its half separation at the view's scale (the host view's
    // units per meter: the same scale the mod maps the hands with).
    {
        float const unitsPerMeter = mevr::vr::host_view::Latest().unitsPerMeter;
        sightFrame.stereoValid = frame.stereoMapValid && unitsPerMeter > 1.0f;
        sightFrame.stereoHalfEyeM = sightFrame.stereoValid ? frame.stereoHalfEyeUU / unitsPerMeter : 0.0f;
    }
    mevr::host::sight_layers::Output const sight = mevr::host::sight_layers::Layers(sightFrame);
    if (sight.looking && sight.replaceRing) {
        gSightSeenMs = now;
    }
    bool const sightHidesRing = gSightSeenMs != 0 && now - gSightSeenMs < 300;

    // ---- Hand reticle ----
    // Where the weapon points: the mod's aim ray (muzzle and barrel) once it publishes one, else the
    // right controller's aim pose. While it has a ray, the game's screen-centre crosshair is hidden
    // (held half a second so a flickering ray doesn't make it blink).
    bool const reticleMode = cfg.handReticle && handAim && gLastGameMode == 0;  // on foot only (not the Mako)
    XrVector3f origin{};
    XrVector3f direction{};
    int source = 0;
    if (reticleMode) {
        mevr::vr::aim_ray::Ray const ray = mevr::vr::aim_ray::Latest();
        bool const producer = ray.sequence != 0 && now - ray.timeMs < 1000;
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
    if (source != 0) {
        gReticleSeenMs = now;
    }
    MELEVR::Pchud::SetCrosshairHidden(reticleMode && gReticleSeenMs != 0 && now - gReticleSeenMs < 500);
    if (source != 0 && !sightHidesRing && frame.worldFrame && !frame.menuOpen && frame.headValid && count < capacity &&
        device != nullptr) {
        overlay::Billboard reticle;
        float const distance = std::clamp(cfg.handReticleDistanceM, 1.0f, 100.0f);
        float const size = std::clamp(cfg.handReticleSizeDeg, 0.2f, 10.0f) * kDegToRad;
        if (overlay::PlaceOnRay(origin, direction, distance, ToVec(frame.headPosition), size, reticle) &&
            EnsureReticle(context, device.Get())) {
            gReticleQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gReticleQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gReticleQuad.space = static_cast<XrSpace>(frame.appSpace);
            gReticleQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gReticleQuad.subImage.swapchain = gReticleSwapchain.handle;
            gReticleQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(kReticleSize), static_cast<int32_t>(kReticleSize)}};
            gReticleQuad.pose = reticle.pose;
            gReticleQuad.size = {reticle.size, reticle.size};
            layersOut[count++] = &gReticleQuad;
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
        }
    }

    // The sight's quads, on top of the ring.
    for (unsigned i = 0; i < sight.count && count < capacity; ++i) {
        layersOut[count++] = sight.layers[i];
    }

    // ---- The squad weapon menu, at the left hand (plugin/SquadPanel): last, on top of ours ----
    if (count < capacity) {
        mevr::host::squad_panel::Frame squad;
        squad.session = session;
        squad.appSpace = static_cast<XrSpace>(frame.appSpace);
        squad.context = context;
        squad.device = device.Get();
        squad.worldFrame = frame.worldFrame;
        squad.menuOpen = frame.menuOpen;
        squad.headValid = frame.headValid;
        squad.head.orientation = ToQuat(frame.headOrientation);
        squad.head.position = ToVec(frame.headPosition);
        if (XrCompositionLayerQuad const* layer = mevr::host::squad_panel::Layer(squad)) {
            layersOut[count++] = layer;
        }
    }
    // ---- The game's wheel at the hand: last, on top ----
    if (wheelLayer != nullptr && count < capacity) {
        layersOut[count++] = wheelLayer;
        if (!gWheel.loggedThisOpening) {
            gWheel.loggedThisOpening = true;
            mevr::log::Info("overlays: wheel panel submitted ({}x{}, {:.2f} x {:.2f} m)", gWheelSwapchain.width, gWheelSwapchain.height,
                gWheelQuad.size.width, gWheelQuad.size.height);
        }
    }

    // ---- The guided calibration (plugin/Calibration): while it runs, its quads are the only ones of ours (the
    // first-run prompt goes on top of the rest) ----
    {
        mevr::host::calibration::Frame calibration;
        calibration.session = session;
        calibration.appSpace = static_cast<XrSpace>(frame.appSpace);
        calibration.context = context;
        calibration.device = device.Get();
        calibration.worldFrame = frame.worldFrame;
        calibration.menuOpen = frame.menuOpen;
        calibration.gameMode = gLastGameMode;
        calibration.headValid = frame.headValid;
        calibration.head.orientation = head;
        calibration.head.position = ToVec(frame.headPosition);
        for (int e = 0; e < 2; ++e) {
            calibration.eyes[e] = ToVec(frame.eyePosition[e]);
        }
        XrCompositionLayerQuad const* calibrationLayers[6] = {};
        bool const running = mevr::host::calibration::Running();
        unsigned const room = std::min<unsigned>(running ? capacity : capacity - count, 6u);
        unsigned const added = mevr::host::calibration::Layers(calibration, calibrationLayers, room);
        if (mevr::host::calibration::Active()) {
            count = 0;
        }
        for (unsigned i = 0; i < added && count < capacity; ++i) {
            layersOut[count++] = calibrationLayers[i];
        }
    }
#if defined(MEVR_WITH_MAIN_MENU)
    // ---- Our main menu (plugin/MainMenu): while it's open, its quad is the only one of ours (SteamVR may sort quads by
    // distance, and nothing of the game's is for now). Closed, it returns at once ----
    {
        mevr::host::main_menu::Frame menu;
        menu.session = session;
        menu.appSpace = static_cast<XrSpace>(frame.appSpace);
        menu.context = context;
        menu.device = device.Get();
        menu.worldFrame = frame.worldFrame;
        menu.headValid = frame.headValid;
        menu.head.orientation = head;
        menu.head.position = ToVec(frame.headPosition);
        menu.projectionValid = frame.projectionValid;
        menu.projectionSwapchain = static_cast<XrSwapchain>(frame.projectionSwapchain[0]);
        menu.projectionRect = {{frame.projectionRect[0][0], frame.projectionRect[0][1]}, {frame.projectionRect[0][2], frame.projectionRect[0][3]}};
        menu.projectionPose.orientation = ToQuat(frame.projectionOrientation[0]);
        menu.projectionPose.position = ToVec(frame.projectionPosition[0]);
        menu.projectionFov = {frame.projectionFov[0][0], frame.projectionFov[0][1], frame.projectionFov[0][2], frame.projectionFov[0][3]};
        if (XrCompositionLayerQuad const* layer = mevr::host::main_menu::Layer(menu); layer != nullptr && capacity > 0) {
            count = 0;
            layersOut[count++] = layer;
        }
    }
#endif
    return count;
}

} // namespace MELEVR::MevrBridge
