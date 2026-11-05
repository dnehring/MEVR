// SPDX-License-Identifier: MIT
// The bloom for our holographic quads (plugin/Glow.hpp).

#include "plugin/Glow.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>

namespace mevr::host::glow {
namespace {

using Microsoft::WRL::ComPtr;

constexpr char kShaderSource[] = R"hlsl(
Texture2D source0 : register(t0);
Texture2D source1 : register(t1);
Texture2D source2 : register(t2);
SamplerState linearClamp : register(s0);

cbuffer Constants : register(b0) {
    float2 texel;   // the input's texel size
    float2 dir;     // the blur's direction
    float4 params;  // threshold, near amount, far amount, spread
};

struct VSOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// Fullscreen triangle from SV_VertexID; no vertex buffer.
VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

// Four bilinear taps: the average of the 4x4 input texels under a half-size texel.
float3 Box(float2 uv) {
    float3 c = source0.Sample(linearClamp, uv + texel * float2(-1.0, -1.0)).rgb;
    c += source0.Sample(linearClamp, uv + texel * float2(1.0, -1.0)).rgb;
    c += source0.Sample(linearClamp, uv + texel * float2(-1.0, 1.0)).rgb;
    c += source0.Sample(linearClamp, uv + texel * float2(1.0, 1.0)).rgb;
    return c * 0.25;
}

// What glows: by the brightest channel, so saturated blue and orange count as much as white.
float4 PSPrefilter(VSOut i) : SV_Target {
    float3 c = Box(i.uv);
    float m = max(c.r, max(c.g, c.b));
    return float4(c * smoothstep(params.x, 1.0, m), 0.0);
}

float4 PSDown(VSOut i) : SV_Target {
    return float4(Box(i.uv), 0.0);
}

// A 9-tap Gaussian in 5 bilinear fetches.
float4 PSBlur(VSOut i) : SV_Target {
    float2 stepUv = dir * texel * params.w;
    float3 c = source0.Sample(linearClamp, i.uv).rgb * 0.2270270;
    c += (source0.Sample(linearClamp, i.uv + stepUv * 1.3846154).rgb + source0.Sample(linearClamp, i.uv - stepUv * 1.3846154).rgb) * 0.3162162;
    c += (source0.Sample(linearClamp, i.uv + stepUv * 3.2307692).rgb + source0.Sample(linearClamp, i.uv - stepUv * 3.2307692).rgb) * 0.0702703;
    return float4(c, 0.0);
}

// The canvas plus its glow; alpha stays the canvas's, so the glow adds light in a premultiplied layer.
float4 PSComposite(VSOut i) : SV_Target {
    float4 c = source0.Load(int3(i.position.xy, 0));
    float3 g = source1.Sample(linearClamp, i.uv).rgb * params.y + source2.Sample(linearClamp, i.uv).rgb * params.z;
    return float4(min(c.rgb + g, 1.0), c.a);
}
)hlsl";

ComPtr<ID3DBlob> Compile(char const* entry, char const* target, std::string* error) {
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    HRESULT const hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "MEVR_Glow", nullptr, nullptr, entry, target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        if (error != nullptr) {
            *error = std::string("glow shader ") + entry + ": " +
                (errors ? static_cast<char const*>(errors->GetBufferPointer()) : "unknown error");
        }
        return nullptr;
    }
    return code;
}

bool Fail(std::string* error, char const* what) {
    if (error != nullptr) {
        *error = what;
    }
    return false;
}

}  // namespace

bool Glow::Initialize(ID3D11Device* device, std::string* error) {
    Shutdown();
    if (device == nullptr) {
        return Fail(error, "glow: no device");
    }
    ComPtr<ID3D11Device1> device1;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1)))) {
        return Fail(error, "glow: ID3D11Device1 unavailable");
    }
    D3D_FEATURE_LEVEL const featureLevel = device->GetFeatureLevel();
    UINT const stateFlags = (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) != 0
        ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
        : 0;
    if (FAILED(device1->CreateDeviceContextState(stateFlags, &featureLevel, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), nullptr,
            &state_))) {
        return Fail(error, "glow: CreateDeviceContextState failed");
    }
    auto const vs = Compile("VSMain", "vs_5_0", error);
    auto const prefilter = Compile("PSPrefilter", "ps_5_0", error);
    auto const down = Compile("PSDown", "ps_5_0", error);
    auto const blur = Compile("PSBlur", "ps_5_0", error);
    auto const composite = Compile("PSComposite", "ps_5_0", error);
    if (!vs || !prefilter || !down || !blur || !composite) {
        state_.Reset();
        return false;
    }
    if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vs_)) ||
        FAILED(device->CreatePixelShader(prefilter->GetBufferPointer(), prefilter->GetBufferSize(), nullptr, &prefilter_)) ||
        FAILED(device->CreatePixelShader(down->GetBufferPointer(), down->GetBufferSize(), nullptr, &down_)) ||
        FAILED(device->CreatePixelShader(blur->GetBufferPointer(), blur->GetBufferSize(), nullptr, &blur_)) ||
        FAILED(device->CreatePixelShader(composite->GetBufferPointer(), composite->GetBufferSize(), nullptr, &composite_))) {
        Shutdown();
        return Fail(error, "glow: shader creation failed");
    }
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = 32;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(device->CreateSamplerState(&sd, &sampler_)) || FAILED(device->CreateBuffer(&bd, nullptr, &constants_)) ||
        FAILED(device->CreateRasterizerState(&rd, &rasterizer_))) {
        Shutdown();
        return Fail(error, "glow: sampler, constants or rasterizer creation failed");
    }
    device_ = device;
    return true;
}

