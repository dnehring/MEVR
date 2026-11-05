// SPDX-License-Identifier: MIT
#include "core/gfx/Raster.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <utility>

namespace mevr::gfx {
namespace {

constexpr int kMaxSide = 8192;
constexpr double kMaxPixels = 4096.0 * 4096.0; // the float canvas is 16 bytes a pixel
constexpr int kMaxNesting = 24;
constexpr double kCurveTolerancePx = 0.05;
constexpr double kRoundTolerancePx = 0.05;

struct Pt {
    double x = 0;
    double y = 0;
};

struct Seg {
    Pt a, b;
};

struct DRect {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty = true;

    void Add(double x, double y) {
        if (empty) {
            x0 = x1 = x;
            y0 = y1 = y;
            empty = false;
            return;
        }
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
    }
    void Union(DRect const& o) {
        if (o.empty) {
            return;
        }
        Add(o.x0, o.y0);
        Add(o.x1, o.y1);
    }
    DRect Intersect(DRect const& o) const {
        if (empty || o.empty) {
            return {};
        }
        DRect r{std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1), false};
        r.empty = r.x1 <= r.x0 || r.y1 <= r.y0;
        return r;
    }
};

Pt Apply(Matrix const& m, double x, double y) {
    return {m.a * x + m.c * y + m.tx, m.b * x + m.d * y + m.ty};
}

DRect TransformRect(Rect const& r, Matrix const& m) {
    DRect out;
    if (r.Empty()) {
        return out;
    }
    for (auto const& [x, y] : {std::pair{r.xMin, r.yMin}, std::pair{r.xMax, r.yMin}, std::pair{r.xMin, r.yMax},
                               std::pair{r.xMax, r.yMax}}) {
        Pt const p = Apply(m, x, y);
        out.Add(p.x, p.y);
    }
    return out;
}

// ---- the scene: a character's frame flattened into shapes to draw and clip masks ----

struct Item {
    Shape const* shape = nullptr;
    Matrix m;
    ColorTransform cx;
    int mask = -1;
};

struct MaskSource {
    std::vector<std::pair<Shape const*, Matrix>> shapes;
    int parent = -1;
};

struct Scene {
    std::vector<Item> items;
    std::vector<MaskSource> masks;
    std::vector<std::unique_ptr<Shape>> morphs; // morph shapes made at their placements' ratios
};

struct Collector {
    Movie const& movie;
    bool playNested;
    RenderNotes& notes;
    Scene& scene;

    Frame const* FrameAt(Sprite const& sprite, int frame) const {
        if (sprite.frames.empty()) {
            return nullptr;
        }
        int const n = static_cast<int>(sprite.frames.size());
        int const f = playNested ? ((frame % n) + n) % n : std::clamp(frame, 0, n - 1);
        return &sprite.frames[static_cast<std::size_t>(f)];
    }

    Shape const* Morph(MorphShape const& morph, std::uint16_t ratio) {
        scene.morphs.push_back(std::make_unique<Shape>(MorphAt(morph, ratio / 65535.0)));
        return scene.morphs.back().get();
    }

    void Character(std::uint16_t id, int frame, std::uint16_t ratio, Matrix const& m, ColorTransform const& cx, int mask, int depth) {
        if (depth > kMaxNesting) {
            ++notes.depthLimited;
            return;
        }
        if (Shape const* s = movie.FindShape(id)) {
            scene.items.push_back({s, m, cx, mask});
            return;
        }
        if (MorphShape const* morph = movie.FindMorphShape(id)) {
            ++notes.morphShapes;
            scene.items.push_back({Morph(*morph, ratio), m, cx, mask});
            return;
        }
        if (Sprite const* sprite = movie.FindSprite(id)) {
            Frame const* f = FrameAt(*sprite, frame);
            if (f) {
                DisplayList(*f, frame, m, cx, mask, depth);
            }
            return;
        }
        switch (movie.KindOf(id)) {
        case CharacterKind::EditText: ++notes.editTexts; break;
        case CharacterKind::Text: ++notes.staticTexts; break;
        case CharacterKind::Button: ++notes.buttons; break;
        case CharacterKind::Imported: ++notes.imported; break;
        default: ++notes.missing; break;
        }
    }

    void DisplayList(Frame const& f, int frame, Matrix const& m, ColorTransform const& cx, int mask, int depth) {
        struct Clip {
            std::uint16_t until;
            int mask;
        };
        std::vector<Clip> clips;
        for (Placement const& p : f.displayList) {
            std::erase_if(clips, [&](Clip const& c) { return p.depth > c.until; });
            int const current = clips.empty() ? mask : clips.back().mask;
            if (!p.visible) {
                continue;
            }
            if (p.filterCount > 0) {
                ++notes.filters;
            }
            if (p.blendMode > 2) {
                ++notes.blendModes;
            }
            Matrix const cm = Multiply(m, p.matrix);
            int const childFrame = playNested ? std::max(0, frame - p.placedFrame) : 0;
            if (p.clipDepth > 0) {
                ++notes.clipLayers;
                MaskSource source;
                source.parent = current;
                MaskShapes(p.character, childFrame, p.ratio, cm, depth + 1, source.shapes);
                scene.masks.push_back(std::move(source));
                clips.push_back({p.clipDepth, static_cast<int>(scene.masks.size()) - 1});
                continue;
            }
            Character(p.character, childFrame, p.ratio, cm, Concat(cx, p.color), current, depth + 1);
        }
    }

