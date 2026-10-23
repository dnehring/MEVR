#include "plugin/menu/MenuDecorators.hpp"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Decorator.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/RenderBox.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace mevr::menu {
namespace {

using Rml::Colourb;
using Rml::ColourbPremultiplied;
using Rml::Vector2f;

constexpr float kPi = 3.14159265358979f;
constexpr float kSqrt2 = 1.41421356f;

ColourbPremultiplied Premultiply(Colourb c, float opacity) {
    return c.ToPremultiplied(opacity);
}

ColourbPremultiplied Lerp(ColourbPremultiplied a, ColourbPremultiplied b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto mix = [t](Rml::byte x, Rml::byte y) {
        return static_cast<Rml::byte>(std::lround(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t));
    };
    return ColourbPremultiplied(mix(a.red, b.red), mix(a.green, b.green), mix(a.blue, b.blue), mix(a.alpha, b.alpha));
}

// ---- Geometry helpers ------------------------------------------------------------------------------------
struct Builder {
    Rml::Mesh mesh;

    int Vertex(Vector2f p, ColourbPremultiplied c, Vector2f uv = {}) {
        mesh.vertices.push_back({p, c, uv});
        return static_cast<int>(mesh.vertices.size()) - 1;
    }
    void Triangle(int a, int b, int c) {
        mesh.indices.push_back(a);
        mesh.indices.push_back(b);
        mesh.indices.push_back(c);
    }
    // A convex polygon, a vertical gradient from `top` (y0) to `bottom` (y1).
    void Fan(std::vector<Vector2f> const& points, ColourbPremultiplied top, ColourbPremultiplied bottom, float y0, float y1) {
        if (points.size() < 3) {
            return;
        }
        float const span = std::max(y1 - y0, 1e-3f);
        int const first = static_cast<int>(mesh.vertices.size());
        for (Vector2f const& p : points) {
            Vertex(p, Lerp(top, bottom, (p.y - y0) / span));
        }
        for (int i = 1; i + 1 < static_cast<int>(points.size()); ++i) {
            Triangle(first, first + i, first + i + 1);
        }
    }
    // The ring between two polygons with the same number of corresponding vertices (outer, inner).
    void Ring(std::vector<Vector2f> const& outer, std::vector<Vector2f> const& inner, ColourbPremultiplied top,
        ColourbPremultiplied bottom, float y0, float y1) {
        if (outer.size() != inner.size() || outer.size() < 3) {
            return;
        }
        float const span = std::max(y1 - y0, 1e-3f);
        int const n = static_cast<int>(outer.size());
        int const first = static_cast<int>(mesh.vertices.size());
        for (int i = 0; i < n; ++i) {
            Vertex(outer[i], Lerp(top, bottom, (outer[i].y - y0) / span));
            Vertex(inner[i], Lerp(top, bottom, (inner[i].y - y0) / span));
        }
        for (int i = 0; i < n; ++i) {
            int const j = (i + 1) % n;
            int const o0 = first + 2 * i, i0 = o0 + 1, o1 = first + 2 * j, i1 = o1 + 1;
            Triangle(o0, o1, i1);
            Triangle(o0, i1, i0);
        }
    }
    void Quad(Vector2f a, Vector2f b, Vector2f c, Vector2f d, ColourbPremultiplied colour) {
        int const i0 = Vertex(a, colour), i1 = Vertex(b, colour), i2 = Vertex(c, colour), i3 = Vertex(d, colour);
        Triangle(i0, i1, i2);
        Triangle(i0, i2, i3);
    }
    // A stroked polyline with mitred joins (limit 2) and butt ends.
    void Stroke(std::vector<Vector2f> const& points, bool closed, float width, ColourbPremultiplied colour) {
        int const n = static_cast<int>(points.size());
        if (n < 2) {
            return;
        }
        float const half = width * 0.5f;
        auto normal = [](Vector2f a, Vector2f b) {
            Vector2f const d = b - a;
            float const length = std::sqrt(d.x * d.x + d.y * d.y);
            return length > 1e-6f ? Vector2f(-d.y / length, d.x / length) : Vector2f(0.0f, 0.0f);
        };
        int const first = static_cast<int>(mesh.vertices.size());
        for (int i = 0; i < n; ++i) {
            bool const hasPrev = closed || i > 0;
            bool const hasNext = closed || i + 1 < n;
            Vector2f const p = points[i];
            Vector2f const prev = points[(i - 1 + n) % n];
            Vector2f const next = points[(i + 1) % n];
            Vector2f nIn = hasPrev ? normal(prev, p) : normal(p, next);
            Vector2f nOut = hasNext ? normal(p, next) : nIn;
            Vector2f m = nIn + nOut;
            float const ml = std::sqrt(m.x * m.x + m.y * m.y);
            float scale = half;
            if (ml > 1e-4f) {
                m = m / ml;
                float const cosine = m.x * nOut.x + m.y * nOut.y;
                scale = half / std::max(cosine, 0.5f);  // miter limit 2
            } else {
                m = nOut;
            }
            Vertex(p + m * scale, colour);
            Vertex(p - m * scale, colour);
        }
        int const segments = closed ? n : n - 1;
        for (int i = 0; i < segments; ++i) {
            int const j = (i + 1) % n;
            int const a0 = first + 2 * i, a1 = a0 + 1, b0 = first + 2 * j, b1 = b0 + 1;
            Triangle(a0, b0, b1);
            Triangle(a0, b1, a1);
        }
    }
};

// A box with its top-left and bottom-right corners cut, inset by `d` (45-degree cuts stay 45 degrees).
std::vector<Vector2f> ChamferPolygon(Vector2f o, Vector2f size, float cut, float d) {
    float const w = size.x;
    float const h = size.y;
    cut = std::clamp(cut, 0.0f, std::min(w, h) * 0.5f);
    if (cut <= 0.0f) {
        return {o + Vector2f(d, d), o + Vector2f(w - d, d), o + Vector2f(w - d, h - d), o + Vector2f(d, h - d)};
    }
    float const k = cut + d * (kSqrt2 - 1.0f);
    return {o + Vector2f(k, d), o + Vector2f(w - d, d), o + Vector2f(w - d, h - k), o + Vector2f(w - k, h - d),
        o + Vector2f(d, h - d), o + Vector2f(d, k)};
}

// ---- chamfer(...) -----------------------------------------------------------------------------------------
struct ChamferIds {
    Rml::PropertyId cut, fillTop, fillBottom, edgeTop, edgeBottom, edgeWidth, highlight;
};

class ChamferDecorator final : public Rml::Decorator {
public:
    Rml::NumericValue cut;
    Rml::NumericValue edgeWidth;
    Colourb fillTop, fillBottom, edgeTop, edgeBottom, highlight;

    Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element, Rml::BoxArea area) const override {
        Rml::RenderBox const box = element->GetRenderBox(area);
        Vector2f const o = box.GetFillOffset();
        Vector2f const size = box.GetFillSize();
        if (size.x <= 0.0f || size.y <= 0.0f) {
            return Rml::Decorator::INVALID_DECORATORDATAHANDLE;
        }
        float const opacity = element->GetComputedValues().opacity();
        float const c = element->ResolveLength(cut);
        float const e = std::max(0.0f, element->ResolveLength(edgeWidth));
        Builder b;
        float const y0 = o.y;
        float const y1 = o.y + size.y;
        std::vector<Vector2f> const outer = ChamferPolygon(o, size, c, 0.0f);
        bool const edged = e > 0.0f && (edgeTop.alpha > 0 || edgeBottom.alpha > 0);
        std::vector<Vector2f> const inner = edged ? ChamferPolygon(o, size, c, e) : outer;
        if (fillTop.alpha > 0 || fillBottom.alpha > 0) {
            b.Fan(inner, Premultiply(fillTop, opacity), Premultiply(fillBottom, opacity), y0, y1);
        }
        if (edged) {
            b.Ring(outer, inner, Premultiply(edgeTop, opacity), Premultiply(edgeBottom, opacity), y0, y1);
        }
        if (highlight.alpha > 0) {
            float const top = o.y + (edged ? e : 0.0f);
            float const x0 = inner[0].x;
            float const x1 = inner[1].x;
            ColourbPremultiplied const h = Premultiply(highlight, opacity);
            b.Quad({x0, top}, {x1, top}, {x1, top + 1.0f}, {x0, top + 1.0f}, h);
        }
        if (b.mesh.indices.empty()) {
            return Rml::Decorator::INVALID_DECORATORDATAHANDLE;
        }
        auto* geometry = new Rml::Geometry(element->GetRenderManager()->MakeGeometry(std::move(b.mesh)));
        return reinterpret_cast<Rml::DecoratorDataHandle>(geometry);
    }
    void ReleaseElementData(Rml::DecoratorDataHandle data) const override {
        delete reinterpret_cast<Rml::Geometry*>(data);
    }
    void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle data) const override {
        reinterpret_cast<Rml::Geometry*>(data)->Render(element->GetAbsoluteOffset(Rml::BoxArea::Border));
    }
};

