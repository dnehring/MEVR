// SPDX-License-Identifier: MIT
// The colour correction (core/vr/Colour, research/27) offline: its shader prelude, the same text the host compiles
// into Halcyon's eye blit and the HUD panel, run on a D3D11 device of its own (hardware, else WARP) and compared
// with the CPU reference, pixel by pixel, on the encoded path (UNORM in, UNORM out: the game's 8-bit backbuffer)
// and the linear path (an _SRGB view in, the eye's sRGB view out: the float HDR backbuffer's path).
//
//   mevr-colour-preview [output folder]   PNGs (default .\colour-preview) and the check
//   mevr-colour-preview --check           the check only (a ctest test): exit 1 if any pixel is more than one
//                                         8-bit step from the CPU reference
//   mevr-colour-preview --bench           also time the shader with and without the correction on this GPU, at
//                                         the PS VR2's eye size (4164x4244, sampled from a 3072x3188 source)
//
// PNGs: strip-<profile>.png, the test strip (core/vr/ColourStrip) through each preset as the headset gets it.

#include "core/PngFile.hpp"
#include "core/vr/Colour.hpp"
#include "core/vr/ColourStrip.hpp"

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
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;
namespace cv = mevr::vr::colour;
namespace fs = std::filesystem;

// The eye blit's use of the hook, reduced to one texel per pixel (no filtering to model).
constexpr char kTestShader[] = R"(
Texture2D<float4> src : register(t0);
struct VSOut { float4 pos : SV_Position; };
VSOut VSMain(uint id : SV_VertexID) {
    float2 p = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);
    VSOut o; o.pos = float4(p, 0.0, 1.0); return o;
}
float4 PSMain(VSOut i) : SV_Target { return MEVR_EYE_OUT(src.Load(int3(int2(i.pos.xy), 0)), i.pos); }
)";

struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> constants;
    bool warp = false;
};

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
    std::string const source = std::string(cv::ShaderPrelude()) + kTestShader;
    auto compile = [&](char const* entry, char const* profile, ComPtr<ID3DBlob>& blob) {
        ComPtr<ID3DBlob> errors;
        if (FAILED(D3DCompile(source.data(), source.size(), "MEVR_ColourPreview", nullptr, nullptr, entry, profile,
                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), errors.GetAddressOf()))) {
            std::printf("%s: %s\n", entry, errors ? static_cast<char const*>(errors->GetBufferPointer()) : "?");
            return false;
        }
        return true;
    };
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    // ps_4_0: the profile Halcyon's eye shaders (and so their variants) are compiled for.
    if (!compile("VSMain", "vs_4_0", vsBlob) || !compile("PSMain", "ps_4_0", psBlob)) {
        return false;
    }
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(cv::Constants);
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    return SUCCEEDED(gpu.device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, gpu.vs.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, gpu.ps.GetAddressOf())) &&
        SUCCEEDED(gpu.device->CreateBuffer(&cb, nullptr, gpu.constants.GetAddressOf()));
}

