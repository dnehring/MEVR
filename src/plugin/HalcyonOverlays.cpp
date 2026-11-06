// SPDX-License-Identifier: MIT
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
// - the game's interaction prompt (the target status widget: the object's name, its action and the A glyph) cut out
//   of the HUD panel and shown big at the object the mod says A would use (core/vr/InteractLabel, research/40);
// - the HUD pieces on the body (core/vr/HudPieces, research/41): the player's status (the health and shield arc, the
//   squad's portraits, the medi-gel count) on the back of the left wrist, a fixed offset from the left grip pose, and
//   the weapon panel (the gun, its heat strip, the grenade count) on the drawn gun where the mod says it is, both cut
//   out of the HUD panel meanwhile ([Hud] in ASI\LE1MEVR.ini, read here);
// - the comfort vignette (plugin/Vignette.cpp, research/31): head-locked, under the others, only while wanted;
// - [MENUVIS] readbacks that tell whether Halcyon's Insert menu texture has visible pixels.
//
// The panel texture holds premultiplied colour and coverage from the UI's "over" draws; Scaleform's other
// blend modes (multiply, screen) add a tint texture (white = the world shows unchanged) that the panel's
// alpha is made from, so they reach the panel too instead of staying in the eyes.

#include "halcyon/le1/mevr_bridge.h"
#include "halcyon/le1/logger.h"
#include "halcyon/le1/pchud.h"
#include "halcyon/le1/vr_config.h"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/AimRay.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/FrameProbe.hpp"
#include "core/vr/HudPieces.hpp"
#include "core/vr/MakoControls.hpp"
#include "core/vr/InteractLabel.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "core/vr/SquadMenu.hpp"
#include "core/vr/WheelCrop.hpp"
#include "plugin/Calibration.hpp"
#include "plugin/Colour.hpp"
#include "plugin/ConversationWorld.hpp"
#include "plugin/GalaxyHologram.hpp"
#include "plugin/SquadPlaceDraw.hpp"
#include "plugin/UiWorldFigure.hpp"
#include "plugin/WorldUi.hpp"
#include "plugin/GamePointer.hpp"
#include "plugin/Halcyon.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/MainMenu.hpp"
#include "plugin/OmniPad.hpp"
#include "plugin/OmniToolRail.hpp"
#endif
#include "plugin/MevrMenu.hpp"
#include "plugin/PerfProbe.hpp"
#include "plugin/RecenterTilt.hpp"
#include "plugin/SightLayers.hpp"
#include "plugin/Spectator.hpp"
#include "plugin/Swapchains.hpp"
#include "plugin/PowerWheel.hpp"
#include "plugin/SquadPanel.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/Vignette.hpp"
#include "plugin/WristBeam.hpp"
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
#include <functional>
#include <cstdio>
#include <cstring>
#include <filesystem>
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
float gPanelBackdrop = 0.0f;  // the world UI's backdrop for this frame's panel blit (research/53; 0: the HUD as it is)
// The UI world's squad member behind the world UI's panel this frame (research/57 section 7; nullopt: none).
std::optional<mevr::host::ui_world_figure::Picture> gPanelFigure;
constexpr float kGalaxyUiWidthM = 1.50f;  // the game's UI frame over the galaxy hologram's table (its travel box ~1 m of it)

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

// ---- Swapchains (Present thread) ---------------------------------------------------------------
struct QuadSwapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;  // owned by the runtime
    std::vector<ComPtr<ID3D11RenderTargetView>> rtvs;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool failed = false;          // refused (or its texture failed) at failedMs
    std::uint64_t failedMs = 0;

    // Refused just now: not tried again until plugin/Swapchains says (2 s, or once room was freed). Never for the session.
    bool Refused() const { return failed && !mevr::host::swapchains::RetryDue(failedMs); }
    void Refuse() {
        failed = true;
        failedMs = GetTickCount64();
    }
    void Forget() {  // the session that owned it is gone: its swapchains went with it
        mevr::host::swapchains::Forget(handle);
        handle = XR_NULL_HANDLE;
        images.clear();
        rtvs.clear();
        width = height = 0;
        failed = false;
    }
    void Destroy() {
        mevr::host::swapchains::Destroy(handle);
        handle = XR_NULL_HANDLE;
        images.clear();
        rtvs.clear();
        width = height = 0;
    }
};

XrSession gSession = XR_NULL_HANDLE;
QuadSwapchain gPanelSwapchain;
QuadSwapchain gReticleSwapchain;
QuadSwapchain gPointerDotSwapchain;   // the game pointer's dot (plugin/GamePointer, research/42)
UINT gPanelFactor = 2;

