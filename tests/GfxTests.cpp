// SPDX-License-Identifier: MIT
// tests/GfxTests.cpp: core/gfx (research/57 section 5, "The game's own art"): the SWF/GFx reader, the rasterizer and the
// TrueType builder, on tiny movies built here byte by byte (no game data): a shape with a hole, a linear gradient, a
// sprite with two labelled frames and a clip layer, a nine-slice sprite with a scaling grid, a stroke, and a
// DefineFont3 with a square glyph, an 'O' ring of quadratic curves and a kerning pair. The fonts are loaded back with
// FreeType when the build has it (MEVR_TEST_FREETYPE). Then garbage: truncations and byte flips must never crash.

#include "core/gfx/Raster.hpp"
#include "core/gfx/Swf.hpp"
#include "core/gfx/SwfFont.hpp"

#ifdef MEVR_TEST_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace mevr::gfx;

int gGfxFailures = 0;

int gGfxChecks = 0;

void GfxCheck(bool ok, char const* what, int line) {
    ++gGfxChecks;
    if (!ok) {
        std::printf("FAIL tests/GfxTests.cpp line %d: %s\n", line, what);
        ++gGfxFailures;
    }
}
#define GCHECK(expr) GfxCheck((expr), #expr, __LINE__)

// ---- writing SWF bytes ----

class Bits {
public:
    std::vector<std::uint8_t> bytes;

    void Put(std::uint32_t v, int n) {
        for (int i = n - 1; i >= 0; --i) {
            if (bit_ == 0) {
                bytes.push_back(0);
            }
            if ((v >> i) & 1u) {
                bytes.back() = static_cast<std::uint8_t>(bytes.back() | (0x80u >> bit_));
            }
            bit_ = (bit_ + 1) & 7;
        }
    }
    void PutS(std::int32_t v, int n) {
        Put(static_cast<std::uint32_t>(v) & (n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1)), n);
    }
    void Align() { bit_ = 0; }
    void U8(std::uint32_t v) {
        Align();
        bytes.push_back(static_cast<std::uint8_t>(v));
    }
    void U16(std::uint32_t v) {
        U8(v & 0xFF);
        U8((v >> 8) & 0xFF);
    }
    void U32(std::uint32_t v) {
        U16(v & 0xFFFF);
        U16(v >> 16);
    }
    void Str(std::string const& s) {
        Align();
        for (char const c : s) {
            bytes.push_back(static_cast<std::uint8_t>(c));
        }
        bytes.push_back(0);
    }
    void Append(std::vector<std::uint8_t> const& b) {
        Align();
        bytes.insert(bytes.end(), b.begin(), b.end());
    }

private:
    int bit_ = 0;
};

int SBitsFor(std::initializer_list<std::int32_t> values) {
    int n = 1;
    for (std::int32_t const v : values) {
        while (!(v >= -(1 << (n - 1)) && v < (1 << (n - 1)))) {
            ++n;
        }
    }
    return n;
}

void PutRect(Bits& b, std::int32_t x0, std::int32_t x1, std::int32_t y0, std::int32_t y1) {
    b.Align();
    int const n = SBitsFor({x0, x1, y0, y1});
    b.Put(static_cast<std::uint32_t>(n), 5);
    b.PutS(x0, n);
    b.PutS(x1, n);
    b.PutS(y0, n);
    b.PutS(y1, n);
    b.Align();
}

// Scales are 16.16 fixed; translation in twips.
void PutMatrix(Bits& b, std::int32_t sx = 65536, std::int32_t sy = 65536, std::int32_t tx = 0, std::int32_t ty = 0) {
    b.Align();
    if (sx != 65536 || sy != 65536) {
        b.Put(1, 1);
        int const n = SBitsFor({sx, sy});
        b.Put(static_cast<std::uint32_t>(n), 5);
        b.PutS(sx, n);
        b.PutS(sy, n);
    } else {
        b.Put(0, 1);
    }
    b.Put(0, 1);
    if (tx == 0 && ty == 0) {
        b.Put(0, 5);
    } else {
        int const n = SBitsFor({tx, ty});
        b.Put(static_cast<std::uint32_t>(n), 5);
        b.PutS(tx, n);
        b.PutS(ty, n);
    }
    b.Align();
}

void PutRgba(Bits& b, std::uint8_t r, std::uint8_t g, std::uint8_t bl, std::uint8_t a = 255) {
    b.U8(r);
    b.U8(g);
    b.U8(bl);
    b.U8(a);
}

// Shape records with the given fill/line index widths.
struct Records {
    Bits& b;
    int fillBits;
    int lineBits;