class ChamferInstancer final : public Rml::DecoratorInstancer {
public:
    ChamferInstancer() {
        ids_.cut = RegisterProperty("cut", "0px").AddParser("length").GetId();
        ids_.fillTop = RegisterProperty("fill-top", "transparent").AddParser("color").GetId();
        ids_.fillBottom = RegisterProperty("fill-bottom", "transparent").AddParser("color").GetId();
        ids_.edgeTop = RegisterProperty("edge-top", "transparent").AddParser("color").GetId();
        ids_.edgeBottom = RegisterProperty("edge-bottom", "transparent").AddParser("color").GetId();
        ids_.edgeWidth = RegisterProperty("edge-width", "1px").AddParser("length").GetId();
        ids_.highlight = RegisterProperty("highlight", "transparent").AddParser("color").GetId();
        RegisterShorthand("decorator", "cut, fill-top, fill-bottom, edge-top, edge-bottom, edge-width, highlight",
            Rml::ShorthandType::FallThrough);
    }
    Rml::SharedPtr<Rml::Decorator> InstanceDecorator(Rml::String const&, Rml::PropertyDictionary const& p,
        Rml::DecoratorInstancerInterface const&) override {
        auto d = Rml::MakeShared<ChamferDecorator>();
        d->cut = p.GetProperty(ids_.cut)->GetNumericValue();
        d->fillTop = p.GetProperty(ids_.fillTop)->Get<Colourb>();
        d->fillBottom = p.GetProperty(ids_.fillBottom)->Get<Colourb>();
        d->edgeTop = p.GetProperty(ids_.edgeTop)->Get<Colourb>();
        d->edgeBottom = p.GetProperty(ids_.edgeBottom)->Get<Colourb>();
        d->edgeWidth = p.GetProperty(ids_.edgeWidth)->GetNumericValue();
        d->highlight = p.GetProperty(ids_.highlight)->Get<Colourb>();
        return d;
    }

private:
    ChamferIds ids_{};
};

