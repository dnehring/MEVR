// SPDX-License-Identifier: MIT
// The body-locked HUD panel's core (plugin/HudPanel.hpp), moved out of plugin/HalcyonOverlays.cpp: the panel texture
// and the UI draws' states (render thread), the blit pipeline, the panel's swapchain and placement (Present thread).

#include "plugin/HudPanel.hpp"

#include "core/Log.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/TestLoop.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace mevr::host::hud_panel {
namespace {

using Microsoft::WRL::ComPtr;
namespace overlay = mevr::vr::overlay;

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;

// Logs a line to our log and to the renderer's (one-shots that belong next to its submit lines).
void LogBoth(std::string const& line) {
    mevr::log::Info("{}", line);
    mevr::host::renderer::LogLine(line);
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
std::atomic<unsigned> gPanelTextDraws{0};
std::atomic<unsigned> gPanelEarlyDraws{0};
std::atomic<unsigned> gPanelEarlyTextDraws{0};
std::atomic<unsigned> gPanelDrawsLastFrame{0};
std::atomic<unsigned> gTintDraws{0};
std::atomic<bool> gPanelIsFloat{false};
std::atomic<unsigned> gEarlyLeft{0};        // Redirect: UI draws before the renderer's left-eye capture
bool gEarlyThisFrame = false;               // Redirect (render thread = Present thread on LE2): this frame's batches
bool gLateThisFrame = false;
std::atomic<unsigned> gNotOver{0};          // Redirect: UI draws neither "over" nor a blend mode (left to the renderer)
bool gPanelNeedsClear = false;
bool gClearBeforeNextUi = false; // clear immediately before the next game UI draw, never after its copy
unsigned gClearsBeforeUi = 0;
bool gTintDirty = false;  // Present thread: the tint texture has draws in it (cleared to white with the panel)
bool gLoggedTint = false;

void ClearPanel(ID3D11DeviceContext* context) {
    float const clear[4] = {0, 0, 0, 0};
    float const white[4] = {1, 1, 1, 1};
    context->ClearRenderTargetView(gPanel.rtv.Get(), clear);
    if (gPanel.tintRtv != nullptr) context->ClearRenderTargetView(gPanel.tintRtv.Get(), white);
    gTintDirty = false;
    gClearBeforeNextUi = false;
    ++gClearsBeforeUi;
}

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
// clip keeping the hue, like the eye blit. Pixels inside any of the three mask rectangles come out transparent (the
// HUD panel while a wheel, the interaction prompt or a HUD piece has a quad of its own).
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
Texture2D<float4> figure : register(t2);
SamplerState figureSampler : register(s0);
cbuffer Params : register(b0) { uint4 info; float4 params; int4 window; int4 masks[3]; float4 figureRect; float4 figureParams; };
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
    [unroll] for (int m = 0; m < 3; ++m) {
        int4 mask = masks[m];
        if (mask.z > mask.x && base.x >= mask.x && base.x < mask.z && base.y >= mask.y && base.y < mask.w)
            return float4(0, 0, 0, 0);
    }
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
    float3 c = 0;
    if (a < 1.0 / 1024.0) {
        a = 0;
    } else {
        c = max(s.rgb, 0.0) / a;
        if (params.x > 0.5) {
            c *= params.y;
            c /= max(1.0, max(c.r, max(c.g, c.b)));
        } else {
            c = saturate(c);
            c = (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
        }
        if (params.z > 0.0) {
            // A game screen standing in the room (research/53): its dark parts at params.z's opacity, the bright ones solid.
            a *= lerp(params.z, 1.0, saturate(max(c.r, max(c.g, c.b)) * 4.0));
        }
    }
    // The UI world's picture behind the game's UI over the 16:9 stage (research/57 section 7: the equipment and squad screens'
    // squad member): figureRect its origin and inverse size in source pixels, figureParams its opacity and decode gamma.
    float4 under = 0;
    if (figureParams.x > 0.0) {
        float2 uv = (float2(base) + 0.5 * float(f) - figureRect.xy) * figureRect.zw;
        if (all(uv >= 0.0) && all(uv <= 1.0)) {
            float3 fc = pow(max(figure.SampleLevel(figureSampler, uv, 0).rgb, 0.0), figureParams.y);
            under = float4(fc * figureParams.x, figureParams.x);
        }
    }
    float aOut = a + under.a * (1.0 - a);
    if (aOut < 1.0 / 1024.0) return float4(0, 0, 0, 0);
    c = (c * a + under.rgb * (1.0 - a)) / aOut;
    c = MEVR_PANEL_OUT(c, i.pos);
    return float4(c * aOut, aOut);
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
    float params[4];        // linear source (1/0), white scale, the world UI's backdrop (0: none), 0
    std::int32_t window[4]; // blit: the window's origin (source pixels), 0, 0; grid: 0, 0, cell width, cell height
    std::int32_t masks[3][4];  // each x0, y0, x1, y1 (source pixels); x1 <= x0: none
    float figureRect[4];       // the UI world's picture (research/57 section 7): the stage's origin (source pixels), 1/width, 1/height
    float figureParams[4];     // its opacity (0: none), decode gamma, 0, 0
};
static_assert(sizeof(PanelParams) == 128);

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
    ComPtr<ID3D11SamplerState> linearClamp;  // the UI world's picture (research/57 section 7)
    bool tried = false;
    bool ready = false;
};
Pipeline gPipeline;
ColourHooks gColour;

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
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(device->CreateSamplerState(&sd, gPipeline.linearClamp.GetAddressOf()));
    gPipeline.ready = ok;
    if (!ok) {
        mevr::log::Error("overlays: HUD panel pipeline creation failed");
    }
    if (ok && gColour.prelude != nullptr) {
        std::string const corrected = std::string(gColour.prelude) + kPanelShader;
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
        c_->PSGetShaderResources(0, 3, srv_);
        c_->PSGetSamplers(0, 1, &sampler_);
        c_->PSGetConstantBuffers(0, 1, &cb_);
    }
    ~StateGuard() {
        ID3D11ShaderResourceView* none[3] = {nullptr, nullptr, nullptr};
        c_->PSSetShaderResources(0, 3, none);
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
        c_->PSSetShaderResources(0, 3, srv_);
        c_->PSSetSamplers(0, 1, &sampler_);
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
        Release(srv_[2]);
        Release(sampler_);
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
    ID3D11ShaderResourceView* srv_[3] = {};
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11Buffer* cb_ = nullptr;
};

// ---- The panel's own swapchain and placement (Present thread) ------------------------------------
QuadSwapchain gPanelSwapchain;
UINT gPanelFactor = 2;
bool gProbedPanel = false;
overlay::Panel gPlacement;
std::atomic<bool> gRecentered{false};
std::atomic<bool> gReplaceRequested{false};

// Layer (LE2's plain panel): its quad and log state.
XrCompositionLayerQuad gQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
XrSession gLayerSession = XR_NULL_HANDLE;
std::uint64_t gLastFrameMs = 0;
bool gLoggedPanel = false;
std::uint64_t gNextPanelLogMs = 0;

} // namespace

