// SPDX-License-Identifier: MIT
// Draws the galaxy hologram for one eye (plugin/GalaxyHologramDraw.hpp).

#include "plugin/GalaxyHologramDraw.hpp"

#include "core/vr/OverlayPlacement.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

#pragma warning(push, 0)
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"
#pragma warning(pop)

namespace mevr::host::galaxy_draw {

namespace gh = vr::galaxy_holo;
using Microsoft::WRL::ComPtr;

namespace {

// Styles (Vertex::extra.x).
constexpr float kStar = 0.0f;
constexpr float kHalo = 1.0f;
constexpr float kPlanet = 2.0f;
constexpr float kRelay = 3.0f;
constexpr float kBand = 4.0f;
constexpr float kGlyph = 5.0f;
constexpr float kPlate = 6.0f;
constexpr float kGalaxy = 7.0f;
constexpr float kGrid = 8.0f;

constexpr int kAtlasSize = 1024;
constexpr float kBakePixels = 56.0f;
constexpr int kFirstChar = 32;
constexpr int kCharCount = 224;  // 32..255

char const kShader[] = R"(
cbuffer Frame : register(b0) {
    float4 vp0; float4 vp1; float4 vp2; float4 vp3;
    float4 time;  // x seconds
};
Texture2D Atlas : register(t0);
SamplerState Linear : register(s0);

struct VsIn { float3 pos : POSITION; float2 uv : TEXCOORD0; float4 colour : COLOR; float4 extra : TEXCOORD1; };
struct PsIn { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 colour : COLOR; float4 extra : TEXCOORD1; };

PsIn VSMain(VsIn v) {
    PsIn o;
    float4 p = float4(v.pos, 1.0);
    o.pos = float4(dot(vp0, p), dot(vp1, p), dot(vp2, p), dot(vp3, p));
    o.uv = v.uv; o.colour = v.colour; o.extra = v.extra;
    return o;
}

float Hash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float Noise(float2 p) {      // smooth value noise
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(Hash(i), Hash(i + float2(1, 0)), f.x), lerp(Hash(i + float2(0, 1)), Hash(i + float2(1, 1)), f.x), f.y);
}
float Sparkles(float2 uv, float cells) {   // round soft points, one in some cells
    float2 p = uv * cells;
    float2 cell = floor(p);
    float h = Hash(cell);
    float2 at = cell + 0.25 + 0.5 * float2(Hash(cell + 7.1), Hash(cell + 3.3));
    float r = length(p - at);
    return step(0.93, h) * exp(-r * r * 18.0) * (h - 0.93) * 14.0;
}

float4 Additive(float3 rgb) { return float4(rgb, 0.0); }

float4 PSMain(PsIn i) : SV_Target {
    int style = (int)round(i.extra.x);
    float2 uv = i.uv;
    float d = length(uv);
    float3 c = i.colour.rgb;
    float k = i.colour.a;
    if (style == 0) {            // a star: a hot core, a soft glow, faint spikes
        float core = smoothstep(0.16, 0.05, d);
        float glow = exp(-d * d * 14.0) * 0.55 + exp(-d * 4.0) * 0.12;
        float spikes = (exp(-abs(uv.x) * 60.0) + exp(-abs(uv.y) * 60.0)) * exp(-d * 4.0) * 0.3;
        float fade = saturate((1.0 - d) * 3.0);
        float3 rgb = (lerp(c, float3(1, 1, 1), core * 0.7) * core * 1.6 + c * (glow + spikes)) * fade;
        return Additive(rgb * k);
    }
    if (style == 1) {            // a halo: a glowing ring
        float ring = exp(-pow((d - 0.62) / 0.07, 2.0)) + 0.25 * exp(-pow((d - 0.62) / 0.2, 2.0));
        return Additive(c * ring * saturate((1.0 - d) * 4.0) * k);
    }
    if (style == 2) {            // a planet: a lit sphere with a rim glow
        float edge = saturate((1.0 - d) * 40.0);
        if (edge <= 0.0) discard;
        float3 n = float3(uv.x, uv.y, sqrt(saturate(1.0 - d * d)));
        float3 l = normalize(i.extra.yzw);
        float lit = saturate(dot(n, l)) * 0.9 + 0.08;
        float bands = 0.92 + 0.08 * sin(uv.y * 22.0 + sin(uv.x * 5.0) * 1.5);
        float rim = pow(saturate(1.0 - n.z), 3.0) * 0.8;
        float a = edge * 0.92;
        float3 rgb = c * lit * bands * a + float3(0.35, 0.75, 1.0) * rim * edge;
        return float4(rgb * k, a * k);
    }
    if (style == 3) {            // a relay: a diamond outline round a glowing centre
        float dd = abs(uv.x) + abs(uv.y);
        float outline = exp(-pow((dd - 0.72) / 0.06, 2.0));
        float centre = exp(-d * d * 30.0);
        return Additive(c * (outline + centre * 1.2) * saturate((1.0 - d) * 2.0 + 0.5) * k);
    }
    if (style == 4) {            // a band: lines and rings, uv.y across
        float a = saturate(1.0 - abs(uv.y));
        a = a * a * (3.0 - 2.0 * a);
        return Additive(c * a * k);
    }
    if (style == 5) {            // a glyph
        float a = Atlas.Sample(Linear, uv).r;
        return float4(c * a * k, a * k);
    }
    if (style == 6) {            // a label's plate: a dark rounded box with a faint edge; extra.y = width / height
        float aspect = i.extra.y;
        float2 p = float2(abs(uv.x) * aspect, abs(uv.y));
        float2 q = p - float2(aspect - 0.45, 0.55);
        float sd = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - 0.45;
        float px = fwidth(sd);
        float a = saturate(0.5 - sd / px) * 0.62;
        float edge = exp(-pow(sd / (px * 1.5), 2.0)) * 0.25;
        return float4(float3(0.004, 0.012, 0.025) * a + c * edge * k, a * k);
    }
    if (style == 7) {            // the galaxy: a barred spiral, uv in its plane
        if (d > 1.0) discard;
        float r = d;
        float th = atan2(uv.y, uv.x);
        float wobble = (Noise(uv * 9.0) - 0.5) * 0.6 + (Noise(uv * 23.0) - 0.5) * 0.25;
        float spiral = th - 3.6 * log(r + 0.05) + wobble;
        float arms = pow(0.5 + 0.5 * cos(2.0 * spiral), 4.0);
        float spurs = pow(0.5 + 0.5 * cos(2.0 * (spiral + 1.1)), 8.0) * 0.45;
        float disc = exp(-r * 2.4) * smoothstep(1.0, 0.65, r);
        float2 barUv = float2(uv.x * cos(0.5) + uv.y * sin(0.5), -uv.x * sin(0.5) + uv.y * cos(0.5));
        float bar = exp(-barUv.x * barUv.x * 70.0 - barUv.y * barUv.y * 10.0);
        float core = exp(-r * r * 60.0) * 1.8 + bar * 0.7;
        float dust = 0.55 + 0.9 * Noise(uv * 14.0 + 3.0) * Noise(uv * 37.0);
        float lanes = 1.0 - 0.55 * pow(0.5 + 0.5 * cos(2.0 * (spiral - 0.35)), 10.0) * smoothstep(0.15, 0.4, r);
        float sparkle = (Sparkles(uv, 150.0) + Sparkles(uv + 0.37, 310.0) * 0.6) * (0.25 + arms) * smoothstep(1.0, 0.55, r);
        float3 armColour = float3(0.50, 0.70, 1.0);
        float3 youngColour = float3(0.75, 0.55, 1.0);
        float3 coreColour = float3(1.0, 0.80, 0.55);
        float3 rgb = (armColour * arms * 1.5 + youngColour * spurs) * disc * dust * lanes + coreColour * core
                   + float3(0.85, 0.92, 1.0) * sparkle + armColour * disc * 0.10;
        return Additive(rgb * c * k);
    }
    if (style == 8) {            // the table's grid: rings and spokes, fading out
        if (d > 1.0) discard;
        float ringCoord = frac(d * 5.0 + 0.5) - 0.5;
        float rings = exp(-pow(ringCoord / max(fwidth(d * 5.0) * 0.9, 0.006), 2.0));
        float th = atan2(uv.y, uv.x) / 6.2831853 * 12.0;
        float spokeCoord = frac(th + 0.5) - 0.5;
        float spokes = exp(-pow(spokeCoord / max(fwidth(th) * 0.9, 0.002), 2.0)) * smoothstep(0.1, 0.3, d);
        float fade = smoothstep(1.0, 0.8, d);
        return Additive(c * (rings * 0.6 + spokes * 0.35 + 0.08) * fade * k);
    }
    return float4(0, 0, 0, 0);
}
)";

