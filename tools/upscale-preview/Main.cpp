// SPDX-License-Identifier: MIT
// The eye upscale (core/vr/Upscale, research/32) offline: FSR 1's EASU and RCAS, the same shader text the host
// compiles into Halcyon's eye blit (the embedded AMD headers), run on a D3D11 device of its own (hardware, else
// WARP) over a synthetic test image, next to a bilinear scale of the same (what Halcyon's fill does).
//
//   mevr-upscale-preview [output folder]   PNGs (default .\upscale-preview) and the check
//   mevr-upscale-preview --check           the check only (a ctest test): the shaders compile, a flat patch stays
//                                          flat, a step stays within its two levels (no ringing), the result is
//                                          sharper than bilinear and not far from it (the mapping is right)
//   mevr-upscale-preview --bench           also time one eye on this GPU: 2118x2160 -> 2824x2880 (0.75 of the
//                                          PS VR2's eye), FSR against bilinear
//
// PNGs: upscale-source.png (600x600), upscale-fsr.png and upscale-bilinear.png (800x800).

#include "core/Embedded.hpp"
#include "core/PngFile.hpp"
#include "core/vr/Upscale.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;
namespace vu = mevr::vr::upscale;
namespace fs = std::filesystem;

// Halcyon's fill, reduced to the bilinear sample (no rectangle: the whole image).
constexpr char kBilinear[] = R"(
Texture2D t0 : register(t0);
SamplerState s0 : register(s0);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
  float2 p = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);
  VSOut o; o.pos = float4(p, 0.0, 1.0); o.uv = float2((p.x + 1.0) * 0.5, 1.0 - ((p.y + 1.0) * 0.5)); return o;
}
float4 PSMain(VSOut i) : SV_Target { return t0.Sample(s0, i.uv); }
)";

struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> easu, rcas, bilinear;
    ComPtr<ID3D11Buffer> fill, constants;
    ComPtr<ID3D11SamplerState> sampler;
    bool warp = false;
};

struct Image {
    int width = 0, height = 0;
    std::vector<std::uint32_t> pixels;  // 0xAABBGGRR
    std::uint32_t& At(int x, int y) { return pixels[static_cast<std::size_t>(y) * width + x]; }
    std::uint32_t At(int x, int y) const { return pixels[static_cast<std::size_t>(y) * width + x]; }
};

struct Target {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> srv;
    int width = 0, height = 0;
};

bool Compile(std::string const& source, char const* entry, char const* profile, ComPtr<ID3DBlob>& blob) {
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3DCompile(source.data(), source.size(), "MEVR_UpscalePreview", nullptr, nullptr, entry, profile,
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), errors.GetAddressOf()))) {
        std::printf("%s: %s\n", entry, errors ? static_cast<char const*>(errors->GetBufferPointer()) : "?");
        return false;
    }
    return true;
}