    void Begin() {
        b.Align();
        b.Put(static_cast<std::uint32_t>(fillBits), 4);
        b.Put(static_cast<std::uint32_t>(lineBits), 4);
    }
    void Style(std::int32_t x, std::int32_t y, std::optional<int> f0, std::optional<int> f1, std::optional<int> line = {}) {
        b.Put(0, 1);
        b.Put(0, 1); // new styles
        b.Put(line ? 1u : 0u, 1);
        b.Put(f1 ? 1u : 0u, 1);
        b.Put(f0 ? 1u : 0u, 1);
        b.Put(1, 1); // move
        int const n = SBitsFor({x, y});
        b.Put(static_cast<std::uint32_t>(n), 5);
        b.PutS(x, n);
        b.PutS(y, n);
        if (f0) {
            b.Put(static_cast<std::uint32_t>(*f0), fillBits);
        }
        if (f1) {
            b.Put(static_cast<std::uint32_t>(*f1), fillBits);
        }
        if (line) {
            b.Put(static_cast<std::uint32_t>(*line), lineBits);
        }
    }
    void Line(std::int32_t dx, std::int32_t dy) {
        b.Put(1, 1);
        b.Put(1, 1);
        int const n = std::max(2, SBitsFor({dx, dy}));
        b.Put(static_cast<std::uint32_t>(n - 2), 4);
        if (dx != 0 && dy != 0) {
            b.Put(1, 1);
            b.PutS(dx, n);
            b.PutS(dy, n);
        } else if (dx == 0) {
            b.Put(0, 1);
            b.Put(1, 1);
            b.PutS(dy, n);
        } else {
            b.Put(0, 1);
            b.Put(0, 1);
            b.PutS(dx, n);
        }
    }
    void Curve(std::int32_t cdx, std::int32_t cdy, std::int32_t adx, std::int32_t ady) {
        b.Put(1, 1);
        b.Put(0, 1);
        int const n = std::max(2, SBitsFor({cdx, cdy, adx, ady}));
        b.Put(static_cast<std::uint32_t>(n - 2), 4);
        b.PutS(cdx, n);
        b.PutS(cdy, n);
        b.PutS(adx, n);
        b.PutS(ady, n);
    }
    void Rect(std::int32_t w, std::int32_t h) {
        Line(w, 0);
        Line(0, h);
        Line(-w, 0);
        Line(0, -h);
    }
    void End() {
        b.Put(0, 1);
        b.Put(0, 5);
        b.Align();
    }
};

void Tag(Bits& out, std::uint16_t code, std::vector<std::uint8_t> const& body) {
    if (body.size() < 63) {
        out.U16(static_cast<std::uint32_t>(code << 6) | static_cast<std::uint32_t>(body.size()));
    } else {
        out.U16(static_cast<std::uint32_t>(code << 6) | 0x3Fu);
        out.U32(static_cast<std::uint32_t>(body.size()));
    }
    out.Append(body);
}

// DefineShape3 of one solid rectangle (fill on its right side as drawn).
std::vector<std::uint8_t> SolidRect(std::uint16_t id, std::int32_t w, std::int32_t h, std::uint8_t r, std::uint8_t g, std::uint8_t bl) {
    Bits b;
    b.U16(id);
    PutRect(b, 0, w, 0, h);
    b.U8(1);
    b.U8(0x00);
    PutRgba(b, r, g, bl);
    b.U8(0);
    Records rec{b, 1, 0};
    rec.Begin();
    rec.Style(0, 0, std::nullopt, 1);
    rec.Rect(w, h);
    rec.End();
    return b.bytes;
}

std::vector<std::uint8_t> PlaceNew(std::uint16_t depth, std::uint16_t character, std::uint16_t clipDepth = 0) {
    Bits b;
    b.U8(0x02 | (clipDepth ? 0x40u : 0u));
    b.U16(depth);
    b.U16(character);
    if (clipDepth) {
        b.U16(clipDepth);
    }
    return b.bytes;
}

std::vector<std::uint8_t> Str(std::string const& s) {
    Bits b;
    b.Str(s);
    return b.bytes;
}

// A DefineFont3 glyph shape.
std::vector<std::uint8_t> GlyphSquare() {
    Bits b;
    Records rec{b, 1, 0};
    rec.Begin();
    rec.Style(2000, 0, std::nullopt, 1);
    rec.Line(16000, 0);
    rec.Line(0, -14000);
    rec.Line(-16000, 0);
    rec.Line(0, 14000);
    rec.End();
    return b.bytes;
}

std::vector<std::uint8_t> GlyphRing() {
    // Centre (10000, -7000) (500, 350 in font units); outer radius 8000 (400), inner 4000 (200); both loops travel
    // the same way round, the outer with the glyph on its fill1 side, the inner on its fill0 side.
    Bits b;
    Records rec{b, 1, 0};
    rec.Begin();
    rec.Style(18000, -7000, std::nullopt, 1);
    rec.Curve(0, -8000, -8000, 0);
    rec.Curve(-8000, 0, 0, 8000);
    rec.Curve(0, 8000, 8000, 0);
    rec.Curve(8000, 0, 0, -8000);
    rec.Style(14000, -7000, 1, 0);
    rec.Curve(0, -4000, -4000, 0);
    rec.Curve(-4000, 0, 0, 4000);
    rec.Curve(0, 4000, 4000, 0);
    rec.Curve(4000, 0, 0, -4000);
    rec.End();
    return b.bytes;
}

