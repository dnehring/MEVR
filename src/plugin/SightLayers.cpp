// The gun's sight on Halcyon's world layer (plugin/SightLayers.hpp, research/21). Present thread only.

#include "plugin/SightLayers.hpp"

#include "core/Log.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "core/vr/Sights.hpp"
#include "plugin/SightPictures.hpp"
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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace mevr::host::sight_layers {
namespace {

using Microsoft::WRL::ComPtr;
namespace sv = vr::sights;

constexpr std::int64_t kSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
constexpr UINT kDotPixels = sight_pictures::kPictureSize;
constexpr UINT kFramePixels = sight_pictures::kPictureSize;
constexpr float kDotDistance = 50.0f;  // meters out along the sight line: the reticle sits at the target's depth
constexpr float kMinAlong = 0.02f;     // an eye this close behind a dot sight's window or closer doesn't count
constexpr float kMaxAlong = 1.0f;      // ...nor one further back than this (held out at arm's length)
constexpr float kScopeBox = 2.0f;      // lens radii: an eye this far off the line still sees the eyepiece (dark)
constexpr float kPi = 3.14159265358979f;

// ---- Swapchains -----------------------------------------------------------------------------------------
struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;  // owned by the runtime
    std::vector<ComPtr<ID3D11RenderTargetView>> rtvs;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool failed = false;
    std::uint64_t signature = 0;  // static images: what they show

    void Forget() {
        handle = XR_NULL_HANDLE;
        images.clear();
        rtvs.clear();
        width = height = 0;
        failed = false;
        signature = 0;
    }
    void Destroy() {
        if (handle != XR_NULL_HANDLE) {
            testloop::ForgetSwapchain(handle);
            xrDestroySwapchain(handle);
        }
        Forget();
    }
};

XrSession gSession = XR_NULL_HANDLE;
Swapchain gDot;
Swapchain gFrame;
Swapchain gScope;
XrCompositionLayerQuad gDotQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
XrCompositionLayerQuad gFrameQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
XrCompositionLayerQuad gScopeQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};

bool Create(XrSession session, std::uint32_t width, std::uint32_t height, Swapchain& out, char const* what, char const* name) {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;  // as the reticle's (filled by a copy too)
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
        r = xrEnumerateSwapchainImages(out.handle, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    }
    if (XR_FAILED(r) || count == 0) {
        log::Error("sights: {} swapchain {}x{} failed (XrResult {}); that part of the sight won't show", what, width, height,
            static_cast<int>(r));
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
    testloop::NameSwapchain(out.handle, name);
    log::Info("sights: {} swapchain {}x{}, {} images", what, width, height, count);
    return true;
}

bool Acquire(Swapchain& swapchain, std::uint32_t& index) {
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

void Release(Swapchain& swapchain) {
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(swapchain.handle, &release);
}

// Fills every image of a fresh swapchain with `pixels` once (premultiplied sRGB RGBA8): its content never
// changes, the runtime keeps showing the last released, and snapshots draw it from the same pixels.
bool FillStatic(ID3D11DeviceContext* context, ID3D11Device* device, Swapchain& swapchain, std::vector<std::uint32_t> const& pixels) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = swapchain.width;
    td.Height = swapchain.height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(), swapchain.width * 4, 0};
    ComPtr<ID3D11Texture2D> source;
    if (FAILED(device->CreateTexture2D(&td, &data, source.GetAddressOf()))) {
        return false;
    }
    for (size_t i = 0; i < swapchain.images.size(); ++i) {
        std::uint32_t index = 0;
        if (!Acquire(swapchain, index)) {
            return false;
        }
        context->CopyResource(swapchain.images[index], source.Get());
        Release(swapchain);
    }
    testloop::SetStaticImage(swapchain.handle, swapchain.width, swapchain.height, pixels);
    return true;
}

// The pictures (reticles, the scope shader) are in plugin/SightPictures.
using namespace sight_pictures;

using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
    ID3DBlob**, ID3DBlob**);

