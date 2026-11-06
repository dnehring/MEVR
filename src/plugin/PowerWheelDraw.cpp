// SPDX-License-Identifier: MIT
// The power wheel's look (plugin/PowerWheelDraw.hpp).

#include "plugin/PowerWheelDraw.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <iterator>
#include <numbers>

#include "core/gfx/Swf.hpp"
#include "core/gfx/SwfFont.hpp"

namespace mevr::host::power_wheel_draw {
namespace {

namespace pw = vr::power_wheel;

constexpr float kPi = std::numbers::pi_v<float>;
constexpr ImVec2 kCentre{kCentreX, kCentreY};

// ---- Colours (sRGB, as ImGui blends them) ------------------------------------------------------------------------------
constexpr ImU32 Rgba(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}
ImU32 WithAlpha(ImU32 colour, float alpha) {
    float const a = static_cast<float>((colour >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(alpha, 0.0f, 1.0f);
    return (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::lround(a)) << IM_COL32_A_SHIFT);
}

struct Theme {
    ImU32 border;      // the tile's edge
    ImU32 borderCore;  // a thin brighter line on it
    ImU32 inset;       // the second outline inside
    ImU32 fillEdge;    // the fill at the border (the game's wheel buttons are brightest at their edges)...
    ImU32 fillCentre;  // ...falling off toward the middle
    ImU32 scanlines;   // the game's panels' fine horizontal lines
    ImU32 label;
};
// LE1's own colours, sampled from its HUD and main wheel textures (Startup_INT.pcc, GUI_SF_HUD.PC_ME_HUD and
// GUI_SF_InGameGui.PC_MainWheel, 2026-11-04): lines (125, 211, 232); a wheel button's blue fill (40, 133, 172) at its
// edge to (23, 72, 104) inside; the chosen button's orange (220, 103, 27) to (117, 48, 2); frames' orange (255, 157, 90);
// panels' deep indigo (3, 19, 96) with fine scanlines. The middles here are darker than the game's, for the icons.
// Since 2026-11-05 (the user: "like it could be projected from the omni tool", the chosen tile "change color to make it
// obvious"): every tile is the omni-tool's projection, thin orange light over see-through glass (the gauntlet's hologram,
// game/OmniTool: mid (233, 147, 81), core (255, 205, 103)); the chosen one turns to ME1's own UI cyan.
Theme Omni() {
    return {Rgba(240, 150, 75, 235), Rgba(255, 214, 160, 200), Rgba(240, 150, 75, 50), Rgba(160, 76, 26, 120), Rgba(100, 42, 12, 70),
        Rgba(0, 0, 0, 70), Rgba(255, 204, 150)};
}
Theme Chosen() {
    return {Rgba(140, 220, 255), Rgba(225, 248, 255), Rgba(140, 220, 255, 70), Rgba(60, 150, 205, 175), Rgba(22, 74, 122, 130),
        Rgba(0, 0, 0, 70), Rgba(240, 252, 255)};
}
ImU32 Mix(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto channel = [&](int shift) {
        float const x = static_cast<float>((a >> shift) & 0xFF);
        float const y = static_cast<float>((b >> shift) & 0xFF);
        return static_cast<ImU32>(std::lround(x + (y - x) * t)) << shift;
    };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT) | channel(IM_COL32_A_SHIFT);
}
constexpr ImU32 kLight = Rgba(255, 160, 80);        // the projection's light
constexpr ImU32 kLightChosen = Rgba(120, 210, 255);  // ...toward the chosen tile
constexpr ImU32 kEmitter = Rgba(255, 210, 150);      // the omni-tool's emitter
// The hint card's and the lock marker's lines, in the same projection's colours.
constexpr ImU32 kLine = Rgba(240, 150, 75);
constexpr ImU32 kChevron = Rgba(255, 214, 160);
constexpr ImU32 kBeamCore = Rgba(255, 232, 196);

// ---- Geometry -----------------------------------------------------------------------------------------------------------
pw::Vec2 Dir(float deg) {
    return {std::cos(deg * kPi / 180.0f), std::sin(deg * kPi / 180.0f)};
}
pw::Vec2 Add(pw::Vec2 a, pw::Vec2 b) {
    return {a.x + b.x, a.y + b.y};
}
pw::Vec2 Mul(pw::Vec2 a, float k) {
    return {a.x * k, a.y * k};
}
ImVec2 ToCanvas(pw::Vec2 p) {
    return {kCentre.x + p.x, kCentre.y - p.y};
}
std::vector<ImVec2> ToCanvas(std::vector<pw::Vec2> const& points) {
    std::vector<ImVec2> out;
    out.reserve(points.size());
    for (pw::Vec2 p : points) {
        out.push_back(ToCanvas(p));
    }
    return out;
}
float Ease(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

// Where a horizontal line crosses a polygon: its leftmost and rightmost crossing (false: it doesn't).
bool SpanAt(std::vector<ImVec2> const& polygon, float y, float& left, float& right) {
    left = FLT_MAX;
    right = -FLT_MAX;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        ImVec2 const a = polygon[i];
        ImVec2 const b = polygon[j];
        if ((a.y > y) != (b.y > y)) {
            float const x = a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y);
            left = std::min(left, x);
            right = std::max(right, x);
        }
    }
    return left <= right;
}

// The strip as triangles, each vertex its own colour (the glass: darker toward the palm).
void FillStrip(ImDrawList* dl, pw::Strip const& strip, ImU32 inner, ImU32 outer) {
    std::size_t const n = strip.outer.size();
    if (n < 2) {
        return;
    }
    ImVec2 const uv = ImGui::GetFontTexUvWhitePixel();
    dl->PrimReserve(static_cast<int>((n - 1) * 6), static_cast<int>(n * 2));
    ImDrawIdx const base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
    for (std::size_t i = 0; i < n; ++i) {
        dl->PrimWriteVtx(ToCanvas(strip.outer[i]), uv, outer);
        dl->PrimWriteVtx(ToCanvas(strip.inner[i]), uv, inner);
    }
    for (std::size_t i = 0; i + 1 < n; ++i) {
        auto const o0 = static_cast<ImDrawIdx>(base + i * 2);
        auto const i0 = static_cast<ImDrawIdx>(o0 + 1);
        auto const o1 = static_cast<ImDrawIdx>(o0 + 2);
        auto const i1 = static_cast<ImDrawIdx>(o0 + 3);
        dl->PrimWriteIdx(o0);
        dl->PrimWriteIdx(o1);
        dl->PrimWriteIdx(i1);
        dl->PrimWriteIdx(o0);
        dl->PrimWriteIdx(i1);
        dl->PrimWriteIdx(i0);
    }
}

// A dashed circle: `on` degrees drawn, `off` skipped.
void DashedCircle(ImDrawList* dl, ImVec2 c, float r, ImU32 colour, float thickness, float on, float off, float phase = 0.0f) {
    for (float a = phase; a < phase + 360.0f; a += on + off) {
        dl->PathArcTo(c, r, a * kPi / 180.0f, (a + on) * kPi / 180.0f, 6);
        dl->PathStroke(colour, 0, thickness);
    }
}

// ---- Text --------------------------------------------------------------------------------------------------------------
std::string Upper(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return text;
}
float GlyphWidth(ImFont* font, float size, char c) {
    char const glyph[2] = {c, 0};
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, glyph).x;
}
float TrackedWidth(ImFont* font, float size, std::string const& text, float tracking) {
    float width = 0.0f;
    for (char c : text) {
        width += GlyphWidth(font, size, c) + tracking;
    }
    return text.empty() ? 0.0f : width - tracking;
}
// Letter-spaced, centred on `centreX`, its top at `top`.
void TrackedCentred(ImDrawList* dl, ImFont* font, float size, float centreX, float top, ImU32 colour, std::string const& text, float tracking) {
    float x = centreX - TrackedWidth(font, size, text, tracking) * 0.5f;
    char glyph[2] = {0, 0};
    for (char c : text) {
        glyph[0] = c;
        dl->AddText(font, size, {std::floor(x), std::floor(top)}, colour, glyph);
        x += GlyphWidth(font, size, c) + tracking;
    }
}