// ---- Render thread ----------------------------------------------------------------------------------------------------

bool Armed() noexcept {
    return gArmed.load(std::memory_order_acquire);
}

ID3D11RenderTargetView* Target(ID3D11DeviceContext* context, unsigned width, unsigned height, int format) noexcept {
    DXGI_FORMAT const fmt = static_cast<DXGI_FORMAT>(format);
    if (gPanel.texture != nullptr && gPanel.width == width && gPanel.height == height && gPanel.format == fmt) {
        if (gClearBeforeNextUi) ClearPanel(context);
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
    gClearBeforeNextUi = false;
    gPanelIsFloat.store(IsFloatFormat(fmt), std::memory_order_relaxed);
    LogBoth("[MEVR-HUD] HUD panel texture " + std::to_string(width) + "x" + std::to_string(height) + " fmt=" +
        std::to_string(format) + (IsFloatFormat(fmt) ? " (float scRGB: scaled to SDR white)" : "") +
        ": the game's UI draws now go to the body-locked panel instead of the eyes");
    return gPanel.rtv.Get();
}

ID3D11BlendState* Blend(ID3D11DeviceContext* context, ID3D11BlendState* game) noexcept {
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

void NoteDraw(bool beforeEyeCopy, bool textBatch) noexcept {
    gPanelDraws.fetch_add(1, std::memory_order_relaxed);
    if (textBatch) gPanelTextDraws.fetch_add(1, std::memory_order_relaxed);
    if (beforeEyeCopy) {
        gPanelEarlyDraws.fetch_add(1, std::memory_order_relaxed);
        if (textBatch) gPanelEarlyTextDraws.fetch_add(1, std::memory_order_relaxed);
    }
}

void TintBlends(ID3D11DeviceContext* context, ID3D11BlendState* game, ID3D11BlendState** colour,
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

ID3D11RenderTargetView* TintTarget() noexcept {
    return gPanel.tintRtv.Get();
}

ID3D11DepthStencilState* NoWrites(ID3D11DeviceContext* context, ID3D11DepthStencilState* game) noexcept {
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

void NoteTint() noexcept {
    gTintDraws.fetch_add(1, std::memory_order_relaxed);
}

bool IsFloat() noexcept {
    return gPanelIsFloat.load(std::memory_order_relaxed);
}

bool Redirect(ID3D11DeviceContext* context, void (*draw)(void*), void* user, bool textBatch, bool afterLeftEye) noexcept {
    if (context == nullptr || draw == nullptr || !Armed()) {
        return false;
    }
    // ME2's SFR draws the UI twice a frame, once in each of its two Draws (research/le2-renderer-integration.md "Step 4"):
    // the batch before its left-eye capture ends up in the right eye (the backbuffer) and never in the left, the batch
    // after it is the one Halcyon mirrors into both. Both go to the panel; the later one replaces the earlier one (the
    // panel is cleared at the first draw after the capture), so a frame whose capture never came still has its HUD.
    if (!afterLeftEye) {
        static unsigned early = 0;  // render thread only
        if (early++ == 0) {
            LogBoth("[MEVR-HUD] a UI draw before the renderer captured its left eye: to the panel until the frame's later "
                    "batch replaces it (counted in the status line)");
        }
        gEarlyLeft.fetch_add(1, std::memory_order_relaxed);
        gEarlyThisFrame = true;
    } else if (gEarlyThisFrame && !gLateThisFrame) {
        gLateThisFrame = true;
        if (gPanel.rtv != nullptr) {
            ClearPanel(context);
            --gClearsBeforeUi;  // not the galaxy's clear before the next UI (LE1's TakeClears)
        }
    }
    ID3D11RenderTargetView* rtv = nullptr;
    context->OMGetRenderTargets(1, &rtv, nullptr);
    if (rtv == nullptr) {
        return false;
    }
    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rtv->GetDesc(&rd);
    D3D11_TEXTURE2D_DESC td{};
    {
        ID3D11Resource* res = nullptr;
        rtv->GetResource(&res);
        if (res != nullptr) {
            ID3D11Texture2D* tex = nullptr;
            if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex))) && tex != nullptr) {
                tex->GetDesc(&td);
                tex->Release();
            }
            res->Release();
        }
    }
    rtv->Release();
    if (td.Width == 0 || td.Height == 0 || td.SampleDesc.Count != 1 || rd.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D) {
        return false;
    }
    ID3D11BlendState* gameBlend = nullptr;
    float blendFactor[4] = {};
    UINT sampleMask = 0;
    context->OMGetBlendState(&gameBlend, blendFactor, &sampleMask);
    D3D11_BLEND_DESC bd{};
    if (gameBlend != nullptr) gameBlend->GetDesc(&bd);
    // As LE1's draw seam (src/halcyon/le1/d3d_capture.cpp MevrHudPanelDraw): "over" draws and Scaleform's blend modes.
    bool const over = gameBlend != nullptr && bd.RenderTarget[0].BlendEnable != FALSE &&
        bd.RenderTarget[0].DestBlend == D3D11_BLEND_INV_SRC_ALPHA;
    bool const tint = !over && gameBlend != nullptr && bd.RenderTarget[0].BlendEnable != FALSE &&
        bd.RenderTarget[0].DestBlendAlpha == D3D11_BLEND_DEST_ALPHA;
    {   // one line per distinct blend (render thread only): what the renderer's UI draws are and where each kind went
        auto const& rt = bd.RenderTarget[0];
        std::uint32_t const key = 1u | (static_cast<std::uint32_t>(rt.BlendEnable != FALSE) << 1) |
            (static_cast<std::uint32_t>(rt.SrcBlend) << 2) | (static_cast<std::uint32_t>(rt.DestBlend) << 7) |
            (static_cast<std::uint32_t>(rt.SrcBlendAlpha) << 12) | (static_cast<std::uint32_t>(rt.DestBlendAlpha) << 17) |
            (static_cast<std::uint32_t>(rt.BlendOp) << 22) | (static_cast<std::uint32_t>(rt.RenderTargetWriteMask & 15u) << 25);
        static std::uint32_t seen[32] = {};
        static int seenCount = 0;
        bool known = false;
        for (int i = 0; i < seenCount && !known; ++i) {
            known = seen[i] == key;
        }
        if (!known && seenCount < 32) {
            seen[seenCount++] = key;
            char line[256];
            std::snprintf(line, sizeof line,
                "[MEVR-HUD] UI draw %s: blend=%d src=%d dst=%d op=%d srcA=%d dstA=%d mask=%X, target %ux%u fmt=%d",
                over ? "to the HUD panel" : tint ? "to the HUD panel (a Scaleform blend mode, as a tint)"
                                                 : "left to the renderer (not 'over' blended)",
                rt.BlendEnable ? 1 : 0, static_cast<int>(rt.SrcBlend), static_cast<int>(rt.DestBlend), static_cast<int>(rt.BlendOp),
                static_cast<int>(rt.SrcBlendAlpha), static_cast<int>(rt.DestBlendAlpha), static_cast<unsigned>(rt.RenderTargetWriteMask),
                td.Width, td.Height, static_cast<int>(rd.Format));
            LogBoth(line);
        }
    }
    ID3D11RenderTargetView* panelRtv = (over || tint) ? Target(context, td.Width, td.Height, static_cast<int>(rd.Format)) : nullptr;
    if (panelRtv == nullptr) {
        if (!over && !tint) gNotOver.fetch_add(1, std::memory_order_relaxed);
        if (gameBlend != nullptr) gameBlend->Release();
        return false;
    }
    ID3D11BlendState* panelBlend = nullptr;
    ID3D11BlendState* tintBlend = nullptr;
    if (over) {
        panelBlend = Blend(context, gameBlend);
    } else {
        TintBlends(context, gameBlend, &panelBlend, &tintBlend);
    }
    ID3D11RenderTargetView* savedRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* savedDsv = nullptr;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRtvs, &savedDsv);
    context->OMSetRenderTargets(1, &panelRtv, savedDsv);  // the game's own depth-stencil: Scaleform masks with stencil
    if (panelBlend != nullptr) context->OMSetBlendState(panelBlend, blendFactor, sampleMask);
    draw(user);
    // The tint pass: the same draw once more, into the tint texture, without depth or stencil writes.
    ID3D11RenderTargetView* tintRtv = (tintBlend != nullptr && (bd.RenderTarget[0].RenderTargetWriteMask & 7u) != 0)
        ? TintTarget() : nullptr;
    if (tintRtv != nullptr) {
        ID3D11DepthStencilState* gameDepth = nullptr;
        UINT stencilRef = 0;
        context->OMGetDepthStencilState(&gameDepth, &stencilRef);
        ID3D11DepthStencilState* noWrites = NoWrites(context, gameDepth);
        context->OMSetRenderTargets(1, &tintRtv, savedDsv);
        context->OMSetBlendState(tintBlend, blendFactor, sampleMask);
        if (noWrites != nullptr) context->OMSetDepthStencilState(noWrites, stencilRef);
        draw(user);
        context->OMSetDepthStencilState(gameDepth, stencilRef);
        if (gameDepth != nullptr) gameDepth->Release();
        NoteTint();
    }
    if (panelBlend != nullptr || tintRtv != nullptr) context->OMSetBlendState(gameBlend, blendFactor, sampleMask);
    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedRtvs, savedDsv);
    for (auto*& r : savedRtvs) {
        if (r != nullptr) {
            r->Release();
            r = nullptr;
        }
    }
    if (savedDsv != nullptr) savedDsv->Release();
    if (gameBlend != nullptr) gameBlend->Release();
    NoteDraw(false, textBatch);
    return true;
}