std::vector<std::uint8_t> GlyphEmpty() {
    Bits b;
    Records rec{b, 1, 0};
    rec.Begin();
    rec.End();
    return b.bytes;
}

std::vector<std::uint8_t> TestFont() {
    std::vector<std::vector<std::uint8_t>> const shapes = {GlyphEmpty(), GlyphSquare(), GlyphRing()};
    std::vector<std::uint16_t> const codes = {' ', 'A', 'O'};
    std::vector<std::int16_t> const advances = {5000, 20000, 22000};
    Bits b;
    b.U16(20);
    b.U8(0x8C);
    b.U8(0);
    std::string const name = "TestFont";
    b.U8(static_cast<std::uint32_t>(name.size()));
    for (char const c : name) {
        b.U8(static_cast<std::uint8_t>(c));
    }
    b.U16(static_cast<std::uint32_t>(shapes.size()));
    std::uint32_t offset = static_cast<std::uint32_t>(4 * shapes.size() + 4);
    for (auto const& s : shapes) {
        b.U32(offset);
        offset += static_cast<std::uint32_t>(s.size());
    }
    b.U32(offset);
    for (auto const& s : shapes) {
        b.Append(s);
    }
    for (std::uint16_t const c : codes) {
        b.U16(c);
    }
    b.U16(18000); // ascent 900
    b.U16(4000);  // descent 200
    b.U16(1000);  // leading 50
    for (std::int16_t const a : advances) {
        b.U16(static_cast<std::uint16_t>(a));
    }
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        PutRect(b, 0, 0, 0, 0);
    }
    b.U16(1);
    b.U16('A');
    b.U16('O');
    b.U16(static_cast<std::uint16_t>(static_cast<std::int16_t>(-2000))); // -100 units
    return b.bytes;
}