    // A clip layer's shapes, whatever they paint (Flash masks with fills only, ignoring colour and alpha).
    void MaskShapes(std::uint16_t id, int frame, std::uint16_t ratio, Matrix const& m, int depth,
                    std::vector<std::pair<Shape const*, Matrix>>& out) {
        if (depth > kMaxNesting) {
            ++notes.depthLimited;
            return;
        }
        if (Shape const* s = movie.FindShape(id)) {
            out.emplace_back(s, m);
            return;
        }
        if (MorphShape const* morph = movie.FindMorphShape(id)) {
            out.emplace_back(Morph(*morph, ratio), m);
            return;
        }
        if (Sprite const* sprite = movie.FindSprite(id)) {
            if (Frame const* f = FrameAt(*sprite, frame)) {
                for (Placement const& p : f->displayList) {
                    if (p.visible && p.clipDepth == 0) {
                        MaskShapes(p.character, playNested ? std::max(0, frame - p.placedFrame) : 0, p.ratio,
                                   Multiply(m, p.matrix), depth + 1, out);
                    }
                }
            }
        }
    }
};

Scene Collect(Movie const& movie, std::uint16_t id, int frame, bool playNested, RenderNotes& notes,
              ColorTransform const& color = {}) {
    Scene scene;
    Collector c{movie, playNested, notes, scene};
    c.Character(id, frame, 0, Matrix{}, color, -1, 0);
    return scene;
}

DRect MaskBounds(Scene const& scene, int mask, std::vector<std::optional<DRect>>& memo, int depth = 0) {
    if (mask < 0 || mask >= static_cast<int>(scene.masks.size()) || depth > 64) {
        return {-1e18, -1e18, 1e18, 1e18, false};
    }
    if (memo[static_cast<std::size_t>(mask)]) {
        return *memo[static_cast<std::size_t>(mask)];
    }
    MaskSource const& source = scene.masks[static_cast<std::size_t>(mask)];
    DRect own;
    for (auto const& [shape, m] : source.shapes) {
        own.Union(TransformRect(shape->bounds, m));
    }
    DRect const r = own.Intersect(MaskBounds(scene, source.parent, memo, depth + 1));
    memo[static_cast<std::size_t>(mask)] = r;
    return r;
}

DRect SceneBounds(Scene const& scene) {
    std::vector<std::optional<DRect>> memo(scene.masks.size());
    DRect all;
    for (Item const& item : scene.items) {
        if (item.shape->edges.empty()) {
            continue;
        }
        all.Union(TransformRect(item.shape->bounds, item.m).Intersect(MaskBounds(scene, item.mask, memo)));
    }
    return all;
}

// ---- root space (twips) to the image: affine, or a nine-slice per axis ----

struct Axis {
    std::array<double, 4> src{};
    std::array<double, 4> dst{};
    std::array<double, 3> slope{};
    bool nine = false;
    double scale = 1;
    double offset = 0;

    static Axis Affine(double scale, double offset) {
        Axis a;
        a.scale = scale;
        a.offset = offset;
        return a;
    }

    static Axis Nine(std::array<double, 4> const& src, std::array<double, 4> const& dst, double edgeSlope) {
        Axis a;
        a.nine = true;
        a.src = src;
        a.dst = dst;
        for (int i = 0; i < 3; ++i) {
            double const s = src[static_cast<std::size_t>(i) + 1] - src[static_cast<std::size_t>(i)];
            double const d = dst[static_cast<std::size_t>(i) + 1] - dst[static_cast<std::size_t>(i)];
            a.slope[static_cast<std::size_t>(i)] = s > 1e-9 ? d / s : (i == 1 ? 0.0 : edgeSlope);
        }
        return a;
    }

    double Map(double v) const {
        if (!nine) {
            return v * scale + offset;
        }
        if (v < src[1]) {
            return dst[1] + (v - src[1]) * slope[0];
        }
        if (v <= src[2]) {
            return dst[1] + (v - src[1]) * slope[1];
        }
        return dst[2] + (v - src[2]) * slope[2];
    }

    double Unmap(double p) const {
        if (!nine) {
            return (p - offset) / scale;
        }
        if (p < dst[1]) {
            return slope[0] > 1e-12 ? src[1] + (p - dst[1]) / slope[0] : src[0];
        }
        if (p <= dst[2]) {
            return slope[1] > 1e-12 ? src[1] + (p - dst[1]) / slope[1] : src[1];
        }
        return slope[2] > 1e-12 ? src[2] + (p - dst[2]) / slope[2] : src[3];
    }

    double MaxSlope() const {
        return nine ? std::max({slope[0], slope[1], slope[2]}) : std::abs(scale);
    }
};

struct Warp {
    Axis x, y;
    double strokeScale = 1; // px per twip for stroke widths

    Pt Map(Pt p) const { return {x.Map(p.x), y.Map(p.y)}; }
    Pt Unmap(Pt p) const { return {x.Unmap(p.x), y.Unmap(p.y)}; }
    double MaxSlope() const { return std::max(x.MaxSlope(), y.MaxSlope()); }
};

// An edge's points in the image (start and end included): the curve flattened finely enough in root space for the
// image's largest scale, then split where a nine-slice bends so straight runs bend with it.
void EdgePoints(Edge const& e, Matrix const& m, Warp const& w, std::vector<Pt>& out) {
    out.clear();
    std::vector<Pt> root;
    Pt const a = Apply(m, e.x0, e.y0);
    Pt const b = Apply(m, e.x1, e.y1);
    root.push_back(a);
    if (e.curve) {
        Pt const c = Apply(m, e.cx, e.cy);
        double const ddx = a.x - 2 * c.x + b.x;
        double const ddy = a.y - 2 * c.y + b.y;
        double const err = std::hypot(ddx, ddy) * w.MaxSlope();
        int n = static_cast<int>(std::ceil(std::sqrt(err / (4 * kCurveTolerancePx))));
        n = std::clamp(n, 1, 1024);
        for (int i = 1; i < n; ++i) {
            double const t = static_cast<double>(i) / n;
            double const u = 1 - t;
            root.push_back({u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y});
        }
    }
    root.push_back(b);
    if (!w.x.nine && !w.y.nine) {
        for (Pt const& p : root) {
            out.push_back(w.Map(p));
        }
        return;
    }
    out.push_back(w.Map(root[0]));
    std::vector<double> ts;
    for (std::size_t i = 1; i < root.size(); ++i) {
        Pt const p = root[i - 1];
        Pt const q = root[i];
        ts.clear();
        for (int k = 1; k <= 2; ++k) {
            double const bx = w.x.src[static_cast<std::size_t>(k)];
            if ((p.x - bx) * (q.x - bx) < 0) {
                ts.push_back((bx - p.x) / (q.x - p.x));
            }
            double const by = w.y.src[static_cast<std::size_t>(k)];
            if ((p.y - by) * (q.y - by) < 0) {
                ts.push_back((by - p.y) / (q.y - p.y));
            }
        }
        std::sort(ts.begin(), ts.end());
        for (double const t : ts) {
            out.push_back(w.Map({p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t}));
        }
        out.push_back(w.Map(q));
    }
}

// ---- coverage: signed area accumulated per cell, then summed along each row ----

class Coverage {
public:
    Coverage(int x0, int y0, int w, int h)
        : x0_(x0), y0_(y0), w_(w), h_(h), stride_(w + 2), acc_(static_cast<std::size_t>(w + 2) * static_cast<std::size_t>(h), 0.0f) {}