// ---- Pieces ------------------------------------------------------------------------------------------------------------
// A chevron pointing along `dir` (wheel coordinates), its tip at `tip`.
void Chevron(ImDrawList* dl, pw::Vec2 tip, pw::Vec2 dir, float halfWidth, float height, float thickness, ImU32 colour) {
    pw::Vec2 const side{-dir.y, dir.x};
    auto at = [&](float u, float v) { return ToCanvas(Add(tip, Add(Mul(dir, u - height), Mul(side, v)))); };
    std::array<ImVec2, 6> const points{at(height, 0.0f), at(0.0f, halfWidth), at(-thickness, halfWidth), at(height - thickness * 1.15f, 0.0f),
        at(-thickness, -halfWidth), at(0.0f, -halfWidth)};
    dl->AddConcavePolyFilled(points.data(), static_cast<int>(points.size()), colour);
}

// A small diamond (the node where a spoke meets a tile).
void Node(ImDrawList* dl, ImVec2 c, float r, ImU32 colour) {
    std::array<ImVec2, 4> const points{ImVec2{c.x, c.y - r}, ImVec2{c.x + r, c.y}, ImVec2{c.x, c.y + r}, ImVec2{c.x - r, c.y}};
    dl->AddConvexPolyFilled(points.data(), 4, colour);
}