// `pixels` (0xAABBGGRR) through the shader. `linear`: a float source holding the bytes decoded (the float HDR
// backbuffer's path, decoded on the CPU so the comparison isn't about the GPU's own sRGB decode), written through
// an _SRGB view (the eye's sRGB view); else UNORM both ways (the encoded path). Returns the target's bytes.
std::vector<std::uint32_t> Run(Gpu& gpu, int width, int height, std::vector<std::uint32_t> const& pixels, cv::Constants const& k,
    bool linear) {
    std::vector<float> decoded;
    if (linear) {
        decoded.resize(pixels.size() * 4);
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            for (int c = 0; c < 4; ++c) {
                float const e = static_cast<float>((pixels[i] >> (8 * c)) & 0xFFu) / 255.0f;
                decoded[i * 4 + c] = c == 3 ? e : cv::Decode(e);
            }
        }
    }
    D3D11_TEXTURE2D_DESC sourceDesc{};
    sourceDesc.Width = static_cast<UINT>(width);
    sourceDesc.Height = static_cast<UINT>(height);
    sourceDesc.MipLevels = 1;
    sourceDesc.ArraySize = 1;
    sourceDesc.Format = linear ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    sourceDesc.SampleDesc.Count = 1;
    sourceDesc.Usage = D3D11_USAGE_IMMUTABLE;
    sourceDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{linear ? static_cast<void const*>(decoded.data()) : static_cast<void const*>(pixels.data()),
        static_cast<UINT>(width) * (linear ? 16u : 4u), 0};
    ComPtr<ID3D11Texture2D> source;
    ComPtr<ID3D11ShaderResourceView> srv;
    D3D11_TEXTURE2D_DESC td = sourceDesc;
    td.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> rtv;
    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    rd.Format = linear ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(gpu.device->CreateTexture2D(&sourceDesc, &data, source.GetAddressOf())) ||
        FAILED(gpu.device->CreateShaderResourceView(source.Get(), nullptr, srv.GetAddressOf())) ||
        FAILED(gpu.device->CreateTexture2D(&td, nullptr, target.GetAddressOf())) ||
        FAILED(gpu.device->CreateRenderTargetView(target.Get(), &rd, rtv.GetAddressOf())) ||
        FAILED(gpu.device->CreateTexture2D(&sd, nullptr, staging.GetAddressOf()))) {
        return {};
    }
    gpu.context->UpdateSubresource(gpu.constants.Get(), 0, nullptr, &k, 0, 0);
    D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    ID3D11RenderTargetView* const target0 = rtv.Get();
    ID3D11ShaderResourceView* const source0 = srv.Get();
    ID3D11Buffer* const constants = gpu.constants.Get();
    gpu.context->OMSetRenderTargets(1, &target0, nullptr);
    gpu.context->RSSetViewports(1, &viewport);
    gpu.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    gpu.context->VSSetShader(gpu.vs.Get(), nullptr, 0);
    gpu.context->PSSetShader(gpu.ps.Get(), nullptr, 0);
    gpu.context->PSSetShaderResources(0, 1, &source0);
    gpu.context->PSSetConstantBuffers(1, 1, &constants);
    gpu.context->Draw(3, 0);
    gpu.context->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(gpu.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return {};
    }
    std::vector<std::uint32_t> out(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        std::memcpy(out.data() + static_cast<std::size_t>(y) * width, static_cast<std::uint8_t const*>(mapped.pData) + y * mapped.RowPitch,
            static_cast<std::size_t>(width) * 4);
    }
    gpu.context->Unmap(staging.Get(), 0);
    return out;
}

// The CPU reference's bytes for the same pixels.
std::vector<std::uint32_t> Reference(int width, int height, std::vector<std::uint32_t> const& pixels, cv::Constants const& k,
    bool linear) {
    std::vector<std::uint32_t> out(pixels.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint32_t const p = pixels[static_cast<std::size_t>(y) * width + x];
            cv::Rgb in;
            for (int c = 0; c < 3; ++c) {
                float const e = static_cast<float>((p >> (8 * c)) & 0xFFu) / 255.0f;
                in[c] = linear ? cv::Decode(e) : e;
            }
            cv::Rgb const v = cv::Correct(k, in, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), linear);
            std::uint32_t q = p & 0xFF000000u;
            for (int c = 0; c < 3; ++c) {
                q |= static_cast<std::uint32_t>(cv::ToUnorm8(linear ? cv::Encode(v[c]) : v[c])) << (8 * c);
            }
            out[static_cast<std::size_t>(y) * width + x] = q;
        }
    }
    return out;
}

struct Diff {
    int maxDiff = 0;
    std::size_t over = 0;   // channels more than one step off
    std::size_t exact = 0;  // channels equal
    std::size_t total = 0;
};

