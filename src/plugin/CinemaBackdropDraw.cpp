// SPDX-License-Identifier: MIT
// The cinema screen's floor and horizon (plugin/CinemaBackdropDraw.hpp).

#include "plugin/CinemaBackdropDraw.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace mevr::host::cinema_backdrop_draw {
namespace {

using Microsoft::WRL::ComPtr;

// Linear light. The floor fogs into the horizon's colour so the two meet without a seam; everything is dim (the screen is the
// thing to look at) but never black, so the eyes keep a floor and a horizon (a stable reference against motion sickness).
constexpr char kShader[] = R"(
cbuffer Frame : register(b0) {
    float4 eyePos;       // xyz
    float4 axisRight;    // the eye's axes in the room
    float4 axisUp;
    float4 axisBack;
    float4 tanFov;       // tan of left, right, up, down
    float4 floorInfo;    // x floor height, y brightness x fade, z time, w floor tile (m)
    float4 screenInfo;   // x, y: the screen's foot (room x, z); z the screen's width; w its yaw
    float4 flags;        // x: a floor texture, y: a sky texture
};
Texture2D floorTex : register(t0);
Texture2D skyTex : register(t1);
SamplerState samp : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

static const float PI = 3.14159265;
static const float3 kZenith  = float3(0.0025, 0.0035, 0.0080);
static const float3 kHorizon = float3(0.0160, 0.0240, 0.0400);
static const float3 kDeck    = float3(0.0080, 0.0100, 0.0130);
static const float3 kGrid    = float3(0.0180, 0.0260, 0.0360);
static const float3 kPool    = float3(0.0200, 0.0220, 0.0280);

float Hash(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float3 Sky(float3 d) {
    float e = d.y;
    if (flags.y > 0.5) {
        float2 uv = float2(atan2(d.x, -d.z) / (2.0 * PI) + 0.5, 0.5 - asin(clamp(e, -1.0, 1.0)) / PI);
        return skyTex.SampleLevel(samp, uv, 0).rgb * 0.25 + kHorizon * saturate(1.0 - abs(e) * 8.0) * 0.5;
    }
    float3 c = lerp(kHorizon, kZenith, saturate(e * 2.5));
    // A sparse starfield in cells of about 2 x 2 degrees; each star a soft dot of about 0.2 degrees, fading toward the horizon.
    float az = atan2(d.x, -d.z);
    float el = asin(clamp(e, -1.0, 1.0));
    float2 g = float2(az * (180.0 / (2.0 * PI)), el * (90.0 / PI));
    float2 cell = floor(g);
    float2 f = frac(g) - 0.5;
    float h = Hash(cell);
    float2 offset = (float2(Hash(cell + 7.1), Hash(cell + 3.7)) - 0.5) * 0.6;
    float star = step(0.975, h) * smoothstep(0.12, 0.0, length(f - offset));
    c += star * (0.06 + 0.20 * Hash(cell + 1.9)) * saturate(e * 5.0) * float3(0.85, 0.92, 1.0);
    return c;
}

float3 Floor(float3 d, float3 eye) {
    float t = (floorInfo.x - eye.y) / d.y;
    float3 hit = eye + t * d;
    float2 rel = hit.xz - screenInfo.xy;
    float r = length(hit.xz - eye.xz);
    float3 c = kDeck;
    if (flags.x > 0.5) {
        float cs = cos(screenInfo.w), sn = sin(screenInfo.w);
        float2 local = float2(cs * rel.x - sn * rel.y, sn * rel.x + cs * rel.y);
        c = floorTex.Sample(samp, local / max(floorInfo.w, 0.1)).rgb * 0.12;
    } else {
        // A faint metre grid, anti-aliased, fading with distance and at grazing angles.
        float2 q = hit.xz;
        float2 w = max(fwidth(q), 1e-4);
        float2 gl = abs(frac(q - 0.5) - 0.5) / w;
        float lineW = 1.0 - saturate(min(gl.x, gl.y) - 0.5);
        float fade = saturate(1.0 - r / 16.0) * saturate(1.0 - max(w.x, w.y) * 1.5);
        c += lineW * fade * kGrid;
    }
    // The screen's light on the floor at its foot.
    float sigma = max(screenInfo.z * 0.45, 0.5);
    c += kPool * exp(-dot(rel, rel) / (2.0 * sigma * sigma));
    // Fog into the horizon.
    float fog = 1.0 - exp(-r / 18.0);
    return lerp(c, kHorizon, fog);
}

float4 PSMain(VSOut i) : SV_Target {
    float x = lerp(tanFov.x, tanFov.y, i.uv.x);
    float y = lerp(tanFov.z, tanFov.w, i.uv.y);
    float3 d = normalize(axisRight.xyz * x + axisUp.xyz * y - axisBack.xyz);
    float3 eye = eyePos.xyz;
    float3 sky = Sky(d);
    float3 c = sky;
    if (d.y < 0.0 && eye.y > floorInfo.x) {
        float3 fl = Floor(d, eye);
        float aa = max(fwidth(d.y), 1e-5);
        c = lerp(fl, sky, smoothstep(-aa * 1.5, 0.0, d.y));
    }
    return float4(c * floorInfo.y, 1.0);
}
)";

struct Constants {
    float eye[4];
    float right[4];
    float up[4];
    float back[4];
    float tanFov[4];
    float floorInfo[4];
    float screenInfo[4];
    float flags[4];
};

XrVector3f Rotate(XrQuaternionf const& q, XrVector3f const& v) {
    // v' = v + 2 w (q x v) + 2 q x (q x v)
    XrVector3f const u{q.x, q.y, q.z};
    XrVector3f const c1{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    XrVector3f const c2{u.y * c1.z - u.z * c1.y, u.z * c1.x - u.x * c1.z, u.x * c1.y - u.y * c1.x};
    return {v.x + 2.0f * (q.w * c1.x + c2.x), v.y + 2.0f * (q.w * c1.y + c2.y), v.z + 2.0f * (q.w * c1.z + c2.z)};
}

} // namespace

bool Renderer::Init(ID3D11Device* device, std::string* error) {
    ready_ = false;
    if (device == nullptr) {
        if (error) *error = "no device";
        return false;
    }
    ComPtr<ID3DBlob> vsBlob, psBlob, errors;
    UINT const flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if (FAILED(D3DCompile(kShader, sizeof(kShader) - 1, "cinema-backdrop", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0,
            vsBlob.GetAddressOf(), errors.GetAddressOf())) ||
        FAILED(D3DCompile(kShader, sizeof(kShader) - 1, "cinema-backdrop", nullptr, nullptr, "PSMain", "ps_5_0", flags, 0,
            psBlob.GetAddressOf(), errors.ReleaseAndGetAddressOf()))) {
        if (error) {
            *error = errors ? std::string(static_cast<char const*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "compile failed";
        }
        return false;
    }
    if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, vs_.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, ps_.ReleaseAndGetAddressOf()))) {
        if (error) *error = "shader creation failed";
        return false;
    }
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(Constants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_ANISOTROPIC;
    sd.MaxAnisotropy = 8;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    dd.StencilEnable = FALSE;
    if (FAILED(device->CreateBuffer(&cb, nullptr, constants_.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateSamplerState(&sd, sampler_.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateRasterizerState(&rd, raster_.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateBlendState(&bd, blend_.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateDepthStencilState(&dd, depth_.ReleaseAndGetAddressOf()))) {
        if (error) *error = "state creation failed";
        return false;
    }
    ready_ = true;
    return true;
}

void Renderer::SetTextures(ID3D11ShaderResourceView* floor, float floorTileM, ID3D11ShaderResourceView* sky) {
    floor_ = floor;
    sky_ = sky;
    floorTileM_ = floorTileM > 0.1f && std::isfinite(floorTileM) ? floorTileM : 2.0f;
}

void Renderer::Draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, int x, int y, int width, int height, Eye const& eye,
    Scene const& scene) {
    if (!ready_ || context == nullptr || target == nullptr || width <= 0 || height <= 0) {
        return;
    }
    Constants k{};
    XrVector3f const right = Rotate(eye.pose.orientation, {1.0f, 0.0f, 0.0f});
    XrVector3f const up = Rotate(eye.pose.orientation, {0.0f, 1.0f, 0.0f});
    XrVector3f const back = Rotate(eye.pose.orientation, {0.0f, 0.0f, 1.0f});
    float const level = std::isfinite(scene.brightness * scene.fade) ? std::max(0.0f, scene.brightness * scene.fade) : 0.0f;
    float const values[8][4] = {
        {eye.pose.position.x, eye.pose.position.y, eye.pose.position.z, 1.0f},
        {right.x, right.y, right.z, 0.0f},
        {up.x, up.y, up.z, 0.0f},
        {back.x, back.y, back.z, 0.0f},
        {std::tan(eye.fov.angleLeft), std::tan(eye.fov.angleRight), std::tan(eye.fov.angleUp), std::tan(eye.fov.angleDown)},
        {scene.floorY, level, scene.timeS, floorTileM_},
        {scene.screenFoot.x, scene.screenFoot.z, scene.screenWidthM, scene.screenYaw},
        {floor_ ? 1.0f : 0.0f, sky_ ? 1.0f : 0.0f, 0.0f, 0.0f},
    };
    std::memcpy(&k, values, sizeof(k));
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    std::memcpy(mapped.pData, &k, sizeof(k));
    context->Unmap(constants_.Get(), 0);

    D3D11_VIEWPORT const viewport{static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    context->OMSetRenderTargets(1, &target, nullptr);
    context->RSSetViewports(1, &viewport);
    context->RSSetState(raster_.Get());
    float const factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetBlendState(blend_.Get(), factor, 0xFFFFFFFFu);
    context->OMSetDepthStencilState(depth_.Get(), 0);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs_.Get(), nullptr, 0);
    context->PSSetShader(ps_.Get(), nullptr, 0);
    ID3D11Buffer* const cbs = constants_.Get();
    context->PSSetConstantBuffers(0, 1, &cbs);
    ID3D11ShaderResourceView* const srvs[2] = {floor_.Get(), sky_.Get()};
    context->PSSetShaderResources(0, 2, srvs);
    ID3D11SamplerState* const smp = sampler_.Get();
    context->PSSetSamplers(0, 1, &smp);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->Draw(3, 0);
    ID3D11ShaderResourceView* const none[2] = {nullptr, nullptr};
    context->PSSetShaderResources(0, 2, none);
}

} // namespace mevr::host::cinema_backdrop_draw
