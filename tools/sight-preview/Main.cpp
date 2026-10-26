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
//   dot.png, holo.png, frame.png        the reticles and the window ring over grey
//   eye-right-dot.png                   the right eye looking through a red dot sight

#include "core/PngFile.hpp"
#include "core/vr/LayerComposite.hpp"
#include "core/vr/OverlayPlacement.hpp"
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

// The test scene, seen along a ray from `o` in direction `d` (not unit).
std::uint32_t Trace(XrVector3f o, XrVector3f d) {
    // The scope tube's rear face: a dark disc 2.5 cm across at the eyepiece, facing back.
    if (d.z < 0.0f) {
        float const t = (kEyepiece.z - o.z) / d.z;
        float const x = o.x + d.x * t - kEyepiece.x;
        float const y = o.y + d.y * t - kEyepiece.y;
        if (t > 0.0f && x * x + y * y < 0.025f * 0.025f) {
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

composite::Image EyeImage(XrVector3f eye) {
    composite::Image image;
    image.width = image.height = kEyeSize;
    image.pixels.resize(static_cast<size_t>(kEyeSize) * kEyeSize);
    for (int y = 0; y < kEyeSize; ++y) {
        for (int x = 0; x < kEyeSize; ++x) {
            float const u = (static_cast<float>(x) + 0.5f) / kEyeSize;
            float const v = (static_cast<float>(y) + 0.5f) / kEyeSize;
            image.pixels[static_cast<size_t>(y) * kEyeSize + x] = Trace(eye, composite::EyeRay(kFov, u, v));
        }
    }
    return image;
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

// One scope picture (premultiplied sRGB pixels, `size` square), as the host draws it.
std::vector<std::uint32_t> RenderScope(Gpu& gpu, ID3D11ShaderResourceView* source, pic::ScopeParams const& params, UINT size) {
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
    c->PSSetShaderResources(0, 1, &source);
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
    fs::path const dir = argc > 1 ? fs::path(argv[1]) : fs::path("sight-preview");
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
    return 0;
}
