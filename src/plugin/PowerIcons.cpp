// SPDX-License-Identifier: MIT
// The power wheel's icons (plugin/PowerIcons.hpp): signed distance fields painted layer over layer in linear light,
// then written as straight-alpha sRGB.

#include "plugin/PowerIcons.hpp"

#include "core/vr/PowerWheel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <string>

namespace mevr::host::power_icons {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr int kN = static_cast<int>(kPixels);
constexpr float kPx = 2.0f / static_cast<float>(kPixels);  // one pixel in the icon's unit square

struct V {
    float x = 0.0f;
    float y = 0.0f;
};
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y}; }
V operator*(V a, float k) { return {a.x * k, a.y * k}; }
float Dot(V a, V b) { return a.x * b.x + a.y * b.y; }
float Len(V a) { return std::sqrt(Dot(a, a)); }
V Rot(V a, float deg) {
    float const c = std::cos(deg * kPi / 180.0f);
    float const s = std::sin(deg * kPi / 180.0f);
    return {a.x * c - a.y * s, a.x * s + a.y * c};
}
V Polar(float deg, float r) { return {r * std::cos(deg * kPi / 180.0f), r * std::sin(deg * kPi / 180.0f)}; }
float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float Smooth(float e0, float e1, float x) {
    float const t = Clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};
float Decode(int v) {
    float const c = static_cast<float>(v) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
Rgb Srgb(int r, int g, int b) { return {Decode(r), Decode(g), Decode(b)}; }
Rgb Mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

// ---- Distances (negative inside) ------------------------------------------------------------------------------------

float Circle(V p, V c, float r) { return Len(p - c) - r; }
// A capsule from a (radius ra) to b (radius rb).
float Capsule(V p, V a, V b, float ra, float rb) {
    V const pa = p - a;
    V const ba = b - a;
    float const h = Clamp01(Dot(pa, ba) / std::max(Dot(ba, ba), 1e-9f));
    return Len(pa - ba * h) - (ra + (rb - ra) * h);
}
float SMin(float a, float b, float k) {
    float const h = Clamp01(0.5f + 0.5f * (b - a) / k);
    return b + (a - b) * h - k * h * (1.0f - h);
}
// An ellipse's outline (approximate distance), centre c, radii rx, ry, turned by deg.
float EllipseLine(V p, V c, float rx, float ry, float deg = 0.0f) {
    V const q = Rot(p - c, -deg);
    float const k = Len({q.x / rx, q.y / ry});
    return std::abs(k - 1.0f) * std::min(rx, ry);
}
// Inigo Quilez's polygon distance.
float Polygon(V p, std::span<V const> v) {
    std::size_t const n = v.size();
    float d = Dot(p - v[0], p - v[0]);
    float s = 1.0f;
    for (std::size_t i = 0, j = n - 1; i < n; j = i, ++i) {
        V const e = v[j] - v[i];
        V const w = p - v[i];
        V const b = w - e * Clamp01(Dot(w, e) / Dot(e, e));
        d = std::min(d, Dot(b, b));
        bool const c1 = p.y >= v[i].y;
        bool const c2 = p.y < v[j].y;
        bool const c3 = e.x * w.y > e.y * w.x;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3)) {
            s = -s;
        }
    }
    return s * std::sqrt(d);
}
// The distance to a polyline's segments (an open path).
float Path(V p, std::span<V const> v) {
    float d = 1e9f;
    for (std::size_t i = 1; i < v.size(); ++i) {
        d = std::min(d, Capsule(p, v[i - 1], v[i], 0.0f, 0.0f));
    }
    return d;
}

float Hash(int x, int y) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
}
float Noise(V p) {
    int const ix = static_cast<int>(std::floor(p.x));
    int const iy = static_cast<int>(std::floor(p.y));
    float const fx = p.x - static_cast<float>(ix);
    float const fy = p.y - static_cast<float>(iy);
    float const ux = fx * fx * (3.0f - 2.0f * fx);
    float const uy = fy * fy * (3.0f - 2.0f * fy);
    float const a = Hash(ix, iy);
    float const b = Hash(ix + 1, iy);
    float const c = Hash(ix, iy + 1);
    float const d = Hash(ix + 1, iy + 1);
    return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy;
}
float Fbm(V p) {
    float sum = 0.0f;
    float amplitude = 0.5f;
    for (int i = 0; i < 4; ++i) {
        sum += amplitude * Noise(p);
        p = p * 2.03f + V{17.1f, 9.7f};
        amplitude *= 0.5f;
    }
    return sum;
}

// ---- The painter ------------------------------------------------------------------------------------------------------

class Painter {
public:
    Painter() : rgb_(static_cast<std::size_t>(kN) * kN * 3, 0.0f), a_(static_cast<std::size_t>(kN) * kN, 0.0f) {}

    // One layer over what's there: `alpha(p)` (0..1) and `colour(p)` per point of the unit square (y up).
    template <class A, class C>
    void Layer(A const& alpha, C const& colour) {
        for (int y = 0; y < kN; ++y) {
            for (int x = 0; x < kN; ++x) {
                V const p{(static_cast<float>(x) + 0.5f) * kPx - 1.0f, 1.0f - (static_cast<float>(y) + 0.5f) * kPx};
                float const a = Clamp01(alpha(p));
                if (a <= 0.0f) {
                    continue;
                }
                Rgb const c = colour(p);
                std::size_t const i = static_cast<std::size_t>(y) * kN + x;
                rgb_[i * 3 + 0] = c.r * a + rgb_[i * 3 + 0] * (1.0f - a);
                rgb_[i * 3 + 1] = c.g * a + rgb_[i * 3 + 1] * (1.0f - a);
                rgb_[i * 3 + 2] = c.b * a + rgb_[i * 3 + 2] * (1.0f - a);
                a_[i] = a + a_[i] * (1.0f - a);
            }
        }
    }
    // A shape: a soft glow around it, then the shape itself.
    template <class D, class C>
    void Shape(D const& distance, C const& colour, Rgb glow, float glowRadius, float glowStrength, float fill = 1.0f) {
        if (glowStrength > 0.0f) {
            Layer([&](V p) { return glowStrength * std::exp(-std::max(distance(p), 0.0f) / glowRadius); },
                [&](V) { return glow; });
        }
        Layer([&](V p) { return fill * Clamp01(0.5f - distance(p) / kPx); }, colour);
    }
    // A line (distance to a path) with a glow.
    template <class D>
    void Line(D const& distance, float width, Rgb core, Rgb glow, float glowRadius, float glowStrength, float strength = 1.0f) {
        Shape([&](V p) { return distance(p) - width; }, [&](V) { return core; }, glow, glowRadius, glowStrength * strength, strength);
    }

