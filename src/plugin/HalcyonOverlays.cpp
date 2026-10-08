// Our quads on top of Halcyon's world layer (seams in src/halcyon/le1/mevr_bridge.h):
// - the body-locked HUD panel: d3d_capture.cpp's draw hooks send the game's UI draws of a
//   head-tracked frame into a panel texture instead of the eye images; here that texture goes into a
//   swapchain and becomes a quad fixed in the tracking space in front of the player's body;
// - the hand reticle: a small ring on the weapon's aim ray (or the right controller's aim pose
//   until the mod publishes the ray), replacing the game's screen-centre crosshair;
// - [MENUVIS] readbacks that tell whether Halcyon's Insert menu texture has visible pixels.

#include "halcyon/le1/mevr_bridge.h"
#include "halcyon/le1/logger.h"
#include "halcyon/le1/pchud.h"
#include "halcyon/le1/vr_config.h"

#include "core/Log.hpp"
#include "core/vr/AimRay.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/OverlayPlacement.hpp"

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
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool failed = false;  // creation failed for this size/format: stop retrying every draw
};
PanelTexture gPanel;
std::atomic<bool> gArmed{false};
std::atomic<unsigned> gPanelDraws{0};
std::atomic<unsigned> gPanelDrawsLastFrame{0};
std::atomic<bool> gPanelIsFloat{false};
bool gPanelNeedsClear = false;

struct BlendEntry {
    ID3D11BlendState* game = nullptr;  // AddRef'd, so the address can't be reused for another state
    ComPtr<ID3D11BlendState> ours;
};
std::vector<BlendEntry> gBlends;  // render thread only; game blend states are few and long-lived

bool IsFloatFormat(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R32G32B32A32_FLOAT ||
        f == DXGI_FORMAT_R11G11B10_FLOAT;
}
bool SamplesLinear(DXGI_FORMAT f) {
    return IsFloatFormat(f) || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
}

// ---- Shaders ------------------------------------------------------------------------------------
// The panel blit: box-filters the panel texture down by an integer factor (premultiplied, so the
// filter is exact), then writes linear premultiplied colour through the swapchain's sRGB view.
// 8-bit UNORM panels hold the game's gamma-encoded blend: un-premultiply, decode, re-premultiply.
// Float (scRGB) panels: scale so SDR white is 1.0 and clip keeping the hue, like the eye blit.
constexpr char kPanelShader[] = R"(
Texture2D<float4> src : register(t0);
cbuffer Params : register(b0) { uint4 info; float4 params; };
struct VSOut { float4 pos : SV_Position; };
VSOut VSMain(uint id : SV_VertexID) {
    float2 p = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);
    VSOut o; o.pos = float4(p, 0.0, 1.0); return o;
}
float4 PSMain(VSOut i) : SV_Target {
    uint f = max(info.x, 1u);
    int2 base = int2(i.pos.xy) * int(f);
    float4 sum = 0;
    [loop] for (uint y = 0; y < f; ++y)
        [loop] for (uint x = 0; x < f; ++x)
            sum += src.Load(int3(min(base + int2(x, y), int2(info.yz) - 1), 0));
    float4 s = sum / float(f * f);
    float a = saturate(s.a);
    if (a < 1.0 / 1024.0) return float4(0, 0, 0, 0);
    float3 c = max(s.rgb, 0.0) / a;
    if (params.x > 0.5) {
        c *= params.y;
        c /= max(1.0, max(c.r, max(c.g, c.b)));
    } else {
        c = saturate(c);
        c = (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
    }
    return float4(c * a, a);
}
)";

struct PanelParams {
    std::uint32_t info[4];  // factor, source width, source height, 0
    float params[4];        // linear source (1/0), white scale, 0, 0
};

using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR,
    LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

