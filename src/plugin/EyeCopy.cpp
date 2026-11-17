// SPDX-License-Identifier: MIT
#include "plugin/EyeCopy.hpp"

#include "core/Log.hpp"
#include "core/d3d11/Blit.hpp"
#include "core/vr/Colour.hpp"
#include "plugin/Colour.hpp"
#include "plugin/Upscale.hpp"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstring>
#include <algorithm>
#include <format>
#include <string>
#include <unordered_map>

namespace mevr::host::eye_copy {
namespace {

using Microsoft::WRL::ComPtr;

// Present thread only.
d3d11::Blit* gBlit = nullptr;  // never destroyed at process exit: no D3D releases then
ComPtr<ID3D11Device> gDevice;
bool gBlitFailed = false;
std::unordered_map<ID3D11Texture2D*, ComPtr<ID3D11RenderTargetView>> gViews;
std::unordered_map<ID3D11Texture2D*, ComPtr<ID3D11RenderTargetView>> gUnormViews;
bool gToldDraw = false;
bool gToldRefused = false;
bool gToldUpscale = false;

// The colour correction's pass (core/vr/Colour's prelude in front): PSPlain reads the source as the pass wants it (an 8-bit
// source's encoded values, or a float source's linear light), PSDecode decodes an 8-bit source first (drawn through an sRGB
// view). Blit's vertex shader gives SV_Position (the dither's pixel) and the uv. MEVR_TONE is LE1's ToEye step (research/27
// 7b): for a float source the hue-preserving clip or [Colour] tonemap's curve, for an 8-bit one (values within 0..1, the
// constants' linear flag off) the identity, bit for bit. An 8-bit source decoded first never gets a curve, as on LE1.
constexpr char kCorrectedShader[] = R"(
Texture2D mevrEyeSource : register(t0);
SamplerState mevrEyeSampler : register(s0);
struct MevrEyeIn { float4 position : SV_Position; float2 uv : TEXCOORD0; };
float4 PSPlain(MevrEyeIn i) : SV_Target {
    return MEVR_EYE_OUT(float4(MEVR_TONE(mevrEyeSource.Sample(mevrEyeSampler, i.uv).rgb), 1.0), i.position);
}
float4 PSDecode(MevrEyeIn i) : SV_Target {
    return MEVR_EYE_OUT(float4(MevrDecode(mevrEyeSource.Sample(mevrEyeSampler, i.uv).rgb), 1.0), i.position);
}
)";
struct Corrected {
    ComPtr<ID3D11PixelShader> plain;
    ComPtr<ID3D11PixelShader> decode;
    ComPtr<ID3D11Buffer> constants;
    vr::colour::Constants uploaded{};
    bool haveUploaded = false;
    bool tried = false;
    bool ready = false;
};
Corrected gCorrected;
std::string gToldCorrected;  // the last "drawn corrected" line, logged when it changes

// The typeless parent of a format (what CopyResource compares), or the format itself.
DXGI_FORMAT Family(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8X8_TYPELESS;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
        return DXGI_FORMAT_R10G10B10A2_TYPELESS;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
        return DXGI_FORMAT_R16G16B16A16_TYPELESS;
    default:
        return f;
    }
}

