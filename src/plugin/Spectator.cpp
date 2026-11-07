// SPDX-License-Identifier: MIT
// The spectator camera's host side (plugin/Spectator.hpp, research/49). Present (render) thread.

#include "plugin/Spectator.hpp"

#include "core/Log.hpp"
#include "core/vr/ScopeCapture.hpp"
#include "core/vr/Spectator.hpp"
#include "core/vr/TestRequest.hpp"
#include "plugin/ScopeCaptureSource.hpp"
#include "plugin/TestLoop.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace mevr::host::spectator {
namespace {

using Microsoft::WRL::ComPtr;
namespace sp = vr::spectator;
namespace sc = vr::scope_capture;

constexpr unsigned kResolveTries = 240;      // frames per render target before giving up on finding its texture
constexpr std::uint32_t kWarmupTicks = 3;    // without a pair arm: ticks after enabling before the texture counts
constexpr std::uint64_t kPictureFreshMs = 200;
constexpr double kStatsSeconds = 10.0;

// ---- Shaders ----------------------------------------------------------------------------------------------------------
char const kShader[] = R"(
cbuffer Params : register(b0) {
    float4 corners[4];  // the quad's corners in clip space: top left, top right, bottom left, bottom right
    float4 uvRect;      // u0, v0, u1, v1
    float4 mode;        // x: copy: decode gamma (> 1), present: 1 = encode sRGB; y: scale; z: 1 = opaque alpha
};
Texture2D tex : register(t0);
SamplerState samp : register(s0);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V VSFull(uint id : SV_VertexID) {
    V o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = uv;
    return o;
}
V VSQuad(uint id : SV_VertexID) {
    static const uint order[6] = {0, 1, 2, 2, 1, 3};
    uint c = order[id];
    V o;
    o.pos = corners[c];
    o.uv = lerp(uvRect.xy, uvRect.zw, float2(c & 1, c >> 1));
    return o;
}
float3 LinearToSrgb(float3 c) {
    c = saturate(c);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}
float4 PSCopy(V i) : SV_Target {
    float3 c = max(tex.Sample(samp, i.uv).rgb, 0.0);
    if (mode.x > 1.0) c = pow(c, mode.x);
    return float4(c * mode.y, 1.0);
}
float4 PSQuad(V i) : SV_Target {
    float4 c = tex.Sample(samp, i.uv);
    c.rgb *= mode.y;
    if (mode.z > 0.5) c.a = 1.0;
    return c;
}
float4 PSPresent(V i) : SV_Target {
    float3 c = max(tex.Sample(samp, i.uv).rgb, 0.0) * mode.y;
    if (mode.x > 0.5) c = LinearToSrgb(c);
    return float4(c, 1.0);
}
)";

struct Params {
    float corners[4][4];
    float uvRect[4];
    float mode[4];
};

using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, D3D_SHADER_MACRO const*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
    ID3DBlob**, ID3DBlob**);

bool Compile(char const* entry, char const* profile, ComPtr<ID3DBlob>& out) {
    static D3DCompileFn const compile = [] {
        HMODULE const module = LoadLibraryW(L"d3dcompiler_47.dll");
        return module != nullptr ? reinterpret_cast<D3DCompileFn>(GetProcAddress(module, "D3DCompile")) : nullptr;
    }();
    if (compile == nullptr) {
        log::Error("spectator: d3dcompiler_47.dll unavailable; the desktop window keeps Halcyon's mirror");
        return false;
    }
    ComPtr<ID3DBlob> errors;
    if (FAILED(compile(kShader, sizeof(kShader) - 1, "MEVR_Spectator", nullptr, nullptr, entry, profile, 0x8000 /* OPTIMIZATION_LEVEL3 */, 0,
            out.ReleaseAndGetAddressOf(), errors.GetAddressOf()))) {
        log::Error("spectator: shader {} failed to compile: {}", entry, errors ? static_cast<char const*>(errors->GetBufferPointer()) : "(no message)");
        return false;
    }
    return true;
}

