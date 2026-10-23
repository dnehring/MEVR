#include "plugin/menu/MenuBackdrop.hpp"

#include "plugin/menu/StateGuard.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cstring>

namespace mevr::menu {
namespace {

using Microsoft::WRL::ComPtr;

constexpr char kShader[] = R"(
cbuffer Params : register(b0) {
    float4 rect;     // the source rectangle (u0, v0, du, dv), or the blur's step (du, dv, 0, 0)
    float4 misc;     // decodes, a destination pixel in source uv (x, y), 0
    float4 center;   // the panel in the eye's view space
    float4 right;
    float4 up;
    float4 tans;     // left, right, up, down
};
Texture2D source : register(t0);
SamplerState linearClamp : register(s0);

struct Out { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

Out VSMain(uint id : SV_VertexID) {
    Out o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float3 Encode(float3 c) {
    c = saturate(c);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

// An eye image's rect, downsampled with four bilinear taps per pixel (a 4 x 4 footprint).
float4 PSCapture(Out i) : SV_Target {
    float2 uv = rect.xy + i.uv * rect.zw;
    float2 d = misc.yz * 0.25;
    float3 c = source.Sample(linearClamp, uv + float2(-d.x, -d.y)).rgb + source.Sample(linearClamp, uv + float2(d.x, -d.y)).rgb +
               source.Sample(linearClamp, uv + float2(-d.x, d.y)).rgb + source.Sample(linearClamp, uv + float2(d.x, d.y)).rgb;
    c *= 0.25;
    if (misc.x > 0.5) c = Encode(c);
    return float4(c, 1.0);
}

static const float3 kDark = float3(0.035, 0.05, 0.065);

// Each backdrop pixel is a point on the panel: where it is in the captured eye image.
float4 PSProject(Out i) : SV_Target {
    float2 p = i.uv * 2.0 - 1.0;
    float3 q = center.xyz + right.xyz * p.x - up.xyz * p.y;
    if (q.z > -0.01) return float4(kDark, 1.0);
    float x = q.x / -q.z;
    float y = q.y / -q.z;
    float2 uv = float2((x - tans.x) / (tans.y - tans.x), (tans.z - y) / (tans.z - tans.w));
    float2 outside = max(max(-uv, uv - 1.0), 0.0);
    float fade = saturate(1.0 - 8.0 * max(outside.x, outside.y));
    float3 c = source.SampleLevel(linearClamp, saturate(uv), 0).rgb;
    return float4(lerp(kDark, c, fade), 1.0);
}

// Nine taps (five bilinear fetches), Gaussian; `rect.xy` the step between taps.
float4 PSBlur(Out i) : SV_Target {
    float2 s = rect.xy;
    float3 c = source.Sample(linearClamp, i.uv).rgb * 0.2270270;
    c += (source.Sample(linearClamp, i.uv + s * 1.3846154).rgb + source.Sample(linearClamp, i.uv - s * 1.3846154).rgb) * 0.3162162;
    c += (source.Sample(linearClamp, i.uv + s * 3.2307692).rgb + source.Sample(linearClamp, i.uv - s * 3.2307692).rgb) * 0.0702703;
    return float4(c, 1.0);
}

// Frosted and dimmed: less colour, darker, a little cool, a faint vignette toward the panel's edges.
float4 PSGrade(Out i) : SV_Target {
    float3 c = source.Sample(linearClamp, i.uv).rgb;
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(luma.xxx, c, 0.62);
    c = c * 0.52 + float3(0.012, 0.022, 0.032);
    float2 e = i.uv * 2.0 - 1.0;
    c *= 1.0 - 0.18 * saturate(dot(e, e) - 0.35);
    return float4(c, 1.0);
}

// No world to show: the mockup's dark ground.
float4 PSClear(Out i) : SV_Target {
    float3 a = float3(0.047, 0.082, 0.114);
    float3 b = float3(0.020, 0.031, 0.047);
    return float4(lerp(a, b, saturate(i.uv.y * 0.8 + i.uv.x * 0.2)), 1.0);
}
)";

struct Params {
    float rect[4];
    float misc[4];
    float center[4];
    float right[4];
    float up[4];
    float tans[4];
};

bool Compile(char const* entry, char const* target, ComPtr<ID3DBlob>& out) {
    ComPtr<ID3DBlob> errors;
    return SUCCEEDED(D3DCompile(kShader, sizeof kShader - 1, "mevr-menu-backdrop", nullptr, nullptr, entry, target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out.GetAddressOf(), errors.GetAddressOf()));
}

bool Target(ID3D11Device* device, unsigned width, unsigned height, DXGI_FORMAT format, ComPtr<ID3D11Texture2D>& texture,
    ComPtr<ID3D11RenderTargetView>& rtv, ComPtr<ID3D11ShaderResourceView>& srv) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = format;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    return SUCCEEDED(device->CreateTexture2D(&td, nullptr, texture.ReleaseAndGetAddressOf())) &&
        SUCCEEDED(device->CreateRenderTargetView(texture.Get(), nullptr, rtv.ReleaseAndGetAddressOf())) &&
        SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, srv.ReleaseAndGetAddressOf()));
}

} // namespace

