// Pictures for the gun's sight (plugin/SightPictures.hpp): the reticles' pixels and the scope's shader.

#include "plugin/SightPictures.hpp"

#include "core/vr/OverlayPlacement.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::host::sight_pictures {

namespace sv = vr::sights;

constexpr unsigned kDotPixels = kPictureSize;
constexpr unsigned kFramePixels = kPictureSize;

// ---- The static pictures --------------------------------------------------------------------------------
namespace {
std::uint32_t Encode(float linear) {
    float const c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint32_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
}
} // namespace

float Decode(std::uint32_t byte) {
    float const c = static_cast<float>(byte & 0xFFu) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

std::uint32_t Pack(float r, float g, float b, float a) {  // linear premultiplied in, sRGB-encoded premultiplied out
    return Encode(r) | (Encode(g) << 8) | (Encode(b) << 16) | (static_cast<std::uint32_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
}

// The quad's angular size for a dot or holo reticle, and where the dot and ring sit in it ([-1, 1] units).
DotLayout LayoutFor(sv::Sight const& sight) {
    DotLayout l;
    if (static_cast<sv::Kind>(sight.kind) == sv::Kind::Holo) {
        l.quadDeg = std::max(sight.ringSizeDeg, sight.dotSizeDeg) * 1.15f;
        l.ringRadius = sight.ringSizeDeg / l.quadDeg;
        l.dotRadius = sight.dotSizeDeg / l.quadDeg;
    } else {
        l.quadDeg = sight.dotSizeDeg * 3.0f;  // room for the glow
        l.dotRadius = 1.0f / 3.0f;
    }
    return l;
}

// An emissive dot (and the holo's ring) with a soft glow, in the sight's colour.
std::vector<std::uint32_t> DotPixels(DotLayout const& layout, std::uint32_t rgb) {
    std::vector<std::uint32_t> pixels(static_cast<size_t>(kDotPixels) * kDotPixels);
    float const cr = Decode(rgb >> 16);
    float const cg = Decode(rgb >> 8);
    float const cb = Decode(rgb);
    float const px = 2.0f / static_cast<float>(kDotPixels);
    float const rd = std::max(layout.dotRadius, 1.5f * px);
    float const ringHalf = std::max(0.022f, 1.0f * px);
    for (unsigned y = 0; y < kDotPixels; ++y) {
        for (unsigned x = 0; x < kDotPixels; ++x) {
            float const u = (static_cast<float>(x) + 0.5f) * px - 1.0f;
            float const v = (static_cast<float>(y) + 0.5f) * px - 1.0f;
            float const d = std::sqrt(u * u + v * v);
            float core = std::clamp((rd - d) / px + 0.5f, 0.0f, 1.0f);
            float glow = d > rd ? 0.45f * std::exp(-(d - rd) * (d - rd) / (2.0f * (0.45f * rd) * (0.45f * rd))) : 0.0f;
            if (layout.ringRadius > 0.0f) {
                float const t = std::abs(d - layout.ringRadius);
                core = std::max(core, std::clamp((ringHalf - t) / px + 0.5f, 0.0f, 1.0f));
                glow = std::max(glow, 0.3f * std::exp(-t * t / (2.0f * ringHalf * ringHalf * 2.0f)));
            }
            float const a = std::clamp(core + glow * (1.0f - core), 0.0f, 1.0f);
            // A hot centre: the middle of the dot leans toward white.
            float const hot = d < rd ? 0.35f * core * (1.0f - d / rd) : 0.0f;
            float const r = cr + (1.0f - cr) * hot;
            float const g = cg + (1.0f - cg) * hot;
            float const b = cb + (1.0f - cb) * hot;
            pixels[static_cast<size_t>(y) * kDotPixels + x] = Pack(r * a, g * a, b * a, a);
        }
    }
    return pixels;
}

// The window's ring: a thin dark band with a lighter inner edge, half transparent.
std::vector<std::uint32_t> FramePixels() {
    std::vector<std::uint32_t> pixels(static_cast<size_t>(kFramePixels) * kFramePixels);
    float const px = 2.0f / static_cast<float>(kFramePixels);
    float const radius = 1.0f / kFrameScale;
    for (unsigned y = 0; y < kFramePixels; ++y) {
        for (unsigned x = 0; x < kFramePixels; ++x) {
            float const u = (static_cast<float>(x) + 0.5f) * px - 1.0f;
            float const v = (static_cast<float>(y) + 0.5f) * px - 1.0f;
            float const d = std::sqrt(u * u + v * v);
            float const band = std::clamp((0.06f - std::abs(d - radius - 0.04f)) / px + 0.5f, 0.0f, 1.0f);
            float const edge = std::clamp((0.012f - std::abs(d - radius + 0.005f)) / px + 0.5f, 0.0f, 1.0f);
            float const a = std::max(0.55f * band, 0.45f * edge);
            float const grey = band > edge ? 0.05f : 0.45f;
            pixels[static_cast<size_t>(y) * kFramePixels + x] = Pack(grey * a, grey * a, grey * a, a);
        }
    }
    return pixels;
}

// ---- The scope's picture: shader and state ----------------------------------------------------------------
// One full-screen triangle into the scope's swapchain image. Per pixel: its point on the lens (lens radii);
// outside the lens, transparent; outside the exit pupil (a disc about where the sight line through the eye
// meets the lens), black; inside, the direction the lens point shows (magnified about the sight line), its
// point `focus` ahead, and where that lands in the source eye's image (core/vr/Sights LensDirection,
// FocusPoint and Project: the same maths, tested there). Then the reticle, fixed on the lens and centred on
// the sight line, and the pupil's soft edge. Writes linear premultiplied colour through the sRGB view.
char const kScopeShader[] = R"(
Texture2D<float4> eyeImage : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Scope : register(b0) {
    float4 eyeAlong;    // xyz the aiming eye (app space); w its distance to the lens plane along the axis
    float4 lensCenter;  // xyz the eyepiece's centre; w its radius (m)
    float4 lensRight;   // xyz; w qx: the sight line through the eye on the lens (lens radii)
    float4 lensUp;      // xyz; w qy
    float4 lensAxis;    // xyz; w the magnification
    float4 source;      // xyz the source eye's position; w the focus distance (m)
    float4 row0;        // world -> the source eye's view (rows); w the exit pupil's radius (lens radii)
    float4 row1;        // w the reticle: 0 duplex, 1 mil-dot, 2 dot
    float4 row2;        // w the picture's size (pixels)
    float4 tanFov;      // left, right, up, down (tangents)
    float4 rect;        // the eye image's part (pixels): x, y, width, height
    float4 texel;       // 1 / texture width, 1 / texture height
    float4 lit;         // the reticle's lit centre (linear)
};
struct VSOut { float4 pos : SV_Position; };
VSOut VSMain(uint id : SV_VertexID) {
    float2 p = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);
    VSOut o; o.pos = float4(p, 0.0, 1.0); return o;
}
float Band(float t, float halfWidth, float px) { return saturate((halfWidth - abs(t)) / px + 0.5); }
float Disc(float2 p, float radius, float px) { return saturate((radius - length(p)) / px + 0.5); }
float4 PSMain(VSOut i) : SV_Target {
    float n = row2.w;
    float px = 2.0 / n;
    float2 lens = float2(i.pos.x / n * 2.0 - 1.0, 1.0 - i.pos.y / n * 2.0);
    float r = length(lens);
    float rim = saturate((1.0 - r) / px + 0.5);
    if (rim <= 0.0) return float4(0.0, 0.0, 0.0, 0.0);
    float2 d = lens - float2(lensRight.w, lensUp.w);
    float pr = length(d) / max(row0.w, 0.01);
    float3 c = float3(0.0, 0.0, 0.0);
    if (pr < 1.0) {
        float3 dir = lensAxis.xyz * eyeAlong.w + (lensRight.xyz * d.x + lensUp.xyz * d.y) * (lensCenter.w / max(lensAxis.w, 0.001));
        float3 p = eyeAlong.xyz + dir * (source.w / max(eyeAlong.w, 0.0001));
        float3 v = p - source.xyz;
        float3 e = float3(dot(row0.xyz, v), dot(row1.xyz, v), dot(row2.xyz, v));
        if (e.z < -1e-6) {
            float tx = e.x / -e.z;
            float ty = e.y / -e.z;
            float su = (tx - tanFov.x) / (tanFov.y - tanFov.x);
            float sv = (tanFov.z - ty) / (tanFov.z - tanFov.w);
            if (su >= 0.0 && su <= 1.0 && sv >= 0.0 && sv <= 1.0)
                c = eyeImage.SampleLevel(linearClamp, (rect.xy + float2(su, sv) * rect.zw) * texel.xy, 0).rgb;
        }
        float style = row1.w;
        float ink = 0.0;
        float glow = 0.0;
        if (style < 1.5) {
            float thin = max(0.004, 0.75 * px);
            float hHalf = (abs(d.x) < 0.33 || style > 0.5) ? thin : 0.02;  // duplex: thick posts outside a third
            float vHalf = (abs(d.y) < 0.33 || style > 0.5) ? thin : 0.02;
            ink = max(Band(d.y, hHalf, px), Band(d.x, vHalf, px));
            if (style > 0.5) {  // mil-dots every tenth of the radius along both lines
                float2 k = round(d / 0.1) * 0.1;
                ink = max(ink, max(Disc(float2(d.x - k.x, d.y), 0.012, px), Disc(float2(d.x, d.y - k.y), 0.012, px)));
            }
            glow = Disc(d, 0.012, px);
        } else {
            glow = Disc(d, 0.02, px);
        }
        c = lerp(c, float3(0.0, 0.0, 0.0), 0.92 * ink);
        c = lerp(c, lit.rgb, glow);
        c *= 1.0 - smoothstep(0.82, 1.0, pr);
    }
    c *= 1.0 - 0.5 * smoothstep(0.9, 1.0, r);
    return float4(c * rim, rim);
}
)";

