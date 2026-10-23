// SPDX-License-Identifier: MIT
// The guided calibration's look (plugin/CalibrationDraw.hpp): the coming main menu's style (the user's mockup,
// 2026-10-22), Mass Effect-inspired but our own. Everything that decides the look is in this file.

#include "plugin/CalibrationDraw.hpp"

#include "core/vr/Calibration.hpp"

#include <Windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace mevr::host::calibration_draw {
namespace {

namespace cal = vr::calibration;

constexpr float kPi = 3.14159265358979f;

// ---- Theme (the menu mockup's palette) --------------------------------------------------------------------
constexpr ImU32 Rgba(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}
constexpr ImU32 kBackground = Rgba(0x08, 0x10, 0x1A, 236);
constexpr ImU32 kSurface = Rgba(0x0E, 0x19, 0x26, 245);
constexpr ImU32 kSurfaceHigh = Rgba(0x13, 0x22, 0x35, 250);
constexpr ImU32 kText = Rgba(0xE6, 0xF2, 0xFB);
constexpr ImU32 kTextDim = Rgba(0x9D, 0xB3, 0xC6);
constexpr ImU32 kLine = Rgba(140, 200, 240, 46);       // rgba(140,200,240,0.18)
constexpr ImU32 kLineStrong = Rgba(140, 200, 240, 110);
constexpr ImU32 kCyan = Rgba(0x7C, 0xCB, 0xF2);
constexpr ImU32 kOrange = Rgba(0xFF, 0x8F, 0x3F);
constexpr ImU32 kOnOrange = Rgba(0x1A, 0x0E, 0x05);

constexpr float kMargin = 44.0f;
constexpr float kChamfer = 30.0f;

ImU32 WithAlpha(ImU32 colour, float alpha) {
    int const a = std::clamp(static_cast<int>(alpha * 255.0f + 0.5f), 0, 255);
    return (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}

ImU32 Mix(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto const channel = [&](int shift) {
        float const x = static_cast<float>((a >> shift) & 0xFF);
        float const y = static_cast<float>((b >> shift) & 0xFF);
        return static_cast<ImU32>(std::lround(x + (y - x) * t)) << shift;
    };
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT) | channel(IM_COL32_A_SHIFT);
}

// ---- Shapes ----------------------------------------------------------------------------------------------
// A rectangle with its corners cut (chamfered), convex, clockwise on screen.
void Chamfer(ImDrawList* dl, ImVec2 a, ImVec2 b, float cut, ImU32 fill, ImU32 line, float thickness) {
    ImVec2 const p[8] = {{a.x + cut, a.y}, {b.x - cut, a.y}, {b.x, a.y + cut}, {b.x, b.y - cut}, {b.x - cut, b.y}, {a.x + cut, b.y},
        {a.x, b.y - cut}, {a.x, a.y + cut}};
    if ((fill & IM_COL32_A_MASK) != 0) {
        dl->AddConvexPolyFilled(p, 8, fill);
    }
    if ((line & IM_COL32_A_MASK) != 0 && thickness > 0.0f) {
        dl->AddPolyline(p, 8, line, thickness, ImDrawFlags_Closed);
    }
}

// The short orange strokes on two opposite corners (top left, bottom right), along the chamfer.
void CornerStrokes(ImDrawList* dl, ImVec2 a, ImVec2 b, float cut, float leg, ImU32 colour, float thickness) {
    ImVec2 const topLeft[4] = {{a.x, a.y + cut + leg}, {a.x, a.y + cut}, {a.x + cut, a.y}, {a.x + cut + leg, a.y}};
    ImVec2 const bottomRight[4] = {{b.x, b.y - cut - leg}, {b.x, b.y - cut}, {b.x - cut, b.y}, {b.x - cut - leg, b.y}};
    dl->AddPolyline(topLeft, 4, colour, thickness, 0);
    dl->AddPolyline(bottomRight, 4, colour, thickness, 0);
}

void Check(ImDrawList* dl, ImVec2 c, float size, ImU32 colour, float thickness) {
    ImVec2 const p[3] = {{c.x - size * 0.45f, c.y}, {c.x - size * 0.12f, c.y + size * 0.33f}, {c.x + size * 0.5f, c.y - size * 0.38f}};
    dl->AddPolyline(p, 3, colour, thickness, 0);
}

// A small target: rings and a centre dot.
void TargetIcon(ImDrawList* dl, ImVec2 c, float r, ImU32 colour) {
    dl->AddCircle(c, r, colour, 32, 2.5f);
    dl->AddCircle(c, r * 0.55f, colour, 24, 2.0f);
    dl->AddCircleFilled(c, r * 0.18f, colour, 12);
}

// ---- Text ------------------------------------------------------------------------------------------------
float Width(ImFont* font, float size, char const* text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
}

void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 colour, char const* text, float alignX = 0.0f) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    float const width = Width(font, size, text);
    dl->AddText(font, size, {std::floor(at.x - width * alignX), std::floor(at.y)}, colour, text);
}