// Our N7 mark for the palm: a slanted N and 7 in white, a red stripe with a white edge along the 7's stem.
void N7(ImDrawList* dl, ImVec2 c, float s, float alpha) {
    constexpr float kSlant = 0.12f;
    constexpr float kShift = -5.0f;  // centres the mark (it runs from x -34 to 45)
    auto at = [&](float x, float y) { return ImVec2{c.x + (x + kShift + kSlant * y) * s, c.y - y * s}; };
    std::array<ImVec2, 10> const n{at(-36.0f, 18.0f), at(-29.0f, 18.0f), at(-9.5f, -8.5f), at(-9.5f, 18.0f), at(-3.0f, 18.0f),
        at(-3.0f, -18.0f), at(-10.0f, -18.0f), at(-29.5f, 8.5f), at(-29.5f, -18.0f), at(-36.0f, -18.0f)};
    std::array<ImVec2, 7> const seven{at(3.0f, 18.0f), at(30.0f, 18.0f), at(30.0f, 11.5f), at(13.5f, -18.0f), at(6.0f, -18.0f), at(21.5f, 11.5f),
        at(3.0f, 11.5f)};
    std::array<ImVec2, 4> const red{at(35.0f, 12.0f), at(41.0f, 12.0f), at(25.0f, -18.0f), at(19.0f, -18.0f)};
    std::array<ImVec2, 4> const edge{at(42.5f, 12.0f), at(44.5f, 12.0f), at(28.5f, -18.0f), at(26.5f, -18.0f)};
    ImU32 const white = WithAlpha(Rgba(206, 212, 226), alpha);
    dl->AddConcavePolyFilled(n.data(), static_cast<int>(n.size()), white);
    dl->AddConcavePolyFilled(seven.data(), static_cast<int>(seven.size()), white);
    dl->AddConvexPolyFilled(red.data(), 4, WithAlpha(Rgba(226, 42, 40), alpha));
    dl->AddConvexPolyFilled(edge.data(), 4, white);
}

struct TileDraw {
    int index = 0;
    float angle = 0.0f;
    pw::Strip strip;
    float scale = 1.0f;    // the chosen tile grows...
    float push = 0.0f;     // ...and comes out
    float middle = 0.0f;   // the radius it grows about
    float centre = 0.0f;   // its middle's radius as drawn (grown, pushed out, opened)
    bool chosen = false;
};

