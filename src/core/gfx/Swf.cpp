// SPDX-License-Identifier: MIT
#include "core/gfx/Swf.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mevr::gfx {

Matrix Multiply(Matrix const& o, Matrix const& i) {
    Matrix r;
    r.a = o.a * i.a + o.c * i.b;
    r.b = o.b * i.a + o.d * i.b;
    r.c = o.a * i.c + o.c * i.d;
    r.d = o.b * i.c + o.d * i.d;
    r.tx = o.a * i.tx + o.c * i.ty + o.tx;
    r.ty = o.b * i.tx + o.d * i.ty + o.ty;
    return r;
}

std::optional<Matrix> Invert(Matrix const& m) {
    double const det = m.a * m.d - m.b * m.c;
    if (!std::isfinite(det) || std::abs(det) < 1e-12) {
        return std::nullopt;
    }
    Matrix r;
    r.a = m.d / det;
    r.b = -m.b / det;
    r.c = -m.c / det;
    r.d = m.a / det;
    r.tx = (m.c * m.ty - m.d * m.tx) / det;
    r.ty = (m.b * m.tx - m.a * m.ty) / det;
    return r;
}

ColorTransform Concat(ColorTransform const& o, ColorTransform const& i) {
    ColorTransform r;
    r.mr = i.mr * o.mr;
    r.mg = i.mg * o.mg;
    r.mb = i.mb * o.mb;
    r.ma = i.ma * o.ma;
    r.ar = i.ar * o.mr + o.ar;
    r.ag = i.ag * o.mg + o.ag;
    r.ab = i.ab * o.mb + o.ab;
    r.aa = i.aa * o.ma + o.aa;
    return r;
}

namespace {

constexpr std::size_t kMaxEdgesPerShape = 1 << 20;
constexpr std::size_t kMaxEdgesPerGlyph = 1 << 14;
constexpr std::size_t kMaxStoredPlacements = 1 << 19; // per movie, over every frame's snapshot
constexpr int kMaxSpriteNesting = 4;

// Bounds-checked little-endian bytes and SWF's MSB-first bit fields. A read past the end returns zero and sets
// Failed(); callers check it once per record, so no loop runs on garbage for long (zeros end shape records).
class Reader {
public:
    Reader(std::span<std::uint8_t const> data, std::size_t pos, std::size_t end)
        : data_(data), pos_(pos), end_(std::min(end, data.size())) {
        if (pos_ > end_) {
            failed_ = true;
            pos_ = end_;
        }
    }

    bool Failed() const { return failed_; }
    void Fail() { failed_ = true; }
    std::size_t Pos() const { return pos_; }
    std::size_t End() const { return end_; }
    std::size_t Remaining() const { return bit_ == 0 ? end_ - pos_ : end_ - pos_ - 1; }

    void Align() {
        if (bit_ != 0) {
            bit_ = 0;
            ++pos_;
        }
    }

    std::uint32_t Bits(int n) {
        std::uint32_t v = 0;
        for (int k = 0; k < n; ++k) {
            if (pos_ >= end_) {
                failed_ = true;
                return 0;
            }
            std::uint32_t const bit = (data_[pos_] >> (7 - bit_)) & 1u;
            v = (v << 1) | bit;
            if (++bit_ == 8) {
                bit_ = 0;
                ++pos_;
            }
        }
        return v;
    }

    std::int32_t SBits(int n) {
        if (n <= 0) {
            return 0;
        }
        std::uint32_t const v = Bits(n);
        if (n >= 32) {
            return static_cast<std::int32_t>(v);
        }
        if (v & (1u << (n - 1))) {
            return static_cast<std::int32_t>(static_cast<std::int64_t>(v) - (std::int64_t{1} << n));
        }
        return static_cast<std::int32_t>(v);
    }