    std::vector<std::uint32_t> Result() const {
        std::vector<std::uint32_t> out(static_cast<std::size_t>(kN) * kN, 0);
        auto encode = [](float linear) {
            float const c = Clamp01(linear);
            float const e = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
            return static_cast<std::uint32_t>(std::lround(Clamp01(e) * 255.0f));
        };
        for (std::size_t i = 0; i < out.size(); ++i) {
            // Glows fade out before the picture's edge, so no square shows on the tile.
            int const px = static_cast<int>(i % static_cast<std::size_t>(kN));
            int const py = static_cast<int>(i / static_cast<std::size_t>(kN));
            float const edge = static_cast<float>(std::min(std::min(px, kN - 1 - px), std::min(py, kN - 1 - py))) / static_cast<float>(kN);
            float const a = a_[i] * Smooth(0.0f, 0.1f, edge);
            if (a < 1.0f / 512.0f) {
                continue;
            }
            std::uint32_t const r = encode(rgb_[i * 3 + 0] / a_[i]);
            std::uint32_t const g = encode(rgb_[i * 3 + 1] / a_[i]);
            std::uint32_t const b = encode(rgb_[i * 3 + 2] / a_[i]);
            std::uint32_t const alpha = static_cast<std::uint32_t>(std::lround(Clamp01(a) * 255.0f));
            out[i] = r | (g << 8) | (b << 16) | (alpha << 24);
        }
        return out;
    }

private:
    std::vector<float> rgb_;  // linear, premultiplied
    std::vector<float> a_;
};

// ---- Palettes ---------------------------------------------------------------------------------------------------------

struct Palette {
    Rgb core;   // the brightest part
    Rgb light;  // the body
    Rgb deep;   // the far end of gradients
    Rgb glow;
};
Palette Biotic() { return {Srgb(244, 240, 255), Srgb(170, 160, 255), Srgb(110, 80, 235), Srgb(130, 90, 255)}; }
Palette Tech() { return {Srgb(236, 250, 255), Srgb(120, 205, 255), Srgb(50, 120, 240), Srgb(70, 160, 255)}; }
Palette Combat() { return {Srgb(255, 246, 228), Srgb(255, 190, 110), Srgb(235, 120, 40), Srgb(255, 140, 50)}; }

// ---- A figure -----------------------------------------------------------------------------------------------------------

struct Pose {
    V head;
    V neck;
    V chest;
    V pelvis;
    V shoulder;
    V elbowA, handA;  // the near arm
    V elbowB, handB;  // the far arm
    V kneeA, footA;
    V kneeB, footB;
    float scale = 1.0f;
};
float Figure(V p, Pose const& f) {
    float const s = f.scale;
    float d = Circle(p, f.head, 0.085f * s);
    d = SMin(d, Capsule(p, f.neck, f.head, 0.034f * s, 0.04f * s), 0.02f);
    d = SMin(d, Capsule(p, f.chest, f.pelvis, 0.078f * s, 0.06f * s), 0.03f);
    d = SMin(d, Capsule(p, f.neck, f.chest, 0.04f * s, 0.07f * s), 0.03f);
    for (auto const& [elbow, hand] : {std::pair{f.elbowA, f.handA}, std::pair{f.elbowB, f.handB}}) {
        d = SMin(d, Capsule(p, f.shoulder, elbow, 0.042f * s, 0.035f * s), 0.025f);
        d = SMin(d, Capsule(p, elbow, hand, 0.034f * s, 0.027f * s), 0.015f);
        d = SMin(d, Circle(p, hand, 0.032f * s), 0.015f);
    }
    for (auto const& [knee, foot] : {std::pair{f.kneeA, f.footA}, std::pair{f.kneeB, f.footB}}) {
        d = SMin(d, Capsule(p, f.pelvis, knee, 0.058f * s, 0.045f * s), 0.03f);
        d = SMin(d, Capsule(p, knee, foot, 0.043f * s, 0.03f * s), 0.015f);
    }
    return d;
}
// The pose made `k` times larger about `about`.
Pose Scaled(Pose f, float k, V about) {
    for (V* p : {&f.head, &f.neck, &f.chest, &f.pelvis, &f.shoulder, &f.elbowA, &f.handA, &f.elbowB, &f.handB, &f.kneeA, &f.footA, &f.kneeB,
             &f.footB}) {
        *p = about + (*p - about) * k;
    }
    f.scale *= k;
    return f;
}

// A glowing silhouette: light at the top, deeper at the bottom, a bright rim.
template <class D>
void Silhouette(Painter& paint, D const& distance, Palette const& c, float top = 0.6f, float bottom = -0.6f) {
    paint.Shape(distance,
        [&](V p) {
            float const t = Clamp01((top - p.y) / (top - bottom));
            float const rim = Smooth(-0.035f, 0.0f, distance(p));
            return Mix(Mix(c.core, c.light, t), c.core, rim * 0.6f);
        },
        c.glow, 0.07f, 0.7f, 0.97f);
}
// Speed streaks behind something moving along `dir` from `from`.
void Streaks(Painter& paint, V from, V dir, Palette const& c, std::span<float const> offsets, std::span<float const> lengths) {
    V const n{-dir.y, dir.x};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        V const a = from + n * offsets[i];
        V const b = a - dir * lengths[i];
        paint.Layer(
            [&](V p) {
                V const pa = p - a;
                V const ba = b - a;
                float const h = Clamp01(Dot(pa, ba) / Dot(ba, ba));
                float const width = 0.021f * (1.0f - h) + 0.004f;
                float const d = Len(pa - ba * h) - width;
                float const fade = (1.0f - h) * (1.0f - h);
                return fade * (Clamp01(0.5f - d / kPx) + 0.45f * std::exp(-std::max(d, 0.0f) / 0.03f));
            },
            [&](V p) { return Mix(c.core, c.light, Clamp01(Len(p - a) / 0.3f)); });
    }
}
void Particles(Painter& paint, Palette const& c, int count, int seed, V centre, float minR, float maxR, float size) {
    for (int i = 0; i < count; ++i) {
        float const angle = Hash(seed, i) * 360.0f;
        float const r = minR + (maxR - minR) * Hash(seed + 7, i);
        float const s = size * (0.5f + Hash(seed + 13, i));
        V const at = centre + Polar(angle, r);
        paint.Shape([&](V p) { return Circle(p, at, s); }, [&](V) { return c.core; }, c.glow, 0.03f, 0.5f, 0.9f);
    }
}

