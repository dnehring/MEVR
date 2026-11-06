// SPDX-License-Identifier: MIT
// Renders the gun sights (src/plugin/SightPictures, core/vr/Sights) offline to PNGs, so the scope's picture
// and the reticles can be checked without the game or a headset: the host's own shader on a D3D11 device of
// its own (hardware, else WARP), fed with eye images ray-traced from a test scene.
//
//   mevr-sight-preview [output folder]   (default: .\sight-preview)
//
// The scene (OpenXR axes, meters): a checkered wall 40 m ahead with a bullseye on the sight line, a floor,
// sky, and the gun's scope tube: a dark disc 5 cm across at the eyepiece, 10 cm in front of the right
// (aiming) eye, which blocks that eye's own view of the target (why the scope magnifies the other eye's
// image by default). The left eye is 6.4 cm to the left.
//   eye-left.png, eye-right.png        the two eye images (what the scope samples)
//   scope-<case>.png                    the scope's picture over grey (its alpha as coverage)
//   eye-right-<case>.png                the right eye with the picture drawn in where the eyepiece is
//   eye-right-pulled-*.png              pulled to the eye (research/21 section 14): the model's eyepiece (a glowing blue
//                                       lens in a dark tube, alone in *-model.png) with the picture drawn on it; since
//                                       section 15 the picture shows only under ADS, so *-model.png is also what a
//                                       scope looks like below ADS
//   scope-capture-<case>.png            the scope's picture from a scope capture (research/39), and <case>-target.png
//                                       the capture's own render target (traced from the eyepiece, 1024 px)
//   dot.png, holo.png, frame.png        the reticles and the window ring over grey
//   eye-right-dot.png                   the right eye looking through a red dot sight
//   eye-right-dot-housing.png, eye-right-dot-window*.png, window.png
//                                       a red dot on a solid housing without and with its window (research/21 section 17)
//
//   mevr-sight-preview [output folder] --art <folder>   also the game's own scope view (research/21 section 16), from its
//                                       three textures exported to PNG (tools/scope-art.py says how; game data, local only):
//                                       game-art-reticle.png, game-art-tube.png (the composed layers over grey) and
//                                       scope-game-*.png, eye-right-game-*.png (the picture with them, as the host draws it)

#include "core/PngFile.hpp"
#include "core/vr/LayerComposite.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "core/vr/ScopeCapture.hpp"
#include "core/vr/Sights.hpp"
#include "plugin/SightPictures.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;
namespace sv = mevr::vr::sights;
namespace pic = mevr::host::sight_pictures;
namespace composite = mevr::vr::composite;
namespace fs = std::filesystem;

constexpr int kEyeSize = 2048;
constexpr float kHalfFov = 0.9424778f;  // 54 degrees: Halcyon's symmetric 108-degree render
constexpr XrFovf kFov{-kHalfFov, kHalfFov, kHalfFov, -kHalfFov};
constexpr XrVector3f kRightEye{0.0f, 0.0f, 0.0f};
constexpr XrVector3f kLeftEye{-0.064f, 0.0f, 0.0f};
constexpr XrVector3f kEyepiece{0.0f, 0.0f, -0.10f};
constexpr float kLensRadius = 0.02f;

float Encode(float linear) {
    float const c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return std::clamp(c, 0.0f, 1.0f);
}

std::uint32_t Rgb(float r, float g, float b) {  // linear in, sRGB bytes out, opaque
    auto byte = [](float v) { return static_cast<std::uint32_t>(Encode(v) * 255.0f + 0.5f); };
    return byte(r) | (byte(g) << 8) | (byte(b) << 16) | 0xFF000000u;
}

// The scope tube's rear face in the scene: a dark disc of `rim` radius at `z` (on the right eye's line), and with `lens`
// a glowing blue eyepiece of that radius in its middle (the model's lens the pulled picture must cover; section 14).
struct Tube {
    float z = kEyepiece.z;
    float rim = 0.025f;
    float lens = 0.0f;
};
Tube gTube;

// The test scene, seen along a ray from `o` in direction `d` (not unit).
std::uint32_t Trace(XrVector3f o, XrVector3f d) {
    // The scope tube's rear face, facing back.
    if (d.z < 0.0f) {
        float const t = (gTube.z - o.z) / d.z;
        float const x = o.x + d.x * t - kEyepiece.x;
        float const y = o.y + d.y * t - kEyepiece.y;
        float const r2 = x * x + y * y;
        if (t > 0.0f && r2 < gTube.lens * gTube.lens) {
            return Rgb(0.10f, 0.45f, 1.0f);  // the model's glowing lens
        }
        if (t > 0.0f && r2 < gTube.rim * gTube.rim) {
            return Rgb(0.02f, 0.02f, 0.025f);
        }
    }
    float best = 1e9f;
    std::uint32_t colour = Rgb(0.35f, 0.55f, 0.9f);  // sky
    if (d.z < 0.0f) {  // the wall 40 m ahead
        float const t = (-40.0f - o.z) / d.z;
        float const x = o.x + d.x * t;
        float const y = o.y + d.y * t;
        if (t > 0.0f && y > -1.6f && y < 8.0f) {
            best = t;
            float const r = std::sqrt(x * x + y * y);
            if (r < 1.0f) {  // the bullseye: rings 0.2 m wide, a black centre 5 cm across
                int const ring = static_cast<int>(r / 0.2f);
                colour = r < 0.025f ? Rgb(0.0f, 0.0f, 0.0f) : (ring % 2 == 0 ? Rgb(0.8f, 0.05f, 0.03f) : Rgb(0.9f, 0.9f, 0.9f));
            } else {
                bool const check = (static_cast<int>(std::floor(x)) + static_cast<int>(std::floor(y))) % 2 == 0;
                colour = check ? Rgb(0.45f, 0.45f, 0.42f) : Rgb(0.2f, 0.2f, 0.19f);
            }
        }
    }
    if (d.y < 0.0f) {  // the floor, 1.6 m down
        float const t = (-1.6f - o.y) / d.y;
        if (t > 0.0f && t < best) {
            float const x = o.x + d.x * t;
            float const z = o.z + d.z * t;
            bool const check = (static_cast<int>(std::floor(x)) + static_cast<int>(std::floor(z))) % 2 == 0;
            colour = check ? Rgb(0.15f, 0.25f, 0.1f) : Rgb(0.1f, 0.18f, 0.07f);
        }
    }
    return colour;
}