// ---- Present thread ---------------------------------------------------------------------------------------------------

void SetArmed(bool armed) noexcept {
    gArmed.store(armed, std::memory_order_release);
}

Draws TakeDraws() noexcept {
    Draws d;
    d.draws = gPanelDraws.exchange(0, std::memory_order_relaxed);
    d.text = gPanelTextDraws.exchange(0, std::memory_order_relaxed);
    d.early = gPanelEarlyDraws.exchange(0, std::memory_order_relaxed);
    d.earlyText = gPanelEarlyTextDraws.exchange(0, std::memory_order_relaxed);
    return d;
}

unsigned TakeTintDraws() noexcept {
    return gTintDraws.exchange(0, std::memory_order_relaxed);
}

void NoteDrawsLastFrame(unsigned draws) noexcept {
    gPanelDrawsLastFrame.store(draws, std::memory_order_relaxed);
}

unsigned DrawsLastFrame() noexcept {
    return gPanelDrawsLastFrame.load(std::memory_order_relaxed);
}

unsigned Width() noexcept {
    return gPanel.width;
}

unsigned Height() noexcept {
    return gPanel.height;
}

bool HasTexture() noexcept {
    return gPanel.texture != nullptr;
}

bool Ready() noexcept {
    return gPanel.texture != nullptr && gPanel.tintSrv != nullptr;
}