    std::uint8_t U8() {
        Align();
        if (pos_ + 1 > end_) {
            failed_ = true;
            return 0;
        }
        return data_[pos_++];
    }
    std::uint16_t U16() {
        Align();
        if (pos_ + 2 > end_) {
            failed_ = true;
            pos_ = end_;
            return 0;
        }
        std::uint16_t const v = static_cast<std::uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
        pos_ += 2;
        return v;
    }
    std::int16_t S16() { return static_cast<std::int16_t>(U16()); }
    std::uint32_t U32() {
        Align();
        if (pos_ + 4 > end_) {
            failed_ = true;
            pos_ = end_;
            return 0;
        }
        std::uint32_t const v = static_cast<std::uint32_t>(data_[pos_]) | (static_cast<std::uint32_t>(data_[pos_ + 1]) << 8) |
                                (static_cast<std::uint32_t>(data_[pos_ + 2]) << 16) |
                                (static_cast<std::uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return v;
    }
    void Skip(std::size_t n) {
        Align();
        if (n > end_ - pos_) {
            failed_ = true;
            pos_ = end_;
            return;
        }
        pos_ += n;
    }
    std::string String() {
        Align();
        std::string s;
        while (pos_ < end_) {
            std::uint8_t const c = data_[pos_++];
            if (c == 0) {
                return s;
            }
            s.push_back(static_cast<char>(c));
        }
        failed_ = true;
        return s;
    }

    Rect ReadRect() {
        Align();
        int const n = static_cast<int>(Bits(5));
        Rect r;
        r.xMin = SBits(n);
        r.xMax = SBits(n);
        r.yMin = SBits(n);
        r.yMax = SBits(n);
        Align();
        return r;
    }

    Matrix ReadMatrix() {
        Align();
        Matrix m;
        if (Bits(1)) {
            int const n = static_cast<int>(Bits(5));
            m.a = SBits(n) / 65536.0;
            m.d = SBits(n) / 65536.0;
        }
        if (Bits(1)) {
            int const n = static_cast<int>(Bits(5));
            m.b = SBits(n) / 65536.0;
            m.c = SBits(n) / 65536.0;
        }
        int const n = static_cast<int>(Bits(5));
        m.tx = SBits(n);
        m.ty = SBits(n);
        Align();
        return m;
    }

    ColorTransform ReadCxform(bool alpha) {
        Align();
        ColorTransform cx;
        bool const hasAdd = Bits(1) != 0;
        bool const hasMult = Bits(1) != 0;
        int const n = static_cast<int>(Bits(4));
        if (hasMult) {
            cx.mr = SBits(n) / 256.0f;
            cx.mg = SBits(n) / 256.0f;
            cx.mb = SBits(n) / 256.0f;
            if (alpha) {
                cx.ma = SBits(n) / 256.0f;
            }
        }
        if (hasAdd) {
            cx.ar = static_cast<float>(SBits(n));
            cx.ag = static_cast<float>(SBits(n));
            cx.ab = static_cast<float>(SBits(n));
            if (alpha) {
                cx.aa = static_cast<float>(SBits(n));
            }
        }
        Align();
        return cx;
    }

    Rgba ReadRgb(bool alpha) {
        Rgba c;
        c.r = U8();
        c.g = U8();
        c.b = U8();
        c.a = alpha ? U8() : 255;
        return c;
    }

private:
    std::span<std::uint8_t const> data_;
    std::size_t pos_ = 0;
    std::size_t end_ = 0;
    int bit_ = 0;
    bool failed_ = false;
};

struct TagHeader {
    std::uint16_t code = 0;
    std::size_t pos = 0; // first byte of the body
    std::size_t length = 0;
};

// The next tag header at `pos` (< end). False at the end or for a header that runs past `end`.
bool ReadTag(std::span<std::uint8_t const> data, std::size_t& pos, std::size_t end, TagHeader& tag) {
    if (pos + 2 > end) {
        return false;
    }
    std::uint16_t const codeLength = static_cast<std::uint16_t>(data[pos] | (data[pos + 1] << 8));
    pos += 2;
    tag.code = static_cast<std::uint16_t>(codeLength >> 6);
    std::size_t length = codeLength & 0x3F;
    if (length == 0x3F) {
        if (pos + 4 > end) {
            return false;
        }
        length = static_cast<std::size_t>(data[pos]) | (static_cast<std::size_t>(data[pos + 1]) << 8) |
                 (static_cast<std::size_t>(data[pos + 2]) << 16) | (static_cast<std::size_t>(data[pos + 3]) << 24);
        pos += 4;
    }
    if (length > end - pos) {
        return false;
    }
    tag.pos = pos;
    tag.length = length;
    pos += length;
    return true;
}

bool ReadFillStyle(Reader& r, int version, FillStyle& f) {
    std::uint8_t const type = r.U8();
    bool const alpha = version >= 3;
    switch (type) {
    case 0x00:
        f.kind = FillKind::Solid;
        f.color = r.ReadRgb(alpha);
        break;
    case 0x10:
    case 0x12:
    case 0x13: {
        f.kind = type == 0x10 ? FillKind::Linear : type == 0x12 ? FillKind::Radial : FillKind::FocalRadial;
        f.matrix = r.ReadMatrix();
        r.Align();
        f.spread = static_cast<std::uint8_t>(r.Bits(2));
        f.interpolation = static_cast<std::uint8_t>(r.Bits(2));
        int const count = static_cast<int>(r.Bits(4));
        f.stops.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            GradientStop s;
            s.ratio = r.U8();
            s.color = r.ReadRgb(alpha);
            f.stops.push_back(s);
        }
        if (type == 0x13) {
            f.focal = std::clamp(r.S16() / 256.0f, -1.0f, 1.0f);
        }
        break;
    }
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
        f.kind = FillKind::Bitmap;
        f.bitmapId = r.U16();
        f.matrix = r.ReadMatrix();
        break;
    default:
        r.Fail();
        return false;
    }
    return !r.Failed();
}

bool ReadStyles(Reader& r, int version, StyleTable& table) {
    std::size_t fills = r.U8();
    if (fills == 0xFF && version >= 2) {
        fills = r.U16();
    }
    for (std::size_t i = 0; i < fills && !r.Failed(); ++i) {
        FillStyle f;
        if (!ReadFillStyle(r, version, f)) {
            return false;
        }
        table.fills.push_back(std::move(f));
    }
    std::size_t lines = r.U8();
    if (lines == 0xFF) {
        lines = r.U16();
    }
    for (std::size_t i = 0; i < lines && !r.Failed(); ++i) {
        LineStyle l;
        l.width = r.U16();
        if (version >= 4) {
            l.startCap = static_cast<std::uint8_t>(r.Bits(2));
            l.join = static_cast<std::uint8_t>(r.Bits(2));
            l.hasFill = r.Bits(1) != 0;
            r.Bits(3); // no-h-scale, no-v-scale, pixel hinting
            r.Bits(5);
            l.noClose = r.Bits(1) != 0;
            l.endCap = static_cast<std::uint8_t>(r.Bits(2));
            if (l.join == 2) {
                l.miterLimit = r.U16() / 256.0f;
            }
            if (l.hasFill) {
                if (!ReadFillStyle(r, version, l.fill)) {
                    return false;
                }
                l.color = l.fill.kind == FillKind::Solid ? l.fill.color : Rgba{};
                if (l.fill.kind == FillKind::Solid) {
                    l.hasFill = false;
                }
            } else {
                l.color = r.ReadRgb(true);
            }
        } else {
            l.color = r.ReadRgb(version >= 3);
        }
        table.lines.push_back(std::move(l));
    }
    return !r.Failed();
}

// MORPHFILLSTYLE: a start and an end fill.
bool ReadMorphFill(Reader& r, FillStyle& s, FillStyle& e) {
    std::uint8_t const type = r.U8();
    switch (type) {
    case 0x00:
        s.kind = e.kind = FillKind::Solid;
        s.color = r.ReadRgb(true);
        e.color = r.ReadRgb(true);
        break;
    case 0x10:
    case 0x12:
    case 0x13: {
        s.kind = e.kind = type == 0x10 ? FillKind::Linear : type == 0x12 ? FillKind::Radial : FillKind::FocalRadial;
        s.matrix = r.ReadMatrix();
        e.matrix = r.ReadMatrix();
        std::uint8_t const head = r.U8();
        s.spread = e.spread = static_cast<std::uint8_t>((head >> 6) & 3);
        s.interpolation = e.interpolation = static_cast<std::uint8_t>((head >> 4) & 3);
        int const count = head & 0x0F;
        for (int i = 0; i < count; ++i) {
            GradientStop a, b;
            a.ratio = r.U8();
            a.color = r.ReadRgb(true);
            b.ratio = r.U8();
            b.color = r.ReadRgb(true);
            s.stops.push_back(a);
            e.stops.push_back(b);
        }
        if (type == 0x13) {
            s.focal = std::clamp(r.S16() / 256.0f, -1.0f, 1.0f);
            e.focal = std::clamp(r.S16() / 256.0f, -1.0f, 1.0f);
        }
        break;
    }
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
        s.kind = e.kind = FillKind::Bitmap;
        s.bitmapId = e.bitmapId = r.U16();
        s.matrix = r.ReadMatrix();
        e.matrix = r.ReadMatrix();
        break;
    default:
        r.Fail();
        return false;
    }
    return !r.Failed();
}

std::int32_t ClampCoord(std::int64_t v) {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(v, -(std::int64_t{1} << 30), std::int64_t{1} << 30));
}