Diff Compare(std::vector<std::uint32_t> const& a, std::vector<std::uint32_t> const& b) {
    Diff d;
    if (a.size() != b.size() || a.empty()) {
        d.maxDiff = 255;
        d.over = 1;
        return d;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (int c = 0; c < 3; ++c) {
            int const va = static_cast<int>((a[i] >> (8 * c)) & 0xFFu);
            int const vb = static_cast<int>((b[i] >> (8 * c)) & 0xFFu);
            int const diff = std::abs(va - vb);
            d.maxDiff = std::max(d.maxDiff, diff);
            d.over += diff > 1 ? 1u : 0u;
            d.exact += diff == 0 ? 1u : 0u;
            ++d.total;
        }
    }
    return d;
}

// The eye blit's shape for timing: a bilinear sample of a larger source into the eye, through the hook, with or
// without the prelude (the plain one is what Halcyon's own shader does at that point).
constexpr char kBenchShader[] = R"(
#ifndef MEVR_EYE_OUT
#define MEVR_EYE_OUT(c, pos) (c)
#endif
Texture2D t0 : register(t0);
SamplerState s0 : register(s0);
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    float2 p = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);
    VSOut o; o.pos = float4(p, 0.0, 1.0); o.uv = float2((p.x + 1.0) * 0.5, 1.0 - ((p.y + 1.0) * 0.5)); return o;
}
float4 PSMain(VSOut i) : SV_Target { return MEVR_EYE_OUT(t0.Sample(s0, i.uv), i.pos); }
)";