std::vector<std::uint8_t> TestMovie() {
    Bits tags;
    // #1: a red square, 100 px, with a 50 px hole in its middle.
    {
        Bits b;
        b.U16(1);
        PutRect(b, 0, 2000, 0, 2000);
        b.U8(1);
        b.U8(0x00);
        PutRgba(b, 255, 0, 0);
        b.U8(0);
        Records rec{b, 1, 0};
        rec.Begin();
        rec.Style(0, 0, std::nullopt, 1);
        rec.Rect(2000, 2000);
        rec.Style(500, 500, 1, 0);
        rec.Rect(1000, 1000);
        rec.End();
        Tag(tags, 32, b.bytes);
    }
    // #2: 100 x 20 px, a linear gradient from blue (left) to green (right).
    {
        Bits b;
        b.U16(2);
        PutRect(b, 0, 2000, 0, 400);
        b.U8(1);
        b.U8(0x10);
        PutMatrix(b, 4000, 4000, 1000, 200); // the gradient square's 32768 twips onto 2000
        b.U8(0x02);                          // pad, normal RGB, 2 stops
        b.U8(0);
        PutRgba(b, 0, 0, 255);
        b.U8(255);
        PutRgba(b, 0, 255, 0);
        b.U8(0);
        Records rec{b, 1, 0};
        rec.Begin();
        rec.Style(0, 0, std::nullopt, 1);
        rec.Rect(2000, 400);
        rec.End();
        Tag(tags, 32, b.bytes);
    }
    // #3: the clip layer, a green rectangle over the square's left half; #4, #5: the nine-slice's red panel and its
    // blue 10 px corner.
    Tag(tags, 32, SolidRect(3, 1000, 2000, 0, 255, 0));
    Tag(tags, 32, SolidRect(4, 2000, 800, 255, 0, 0));
    Tag(tags, 32, SolidRect(5, 200, 200, 0, 0, 255));
    // #6: a 2 px black stroke, 100 px long.
    {
        Bits b;
        b.U16(6);
        PutRect(b, -20, 2020, -20, 20);
        b.U8(0);
        b.U8(1);
        b.U16(40);
        PutRgba(b, 0, 0, 0);
        Records rec{b, 0, 1};
        rec.Begin();
        rec.Style(0, 0, std::nullopt, std::nullopt, 1);
        rec.Line(2000, 0);
        rec.End();
        Tag(tags, 32, b.bytes);
    }
    // #10: frame 0 'first' = the square clipped by #3; frame 1 'second' = the clip gone, the square tinted magenta.
    {
        Bits s;
        s.U16(10);
        s.U16(2);
        Tag(s, 26, PlaceNew(1, 3, 2));
        Tag(s, 26, PlaceNew(2, 1));
        Tag(s, 43, Str("first"));
        Tag(s, 1, {});
        Bits remove;
        remove.U16(1);
        Tag(s, 28, remove.bytes);
        Bits move;
        move.U8(0x01 | 0x08); // move, colour transform
        move.U16(2);
        move.Put(1, 1);       // add terms
        move.Put(1, 1);       // mult terms
        move.Put(10, 4);
        for (int i = 0; i < 4; ++i) {
            move.PutS(256, 10);
        }
        move.PutS(0, 10);
        move.PutS(0, 10);
        move.PutS(255, 10);
        move.PutS(0, 10);
        move.Align();
        Tag(s, 26, move.bytes);
        Tag(s, 43, Str("second"));
        Tag(s, 1, {});
        Tag(s, 0, {});
        Tag(tags, 39, s.bytes);
    }
    // #11: the panel #4 with its corner #5 on top, and a scaling grid 10 px in from every side.
    {
        Bits s;
        s.U16(11);
        s.U16(1);
        Tag(s, 26, PlaceNew(1, 4));
        Tag(s, 26, PlaceNew(2, 5));
        Tag(s, 1, {});
        Tag(s, 0, {});
        Tag(tags, 39, s.bytes);
        Bits g;
        g.U16(11);
        PutRect(g, 200, 1800, 200, 600);
        Tag(tags, 78, g.bytes);
    }
    // #7: a morph from a 50 px red square to a 100 x 50 px blue rectangle; #30 imported from another movie; #12 shows
    // the morph half way and places the import.
    {
        Bits styles;
        styles.U8(1);
        styles.U8(0x00);
        PutRgba(styles, 255, 0, 0);
        PutRgba(styles, 0, 0, 255);
        styles.U8(0);
        Records start{styles, 1, 0};
        start.Begin();
        start.Style(0, 0, std::nullopt, 1);
        start.Rect(1000, 1000);
        start.End();
        Bits endEdges;
        Records end{endEdges, 0, 0};
        end.Begin();
        end.Style(0, 0, std::nullopt, std::nullopt);
        end.Rect(2000, 1000);
        end.End();
        Bits b;
        b.U16(7);
        PutRect(b, 0, 1000, 0, 1000);
        PutRect(b, 0, 2000, 0, 1000);
        b.U32(static_cast<std::uint32_t>(styles.bytes.size()));
        b.Append(styles.bytes);
        b.Append(endEdges.bytes);
        Tag(tags, 46, b.bytes);

        Bits im;
        im.Str("Icons.swf");
        im.U8(1);
        im.U8(0);
        im.U16(1);
        im.U16(30);
        im.Str("xboxBtnA");
        Tag(tags, 71, im.bytes);

        Bits s;
        s.U16(12);
        s.U16(1);
        Bits place;
        place.U8(0x02 | 0x10); // character, ratio
        place.U16(1);
        place.U16(7);
        place.U16(32768);
        Tag(s, 26, place.bytes);
        Tag(s, 26, PlaceNew(2, 30));
        Tag(s, 1, {});
        Tag(s, 0, {});
        Tag(tags, 39, s.bytes);
    }
    Tag(tags, 75, TestFont());
    {
        Bits e;
        e.U16(2);
        e.U16(10);
        e.Str("tabSprite");
        e.U16(11);
        e.Str("panel");
        Tag(tags, 56, e.bytes);
    }
    Tag(tags, 26, PlaceNew(1, 10));
    Tag(tags, 1, {});
    Tag(tags, 0, {});

    Bits movie;
    movie.U8('F');
    movie.U8('W');
    movie.U8('S');
    movie.U8(10);
    movie.U32(0);
    PutRect(movie, 0, 11000, 0, 8000);
    movie.U16(24 << 8);
    movie.U16(1);
    movie.Append(tags.bytes);
    std::uint32_t const size = static_cast<std::uint32_t>(movie.bytes.size());
    std::memcpy(movie.bytes.data() + 4, &size, 4);
    return movie.bytes;
}

bool IsColor(std::uint32_t p, int r, int g, int b, int a, int tolerance = 3) {
    return std::abs(Red(p) - r) <= tolerance && std::abs(Green(p) - g) <= tolerance && std::abs(Blue(p) - b) <= tolerance &&
           std::abs(Alpha(p) - a) <= tolerance;
}

std::uint32_t BigEndian32(std::vector<std::uint8_t> const& b, std::size_t at) {
    return (static_cast<std::uint32_t>(b[at]) << 24) | (static_cast<std::uint32_t>(b[at + 1]) << 16) |
           (static_cast<std::uint32_t>(b[at + 2]) << 8) | b[at + 3];
}