bool Compile(char const* entry, char const* profile, ComPtr<ID3DBlob>& out) {
    static D3DCompileFn compile = [] {
        HMODULE module = LoadLibraryW(L"d3dcompiler_47.dll");
        return module != nullptr ? reinterpret_cast<D3DCompileFn>(GetProcAddress(module, "D3DCompile")) : nullptr;
    }();
    if (compile == nullptr) {
        log::Error("sights: d3dcompiler_47.dll unavailable; no scope picture");
        return false;
    }
    ComPtr<ID3DBlob> errors;
    HRESULT const hr = compile(kScopeShader, kScopeShaderSize, "MEVR_Scope", nullptr, nullptr, entry, profile,
        0x8000 /* D3DCOMPILE_OPTIMIZATION_LEVEL3 */, 0, out.ReleaseAndGetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        log::Error("sights: scope shader {} failed to compile: {}", entry,
            errors ? static_cast<char const*>(errors->GetBufferPointer()) : "(no message)");
        return false;
    }
    return true;
}

struct Pipeline {
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> params;
    ComPtr<ID3D11SamplerState> sampler;
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
    bool ok = SUCCEEDED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, gPipeline.vs.GetAddressOf())) &&
        SUCCEEDED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, gPipeline.ps.GetAddressOf()));
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(ScopeParams);
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
        log::Error("sights: scope pipeline creation failed; no scope picture");
    }
    return ok;
}

// Saves and restores what the scope pass touches on the game's context (UE3 caches its own state).
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
    ID3D11ShaderResourceView* srv_ = nullptr;
    ID3D11Buffer* cb_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
};

// ---- The eye image a scope magnifies: copied as Halcyon releases it ----------------------------------------
struct EyeCopy {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11DeviceContext> context;
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT family = DXGI_FORMAT_UNKNOWN;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    std::uint64_t frame = 0;  // the frame (gFrameCount) it belongs to
    bool toldUnsupported = false;
    unsigned copies = 0;
};
EyeCopy gCopy;
std::uint64_t gFrameCount = 0;

// A swapchain format's typeless family and the view it's read through: the eyes' 8-bit images hold
// sRGB-encoded colour (Halcyon's eye swapchains are sRGB), read back as linear; FP16 is linear already.
bool Family(DXGI_FORMAT format, DXGI_FORMAT& family, DXGI_FORMAT& view) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        family = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        view = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        return true;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        family = DXGI_FORMAT_B8G8R8A8_TYPELESS;
        view = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        return true;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        family = DXGI_FORMAT_R16G16B16A16_TYPELESS;
        view = DXGI_FORMAT_R16G16B16A16_FLOAT;
        return true;
    default:
        return false;
    }
}

// testloop's release tap: the source eye's image, right before Halcyon hands it to the runtime.
void OnEyeReleased(XrSwapchain swapchain, ID3D11Texture2D* image) {
    D3D11_TEXTURE2D_DESC desc{};
    image->GetDesc(&desc);
    DXGI_FORMAT family = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT view = DXGI_FORMAT_UNKNOWN;
    if (!Family(desc.Format, family, view) || desc.SampleDesc.Count != 1 || desc.ArraySize != 1) {
        if (!gCopy.toldUnsupported) {
            gCopy.toldUnsupported = true;
            log::Warn("sights: the eye image is format {} ({} samples, {} layers): not copied, so no scope picture",
                static_cast<int>(desc.Format), desc.SampleDesc.Count, desc.ArraySize);
        }
        return;
    }
    if (gCopy.texture == nullptr || gCopy.width != desc.Width || gCopy.height != desc.Height || gCopy.family != family) {
        ComPtr<ID3D11Device> device;
        image->GetDevice(device.GetAddressOf());
        D3D11_TEXTURE2D_DESC td{};
        td.Width = desc.Width;
        td.Height = desc.Height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = family;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = view;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        gCopy.texture.Reset();
        gCopy.srv.Reset();
        if (device == nullptr || FAILED(device->CreateTexture2D(&td, nullptr, gCopy.texture.GetAddressOf())) ||
            FAILED(device->CreateShaderResourceView(gCopy.texture.Get(), &sd, gCopy.srv.GetAddressOf()))) {
            log::Error("sights: the eye image's copy ({}x{}) failed; no scope picture", desc.Width, desc.Height);
            gCopy.texture.Reset();
            gCopy.srv.Reset();
            gCopy.toldUnsupported = true;
            return;
        }
        if (gCopy.context == nullptr) {
            device->GetImmediateContext(gCopy.context.GetAddressOf());
        }
        gCopy.width = desc.Width;
        gCopy.height = desc.Height;
        gCopy.family = family;
        log::Info("sights: the scope magnifies a copy of the eye image ({}x{}, format {}), taken as it's released", desc.Width,
            desc.Height, static_cast<int>(desc.Format));
    }
    gCopy.context->CopyResource(gCopy.texture.Get(), image);
    gCopy.swapchain = swapchain;
    gCopy.frame = gFrameCount + 1;  // released before this frame's layers are built (OverlayLayers counts it then)
    ++gCopy.copies;
}