struct Pipeline {
    ComPtr<ID3D11VertexShader> vsFull;
    ComPtr<ID3D11VertexShader> vsQuad;
    ComPtr<ID3D11PixelShader> psCopy;
    ComPtr<ID3D11PixelShader> psQuad;
    ComPtr<ID3D11PixelShader> psPresent;
    ComPtr<ID3D11Buffer> params;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> opaque;
    ComPtr<ID3D11BlendState> premultiplied;
    ComPtr<ID3D11BlendState> straight;
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
    ComPtr<ID3DBlob> vsFull, vsQuad, psCopy, psQuad, psPresent;
    if (!Compile("VSFull", "vs_5_0", vsFull) || !Compile("VSQuad", "vs_5_0", vsQuad) || !Compile("PSCopy", "ps_5_0", psCopy) ||
        !Compile("PSQuad", "ps_5_0", psQuad) || !Compile("PSPresent", "ps_5_0", psPresent)) {
        return false;
    }
    bool ok = SUCCEEDED(device->CreateVertexShader(vsFull->GetBufferPointer(), vsFull->GetBufferSize(), nullptr, gPipeline.vsFull.GetAddressOf())) &&
        SUCCEEDED(device->CreateVertexShader(vsQuad->GetBufferPointer(), vsQuad->GetBufferSize(), nullptr, gPipeline.vsQuad.GetAddressOf())) &&
        SUCCEEDED(device->CreatePixelShader(psCopy->GetBufferPointer(), psCopy->GetBufferSize(), nullptr, gPipeline.psCopy.GetAddressOf())) &&
        SUCCEEDED(device->CreatePixelShader(psQuad->GetBufferPointer(), psQuad->GetBufferSize(), nullptr, gPipeline.psQuad.GetAddressOf())) &&
        SUCCEEDED(device->CreatePixelShader(psPresent->GetBufferPointer(), psPresent->GetBufferSize(), nullptr, gPipeline.psPresent.GetAddressOf()));
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Params);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ok = ok && SUCCEEDED(device->CreateBuffer(&cb, nullptr, gPipeline.params.GetAddressOf()));
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    ok = ok && SUCCEEDED(device->CreateSamplerState(&sd, gPipeline.sampler.GetAddressOf()));
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(device->CreateBlendState(&bd, gPipeline.opaque.GetAddressOf()));
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    ok = ok && SUCCEEDED(device->CreateBlendState(&bd, gPipeline.premultiplied.GetAddressOf()));
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    ok = ok && SUCCEEDED(device->CreateBlendState(&bd, gPipeline.straight.GetAddressOf()));
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
        log::Error("spectator: pipeline creation failed; the desktop window keeps Halcyon's mirror");
    }
    return ok;
}

// Saves and restores what our passes touch on the game's context (UE3 caches its own state).
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
        c_->VSGetConstantBuffers(0, 1, &vsCb_);
        c_->PSGetShaderResources(0, 1, &srv_);
        c_->PSGetConstantBuffers(0, 1, &cb_);
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
        c_->RSSetScissorRects(scissorCount_, scissors_);
        c_->IASetPrimitiveTopology(topology_);
        c_->IASetInputLayout(layout_);
        c_->VSSetShader(vs_, nullptr, 0);
        c_->PSSetShader(ps_, nullptr, 0);
        c_->GSSetShader(gs_, nullptr, 0);
        c_->HSSetShader(hs_, nullptr, 0);
        c_->DSSetShader(ds_, nullptr, 0);
        c_->VSSetConstantBuffers(0, 1, &vsCb_);
        c_->PSSetShaderResources(0, 1, &srv_);
        c_->PSSetConstantBuffers(0, 1, &cb_);
        c_->PSSetSamplers(0, 1, &sampler_);
        for (auto*& r : rtvs_) {
            Drop(r);
        }
        Drop(dsv_);
        Drop(blend_);
        Drop(depth_);
        Drop(raster_);
        Drop(layout_);
        Drop(vs_);
        Drop(ps_);
        Drop(gs_);
        Drop(hs_);
        Drop(ds_);
        Drop(vsCb_);
        Drop(srv_);
        Drop(cb_);
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
    D3D11_RECT scissors_[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT scissorCount_ = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_PRIMITIVE_TOPOLOGY topology_ = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11GeometryShader* gs_ = nullptr;
    ID3D11HullShader* hs_ = nullptr;
    ID3D11DomainShader* ds_ = nullptr;
    ID3D11Buffer* vsCb_ = nullptr;
    ID3D11ShaderResourceView* srv_ = nullptr;
    ID3D11Buffer* cb_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
};

// ---- Formats ---------------------------------------------------------------------------------------------------------
// A quad's copy: stored typeless where the family has one, read through the sRGB view (Halcyon and our quads write
// display-encoded 8-bit values into every 8-bit image: core/vr/ImageConvert), or as float (linear).
struct CopyFormat {
    DXGI_FORMAT storage = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT view = DXGI_FORMAT_UNKNOWN;
};
std::optional<CopyFormat> CopyFormatFor(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return CopyFormat{DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return CopyFormat{DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB};
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return CopyFormat{DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_FLOAT};
    default: return std::nullopt;
    }
}