// The sRGB view the blit draws through (it writes linear light), or UNKNOWN for an image it can't draw into right.
DXGI_FORMAT SrgbView(DXGI_FORMAT image) {
    switch (image) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

ID3D11RenderTargetView* ViewOf(ID3D11Texture2D* image, D3D11_TEXTURE2D_DESC const& desc);

// The UNORM view an 8-bit source's encoded values are written through unchanged (as the raw copy leaves them), or
// UNKNOWN when the image has none (an image made in an _SRGB format).
DXGI_FORMAT UnormView(DXGI_FORMAT image) {
    switch (image) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

ID3D11RenderTargetView* UnormViewOf(ID3D11Texture2D* image, D3D11_TEXTURE2D_DESC const& desc) {
    auto const it = gUnormViews.find(image);
    if (it != gUnormViews.end()) {
        return it->second.Get();
    }
    D3D11_RENDER_TARGET_VIEW_DESC view{};
    view.Format = UnormView(desc.Format);
    view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (view.Format == DXGI_FORMAT_UNKNOWN || (desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0 ||
        FAILED(gDevice->CreateRenderTargetView(image, &view, &rtv))) {
        rtv.Reset();
    }
    gUnormViews.emplace(image, rtv);
    return rtv.Get();
}

bool EnsureBlit(ID3D11DeviceContext* context, DXGI_FORMAT sourceFormat) {
    ComPtr<ID3D11Device> device;
    context->GetDevice(&device);
    if (gBlit != nullptr && gDevice.Get() != device.Get()) return false;
    if (gBlit == nullptr && !gBlitFailed) {
        auto* blit = new d3d11::Blit;
        if (device != nullptr && blit->Initialize(device.Get())) {
            gBlit = blit;
            gDevice = device;
        } else {
            delete blit;
            gBlitFailed = true;
            log::Error("eye copy: the blit didn't start; an eye whose source is format {} stays as it was",
                static_cast<int>(sourceFormat));
        }
    }
    return gBlit != nullptr;
}

bool EnsureCorrected() {
    if (gCorrected.tried) {
        return gCorrected.ready;
    }
    gCorrected.tried = true;
    std::string const source = std::string(vr::colour::ShaderPrelude()) + kCorrectedShader;
    ComPtr<ID3DBlob> blobs[2];
    char const* const entries[2] = {"PSPlain", "PSDecode"};
    for (int i = 0; i < 2; ++i) {
        ComPtr<ID3DBlob> errors;
        if (FAILED(D3DCompile(source.data(), source.size(), "MEVR_EyeCopyColour", nullptr, nullptr, entries[i], "ps_5_0",
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blobs[i], &errors))) {
            log::Error("eye copy: the colour correction's shader {} failed: {}: the eyes stay uncorrected", entries[i],
                errors ? static_cast<char const*>(errors->GetBufferPointer()) : "(no message)");
            return false;
        }
    }
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(vr::colour::Constants);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(gDevice->CreatePixelShader(blobs[0]->GetBufferPointer(), blobs[0]->GetBufferSize(), nullptr, &gCorrected.plain)) ||
        FAILED(gDevice->CreatePixelShader(blobs[1]->GetBufferPointer(), blobs[1]->GetBufferSize(), nullptr, &gCorrected.decode)) ||
        FAILED(gDevice->CreateBuffer(&desc, nullptr, &gCorrected.constants))) {
        log::Error("eye copy: the colour correction's shaders couldn't be made: the eyes stay uncorrected");
        gCorrected = Corrected{};
        gCorrected.tried = true;
        return false;
    }
    gCorrected.ready = true;
    log::Info("eye copy: the colour correction's pass is ready (plugin/Colour's profile decides when it draws)");
    return true;
}

// The copy with the colour correction (plugin/Colour): false when nothing is to be corrected or it can't draw.
bool DrawCorrected(ID3D11DeviceContext* context, ID3D11Texture2D* image, ID3D11Texture2D* source,
    D3D11_TEXTURE2D_DESC const& from, D3D11_TEXTURE2D_DESC const& to, int shiftX, int eye) {
    vr::colour::Constants k{};
    bool const encoded = d3d11::IsGammaEncodedUnorm(from.Format);
    if (!host::colour::EyeConstants(eye, !encoded, &k)) {
        return false;  // neutral or comparing: the copies as they were
    }
    if (!EnsureBlit(context, from.Format) || !EnsureCorrected()) {
        return false;
    }
    ID3D11RenderTargetView* view = nullptr;
    ID3D11PixelShader* shader = gCorrected.plain.Get();
    bool linear = !encoded;
    char const* how = "its sRGB view (linear light)";
    if (encoded) {
        view = UnormViewOf(image, to);
        how = "its UNORM view (the encoded values)";
        if (view == nullptr) {
            view = ViewOf(image, to);
            shader = gCorrected.decode.Get();
            linear = true;
            how = "its sRGB view (decoded first)";
        }
    } else {
        view = ViewOf(image, to);
    }
    if (view == nullptr) {
        return false;
    }
    k.misc[1] = linear ? 1.0f : 0.0f;
    if (!gCorrected.haveUploaded || std::memcmp(&k, &gCorrected.uploaded, sizeof k) != 0) {
        context->UpdateSubresource(gCorrected.constants.Get(), 0, nullptr, &k, 0, 0);
        gCorrected.uploaded = k;
        gCorrected.haveUploaded = true;
    }
    if (!gBlit->DrawWith(context, source, view, to.Width, to.Height, shiftX, shader, gCorrected.constants.Get())) return false;
    std::string const line = std::format("eye copy: the colour correction draws the game's picture ({}x{}, format {}) into the "
                                         "images ({}x{}, format {}) through {}",
        from.Width, from.Height, static_cast<int>(from.Format), to.Width, to.Height, static_cast<int>(to.Format), how);
    if (line != gToldCorrected) {
        gToldCorrected = line;
        log::Info("{}", line);
    }
    return true;
}

ID3D11RenderTargetView* ViewOf(ID3D11Texture2D* image, D3D11_TEXTURE2D_DESC const& desc) {
    auto const it = gViews.find(image);
    if (it != gViews.end()) {
        return it->second.Get();
    }
    D3D11_RENDER_TARGET_VIEW_DESC view{};
    view.Format = SrgbView(desc.Format);
    view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (view.Format == DXGI_FORMAT_UNKNOWN || (desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0 ||
        FAILED(gDevice->CreateRenderTargetView(image, &view, &rtv))) {
        rtv.Reset();
    }
    gViews.emplace(image, rtv);  // a failure is remembered as null: not tried again for this image
    return rtv.Get();
}

} // namespace

bool CopyCompatible(DXGI_FORMAT a, DXGI_FORMAT b) {
    return Family(a) == Family(b);
}

bool Copy(ID3D11DeviceContext* context, ID3D11Texture2D* image, ID3D11Texture2D* source, int shiftX, int eye) {
    if (context == nullptr || image == nullptr || source == nullptr || image == source ||
        context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
        return false;
    }
    ComPtr<ID3D11Device> device, sourceDevice, imageDevice;
    context->GetDevice(&device); source->GetDevice(&sourceDevice); image->GetDevice(&imageDevice);
    if (device.Get() != sourceDevice.Get() || device.Get() != imageDevice.Get()) return false;
    D3D11_TEXTURE2D_DESC from{};
    D3D11_TEXTURE2D_DESC to{};
    source->GetDesc(&from);
    image->GetDesc(&to);
    if (from.MipLevels != 1 || from.ArraySize != 1 || to.MipLevels != 1 || to.ArraySize != 1 ||
        to.SampleDesc.Count != 1 || to.Usage != D3D11_USAGE_DEFAULT || to.Width == 0 || to.Height == 0) return false;
    // One convergence policy for both raw and drawn eyes: unshifted fill followed by the
    // shifted overwrite. Clamp without abs(INT_MIN); the vacated strip never stays stale.
    shiftX = std::clamp(shiftX, -static_cast<int>(to.Width - 1), static_cast<int>(to.Width - 1));
    // An 8-bit picture smaller than the image: ME1's eye upscale (plugin/Upscale: FSR 1 with the colour correction at
    // its end, [Upscale] in the game's MEVR.ini), written through the image's UNORM view as LE1's blit writes it.
    if (d3d11::IsGammaEncodedUnorm(from.Format) && from.Width < to.Width && from.Height < to.Height &&
        EnsureBlit(context, from.Format)) {
        ID3D11RenderTargetView* const view = UnormViewOf(image, to);
        if (view != nullptr && upscale::Copy(context, *gBlit, source, view, from.Width, from.Height, from.Format, to.Width,
                to.Height, shiftX, eye)) {
            if (!gToldUpscale) {
                gToldUpscale = true;
                log::Info("eye copy: FSR 1 scales the game's picture ({}x{}, format {}) up into the images ({}x{}, format {}) "
                          "through their UNORM view",
                    from.Width, from.Height, static_cast<int>(from.Format), to.Width, to.Height, static_cast<int>(to.Format));
            }
            return true;
        }
    }
    if (DrawCorrected(context, image, source, from, to, shiftX, eye)) {
        return true;
    }
    if (CopyCompatible(from.Format, to.Format) && from.Width == to.Width && from.Height == to.Height &&
        from.SampleDesc.Count == to.SampleDesc.Count && from.SampleDesc.Quality == to.SampleDesc.Quality) {
        context->CopyResource(image, source);
        if (shiftX != 0) {
            UINT const offset = static_cast<UINT>(shiftX > 0 ? shiftX : -shiftX);
            D3D11_BOX box{shiftX < 0 ? offset : 0u, 0, 0, shiftX < 0 ? from.Width : from.Width - offset, from.Height, 1};
            context->CopySubresourceRegion(image, 0, shiftX > 0 ? offset : 0u, 0, 0, source, 0, &box);
        }
        return SUCCEEDED(device->GetDeviceRemovedReason());
    }
    if (!EnsureBlit(context, from.Format)) return false;
    ID3D11RenderTargetView* const view = gBlit != nullptr ? ViewOf(image, to) : nullptr;
    if (view == nullptr) {
        if (!gToldRefused) {
            gToldRefused = true;
            log::Warn("eye copy: a source of format {} can't be drawn into an image of format {} (bind {:#x}); refused "
                      "without an incompatible raw copy",
                static_cast<int>(from.Format), static_cast<int>(to.Format), to.BindFlags);
        }
        return false;
    }
    if (!gBlit->Draw(context, source, view, to.Width, to.Height, shiftX)) return false;
    if (!gToldDraw) {
        gToldDraw = true;
        log::Info("eye copy: the game's picture ({}x{}, format {}) is drawn into the eye images ({}x{}, format {}) through "
                  "an sRGB view: a full raw copy is unavailable",
            from.Width, from.Height, static_cast<int>(from.Format), to.Width, to.Height, static_cast<int>(to.Format));
    }
    return true;
}

void Reset() {
    gViews.clear();
    gUnormViews.clear();
}

} // namespace mevr::host::eye_copy