void DrawTile(ImDrawList* dl, Fonts const& fonts, WheelView const& view, Entry const& entry, TileDraw const& t, float alpha) {
    Theme const theme = t.chosen ? Chosen() : Omni();
    float const dim = entry.enabled ? 1.0f : 0.5f;
    std::vector<ImVec2> const outline = ToCanvas(pw::Outline(t.strip));

    // The game's button fill: brightest at the border, falling off toward the middle (the outline shrunk about the tile's
    // middle in rings, each vertex its own colour; the middle a fan).
    {
        constexpr int kRings = 8;
        constexpr float kInnermost = 0.5f;
        ImVec2 const uv = ImGui::GetFontTexUvWhitePixel();
        ImVec2 const pivot = ToCanvas(Mul(Dir(t.angle), t.centre));
        std::vector<std::vector<ImVec2>> rings;
        std::vector<ImU32> colours;
        for (int k = 0; k <= kRings; ++k) {
            float const u = static_cast<float>(k) / static_cast<float>(kRings);
            rings.push_back(ToCanvas(pw::Outline(pw::Transformed(t.strip, t.angle, t.centre, 1.0f - (1.0f - kInnermost) * u, 0.0f))));
            float const falloff = 1.0f - std::pow(1.0f - u, 2.4f);
            colours.push_back(WithAlpha(Mix(theme.fillEdge, theme.fillCentre, falloff), alpha * (entry.enabled ? 1.0f : 0.8f)));
        }
        std::size_t const n = rings.front().size();
        for (int k = 0; k < kRings; ++k) {
            dl->PrimReserve(static_cast<int>(n * 6), static_cast<int>(n * 2));
            ImDrawIdx const base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
            for (std::size_t i = 0; i < n; ++i) {
                dl->PrimWriteVtx(rings[static_cast<std::size_t>(k)][i], uv, colours[static_cast<std::size_t>(k)]);
                dl->PrimWriteVtx(rings[static_cast<std::size_t>(k) + 1][i], uv, colours[static_cast<std::size_t>(k) + 1]);
            }
            for (std::size_t i = 0; i < n; ++i) {
                std::size_t const j = (i + 1) % n;
                auto const a0 = static_cast<ImDrawIdx>(base + i * 2);
                auto const a1 = static_cast<ImDrawIdx>(a0 + 1);
                auto const b0 = static_cast<ImDrawIdx>(base + j * 2);
                auto const b1 = static_cast<ImDrawIdx>(b0 + 1);
                dl->PrimWriteIdx(a0);
                dl->PrimWriteIdx(b0);
                dl->PrimWriteIdx(b1);
                dl->PrimWriteIdx(a0);
                dl->PrimWriteIdx(b1);
                dl->PrimWriteIdx(a1);
            }
        }
        std::vector<ImVec2> const& last = rings.back();
        dl->PrimReserve(static_cast<int>(last.size() * 3), static_cast<int>(last.size() + 1));
        ImDrawIdx const base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
        dl->PrimWriteVtx(pivot, uv, colours.back());
        for (ImVec2 p : last) {
            dl->PrimWriteVtx(p, uv, colours.back());
        }
        for (std::size_t i = 0; i < last.size(); ++i) {
            dl->PrimWriteIdx(base);
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + i));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + (i + 1) % last.size()));
        }
    }
    // The game's panels' fine horizontal scanlines, inside an inset of the border.
    pw::Strip const insetStrip = pw::Transformed(t.strip, t.angle, t.centre, 0.94f, 0.0f);
    std::vector<ImVec2> const inset = ToCanvas(pw::Outline(insetStrip));
    {
        float y0 = FLT_MAX;
        float y1 = -FLT_MAX;
        for (ImVec2 p : inset) {
            y0 = std::min(y0, p.y);
            y1 = std::max(y1, p.y);
        }
        ImU32 const line = WithAlpha(theme.scanlines, alpha * dim);
        for (float y = std::floor(y0) + 1.5f; y < y1; y += 3.0f) {
            float left = 0.0f;
            float right = 0.0f;
            if (SpanAt(inset, y, left, right) && right - left > 4.0f) {
                dl->AddLine({left + 1.0f, y}, {right - 1.0f, y}, line, 1.0f);
            }
        }
    }
    dl->AddPolyline(inset.data(), static_cast<int>(inset.size()), WithAlpha(theme.inset, alpha * dim), ImDrawFlags_Closed, 1.2f);

    // Its cooldown: a veil, and the border lit only as far as it has recharged.
    if (entry.cooldown > 0.0f) {
        FillStrip(dl, t.strip, WithAlpha(Rgba(0, 0, 0, 150), alpha * entry.cooldown), WithAlpha(Rgba(0, 0, 0, 110), alpha * entry.cooldown));
    }
    float const thickness = t.chosen ? 2.6f : 1.8f;
    ImU32 const border = WithAlpha(theme.border, alpha * dim);
    if (entry.cooldown > 0.0f) {
        dl->AddPolyline(outline.data(), static_cast<int>(outline.size()), WithAlpha(theme.border, alpha * 0.35f), ImDrawFlags_Closed, thickness);
        std::size_t const lit = static_cast<std::size_t>(std::lround((1.0f - entry.cooldown) * static_cast<float>(t.strip.outer.size() - 1))) + 1;
        std::vector<ImVec2> const outer = ToCanvas(t.strip.outer);
        if (lit >= 2) {
            dl->AddPolyline(outer.data(), static_cast<int>(std::min(lit, outer.size())), border, 0, thickness);
        }
    } else {
        dl->AddPolyline(outline.data(), static_cast<int>(outline.size()), border, ImDrawFlags_Closed, thickness);
        dl->AddPolyline(outline.data(), static_cast<int>(outline.size()), WithAlpha(theme.borderCore, alpha * dim * (t.chosen ? 0.9f : 0.55f)),
            ImDrawFlags_Closed, t.chosen ? 1.6f : 1.0f);
    }

    // The icon over the name, upright whatever the tile's angle.
    pw::Vec2 const middle = Mul(Dir(t.angle), t.centre + (view.tile.outerRadius - view.tile.innerRadius) * 0.02f);
    ImVec2 const c = ToCanvas(middle);
    float const iconSize = 0.70f * (view.tile.outerRadius - view.tile.innerRadius) * t.scale;
    float const labelSize = (fonts.gameFont ? 34.0f : 42.0f) * (t.chosen ? 1.06f : 1.0f);
    float const iconTop = c.y - iconSize * 0.66f;
    float const labelTop = iconTop + iconSize * 0.94f;
    if (entry.icon != 0) {
        // The icons are white light (power_icons::Hologram): tinted the tile's colour.
        ImU32 const tint = entry.enabled ? WithAlpha(t.chosen ? Rgba(235, 250, 255) : Rgba(255, 185, 110), alpha)
                                         : WithAlpha(Rgba(150, 120, 100), alpha * 0.6f);
        dl->AddImage(ImTextureRef(entry.icon), {c.x - iconSize * 0.5f, iconTop}, {c.x + iconSize * 0.5f, iconTop + iconSize}, {0.0f, 0.0f},
            {1.0f, 1.0f}, tint);
    }
    bool const upper = view.upperCase || fonts.gameFont;  // the game's Biomass, as the game sets it: capitals
    std::string const text = upper ? Upper(entry.label) : entry.label;
    float left = 0.0f;
    float right = 0.0f;
    float centreX = c.x;
    float room = 240.0f;
    if (SpanAt(outline, labelTop + labelSize * 0.5f, left, right)) {
        centreX = (left + right) * 0.5f;
        room = right - left - 26.0f;
    }
    float size = labelSize;
    float const tracking = fonts.gameFont ? 1.2f : upper ? 2.4f : 0.8f;
    float const width = TrackedWidth(fonts.label, size, text, tracking);
    ImU32 const colour = WithAlpha(theme.label, alpha * dim);
    // A long name on a narrow tile goes on two lines (split at the space nearest its middle) rather than shrinking far.
    std::size_t const space = [&] {
        std::size_t best = std::string::npos;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == ' ' && (best == std::string::npos ||
                                      std::abs(static_cast<int>(i) * 2 - static_cast<int>(text.size())) <
                                          std::abs(static_cast<int>(best) * 2 - static_cast<int>(text.size())))) {
                best = i;
            }
        }
        return best;
    }();
    if (width > room * 1.25f && space != std::string::npos) {
        std::string const first = text.substr(0, space);
        std::string const second = text.substr(space + 1);
        size = labelSize * 0.82f;
        float const longest = std::max(TrackedWidth(fonts.label, size, first, tracking), TrackedWidth(fonts.label, size, second, tracking));
        if (longest > room && longest > 0.0f) {
            size = std::max(size * room / longest, labelSize * 0.55f);
        }
        float const top = labelTop + labelSize * 0.5f - size * 0.95f;
        TrackedCentred(dl, fonts.label, size, centreX, top, colour, first, tracking);
        TrackedCentred(dl, fonts.label, size, centreX, top + size * 0.92f, colour, second, tracking);
        return;
    }
    if (width > room && width > 0.0f) {
        size = std::max(size * room / width, size * 0.6f);
    }
    TrackedCentred(dl, fonts.label, size, centreX, labelTop + (labelSize - size) * 0.5f, colour, text, tracking);
}

}  // namespace