std::size_t const kScopeShaderSize = sizeof(kScopeShader) - 1;

ScopeParams MakeScopeParams(sv::Sight const& sight, sv::Lens const& lens, sv::ScopeView const& view, XrVector3f const& eye,
    XrPosef const& source, XrFovf const& fov, XrRect2Di const& rect, unsigned textureWidth, unsigned textureHeight, unsigned size) {
    return MakeScopeParams(sight, lens, view, eye, sv::SampleViewOf(sv::EyeImage{}, source, fov), rect, textureWidth, textureHeight, size);
}

ScopeParams MakeScopeParams(sv::Sight const& sight, sv::Lens const& lens, sv::ScopeView const& view, XrVector3f const& eye,
    sv::SampleView const& sample, XrRect2Di const& rect, unsigned textureWidth, unsigned textureHeight, unsigned size) {
    XrPosef const& source = sample.pose;
    XrVector3f const r0 = vr::overlay::Rotate(source.orientation, {1.0f, 0.0f, 0.0f});
    XrVector3f const r1 = vr::overlay::Rotate(source.orientation, {0.0f, 1.0f, 0.0f});
    XrVector3f const r2 = vr::overlay::Rotate(source.orientation, {0.0f, 0.0f, 1.0f});
    float const lit[3] = {Decode(sight.scopeColor >> 16), Decode(sight.scopeColor >> 8), Decode(sight.scopeColor)};
    return ScopeParams{
        {eye.x, eye.y, eye.z, view.along},
        {lens.center.x, lens.center.y, lens.center.z, lens.radius},
        {lens.right.x, lens.right.y, lens.right.z, view.qx},
        {lens.up.x, lens.up.y, lens.up.z, view.qy},
        {lens.axis.x, lens.axis.y, lens.axis.z, std::clamp(sight.zoom, 1.0f, 12.0f)},
        {source.position.x, source.position.y, source.position.z, std::max(sight.focusM, 0.5f)},
        {r0.x, r0.y, r0.z, view.pupil},
        {r1.x, r1.y, r1.z, static_cast<float>(sight.scopeReticle)},
        {r2.x, r2.y, r2.z, static_cast<float>(size)},
        {sample.tanLeft, sample.tanRight, sample.tanUp, sample.tanDown},
        {static_cast<float>(rect.offset.x), static_cast<float>(rect.offset.y), static_cast<float>(rect.extent.width),
            static_cast<float>(rect.extent.height)},
        {1.0f / static_cast<float>(std::max(textureWidth, 1u)), 1.0f / static_cast<float>(std::max(textureHeight, 1u)), 0.0f, 0.0f},
        {lit[0], lit[1], lit[2], 1.0f},
    };
}

} // namespace mevr::host::sight_pictures