Backdrop::Backdrop() = default;
Backdrop::~Backdrop() = default;

bool Backdrop::Init(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (ready_) {
        return true;
    }
    if (device == nullptr) {
        return false;
    }
    device_ = device;
    ComPtr<ID3DBlob> vs, capture, project, blur, grade, clear;
    if (!Compile("VSMain", "vs_5_0", vs) || !Compile("PSCapture", "ps_5_0", capture) || !Compile("PSProject", "ps_5_0", project) ||
        !Compile("PSBlur", "ps_5_0", blur) || !Compile("PSGrade", "ps_5_0", grade) || !Compile("PSClear", "ps_5_0", clear)) {
        return false;
    }
    auto ps = [&](ComPtr<ID3DBlob> const& blob, ComPtr<ID3D11PixelShader>& out) {
        return SUCCEEDED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, out.GetAddressOf()));
    };
    if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, vs_.GetAddressOf())) ||
        !ps(capture, capturePs_) || !ps(project, projectPs_) || !ps(blur, blurPs_) || !ps(grade, gradePs_) || !ps(clear, clearPs_)) {
        return false;
    }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(Params);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateBuffer(&bd, nullptr, constants_.GetAddressOf())) ||
        FAILED(device->CreateSamplerState(&sd, sampler_.GetAddressOf()))) {
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        if (!Target(device, kBackdropWidth, kBackdropHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, work_[i], workRtv_[i], workSrv_[i])) {
            return false;
        }
    }
    if (!Target(device, kBackdropWidth, kBackdropHeight, DXGI_FORMAT_R8G8B8A8_UNORM, final_, finalRtv_, finalSrv_)) {
        return false;
    }
    ready_ = true;
    Clear(context);
    return true;
}

ID3D11ShaderResourceView* Backdrop::View() const {
    return finalSrv_.Get();
}

namespace {

void Pass(ID3D11DeviceContext* c, ID3D11VertexShader* vs, ID3D11PixelShader* ps, ID3D11Buffer* constants, Params const& params,
    ID3D11SamplerState* sampler, ID3D11ShaderResourceView* input, ID3D11RenderTargetView* output, unsigned width, unsigned height) {
    c->UpdateSubresource(constants, 0, nullptr, &params, 0, 0);
    ID3D11ShaderResourceView* const none = nullptr;
    c->PSSetShaderResources(0, 1, &none);
    c->OMSetRenderTargets(1, &output, nullptr);
    D3D11_VIEWPORT const viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    c->RSSetViewports(1, &viewport);
    c->IASetInputLayout(nullptr);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->VSSetShader(vs, nullptr, 0);
    c->PSSetShader(ps, nullptr, 0);
    c->PSSetConstantBuffers(0, 1, &constants);
    c->PSSetSamplers(0, 1, &sampler);
    c->PSSetShaderResources(0, 1, &input);
    c->Draw(3, 0);
}

void Fixed(ID3D11DeviceContext* c) {
    c->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
    c->OMSetDepthStencilState(nullptr, 0);
    c->RSSetState(nullptr);
}

} // namespace

