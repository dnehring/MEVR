// SPDX-License-Identifier: MIT
// Real EyeCopy + Blit, software D3D11 only: no window, game, headset or HDR-setting changes.
#include "plugin/EyeCopy.hpp"
#include "plugin/Colour.hpp"
#include "core/Log.hpp"
#include "core/d3d11/Blit.hpp"
#include "core/vr/Colour.hpp"
#include "TestTemp.hpp"

#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

// The only replaced production dependency is the host's live settings lookup. Neutral
// selects the actual uncorrected EyeCopy path; the real Colour shader code still links.
namespace mevr::host::colour {
bool EyeConstants(int, bool, vr::colour::Constants*) { return false; }
// The eye upscale's last pass asks these (plugin/Upscale, the real module): the test's own grade, bound at b1 as
// plugin/Colour binds it, when `gTestGrade` is set.
bool gTestGrade = false;
vr::colour::Constants gTestConstants{};
Microsoft::WRL::ComPtr<ID3D11Buffer> gTestBuffer;
bool BeginEye(ID3D11DeviceContext* context, int, bool) {
    if (!gTestGrade) return false;
    Microsoft::WRL::ComPtr<ID3D11Device> device; context->GetDevice(&device);
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth = sizeof gTestConstants; desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA const data{&gTestConstants, 0, 0};
    gTestBuffer.Reset();
    if (FAILED(device->CreateBuffer(&desc, &data, &gTestBuffer))) return false;
    ID3D11Buffer* const buffer = gTestBuffer.Get(); context->PSSetConstantBuffers(1, 1, &buffer);
    return true;
}
void EndEye(ID3D11DeviceContext* context) {
    ID3D11Buffer* const none = nullptr; context->PSSetConstantBuffers(1, 1, &none);
}
}
// [Upscale] in the test's own settings file (the host's <GAME>MEVR.ini).
namespace mevr::host {
std::wstring gTestSettings;
std::wstring const& SettingsPath() noexcept { return gTestSettings; }
}