// ---- State -----------------------------------------------------------------------------------------------------------
struct LayerCopy {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT sourceFormat = DXGI_FORMAT_UNKNOWN;
    bool isStatic = false;
    bool failed = false;
    bool written = false;
};
std::unordered_map<XrSwapchain, LayerCopy> gCopies;
std::unordered_set<XrSwapchain> gProjection;  // the eye swapchains: never copied

struct Capture {
    std::uint64_t generation = 0;
    std::uint64_t resource = 0;
    unsigned tries = 0;
    bool gaveUp = false;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    D3D11_TEXTURE2D_DESC desc{};
    float decode = 1.0f;
    bool floatTexture = false;
};
Capture gCapture;
// The game's device, from the window's swap chain (MirrorPresent), for frames before any quad was copied. Held WITHOUT a
// reference: the game's device outlives us, and a static's Release at process exit would run after d3d11 may be gone.
ID3D11Device* gWindowDevice = nullptr;
sc::TexturePath gHint;
std::uint64_t gSeenResets = 0;

struct Picture {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
    UINT width = 0;
    UINT height = 0;
};
Picture gPicture;
std::uint64_t gPictureMs = 0;
bool gPictureMirror = false;

struct Backbuffer {
    ID3D11Texture2D* identity = nullptr;  // identity only
    ComPtr<ID3D11RenderTargetView> rtv;
    bool encode = false;
    bool isFloat = false;
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool failed = false;
};
Backbuffer gBackbuffer;

std::atomic<bool> gWanted{false};
std::uint64_t gPairArm = 0;
float gWhiteScale = 1.0f;

// Statistics (Present thread).
std::uint64_t gFramesComposed = 0;
std::uint64_t gFramesMirrored = 0;
std::uint64_t gFramesFromArm = 0;
std::uint64_t gLayersDrawn = 0;
std::uint64_t gComposedSinceStats = 0;
std::uint64_t gMirroredSinceStats = 0;
double gComposeMs = 0.0;
double gMirrorMs = 0.0;
double gCopyMs = 0.0;
std::chrono::steady_clock::time_point gStatsStart = std::chrono::steady_clock::now();
std::string gLastReason = "off";
std::string gLoggedReason;
bool gLoggedFirst = false;
bool gLoggedMirror = false;
std::string gLastLayers;

double Ms(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

void Reason(std::string reason) {
    if (reason != gLoggedReason) {
        gLoggedReason = reason;
        log::Info("spectator: {}", reason);
    }
    gLastReason = std::move(reason);
}

bool ResolveCapture(ID3D11Device* device, sp::Frame const& frame) {
    if (std::uint64_t const resets = sp::Resets(); resets != gSeenResets) {
        gSeenResets = resets;
        gCapture = Capture{};
    }
    if (gCapture.generation != frame.generation || gCapture.resource != frame.resource) {
        gCapture = Capture{};
        gCapture.generation = frame.generation;
        gCapture.resource = frame.resource;
    }
    if (gCapture.srv != nullptr) {
        return true;
    }
    if (gCapture.gaveUp) {
        return false;
    }
    ++gCapture.tries;
    // A capture target's texture sits behind an unaligned pointer (+0xA0 -> +0x8, research/39 section 11): the search finds it
    // only by the class of a texture of our own on the game's device, which NoteDevice learns. Until 2026-11-07 only the scope,
    // the galaxy map, the world UI's figure and the sights noted it, so on a walk with none of them the spectator never found
    // its picture and the window kept Halcyon's one-eye mirror (research/49, the first run).
    scope_capture_source::NoteDevice(device);
    D3D11_TEXTURE2D_DESC desc{};
    ID3D11Texture2D* const found = scope_capture_source::FindTargetTexture(frame.resource, frame.width, frame.height, gHint, &desc);
    if (found == nullptr) {
        if (gCapture.tries >= kResolveTries) {
            gCapture.gaveUp = true;
            log::Warn("spectator: no {}x{} D3D11 texture found behind the render target's resource 0x{:X} in {} frames (last search: {}; the "
                      "known class's textures there:{}); the desktop window keeps Halcyon's mirror", frame.width, frame.height,
                frame.resource, gCapture.tries, scope_capture_source::LastSearch(), scope_capture_source::DescribeTextures(frame.resource));
        }
        return false;
    }
    gCapture.texture.Attach(found);
    gCapture.desc = desc;
    DXGI_FORMAT view = DXGI_FORMAT_UNKNOWN;
    if (!scope_capture_source::CaptureViewFormat(desc.Format, view, gCapture.decode)) {
        gCapture.gaveUp = true;
        gCapture.texture.Reset();
        log::Warn("spectator: the capture's texture has format {}, which isn't read; the desktop window keeps Halcyon's mirror",
            static_cast<int>(desc.Format));
        return false;
    }
    gCapture.floatTexture = view == DXGI_FORMAT_R16G16B16A16_FLOAT || view == DXGI_FORMAT_R32G32B32A32_FLOAT || view == DXGI_FORMAT_R11G11B10_FLOAT;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = view;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    if (FAILED(device->CreateShaderResourceView(gCapture.texture.Get(), &sd, gCapture.srv.GetAddressOf()))) {
        gCapture.gaveUp = true;
        gCapture.texture.Reset();
        log::Warn("spectator: no view of format {} on the capture's texture (format {})", static_cast<int>(view), static_cast<int>(desc.Format));
        return false;
    }
    log::Info("spectator: the render target's D3D11 texture: {}x{} format {}, read as format {}{} (found in {} frames)", desc.Width, desc.Height,
        static_cast<int>(desc.Format), static_cast<int>(view),
        gCapture.decode != 1.0f ? std::format(", decoded with gamma {:.1f}", gCapture.decode) : std::string(), gCapture.tries);
    return true;
}

bool EnsurePicture(ID3D11Device* device, UINT width, UINT height) {
    if (gPicture.texture != nullptr && gPicture.width == width && gPicture.height == height) {
        return true;
    }
    gPicture = Picture{};
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&td, nullptr, gPicture.texture.GetAddressOf())) ||
        FAILED(device->CreateRenderTargetView(gPicture.texture.Get(), nullptr, gPicture.rtv.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(gPicture.texture.Get(), nullptr, gPicture.srv.GetAddressOf()))) {
        gPicture = Picture{};
        log::Error("spectator: the {}x{} picture texture failed", width, height);
        return false;
    }
    gPicture.width = width;
    gPicture.height = height;
    log::Info("spectator: the picture is {}x{} FP16", width, height);
    return true;
}