bool HasTarget() noexcept {
    return gPanel.rtv != nullptr;
}

bool Usable() noexcept {
    return !gPanelSwapchain.Refused() && !gPanel.failed && !(gPipeline.tried && !gPipeline.ready);
}

ID3D11Texture2D* Texture() noexcept {
    return gPanel.texture.Get();
}

void SetColour(ColourHooks const& hooks) noexcept {
    gColour = hooks;
}

ID3D11Texture2D* Blit(ID3D11DeviceContext* context, ID3D11Device* device, QuadSwapchain& swapchain, Window const& window,
    UINT factor, Masks const& masks, float whiteScale, BlitOptions const& options) noexcept {
    if (!EnsurePipeline(device) || gPanel.srv == nullptr || gPanel.tintSrv == nullptr) {
        return nullptr;
    }
    std::uint32_t index = 0;
    if (!quad_swapchain::Acquire(swapchain, index)) {
        return nullptr;
    }
    bool drew = false;
    ComPtr<ID3D11RenderTargetView>& rtv = swapchain.rtvs[index];
    if (rtv == nullptr) {
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = static_cast<DXGI_FORMAT>(quad_swapchain::kFormat);
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(swapchain.images[index], &rd, rtv.GetAddressOf());
    }
    if (rtv != nullptr) {
        PanelParams params{{factor, gPanel.width, gPanel.height, 0},
            {SamplesLinear(gPanel.format) ? 1.0f : 0.0f, IsFloatFormat(gPanel.format) ? whiteScale : 1.0f, options.backdrop, 0.0f},
            {static_cast<std::int32_t>(window.x), static_cast<std::int32_t>(window.y), 0, 0}, {}};
        for (unsigned m = 0; m < masks.count && m < Masks::kMax; ++m) {
            params.masks[m][0] = masks.rects[m].x0;
            params.masks[m][1] = masks.rects[m].y0;
            params.masks[m][2] = masks.rects[m].x1;
            params.masks[m][3] = masks.rects[m].y1;
        }
        ID3D11ShaderResourceView* const figure = options.figure;
        if (figure != nullptr) {
            for (int i = 0; i < 4; ++i) {
                params.figureRect[i] = options.figureRect[i];
            }
            params.figureParams[0] = options.figureOpacity;
            params.figureParams[1] = options.figureGamma;
        }
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
        bool const corrected = gPipeline.colourPs != nullptr && gColour.begin != nullptr && gColour.begin(context, options.colourSeed);
        context->VSSetShader(gPipeline.vs.Get(), nullptr, 0);
        context->PSSetShader(corrected ? gPipeline.colourPs.Get() : gPipeline.ps.Get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->PSSetShaderResources(0, 2, sources);
        if (figure != nullptr) {
            ID3D11SamplerState* const sampler = gPipeline.linearClamp.Get();
            ID3D11ShaderResourceView* figureSrv = figure;
            context->PSSetShaderResources(2, 1, &figureSrv);
            context->PSSetSamplers(0, 1, &sampler);
        }
        context->PSSetConstantBuffers(0, 1, &constants);
        context->Draw(3, 0);
        if (corrected && gColour.end != nullptr) {
            gColour.end(context);
        }
        drew = true;
    }
    ID3D11Texture2D* const image = swapchain.images[index];
    if (drew && options.probe != nullptr) {  // one look at what the headset gets: the panel image after the blit
        options.probe(context, image, gPanel.texture.Get());
    }
    mevr::host::testloop::OnImageWritten(swapchain.handle, image);  // snapshots
    quad_swapchain::Release(swapchain);
    return drew ? image : nullptr;
}

bool DrawCoverageGrid(ID3D11DeviceContext* context, ID3D11Device* device, ID3D11RenderTargetView* target, UINT columns,
    UINT rows) noexcept {
    if (!EnsurePipeline(device) || gPanel.srv == nullptr || gPanel.tintSrv == nullptr || target == nullptr) {
        return false;
    }
    std::int32_t const cellW = static_cast<std::int32_t>((gPanel.width + columns - 1) / columns);
    std::int32_t const cellH = static_cast<std::int32_t>((gPanel.height + rows - 1) / rows);
    PanelParams params{{1, gPanel.width, gPanel.height, 0}, {0.0f, 1.0f, 0.0f, 0.0f}, {0, 0, cellW, cellH}, {}};
    context->UpdateSubresource(gPipeline.params.Get(), 0, nullptr, &params, 0, 0);
    StateGuard guard(context);
    D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(columns), static_cast<float>(rows), 0.0f, 1.0f};
    float const blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
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
    return true;
}

