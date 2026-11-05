// SPDX-License-Identifier: MIT
// mevr-gfx-dump: a local look at a Scaleform GFx / SWF movie through core/gfx (research/57 section 5, "The game's own art"):
// its exports, sprites, shapes and fonts; a character drawn to PNG at a frame or label; a nine-slice fit; a font
// rebuilt as TrueType; and a text sample rendered by FreeType from that TrueType file. It reads the player's own
// files; what it writes stays local (game art: never commit it).
//
//   mevr-gfx-dump <file.gfx> list
//   mevr-gfx-dump <file.gfx> render <id|export> [frame|label] [scale] <out.png>
//   mevr-gfx-dump <file.gfx> frames <id|export> [scale] <out.png>       every labelled frame, stacked
//   mevr-gfx-dump <file.gfx> fit <id|export> <frame|label> <w> <h> <out.png> [left top right bottom] [cornerScale]
//   mevr-gfx-dump <file.gfx> font <fontId> <out.ttf> [--all]
//   mevr-gfx-dump <file.gfx> sample <fontId> "<text>" <px> <out.png>
//   mevr-gfx-dump <file.gfx> shape <id>                                 a shape's styles and edges
//   mevr-gfx-dump <file.gfx> uses <id>                                  where a character is placed
// Frames are 0-based. Options anywhere: --bg=RRGGBB composites PNGs over that colour (default: transparent);
// --alpha=N adds N (0..255) to every alpha (RenderOptions::color), for art the game fades in by script.

#include "core/PngFile.hpp"
#include "core/gfx/Raster.hpp"
#include "core/gfx/Swf.hpp"
#include "core/gfx/SwfFont.hpp"

#include <Windows.h>
#include <objbase.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace mevr::gfx;

std::string Utf8(std::wstring const& w) {
    if (w.empty()) {
        return {};
    }
    int const n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::u32string Utf32(std::string const& s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        auto const c = static_cast<unsigned char>(s[i]);
        char32_t cp = 0xFFFD;
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
        }
        out.push_back(cp);
        i += n;
    }
    return out;
}

