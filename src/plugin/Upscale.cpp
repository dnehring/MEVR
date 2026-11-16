// SPDX-License-Identifier: MIT
// Spatial upscaling of the eye images (plugin/Upscale.hpp, research/32): FSR 1's EASU and RCAS, the settings, the cost.
// One pass for the three games (research/27 "One eye-image pipeline"): LE1 reaches it from Halcyon's eye blit (the
// [MEVR] upscale seam, in Halcyon's pipeline state), the sequels from plugin/EyeCopy (Copy, in a state of our own).

#include "plugin/Upscale.hpp"

#include "core/Embedded.hpp"
#include "core/Log.hpp"
#include "core/vr/Colour.hpp"
#include "core/vr/TestRequest.hpp"
#include "core/d3d11/Blit.hpp"
#include "core/vr/Upscale.hpp"
#if defined(MEVR_WITH_HALCYON)
#include "halcyon/le1/mevr_bridge.h"
#endif
#include "plugin/Colour.hpp"
#include "plugin/Renderer.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

namespace mevr::host::upscale {
namespace {

using Microsoft::WRL::ComPtr;
namespace vu = mevr::vr::upscale;
namespace cv = mevr::vr::colour;

// ---- Settings ([Upscale] in ASI\<GAME>MEVR.ini, re-read once a second on the Present thread) --------------------
std::wstring const& IniPath() {
    static std::wstring path = mevr::host::SettingsPath();  // <exe dir>\ASI\<GAME>MEVR.ini, as the mod
    return path;
}

std::mutex gMutex;  // the settings and status (StatusJson from other threads)
vu::Settings gSettings;
std::uint64_t gNextReadMs = 0;
bool gLoggedSettings = false;
unsigned gLastSrcW = 0, gLastSrcH = 0, gLastDstW = 0, gLastDstH = 0;
bool gActive = false;      // the last eye blit took our pass
double gAverageMs = -1.0;  // the last report's GPU time per eye

void Tick() {
    std::uint64_t const now = GetTickCount64();
    if (now < gNextReadMs) {
        return;
    }
    gNextReadMs = now + 1000;
    std::wstring const& path = IniPath();
    wchar_t method[32]{};
    wchar_t sharpness[32]{};
    if (!path.empty()) {
        GetPrivateProfileStringW(L"Upscale", L"method", L"fsr", method, 32, path.c_str());  // fsr by default since 2026-11-05
        GetPrivateProfileStringW(L"Upscale", L"sharpness", L"0.9", sharpness, 32, path.c_str());
    }
    std::string narrowMethod;
    for (wchar_t const* p = method; *p != L'\0'; ++p) {
        narrowMethod += static_cast<char>(*p < 128 ? *p : L'?');
    }
    vu::Settings const s = vu::Parse(narrowMethod, path.empty() ? 0.9 : std::wcstod(sharpness, nullptr));
    std::lock_guard<std::mutex> const lock(gMutex);
    if (!gLoggedSettings || s.method != gSettings.method || s.sharpness != gSettings.sharpness) {
        gLoggedSettings = true;
        gSettings = s;
        log::Info("upscale: {} ([Upscale] method = {}, sharpness = {:.2f}){}",
            s.method == vu::Method::Fsr ? "a stereo target smaller than the eye is scaled up by FSR 1 (EASU, then RCAS)"
                                        : "Halcyon's own bilinear fill (no upscaling)",
            vu::Name(s.method), s.sharpness,
            s.method == vu::Method::Fsr ? std::format(" (RCAS {:.2f} stops)", vu::RcasStops(s.sharpness)) : "");
    }
}

// ---- Shaders and resources (Present thread) -----------------------------------------------------------------
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT,
    UINT, ID3DBlob**, ID3DBlob**);

D3DCompileFn Compiler() {
    static D3DCompileFn const compile = [] {
        HMODULE const module = LoadLibraryW(L"d3dcompiler_47.dll");
        return module != nullptr ? reinterpret_cast<D3DCompileFn>(GetProcAddress(module, "D3DCompile")) : nullptr;
    }();
    return compile;
}

struct Gpu {
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> easu;
    ComPtr<ID3D11PixelShader> rcasPlain;
    ComPtr<ID3D11PixelShader> rcasCorrected;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Texture2D> middle;  // EASU's output at the eye's size
    ComPtr<ID3D11RenderTargetView> middleRtv;
    ComPtr<ID3D11ShaderResourceView> middleSrv;
    unsigned middleW = 0, middleH = 0;
    DXGI_FORMAT middleFormat = DXGI_FORMAT_UNKNOWN;
    vu::Constants uploaded{};
    bool haveUploaded = false;
    bool ready = false;
    bool failed = false;
};
Gpu gGpu;

bool Compile(std::string const& source, char const* entry, char const* profile, ID3DBlob** blob) {
    auto const compile = Compiler();
    ComPtr<ID3DBlob> errors;
    HRESULT const hr = compile == nullptr ? E_FAIL
        : compile(source.data(), source.size(), "MEVR_Upscale", nullptr, nullptr, entry, profile,
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob, errors.GetAddressOf());
    if (FAILED(hr)) {
        log::Error("upscale: shader {} failed (0x{:08X}): {}: no upscaling", entry, static_cast<std::uint32_t>(hr),
            errors ? static_cast<char const*>(errors->GetBufferPointer()) : "(no message or no d3dcompiler_47.dll)");
        return false;
    }
    return true;
}

bool EnsureShaders(ID3D11Device* device) {
    if (gGpu.ready || gGpu.failed) {
        return gGpu.ready;
    }
    gGpu.failed = true;
    std::string const plain = vu::ShaderSource(std::string_view(mevr::embedded::kFfxA, mevr::embedded::kFfxASize),
        std::string_view(mevr::embedded::kFfxFsr1, mevr::embedded::kFfxFsr1Size));
    std::string const corrected = std::string(cv::ShaderPrelude()) + plain;
    ComPtr<ID3DBlob> vs, easu, rcasPlain, rcasCorrected;
    if (!Compile(plain, "VSMain", "vs_5_0", vs.GetAddressOf()) || !Compile(plain, "PSEasu", "ps_5_0", easu.GetAddressOf()) ||
        !Compile(plain, "PSRcas", "ps_5_0", rcasPlain.GetAddressOf()) ||
        !Compile(corrected, "PSRcas", "ps_5_0", rcasCorrected.GetAddressOf())) {
        return false;
    }
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(vu::Constants);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, gGpu.vs.GetAddressOf())) ||
        FAILED(device->CreatePixelShader(easu->GetBufferPointer(), easu->GetBufferSize(), nullptr, gGpu.easu.GetAddressOf())) ||
        FAILED(device->CreatePixelShader(rcasPlain->GetBufferPointer(), rcasPlain->GetBufferSize(), nullptr, gGpu.rcasPlain.GetAddressOf())) ||
        FAILED(device->CreatePixelShader(rcasCorrected->GetBufferPointer(), rcasCorrected->GetBufferSize(), nullptr,
            gGpu.rcasCorrected.GetAddressOf())) ||
        FAILED(device->CreateBuffer(&cb, nullptr, gGpu.constants.GetAddressOf()))) {
        log::Error("upscale: creating the shaders failed: no upscaling");
        return false;
    }
    gGpu.failed = false;
    gGpu.ready = true;
    log::Info("upscale: FSR 1 shaders ready (EASU, RCAS plain and with the colour correction)");
    return true;
}

