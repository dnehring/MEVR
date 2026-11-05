// SPDX-License-Identifier: MIT
#include "core/gfx/SwfFont.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace mevr::gfx {

std::vector<FontInfo> Fonts(Movie const& movie) {
    std::vector<FontInfo> out;
    for (auto const& [id, f] : movie.FontTable()) {
        FontInfo info;
        info.id = id;
        info.name = f.name;
        info.glyphCount = f.glyphs.size();
        info.version = f.version;
        info.hasLayout = f.hasLayout;
        info.kerningPairs = f.kerning.size();
        info.bold = f.Bold();
        info.italic = f.Italic();
        out.push_back(std::move(info));
    }
    return out;
}

namespace {

constexpr int kUnitsPerEm = 1024;
constexpr std::size_t kMaxKernPairs = (65535 - 14) / 6;

struct TtPoint {
    std::int32_t x = 0;
    std::int32_t y = 0;
    bool on = true;
};

using Contour = std::vector<TtPoint>;

struct TtGlyph {
    std::vector<Contour> contours;
    std::int32_t advance = 0;
    std::int32_t xMin = 0, yMin = 0, xMax = 0, yMax = 0;
    bool HasOutline() const { return !contours.empty(); }
};

std::int32_t Clamp16(double v) {
    return static_cast<std::int32_t>(std::clamp(std::lround(v), -32768L, 32767L));
}

// The glyph's edges as closed loops, each edge oriented with the glyph on its fill1 side (fill0 edges reversed),
// chained end to start: the same convention for every glyph, so every outer loop winds one way and every hole the
// other.
std::vector<Contour> GlyphContours(Glyph const& glyph, int emScale) {
    struct Oriented {
        std::int64_t x0, y0, cx, cy, x1, y1;
        bool curve;
    };
    std::vector<Oriented> edges;
    edges.reserve(glyph.edges.size());
    for (Edge const& e : glyph.edges) {
        bool const left = e.fill0 != 0;
        bool const right = e.fill1 != 0;
        if (left == right) {
            continue;
        }
        if (right) {
            edges.push_back({e.x0, e.y0, e.cx, e.cy, e.x1, e.y1, e.curve});
        } else {
            edges.push_back({e.x1, e.y1, e.cx, e.cy, e.x0, e.y0, e.curve});
        }
    }
    std::multimap<std::pair<std::int64_t, std::int64_t>, std::size_t> starts;
    for (std::size_t i = 0; i < edges.size(); ++i) {
        starts.emplace(std::pair{edges[i].x0, edges[i].y0}, i);
    }
    std::vector<bool> used(edges.size(), false);
    double const k = 1.0 / std::max(1, emScale);
    std::vector<Contour> contours;
    for (std::size_t first = 0; first < edges.size(); ++first) {
        if (used[first]) {
            continue;
        }
        Contour c;
        std::size_t i = first;
        std::pair<std::int64_t, std::int64_t> const home{edges[first].x0, edges[first].y0};
        while (true) {
            used[i] = true;
            Oriented const& e = edges[i];
            c.push_back({Clamp16(e.x0 * k), Clamp16(-e.y0 * k), true});
            if (e.curve) {
                c.push_back({Clamp16(e.cx * k), Clamp16(-e.cy * k), false});
            }
            std::pair<std::int64_t, std::int64_t> const end{e.x1, e.y1};
            if (end == home) {
                break;
            }
            std::size_t next = edges.size();
            auto [lo, hi] = starts.equal_range(end);
            for (auto it = lo; it != hi; ++it) {
                if (!used[it->second]) {
                    next = it->second;
                    break;
                }
            }
            if (next == edges.size()) {
                break; // open: TrueType closes it with a line
            }
            i = next;
        }
        // Drop on-curve points that repeat the one before (after rounding), and the closing repeat of the first.
        Contour clean;
        for (TtPoint const& p : c) {
            if (!clean.empty() && p.on && clean.back().on && p.x == clean.back().x && p.y == clean.back().y) {
                continue;
            }
            clean.push_back(p);
        }
        while (clean.size() > 1 && clean.back().on && clean.back().x == clean.front().x && clean.back().y == clean.front().y) {
            clean.pop_back();
        }
        if (clean.size() >= 3) {
            contours.push_back(std::move(clean));
        }
    }
    return contours;
}

double SignedArea(Contour const& c) {
    double a = 0;
    for (std::size_t i = 0; i < c.size(); ++i) {
        TtPoint const& p = c[i];
        TtPoint const& q = c[(i + 1) % c.size()];
        a += static_cast<double>(p.x) * q.y - static_cast<double>(q.x) * p.y;
    }
    return a / 2;
}

void Reverse(Contour& c) {
    std::reverse(c.begin(), c.end());
    // Start on an on-curve point again (TrueType allows otherwise, but keep it plain).
    auto it = std::find_if(c.begin(), c.end(), [](TtPoint const& p) { return p.on; });
    if (it != c.end()) {
        std::rotate(c.begin(), it, c.end());
    }
}

// Big-endian bytes.
class Be {
public:
    std::vector<std::uint8_t> bytes;