    int X0() const { return x0_; }
    int Y0() const { return y0_; }
    int W() const { return w_; }
    int H() const { return h_; }

    void Line(Pt a, Pt b) {
        double ax = a.x - x0_, ay = a.y - y0_, bx = b.x - x0_, by = b.y - y0_;
        if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(bx) || !std::isfinite(by) || ay == by) {
            return;
        }
        double dir = 1;
        if (ay > by) {
            std::swap(ax, bx);
            std::swap(ay, by);
            dir = -1;
        }
        if (by <= 0 || ay >= h_) {
            return;
        }
        double const dxdy = (bx - ax) / (by - ay);
        double const top = std::max(ay, 0.0);
        double const bottom = std::min(by, static_cast<double>(h_));
        int const yStart = static_cast<int>(std::floor(top));
        int const yEnd = std::min(h_, static_cast<int>(std::ceil(bottom)));
        for (int y = yStart; y < yEnd; ++y) {
            double const r0 = std::max(static_cast<double>(y), top);
            double const r1 = std::min(static_cast<double>(y) + 1, bottom);
            double const dy = r1 - r0;
            if (dy <= 0) {
                continue;
            }
            double const xa = std::clamp(ax + (r0 - ay) * dxdy, 0.0, static_cast<double>(w_));
            double const xb = std::clamp(ax + (r1 - ay) * dxdy, 0.0, static_cast<double>(w_));
            Row(acc_.data() + static_cast<std::size_t>(y) * stride_, xa, xb, dy * dir);
        }
    }

    // Coverage 0..1 per pixel (nonzero: |winding| clamped), in place; At() reads it.
    void Resolve() {
        for (int y = 0; y < h_; ++y) {
            float* row = acc_.data() + static_cast<std::size_t>(y) * stride_;
            float sum = 0;
            for (int x = 0; x < w_; ++x) {
                sum += row[x];
                row[x] = std::min(1.0f, std::abs(sum));
            }
        }
    }

    float At(int x, int y) const { return acc_[static_cast<std::size_t>(y) * stride_ + x]; }

private:
    // One row's share of a segment spanning [xa, xb] across `d` of the row's height (signed): the area of each cell
    // to the right of the segment, written as differences so the row's running sum gives it back.
    static void Row(float* row, double xa, double xb, double d) {
        double const xl = std::min(xa, xb);
        double const xr = std::max(xa, xb);
        double const lf = std::floor(xl);
        int const il = static_cast<int>(lf);
        int const ir = static_cast<int>(std::ceil(xr));
        if (ir <= il + 1) {
            double const mid = 0.5 * (xa + xb) - lf;
            row[il] += static_cast<float>(d * (1 - mid));
            row[il + 1] += static_cast<float>(d * mid);
            return;
        }
        double const s = 1.0 / (xr - xl);
        double const fl = xl - lf;
        double const a0 = 0.5 * s * (1 - fl) * (1 - fl);
        double const fr = xr - (ir - 1);
        double const am = 0.5 * s * fr * fr;
        row[il] += static_cast<float>(d * a0);
        if (ir == il + 2) {
            row[il + 1] += static_cast<float>(d * (1 - a0 - am));
        } else {
            double const a1 = s * (1.5 - fl);
            row[il + 1] += static_cast<float>(d * (a1 - a0));
            for (int i = il + 2; i < ir - 1; ++i) {
                row[i] += static_cast<float>(d * s);
            }
            double const a2 = a1 + (ir - il - 3) * s;
            row[ir - 1] += static_cast<float>(d * (1 - a2 - am));
        }
        row[ir] += static_cast<float>(d * am);
    }

    int x0_, y0_, w_, h_, stride_;
    std::vector<float> acc_;
};

// A coverage buffer over the integer box around `box` inside the canvas, or nothing if it misses the canvas.
std::optional<Coverage> MakeCoverage(DRect const& box, int width, int height) {
    if (box.empty) {
        return std::nullopt;
    }
    int const x0 = std::max(0, static_cast<int>(std::floor(box.x0)));
    int const y0 = std::max(0, static_cast<int>(std::floor(box.y0)));
    int const x1 = std::min(width, static_cast<int>(std::ceil(box.x1)) + 1);
    int const y1 = std::min(height, static_cast<int>(std::ceil(box.y1)) + 1);
    if (x1 <= x0 || y1 <= y0) {
        return std::nullopt;
    }
    return Coverage(x0, y0, x1 - x0, y1 - y0);
}

// ---- paint ----

using Color = std::array<float, 4>; // premultiplied 0..1

float Channel(float v, float mult, float add) {
    return std::clamp(v * mult + add, 0.0f, 255.0f);
}

Color Transformed(ColorTransform const& cx, float r, float g, float b, float a) {
    float const alpha = Channel(a, cx.ma, cx.aa) / 255.0f;
    return {Channel(r, cx.mr, cx.ar) / 255.0f * alpha, Channel(g, cx.mg, cx.ag) / 255.0f * alpha,
            Channel(b, cx.mb, cx.ab) / 255.0f * alpha, alpha};
}