// The scene from `eye` looking along -Z through `fov`, `size` pixels square.
composite::Image TraceImage(XrVector3f eye, XrFovf const& fov, int size) {
    composite::Image image;
    image.width = image.height = size;
    image.pixels.resize(static_cast<size_t>(size) * size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float const u = (static_cast<float>(x) + 0.5f) / size;
            float const v = (static_cast<float>(y) + 0.5f) / size;
            image.pixels[static_cast<size_t>(y) * size + x] = Trace(eye, composite::EyeRay(fov, u, v));
        }
    }
    return image;
}

composite::Image EyeImage(XrVector3f eye) {
    return TraceImage(eye, kFov, kEyeSize);
}

// Premultiplied sRGB pixels over a mid grey (for looking at a quad's own picture).
std::vector<std::uint32_t> OverGrey(std::vector<std::uint32_t> const& premultiplied) {
    std::vector<std::uint32_t> out(premultiplied.size());
    for (size_t i = 0; i < premultiplied.size(); ++i) {
        std::uint32_t const p = premultiplied[i];
        float const a = static_cast<float>(p >> 24) / 255.0f;
        auto channel = [&](int shift) {
            float const c = static_cast<float>((p >> shift) & 0xFFu) / 255.0f;
            return static_cast<std::uint32_t>(std::clamp(c + 0.5f * (1.0f - a), 0.0f, 1.0f) * 255.0f + 0.5f) << shift;
        };
        out[i] = channel(0) | channel(8) | channel(16) | 0xFF000000u;
    }
    return out;
}

// Premultiplied to straight alpha (LayerComposite's quads).
composite::Image Straight(std::vector<std::uint32_t> const& premultiplied, int size) {
    composite::Image image;
    image.width = image.height = size;
    image.pixels.resize(premultiplied.size());
    for (size_t i = 0; i < premultiplied.size(); ++i) {
        std::uint32_t const p = premultiplied[i];
        std::uint32_t const a = p >> 24;
        auto channel = [&](int shift) {
            std::uint32_t const c = (p >> shift) & 0xFFu;
            return (a == 0 ? 0u : std::min(255u, (c * 255u + a / 2) / a)) << shift;
        };
        image.pixels[i] = channel(0) | channel(8) | channel(16) | (a << 24);
    }
    return image;
}

bool Save(fs::path const& dir, char const* name, int width, int height, std::vector<std::uint32_t> const& pixels) {
    std::string error;
    if (!mevr::png::Write(dir / name, width, height, pixels.data(), &error)) {
        std::printf("%s: %s\n", name, error.c_str());
        return false;
    }
    std::printf("wrote %s\n", name);
    return true;
}

struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> params;
    ComPtr<ID3D11SamplerState> sampler;
    // The game's scope view's composition (research/21 section 16) and its two layers, when --art is given.
    ComPtr<ID3D11VertexShader> artVs;
    ComPtr<ID3D11PixelShader> artPs;
    ComPtr<ID3D11Buffer> artParams;
    ComPtr<ID3D11ShaderResourceView> artLayers[2];
};

bool Start(Gpu& gpu) {
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        gpu.device.GetAddressOf(), nullptr, gpu.context.GetAddressOf());
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, gpu.device.GetAddressOf(),
            nullptr, gpu.context.GetAddressOf());
    }
    if (FAILED(hr)) {
        std::printf("no D3D11 device\n");
        return false;
    }
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> errors;
    auto compile = [&](char const* entry, char const* profile, ComPtr<ID3DBlob>& blob) {
        if (FAILED(D3DCompile(pic::kScopeShader, pic::kScopeShaderSize, "MEVR_Scope", nullptr, nullptr, entry, profile,
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), errors.ReleaseAndGetAddressOf()))) {
            std::printf("%s: %s\n", entry, errors ? static_cast<char const*>(errors->GetBufferPointer()) : "?");
            return false;
        }
        return true;
    };
    if (!compile("VSMain", "vs_5_0", vsBlob) || !compile("PSMain", "ps_5_0", psBlob)) {
        return false;
    }
    ComPtr<ID3DBlob> artVsBlob;
    ComPtr<ID3DBlob> artPsBlob;
    auto compileArt = [&](char const* entry, char const* profile, ComPtr<ID3DBlob>& blob) {
        if (FAILED(D3DCompile(pic::kArtShader, pic::kArtShaderSize, "MEVR_ScopeArt", nullptr, nullptr, entry, profile,
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), errors.ReleaseAndGetAddressOf()))) {
            std::printf("%s: %s\n", entry, errors ? static_cast<char const*>(errors->GetBufferPointer()) : "?");
            return false;
        }
        return true;
    };
    if (!compileArt("VSMain", "vs_5_0", artVsBlob) || !compileArt("PSCompose", "ps_5_0", artPsBlob)) {
        return false;
    }
    D3D11_BUFFER_DESC acb{};
    acb.ByteWidth = sizeof(pic::ArtParams);
    acb.Usage = D3D11_USAGE_DEFAULT;
    acb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(gpu.device->CreateVertexShader(artVsBlob->GetBufferPointer(), artVsBlob->GetBufferSize(), nullptr, gpu.artVs.GetAddressOf())) ||
        FAILED(gpu.device->CreatePixelShader(artPsBlob->GetBufferPointer(), artPsBlob->GetBufferSize(), nullptr, gpu.artPs.GetAddressOf())) ||
        FAILED(gpu.device->CreateBuffer(&acb, nullptr, gpu.artParams.GetAddressOf()))) {
        return false;
    }
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(pic::ScopeParams);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    return SUCCEEDED(gpu.device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, gpu.vs.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, gpu.ps.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreateBuffer(&cb, nullptr, gpu.params.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreateSamplerState(&sd, gpu.sampler.GetAddressOf()));
}