    void U8(std::uint32_t v) { bytes.push_back(static_cast<std::uint8_t>(v)); }
    void U16(std::uint32_t v) {
        bytes.push_back(static_cast<std::uint8_t>(v >> 8));
        bytes.push_back(static_cast<std::uint8_t>(v));
    }
    void I16(std::int32_t v) { U16(static_cast<std::uint32_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(v, -32768, 32767))))); }
    void U32(std::uint32_t v) {
        U16(v >> 16);
        U16(v & 0xFFFF);
    }
    void Tag(char const* t) {
        for (int i = 0; i < 4; ++i) {
            U8(static_cast<std::uint8_t>(t[i]));
        }
    }
    void Append(std::vector<std::uint8_t> const& b) { bytes.insert(bytes.end(), b.begin(), b.end()); }
    void Pad4() {
        while (bytes.size() % 4) {
            bytes.push_back(0);
        }
    }
    std::size_t Size() const { return bytes.size(); }
    void PatchU32(std::size_t at, std::uint32_t v) {
        bytes[at] = static_cast<std::uint8_t>(v >> 24);
        bytes[at + 1] = static_cast<std::uint8_t>(v >> 16);
        bytes[at + 2] = static_cast<std::uint8_t>(v >> 8);
        bytes[at + 3] = static_cast<std::uint8_t>(v);
    }
};

std::uint32_t Checksum(std::vector<std::uint8_t> const& b, std::size_t from, std::size_t length) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < length; i += 4) {
        std::uint32_t v = 0;
        for (std::size_t k = 0; k < 4; ++k) {
            v = (v << 8) | (i + k < length ? b[from + i + k] : 0u);
        }
        sum += v;
    }
    return sum;
}

std::uint16_t Log2Floor(std::size_t n) {
    std::uint16_t k = 0;
    while ((std::size_t{2} << k) <= n) {
        ++k;
    }
    return k;
}

std::u16string Utf16(std::string const& s) {
    std::u16string out;
    std::size_t i = 0;
    while (i < s.size()) {
        auto const c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0xFFFD;
        std::size_t n = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6 && i + 1 < s.size()) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            n = 2;
        } else if ((c >> 4) == 0xE && i + 2 < s.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            n = 3;
        } else if ((c >> 3) == 0x1E && i + 3 < s.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                 ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
            n = 4;
        }
        i += n;
        if (cp >= 0x10000 && cp <= 0x10FFFF) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp > 0xFFFF ? 0xFFFD : cp));
        }
    }
    return out;
}

std::string PostScriptName(std::string const& family, std::string const& style) {
    std::string out;
    for (char const c : family + "-" + style) {
        if (c > 32 && c < 127 && c != '[' && c != ']' && c != '(' && c != ')' && c != '{' && c != '}' && c != '<' &&
            c != '>' && c != '/' && c != '%') {
            out.push_back(c);
        }
    }
    if (out.size() > 63) {
        out.resize(63);
    }
    return out.empty() ? std::string("MEVRFont") : out;
}