bool Backdrop::Capture(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source, int const rect[4], unsigned width,
    unsigned height, bool decodes) {
    if (!ready_ || context == nullptr || source == nullptr || rect[2] <= 0 || rect[3] <= 0 || width == 0 || height == 0) {
        return false;
    }
    unsigned const longest = static_cast<unsigned>(std::max(rect[2], rect[3]));
    unsigned const factor = std::max(1u, (longest + kCaptureMax - 1) / kCaptureMax);
    unsigned const w = std::max(16u, static_cast<unsigned>(rect[2]) / factor);
    unsigned const h = std::max(16u, static_cast<unsigned>(rect[3]) / factor);
    if (capture_ == nullptr || w != captureW_ || h != captureH_) {
        if (!Target(device_.Get(), w, h, DXGI_FORMAT_R8G8B8A8_UNORM, capture_, captureRtv_, captureSrv_)) {
            capture_.Reset();
            return false;
        }
        captureW_ = w;
        captureH_ = h;
    }
    Params p{};
    p.rect[0] = static_cast<float>(rect[0]) / static_cast<float>(width);
    p.rect[1] = static_cast<float>(rect[1]) / static_cast<float>(height);
    p.rect[2] = static_cast<float>(rect[2]) / static_cast<float>(width);
    p.rect[3] = static_cast<float>(rect[3]) / static_cast<float>(height);
    p.misc[0] = decodes ? 1.0f : 0.0f;
    p.misc[1] = p.rect[2] / static_cast<float>(w);
    p.misc[2] = p.rect[3] / static_cast<float>(h);
    StateGuard const guard(context);
    Fixed(context);
    Pass(context, vs_.Get(), capturePs_.Get(), constants_.Get(), p, sampler_.Get(), source, captureRtv_.Get(), w, h);
    captured_ = true;
    return true;
}

bool Backdrop::Compose(ID3D11DeviceContext* context, BackdropView const& view) {
    if (!ready_ || !captured_ || context == nullptr) {
        return false;
    }
    StateGuard const guard(context);
    Fixed(context);
    Params p{};
    std::memcpy(p.center, view.center, sizeof view.center);
    std::memcpy(p.right, view.right, sizeof view.right);
    std::memcpy(p.up, view.up, sizeof view.up);
    p.tans[0] = view.tanLeft;
    p.tans[1] = view.tanRight;
    p.tans[2] = view.tanUp;
    p.tans[3] = view.tanDown;
    Pass(context, vs_.Get(), projectPs_.Get(), constants_.Get(), p, sampler_.Get(), captureSrv_.Get(), workRtv_[0].Get(),
        kBackdropWidth, kBackdropHeight);
    // Two rounds of horizontal and vertical blur, taps two texels apart: a Gaussian of about 8 backdrop pixels
    // (24 of the menu's), the mockup's blur(22px).
    float const du = 2.0f / static_cast<float>(kBackdropWidth);
    float const dv = 2.0f / static_cast<float>(kBackdropHeight);
    for (int round = 0; round < 2; ++round) {
        Params b{};
        b.rect[0] = du;
        Pass(context, vs_.Get(), blurPs_.Get(), constants_.Get(), b, sampler_.Get(), workSrv_[0].Get(), workRtv_[1].Get(),
            kBackdropWidth, kBackdropHeight);
        b.rect[0] = 0.0f;
        b.rect[1] = dv;
        Pass(context, vs_.Get(), blurPs_.Get(), constants_.Get(), b, sampler_.Get(), workSrv_[1].Get(), workRtv_[0].Get(),
            kBackdropWidth, kBackdropHeight);
    }
    Pass(context, vs_.Get(), gradePs_.Get(), constants_.Get(), Params{}, sampler_.Get(), workSrv_[0].Get(), finalRtv_.Get(),
        kBackdropWidth, kBackdropHeight);
    return true;
}

void Backdrop::Clear(ID3D11DeviceContext* context) {
    if (!ready_ || context == nullptr) {
        return;
    }
    StateGuard const guard(context);
    Fixed(context);
    Pass(context, vs_.Get(), clearPs_.Get(), constants_.Get(), Params{}, sampler_.Get(), nullptr, finalRtv_.Get(), kBackdropWidth,
        kBackdropHeight);
}

} // namespace mevr::menu
