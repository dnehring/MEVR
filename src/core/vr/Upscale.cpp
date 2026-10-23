// SPDX-License-Identifier: MIT
#include "core/vr/Upscale.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace mevr::vr::upscale {
namespace {

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

// IEEE half of a float (round to nearest even), for RCAS's packed constant (its 16-bit path reads it).
std::uint32_t Half(float f) {
    std::uint32_t const u = Bits(f);
    std::uint32_t const sign = (u >> 16) & 0x8000u;
    std::int32_t exponent = static_cast<std::int32_t>((u >> 23) & 0xFFu) - 127 + 15;
    std::uint32_t mantissa = u & 0x7FFFFFu;
    if (((u >> 23) & 0xFFu) == 0xFFu) {
        return sign | 0x7C00u | (mantissa != 0 ? 0x200u : 0u);  // inf / nan
    }
    if (exponent >= 0x1F) {
        return sign | 0x7C00u;  // overflow
    }
    if (exponent <= 0) {
        if (exponent < -10) {
            return sign;  // underflow to zero
        }
        mantissa |= 0x800000u;
        std::uint32_t const shift = static_cast<std::uint32_t>(14 - exponent);
        std::uint32_t half = mantissa >> shift;
        std::uint32_t const rest = mantissa & ((1u << shift) - 1u);
        std::uint32_t const halfway = 1u << (shift - 1);
        if (rest > halfway || (rest == halfway && (half & 1u) != 0)) {
            ++half;
        }
        return sign | half;
    }
    std::uint32_t half = sign | (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13);
    std::uint32_t const rest = mantissa & 0x1FFFu;
    if (rest > 0x1000u || (rest == 0x1000u && (half & 1u) != 0)) {
        ++half;  // rounds into the exponent correctly
    }
    return half;
}

bool Equal(std::string_view a, char const* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return i == a.size() && b[i] == '\0';
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) s.remove_suffix(1);
    return s;
}

// Our part of the shader, after the FSR headers.
constexpr char kEntryPoints[] = R"(
#ifndef MEVR_EYE_OUT
#define MEVR_EYE_OUT(c, pos) (c)
#endif
Texture2D t0 : register(t0);
SamplerState s0 : register(s0);
cbuffer FillParams : register(b0) { float4 fillUv; float4 imageRect; float4 colorScale; };
cbuffer MevrUpscale : register(b2) { uint4 mevrCon0; uint4 mevrCon1; uint4 mevrCon2; uint4 mevrCon3; uint4 mevrRcas; float4 mevrSizes; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
  float2 p = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);
  VSOut o; o.pos = float4(p, 0.0, 1.0); o.uv = float2((p.x + 1.0) * 0.5, 1.0 - ((p.y + 1.0) * 0.5)); return o;
}
float4 ToEye(float4 c) { c.rgb *= colorScale.x; c.rgb /= max(1.0, max(c.r, max(c.g, c.b))); return c; }
AF4 FsrEasuRF(AF2 p) { return t0.GatherRed(s0, p); }
AF4 FsrEasuGF(AF2 p) { return t0.GatherGreen(s0, p); }
AF4 FsrEasuBF(AF2 p) { return t0.GatherBlue(s0, p); }
float4 PSEasu(VSOut i) : SV_Target {
  float2 local = (i.uv - imageRect.xy) / imageRect.zw;
  if (local.x < 0.0 || local.x > 1.0 || local.y < 0.0 || local.y > 1.0) return float4(0.0, 0.0, 0.0, 1.0);
  AF3 c;
  FsrEasuF(c, AU2(i.pos.xy), mevrCon0, mevrCon1, mevrCon2, mevrCon3);
  return float4(c, 1.0);
}
AF4 FsrRcasLoadF(ASU2 p) { return t0.Load(int3(p, 0)); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
float4 PSRcas(VSOut i) : SV_Target {
  AF3 c;
  FsrRcasF(c.r, c.g, c.b, AU2(i.pos.xy), mevrRcas);
  return MEVR_EYE_OUT(ToEye(float4(c, 1.0)), i.pos);
}
)";

} // namespace