std::vector<std::uint8_t> NameTable(std::string const& family, std::string const& style) {
    std::string const full = style == "Regular" ? family : family + " " + style;
    std::vector<std::pair<int, std::u16string>> names = {
        {1, Utf16(family)},
        {2, Utf16(style)},
        {3, Utf16("MEVR:" + PostScriptName(family, style))},
        {4, Utf16(full)},
        {5, Utf16("Version 1.000")},
        {6, Utf16(PostScriptName(family, style))},
    };
    Be b;
    b.U16(0);
    b.U16(static_cast<std::uint32_t>(names.size()));
    b.U16(static_cast<std::uint32_t>(6 + 12 * names.size()));
    std::size_t offset = 0;
    Be strings;
    for (auto const& [id, text] : names) {
        std::size_t const length = std::min<std::size_t>(text.size() * 2, 0xFFFE);
        b.U16(3);      // Windows
        b.U16(1);      // Unicode BMP
        b.U16(0x0409); // en-US
        b.U16(static_cast<std::uint32_t>(id));
        b.U16(static_cast<std::uint32_t>(length));
        b.U16(static_cast<std::uint32_t>(offset));
        for (std::size_t i = 0; i < length / 2; ++i) {
            strings.U16(text[i]);
        }
        offset += length;
    }
    b.Append(strings.bytes);
    return b.bytes;
}

std::vector<std::uint8_t> CmapTable(std::vector<std::pair<std::uint16_t, std::uint16_t>> const& map) {
    struct Segment {
        std::uint16_t start, end;
        std::uint16_t delta;
    };
    std::vector<Segment> segs;
    for (auto const& [code, glyph] : map) {
        std::uint16_t const delta = static_cast<std::uint16_t>(glyph - code);
        if (!segs.empty() && segs.back().end + 1 == code && segs.back().delta == delta) {
            segs.back().end = code;
        } else {
            segs.push_back({code, code, delta});
        }
    }
    segs.push_back({0xFFFF, 0xFFFF, 1});
    std::size_t const n = segs.size();
    std::uint16_t const log = Log2Floor(n);
    std::uint32_t const searchRange = 2u << log;
    Be sub;
    sub.U16(4);
    sub.U16(static_cast<std::uint32_t>(16 + 8 * n));
    sub.U16(0);
    sub.U16(static_cast<std::uint32_t>(2 * n));
    sub.U16(searchRange);
    sub.U16(log);
    sub.U16(static_cast<std::uint32_t>(2 * n - searchRange));
    for (Segment const& s : segs) {
        sub.U16(s.end);
    }
    sub.U16(0);
    for (Segment const& s : segs) {
        sub.U16(s.start);
    }
    for (Segment const& s : segs) {
        sub.U16(s.delta);
    }
    for (std::size_t i = 0; i < n; ++i) {
        sub.U16(0);
    }
    Be b;
    b.U16(0);
    b.U16(2);
    b.U16(0); // Unicode
    b.U16(3); // BMP
    b.U32(20);
    b.U16(3); // Windows
    b.U16(1); // Unicode BMP
    b.U32(20);
    b.Append(sub.bytes);
    return b.bytes;
}

std::vector<std::uint8_t> GlyphRecord(TtGlyph const& g) {
    Be b;
    if (!g.HasOutline()) {
        return {};
    }
    b.I16(static_cast<std::int32_t>(g.contours.size()));
    b.I16(g.xMin);
    b.I16(g.yMin);
    b.I16(g.xMax);
    b.I16(g.yMax);
    std::size_t end = 0;
    for (Contour const& c : g.contours) {
        end += c.size();
        b.U16(static_cast<std::uint32_t>(end - 1));
    }
    b.U16(0); // no instructions
    Be xs, ys;
    std::int32_t px = 0, py = 0;
    for (Contour const& c : g.contours) {
        for (TtPoint const& p : c) {
            std::uint32_t flag = p.on ? 0x01u : 0x00u;
            std::int32_t const dx = p.x - px;
            std::int32_t const dy = p.y - py;
            if (dx == 0) {
                flag |= 0x10;
            } else if (std::abs(dx) < 256) {
                flag |= 0x02 | (dx > 0 ? 0x10 : 0);
                xs.U8(static_cast<std::uint32_t>(std::abs(dx)));
            } else {
                xs.I16(dx);
            }
            if (dy == 0) {
                flag |= 0x20;
            } else if (std::abs(dy) < 256) {
                flag |= 0x04 | (dy > 0 ? 0x20 : 0);
                ys.U8(static_cast<std::uint32_t>(std::abs(dy)));
            } else {
                ys.I16(dy);
            }
            b.U8(flag);
            px = p.x;
            py = p.y;
        }
    }
    b.Append(xs.bytes);
    b.Append(ys.bytes);
    b.Pad4();
    return b.bytes;
}