// The eye image as the game's eye swapchain holds it: sRGB bytes, read through an sRGB view.
ComPtr<ID3D11ShaderResourceView> Upload(Gpu& gpu, composite::Image const& image) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(image.width);
    td.Height = static_cast<UINT>(image.height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{image.pixels.data(), static_cast<UINT>(image.width) * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> srv;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
    vd.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    vd.Texture2D.MipLevels = 1;
    if (FAILED(gpu.device->CreateTexture2D(&td, &data, texture.GetAddressOf())) ||
        FAILED(gpu.device->CreateShaderResourceView(texture.Get(), &vd, srv.GetAddressOf()))) {
        return nullptr;
    }
    return srv;
}

// The game's three textures from PNGs (gamma-encoded RGBA, as the game holds them), composed into the two layers the host
// makes (plugin/SightLayers PrepareArt: the same shader and constants), with mips. False if any is missing.
bool ComposeArt(Gpu& gpu, fs::path const& dir) {
    namespace art = mevr::vr::scope_art;
    ComPtr<ID3D11ShaderResourceView> sources[art::kTextures];
    for (int k = 0; k < art::kTextures; ++k) {
        int width = 0;
        int height = 0;
        std::vector<std::uint32_t> pixels;
        std::string error;
        fs::path const file = dir / (std::string(art::Info(k).name) + ".png");
        if (!mevr::png::Read(file, width, height, pixels, &error)) {
            std::printf("--art: %s: %s\n", file.string().c_str(), error.c_str());
            return false;
        }
        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(width);
        td.Height = static_cast<UINT>(height);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(), static_cast<UINT>(width) * 4, 0};
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(gpu.device->CreateTexture2D(&td, &data, texture.GetAddressOf())) ||
            FAILED(gpu.device->CreateShaderResourceView(texture.Get(), nullptr, sources[k].GetAddressOf()))) {
            return false;
        }
    }
    float const decode[art::kTextures] = {2.2f, 2.2f, 2.2f};
    ID3D11DeviceContext* const c = gpu.context.Get();
    for (int l = 0; l < art::kLayers; ++l) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = td.Height = pic::kArtLayerSize;
        td.MipLevels = 0;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11RenderTargetView> rtv;
        if (FAILED(gpu.device->CreateTexture2D(&td, nullptr, texture.GetAddressOf())) ||
            FAILED(gpu.device->CreateRenderTargetView(texture.Get(), nullptr, rtv.GetAddressOf())) ||
            FAILED(gpu.device->CreateShaderResourceView(texture.Get(), nullptr, gpu.artLayers[l].ReleaseAndGetAddressOf()))) {
            return false;
        }
        pic::ArtParams const params = pic::MakeArtParams(static_cast<art::Layer>(l), decode);
        if (!(params.info[0] > 0.0f)) {
            return false;
        }
        c->UpdateSubresource(gpu.artParams.Get(), 0, nullptr, &params, 0, 0);
        D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(pic::kArtLayerSize), static_cast<float>(pic::kArtLayerSize), 0.0f, 1.0f};
        ID3D11RenderTargetView* target = rtv.Get();
        ID3D11ShaderResourceView* views[art::kTextures] = {sources[0].Get(), sources[1].Get(), sources[2].Get()};
        ID3D11Buffer* constants = gpu.artParams.Get();
        ID3D11SamplerState* sampler = gpu.sampler.Get();
        c->OMSetRenderTargets(1, &target, nullptr);
        c->RSSetViewports(1, &viewport);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(gpu.artVs.Get(), nullptr, 0);
        c->PSSetShader(gpu.artPs.Get(), nullptr, 0);
        c->PSSetShaderResources(0, art::kTextures, views);
        c->PSSetConstantBuffers(0, 1, &constants);
        c->PSSetSamplers(0, 1, &sampler);
        c->Draw(3, 0);
        ID3D11RenderTargetView* none = nullptr;
        c->OMSetRenderTargets(1, &none, nullptr);
        c->GenerateMips(gpu.artLayers[l].Get());
    }
    return true;
}