// SHAPE / SHAPEWITHSTYLE records. `styles` gets the tables StateNewStyles adds (glyphs pass an empty one and never
// add). False if the records ran past their data or grew past `maxEdges`.
bool ReadShapeRecords(Reader& r, int version, std::vector<StyleTable>& styles, std::vector<Edge>& edges, std::size_t maxEdges) {
    r.Align();
    int fillBits = static_cast<int>(r.Bits(4));
    int lineBits = static_cast<int>(r.Bits(4));
    std::int64_t x = 0, y = 0;
    std::uint16_t fill0 = 0, fill1 = 0, line = 0;
    std::uint16_t table = 0;
    while (!r.Failed()) {
        if (r.Bits(1) == 0) {
            std::uint32_t const flags = r.Bits(5);
            if (flags == 0) {
                return !r.Failed();
            }
            if (flags & 0x01) {
                int const n = static_cast<int>(r.Bits(5));
                x = r.SBits(n);
                y = r.SBits(n);
            }
            std::optional<std::uint16_t> f0, f1, ln;
            if (flags & 0x02) {
                f0 = static_cast<std::uint16_t>(r.Bits(fillBits));
            }
            if (flags & 0x04) {
                f1 = static_cast<std::uint16_t>(r.Bits(fillBits));
            }
            if (flags & 0x08) {
                ln = static_cast<std::uint16_t>(r.Bits(lineBits));
            }
            if (flags & 0x10) {
                StyleTable t;
                if (!ReadStyles(r, version, t)) {
                    return false;
                }
                if (styles.size() >= 0xFFFF) {
                    return false;
                }
                styles.push_back(std::move(t));
                table = static_cast<std::uint16_t>(styles.size() - 1);
                fill0 = fill1 = line = 0;
                fillBits = static_cast<int>(r.Bits(4));
                lineBits = static_cast<int>(r.Bits(4));
            }
            if (f0) {
                fill0 = *f0;
            }
            if (f1) {
                fill1 = *f1;
            }
            if (ln) {
                line = *ln;
            }
            continue;
        }
        Edge e;
        e.x0 = ClampCoord(x);
        e.y0 = ClampCoord(y);
        e.fill0 = fill0;
        e.fill1 = fill1;
        e.line = line;
        e.table = table;
        bool const straight = r.Bits(1) != 0;
        int const n = static_cast<int>(r.Bits(4)) + 2;
        if (straight) {
            std::int64_t dx = 0, dy = 0;
            if (r.Bits(1)) {
                dx = r.SBits(n);
                dy = r.SBits(n);
            } else if (r.Bits(1)) {
                dy = r.SBits(n);
            } else {
                dx = r.SBits(n);
            }
            x += dx;
            y += dy;
            e.cx = e.x0;
            e.cy = e.y0;
        } else {
            std::int64_t const cx = x + r.SBits(n);
            std::int64_t const cy = y + r.SBits(n);
            x = cx + r.SBits(n);
            y = cy + r.SBits(n);
            e.curve = true;
            e.cx = ClampCoord(cx);
            e.cy = ClampCoord(cy);
        }
        x = ClampCoord(x);
        y = ClampCoord(y);
        e.x1 = static_cast<std::int32_t>(x);
        e.y1 = static_cast<std::int32_t>(y);
        if (r.Failed()) {
            return false;
        }
        edges.push_back(e);
        if (edges.size() > maxEdges) {
            return false;
        }
    }
    return false;
}