bool Start(Gpu& gpu) {
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        gpu.device.GetAddressOf(), nullptr, gpu.context.GetAddressOf());
    if (FAILED(hr)) {
        gpu.warp = true;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, gpu.device.GetAddressOf(),
            nullptr, gpu.context.GetAddressOf());
    }
    if (FAILED(hr)) {
        std::printf("no D3D11 device\n");
        return false;
    }
    std::string const source = vu::ShaderSource(std::string_view(mevr::embedded::kFfxA, mevr::embedded::kFfxASize),
        std::string_view(mevr::embedded::kFfxFsr1, mevr::embedded::kFfxFsr1Size));
    ComPtr<ID3DBlob> vs, easu, rcas, bilinearVs, bilinear;
    if (!Compile(source, "VSMain", "vs_5_0", vs) || !Compile(source, "PSEasu", "ps_5_0", easu) ||
        !Compile(source, "PSRcas", "ps_5_0", rcas) || !Compile(kBilinear, "PSMain", "ps_5_0", bilinear)) {
        return false;
    }
    D3D11_BUFFER_DESC cb{};
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.ByteWidth = 48;
    D3D11_BUFFER_DESC cb2 = cb;
    cb2.ByteWidth = sizeof(vu::Constants);
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    return SUCCEEDED(gpu.device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, gpu.vs.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreatePixelShader(easu->GetBufferPointer(), easu->GetBufferSize(), nullptr, gpu.easu.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreatePixelShader(rcas->GetBufferPointer(), rcas->GetBufferSize(), nullptr, gpu.rcas.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreatePixelShader(bilinear->GetBufferPointer(), bilinear->GetBufferSize(), nullptr, gpu.bilinear.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreateBuffer(&cb, nullptr, gpu.fill.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreateBuffer(&cb2, nullptr, gpu.constants.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreateSamplerState(&sd, gpu.sampler.GetAddressOf()));
}

std::uint32_t Grey(int v) {
    auto const c = static_cast<std::uint32_t>(std::clamp(v, 0, 255));
    return 0xFF000000u | (c << 16) | (c << 8) | c;
}

// 600x600: a row of four 150x150 blocks (flat 128; a 3-px chequerboard; 1-px lines; a 40 | 200 step), a diagonal
// edge (30 / 220), a horizontal gradient, and colour bars with soft edges.
Image TestImage() {
    Image img;
    img.width = 600;
    img.height = 600;
    img.pixels.assign(600 * 600, Grey(128));
    for (int y = 0; y < 150; ++y) {
        for (int x = 150; x < 300; ++x) img.At(x, y) = Grey(((x / 3 + y / 3) & 1) != 0 ? 255 : 0);
        for (int x = 300; x < 450; ++x) img.At(x, y) = Grey((x % 6 == 0 || y % 6 == 0) ? 240 : 20);
        for (int x = 450; x < 600; ++x) img.At(x, y) = Grey(x < 525 ? 40 : 200);
    }
    for (int y = 150; y < 300; ++y) {
        for (int x = 0; x < 600; ++x) img.At(x, y) = Grey(x + (y - 150) * 2 < 450 ? 30 : 220);
    }
    for (int y = 300; y < 450; ++y) {
        for (int x = 0; x < 600; ++x) img.At(x, y) = Grey(x * 255 / 599);
    }
    for (int y = 450; y < 600; ++y) {
        for (int x = 0; x < 600; ++x) {
            int const band = x / 100;
            float const t = static_cast<float>(x % 100) / 100.0f;
            float const soft = 0.5f - 0.5f * std::cos(t * 6.2831853f);  // 0 at the band edges, 1 in the middle
            auto const c = static_cast<std::uint32_t>(std::lround(40.0f + 200.0f * soft));
            std::uint32_t const rgb = band % 3 == 0 ? (c) : band % 3 == 1 ? (c << 8) : (c << 16);
            img.At(x, y) = 0xFF000000u | rgb | 0x00202020u;
        }
    }
    return img;
}

Image Noise(int width, int height) {
    Image img;
    img.width = width;
    img.height = height;
    img.pixels.resize(static_cast<std::size_t>(width) * height);
    std::uint32_t s = 12345;
    for (std::uint32_t& p : img.pixels) {
        s = s * 1664525u + 1013904223u;
        p = 0xFF000000u | (s >> 8);
    }
    return img;
}

bool Upload(Gpu& gpu, Image const& img, ComPtr<ID3D11ShaderResourceView>& srv) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(img.width);
    td.Height = static_cast<UINT>(img.height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{img.pixels.data(), static_cast<UINT>(img.width) * 4u, 0};
    ComPtr<ID3D11Texture2D> texture;
    return SUCCEEDED(gpu.device->CreateTexture2D(&td, &data, texture.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreateShaderResourceView(texture.Get(), nullptr, srv.GetAddressOf()));
}

bool MakeTarget(Gpu& gpu, int width, int height, Target& t) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(width);
    td.Height = static_cast<UINT>(height);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    t.width = width;
    t.height = height;
    return SUCCEEDED(gpu.device->CreateTexture2D(&td, nullptr, t.texture.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreateRenderTargetView(t.texture.Get(), nullptr, t.rtv.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreateShaderResourceView(t.texture.Get(), nullptr, t.srv.GetAddressOf()));
}

void Pass(Gpu& gpu, ID3D11PixelShader* ps, ID3D11ShaderResourceView* source, Target const& target) {
    D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(target.width), static_cast<float>(target.height), 0.0f, 1.0f};
    ID3D11RenderTargetView* const rtv = target.rtv.Get();
    ID3D11Buffer* const b0 = gpu.fill.Get();
    ID3D11Buffer* const b2 = gpu.constants.Get();
    ID3D11SamplerState* const s0 = gpu.sampler.Get();
    ID3D11ShaderResourceView* nullSrv = nullptr;
    gpu.context->OMSetRenderTargets(1, &rtv, nullptr);
    gpu.context->RSSetViewports(1, &viewport);
    gpu.context->IASetInputLayout(nullptr);
    gpu.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    gpu.context->VSSetShader(gpu.vs.Get(), nullptr, 0);
    gpu.context->PSSetShader(ps, nullptr, 0);
    gpu.context->PSSetShaderResources(0, 1, &source);
    gpu.context->PSSetSamplers(0, 1, &s0);
    gpu.context->PSSetConstantBuffers(0, 1, &b0);
    gpu.context->PSSetConstantBuffers(2, 1, &b2);
    gpu.context->Draw(3, 0);
    gpu.context->PSSetShaderResources(0, 1, &nullSrv);
    gpu.context->OMSetRenderTargets(0, nullptr, nullptr);
}

void SetConstants(Gpu& gpu, int srcW, int srcH, int dstW, int dstH, float sharpness) {
    float const fill[12] = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    float const whole[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    vu::Constants const k = vu::MakeConstants(static_cast<unsigned>(srcW), static_cast<unsigned>(srcH),
        static_cast<unsigned>(dstW), static_cast<unsigned>(dstH), whole, whole, sharpness);
    gpu.context->UpdateSubresource(gpu.fill.Get(), 0, nullptr, fill, 0, 0);
    gpu.context->UpdateSubresource(gpu.constants.Get(), 0, nullptr, &k, 0, 0);
}

// EASU into `middle`, RCAS into `out`.
void RunFsr(Gpu& gpu, ID3D11ShaderResourceView* source, Target const& middle, Target const& out) {
    Pass(gpu, gpu.easu.Get(), source, middle);
    Pass(gpu, gpu.rcas.Get(), middle.srv.Get(), out);
}

Image Readback(Gpu& gpu, Target const& t) {
    D3D11_TEXTURE2D_DESC td{};
    t.texture->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Image img;
    if (FAILED(gpu.device->CreateTexture2D(&td, nullptr, staging.GetAddressOf()))) {
        return img;
    }
    gpu.context->CopyResource(staging.Get(), t.texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(gpu.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return img;
    }
    img.width = t.width;
    img.height = t.height;
    img.pixels.resize(static_cast<std::size_t>(t.width) * t.height);
    for (int y = 0; y < t.height; ++y) {
        std::memcpy(img.pixels.data() + static_cast<std::size_t>(y) * t.width,
            static_cast<std::uint8_t const*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch, static_cast<std::size_t>(t.width) * 4);
    }
    gpu.context->Unmap(staging.Get(), 0);
    return img;
}

int Channel(std::uint32_t p, int c) {
    return static_cast<int>((p >> (8 * c)) & 0xFFu);
}

// Mean of |x[i+1] - x[i]| over a region, per channel, summed: how much edge there is.
double EdgeEnergy(Image const& img, int x0, int y0, int x1, int y1) {
    double sum = 0.0;
    std::size_t n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x + 1 < x1; ++x) {
            for (int c = 0; c < 3; ++c) {
                sum += std::abs(Channel(img.At(x + 1, y), c) - Channel(img.At(x, y), c));
            }
            ++n;
        }
    }
    return n == 0 ? 0.0 : sum / static_cast<double>(n);
}

double MeanDifference(Image const& a, Image const& b, int x0, int y0, int x1, int y1) {
    double sum = 0.0;
    std::size_t n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            for (int c = 0; c < 3; ++c) {
                sum += std::abs(Channel(a.At(x, y), c) - Channel(b.At(x, y), c));
                ++n;
            }
        }
    }
    return n == 0 ? 0.0 : sum / static_cast<double>(n);
}

bool Within(Image const& img, int x0, int y0, int x1, int y1, int low, int high, char const* what) {
    int minSeen = 255, maxSeen = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            for (int c = 0; c < 3; ++c) {
                int const v = Channel(img.At(x, y), c);
                minSeen = std::min(minSeen, v);
                maxSeen = std::max(maxSeen, v);
            }
        }
    }
    bool const ok = minSeen >= low && maxSeen <= high;
    std::printf("  %s: values %d..%d (allowed %d..%d) %s\n", what, minSeen, maxSeen, low, high, ok ? "ok" : "FAIL");
    return ok;
}

double Bench(Gpu& gpu, bool fsr, int iterations) {
    Image const src = Noise(2118, 2160);
    ComPtr<ID3D11ShaderResourceView> srv;
    Target middle, out;
    if (!Upload(gpu, src, srv) || !MakeTarget(gpu, 2824, 2880, middle) || !MakeTarget(gpu, 2824, 2880, out)) {
        return -1.0;
    }
    SetConstants(gpu, 2118, 2160, 2824, 2880, 0.9f);
    D3D11_QUERY_DESC const disjointDesc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    D3D11_QUERY_DESC const stampDesc{D3D11_QUERY_TIMESTAMP, 0};
    ComPtr<ID3D11Query> disjoint, begin, end;
    if (FAILED(gpu.device->CreateQuery(&disjointDesc, disjoint.GetAddressOf())) ||
        FAILED(gpu.device->CreateQuery(&stampDesc, begin.GetAddressOf())) || FAILED(gpu.device->CreateQuery(&stampDesc, end.GetAddressOf()))) {
        return -1.0;
    }
    for (int i = 0; i < 3; ++i) {  // warm up
        if (fsr) RunFsr(gpu, srv.Get(), middle, out); else Pass(gpu, gpu.bilinear.Get(), srv.Get(), out);
    }
    gpu.context->Begin(disjoint.Get());
    gpu.context->End(begin.Get());
    for (int i = 0; i < iterations; ++i) {
        if (fsr) RunFsr(gpu, srv.Get(), middle, out); else Pass(gpu, gpu.bilinear.Get(), srv.Get(), out);
    }
    gpu.context->End(end.Get());
    gpu.context->End(disjoint.Get());
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    UINT64 t0 = 0, t1 = 0;
    while (gpu.context->GetData(disjoint.Get(), &dj, sizeof dj, 0) != S_OK) Sleep(1);
    while (gpu.context->GetData(begin.Get(), &t0, sizeof t0, 0) != S_OK) Sleep(1);
    while (gpu.context->GetData(end.Get(), &t1, sizeof t1, 0) != S_OK) Sleep(1);
    if (dj.Disjoint || dj.Frequency == 0 || t1 < t0) {
        return -1.0;
    }
    return static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(dj.Frequency) / iterations;
}

} // namespace