struct FrameConstants {
    float vp[4][4];
    float time[4];
};

XrVector3f RotateBy(XrQuaternionf const& q, XrVector3f const& v) { return vr::overlay::Rotate(q, v); }

void ColourOf(gh::Colour const& c, float out[4]) {
    out[0] = c.r;
    out[1] = c.g;
    out[2] = c.b;
    out[3] = c.a;
}

// One UTF-8 code point (Latin-1 is all the font has); '?' for the rest.
int NextCodePoint(std::string const& s, size_t& i) {
    unsigned char const c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        ++i;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
        int const cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        i += 2;
        return cp;
    }
    size_t len = (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
    i += len;
    return '?';
}

} // namespace

bool Renderer::Init(ID3D11Device* device, std::filesystem::path const& fontFile, std::string* error) {
    ready_ = false;
    if (device == nullptr) {
        if (error) *error = "no device";
        return false;
    }
    ComPtr<ID3DBlob> vsBlob, psBlob, errors;
    UINT const flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if (FAILED(D3DCompile(kShader, sizeof(kShader) - 1, "galaxy-hologram", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0,
                          vsBlob.GetAddressOf(), errors.ReleaseAndGetAddressOf()))) {
        if (error) *error = errors ? std::string(static_cast<char const*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "VS";
        return false;
    }
    if (FAILED(D3DCompile(kShader, sizeof(kShader) - 1, "galaxy-hologram", nullptr, nullptr, "PSMain", "ps_5_0", flags, 0,
                          psBlob.GetAddressOf(), errors.ReleaseAndGetAddressOf()))) {
        if (error) *error = errors ? std::string(static_cast<char const*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "PS";
        return false;
    }
    if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, vs_.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, ps_.ReleaseAndGetAddressOf()))) {
        if (error) *error = "CreateShader failed";
        return false;
    }
    D3D11_INPUT_ELEMENT_DESC const elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, colour), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, extra), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device->CreateInputLayout(elements, 4, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), layout_.ReleaseAndGetAddressOf()))) {
        if (error) *error = "CreateInputLayout failed";
        return false;
    }
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(FrameConstants);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&cb, nullptr, constants_.ReleaseAndGetAddressOf()))) {
        if (error) *error = "constant buffer";
        return false;
    }
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = TRUE;
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    sd.MipLODBias = -0.5f;
    if (FAILED(device->CreateBlendState(&bd, blend_.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateRasterizerState(&rd, raster_.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateDepthStencilState(&dd, depth_.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateSamplerState(&sd, sampler_.ReleaseAndGetAddressOf()))) {
        if (error) *error = "render states";
        return false;
    }
    std::string fontError;
    fontReady_ = !fontFile.empty() && BakeFont(device, fontFile, &fontError);
    if (!fontReady_ && error != nullptr) {
        *error = fontFile.empty() ? "no font: labels skipped" : "font: " + fontError + ": labels skipped";
    }
    ready_ = true;
    return true;
}

bool Renderer::BakeFont(ID3D11Device* device, std::filesystem::path const& fontFile, std::string* error) {
    std::ifstream in(fontFile, std::ios::binary);
    if (!in) {
        if (error) *error = "can't open the font file";
        return false;
    }
    std::vector<unsigned char> const ttf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<unsigned char> pixels(static_cast<size_t>(kAtlasSize) * kAtlasSize);
    std::vector<stbtt_packedchar> packed(kCharCount);
    stbtt_pack_context pack{};
    if (!stbtt_PackBegin(&pack, pixels.data(), kAtlasSize, kAtlasSize, 0, 2, nullptr)) {
        if (error) *error = "stbtt_PackBegin";
        return false;
    }
    stbtt_PackSetOversampling(&pack, 2, 2);
    bool const ok = stbtt_PackFontRange(&pack, ttf.data(), 0, kBakePixels, kFirstChar, kCharCount, packed.data()) != 0;
    stbtt_PackEnd(&pack);
    if (!ok) {
        if (error) *error = "the glyphs don't fit the atlas";
        return false;
    }
    stbtt_fontinfo info{};
    if (!stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0))) {
        if (error) *error = "stbtt_InitFont";
        return false;
    }
    int ascent = 0, descent = 0, gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
    float const scale = stbtt_ScaleForPixelHeight(&info, kBakePixels);
    fontAscent_ = static_cast<float>(ascent) * scale;
    fontPixels_ = static_cast<float>(ascent - descent) * scale;
    glyphs_.assign(kCharCount, Glyph{});
    for (int i = 0; i < kCharCount; ++i) {
        float x = 0.0f, y = 0.0f;
        stbtt_aligned_quad q{};
        stbtt_GetPackedQuad(packed.data(), kAtlasSize, kAtlasSize, i, &x, &y, &q, 0);
        glyphs_[static_cast<size_t>(i)] = {q.x0, q.y0, q.x1, q.y1, q.s0, q.t0, q.s1, q.t1, packed[static_cast<size_t>(i)].xadvance};
    }
    // The atlas with a box-filtered mip chain, so small labels stay smooth.
    std::vector<std::vector<unsigned char>> levels;
    levels.push_back(pixels);
    int size = kAtlasSize;
    while (size > 8) {
        int const half = size / 2;
        std::vector<unsigned char> next(static_cast<size_t>(half) * half);
        std::vector<unsigned char> const& prev = levels.back();
        for (int y = 0; y < half; ++y) {
            for (int x = 0; x < half; ++x) {
                int const s = prev[static_cast<size_t>(2 * y) * size + 2 * x] + prev[static_cast<size_t>(2 * y) * size + 2 * x + 1] +
                              prev[static_cast<size_t>(2 * y + 1) * size + 2 * x] + prev[static_cast<size_t>(2 * y + 1) * size + 2 * x + 1];
                next[static_cast<size_t>(y) * half + x] = static_cast<unsigned char>((s + 2) / 4);
            }
        }
        levels.push_back(std::move(next));
        size = half;
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = kAtlasSize;
    td.MipLevels = static_cast<UINT>(levels.size());
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> data(levels.size());
    for (size_t l = 0; l < levels.size(); ++l) {
        data[l].pSysMem = levels[l].data();
        data[l].SysMemPitch = static_cast<UINT>(kAtlasSize >> l);
    }
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device->CreateTexture2D(&td, data.data(), texture.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, atlas_.ReleaseAndGetAddressOf()))) {
        if (error) *error = "atlas texture";
        return false;
    }
    return true;
}