bool SkipFilters(Reader& r, std::uint8_t& count) {
    count = r.U8();
    for (int i = 0; i < count && !r.Failed(); ++i) {
        switch (r.U8()) {
        case 0: r.Skip(23); break; // drop shadow
        case 1: r.Skip(9); break;  // blur
        case 2: r.Skip(15); break; // glow
        case 3: r.Skip(27); break; // bevel
        case 4:                    // gradient glow
        case 7: {                  // gradient bevel
            std::size_t const n = r.U8();
            r.Skip(5 * n + 19);
            break;
        }
        case 5: { // convolution
            std::size_t const mx = r.U8();
            std::size_t const my = r.U8();
            r.Skip(8 + 4 * mx * my + 5);
            break;
        }
        case 6: r.Skip(80); break; // colour matrix
        default: r.Fail(); break;
        }
    }
    return !r.Failed();
}

bool EqualsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

struct MovieParser {
    Movie& movie;
    std::span<std::uint8_t const> data;
    std::size_t storedPlacements = 0;
    std::map<std::uint16_t, Rect> grids;

    void Problem(char const* what, TagHeader const& tag) {
        ++movie.notes_.badTags;
        if (movie.notes_.firstProblem.empty()) {
            movie.notes_.firstProblem = std::string(what) + " (tag " + std::to_string(tag.code) + " at byte " +
                                        std::to_string(tag.pos) + ")";
        }
    }

    bool IsDefined(std::uint16_t id) const {
        return movie.shapes_.contains(id) || movie.morphs_.contains(id) || movie.sprites_.contains(id) ||
               movie.editTexts_.contains(id) || movie.fonts_.contains(id) || movie.others_.contains(id);
    }

    void DefineMorph(TagHeader const& tag) {
        Reader r(data, tag.pos, tag.pos + tag.length);
        MorphShape m;
        m.version = tag.code == 84 ? 2 : 1;
        m.id = r.U16();
        m.startBounds = r.ReadRect();
        m.endBounds = r.ReadRect();
        if (m.version == 2) {
            r.ReadRect();
            r.ReadRect();
            r.U8();
        }
        std::uint32_t const offset = r.U32();
        std::size_t const endEdges = r.Pos() + offset;
        std::size_t fills = r.U8();
        if (fills == 0xFF) {
            fills = r.U16();
        }
        for (std::size_t i = 0; i < fills && !r.Failed(); ++i) {
            FillStyle s, e;
            if (!ReadMorphFill(r, s, e)) {
                break;
            }
            m.startFills.push_back(std::move(s));
            m.endFills.push_back(std::move(e));
        }
        std::size_t lines = r.U8();
        if (lines == 0xFF) {
            lines = r.U16();
        }
        for (std::size_t i = 0; i < lines && !r.Failed(); ++i) {
            LineStyle s, e;
            s.width = r.U16();
            e.width = r.U16();
            if (m.version == 2) {
                s.startCap = e.startCap = static_cast<std::uint8_t>(r.Bits(2));
                s.join = e.join = static_cast<std::uint8_t>(r.Bits(2));
                bool const hasFill = r.Bits(1) != 0;
                r.Bits(3);
                r.Bits(5);
                s.noClose = e.noClose = r.Bits(1) != 0;
                s.endCap = e.endCap = static_cast<std::uint8_t>(r.Bits(2));
                if (s.join == 2) {
                    s.miterLimit = e.miterLimit = r.U16() / 256.0f;
                }
                if (hasFill) {
                    if (!ReadMorphFill(r, s.fill, e.fill)) {
                        break;
                    }
                    s.hasFill = e.hasFill = s.fill.kind != FillKind::Solid;
                    s.color = s.fill.color;
                    e.color = e.fill.color;
                } else {
                    s.color = r.ReadRgb(true);
                    e.color = r.ReadRgb(true);
                }
            } else {
                s.color = r.ReadRgb(true);
                e.color = r.ReadRgb(true);
            }
            m.startLines.push_back(std::move(s));
            m.endLines.push_back(std::move(e));
        }
        std::vector<StyleTable> none;
        if (r.Failed() || !ReadShapeRecords(r, 1, none, m.startEdges, kMaxEdgesPerShape)) {
            Problem("a morph shape's start runs past the tag", tag);
            return;
        }
        if (endEdges < tag.pos + tag.length) {
            Reader end(data, endEdges, tag.pos + tag.length);
            std::vector<StyleTable> unused;
            if (!ReadShapeRecords(end, 1, unused, m.endEdges, kMaxEdgesPerShape)) {
                m.endEdges.clear();
                Problem("a morph shape's end runs past the tag", tag);
            }
        }
        if (!IsDefined(m.id)) {
            movie.morphs_.emplace(m.id, std::move(m));
        }
    }

    void Other(std::uint16_t id, CharacterKind kind) {
        if (!IsDefined(id)) {
            movie.others_[id] = kind;
        }
    }

    void DefineShape(TagHeader const& tag) {
        int const version = tag.code == 2 ? 1 : tag.code == 22 ? 2 : tag.code == 32 ? 3 : 4;
        Reader r(data, tag.pos, tag.pos + tag.length);
        Shape s;
        s.id = r.U16();
        s.version = version;
        s.bounds = r.ReadRect();
        s.edgeBounds = s.bounds;
        if (version == 4) {
            s.edgeBounds = r.ReadRect();
            r.U8();
        }
        s.styles.emplace_back();
        if (!ReadStyles(r, version, s.styles[0]) || !ReadShapeRecords(r, version, s.styles, s.edges, kMaxEdgesPerShape)) {
            Problem("a shape's records run past the tag", tag);
            return;
        }
        if (!IsDefined(s.id)) {
            movie.shapes_.emplace(s.id, std::move(s));
        }
    }