// ---- State ------------------------------------------------------------------------------------------------
int gDotEye = -1;          // the eye looking through a dot or holo sight
int gAiming = -1;          // the eye aiming through a scope
int gSource = -1;          // whose image it magnifies
bool gScopeDrawn = false;  // this frame
sv::Line gLines[2];
sv::Sight gSight{0};
std::uint32_t gView = 0;
std::uint32_t gLoggedView = UINT32_MAX;
int gLoggedKind = -1;
bool gLoggedDot = false;
bool gLoggedScope = false;
bool gLoggedFrame = false;
// Halcyon's stereo against the true eyes (research/21 section 9): each eye image as drawn, and how far it moved the
// quad it last corrected (for the snapshot).
sv::EyeImage gImages[2];
sv::ShownError gCorrection{};
int gCorrectedEye = -1;
float gCorrectionCm = 0.0f;  // how far the corrected quad moved (its true place to where the image shows it)
std::string gStereoText = "not known yet";
std::string gLoggedStereo;

void Off() {
    gDotEye = -1;
    gAiming = -1;
    gSource = -1;
    testloop::SetReleaseTap(XR_NULL_HANDLE, nullptr);
}

XrCompositionLayerQuad MakeQuad(XrSpace space, Swapchain const& swapchain, sv::Quad const& quad, XrEyeVisibility visibility) {
    XrCompositionLayerQuad q{XR_TYPE_COMPOSITION_LAYER_QUAD};
    q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
    q.space = space;
    q.eyeVisibility = visibility;
    q.subImage.swapchain = swapchain.handle;
    q.subImage.imageRect = {{0, 0}, {static_cast<int32_t>(swapchain.width), static_cast<int32_t>(swapchain.height)}};
    q.pose = quad.pose;
    q.size = {quad.size, quad.size};
    return q;
}

// The dot or holo reticle's swapchain, made (again) for these looks.
bool EnsureDot(Frame const& frame, sv::Sight const& sight, DotLayout const& layout) {
    std::uint64_t const signature = (static_cast<std::uint64_t>(sight.kind) << 56) ^ (static_cast<std::uint64_t>(sight.dotColor) << 24) ^
        static_cast<std::uint64_t>(std::lround(layout.dotRadius * 4096.0f)) ^ (static_cast<std::uint64_t>(std::lround(layout.ringRadius * 4096.0f)) << 12);
    if (gDot.handle != XR_NULL_HANDLE && gDot.signature == signature) {
        return true;
    }
    if (gDot.handle != XR_NULL_HANDLE) {
        gDot.Destroy();
    }
    if (gDot.failed || !Create(frame.session, kDotPixels, kDotPixels, gDot, "sight reticle", "sight")) {
        return false;
    }
    if (!FillStatic(frame.context, frame.device, gDot, DotPixels(layout, sight.dotColor))) {
        log::Error("sights: the reticle's picture couldn't be filled; no dot");
        gDot.Destroy();
        gDot.failed = true;
        return false;
    }
    gDot.signature = signature;
    return true;
}

bool EnsureFrame(Frame const& frame) {
    if (gFrame.handle != XR_NULL_HANDLE) {
        return true;
    }
    if (gFrame.failed || !Create(frame.session, kFramePixels, kFramePixels, gFrame, "sight window ring", "sightframe")) {
        return false;
    }
    if (!FillStatic(frame.context, frame.device, gFrame, FramePixels())) {
        gFrame.Destroy();
        gFrame.failed = true;
        return false;
    }
    return true;
}