// Milliseconds per draw (median of 9 runs of 20 draws), or -1.
double TimeShader(Gpu& gpu, bool corrected) {
    std::string const hlsl = (corrected ? std::string(cv::ShaderPrelude()) : std::string()) + kBenchShader;
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (FAILED(D3DCompile(hlsl.data(), hlsl.size(), "MEVR_ColourBench", nullptr, nullptr, "VSMain", "vs_4_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, vsBlob.GetAddressOf(), nullptr)) ||
        FAILED(D3DCompile(hlsl.data(), hlsl.size(), "MEVR_ColourBench", nullptr, nullptr, "PSMain", "ps_4_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, psBlob.GetAddressOf(), nullptr))) {
        return -1.0;
    }
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    gpu.device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, vs.GetAddressOf());
    gpu.device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, ps.GetAddressOf());
    auto texture = [&](UINT w, UINT h, UINT bind, ComPtr<ID3D11Texture2D>& out) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = bind;
        return SUCCEEDED(gpu.device->CreateTexture2D(&td, nullptr, out.GetAddressOf()));
    };
    ComPtr<ID3D11Texture2D> source;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11SamplerState> sampler;
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (vs == nullptr || ps == nullptr || !texture(3072, 3188, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, source) ||
        !texture(4164, 4244, D3D11_BIND_RENDER_TARGET, target) ||
        FAILED(gpu.device->CreateShaderResourceView(source.Get(), nullptr, srv.GetAddressOf())) ||
        FAILED(gpu.device->CreateRenderTargetView(target.Get(), nullptr, rtv.GetAddressOf())) ||
        FAILED(gpu.device->CreateSamplerState(&sd, sampler.GetAddressOf()))) {
        return -1.0;
    }
    cv::Constants const k = cv::MakeConstants(cv::Preset(cv::Profile::Quest), 0.5f, false, 0u);
    gpu.context->UpdateSubresource(gpu.constants.Get(), 0, nullptr, &k, 0, 0);
    D3D11_VIEWPORT viewport{0.0f, 0.0f, 4164.0f, 4244.0f, 0.0f, 1.0f};
    ID3D11RenderTargetView* const target0 = rtv.Get();
    ID3D11ShaderResourceView* const source0 = srv.Get();
    ID3D11SamplerState* const sampler0 = sampler.Get();
    ID3D11Buffer* const constants = gpu.constants.Get();
    gpu.context->OMSetRenderTargets(1, &target0, nullptr);
    gpu.context->RSSetViewports(1, &viewport);
    gpu.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    gpu.context->VSSetShader(vs.Get(), nullptr, 0);
    gpu.context->PSSetShader(ps.Get(), nullptr, 0);
    gpu.context->PSSetShaderResources(0, 1, &source0);
    gpu.context->PSSetSamplers(0, 1, &sampler0);
    gpu.context->PSSetConstantBuffers(1, 1, &constants);
    D3D11_QUERY_DESC const disjointDesc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    D3D11_QUERY_DESC const stampDesc{D3D11_QUERY_TIMESTAMP, 0};
    std::vector<double> runs;
    constexpr int kDraws = 20;
    for (int run = 0; run < 10; ++run) {
        ComPtr<ID3D11Query> disjoint;
        ComPtr<ID3D11Query> begin;
        ComPtr<ID3D11Query> end;
        gpu.device->CreateQuery(&disjointDesc, disjoint.GetAddressOf());
        gpu.device->CreateQuery(&stampDesc, begin.GetAddressOf());
        gpu.device->CreateQuery(&stampDesc, end.GetAddressOf());
        gpu.context->Begin(disjoint.Get());
        gpu.context->End(begin.Get());
        for (int d = 0; d < kDraws; ++d) {
            gpu.context->Draw(3, 0);
        }
        gpu.context->End(end.Get());
        gpu.context->End(disjoint.Get());
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        while (gpu.context->GetData(disjoint.Get(), &dj, sizeof dj, 0) == S_FALSE) {
        }
        UINT64 t0 = 0;
        UINT64 t1 = 0;
        while (gpu.context->GetData(begin.Get(), &t0, sizeof t0, 0) == S_FALSE) {
        }
        while (gpu.context->GetData(end.Get(), &t1, sizeof t1, 0) == S_FALSE) {
        }
        if (run > 0 && !dj.Disjoint && dj.Frequency != 0) {  // the first run warms up
            runs.push_back(static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(dj.Frequency) / kDraws);
        }
    }
    if (runs.empty()) {
        return -1.0;
    }
    std::sort(runs.begin(), runs.end());
    return runs[runs.size() / 2];
}

// Every 8-bit value in every channel: r = x, g = y, b mixed.
std::vector<std::uint32_t> Cube() {
    std::vector<std::uint32_t> out(256 * 256);
    for (std::uint32_t y = 0; y < 256; ++y) {
        for (std::uint32_t x = 0; x < 256; ++x) {
            std::uint32_t const b = (x * 7u + y * 13u) & 0xFFu;
            out[y * 256 + x] = x | (y << 8) | (b << 16) | 0xFF000000u;
        }
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    bool checkOnly = false;
    bool bench = false;
    fs::path folder = "colour-preview";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--check") == 0) {
            checkOnly = true;
        } else if (std::strcmp(argv[i], "--bench") == 0) {
            bench = true;
        } else {
            folder = argv[i];
        }
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Gpu gpu;
    if (!Start(gpu)) {
        return EXIT_FAILURE;
    }
    std::printf("device: %s\n", gpu.warp ? "WARP" : "hardware");

    struct Case {
        char const* name;
        cv::Settings settings;
    };
    cv::Settings extreme;  // every value at an end of its range: the clamps
    extreme.brightness = 1.5f;
    extreme.contrast = 0.5f;
    extreme.saturation = 2.0f;
    extreme.gamma = 0.5f;
    extreme.black = -0.2f;
    extreme.gain = {0.5f, 1.5f, 1.0f};
    cv::Settings other = extreme;
    other.brightness = 0.5f;
    other.contrast = 1.5f;
    other.saturation = 0.0f;
    other.gamma = 2.0f;
    other.black = 0.2f;
    Case const cases[] = {{"neutral", cv::Preset(cv::Profile::Neutral)}, {"quest", cv::Preset(cv::Profile::Quest)},
        {"vivid", cv::Preset(cv::Profile::Vivid)}, {"extreme-a", extreme}, {"extreme-b", other}};

    cv::Strip const strip = cv::TestStrip();
    std::vector<std::uint32_t> const cube = Cube();
    bool ok = true;
    for (Case const& c : cases) {
        for (bool const linear : {false, true}) {
            for (float const dither : {0.0f, 0.5f}) {
                cv::Constants const k = cv::MakeConstants(c.settings, dither, linear, 1u);
                Diff const dc = Compare(Run(gpu, 256, 256, cube, k, linear), Reference(256, 256, cube, k, linear));
                Diff const ds = Compare(Run(gpu, strip.width, strip.height, strip.pixels, k, linear),
                    Reference(strip.width, strip.height, strip.pixels, k, linear));
                bool const pass = dc.over == 0 && ds.over == 0;
                ok = ok && pass;
                std::printf("%-10s %-7s dither %.1f: GPU vs CPU max %d step(s), %.2f%% exact, %zu over one step %s\n", c.name,
                    linear ? "linear" : "encoded", dither, std::max(dc.maxDiff, ds.maxDiff),
                    100.0 * static_cast<double>(dc.exact + ds.exact) / static_cast<double>(dc.total + ds.total), dc.over + ds.over,
                    pass ? "ok" : "FAIL");
            }
        }
    }
    // Neutral through the corrected shader (no dither) must give back the input (the raw path is what runs, but
    // the math's identity is the same).
    {
        cv::Constants const k = cv::MakeConstants(cv::Settings{}, 0.0f, false, 0u);
        Diff const d = Compare(Run(gpu, 256, 256, cube, k, false), cube);
        bool const pass = d.maxDiff == 0;
        ok = ok && pass;
        std::printf("neutral without dither = the input: max %d %s\n", d.maxDiff, pass ? "ok" : "FAIL");
    }

    // What the presets do to a few grey steps (encoded, no dither).
    std::printf("\ngrey in  ");
    int const greys[] = {0, 8, 13, 20, 32, 64, 96, 128, 160, 192, 224, 255};
    for (int const g : greys) {
        std::printf("%5d", g);
    }
    std::printf("\n");
    for (cv::Profile const p : {cv::Profile::Quest, cv::Profile::Vivid}) {
        cv::Constants const k = cv::MakeConstants(cv::Preset(p), 0.0f, false, 0u);
        std::printf("%-8s ", cv::Name(p));
        for (int const g : greys) {
            float const e = static_cast<float>(g) / 255.0f;
            std::printf("%5d", cv::ToUnorm8(cv::Grade(k, {e, e, e})[0]));
        }
        std::printf("\n");
    }

    if (bench) {
        double const plain = TimeShader(gpu, false);
        double const corrected = TimeShader(gpu, true);
        std::printf("\nGPU time per eye blit at 4164x4244 (from 3072x3188), %s: plain %.3f ms, corrected %.3f ms: +%.3f ms per eye\n",
            gpu.warp ? "WARP" : "this GPU", plain, corrected, corrected - plain);
    }
    if (!checkOnly) {
        std::error_code error;
        fs::create_directories(folder, error);
        for (Case const& c : cases) {
            cv::Constants const k = cv::MakeConstants(c.settings, 0.5f, false, 1u);
            std::vector<std::uint32_t> const out = Run(gpu, strip.width, strip.height, strip.pixels, k, false);
            fs::path const path = folder / ("strip-" + std::string(c.name) + ".png");
            std::string message;
            if (out.empty() || !mevr::png::Write(path, strip.width, strip.height, out.data(), &message)) {
                std::printf("%s: %s\n", path.string().c_str(), message.c_str());
                ok = false;
            }
        }
        std::printf("\nPNGs in %s\n", fs::absolute(folder).string().c_str());
    }
    std::printf("%s\n", ok ? "colour shader matches the CPU reference" : "colour shader check FAILED");
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