    void DefineEditText(TagHeader const& tag) {
        Reader r(data, tag.pos, tag.pos + tag.length);
        EditText t;
        t.id = r.U16();
        t.bounds = r.ReadRect();
        std::uint8_t const f1 = r.U8();
        std::uint8_t const f2 = r.U8();
        if (f1 & 0x01) {
            t.fontId = r.U16();
        }
        if (f2 & 0x80) {
            r.String();
        }
        if ((f1 & 0x01) || (f2 & 0x80)) {
            t.fontHeight = r.U16();
        }
        if (f1 & 0x04) {
            r.ReadRgb(true);
        }
        if (f1 & 0x02) {
            r.U16();
        }
        if (f2 & 0x20) {
            r.Skip(9);
        }
        t.variable = r.String();
        if (r.Failed()) {
            Problem("an edit text runs past the tag", tag);
        }
        if (!IsDefined(t.id)) {
            movie.editTexts_.emplace(t.id, std::move(t));
        }
    }

    void DefineFont(TagHeader const& tag) {
        Reader r(data, tag.pos, tag.pos + tag.length);
        Font f;
        f.version = tag.code == 75 ? 3 : 2;
        f.emScale = f.version == 3 ? 20 : 1;
        f.id = r.U16();
        f.flags = r.U8();
        f.language = r.U8();
        std::size_t const nameLength = r.U8();
        for (std::size_t i = 0; i < nameLength && !r.Failed(); ++i) {
            char const c = static_cast<char>(r.U8());
            if (c != 0) {
                f.name.push_back(c);
            }
        }
        std::size_t const count = r.U16();
        bool const wideOffsets = (f.flags & 0x08) != 0;
        bool const wideCodes = (f.flags & 0x04) != 0;
        std::size_t const tableStart = r.Pos();
        std::size_t const tagEnd = tag.pos + tag.length;
        if (r.Failed()) {
            Problem("a font's header runs past the tag", tag);
            return;
        }
        if (count > 0) {
            std::vector<std::size_t> offsets(count);
            for (std::size_t i = 0; i < count; ++i) {
                offsets[i] = wideOffsets ? r.U32() : r.U16();
            }
            std::size_t const codeOffset = wideOffsets ? r.U32() : r.U16();
            if (r.Failed() || codeOffset > tagEnd - tableStart) {
                Problem("a font's offset table runs past the tag", tag);
                return;
            }
            f.glyphs.resize(count);
            for (std::size_t i = 0; i < count; ++i) {
                std::size_t const start = tableStart + offsets[i];
                std::size_t const end = tableStart + (i + 1 < count ? offsets[i + 1] : codeOffset);
                if (start > tagEnd || end > tagEnd || end < start) {
                    Problem("a glyph's offset is outside the font", tag);
                    continue;
                }
                Reader g(data, start, end);
                std::vector<StyleTable> none;
                if (!ReadShapeRecords(g, 1, none, f.glyphs[i].edges, kMaxEdgesPerGlyph) || !none.empty()) {
                    f.glyphs[i].edges.clear();
                    Problem("a glyph's shape runs past its slot", tag);
                }
            }
            Reader c(data, tableStart + codeOffset, tagEnd);
            for (std::size_t i = 0; i < count; ++i) {
                f.glyphs[i].code = wideCodes ? c.U16() : c.U8();
            }
            if (c.Failed()) {
                Problem("a font's code table runs past the tag", tag);
                return;
            }
            if (f.flags & 0x80) {
                f.ascent = c.U16();
                f.descent = c.U16();
                f.leading = c.S16();
                // The spec says SI16, but no advance is negative and DefineFont3's 20x units overflow it for the widest
                // glyphs (Biomass's W: 35,000 > 32,767, read as negative, so the next letter was drawn over it): unsigned.
                for (std::size_t i = 0; i < count; ++i) {
                    f.glyphs[i].advance = c.U16();
                }
                for (std::size_t i = 0; i < count; ++i) {
                    f.glyphs[i].bounds = c.ReadRect();
                }
                std::size_t const kerns = c.U16();
                for (std::size_t i = 0; i < kerns && !c.Failed(); ++i) {
                    Kerning k;
                    k.left = wideCodes ? c.U16() : c.U8();
                    k.right = wideCodes ? c.U16() : c.U8();
                    k.adjustment = c.S16();
                    if (!c.Failed()) {
                        f.kerning.push_back(k);
                    }
                }
                f.hasLayout = true;
                if (c.Failed()) {
                    Problem("a font's layout runs past the tag", tag);
                }
            }
        }
        if (!IsDefined(f.id)) {
            movie.fonts_.emplace(f.id, std::move(f));
        }
    }

    void Export(TagHeader const& tag) {
        Reader r(data, tag.pos, tag.pos + tag.length);
        std::size_t const count = r.U16();
        for (std::size_t i = 0; i < count && !r.Failed(); ++i) {
            std::uint16_t const id = r.U16();
            std::string name = r.String();
            if (!r.Failed()) {
                movie.exports_.emplace_back(id, std::move(name));
            }
        }
        if (r.Failed()) {
            Problem("an export list runs past the tag", tag);
        }
    }

    void ImportList(TagHeader const& tag) {
        Reader r(data, tag.pos, tag.pos + tag.length);
        std::string const url = r.String();
        if (tag.code == 71) {
            r.U8();
            r.U8();
        }
        std::size_t const count = r.U16();
        for (std::size_t i = 0; i < count && !r.Failed(); ++i) {
            Import im;
            im.id = r.U16();
            im.name = r.String();
            im.url = url;
            if (!r.Failed()) {
                Other(im.id, CharacterKind::Imported);
                movie.imports_.push_back(std::move(im));
            }
        }
        if (r.Failed()) {
            Problem("an import list runs past the tag", tag);
        }
    }