void DrawWheel(ImDrawList* dl, Fonts const& fonts, WheelView const& view) {
    int const count = static_cast<int>(view.entries.size());
    if (count == 0 || fonts.label == nullptr) {
        return;
    }
    float const open = Ease(view.open);
    float const alpha = std::clamp(view.open * 1.6f, 0.0f, 1.0f);
    float const spread = 0.55f + 0.45f * open;  // the tiles fly out from the palm as it opens
    std::vector<float> const angles = pw::Angles(count, view.layout);
    pw::TileSettings tile = view.tile;
    tile.halfWidthDeg = pw::HalfWidthDeg(count, view.tile, view.layout);
    float const middle = (tile.innerRadius + tile.outerRadius) * 0.5f;

    // Each tile's place.
    std::vector<TileDraw> tiles;
    tiles.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        TileDraw t;
        t.index = i;
        t.angle = angles[static_cast<std::size_t>(i)];
        t.chosen = i == view.selected;
        float const grow = t.chosen ? Ease(view.selectedFor / 0.18f) : 0.0f;
        t.scale = 1.0f + 0.10f * grow;
        t.push = 16.0f * grow;
        t.middle = middle;
        pw::Strip const base = pw::TileStrip(t.angle, tile);
        t.strip = pw::Transformed(pw::Transformed(base, t.angle, middle, t.scale, t.push), t.angle, 0.0f, spread, 0.0f);
        t.centre = (middle + t.push) * spread;
        tiles.push_back(std::move(t));
    }

    // The omni-tool projecting the wheel (the user, 2026-11-05: not "a flat line going through the middle"): from an emitter in
    // the wheel's gap, where the forearm and the gauntlet are, a soft cone of light to each tile's inner edge, brightest at the
    // source, thin rays along its sides, and specks of dust drifting out through the light.
    ImVec2 const uv = ImGui::GetFontTexUvWhitePixel();
    ImVec2 emitter = ToCanvas(Mul(Dir(view.layout.gapCentreDeg), tile.outerRadius * spread + 30.0f));
    emitter.x = std::clamp(emitter.x, 24.0f, static_cast<float>(kCanvas) - 24.0f);
    emitter.y = std::clamp(emitter.y, 24.0f, static_cast<float>(kCanvas) - 24.0f);
    for (TileDraw const& t : tiles) {
        ImU32 const light = t.chosen ? kLightChosen : kLight;
        float const strength = (t.chosen ? 0.30f : 0.20f) * alpha * (view.entries[static_cast<std::size_t>(t.index)].enabled ? 1.0f : 0.5f);
        ImVec2 const left = ToCanvas(t.strip.inner.front());
        ImVec2 const right = ToCanvas(t.strip.inner.back());
        ImVec2 const reachL{emitter.x + (left.x - emitter.x) * open, emitter.y + (left.y - emitter.y) * open};
        ImVec2 const reachR{emitter.x + (right.x - emitter.x) * open, emitter.y + (right.y - emitter.y) * open};
        dl->PrimReserve(3, 3);
        ImDrawIdx const base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
        dl->PrimWriteVtx(emitter, uv, WithAlpha(light, strength));
        dl->PrimWriteVtx(reachL, uv, WithAlpha(light, strength * 0.25f));
        dl->PrimWriteVtx(reachR, uv, WithAlpha(light, strength * 0.25f));
        dl->PrimWriteIdx(base);
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
        dl->AddLine(emitter, reachL, WithAlpha(light, strength * 1.4f), 1.0f);
        dl->AddLine(emitter, reachR, WithAlpha(light, strength * 1.4f), 1.0f);
        // Dust: a fixed scatter per tile, drifting outward along the light and fading as it goes.
        for (int k = 0; k < 18; ++k) {
            float const seed = static_cast<float>(t.index * 131 + k * 37);
            float const across = std::fmod(seed * 0.6180339f, 1.0f);
            float const along = std::fmod(seed * 0.4142135f + view.time * (0.05f + 0.03f * across), 1.0f);
            float const reach = 0.12f + 0.83f * along;
            ImVec2 const edge{left.x + (right.x - left.x) * across, left.y + (right.y - left.y) * across};
            ImVec2 const at{emitter.x + (edge.x - emitter.x) * reach * open, emitter.y + (edge.y - emitter.y) * reach * open};
            dl->AddCircleFilled(at, 1.0f + 1.2f * std::fmod(seed * 0.7071f, 1.0f), WithAlpha(light, alpha * 0.55f * (1.0f - along)), 6);
        }
    }
    // The emitter: a hot point in a soft halo.
    dl->AddCircleFilled(emitter, 30.0f, WithAlpha(kEmitter, alpha * 0.10f), 32);
    dl->AddCircleFilled(emitter, 14.0f, WithAlpha(kEmitter, alpha * 0.35f), 24);
    dl->AddCircleFilled(emitter, 5.0f, WithAlpha(Rgba(255, 240, 220), alpha), 16);

    // The tiles, the chosen one last (on top).
    for (TileDraw const& t : tiles) {
        if (!t.chosen) {
            DrawTile(dl, fonts, view, view.entries[static_cast<std::size_t>(t.index)], t, alpha);
        }
    }
    for (TileDraw const& t : tiles) {
        if (t.chosen) {
            DrawTile(dl, fonts, view, view.entries[static_cast<std::size_t>(t.index)], t, alpha);
        }
    }
}