// ---- backdrop(...) ----------------------------------------------------------------------------------------
struct BackdropData {
    Rml::Geometry picture;   // textured
    Rml::Geometry overlay;   // edge, accents, scanlines
};

class BackdropDecorator final : public Rml::Decorator {
public:
    Rml::NumericValue cut;
    Rml::NumericValue accentLength;
    Colourb tint, edge, accent, scanline;
    int texture = -1;

    Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element, Rml::BoxArea area) const override {
        Rml::RenderBox const box = element->GetRenderBox(area);
        Vector2f const o = box.GetFillOffset();
        Vector2f const size = box.GetFillSize();
        if (size.x <= 0.0f || size.y <= 0.0f) {
            return Rml::Decorator::INVALID_DECORATORDATAHANDLE;
        }
        float const opacity = element->GetComputedValues().opacity();
        float const c = std::clamp(element->ResolveLength(cut), 0.0f, std::min(size.x, size.y) * 0.5f);
        auto* data = new BackdropData;
        Rml::RenderManager* const render = element->GetRenderManager();

        // The picture, across the chamfered panel.
        {
            Builder b;
            std::vector<Vector2f> const outer = ChamferPolygon(o, size, c, 0.0f);
            ColourbPremultiplied const t = Premultiply(tint, opacity);
            int const first = static_cast<int>(b.mesh.vertices.size());
            for (Vector2f const& p : outer) {
                b.Vertex(p, t, {(p.x - o.x) / size.x, (p.y - o.y) / size.y});
            }
            for (int i = 1; i + 1 < static_cast<int>(outer.size()); ++i) {
                b.Triangle(first, first + i, first + i + 1);
            }
            data->picture = render->MakeGeometry(std::move(b.mesh));
        }
        // Scanlines, the edge and the accents.
        Builder b;
        if (scanline.alpha > 0) {
            ColourbPremultiplied const s = Premultiply(scanline, opacity);
            for (float y = 0.0f; y + 1.0f <= size.y; y += 3.0f) {
                // The rows' extent inside the cut corners (top-left: x >= c - y; bottom-right: x <= w - (c - (h - y))).
                float const x0 = std::max(0.0f, c - (y + 1.0f));
                float const x1 = std::min(size.x, size.x - (c - (size.y - y - 1.0f)));
                if (x1 > x0) {
                    b.Quad(o + Vector2f(x0, y), o + Vector2f(x1, y), o + Vector2f(x1, y + 1.0f), o + Vector2f(x0, y + 1.0f), s);
                }
            }
        }
        if (edge.alpha > 0) {
            b.Ring(ChamferPolygon(o, size, c, 0.0f), ChamferPolygon(o, size, c, 1.0f), Premultiply(edge, opacity),
                Premultiply(edge, opacity), o.y, o.y + size.y);
        }
        if (accent.alpha > 0 && c > 0.0f) {
            float const run = std::max(0.0f, element->ResolveLength(accentLength));
            ColourbPremultiplied const a = Premultiply(accent, opacity);
            float const inset = 1.5f;  // the 3 px stroke's centre, just inside the edge
            b.Stroke({o + Vector2f(inset, c + inset * (kSqrt2 - 1.0f)), o + Vector2f(c + inset * (kSqrt2 - 1.0f), inset),
                         o + Vector2f(c + run, inset)},
                false, 3.0f, a);
            b.Stroke({o + Vector2f(size.x - inset, size.y - c - inset * (kSqrt2 - 1.0f)),
                         o + Vector2f(size.x - c - inset * (kSqrt2 - 1.0f), size.y - inset), o + Vector2f(size.x - c - run, size.y - inset)},
                false, 3.0f, a);
        }
        data->overlay = render->MakeGeometry(std::move(b.mesh));
        return reinterpret_cast<Rml::DecoratorDataHandle>(data);
    }
    void ReleaseElementData(Rml::DecoratorDataHandle handle) const override {
        delete reinterpret_cast<BackdropData*>(handle);
    }
    void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle handle) const override {
        auto* data = reinterpret_cast<BackdropData*>(handle);
        Vector2f const at = element->GetAbsoluteOffset(Rml::BoxArea::Border);
        if (texture >= 0) {
            data->picture.Render(at, GetTexture(texture));
        }
        data->overlay.Render(at);
    }
    using Rml::Decorator::AddTexture;
};