// Draws the scope's picture into its swapchain; false if anything is missing.
bool RenderScope(Frame const& frame, sv::Sight const& sight, sv::Lens const& lens, sv::ScopeView const& view, XrVector3f const& eye,
    int source) {
    std::uint32_t const size = static_cast<std::uint32_t>(std::clamp(sight.resolution, 128, 2048));
    if (gScope.handle != XR_NULL_HANDLE && gScope.width != size) {
        gScope.Destroy();
    }
    if (gScope.handle == XR_NULL_HANDLE && (gScope.failed || !Create(frame.session, size, size, gScope, "scope picture", "scope"))) {
        return false;
    }
    if (!EnsurePipeline(frame.device) || gCopy.srv == nullptr) {
        return false;
    }
    // The source image as Halcyon drew it (its render place and its convergence shift): the picture then shows the
    // world where it truly is, so the reticle sits on what the sight line points at (research/21 section 9).
    sv::SampleView const sample = sv::SampleViewOf(gImages[source], frame.projectionPose[source], frame.projectionFov[source]);
    ScopeParams const params = MakeScopeParams(sight, lens, view, eye, sample, frame.projectionRect[source], gCopy.width, gCopy.height, size);
    std::uint32_t index = 0;
    if (!Acquire(gScope, index)) {
        return false;
    }
    ComPtr<ID3D11RenderTargetView>& rtv = gScope.rtvs[index];
    if (rtv == nullptr) {
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = static_cast<DXGI_FORMAT>(kSwapchainFormat);
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        frame.device->CreateRenderTargetView(gScope.images[index], &rd, rtv.GetAddressOf());
    }
    bool drew = false;
    if (rtv != nullptr) {
        ID3D11DeviceContext* const c = frame.context;
        c->UpdateSubresource(gPipeline.params.Get(), 0, nullptr, &params, 0, 0);
        StateGuard guard(c);
        D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f};
        float const blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        ID3D11RenderTargetView* target = rtv.Get();
        ID3D11ShaderResourceView* image = gCopy.srv.Get();
        ID3D11Buffer* constants = gPipeline.params.Get();
        ID3D11SamplerState* sampler = gPipeline.sampler.Get();
        c->OMSetRenderTargets(1, &target, nullptr);
        c->OMSetBlendState(gPipeline.opaque.Get(), blendFactor, 0xFFFFFFFFu);
        c->OMSetDepthStencilState(gPipeline.noDepth.Get(), 0);
        c->RSSetState(gPipeline.raster.Get());
        c->RSSetViewports(1, &viewport);
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(gPipeline.vs.Get(), nullptr, 0);
        c->PSSetShader(gPipeline.ps.Get(), nullptr, 0);
        c->GSSetShader(nullptr, nullptr, 0);
        c->HSSetShader(nullptr, nullptr, 0);
        c->DSSetShader(nullptr, nullptr, 0);
        c->PSSetShaderResources(0, 1, &image);
        c->PSSetConstantBuffers(0, 1, &constants);
        c->PSSetSamplers(0, 1, &sampler);
        c->Draw(3, 0);
        drew = true;
    }
    testloop::OnImageWritten(gScope.handle, gScope.images[index]);  // snapshots
    Release(gScope);
    return drew;
}

char const* EyeName(int eye) {
    return eye == 0 ? "left" : eye == 1 ? "right" : "none";
}

XrEyeVisibility VisibilityOf(int eye) {
    return eye == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT;
}

