// SPDX-License-Identifier: MIT
// Real D3D11 recording/replay: GPU-generated indirect arguments and hidden UAV counters.
#include "plugin/DrawReplay.hpp"
#include "core/Hook.hpp"
#include "core/Log.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <array>
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
        std::cout << (hardware ? "Hardware" : "WARP") << " replay checks passed\n";
        return 0;
    } catch (std::exception const& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