void SetParams(ID3D11DeviceContext* context, Params const& p) {
    context->UpdateSubresource(gPipeline.params.Get(), 0, nullptr, &p, 0, 0);
}

void Bind(ID3D11DeviceContext* context, ID3D11VertexShader* vs, ID3D11PixelShader* ps, ID3D11BlendState* blend) {
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs, nullptr, 0);
    context->PSSetShader(ps, nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* const cb = gPipeline.params.Get();
    context->VSSetConstantBuffers(0, 1, &cb);
    context->PSSetConstantBuffers(0, 1, &cb);
    ID3D11SamplerState* const sampler = gPipeline.sampler.Get();
    context->PSSetSamplers(0, 1, &sampler);
    float const factor[4] = {};
    context->OMSetBlendState(blend, factor, 0xFFFFFFFFu);
    context->OMSetDepthStencilState(gPipeline.noDepth.Get(), 0);
    context->RSSetState(gPipeline.raster.Get());
}

// A quad's static image (the reticle): its pixels uploaded once (premultiplied sRGB RGBA8).
LayerCopy* StaticCopy(ID3D11Device* device, XrSwapchain swapchain) {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint32_t> pixels;
    if (!testloop::CopyStaticImage(swapchain, width, height, pixels)) {
        return nullptr;
    }
    LayerCopy& copy = gCopies[swapchain];
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(), width * 4u, 0};
    if (FAILED(device->CreateTexture2D(&td, &data, copy.texture.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(copy.texture.Get(), nullptr, copy.srv.ReleaseAndGetAddressOf()))) {
        copy.failed = true;
        return nullptr;
    }
    copy.width = width;
    copy.height = height;
    copy.isStatic = true;
    copy.written = true;
    return &copy;
}

// The quad's corners in the camera's clip space (top left, top right, bottom left, bottom right); false if it faces away.
bool QuadCorners(Layer const& layer, XrPosef const& camera, float tanX, float tanY, float out[4][4]) {
    XrVector3f const normal = sp::Rotate(layer.pose.orientation, {0.0f, 0.0f, 1.0f});
    XrVector3f const toCamera{camera.position.x - layer.pose.position.x, camera.position.y - layer.pose.position.y,
        camera.position.z - layer.pose.position.z};
    if (!(normal.x * toCamera.x + normal.y * toCamera.y + normal.z * toCamera.z > 0.0f)) {
        return false;  // seen from behind: the compositor's front only (as core/vr/LayerComposite)
    }
    XrQuaternionf const inverse = sp::Conjugate(sp::Normalize(camera.orientation));
    float const hw = 0.5f * layer.size.width, hh = 0.5f * layer.size.height;
    float const local[4][2] = {{-hw, hh}, {hw, hh}, {-hw, -hh}, {hw, -hh}};
    bool anyInFront = false;
    for (int c = 0; c < 4; ++c) {
        XrVector3f const offset = sp::Rotate(layer.pose.orientation, {local[c][0], local[c][1], 0.0f});
        XrVector3f const world{layer.pose.position.x + offset.x - camera.position.x, layer.pose.position.y + offset.y - camera.position.y,
            layer.pose.position.z + offset.z - camera.position.z};
        XrVector3f const q = sp::Rotate(inverse, world);
        float const w = -q.z;
        out[c][0] = q.x / tanX;
        out[c][1] = q.y / tanY;
        out[c][2] = w - 0.01f;  // the near plane 1 cm out: 0 <= z <= w clips what is behind
        out[c][3] = w;
        anyInFront = anyInFront || w > 0.01f;
    }
    return anyInFront;
}

} // namespace