float Distance(XrVector3f a, XrVector3f b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// Each eye image as Halcyon's Stereo 2 drew it, when the sight asks for it and the host knows; logged on change.
void UpdateImages(Frame const& frame, sv::Sight const& sight) {
    bool const wanted = (sight.flags & sv::kMatchStereo) != 0;
    bool const known = frame.stereoValid && frame.projectionValid;
    for (int e = 0; e < 2; ++e) {
        gImages[e] = wanted && known ? sv::MakeEyeImage(frame.projectionPose, e, frame.stereoHalfEyeM, frame.imageShiftTan[e])
                                     : sv::EyeImage{};
    }
    if (!wanted) {
        gStereoText = "not matched ([Sights] matchStereo = 0): the sights sit at their true place";
    } else if (!known) {
        gStereoText = "unknown (not a Stereo 2 frame): the sights sit at their true place";
    } else {
        constexpr float kDeg = 180.0f / kPi;
        float const trueHalf = 0.5f * Distance(frame.projectionPose[0].position, frame.projectionPose[1].position);
        gStereoText = std::format("Stereo 2 drew the eyes {:.1f} cm either side of their midpoint (the headset's {:.1f} cm) and moved "
                                  "the left image {:+.2f} and the right {:+.2f} degrees at submit (convergence): the dot and the scope "
                                  "are drawn where the images show the world",
            frame.stereoHalfEyeM * 100.0f, trueHalf * 100.0f, std::atan(frame.imageShiftTan[0]) * kDeg,
            std::atan(frame.imageShiftTan[1]) * kDeg);
    }
    if (gStereoText != gLoggedStereo) {
        gLoggedStereo = gStereoText;
        log::Info("[MEVR-SIGHT] eye images: {}", gStereoText);
    }
}

// The true eyes' midpoint (as submitted).
XrVector3f Mid() {
    XrVector3f const& a = gImages[0].pose.position;
    XrVector3f const& b = gImages[1].pose.position;
    return {0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z)};
}

// `point` moved to where eye `eye`'s image shows it (for a quad seen by that eye only), noted for the snapshot.
XrVector3f ForEye(int eye, XrVector3f const& point) {
    if (eye < 0 || eye > 1 || !gImages[eye].valid) {
        return point;
    }
    XrVector3f const shown = sv::AsShown(gImages[eye], point);
    gCorrection = sv::ErrorOf(gImages[eye], point);
    gCorrectedEye = eye;
    gCorrectionCm = Distance(shown, point) * 100.0f;
    return shown;
}

} // namespace

