// SPDX-License-Identifier: MIT
// Real D3D11 recording/replay: GPU-generated indirect arguments and hidden UAV counters.
#include "plugin/DrawReplay.hpp"
#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "core/vr/ScreenMatrix.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace replay = mevr::host::draw_replay;

namespace {
ID3D11UnorderedAccessView* const noUav = nullptr;
void Check(bool ok, char const* what) {
    if (!ok) throw std::runtime_error(what);
}
void Hr(HRESULT hr, char const* what) { Check(SUCCEEDED(hr), what); }

ComPtr<ID3D11ComputeShader> Shader(ID3D11Device* device, char const* text) {
    ComPtr<ID3DBlob> code, errors;
    HRESULT const hr = D3DCompile(text, std::strlen(text), nullptr, nullptr, nullptr, "main", "cs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &errors);
    if (errors) std::cerr << static_cast<char const*>(errors->GetBufferPointer());
    Hr(hr, "compile compute shader");
    ComPtr<ID3D11ComputeShader> shader;
    Hr(device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader), "create shader");
    return shader;
}

struct Buffer {
    ComPtr<ID3D11Buffer> resource;
    ComPtr<ID3D11UnorderedAccessView> uav;
};
Buffer MakeBuffer(ID3D11Device* device, UINT words, UINT misc, UINT uavFlags, DXGI_FORMAT format) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = words * sizeof(UINT);
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = misc;
    if (misc & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) desc.StructureByteStride = sizeof(UINT);
    Buffer out;
    Hr(device->CreateBuffer(&desc, nullptr, &out.resource), "create buffer");
    D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
    view.Format = format;
    view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    view.Buffer.NumElements = words;
    view.Buffer.Flags = uavFlags;
    Hr(device->CreateUnorderedAccessView(out.resource.Get(), &view, &out.uav), "create UAV");
    return out;
}

std::vector<UINT> Read(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Buffer* source) {
    D3D11_BUFFER_DESC desc{};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    Hr(device->CreateBuffer(&desc, nullptr, &staging), "create readback");
    context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Hr(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "readback map");
    std::vector<UINT> result(desc.ByteWidth / sizeof(UINT));
    std::memcpy(result.data(), mapped.pData, desc.ByteWidth);
    context->Unmap(staging.Get(), 0);
    return result;
}

void ReplayRecorded() {
    std::string why;
    Check(replay::TailReplayable(&why), why.c_str());
    Check(replay::Replay({}, {}) == 0, "replay failed");
}