void DrawHint(ImDrawList* dl, Fonts const& fonts, HintView const& view) {
    float const alpha = std::clamp(view.open, 0.0f, 1.0f);
    if (alpha <= 0.0f || fonts.title == nullptr) {
        return;
    }
    float const w = static_cast<float>(kHintWidth);
    float const h = static_cast<float>(kHintHeight);
    float const m = 14.0f;  // margin for the glow
    float const cut = 26.0f;
    std::array<ImVec2, 6> const card{ImVec2{m + cut, m}, ImVec2{w - m, m}, ImVec2{w - m, h - m - cut}, ImVec2{w - m - cut, h - m},
        ImVec2{m, h - m}, ImVec2{m, m + cut}};
    // The game's panel: deep indigo with fine scanlines, a thin cyan frame with 45-degree corners.
    dl->AddConvexPolyFilled(card.data(), 6, WithAlpha(Rgba(4, 14, 52, 222), alpha));
    for (float y = m + 2.5f; y < h - m - 1.0f; y += 3.0f) {
        float const x0 = y < m + cut ? m + cut - (y - m) : m;
        float const x1 = y > h - m - cut ? w - m - (y - (h - m - cut)) : w - m;
        dl->AddLine({x0 + 1.0f, y}, {x1 - 1.0f, y}, WithAlpha(Rgba(150, 200, 240, 14), alpha), 1.0f);
    }
    dl->AddPolyline(card.data(), 6, WithAlpha(kLine, alpha), ImDrawFlags_Closed, 2.0f);

    // The hand, in a box of its own.
    float const boxRight = m + 150.0f;
    dl->AddLine({boxRight, m + 22.0f}, {boxRight, h - m - 22.0f}, WithAlpha(kLine, alpha * 0.6f), 1.6f);
    ImVec2 const palm{m + 76.0f, h * 0.5f + 18.0f};
    ImU32 const line = WithAlpha(Rgba(226, 244, 250), alpha);
    float const s = 1.15f;
    auto finger = [&](float x, float top, float bottom) {
        dl->AddRect({palm.x + (x - 8.0f) * s, palm.y + top * s}, {palm.x + (x + 8.0f) * s, palm.y + bottom * s}, line, 8.0f * s, 0, 2.6f);
    };
    finger(-24.0f, -60.0f, -14.0f);
    finger(-8.0f, -70.0f, -14.0f);
    finger(8.0f, -66.0f, -14.0f);
    finger(24.0f, -54.0f, -14.0f);
    dl->AddRect({palm.x - 33.0f * s, palm.y - 18.0f * s}, {palm.x + 33.0f * s, palm.y + 34.0f * s}, line, 16.0f * s, 0, 2.6f);
    // The thumb: a rounded digit out from the palm's side, up and to the left.
    {
        ImVec2 const base{palm.x - 30.0f * s, palm.y + 12.0f * s};
        ImVec2 const tip{palm.x - 50.0f * s, palm.y - 16.0f * s};
        float const r = 8.0f * s;
        float const along = std::atan2(tip.y - base.y, tip.x - base.x);
        dl->PathArcTo(tip, r, along - kPi * 0.5f, along + kPi * 0.5f, 12);
        dl->PathArcTo(base, r, along + kPi * 0.5f, along + kPi * 1.5f, 12);
        dl->PathStroke(line, ImDrawFlags_Closed, 2.6f);
    }

    float const x = boxRight + 26.0f;
    dl->AddText(fonts.title, 40.0f, {x, m + 14.0f}, WithAlpha(Rgba(255, 157, 90), alpha), view.title.c_str());
    float y = m + 64.0f;
    for (std::string const& text : view.lines) {
        dl->AddText(fonts.body, 31.0f, {x, y}, WithAlpha(Rgba(226, 238, 244), alpha), text.c_str());
        y += 38.0f;
    }
}