void OnSessionCreated() {
    gCopies.clear();
    gProjection.clear();
}

void NoteFrame(std::uint64_t pairArm, float floatWhiteScale) {
    gPairArm = pairArm;
    gWhiteScale = floatWhiteScale > 0.0f && std::isfinite(floatWhiteScale) ? floatWhiteScale : 1.0f;
    sp::Frame const latest = sp::Latest(250);
    bool const wanted = latest.state == sp::kCapturing && latest.resource != 0;
    if (!wanted && gWanted.load(std::memory_order_relaxed)) {
        gCopies.clear();  // the memory back: nothing is copied while it's off
        gCapture = Capture{};
        gPicture = Picture{};
        gBackbuffer = Backbuffer{};
        Reason(latest.state == sp::kCapturing ? "no render target yet" : "off: the mod isn't capturing");
    }
    gWanted.store(wanted, std::memory_order_relaxed);
}

void NoteDrawArm(std::uint64_t arm) {
    sp::NoteDrawArm(arm);
}

bool Wanted() {
    return gWanted.load(std::memory_order_relaxed);
}

void OnImageReleased(XrSwapchain swapchain, ID3D11Texture2D* image) {
    if (!Wanted() || image == nullptr || gProjection.count(swapchain) != 0) {
        return;
    }
    auto const begin = std::chrono::steady_clock::now();
    LayerCopy& copy = gCopies[swapchain];
    if (copy.failed || copy.isStatic) {
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    image->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1 || desc.ArraySize != 1) {
        copy.failed = true;
        log::Info("spectator: a quad's swapchain ({}x{}, {} samples, {} layers) isn't copied: left out of the picture", desc.Width, desc.Height,
            desc.SampleDesc.Count, desc.ArraySize);
        return;
    }
    ComPtr<ID3D11Device> device;
    image->GetDevice(device.GetAddressOf());
    if (copy.texture == nullptr || copy.width != desc.Width || copy.height != desc.Height || copy.sourceFormat != desc.Format) {
        auto const format = CopyFormatFor(desc.Format);
        if (!format) {
            copy.failed = true;
            log::Info("spectator: a quad's swapchain has format {}: left out of the picture", static_cast<int>(desc.Format));
            return;
        }
        D3D11_TEXTURE2D_DESC td = desc;
        td.Format = format->storage;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = 0;
        td.MiscFlags = 0;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = format->view;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = desc.MipLevels;
        if (FAILED(device->CreateTexture2D(&td, nullptr, copy.texture.ReleaseAndGetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(copy.texture.Get(), &sd, copy.srv.ReleaseAndGetAddressOf()))) {
            copy.failed = true;
            log::Warn("spectator: no copy of a {}x{} quad (format {}): left out of the picture", desc.Width, desc.Height, static_cast<int>(desc.Format));
            return;
        }
        copy.width = desc.Width;
        copy.height = desc.Height;
        copy.sourceFormat = desc.Format;
    }
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(context.GetAddressOf());
    context->CopyResource(copy.texture.Get(), image);
    copy.written = true;
    gCopyMs += Ms(begin);
}

ID3D11Texture2D* OnEndFrame(std::vector<XrSwapchain> const& projectionSwapchains, std::vector<Layer> const& layers) {
    for (XrSwapchain const s : projectionSwapchains) {
        if (gProjection.insert(s).second) {
            gCopies.erase(s);  // copied once before it was known to be an eye
        }
    }
    gPictureMirror = false;
    if (!Wanted()) {
        return nullptr;
    }
    auto const begin = std::chrono::steady_clock::now();
    sp::Frame const latest = sp::Latest(250);
    if (latest.state != sp::kCapturing || latest.width == 0 || latest.height == 0) {
        Reason("no render target yet");
        return nullptr;
    }
    // The publish the presented Draw rendered (its pair arm), else the one before the latest (research/39 section 5).
    std::optional<sp::Frame> rendered;
    bool fromArm = false;
    if (auto const sequence = sp::SequenceForArm(gPairArm)) {
        rendered = sp::BySequence(*sequence);
        fromArm = rendered && rendered->state == sp::kCapturing && rendered->generation == latest.generation;
        if (!fromArm) {
            Reason("waiting: the presented Draw rendered before the capture was on");
            return nullptr;
        }
    } else {
        if (latest.enabledTicks < kWarmupTicks) {
            Reason("waiting: just enabled");
            return nullptr;
        }
        rendered = sp::BySequence(latest.sequence - 1);
        if (!rendered || rendered->state != sp::kCapturing || rendered->generation != latest.generation) {
            rendered = latest;
        }
    }
    // A context and device: from any quad's copy, else from the capture search's d3d11 (the game's device).
    ID3D11Device* device = nullptr;
    ComPtr<ID3D11Device> deviceRef;
    for (auto const& [swapchain, copy] : gCopies) {
        if (copy.texture != nullptr) {
            copy.texture->GetDevice(deviceRef.GetAddressOf());
            break;
        }
    }
    if (deviceRef == nullptr && gCapture.texture != nullptr) {
        gCapture.texture->GetDevice(deviceRef.GetAddressOf());
    }
    if (deviceRef == nullptr) {
        deviceRef = gWindowDevice;  // a reference of our own for this frame (ComPtr's assignment AddRefs)
    }
    if (deviceRef == nullptr) {
        // The first frames: the capture's texture through the search, which needs no device to find it.
        D3D11_TEXTURE2D_DESC desc{};
        sc::TexturePath hint = gHint;
        if (ID3D11Texture2D* const t = scope_capture_source::FindTargetTexture(latest.resource, latest.width, latest.height, hint, &desc)) {
            t->GetDevice(deviceRef.GetAddressOf());
            t->Release();
        }
    }
    device = deviceRef.Get();
    if (device == nullptr) {
        Reason("looking for the render target's texture");
        return nullptr;
    }
    if (!EnsurePipeline(device)) {
        Reason("off: no pipeline");
        return nullptr;
    }
    if (!ResolveCapture(device, latest)) {
        Reason(gCapture.gaveUp ? "off: the render target's texture wasn't found" : "looking for the render target's texture");
        return nullptr;
    }
    if (!EnsurePicture(device, gCapture.desc.Width, gCapture.desc.Height)) {
        Reason("off: no picture texture");
        return nullptr;
    }
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(context.GetAddressOf());
    std::string drawn;
    std::size_t layersDrawn = 0;
    {
        StateGuard const guard(context.Get());
        ID3D11RenderTargetView* const rtv = gPicture.rtv.Get();
        context->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT const viewport{0.0f, 0.0f, static_cast<float>(gPicture.width), static_cast<float>(gPicture.height), 0.0f, 1.0f};
        context->RSSetViewports(1, &viewport);
        D3D11_RECT const scissor{0, 0, static_cast<LONG>(gPicture.width), static_cast<LONG>(gPicture.height)};
        context->RSSetScissorRects(1, &scissor);
        // The world: the capture, in "SDR white = 1" linear light (a float capture is in the game's float frame's scale).
        Params params{};
        params.mode[0] = gCapture.decode;
        params.mode[1] = gCapture.floatTexture ? gWhiteScale : 1.0f;
        SetParams(context.Get(), params);
        Bind(context.Get(), gPipeline.vsFull.Get(), gPipeline.psCopy.Get(), gPipeline.opaque.Get());
        ID3D11ShaderResourceView* const captureSrv = gCapture.srv.Get();
        context->PSSetShaderResources(0, 1, &captureSrv);
        context->Draw(3, 0);
        // Our layers over it, in submission order, from where the capture rendered.
        if ((rendered->flags & sp::kFlagLayers) != 0) {
            for (Layer const& layer : layers) {
                if (layer.name == "vignette" || layer.visibility == XR_EYE_VISIBILITY_LEFT) {
                    continue;  // the comfort vignette isn't part of the view; one eye's layers: the right eye's
                }
                LayerCopy* copy = nullptr;
                if (auto const it = gCopies.find(layer.swapchain); it != gCopies.end() && it->second.written && it->second.srv != nullptr) {
                    copy = &it->second;
                } else if (it == gCopies.end() || !it->second.failed) {
                    copy = StaticCopy(device, layer.swapchain);
                }
                if (copy == nullptr || copy->srv == nullptr) {
                    continue;
                }
                if (!QuadCorners(layer, rendered->pose, rendered->tanHalfX, rendered->tanHalfY, params.corners)) {
                    continue;
                }
                float const w = static_cast<float>(copy->width), h = static_cast<float>(copy->height);
                bool const fullRect = layer.rect.extent.width <= 0 || layer.rect.extent.height <= 0;
                params.uvRect[0] = fullRect ? 0.0f : static_cast<float>(layer.rect.offset.x) / w;
                params.uvRect[1] = fullRect ? 0.0f : static_cast<float>(layer.rect.offset.y) / h;
                params.uvRect[2] = fullRect ? 1.0f : static_cast<float>(layer.rect.offset.x + layer.rect.extent.width) / w;
                params.uvRect[3] = fullRect ? 1.0f : static_cast<float>(layer.rect.offset.y + layer.rect.extent.height) / h;
                bool const blend = (layer.flags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) != 0;
                bool const premultiplied = blend && (layer.flags & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT) == 0;
                params.mode[0] = 0.0f;
                params.mode[1] = 1.0f;
                params.mode[2] = blend ? 0.0f : 1.0f;
                SetParams(context.Get(), params);
                Bind(context.Get(), gPipeline.vsQuad.Get(), gPipeline.psQuad.Get(),
                    !blend ? gPipeline.opaque.Get() : premultiplied ? gPipeline.premultiplied.Get() : gPipeline.straight.Get());
                ID3D11ShaderResourceView* const srv = copy->srv.Get();
                context->PSSetShaderResources(0, 1, &srv);
                context->Draw(6, 0);
                ++layersDrawn;
                drawn += (drawn.empty() ? "" : ", ") + (layer.name.empty() ? std::string("quad") : layer.name);
            }
        }
    }
    for (auto& [swapchain, copy] : gCopies) {
        if (!copy.isStatic) {
            copy.written = false;  // a quad not released next frame isn't shown with an old image
        }
    }
    gPictureMs = GetTickCount64();
    gPictureMirror = (latest.flags & sp::kFlagMirror) != 0;
    ++gFramesComposed;
    ++gComposedSinceStats;
    gFramesFromArm += fromArm ? 1u : 0u;
    gLayersDrawn += layersDrawn;
    gLastLayers = drawn;
    gComposeMs += Ms(begin);
    Reason(std::format("composing: {}x{} from the {} camera{}", gPicture.width, gPicture.height, rendered->camera == sp::kHead ? "head" : "free",
        (rendered->flags & sp::kFlagLayers) != 0 ? "" : " (no layers: no head-tracked view, or [Spectator] layers = 0)"));
    if (!gLoggedFirst) {
        gLoggedFirst = true;
        log::Info("spectator: first picture: {}x{}, {} layer{} ({}), pose {}", gPicture.width, gPicture.height, layersDrawn, layersDrawn == 1 ? "" : "s",
            drawn.empty() ? "none" : drawn, fromArm ? "from the presented Draw's arm" : "the publish before the latest");
    }
    double const elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - gStatsStart).count();
    if (elapsed >= kStatsSeconds) {
        double const n = static_cast<double>(std::max<std::uint64_t>(gComposedSinceStats, 1));
        log::Info("spectator: {} pictures and {} mirrored in {:.0f} s; CPU per picture: copies {:.3f} ms, composite {:.3f} ms, mirror {:.3f} ms; "
                  "layers: {}", gComposedSinceStats, gMirroredSinceStats, elapsed, gCopyMs / n, gComposeMs / n,
            gMirrorMs / static_cast<double>(std::max<std::uint64_t>(gMirroredSinceStats, 1)), gLastLayers.empty() ? "none" : gLastLayers);
        gStatsStart = std::chrono::steady_clock::now();
        gComposedSinceStats = gMirroredSinceStats = 0;
        gComposeMs = gMirrorMs = gCopyMs = 0.0;
    }
    return gPicture.texture.Get();
}