void IndirectAndClear(ID3D11Device* device, ID3D11DeviceContext* context) {
    auto args = MakeBuffer(device, 6, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
        D3D11_BUFFER_UAV_FLAG_RAW, DXGI_FORMAT_R32_TYPELESS);
    auto result = MakeBuffer(device, 4, 0, 0, DXGI_FORMAT_R32_UINT);
    auto prepare = Shader(device, R"(
        RWByteAddressBuffer args : register(u3);
        [numthreads(1,1,1)] void main() {
            uint old; args.InterlockedAdd(0, 2, old); args.Store(4, 1); args.Store(8, 1);
            args.InterlockedAdd(12, 3, old); args.Store(16, 1); args.Store(20, 1);
        })");
    auto work = Shader(device, R"(
        RWBuffer<uint> result : register(u5);
        [numthreads(1,1,1)] void main() { InterlockedAdd(result[0], 1); }
    )");
    // A four-channel UINT texture verifies all clear channels, including high bits and copied values.
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = textureDesc.Height = textureDesc.MipLevels = textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R32G32B32A32_UINT;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11UnorderedAccessView> textureView;
    Hr(device->CreateTexture2D(&textureDesc, nullptr, &texture), "create UINT texture");
    Hr(device->CreateUnorderedAccessView(texture.Get(), nullptr, &textureView), "create UINT texture view");
    UINT zero[4]{};
    UINT channels[4]{0x80000001u, 23, 0xffffffffu, 91};
    replay::BeginRecording(0);
    replay::MarkTail();
    context->ClearUnorderedAccessViewUint(args.uav.Get(), zero);
    context->ClearUnorderedAccessViewUint(result.uav.Get(), zero);
    context->ClearUnorderedAccessViewUint(textureView.Get(), channels);
    context->CSSetUnorderedAccessViews(3, 1, args.uav.GetAddressOf(), nullptr);
    context->CSSetShader(prepare.Get(), nullptr, 0);
    context->Dispatch(1, 1, 1);
    context->CSSetUnorderedAccessViews(3, 1, &noUav, nullptr);
    context->CSSetUnorderedAccessViews(5, 1, result.uav.GetAddressOf(), nullptr);
    context->CSSetShader(work.Get(), nullptr, 0);
    context->DispatchIndirect(args.resource.Get(), 0);
    context->DispatchIndirect(args.resource.Get(), 12);
    context->CSSetUnorderedAccessViews(5, 1, &noUav, nullptr);
    replay::EndRecording();
    Check(Read(device, context, result.resource.Get())[0] == 5, "first pass indirect result");
    std::memset(channels, 0, sizeof(channels));
    UINT poison[4]{1, 1, 1, 1};
    context->ClearUnorderedAccessViewUint(args.uav.Get(), poison);
    context->ClearUnorderedAccessViewUint(result.uav.Get(), poison);
    context->ClearUnorderedAccessViewUint(textureView.Get(), zero);
    // The recorded commands must retain objects even after the caller drops its own references.
    args.resource.Reset();
    args.uav.Reset();
    textureView.Reset();
    for (int pass = 0; pass != 2; ++pass) {
        ReplayRecorded();
        Check(Read(device, context, result.resource.Get())[0] == 5, "replayed clear/prepare/indirect ordering or offset");
    }
    ComPtr<ID3D11UnorderedAccessView> bound;
    context->CSGetUnorderedAccessViews(5, 1, &bound);
    Check(!bound, "null UAV entry must unbind its slot");
    textureDesc.Usage = D3D11_USAGE_STAGING;
    textureDesc.BindFlags = 0;
    textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Hr(device->CreateTexture2D(&textureDesc, nullptr, &staging), "create texture readback");
    context->CopyResource(staging.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Hr(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "map texture readback");
    std::array<UINT, 4> actual{};
    std::memcpy(actual.data(), mapped.pData, sizeof(actual));
    context->Unmap(staging.Get(), 0);
    Check(actual == std::array<UINT, 4>{0x80000001u, 23, 0xffffffffu, 91}, "clear values must be copied bit-exactly");
    replay::OnPresentEnd();
    std::cout << "PASS: GPU-generated indirect arguments, offsets, clear values, retained resources and UAV unbind\n";
}

void Counters(ID3D11Device* device, ID3D11DeviceContext* context) {
    auto append = MakeBuffer(device, 16, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, D3D11_BUFFER_UAV_FLAG_APPEND, DXGI_FORMAT_UNKNOWN);
    auto shader = Shader(device, R"(
        AppendStructuredBuffer<uint> items : register(u2);
        [numthreads(1,1,1)] void main() { items.Append(42); }
    )");
    auto count = MakeBuffer(device, 1, 0, 0, DXGI_FORMAT_R32_UINT);
    UINT initial[2]{3, UINT(-1)};
    UINT keep = UINT(-1);
    ID3D11UnorderedAccessView* views[2]{append.uav.Get(), nullptr};
    replay::BeginRecording(0);
    replay::MarkTail();
    context->CSSetShader(shader.Get(), nullptr, 0);
    context->CSSetUnorderedAccessViews(2, 2, views, initial);
    context->Dispatch(1, 1, 1);
    context->CSSetUnorderedAccessViews(2, 1, append.uav.GetAddressOf(), &keep);
    context->Dispatch(1, 1, 1);
    context->CSSetUnorderedAccessViews(2, 1, append.uav.GetAddressOf(), nullptr);
    context->Dispatch(1, 1, 1);
    context->CSSetUnorderedAccessViews(2, 1, &noUav, nullptr);
    context->CSSetUnorderedAccessViews(0, 0, nullptr, nullptr);
    replay::EndRecording();
    context->CopyStructureCount(count.resource.Get(), 0, append.uav.Get());
    Check(Read(device, context, count.resource.Get())[0] == 6, "first pass counter");
    initial[0] = 9;
    views[0] = nullptr;
    keep = 11;
    context->CSSetUnorderedAccessViews(2, 1, append.uav.GetAddressOf(), initial);
    context->CSSetUnorderedAccessViews(2, 1, &noUav, nullptr);
    UINT zero[4]{};
    context->ClearUnorderedAccessViewUint(append.uav.Get(), zero);
    for (int pass = 0; pass != 2; ++pass) {
        ReplayRecorded();
        context->CopyStructureCount(count.resource.Get(), 0, append.uav.Get());
        UINT const counter = Read(device, context, count.resource.Get())[0];
        Check(counter == 6, "reset, keep and null counter values must survive replay");
        auto const data = Read(device, context, append.resource.Get());
        Check(data[2] == 0 && data[3] == 42 && data[4] == 42 && data[5] == 42 && data[6] == 0, "append positions after replay");
    }
    replay::OnPresentEnd();
    std::cout << "PASS: append counter reset, UINT(-1), null counts and zero-count bind\n";
    replay::BeginRecording(0);
    replay::MarkTail();
    context->CopyStructureCount(count.resource.Get(), 0, append.uav.Get());
    replay::EndRecording();
    std::string why;
    Check(!replay::TailReplayable(&why) && why.find("slot 49") != std::string::npos,
        "an unsupported command must still block replay");
    replay::OnPresentEnd();
    std::cout << "PASS: unsupported-command eligibility guard retained\n";
}
// research/37, "The right eye's shadows": a pixel shader taking ScreenToShadowMatrix, recorded with the first eye's
// matrix and replayed for a second eye, must draw with the corrected matrix, and the buffer must hold the first eye's
// bytes again afterwards. Both upload routes (UpdateSubresource on a default buffer, Map on a dynamic one).
void ScreenMatrices(ID3D11Device* device, ID3D11DeviceContext* context, bool dynamic) {
    namespace sm = mevr::vr::screen_matrix;
    // Two eyes 6.4 units apart with mirrored off-centre projections (as tests/ScreenMatrixTests.cpp).
    auto proj = [](float l, float r, float d, float u, float n) {
        return sm::Mat4{2.0f / (r - l), 0, 0, 0, 0, 2.0f / (u - d), 0, 0, -(r + l) / (r - l), -(u + d) / (u - d), 1, 1, 0, 0, -n, 0};
    };
    sm::Mat4 const basis{0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1};
    float const cy = std::cos(0.6f), sy = std::sin(0.6f);
    sm::Mat4 const yaw{cy, sy, 0, 0, -sy, cy, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    sm::Mat4 const rot = sm::Multiply(yaw, basis);
    sm::Mat4 const tvp1 = sm::Multiply(rot, proj(-1.2f, 0.9f, -1.0f, 1.0f, 10.0f));
    sm::Mat4 const tvp2 = sm::Multiply(rot, proj(-0.9f, 1.2f, -1.0f, 1.0f, 10.0f));
    float const pvt1[3] = {1000.0f - 3.2f * rot[0], -2000.0f - 3.2f * rot[4], -300.0f};
    float const pvt2[3] = {1000.0f + 3.2f * rot[0], -2000.0f + 3.2f * rot[4], -300.0f};
    auto const inv1 = sm::Inverse(tvp1);
    auto const inv2 = sm::Inverse(tvp2);
    Check(inv1 && inv2, "invert the views");
    // The view bytes the replay compares: TVP at +0, its inverse at +64, the pre-view translation at +128.
    auto fields = [](sm::Mat4 const& tvp, sm::Mat4 const& inv, float const* pvt) {
        replay::ViewFields f;
        f.perView = 144;
        f.views = 1;
        f.bytes.assign(144, 0);
        std::memcpy(f.bytes.data(), tvp.data(), 64);
        std::memcpy(f.bytes.data() + 64, inv.data(), 64);
        std::memcpy(f.bytes.data() + 128, pvt, 12);
        return f;
    };
    replay::ViewLayout layout;
    layout.translatedViewProjection = 0;
    layout.invTranslatedViewProjection = 64;
    layout.preViewTranslation = 128;
    replay::SetViewLayout(layout);
    replay::ViewFields const first = fields(tvp1, *inv1, pvt1);
    replay::ViewFields const second = fields(tvp2, *inv2, pvt2);

    // The first eye's ScreenToShadowMatrix: an arbitrary R behind S1 x Inv(TVP1).
    auto const s1 = sm::ScreenToClip(tvp1);
    Check(s1.has_value(), "S1");
    sm::Mat4 const r{0.002f, 0.0003f, 0, 0, 0, -0.002f, 0.0001f, 0, 0.0004f, 0, 0.0001f, 0, 0.5f, 0.5f, 0.01f, 1};
    sm::Mat4 const m1 = sm::Multiply(sm::Multiply(*s1, *inv1), r);

    char const* const vsText = "float4 main(uint id : SV_VertexID) : SV_Position {"
                               " float2 p = float2(id & 2, (id << 1) & 2); return float4(p * 2 - 1, 0.5, 1); }";   // clockwise
    char const* const psText = "float4 Input; row_major float4x4 ScreenToShadowMatrix;"
                               " float4 main(float4 p : SV_Position) : SV_Target { return mul(Input, ScreenToShadowMatrix); }";
    ComPtr<ID3DBlob> vsCode, psCode, errors;
    Hr(D3DCompile(vsText, std::strlen(vsText), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsCode, &errors), "compile VS");
    Hr(D3DCompile(psText, std::strlen(psText), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psCode, &errors), "compile PS");
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    Hr(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs), "create VS");
    Hr(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps), "create PS");
    replay::NotePixelShader(ps.Get(), psCode->GetBufferPointer(), psCode->GetBufferSize());

    // The constant buffer: Input at +0, the matrix at +16 (as the compiler lays out $Globals).
    float constants[20]{};
    float const input[4] = {0.3f, -0.2f, 250.0f, 1.0f};   // the second eye's (x z, y z, z, 1)
    std::memcpy(constants, input, 16);
    std::memcpy(constants + 4, m1.data(), 64);
    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = sizeof(constants);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    cbDesc.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    ComPtr<ID3D11Buffer> cb;
    Hr(device->CreateBuffer(&cbDesc, nullptr, &cb), "create constant buffer");

    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> rtv;
    Hr(device->CreateTexture2D(&td, nullptr, &target), "create target");
    Hr(device->CreateRenderTargetView(target.Get(), nullptr, &rtv), "create RTV");
    auto readPixel = [&] {
        D3D11_TEXTURE2D_DESC sd = td;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        Hr(device->CreateTexture2D(&sd, nullptr, &staging), "create pixel readback");
        context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Hr(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "map pixel");
        std::array<float, 4> px{};
        std::memcpy(px.data(), mapped.pData, sizeof(px));
        context->Unmap(staging.Get(), 0);
        return px;
    };

    D3D11_VIEWPORT const vp{0, 0, 1, 1, 0, 1};
    replay::BeginRecording(0);
    replay::MarkTail();
    context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    context->RSSetViewports(1, &vp);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetShader(ps.Get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, cb.GetAddressOf());
    if (dynamic) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Hr(context->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "map constants");
        std::memcpy(mapped.pData, constants, sizeof(constants));
        context->Unmap(cb.Get(), 0);
    } else {
        context->UpdateSubresource(cb.Get(), 0, nullptr, constants, 0, 0);
    }
    context->Draw(3, 0);
    replay::EndRecording();

    float const zero[4]{};
    context->ClearRenderTargetView(rtv.Get(), zero);
    std::string why;
    Check(replay::TailReplayable(&why), why.c_str());
    Check(replay::Replay(first, second) == 0, "replay with the second eye's view");
    std::array<float, 4> const got = readPixel();
    replay::Stats const stats = replay::TakeStats();
    std::cout << "screen matrices: patched " << stats.screenPatched << ", missed " << stats.screenMissed << ", replayed draws "
              << stats.replayedDraws << ", pixel " << got[0] << " " << got[1] << " " << got[2] << " " << got[3] << "\n";

    auto const c = sm::SecondEyeCorrection(tvp1, tvp2, pvt1, pvt2);
    Check(c.has_value(), "correction");
    sm::Mat4 m2 = m1;
    sm::Apply(*c, m2.data(), true);
    for (int k = 0; k < 4; ++k) {
        float const want = input[0] * m2[k] + input[1] * m2[4 + k] + input[2] * m2[8 + k] + input[3] * m2[12 + k];
        float const first1 = input[0] * m1[k] + input[1] * m1[4 + k] + input[2] * m1[8 + k] + input[3] * m1[12 + k];
        if (k == 0) {
            std::cout << "  want " << want << ", the first eye's " << first1 << "\n";
        }
        Check(std::fabs(got[k] - want) <= 1e-3f * (1.0f + std::fabs(want)), "the replayed draw used the second eye's matrix");
        if (k == 0) {
            Check(std::fabs(want - first1) > 1e-3f, "the test's eyes differ enough to tell");
        }
    }
    // The engine's buffer holds the first eye's bytes again (it re-uploads only what it changed itself).
    D3D11_BUFFER_DESC rd = cbDesc;
    rd.Usage = D3D11_USAGE_STAGING;
    rd.BindFlags = 0;
    rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> back;
    Hr(device->CreateBuffer(&rd, nullptr, &back), "create constants readback");
    context->CopyResource(back.Get(), cb.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Hr(context->Map(back.Get(), 0, D3D11_MAP_READ, 0, &mapped), "map constants readback");
    bool const restored = std::memcmp(mapped.pData, constants, sizeof(constants)) == 0;
    context->Unmap(back.Get(), 0);
    Check(restored, "the first eye's constants put back after the replay");
    Check(stats.screenPatched == 1 && stats.screenShadowShaders >= 1, "one draw corrected, the shader known");
    replay::OnPresentEnd();
    std::cout << "PASS: second-eye ScreenToShadowMatrix through " << (dynamic ? "Map" : "UpdateSubresource") << "\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        bool const hardware = argc > 1 && std::string(argv[1]) == "--hardware";
        mevr::log::SetSink([](char const*, char const* message) { std::cerr << message << '\n'; });
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL const level = D3D_FEATURE_LEVEL_11_0;
        Hr(D3D11CreateDevice(nullptr, hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr, 0,
            &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context), "create D3D11 device");
        mevr::hook::Context hooks(nullptr, "ReplayGpuTest");
        Check(replay::Attach(context.Get(), &hooks), "attach recorder");
        IndirectAndClear(device.Get(), context.Get());
        Counters(device.Get(), context.Get());
        ScreenMatrices(device.Get(), context.Get(), false);
        ScreenMatrices(device.Get(), context.Get(), true);
        std::cout << (hardware ? "Hardware" : "WARP") << " replay checks passed\n";
        return 0;
    } catch (std::exception const& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