// `evict`: how plugin/Swapchains lets go of it for room (default: Destroy; it's made again when next wanted).
bool CreateSwapchain(XrSession session, std::uint32_t width, std::uint32_t height, QuadSwapchain& out,
    char const* what, std::function<void()> evict = {}) {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = kSwapchainFormat;
    info.sampleCount = 1;
    info.width = width;
    info.height = height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (!evict) {
        evict = [&out] { out.Destroy(); };
    }
    out.Destroy();
    if (XR_FAILED(mevr::host::swapchains::Create(session, info, what, std::move(evict), out.handle, out.images))) {
        out.Refuse();  // plugin/Swapchains logged it
        return false;
    }
    out.failed = false;
    out.rtvs.resize(out.images.size());
    out.width = width;
    out.height = height;
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
// smaller; pixels inside any of `masks` come out transparent. Returns the image written (released), or nullptr.
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
// Up to three rectangles cut out of a blit (the shader's masks): a wheel or the interaction prompt, the wrist piece, the
// gun piece.
struct Masks {
    static constexpr unsigned kMax = 3;
    Mask rects[kMax];
    unsigned count = 0;
    void Add(UINT x, UINT y, UINT width, UINT height) {
        if (count < kMax && width > 0 && height > 0) {
            rects[count++] = {static_cast<INT>(x), static_cast<INT>(y), static_cast<INT>(x + width), static_cast<INT>(y + height)};
        }
    }
};

ID3D11Texture2D* Blit(ID3D11DeviceContext* context, ID3D11Device* device, QuadSwapchain& swapchain, Window const& window,
    UINT factor, Masks const& masks, float whiteScale, bool probe = false) {
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
            {SamplesLinear(gPanel.format) ? 1.0f : 0.0f, IsFloatFormat(gPanel.format) ? whiteScale : 1.0f, gPanelBackdrop, 0.0f},
            {static_cast<std::int32_t>(window.x), static_cast<std::int32_t>(window.y), 0, 0}, {}};
        for (unsigned m = 0; m < masks.count && m < Masks::kMax; ++m) {
            params.masks[m][0] = masks.rects[m].x0;
            params.masks[m][1] = masks.rects[m].y0;
            params.masks[m][2] = masks.rects[m].x1;
            params.masks[m][3] = masks.rects[m].y1;
        }
        // The world UI's panel: the UI world's squad member behind the game's UI, over the 16:9 stage (research/57 section 7).
        ID3D11ShaderResourceView* figure = nullptr;
        if (&swapchain == &gPanelSwapchain && gPanelFigure && gPanelFigure->srv != nullptr && gPanelFigure->opacity > 0.0f) {
            mevr::vr::world_ui::Rect const stage = mevr::vr::world_ui::Stage(static_cast<float>(gPanel.width), static_cast<float>(gPanel.height));
            float const w = (stage.u1 - stage.u0) * static_cast<float>(gPanel.width);
            float const h = (stage.v1 - stage.v0) * static_cast<float>(gPanel.height);
            if (w > 1.0f && h > 1.0f) {
                params.figureRect[0] = stage.u0 * static_cast<float>(gPanel.width);
                params.figureRect[1] = stage.v0 * static_cast<float>(gPanel.height);
                params.figureRect[2] = 1.0f / w;
                params.figureRect[3] = 1.0f / h;
                params.figureParams[0] = std::clamp(gPanelFigure->opacity, 0.0f, 1.0f);
                params.figureParams[1] = gPanelFigure->decodeGamma;
                figure = gPanelFigure->srv;
            }
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
        bool const corrected = gPipeline.colourPs != nullptr &&
            mevr::host::colour::BeginPanel(context, &swapchain == &gPanelSwapchain ? 2u : 3u);
        context->VSSetShader(gPipeline.vs.Get(), nullptr, 0);
        context->PSSetShader(corrected ? gPipeline.colourPs.Get() : gPipeline.ps.Get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->PSSetShaderResources(0, 2, sources);
        if (figure != nullptr) {
            ID3D11SamplerState* const sampler = gPipeline.linearClamp.Get();
            context->PSSetShaderResources(2, 1, &figure);
            context->PSSetSamplers(0, 1, &sampler);
        }
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
bool EnsureSwapchain(QuadSwapchain& swapchain, std::uint32_t width, std::uint32_t height, char const* what, char const* name,
    std::function<void()> evict = {}) {
    if (swapchain.handle != XR_NULL_HANDLE && (swapchain.width != width || swapchain.height != height)) {
        swapchain.Destroy();
    }
    if (swapchain.handle == XR_NULL_HANDLE) {
        if (swapchain.Refused() || !CreateSwapchain(gSession, width, height, swapchain, what, std::move(evict))) {
            return false;
        }
        mevr::host::testloop::NameSwapchain(swapchain.handle, name);
    }
    return true;
}

bool RenderPanel(ID3D11DeviceContext* context, ID3D11Device* device, float whiteScale, Masks const& masks) {
    UINT const factor = std::clamp<UINT>(static_cast<UINT>(std::lround(gPanel.width / 1400.0)), 2u, 4u);
    std::uint32_t const width = std::max<std::uint32_t>(gPanel.width / factor, 16);
    std::uint32_t const height = std::max<std::uint32_t>(gPanel.height / factor, 16);
    if (!EnsureSwapchain(gPanelSwapchain, width, height, "HUD panel", "hud")) {
        return false;
    }
    gPanelFactor = factor;
    bool const probe = !gProbedPanel && gPanelDrawsLastFrame.load(std::memory_order_relaxed) > 0;
    ID3D11Texture2D* const image =
        Blit(context, device, gPanelSwapchain, {0, 0, gPanel.width, gPanel.height}, factor, masks, whiteScale, probe);
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
    PanelParams params{{1, gPanel.width, gPanel.height, 0}, {0.0f, 1.0f, 0.0f, 0.0f}, {0, 0, cellW, cellH}, {}};
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

// ---- The interaction prompt at the object (Present thread; research/40) ----------------------------------------
namespace il = mevr::vr::interact;

QuadSwapchain gLabelSwapchain;
XrCompositionLayerQuad gLabelQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};

struct LabelState {
    bool shown = false;               // a label went out last frame
    std::uint32_t serial = 0;         // the mod's selection it was logged for
    std::uint32_t lastLabelSize = 0;  // the mod's Label size last seen (to log a mismatch once)
};
LabelState gLabel;

// A conversation in the world (research/59): its subtitle and wheel on the label's and the game wheel's swapchains.
struct ConvoUiState {
    bool subtitleShown = false;
    std::uint32_t speakerSerial = 0;
    bool wheelShown = false;
    std::uint32_t conversation = 0;
    XrPosef wheelPose{};
};
ConvoUiState gConvoUi;

// The mod's latest label, or none (a struct of another size: host and mod from different builds).
il::Label LabelWanted() {
    il::Label latest = il::Latest();
    if (latest.size == sizeof(il::Label)) {
        return latest;
    }
    if (latest.size != 0 && latest.size != gLabel.lastLabelSize) {
        gLabel.lastLabelSize = latest.size;
        mevr::log::Warn("overlays: the mod's interaction label is {} bytes, the host's {}: no label; rebuild both", latest.size,
            sizeof(il::Label));
    }
    return il::Label{};
}

// ---- HUD pieces on the body (Present thread; research/41) ------------------------------------------------------
// The player's status on the left wrist (from the left grip pose, host only) and the weapon panel on the gun (where
// the mod says, HostServices v22). [Hud] is read here from the settings file, once a second while it changes.
namespace hp = mevr::vr::hud_pieces;

QuadSwapchain gWristSwapchain;
QuadSwapchain gGunSwapchain;
XrCompositionLayerQuad gWristQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
XrCompositionLayerQuad gGunQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
// The cockpit in the Mako (research/47): the radar on a display of its own (the vehicle's status uses the gun piece's).
QuadSwapchain gRadarSwapchain;
XrCompositionLayerQuad gRadarQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
bool gCockpitHudShown = false;

struct PiecesState {
    bool wristShown = false;          // the wrist piece went out last frame
    bool gunShown = false;            // the gun piece went out last frame
    std::uint32_t gunSerial = 0;      // the mod's weapon it was logged for
    std::uint32_t lastPiecesSize = 0; // the mod's Pieces size last seen (to log a mismatch once)
    std::uint64_t nextLogMs = 0;      // the wrist's place, now and then
};
PiecesState gPieces;

hp::Settings gHudSettings;
std::string gHudSettingsText;
std::uint64_t gHudSettingsNextMs = 0;
FILETIME gHudSettingsStamp{};
bool gHudSettingsLoaded = false;

// [Hud] from ASI\LE1MEVR.ini (the mod's file too): re-read within a second of a change.
hp::Settings const& HudSettings(std::uint64_t now) {
    if (now < gHudSettingsNextMs) {
        return gHudSettings;
    }
    gHudSettingsNextMs = now + 1000;
    std::wstring const& path = SettingsPath();
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return gHudSettings;
    }
    if (gHudSettingsLoaded && CompareFileTime(&data.ftLastWriteTime, &gHudSettingsStamp) == 0) {
        return gHudSettings;
    }
    gHudSettingsStamp = data.ftLastWriteTime;
    gHudSettingsLoaded = true;
    mevr::ini::Loaded const loaded = mevr::ini::Read(std::filesystem::path(path));
    std::string problems;
    gHudSettings = hp::ReadSettings(loaded.file, &problems);
    if (!problems.empty()) {
        mevr::log::Warn("overlays: [Hud] {}", problems);
    }
    if (std::string const text = hp::Describe(gHudSettings); text != gHudSettingsText) {
        gHudSettingsText = text;
        LogBoth("[MEVR-HUD] pieces: " + text);
    }
    return gHudSettings;
}

// The mod's latest pieces, or none (a struct of another size: host and mod from different builds).
hp::Pieces PiecesWanted() {
    hp::Pieces latest = hp::Latest();
    if (latest.size == sizeof(hp::Pieces)) {
        return latest;
    }
    if (latest.size != 0 && latest.size != gPieces.lastPiecesSize) {
        gPieces.lastPiecesSize = latest.size;
        mevr::log::Warn("overlays: the mod's HUD pieces are {} bytes, the host's {}: the weapon panel stays on the HUD; rebuild both",
            latest.size, sizeof(hp::Pieces));
    }
    return hp::Pieces{};
}

// One HUD piece's quad: its band blitted into its swapchain (1:1), the quad placed. False if anything failed.
bool ShowPiece(ID3D11DeviceContext* context, ID3D11Device* device, QuadSwapchain& swapchain, XrCompositionLayerQuad& quad,
    char const* what, char const* name, hp::Pixels const& px, XrVector3f const& position, XrVector3f const& head, float widthM,
    XrVector3f const& up, void* appSpace, float whiteScale) {
    hp::Quad placed;
    float const aspect = static_cast<float>(px.width) / static_cast<float>(px.height);
    if (px.width == 0 || px.height == 0 || !hp::Place(position, head, widthM, aspect, placed, up) ||
        !EnsureSwapchain(swapchain, px.width, px.height, what, name) ||
        Blit(context, device, swapchain, {px.x, px.y, px.width, px.height}, 1, Masks{}, whiteScale) == nullptr) {
        return false;
    }
    quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
    quad.space = static_cast<XrSpace>(appSpace);
    quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad.subImage.swapchain = swapchain.handle;
    quad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(px.width), static_cast<int32_t>(px.height)}};
    quad.pose = placed.pose;
    quad.size = {placed.width, placed.height};
    return true;
}