    // Plays a timeline's control tags into frames. `top`: the file's own tag list, where definitions live too.
    void Timeline(std::size_t pos, std::size_t end, Sprite& sprite, bool top, int nesting) {
        std::map<std::uint16_t, Placement> current;
        std::vector<std::string> labels;
        int frame = 0;
        bool pending = false;
        auto showFrame = [&] {
            Frame f;
            f.labels = std::move(labels);
            labels.clear();
            if (storedPlacements + current.size() <= kMaxStoredPlacements) {
                f.displayList.reserve(current.size());
                for (auto const& [depth, p] : current) {
                    f.displayList.push_back(p);
                }
                storedPlacements += current.size();
            } else {
                ++movie.notes_.badTags;
            }
            sprite.frames.push_back(std::move(f));
            ++frame;
            pending = false;
        };
        TagHeader tag;
        while (ReadTag(data, pos, end, tag)) {
            ++movie.notes_.tags;
            Reader r(data, tag.pos, tag.pos + tag.length);
            switch (tag.code) {
            case 0:
                pos = end;
                break;
            case 1:
                showFrame();
                break;
            case 4: { // PlaceObject
                Placement p;
                p.character = r.U16();
                p.depth = r.U16();
                p.matrix = r.ReadMatrix();
                if (r.Remaining() > 0) {
                    p.color = r.ReadCxform(false);
                }
                p.placedFrame = frame;
                if (r.Failed()) {
                    Problem("PlaceObject runs past the tag", tag);
                    break;
                }
                current[p.depth] = std::move(p);
                pending = true;
                break;
            }
            case 26:
            case 70: { // PlaceObject2, PlaceObject3
                std::uint8_t const flags = r.U8();
                std::uint8_t const flags2 = tag.code == 70 ? r.U8() : 0;
                std::uint16_t const depth = r.U16();
                if (tag.code == 70 && ((flags2 & 0x08) || ((flags2 & 0x10) && (flags & 0x02)))) {
                    r.String();
                }
                std::optional<std::uint16_t> character;
                if (flags & 0x02) {
                    character = r.U16();
                }
                std::optional<Matrix> matrix;
                if (flags & 0x04) {
                    matrix = r.ReadMatrix();
                }
                std::optional<ColorTransform> color;
                if (flags & 0x08) {
                    color = r.ReadCxform(true);
                }
                std::optional<std::uint16_t> ratio;
                if (flags & 0x10) {
                    ratio = r.U16();
                }
                std::optional<std::string> name;
                if (flags & 0x20) {
                    name = r.String();
                }
                std::optional<std::uint16_t> clipDepth;
                if (flags & 0x40) {
                    clipDepth = r.U16();
                }
                std::optional<std::uint8_t> filters, blend;
                std::optional<bool> visible;
                if (tag.code == 70) {
                    if (flags2 & 0x01) {
                        std::uint8_t n = 0;
                        SkipFilters(r, n);
                        filters = n;
                    }
                    if (flags2 & 0x02) {
                        blend = r.U8();
                    }
                    if (flags2 & 0x04) {
                        r.U8();
                    }
                    if ((flags2 & 0x20) && r.Remaining() >= 1) {
                        visible = r.U8() != 0;
                    }
                }
                if (r.Failed()) {
                    Problem("PlaceObject2/3 runs past the tag", tag);
                    break;
                }
                auto it = current.find(depth);
                Placement p;
                if ((flags & 0x01) && it != current.end()) {
                    p = it->second;
                    if (character && *character != p.character) {
                        p.character = *character;
                        p.placedFrame = frame;
                    }
                } else if (character) {
                    p.depth = depth;
                    p.character = *character;
                    p.placedFrame = frame;
                } else {
                    break;
                }
                if (matrix) {
                    p.matrix = *matrix;
                }
                if (color) {
                    p.color = *color;
                }
                if (ratio) {
                    p.ratio = *ratio;
                }
                if (name) {
                    p.name = std::move(*name);
                }
                if (clipDepth) {
                    p.clipDepth = *clipDepth;
                }
                if (filters) {
                    p.filterCount = *filters;
                }
                if (blend) {
                    p.blendMode = *blend;
                }
                if (visible) {
                    p.visible = *visible;
                }
                current[depth] = std::move(p);
                pending = true;
                break;
            }
            case 5: // RemoveObject
                r.U16();
                current.erase(r.U16());
                pending = true;
                break;
            case 28: // RemoveObject2
                current.erase(r.U16());
                pending = true;
                break;
            case 43: { // FrameLabel
                std::string label = r.String();
                if (!label.empty()) {
                    labels.push_back(std::move(label));
                }
                pending = true;
                break;
            }
            default:
                if (top) {
                    Definition(tag, nesting);
                } else if (tag.code != 12 && tag.code != 15 && tag.code != 18 && tag.code != 19 && tag.code != 45 &&
                           tag.code != 89) {
                    ++movie.notes_.unknownTags;
                }
                break;
            }
        }
        if (pending || sprite.frames.empty()) {
            showFrame();
        }
    }