// A composed layer (premultiplied linear float) as sRGB pixels over grey, to look at.
std::vector<std::uint32_t> LayerOverGrey(Gpu& gpu, ID3D11ShaderResourceView* layer) {
    ComPtr<ID3D11Resource> resource;
    layer->GetResource(resource.GetAddressOf());
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = pic::kArtLayerSize;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(gpu.device->CreateTexture2D(&td, nullptr, staging.GetAddressOf()))) {
        return {};
    }
    gpu.context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, resource.Get(), 0, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(gpu.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return {};
    }
    auto const half = [](std::uint16_t h) {
        std::uint32_t const sign = (h >> 15) & 1u;
        std::uint32_t const exponent = (h >> 10) & 0x1Fu;
        std::uint32_t const mantissa = h & 0x3FFu;
        float const v = exponent == 0 ? std::ldexp(static_cast<float>(mantissa), -24)
            : exponent == 31          ? 65504.0f
                                      : std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
        return sign != 0 ? -v : v;
    };
    std::vector<std::uint32_t> out(static_cast<size_t>(pic::kArtLayerSize) * pic::kArtLayerSize);
    for (UINT y = 0; y < pic::kArtLayerSize; ++y) {
        auto const* row = reinterpret_cast<std::uint16_t const*>(static_cast<std::uint8_t const*>(mapped.pData) + y * mapped.RowPitch);
        for (UINT x = 0; x < pic::kArtLayerSize; ++x) {
            float const a = half(row[4 * x + 3]);
            float const grey = 0.18f * (1.0f - a);  // premultiplied over a linear mid grey
            out[static_cast<size_t>(y) * pic::kArtLayerSize + x] =
                Rgb(half(row[4 * x]) + grey, half(row[4 * x + 1]) + grey, half(row[4 * x + 2]) + grey);
        }
    }
    gpu.context->Unmap(staging.Get(), 0);
    return out;
}

// One scope picture (premultiplied sRGB pixels, `size` square), as the host draws it (with the game's layers when given).
std::vector<std::uint32_t> RenderScope(Gpu& gpu, ID3D11ShaderResourceView* source, pic::ScopeParams const& params, UINT size,
    bool gameArt = false) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = size;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> rtv;
    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(gpu.device->CreateTexture2D(&td, nullptr, target.GetAddressOf())) ||
        FAILED(gpu.device->CreateRenderTargetView(target.Get(), nullptr, rtv.GetAddressOf())) ||
        FAILED(gpu.device->CreateTexture2D(&sd, nullptr, staging.GetAddressOf()))) {
        return {};
    }
    ID3D11DeviceContext* const c = gpu.context.Get();
    c->UpdateSubresource(gpu.params.Get(), 0, nullptr, &params, 0, 0);
    D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f};
    ID3D11RenderTargetView* targets = rtv.Get();
    ID3D11Buffer* constants = gpu.params.Get();
    ID3D11SamplerState* sampler = gpu.sampler.Get();
    c->OMSetRenderTargets(1, &targets, nullptr);
    c->RSSetViewports(1, &viewport);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(gpu.vs.Get(), nullptr, 0);
    c->PSSetShader(gpu.ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView* views[3] = {source, gameArt ? gpu.artLayers[0].Get() : nullptr, gameArt ? gpu.artLayers[1].Get() : nullptr};
    c->PSSetShaderResources(0, 3, views);
    c->PSSetConstantBuffers(0, 1, &constants);
    c->PSSetSamplers(0, 1, &sampler);
    c->Draw(3, 0);
    c->CopyResource(staging.Get(), target.Get());
    std::vector<std::uint32_t> pixels(static_cast<size_t>(size) * size);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(c->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return {};
    }
    for (UINT y = 0; y < size; ++y) {
        std::memcpy(pixels.data() + static_cast<size_t>(y) * size, static_cast<std::uint8_t const*>(mapped.pData) + y * mapped.RowPitch,
            size * 4);
    }
    c->Unmap(staging.Get(), 0);
    return pixels;
}

struct Case {
    char const* name;
    XrVector3f eye;      // the aiming (right) eye
    bool otherEye;       // magnify the left eye's image
    sv::ScopeReticle reticle;
    float zoom;
    bool virtualEyepiece = false;  // research/21 section 13: the picture 22 degrees across wherever the eye is behind it
};

} // namespace