void DrawLock(ImDrawList* dl, LockDrawView const& view) {
    float const size = static_cast<float>(kLockCanvas);
    ImVec2 const c{size * 0.5f, size * 0.5f};
    Theme const theme = view.ready ? Chosen() : Omni();
    float const alpha = view.ready ? 1.0f : 0.70f;
    float const closed = size * kLockBox * 0.5f;  // the brackets' half box, closed
    if (view.onActor) {
        // The brackets close in from half again as wide over 0.18 s.
        float const half = closed * (1.0f + 0.5f * (1.0f - Ease(view.lockedFor / 0.18f)));
        float const arm = closed * 0.42f;
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sy = -1; sy <= 1; sy += 2) {
                float const fx = static_cast<float>(sx);
                float const fy = static_cast<float>(sy);
                ImVec2 const corner{c.x + fx * half, c.y + fy * half};
                std::array<ImVec2, 3> const points{ImVec2{corner.x, corner.y - fy * arm}, corner, ImVec2{corner.x - fx * arm, corner.y}};
                dl->AddPolyline(points.data(), 3, WithAlpha(theme.border, alpha), 0, 6.0f);
                dl->AddPolyline(points.data(), 3, WithAlpha(theme.borderCore, alpha), 0, 2.0f);
            }
        }
        Node(dl, c, 9.0f, WithAlpha(theme.border, alpha));
        Node(dl, c, 4.0f, WithAlpha(theme.borderCore, alpha));
    } else {
        // A place: a slowly turning dashed ring with a cross, smaller.
        float const r = closed * 0.70f;
        DashedCircle(dl, c, r, WithAlpha(theme.border, alpha), 4.0f, 30.0f, 15.0f, view.time * 40.0f);
        float const cross = r * 0.35f;
        dl->AddLine({c.x - cross, c.y}, {c.x + cross, c.y}, WithAlpha(theme.borderCore, alpha), 2.0f);
        dl->AddLine({c.x, c.y - cross}, {c.x, c.y + cross}, WithAlpha(theme.borderCore, alpha), 2.0f);
    }
    // The cooldown ring just outside the box: the run part orange, the rest blue.
    float const ringR = closed * 1.28f;
    float const done = 1.0f - std::clamp(view.cooldown, 0.0f, 1.0f);
    if (done < 1.0f) {
        float const start = -kPi * 0.5f;
        dl->PathArcTo(c, ringR, start, start + 2.0f * kPi, 64);
        dl->PathStroke(WithAlpha(kLine, 0.35f), 0, 2.0f);
        if (done > 0.0f) {
            dl->PathArcTo(c, ringR, start, start + 2.0f * kPi * done, 64);
            dl->PathStroke(WithAlpha(kChevron, 0.95f), 0, 4.0f);
        }
    }
    // The equipped power's icon in a cut-corner box, below and right of the brackets.
    if (view.icon != 0) {
        float const s = size * 0.20f;
        ImVec2 const o{c.x + closed * 1.15f, c.y + closed * 0.75f};
        float const cut = s * 0.25f;
        std::array<ImVec2, 5> const box{ImVec2{o.x, o.y}, ImVec2{o.x + s, o.y}, ImVec2{o.x + s, o.y + s - cut}, ImVec2{o.x + s - cut, o.y + s},
            ImVec2{o.x, o.y + s}};
        dl->AddConvexPolyFilled(box.data(), 5, WithAlpha(theme.fillCentre, alpha));
        dl->AddPolyline(box.data(), 5, WithAlpha(theme.border, alpha), ImDrawFlags_Closed, 2.0f);
        float const pad = s * 0.12f;
        dl->AddImage(ImTextureRef(view.icon), {o.x + pad, o.y + pad}, {o.x + s - pad, o.y + s - pad}, {0.0f, 0.0f}, {1.0f, 1.0f},
            WithAlpha(theme.label, alpha));
    }
    // A cast: a ring growing out of the brackets over 0.35 s.
    if (view.sinceCast >= 0.0f && view.sinceCast < 0.35f) {
        float const t = view.sinceCast / 0.35f;
        float const r = closed * (0.6f + 0.9f * Ease(t));
        dl->AddCircle(c, std::min(r, size * 0.5f - 4.0f), WithAlpha(kBeamCore, 1.0f - t), 64, 6.0f * (1.0f - t) + 1.0f);
    }
}