float ToLinear(float c) {
    c /= 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float ToSrgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return 255.0f * (c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
}

struct Paint {
    bool skip = false;
    bool gradient = false;
    Color solid{};
    FillKind kind = FillKind::Solid;
    std::uint8_t spread = 0;
    float focal = 0;
    Matrix toGradient; // root twips -> gradient square
    std::array<Color, 256> lut{};

    static Paint Of(FillStyle const& f, Matrix const& m, ColorTransform const& cx, RenderNotes& notes) {
        Paint p;
        p.kind = f.kind;
        if (f.kind == FillKind::Bitmap) {
            ++notes.bitmapFills;
            p.skip = true;
            return p;
        }
        if (f.kind == FillKind::Solid) {
            p.solid = Transformed(cx, f.color.r, f.color.g, f.color.b, f.color.a);
            return p;
        }
        if (f.stops.empty()) {
            p.skip = true;
            return p;
        }
        p.gradient = true;
        p.spread = f.spread;
        p.focal = f.focal;
        bool const linear = f.interpolation == 1;
        for (int i = 0; i < 256; ++i) {
            GradientStop const* lo = &f.stops.front();
            GradientStop const* hi = &f.stops.front();
            for (GradientStop const& s : f.stops) {
                if (s.ratio <= i) {
                    lo = &s;
                    hi = &s;
                } else {
                    hi = &s;
                    break;
                }
            }
            if (i < f.stops.front().ratio) {
                lo = hi = &f.stops.front();
            }
            float t = 0;
            if (hi != lo && hi->ratio > lo->ratio) {
                t = static_cast<float>(i - lo->ratio) / static_cast<float>(hi->ratio - lo->ratio);
            }
            auto mix = [&](std::uint8_t x, std::uint8_t y) {
                if (linear) {
                    return ToSrgb(ToLinear(x) + (ToLinear(y) - ToLinear(x)) * t);
                }
                return static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t;
            };
            float const a = static_cast<float>(lo->color.a) + (static_cast<float>(hi->color.a) - lo->color.a) * t;
            p.lut[static_cast<std::size_t>(i)] =
                Transformed(cx, mix(lo->color.r, hi->color.r), mix(lo->color.g, hi->color.g), mix(lo->color.b, hi->color.b), a);
        }
        if (auto inv = Invert(Multiply(m, f.matrix))) {
            p.toGradient = *inv;
        } else {
            p.gradient = false;
            p.solid = p.lut[255];
        }
        return p;
    }

    Color At(Warp const& w, double px, double py) const {
        if (!gradient) {
            return solid;
        }
        Pt const root = w.Unmap({px, py});
        Pt const g = Apply(toGradient, root.x, root.y);
        double t = 0;
        if (kind == FillKind::Linear) {
            t = (g.x + 16384.0) / 32768.0;
        } else if (kind == FillKind::Radial || focal == 0) {
            t = std::hypot(g.x, g.y) / 16384.0;
        } else {
            // Focal: the share of the way from the focal point to the circle, along the ray through the pixel.
            double const fx = focal * 16384.0;
            double const dx = g.x - fx;
            double const dy = g.y;
            double const dd = dx * dx + dy * dy;
            if (dd < 1e-12) {
                t = 0;
            } else {
                double const fd = fx * dx;
                double const c = fx * fx - 16384.0 * 16384.0;
                double const k = (-fd + std::sqrt(std::max(0.0, fd * fd - dd * c))) / dd;
                t = k > 1e-12 ? 1.0 / k : 1.0;
            }
        }
        if (spread == 1) {
            t = std::fmod(std::abs(t), 2.0);
            if (t > 1) {
                t = 2 - t;
            }
        } else if (spread == 2) {
            t -= std::floor(t);
        } else {
            t = std::clamp(t, 0.0, 1.0);
        }
        if (!std::isfinite(t)) {
            t = 0;
        }
        return lut[static_cast<std::size_t>(std::clamp(static_cast<int>(t * 255.0 + 0.5), 0, 255))];
    }
};

// A clip mask's coverage over its own box (nothing outside it shows).
struct MaskBuf {
    bool built = false;
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<float> cov;

    float At(int x, int y) const {
        x -= x0;
        y -= y0;
        if (x < 0 || y < 0 || x >= w || y >= h) {
            return 0.0f;
        }
        return cov[static_cast<std::size_t>(y) * w + x];
    }
};

struct Canvas {
    int width = 0;
    int height = 0;
    std::vector<float> rgba; // premultiplied

    void Fill(Coverage const& cov, Paint const& paint, MaskBuf const* mask, Warp const& w) {
        if (paint.skip) {
            return;
        }
        for (int y = 0; y < cov.H(); ++y) {
            int const cy = cov.Y0() + y;
            for (int x = 0; x < cov.W(); ++x) {
                float c = cov.At(x, y);
                int const cx = cov.X0() + x;
                std::size_t const index = static_cast<std::size_t>(cy) * width + cx;
                if (mask) {
                    c *= mask->At(cx, cy);
                }
                if (c <= 1.0f / 1024.0f) {
                    continue;
                }
                Color const src = paint.At(w, cx + 0.5, cy + 0.5);
                float* d = rgba.data() + index * 4;
                float const keep = 1 - src[3] * c;
                for (int k = 0; k < 4; ++k) {
                    d[k] = src[static_cast<std::size_t>(k)] * c + d[k] * keep;
                }
            }
        }
    }
};

// One style table's fills, added up before they meet the canvas: a shape's fills partition the plane, so along an
// edge two styles share a pixel's coverage (a + (1 - a) = 1) instead of each blending over the other and leaving a
// see-through seam.
struct Layer {
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<float> rgba;

    Layer(int x0_, int y0_, int w_, int h_) : x0(x0_), y0(y0_), w(w_), h(h_), rgba(static_cast<std::size_t>(w_) * h_ * 4, 0.0f) {}

    void Add(Coverage const& cov, Paint const& paint, Warp const& warp) {
        for (int y = 0; y < cov.H(); ++y) {
            int const cy = cov.Y0() + y;
            for (int x = 0; x < cov.W(); ++x) {
                float const c = cov.At(x, y);
                if (c <= 1.0f / 1024.0f) {
                    continue;
                }
                int const cx = cov.X0() + x;
                Color const src = paint.At(warp, cx + 0.5, cy + 0.5);
                float* d = rgba.data() + (static_cast<std::size_t>(cy - y0) * w + (cx - x0)) * 4;
                for (int k = 0; k < 4; ++k) {
                    d[k] += src[static_cast<std::size_t>(k)] * c;
                }
            }
        }
    }

    void Over(Canvas& canvas, MaskBuf const* mask) const {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float const* s = rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
                std::size_t const index = static_cast<std::size_t>(y0 + y) * canvas.width + (x0 + x);
                float const m = mask ? mask->At(x0 + x, y0 + y) : 1.0f;
                float const a = std::min(1.0f, s[3]) * m;
                if (a <= 0) {
                    continue;
                }
                float* d = canvas.rgba.data() + index * 4;
                float const keep = 1 - a;
                for (int k = 0; k < 3; ++k) {
                    d[k] = std::min(s[k] * m, a) + d[k] * keep;
                }
                d[3] = a + d[3] * keep;
            }
        }
    }
};