void Renderer::AddQuad(XrVector3f const& c, XrVector3f const& right, XrVector3f const& up, float halfW, float halfH, float const colour[4],
                       float style, float e1, float e2, float e3) {
    XrVector3f const r = gh::Mul(right, halfW);
    XrVector3f const u = gh::Mul(up, halfH);
    XrVector3f const corners[4] = {gh::Sub(gh::Sub(c, r), u), gh::Sub(gh::Add(c, r), u), gh::Add(gh::Add(c, r), u), gh::Add(gh::Sub(c, r), u)};
    float const uvs[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    int const order[6] = {0, 1, 2, 0, 2, 3};
    for (int i : order) {
        Vertex v{};
        v.position[0] = corners[i].x;
        v.position[1] = corners[i].y;
        v.position[2] = corners[i].z;
        v.uv[0] = uvs[i][0];
        v.uv[1] = uvs[i][1];
        std::memcpy(v.colour, colour, sizeof(v.colour));
        v.extra[0] = style;
        v.extra[1] = e1;
        v.extra[2] = e2;
        v.extra[3] = e3;
        vertices_.push_back(v);
    }
}

void Renderer::AddRibbon(std::vector<XrVector3f> const& points, bool closed, float widthM, float const colour[4], XrVector3f const& eye) {
    size_t const n = points.size();
    if (n < 2) {
        return;
    }
    size_t const segments = closed ? n : n - 1;
    float const half = widthM * 0.5f;
    for (size_t s = 0; s < segments; ++s) {
        XrVector3f const a = points[s];
        XrVector3f const b = points[(s + 1) % n];
        XrVector3f const along = gh::Normalize(gh::Sub(b, a));
        XrVector3f const mid = gh::Mul(gh::Add(a, b), 0.5f);
        XrVector3f side = gh::Normalize(gh::Cross(along, gh::Sub(eye, mid)));
        if (gh::Length(side) < 0.5f) {
            continue;
        }
        side = gh::Mul(side, half);
        XrVector3f const corners[4] = {gh::Sub(a, side), gh::Sub(b, side), gh::Add(b, side), gh::Add(a, side)};
        float const vs[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        int const order[6] = {0, 1, 2, 0, 2, 3};
        for (int i : order) {
            Vertex v{};
            v.position[0] = corners[i].x;
            v.position[1] = corners[i].y;
            v.position[2] = corners[i].z;
            v.uv[0] = 0.0f;
            v.uv[1] = vs[i];
            std::memcpy(v.colour, colour, sizeof(v.colour));
            v.extra[0] = kBand;
            vertices_.push_back(v);
        }
    }
}

float Renderer::TextWidth(std::string const& text) const {
    float w = 0.0f;
    for (size_t i = 0; i < text.size();) {
        int cp = NextCodePoint(text, i);
        if (cp < kFirstChar || cp >= kFirstChar + kCharCount) {
            cp = '?';
        }
        w += glyphs_[static_cast<size_t>(cp - kFirstChar)].advance;
    }
    return w;
}

unsigned Renderer::AddText(std::string const& text, XrVector3f const& anchor, XrVector3f const& right, XrVector3f const& up, float heightM,
                           float const colour[4]) {
    if (!fontReady_ || text.empty()) {
        return 0;
    }
    float const s = heightM / fontPixels_;  // metres per font pixel
    float const width = TextWidth(text) * s;
    // The pen starts at the left end of the baseline; the anchor is the text's bottom centre (the descent below it).
    float const descent = (fontPixels_ - fontAscent_) * s;
    XrVector3f const baseline = gh::Add(gh::Sub(anchor, gh::Mul(right, width * 0.5f)), gh::Mul(up, descent));
    float pen = 0.0f;
    unsigned count = 0;
    for (size_t i = 0; i < text.size();) {
        int cp = NextCodePoint(text, i);
        if (cp < kFirstChar || cp >= kFirstChar + kCharCount) {
            cp = '?';
        }
        Glyph const& g = glyphs_[static_cast<size_t>(cp - kFirstChar)];
        if (g.x1 > g.x0 && g.y1 > g.y0) {
            // Font pixels: x right, y down from the baseline.
            float const cx = (pen + (g.x0 + g.x1) * 0.5f) * s;
            float const cy = -((g.y0 + g.y1) * 0.5f) * s;
            float const hw = (g.x1 - g.x0) * 0.5f * s;
            float const hh = (g.y1 - g.y0) * 0.5f * s;
            XrVector3f const c = gh::Add(gh::Add(baseline, gh::Mul(right, cx)), gh::Mul(up, cy));
            size_t const first = vertices_.size();
            AddQuad(c, right, up, hw, hh, colour, kGlyph);
            // The quad's uv runs -1..1; a glyph samples the atlas instead (v down).
            for (size_t v = first; v < vertices_.size(); ++v) {
                float const qu = vertices_[v].uv[0];
                float const qv = vertices_[v].uv[1];
                vertices_[v].uv[0] = qu < 0.0f ? g.u0 : g.u1;
                vertices_[v].uv[1] = qv < 0.0f ? g.v1 : g.v0;
            }
            ++count;
        }
        pen += g.advance;
    }
    return count;
}

Stats Renderer::Prepare(gh::DrawList const& list, XrPosef const& head) {
    vertices_.clear();
    Stats stats;
    XrVector3f const eye = head.position;
    XrVector3f const headRight = RotateBy(head.orientation, {1.0f, 0.0f, 0.0f});
    XrVector3f const headUp = RotateBy(head.orientation, {0.0f, 1.0f, 0.0f});
    float colour[4];

    // Discs lie in their plane.
    for (gh::Disc const& d : list.discs) {
        XrVector3f const n = gh::Normalize(d.normal);
        XrVector3f fwd = gh::Normalize(gh::Sub(d.forward, gh::Mul(n, gh::Dot(d.forward, n))));
        XrVector3f const rgt = gh::Cross(fwd, n);
        ColourOf(d.colour, colour);
        AddQuad(d.centre, rgt, fwd, d.radiusM, d.radiusM, colour, d.style == gh::DiscStyle::Galaxy ? kGalaxy : kGrid);
    }
    // Rings and lines as ribbons facing the head.
    for (gh::Ring const& r : list.rings) {
        XrVector3f const n = gh::Normalize(r.normal);
        XrVector3f a = gh::Cross(n, {1.0f, 0.0f, 0.0f});
        if (gh::Length(a) < 0.3f) {
            a = gh::Cross(n, {0.0f, 0.0f, 1.0f});
        }
        a = gh::Normalize(a);
        XrVector3f const b = gh::Cross(n, a);
        int const segments = std::clamp(static_cast<int>(r.radiusM / 0.004f), 24, 128);
        std::vector<XrVector3f> points;
        points.reserve(static_cast<size_t>(segments));
        for (int i = 0; i < segments; ++i) {
            float const t = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments);
            points.push_back(gh::Add(r.centre, gh::Add(gh::Mul(a, std::cos(t) * r.radiusM), gh::Mul(b, std::sin(t) * r.radiusM))));
        }
        ColourOf(r.colour, colour);
        AddRibbon(points, true, r.widthM, colour, eye);
    }
    for (gh::Line const& l : list.lines) {
        ColourOf(l.colour, colour);
        AddRibbon({l.a, l.b}, false, l.widthM, colour, eye);
    }
    // Sprites face the head (the same shape for both eyes).
    for (gh::Sprite const& s : list.sprites) {
        ColourOf(s.colour, colour);
        float style = kStar;
        float e1 = 0.0f, e2 = 0.0f, e3 = 0.0f;
        float scale = 1.0f;
        switch (s.style) {
        case gh::SpriteStyle::Star: style = kStar; scale = 3.0f; break;  // the glow reaches well past the body
        case gh::SpriteStyle::Halo: style = kHalo; break;
        case gh::SpriteStyle::Relay: style = kRelay; scale = 1.6f; break;
        case gh::SpriteStyle::Planet: {
            style = kPlanet;
            XrVector3f const toHead = gh::Normalize(gh::Sub(eye, s.centre));
            e1 = gh::Dot(s.light, headRight);
            e2 = gh::Dot(s.light, headUp);
            e3 = gh::Dot(s.light, toHead);
            break;
        }
        }
        AddQuad(s.centre, headRight, headUp, s.radiusM * scale, s.radiusM * scale, colour, style, e1, e2, e3);
    }
    // Labels last: a plate, then the text.
    for (gh::Label const& l : list.labels) {
        if (!fontReady_) {
            break;
        }
        float const width = TextWidth(l.text) * (l.heightM / fontPixels_);
        if (l.plate) {
            float const padX = l.heightM * 0.45f;
            float const halfW = width * 0.5f + padX;
            float const halfH = l.heightM * 0.62f;
            XrVector3f const c = gh::Add(l.anchor, gh::Mul(headUp, l.heightM * 0.5f));
            float plateColour[4] = {0.35f, 0.75f, 1.0f, 1.0f};
            AddQuad(c, headRight, headUp, halfW, halfH, plateColour, kPlate, halfW / halfH);
        }
        ColourOf(l.colour, colour);
        stats.glyphs += AddText(l.text, l.anchor, headRight, headUp, l.heightM, colour);
    }
    stats.vertices = static_cast<unsigned>(vertices_.size());
    return stats;
}

void Renderer::ViewProjection(Eye const& eye, float nearM, float farM, float rows[4][4]) {
    // View: room to eye (the eye looks down -Z).
    XrQuaternionf const q = eye.pose.orientation;
    XrQuaternionf const inv{-q.x, -q.y, -q.z, q.w};
    XrVector3f const ex = RotateBy(inv, {1.0f, 0.0f, 0.0f});
    XrVector3f const ey = RotateBy(inv, {0.0f, 1.0f, 0.0f});
    XrVector3f const ez = RotateBy(inv, {0.0f, 0.0f, 1.0f});
    // Rows of the view matrix: view = R^-1 (p - t); R^-1's columns are ex, ey, ez.
    XrVector3f const t = eye.pose.position;
    float view[4][4] = {
        {ex.x, ey.x, ez.x, 0.0f},
        {ex.y, ey.y, ez.y, 0.0f},
        {ex.z, ey.z, ez.z, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    };
    for (int r = 0; r < 3; ++r) {
        view[r][3] = -(view[r][0] * t.x + view[r][1] * t.y + view[r][2] * t.z);
    }
    float const l = std::tan(eye.fov.angleLeft), rgt = std::tan(eye.fov.angleRight);
    float const u = std::tan(eye.fov.angleUp), d = std::tan(eye.fov.angleDown);
    float const proj[4][4] = {
        {2.0f / (rgt - l), 0.0f, (rgt + l) / (rgt - l), 0.0f},
        {0.0f, 2.0f / (u - d), (u + d) / (u - d), 0.0f},
        {0.0f, 0.0f, farM / (nearM - farM), nearM * farM / (nearM - farM)},
        {0.0f, 0.0f, -1.0f, 0.0f},
    };
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) {
                s += proj[r][k] * view[k][c];
            }
            rows[r][c] = s;
        }
    }
}