void MirrorPresent(IDXGISwapChain* swapChain) {
    if (gWindowDevice == nullptr && swapChain != nullptr) {
        ComPtr<ID3D11Device> d;
        if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(d.GetAddressOf())))) {
            gWindowDevice = d.Get();
        }
    }
    if (!gPictureMirror || gPicture.srv == nullptr || swapChain == nullptr || GetTickCount64() - gPictureMs > kPictureFreshMs ||
        !gPipeline.ready) {
        return;
    }
    auto const begin = std::chrono::steady_clock::now();
    ComPtr<ID3D11Texture2D> backbuffer;
    if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backbuffer.GetAddressOf()))) || backbuffer == nullptr) {
        return;
    }
    ComPtr<ID3D11Device> device;
    backbuffer->GetDevice(device.GetAddressOf());
    if (gBackbuffer.identity != backbuffer.Get()) {
        gBackbuffer = Backbuffer{};
        gBackbuffer.identity = backbuffer.Get();
        D3D11_TEXTURE2D_DESC desc{};
        backbuffer->GetDesc(&desc);
        gBackbuffer.width = desc.Width;
        gBackbuffer.height = desc.Height;
        gBackbuffer.format = desc.Format;
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        switch (desc.Format) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; gBackbuffer.encode = true; break;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; gBackbuffer.encode = true; break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_R10G10B10A2_UNORM: rd.Format = desc.Format; gBackbuffer.encode = true; break;
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: rd.Format = desc.Format; break;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: rd.Format = desc.Format; gBackbuffer.isFloat = true; break;
        default: gBackbuffer.failed = true; break;
        }
        if (!gBackbuffer.failed && FAILED(device->CreateRenderTargetView(backbuffer.Get(), &rd, gBackbuffer.rtv.GetAddressOf()))) {
            gBackbuffer.failed = true;
        }
        log::Info("spectator: the desktop window's backbuffer is {}x{} format {}: {}", desc.Width, desc.Height, static_cast<int>(desc.Format),
            gBackbuffer.failed ? "not drawn into (Halcyon's mirror stays)"
                               : gBackbuffer.isFloat ? "linear (scRGB)" : gBackbuffer.encode ? "sRGB-encoded by the shader" : "sRGB view");
    }
    if (gBackbuffer.failed || gBackbuffer.rtv == nullptr) {
        return;
    }
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(context.GetAddressOf());
    {
        StateGuard const guard(context.Get());
        float const black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        context->ClearRenderTargetView(gBackbuffer.rtv.Get(), black);
        // Letterboxed: the picture's aspect, as large as the backbuffer allows, centred.
        float const bw = static_cast<float>(gBackbuffer.width), bh = static_cast<float>(gBackbuffer.height);
        float const aspect = static_cast<float>(gPicture.width) / static_cast<float>(gPicture.height);
        float w = bw, h = bw / aspect;
        if (h > bh) {
            h = bh;
            w = bh * aspect;
        }
        D3D11_VIEWPORT const viewport{std::floor(0.5f * (bw - w)), std::floor(0.5f * (bh - h)), std::floor(w), std::floor(h), 0.0f, 1.0f};
        ID3D11RenderTargetView* const rtv = gBackbuffer.rtv.Get();
        context->OMSetRenderTargets(1, &rtv, nullptr);
        context->RSSetViewports(1, &viewport);
        D3D11_RECT const scissor{0, 0, static_cast<LONG>(gBackbuffer.width), static_cast<LONG>(gBackbuffer.height)};
        context->RSSetScissorRects(1, &scissor);
        Params params{};
        params.mode[0] = gBackbuffer.encode ? 1.0f : 0.0f;
        params.mode[1] = gBackbuffer.isFloat ? 1.0f / gWhiteScale : 1.0f;
        SetParams(context.Get(), params);
        Bind(context.Get(), gPipeline.vsFull.Get(), gPipeline.psPresent.Get(), gPipeline.opaque.Get());
        ID3D11ShaderResourceView* const srv = gPicture.srv.Get();
        context->PSSetShaderResources(0, 1, &srv);
        context->Draw(3, 0);
    }
    ++gFramesMirrored;
    ++gMirroredSinceStats;
    gMirrorMs += Ms(begin);
    if (!gLoggedMirror) {
        gLoggedMirror = true;
        log::Info("spectator: the desktop window shows the spectator's picture (letterboxed into {}x{})", gBackbuffer.width, gBackbuffer.height);
    }
}

std::string StatusJson() {
    namespace json = vr::test_request;
    return std::format("{{\"state\": {}, \"wanted\": {}, \"picture\": [{}, {}], \"capture\": {{\"size\": [{}, {}], \"format\": {}, "
                       "\"decodeGamma\": {}, \"float\": {}, \"tries\": {}}}, \"whiteScale\": {}, \"framesComposed\": {}, \"framesMirrored\": {}, "
                       "\"framesFromArm\": {}, \"layersDrawn\": {}, \"lastLayers\": {}, \"backbuffer\": {{\"size\": [{}, {}], \"format\": {}}}}}",
        json::JsonString(gLastReason), Wanted() ? "true" : "false", gPicture.width, gPicture.height, gCapture.desc.Width, gCapture.desc.Height,
        static_cast<int>(gCapture.desc.Format), json::JsonNumber(gCapture.decode), gCapture.floatTexture ? "true" : "false", gCapture.tries,
        json::JsonNumber(gWhiteScale), gFramesComposed, gFramesMirrored, gFramesFromArm, gLayersDrawn, json::JsonString(gLastLayers),
        gBackbuffer.width, gBackbuffer.height, static_cast<int>(gBackbuffer.format));
}

} // namespace mevr::host::spectator