QuadSwapchain& Swapchain() noexcept {
    return gPanelSwapchain;
}

UINT Factor() noexcept {
    return gPanelFactor;
}

bool Render(ID3D11DeviceContext* context, ID3D11Device* device, XrSession session, float whiteScale, Masks const& masks,
    BlitOptions const& options) noexcept {
    UINT const factor = std::clamp<UINT>(static_cast<UINT>(std::lround(gPanel.width / 1400.0)), 2u, 4u);
    std::uint32_t const width = std::max<std::uint32_t>(gPanel.width / factor, 16);
    std::uint32_t const height = std::max<std::uint32_t>(gPanel.height / factor, 16);
    if (!quad_swapchain::Ensure(session, gPanelSwapchain, width, height, "HUD panel", "hud")) {
        return false;
    }
    gPanelFactor = factor;
    bool const probe = !gProbedPanel && gPanelDrawsLastFrame.load(std::memory_order_relaxed) > 0;
    BlitOptions blit = options;
    if (!probe) {
        blit.probe = nullptr;
    }
    ID3D11Texture2D* const image =
        Blit(context, device, gPanelSwapchain, {0, 0, gPanel.width, gPanel.height}, factor, masks, whiteScale, blit);
    gProbedPanel = gProbedPanel || (probe && image != nullptr);
    return image != nullptr;
}