int main(int argc, char** argv) {
    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    fs::path dir = "sight-preview";
    fs::path artDir;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--art" && i + 1 < argc) {
            artDir = argv[++i];
        } else {
            dir = argv[i];
        }
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    Gpu gpu;
    if (!Start(gpu)) {
        return 1;
    }
    composite::Image const right = EyeImage(kRightEye);
    composite::Image const left = EyeImage(kLeftEye);
    Save(dir, "eye-right.png", right.width, right.height, right.pixels);
    Save(dir, "eye-left.png", left.width, left.height, left.pixels);
    ComPtr<ID3D11ShaderResourceView> const rightSrv = Upload(gpu, right);
    ComPtr<ID3D11ShaderResourceView> const leftSrv = Upload(gpu, left);

    sv::Sight sight;
    sight.kind = static_cast<std::int32_t>(sv::Kind::Scope);
    sight.center = kEyepiece;
    sight.axis = {0.0f, 0.0f, -1.0f};
    sight.up = {0.0f, 1.0f, 0.0f};
    sight.radius = kLensRadius;
    sight.focusM = 40.0f;
    sight.eyeReliefM = 0.12f;
    sight.scopeColor = 0xFF2020;
    auto const lens = sv::MakeLens(sight.center, sight.axis, sight.up, sight.radius);
    XrPosef const leftPose{{0.0f, 0.0f, 0.0f, 1.0f}, kLeftEye};
    XrPosef const rightPose{{0.0f, 0.0f, 0.0f, 1.0f}, kRightEye};
    XrRect2Di const rect{{0, 0}, {kEyeSize, kEyeSize}};
    constexpr UINT kScopeSize = 768;
    Case const cases[] = {
        {"other-eye", kRightEye, true, sv::ScopeReticle::Duplex, 4.0f},
        {"same-eye", kRightEye, false, sv::ScopeReticle::Duplex, 4.0f},
        {"off-axis", {0.008f, 0.005f, 0.0f}, true, sv::ScopeReticle::Duplex, 4.0f},
        {"far-back", {0.0f, 0.0f, 0.25f}, true, sv::ScopeReticle::Duplex, 4.0f},
        {"mildot-6x", kRightEye, true, sv::ScopeReticle::MilDot, 6.0f},
        {"dot-2x", kRightEye, true, sv::ScopeReticle::Dot, 2.0f},
        // The 2026-10-25 report: the eye 30-60 cm behind the eyepiece. The physical lens there, then the virtual eyepiece.
        {"physical-45cm", {0.0f, 0.0f, 0.35f}, true, sv::ScopeReticle::Duplex, 4.0f},
        {"virtual-30cm", {0.0f, 0.0f, 0.20f}, true, sv::ScopeReticle::Duplex, 4.0f, true},
        {"virtual-45cm", {0.0f, 0.0f, 0.35f}, true, sv::ScopeReticle::Duplex, 4.0f, true},
        {"virtual-60cm", {0.0f, 0.0f, 0.50f}, true, sv::ScopeReticle::Duplex, 4.0f, true},
        {"virtual-45cm-off-2.5cm", {0.025f, 0.0f, 0.35f}, true, sv::ScopeReticle::Duplex, 4.0f, true},
        {"virtual-80cm-fading", {0.0f, 0.0f, 0.70f}, true, sv::ScopeReticle::Duplex, 4.0f, true},
    };
    sight.scopeFieldDeg = 22.0f;
    sight.scopeEyeBox = 0.05f;
    sight.scopeMaxM = 0.70f;
    for (Case const& c : cases) {
        sight.zoom = c.zoom;
        sight.scopeReticle = static_cast<std::int32_t>(c.reticle);
        sv::Lens pictureLens = *lens;
        auto const view = c.virtualEyepiece
            ? sv::VirtualView(*lens, c.eye, sv::Eyepiece{sight.scopeFieldDeg, sight.scopeEyeBox, sight.scopeMaxM}, pictureLens)
            : sv::View(*lens, c.eye, sight.eyeReliefM);
        if (!view) {
            std::printf("%s: the eye isn't behind the lens\n", c.name);
            continue;
        }
        pic::ScopeParams const params = pic::MakeScopeParams(sight, pictureLens, *view, c.eye, c.otherEye ? leftPose : rightPose, kFov, rect,
            kEyeSize, kEyeSize, kScopeSize);
        std::vector<std::uint32_t> const picture =
            RenderScope(gpu, (c.otherEye ? leftSrv : rightSrv).Get(), params, kScopeSize);
        if (picture.empty()) {
            std::printf("%s: render failed\n", c.name);
            continue;
        }
        Save(dir, (std::string("scope-") + c.name + ".png").c_str(), kScopeSize, kScopeSize, OverGrey(picture));
        // The aiming eye's view with the picture where the eyepiece is (as the compositor would show it). The
        // eye image is the centred eye's; the case's eye only moves the picture's content.
        composite::Image eye = right;
        composite::Image const quadImage = Straight(picture, static_cast<int>(kScopeSize));
        sv::Quad const disc = sv::Disc(sight.center, sight.axis, sight.up, pictureLens.radius);
        composite::Quad quad;
        quad.pose = disc.pose;
        quad.width = quad.height = disc.size;
        quad.image = &quadImage;
        composite::Draw(eye, composite::Eye{{{0.0f, 0.0f, 0.0f, 1.0f}, c.eye}, kFov}, quad);
        Save(dir, (std::string("eye-right-") + c.name + ".png").c_str(), eye.width, eye.height, eye.pixels);
        std::printf("  %s: along %.3f m, reticle at (%.2f, %.2f), pupil centre (%.2f, %.2f) picture radii, pupil %.2f, opacity %.2f, "
                    "%.1fx, the picture %.1f degrees across\n",
            c.name, view->along, view->qx, view->qy, view->px, view->py, view->pupil, view->fade, c.zoom,
            2.0 * std::atan(pictureLens.radius / view->along) * 180.0 / 3.14159265);
    }

    // Pulled to the eye (research/21 section 14): the model's eyepiece (a glowing blue lens in a dark tube) on the right
    // eye's line, the picture drawn on it as the host does with the field PulledFieldDeg gives (the eyepiece seen from the
    // relief), at the relief and with the pull at its limit; the rifle's 1.5x on its smaller eyepiece; and the 2026-10-26
    // report's hold without the pull (22 degrees at 0.235 m: the picture spills well past the eyepiece).
    struct Pulled {
        char const* name;
        float eyepieceM;   // the eyepiece this far in front of the right eye
        float lensM;       // the model's eyepiece radius
        float rimM;        // the tube's rear face around it
        float zoom;
        float fieldDeg;    // 0: PulledFieldDeg(lens, relief)
    };
    constexpr float kRelief = 0.12f;
    Pulled const pulledCases[] = {
        {"pulled-sniper-4x", kRelief, 0.020f, 0.028f, 4.0f, 0.0f},
        {"pulled-sniper-at-limit", 0.165f, 0.020f, 0.028f, 4.0f, 0.0f},  // 0.315 m held, pulled 0.15
        // The 2026-10-27 session at section 14's 15 cm limit (the eye 0.16-0.20 m back); under ADS (section 15) the 35 cm
        // pull brings the same hold to the relief: pulled-sniper-4x.
        {"pulled-sniper-old-limit-18cm", 0.18f, 0.020f, 0.028f, 4.0f, 0.0f},
        {"pulled-rifle-1.5x", kRelief, 0.016f, 0.022f, 1.5f, 0.0f},
        {"unpulled-sniper-23cm", 0.235f, 0.020f, 0.028f, 4.0f, 22.0f},
    };
    for (Pulled const& c : pulledCases) {
        gTube = Tube{-c.eyepieceM, c.rimM, c.lensM};
        composite::Image const aiming = EyeImage(kRightEye);
        composite::Image const other = EyeImage(kLeftEye);
        ComPtr<ID3D11ShaderResourceView> const otherSrv = Upload(gpu, other);
        sv::Sight s = sight;
        s.center = {0.0f, 0.0f, -c.eyepieceM};
        s.radius = c.lensM;
        s.zoom = c.zoom;
        s.scopeReticle = static_cast<std::int32_t>(c.zoom < 2.0f ? sv::ScopeReticle::Dot : sv::ScopeReticle::Duplex);
        s.scopeFieldDeg = c.fieldDeg > 0.0f ? c.fieldDeg : sv::PulledFieldDeg(c.lensM, kRelief);
        auto const pulledLens = sv::MakeLens(s.center, s.axis, s.up, s.radius);
        sv::Lens pictureLens = *pulledLens;
        auto const view = sv::VirtualView(*pulledLens, kRightEye, sv::Eyepiece{s.scopeFieldDeg, s.scopeEyeBox, s.scopeMaxM}, pictureLens);
        if (!view) {
            std::printf("%s: no view\n", c.name);
            continue;
        }
        pic::ScopeParams const params =
            pic::MakeScopeParams(s, pictureLens, *view, kRightEye, leftPose, kFov, rect, kEyeSize, kEyeSize, kScopeSize);
        std::vector<std::uint32_t> const picture = RenderScope(gpu, otherSrv.Get(), params, kScopeSize);
        if (picture.empty()) {
            std::printf("%s: render failed\n", c.name);
            continue;
        }
        Save(dir, (std::string("scope-") + c.name + ".png").c_str(), kScopeSize, kScopeSize, OverGrey(picture));
        composite::Image eye = aiming;
        Save(dir, (std::string("eye-right-") + c.name + "-model.png").c_str(), eye.width, eye.height, eye.pixels);
        composite::Image const quadImage = Straight(picture, static_cast<int>(kScopeSize));
        sv::Quad const disc = sv::Disc(s.center, s.axis, s.up, pictureLens.radius);
        composite::Quad quad;
        quad.pose = disc.pose;
        quad.width = quad.height = disc.size;
        quad.image = &quadImage;
        composite::Draw(eye, composite::Eye{rightPose, kFov}, quad);
        Save(dir, (std::string("eye-right-") + c.name + ".png").c_str(), eye.width, eye.height, eye.pixels);
        std::printf("  %s: the eyepiece %.3f m ahead, its lens %.1f cm across (%.1f degrees), the picture %.1f cm across (%.1f degrees; "
                    "field %.1f), %.1fx\n",
            c.name, c.eyepieceM, 2.0f * c.lensM * 100.0f, 2.0 * std::atan(c.lensM / c.eyepieceM) * 180.0 / 3.14159265,
            2.0f * pictureLens.radius * 100.0f, 2.0 * std::atan(pictureLens.radius / view->along) * 180.0 / 3.14159265, s.scopeFieldDeg,
            c.zoom);
    }
    gTube = Tube{};
    // The game's own scope view (research/21 section 16): its composed layers, then the pulled sniper scope (SNPa's eyepiece,
    // 2.1 units at 100 units per meter) at the game's two magnifications, and the eye 1.5 cm off the line (the reticle stays on
    // the target, the tube's shadow creeps in).
    if (!artDir.empty()) {
        if (!ComposeArt(gpu, artDir)) {
            std::printf("--art: the game's scope view couldn't be composed from %s\n", artDir.string().c_str());
            return 1;
        }
        Save(dir, "game-art-reticle.png", pic::kArtLayerSize, pic::kArtLayerSize, LayerOverGrey(gpu, gpu.artLayers[0].Get()));
        Save(dir, "game-art-tube.png", pic::kArtLayerSize, pic::kArtLayerSize, LayerOverGrey(gpu, gpu.artLayers[1].Get()));
        struct GameCase {
            char const* name;
            XrVector3f eye;
            float zoom;
        };
        GameCase const gameCases[] = {
            {"game-pulled-sniper-3.5x", kRightEye, 3.504f},
            {"game-pulled-sniper-7x", kRightEye, 7.015f},
            {"game-pulled-sniper-off-1.5cm", {0.015f, 0.0f, 0.0f}, 3.504f},
        };
        constexpr float kLens = 0.021f;
        for (GameCase const& c : gameCases) {
            gTube = Tube{-kRelief, 0.029f, kLens};
            composite::Image const aiming = EyeImage(kRightEye);
            ComPtr<ID3D11ShaderResourceView> const otherSrv = Upload(gpu, EyeImage(kLeftEye));
            sv::Sight s = sight;
            s.center = {0.0f, 0.0f, -kRelief};
            s.radius = kLens;
            s.zoom = c.zoom;
            s.scopeReticle = static_cast<std::int32_t>(sv::ScopeReticle::Game);
            s.scopeFieldDeg = sv::PulledFieldDeg(kLens, kRelief);
            auto const gameLens = sv::MakeLens(s.center, s.axis, s.up, s.radius);
            sv::Lens pictureLens = *gameLens;
            auto const view = sv::VirtualView(*gameLens, c.eye, sv::Eyepiece{s.scopeFieldDeg, s.scopeEyeBox, s.scopeMaxM}, pictureLens);
            if (!view) {
                std::printf("%s: no view\n", c.name);
                continue;
            }
            pic::ScopeParams const params =
                pic::MakeScopeParams(s, pictureLens, *view, c.eye, leftPose, kFov, rect, kEyeSize, kEyeSize, kScopeSize, 1.0f, true);
            std::vector<std::uint32_t> const picture = RenderScope(gpu, otherSrv.Get(), params, kScopeSize, true);
            if (picture.empty()) {
                std::printf("%s: render failed\n", c.name);
                continue;
            }
            Save(dir, (std::string("scope-") + c.name + ".png").c_str(), kScopeSize, kScopeSize, OverGrey(picture));
            composite::Image eye = aiming;
            composite::Image const quadImage = Straight(picture, static_cast<int>(kScopeSize));
            sv::Quad const disc = sv::Disc(s.center, s.axis, s.up, pictureLens.radius);
            composite::Quad quad;
            quad.pose = disc.pose;
            quad.width = quad.height = disc.size;
            quad.image = &quadImage;
            composite::Draw(eye, composite::Eye{{{0.0f, 0.0f, 0.0f, 1.0f}, c.eye}, kFov}, quad);
            Save(dir, (std::string("eye-right-") + c.name + ".png").c_str(), eye.width, eye.height, eye.pixels);
            std::printf("  %s: %.1fx, the picture %.1f degrees across, the field (the game's clear disc) %.1f of it\n", c.name, c.zoom,
                2.0 * std::atan(pictureLens.radius / view->along) * 180.0 / 3.14159265, mevr::vr::scope_art::kFieldShare);
        }
        gTube = Tube{};
    }
    // The scope capture (research/39): the engine renders the scope's own narrow field from the eyepiece into a 1024-pixel
    // target, and the same shader samples that instead of the other eye's image (its pose and square frustum, the whole
    // texture). Traced here from the eyepiece (the scope tube's face lies on the near plane, as the capture's near plane
    // past the muzzle leaves the gun out).
    {
        namespace capture = mevr::vr::scope_capture;
        constexpr int kCaptureSize = 1024;
        struct CaptureCase {
            char const* name;
            XrVector3f eye;
            float zoom;
        };
        CaptureCase const captureCases[] = {
            {"capture-virtual-45cm", {0.0f, 0.0f, 0.35f}, 4.0f},
            {"capture-virtual-45cm-off-2.5cm", {0.025f, 0.0f, 0.35f}, 4.0f},
            {"capture-virtual-30cm-1.5x", {0.0f, 0.0f, 0.20f}, 1.5f},
        };
        for (CaptureCase const& c : captureCases) {
            sight.zoom = c.zoom;
            sight.scopeReticle = static_cast<std::int32_t>(sv::ScopeReticle::Duplex);
            sv::Lens pictureLens = *lens;
            auto const view = sv::VirtualView(*lens, c.eye, sv::Eyepiece{sight.scopeFieldDeg, sight.scopeEyeBox, sight.scopeMaxM}, pictureLens);
            auto const orientation = capture::CameraOrientation(sight.axis, sight.up);
            if (!view || !orientation) {
                continue;
            }
            float const half = std::atan(capture::TanHalf(capture::FovDeg(sight.scopeFieldDeg, c.zoom, 0.25f)));
            XrFovf const frustum{-half, half, half, -half};
            composite::Image const target = TraceImage(kEyepiece, frustum, kCaptureSize);
            Save(dir, (std::string(c.name) + "-target.png").c_str(), target.width, target.height, target.pixels);
            ComPtr<ID3D11ShaderResourceView> const targetSrv = Upload(gpu, target);
            XrRect2Di const whole{{0, 0}, {kCaptureSize, kCaptureSize}};
            pic::ScopeParams const params = pic::MakeScopeParams(sight, pictureLens, *view, c.eye, XrPosef{*orientation, kEyepiece}, frustum,
                whole, kCaptureSize, kCaptureSize, kScopeSize);
            std::vector<std::uint32_t> const picture = RenderScope(gpu, targetSrv.Get(), params, kScopeSize);
            if (picture.empty()) {
                continue;
            }
            Save(dir, (std::string("scope-") + c.name + ".png").c_str(), kScopeSize, kScopeSize, OverGrey(picture));
            std::printf("  %s: the capture %.2f degrees across (%d px: %.0f px per degree of the world; these eye images have 19, the headset's about 24), %.1fx\n",
                c.name, 2.0 * half * 180.0 / 3.14159265, kCaptureSize, kCaptureSize / (2.0 * half * 180.0 / 3.14159265), c.zoom);
        }
    }

    // The reticles and the window ring.
    sv::Sight dot;
    dot.kind = static_cast<std::int32_t>(sv::Kind::Dot);
    dot.dotColor = 0xFF3020;
    std::vector<std::uint32_t> const dotPixels = pic::DotPixels(pic::LayoutFor(dot), dot.dotColor);
    Save(dir, "dot.png", pic::kPictureSize, pic::kPictureSize, OverGrey(dotPixels));
    sv::Sight holo = dot;
    holo.kind = static_cast<std::int32_t>(sv::Kind::Holo);
    Save(dir, "holo.png", pic::kPictureSize, pic::kPictureSize, OverGrey(pic::DotPixels(pic::LayoutFor(holo), holo.dotColor)));
    Save(dir, "frame.png", pic::kPictureSize, pic::kPictureSize, OverGrey(pic::FramePixels()));
    // The right eye looking through a red dot sight 30 cm ahead, 2 cm below the eye, the gun pointing a
    // degree left of straight ahead: the dot lands a degree left of the bullseye, wherever the eye is.
    {
        XrVector3f const window{0.0f, -0.02f, -0.30f};
        XrVector3f axis{-std::sin(0.0174533f), 0.0f, -std::cos(0.0174533f)};
        composite::Image eye = right;
        composite::Image const frameImage = Straight(pic::FramePixels(), static_cast<int>(pic::kPictureSize));
        sv::Quad const frame = sv::Disc(window, axis, {0.0f, 1.0f, 0.0f}, 0.03f * pic::kFrameScale);
        composite::Quad ring;
        ring.pose = frame.pose;
        ring.width = ring.height = frame.size;
        ring.image = &frameImage;
        composite::Draw(eye, composite::Eye{rightPose, kFov}, ring);
        pic::DotLayout const layout = pic::LayoutFor(holo);
        composite::Image const dotImage = Straight(pic::DotPixels(layout, holo.dotColor), static_cast<int>(pic::kPictureSize));
        sv::Quad const reticle = sv::Reticle(window, axis, {0.0f, 1.0f, 0.0f}, 50.0f, layout.quadDeg * 3.14159265f / 180.0f);
        composite::Quad q;
        q.pose = reticle.pose;
        q.width = q.height = reticle.size;
        q.image = &dotImage;
        composite::Draw(eye, composite::Eye{rightPose, kFov}, q);
        Save(dir, "eye-right-holo.png", eye.width, eye.height, eye.pixels);
    }
    // A red dot on a solid housing (research/21 section 17): the scene's tube face 10 cm ahead blocks the right eye like the
    // assault rifle's box; with kWindow the eye sees the world through a 3 cm window there (the left eye's image at 1x), the
    // ring and the dot over it. eye-right-dot-housing.png is the same without the window (what the user saw), and
    // eye-right-dot-window-off-*.png the eye off the window's line.
    {
        XrVector3f const window{0.0f, 0.0f, -0.10f};
        XrVector3f const axis{0.0f, 0.0f, -1.0f};
        XrVector3f const up{0.0f, 1.0f, 0.0f};
        sv::Sight s = dot;
        s.center = window;
        s.axis = axis;
        s.up = up;
        s.radius = 0.03f;
        s.focusM = 40.0f;
        s.zoom = 1.0f;
        s.flags = sv::kFrame | sv::kWindow;
        auto const windowLens = sv::MakeLens(window, axis, up, s.radius);
        composite::Image const frameImage = Straight(pic::FramePixels(), static_cast<int>(pic::kPictureSize));
        pic::DotLayout const layout = pic::LayoutFor(s);
        composite::Image const dotImage = Straight(pic::DotPixels(layout, s.dotColor), static_cast<int>(pic::kPictureSize));
        struct WindowCase {
            char const* name;
            XrVector3f eye;
            bool window;
        };
        WindowCase const windowCases[] = {
            {"dot-housing", kRightEye, false},
            {"dot-window", kRightEye, true},
            {"dot-window-off-1.5cm", {0.015f, -0.01f, 0.0f}, true},
        };
        for (WindowCase const& c : windowCases) {
            composite::Image eye = TraceImage(c.eye, kFov, kEyeSize);
            composite::Eye const view{{{0.0f, 0.0f, 0.0f, 1.0f}, c.eye}, kFov};
            if (c.window && windowLens) {
                auto const wv = sv::WindowView(*windowLens, c.eye, 0.02f, 1.0f);
                if (wv) {
                    pic::ScopeParams const params =
                        pic::MakeWindowParams(s, *windowLens, *wv, c.eye, leftPose, kFov, rect, kEyeSize, kEyeSize, kScopeSize);
                    std::vector<std::uint32_t> const picture = RenderScope(gpu, leftSrv.Get(), params, kScopeSize);
                    if (std::string(c.name) == "dot-window") {
                        Save(dir, "window.png", kScopeSize, kScopeSize, OverGrey(picture));
                    }
                    composite::Image const pictureImage = Straight(picture, static_cast<int>(kScopeSize));
                    sv::Quad const disc = sv::Disc(window, axis, up, s.radius);
                    composite::Quad wq;
                    wq.pose = disc.pose;
                    wq.width = wq.height = disc.size;
                    wq.image = &pictureImage;
                    composite::Draw(eye, view, wq);
                }
            }
            sv::Quad const frame = sv::Disc(window, axis, up, s.radius * pic::kFrameScale);
            composite::Quad ring;
            ring.pose = frame.pose;
            ring.width = ring.height = frame.size;
            ring.image = &frameImage;
            composite::Draw(eye, view, ring);
            sv::Quad const reticle = sv::Reticle(window, axis, up, 50.0f, layout.quadDeg * 3.14159265f / 180.0f);
            composite::Quad q;
            q.pose = reticle.pose;
            q.width = q.height = reticle.size;
            q.image = &dotImage;
            composite::Draw(eye, view, q);
            Save(dir, (std::string("eye-right-") + c.name + ".png").c_str(), eye.width, eye.height, eye.pixels);
        }
    }
    return 0;
}