// ---- strokes: quads per segment plus joins and caps, all wound the same way so nonzero is their union ----

class StrokeBuilder {
public:
    explicit StrokeBuilder(std::vector<std::vector<Pt>>& polygons) : polygons_(polygons) {}

    void Polyline(std::vector<Pt> pts, double hw, LineStyle const& style, bool roundOnly) {
        pts.erase(std::unique(pts.begin(), pts.end(),
                              [](Pt const& a, Pt const& b) { return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9; }),
                  pts.end());
        std::uint8_t const join = roundOnly ? 0 : style.join;
        std::uint8_t const startCap = roundOnly ? 0 : style.startCap;
        std::uint8_t const endCap = roundOnly ? 0 : style.endCap;
        if (pts.empty()) {
            return;
        }
        if (pts.size() == 1) {
            if (startCap == 0) {
                Circle(pts[0], hw);
            } else if (startCap == 2) {
                Pt const p = pts[0];
                polygons_.push_back({{p.x - hw, p.y - hw}, {p.x + hw, p.y - hw}, {p.x + hw, p.y + hw}, {p.x - hw, p.y + hw}});
            }
            return;
        }
        bool const closed = pts.size() > 2 && std::abs(pts.front().x - pts.back().x) < 1e-6 &&
                            std::abs(pts.front().y - pts.back().y) < 1e-6 && !style.noClose;
        std::size_t const n = pts.size();
        for (std::size_t i = 0; i + 1 < n; ++i) {
            Pt a = pts[i];
            Pt b = pts[i + 1];
            Pt const d = Unit(a, b);
            if (!closed && i == 0 && startCap == 2) {
                a = {a.x - d.x * hw, a.y - d.y * hw};
            }
            if (!closed && i + 2 == n && endCap == 2) {
                b = {b.x + d.x * hw, b.y + d.y * hw};
            }
            Pt const nr{-d.y * hw, d.x * hw};
            polygons_.push_back({{a.x + nr.x, a.y + nr.y}, {b.x + nr.x, b.y + nr.y}, {b.x - nr.x, b.y - nr.y}, {a.x - nr.x, a.y - nr.y}});
        }
        for (std::size_t i = 1; i + 1 < n; ++i) {
            Join(pts[i], Unit(pts[i - 1], pts[i]), Unit(pts[i], pts[i + 1]), hw, join, style.miterLimit);
        }
        if (closed) {
            Join(pts[0], Unit(pts[n - 2], pts[n - 1]), Unit(pts[0], pts[1]), hw, join, style.miterLimit);
        } else {
            if (startCap == 0) {
                Circle(pts.front(), hw);
            }
            if (endCap == 0) {
                Circle(pts.back(), hw);
            }
        }
    }

private:
    static Pt Unit(Pt a, Pt b) {
        double const dx = b.x - a.x;
        double const dy = b.y - a.y;
        double const len = std::hypot(dx, dy);
        return len > 1e-12 ? Pt{dx / len, dy / len} : Pt{1, 0};
    }

    void Circle(Pt c, double r) {
        double const step = std::acos(std::clamp(1 - kRoundTolerancePx / r, -1.0, 1.0));
        int n = step > 1e-6 ? static_cast<int>(std::ceil(std::numbers::pi / step)) : 96;
        n = std::clamp(n, 8, 96);
        std::vector<Pt> poly;
        poly.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            double const a = 2 * std::numbers::pi * i / n;
            poly.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
        }
        polygons_.push_back(std::move(poly));
    }

    void Join(Pt v, Pt d0, Pt d1, double hw, std::uint8_t join, float miterLimit) {
        if (join == 0) {
            Circle(v, hw);
            return;
        }
        Pt const n0{-d0.y * hw, d0.x * hw};
        Pt const n1{-d1.y * hw, d1.x * hw};
        double const cross = d0.x * d1.y - d0.y * d1.x;
        double const outer = cross > 0 ? -1.0 : 1.0;
        Pt const a{v.x + outer * n0.x, v.y + outer * n0.y};
        Pt const b{v.x + outer * n1.x, v.y + outer * n1.y};
        double const dot = d0.x * d1.x + d0.y * d1.y;
        if (join == 2 && 1 + dot > 1e-9) {
            double const cosHalf = std::sqrt((1 + dot) / 2);
            Pt const mid{n0.x + n1.x, n0.y + n1.y};
            double const midLen = std::hypot(mid.x, mid.y);
            if (cosHalf > 1e-9 && 1 / cosHalf <= std::max(1.0f, miterLimit) && midLen > 1e-12) {
                double const reach = hw / cosHalf;
                Pt const tip{v.x + outer * mid.x / midLen * reach, v.y + outer * mid.y / midLen * reach};
                polygons_.push_back({v, a, tip, b});
                return;
            }
        }
        polygons_.push_back({v, a, b});
    }

    std::vector<std::vector<Pt>>& polygons_;
};