Settings Parse(std::string_view method, double sharpness) {
    Settings s;
    std::string_view const m = Trim(method);
    if (Equal(m, "fsr") || Equal(m, "fsr1") || Equal(m, "easu") || Equal(m, "on") || Equal(m, "1")) {
        s.method = Method::Fsr;
    }
    if (std::isfinite(sharpness)) {
        s.sharpness = static_cast<float>(std::clamp(sharpness, 0.0, 1.0));
    }
    return s;
}

char const* Name(Method m) {
    return m == Method::Fsr ? "fsr" : "off";
}

float RcasStops(float sharpness) {
    if (!std::isfinite(sharpness)) {
        sharpness = 0.9f;
    }
    return (1.0f - std::clamp(sharpness, 0.0f, 1.0f)) * 2.0f;
}

Constants MakeConstants(unsigned srcW, unsigned srcH, unsigned dstW, unsigned dstH, float const fillUv[4],
    float const imageRect[4], float sharpness) {
    Constants k{};
    float const inW = static_cast<float>(srcW == 0 ? 1u : srcW);
    float const inH = static_cast<float>(srcH == 0 ? 1u : srcH);
    float const outW = static_cast<float>(dstW == 0 ? 1u : dstW);
    float const outH = static_cast<float>(dstH == 0 ? 1u : dstH);
    float const rectW = imageRect[2] != 0.0f ? imageRect[2] : 1.0f;
    float const rectH = imageRect[3] != 0.0f ? imageRect[3] : 1.0f;
    // target pixel ip -> uv (ip + 0.5) / out -> local (uv - rect.xy) / rect.wh -> source uv fill.xy + local * fill.wh
    // -> source pixel uv * in - 0.5: an affine map ip * scale + offset (FsrEasuCon's con0 for the plain case).
    float const scaleX = fillUv[2] * inW / (outW * rectW);
    float const scaleY = fillUv[3] * inH / (outH * rectH);
    float const offsetX = ((0.5f / outW - imageRect[0]) / rectW * fillUv[2] + fillUv[0]) * inW - 0.5f;
    float const offsetY = ((0.5f / outH - imageRect[1]) / rectH * fillUv[3] + fillUv[1]) * inH - 0.5f;
    k.con0[0] = Bits(scaleX);
    k.con0[1] = Bits(scaleY);
    k.con0[2] = Bits(offsetX);
    k.con0[3] = Bits(offsetY);
    // FsrEasuCon (ffx_fsr1.h): the texel size and the gather centres, from the source's size.
    k.con1[0] = Bits(1.0f / inW);
    k.con1[1] = Bits(1.0f / inH);
    k.con1[2] = Bits(1.0f / inW);
    k.con1[3] = Bits(-1.0f / inH);
    k.con2[0] = Bits(-1.0f / inW);
    k.con2[1] = Bits(2.0f / inH);
    k.con2[2] = Bits(1.0f / inW);
    k.con2[3] = Bits(2.0f / inH);
    k.con3[0] = Bits(0.0f);
    k.con3[1] = Bits(4.0f / inH);
    k.con3[2] = 0;
    k.con3[3] = 0;
    // FsrRcasCon: stops -> linear, once as a float and once as two halves.
    float const sharp = std::exp2(-RcasStops(sharpness));
    k.rcas[0] = Bits(sharp);
    k.rcas[1] = Half(sharp) | (Half(sharp) << 16);
    k.rcas[2] = 0;
    k.rcas[3] = 0;
    k.sizes[0] = inW;
    k.sizes[1] = inH;
    k.sizes[2] = outW;
    k.sizes[3] = outH;
    return k;
}

std::string ShaderSource(std::string_view ffxA, std::string_view ffxFsr1) {
    std::string source;
    source.reserve(ffxA.size() + ffxFsr1.size() + sizeof kEntryPoints + 128);
    source += "#define A_GPU 1\n#define A_HLSL 1\n";
    source.append(ffxA.data(), ffxA.size());
    source += "\n#define FSR_EASU_F 1\n#define FSR_RCAS_F 1\n";
    source.append(ffxFsr1.data(), ffxFsr1.size());
    source += "\n";
    source += kEntryPoints;
    return source;
}

} // namespace mevr::vr::upscale
