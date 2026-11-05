// SPDX-License-Identifier: MIT
#pragma once

#include "core/gfx/Swf.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// TrueType fonts built in memory from a movie's DefineFont3 (or DefineFont2) glyphs (research/54, "The game's own
// art"), so RmlUi's FreeType engine can load the game's own faces from memory. The glyphs' quadratic edges map to
// TrueType's on- and off-curve points directly; coordinates / emScale (20 for DefineFont3), y flipped, unitsPerEm
// 1024; ascender, descender and line gap from the font's layout; advances from its advance table; a format-0 'kern'
// table from its kerning records. Tables: cmap (format 4), glyf, loca, head, hhea, hmtx, maxp, name, post, OS/2,
// kern, with their checksums. Contours are wound one way (outer clockwise), so the nonzero rule leaves holes open.
// Nothing is cached or written: the caller owns the bytes.
namespace mevr::gfx {

struct FontInfo {
    std::uint16_t id = 0;
    std::string name;
    std::size_t glyphCount = 0;
    int version = 3;          // DefineFont2 or 3
    bool hasLayout = false;
    std::size_t kerningPairs = 0;
    bool bold = false;
    bool italic = false;
};

std::vector<FontInfo> Fonts(Movie const& movie);

struct TtfOptions {
    bool everyGlyph = false;  // every code the font has (e.g. its Japanese), not only U+0020..U+007E and U+00A0..U+017F
    std::string styleName;    // the name table's subfamily; default from the font's bold/italic flags
};

// A TrueType file for font `fontId`, or nullopt (with `error`) when the movie has no such font or no glyph in range.
std::optional<std::vector<std::uint8_t>> FontAsTtf(Movie const& movie, std::uint16_t fontId, std::string const& familyName,
                                                    TtfOptions const& options = {}, std::string* error = nullptr);

} // namespace mevr::gfx