class BackdropInstancer final : public Rml::DecoratorInstancer {
public:
    BackdropInstancer() {
        cut_ = RegisterProperty("cut", "0px").AddParser("length").GetId();
        image_ = RegisterProperty("image", "").AddParser("string").GetId();
        tint_ = RegisterProperty("tint", "white").AddParser("color").GetId();
        edge_ = RegisterProperty("edge", "transparent").AddParser("color").GetId();
        accent_ = RegisterProperty("accent", "transparent").AddParser("color").GetId();
        accentLength_ = RegisterProperty("accent-length", "0px").AddParser("length").GetId();
        scanline_ = RegisterProperty("scanline", "transparent").AddParser("color").GetId();
        RegisterShorthand("decorator", "cut, image, tint, edge, accent, accent-length, scanline", Rml::ShorthandType::FallThrough);
    }
    Rml::SharedPtr<Rml::Decorator> InstanceDecorator(Rml::String const&, Rml::PropertyDictionary const& p,
        Rml::DecoratorInstancerInterface const& context) override {
        auto d = Rml::MakeShared<BackdropDecorator>();
        d->cut = p.GetProperty(cut_)->GetNumericValue();
        d->tint = p.GetProperty(tint_)->Get<Colourb>();
        d->edge = p.GetProperty(edge_)->Get<Colourb>();
        d->accent = p.GetProperty(accent_)->Get<Colourb>();
        d->accentLength = p.GetProperty(accentLength_)->GetNumericValue();
        d->scanline = p.GetProperty(scanline_)->Get<Colourb>();
        Rml::String const image = p.GetProperty(image_)->Get<Rml::String>();
        if (!image.empty()) {
            d->texture = d->AddTexture(context.GetTexture(image));
        }
        return d;
    }

private:
    Rml::PropertyId cut_{}, image_{}, tint_{}, edge_{}, accent_{}, accentLength_{}, scanline_{};
};