// ---- The icons -------------------------------------------------------------------------------------------------------

void PaintThrow(Painter& paint) {
    Palette const c = Biotic();
    V const dir{0.78f, 0.62f};
    Pose f;
    f.pelvis = {-0.04f, -0.08f};
    f.chest = {0.17f, 0.12f};
    f.neck = {0.235f, 0.19f};
    f.head = {0.30f, 0.27f};
    f.shoulder = {0.18f, 0.15f};
    f.elbowA = {0.35f, 0.29f};
    f.handA = {0.52f, 0.42f};
    f.elbowB = {0.33f, 0.08f};
    f.handB = {0.50f, 0.10f};
    f.kneeA = {-0.22f, -0.12f};
    f.footA = {-0.42f, -0.24f};
    f.kneeB = {-0.12f, -0.33f};
    f.footB = {-0.33f, -0.47f};
    f = Scaled(f, 1.42f, {0.08f, -0.02f});
    std::array<float, 7> const offsets{-0.36f, -0.24f, -0.12f, -0.02f, 0.09f, 0.2f, 0.32f};
    std::array<float, 7> const lengths{0.42f, 0.66f, 0.8f, 0.58f, 0.86f, 0.62f, 0.44f};
    Streaks(paint, f.pelvis + dir * 0.1f, dir, c, offsets, lengths);
    Silhouette(paint, [&](V p) { return Figure(p, f); }, c, 0.6f, -0.6f);
}

void PaintLift(Painter& paint) {
    Palette const c = Biotic();
    // The field holding it up: cups of light under the body.
    for (int i = 0; i < 3; ++i) {
        float const rx = 0.78f - 0.16f * static_cast<float>(i);
        float const ry = 0.30f - 0.06f * static_cast<float>(i);
        V const centre{0.0f, -0.12f - 0.04f * static_cast<float>(i)};
        float const strength = 0.95f - 0.2f * static_cast<float>(i);
        paint.Line(
            [&](V p) {
                float const below = Smooth(0.02f, -0.06f, p.y - centre.y);  // the lower half only, fading at its tips
                return EllipseLine(p, centre, rx, ry) + (1.0f - below) * 0.2f;
            },
            0.012f, c.core, c.glow, 0.05f, 0.6f, strength);
    }
    Pose f;
    f.pelvis = {0.08f, 0.12f};
    f.chest = {-0.17f, 0.24f};
    f.neck = {-0.26f, 0.29f};
    f.head = {-0.36f, 0.35f};
    f.shoulder = {-0.16f, 0.26f};
    f.elbowA = {-0.16f, 0.06f};
    f.handA = {-0.24f, -0.07f};
    f.elbowB = {-0.04f, 0.42f};
    f.handB = {0.10f, 0.52f};
    f.kneeA = {0.30f, 0.06f};
    f.footA = {0.36f, -0.14f};
    f.kneeB = {0.30f, 0.24f};
    f.footB = {0.50f, 0.16f};
    f = Scaled(f, 1.28f, {0.0f, 0.22f});
    Particles(paint, c, 9, 3, {0.0f, -0.1f}, 0.35f, 0.75f, 0.014f);
    Silhouette(paint, [&](V p) { return Figure(p, f); }, c, 0.62f, -0.1f);
}

void PaintWarp(Painter& paint) {
    Palette const c{Srgb(255, 236, 255), Srgb(205, 120, 255), Srgb(120, 50, 230), Srgb(170, 80, 255)};
    V const head{0.28f, 0.30f};
    V const tail = V{-0.78f, -0.72f};
    V const axis = (tail - head) * (1.0f / Len(tail - head));
    V const side{-axis.y, axis.x};
    auto flame = [&](V p) {
        V const q = p - head;
        float const u = Dot(q, axis);         // along the tail
        float const v = Dot(q, side);         // across it
        float const length = Len(tail - head);
        float const t = Clamp01(u / length);
        float const wobble = (Fbm({u * 5.0f, v * 3.0f}) - 0.5f) * 0.25f * t;
        float const width = 0.40f * (1.0f - t) * (1.0f - t) + 0.03f;
        float const across = std::abs(v + wobble) / width;
        float const tongues = Smooth(0.25f, 0.75f, Fbm({u * 7.0f - 3.0f, v * 9.0f}));
        float const body = (1.0f - Smooth(0.6f, 1.0f, across)) * (u > -0.2f ? 1.0f : 0.0f);
        return body * (0.45f + 0.55f * tongues) * Smooth(-0.22f, 0.0f, u);
    };
    paint.Layer([&](V p) { return 0.5f * std::exp(-std::max(Circle(p, head, 0.2f), 0.0f) / 0.12f); }, [&](V) { return c.glow; });
    paint.Layer(flame, [&](V p) {
        float const t = Clamp01(Dot(p - head, axis) / Len(tail - head));
        return Mix(c.light, c.deep, t);
    });
    // The head: a bright core with a dark eye, the warp's pull.
    paint.Shape([&](V p) { return Circle(p, head, 0.22f); }, [&](V p) { return Mix(c.core, c.light, Clamp01(Len(p - head) / 0.22f)); },
        c.glow, 0.1f, 0.8f, 0.95f);
    paint.Layer([&](V p) { return 0.75f * Clamp01(0.5f - Circle(p, head + V{0.04f, 0.03f}, 0.09f) / kPx); },
        [&](V) { return Srgb(40, 10, 70); });
    for (int i = 0; i < 10; ++i) {
        float const t = 0.15f + 0.8f * Hash(5, i);
        V const at = head + axis * (t * Len(tail - head)) + side * ((Hash(9, i) - 0.5f) * 0.5f * (1.0f - t));
        float const r = 0.008f + 0.014f * Hash(11, i);
        paint.Shape([&](V p) { return Circle(p, at, r); }, [&](V) { return c.core; }, c.glow, 0.03f, 0.6f, 0.9f);
    }
}