// Shrunk (to 65% at most) to fit `maxWidth`, on the same middle line.
void FitText(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 colour, char const* text, float maxWidth, float alignX = 0.0f) {
    float const width = Width(font, size, text);
    float const fitted = width > maxWidth && width > 0.0f ? std::max(size * maxWidth / width, size * 0.65f) : size;
    Text(dl, font, fitted, {at.x, at.y + (size - fitted) * 0.5f}, colour, text, alignX);
}

// Letter-spaced (the kicker's caps).
float Tracked(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 colour, char const* text, float tracking) {
    float x = at.x;
    char glyph[2] = {0, 0};
    for (char const* c = text; *c != '\0'; ++c) {
        glyph[0] = *c;
        dl->AddText(font, size, {std::floor(x), std::floor(at.y)}, colour, glyph);
        x += Width(font, size, glyph) + tracking;
    }
    return x - at.x;
}

float Wrapped(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 colour, std::string const& text, float width) {
    if (text.empty()) {
        return 0.0f;
    }
    ImVec2 const extent = font->CalcTextSizeA(size, FLT_MAX, width, text.c_str());
    dl->AddText(font, size, {std::floor(at.x), std::floor(at.y)}, colour, text.c_str(), nullptr, width);
    return extent.y;
}

// ---- Buttons ---------------------------------------------------------------------------------------------
constexpr float kButtonText = 23.0f;
constexpr float kButtonHeight = 50.0f;
constexpr float kButtonGap = 12.0f;
constexpr float kButtonPad = 22.0f;
constexpr float kBottom = 612.0f;  // the bottom row's lower edge

bool Accent(int id) {
    return id == kSave || id == kCalibrateNow;
}

} // namespace

// ---- Words -----------------------------------------------------------------------------------------------

ScreenText TextFor(Screen screen) {
    switch (screen) {
    case Screen::Prompt:
        return {"CALIBRATE THESE CONTROLLERS?",
            "About a minute: stand, stretch your arms, crouch, then aim a pistol and a rifle at three targets. Your "
            "sights then line up with your eye, and the two-handed grip and the crouch fit you.",
            0};
    case Screen::Stand: return {"STAND TALL, LOOK AHEAD", "Stand straight where you can stretch your arms, and look at this panel.", 1};
    case Screen::TPose: return {"ARMS OUT TO THE SIDES", "Straight out, level with your shoulders, like a T.", 2};
    case Screen::Forward: return {"BOTH ARMS STRAIGHT AHEAD", "Level with your shoulders, hands about a shoulder-width apart.", 2};
    case Screen::Crouch: return {"CROUCH DOWN", "As low as you would go to hide behind cover, and hold it.", 3};
    case Screen::PistolGrab:
        return {"TAKE THE PISTOL", "Squeeze the right grip at the orange orb (the ring is your hand), and keep it squeezed.", 4};
    case Screen::PistolAim: return {"AIM AT THE LIT TARGET", "Aim the pistol the way you naturally would, and keep it on the target.", 4};
    case Screen::RifleGrab:
        return {"TAKE THE RIFLE", "Right grip on the near orb, left grip on the far one; the rings show your hands. Keep both squeezed.", 5};
    case Screen::RifleAim:
        return {"SHOULDER IT, AIM AT THE LIT TARGET",
            "Right hand at your shoulder by your cheek, left hand out where you hold a foregrip. Look along it at the target.", 5};
    case Screen::Check:
        return {"CHECK YOUR AIM", "Squeeze the right grip (pistol) or both grips (rifle) and aim: the red dot should sit on the target.", 6};
    case Screen::Saved: return {"SAVED", "Calibrated for these controllers. The guns follow it from now on.", 6};
    }
    return {};
}

