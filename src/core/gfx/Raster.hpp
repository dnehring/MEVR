// SPDX-License-Identifier: MIT
#pragma once

#include "core/gfx/Swf.hpp"

#include <cstdint>
#include <optional>
#include <vector>

// A CPU rasterizer for the movies core/gfx/Swf reads (research/54, "The game's own art"): a character (shape or
// sprite) at a frame to an image, anti-aliased by exact area coverage (the signed-area accumulation of stb_truetype
// and font-rs, written here from the math), each fill style filled by the nonzero rule over its fill0/fill1-oriented
// edges, quadratic curves flattened adaptively. Solid, linear, radial and focal gradient fills (pad, reflect, repeat;
// normal or linear-RGB interpolation); bitmap fills are skipped (counted, transparent). Strokes with round, bevel or
// miter joins and round, square or no caps, at least 1 px. Sprites composite through their display lists: matrices,
// colour transforms, morph shapes at their placement's ratio, and clip layers (a clipDepth layer masks the depths above
// it up to its clipDepth and isn't drawn itself). Edit texts, static texts and characters imported from other movies
// are skipped; filters and blend modes are ignored (all counted in RenderNotes).
namespace mevr::gfx {

// Premultiplied RGBA8, R in the low byte (D3D11's R8G8B8A8, RmlUi's premultiplied convention), row after row.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;
    // Render only: where pixel (0, 0)'s top-left corner lies in the character's space (twips), and px per twip.
    double originX = 0;
    double originY = 0;
    double pxPerTwip = 0;

    bool Empty() const { return width <= 0 || height <= 0; }
    std::uint32_t At(int x, int y) const {
        return x < 0 || y < 0 || x >= width || y >= height ? 0u : pixels[static_cast<std::size_t>(y) * width + x];
    }
};

inline std::uint8_t Red(std::uint32_t p) { return static_cast<std::uint8_t>(p & 0xFF); }
inline std::uint8_t Green(std::uint32_t p) { return static_cast<std::uint8_t>((p >> 8) & 0xFF); }
inline std::uint8_t Blue(std::uint32_t p) { return static_cast<std::uint8_t>((p >> 16) & 0xFF); }
inline std::uint8_t Alpha(std::uint32_t p) { return static_cast<std::uint8_t>(p >> 24); }

// A nine-slice's fixed borders, in source pixels (twips / 20).
struct Insets {
    float left = 0;
    float top = 0;
    float right = 0;
    float bottom = 0;
};

// What a render met that it doesn't draw, for the caller's log.
struct RenderNotes {
    int shapes = 0;          // shape instances drawn
    int bitmapFills = 0;     // fill styles with a bitmap: skipped (transparent)
    int editTexts = 0;       // DefineEditText: skipped
    int staticTexts = 0;     // DefineText/2: skipped
    int morphShapes = 0;     // morph shapes drawn (at their placement's ratio)
    int buttons = 0;         // skipped
    int imported = 0;        // characters another movie defines (Movie::Imports): not drawn
    int missing = 0;         // placements of ids the movie doesn't define (or bitmaps placed directly)
    int clipLayers = 0;
    int filters = 0;         // placements with filters (ignored)
    int blendModes = 0;      // placements with a blend mode other than normal/layer (drawn as normal)
    int depthLimited = 0;    // nesting cut at the depth limit
    bool tooLarge = false;   // the image would exceed 8192 px a side or 4096 x 4096 px in all: nothing drawn
};

struct RenderOptions {
    int pad = 2;                  // transparent pixels around the tight bounds
    bool playNested = false;      // nested sprites at (frame - the frame they were placed) instead of their frame 0
    ColorTransform color;         // applied over the whole character (a tint, a fade, or alpha added to art a script fades in)
    RenderNotes* notes = nullptr;
};

// The painted bounds of a character at a frame (twips, its own space): its shapes' bounds clipped by their clip
// layers. Empty characters (or unknown ids) give nullopt. Frames are 0-based and clamp to the last.
std::optional<Rect> Bounds(Movie const& movie, std::uint16_t id, int frame, bool playNested = false);

// The character at `frame` at `scale` (1 = one pixel per 20 twips): its tight bounds plus a small pad.
Image Render(Movie const& movie, std::uint16_t id, int frame, float scale, RenderNotes* notes = nullptr);
Image Render(Movie const& movie, std::uint16_t id, int frame, float scale, RenderOptions const& options);

struct FitOptions {
    float cornerScale = 1;        // output px per source px in the corners (e.g. 2 for a sharper, larger border)
    std::optional<Rect> source;   // the source box (twips) mapped onto the target; default: Bounds() of that frame
    bool playNested = false;
    ColorTransform color;         // as RenderOptions::color
    RenderNotes* notes = nullptr;
};

// The character at `frame` filling a width x height box with its corners kept: a nine-slice from the sprite's
// DefineScalingGrid when it has one, else from `insets` (source px; all zero: a plain stretch). The corners keep
// their size (x cornerScale, shrunk only if they don't fit), the edges stretch along their length, the middle both
// ways; strokes keep their width.
Image RenderFit(Movie const& movie, std::uint16_t id, int frame, int width, int height, Insets insets = {},
                FitOptions const& options = {});

// Straight (un-premultiplied) RGBA8 for PNG writers (core/PngFile takes straight alpha).
std::vector<std::uint32_t> Unpremultiplied(Image const& image);

} // namespace mevr::gfx