void PaintSingularity(Painter& paint) {
    Palette const c{Srgb(246, 236, 255), Srgb(176, 130, 255), Srgb(105, 60, 225), Srgb(140, 90, 255)};
    V const centre{0.0f, 0.04f};
    float const tilt = 0.72f;  // the disc seen from a little above
    auto disc = [&](V p) {
        V const q{p.x - centre.x, (p.y - centre.y) / tilt};
        return q;
    };
    paint.Layer([&](V p) { return 0.45f * std::exp(-std::max(Len(disc(p)) - 0.3f, 0.0f) / 0.2f); }, [&](V) { return c.glow; });
    paint.Layer(
        [&](V p) {
            V const q = disc(p);
            float const r = Len(q);
            float const theta = std::atan2(q.y, q.x);
            float const arms = 0.5f + 0.5f * std::cos(4.0f * theta + 7.5f * std::log(std::max(r, 1e-3f)));
            float const grain = Fbm({theta * 2.0f + 8.0f * r, r * 6.0f});
            float const envelope = Smooth(0.16f, 0.22f, r) * (1.0f - Smooth(0.5f, 0.95f, r));
            return envelope * Clamp01(std::pow(arms, 2.2f) * 1.3f + 0.25f * grain);
        },
        [&](V p) { return Mix(c.core, c.light, Smooth(0.18f, 0.6f, Len(disc(p)))); });
    // The event horizon: a dark core with a bright rim.
    paint.Line([&](V p) { return std::abs(Len(disc(p)) - 0.19f) * tilt; }, 0.012f, c.core, c.glow, 0.04f, 0.8f);
    paint.Layer([&](V p) { return 0.95f * Clamp01(0.5f - (Len(disc(p)) - 0.18f) * tilt / kPx); }, [&](V) { return Srgb(16, 6, 34); });
    // Debris drawn in.
    std::array<std::array<V, 5>, 7> rocks{};
    std::array<V, 7> const places{V{-0.78f, -0.02f}, V{-0.55f, 0.48f}, V{0.66f, 0.40f}, V{0.80f, -0.10f}, V{0.55f, -0.46f},
        V{-0.48f, -0.46f}, V{0.12f, 0.70f}};
    for (std::size_t i = 0; i < rocks.size(); ++i) {
        float const size = 0.05f + 0.06f * Hash(21, static_cast<int>(i));
        for (std::size_t k = 0; k < 5; ++k) {
            float const angle = static_cast<float>(k) * 72.0f + 30.0f * Hash(23, static_cast<int>(i * 5 + k));
            rocks[i][k] = places[i] + Polar(angle, size * (0.7f + 0.5f * Hash(29, static_cast<int>(i * 5 + k))));
        }
        V const at = places[i];
        std::span<V const> const rock(rocks[i]);
        paint.Shape([&](V p) { return Polygon(p, rock); },
            [&](V p) { return Mix(Srgb(150, 135, 190), Srgb(70, 60, 95), Clamp01((at.y - p.y) / 0.12f + 0.5f)); }, c.glow, 0.04f, 0.35f,
            0.95f);
    }
}

void PaintStasis(Painter& paint) {
    Palette const c = Biotic();
    V const centre{0.0f, -0.06f};
    float const radius = 0.76f;
    // The bubble: brighter toward its rim.
    paint.Layer([&](V p) { float const r = Len(p - centre) / radius; return r < 1.0f ? 0.35f * std::pow(r, 3.0f) : 0.0f; },
        [&](V) { return c.light; });
    paint.Line([&](V p) { return std::abs(Circle(p, centre, radius)); }, 0.012f, c.core, c.glow, 0.05f, 0.7f);
    Pose f;
    f.pelvis = {0.0f, -0.16f};
    f.chest = {0.0f, 0.11f};
    f.neck = {0.0f, 0.2f};
    f.head = {0.0f, 0.3f};
    f.shoulder = {0.0f, 0.15f};
    f.elbowA = {0.19f, 0.04f};
    f.handA = {0.29f, -0.08f};
    f.elbowB = {-0.19f, 0.04f};
    f.handB = {-0.29f, -0.08f};
    f.kneeA = {0.09f, -0.38f};
    f.footA = {0.12f, -0.58f};
    f.kneeB = {-0.09f, -0.38f};
    f.footB = {-0.12f, -0.58f};
    Silhouette(paint, [&](V p) { return Figure(p, f); }, c, 0.4f, -0.6f);
    // The rings holding it.
    paint.Line([&](V p) { return EllipseLine(p, centre, 0.86f, 0.17f, 18.0f); }, 0.008f, c.core, c.glow, 0.04f, 0.6f, 0.9f);
    paint.Line([&](V p) { return EllipseLine(p, centre, 0.86f, 0.17f, -18.0f); }, 0.008f, c.core, c.glow, 0.04f, 0.6f, 0.75f);
}

// Distance to the edges of a hexagon tiling (cells `size` across their flats).
float HexEdges(V p, float size) {
    V const r{1.0f, 1.7320508f};
    V const h = r * 0.5f;
    V const q = p * (1.0f / size);
    auto modv = [](V a, V m) { return V{a.x - m.x * std::floor(a.x / m.x), a.y - m.y * std::floor(a.y / m.y)}; };
    V const a = modv(q, r) - h;
    V const b = modv(q - h, r) - h;
    V const g = Dot(a, a) < Dot(b, b) ? a : b;
    V const ga{std::abs(g.x), std::abs(g.y)};
    float const hex = std::max(Dot(ga, V{0.5f, 0.8660254f}), ga.x);  // 0.5 at the edge
    return (0.5f - hex) * size;
}

