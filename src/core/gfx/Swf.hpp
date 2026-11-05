// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// A reader for uncompressed SWF and Scaleform GFx movies (research/55 section 5, "The game's own art"): the character table
// (shapes, sprites with their frames and labels, scaling grids, exported names, edit-text bounds, DefineFont2/3 fonts)
// for the rasterizer (core/gfx/Raster) and the TrueType builder (core/gfx/SwfFont). Game-independent: it reads bytes
// handed to it and never touches files. Every read is bounds-checked; a bad tag is skipped and counted, never trusted.
// Coordinates stay in twips (1/20 px) as the file stores them.
namespace mevr::gfx {

struct Rect {
    std::int32_t xMin = 0, xMax = 0, yMin = 0, yMax = 0;
    bool Empty() const { return xMax <= xMin || yMax <= yMin; }
};

// x' = a x + c y + tx, y' = b x + d y + ty (SWF's MATRIX: a/d scale, b/c rotate-skew, translation in twips).
struct Matrix {
    double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
};
Matrix Multiply(Matrix const& outer, Matrix const& inner); // inner first, then outer
std::optional<Matrix> Invert(Matrix const& m);

// Colour transform: channel' = channel * mult + add, on straight 0..255 values, then clamped (CXFORMWITHALPHA).
struct ColorTransform {
    float mr = 1, mg = 1, mb = 1, ma = 1;
    float ar = 0, ag = 0, ab = 0, aa = 0;
};
ColorTransform Concat(ColorTransform const& outer, ColorTransform const& inner);

struct Rgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct GradientStop {
    std::uint8_t ratio = 0;
    Rgba color;
};

enum class FillKind : std::uint8_t { Solid, Linear, Radial, FocalRadial, Bitmap };

struct FillStyle {
    FillKind kind = FillKind::Solid;
    Rgba color;                       // Solid
    Matrix matrix;                    // gradients and bitmaps: their space -> shape space
    std::vector<GradientStop> stops;  // up to 15
    std::uint8_t spread = 0;          // 0 pad, 1 reflect, 2 repeat
    std::uint8_t interpolation = 0;   // 0 normal RGB, 1 linear RGB
    float focal = 0;                  // FocalRadial: -1..1 along the gradient's x axis
    std::uint16_t bitmapId = 0;       // Bitmap (not drawn)
};

struct LineStyle {
    std::uint16_t width = 0;          // twips
    Rgba color;
    bool hasFill = false;             // LINESTYLE2 with a fill (then `fill` paints it)
    FillStyle fill;
    std::uint8_t startCap = 0;        // 0 round, 1 none, 2 square
    std::uint8_t endCap = 0;
    std::uint8_t join = 0;            // 0 round, 1 bevel, 2 miter
    float miterLimit = 3;
    bool noClose = false;
};

// One style table: the shape's own, then one more per StateNewStyles record.
struct StyleTable {
    std::vector<FillStyle> fills;
    std::vector<LineStyle> lines;
};

// One edge, absolute twips. fill0/fill1/line are 1-based indices into styles[table] (0: none). fill0 is the style on
// the edge's left side as drawn (SWF), fill1 the one on its right.
struct Edge {
    std::int32_t x0 = 0, y0 = 0, cx = 0, cy = 0, x1 = 0, y1 = 0;
    bool curve = false;
    std::uint16_t fill0 = 0, fill1 = 0, line = 0;
    std::uint16_t table = 0;
};

struct Shape {
    std::uint16_t id = 0;
    int version = 1;                  // DefineShape 1..4
    Rect bounds;                      // includes strokes
    Rect edgeBounds;                  // DefineShape4's, else bounds
    std::vector<StyleTable> styles;   // at least one
    std::vector<Edge> edges;
};

// DefineMorphShape/2: a shape tweened between a start and an end, drawn at a placement's ratio (0..65535). The end
// edges pair with the start edges in order; the styles pair by index.
struct MorphShape {
    std::uint16_t id = 0;
    int version = 1;                  // DefineMorphShape 1 or 2
    Rect startBounds, endBounds;
    std::vector<FillStyle> startFills, endFills;
    std::vector<LineStyle> startLines, endLines;
    std::vector<Edge> startEdges, endEdges;
};
// The morph at t (0 = start, 1 = end) as an ordinary shape (version 3, or 4 for DefineMorphShape2's line styles).
Shape MorphAt(MorphShape const& morph, double t);

struct Placement {
    std::uint16_t depth = 0;
    std::uint16_t character = 0;
    Matrix matrix;
    ColorTransform color;
    std::uint16_t clipDepth = 0;      // > 0: a clip layer masking depths (depth, clipDepth]; not drawn itself
    std::uint16_t ratio = 0;
    std::string name;
    std::uint8_t blendMode = 0;       // 0/1 normal, 2 layer, 3+ others (drawn as normal)
    std::uint8_t filterCount = 0;     // filters are parsed past, not applied
    bool visible = true;
    int placedFrame = 0;              // the frame the character was (re)placed at, for nested timelines
};

struct Frame {
    std::vector<std::string> labels;
    std::vector<Placement> displayList; // sorted by depth
};

struct Sprite {
    std::uint16_t id = 0;
    std::uint16_t declaredFrames = 0;
    std::vector<Frame> frames;        // at least one
    std::optional<Rect> scalingGrid;  // DefineScalingGrid, sprite space (twips)
};

struct EditText {
    std::uint16_t id = 0;
    Rect bounds;
    std::uint16_t fontId = 0;
    std::uint16_t fontHeight = 0;     // twips
    std::string variable;
};

struct Glyph {
    std::uint16_t code = 0;           // UCS-2 (wide codes) or the font's 8-bit code
    std::int16_t advance = 0;         // font units x emScale (0 without a layout)
    Rect bounds;                      // from the font's bounds table (may be zero)
    std::vector<Edge> edges;          // fill style 1 is the glyph; table 0
};

struct Kerning {
    std::uint16_t left = 0, right = 0; // codes
    std::int16_t adjustment = 0;       // font units x emScale
};

struct Font {
    std::uint16_t id = 0;
    int version = 3;                  // DefineFont2 or 3
    std::string name;
    std::uint8_t flags = 0;           // 0x80 layout, 0x08 wide offsets, 0x04 wide codes, 0x02 italic, 0x01 bold
    std::uint8_t language = 0;
    int emScale = 20;                 // 20 for DefineFont3 (EM = 1024 x 20), 1 for DefineFont2
    bool hasLayout = false;
    std::int32_t ascent = 0, descent = 0, leading = 0; // font units x emScale
    std::vector<Glyph> glyphs;
    std::vector<Kerning> kerning;
    bool Bold() const { return (flags & 0x01) != 0; }
    bool Italic() const { return (flags & 0x02) != 0; }
};

// What a character id names. Bitmap, Text, Button and Other are known but not drawn; Imported is defined in another
// movie (ImportAssets/2: e.g. ME1's controller icons from Xbox_ControllerIcons.swf, its fonts from gfxfontlib.swf).
enum class CharacterKind : std::uint8_t { None, Shape, Sprite, EditText, Font, Bitmap, MorphShape, Text, Button, Imported, Other };

struct Import {
    std::uint16_t id = 0;             // the id this movie uses
    std::string name;                 // the export name in the other movie
    std::string url;                  // the other movie, as the file names it
};

struct ParseNotes {
    int tags = 0;
    int badTags = 0;                  // tags whose contents ran past their length or made no sense (skipped)
    int unknownTags = 0;
    std::string firstProblem;
};

class Movie {
public:
    // Uncompressed SWF ('FWS') or GFx ('GFX') only. A compressed movie ('CWS', 'CFX', 'ZWS') fails with a message:
    // there's no zlib in the project. Never throws on bad data; returns nullopt only for a bad header.
    static std::optional<Movie> Parse(std::span<std::uint8_t const> bytes, std::string* error = nullptr);