void TestParse(Movie const& movie) {
    GCHECK(!movie.IsGfx());
    GCHECK(movie.Version() == 10);
    GCHECK(movie.Notes().badTags == 0);
    GCHECK(movie.KindOf(1) == CharacterKind::Shape);
    GCHECK(movie.KindOf(10) == CharacterKind::Sprite);
    GCHECK(movie.KindOf(20) == CharacterKind::Font);
    GCHECK(movie.KindOf(99) == CharacterKind::None);
    GCHECK(movie.Exported("tabSprite") == std::optional<std::uint16_t>(std::uint16_t{10}));
    GCHECK(movie.Exported("PANEL") == std::optional<std::uint16_t>(std::uint16_t{11}));
    GCHECK(!movie.Exported("nothing"));

    Shape const* square = movie.FindShape(1);
    GCHECK(square && square->edges.size() == 8 && square->styles.size() == 1 && square->styles[0].fills.size() == 1);
    Shape const* gradient = movie.FindShape(2);
    GCHECK(gradient && gradient->styles[0].fills[0].kind == FillKind::Linear && gradient->styles[0].fills[0].stops.size() == 2);
    Shape const* stroke = movie.FindShape(6);
    GCHECK(stroke && stroke->styles[0].lines.size() == 1 && stroke->styles[0].lines[0].width == 40);

    Sprite const* tab = movie.FindSprite(10);
    GCHECK(tab && tab->frames.size() == 2);
    if (tab && tab->frames.size() == 2) {
        GCHECK(tab->frames[0].displayList.size() == 2 && tab->frames[0].displayList[0].clipDepth == 2);
        GCHECK(tab->frames[1].displayList.size() == 1 && tab->frames[1].displayList[0].character == 1);
        GCHECK(tab->frames[1].displayList[0].color.ab == 255.0f);
    }
    GCHECK(movie.FrameOf(10, "first") == std::optional<int>(0));
    GCHECK(movie.FrameOf(10, "second") == std::optional<int>(1));
    GCHECK(movie.FrameOf(10, "SECOND") == std::optional<int>(1));
    GCHECK(!movie.FrameOf(10, "third"));
    GCHECK(!movie.FrameOf(99, "first"));
    Sprite const* panel = movie.FindSprite(11);
    GCHECK(panel && panel->scalingGrid && panel->scalingGrid->xMin == 200 && panel->scalingGrid->xMax == 1800 &&
           panel->scalingGrid->yMin == 200 && panel->scalingGrid->yMax == 600);
    Sprite const* root = movie.FindSprite(0);
    GCHECK(root && root->frames.size() == 1 && root->frames[0].displayList.size() == 1);

    Font const* font = movie.FindFont(20);
    GCHECK(font && font->name == "TestFont" && font->glyphs.size() == 3 && font->hasLayout && font->kerning.size() == 1);
    if (font && font->glyphs.size() == 3) {
        GCHECK(font->glyphs[0].code == ' ' && font->glyphs[0].edges.empty());
        GCHECK(font->glyphs[1].code == 'A' && font->glyphs[1].edges.size() == 4 && font->glyphs[1].advance == 20000);
        GCHECK(font->glyphs[2].code == 'O' && font->glyphs[2].edges.size() == 8);
        GCHECK(font->ascent == 18000 && font->descent == 4000 && font->leading == 1000);
        GCHECK(font->kerning[0].left == 'A' && font->kerning[0].right == 'O' && font->kerning[0].adjustment == -2000);
    }
    auto const fonts = Fonts(movie);
    GCHECK(fonts.size() == 1 && fonts[0].id == 20 && fonts[0].glyphCount == 3 && fonts[0].kerningPairs == 1);

    MorphShape const* morph = movie.FindMorphShape(7);
    GCHECK(morph && movie.KindOf(7) == CharacterKind::MorphShape);
    if (morph) {
        GCHECK(morph->startEdges.size() == 4 && morph->endEdges.size() == 4 && morph->startFills.size() == 1);
        Shape const half = MorphAt(*morph, 0.5);
        GCHECK(half.bounds.xMax == 1500 && half.bounds.yMax == 1000 && half.edges.size() == 4);
        GCHECK(half.styles.size() == 1 && half.styles[0].fills[0].color.r == 128 && half.styles[0].fills[0].color.b == 128);
    }
    GCHECK(movie.KindOf(30) == CharacterKind::Imported);
    GCHECK(movie.Imports().size() == 1 && movie.Imports()[0].id == 30 && movie.Imports()[0].name == "xboxBtnA" &&
           movie.Imports()[0].url == "Icons.swf");
    Sprite const* tween = movie.FindSprite(12);
    GCHECK(tween && tween->frames.size() == 1 && tween->frames[0].displayList.size() == 2 &&
           tween->frames[0].displayList[0].ratio == 32768);
}