void PaintBarrier(Painter& paint) {
    Palette const c{Srgb(230, 242, 255), Srgb(120, 170, 255), Srgb(40, 80, 210), Srgb(70, 130, 255)};
    V const base{0.0f, -0.36f};
    float const radius = 0.74f;
    auto inside = [&](V p) { return std::max(Circle(p, base, radius), base.y - p.y); };
    auto sphere = [&](V p) {
        V const q = (p - base) * (1.0f / radius);
        float const z = std::sqrt(std::max(0.0f, 1.0f - Dot(q, q)));
        return std::pair{q * (1.0f / (0.55f + 0.45f * z)), z};  // the cells stretch toward the rim, as on a dome
    };
    paint.Layer([&](V p) { return 0.45f * std::exp(-std::max(inside(p), 0.0f) / 0.08f); }, [&](V) { return c.glow; });
    paint.Layer(
        [&](V p) {
            float const coverage = Clamp01(0.5f - inside(p) / kPx);
            auto const [q, z] = sphere(p);
            return coverage * (0.22f + 0.45f * std::pow(1.0f - z, 2.0f));
        },
        [&](V p) { return Mix(c.light, c.deep, Clamp01((p.y - base.y) / radius)); });
    paint.Layer(
        [&](V p) {
            float const coverage = Clamp01(0.5f - (inside(p) + 0.012f) / kPx);
            auto const [q, z] = sphere(p);
            float const d = HexEdges(q + V{0.0f, 0.12f}, 0.36f) * radius * (0.55f + 0.45f * z);
            float const line = Clamp01(0.5f - (d - 0.008f) / kPx) + 0.35f * std::exp(-std::max(d, 0.0f) / 0.02f);
            return coverage * line * (0.65f + 0.35f * (1.0f - z));
        },
        [&](V) { return c.light; });
    // The rim and the base.
    paint.Line([&](V p) { return p.y < base.y - 0.01f ? 1.0f : std::abs(Circle(p, base, radius)); }, 0.013f, c.core, c.glow, 0.05f, 0.75f);
    paint.Line([&](V p) { return EllipseLine(p, base, radius * 1.02f, 0.07f); }, 0.009f, c.core, c.glow, 0.05f, 0.6f, 0.85f);
}

// A lightning bolt from a to b, its kinks from the hash `seed`.
void Bolt(Painter& paint, V a, V b, int seed, float width, Palette const& c, float strength = 1.0f) {
    std::array<V, 7> points{};
    V const d = b - a;
    V const n = V{-d.y, d.x} * (1.0f / std::max(Len(d), 1e-3f));
    for (int i = 0; i < 7; ++i) {
        float const t = static_cast<float>(i) / 6.0f;
        float const jitter = (i == 0 || i == 6) ? 0.0f : (Hash(seed, i) - 0.5f) * 0.28f;
        points[static_cast<std::size_t>(i)] = a + d * t + n * jitter;
    }
    paint.Line([&](V p) { return Path(p, points); }, width, c.core, c.glow, 0.05f, 0.7f, strength);
}

void PaintOverload(Painter& paint) {
    Palette const c = Tech();
    std::array<V, 7> const bolt{V{0.12f, 0.78f}, V{-0.30f, 0.02f}, V{-0.02f, 0.04f}, V{-0.18f, -0.78f}, V{0.32f, 0.10f}, V{0.04f, 0.08f},
        V{0.30f, 0.78f}};
    paint.Shape([&](V p) { return Polygon(p, bolt); }, [&](V p) { return Mix(c.core, c.light, Clamp01(0.5f - p.y * 0.6f)); }, c.glow, 0.08f,
        0.75f, 0.97f);
    Bolt(paint, {-0.2f, 0.3f}, {-0.75f, 0.55f}, 3, 0.008f, c, 0.8f);
    Bolt(paint, {0.18f, -0.2f}, {0.75f, -0.5f}, 5, 0.008f, c, 0.8f);
    Bolt(paint, {0.25f, 0.4f}, {0.7f, 0.3f}, 7, 0.006f, c, 0.6f);
}

void PaintDamping(Painter& paint) {
    Palette const c = Tech();
    for (int i = 0; i < 3; ++i) {
        float const r = 0.38f + 0.18f * static_cast<float>(i);
        paint.Line([&](V p) { return std::abs(Circle(p, {}, r)); }, 0.008f, c.light, c.glow, 0.04f, 0.5f, 0.75f - 0.2f * static_cast<float>(i));
    }
    std::array<V, 64> wave{};
    for (std::size_t i = 0; i < wave.size(); ++i) {
        float const x = -0.85f + 1.7f * static_cast<float>(i) / 63.0f;
        float const amplitude = 0.42f * Clamp01((0.35f - x) / 1.2f);  // dies away to the right
        wave[i] = {x, amplitude * std::sin(x * 14.0f)};
    }
    paint.Line([&](V p) { return Path(p, wave); }, 0.016f, c.core, c.glow, 0.06f, 0.75f);
}

void PaintSabotage(Painter& paint) {
    Palette const c = Tech();
    std::array<V, 10> const gun{V{-0.75f, 0.22f}, V{0.55f, 0.22f}, V{0.62f, 0.12f}, V{0.62f, 0.02f}, V{-0.05f, 0.02f}, V{-0.18f, -0.62f},
        V{-0.45f, -0.62f}, V{-0.38f, -0.02f}, V{-0.75f, -0.02f}, V{-0.80f, 0.12f}};
    paint.Shape([&](V p) { return Polygon(p, gun); }, [&](V p) { return Mix(c.light, c.deep, Clamp01(0.4f - p.y)); }, c.glow, 0.06f, 0.55f,
        0.85f);
    Palette const spark{Srgb(255, 245, 225), Srgb(255, 170, 90), Srgb(230, 90, 40), Srgb(255, 120, 50)};
    std::array<V, 5> const crack{V{0.05f, 0.24f}, V{0.12f, 0.12f}, V{0.02f, 0.06f}, V{0.1f, -0.06f}, V{0.04f, -0.2f}};
    paint.Line([&](V p) { return Path(p, crack); }, 0.012f, spark.core, spark.glow, 0.05f, 0.8f);
    Particles(paint, spark, 9, 31, {0.12f, 0.15f}, 0.12f, 0.45f, 0.014f);
}