void Glow::Shutdown() {
    half_ = {};
    quarter_ = {};
    rasterizer_.Reset();
    constants_.Reset();
    sampler_.Reset();
    composite_.Reset();
    blur_.Reset();
    down_.Reset();
    prefilter_.Reset();
    vs_.Reset();
    state_.Reset();
    device_.Reset();
}

bool Glow::EnsureLevel(Level& level, unsigned width, unsigned height) {
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    if (level.width == width && level.height == height && level.texture[0]) {
        return true;
    }
    level = {};
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;  // faint haze without banding
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int i = 0; i < 2; ++i) {
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &level.texture[i])) ||
            FAILED(device_->CreateShaderResourceView(level.texture[i].Get(), nullptr, &level.srv[i])) ||
            FAILED(device_->CreateRenderTargetView(level.texture[i].Get(), nullptr, &level.rtv[i]))) {
            level = {};
            return false;
        }
    }
    level.width = width;
    level.height = height;
    return true;
}

bool Glow::EnsureLevels(unsigned width, unsigned height) {
    return EnsureLevel(half_, (width + 1) / 2, (height + 1) / 2) && EnsureLevel(quarter_, (width + 3) / 4, (height + 3) / 4);
}

void Glow::Pass(ID3D11DeviceContext* context, ID3D11PixelShader* shader, ID3D11ShaderResourceView* const* inputs,
    unsigned inputCount, ID3D11RenderTargetView* target, unsigned width, unsigned height, float const (&constants)[8]) {
    ID3D11ShaderResourceView* const none[3] = {};
    context->PSSetShaderResources(0, 3, none);  // nothing bound as an input may be the target
    context->OMSetRenderTargets(1, &target, nullptr);
    context->PSSetShaderResources(0, inputCount, inputs);
    context->UpdateSubresource(constants_.Get(), 0, nullptr, constants, 0, 0);
    D3D11_VIEWPORT const viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    context->RSSetViewports(1, &viewport);
    context->PSSetShader(shader, nullptr, 0);
    context->Draw(3, 0);
}

bool Glow::Apply(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, unsigned width, unsigned height,
    ID3D11RenderTargetView* target, Settings const& settings) {
    if (!device_ || context == nullptr || source == nullptr || target == nullptr || width == 0 || height == 0 ||
        !EnsureLevels(width, height)) {
        return false;
    }
    ComPtr<ID3D11DeviceContext1> context1;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&context1)))) {
        return false;
    }
    ComPtr<ID3DDeviceContextState> previous;
    context1->SwapDeviceContextState(state_.Get(), &previous);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->VSSetShader(vs_.Get(), nullptr, 0);
    ID3D11SamplerState* const sampler = sampler_.Get();
    context->PSSetSamplers(0, 1, &sampler);
    ID3D11Buffer* const constants = constants_.Get();
    context->PSSetConstantBuffers(0, 1, &constants);
    context->RSSetState(rasterizer_.Get());
    context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    context->OMSetDepthStencilState(nullptr, 0);

    float const spread = std::max(settings.spread, 0.0f);
    auto params = [&](float texelX, float texelY, float dirX, float dirY) {
        return std::array<float, 8>{texelX, texelY, dirX, dirY, settings.threshold, settings.nearAmount, settings.farAmount, spread};
    };
    float const sw = 1.0f / static_cast<float>(width);
    float const sh = 1.0f / static_cast<float>(height);
    float const hw = 1.0f / static_cast<float>(half_.width);
    float const hh = 1.0f / static_cast<float>(half_.height);
    float const qw = 1.0f / static_cast<float>(quarter_.width);
    float const qh = 1.0f / static_cast<float>(quarter_.height);
    auto run = [&](ID3D11PixelShader* shader, std::initializer_list<ID3D11ShaderResourceView*> inputs, ID3D11RenderTargetView* out,
                   unsigned w, unsigned h, std::array<float, 8> const& values) {
        float c[8];
        std::memcpy(c, values.data(), sizeof(c));
        Pass(context, shader, inputs.begin(), static_cast<unsigned>(inputs.size()), out, w, h, c);
    };
    run(prefilter_.Get(), {source}, half_.rtv[0].Get(), half_.width, half_.height, params(sw, sh, 0.0f, 0.0f));
    run(blur_.Get(), {half_.srv[0].Get()}, half_.rtv[1].Get(), half_.width, half_.height, params(hw, hh, 1.0f, 0.0f));
    run(blur_.Get(), {half_.srv[1].Get()}, half_.rtv[0].Get(), half_.width, half_.height, params(hw, hh, 0.0f, 1.0f));
    run(down_.Get(), {half_.srv[0].Get()}, quarter_.rtv[0].Get(), quarter_.width, quarter_.height, params(hw, hh, 0.0f, 0.0f));
    run(blur_.Get(), {quarter_.srv[0].Get()}, quarter_.rtv[1].Get(), quarter_.width, quarter_.height, params(qw, qh, 1.5f, 0.0f));
    run(blur_.Get(), {quarter_.srv[1].Get()}, quarter_.rtv[0].Get(), quarter_.width, quarter_.height, params(qw, qh, 0.0f, 1.5f));
    run(composite_.Get(), {source, half_.srv[0].Get(), quarter_.srv[0].Get()}, target, width, height, params(sw, sh, 0.0f, 0.0f));

    ID3D11ShaderResourceView* const none[3] = {};
    context->PSSetShaderResources(0, 3, none);
    ID3D11RenderTargetView* const noTarget = nullptr;
    context->OMSetRenderTargets(1, &noTarget, nullptr);
    context1->SwapDeviceContextState(previous.Get(), nullptr);
    return true;
}

}  // namespace mevr::host::glow