namespace {
using Microsoft::WRL::ComPtr;
namespace copy = mevr::host::eye_copy;
constexpr UINT kWidth = 8, kHeight = 4;
using Pixel = std::array<std::uint8_t, 4>;
using HalfPixel = std::array<std::uint16_t, 4>;
int failures = 0;
void Check(bool ok, char const* expression, int line) {
    if (!ok) { ++failures; std::printf("line %d: %s\n", line, expression); }
}
#define CHECK(expression) Check((expression), #expression, __LINE__)
void Require(HRESULT hr, char const* operation) {
    if (FAILED(hr)) throw std::runtime_error(std::string(operation) + ": HRESULT " + std::to_string(static_cast<unsigned>(hr)));
}
ComPtr<ID3D11Texture2D> Texture(ID3D11Device* device, DXGI_FORMAT format, UINT bind,
    void const* data = nullptr, UINT pitch = 0) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kWidth; desc.Height = kHeight; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = bind;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = data; initial.SysMemPitch = pitch;
    ComPtr<ID3D11Texture2D> texture;
    Require(device->CreateTexture2D(&desc, data ? &initial : nullptr, &texture), "CreateTexture2D");
    return texture;
}
std::vector<Pixel> Read(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    Require(device->CreateTexture2D(&desc, nullptr, &staging), "Create staging");
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Require(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map readback");
    std::vector<Pixel> pixels(kWidth * kHeight);
    for (UINT y = 0; y < kHeight; ++y)
        std::memcpy(pixels.data() + y * kWidth, static_cast<std::uint8_t*>(mapped.pData) + y * mapped.RowPitch, kWidth * sizeof(Pixel));
    context->Unmap(staging.Get(), 0);
    return pixels;
}
bool Near(Pixel const& actual, Pixel const& expected, int tolerance = 2) {
    for (unsigned c = 0; c < 4; ++c) {
        int const difference = static_cast<int>(actual[c]) - expected[c];
        if (difference < -tolerance || difference > tolerance) return false;
    }
    return true;
}
void CheckPixels(std::vector<Pixel> const& actual, std::vector<Pixel> const& expected, char const* label, int tolerance = 2) {
    CHECK(actual.size() == expected.size());
    unsigned wrong = 0;
    for (std::size_t i = 0; i < actual.size() && i < expected.size(); ++i) if (!Near(actual[i], expected[i], tolerance)) {
        if (++wrong == 1) std::printf("%s first mismatch at %zu: actual %u,%u,%u,%u expected %u,%u,%u,%u\n", label, i,
            actual[i][0], actual[i][1], actual[i][2], actual[i][3], expected[i][0], expected[i][1], expected[i][2], expected[i][3]);
    }
    CHECK(wrong == 0);
}
unsigned DebugErrors(ID3D11InfoQueue* queue) {
    unsigned errors = 0;
    auto const count = queue->GetNumStoredMessagesAllowedByRetrievalFilter();
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T size = 0;
        Require(queue->GetMessage(i, nullptr, &size), "GetMessage size");
        std::vector<std::uint8_t> bytes(size);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
        Require(queue->GetMessage(i, message, &size), "GetMessage");
        if (message->Severity == D3D11_MESSAGE_SEVERITY_ERROR || message->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION) ++errors;
    }
    return errors;
}
void Run(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11InfoQueue* queue) {
    // Exact half-float inputs, with independently fixed sRGB byte expectations. Quarter
    // linear light must become ~137, not 64 (missing encode) or ~194 (double encode).
    std::array<HalfPixel, 4> const hdrValues{{
        {0x3400, 0x3800, 0x3c00, 0x3c00}, // .25, .5, 1
        {0x3c00, 0x3400, 0x0000, 0x3c00}, // 1, .25, 0
        {0x0000, 0x2800, 0x3800, 0x3c00}, // 0, .03125, .5
        {0x4400, 0x3c00, 0x3400, 0x3c00}, // 4, 1, .25 (neutral path clips >1)
    }};
    std::array<Pixel, 4> const expectedHdr{{{137,188,255,255}, {255,137,0,255}, {0,49,188,255}, {255,255,137,255}}};
    std::vector<HalfPixel> hdr(kWidth * kHeight);
    std::vector<Pixel> expected(kWidth * kHeight), sentinel(kWidth * kHeight, Pixel{13,19,23,29});
    for (UINT y = 0; y < kHeight; ++y) for (UINT x = 0; x < kWidth; ++x) {
        hdr[y * kWidth + x] = hdrValues[x / 2]; expected[y * kWidth + x] = expectedHdr[x / 2];
    }
    // Like a game's backbuffer, the source cannot be sampled directly: Blit must make its
    // own shader-readable copy. Both target forms occur as swapchain images/views.
    auto source = Texture(device, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET, hdr.data(), kWidth * sizeof(HalfPixel));
    auto target = Texture(device, DXGI_FORMAT_R8G8B8A8_TYPELESS, D3D11_BIND_RENDER_TARGET, sentinel.data(), kWidth * sizeof(Pixel));
    CHECK(!copy::CopyCompatible(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R8G8B8A8_TYPELESS));
    queue->ClearStoredMessages();
    context->CopyResource(target.Get(), source.Get()); // deliberately invalid negative control
    CHECK(DebugErrors(queue) > 0);
    CheckPixels(Read(device, context, target.Get()), sentinel, "incompatible raw copy", 0);
    queue->ClearStoredMessages();

    // Seed a game-owned pipeline state. The conversion must restore it, not leak its RTV,
    // fullscreen topology or shifted viewport into the next game draw.
    auto gameTarget = Texture(device, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    ComPtr<ID3D11RenderTargetView> gameView;
    Require(device->CreateRenderTargetView(gameTarget.Get(), nullptr, &gameView), "game RTV");
    ID3D11RenderTargetView* view = gameView.Get(); context->OMSetRenderTargets(1, &view, nullptr);
    D3D11_VIEWPORT const gameViewport{2,3,6,7,0.2f,0.8f}; context->RSSetViewports(1, &gameViewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);

    for (int shift : {0, 2, -2}) {
        context->UpdateSubresource(target.Get(), 0, nullptr, sentinel.data(), kWidth * sizeof(Pixel), 0);
        CHECK(copy::Copy(context, target.Get(), source.Get(), shift, shift < 0 ? 1 : 0));
        auto shifted = expected;
        for (UINT y = 0; y < kHeight; ++y) for (int x = 0; x < static_cast<int>(kWidth); ++x) {
            int const from = x - shift;
            if (from >= 0 && from < static_cast<int>(kWidth)) shifted[y * kWidth + x] = expected[y * kWidth + from];
        }
        CheckPixels(Read(device, context, target.Get()), shifted, "float HDR / convergence");
        UINT count = 1; D3D11_VIEWPORT after{}; context->RSGetViewports(&count, &after);
        CHECK(count == 1 && std::memcmp(&after, &gameViewport, sizeof after) == 0);
        D3D11_PRIMITIVE_TOPOLOGY topology{}; context->IAGetPrimitiveTopology(&topology);
        CHECK(topology == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
        ComPtr<ID3D11RenderTargetView> afterView; context->OMGetRenderTargets(1, &afterView, nullptr);
        CHECK(afterView.Get() == gameView.Get());
    }
    auto srgbTarget = Texture(device, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, D3D11_BIND_RENDER_TARGET);
    CHECK(copy::Copy(context, srgbTarget.Get(), source.Get(), 0, -1));
    CheckPixels(Read(device, context, srgbTarget.Get()), expected, "typed sRGB target / mono");

    // Neutral SDR uses the complete operation's validated raw path (including alpha).
    // No second caller copy is needed, and no unintended gamma adjustment occurs.
    std::vector<Pixel> sdr(kWidth * kHeight, Pixel{64,128,192,83});
    auto sdrSource = Texture(device, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, sdr.data(), kWidth * sizeof(Pixel));
    CHECK(copy::CopyCompatible(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_TYPELESS));
    CHECK(copy::Copy(context, target.Get(), sdrSource.Get()));
    CheckPixels(Read(device, context, target.Get()), sdr, "SDR compatible raw copy", 0);

    // Cross-family SDR must draw; decode before the sRGB RTV re-encodes, preserving colour
    // rather than brightening it twice. Distinct channels catch BGRA/RGBA confusion.
    std::vector<Pixel> bgra(kWidth * kHeight, Pixel{192,128,64,83});
    auto bgraSource = Texture(device, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_BIND_RENDER_TARGET, bgra.data(), kWidth * sizeof(Pixel));
    CHECK(copy::Copy(context, target.Get(), bgraSource.Get()));
    CheckPixels(Read(device, context, target.Get()), std::vector<Pixel>(kWidth * kHeight, Pixel{64,128,192,255}), "SDR decoded conversion");

    // Raw convergence follows the same fill/shift policy as HDR, including large offsets.
    auto rawPattern = expected;
    for (auto& pixel : rawPattern) pixel[3] = 83;
    context->UpdateSubresource(sdrSource.Get(), 0, nullptr, rawPattern.data(), kWidth * sizeof(Pixel), 0);
    for (int shift : {2, -2, 999, -999}) {
        CHECK(copy::Copy(context, target.Get(), sdrSource.Get(), shift));
        auto shifted = rawPattern;
        int const bounded = shift > 7 ? 7 : shift < -7 ? -7 : shift;
        for (UINT y = 0; y < kHeight; ++y) for (int x = 0; x < static_cast<int>(kWidth); ++x) {
            int const from = x - bounded;
            if (from >= 0 && from < static_cast<int>(kWidth)) shifted[y * kWidth + x] = rawPattern[y * kWidth + from];
        }
        CheckPixels(Read(device, context, target.Get()), shifted, "raw convergence", 0);
    }

    // A compatible format alone is not permission to CopyResource: sizes must match too.
    D3D11_TEXTURE2D_DESC smallDesc{}; sdrSource->GetDesc(&smallDesc); smallDesc.Width = 4;
    D3D11_SUBRESOURCE_DATA smallData{}; smallData.pSysMem = sdr.data(); smallData.SysMemPitch = 4 * sizeof(Pixel);
    ComPtr<ID3D11Texture2D> smallSource;
    Require(device->CreateTexture2D(&smallDesc, &smallData, &smallSource), "small SDR source");
    CHECK(copy::Copy(context, target.Get(), smallSource.Get()));
    CheckPixels(Read(device, context, target.Get()), std::vector<Pixel>(kWidth * kHeight, Pixel{64,128,192,255}), "same-family resize draws");

    // Actual unsupported target/source failures must report false, preserve pixels and
    // issue no invalid fallback. In particular, EnsureCopy's unsupported-source return
    // used to be swallowed by void Blit::Draw while EyeCopy claimed success.
    auto unsupportedTarget = Texture(device, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, sentinel.data(), kWidth * sizeof(Pixel));
    CHECK(!copy::Copy(context, unsupportedTarget.Get(), source.Get()));
    CheckPixels(Read(device, context, unsupportedTarget.Get()), sentinel, "no sRGB target view", 0);
    auto noTargetView = Texture(device, DXGI_FORMAT_R8G8B8A8_TYPELESS, 0, sentinel.data(), kWidth * sizeof(Pixel));
    CHECK(!copy::Copy(context, noTargetView.Get(), source.Get()));
    CheckPixels(Read(device, context, noTargetView.Get()), sentinel, "no target bind", 0);
    auto typelessSource = Texture(device, DXGI_FORMAT_R16G16B16A16_TYPELESS, D3D11_BIND_RENDER_TARGET, hdr.data(), kWidth * sizeof(HalfPixel));
    context->UpdateSubresource(target.Get(), 0, nullptr, sentinel.data(), kWidth * sizeof(Pixel), 0);
    CHECK(!copy::Copy(context, target.Get(), typelessSource.Get()));
    CheckPixels(Read(device, context, target.Get()), sentinel, "EnsureCopy refused source", 0);
    CHECK(!copy::Copy(nullptr, target.Get(), source.Get()));
    CHECK(!copy::Copy(context, nullptr, source.Get()));
    CHECK(!copy::Copy(context, target.Get(), nullptr));
    CHECK(!copy::Copy(context, target.Get(), target.Get()));
    mevr::d3d11::Blit uninitialized;
    CHECK(!uninitialized.Draw(context, source.Get(), gameView.Get(), kWidth, kHeight));
    CHECK(!uninitialized.DrawWith(context, source.Get(), gameView.Get(), kWidth, kHeight, 0, nullptr, nullptr));

    D3D11_TEXTURE2D_DESC arrayDesc{}; sdrSource->GetDesc(&arrayDesc); arrayDesc.ArraySize = 2;
    ComPtr<ID3D11Texture2D> arraySource;
    Require(device->CreateTexture2D(&arrayDesc, nullptr, &arraySource), "array source");
    CHECK(!copy::Copy(context, target.Get(), arraySource.Get()));
    CheckPixels(Read(device, context, target.Get()), sentinel, "array source refused", 0);

    // The cached single-sample intermediate must switch between CopyResource and resolve
    // when only the source's sample count changes, then switch back without stale state.
    D3D11_TEXTURE2D_DESC msaaDesc{}; source->GetDesc(&msaaDesc); msaaDesc.SampleDesc.Count = 4;
    ComPtr<ID3D11Texture2D> msaaSource; ComPtr<ID3D11RenderTargetView> msaaView;
    Require(device->CreateTexture2D(&msaaDesc, nullptr, &msaaSource), "MSAA source");
    Require(device->CreateRenderTargetView(msaaSource.Get(), nullptr, &msaaView), "MSAA source view");
    constexpr float colour[]{0.25f,0.5f,1.0f,1.0f}; context->ClearRenderTargetView(msaaView.Get(), colour);
    CHECK(copy::Copy(context, target.Get(), source.Get()));
    CHECK(copy::Copy(context, target.Get(), msaaSource.Get()));
    CheckPixels(Read(device, context, target.Get()), std::vector<Pixel>(kWidth * kHeight, Pixel{137,188,255,255}), "single to MSAA resolve");
    CHECK(copy::Copy(context, target.Get(), source.Get()));
    CheckPixels(Read(device, context, target.Get()), expected, "MSAA back to single sample");
    CHECK(DebugErrors(queue) == 0);
    context->ClearState();
    copy::Reset();
}

// ME1's eye upscale in the copy (plugin/Upscale, the real module and FSR 1's real shaders; research/27 "One eye-image
// pipeline"): an 8-bit picture smaller than the image both ways is scaled up by EASU and RCAS through the image's UNORM
// view, with the sequels' convergence shift and, when on, the colour correction at RCAS's end; the game's pipeline
// state comes back. Then the method off: the same copy drawn bilinearly.
constexpr UINT kSmallW = 16, kSmallH = 8, kBigW = 32, kBigH = 16;
ComPtr<ID3D11Texture2D> Sized(ID3D11Device* device, DXGI_FORMAT format, UINT width, UINT height, void const* data) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1; desc.Format = format;
    desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = data; initial.SysMemPitch = width * sizeof(Pixel);
    ComPtr<ID3D11Texture2D> texture;
    Require(device->CreateTexture2D(&desc, data ? &initial : nullptr, &texture), "CreateTexture2D sized");
    return texture;
}
std::vector<Pixel> ReadBig(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ComPtr<ID3D11Texture2D> typed;  // a TYPELESS image can't be mapped from a staging copy of its own format
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Require(device->CreateTexture2D(&desc, nullptr, &typed), "Create staging big");
    context->CopyResource(typed.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Require(context->Map(typed.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map big");
    std::vector<Pixel> pixels(kBigW * kBigH);
    for (UINT y = 0; y < kBigH; ++y)
        std::memcpy(pixels.data() + y * kBigW, static_cast<std::uint8_t*>(mapped.pData) + y * mapped.RowPitch, kBigW * sizeof(Pixel));
    context->Unmap(typed.Get(), 0);
    return pixels;
}
void CheckColumns(std::vector<Pixel> const& image, UINT first, UINT last, Pixel const& want, char const* label, int tolerance) {
    for (UINT y = 0; y < kBigH; ++y) for (UINT x = first; x <= last; ++x) {
        if (!Near(image[y * kBigW + x], want, tolerance)) {
            ++failures;
            Pixel const& got = image[y * kBigW + x];
            std::printf("%s: pixel (%u, %u) is %u %u %u %u, wanted %u %u %u %u\n", label, x, y, got[0], got[1], got[2], got[3],
                want[0], want[1], want[2], want[3]);
            return;
        }
    }
}
void RunUpscale(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11InfoQueue* queue, std::wstring const& ini) {
    WritePrivateProfileStringW(L"Upscale", L"method", L"fsr", ini.c_str());
    WritePrivateProfileStringW(L"Upscale", L"sharpness", L"0.9", ini.c_str());
    Pixel const a{64, 128, 192, 255}, b{200, 60, 20, 255};
    std::vector<Pixel> halves(kSmallW * kSmallH);
    for (UINT y = 0; y < kSmallH; ++y) for (UINT x = 0; x < kSmallW; ++x) halves[y * kSmallW + x] = x < kSmallW / 2 ? a : b;
    auto source = Sized(device, DXGI_FORMAT_R8G8B8A8_UNORM, kSmallW, kSmallH, halves.data());
    std::vector<Pixel> const sentinel(kBigW * kBigH, Pixel{13, 19, 23, 29});
    auto image = Sized(device, DXGI_FORMAT_R8G8B8A8_TYPELESS, kBigW, kBigH, sentinel.data());

    auto gameTarget = Texture(device, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    ComPtr<ID3D11RenderTargetView> gameView;
    Require(device->CreateRenderTargetView(gameTarget.Get(), nullptr, &gameView), "game RTV");
    ID3D11RenderTargetView* view = gameView.Get(); context->OMSetRenderTargets(1, &view, nullptr);
    D3D11_VIEWPORT const gameViewport{2,3,6,7,0.2f,0.8f}; context->RSSetViewports(1, &gameViewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    queue->ClearStoredMessages();

    // No shift: each half keeps its colour away from the edge (the edge is at column 16).
    CHECK(copy::Copy(context, image.Get(), source.Get(), 0, 0));
    auto out = ReadBig(device, context, image.Get());
    CheckColumns(out, 0, 11, a, "FSR left half", 2);
    CheckColumns(out, 20, 31, b, "FSR right half", 2);
    // Shifted 8 pixels right: the edge at column 24; the vacated 8 columns keep the unshifted picture (the left colour).
    CHECK(copy::Copy(context, image.Get(), source.Get(), 8, 1));
    out = ReadBig(device, context, image.Get());
    CheckColumns(out, 0, 19, a, "FSR shifted right: left colour", 2);
    CheckColumns(out, 30, 31, b, "FSR shifted right: right colour", 8);
    // Shifted 8 pixels left: the edge at column 8; the vacated columns on the right keep the unshifted right colour.
    CHECK(copy::Copy(context, image.Get(), source.Get(), -8, 0));
    out = ReadBig(device, context, image.Get());
    CheckColumns(out, 0, 1, a, "FSR shifted left: left colour", 8);
    CheckColumns(out, 12, 31, b, "FSR shifted left: right colour", 2);
    // The game's pipeline state is back.
    UINT count = 1; D3D11_VIEWPORT after{}; context->RSGetViewports(&count, &after);
    CHECK(count == 1 && std::memcmp(&after, &gameViewport, sizeof after) == 0);
    D3D11_PRIMITIVE_TOPOLOGY topology{}; context->IAGetPrimitiveTopology(&topology);
    CHECK(topology == D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    ComPtr<ID3D11RenderTargetView> afterView; context->OMGetRenderTargets(1, &afterView, nullptr);
    CHECK(afterView.Get() == gameView.Get());

    // The colour correction at RCAS's end: a flat picture comes out as core/vr/Colour's CPU reference grades it.
    namespace cv = mevr::vr::colour;
    cv::Settings grade;
    grade.contrast = 1.06f; grade.saturation = 1.12f; grade.gamma = 0.90f; grade.black = 0.03f;
    mevr::host::colour::gTestConstants = cv::MakeConstants(grade, 0.0f, false, 0u);
    mevr::host::colour::gTestGrade = true;
    std::vector<Pixel> const flat(kSmallW * kSmallH, a);
    context->UpdateSubresource(source.Get(), 0, nullptr, flat.data(), kSmallW * sizeof(Pixel), 0);
    CHECK(copy::Copy(context, image.Get(), source.Get(), 0, 0));
    cv::Rgb const graded = cv::Correct(mevr::host::colour::gTestConstants, {a[0] / 255.0f, a[1] / 255.0f, a[2] / 255.0f}, 5, 5, false);
    Pixel const wanted{cv::ToUnorm8(graded[0]), cv::ToUnorm8(graded[1]), cv::ToUnorm8(graded[2]), 255};
    CHECK(!Near(wanted, a, 2));  // the grade changes this colour, so the check below means something
    CheckColumns(ReadBig(device, context, image.Get()), 0, 31, wanted, "FSR with the colour correction", 1);
    mevr::host::colour::gTestGrade = false;
    CHECK(copy::Copy(context, image.Get(), source.Get(), 0, 0));
    CheckColumns(ReadBig(device, context, image.Get()), 0, 31, a, "FSR, flat stays flat", 1);

    // The method off (re-read within a second): the larger image is still filled, bilinearly.
    WritePrivateProfileStringW(L"Upscale", L"method", L"off", ini.c_str());
    Sleep(1200);
    context->UpdateSubresource(image.Get(), 0, nullptr, sentinel.data(), kBigW * sizeof(Pixel), 0);
    CHECK(copy::Copy(context, image.Get(), source.Get(), 0, 0));
    CheckColumns(ReadBig(device, context, image.Get()), 0, 31, a, "method off: bilinear fill", 1);
    CHECK(DebugErrors(queue) == 0);
    context->ClearState();
    copy::Reset();
    mevr::host::colour::gTestBuffer.Reset();
}
}

int main() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    HRESULT const hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, 2, D3D11_SDK_VERSION, &device, nullptr, &context);
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) {
        std::puts("SKIP: Windows D3D11 debug layer is required for the incompatible-copy negative control");
        return 77;
    }
    try {
        Require(hr, "Create WARP device");
        ComPtr<ID3D11InfoQueue> queue; Require(device.As(&queue), "D3D11 debug queue");
        Run(device.Get(), context.Get(), queue.Get());
        mevr::host::gTestSettings = (mevr::tests::TempRoot() / L"EyeCopyWarp.ini").wstring();
        RunUpscale(device.Get(), context.Get(), queue.Get(), mevr::host::gTestSettings);
        mevr::tests::RemoveTempRoot();
    } catch (std::exception const& error) {
        std::printf("ERROR: %s\n", error.what()); return 1;
    }
    std::printf("EyeCopy/Blit WARP: %s (HDR, SDR, shift, failure refusal, resize, MSAA, pipeline restore, FSR upscale with "
                "shift and colour)\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