void PaintHacking(Painter& paint) {
    Palette const c = Tech();
    paint.Line([&](V p) { return std::abs(Circle(p, {}, 0.62f)); }, 0.012f, c.light, c.glow, 0.05f, 0.6f);
    for (int i = 0; i < 6; ++i) {
        float const angle = 30.0f + 60.0f * static_cast<float>(i);
        std::array<V, 3> const trace{Polar(angle, 0.24f), Polar(angle + 12.0f, 0.42f), Polar(angle + 12.0f, 0.62f)};
        paint.Line([&](V p) { return Path(p, trace); }, 0.008f, c.light, c.glow, 0.04f, 0.5f, 0.85f);
        V const node = Polar(angle + 12.0f, 0.42f);
        paint.Shape([&](V p) { return Circle(p, node, 0.03f); }, [&](V) { return c.core; }, c.glow, 0.04f, 0.6f);
    }
    // The geth's eye.
    paint.Shape([&](V p) { return Circle(p, {}, 0.18f); }, [&](V p) { return Mix(c.core, c.light, Clamp01(Len(p) / 0.18f)); }, c.glow, 0.1f,
        0.9f);
}

void PaintNeuralShock(Painter& paint) {
    Palette const c = Tech();
    std::array<V, 12> const head{V{-0.42f, -0.72f}, V{-0.40f, -0.25f}, V{-0.52f, 0.05f}, V{-0.45f, 0.42f}, V{-0.15f, 0.68f}, V{0.2f, 0.66f},
        V{0.45f, 0.42f}, V{0.5f, 0.12f}, V{0.62f, -0.08f}, V{0.5f, -0.14f}, V{0.46f, -0.4f}, V{0.2f, -0.45f}};
    paint.Line([&](V p) { return std::abs(Polygon(p, head)); }, 0.012f, c.light, c.glow, 0.05f, 0.6f);
    paint.Layer([&](V p) { return 0.2f * Clamp01(0.5f - Polygon(p, head) / kPx); }, [&](V) { return c.deep; });
    std::array<V, 6> const bolt{V{0.06f, 0.5f}, V{-0.16f, 0.12f}, V{0.02f, 0.12f}, V{-0.12f, -0.28f}, V{0.2f, 0.18f}, V{0.04f, 0.18f}};
    paint.Shape([&](V p) { return Polygon(p, bolt); }, [&](V) { return c.core; }, c.glow, 0.07f, 0.8f);
}

std::array<V, 9> ShieldPoints(float s, V at = {}) {
    return {at + V{0.0f, 0.70f} * s, at + V{0.55f, 0.52f} * s, at + V{0.56f, 0.05f} * s, at + V{0.42f, -0.36f} * s,
        at + V{0.0f, -0.74f} * s, at + V{-0.42f, -0.36f} * s, at + V{-0.56f, 0.05f} * s, at + V{-0.55f, 0.52f} * s,
        at + V{0.0f, 0.70f} * s};
}

void PaintShieldBoost(Painter& paint) {
    Palette const c = Tech();
    std::array<V, 9> const shield = ShieldPoints(1.0f);
    std::span<V const> const outline(shield.data(), 8);
    paint.Layer([&](V p) { return 0.3f * Clamp01(0.5f - Polygon(p, outline) / kPx); }, [&](V p) { return Mix(c.light, c.deep, Clamp01(0.5f - p.y)); });
    paint.Line([&](V p) { return std::abs(Polygon(p, outline)); }, 0.016f, c.core, c.glow, 0.06f, 0.7f);
    std::array<V, 3> const chevronA{V{-0.28f, -0.12f}, V{0.0f, 0.16f}, V{0.28f, -0.12f}};
    std::array<V, 3> const chevronB{V{-0.28f, -0.36f}, V{0.0f, -0.08f}, V{0.28f, -0.36f}};
    paint.Line([&](V p) { return Path(p, chevronA); }, 0.03f, c.core, c.glow, 0.05f, 0.7f);
    paint.Line([&](V p) { return Path(p, chevronB); }, 0.03f, c.core, c.glow, 0.05f, 0.7f, 0.7f);
}

void PaintAdrenaline(Painter& paint) {
    Palette const c = Combat();
    for (int i = 0; i < 3; ++i) {
        float const y = -0.42f + 0.32f * static_cast<float>(i);
        std::array<V, 3> const chevron{V{-0.42f, y}, V{0.0f, y + 0.3f}, V{0.42f, y}};
        paint.Line([&](V p) { return Path(p, chevron); }, 0.04f, c.core, c.glow, 0.06f, 0.7f, 0.55f + 0.2f * static_cast<float>(i));
    }
}

void PaintImmunity(Painter& paint) {
    Palette const c = Combat();
    std::array<V, 9> const shield = ShieldPoints(0.95f);
    std::span<V const> const outline(shield.data(), 8);
    paint.Shape([&](V p) { return Polygon(p, outline); }, [&](V p) { return Mix(c.light, c.deep, Clamp01(0.5f - p.y)); }, c.glow, 0.07f, 0.6f,
        0.55f);
    paint.Line([&](V p) { return std::abs(Polygon(p, outline)); }, 0.014f, c.core, c.glow, 0.05f, 0.6f);
    paint.Shape([&](V p) { return std::min(Capsule(p, {0.0f, -0.32f}, {0.0f, 0.36f}, 0.07f, 0.07f), Capsule(p, {-0.3f, 0.06f}, {0.3f, 0.06f}, 0.07f, 0.07f)); },
        [&](V) { return c.core; }, c.glow, 0.05f, 0.6f);
}