void Renderer::Draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, int width, int height, Eye const& eye, float timeS) {
    if (!ready_ || context == nullptr || target == nullptr || vertices_.empty()) {
        return;
    }
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    if (vertices_.size() > vertexCapacity_) {
        unsigned const capacity = static_cast<unsigned>(vertices_.size() + vertices_.size() / 2 + 256);
        D3D11_BUFFER_DESC vb{};
        vb.ByteWidth = capacity * static_cast<unsigned>(sizeof(Vertex));
        vb.Usage = D3D11_USAGE_DYNAMIC;
        vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&vb, nullptr, vertexBuffer_.ReleaseAndGetAddressOf()))) {
            vertexCapacity_ = 0;
            return;
        }
        vertexCapacity_ = capacity;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(vertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    std::memcpy(mapped.pData, vertices_.data(), vertices_.size() * sizeof(Vertex));
    context->Unmap(vertexBuffer_.Get(), 0);

    FrameConstants fc{};
    ViewProjection(eye, 0.03f, 50.0f, fc.vp);
    fc.time[0] = timeS;
    if (FAILED(context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return;
    }
    std::memcpy(mapped.pData, &fc, sizeof(fc));
    context->Unmap(constants_.Get(), 0);

    D3D11_VIEWPORT const viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    context->OMSetRenderTargets(1, &target, nullptr);
    context->RSSetViewports(1, &viewport);
    context->RSSetState(raster_.Get());
    float const factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetBlendState(blend_.Get(), factor, 0xFFFFFFFFu);
    context->OMSetDepthStencilState(depth_.Get(), 0);
    context->IASetInputLayout(layout_.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT const stride = sizeof(Vertex);
    UINT const offset = 0;
    ID3D11Buffer* const vb = vertexBuffer_.Get();
    context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context->VSSetShader(vs_.Get(), nullptr, 0);
    ID3D11Buffer* const cbs = constants_.Get();
    context->VSSetConstantBuffers(0, 1, &cbs);
    context->PSSetShader(ps_.Get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &cbs);
    ID3D11ShaderResourceView* const srv = atlas_.Get();
    context->PSSetShaderResources(0, 1, &srv);
    ID3D11SamplerState* const smp = sampler_.Get();
    context->PSSetSamplers(0, 1, &smp);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->Draw(static_cast<UINT>(vertices_.size()), 0);
}

} // namespace mevr::host::galaxy_draw