void TestRender(Movie const& movie) {
    // The square with a hole, 1 px per 20 twips, 2 px pad.
    Image const square = Render(movie, 1, 0, 1.0f);
    GCHECK(square.width == 104 && square.height == 104);
    GCHECK(IsColor(square.At(12, 12), 255, 0, 0, 255, 0));   // inside the fill
    GCHECK(IsColor(square.At(2, 52), 255, 0, 0, 255, 0));    // the left edge's own column, fully covered
    GCHECK(Alpha(square.At(1, 52)) == 0);                    // just outside it
    GCHECK(Alpha(square.At(52, 52)) == 0);                   // the hole
    GCHECK(Alpha(square.At(0, 0)) == 0);                     // the pad
    // Half scale: the hole's edge falls in a pixel's middle, half covered; straight colour still red.
    Image const half = Render(movie, 1, 0, 0.5f);
    GCHECK(half.width == 54);
    GCHECK(std::abs(Alpha(half.At(14, 27)) - 128) <= 2);
    auto const straight = Unpremultiplied(half);
    GCHECK(IsColor(straight[27 * static_cast<std::size_t>(half.width) + 14], 255, 0, 0, 128, 2));

    // The gradient's two ends and middle.
    Image const grad = Render(movie, 2, 0, 1.0f);
    GCHECK(grad.width == 104 && grad.height == 24);
    GCHECK(Blue(grad.At(2, 12)) >= 250 && Green(grad.At(2, 12)) <= 5 && Alpha(grad.At(2, 12)) == 255);
    GCHECK(Green(grad.At(101, 12)) >= 250 && Blue(grad.At(101, 12)) <= 5);
    GCHECK(std::abs(Green(grad.At(52, 12)) - 128) <= 4 && std::abs(Blue(grad.At(52, 12)) - 127) <= 4);

    // The clip layer: frame 0 shows the left half only, and the clip's own green never paints.
    auto const bounds = Bounds(movie, 10, 0);
    GCHECK(bounds && bounds->xMin == 0 && bounds->xMax == 1000 && bounds->yMin == 0 && bounds->yMax == 2000);
    GCHECK(Render(movie, 10, 0, 1.0f).width == 54);
    FitOptions whole;
    whole.source = Rect{0, 2000, 0, 2000};
    Image const clipped = RenderFit(movie, 10, 0, 100, 100, {}, whole);
    GCHECK(clipped.width == 100 && clipped.height == 100);
    GCHECK(IsColor(clipped.At(10, 10), 255, 0, 0, 255, 0));
    GCHECK(Alpha(clipped.At(75, 10)) == 0);
    GCHECK(Alpha(clipped.At(40, 50)) == 0);
    bool green = false;
    for (std::uint32_t const p : clipped.pixels) {
        green = green || Green(p) > 0;
    }
    GCHECK(!green);
    // Frame 1 ('second'): the clip removed, the square tinted by its colour transform (+255 blue).
    int const second = movie.FrameOf(10, "second").value_or(-1);
    Image const tinted = RenderFit(movie, 10, second, 100, 100, {}, whole);
    GCHECK(IsColor(tinted.At(75, 10), 255, 0, 255, 255, 0));
    GCHECK(IsColor(tinted.At(10, 10), 255, 0, 255, 255, 0));
    GCHECK(Alpha(tinted.At(50, 50)) == 0);

    // The nine-slice: 100 x 40 px into 300 x 60, the 10 px blue corner unscaled (a plain stretch would make it 30 x 15).
    Image const fit = RenderFit(movie, 11, 0, 300, 60);
    GCHECK(fit.width == 300 && fit.height == 60);
    GCHECK(IsColor(fit.At(5, 5), 0, 0, 255, 255, 0));
    GCHECK(IsColor(fit.At(15, 5), 255, 0, 0, 255, 0));
    GCHECK(IsColor(fit.At(5, 12), 255, 0, 0, 255, 0));
    GCHECK(IsColor(fit.At(150, 30), 255, 0, 0, 255, 0));
    GCHECK(IsColor(fit.At(299, 59), 255, 0, 0, 255, 0));
    GCHECK(IsColor(fit.At(0, 0), 0, 0, 255, 255, 0));
    // Twice the corner scale: the corner 20 px.
    FitOptions sharp;
    sharp.cornerScale = 2;
    Image const fit2 = RenderFit(movie, 11, 0, 300, 60, {}, sharp);
    GCHECK(IsColor(fit2.At(15, 15), 0, 0, 255, 255, 0) && IsColor(fit2.At(25, 5), 255, 0, 0, 255, 0));
    // Explicit insets on a shape (no grid): the edges reach the box's edges.
    Image const panel = RenderFit(movie, 4, 0, 300, 60, Insets{10, 10, 10, 10});
    GCHECK(IsColor(panel.At(0, 0), 255, 0, 0, 255, 0) && IsColor(panel.At(299, 59), 255, 0, 0, 255, 0));
    // No insets: a plain stretch.
    Image const stretched = RenderFit(movie, 5, 0, 30, 30);
    GCHECK(IsColor(stretched.At(0, 0), 0, 0, 255, 255, 0) && IsColor(stretched.At(29, 29), 0, 0, 255, 255, 0));

    // The 2 px stroke: two opaque rows along its middle, nothing three rows off.
    Image const line = Render(movie, 6, 0, 1.0f);
    GCHECK(line.height == 6);
    GCHECK(Alpha(line.At(50, 2)) >= 250 && Alpha(line.At(50, 3)) >= 250);
    GCHECK(Alpha(line.At(50, 0)) == 0 && Alpha(line.At(50, 5)) == 0);
    GCHECK(Red(line.At(50, 2)) == 0);

    // The morph at its placement's ratio (half way: 75 px wide, half red and half blue); the import is counted, not drawn.
    RenderNotes tweenNotes;
    Image const tween = Render(movie, 12, 0, 1.0f, &tweenNotes);
    GCHECK(tween.width == 79 && tween.height == 54);
    GCHECK(IsColor(tween.At(40, 27), 128, 0, 127, 255, 2));
    GCHECK(tweenNotes.morphShapes == 1 && tweenNotes.imported == 1 && tweenNotes.missing == 0);
    // An outer colour transform: alpha halved over everything.
    RenderOptions faded;
    faded.color.ma = 0.5f;
    GCHECK(std::abs(Alpha(Render(movie, 1, 0, 1.0f, faded).At(12, 12)) - 128) <= 1);

    // The main timeline draws the tab sprite; an unknown id draws nothing.
    GCHECK(Render(movie, 0, 0, 1.0f).width == 54);
    RenderNotes notes;
    GCHECK(Render(movie, 99, 0, 1.0f, &notes).Empty());
    GCHECK(Render(movie, 1, 0, 0.0f).Empty());
    // Too large (a side over 8192 px, or over 4096 x 4096 in all): nothing drawn, and said.
    RenderNotes big;
    GCHECK(Render(movie, 1, 0, 100.0f, &big).Empty() && big.tooLarge);
    RenderNotes many;
    GCHECK(Render(movie, 1, 0, 60.0f, &many).Empty() && many.tooLarge);
    // Both frames of the clipped sprite in one render each, the clip freed and rebuilt per render.
    for (int i = 0; i < 3; ++i) {
        GCHECK(Alpha(RenderFit(movie, 10, 0, 100, 100, {}, whole).At(75, 10)) == 0);
    }
}