// ---- drawing a scene ----

class Renderer {
public:
    Renderer(Scene const& scene, int width, int height, Warp const& warp, RenderNotes& notes)
        : scene_(scene), warp_(warp), notes_(notes), masks_(scene.masks.size()), lastUse_(scene.masks.size(), -1) {
        canvas_.width = width;
        canvas_.height = height;
        canvas_.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0.0f);
        // A mask is freed after the last item it clips; a parent lives as long as its children, which are built
        // from it when first used.
        for (std::size_t k = 0; k < scene.items.size(); ++k) {
            int const m = scene.items[k].mask;
            if (m >= 0 && m < static_cast<int>(lastUse_.size())) {
                lastUse_[static_cast<std::size_t>(m)] = static_cast<int>(k);
            }
        }
        for (std::size_t i = scene.masks.size(); i-- > 0;) {
            int const p = scene.masks[i].parent;
            if (p >= 0 && p < static_cast<int>(i)) {
                lastUse_[static_cast<std::size_t>(p)] = std::max(lastUse_[static_cast<std::size_t>(p)], lastUse_[i]);
            }
        }
    }

    void Draw() {
        for (std::size_t k = 0; k < scene_.items.size(); ++k) {
            DrawItem(scene_.items[k]);
            for (std::size_t i = 0; i < masks_.size(); ++i) {
                if (lastUse_[i] == static_cast<int>(k) && masks_[i].built) {
                    masks_[i].cov = {};
                    masks_[i].w = masks_[i].h = 0;
                }
            }
        }
    }

    Image ToImage() const {
        Image img;
        img.width = canvas_.width;
        img.height = canvas_.height;
        img.pixels.resize(static_cast<std::size_t>(img.width) * img.height);
        for (std::size_t i = 0; i < img.pixels.size(); ++i) {
            float const* p = canvas_.rgba.data() + i * 4;
            float const a = std::clamp(p[3], 0.0f, 1.0f);
            auto byte = [&](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, a) * 255.0f)); };
            std::uint32_t const ab = static_cast<std::uint32_t>(std::lround(a * 255.0f));
            img.pixels[i] = byte(p[0]) | (byte(p[1]) << 8) | (byte(p[2]) << 16) | (ab << 24);
        }
        return img;
    }