char const* StepLabel(int pip) {
    switch (pip) {
    case 1: return "Height";
    case 2: return "Arms";
    case 3: return "Crouch";
    case 4: return "Pistol";
    case 5: return "Rifle";
    case 6: return "Check";
    default: return "";
    }
}

char const* ButtonLabel(int id) {
    switch (id) {
    case kSkip: return "SKIP";
    case kRestart: return "REDO THIS STEP";
    case kCancel: return "CANCEL";
    case kSave: return "SAVE";
    case kSwitchEye: return "SWITCH EYE";
    case kRedoBody: return "REDO BODY";
    case kRedoPistol: return "REDO PISTOL";
    case kRedoRifle: return "REDO RIFLE";
    case kCalibrateNow: return "CALIBRATE NOW";
    case kDismiss: return "NOT NOW";
    default: return "";
    }
}

std::vector<Button> LayoutButtons(PanelView const& view) {
    // Measured without a font (the layout mustn't depend on one being loaded): generous for Rajdhani,
    // Bahnschrift and Segoe UI caps.
    auto const width = [](int id) {
        return static_cast<float>(std::char_traits<char>::length(ButtonLabel(id))) * kButtonText * 0.60f + 2.0f * kButtonPad;
    };
    std::vector<std::vector<int>> rows(1);
    for (int id : view.buttons) {
        if (id == kRowBreak) {
            if (!rows.back().empty()) {
                rows.emplace_back();
            }
            continue;
        }
        rows.back().push_back(id);
    }
    std::vector<Button> out;
    float y1 = kBottom;
    for (auto row = rows.rbegin(); row != rows.rend(); ++row) {
        float x1 = static_cast<float>(kPanelWidth) - kMargin;  // right-aligned: the hints keep the bottom left
        for (auto id = row->rbegin(); id != row->rend(); ++id) {
            float const w = width(*id);
            out.push_back({*id, x1 - w, y1 - kButtonHeight, x1, y1});
            x1 -= w + kButtonGap;
        }
        y1 -= kButtonHeight + kButtonGap;
    }
    return out;
}

int ButtonAt(PanelView const& view, float x, float y) {
    for (Button const& b : LayoutButtons(view)) {
        if (x >= b.x0 && x <= b.x1 && y >= b.y0 && y <= b.y1) {
            return b.id;
        }
    }
    return kNoButton;
}