// ---- icon(<name>) -----------------------------------------------------------------------------------------
// The mockup's inline SVG icons (MainGlass.dc.html), as strokes and fills in their own viewBox units.
struct Shape {
    std::vector<Vector2f> points;
    bool closed = false;
    bool fill = false;
    float alpha = 1.0f;  // a stroke-opacity
};
struct Icon {
    char const* name;
    float box;     // the viewBox's size (square)
    float stroke;  // stroke width in viewBox units
    std::vector<Shape> shapes;
};

Shape Line(std::initializer_list<Vector2f> points) {
    return Shape{points, false, false};
}
Shape Closed(std::initializer_list<Vector2f> points) {
    return Shape{points, true, false};
}
Shape Filled(std::initializer_list<Vector2f> points) {
    return Shape{points, true, true};
}
// Degrees, clockwise on screen (y down) from +x.
Shape Arc(float cx, float cy, float r, float from, float to, int steps = 24) {
    Shape s;
    for (int i = 0; i <= steps; ++i) {
        float const a = (from + (to - from) * static_cast<float>(i) / static_cast<float>(steps)) * kPi / 180.0f;
        s.points.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    return s;
}
Shape Circle(float cx, float cy, float r, float alpha = 1.0f) {
    Shape s = Arc(cx, cy, r, 0.0f, 360.0f, 40);
    s.points.pop_back();
    s.closed = true;
    s.alpha = alpha;
    return s;
}
// A rounded rectangle (x, y, w, h, radius).
Shape Rounded(float x, float y, float w, float h, float r) {
    Shape s;
    auto corner = [&](float cx, float cy, float from) {
        for (int i = 0; i <= 8; ++i) {
            float const a = (from + 90.0f * static_cast<float>(i) / 8.0f) * kPi / 180.0f;
            s.points.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
        }
    };
    corner(x + w - r, y + r, -90.0f);
    corner(x + w - r, y + h - r, 0.0f);
    corner(x + r, y + h - r, 90.0f);
    corner(x + r, y + r, 180.0f);
    s.closed = true;
    return s;
}
Shape Quadratic(Vector2f a, Vector2f c, Vector2f b, int steps = 16) {
    Shape s;
    for (int i = 0; i <= steps; ++i) {
        float const t = static_cast<float>(i) / static_cast<float>(steps);
        float const u = 1.0f - t;
        s.points.push_back(a * (u * u) + c * (2.0f * u * t) + b * (t * t));
    }
    return s;
}

std::vector<Icon> const& Icons() {
    static std::vector<Icon> const icons = [] {
        std::vector<Icon> v;
        // The brand: a hexagon with a chevron and a bar (40 box).
        v.push_back({"brand", 40.0f, 2.0f,
            {Closed({{20, 3}, {35, 11.5f}, {35, 28.5f}, {20, 37}, {5, 28.5f}, {5, 11.5f}}), Line({{13, 22}, {20, 12}, {27, 22}}),
                Line({{16, 26}, {24, 26}})}});
        // A headset: "M3 9h18v6a2 2 0 0 1-2 2h-4l-3-3-3 3H5a2 2 0 0 1-2-2z".
        {
            Shape s;
            s.points = {{3, 9}, {21, 9}, {21, 15}};
            Shape const c1 = Arc(19, 15, 2, 0, 90, 6);
            s.points.insert(s.points.end(), c1.points.begin() + 1, c1.points.end());
            s.points.insert(s.points.end(), {{15, 17}, {12, 14}, {9, 17}, {5, 17}});
            Shape const c2 = Arc(5, 15, 2, 90, 180, 6);
            s.points.insert(s.points.end(), c2.points.begin() + 1, c2.points.end());
            s.closed = true;
            v.push_back({"headset", 24.0f, 1.8f, {s}});
        }
        v.push_back({"controller", 24.0f, 1.8f, {Rounded(2, 7, 20, 10, 5), Line({{7, 10}, {7, 14}}), Line({{5, 12}, {9, 12}})}});
        v.push_back({"controls", 24.0f, 1.8f,
            {Rounded(2, 7, 20, 10, 5), Line({{7, 10}, {7, 14}}), Line({{5, 12}, {9, 12}}), Circle(16, 11, 1), Circle(18, 13, 1)}});
        v.push_back({"check", 24.0f, 2.0f, {Line({{5, 12}, {9, 16}, {19, 6}})}});
        v.push_back({"play", 24.0f, 0.0f, {Filled({{7, 4.5f}, {19, 12}, {7, 19.5f}})}});
        {
            Shape arc = Arc(12, 12, 8, 0, 315, 32);
            v.push_back({"turn", 24.0f, 1.8f, {arc, Line({{20, 4}, {20, 8}, {16, 8}})}});
            v.push_back({"recenter", 24.0f, 2.0f, {arc, Line({{20, 4}, {20, 8}, {16, 8}})}});
        }
        v.push_back({"body", 24.0f, 1.8f,
            {Circle(12, 4.5f, 2.5f), Line({{12, 8}, {12, 15}}), Line({{8, 22}, {12, 15}, {16, 22}}), Line({{5, 11}, {19, 11}})}});
        {
            Shape top = Quadratic({2, 12}, {12, -2}, {22, 12});
            Shape const bottom = Quadratic({22, 12}, {12, 26}, {2, 12});
            top.points.insert(top.points.end(), bottom.points.begin() + 1, bottom.points.end() - 1);
            top.closed = true;
            v.push_back({"eye", 24.0f, 1.8f, {top, Circle(12, 12, 3)}});
        }
        v.push_back({"crosshair", 24.0f, 1.8f,
            {Circle(12, 12, 8), Line({{12, 2}, {12, 6}}), Line({{12, 18}, {12, 22}}), Line({{2, 12}, {6, 12}}), Line({{18, 12}, {22, 12}})}});
        v.push_back({"sliders", 24.0f, 1.8f,
            {Line({{4, 6}, {14, 6}}), Line({{18, 6}, {20, 6}}), Line({{4, 12}, {8, 12}}), Line({{12, 12}, {20, 12}}),
                Line({{4, 18}, {16, 18}}), Circle(16, 6, 2), Circle(10, 12, 2), Circle(18, 18, 2)}});
        v.push_back({"info", 24.0f, 1.8f, {Circle(12, 12, 9), Line({{12, 11}, {12, 17}}), Line({{12, 7.5f}, {12, 8.5f}})}});
        v.push_back({"target", 24.0f, 2.0f,
            {Circle(12, 12, 8), Circle(12, 12, 3), Line({{12, 1}, {12, 5}}), Line({{12, 19}, {12, 23}}), Line({{1, 12}, {5, 12}}),
                Line({{19, 12}, {23, 12}})}});
        v.push_back({"calibration", 64.0f, 2.0f,
            {Circle(32, 32, 26), Circle(32, 32, 16, 0.6f), Circle(32, 32, 5), Line({{32, 2}, {32, 12}}), Line({{32, 52}, {32, 62}}),
                Line({{2, 32}, {12, 32}}), Line({{52, 32}, {62, 32}})}});
        v.push_back({"align", 24.0f, 2.0f, {Circle(12, 6, 3), Line({{12, 9}, {12, 21}}), Line({{6, 15}, {18, 15}})}});
        v.push_back({"place", 24.0f, 2.0f, {Closed({{4, 6}, {20, 6}, {20, 16}, {4, 16}}), Line({{12, 16}, {12, 20}}), Line({{8, 20}, {16, 20}})}});
        v.push_back({"trash", 24.0f, 2.0f,
            {Line({{4, 7}, {20, 7}}), Line({{9, 7}, {9, 4}, {15, 4}, {15, 7}}), Line({{6, 7}, {7, 20}, {17, 20}, {18, 7}})}});
        v.push_back({"camera", 24.0f, 2.0f, {Closed({{3, 7}, {8, 7}, {10, 5}, {14, 5}, {16, 7}, {21, 7}, {21, 19}, {3, 19}}), Circle(12, 13, 3.5f)}});
        v.push_back({"record", 24.0f, 2.0f, {Circle(12, 12, 8), Circle(12, 12, 3)}});
        v.push_back({"timer", 24.0f, 2.0f, {Circle(12, 13, 8), Line({{12, 13}, {12, 9}}), Line({{9, 2}, {15, 2}})}});
        return v;
    }();
    return icons;
}

Icon const* FindIcon(std::string const& name) {
    for (Icon const& icon : Icons()) {
        if (name == icon.name) {
            return &icon;
        }
    }
    return nullptr;
}

struct IconData {
    Rml::Geometry geometry;
    Colourb colour;
    float opacity = -1.0f;
    Rml::BoxArea area = Rml::BoxArea::Content;
};

class IconDecorator final : public Rml::Decorator {
public:
    Icon const* icon = nullptr;

    void Build(Rml::Element* element, IconData& data) const {
        Rml::RenderBox const box = element->GetRenderBox(data.area);
        Vector2f const o = box.GetFillOffset();
        Vector2f const size = box.GetFillSize();
        Rml::ComputedValues const& computed = element->GetComputedValues();
        data.colour = computed.color();
        data.opacity = computed.opacity();
        float const scale = std::min(size.x, size.y) / icon->box;
        Vector2f const origin = o + Vector2f((size.x - icon->box * scale) * 0.5f, (size.y - icon->box * scale) * 0.5f);
        Builder b;
        for (Shape const& shape : icon->shapes) {
            std::vector<Vector2f> points;
            points.reserve(shape.points.size());
            for (Vector2f const& p : shape.points) {
                points.push_back(origin + p * scale);
            }
            ColourbPremultiplied const colour = data.colour.ToPremultiplied(data.opacity * shape.alpha);
            if (shape.fill) {
                b.Fan(points, colour, colour, 0.0f, 1.0f);
            } else {
                b.Stroke(points, shape.closed, std::max(icon->stroke * scale, 1.0f), colour);
            }
        }
        data.geometry = element->GetRenderManager()->MakeGeometry(std::move(b.mesh));
    }

    Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element, Rml::BoxArea area) const override {
        if (icon == nullptr) {
            return Rml::Decorator::INVALID_DECORATORDATAHANDLE;
        }
        auto* data = new IconData;
        data->area = area;
        Build(element, *data);
        return reinterpret_cast<Rml::DecoratorDataHandle>(data);
    }
    void ReleaseElementData(Rml::DecoratorDataHandle handle) const override {
        delete reinterpret_cast<IconData*>(handle);
    }
    void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle handle) const override {
        auto* data = reinterpret_cast<IconData*>(handle);
        Rml::ComputedValues const& computed = element->GetComputedValues();
        if (computed.color() != data->colour || computed.opacity() != data->opacity) {
            Build(element, *data);  // the colour changed (hover, on): RmlUi only regenerates decorators for layout
        }
        data->geometry.Render(element->GetAbsoluteOffset(Rml::BoxArea::Border));
    }
};