void PaintUnity(Painter& paint) {
    Palette const c = Combat();
    // Two figures, one helping the other up.
    Pose a;
    a.pelvis = {-0.28f, -0.2f};
    a.chest = {-0.26f, 0.08f};
    a.neck = {-0.26f, 0.17f};
    a.head = {-0.27f, 0.27f};
    a.shoulder = {-0.26f, 0.12f};
    a.elbowA = {-0.08f, 0.06f};
    a.handA = {0.06f, -0.02f};
    a.elbowB = {-0.4f, -0.02f};
    a.handB = {-0.44f, -0.16f};
    a.kneeA = {-0.2f, -0.44f};
    a.footA = {-0.2f, -0.68f};
    a.kneeB = {-0.36f, -0.44f};
    a.footB = {-0.4f, -0.68f};
    Pose b;
    b.pelvis = {0.36f, -0.5f};
    b.chest = {0.24f, -0.24f};
    b.neck = {0.2f, -0.16f};
    b.head = {0.18f, -0.06f};
    b.shoulder = {0.23f, -0.2f};
    b.elbowA = {0.14f, -0.12f};
    b.handA = {0.08f, -0.03f};
    b.elbowB = {0.4f, -0.36f};
    b.handB = {0.48f, -0.5f};
    b.kneeA = {0.56f, -0.42f};
    b.footA = {0.62f, -0.66f};
    b.kneeB = {0.2f, -0.6f};
    b.footB = {0.42f, -0.7f};
    Silhouette(paint, [&](V p) { return std::min(Figure(p, a), Figure(p, b)); }, c, 0.3f, -0.7f);
    paint.Shape([&](V p) { return std::min(Capsule(p, {0.0f, 0.44f}, {0.0f, 0.76f}, 0.045f, 0.045f), Capsule(p, {-0.16f, 0.6f}, {0.16f, 0.6f}, 0.045f, 0.045f)); },
        [&](V) { return c.core; }, c.glow, 0.05f, 0.7f);
}

void Reticle(Painter& paint, Palette const& c, float radius) {
    paint.Line([&](V p) { return std::abs(Circle(p, {}, radius)); }, 0.014f, c.light, c.glow, 0.05f, 0.6f);
    paint.Line([&](V p) { return std::abs(Circle(p, {}, radius * 0.45f)); }, 0.008f, c.light, c.glow, 0.04f, 0.5f, 0.7f);
    for (float angle : {0.0f, 90.0f, 180.0f, 270.0f}) {
        V const a = Polar(angle, radius * 0.6f);
        V const b = Polar(angle, radius * 1.18f);
        paint.Line([&](V p) { return Capsule(p, a, b, 0.0f, 0.0f); }, 0.014f, c.core, c.glow, 0.04f, 0.6f);
    }
    paint.Shape([&](V p) { return Circle(p, {}, 0.045f); }, [&](V) { return c.core; }, c.glow, 0.05f, 0.8f);
}

void PaintMarksman(Painter& paint) { Reticle(paint, Combat(), 0.6f); }

void PaintOverkill(Painter& paint) {
    Palette const c = Combat();
    for (int i = 0; i < 3; ++i) {
        float const x = -0.38f + 0.38f * static_cast<float>(i);
        std::array<V, 6> const bullet{V{x - 0.1f, -0.6f}, V{x + 0.1f, -0.6f}, V{x + 0.1f, 0.2f}, V{x + 0.05f, 0.48f}, V{x - 0.05f, 0.48f},
            V{x - 0.1f, 0.2f}};
        paint.Shape([&](V p) { return Polygon(p, bullet); }, [&](V p) { return Mix(c.core, c.deep, Clamp01(0.4f - p.y)); }, c.glow, 0.06f, 0.6f,
            0.95f);
    }
}

void PaintCarnage(Painter& paint) {
    Palette const c = Combat();
    for (int i = 0; i < 9; ++i) {
        float const angle = -24.0f + 6.0f * static_cast<float>(i);
        V const from = {-0.7f, 0.0f};
        V const to = from + Polar(angle, 1.05f + 0.25f * Hash(41, i));
        paint.Line([&](V p) { return Capsule(p, from, to, 0.0f, 0.0f); }, 0.006f, c.light, c.glow, 0.03f, 0.4f, 0.7f);
        paint.Shape([&](V p) { return Circle(p, to, 0.04f); }, [&](V) { return c.core; }, c.glow, 0.05f, 0.7f);
    }
    paint.Shape([&](V p) { return Circle(p, {-0.72f, 0.0f}, 0.1f); }, [&](V) { return c.core; }, c.glow, 0.1f, 0.9f);
}

void PaintAssassination(Painter& paint) {
    Palette const c = Combat();
    Reticle(paint, c, 0.56f);
    paint.Line([&](V p) { return std::min(Capsule(p, {-0.3f, -0.3f}, {0.3f, 0.3f}, 0.0f, 0.0f), Capsule(p, {-0.3f, 0.3f}, {0.3f, -0.3f}, 0.0f, 0.0f)); },
        0.02f, Srgb(255, 236, 225), Srgb(255, 80, 60), 0.05f, 0.7f, 0.9f);
}

void PaintGeneric(Painter& paint) {
    Palette const c = Biotic();
    std::array<V, 6> hex{};
    for (std::size_t i = 0; i < hex.size(); ++i) {
        hex[i] = Polar(90.0f + 60.0f * static_cast<float>(i), 0.62f);
    }
    paint.Line([&](V p) { return std::abs(Polygon(p, hex)); }, 0.016f, c.light, c.glow, 0.06f, 0.6f);
    paint.Shape([&](V p) { return Circle(p, {}, 0.2f); }, [&](V p) { return Mix(c.core, c.light, Clamp01(Len(p) / 0.2f)); }, c.glow, 0.1f, 0.8f);
}

}  // namespace

char const* Name(Icon icon) {
    switch (icon) {
    case Icon::Generic: return "generic";
    case Icon::Throw: return "throw";
    case Icon::Lift: return "lift";
    case Icon::Warp: return "warp";
    case Icon::Singularity: return "singularity";
    case Icon::Stasis: return "stasis";
    case Icon::Barrier: return "barrier";
    case Icon::Overload: return "overload";
    case Icon::Damping: return "damping";
    case Icon::Sabotage: return "sabotage";
    case Icon::Hacking: return "hacking";
    case Icon::NeuralShock: return "neural-shock";
    case Icon::ShieldBoost: return "shield-boost";
    case Icon::Adrenaline: return "adrenaline";
    case Icon::Immunity: return "immunity";
    case Icon::Unity: return "unity";
    case Icon::Marksman: return "marksman";
    case Icon::Overkill: return "overkill";
    case Icon::Carnage: return "carnage";
    case Icon::Assassination: return "assassination";
    case Icon::Count: break;
    }
    return "?";
}