void DrawPanel(ImDrawList* dl, Fonts const& fonts, PanelView const& view) {
    ImFont* const heading = fonts.heading;
    ImFont* const body = fonts.body != nullptr ? fonts.body : fonts.heading;
    float const W = static_cast<float>(kPanelWidth);
    float const H = static_cast<float>(kPanelHeight);
    float const right = W - kMargin;

    // The frame: chamfered, a hairline, orange strokes on two corners; a capture flashes it cyan.
    ImVec2 const a{8.0f, 8.0f};
    ImVec2 const b{W - 8.0f, H - 8.0f};
    Chamfer(dl, a, b, kChamfer, kBackground, 0, 0.0f);
    if (view.flash > 0.0f) {
        for (int i = 3; i >= 1; --i) {
            Chamfer(dl, a, b, kChamfer, 0, WithAlpha(kCyan, 0.12f * view.flash), 2.0f + static_cast<float>(i) * 6.0f);
        }
    }
    Chamfer(dl, a, b, kChamfer, 0, Mix(kLine, kCyan, view.flash), 2.0f);
    CornerStrokes(dl, a, b, kChamfer, 46.0f, kOrange, 4.0f);

    // The kicker, the controllers, the step chip.
    float const kickerWidth = Tracked(dl, heading, 22.0f, {kMargin, 30.0f}, kCyan, "CALIBRATION", 3.5f);
    if (!view.device.empty()) {
        FitText(dl, body, 18.0f, {kMargin + kickerWidth + 16.0f, 33.0f}, kTextDim, view.device.c_str(), right - 190.0f - (kMargin + kickerWidth + 16.0f));
    }
    if (view.step > 0) {
        char chip[32];
        std::snprintf(chip, sizeof chip, "Step %d of %d", view.step, view.steps);
        float const w = Width(heading, 20.0f, chip) + 28.0f;
        Chamfer(dl, {right - w, 24.0f}, {right, 56.0f}, 8.0f, kSurfaceHigh, kLineStrong, 1.5f);
        Text(dl, heading, 20.0f, {right - w * 0.5f, 30.0f}, kText, chip, 0.5f);
    }

    // The progress bar: done cyan with a check, the current orange, the rest a surface.
    float y = 74.0f;
    if (view.step > 0) {
        float const gap = 10.0f;
        float const segment = (right - kMargin - gap * static_cast<float>(view.steps - 1)) / static_cast<float>(view.steps);
        for (int i = 1; i <= view.steps; ++i) {
            float const x0 = kMargin + static_cast<float>(i - 1) * (segment + gap);
            bool const done = i < view.step;
            bool const now = i == view.step;
            ImU32 const fill = done ? kCyan : now ? kOrange : kSurfaceHigh;
            Chamfer(dl, {x0, y}, {x0 + segment, y + 8.0f}, 3.0f, fill, now || done ? 0 : kLine, 1.0f);
            float labelX = x0;
            if (done) {
                Check(dl, {x0 + 9.0f, y + 26.0f}, 14.0f, kCyan, 2.5f);
                labelX += 22.0f;
            }
            Text(dl, body, 19.0f, {labelX, y + 15.0f}, done ? kCyan : now ? kText : kTextDim, StepLabel(i));
        }
        y += 62.0f;
    } else {
        y += 18.0f;
    }

    // The instruction and its explanation.
    FitText(dl, heading, 50.0f, {kMargin, y}, kText, view.title.c_str(), right - kMargin);
    y += 62.0f;
    y += Wrapped(dl, body, 26.0f, {kMargin, y}, kTextDim, view.body, right - kMargin) + 18.0f;

    // The middle: the target chips, the status, the summary and the message on the left; the ring on the right.
    bool const ring = view.progress >= 0.0f;
    float const leftWidth = (ring ? 640.0f : right) - kMargin;
    if (view.targets > 0) {
        float const chip = 176.0f;
        for (int i = 0; i < view.targets; ++i) {
            float const x0 = kMargin + static_cast<float>(i) * (chip + 12.0f);
            bool const done = i < 3 && view.targetTaken[i];
            bool const now = i == view.targetCurrent;
            ImU32 const accent = now ? kOrange : done ? kCyan : kTextDim;
            Chamfer(dl, {x0, y}, {x0 + chip, y + 60.0f}, 9.0f, now ? kSurfaceHigh : kSurface, now ? kOrange : done ? WithAlpha(kCyan, 0.6f) : kLine,
                now ? 2.5f : 1.5f);
            if (done && !now) {
                Check(dl, {x0 + 28.0f, y + 30.0f}, 22.0f, kCyan, 3.5f);
            } else {
                TargetIcon(dl, {x0 + 28.0f, y + 30.0f}, 14.0f, accent);
            }
            char label[16];
            std::snprintf(label, sizeof label, "TARGET %d", i + 1);
            Text(dl, heading, 21.0f, {x0 + 52.0f, y + 9.0f}, now ? kText : done ? kCyan : kTextDim, label);
            Text(dl, body, 16.0f, {x0 + 52.0f, y + 33.0f}, accent, now ? "AIM NOW" : done ? "TAKEN" : "NEXT");
        }
        y += 78.0f;
    }
    if (!view.status.empty()) {
        FitText(dl, heading, 30.0f, {kMargin, y}, view.statusWarn ? kOrange : view.progress > 0.0f ? kText : kCyan, view.status.c_str(),
            leftWidth);
        y += 44.0f;
    }
    for (std::string const& line : view.summary) {
        Chamfer(dl, {kMargin, y}, {kMargin + leftWidth, y + 38.0f}, 6.0f, kSurface, kLine, 1.0f);
        dl->AddRectFilled({kMargin + 1.0f, y + 8.0f}, {kMargin + 4.0f, y + 30.0f}, kCyan);
        FitText(dl, body, 21.0f, {kMargin + 16.0f, y + 7.0f}, kText, line.c_str(), leftWidth - 24.0f);
        y += 44.0f;
    }
    if (!view.message.empty()) {
        Wrapped(dl, body, 22.0f, {kMargin, y + 4.0f}, view.warn ? kOrange : kCyan, view.message, leftWidth);
    }
    if (ring) {
        ImVec2 const c{right - 104.0f, 352.0f};
        float const r = 84.0f;
        dl->AddCircle(c, r, kSurfaceHigh, 96, 12.0f);
        dl->AddCircle(c, r + 9.0f, kLine, 96, 1.5f);
        float const p = std::clamp(view.progress, 0.0f, 1.0f);
        if (p > 0.0f) {
            dl->PathArcTo(c, r, -kPi * 0.5f, -kPi * 0.5f + 2.0f * kPi * p, 96);
            dl->PathStroke(kOrange, 12.0f);
        }
        Text(dl, heading, 26.0f, {c.x, c.y - 30.0f}, p > 0.0f ? kText : kTextDim, "HOLD", 0.5f);
        Text(dl, heading, 26.0f, {c.x, c.y - 2.0f}, p > 0.0f ? kText : kTextDim, "STILL", 0.5f);
    }

    // The bottom: the hints on the left, the buttons on the right.
    std::vector<Button> const buttons = LayoutButtons(view);
    float lineEnd = right;  // the rule over the hints stops short of a second row of buttons
    for (Button const& button : buttons) {
        if (button.y1 < kBottom - 1.0f) {
            lineEnd = std::min(lineEnd, button.x0 - 16.0f);
        }
    }
    dl->AddLine({kMargin, 548.0f}, {lineEnd, 548.0f}, kLine, 1.5f);
    if (view.hints) {
        float x = kMargin;
        auto const hint = [&](char const* key, char const* what) {
            float const w = Width(heading, 18.0f, key) + 20.0f;
            Chamfer(dl, {x, 571.0f}, {x + w, 599.0f}, 5.0f, kSurfaceHigh, kLineStrong, 1.2f);
            Text(dl, heading, 18.0f, {x + w * 0.5f, 575.0f}, kText, key, 0.5f);
            x += w + 10.0f;
            Text(dl, body, 19.0f, {x, 575.0f}, kTextDim, what);
            x += Width(body, 19.0f, what) + 26.0f;
        };
        hint("B", "back a step");
        hint("HOLD BOTH STICKS", "cancel");
    }
    for (Button const& button : buttons) {
        bool const hovered = button.id == view.hovered;
        ImVec2 const p0{button.x0, button.y0};
        ImVec2 const p1{button.x1, button.y1};
        if (Accent(button.id)) {
            if (hovered) {
                for (int i = 3; i >= 1; --i) {
                    Chamfer(dl, p0, p1, 9.0f, 0, WithAlpha(kOrange, 0.14f), 2.0f + static_cast<float>(i) * 5.0f);
                }
            }
            Chamfer(dl, p0, p1, 9.0f, hovered ? Mix(kOrange, Rgba(255, 255, 255), 0.18f) : kOrange, 0, 0.0f);
            Text(dl, heading, kButtonText, {(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f - kButtonText * 0.56f}, kOnOrange, ButtonLabel(button.id), 0.5f);
        } else {
            if (hovered) {
                for (int i = 3; i >= 1; --i) {
                    Chamfer(dl, p0, p1, 9.0f, 0, WithAlpha(kCyan, 0.12f), 2.0f + static_cast<float>(i) * 5.0f);
                }
            }
            Chamfer(dl, p0, p1, 9.0f, hovered ? kSurfaceHigh : kSurface, hovered ? kCyan : WithAlpha(kCyan, 0.55f), hovered ? 2.5f : 1.8f);
            Text(dl, heading, kButtonText, {(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f - kButtonText * 0.56f}, hovered ? kCyan : kText,
                ButtonLabel(button.id), 0.5f);
        }
    }

    // Where a controller points.
    if (view.pointer) {
        ImVec2 const p{view.pointerX, view.pointerY};
        dl->AddCircle(p, 15.0f, WithAlpha(kOrange, 0.35f), 32, 7.0f);
        dl->AddCircle(p, 13.0f, kOrange, 32, 3.0f);
        dl->AddCircleFilled(p, 3.5f, kText, 12);
    }
}

void DrawBoard(ImDrawList* dl, Fonts const& fonts, BoardView const& view) {
    ImFont* const heading = fonts.heading;
    float const S = static_cast<float>(kBoardSize);
    float const perMeter = S / cal::kBoardSize;
    // Corner brackets (the board's place in the room), orange on two opposite corners like the panel.
    float const m = 18.0f;
    float const arm = 90.0f;
    ImVec2 const corners[4] = {{m, m}, {S - m, m}, {S - m, S - m}, {m, S - m}};
    for (int i = 0; i < 4; ++i) {
        float const sx = i == 0 || i == 3 ? 1.0f : -1.0f;
        float const sy = i < 2 ? 1.0f : -1.0f;
        ImVec2 const c = corners[i];
        ImU32 const col = i == 0 || i == 2 ? kOrange : WithAlpha(kCyan, 0.55f);
        dl->AddLine(c, {c.x + sx * arm, c.y}, col, 3.0f);
        dl->AddLine(c, {c.x, c.y + sy * arm}, col, 3.0f);
    }
    float const r = cal::kTargetRadius * perMeter;
    // Why the aim isn't captured, across the top (clear of the upper left target): where the player is looking.
    if (!view.hint.empty()) {
        float const size = 44.0f;
        float const maxWidth = S - 2.0f * 260.0f;
        float const w = std::min(Width(heading, size, view.hint.c_str()), maxWidth) + 48.0f;
        ImU32 const accent = view.hintWarn ? kOrange : kCyan;
        Chamfer(dl, {S * 0.5f - w * 0.5f, 34.0f}, {S * 0.5f + w * 0.5f, 98.0f}, 10.0f, Rgba(0x08, 0x10, 0x1A, 210), WithAlpha(accent, 0.8f), 2.5f);
        FitText(dl, heading, size, {S * 0.5f, 42.0f}, accent, view.hint.c_str(), maxWidth, 0.5f);
    }
    for (int i = 0; i < cal::kTargets; ++i) {
        ImVec2 const c{S * 0.5f + cal::kTargetX[i] * perMeter, S * 0.5f - cal::kTargetY[i] * perMeter};
        bool const current = i == view.current;
        bool const done = view.done[i];
        float const pulse = current ? 0.5f + 0.5f * std::sin(view.time * 5.0f) : 0.0f;
        ImU32 const colour = current ? Mix(kOrange, kCyan, view.flash) : kCyan;
        dl->AddCircleFilled(c, r * 1.15f, Rgba(0x08, 0x10, 0x1A, 170), 64);  // reads against a bright scene
        if (current) {
            for (int g = 4; g >= 1; --g) {
                dl->AddCircle(c, r, WithAlpha(kOrange, 0.10f + 0.05f * pulse), 64, 4.0f + static_cast<float>(g) * 6.0f);
            }
        }
        dl->AddCircle(c, r, colour, 64, current ? 5.0f : 3.5f);
        dl->AddCircle(c, r * 0.62f, WithAlpha(colour, current ? 1.0f : 0.8f), 48, 3.0f);
        dl->AddCircle(c, r * 0.28f, colour, 32, 3.0f);
        if (done && !current) {
            dl->AddCircleFilled(c, r * 0.28f, WithAlpha(kCyan, 0.85f), 32);
            Check(dl, c, r * 0.32f, Rgba(0x08, 0x10, 0x1A), 4.0f);
        } else {
            dl->AddCircleFilled(c, r * 0.08f, colour, 16);
        }
        for (int k = 0; k < 4; ++k) {
            float const ang = static_cast<float>(k) * kPi * 0.5f;
            ImVec2 const d{std::cos(ang), std::sin(ang)};
            dl->AddLine({c.x + d.x * r * 1.15f, c.y + d.y * r * 1.15f}, {c.x + d.x * r * 1.5f, c.y + d.y * r * 1.5f}, colour, 3.0f);
        }
        if (current) {
            dl->AddCircle(c, r * (1.75f + 0.12f * pulse), WithAlpha(kOrange, 0.35f + 0.3f * pulse), 64, 3.0f);
            Text(dl, heading, 46.0f, {c.x, c.y + r * 1.9f}, kOrange, "AIM HERE", 0.5f);
        } else {
            char number[8];
            std::snprintf(number, sizeof number, "%d", i + 1);
            Text(dl, heading, 38.0f, {c.x, c.y + r * 1.6f}, done ? kCyan : kTextDim, number, 0.5f);
        }
    }
}

// ---- Pictures -----------------------------------------------------------------------------------------------
namespace {

// Linear light -> an sRGB byte.
std::uint32_t Encode(float linear) {
    linear = std::clamp(linear, 0.0f, 1.0f);
    float const c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint32_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
}

// Premultiplied: `rgb` (linear) at coverage `a`.
std::uint32_t Pixel(float r, float g, float b, float a) {
    a = std::clamp(a, 0.0f, 1.0f);
    return Encode(r * a) | (Encode(g * a) << 8) | (Encode(b * a) << 16) | (static_cast<std::uint32_t>(std::lround(a * 255.0f)) << 24);
}

} // namespace

std::vector<std::uint32_t> OrbPicture(bool active) {
    unsigned const n = kOrbPixels;
    std::vector<std::uint32_t> pixels(static_cast<size_t>(n) * n);
    // Linear colours: the accent orange (#FF8F3F) to grab, the highlight cyan (#7CCBF2) in the hand.
    float const base[3] = {active ? 1.0f : 0.20f, active ? 0.27f : 0.60f, active ? 0.05f : 0.89f};
    float const sphere = 0.58f;   // the ball's radius, of the half size
    float const ring = 0.74f;     // the holographic ring around it
    for (unsigned y = 0; y < n; ++y) {
        for (unsigned x = 0; x < n; ++x) {
            float const px = (static_cast<float>(x) + 0.5f) / static_cast<float>(n) * 2.0f - 1.0f;
            float const py = 1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(n) * 2.0f;
            float const d = std::sqrt(px * px + py * py);
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            if (d < sphere) {
                float const sx = px / sphere, sy = py / sphere;
                float const sz = std::sqrt(std::max(0.0f, 1.0f - sx * sx - sy * sy));
                // Light from the upper left, a rim (fresnel) that makes it read as a hologram, a highlight.
                float const lambert = std::max(0.0f, -0.45f * sx + 0.55f * sy + 0.70f * sz);
                float const rim = std::pow(1.0f - sz, 2.5f);
                float const spec = std::pow(std::max(0.0f, -0.35f * sx + 0.45f * sy + 0.82f * sz), 40.0f);
                float const shade = 0.22f + 0.55f * lambert + 0.9f * rim;
                r = base[0] * shade + spec;
                g = base[1] * shade + spec;
                b = base[2] * shade + spec;
                a = 0.78f + 0.22f * rim;
                a *= std::clamp((sphere - d) * static_cast<float>(n) * 0.5f, 0.0f, 1.0f);  // a one-pixel soft edge
            }
            // The ring and the glow, added over (light: they don't hide what's behind).
            float const ringLight = std::exp(-std::pow((d - ring) / 0.025f, 2.0f)) * 0.9f;
            float const glow = d > sphere * 0.9f ? std::pow(std::max(0.0f, 1.0f - (d - sphere * 0.9f) / (1.0f - sphere * 0.9f)), 2.2f) * 0.45f : 0.0f;
            float const add = ringLight + glow;
            if (add > 0.0f) {
                float const covered = a;
                a = covered + (1.0f - covered) * std::min(1.0f, add);
                float const k = a > 0.0f ? 1.0f / a : 0.0f;
                r = (r * covered + base[0] * add * 1.2f) * k;
                g = (g * covered + base[1] * add * 1.2f) * k;
                b = (b * covered + base[2] * add * 1.2f) * k;
            }
            pixels[static_cast<size_t>(y) * n + x] = Pixel(r, g, b, a);
        }
    }
    return pixels;
}

std::vector<std::uint32_t> DotPicture() {
    unsigned const n = kDotPixels;
    std::vector<std::uint32_t> pixels(static_cast<size_t>(n) * n);
    for (unsigned y = 0; y < n; ++y) {
        for (unsigned x = 0; x < n; ++x) {
            float const px = (static_cast<float>(x) + 0.5f) / static_cast<float>(n) * 2.0f - 1.0f;
            float const py = (static_cast<float>(y) + 0.5f) / static_cast<float>(n) * 2.0f - 1.0f;
            float const d = std::sqrt(px * px + py * py);
            float const core = std::clamp((0.30f - d) * static_cast<float>(n) * 0.5f, 0.0f, 1.0f);
            float const glow = std::pow(std::max(0.0f, 1.0f - d), 3.0f) * 0.7f;
            float const hot = std::clamp((0.12f - d) * static_cast<float>(n) * 0.5f, 0.0f, 1.0f);
            float const a = std::min(1.0f, core + glow * (1.0f - core));
            float const k = a > 0.0f ? 1.0f / a : 0.0f;
            float const r = (core * 1.0f + glow * (1.0f - core) * 1.0f) * k;
            float const g = (core * (0.12f + 0.75f * hot) + glow * (1.0f - core) * 0.10f) * k;
            float const b = (core * (0.06f + 0.6f * hot) + glow * (1.0f - core) * 0.05f) * k;
            pixels[static_cast<size_t>(y) * n + x] = Pixel(r, g, b, a);
        }
    }
    return pixels;
}

std::vector<std::uint32_t> HandMarkerPicture() {
    unsigned const n = kMarkerPixels;
    std::vector<std::uint32_t> pixels(static_cast<size_t>(n) * n);
    // Linear cyan (#7CCBF2): a ring with a dark edge (reads over a bright scene) and a small centre dot.
    float const cyan[3] = {0.20f, 0.60f, 0.89f};
    for (unsigned y = 0; y < n; ++y) {
        for (unsigned x = 0; x < n; ++x) {
            float const px = (static_cast<float>(x) + 0.5f) / static_cast<float>(n) * 2.0f - 1.0f;
            float const py = (static_cast<float>(y) + 0.5f) / static_cast<float>(n) * 2.0f - 1.0f;
            float const d = std::sqrt(px * px + py * py);
            float const pixel = 2.0f / static_cast<float>(n);
            auto const band = [&](float inner, float outer) {
                return std::clamp((d - inner) / pixel + 0.5f, 0.0f, 1.0f) * std::clamp((outer - d) / pixel + 0.5f, 0.0f, 1.0f);
            };
            float const ring = band(0.70f, 0.86f);
            float const edge = std::max(band(0.62f, 0.70f), band(0.86f, 0.94f));
            float const dot = std::clamp((0.14f - d) / pixel + 0.5f, 0.0f, 1.0f);
            float const light = std::max(ring, dot);
            float const a = std::min(1.0f, light + edge * 0.6f * (1.0f - light));
            float const k = a > 0.0f ? light / a : 0.0f;
            pixels[static_cast<size_t>(y) * n + x] = Pixel(cyan[0] * k * 1.3f, cyan[1] * k * 1.3f, cyan[2] * k * 1.3f, a);
        }
    }
    return pixels;
}

// ---- Fonts ---------------------------------------------------------------------------------------------------
namespace {

std::string Utf8(std::wstring const& wide) {
    if (wide.empty()) {
        return {};
    }
    int const n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(n, 0)), '\0');
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

} // namespace

Fonts LoadFonts(ImGuiIO& io, std::vector<std::filesystem::path> const& folders, std::string& chosen) {
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    std::filesystem::path const system = std::filesystem::path(windows) / L"Fonts";
    std::vector<std::filesystem::path> headings;
    std::vector<std::filesystem::path> bodies;
    for (std::filesystem::path const& folder : folders) {
        headings.push_back(folder / L"Rajdhani-SemiBold.ttf");
        headings.push_back(folder / L"Rajdhani-Bold.ttf");
        bodies.push_back(folder / L"TitilliumWeb-Regular.ttf");
        bodies.push_back(folder / L"TitilliumWeb-SemiBold.ttf");
    }
    headings.push_back(system / L"bahnschrift.ttf");
    headings.push_back(system / L"segoeui.ttf");
    bodies.push_back(system / L"bahnschrift.ttf");
    bodies.push_back(system / L"segoeui.ttf");
    Fonts fonts;
    std::string headingName;
    std::string bodyName;
    fonts.heading = FirstFont(io, headings, headingName);
    fonts.body = FirstFont(io, bodies, bodyName);
    if (fonts.heading == nullptr) {
        fonts.heading = io.Fonts->AddFontDefault();
        headingName = "ImGui's default";
    }
    if (fonts.body == nullptr) {
        fonts.body = fonts.heading;
        bodyName = headingName;
    }
    chosen = "headings " + headingName + ", text " + bodyName;
    return fonts;
}

} // namespace mevr::host::calibration_draw