    void Definition(TagHeader const& tag, int nesting) {
        Reader r(data, tag.pos, tag.pos + tag.length);
        switch (tag.code) {
        case 2:
        case 22:
        case 32:
        case 83:
            DefineShape(tag);
            break;
        case 39: {
            if (nesting >= kMaxSpriteNesting) {
                Problem("sprites nested too deep", tag);
                break;
            }
            Sprite s;
            s.id = r.U16();
            s.declaredFrames = r.U16();
            if (r.Failed()) {
                Problem("a sprite's header runs past the tag", tag);
                break;
            }
            Timeline(r.Pos(), tag.pos + tag.length, s, false, nesting + 1);
            if (s.id != 0 && !IsDefined(s.id)) {
                movie.sprites_.emplace(s.id, std::move(s));
            }
            break;
        }
        case 37:
            DefineEditText(tag);
            break;
        case 48:
        case 75:
            DefineFont(tag);
            break;
        case 56:
            Export(tag);
            break;
        case 57:
        case 71:
            ImportList(tag);
            break;
        case 78: {
            std::uint16_t const id = r.U16();
            Rect const grid = r.ReadRect();
            if (!r.Failed()) {
                grids[id] = grid;
            }
            break;
        }
        case 6:
        case 20:
        case 21:
        case 35:
        case 36:
        case 90:
        case 1001:
        case 1008:
            Other(r.U16(), CharacterKind::Bitmap);
            break;
        case 1009:
            Other(static_cast<std::uint16_t>(r.U32()), CharacterKind::Bitmap);
            break;
        case 46:
        case 84:
            DefineMorph(tag);
            break;
        case 11:
        case 33:
            Other(r.U16(), CharacterKind::Text);
            break;
        case 7:
        case 34:
            Other(r.U16(), CharacterKind::Button);
            break;
        case 10:
        case 14:
        case 60:
        case 87:
        case 91:
        case 1005:
        case 1006:
        case 1007:
            Other(r.U16(), CharacterKind::Other);
            break;
        case 8: case 9: case 12: case 13: case 15: case 18: case 19: case 24: case 45: case 58: case 59:
        case 62: case 64: case 65: case 66: case 69: case 73: case 74: case 76: case 77: case 82: case 86:
        case 88: case 89: case 1000: case 1002: case 1003: case 1004:
            break;
        default:
            ++movie.notes_.unknownTags;
            break;
        }
    }
};

std::optional<Movie> Movie::Parse(std::span<std::uint8_t const> bytes, std::string* error) {
    auto fail = [&](char const* why) -> std::optional<Movie> {
        if (error) {
            *error = why;
        }
        return std::nullopt;
    };
    if (bytes.size() < 8) {
        return fail("too short for a SWF/GFx header");
    }
    char const s0 = static_cast<char>(bytes[0]);
    char const s1 = static_cast<char>(bytes[1]);
    char const s2 = static_cast<char>(bytes[2]);
    bool const fws = s0 == 'F' && s1 == 'W' && s2 == 'S';
    bool const gfx = s0 == 'G' && s1 == 'F' && s2 == 'X';
    if ((s0 == 'C' && s1 == 'W' && s2 == 'S') || (s0 == 'C' && s1 == 'F' && s2 == 'X')) {
        return fail("a zlib-compressed movie (CWS/CFX): this reader takes uncompressed GFX/FWS only");
    }
    if (s0 == 'Z' && s1 == 'W' && s2 == 'S') {
        return fail("an LZMA-compressed movie (ZWS): this reader takes uncompressed GFX/FWS only");
    }
    if (!fws && !gfx) {
        return fail("not a SWF/GFx movie (no FWS/GFX signature)");
    }
    Movie movie;
    movie.gfx_ = gfx;
    movie.version_ = bytes[3];
    std::size_t const declared = static_cast<std::size_t>(bytes[4]) | (static_cast<std::size_t>(bytes[5]) << 8) |
                                 (static_cast<std::size_t>(bytes[6]) << 16) | (static_cast<std::size_t>(bytes[7]) << 24);
    std::size_t const end = declared >= 8 ? std::min(declared, bytes.size()) : bytes.size();
    Reader r(bytes, 8, end);
    movie.frameRect_ = r.ReadRect();
    movie.frameRate_ = r.U16() / 256.0f;
    std::uint16_t const frames = r.U16();
    if (r.Failed()) {
        return fail("the header runs past the end of the data");
    }
    MovieParser parser{movie, bytes.first(end)};
    Sprite root;
    root.id = 0;
    root.declaredFrames = frames;
    parser.Timeline(r.Pos(), end, root, true, 0);
    movie.sprites_[0] = std::move(root);
    for (auto const& [id, grid] : parser.grids) {
        if (auto it = movie.sprites_.find(id); it != movie.sprites_.end() && id != 0) {
            it->second.scalingGrid = grid;
        }
    }
    return movie;
}

CharacterKind Movie::KindOf(std::uint16_t id) const {
    if (shapes_.contains(id)) {
        return CharacterKind::Shape;
    }
    if (morphs_.contains(id)) {
        return CharacterKind::MorphShape;
    }
    if (sprites_.contains(id)) {
        return CharacterKind::Sprite;
    }
    if (editTexts_.contains(id)) {
        return CharacterKind::EditText;
    }
    if (fonts_.contains(id)) {
        return CharacterKind::Font;
    }
    if (auto it = others_.find(id); it != others_.end()) {
        return it->second;
    }
    return CharacterKind::None;
}

Shape const* Movie::FindShape(std::uint16_t id) const {
    auto it = shapes_.find(id);
    return it == shapes_.end() ? nullptr : &it->second;
}

MorphShape const* Movie::FindMorphShape(std::uint16_t id) const {
    auto it = morphs_.find(id);
    return it == morphs_.end() ? nullptr : &it->second;
}

