#include "core/d3d11/Blit.hpp"

#include "core/Log.hpp"

#include <d3dcompiler.h>

#include <cstring>

namespace mevr::d3d11 {
namespace {

using Microsoft::WRL::ComPtr;

constexpr char kShaderSource[] = R"hlsl(
Texture2D source : register(t0);
SamplerState linearClamp : register(s0);

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

float3 SrgbToLinear(float3 c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float4 PSMain(VSOut i) : SV_Target {
    float3 c = source.Sample(linearClamp, i.uv).rgb;
#if DECODE_SRGB
    c = SrgbToLinear(c);
#endif
    return float4(c, 1.0);
}
)hlsl";

ComPtr<ID3DBlob> Compile(char const* entry, char const* target, bool decodeSrgb) {
    D3D_SHADER_MACRO const defines[] = {{"DECODE_SRGB", decodeSrgb ? "1" : "0"}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    HRESULT const hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "MEVR_Blit", defines, nullptr,
        entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(hr)) {
        log::Error("blit shader {} failed: {}", entry,
            errors ? static_cast<char const*>(errors->GetBufferPointer()) : "unknown error");
        return nullptr;
    }
    return code;
}

} // namespace

bool IsGammaEncodedUnorm(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return true;
    default:
        return false;
    }
}

char const* FormatName(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8X8_UNORM: return "B8G8R8X8_UNORM";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    default: return "other";
    }
}

bool Blit::Initialize(ID3D11Device* device) {
    device_ = device;
    if (FAILED(device->CreateDeferredContext(0, &deferred_))) {
        log::Error("blit: CreateDeferredContext failed");
        return false;
    }
    auto const vsCode = Compile("VSMain", "vs_5_0", false);
    auto const psCode = Compile("PSMain", "ps_5_0", false);
    auto const psDecodeCode = Compile("PSMain", "ps_5_0", true);
    if (!vsCode || !psCode || !psDecodeCode) {
        return false;
    }
    if (FAILED(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs_)) ||
        FAILED(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &psPassthrough_)) ||
        FAILED(device->CreatePixelShader(psDecodeCode->GetBufferPointer(), psDecodeCode->GetBufferSize(), nullptr, &psDecodeSrgb_))) {
        log::Error("blit: shader creation failed");
        return false;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sd, &sampler_))) {
        log::Error("blit: sampler creation failed");
        return false;
    }
    return true;
}

void Blit::Shutdown() {
    copySrv_.Reset();
    copy_.Reset();
    sampler_.Reset();
    psDecodeSrgb_.Reset();
    psPassthrough_.Reset();
    vs_.Reset();
    deferred_.Reset();
    device_.Reset();
    copyDesc_ = {};
}

bool Blit::EnsureCopy(ID3D11Texture2D* source) {
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    if (copy_ && desc.Width == copyDesc_.Width && desc.Height == copyDesc_.Height &&
        desc.Format == copyDesc_.Format) {
        return true;
    }
    copySrv_.Reset();
    copy_.Reset();

    sourceIsMultisampled_ = desc.SampleDesc.Count > 1;
    decodeSrgb_ = IsGammaEncodedUnorm(desc.Format);
    D3D11_TEXTURE2D_DESC copyDesc = desc;
    copyDesc.MipLevels = 1;
    copyDesc.ArraySize = 1;
    copyDesc.SampleDesc = {1, 0};
    copyDesc.Usage = D3D11_USAGE_DEFAULT;
    copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    copyDesc.CPUAccessFlags = 0;
    copyDesc.MiscFlags = 0;
    if (FAILED(device_->CreateTexture2D(&copyDesc, nullptr, &copy_)) ||
        FAILED(device_->CreateShaderResourceView(copy_.Get(), nullptr, &copySrv_))) {
        log::Error("blit: copy texture {}x{} {} failed", desc.Width, desc.Height, FormatName(desc.Format));
        copy_.Reset();
        return false;
    }
    copyDesc_ = copyDesc;
    log::Info("blit: source {}x{} {} (format {}), msaa {}, decode sRGB {}", desc.Width, desc.Height,
        FormatName(desc.Format), static_cast<int>(desc.Format), desc.SampleDesc.Count, decodeSrgb_);
    return true;
}

void Blit::Draw(ID3D11DeviceContext* immediate, ID3D11Texture2D* source, ID3D11RenderTargetView* target,
    UINT targetWidth, UINT targetHeight) {
    if (!deferred_ || !EnsureCopy(source)) {
        return;
    }
    ID3D11DeviceContext* const ctx = deferred_.Get();
    if (sourceIsMultisampled_) {
        ctx->ResolveSubresource(copy_.Get(), 0, source, 0, copyDesc_.Format);
    } else {
        ctx->CopyResource(copy_.Get(), source);
    }

    D3D11_VIEWPORT const viewport{0.0f, 0.0f, static_cast<float>(targetWidth), static_cast<float>(targetHeight), 0.0f, 1.0f};
    ID3D11ShaderResourceView* const srv = copySrv_.Get();
    ID3D11SamplerState* const sampler = sampler_.Get();
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs_.Get(), nullptr, 0);
    ctx->PSSetShader(decodeSrgb_ ? psDecodeSrgb_.Get() : psPassthrough_.Get(), nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->PSSetSamplers(0, 1, &sampler);
    ctx->OMSetRenderTargets(1, &target, nullptr);
    ctx->RSSetViewports(1, &viewport);
    ctx->Draw(3, 0);

    ComPtr<ID3D11CommandList> commands;
    if (SUCCEEDED(ctx->FinishCommandList(FALSE, &commands))) {
        immediate->ExecuteCommandList(commands.Get(), TRUE);  // TRUE: restore the game's state
    }
}

} // namespace mevr::d3d11