void EndFrame(ID3D11DeviceContext* context, unsigned draws, unsigned tintDraws, bool keepUntilNextUi) noexcept {
    if (tintDraws > 0 && !gLoggedTint) {
        gLoggedTint = true;
        mevr::log::Info("overlays: first frame with Scaleform blend-mode draws in the panel's tint ({} draws)", tintDraws);
    }
    gTintDirty = gTintDirty || tintDraws > 0;
    if (keepUntilNextUi) {
        // Preserve a complete image until the next game's UI starts. A frame without Flash keeps
        // the last complete image; a runtime/desktop draw inside Present cannot start that frame.
        gClearBeforeNextUi = true;
    } else if (gPanel.rtv != nullptr && context != nullptr && (draws > 0 || gPanelNeedsClear || gClearBeforeNextUi)) {
        float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->ClearRenderTargetView(gPanel.rtv.Get(), clear);
        if (gTintDirty && gPanel.tintRtv != nullptr) {
            float const white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            context->ClearRenderTargetView(gPanel.tintRtv.Get(), white);
            gTintDirty = false;
        }
        gPanelNeedsClear = false;
        gClearBeforeNextUi = false;
    }
}

void NoteArmChange() noexcept {
    gPanelNeedsClear = true;
}

unsigned TakeClears() noexcept {
    unsigned const clears = gClearsBeforeUi;
    gClearsBeforeUi = 0;
    return clears;
}