Output Layers(Frame const& frame) {
    Output out;
    ++gFrameCount;
    gScopeDrawn = false;
    if (frame.session != gSession) {
        gDot.Forget();
        gFrame.Forget();
        gScope.Forget();
        gCopy.swapchain = XR_NULL_HANDLE;
        gSession = frame.session;
        Off();
    }
    sv::Sight const sight = sv::Latest();
    gSight = sight;
    auto const kind = static_cast<sv::Kind>(sight.kind);
    if (sight.kind != gLoggedKind) {
        gLoggedKind = sight.kind;
        log::Info("sights: the host draws {}", kind == sv::Kind::None ? std::string("no sight")
                                                                    : std::format("a {} sight for the {}", sv::Name(kind),
                                                                          sv::WeaponName(sight.weapon)));
    }
    bool const usable = kind != sv::Kind::None && frame.worldFrame && !frame.menuOpen && frame.onFoot && frame.eyesValid &&
        frame.context != nullptr && frame.device != nullptr && frame.session != XR_NULL_HANDLE && sight.radius > 0.0f;
    std::uint32_t view = 0;
    gCorrectedEye = -1;
    gCorrection = {};
    gCorrectionCm = 0.0f;
    if (!usable) {
        Off();
    } else {
        UpdateImages(frame, sight);
        for (int e = 0; e < 2; ++e) {
            gLines[e] = sv::EyeLine(frame.eyes[e].position, sight.center, sight.axis, sight.up);
        }
        if (kind == sv::Kind::Dot || kind == sv::Kind::Holo) {
            gAiming = -1;
            gSource = -1;
            testloop::SetReleaseTap(XR_NULL_HANDLE, nullptr);
            // The eye looking through: the one whose line along the sight passes nearest the window's centre, within
            // the eye box (more forgiving than the drawn window: a gun raised to the face shows its dot), kept with
            // hysteresis. One eye only, like a real red dot seen with both eyes open (the other eye looks past it),
            // so the dot can sit on that eye's image of the target.
            float const box = std::max(sight.eyeBox, sight.radius);
            gDotEye = sv::AimingEye(gLines, box, kMinAlong, kMaxAlong, gDotEye);
            bool const on[2] = {gDotEye == 0, gDotEye == 1};
            if ((sight.flags & sv::kFrame) != 0 && out.count < 2 && EnsureFrame(frame)) {
                gFrameQuad = MakeQuad(frame.appSpace, gFrame, sv::Disc(sight.center, sight.axis, sight.up, sight.radius * kFrameScale),
                    XR_EYE_VISIBILITY_BOTH);
                out.layers[out.count++] = &gFrameQuad;
                if (!gLoggedFrame) {
                    gLoggedFrame = true;
                    log::Info("[MEVR-SIGHT] first sight window ring at ({:.3f}, {:.3f}, {:.3f}) m, {:.3f} m across",
                        sight.center.x, sight.center.y, sight.center.z, gFrameQuad.size.width);
                }
            }
            DotLayout const layout = LayoutFor(sight);
            if ((on[0] || on[1]) && out.count < 2 && EnsureDot(frame, sight, layout)) {
                sv::Quad quad = sv::Reticle(sight.center, sight.axis, sight.up, kDotDistance, layout.quadDeg * kPi / 180.0f);
                // Collimated on the target as that eye's image shows it (Halcyon's convergence moves each image
                // about a degree sideways: research/21 section 9).
                quad.pose.position = ForEye(gDotEye, quad.pose.position);
                gDotQuad = MakeQuad(frame.appSpace, gDot, quad, VisibilityOf(gDotEye));
                out.layers[out.count++] = &gDotQuad;
                view |= (on[0] ? sv::kViewLeft : 0u) | (on[1] ? sv::kViewRight : 0u);
                if (!gLoggedDot) {
                    gLoggedDot = true;
                    int const e = on[1] ? 1 : 0;
                    log::Info("[MEVR-SIGHT] first {} reticle: the {} eye looks through the window ({:.3f} m behind it, {:.4f} m off "
                              "the line, window radius {:.4f} m, eye box {:.4f} m); the reticle {:.0f} m out along the sight line, "
                              "{:.2f} deg, moved {:+.2f} degrees right and {:+.2f} up onto that eye's image",
                        sv::Name(kind), EyeName(e), gLines[e].along, gLines[e].lateral, sight.radius, box, kDotDistance, layout.quadDeg,
                        gCorrection.rightDeg, gCorrection.upDeg);
                }
            }
        } else if (kind == sv::Kind::Scope) {
            gDotEye = -1;
            float const relief = std::max(sight.eyeReliefM, 0.01f);
            gAiming = sv::AimingEye(gLines, kScopeBox * sight.radius, 0.01f, 6.0f * relief, gAiming);
            // The eyepiece where the aiming eye's image shows the gun's scope (Halcyon's stereo isn't metric: a few
            // millimetres at the face), so the picture sits on the model; its reticle still points along the sight
            // line (the picture is centred on the line through the eye, wherever the lens is).
            XrVector3f const center = gAiming >= 0 ? ForEye(gAiming, sight.center) : sight.center;
            auto const lens = sv::MakeLens(center, sight.axis, sight.up, sight.radius);
            if (!lens) {
                gAiming = -1;
            }
            std::optional<sv::ScopeView> scopeView;
            if (gAiming >= 0) {
                scopeView = sv::View(*lens, frame.eyes[gAiming].position, relief);
            }
            if (scopeView && frame.projectionValid) {
                gSource = (sight.flags & sv::kOtherEye) != 0 ? 1 - gAiming : gAiming;
                XrSwapchain const swapchain = frame.projectionSwapchain[gSource];
                testloop::SetReleaseTap(swapchain, &OnEyeReleased);  // next frame's copy
                view |= gAiming == 0 ? sv::kViewLeft : sv::kViewRight;
                // This frame's copy (or the last, if the eye image wasn't released again: that's what shows).
                bool const fresh = gCopy.srv != nullptr && gCopy.swapchain == swapchain && gCopy.frame + 2 >= gFrameCount;
                if (fresh && out.count < 2 &&
                    RenderScope(frame, sight, *lens, *scopeView, frame.eyes[gAiming].position, gSource)) {
                    gScopeQuad = MakeQuad(frame.appSpace, gScope, sv::Disc(lens->center, sight.axis, sight.up, sight.radius),
                        gAiming == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT);
                    out.layers[out.count++] = &gScopeQuad;
                    view |= sv::kViewScope;
                    gScopeDrawn = true;
                    if (!gLoggedScope) {
                        gLoggedScope = true;
                        log::Info("[MEVR-SIGHT] first scope picture: the {} eye aims ({:.3f} m behind the eyepiece, {:.4f} m off the "
                                  "line, pupil {:.2f}), {:.1f}x from the {} eye's image ({}x{} rect at ({}, {})), focus {:.1f} m, "
                                  "eyepiece {:.4f} m across",
                            EyeName(gAiming), scopeView->along, gLines[gAiming].lateral, scopeView->pupil, sight.zoom, EyeName(gSource),
                            frame.projectionRect[gSource].extent.width, frame.projectionRect[gSource].extent.height,
                            frame.projectionRect[gSource].offset.x, frame.projectionRect[gSource].offset.y, sight.focusM,
                            2.0f * sight.radius);
                    }
                }
            } else {
                gSource = -1;
                testloop::SetReleaseTap(XR_NULL_HANDLE, nullptr);
            }
        }
    }
    gView = view;
    sv::PublishView(view);
    if (view != gLoggedView) {
        gLoggedView = view;
        log::Info("[MEVR-SIGHT] {}", view == 0 ? std::string("no eye through the sight")
                                               : std::format("{} through the {} sight{}",
                                                     (view & sv::kViewLeft) && (view & sv::kViewRight) ? "both eyes"
                                                     : (view & sv::kViewLeft)                          ? "the left eye"
                                                                                                       : "the right eye",
                                                     sv::Name(kind), (view & sv::kViewScope) ? " (the scope's picture is up)" : ""));
    }
    out.looking = view != 0;
    out.replaceRing = (sight.flags & sv::kReplaceRing) != 0;
    return out;
}