    bool IsGfx() const { return gfx_; }
    int Version() const { return version_; }
    Rect const& FrameRect() const { return frameRect_; }
    float FrameRate() const { return frameRate_; }
    ParseNotes const& Notes() const { return notes_; }

    CharacterKind KindOf(std::uint16_t id) const;
    Shape const* FindShape(std::uint16_t id) const;
    MorphShape const* FindMorphShape(std::uint16_t id) const;
    Sprite const* FindSprite(std::uint16_t id) const; // 0: the main timeline
    EditText const* FindEditText(std::uint16_t id) const;
    Font const* FindFont(std::uint16_t id) const;

    // 0-based frame of a sprite (0: the main timeline) whose labels include `label` (case-sensitive first, then
    // ASCII case-insensitive, as Flash's gotoAndStop).
    std::optional<int> FrameOf(std::uint16_t sprite, std::string_view label) const;
    // The character exported (ExportAssets) under `name` (case-sensitive first, then case-insensitive).
    std::optional<std::uint16_t> Exported(std::string_view name) const;
    std::vector<std::pair<std::uint16_t, std::string>> const& Exports() const { return exports_; }
    std::vector<Import> const& Imports() const { return imports_; }

    std::map<std::uint16_t, Shape> const& Shapes() const { return shapes_; }
    std::map<std::uint16_t, MorphShape> const& MorphShapes() const { return morphs_; }
    std::map<std::uint16_t, Sprite> const& Sprites() const { return sprites_; }
    std::map<std::uint16_t, EditText> const& EditTexts() const { return editTexts_; }
    std::map<std::uint16_t, Font> const& FontTable() const { return fonts_; }

private:
    friend struct MovieParser;
    bool gfx_ = false;
    int version_ = 0;
    Rect frameRect_;
    float frameRate_ = 0;
    ParseNotes notes_;
    std::map<std::uint16_t, Shape> shapes_;
    std::map<std::uint16_t, MorphShape> morphs_;
    std::map<std::uint16_t, Sprite> sprites_;
    std::map<std::uint16_t, EditText> editTexts_;
    std::map<std::uint16_t, Font> fonts_;
    std::unordered_map<std::uint16_t, CharacterKind> others_;
    std::vector<std::pair<std::uint16_t, std::string>> exports_;
    std::vector<Import> imports_;
};

} // namespace mevr::gfx