// ---- Fonts -----------------------------------------------------------------------------------------------------------
namespace {

std::string Utf8(std::wstring const& wide) {
    if (wide.empty()) {
        return {};
    }
    int const n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(n, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

ImFont* FirstFont(ImGuiIO& io, std::vector<std::filesystem::path> const& paths, std::string& name) {
    for (std::filesystem::path const& path : paths) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            if (ImFont* font = io.Fonts->AddFontFromFileTTF(Utf8(path.wstring()).c_str(), 48.0f)) {
                name = Utf8(path.filename().wstring());
                return font;
            }
        }
    }
    return nullptr;
}

}  // namespace

Fonts LoadFonts(ImGuiIO& io, std::vector<std::filesystem::path> const& folders, std::string& chosen) {
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    std::filesystem::path const system = std::filesystem::path(windows) / L"Fonts";
    std::vector<std::filesystem::path> labels;
    std::vector<std::filesystem::path> titles;
    std::vector<std::filesystem::path> bodies;
    for (std::filesystem::path const& folder : folders) {
        labels.push_back(folder / L"Rajdhani-Bold.ttf");
        titles.push_back(folder / L"TitilliumWeb-SemiBold.ttf");
        bodies.push_back(folder / L"TitilliumWeb-Regular.ttf");
    }
    for (auto* list : {&labels, &titles, &bodies}) {
        list->push_back(system / L"bahnschrift.ttf");
        list->push_back(system / L"segoeui.ttf");
    }
    Fonts fonts;
    std::string labelName;
    // The game's own Biomass (the user, 2026-11-05: "use the games own font"), built from the player's own game-art cache
    // (game/GameArt copies GUI_SF_Fonts there) through core/gfx; kept alive for the atlas. Missing: the fonts below.
    {
        static std::vector<std::uint8_t> biomass;
        if (biomass.empty()) {
            wchar_t local[MAX_PATH]{};
            if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) != 0) {
                std::filesystem::path const file = std::filesystem::path(local) / L"MEVR" / L"gameart" / L"GUI_SF_Fonts.Fonts.gfx";
                std::ifstream in(file, std::ios::binary);
                std::vector<std::uint8_t> const bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (!bytes.empty()) {
                    if (auto const movie = gfx::Movie::Parse(bytes)) {
                        for (gfx::FontInfo const& info : gfx::Fonts(*movie)) {
                            if (info.name.starts_with("Biomass")) {
                                if (auto ttf = gfx::FontAsTtf(*movie, info.id, "ME1 Biomass")) {
                                    biomass = std::move(*ttf);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
        if (!biomass.empty()) {
            ImFontConfig config;
            config.FontDataOwnedByAtlas = false;
            fonts.label = io.Fonts->AddFontFromMemoryTTF(biomass.data(), static_cast<int>(biomass.size()), 48.0f, &config);
            if (fonts.label != nullptr) {
                fonts.gameFont = true;
                labelName = "the game's Biomass";
            }
        }
    }
    std::string titleName;
    std::string bodyName;
    if (fonts.label == nullptr) {
        fonts.label = FirstFont(io, labels, labelName);
    }
    fonts.title = FirstFont(io, titles, titleName);
    fonts.body = FirstFont(io, bodies, bodyName);
    if (fonts.label == nullptr) {
        fonts.label = io.Fonts->AddFontDefault();
        labelName = "ImGui's default";
    }
    if (fonts.title == nullptr) {
        fonts.title = fonts.label;
        titleName = labelName;
    }
    if (fonts.body == nullptr) {
        fonts.body = fonts.title;
        bodyName = titleName;
    }
    chosen = "labels " + labelName + ", title " + titleName + ", text " + bodyName;
    return fonts;
}

}  // namespace mevr::host::power_wheel_draw