class IconInstancer final : public Rml::DecoratorInstancer {
public:
    IconInstancer() {
        name_ = RegisterProperty("name", "").AddParser("string").GetId();
        RegisterShorthand("decorator", "name", Rml::ShorthandType::FallThrough);
    }
    Rml::SharedPtr<Rml::Decorator> InstanceDecorator(Rml::String const&, Rml::PropertyDictionary const& p,
        Rml::DecoratorInstancerInterface const&) override {
        auto d = Rml::MakeShared<IconDecorator>();
        d->icon = FindIcon(p.GetProperty(name_)->Get<Rml::String>());
        return d->icon != nullptr ? d : nullptr;
    }

private:
    Rml::PropertyId name_{};
};

} // namespace

void RegisterDecorators() {
    static ChamferInstancer chamfer;
    static BackdropInstancer backdrop;
    static IconInstancer icon;
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    Rml::Factory::RegisterDecoratorInstancer("chamfer", &chamfer);
    Rml::Factory::RegisterDecoratorInstancer("backdrop", &backdrop);
    Rml::Factory::RegisterDecoratorInstancer("icon", &icon);
}

char const* const* IconNames(int& count) {
    static std::vector<char const*> names = [] {
        std::vector<char const*> out;
        for (Icon const& icon : Icons()) {
            out.push_back(icon.name);
        }
        return out;
    }();
    count = static_cast<int>(names.size());
    return names.data();
}

} // namespace mevr::menu