mevr::vr::overlay::Panel& Placement() noexcept {
    return gPlacement;
}

void Recentered() noexcept {
    gRecentered.store(true, std::memory_order_release);
}

void RequestReplace() noexcept {
    gReplaceRequested.store(true, std::memory_order_release);
}

void Place(XrQuaternionf const& head, bool headValid, overlay::PanelSettings const& settings, float followDeg, float dt) noexcept {
    if (gRecentered.exchange(false, std::memory_order_acq_rel)) {
        gPlacement.Reset();
        mevr::log::Info("overlays: recentered, HUD panel straight ahead");
    }
    if (headValid && gReplaceRequested.exchange(false, std::memory_order_acq_rel)) {
        gPlacement.FaceHeading(head);
        mevr::log::Info("overlays: HUD panel re-placed at heading {:.0f} deg", gPlacement.Yaw() / kDegToRad);
    }
    bool const wasFollowing = gPlacement.Following();
    gPlacement.Update(head, headValid, settings, dt);
    if (gPlacement.Following() && !wasFollowing) {
        mevr::log::Info("overlays: head turned past {:.0f} deg: HUD panel follows", followDeg);
    }
}

void ForgetSession() noexcept {
    gPanelSwapchain.Forget();
}

XrCompositionLayerQuad const* Layer(Frame const& frame) noexcept {
    std::uint64_t const now = GetTickCount64();
    float const dt = gLastFrameMs == 0 ? 0.0f : static_cast<float>(now - gLastFrameMs) / 1000.0f;
    gLastFrameMs = now;
    if (frame.context == nullptr || frame.session == XR_NULL_HANDLE) {
        SetArmed(false);
        return nullptr;
    }
    if (frame.session != gLayerSession) {
        ForgetSession();  // its swapchain went with the old session
        gLayerSession = frame.session;
    }
    bool const engaged = frame.enabled && frame.stereo && Usable();
    Draws const draws = TakeDraws();
    overlay::PanelSettings settings;
    settings.distance = std::clamp(frame.distanceM, 0.5f, 5.0f);
    settings.drop = std::clamp(frame.dropM, -1.0f, 1.5f);
    settings.followAfter = frame.followDeg > 0.0f ? std::clamp(frame.followDeg, 10.0f, 170.0f) * kDegToRad : 0.0f;
    Place(frame.head.orientation, frame.headValid, settings, frame.followDeg, dt);

    XrCompositionLayerQuad const* shown = nullptr;
    // Gameplay's world frames, or any frame of the 3D cinema screen with its UI layer (LE1's HalcyonOverlays `screenUi`).
    bool const wanted = (frame.worldFrame && frame.gameplay) || frame.screenUi;
    static bool screenUiShown = false;  // Present thread
    if (wanted && engaged && gPanel.texture != nullptr && frame.device != nullptr) {
        NoteDrawsLastFrame(draws.draws);
        BlitOptions options;
        options.colourSeed = 2;  // the HUD panel's dither pattern, as LE1's
        options.backdrop = frame.backdrop;  // a game screen standing in the world (LE1's BlitOptionsFor)
        if (Render(frame.context, frame.device, frame.session, frame.whiteScale, frame.screenUi && !frame.screenMasks ? Masks{} : frame.masks, options)) {
            float const width = std::clamp(frame.widthM, 0.3f, 4.0f);
            gQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gQuad.space = frame.appSpace;
            gQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gQuad.subImage.swapchain = gPanelSwapchain.handle;
            gQuad.subImage.imageRect = {{0, 0},
                {static_cast<int32_t>(gPanelSwapchain.width), static_cast<int32_t>(gPanelSwapchain.height)}};
            gQuad.pose = gPlacement.Pose(settings);
            gQuad.size = {width, width * static_cast<float>(gPanel.height) / static_cast<float>(gPanel.width)};
            if (frame.screenUi) {
                // The part of the window the screen shows, on a quad in front of the screen toward the eyes, as large to the
                // eye as on the screen: no disparity of its own, so it sits nearer than the scene behind it (LE1's).
                auto const sw = static_cast<float>(gPanelSwapchain.width);
                auto const sh = static_cast<float>(gPanelSwapchain.height);
                float const* const part = frame.screenPart;
                gQuad.subImage.imageRect = {{static_cast<int32_t>(part[0] * sw), static_cast<int32_t>(part[1] * sh)},
                    {std::max<int32_t>(1, static_cast<int32_t>((part[2] - part[0]) * sw)),
                        std::max<int32_t>(1, static_cast<int32_t>((part[3] - part[1]) * sh))}};
                gQuad.space = frame.screenSpace != XR_NULL_HANDLE ? frame.screenSpace : frame.appSpace;
                gQuad.pose = frame.screenPose;
                gQuad.size = frame.screenSize;
                if (!screenUiShown) {
                    char line[320];
                    std::snprintf(line, sizeof line,
                        "[MEVR-HUD] the 3D screen's UI on its own layer %.2f m in front of the screen (%.2f x %.2f m); the UI's part "
                        "u %.3f..%.3f v %.3f..%.3f of %ux%u", frame.screenForwardM, gQuad.size.width, gQuad.size.height, part[0],
                        part[2], part[1], part[3], gPanel.width, gPanel.height);
                    LogBoth(line);
                }
                screenUiShown = true;
            }
            shown = &gQuad;
            if (!gLoggedPanel || now >= gNextPanelLogMs) {
                char line[384];
                std::snprintf(line, sizeof line,
                    "[MEVR-HUD] panel layer: %u UI draws this frame, %ux%u (1/%u of %ux%u), %.2f x %.2f m at "
                    "(%.2f, %.2f, %.2f) m, heading %.0f deg; since the last line: %u UI draws before the renderer's left-eye "
                    "capture (replaced by the later batch), %u left to the renderer (not 'over' blended)",
                    draws.draws, gPanelSwapchain.width, gPanelSwapchain.height, gPanelFactor, gPanel.width, gPanel.height,
                    gQuad.size.width, gQuad.size.height, gQuad.pose.position.x, gQuad.pose.position.y, gQuad.pose.position.z,
                    gPlacement.Yaw() / kDegToRad, gEarlyLeft.exchange(0, std::memory_order_relaxed),
                    gNotOver.exchange(0, std::memory_order_relaxed));
                LogBoth(line);
                gLoggedPanel = true;
                gNextPanelLogMs = now + 30000;
            }
        }
    }
    if (screenUiShown && !(frame.screenUi && shown != nullptr)) {
        screenUiShown = false;
        LogBoth("[MEVR-HUD] the 3D screen's UI layer is gone");
    }
    if (frame.beforeClear != nullptr) {
        frame.beforeClear(frame.user, shown != nullptr);  // windows of this frame's UI (the HUD pieces), before the clear
    }
    EndFrame(frame.context, draws.draws, TakeTintDraws(), false);
    gEarlyThisFrame = false;
    gLateThisFrame = false;
    // Next frame's UI goes to the panel only if this one showed it (or nothing went there yet, so the texture can be
    // made), and the panel works.
    bool const armNext = engaged && wanted && (shown != nullptr || gPanel.texture == nullptr);
    if (armNext != Armed()) {
        mevr::log::Info("overlays: game UI goes to {}", !armNext ? "the eye images"
                                                        : frame.screenUi ? "a layer in front of the 3D screen"
                                                                         : "the body-locked HUD panel");
        NoteArmChange();
    }
    SetArmed(armNext);
    return shown;
}

} // namespace mevr::host::hud_panel