bool Wanted(std::uint16_t code, bool everyGlyph) {
    if (code == 0xFFFF) {
        return false;
    }
    if (everyGlyph) {
        return code >= 0x20;
    }
    return (code >= 0x20 && code <= 0x7E) || (code >= 0xA0 && code <= 0x17F);
}

} // namespace

std::optional<std::vector<std::uint8_t>> FontAsTtf(Movie const& movie, std::uint16_t fontId, std::string const& familyName,
                                                    TtfOptions const& options, std::string* error) {
    auto fail = [&](std::string const& why) -> std::optional<std::vector<std::uint8_t>> {
        if (error) {
            *error = why;
        }
        return std::nullopt;
    };
    Font const* font = movie.FindFont(fontId);
    if (!font) {
        return fail("the movie has no DefineFont2/3 with id " + std::to_string(fontId));
    }
    int const em = std::max(1, font->emScale);
    double const k = 1.0 / em;

    // Glyph 0 is .notdef (empty); then the font's glyphs in code order.
    std::vector<std::pair<std::uint16_t, Glyph const*>> picked;
    for (Glyph const& g : font->glyphs) {
        if (Wanted(g.code, options.everyGlyph)) {
            picked.emplace_back(g.code, &g);
        }
    }
    std::sort(picked.begin(), picked.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    picked.erase(std::unique(picked.begin(), picked.end(), [](auto const& a, auto const& b) { return a.first == b.first; }),
                 picked.end());
    if (picked.empty()) {
        return fail("font " + std::to_string(fontId) + " has no glyph in the wanted ranges");
    }

    std::vector<TtGlyph> glyphs(picked.size() + 1);
    glyphs[0].advance = kUnitsPerEm / 2;
    std::vector<std::pair<std::uint16_t, std::uint16_t>> cmap;
    std::unordered_map<std::uint16_t, std::uint16_t> glyphOfCode;
    double area = 0;
    for (std::size_t i = 0; i < picked.size(); ++i) {
        TtGlyph& t = glyphs[i + 1];
        t.contours = GlyphContours(*picked[i].second, em);
        for (Contour const& c : t.contours) {
            area += SignedArea(c);
        }
        cmap.emplace_back(picked[i].first, static_cast<std::uint16_t>(i + 1));
        glyphOfCode[picked[i].first] = static_cast<std::uint16_t>(i + 1);
    }
    // Outer contours clockwise (TrueType's way; y up, so a negative shoelace area). Every glyph shares the movie's
    // fill1 convention, so one decision for the whole font.
    if (area > 0) {
        for (TtGlyph& t : glyphs) {
            for (Contour& c : t.contours) {
                Reverse(c);
            }
        }
    }
    // A no-break space that the font lacks reads as its space.
    if (!glyphOfCode.contains(0xA0) && glyphOfCode.contains(0x20)) {
        cmap.emplace_back(static_cast<std::uint16_t>(0xA0), glyphOfCode[0x20]);
        std::sort(cmap.begin(), cmap.end());
    }

    std::int32_t fontXMin = 0, fontYMin = 0, fontXMax = 0, fontYMax = 0;
    bool anyOutline = false;
    std::size_t maxPoints = 0, maxContours = 0;
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        TtGlyph& t = glyphs[i];
        if (i > 0) {
            Glyph const& src = *picked[i - 1].second;
            t.advance = font->hasLayout ? Clamp16(src.advance * k) : 0;
        }
        std::size_t points = 0;
        bool first = true;
        for (Contour const& c : t.contours) {
            points += c.size();
            for (TtPoint const& p : c) {
                if (first) {
                    t.xMin = t.xMax = p.x;
                    t.yMin = t.yMax = p.y;
                    first = false;
                }
                t.xMin = std::min(t.xMin, p.x);
                t.xMax = std::max(t.xMax, p.x);
                t.yMin = std::min(t.yMin, p.y);
                t.yMax = std::max(t.yMax, p.y);
            }
        }
        if (i > 0 && !font->hasLayout) {
            t.advance = t.HasOutline() ? t.xMax + kUnitsPerEm / 10 : kUnitsPerEm / 4;
        }
        t.advance = std::max(0, t.advance);
        if (points > 0xFFFF) {
            t.contours.clear();
            points = 0;
        }
        maxPoints = std::max(maxPoints, points);
        maxContours = std::max(maxContours, t.contours.size());
        if (t.HasOutline()) {
            if (!anyOutline) {
                fontXMin = t.xMin;
                fontYMin = t.yMin;
                fontXMax = t.xMax;
                fontYMax = t.yMax;
                anyOutline = true;
            }
            fontXMin = std::min(fontXMin, t.xMin);
            fontYMin = std::min(fontYMin, t.yMin);
            fontXMax = std::max(fontXMax, t.xMax);
            fontYMax = std::max(fontYMax, t.yMax);
        }
    }

    std::int32_t ascender = font->hasLayout ? Clamp16(font->ascent * k) : fontYMax;
    std::int32_t descender = font->hasLayout ? -Clamp16(font->descent * k) : fontYMin;
    std::int32_t const lineGap = font->hasLayout ? std::max(0, Clamp16(font->leading * k)) : 0;
    if (ascender <= 0 && descender >= 0) {
        ascender = kUnitsPerEm * 4 / 5;
        descender = -kUnitsPerEm / 5;
    }

    // glyf and loca (long offsets).
    Be glyf;
    std::vector<std::uint32_t> loca;
    for (TtGlyph const& t : glyphs) {
        loca.push_back(static_cast<std::uint32_t>(glyf.Size()));
        glyf.Append(GlyphRecord(t));
    }
    loca.push_back(static_cast<std::uint32_t>(glyf.Size()));
    Be locaTable;
    for (std::uint32_t const o : loca) {
        locaTable.U32(o);
    }

    // hmtx, hhea's extremes.
    Be hmtx;
    std::int32_t advanceMax = 0, minLsb = 0, minRsb = 0, maxExtent = 0;
    bool firstMetric = true;
    double advanceSum = 0;
    int advanceCount = 0;
    for (TtGlyph const& t : glyphs) {
        std::int32_t const lsb = t.HasOutline() ? t.xMin : 0;
        hmtx.U16(static_cast<std::uint32_t>(std::clamp(t.advance, 0, 65535)));
        hmtx.I16(lsb);
        advanceMax = std::max(advanceMax, t.advance);
        if (t.advance > 0) {
            advanceSum += t.advance;
            ++advanceCount;
        }
        if (t.HasOutline()) {
            std::int32_t const rsb = t.advance - t.xMax;
            std::int32_t const extent = lsb + (t.xMax - t.xMin);
            if (firstMetric) {
                minLsb = lsb;
                minRsb = rsb;
                maxExtent = extent;
                firstMetric = false;
            }
            minLsb = std::min(minLsb, lsb);
            minRsb = std::min(minRsb, rsb);
            maxExtent = std::max(maxExtent, extent);
        }
    }

    std::string const style = !options.styleName.empty() ? options.styleName
                              : font->Bold() && font->Italic() ? "Bold Italic"
                              : font->Bold()                   ? "Bold"
                              : font->Italic()                 ? "Italic"
                                                               : "Regular";

    Be head;
    head.U32(0x00010000);
    head.U32(0x00010000);
    head.U32(0); // checkSumAdjustment, patched below
    head.U32(0x5F0F3CF5);
    head.U16(0x0003); // baseline at y = 0, lsb at x = 0
    head.U16(kUnitsPerEm);
    head.U32(0);
    head.U32(0); // created
    head.U32(0);
    head.U32(0); // modified
    head.I16(fontXMin);
    head.I16(fontYMin);
    head.I16(fontXMax);
    head.I16(fontYMax);
    head.U16((font->Bold() ? 0x01u : 0u) | (font->Italic() ? 0x02u : 0u));
    head.U16(8);
    head.I16(2);
    head.I16(1); // long loca
    head.I16(0);

    Be hhea;
    hhea.U32(0x00010000);
    hhea.I16(ascender);
    hhea.I16(descender);
    hhea.I16(lineGap);
    hhea.U16(static_cast<std::uint32_t>(std::clamp(advanceMax, 0, 65535)));
    hhea.I16(minLsb);
    hhea.I16(minRsb);
    hhea.I16(maxExtent);
    hhea.I16(1);
    hhea.I16(0);
    hhea.I16(0);
    for (int i = 0; i < 5; ++i) {
        hhea.I16(0);
    }
    hhea.U16(static_cast<std::uint32_t>(glyphs.size()));

    Be maxp;
    maxp.U32(0x00010000);
    maxp.U16(static_cast<std::uint32_t>(glyphs.size()));
    maxp.U16(static_cast<std::uint32_t>(maxPoints));
    maxp.U16(static_cast<std::uint32_t>(maxContours));
    maxp.U16(0);
    maxp.U16(0);
    maxp.U16(2);
    for (int i = 0; i < 8; ++i) {
        maxp.U16(0);
    }

    Be post;
    post.U32(0x00030000);
    post.U32(0);
    post.I16(-kUnitsPerEm / 10);
    post.I16(kUnitsPerEm / 20);
    post.U32(0);
    for (int i = 0; i < 4; ++i) {
        post.U32(0);
    }

    auto yMaxOf = [&](std::uint16_t code) -> std::int32_t {
        auto it = glyphOfCode.find(code);
        return it == glyphOfCode.end() ? 0 : glyphs[it->second].yMax;
    };
    std::uint16_t const firstChar = cmap.front().first;
    std::uint16_t const lastChar = cmap.back().first;
    std::uint32_t ranges1 = 0;
    for (auto const& [code, glyph] : cmap) {
        if (code < 0x80) {
            ranges1 |= 1u << 0;
        } else if (code < 0x100) {
            ranges1 |= 1u << 1;
        } else if (code < 0x180) {
            ranges1 |= 1u << 2;
        }
    }
    Be os2;
    os2.U16(4);
    os2.I16(advanceCount > 0 ? static_cast<std::int32_t>(std::lround(advanceSum / advanceCount)) : kUnitsPerEm / 2);
    os2.U16(font->Bold() ? 700 : 400);
    os2.U16(5);
    os2.U16(0); // installable
    os2.I16(kUnitsPerEm * 65 / 100);
    os2.I16(kUnitsPerEm * 60 / 100);
    os2.I16(0);
    os2.I16(kUnitsPerEm * 14 / 100);
    os2.I16(kUnitsPerEm * 65 / 100);
    os2.I16(kUnitsPerEm * 60 / 100);
    os2.I16(0);
    os2.I16(kUnitsPerEm * 48 / 100);
    os2.I16(kUnitsPerEm / 20);
    os2.I16(kUnitsPerEm * 26 / 100);
    os2.I16(0);
    for (int i = 0; i < 10; ++i) {
        os2.U8(0);
    }
    os2.U32(ranges1);
    os2.U32(0);
    os2.U32(0);
    os2.U32(0);
    os2.Tag("NONE");
    os2.U16((font->Bold() ? 0x20u : 0u) | (font->Italic() ? 0x01u : 0u) | (!font->Bold() && !font->Italic() ? 0x40u : 0u));
    os2.U16(firstChar);
    os2.U16(lastChar);
    os2.I16(ascender);
    os2.I16(descender);
    os2.I16(lineGap);
    os2.U16(static_cast<std::uint32_t>(std::max(ascender, fontYMax)));
    os2.U16(static_cast<std::uint32_t>(std::max(-descender, -fontYMin)));
    os2.U32(1); // Latin 1
    os2.U32(0);
    os2.I16(yMaxOf('x'));
    os2.I16(yMaxOf('H'));
    os2.U16(0);
    os2.U16(0x20);
    os2.U16(2);

    // kern: the font's pairs between glyphs we kept, sorted by (left, right) glyph.
    std::vector<std::tuple<std::uint16_t, std::uint16_t, std::int32_t>> pairs;
    for (Kerning const& kp : font->kerning) {
        auto l = glyphOfCode.find(kp.left);
        auto r = glyphOfCode.find(kp.right);
        std::int32_t const value = Clamp16(kp.adjustment * k);
        if (l != glyphOfCode.end() && r != glyphOfCode.end() && value != 0) {
            pairs.emplace_back(l->second, r->second, value);
        }
    }
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end(),
                            [](auto const& a, auto const& b) { return std::get<0>(a) == std::get<0>(b) && std::get<1>(a) == std::get<1>(b); }),
                pairs.end());
    if (pairs.size() > kMaxKernPairs) {
        pairs.resize(kMaxKernPairs);
    }

    struct Table {
        char const* tag;
        std::vector<std::uint8_t> data;
    };
    std::vector<Table> tables;
    tables.push_back({"OS/2", os2.bytes});
    tables.push_back({"cmap", CmapTable(cmap)});
    tables.push_back({"glyf", glyf.bytes});
    tables.push_back({"head", head.bytes});
    tables.push_back({"hhea", hhea.bytes});
    tables.push_back({"hmtx", hmtx.bytes});
    if (!pairs.empty()) {
        Be kern;
        kern.U16(0);
        kern.U16(1);
        kern.U16(0);
        kern.U16(static_cast<std::uint32_t>(14 + 6 * pairs.size()));
        kern.U16(0x0001); // horizontal, format 0
        kern.U16(static_cast<std::uint32_t>(pairs.size()));
        std::uint16_t const log = Log2Floor(pairs.size());
        std::uint32_t const range = 6u << log;
        kern.U16(range);
        kern.U16(log);
        kern.U16(static_cast<std::uint32_t>(6 * pairs.size() - range));
        for (auto const& [l, r, v] : pairs) {
            kern.U16(l);
            kern.U16(r);
            kern.I16(v);
        }
        tables.push_back({"kern", kern.bytes});
    }
    tables.push_back({"loca", locaTable.bytes});
    tables.push_back({"maxp", maxp.bytes});
    tables.push_back({"name", NameTable(familyName.empty() ? font->name : familyName, style)});
    tables.push_back({"post", post.bytes});
    std::sort(tables.begin(), tables.end(), [](Table const& a, Table const& b) { return std::memcmp(a.tag, b.tag, 4) < 0; });

    Be file;
    std::size_t const n = tables.size();
    std::uint16_t const log = Log2Floor(n);
    std::uint32_t const range = 16u << log;
    file.U32(0x00010000);
    file.U16(static_cast<std::uint32_t>(n));
    file.U16(range);
    file.U16(log);
    file.U16(static_cast<std::uint32_t>(16 * n - range));
    std::size_t offset = 12 + 16 * n;
    std::size_t headOffset = 0;
    for (Table const& t : tables) {
        file.Tag(t.tag);
        file.U32(Checksum(t.data, 0, t.data.size()));
        file.U32(static_cast<std::uint32_t>(offset));
        file.U32(static_cast<std::uint32_t>(t.data.size()));
        if (std::memcmp(t.tag, "head", 4) == 0) {
            headOffset = offset;
        }
        offset += (t.data.size() + 3) & ~std::size_t{3};
    }
    for (Table const& t : tables) {
        file.Append(t.data);
        file.Pad4();
    }
    file.PatchU32(headOffset + 8, 0xB1B0AFBAu - Checksum(file.bytes, 0, file.Size()));
    return std::move(file.bytes);
}

} // namespace mevr::gfx