int main(int argc, char** argv) {
    bool checkOnly = false, bench = false;
    fs::path folder = "upscale-preview";
    for (int i = 1; i < argc; ++i) {
        std::string const arg = argv[i];
        if (arg == "--check") checkOnly = true;
        else if (arg == "--bench") bench = true;
        else folder = arg;
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Gpu gpu;
    if (!Start(gpu)) {
        return EXIT_FAILURE;
    }
    std::printf("device: %s\n", gpu.warp ? "WARP (software)" : "hardware");

    Image const src = TestImage();
    ComPtr<ID3D11ShaderResourceView> srv;
    Target middle, fsrOut, bilinearOut;
    if (!Upload(gpu, src, srv) || !MakeTarget(gpu, 800, 800, middle) || !MakeTarget(gpu, 800, 800, fsrOut) ||
        !MakeTarget(gpu, 800, 800, bilinearOut)) {
        std::printf("resources failed\n");
        return EXIT_FAILURE;
    }
    SetConstants(gpu, 600, 600, 800, 800, 0.9f);
    RunFsr(gpu, srv.Get(), middle, fsrOut);
    Pass(gpu, gpu.bilinear.Get(), srv.Get(), bilinearOut);
    Image const easu = Readback(gpu, middle);
    Image const fsr = Readback(gpu, fsrOut);
    Image const bilinear = Readback(gpu, bilinearOut);
    if (easu.pixels.empty() || fsr.pixels.empty() || bilinear.pixels.empty()) {
        std::printf("readback failed\n");
        return EXIT_FAILURE;
    }

    // The checks (the source's blocks are 150 px: 200 px in the target; margins keep clear of the block edges).
    // EASU clamps each pixel to its four nearest taps (no ringing); RCAS sharpens with a halo it only limits
    // against clipping at 0 and 1 (FSR_RCAS_LIMIT), so a 40|200 step may overshoot by a few levels on each side.
    bool ok = true;
    std::printf("check:\n");
    ok &= Within(fsr, 10, 10, 190, 190, 127, 129, "flat patch stays flat");
    ok &= Within(easu, 610, 10, 790, 190, 39, 201, "EASU keeps a 40|200 step within its levels (no ringing)");
    ok &= Within(fsr, 610, 10, 790, 190, 24, 216, "RCAS's halo on it stays within 10% of the step");
    double const edgeFsr = EdgeEnergy(fsr, 0, 200, 800, 400);
    double const edgeBilinear = EdgeEnergy(bilinear, 0, 200, 800, 400);
    std::printf("  edge energy on the diagonal edge: FSR %.3f, bilinear %.3f %s\n", edgeFsr, edgeBilinear,
        edgeFsr >= edgeBilinear ? "ok (sharper)" : "FAIL");
    ok &= edgeFsr >= edgeBilinear;
    double const difference = MeanDifference(fsr, bilinear, 0, 200, 800, 800);
    std::printf("  mean difference to bilinear below the top row: %.2f of 255 %s\n", difference, difference < 16.0 ? "ok" : "FAIL (mapping?)");
    ok &= difference < 16.0;
    std::printf("%s\n", ok ? "upscale shader check passed" : "upscale shader check FAILED");

    if (!checkOnly) {
        std::error_code ec;
        fs::create_directories(folder, ec);
        std::string error;
        if (!mevr::png::Write(folder / "upscale-source.png", src.width, src.height, src.pixels.data(), &error) ||
            !mevr::png::Write(folder / "upscale-fsr.png", fsr.width, fsr.height, fsr.pixels.data(), &error) ||
            !mevr::png::Write(folder / "upscale-bilinear.png", bilinear.width, bilinear.height, bilinear.pixels.data(), &error)) {
            std::printf("PNG: %s\n", error.c_str());
            return EXIT_FAILURE;
        }
        std::printf("wrote %s\n", folder.string().c_str());
    }
    if (bench) {
        double const fsrMs = Bench(gpu, true, 40);
        double const bilinearMs = Bench(gpu, false, 40);
        std::printf("bench, one eye 2118x2160 -> 2824x2880 on this GPU: FSR 1 (EASU + RCAS) %.3f ms, bilinear %.3f ms\n", fsrMs, bilinearMs);
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