private:
    // A clip layer's coverage: the union of its shapes' fills, times its parent clip's (nested clip layers intersect).
    MaskBuf const* Mask(int index, int depth = 0) {
        if (index < 0 || index >= static_cast<int>(masks_.size()) || depth > 64) {
            return nullptr;
        }
        MaskBuf& slot = masks_[static_cast<std::size_t>(index)];
        if (slot.built) {
            return &slot;
        }
        slot.built = true;
        MaskSource const& source = scene_.masks[static_cast<std::size_t>(index)];
        std::vector<Coverage> parts;
        std::vector<Pt> pts;
        for (auto const& [shape, m] : source.shapes) {
            for (std::size_t table = 0; table < shape->styles.size(); ++table) {
                std::vector<Seg> segs;
                DRect box;
                for (Edge const& e : shape->edges) {
                    bool const left = e.fill0 != 0;
                    bool const right = e.fill1 != 0;
                    if (e.table != table || left == right) {
                        continue;
                    }
                    EdgePoints(e, m, warp_, pts);
                    if (left) {
                        std::reverse(pts.begin(), pts.end());
                    }
                    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
                        segs.push_back({pts[i], pts[i + 1]});
                        box.Add(pts[i].x, pts[i].y);
                    }
                    if (!pts.empty()) {
                        box.Add(pts.back().x, pts.back().y);
                    }
                }
                auto cov = MakeCoverage(box, canvas_.width, canvas_.height);
                if (!cov) {
                    continue;
                }
                for (Seg const& s : segs) {
                    cov->Line(s.a, s.b);
                }
                cov->Resolve();
                parts.push_back(std::move(*cov));
            }
        }
        if (parts.empty()) {
            return &slot; // an empty box: everything it clips is hidden
        }
        int x0 = parts[0].X0(), y0 = parts[0].Y0(), x1 = x0 + parts[0].W(), y1 = y0 + parts[0].H();
        for (Coverage const& c : parts) {
            x0 = std::min(x0, c.X0());
            y0 = std::min(y0, c.Y0());
            x1 = std::max(x1, c.X0() + c.W());
            y1 = std::max(y1, c.Y0() + c.H());
        }
        slot.x0 = x0;
        slot.y0 = y0;
        slot.w = x1 - x0;
        slot.h = y1 - y0;
        slot.cov.assign(static_cast<std::size_t>(slot.w) * slot.h, 0.0f);
        for (Coverage const& c : parts) {
            for (int y = 0; y < c.H(); ++y) {
                for (int x = 0; x < c.W(); ++x) {
                    float& cell = slot.cov[static_cast<std::size_t>(c.Y0() + y - y0) * slot.w + (c.X0() + x - x0)];
                    cell = std::max(cell, c.At(x, y));
                }
            }
        }
        if (source.parent >= 0) {
            MaskBuf const* parent = Mask(source.parent, depth + 1);
            for (int y = 0; y < slot.h; ++y) {
                for (int x = 0; x < slot.w; ++x) {
                    slot.cov[static_cast<std::size_t>(y) * slot.w + x] *= parent ? parent->At(x0 + x, y0 + y) : 1.0f;
                }
            }
        }
        return &slot;
    }

    void FillSegments(std::vector<Seg> const& segs, Paint const& paint, MaskBuf const* mask) {
        if (segs.empty() || paint.skip) {
            return;
        }
        DRect box;
        for (Seg const& s : segs) {
            box.Add(s.a.x, s.a.y);
            box.Add(s.b.x, s.b.y);
        }
        auto cov = MakeCoverage(box, canvas_.width, canvas_.height);
        if (!cov) {
            return;
        }
        for (Seg const& s : segs) {
            cov->Line(s.a, s.b);
        }
        cov->Resolve();
        canvas_.Fill(*cov, paint, mask, warp_);
    }

    void FlushTable(Item const& item, StyleTable const& table, std::vector<std::vector<Seg>>& fills,
                    std::vector<std::vector<std::vector<Pt>>>& lines, MaskBuf const* mask) {
        DRect all;
        for (std::size_t i = 1; i < fills.size(); ++i) {
            for (Seg const& s : fills[i]) {
                all.Add(s.a.x, s.a.y);
                all.Add(s.b.x, s.b.y);
            }
        }
        if (auto area = MakeCoverage(all, canvas_.width, canvas_.height)) {
            Layer layer(area->X0(), area->Y0(), area->W(), area->H());
            bool painted = false;
            for (std::size_t i = 1; i < fills.size(); ++i) {
                if (fills[i].empty()) {
                    continue;
                }
                Paint const paint = Paint::Of(table.fills[i - 1], item.m, item.cx, notes_);
                if (paint.skip) {
                    continue;
                }
                DRect box;
                for (Seg const& s : fills[i]) {
                    box.Add(s.a.x, s.a.y);
                    box.Add(s.b.x, s.b.y);
                }
                auto cov = MakeCoverage(box, canvas_.width, canvas_.height);
                if (!cov) {
                    continue;
                }
                for (Seg const& s : fills[i]) {
                    cov->Line(s.a, s.b);
                }
                cov->Resolve();
                layer.Add(*cov, paint, warp_);
                painted = true;
            }
            if (painted) {
                layer.Over(canvas_, mask);
            }
        }
        for (auto& bucket : fills) {
            bucket.clear();
        }
        double const det = std::sqrt(std::abs(item.m.a * item.m.d - item.m.b * item.m.c));
        for (std::size_t i = 1; i < lines.size(); ++i) {
            if (lines[i].empty()) {
                continue;
            }
            LineStyle const& style = table.lines[i - 1];
            double const widthPx = std::max(1.0, style.width * warp_.strokeScale * det);
            std::vector<std::vector<Pt>> polygons;
            StrokeBuilder builder(polygons);
            for (auto& polyline : lines[i]) {
                builder.Polyline(std::move(polyline), widthPx / 2, style, item.shape->version < 4);
            }
            lines[i].clear();
            std::vector<Seg> segs;
            for (auto const& poly : polygons) {
                double area = 0;
                for (std::size_t k = 0; k < poly.size(); ++k) {
                    Pt const& p = poly[k];
                    Pt const& q = poly[(k + 1) % poly.size()];
                    area += p.x * q.y - q.x * p.y;
                }
                if (std::abs(area) < 1e-12) {
                    continue;
                }
                for (std::size_t k = 0; k < poly.size(); ++k) {
                    Pt const& p = poly[k];
                    Pt const& q = poly[(k + 1) % poly.size()];
                    segs.push_back(area > 0 ? Seg{p, q} : Seg{q, p});
                }
            }
            FillStyle solid;
            solid.color = style.color;
            FillSegments(segs, Paint::Of(style.hasFill ? style.fill : solid, item.m, item.cx, notes_), mask);
        }
    }

    void DrawItem(Item const& item) {
        Shape const& shape = *item.shape;
        if (shape.edges.empty() || shape.styles.empty()) {
            return;
        }
        ++notes_.shapes;
        MaskBuf const* mask = item.mask >= 0 ? Mask(item.mask) : nullptr;
        std::size_t table = shape.edges.front().table;
        std::vector<std::vector<Seg>> fills;
        std::vector<std::vector<std::vector<Pt>>> lines;
        auto reset = [&](std::size_t t) {
            fills.assign(t < shape.styles.size() ? shape.styles[t].fills.size() + 1 : 1, {});
            lines.assign(t < shape.styles.size() ? shape.styles[t].lines.size() + 1 : 1, {});
        };
        reset(table);
        std::vector<Pt> pts;
        std::uint16_t lastLine = 0;
        std::int32_t lastX = 0, lastY = 0;
        for (Edge const& e : shape.edges) {
            if (e.table != table) {
                if (table < shape.styles.size()) {
                    FlushTable(item, shape.styles[table], fills, lines, mask);
                }
                table = e.table;
                reset(table);
                lastLine = 0;
            }
            bool const fillEdge = e.fill0 != e.fill1;
            bool const lineEdge = e.line != 0 && e.line < lines.size();
            if (!fillEdge && !lineEdge) {
                lastLine = 0;
                continue;
            }
            EdgePoints(e, item.m, warp_, pts);
            if (fillEdge) {
                if (e.fill1 != 0 && e.fill1 < fills.size()) {
                    auto& bucket = fills[e.fill1];
                    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
                        bucket.push_back({pts[i], pts[i + 1]});
                    }
                }
                if (e.fill0 != 0 && e.fill0 < fills.size()) {
                    auto& bucket = fills[e.fill0];
                    for (std::size_t i = pts.size(); i-- > 1;) {
                        bucket.push_back({pts[i], pts[i - 1]});
                    }
                }
            }
            if (lineEdge) {
                auto& polylines = lines[e.line];
                if (lastLine == e.line && lastX == e.x0 && lastY == e.y0 && !polylines.empty()) {
                    polylines.back().insert(polylines.back().end(), pts.begin() + 1, pts.end());
                } else {
                    polylines.push_back(pts);
                }
                lastLine = e.line;
                lastX = e.x1;
                lastY = e.y1;
            } else {
                lastLine = 0;
            }
        }
        if (table < shape.styles.size()) {
            FlushTable(item, shape.styles[table], fills, lines, mask);
        }
    }

    Scene const& scene_;
    Warp warp_;
    RenderNotes& notes_;
    Canvas canvas_;
    std::vector<MaskBuf> masks_;
    std::vector<int> lastUse_;
};