bool EnsureMiddle(ID3D11Device* device, unsigned width, unsigned height, DXGI_FORMAT format) {
    if (gGpu.middle != nullptr && gGpu.middleW == width && gGpu.middleH == height && gGpu.middleFormat == format) {
        return true;
    }
    gGpu.middle.Reset();
    gGpu.middleRtv.Reset();
    gGpu.middleSrv.Reset();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = format;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&td, nullptr, gGpu.middle.GetAddressOf())) ||
        FAILED(device->CreateRenderTargetView(gGpu.middle.Get(), nullptr, gGpu.middleRtv.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(gGpu.middle.Get(), nullptr, gGpu.middleSrv.GetAddressOf()))) {
        log::Error("upscale: the {}x{} intermediate couldn't be made: no upscaling", width, height);
        gGpu.failed = true;
        return false;
    }
    gGpu.middleW = width;
    gGpu.middleH = height;
    gGpu.middleFormat = format;
    return true;
}

#if defined(MEVR_WITH_HALCYON)
// The source's size and format (UNORM 8-bit only: the encoded SDR path), cached by view.
struct Source {
    ID3D11ShaderResourceView* view = nullptr;
    unsigned width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool eightBit = false;
};
Source gSource;

bool Describe(ID3D11ShaderResourceView* view, Source& out) {
    if (view == gSource.view && view != nullptr) {
        out = gSource;
        return true;
    }
    ComPtr<ID3D11Resource> resource;
    view->GetResource(resource.GetAddressOf());
    ComPtr<ID3D11Texture2D> texture;
    if (resource == nullptr || FAILED(resource.As(&texture)) || texture == nullptr) {
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
    view->GetDesc(&vd);
    Source s;
    s.view = view;
    s.width = desc.Width;
    s.height = desc.Height;
    s.format = vd.Format;
    s.eightBit = vd.Format == DXGI_FORMAT_R8G8B8A8_UNORM || vd.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
        vd.Format == DXGI_FORMAT_B8G8R8X8_UNORM;
    gSource = s;
    out = s;
    return true;
}

#endif

// ---- Cost: GPU timestamps around the two passes, reported every 10 s ----------------------------------------
struct QuerySet {
    ComPtr<ID3D11Query> disjoint, begin, end;
    bool pending = false;
};
std::array<QuerySet, 8> gQueries;
int gOpenQuery = -1;
double gSumMs = 0.0;
std::uint64_t gCount = 0;
std::uint64_t gReportAtMs = 0;

void PollQueries(ID3D11DeviceContext* context) {
    for (QuerySet& q : gQueries) {
        if (!q.pending) {
            continue;
        }
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        UINT64 t0 = 0, t1 = 0;
        if (context->GetData(q.disjoint.Get(), &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            context->GetData(q.begin.Get(), &t0, sizeof t0, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            context->GetData(q.end.Get(), &t1, sizeof t1, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
            continue;
        }
        q.pending = false;
        if (!dj.Disjoint && dj.Frequency != 0 && t1 >= t0) {
            gSumMs += static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(dj.Frequency);
            ++gCount;
        }
    }
    std::uint64_t const now = GetTickCount64();
    if (gReportAtMs == 0) {
        gReportAtMs = now + 10000;
    } else if (now >= gReportAtMs) {
        gReportAtMs = now + 10000;
        if (gCount != 0) {
            std::lock_guard<std::mutex> const lock(gMutex);
            gAverageMs = gSumMs / static_cast<double>(gCount);
            log::Info("upscale: FSR 1 {}x{} -> {}x{}: {:.3f} ms of GPU per eye, last 10 s ({} eyes)", gLastSrcW, gLastSrcH,
                gLastDstW, gLastDstH, gAverageMs, gCount);
            gSumMs = 0.0;
            gCount = 0;
        }
    }
}

void TimerBegin(ID3D11DeviceContext* context, ID3D11Device* device) {
    PollQueries(context);
    gOpenQuery = -1;
    for (int i = 0; i < static_cast<int>(gQueries.size()); ++i) {
        QuerySet& q = gQueries[i];
        if (q.pending) {
            continue;
        }
        if (q.disjoint == nullptr) {
            D3D11_QUERY_DESC const disjoint{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            D3D11_QUERY_DESC const stamp{D3D11_QUERY_TIMESTAMP, 0};
            if (FAILED(device->CreateQuery(&disjoint, q.disjoint.GetAddressOf())) ||
                FAILED(device->CreateQuery(&stamp, q.begin.GetAddressOf())) ||
                FAILED(device->CreateQuery(&stamp, q.end.GetAddressOf()))) {
                q = QuerySet{};
                return;
            }
        }
        context->Begin(q.disjoint.Get());
        context->End(q.begin.Get());
        gOpenQuery = i;
        return;
    }
}

void TimerEnd(ID3D11DeviceContext* context) {
    if (gOpenQuery < 0) {
        return;
    }
    QuerySet& q = gQueries[gOpenQuery];
    context->End(q.end.Get());
    context->End(q.disjoint.Get());
    q.pending = true;
    gOpenQuery = -1;
}

// The two passes, in the pipeline state the caller set for a full-screen triangle of the eye's size (viewport, blend,
// depth, raster, topology, layout): EASU, the source into the intermediate at the eye's size, then RCAS, the
// intermediate into the eye with the colour correction at the end when it's on. `constants` is uploaded already when
// `shiftPx` is 0. The sequels' convergence shift (`shiftPx`, plugin/EyeCopy's policy: the unshifted picture, then the
// shifted one over it, so the vacated strip keeps the unshifted edge) is two EASU draws, each in its own columns: the
// pass reads its target pixel from SV_Position, so a narrower viewport is a scissor.
void Passes(ID3D11DeviceContext* context, ID3D11Device* device, ID3D11ShaderResourceView* source, ID3D11SamplerState* sampler,
    ID3D11Buffer* fillConstants, ID3D11RenderTargetView* eyeRtv, unsigned eyeWidth, unsigned eyeHeight,
    vu::Constants const& constants, int shiftPx, bool linearSource, int eye) {
    ID3D11Buffer* savedB2 = nullptr;
    context->PSGetConstantBuffers(2, 1, &savedB2);
    TimerBegin(context, device);

    ID3D11Buffer* const b2 = gGpu.constants.Get();
    ID3D11ShaderResourceView* nullSrv = nullptr;
    ID3D11RenderTargetView* const middleRtv = gGpu.middleRtv.Get();
    context->OMSetRenderTargets(1, &middleRtv, nullptr);
    context->VSSetShader(gGpu.vs.Get(), nullptr, 0);
    context->PSSetShader(gGpu.easu.Get(), nullptr, 0);
    context->PSSetShaderResources(0, 1, &source);
    context->PSSetSamplers(0, 1, &sampler);
    context->PSSetConstantBuffers(0, 1, &fillConstants);
    context->PSSetConstantBuffers(2, 1, &b2);
    if (shiftPx == 0) {
        context->Draw(3, 0);
    } else {
        float const width = static_cast<float>(eyeWidth);
        float const height = static_cast<float>(eyeHeight);
        float const strip = static_cast<float>(shiftPx > 0 ? shiftPx : -shiftPx);
        // The vacated strip, unshifted: the left columns for a shift to the right, the right ones for one to the left.
        D3D11_VIEWPORT const vacated{shiftPx > 0 ? 0.0f : width - strip, 0.0f, strip, height, 0.0f, 1.0f};
        D3D11_VIEWPORT const moved{shiftPx > 0 ? strip : 0.0f, 0.0f, width - strip, height, 0.0f, 1.0f};
        D3D11_VIEWPORT const whole{0.0f, 0.0f, width, height, 0.0f, 1.0f};
        context->UpdateSubresource(gGpu.constants.Get(), 0, nullptr, &constants, 0, 0);
        context->RSSetViewports(1, &vacated);
        context->Draw(3, 0);
        vu::Constants const shifted = vu::Shifted(constants, shiftPx);
        context->UpdateSubresource(gGpu.constants.Get(), 0, nullptr, &shifted, 0, 0);
        gGpu.uploaded = shifted;
        gGpu.haveUploaded = true;
        context->RSSetViewports(1, &moved);
        context->Draw(3, 0);
        context->RSSetViewports(1, &whole);
    }
    context->PSSetShaderResources(0, 1, &nullSrv);

    context->OMSetRenderTargets(1, &eyeRtv, nullptr);
    bool const corrected = mevr::host::colour::BeginEye(context, eye, linearSource);
    context->PSSetShader(corrected ? gGpu.rcasCorrected.Get() : gGpu.rcasPlain.Get(), nullptr, 0);
    ID3D11ShaderResourceView* const middleSrv = gGpu.middleSrv.Get();
    context->PSSetShaderResources(0, 1, &middleSrv);
    context->Draw(3, 0);
    mevr::host::colour::EndEye(context);
    context->PSSetShaderResources(0, 1, &nullSrv);

    TimerEnd(context);
    context->PSSetConstantBuffers(2, 1, &savedB2);
    if (savedB2 != nullptr) {
        savedB2->Release();
    }
}

void Upload(ID3D11DeviceContext* context, vu::Constants const& k) {
    if (!gGpu.haveUploaded || std::memcmp(&k, &gGpu.uploaded, sizeof k) != 0) {
        context->UpdateSubresource(gGpu.constants.Get(), 0, nullptr, &k, 0, 0);
        gGpu.uploaded = k;
        gGpu.haveUploaded = true;
    }
}

void NoteActive(unsigned srcW, unsigned srcH, unsigned dstW, unsigned dstH) {
    std::lock_guard<std::mutex> const lock(gMutex);
    gLastSrcW = srcW;
    gLastSrcH = srcH;
    gLastDstW = dstW;
    gLastDstH = dstH;
    gActive = true;
}

#if !defined(MEVR_WITH_HALCYON)
// ---- The sequels' pass (plugin/EyeCopy): a pipeline state of our own (core/d3d11/Blit's) ----------------------------
// Halcyon's FillParams (b0) as the shaders read it: the whole source into the whole image, no HDR scale.
struct FillParams {
    float fillUv[4];
    float imageRect[4];
    float colorScale[4];
};
constexpr FillParams kWholeImage{{0.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};
ComPtr<ID3D11Buffer> gWholeFill;

struct CopyJob {
    ID3D11Device* device;
    ID3D11RenderTargetView* target;
    unsigned width, height;
    vu::Constants constants;
    int shiftPx;
    int eye;
};

bool CopyPasses(d3d11::Blit::Stage const& stage, void* user) {
    CopyJob const& job = *static_cast<CopyJob const*>(user);
    D3D11_VIEWPORT const whole{0.0f, 0.0f, static_cast<float>(job.width), static_cast<float>(job.height), 0.0f, 1.0f};
    stage.context->RSSetViewports(1, &whole);
    if (job.shiftPx == 0) {
        Upload(stage.context, job.constants);
    }
    Passes(stage.context, job.device, stage.source, stage.sampler, gWholeFill.Get(), job.target, job.width, job.height,
        job.constants, job.shiftPx, false, job.eye);
    return true;
}

#endif
} // namespace

std::string StatusJson() {
    std::lock_guard<std::mutex> const lock(gMutex);
    return std::format("{{\"method\": {}, \"sharpness\": {:.3f}, \"active\": {}, \"source\": [{}, {}], \"target\": [{}, {}], "
        "\"gpuMs\": {}}}", vr::test_request::JsonString(vu::Name(gSettings.method)), gSettings.sharpness,
        gActive ? "true" : "false", gLastSrcW, gLastSrcH, gLastDstW, gLastDstH,
        gAverageMs >= 0.0 ? vr::test_request::JsonNumber(gAverageMs) : "null");
}


vr::upscale::Size EyeImageSize(unsigned sourceWidth, unsigned sourceHeight, unsigned recommendedWidth,
    unsigned recommendedHeight) {
    Tick();
    vu::Settings settings;
    {
        std::lock_guard<std::mutex> const lock(gMutex);
        settings = gSettings;
    }
    vu::Size const size = vu::EyeImageSize(settings, sourceWidth, sourceHeight, recommendedWidth, recommendedHeight);
    log::Info("upscale: the eye images are {}x{} for a {}x{} picture (the headset recommends {}x{}; [Upscale] method = {}): {}",
        size.width, size.height, sourceWidth, sourceHeight, recommendedWidth, recommendedHeight, vu::Name(settings.method),
        size.width != sourceWidth || size.height != sourceHeight ? "FSR 1 scales each eye up at the copy"
                                                                 : "the picture's own size, copied as it is");
    return size;
}

#if !defined(MEVR_WITH_HALCYON)  // LE1 has no plugin/EyeCopy: its blit reaches Passes through the seam below
bool Copy(ID3D11DeviceContext* context, d3d11::Blit& blit, ID3D11Texture2D* source, ID3D11RenderTargetView* target,
    unsigned sourceWidth, unsigned sourceHeight, DXGI_FORMAT sourceFormat, unsigned width, unsigned height, int shiftX,
    int eye) {
    Tick();
    vu::Settings settings;
    {
        std::lock_guard<std::mutex> const lock(gMutex);
        settings = gSettings;
        gActive = false;
    }
    bool const eightBit = sourceFormat == DXGI_FORMAT_R8G8B8A8_UNORM || sourceFormat == DXGI_FORMAT_B8G8R8A8_UNORM ||
        sourceFormat == DXGI_FORMAT_B8G8R8X8_UNORM;
    if (settings.method != vu::Method::Fsr || context == nullptr || source == nullptr || target == nullptr || !eightBit ||
        sourceWidth >= width || sourceHeight >= height || gGpu.failed) {
        return false;  // not a smaller 8-bit picture: the caller's own copy
    }
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    if (device == nullptr || !EnsureShaders(device.Get()) || !EnsureMiddle(device.Get(), width, height, sourceFormat)) {
        return false;
    }
    if (gWholeFill == nullptr) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(FillParams);
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA const data{&kWholeImage, 0, 0};
        if (FAILED(device->CreateBuffer(&desc, &data, gWholeFill.GetAddressOf()))) {
            gGpu.failed = true;
            log::Error("upscale: the fill constants couldn't be made: no upscaling");
            return false;
        }
    }
    CopyJob job{device.Get(), target, width, height,
        vu::MakeConstants(sourceWidth, sourceHeight, width, height, kWholeImage.fillUv, kWholeImage.imageRect,
            settings.sharpness),
        shiftX, eye};
    if (!blit.Run(context, source, &CopyPasses, &job)) {
        return false;
    }
    NoteActive(sourceWidth, sourceHeight, width, height);
    return true;
}

#endif
} // namespace mevr::host::upscale

#if defined(MEVR_WITH_HALCYON)
// ---- The [MEVR] upscale seam in Halcyon's eye blit (src/halcyon/le1/mevr_bridge.h) ---------------------------
namespace MELEVR::MevrBridge {

bool EyeUpscale(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, ID3D11SamplerState* sampler,
    ID3D11Buffer* fillConstants, ID3D11RenderTargetView* eyeRtv, unsigned eyeWidth, unsigned eyeHeight,
    const float fill[12], bool linearSource, int eye) noexcept {
    using namespace mevr::host::upscale;
    Tick();
    vu::Settings settings;
    {
        std::lock_guard<std::mutex> const lock(gMutex);
        settings = gSettings;
        gActive = false;
    }
    if (settings.method != vu::Method::Fsr || linearSource || context == nullptr || source == nullptr || eyeRtv == nullptr ||
        fillConstants == nullptr || eyeWidth == 0 || eyeHeight == 0 || gGpu.failed) {
        return false;
    }
    Source src;
    if (!Describe(source, src) || !src.eightBit || src.width >= eyeWidth || src.height >= eyeHeight) {
        return false;  // not a smaller 8-bit image: Halcyon's fill
    }
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    if (device == nullptr || !EnsureShaders(device.Get()) || !EnsureMiddle(device.Get(), eyeWidth, eyeHeight, src.format)) {
        return false;
    }
    vu::Constants const k = vu::MakeConstants(src.width, src.height, eyeWidth, eyeHeight, fill, fill + 4, settings.sharpness);
    Upload(context, k);
    NoteActive(src.width, src.height, eyeWidth, eyeHeight);
    // Halcyon set the viewport, blend, depth, raster, topology and layout for a full-screen triangle of this size already.
    Passes(context, device.Get(), source, sampler, fillConstants, eyeRtv, eyeWidth, eyeHeight, k, 0, linearSource, eye);
    return true;
}

} // namespace MELEVR::MevrBridge
#endif