// ---- The Mako's lever and stick (research/46): two quads lying flat at the seat, drawn on the CPU when the picture changes ----
namespace mc = mevr::vr::mako_controls;

struct ControlQuad {
    QuadSwapchain swapchain;
    ComPtr<ID3D11Texture2D> upload;
    std::uint32_t key = 0xFFFFFFFFu;  // the picture in the swapchain's last released image
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};

    void Forget() {
        swapchain.Forget();
        key = 0xFFFFFFFFu;
    }
};
ControlQuad gLeverQuad;
ControlQuad gStickQuad;
bool gMakoControlsShown = false;
std::uint32_t gMakoViewSize = 0;  // a mismatch already logged

// The picture into the quad's swapchain if it changed. False if anything failed (the quad isn't shown).
template <class Pixels>
bool UpdateControl(ID3D11DeviceContext* context, ID3D11Device* device, ControlQuad& control, std::uint32_t width, std::uint32_t height,
    std::uint32_t key, char const* what, char const* name, Pixels const& pixels) {
    if (!EnsureSwapchain(control.swapchain, width, height, what, name, [&control] {
            control.swapchain.Destroy();
            control.key = 0xFFFFFFFFu;  // made again, it's drawn again
        })) {
        return false;
    }
    if (key == control.key) {
        return true;
    }
    if (control.upload == nullptr) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = width;
        td.Height = height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        if (FAILED(device->CreateTexture2D(&td, nullptr, control.upload.GetAddressOf()))) {
            mevr::log::Error("overlays: {} texture creation failed", what);
            control.swapchain.Destroy();
            control.swapchain.Refuse();
            return false;
        }
    }
    std::vector<std::uint32_t> image = pixels();
    context->UpdateSubresource(control.upload.Get(), 0, nullptr, image.data(), width * 4, 0);
    std::uint32_t index = 0;
    if (!AcquireImage(control.swapchain, index)) {
        return false;
    }
    context->CopyResource(control.swapchain.images[index], control.upload.Get());
    ReleaseImage(control.swapchain);
    control.key = key;
    // Snapshots draw it from these pixels: the image is released only when the picture changes.
    mevr::host::testloop::SetStaticImage(control.swapchain.handle, width, height, std::move(image));
    return true;
}