void TestTtf(Movie const& movie) {
    std::string error;
    GCHECK(!FontAsTtf(movie, 99, "x", {}, &error) && !error.empty());
    auto ttf = FontAsTtf(movie, 20, "TestFamily", {}, &error);
    GCHECK(ttf.has_value());
    if (!ttf) {
        std::printf("FontAsTtf: %s\n", error.c_str());
        return;
    }
    std::vector<std::uint8_t> const& f = *ttf;
    GCHECK(f.size() > 12 && f.size() % 4 == 0);
    GCHECK(BigEndian32(f, 0) == 0x00010000u);
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i + 3 < f.size(); i += 4) {
        sum += BigEndian32(f, i);
    }
    GCHECK(sum == 0xB1B0AFBAu);
    // Every table's checksum, and the directory sorted by tag.
    std::size_t const tables = (static_cast<std::size_t>(f[4]) << 8) | f[5];
    GCHECK(tables == 11);
    bool sorted = true, sums = true, kern = false;
    for (std::size_t i = 0; i < tables && 12 + 16 * i + 16 <= f.size(); ++i) {
        std::size_t const rec = 12 + 16 * i;
        if (i > 0 && std::memcmp(f.data() + rec - 16, f.data() + rec, 4) >= 0) {
            sorted = false;
        }
        kern = kern || std::memcmp(f.data() + rec, "kern", 4) == 0;
        std::uint32_t const offset = BigEndian32(f, rec + 8);
        std::uint32_t const length = BigEndian32(f, rec + 12);
        if (offset + length > f.size()) {
            sums = false;
            continue;
        }
        std::uint32_t s = 0;
        for (std::uint32_t k = 0; k < length; k += 4) {
            std::uint32_t v = 0;
            for (std::uint32_t j = 0; j < 4; ++j) {
                v = (v << 8) | (k + j < length ? f[offset + k + j] : 0u);
            }
            s += v;
        }
        if (std::memcmp(f.data() + rec, "head", 4) == 0) {
            s -= BigEndian32(f, offset + 8); // its checksum counts checkSumAdjustment as zero
        }
        sums = sums && s == BigEndian32(f, rec + 4);
    }
    GCHECK(sorted && sums && kern);