namespace {

double Mix(double a, double b, double t) {
    return a + (b - a) * t;
}

std::int32_t MixCoord(double a, double b, double t) {
    return static_cast<std::int32_t>(std::lround(std::clamp(Mix(a, b, t), -1073741824.0, 1073741824.0)));
}

std::uint8_t MixByte(std::uint8_t a, std::uint8_t b, double t) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(Mix(a, b, t)), 0L, 255L));
}

Rgba MixColor(Rgba const& a, Rgba const& b, double t) {
    return {MixByte(a.r, b.r, t), MixByte(a.g, b.g, t), MixByte(a.b, b.b, t), MixByte(a.a, b.a, t)};
}

FillStyle MixFill(FillStyle const& a, FillStyle const& b, double t) {
    FillStyle f = a;
    f.color = MixColor(a.color, b.color, t);
    f.matrix = {Mix(a.matrix.a, b.matrix.a, t), Mix(a.matrix.b, b.matrix.b, t), Mix(a.matrix.c, b.matrix.c, t),
                Mix(a.matrix.d, b.matrix.d, t), Mix(a.matrix.tx, b.matrix.tx, t), Mix(a.matrix.ty, b.matrix.ty, t)};
    for (std::size_t i = 0; i < f.stops.size() && i < b.stops.size(); ++i) {
        f.stops[i].ratio = MixByte(a.stops[i].ratio, b.stops[i].ratio, t);
        f.stops[i].color = MixColor(a.stops[i].color, b.stops[i].color, t);
    }
    f.focal = static_cast<float>(Mix(a.focal, b.focal, t));
    return f;
}

} // namespace

Shape MorphAt(MorphShape const& morph, double t) {
    t = std::isfinite(t) ? std::clamp(t, 0.0, 1.0) : 0.0;
    Shape s;
    s.id = morph.id;
    s.version = morph.version == 2 ? 4 : 3;
    s.bounds = {MixCoord(morph.startBounds.xMin, morph.endBounds.xMin, t), MixCoord(morph.startBounds.xMax, morph.endBounds.xMax, t),
                MixCoord(morph.startBounds.yMin, morph.endBounds.yMin, t), MixCoord(morph.startBounds.yMax, morph.endBounds.yMax, t)};
    s.edgeBounds = s.bounds;
    StyleTable table;
    for (std::size_t i = 0; i < morph.startFills.size(); ++i) {
        table.fills.push_back(i < morph.endFills.size() ? MixFill(morph.startFills[i], morph.endFills[i], t) : morph.startFills[i]);
    }
    for (std::size_t i = 0; i < morph.startLines.size(); ++i) {
        LineStyle l = morph.startLines[i];
        if (i < morph.endLines.size()) {
            LineStyle const& e = morph.endLines[i];
            l.width = static_cast<std::uint16_t>(std::clamp(std::lround(Mix(l.width, e.width, t)), 0L, 65535L));
            l.color = MixColor(l.color, e.color, t);
            l.fill = MixFill(l.fill, e.fill, t);
        }
        table.lines.push_back(std::move(l));
    }
    s.styles.push_back(std::move(table));
    s.edges.reserve(morph.startEdges.size());
    for (std::size_t i = 0; i < morph.startEdges.size(); ++i) {
        Edge e = morph.startEdges[i];
        e.table = 0;
        if (i < morph.endEdges.size()) {
            Edge const& a = morph.startEdges[i];
            Edge const& b = morph.endEdges[i];
            // A straight edge tweens as a curve whose control point is its middle.
            double const acx = a.curve ? a.cx : (a.x0 + static_cast<double>(a.x1)) / 2;
            double const acy = a.curve ? a.cy : (a.y0 + static_cast<double>(a.y1)) / 2;
            double const bcx = b.curve ? b.cx : (b.x0 + static_cast<double>(b.x1)) / 2;
            double const bcy = b.curve ? b.cy : (b.y0 + static_cast<double>(b.y1)) / 2;
            e.x0 = MixCoord(a.x0, b.x0, t);
            e.y0 = MixCoord(a.y0, b.y0, t);
            e.x1 = MixCoord(a.x1, b.x1, t);
            e.y1 = MixCoord(a.y1, b.y1, t);
            e.curve = a.curve || b.curve;
            e.cx = e.curve ? MixCoord(acx, bcx, t) : e.x0;
            e.cy = e.curve ? MixCoord(acy, bcy, t) : e.y0;
        }
        s.edges.push_back(e);
    }
    return s;
}

Sprite const* Movie::FindSprite(std::uint16_t id) const {
    auto it = sprites_.find(id);
    return it == sprites_.end() ? nullptr : &it->second;
}

EditText const* Movie::FindEditText(std::uint16_t id) const {
    auto it = editTexts_.find(id);
    return it == editTexts_.end() ? nullptr : &it->second;
}

Font const* Movie::FindFont(std::uint16_t id) const {
    auto it = fonts_.find(id);
    return it == fonts_.end() ? nullptr : &it->second;
}

std::optional<int> Movie::FrameOf(std::uint16_t sprite, std::string_view label) const {
    Sprite const* s = FindSprite(sprite);
    if (!s) {
        return std::nullopt;
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t i = 0; i < s->frames.size(); ++i) {
            for (std::string const& l : s->frames[i].labels) {
                if (pass == 0 ? l == label : EqualsNoCase(l, label)) {
                    return static_cast<int>(i);
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<std::uint16_t> Movie::Exported(std::string_view name) const {
    for (int pass = 0; pass < 2; ++pass) {
        for (auto const& [id, n] : exports_) {
            if (pass == 0 ? n == name : EqualsNoCase(n, name)) {
                return id;
            }
        }
    }
    return std::nullopt;
}

} // namespace mevr::gfx