Icon ForPower(std::string_view label) {
    // The shared table (core/vr/PowerWheel, research/55 section 3) numbers the icons in Icon's order.
    vr::power_wheel::PowerInfo const* const info = vr::power_wheel::FindPower(std::string(label).c_str());
    return info != nullptr && info->icon > 0 && info->icon < static_cast<int>(Icon::Count) ? static_cast<Icon>(info->icon) : Icon::Generic;
}
static_assert(static_cast<int>(Icon::Throw) == 1 && static_cast<int>(Icon::Barrier) == 6 && static_cast<int>(Icon::Overload) == 7 &&
                  static_cast<int>(Icon::ShieldBoost) == 12 && static_cast<int>(Icon::Assassination) == 19,
    "core/vr/PowerWheel's FindPower numbers the icons in this order");

std::vector<std::uint32_t> Picture(Icon icon) {
    Painter paint;
    switch (icon) {
    case Icon::Throw: PaintThrow(paint); break;
    case Icon::Lift: PaintLift(paint); break;
    case Icon::Warp: PaintWarp(paint); break;
    case Icon::Singularity: PaintSingularity(paint); break;
    case Icon::Stasis: PaintStasis(paint); break;
    case Icon::Barrier: PaintBarrier(paint); break;
    case Icon::Overload: PaintOverload(paint); break;
    case Icon::Damping: PaintDamping(paint); break;
    case Icon::Sabotage: PaintSabotage(paint); break;
    case Icon::Hacking: PaintHacking(paint); break;
    case Icon::NeuralShock: PaintNeuralShock(paint); break;
    case Icon::ShieldBoost: PaintShieldBoost(paint); break;
    case Icon::Adrenaline: PaintAdrenaline(paint); break;
    case Icon::Immunity: PaintImmunity(paint); break;
    case Icon::Unity: PaintUnity(paint); break;
    case Icon::Marksman: PaintMarksman(paint); break;
    case Icon::Overkill: PaintOverkill(paint); break;
    case Icon::Carnage: PaintCarnage(paint); break;
    case Icon::Assassination: PaintAssassination(paint); break;
    case Icon::Generic:
    case Icon::Count: PaintGeneric(paint); break;
    }
    return paint.Result();
}

namespace {

// A square kPixels map's separable box blur of `radius`, twice (close to a Gaussian).
std::vector<float> Blur(std::vector<float> const& in, int radius) {
    int const n = static_cast<int>(kPixels);
    std::vector<float> a = in;
    std::vector<float> b(in.size(), 0.0f);
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                float sum = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    sum += a[static_cast<std::size_t>(y * n + std::clamp(x + k, 0, n - 1))];
                }
                b[static_cast<std::size_t>(y * n + x)] = sum / static_cast<float>(2 * radius + 1);
            }
        }
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                float sum = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    sum += b[static_cast<std::size_t>(std::clamp(y + k, 0, n - 1) * n + x)];
                }
                a[static_cast<std::size_t>(y * n + x)] = sum / static_cast<float>(2 * radius + 1);
            }
        }
    }
    return a;
}

// The smallest (shrink) or largest (grow) value within `radius` (a square), per pixel.
std::vector<float> Extreme(std::vector<float> const& in, int radius, bool grow) {
    int const n = static_cast<int>(kPixels);
    std::vector<float> out(in.size(), 0.0f);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            float v = grow ? 0.0f : 1.0f;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    float const s = in[static_cast<std::size_t>(std::clamp(y + dy, 0, n - 1) * n + std::clamp(x + dx, 0, n - 1))];
                    v = grow ? std::max(v, s) : std::min(v, s);
                }
            }
            out[static_cast<std::size_t>(y * n + x)] = v;
        }
    }
    return out;
}

}  // namespace

std::vector<std::uint32_t> Hologram(std::vector<std::uint32_t> const& picture) {
    int const n = static_cast<int>(kPixels);
    std::size_t const count = static_cast<std::size_t>(n) * static_cast<std::size_t>(n);
    if (picture.size() != count) {
        return picture;
    }
    // The core strokes: where the painted light is bright (its glow left out), softened a little.
    std::vector<float> core(count, 0.0f);
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t const p = picture[i];
        float const r = static_cast<float>(p & 0xFF);
        float const g = static_cast<float>((p >> 8) & 0xFF);
        float const b = static_cast<float>((p >> 16) & 0xFF);
        float const a = static_cast<float>((p >> 24) & 0xFF) / 255.0f;
        float const lum = (0.299f * r + 0.587f * g + 0.114f * b) * a;
        core[i] = std::clamp((lum - 150.0f) / 50.0f, 0.0f, 1.0f);
    }
    core = Extreme(core, 1, true);
    std::vector<float> const inner = Extreme(core, 3, false);
    std::vector<float> const halo = Blur(core, 6);
    std::vector<std::uint32_t> out(count, 0);
    for (int y = 0; y < n; ++y) {
        float const fade = 1.0f - 0.55f * static_cast<float>(y) / static_cast<float>(n);
        bool const scan = y % 3 == 0;
        for (int x = 0; x < n; ++x) {
            std::size_t const i = static_cast<std::size_t>(y * n + x);
            float const edge = std::max(core[i] - inner[i], 0.0f);
            float const body = core[i] * 0.42f * fade;
            float const ghost = 0.16f * core[static_cast<std::size_t>(std::min(y + 5, n - 1) * n + x)];  // the image a few pixels up
            float const glow = 0.55f * halo[i];
            float alpha = 1.0f - (1.0f - std::max(edge, body)) * (1.0f - glow) * (1.0f - ghost);
            if (scan) {
                alpha *= 0.6f;
            }
            auto const a = static_cast<std::uint32_t>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
            out[i] = (a << 24) | 0x00FFFFFFu;  // white light: the wheel tints it
        }
    }
    return out;
}

}  // namespace mevr::host::power_icons