#ifdef MEVR_TEST_FREETYPE
    FT_Library lib = nullptr;
    FT_Face face = nullptr;
    GCHECK(FT_Init_FreeType(&lib) == 0);
    GCHECK(FT_New_Memory_Face(lib, f.data(), static_cast<FT_Long>(f.size()), 0, &face) == 0);
    if (face) {
        GCHECK(std::string(face->family_name ? face->family_name : "") == "TestFamily");
        GCHECK(face->num_glyphs == 4);
        GCHECK(face->units_per_EM == 1024);
        GCHECK(face->ascender == 900 && face->descender == -200);
        GCHECK(FT_HAS_KERNING(face));
        FT_UInt const a = FT_Get_Char_Index(face, 'A');
        FT_UInt const o = FT_Get_Char_Index(face, 'O');
        FT_UInt const space = FT_Get_Char_Index(face, ' ');
        GCHECK(a != 0 && o != 0 && space != 0 && FT_Get_Char_Index(face, 0xA0) == space);
        GCHECK(FT_Load_Glyph(face, a, FT_LOAD_NO_SCALE) == 0 && face->glyph->advance.x == 1000);
        GCHECK(FT_Load_Glyph(face, o, FT_LOAD_NO_SCALE) == 0 && face->glyph->advance.x == 1100);
        GCHECK(FT_Load_Glyph(face, space, FT_LOAD_NO_SCALE) == 0 && face->glyph->advance.x == 250);
        FT_Vector kerning{};
        GCHECK(FT_Get_Kerning(face, a, o, FT_KERNING_UNSCALED, &kerning) == 0 && kerning.x == -100);
        GCHECK(FT_Get_Kerning(face, o, a, FT_KERNING_UNSCALED, &kerning) == 0 && kerning.x == 0);

        // 'O' at 0.25 px per unit: ink on the ring (radius 300 units), none in the hole (its middle) or beyond it.
        GCHECK(FT_Set_Pixel_Sizes(face, 0, 256) == 0);
        GCHECK(FT_Load_Glyph(face, o, FT_LOAD_RENDER | FT_LOAD_NO_HINTING) == 0);
        FT_Bitmap const& bm = face->glyph->bitmap;
        int const left = face->glyph->bitmap_left;
        int const top = face->glyph->bitmap_top;
        auto at = [&](double ux, double uy) -> int {
            int const col = static_cast<int>(std::floor(ux * 0.25)) - left;
            int const row = top - 1 - static_cast<int>(std::floor(uy * 0.25));
            if (col < 0 || row < 0 || col >= static_cast<int>(bm.width) || row >= static_cast<int>(bm.rows)) {
                return 0;
            }
            return bm.buffer[static_cast<std::ptrdiff_t>(row) * bm.pitch + col];
        };
        GCHECK(at(800, 350) > 200);  // right of the ring
        GCHECK(at(500, 650) > 200);  // top of the ring
        GCHECK(at(500, 350) == 0);   // the hole
        GCHECK(at(560, 380) == 0);
        // 'A' is a solid square.
        GCHECK(FT_Load_Glyph(face, a, FT_LOAD_RENDER | FT_LOAD_NO_HINTING) == 0);
        FT_Bitmap const& sq = face->glyph->bitmap;
        GCHECK(sq.width >= 199 && sq.width <= 201 && sq.rows >= 174 && sq.rows <= 176);
        GCHECK(sq.buffer[static_cast<std::ptrdiff_t>(sq.rows / 2) * sq.pitch + sq.width / 2] == 255);
        FT_Done_Face(face);
    }
    FT_Done_FreeType(lib);
#endif
}

// Garbage in: every truncation of the movie and many byte flips parse, render and convert without crashing.
void TestRobustness(std::vector<std::uint8_t> const& good) {
    std::string error;
    std::vector<std::uint8_t> compressed = good;
    compressed[0] = 'C';
    GCHECK(!Movie::Parse(compressed, &error) && error.find("compressed") != std::string::npos);
    std::vector<std::uint8_t> gfx = good;
    gfx[0] = 'G';
    gfx[1] = 'F';
    gfx[2] = 'X';
    auto asGfx = Movie::Parse(gfx);
    GCHECK(asGfx && asGfx->IsGfx() && asGfx->FindSprite(10) != nullptr);
    GCHECK(!Movie::Parse(std::vector<std::uint8_t>{}, &error));
    GCHECK(!Movie::Parse(std::vector<std::uint8_t>{'F', 'W', 'S', 10, 0}, &error));

    auto exercise = [](Movie const& m) {
        for (auto const& [id, shape] : m.Shapes()) {
            (void)Render(m, id, 0, 0.25f);
        }
        for (auto const& [id, sprite] : m.Sprites()) {
            (void)Render(m, id, 0, 0.25f);
            (void)Render(m, id, 1, 0.25f);
            (void)RenderFit(m, id, 0, 40, 20, Insets{2, 2, 2, 2});
        }
        for (auto const& [id, font] : m.FontTable()) {
            (void)FontAsTtf(m, id, "Fuzz");
        }
    };
    for (std::size_t length = 8; length < good.size(); ++length) {
        std::vector<std::uint8_t> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
        if (auto m = Movie::Parse(cut)) {
            exercise(*m);
        }
    }
    std::uint32_t seed = 12345;
    auto next = [&] {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    int parsed = 0;
    for (int round = 0; round < 400; ++round) {
        std::vector<std::uint8_t> bad = good;
        int const flips = 1 + static_cast<int>(next() % 4);
        for (int i = 0; i < flips; ++i) {
            std::size_t const at = 8 + next() % (bad.size() - 8);
            bad[at] = static_cast<std::uint8_t>(next());
        }
        if (auto m = Movie::Parse(bad)) {
            ++parsed;
            exercise(*m);
        }
    }
    GCHECK(parsed > 0);
}

} // namespace

int RunGfxTests() {
    std::vector<std::uint8_t> const bytes = TestMovie();
    std::string error;
    auto movie = Movie::Parse(bytes, &error);
    GfxCheck(movie.has_value(), "the synthetic movie parses", __LINE__);
    if (!movie) {
        std::printf("Movie::Parse: %s\n", error.c_str());
        return gGfxFailures;
    }
    TestParse(*movie);
    TestRender(*movie);
    TestTtf(*movie);
    TestRobustness(bytes);
#ifdef MEVR_TEST_FREETYPE
    char const* freetype = "with FreeType";
#else
    char const* freetype = "without FreeType (the TrueType files' bytes only)";
#endif
    std::printf("gfx: %d checks, %d failed, %s\n", gGfxChecks, gGfxFailures, freetype);
    return gGfxFailures;
}