bool Compile(char const* entry, char const* profile, ComPtr<ID3DBlob>& out) {
    static D3DCompileFn compile = [] {
        HMODULE module = LoadLibraryW(L"d3dcompiler_47.dll");
        return module != nullptr ? reinterpret_cast<D3DCompileFn>(GetProcAddress(module, "D3DCompile")) : nullptr;
    }();
    if (compile == nullptr) {
        mevr::log::Error("overlays: d3dcompiler_47.dll unavailable; no HUD panel");
        return false;
    }
    ComPtr<ID3DBlob> errors;
    HRESULT const hr = compile(kPanelShader, sizeof(kPanelShader) - 1, "MEVR_HudPanel", nullptr, nullptr, entry,
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
    if (!Compile("VSMain", "vs_5_0", vsBlob) || !Compile("PSMain", "ps_5_0", psBlob)) {
        return false;
    }
    bool ok = SUCCEEDED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr,
                  gPipeline.vs.GetAddressOf())) &&
        SUCCEEDED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
            gPipeline.ps.GetAddressOf()));
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
        c_->PSGetShaderResources(0, 1, &srv_);
        c_->PSGetConstantBuffers(0, 1, &cb_);
    }
    ~StateGuard() {
        ID3D11ShaderResourceView* none = nullptr;
        c_->PSSetShaderResources(0, 1, &none);
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
        c_->PSSetShaderResources(0, 1, &srv_);
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
        Release(srv_);
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
    ID3D11ShaderResourceView* srv_ = nullptr;
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
std::uint64_t gNextPanelLogMs = 0;

bool RenderPanel(ID3D11DeviceContext* context, ID3D11Device* device, float whiteScale) {
    if (!EnsurePipeline(device) || gPanel.srv == nullptr) {
        return false;
    }
    UINT const factor = std::clamp<UINT>(static_cast<UINT>(std::lround(gPanel.width / 1400.0)), 2u, 4u);
    std::uint32_t const width = std::max<std::uint32_t>(gPanel.width / factor, 16);
    std::uint32_t const height = std::max<std::uint32_t>(gPanel.height / factor, 16);
    if (gPanelSwapchain.handle != XR_NULL_HANDLE && (gPanelSwapchain.width != width || gPanelSwapchain.height != height)) {
        gPanelSwapchain.Destroy();
    }
    if (gPanelSwapchain.handle == XR_NULL_HANDLE) {
        if (gPanelSwapchain.failed || !CreateSwapchain(gSession, width, height, gPanelSwapchain, "HUD panel")) {
            return false;
        }
    }
    gPanelFactor = factor;
    std::uint32_t index = 0;
    if (!AcquireImage(gPanelSwapchain, index)) {
        return false;
    }
    bool drew = false;
    ComPtr<ID3D11RenderTargetView>& rtv = gPanelSwapchain.rtvs[index];
    if (rtv == nullptr) {
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = static_cast<DXGI_FORMAT>(kSwapchainFormat);
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(gPanelSwapchain.images[index], &rd, rtv.GetAddressOf());
    }
    if (rtv != nullptr) {
        PanelParams params{{factor, gPanel.width, gPanel.height, 0},
            {SamplesLinear(gPanel.format) ? 1.0f : 0.0f, IsFloatFormat(gPanel.format) ? whiteScale : 1.0f, 0.0f, 0.0f}};
        context->UpdateSubresource(gPipeline.params.Get(), 0, nullptr, &params, 0, 0);
        StateGuard guard(context);
        D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
        float const blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        ID3D11RenderTargetView* target = rtv.Get();
        ID3D11ShaderResourceView* source = gPanel.srv.Get();
        ID3D11Buffer* constants = gPipeline.params.Get();
        context->OMSetRenderTargets(1, &target, nullptr);
        context->OMSetBlendState(gPipeline.opaque.Get(), blendFactor, 0xFFFFFFFFu);
        context->OMSetDepthStencilState(gPipeline.noDepth.Get(), 0);
        context->RSSetState(gPipeline.raster.Get());
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(gPipeline.vs.Get(), nullptr, 0);
        context->PSSetShader(gPipeline.ps.Get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        context->PSSetShaderResources(0, 1, &source);
        context->PSSetConstantBuffers(0, 1, &constants);
        context->Draw(3, 0);
        drew = true;
    }
    if (drew && !gProbedPanel && gPanelDrawsLastFrame.load(std::memory_order_relaxed) > 0) {
        gProbedPanel = true;  // one look at what the headset gets: the panel image after the blit
        ProbeTextureAlpha(context, gPanelSwapchain.images[index], "HUD panel swapchain image (first frame with UI)");
        ProbeTextureAlpha(context, gPanel.texture.Get(), "HUD panel texture (the game's UI draws)");
    }
    ReleaseImage(gPanelSwapchain);
    return drew;
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
    if (device == nullptr || FAILED(device->CreateTexture2D(&td, nullptr, gPanel.texture.GetAddressOf())) ||
        FAILED(device->CreateRenderTargetView(gPanel.texture.Get(), nullptr, gPanel.rtv.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(gPanel.texture.Get(), nullptr, gPanel.srv.GetAddressOf()))) {
        mevr::log::Error("overlays: HUD panel texture {}x{} format {} failed; the HUD stays in the frame", width,
            height, format);
        PanelTexture const failed{{}, {}, {}, width, height, fmt, true};
        gPanel = failed;
        return nullptr;
    }
    float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->ClearRenderTargetView(gPanel.rtv.Get(), clear);
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

bool HudPanelIsFloat() noexcept {
    return gPanelIsFloat.load(std::memory_order_relaxed);
}

void OnRecentered() noexcept {
    gRecentered.store(true, std::memory_order_release);
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
        gSession = session;
    }
    auto const& cfg = MELEVR::Config::Get();
    std::uint64_t const now = GetTickCount64();
    float const dt = gLastFrameMs == 0 ? 0.0f : static_cast<float>(now - gLastFrameMs) / 1000.0f;
    gLastFrameMs = now;

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

    bool panelShown = false;
    if (frame.worldFrame && engaged && gameplay && gPanel.texture != nullptr && count < capacity && device != nullptr) {
        gPanelDrawsLastFrame.store(draws, std::memory_order_relaxed);
        if (RenderPanel(context, device.Get(), frame.floatWhiteScale)) {
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
    if (gPanel.rtv != nullptr && (draws > 0 || gPanelNeedsClear)) {
        float const clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->ClearRenderTargetView(gPanel.rtv.Get(), clear);
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
    if (source != 0 && frame.worldFrame && !frame.menuOpen && frame.headValid && count < capacity && device != nullptr) {
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
    return count;
}

} // namespace MELEVR::MevrBridge