void PlaceControl(ControlQuad& control, XrVector3f const& at, XrVector3f const& forward, std::uint32_t width, std::uint32_t height, float widthM,
    float lengthM, void* appSpace) {
    control.quad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    control.quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
    control.quad.space = static_cast<XrSpace>(appSpace);
    control.quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    control.quad.subImage.swapchain = control.swapchain.handle;
    control.quad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(width), static_cast<int32_t>(height)}};
    control.quad.pose = mc::FlatPose(at, forward);
    control.quad.size = {widthM, lengthM};
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
    if (gReticleSwapchain.Refused() || !CreateSwapchain(gSession, kReticleSize, kReticleSize, gReticleSwapchain, "reticle")) {
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
        if (!AcquireImage(gReticleSwapchain, index)) {
            mevr::log::Error("overlays: reticle swapchain image acquire failed");
            gReticleSwapchain.Destroy();
            gReticleSwapchain.Refuse();
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

// The game pointer's dot (plugin/GamePointer, research/42): a filled light dot over a half-transparent dark rim, the
// menu pointer's look (MevrMenuPointer.cpp), premultiplied; one static image like the reticle's.
constexpr UINT kPointerDotSize = 64;

std::vector<std::uint32_t> PointerDotPixels() {
    std::vector<std::uint32_t> pixels(static_cast<size_t>(kPointerDotSize) * kPointerDotSize);
    auto encode = [](float linear) {
        float const c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint32_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    float const px = 2.0f / static_cast<float>(kPointerDotSize);
    auto disc = [px](float d, float radius) { return std::clamp((radius - d) / px + 0.5f, 0.0f, 1.0f); };
    for (UINT y = 0; y < kPointerDotSize; ++y) {
        for (UINT x = 0; x < kPointerDotSize; ++x) {
            float const u = (static_cast<float>(x) + 0.5f) * px - 1.0f;
            float const v = (static_cast<float>(y) + 0.5f) * px - 1.0f;
            float const d = std::sqrt(u * u + v * v);
            float const light = disc(d, 0.62f);
            float const rimAlpha = 0.75f * disc(d, 0.95f);
            float const a = light + rimAlpha * (1.0f - light);
            float const r = light * 0.31f, g = light * 0.78f, b = light * 1.0f;
            pixels[static_cast<size_t>(y) * kPointerDotSize + x] =
                encode(r) | (encode(g) << 8) | (encode(b) << 16) | (static_cast<std::uint32_t>(a * 255.0f + 0.5f) << 24);
        }
    }
    return pixels;
}

bool EnsurePointerDot(ID3D11DeviceContext* context, ID3D11Device* device) {
    if (gPointerDotSwapchain.handle != XR_NULL_HANDLE) {
        return true;
    }
    if (gPointerDotSwapchain.Refused() ||
        !CreateSwapchain(gSession, kPointerDotSize, kPointerDotSize, gPointerDotSwapchain, "game pointer dot")) {
        return false;
    }
    std::vector<std::uint32_t> const pixels = PointerDotPixels();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kPointerDotSize;
    td.Height = kPointerDotSize;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(), kPointerDotSize * 4, 0};
    ComPtr<ID3D11Texture2D> source;
    if (FAILED(device->CreateTexture2D(&td, &data, source.GetAddressOf()))) {
        mevr::log::Error("overlays: game pointer dot texture creation failed");
        gPointerDotSwapchain.Destroy();
        gPointerDotSwapchain.Refuse();
        return false;
    }
    for (size_t i = 0; i < gPointerDotSwapchain.images.size(); ++i) {
        std::uint32_t index = 0;
        if (!AcquireImage(gPointerDotSwapchain, index)) {
            mevr::log::Error("overlays: game pointer dot swapchain image acquire failed");
            gPointerDotSwapchain.Destroy();
            gPointerDotSwapchain.Refuse();
            return false;
        }
        context->CopyResource(gPointerDotSwapchain.images[index], source.Get());
        ReleaseImage(gPointerDotSwapchain);
    }
    mevr::host::testloop::NameSwapchain(gPointerDotSwapchain.handle, "gamepointer");
    mevr::host::testloop::SetStaticImage(gPointerDotSwapchain.handle, kPointerDotSize, kPointerDotSize, pixels);
    return true;
}

XrCompositionLayerQuad gPointerDotQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
bool gLoggedPointerDot = false;

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
        gPointerDotSwapchain.Forget();
        gWheelSwapchain.Forget();
        gLabelSwapchain.Forget();
        gWristSwapchain.Forget();
        gGunSwapchain.Forget();
        gRadarSwapchain.Forget();
        gLeverQuad.Forget();
        gStickQuad.Forget();
        mevr::host::ui_world_figure::Forget();
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
    // The galaxy map open in the CIC (research/51 section 8): the game's UI (its own map hidden, so its travel question) goes to
    // the panel too, which then stands over the hologram's table instead of in front of the body.
    bool const galaxyUi = gLastGameMode == 9 && mevr::host::galaxy_hologram::InWorld();
    // One of the game's screens standing in the room (research/53: the shop...): its UI goes to the panel, placed where it opened.
    bool const worldUi = mevr::vr::world_ui::ScreenMode(gLastGameMode) && mevr::host::world_ui::Active();
    // A conversation in the world (research/59): its movie goes to the panel, its subtitle and wheel cut out of it below.
    mevr::host::conversation_world::NoteFrame(frame.gameMode);
    std::optional<mevr::vr::conversation_world::State> const convo =
        gLastGameMode == 5 ? mevr::host::conversation_world::Current() : std::nullopt;
    bool const uiToPanel = gameplay || galaxyUi || worldUi || convo.has_value();

    // The game's own crosshair marks the screen centre (the head), not the gun: hidden while our
    // hand reticle stands in for it. The HUD movie keeps its authored layout on the panel (Halcyon's
    // head-locked shrink-and-centre would only make it small there).
    bool const handAim = ControllerAimActive();
    bool const panelUsable = !gPanelSwapchain.Refused() && !gPanel.failed && !(gPipeline.tried && !gPipeline.ready);
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
        gPanel.texture != nullptr && gPanel.tintSrv != nullptr && !gWheelSwapchain.Refused() && device != nullptr;
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
    Masks hudMasks;
    if (wheelWanted) {
        hudMasks.Add(cropPx.x, cropPx.y, cropPx.width, cropPx.height);
    }
    // The interaction prompt at the object (never with a wheel up: both take their part out of the same panel).
    il::Label const label = LabelWanted();
    bool const labelWanted = label.visible && !wheelWanted && engaged && gameplay && gLastGameMode == 0 && frame.worldFrame &&
        !frame.menuOpen && frame.headValid && gPanel.texture != nullptr && gPanel.tintSrv != nullptr && !gLabelSwapchain.Refused() &&
        device != nullptr;
    il::Rect const labelRect = label.crop[2] > 0.0f && label.crop[3] > 0.0f
        ? il::Rect{label.crop[0], label.crop[1], label.crop[2], label.crop[3]}
        : il::DefaultCrop(gPanel.width, gPanel.height);
    il::Pixels const labelPx = il::ToPixels(labelRect, gPanel.width, gPanel.height);
    if (labelWanted && labelPx.width > 0) {
        hudMasks.Add(labelPx.x, labelPx.y, labelPx.width, labelPx.height);
    }
    // The conversation's subtitle at the speaker and its wheel at a hand (research/59): their bands out of the panel. The label's
    // and the game wheel's swapchains serve them (neither is up in a conversation; the runtime limits swapchains).
    namespace cwv = mevr::vr::conversation_world;
    bool const convoAllowed = convo && engaged && frame.worldFrame && !frame.menuOpen && frame.headValid && gPanel.texture != nullptr &&
        gPanel.tintSrv != nullptr && device != nullptr;
    bool const convoSubtitleWanted = convoAllowed && convo->subtitle != 0 && !gLabelSwapchain.Refused();
    bool const convoWheelWanted = convoAllowed && convo->wheel != 0 && !gWheelSwapchain.Refused();
    il::Pixels const convoSubPx = convo ? il::ToPixels(convo->subtitleCrop[2] > 0.0f && convo->subtitleCrop[3] > 0.0f
                                                           ? il::Rect{convo->subtitleCrop[0], convo->subtitleCrop[1], convo->subtitleCrop[2], convo->subtitleCrop[3]}
                                                           : cwv::DefaultSubtitleCrop(gPanel.width, gPanel.height),
                                              gPanel.width, gPanel.height)
                                        : il::Pixels{};
    il::Pixels const convoWheelPx = convo ? il::ToPixels(convo->wheelCrop[2] > 0.0f && convo->wheelCrop[3] > 0.0f
                                                             ? il::Rect{convo->wheelCrop[0], convo->wheelCrop[1], convo->wheelCrop[2], convo->wheelCrop[3]}
                                                             : cwv::DefaultWheelCrop(gPanel.width, gPanel.height),
                                                gPanel.width, gPanel.height)
                                          : il::Pixels{};
    if (convoSubtitleWanted && convoSubPx.width > 0) {
        hudMasks.Add(convoSubPx.x, convoSubPx.y, convoSubPx.width, convoSubPx.height);
    }
    if (convoWheelWanted && convoWheelPx.width > 0) {
        hudMasks.Add(convoWheelPx.x, convoWheelPx.y, convoWheelPx.width, convoWheelPx.height);
    }
    // The HUD pieces on the body (research/41): on foot only, with the HUD on the panel, never with a wheel up. The wrist
    // needs the left controller tracked; the gun needs the mod's anchor (a drawn gun aimed along its barrel).
    hp::Settings const& hud = HudSettings(now);
    hp::Pieces const pieces = PiecesWanted();
    mevr::vr::controllers::State const pieceControllers = mevr::vr::controllers::Latest();
    bool const piecesAllowed = !wheelWanted && engaged && gameplay && gLastGameMode == 0 && frame.worldFrame && !frame.menuOpen &&
        frame.headValid && gPanel.texture != nullptr && gPanel.tintSrv != nullptr && device != nullptr;
    // In the Mako's cockpit (the mod's pieces say where its displays are): the vehicle's status (the weapon panel's band) and
    // the radar on them, the rest of the HUD panel at the cockpit's anchor, none over its screens (research/47).
    bool const cockpitHud = pieces.cockpit && !wheelWanted && engaged && gameplay && gLastGameMode == 1 && frame.worldFrame &&
        !frame.menuOpen && frame.headValid && gPanel.texture != nullptr && gPanel.tintSrv != nullptr && device != nullptr;
    if (cockpitHud != gCockpitHudShown) {
        gCockpitHudShown = cockpitHud;
        LogBoth(cockpitHud ? "[MEVR-HUD] the cockpit's HUD: the Mako's status and radar on its displays, the HUD panel at its anchor"
                           : "[MEVR-HUD] the cockpit's HUD is off: the HUD panel as before");
    }
    bool const wristWanted = piecesAllowed && hud.wristStatus && !gWristSwapchain.Refused() && pieceControllers.active &&
        pieceControllers.left.gripValid;
    bool const gunWanted = ((piecesAllowed && hud.gunStatus && pieces.gunVisible) || cockpitHud) && !gGunSwapchain.Refused();
    bool const radarWanted = cockpitHud && !gRadarSwapchain.Refused();
    hp::Pixels const wristPx = hp::ToPixels(hp::WristCrop(hud, gPanel.width, gPanel.height), gPanel.width, gPanel.height);
    hp::Pixels const gunPx = hp::ToPixels(hp::GunCrop(hud, gPanel.width, gPanel.height), gPanel.width, gPanel.height);
    hp::Pixels const radarPx = hp::ToPixels(hp::DefaultRadarCrop(gPanel.width, gPanel.height), gPanel.width, gPanel.height);
    if (wristWanted && wristPx.width > 0) {
        hudMasks.Add(wristPx.x, wristPx.y, wristPx.width, wristPx.height);
    }
    if (gunWanted && gunPx.width > 0) {
        hudMasks.Add(gunPx.x, gunPx.y, gunPx.width, gunPx.height);
    }
    if (radarWanted && radarPx.width > 0) {
        hudMasks.Add(radarPx.x, radarPx.y, radarPx.width, radarPx.height);
    }

    bool panelShown = false;
    bool galaxyPanel = false;  // the panel goes after the galaxy hologram's layer (over it)
    std::optional<mevr::host::world_ui::Shown> worldShown;
    if (worldUi && gPanel.texture != nullptr) {
        // The left wrist (where the HUD's wrist piece sits): a screen marked for it is projected from there (research/53 section 6).
        std::optional<XrVector3f> wrist;
        if (pieceControllers.active && pieceControllers.left.gripValid) {
            wrist = hp::WristPoint(pieceControllers.left.grip, hud.wristOffset);
        }
        worldShown = mevr::host::world_ui::Current(ToVec(frame.headPosition), ToQuat(frame.headOrientation), frame.headValid, gPanel.width,
            gPanel.height, wrist);
    }
    gPanelBackdrop = worldShown ? std::max(worldShown->backdrop, 1.0f / 256.0f) : 0.0f;
    gPanelFigure = worldShown && device != nullptr ? mevr::host::ui_world_figure::ForFrame(device.Get(), gPanelBackdrop) : std::nullopt;
    if (frame.worldFrame && engaged && uiToPanel && gPanel.texture != nullptr && count < capacity && device != nullptr &&
        (!worldUi || worldShown)) {
        gPanelDrawsLastFrame.store(draws, std::memory_order_relaxed);
        if (RenderPanel(context, device.Get(), frame.floatWhiteScale, worldUi ? Masks{} : hudMasks)) {
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
            hp::Quad cockpitPanel;
            if (cockpitHud && hp::Place(pieces.panelAnchor, ToVec(frame.headPosition), pieces.panelWidthM,
                                  static_cast<float>(gPanel.width) / static_cast<float>(gPanel.height), cockpitPanel, pieces.cockpitUp)) {
                gPanelQuad.pose = cockpitPanel.pose;  // the cockpit's place for what's left of the HUD, clear of its screens
                gPanelQuad.size = {cockpitPanel.width, cockpitPanel.height};
            }
            if (worldShown) {
                // The screen's shown part (the stage's content) of the panel image, where it stood when it opened; the game
                // pointer gets the whole window on its plane.
                auto const sw = static_cast<float>(gPanelSwapchain.width);
                auto const sh = static_cast<float>(gPanelSwapchain.height);
                gPanelQuad.subImage.imageRect = {{static_cast<int32_t>(worldShown->part.u0 * sw), static_cast<int32_t>(worldShown->part.v0 * sh)},
                    {std::max<int32_t>(1, static_cast<int32_t>((worldShown->part.u1 - worldShown->part.u0) * sw)),
                        std::max<int32_t>(1, static_cast<int32_t>((worldShown->part.v1 - worldShown->part.v0) * sh))}};
                gPanelQuad.pose = worldShown->panel.pose;
                gPanelQuad.size = {worldShown->panel.widthM, worldShown->panel.heightM};
                // Not while the bypass puzzle is played: any mouse motion on its screen turns its marker without end (a ray
                // anywhere on the window spun it, research/53 section 6.3); the omni-tool's d-pad plays it (plugin/OmniPad).
                if (!worldShown->puzzle) {
                    float const touchArea[4] = {worldShown->part.u0, worldShown->part.v0, worldShown->part.u1, worldShown->part.v1};
                    mevr::host::game_pointer::NoteGameUiQuad(worldShown->whole.pose, worldShown->whole.widthM, worldShown->whole.heightM,
                        MELEVR::MevrBridge::PanelKind::GameMenu, touchArea, worldShown->onWrist ? 0 : -1);
                } else {
                    mevr::host::game_pointer::HoldOffForPuzzle();  // its first frame forgets the quad at once
                }
            }
            if (galaxyUi) {
                float const uiWidth = mevr::host::galaxy_hologram::UiPanelWidthM(kGalaxyUiWidthM);
                gPanelQuad.pose = mevr::host::galaxy_hologram::UiPanelPose(ToVec(frame.headPosition));
                gPanelQuad.size = {uiWidth, uiWidth * static_cast<float>(gPanel.height) / static_cast<float>(gPanel.width)};
                galaxyPanel = true;
            } else {
                // A screen projected from the wrist: its fan of light first, under the panel.
                if (worldShown && worldShown->beam && count + 1 < capacity) {
                    mevr::host::wrist_beam::Frame beamFrame;
                    beamFrame.session = session;
                    beamFrame.appSpace = static_cast<XrSpace>(frame.appSpace);
                    beamFrame.context = context;
                    beamFrame.device = device.Get();
                    if (XrCompositionLayerQuad const* beam = mevr::host::wrist_beam::Layer(beamFrame, *worldShown->beam)) {
                        layersOut[count++] = beam;
                    }
                }
                layersOut[count++] = &gPanelQuad;
            }
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
    // The prompt's part of the UI on a quad at the object, facing the eyes, the same angular size at any distance.
    if (labelWanted && panelShown && labelPx.width > 0 && count < capacity) {
        il::Quad quad;
        float const aspect = static_cast<float>(labelPx.width) / static_cast<float>(labelPx.height);
        if (il::Place(label.position, ToVec(frame.headPosition), label.heightDeg, aspect, quad) &&
            EnsureSwapchain(gLabelSwapchain, labelPx.width, labelPx.height, "interaction label", "label") &&
            Blit(context, device.Get(), gLabelSwapchain, {labelPx.x, labelPx.y, labelPx.width, labelPx.height}, 1, Masks{},
                frame.floatWhiteScale) != nullptr) {
            gLabelQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gLabelQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gLabelQuad.space = static_cast<XrSpace>(frame.appSpace);
            gLabelQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gLabelQuad.subImage.swapchain = gLabelSwapchain.handle;
            gLabelQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(labelPx.width), static_cast<int32_t>(labelPx.height)}};
            gLabelQuad.pose = quad.pose;
            gLabelQuad.size = {quad.width, quad.height};
            layersOut[count++] = &gLabelQuad;
            if (!gLabel.shown || gLabel.serial != label.serial) {
                char line[320];
                std::snprintf(line, sizeof line,
                    "[MEVR-INTERACT] the prompt at the object: (%.2f, %.2f, %.2f) m, %.2f x %.2f m (%.1f deg tall); the UI's part "
                    "(%u, %u) %ux%u of %ux%u (%s)", quad.pose.position.x, quad.pose.position.y, quad.pose.position.z, quad.width,
                    quad.height, label.heightDeg, labelPx.x, labelPx.y, labelPx.width, labelPx.height, gPanel.width, gPanel.height,
                    label.crop[2] > 0.0f ? "[Interact] crop" : "the game's own place");
                LogBoth(line);
            }
            gLabel.shown = true;
            gLabel.serial = label.serial;
        }
    } else if (gLabel.shown) {
        gLabel.shown = false;
        LogBoth("[MEVR-INTERACT] the prompt at the object is gone");
    }
    // A conversation in the world (research/59): the speaker's line under their face, facing the eyes, the same angular size at
    // any distance (the interaction label's way); the dialogue wheel above a hand, placed where it opens (the game wheel's way).
    if (convoSubtitleWanted && panelShown && convoSubPx.width > 0 && count < capacity) {
        il::Quad quad;
        float const aspect = static_cast<float>(convoSubPx.width) / static_cast<float>(convoSubPx.height);
        XrVector3f const at{convo->subtitleAt[0], convo->subtitleAt[1], convo->subtitleAt[2]};
        if (il::Place(at, ToVec(frame.headPosition), convo->subtitleHeightDeg, aspect, quad) &&
            EnsureSwapchain(gLabelSwapchain, convoSubPx.width, convoSubPx.height, "conversation subtitle", "label") &&
            Blit(context, device.Get(), gLabelSwapchain, {convoSubPx.x, convoSubPx.y, convoSubPx.width, convoSubPx.height}, 1, Masks{},
                frame.floatWhiteScale) != nullptr) {
            gLabelQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gLabelQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gLabelQuad.space = static_cast<XrSpace>(frame.appSpace);
            gLabelQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gLabelQuad.subImage.swapchain = gLabelSwapchain.handle;
            gLabelQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(convoSubPx.width), static_cast<int32_t>(convoSubPx.height)}};
            gLabelQuad.pose = quad.pose;
            gLabelQuad.size = {quad.width, quad.height};
            layersOut[count++] = &gLabelQuad;
            if (!gConvoUi.subtitleShown || gConvoUi.speakerSerial != convo->speakerSerial) {
                char line[320];
                std::snprintf(line, sizeof line,
                    "[MEVR-CONVO] the subtitle at the speaker: (%.2f, %.2f, %.2f) m, %.2f x %.2f m (%.1f deg tall); the UI's part (%u, %u) "
                    "%ux%u of %ux%u (%s)", quad.pose.position.x, quad.pose.position.y, quad.pose.position.z, quad.width, quad.height,
                    convo->subtitleHeightDeg, convoSubPx.x, convoSubPx.y, convoSubPx.width, convoSubPx.height, gPanel.width, gPanel.height,
                    convo->subtitleCrop[2] > 0.0f ? "[Cinematics] subtitleCrop" : "the default band");
                LogBoth(line);
            }
            gConvoUi.subtitleShown = true;
            gConvoUi.speakerSerial = convo->speakerSerial;
        }
    } else if (gConvoUi.subtitleShown) {
        gConvoUi.subtitleShown = false;
        LogBoth("[MEVR-CONVO] the subtitle at the speaker is gone");
    }
    XrCompositionLayerQuad const* convoWheelLayer = nullptr;
    if (convoWheelWanted && panelShown && convoWheelPx.width > 0) {
        if (!gConvoUi.wheelShown || gConvoUi.conversation != convo->conversation) {
            XrPosef headPose{};
            headPose.orientation = head;
            headPose.position = ToVec(frame.headPosition);
            char const* from = "";
            gConvoUi.wheelPose = PlaceWheel(convo->wheelHand, std::clamp(convo->wheelDistanceM, 0.05f, 0.8f), headPose, from);
            char line[320];
            std::snprintf(line, sizeof line,
                "[MEVR-CONVO] the dialogue wheel %.2f m from %s, at (%.2f, %.2f, %.2f) m, %.2f m wide; the UI's part (%u, %u) %ux%u of %ux%u (%s)",
                convo->wheelDistanceM, from, gConvoUi.wheelPose.position.x, gConvoUi.wheelPose.position.y, gConvoUi.wheelPose.position.z,
                convo->wheelWidthM, convoWheelPx.x, convoWheelPx.y, convoWheelPx.width, convoWheelPx.height, gPanel.width, gPanel.height,
                convo->wheelCrop[2] > 0.0f ? "[Cinematics] wheelCrop" : "the default band");
            LogBoth(line);
        }
        gConvoUi.wheelShown = true;
        gConvoUi.conversation = convo->conversation;
        UINT const factor = std::clamp<UINT>(static_cast<UINT>(std::lround(convoWheelPx.width / 1024.0)), 1u, 4u);
        std::uint32_t const width = std::max<std::uint32_t>(convoWheelPx.width / factor, 16);
        std::uint32_t const height = std::max<std::uint32_t>(convoWheelPx.height / factor, 16);
        if (EnsureSwapchain(gWheelSwapchain, width, height, "conversation wheel", "wheel") &&
            Blit(context, device.Get(), gWheelSwapchain, {convoWheelPx.x, convoWheelPx.y, convoWheelPx.width, convoWheelPx.height}, factor,
                Masks{}, frame.floatWhiteScale) != nullptr) {
            float const quadWidth = std::clamp(convo->wheelWidthM, 0.15f, 1.5f);
            gWheelQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gWheelQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gWheelQuad.space = static_cast<XrSpace>(frame.appSpace);
            gWheelQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gWheelQuad.subImage.swapchain = gWheelSwapchain.handle;
            gWheelQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(width), static_cast<int32_t>(height)}};
            gWheelQuad.pose = gConvoUi.wheelPose;
            gWheelQuad.size = {quadWidth, quadWidth * static_cast<float>(convoWheelPx.height) / static_cast<float>(convoWheelPx.width)};
            convoWheelLayer = &gWheelQuad;
        }
    } else if (gConvoUi.wheelShown) {
        gConvoUi.wheelShown = false;
        LogBoth("[MEVR-CONVO] the dialogue wheel is gone");
    }
    // The player's status on the back of the left wrist: a small quad a fixed offset from the left grip pose, facing the
    // eyes, upright (research/41).
    XrVector3f const headPosition = ToVec(frame.headPosition);
    if (wristWanted && panelShown && wristPx.width > 0 && count < capacity) {
        XrVector3f const at = hp::WristPoint(pieceControllers.left.grip, hud.wristOffset);
        if (ShowPiece(context, device.Get(), gWristSwapchain, gWristQuad, "wrist status", "wrist", wristPx, at, headPosition,
                hud.wristWidthM, {0.0f, 0.0f, 0.0f}, frame.appSpace, frame.floatWhiteScale)) {
            layersOut[count++] = &gWristQuad;
            if (!gPieces.wristShown || now >= gPieces.nextLogMs) {
                char line[320];
                std::snprintf(line, sizeof line,
                    "[MEVR-HUD] the player's status on the left wrist: (%.2f, %.2f, %.2f) m, %.3f x %.3f m; the UI's part (%u, %u) %ux%u "
                    "of %ux%u (%s)", gWristQuad.pose.position.x, gWristQuad.pose.position.y, gWristQuad.pose.position.z,
                    gWristQuad.size.width, gWristQuad.size.height, wristPx.x, wristPx.y, wristPx.width, wristPx.height, gPanel.width,
                    gPanel.height, hud.wristCrop.Valid() ? "[Hud] wristCrop" : "the measured band");
                LogBoth(line);
                gPieces.nextLogMs = now + 60000;
            }
            gPieces.wristShown = true;
        }
    } else if (gPieces.wristShown) {
        gPieces.wristShown = false;
        LogBoth(std::string("[MEVR-HUD] the player's status is back on the HUD panel") +
            (!pieceControllers.active || !pieceControllers.left.gripValid ? " (the left controller isn't tracked)" : ""));
    }
    // The weapon panel on the gun, where the mod says the held gun is (above the receiver, behind the sights), facing the
    // eyes, leaning with the gun's top.
    if (gunWanted && panelShown && gunPx.width > 0 && count < capacity) {
        if (ShowPiece(context, device.Get(), gGunSwapchain, gGunQuad, "gun status", "gun", gunPx, cockpitHud ? pieces.statusAnchor : pieces.gunAnchor,
                headPosition, cockpitHud ? pieces.statusWidthM : hud.gunWidthM, cockpitHud ? pieces.cockpitUp : pieces.gunUp, frame.appSpace,
                frame.floatWhiteScale)) {
            layersOut[count++] = &gGunQuad;
            if (!gPieces.gunShown || gPieces.gunSerial != pieces.serial) {
                char line[320];
                std::snprintf(line, sizeof line,
                    "[MEVR-HUD] the weapon panel on the gun: (%.2f, %.2f, %.2f) m, %.3f x %.3f m; the UI's part (%u, %u) %ux%u of %ux%u "
                    "(%s)", gGunQuad.pose.position.x, gGunQuad.pose.position.y, gGunQuad.pose.position.z, gGunQuad.size.width,
                    gGunQuad.size.height, gunPx.x, gunPx.y, gunPx.width, gunPx.height, gPanel.width, gPanel.height,
                    hud.gunCrop.Valid() ? "[Hud] gunCrop" : "the measured band");
                LogBoth(line);
            }
            gPieces.gunShown = true;
            gPieces.gunSerial = pieces.serial;
        }
    } else if (gPieces.gunShown) {
        gPieces.gunShown = false;
        LogBoth("[MEVR-HUD] the weapon panel is back on the HUD panel");
    }
    // The Mako's radar on the cockpit's display (research/47).
    if (radarWanted && panelShown && radarPx.width > 0 && count < capacity) {
        if (ShowPiece(context, device.Get(), gRadarSwapchain, gRadarQuad, "cockpit radar", "radar", radarPx, pieces.radarAnchor, headPosition,
                pieces.radarWidthM, pieces.cockpitUp, frame.appSpace, frame.floatWhiteScale)) {
            layersOut[count++] = &gRadarQuad;
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
            Blit(context, device.Get(), gWheelSwapchain, {cropPx.x, cropPx.y, cropPx.width, cropPx.height}, factor, Masks{},
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
    bool const armNext = engaged && uiToPanel && frame.worldFrame && (panelShown || gPanel.texture == nullptr);
    if (armNext != gArmed.load(std::memory_order_relaxed)) {
        mevr::log::Info("overlays: game UI goes to {}", !armNext ? "the eye images" : galaxyUi ? "a panel over the galaxy hologram's table"
                                                                                                : worldUi ? "a screen standing in the room"
                                                                                                : "the body-locked HUD panel");
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
    sightFrame.pairArm = frame.pairArm;  // the scope capture's pose for this pair (research/39)
    mevr::host::spectator::NoteFrame(frame.pairArm, frame.floatWhiteScale);  // and the spectator camera's (research/49)
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
    }
    mevr::host::sight_layers::Output const sight = mevr::host::sight_layers::Layers(sightFrame);
    if (sight.looking && sight.replaceRing) {
        gSightSeenMs = now;
    }
    bool const sightHidesRing = gSightSeenMs != 0 && now - gSightSeenMs < 300;

    // ---- The Mako's lever and stick (research/46): under the reticle ----
    {
        mc::View const view = mc::Latest();
        if (view.size != 0 && view.size != sizeof(mc::View) && view.size != gMakoViewSize) {
            gMakoViewSize = view.size;
            mevr::log::Warn("overlays: the mod's Mako controls are {} bytes, the host's {}: not drawn; rebuild both", view.size, sizeof(mc::View));
        }
        bool const wanted = view.size == sizeof(mc::View) && view.visible && frame.worldFrame && !frame.menuOpen && frame.headValid &&
            gLastGameMode == 1 && device != nullptr;
        bool shown = false;
        if (wanted && count + 2 <= capacity) {
            bool const lever = !gLeverQuad.swapchain.Refused() &&
                UpdateControl(context, device.Get(), gLeverQuad, mc::kLeverPixelsWide, mc::kLeverPixelsLong,
                    mc::LeverKey(view.lever, view.detent, view.leverNear, view.leverHeld), "Mako lever", "makolever",
                    [&view] { return mc::LeverPixels(view.lever, view.travelM, view.detent, view.leverNear, view.leverHeld); });
            bool const stick = !gStickQuad.swapchain.Refused() &&
                UpdateControl(context, device.Get(), gStickQuad, mc::kStickPixels, mc::kStickPixels,
                    mc::StickKey(view.steer, view.stickNear, view.stickHeld), "Mako stick", "makostick",
                    [&view] { return mc::StickPixels(view.steer, view.stickNear, view.stickHeld); });
            if (lever) {
                PlaceControl(gLeverQuad, view.leverBase, view.forward, mc::kLeverPixelsWide, mc::kLeverPixelsLong, mc::kLeverWidthM, mc::kLeverLengthM, frame.appSpace);
                layersOut[count++] = &gLeverQuad.quad;
            }
            if (stick) {
                PlaceControl(gStickQuad, view.stickBase, view.forward, mc::kStickPixels, mc::kStickPixels, mc::kStickSizeM, mc::kStickSizeM, frame.appSpace);
                layersOut[count++] = &gStickQuad.quad;
            }
            shown = lever || stick;
        }
        if (shown != gMakoControlsShown) {
            gMakoControlsShown = shown;
            if (shown) {
                char line[256];
                std::snprintf(line, sizeof line, "[MEVR-MAKO] the lever and the stick are drawn: lever at (%.2f, %.2f, %.2f), stick at (%.2f, %.2f, %.2f) m",
                    view.leverBase.x, view.leverBase.y, view.leverBase.z, view.stickBase.x, view.stickBase.y, view.stickBase.z);
                LogBoth(line);
            } else {
                LogBoth("[MEVR-MAKO] the lever and the stick are gone");
            }
        }
    }

    // ---- Hand reticle ----
    // Where the weapon points: the mod's aim ray (muzzle and barrel) once it publishes one, else the
    // right controller's aim pose. While it has a ray, the game's screen-centre crosshair is hidden
    // (held half a second so a flickering ray doesn't make it blink).
    // On foot; in the Mako (game mode 1, research/44) only on a ray the mod publishes (the controller's, while it aims the
    // turret), never the controller fallback: the game's own turret aim shows its own crosshair otherwise.
    mevr::vr::aim_ray::Ray const ray = mevr::vr::aim_ray::Latest();
    bool const producer = ray.sequence != 0 && now - ray.timeMs < 1000;
    bool const makoRay = gLastGameMode == 1 && producer && ray.valid;
    bool const reticleMode = cfg.handReticle && handAim && (gLastGameMode == 0 || makoRay);
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

    // ---- The galaxy hologram (plugin/GalaxyHologram, research/51 section 8): the map on a table, over the game's flat screen;
    // before the game pointer, which leaves the trigger alone while a controller points at the table ----
    {
        mevr::host::galaxy_hologram::Frame galaxy;
        galaxy.session = session;
        galaxy.appSpace = static_cast<XrSpace>(frame.appSpace);
        galaxy.context = context;
        galaxy.device = device.Get();
        galaxy.gameMode = frame.gameMode;
        galaxy.menuOpen = frame.menuOpen;
        galaxy.headValid = frame.headValid;
        galaxy.head = {ToQuat(frame.headOrientation), ToVec(frame.headPosition)};
        for (int e = 0; e < 2; ++e) {
            galaxy.eyes[e] = {ToQuat(frame.eyeOrientation[e]), ToVec(frame.eyePosition[e])};
            XrFovf const fov{frame.projectionFov[e][0], frame.projectionFov[e][1], frame.projectionFov[e][2], frame.projectionFov[e][3]};
            bool const sane = fov.angleRight - fov.angleLeft > 0.5f && fov.angleUp - fov.angleDown > 0.5f;
            galaxy.fov[e] = sane ? fov : XrFovf{-0.785398f, 0.785398f, 0.785398f, -0.785398f};
        }
        galaxy.eyeWidth = frame.projectionRect[0][2];
        galaxy.eyeHeight = frame.projectionRect[0][3];
        if (XrCompositionLayerBaseHeader const* layer = mevr::host::galaxy_hologram::Layer(galaxy); layer != nullptr && count < capacity) {
            layersOut[count++] = layer;
        }
        // Squad placement's arc and ring while the right B is held (game/SquadPlace, HostServices v32).
        {
            mevr::host::squad_place_draw::Frame place;
            place.session = galaxy.session;
            place.appSpace = galaxy.appSpace;
            place.context = galaxy.context;
            place.device = galaxy.device;
            place.headValid = galaxy.headValid;
            place.head = galaxy.head;
            for (int e = 0; e < 2; ++e) {
                place.eyes[e] = galaxy.eyes[e];
                place.fov[e] = galaxy.fov[e];
            }
            place.eyeWidth = galaxy.eyeWidth;
            place.eyeHeight = galaxy.eyeHeight;
            if (XrCompositionLayerBaseHeader const* layer = mevr::host::squad_place_draw::Layer(place); layer != nullptr && count < capacity) {
                layersOut[count++] = layer;
            }
        }
        // The game's own map as a stereo window over the table (the mod's captures), under the game's UI.
        XrCompositionLayerBaseHeader const* window[2] = {};
        int const windowLayers = mevr::host::galaxy_hologram::WindowLayers(galaxy, window);
        for (int i = 0; i < windowLayers && count < capacity; ++i) {
            layersOut[count++] = window[i];
        }
        // The game's UI over the table (its travel question; with the window, its whole map: labels, title, buttons): over the
        // hologram, and the game pointer's while the game asks or the window shows the map.
        if (galaxyPanel && count < capacity) {
            layersOut[count++] = &gPanelQuad;
            if (mevr::host::galaxy_hologram::AskingTravel() || mevr::host::galaxy_hologram::WindowOn()) {
                mevr::host::game_pointer::NoteGameUiQuad(gPanelQuad.pose, gPanelQuad.size.width, gPanelQuad.size.height);
            }
        }
    }

    // ---- The game pointer (plugin/GamePointer, research/42): on a flat frame (the game's menus, the galaxy map), a
    // controller's ray on the flat screen is the game's mouse; its dot sits on the quad, above it in layer order.
    // Stepped every frame (it releases what it holds when the flat screen goes away) ----
    {
        mevr::host::game_pointer::Frame pointerFrame;
        pointerFrame.appSpace = static_cast<XrSpace>(frame.appSpace);
        pointerFrame.halcyonMenuOpen = frame.menuOpen;
        pointerFrame.headValid = frame.headValid;
        pointerFrame.head = ToVec(frame.headPosition);
        mevr::host::game_pointer::Dot dot;
        if (mevr::host::game_pointer::Step(pointerFrame, dot) && count < capacity && device != nullptr &&
            EnsurePointerDot(context, device.Get())) {
            gPointerDotQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
            gPointerDotQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
            gPointerDotQuad.space = static_cast<XrSpace>(frame.appSpace);
            gPointerDotQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            gPointerDotQuad.subImage.swapchain = gPointerDotSwapchain.handle;
            gPointerDotQuad.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(kPointerDotSize), static_cast<int32_t>(kPointerDotSize)}};
            gPointerDotQuad.pose = dot.pose;
            float const size = dot.pressed ? dot.size * 0.8f : dot.size;  // a press shrinks it a little
            gPointerDotQuad.size = {size, size};
            layersOut[count++] = &gPointerDotQuad;
            if (!gLoggedPointerDot) {
                gLoggedPointerDot = true;
                mevr::log::Info("overlays: first game pointer dot at ({:.2f}, {:.2f}, {:.2f}) m, {:.3f} m across", dot.pose.position.x,
                    dot.pose.position.y, dot.pose.position.z, dot.size);
            }
        }
    }

    // ---- The power wheel at the left hand (plugin/PowerWheel, research/55), its hint card, the lock's marker: on top of ours ----
    if (count < capacity) {
        mevr::host::power_wheel::Frame wheel;
        wheel.session = session;
        wheel.appSpace = static_cast<XrSpace>(frame.appSpace);
        wheel.context = context;
        wheel.device = device.Get();
        wheel.worldFrame = frame.worldFrame;
        wheel.menuOpen = frame.menuOpen;
        wheel.head = ToVec(frame.headPosition);
        wheel.headValid = frame.headValid;
        XrCompositionLayerQuad const* wheelLayers[3] = {};
        int const n = mevr::host::power_wheel::Layers(wheel, wheelLayers, static_cast<int>(std::min<unsigned>(3u, capacity - count)));
        for (int i = 0; i < n; ++i) {
            layersOut[count++] = wheelLayers[i];
        }
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
    // ---- The game's wheel at the hand: last, on top (the conversation's wheel likewise, research/59) ----
    if (convoWheelLayer != nullptr && count < capacity) {
        layersOut[count++] = convoWheelLayer;
    }
    if (wheelLayer != nullptr && count < capacity) {
        layersOut[count++] = wheelLayer;
        if (!gWheel.loggedThisOpening) {
            gWheel.loggedThisOpening = true;
            mevr::log::Info("overlays: wheel panel submitted ({}x{}, {:.2f} x {:.2f} m)", gWheelSwapchain.width, gWheelSwapchain.height,
                gWheelQuad.size.width, gWheelQuad.size.height);
        }
    }

    // ---- The comfort vignette (plugin/Vignette, research/31): head-locked, under all of ours (first), so the HUD,
    // the reticle and the sight stay clear. Asked for last, so it never takes a slot from the others (when it shows,
    // the player is turning or walking: no squad menu, no wheel). Nothing at all while it isn't wanted ----
    if (count < capacity) {
        mevr::host::vignette::Frame vignette;
        vignette.session = session;
        vignette.context = context;
        vignette.device = device.Get();
        vignette.worldFrame = frame.worldFrame;
        vignette.menuOpen = frame.menuOpen;
        vignette.fovValid = frame.projectionValid;
        std::memcpy(vignette.fov, frame.projectionFov, sizeof vignette.fov);
        if (XrCompositionLayerQuad const* layer = mevr::host::vignette::Layer(vignette)) {
            for (unsigned i = count; i > 0; --i) {
                layersOut[i] = layersOut[i - 1];
            }
            layersOut[0] = layer;
            ++count;
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
#if defined(MEVR_WITH_MAIN_MENU)
        // Our menu hides the first-run prompt as Halcyon's does: its delay and timeout wait for it to close.
        calibration.menuOpen = frame.menuOpen || mevr::host::main_menu::IsOpen();
#else
        calibration.menuOpen = frame.menuOpen;
#endif
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
    // ---- The omni-tool's rail (plugin/OmniToolRail, research/57): beside the game's screen on the wrist, or alone on the wrist
    // when the player taps it open ----
    {
        mevr::host::omni_rail::Frame rail;
        rail.session = session;
        rail.appSpace = static_cast<XrSpace>(frame.appSpace);
        rail.device = device.Get();
        rail.context = context;
        rail.headValid = frame.headValid;
        rail.eyes = ToVec(frame.headPosition);
        rail.screen = worldShown;
        rail.display = mevr::vr::world_ui::LatestEmitter(400);  // the mod sends it every tick while the rail stands alone
        if (pieceControllers.active && pieceControllers.left.gripValid) {
            rail.wristValid = true;
            rail.wrist = hp::WristPoint(pieceControllers.left.grip, hud.wristOffset);
        }
        if (pieces.bodyValid) {
            rail.body = mevr::vr::omni_rail::Body{pieces.leftWrist, pieces.leftElbow, pieces.rightIndex1, pieces.rightIndex2};
        }
        rail.gunDrawn = pieces.gunVisible;
        rail.gameMode = gLastGameMode;
        rail.othersOwnInput = frame.menuOpen || mevr::host::main_menu::IsOpen() || mevr::host::calibration::Active();
        if (XrCompositionLayerQuad const* layer = mevr::host::omni_rail::Layer(rail); layer != nullptr && count < capacity) {
            layersOut[count++] = layer;
        }
        if (XrCompositionLayerQuad const* target = mevr::host::omni_rail::TargetLayer(); target != nullptr && count < capacity) {
            layersOut[count++] = target;  // the wrist tap's target, over the wrist readout's layer
            static bool logged = false;
            if (!logged) {
                logged = true;
                mevr::log::Info("omni-tool rail: the wrist tap's target goes in as our quad {} of {}", count, capacity);
            }
        }
    }
    // ---- The bypass puzzle's d-pad (plugin/OmniPad, research/53 section 6.3): to the right of its screen while it's played ----
    {
        mevr::host::omni_pad::Frame padFrame;
        padFrame.session = session;
        padFrame.appSpace = static_cast<XrSpace>(frame.appSpace);
        padFrame.device = device.Get();
        padFrame.context = context;
        padFrame.screen = worldShown;
        padFrame.othersOwnInput = frame.menuOpen || mevr::host::main_menu::IsOpen() || mevr::host::calibration::Active();
        if (XrCompositionLayerQuad const* layer = mevr::host::omni_pad::Layer(padFrame); layer != nullptr && count < capacity) {
            layersOut[count++] = layer;
        }
    }
    // ---- Our main menu (plugin/MainMenu): while it's open, its quad is the only one of ours (SteamVR may sort quads by
    // distance, and nothing of the game's is for now). Closed, it returns at once ----
    {
        mevr::host::main_menu::Frame menu;
        menu.session = session;
        menu.appSpace = static_cast<XrSpace>(frame.appSpace);
        menu.context = context;
        menu.device = device.Get();
        menu.worldFrame = frame.worldFrame;
        menu.gameMode = gLastGameMode;  // the first run waits for on foot
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