std::optional<int> Int(std::string const& s) {
    int v = 0;
    auto const [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

std::optional<float> Float(std::string const& s) {
    float v = 0;
    auto const [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

std::optional<std::vector<std::uint8_t>> ReadFile(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::filesystem::path PathOf(std::string const& utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<char8_t const*>(utf8.data()), utf8.size()));
}

std::optional<std::uint32_t> gBackground; // 0xRRGGBB
ColorTransform gColor;

// Straight-alpha pixels for the PNG writer, over the background when one is set.
std::vector<std::uint32_t> ForPng(Image const& img) {
    if (!gBackground) {
        return Unpremultiplied(img);
    }
    std::uint32_t const bg = *gBackground;
    float const br = static_cast<float>((bg >> 16) & 0xFF), bgg = static_cast<float>((bg >> 8) & 0xFF),
                bb = static_cast<float>(bg & 0xFF);
    std::vector<std::uint32_t> out(img.pixels.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        std::uint32_t const p = img.pixels[i];
        float const keep = 1 - Alpha(p) / 255.0f;
        auto ch = [&](std::uint8_t v, float b) { return static_cast<std::uint32_t>(std::lround(std::min(255.0f, v + b * keep))); };
        out[i] = ch(Red(p), br) | (ch(Green(p), bgg) << 8) | (ch(Blue(p), bb) << 16) | 0xFF000000u;
    }
    return out;
}

bool WritePng(std::filesystem::path const& path, Image const& img) {
    if (img.Empty()) {
        std::printf("nothing to write: the image is empty\n");
        return false;
    }
    std::string error;
    auto const pixels = ForPng(img);
    if (!mevr::png::Write(path, img.width, img.height, pixels.data(), &error)) {
        std::printf("can't write %s: %s\n", path.string().c_str(), error.c_str());
        return false;
    }
    std::printf("wrote %s (%d x %d)\n", path.string().c_str(), img.width, img.height);
    return true;
}

std::optional<std::uint16_t> Character(Movie const& movie, std::string const& s) {
    if (auto v = Int(s); v && *v >= 0 && *v <= 0xFFFF) {
        return static_cast<std::uint16_t>(*v);
    }
    return movie.Exported(s);
}

std::optional<int> FrameArg(Movie const& movie, std::uint16_t id, std::string const& s) {
    if (auto v = Int(s)) {
        return *v;
    }
    return movie.FrameOf(id, s);
}

void PrintNotes(RenderNotes const& n) {
    std::printf("drew %d shape instance(s)", n.shapes);
    if (n.clipLayers) std::printf(", %d clip layer(s)", n.clipLayers);
    if (n.bitmapFills) std::printf(", %d bitmap fill(s) skipped", n.bitmapFills);
    if (n.editTexts) std::printf(", %d edit text(s) skipped", n.editTexts);
    if (n.staticTexts) std::printf(", %d static text(s) skipped", n.staticTexts);
    if (n.morphShapes) std::printf(", %d morph shape(s) at their ratio", n.morphShapes);
    if (n.buttons) std::printf(", %d button(s) skipped", n.buttons);
    if (n.imported) std::printf(", %d imported character(s) skipped (see list's imports)", n.imported);
    if (n.missing) std::printf(", %d missing/undrawable character(s)", n.missing);
    if (n.filters) std::printf(", %d placement(s) with filters (ignored)", n.filters);
    if (n.blendModes) std::printf(", %d blend mode(s) (drawn as normal)", n.blendModes);
    if (n.depthLimited) std::printf(", nesting cut %d time(s)", n.depthLimited);
    if (n.tooLarge) std::printf(", too large");
    std::printf("\n");
}

double Px(std::int32_t twips) {
    return twips / 20.0;
}

int List(Movie const& movie) {
    Rect const& stage = movie.FrameRect();
    std::printf("%s version %d, stage %.1f x %.1f px, %.2f fps; %d tags (%d bad, %d unknown)%s%s\n",
                movie.IsGfx() ? "GFx" : "SWF", movie.Version(), Px(stage.xMax - stage.xMin), Px(stage.yMax - stage.yMin), movie.FrameRate(),
                movie.Notes().tags, movie.Notes().badTags, movie.Notes().unknownTags,
                movie.Notes().firstProblem.empty() ? "" : "; first problem: ", movie.Notes().firstProblem.c_str());
    std::printf("\nexports (%zu):\n", movie.Exports().size());
    for (auto const& [id, name] : movie.Exports()) {
        char const* kind = "?";
        switch (movie.KindOf(id)) {
        case CharacterKind::Shape: kind = "shape"; break;
        case CharacterKind::Sprite: kind = "sprite"; break;
        case CharacterKind::EditText: kind = "edit text"; break;
        case CharacterKind::Font: kind = "font"; break;
        case CharacterKind::Bitmap: kind = "bitmap"; break;
        case CharacterKind::Text: kind = "text"; break;
        case CharacterKind::Button: kind = "button"; break;
        case CharacterKind::MorphShape: kind = "morph shape"; break;
        default: break;
        }
        std::printf("  %5u %-40s %s\n", id, name.c_str(), kind);
    }
    std::printf("\nimports (%zu):\n", movie.Imports().size());
    for (Import const& im : movie.Imports()) {
        std::printf("  %5u %-40s from %s\n", im.id, im.name.c_str(), im.url.c_str());
    }
    std::printf("\nsprites (%zu, id 0 = the main timeline):\n", movie.Sprites().size());
    for (auto const& [id, s] : movie.Sprites()) {
        std::printf("  %5u %3zu frame(s)", id, s.frames.size());
        if (s.scalingGrid) {
            Rect const& g = *s.scalingGrid;
            std::printf(", scaling grid %.1f,%.1f .. %.1f,%.1f", Px(g.xMin), Px(g.yMin), Px(g.xMax), Px(g.yMax));
        }
        if (auto b = Bounds(movie, id, 0)) {
            std::printf(", frame 0 %.1f x %.1f px", Px(b->xMax - b->xMin), Px(b->yMax - b->yMin));
        }
        bool any = false;
        for (std::size_t i = 0; i < s.frames.size(); ++i) {
            for (std::string const& l : s.frames[i].labels) {
                std::printf("%s%s@%zu", any ? ", " : "; labels ", l.c_str(), i);
                any = true;
            }
        }
        std::printf("\n");
        if (!s.frames.empty()) {
            std::printf("        frame 0:");
            for (Placement const& p : s.frames[0].displayList) {
                std::printf(" [d%u #%u%s%s%s]", p.depth, p.character, p.name.empty() ? "" : " ", p.name.c_str(),
                            p.clipDepth ? " clip" : "");
            }
            std::printf("\n");
        }
    }
    std::printf("\nshapes (%zu):\n", movie.Shapes().size());
    for (auto const& [id, s] : movie.Shapes()) {
        std::size_t fills = 0, lines = 0;
        int gradients = 0, bitmaps = 0;
        for (StyleTable const& t : s.styles) {
            fills += t.fills.size();
            lines += t.lines.size();
            for (FillStyle const& fs : t.fills) {
                gradients += fs.kind == FillKind::Linear || fs.kind == FillKind::Radial || fs.kind == FillKind::FocalRadial;
                bitmaps += fs.kind == FillKind::Bitmap;
            }
        }
        std::printf("  %5u DefineShape%d %.1f x %.1f px, %zu edges, %zu fills (%d gradient, %d bitmap), %zu lines\n", id,
                    s.version, Px(s.bounds.xMax - s.bounds.xMin), Px(s.bounds.yMax - s.bounds.yMin), s.edges.size(), fills,
                    gradients, bitmaps, lines);
    }
    std::printf("\nmorph shapes (%zu):\n", movie.MorphShapes().size());
    for (auto const& [id, m] : movie.MorphShapes()) {
        std::printf("  %5u DefineMorphShape%s %.1f x %.1f -> %.1f x %.1f px, %zu/%zu edges, %zu fills, %zu lines\n", id,
                    m.version == 2 ? "2" : "", Px(m.startBounds.xMax - m.startBounds.xMin), Px(m.startBounds.yMax - m.startBounds.yMin),
                    Px(m.endBounds.xMax - m.endBounds.xMin), Px(m.endBounds.yMax - m.endBounds.yMin), m.startEdges.size(),
                    m.endEdges.size(), m.startFills.size(), m.startLines.size());
    }
    std::printf("\nedit texts (%zu)\n", movie.EditTexts().size());
    std::printf("\nfonts:\n");
    for (FontInfo const& info : Fonts(movie)) {
        Font const* f = movie.FindFont(info.id);
        int ascii = 0;
        for (Glyph const& g : f->glyphs) {
            ascii += g.code >= 0x20 && g.code <= 0x7E;
        }
        std::printf("  %5u '%s' DefineFont%d flags 0x%02x, %zu glyphs (ASCII %d of 95), layout %s ascent %d descent %d leading %d "
                    "(/%d), %zu kerning pairs\n",
                    info.id, info.name.c_str(), info.version, f->flags, info.glyphCount, ascii, info.hasLayout ? "yes" : "no",
                    f->ascent, f->descent, f->leading, f->emScale, info.kerningPairs);
    }
    return 0;
}

int RenderCommand(Movie const& movie, std::vector<std::string> const& args) {
    // render <id> [frame|label] [scale] <out.png>
    if (args.size() < 2) {
        std::printf("render <id|export> [frame|label] [scale] <out.png>\n");
        return 2;
    }
    auto id = Character(movie, args[0]);
    if (!id) {
        std::printf("no character '%s'\n", args[0].c_str());
        return 1;
    }
    int frame = 0;
    float scale = 1;
    if (args.size() >= 3) {
        auto f = FrameArg(movie, *id, args[1]);
        if (!f) {
            std::printf("no frame '%s' in %u\n", args[1].c_str(), *id);
            return 1;
        }
        frame = *f;
    }
    if (args.size() >= 4) {
        scale = Float(args[2]).value_or(1.0f);
    }
    RenderNotes notes;
    auto const t0 = std::chrono::steady_clock::now();
    RenderOptions options;
    options.notes = &notes;
    options.color = gColor;
    Image const img = Render(movie, *id, frame, scale, options);
    auto const ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("character %u frame %d at %.2fx: %.1f ms; ", *id, frame, scale, ms);
    PrintNotes(notes);
    return WritePng(PathOf(args.back()), img) ? 0 : 1;
}

int FramesCommand(Movie const& movie, std::vector<std::string> const& args) {
    if (args.size() < 2) {
        std::printf("frames <id|export> [scale] <out.png>\n");
        return 2;
    }
    auto id = Character(movie, args[0]);
    Sprite const* sprite = id ? movie.FindSprite(*id) : nullptr;
    if (!sprite) {
        std::printf("no sprite '%s'\n", args[0].c_str());
        return 1;
    }
    float const scale = args.size() >= 3 ? Float(args[1]).value_or(1.0f) : 1.0f;
    std::vector<int> wanted;
    for (std::size_t i = 0; i < sprite->frames.size(); ++i) {
        if (!sprite->frames[i].labels.empty()) {
            wanted.push_back(static_cast<int>(i));
        }
    }
    if (wanted.empty()) {
        wanted.push_back(0);
    }
    // One sheet: each frame at the same place (the union of their bounds), stacked with a 6 px gap.
    std::optional<Rect> all;
    for (int f : wanted) {
        if (auto b = Bounds(movie, *id, f)) {
            all = all ? Rect{std::min(all->xMin, b->xMin), std::max(all->xMax, b->xMax), std::min(all->yMin, b->yMin),
                             std::max(all->yMax, b->yMax)}
                      : *b;
        }
    }
    if (!all) {
        std::printf("nothing drawn\n");
        return 1;
    }
    std::vector<Image> images;
    int width = 0, height = 0;
    for (int f : wanted) {
        FitOptions fit;
        fit.color = gColor;
        fit.source = *all;
        int const w = static_cast<int>(std::ceil((all->xMax - all->xMin) / 20.0 * scale));
        int const h = static_cast<int>(std::ceil((all->yMax - all->yMin) / 20.0 * scale));
        fit.cornerScale = scale;
        Image img = RenderFit(movie, *id, f, w, h, {}, fit);
        std::printf("frame %d", f);
        for (std::string const& l : sprite->frames[static_cast<std::size_t>(f)].labels) {
            std::printf(" '%s'", l.c_str());
        }
        std::printf(":");
        for (Placement const& p : sprite->frames[static_cast<std::size_t>(f)].displayList) {
            std::printf(" [d%u #%u%s%s%s ratio %u]", p.depth, p.character, p.name.empty() ? "" : " ", p.name.c_str(),
                        p.clipDepth ? " clip" : "", p.ratio);
        }
        std::printf("\n");
        width = std::max(width, img.width);
        height += img.height + 6;
        images.push_back(std::move(img));
    }
    Image sheet;
    sheet.width = width + 12;
    sheet.height = height + 6;
    sheet.pixels.assign(static_cast<std::size_t>(sheet.width) * sheet.height, 0u);
    int y = 6;
    for (Image const& img : images) {
        for (int r = 0; r < img.height; ++r) {
            for (int c = 0; c < img.width; ++c) {
                sheet.pixels[static_cast<std::size_t>(y + r) * sheet.width + 6 + c] = img.At(c, r);
            }
        }
        y += img.height + 6;
    }
    return WritePng(PathOf(args.back()), sheet) ? 0 : 1;
}

int FitCommand(Movie const& movie, std::vector<std::string> const& args) {
    // fit <id> <frame|label> <w> <h> <out.png> [left top right bottom] [cornerScale]
    if (args.size() < 5) {
        std::printf("fit <id|export> <frame|label> <w> <h> <out.png> [left top right bottom] [cornerScale]\n");
        return 2;
    }
    auto id = Character(movie, args[0]);
    if (!id) {
        std::printf("no character '%s'\n", args[0].c_str());
        return 1;
    }
    auto frame = FrameArg(movie, *id, args[1]);
    auto w = Int(args[2]);
    auto h = Int(args[3]);
    if (!frame || !w || !h) {
        std::printf("bad frame or size\n");
        return 2;
    }
    Insets insets;
    if (args.size() >= 9) {
        insets = {Float(args[5]).value_or(0), Float(args[6]).value_or(0), Float(args[7]).value_or(0), Float(args[8]).value_or(0)};
    }
    FitOptions options;
    options.color = gColor;
    RenderNotes notes;
    options.notes = &notes;
    if (args.size() >= 10) {
        options.cornerScale = Float(args[9]).value_or(1.0f);
    }
    Sprite const* sprite = movie.FindSprite(*id);
    if (auto b = Bounds(movie, *id, *frame)) {
        std::printf("source %.1f x %.1f px%s\n", Px(b->xMax - b->xMin), Px(b->yMax - b->yMin),
                    sprite && sprite->scalingGrid ? " (the sprite's scaling grid sets the corners)" : "");
    }
    Image const img = RenderFit(movie, *id, *frame, *w, *h, insets, options);
    PrintNotes(notes);
    return WritePng(PathOf(args[4]), img) ? 0 : 1;
}

void PrintFill(FillStyle const& f) {
    switch (f.kind) {
    case FillKind::Solid:
        std::printf("solid %02x%02x%02x a%u", f.color.r, f.color.g, f.color.b, f.color.a);
        break;
    case FillKind::Bitmap:
        std::printf("bitmap #%u", f.bitmapId);
        break;
    default:
        std::printf("%s gradient [%.4f %.4f %.4f %.4f | %.0f %.0f] spread %u interp %u focal %.2f:",
                    f.kind == FillKind::Linear ? "linear" : f.kind == FillKind::Radial ? "radial" : "focal", f.matrix.a, f.matrix.b,
                    f.matrix.c, f.matrix.d, f.matrix.tx, f.matrix.ty, f.spread, f.interpolation, f.focal);
        for (GradientStop const& s : f.stops) {
            std::printf(" %u:%02x%02x%02x a%u", s.ratio, s.color.r, s.color.g, s.color.b, s.color.a);
        }
        break;
    }
}

int ShapeCommand(Movie const& movie, std::vector<std::string> const& args) {
    auto id = args.empty() ? std::nullopt : Character(movie, args[0]);
    Shape const* shape = id ? movie.FindShape(*id) : nullptr;
    if (!shape) {
        std::printf("shape <id>: no such shape\n");
        return 1;
    }
    std::printf("DefineShape%d #%u bounds %d..%d x %d..%d twips\n", shape->version, shape->id, shape->bounds.xMin,
                shape->bounds.xMax, shape->bounds.yMin, shape->bounds.yMax);
    for (std::size_t t = 0; t < shape->styles.size(); ++t) {
        StyleTable const& table = shape->styles[t];
        for (std::size_t i = 0; i < table.fills.size(); ++i) {
            std::printf("  table %zu fill %zu: ", t, i + 1);
            PrintFill(table.fills[i]);
            std::printf("\n");
        }
        for (std::size_t i = 0; i < table.lines.size(); ++i) {
            LineStyle const& l = table.lines[i];
            std::printf("  table %zu line %zu: width %u, %02x%02x%02x a%u, caps %u/%u join %u%s", t, i + 1, l.width, l.color.r,
                        l.color.g, l.color.b, l.color.a, l.startCap, l.endCap, l.join, l.hasFill ? ", fill " : "");
            if (l.hasFill) {
                PrintFill(l.fill);
            }
            std::printf("\n");
        }
    }
    for (Edge const& e : shape->edges) {
        std::printf("  t%u f%u/%u l%u (%d,%d)%s(%d,%d)\n", e.table, e.fill0, e.fill1, e.line, e.x0, e.y0, e.curve ? " ~ " : " - ",
                    e.x1, e.y1);
    }
    return 0;
}

// Where a character is placed: sprite, frames (first and last of each run), depth, and its colour transform.
int UsesCommand(Movie const& movie, std::vector<std::string> const& args) {
    auto id = args.empty() ? std::nullopt : Character(movie, args[0]);
    if (!id) {
        std::printf("uses <id>\n");
        return 2;
    }
    for (auto const& [sid, sprite] : movie.Sprites()) {
        for (std::size_t f = 0; f < sprite.frames.size(); ++f) {
            for (Placement const& p : sprite.frames[f].displayList) {
                if (p.character != *id) {
                    continue;
                }
                bool const sameAsBefore =
                    f > 0 && std::any_of(sprite.frames[f - 1].displayList.begin(), sprite.frames[f - 1].displayList.end(),
                                         [&](Placement const& q) {
                                             return q.depth == p.depth && q.character == p.character &&
                                                    q.color.ma == p.color.ma && q.color.aa == p.color.aa;
                                         });
                if (sameAsBefore) {
                    continue;
                }
                ColorTransform const& c = p.color;
                std::printf("  sprite %u frame %zu depth %u%s%s: cxform mult %.2f %.2f %.2f %.2f add %.0f %.0f %.0f %.0f%s\n", sid, f,
                            p.depth, p.name.empty() ? "" : " ", p.name.c_str(), c.mr, c.mg, c.mb, c.ma, c.ar, c.ag, c.ab, c.aa,
                            p.clipDepth ? " (clip layer)" : "");
            }
        }
    }
    return 0;
}

int FontCommand(Movie const& movie, std::vector<std::string> const& args) {
    if (args.size() < 2) {
        std::printf("font <fontId> <out.ttf> [--all]\n");
        return 2;
    }
    auto id = Int(args[0]);
    Font const* font = id ? movie.FindFont(static_cast<std::uint16_t>(*id)) : nullptr;
    if (!font) {
        std::printf("no font %s\n", args[0].c_str());
        return 1;
    }
    TtfOptions options;
    options.everyGlyph = args.size() >= 3 && args[2] == "--all";
    std::string error;
    auto const t0 = std::chrono::steady_clock::now();
    auto ttf = FontAsTtf(movie, font->id, font->name, options, &error);
    auto const ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!ttf) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    std::ofstream out(PathOf(args[1]), std::ios::binary);
    out.write(reinterpret_cast<char const*>(ttf->data()), static_cast<std::streamsize>(ttf->size()));
    std::printf("wrote %s: %zu bytes in %.1f ms\n", args[1].c_str(), ttf->size(), ms);
    return out ? 0 : 1;
}

int SampleCommand(Movie const& movie, std::vector<std::string> const& args) {
    if (args.size() < 4) {
        std::printf("sample <fontId> \"<text>\" <px> <out.png>\n");
        return 2;
    }
    auto id = Int(args[0]);
    auto px = Int(args[2]);
    Font const* font = id ? movie.FindFont(static_cast<std::uint16_t>(*id)) : nullptr;
    if (!font || !px || *px <= 0 || *px > 1000) {
        std::printf("bad font or size\n");
        return 2;
    }
    std::string error;
    auto ttf = FontAsTtf(movie, font->id, font->name, {}, &error);
    if (!ttf) {
        std::printf("%s\n", error.c_str());
        return 1;
    }
    FT_Library lib = nullptr;
    FT_Face face = nullptr;
    if (FT_Init_FreeType(&lib) != 0 ||
        FT_New_Memory_Face(lib, ttf->data(), static_cast<FT_Long>(ttf->size()), 0, &face) != 0) {
        std::printf("FreeType refused the font\n");
        return 1;
    }
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(*px));
    std::printf("FreeType: family '%s' style '%s', %ld glyphs, kerning %s, ascender %ld descender %ld height %ld (26.6)\n",
                face->family_name, face->style_name, face->num_glyphs, FT_HAS_KERNING(face) ? "yes" : "no",
                face->size->metrics.ascender, face->size->metrics.descender, face->size->metrics.height);
    std::u32string const text = Utf32(args[1]);
    // Measure, then draw.
    long penMax = 0;
    {
        long pen = 0;
        FT_UInt prev = 0;
        char32_t prevChar = 0;
        for (char32_t c : text) {
            FT_UInt const gi = FT_Get_Char_Index(face, c);
            if (prev && gi && FT_HAS_KERNING(face)) {
                FT_Vector d{};
                FT_Get_Kerning(face, prev, gi, FT_KERNING_UNSCALED, &d);
                if (d.x != 0 && prevChar < 128 && c < 128) {
                    std::printf("kerning %c%c: %ld units\n", static_cast<char>(prevChar), static_cast<char>(c), d.x);
                }
                FT_Get_Kerning(face, prev, gi, FT_KERNING_DEFAULT, &d);
                pen += d.x;
            }
            if (FT_Load_Glyph(face, gi, FT_LOAD_NO_HINTING) == 0) {
                pen += face->glyph->advance.x;
            }
            prev = gi;
            prevChar = c;
        }
        penMax = pen;
    }
    int const margin = 8;
    int const ascender = static_cast<int>((face->size->metrics.ascender + 63) >> 6);
    int const descender = static_cast<int>((-face->size->metrics.descender + 63) >> 6);
    Image img;
    img.width = static_cast<int>(penMax >> 6) + 2 * margin;
    img.height = ascender + descender + 2 * margin;
    img.pixels.assign(static_cast<std::size_t>(img.width) * img.height, 0u);
    long pen = static_cast<long>(margin) << 6;
    int const baseline = margin + ascender;
    FT_UInt prev = 0;
    for (char32_t c : text) {
        FT_UInt const gi = FT_Get_Char_Index(face, c);
        if (prev && gi && FT_HAS_KERNING(face)) {
            FT_Vector d{};
            FT_Get_Kerning(face, prev, gi, FT_KERNING_DEFAULT, &d);
            pen += d.x;
        }
        prev = gi;
        if (FT_Load_Glyph(face, gi, FT_LOAD_NO_HINTING) != 0) {
            continue;
        }
        FT_GlyphSlot slot = face->glyph;
        FT_Outline_Translate(&slot->outline, pen & 63, 0);
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
            continue;
        }
        FT_Bitmap const& bm = slot->bitmap;
        int const ox = static_cast<int>(pen >> 6) + slot->bitmap_left;
        int const oy = baseline - slot->bitmap_top;
        for (unsigned r = 0; r < bm.rows; ++r) {
            for (unsigned col = 0; col < bm.width; ++col) {
                int const x = ox + static_cast<int>(col);
                int const y = oy + static_cast<int>(r);
                if (x < 0 || y < 0 || x >= img.width || y >= img.height) {
                    continue;
                }
                std::uint32_t const a = bm.buffer[static_cast<std::ptrdiff_t>(r) * bm.pitch + col];
                std::uint32_t& p = img.pixels[static_cast<std::size_t>(y) * img.width + x];
                std::uint32_t const old = Alpha(p);
                std::uint32_t const na = std::min<std::uint32_t>(255u, a + old * (255u - a) / 255u);
                // Premultiplied pale cyan (190, 240, 248).
                p = (190u * na / 255u) | ((240u * na / 255u) << 8) | ((248u * na / 255u) << 16) | (na << 24);
            }
        }
        pen += slot->advance.x;
    }
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    return WritePng(PathOf(args[3]), img) ? 0 : 1;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = Utf8(argv[i]);
        if (a.rfind("--bg=", 0) == 0) {
            std::uint32_t v = 0;
            std::from_chars(a.data() + 5, a.data() + a.size(), v, 16);
            gBackground = v;
            continue;
        }
        if (a.rfind("--alpha=", 0) == 0) {
            gColor.aa = Float(a.substr(8)).value_or(0.0f);
            continue;
        }
        args.push_back(std::move(a));
    }
    if (args.size() < 2) {
        std::printf("mevr-gfx-dump <file.gfx> list | render | frames | fit | font | sample ... (see tools/gfx-dump/Main.cpp)\n");
        return 2;
    }
    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    auto bytes = ReadFile(PathOf(args[0]));
    if (!bytes) {
        std::printf("can't read %s\n", args[0].c_str());
        return 1;
    }
    std::string error;
    auto const t0 = std::chrono::steady_clock::now();
    auto movie = Movie::Parse(*bytes, &error);
    auto const ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!movie) {
        std::printf("%s: %s\n", args[0].c_str(), error.c_str());
        return 1;
    }
    std::printf("parsed %zu bytes in %.1f ms\n", bytes->size(), ms);
    std::string const command = args[1];
    std::vector<std::string> const rest(args.begin() + 2, args.end());
    if (command == "list") {
        return List(*movie);
    }
    if (command == "render") {
        return RenderCommand(*movie, rest);
    }
    if (command == "frames") {
        return FramesCommand(*movie, rest);
    }
    if (command == "fit") {
        return FitCommand(*movie, rest);
    }
    if (command == "font") {
        return FontCommand(*movie, rest);
    }
    if (command == "shape") {
        return ShapeCommand(*movie, rest);
    }
    if (command == "uses") {
        return UsesCommand(*movie, rest);
    }
    if (command == "sample") {
        return SampleCommand(*movie, rest);
    }
    std::printf("unknown command '%s'\n", command.c_str());
    return 2;
}