std::string StatusJson() {
    auto const line = [](sv::Line const& l) {
        return std::format("{{\"along\": {:.4f}, \"lateral\": {:.4f}, \"x\": {:.4f}, \"y\": {:.4f}}}", l.along, l.lateral, l.x, l.y);
    };
    return std::format("{{\"kind\": \"{}\", \"weapon\": \"{}\", \"center\": [{:.4f}, {:.4f}, {:.4f}], \"axis\": [{:.4f}, {:.4f}, {:.4f}], "
                       "\"radius\": {:.4f}, \"zoom\": {:.2f}, \"eyes\": [{}, {}], \"looking\": [{}, {}], \"aimingEye\": \"{}\", "
                       "\"sourceEye\": \"{}\", \"scopeDrawn\": {}, \"view\": {}, \"copies\": {}, \"copySize\": [{}, {}], "
                       "\"eyeBox\": {:.4f}, \"dotEye\": \"{}\", \"stereo\": {{\"matched\": [{}, {}], \"halfEyeM\": [{:.4f}, {:.4f}], "
                       "\"shiftDeg\": [{:.3f}, {:.3f}], \"text\": \"{}\", \"correctedEye\": \"{}\", \"correctionDeg\": [{:.3f}, {:.3f}], "
                       "\"correctionCm\": {:.3f}}}}}",
        sv::Name(static_cast<sv::Kind>(gSight.kind)), sv::WeaponName(gSight.weapon), gSight.center.x, gSight.center.y, gSight.center.z,
        gSight.axis.x, gSight.axis.y, gSight.axis.z, gSight.radius, gSight.zoom, line(gLines[0]), line(gLines[1]),
        gDotEye == 0 ? "true" : "false", gDotEye == 1 ? "true" : "false", EyeName(gAiming), EyeName(gSource),
        gScopeDrawn ? "true" : "false", gView, gCopy.copies, gCopy.width, gCopy.height, gSight.eyeBox, EyeName(gDotEye),
        gImages[0].valid ? "true" : "false", gImages[1].valid ? "true" : "false",
        gImages[0].valid ? Distance(gImages[0].renderEye, Mid()) : 0.0f, gImages[1].valid ? Distance(gImages[1].renderEye, Mid()) : 0.0f,
        std::atan(gImages[0].shiftTan) * 180.0f / kPi, std::atan(gImages[1].shiftTan) * 180.0f / kPi, gStereoText,
        EyeName(gCorrectedEye), gCorrection.rightDeg, gCorrection.upDeg, gCorrectionCm);
}

} // namespace mevr::host::sight_layers