RenderNotes& NotesOr(RenderNotes* notes, RenderNotes& local) {
    return notes ? *notes : local;
}

Image Draw(Scene const& scene, int width, int height, Warp const& warp, RenderNotes& notes) {
    Renderer r(scene, width, height, warp, notes);
    r.Draw();
    return r.ToImage();
}

} // namespace

std::optional<Rect> Bounds(Movie const& movie, std::uint16_t id, int frame, bool playNested) {
    RenderNotes notes;
    Scene const scene = Collect(movie, id, frame, playNested, notes);
    DRect const b = SceneBounds(scene);
    if (b.empty) {
        return std::nullopt;
    }
    auto clampTwips = [](double v) { return static_cast<std::int32_t>(std::clamp(v, -1e9, 1e9)); };
    return Rect{clampTwips(std::floor(b.x0)), clampTwips(std::ceil(b.x1)), clampTwips(std::floor(b.y0)),
                clampTwips(std::ceil(b.y1))};
}

Image Render(Movie const& movie, std::uint16_t id, int frame, float scale, RenderNotes* notes) {
    RenderOptions options;
    options.notes = notes;
    return Render(movie, id, frame, scale, options);
}

Image Render(Movie const& movie, std::uint16_t id, int frame, float scale, RenderOptions const& options) {
    RenderNotes local;
    RenderNotes& notes = NotesOr(options.notes, local);
    if (!(scale > 0) || !std::isfinite(scale)) {
        return {};
    }
    Scene const scene = Collect(movie, id, frame, options.playNested, notes, options.color);
    DRect const b = SceneBounds(scene);
    if (b.empty) {
        return {};
    }
    int const pad = std::clamp(options.pad, 0, 256);
    double const k = scale / 20.0;
    double const left = std::floor(b.x0 * k);
    double const top = std::floor(b.y0 * k);
    double const w = std::ceil(b.x1 * k) - left + 2.0 * pad;
    double const h = std::ceil(b.y1 * k) - top + 2.0 * pad;
    if (!(w >= 1 && h >= 1) || w > kMaxSide || h > kMaxSide || w * h > kMaxPixels) {
        notes.tooLarge = w > kMaxSide || h > kMaxSide || w * h > kMaxPixels;
        return {};
    }
    Warp warp;
    warp.x = Axis::Affine(k, pad - left);
    warp.y = Axis::Affine(k, pad - top);
    warp.strokeScale = k;
    Image img = Draw(scene, static_cast<int>(w), static_cast<int>(h), warp, notes);
    img.pxPerTwip = k;
    img.originX = (left - pad) / k;
    img.originY = (top - pad) / k;
    return img;
}

Image RenderFit(Movie const& movie, std::uint16_t id, int frame, int width, int height, Insets insets, FitOptions const& options) {
    RenderNotes local;
    RenderNotes& notes = NotesOr(options.notes, local);
    if (width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide || static_cast<double>(width) * height > kMaxPixels) {
        notes.tooLarge = width > 0 && height > 0;
        return {};
    }
    Scene const scene = Collect(movie, id, frame, options.playNested, notes, options.color);
    DRect box;
    if (options.source && !options.source->Empty()) {
        box = {static_cast<double>(options.source->xMin), static_cast<double>(options.source->yMin),
               static_cast<double>(options.source->xMax), static_cast<double>(options.source->yMax), false};
    } else {
        box = SceneBounds(scene);
    }
    if (box.empty) {
        return {};
    }
    // The borders in twips: the sprite's scaling grid, else the given insets.
    double l = insets.left * 20.0, t = insets.top * 20.0, r = insets.right * 20.0, bt = insets.bottom * 20.0;
    if (Sprite const* sprite = movie.FindSprite(id); sprite && sprite->scalingGrid && !sprite->scalingGrid->Empty()) {
        Rect const& g = *sprite->scalingGrid;
        l = g.xMin - box.x0;
        t = g.yMin - box.y0;
        r = box.x1 - g.xMax;
        bt = box.y1 - g.yMax;
    }
    double const sw = box.x1 - box.x0;
    double const sh = box.y1 - box.y0;
    auto fitPair = [](double& a, double& b, double span) {
        a = std::max(0.0, std::isfinite(a) ? a : 0.0);
        b = std::max(0.0, std::isfinite(b) ? b : 0.0);
        if (a + b > span && a + b > 0) {
            double const f = span / (a + b);
            a *= f;
            b *= f;
        }
    };
    fitPair(l, r, sw);
    fitPair(t, bt, sh);
    double const corner = std::max(1e-3, static_cast<double>(options.cornerScale)) / 20.0; // px per twip
    double lp = l * corner, rp = r * corner, tp = t * corner, bp = bt * corner;
    fitPair(lp, rp, width);
    fitPair(tp, bp, height);
    Warp warp;
    warp.x = Axis::Nine({box.x0, box.x0 + l, box.x1 - r, box.x1}, {0, lp, width - rp, static_cast<double>(width)}, corner);
    warp.y = Axis::Nine({box.y0, box.y0 + t, box.y1 - bt, box.y1}, {0, tp, height - bp, static_cast<double>(height)}, corner);
    warp.strokeScale = corner;
    return Draw(scene, width, height, warp, notes);
}

std::vector<std::uint32_t> Unpremultiplied(Image const& image) {
    std::vector<std::uint32_t> out(image.pixels.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        std::uint32_t const p = image.pixels[i];
        std::uint32_t const a = Alpha(p);
        if (a == 0) {
            continue;
        }
        auto channel = [&](std::uint32_t v) { return std::min<std::uint32_t>(255u, (v * 255u + a / 2) / a); };
        out[i] = channel(Red(p)) | (channel(Green(p)) << 8) | (channel(Blue(p)) << 16) | (a << 24);
    }
    return out;
}

} // namespace mevr::gfx
