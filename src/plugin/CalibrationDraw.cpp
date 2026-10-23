// The guided calibration's look (plugin/CalibrationDraw.hpp). ME1's holographic UI, from the game's own
// screens: pale blue-white strokes with a soft glow over a dark translucent blue, thin rules, bevelled
// corners, and orange for what wants the player's attention (the step, the target, the orb to take).

#include "plugin/CalibrationDraw.hpp"

#include "core/vr/Calibration.hpp"

#include <Windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <string>

namespace mevr::host::calibration_draw {
namespace {

namespace cal = vr::calibration;

constexpr float kPi = 3.14159265358979f;

constexpr ImU32 Rgba(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}
constexpr ImU32 kFill = Rgba(8, 22, 42, 212);
constexpr ImU32 kHeaderFill = Rgba(14, 38, 66, 225);
constexpr ImU32 kPale = Rgba(214, 238, 255);
constexpr ImU32 kPaleDim = Rgba(150, 196, 232, 200);
constexpr ImU32 kPaleFaint = Rgba(120, 180, 230, 80);
constexpr ImU32 kGlow = Rgba(110, 190, 255);
constexpr ImU32 kText = Rgba(232, 244, 255);
constexpr ImU32 kTextDim = Rgba(160, 196, 226);
constexpr ImU32 kOrange = Rgba(255, 150, 40);
constexpr ImU32 kOrangeFill = Rgba(255, 140, 30, 236);
constexpr ImU32 kOnOrange = Rgba(34, 18, 4);
constexpr ImU32 kButtonFill = Rgba(16, 40, 68, 230);
constexpr ImU32 kWarn = Rgba(255, 176, 60);

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

// A rectangle with its corners cut.
void Bevel(ImDrawList* dl, ImVec2 a, ImVec2 b, float cut, ImU32 fill, ImU32 line, float thickness) {
    ImVec2 const p[8] = {{a.x + cut, a.y}, {b.x - cut, a.y}, {b.x, a.y + cut}, {b.x, b.y - cut}, {b.x - cut, b.y}, {a.x + cut, b.y},
        {a.x, b.y - cut}, {a.x, a.y + cut}};
    if ((fill & IM_COL32_A_MASK) != 0) {
        dl->AddConvexPolyFilled(p, 8, fill);
    }
    if ((line & IM_COL32_A_MASK) != 0 && thickness > 0.0f) {
        dl->AddPolyline(p, 8, line, thickness, ImDrawFlags_Closed);
    }
}

// The holographic glow: the same outline wider and fainter, under the line itself.
void GlowBevel(ImDrawList* dl, ImVec2 a, ImVec2 b, float cut, ImU32 colour, float thickness, float strength = 1.0f) {
    for (int i = 4; i >= 1; --i) {
        float const g = static_cast<float>(i) * 2.5f;
        Bevel(dl, {a.x, a.y}, {b.x, b.y}, cut, 0, WithAlpha(colour, 0.10f * strength), thickness + g * 2.0f);
    }
    Bevel(dl, a, b, cut, 0, colour, thickness);
}

void GlowCircle(ImDrawList* dl, ImVec2 c, float r, ImU32 colour, float thickness, float strength = 1.0f) {
    for (int i = 4; i >= 1; --i) {
        dl->AddCircle(c, r, WithAlpha(colour, 0.09f * strength), 64, thickness + static_cast<float>(i) * 5.0f);
    }
    dl->AddCircle(c, r, colour, 64, thickness);
}

float TextWidth(ImFont* font, float size, char const* text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
}

void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 colour, char const* text, float alignX = 0.0f) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    float const width = TextWidth(font, size, text);
    dl->AddText(font, size, {std::floor(at.x - width * alignX), std::floor(at.y)}, colour, text);
}

// Wrapped to `width`; returns the height used.
float Wrapped(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 colour, std::string const& text, float width) {
    if (text.empty()) {
        return 0.0f;
    }
    ImVec2 const extent = font->CalcTextSizeA(size, FLT_MAX, width, text.c_str());
    dl->AddText(font, size, {std::floor(at.x), std::floor(at.y)}, colour, text.c_str(), nullptr, width);
    return extent.y;
}

constexpr float kButtonText = 26.0f;
constexpr float kButtonHeight = 54.0f;
constexpr float kButtonGap = 14.0f;
constexpr float kButtonPad = 22.0f;
constexpr float kMarginX = 40.0f;

} // namespace

ScreenText TextFor(Screen screen) {
    switch (screen) {
    case Screen::Prompt:
        return {"Calibrate for these controllers?",
            "About a minute: stand tall, stretch your arms, crouch, then aim a pistol and a rifle at three targets. "
            "Your sights will line up with your eye, and the two-handed grip and the crouch will fit you. "
            "You can run it any time from the VR hands tab.",
            0};
    case Screen::Stand:
        return {"Stand tall, look ahead",
            "Stand up straight where you have room to stretch your arms, and look at this panel. Hold still.", 1};
    case Screen::TPose:
        return {"Arms out to the sides",
            "Hold both arms straight out to the sides, level with your shoulders, like a T. Hold still.", 2};
    case Screen::Forward:
        return {"Both arms straight ahead",
            "Now point both arms straight forward, level with your shoulders, hands about as far apart as your "
            "shoulders. Hold still.",
            2};
    case Screen::Crouch:
        return {"Crouch down", "Crouch as low as you would to hide behind cover, and hold it there.", 3};
    case Screen::PistolGrab:
        return {"Take the pistol",
            "Reach for the orange orb with your right hand and squeeze the grip. Keep it squeezed: the orb is your pistol.", 4};
    case Screen::PistolAim:
        return {"Aim the pistol",
            "Aim with your right hand the way you aim a pistol, at the orange target. Keep it on the target until the "
            "ring fills.",
            4};
    case Screen::RifleGrab:
        return {"Take the rifle",
            "Squeeze the near orb with your right hand and the far orb with your left. Keep both grips squeezed.", 5};
    case Screen::RifleAim:
        return {"Shoulder the rifle and aim",
            "Bring it to your shoulder and look along it as you would a rifle. Aim at the orange target and hold.", 5};
    case Screen::Check:
        return {"Check your aim",
            "Squeeze the right grip and aim at a target: the red dot is where your pistol now points. Squeeze both "
            "grips for the rifle. The dot should sit on what you aim at.",
            6};
    case Screen::Saved:
        return {"Saved", "Your hands, aim and body are calibrated for these controllers. The guns follow it now.", 6};
    }
    return {};
}

char const* ButtonLabel(int id) {
    switch (id) {
    case kSkip: return "SKIP STEP";
    case kRestart: return "START STEP AGAIN";
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
    // Measured without a font (layout must not depend on one being loaded): about 0.56 em per character in
    // Bahnschrift's caps, generous enough for Segoe UI.
    auto const width = [](int id) {
        return static_cast<float>(std::char_traits<char>::length(ButtonLabel(id))) * kButtonText * 0.58f + 2.0f * kButtonPad;
    };
    float const maxRow = static_cast<float>(kPanelWidth) - 2.0f * kMarginX;
    std::vector<std::vector<int>> rows(1);
    float used = 0.0f;
    for (int id : view.buttons) {
        if (id == kRowBreak) {
            if (!rows.back().empty()) {
                rows.emplace_back();
                used = 0.0f;
            }
            continue;
        }
        float const w = width(id);
        if (!rows.back().empty() && used + kButtonGap + w > maxRow) {
            rows.emplace_back();
            used = 0.0f;
        }
        used += (rows.back().empty() ? 0.0f : kButtonGap) + w;
        rows.back().push_back(id);
    }
    std::vector<Button> out;
    float y1 = static_cast<float>(kPanelHeight) - 26.0f;
    for (auto row = rows.rbegin(); row != rows.rend(); ++row) {
        float x = kMarginX;
        for (int id : *row) {
            float const w = width(id);
            out.push_back({id, x, y1 - kButtonHeight, x + w, y1});
            x += w + kButtonGap;
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

void DrawPanel(ImDrawList* dl, ImFont* font, PanelView const& view) {
    float const W = static_cast<float>(kPanelWidth);
    float const H = static_cast<float>(kPanelHeight);
    // The frame: dark translucent blue, a pale glowing outline; a capture flashes it.
    Bevel(dl, {8.0f, 8.0f}, {W - 8.0f, H - 8.0f}, 28.0f, kFill, 0, 0.0f);
    GlowBevel(dl, {8.0f, 8.0f}, {W - 8.0f, H - 8.0f}, 28.0f, Mix(kPaleDim, kOrange, view.flash), 2.5f, 1.0f + view.flash * 2.0f);
    // The header: the title of the whole thing and the controllers it's for.
    Bevel(dl, {22.0f, 20.0f}, {W - 22.0f, 78.0f}, 14.0f, kHeaderFill, 0, 0.0f);
    Text(dl, font, 30.0f, {kMarginX, 32.0f}, kText, view.prompt ? "VR CALIBRATION" : "VR CALIBRATION");
    std::string const device = view.device.empty() ? std::string("unknown controllers") : view.device;
    Text(dl, font, 22.0f, {W - kMarginX, 38.0f}, kTextDim, device.c_str(), 1.0f);
    dl->AddLine({kMarginX, 86.0f}, {kMarginX + 230.0f, 86.0f}, kOrange, 4.0f);
    dl->AddLine({kMarginX + 238.0f, 86.0f}, {W - kMarginX, 86.0f}, kPaleFaint, 2.0f);

    float y = 104.0f;
    // The steps: one pip each, the current one orange.
    if (view.step > 0 && view.steps > 0) {
        for (int i = 1; i <= view.steps; ++i) {
            ImVec2 const c{kMarginX + 14.0f + static_cast<float>(i - 1) * 40.0f, y + 14.0f};
            if (i < view.step) {
                dl->AddNgonFilled(c, 11.0f, kPaleDim, 6);
            } else if (i == view.step) {
                float const pulse = 0.5f + 0.5f * std::sin(view.time * 4.0f);
                dl->AddNgonFilled(c, 13.0f, kOrangeFill, 6);
                dl->AddNgon(c, 17.0f + pulse * 3.0f, WithAlpha(kOrange, 0.5f), 6, 2.0f);
            } else {
                dl->AddNgon(c, 11.0f, kPaleDim, 6, 2.0f);
            }
        }
        char text[32];
        std::snprintf(text, sizeof text, "STEP %d OF %d", view.step, view.steps);
        Text(dl, font, 22.0f, {kMarginX + static_cast<float>(view.steps) * 40.0f + 12.0f, y + 2.0f}, kTextDim, text);
        y += 44.0f;
    } else {
        y += 6.0f;
    }

    // The title and what to do.
    Text(dl, font, 50.0f, {kMarginX, y}, kText, view.title.c_str());
    y += 66.0f;
    float const textWidth = W - 2.0f * kMarginX;
    y += Wrapped(dl, font, 31.0f, {kMarginX, y}, Mix(kText, kTextDim, 0.25f), view.body, textWidth);
    for (std::string const& line : view.summary) {
        y += 6.0f;
        dl->AddNgonFilled({kMarginX + 6.0f, y + 15.0f}, 5.0f, kPaleDim, 6);
        y += Wrapped(dl, font, 27.0f, {kMarginX + 22.0f, y}, kText, line, textWidth - 22.0f);
    }

    // The buttons from the bottom; above them the message and the hold-still ring.
    std::vector<Button> const buttons = LayoutButtons(view);
    float top = H - 26.0f;
    for (Button const& b : buttons) {
        top = std::min(top, b.y0);
    }
    float statusY = top - 64.0f;
    if (!view.message.empty()) {
        float const h = font->CalcTextSizeA(28.0f, FLT_MAX, textWidth, view.message.c_str()).y;
        statusY = std::min(statusY, top - 64.0f - h - 8.0f);
        Wrapped(dl, font, 28.0f, {kMarginX, top - 14.0f - h}, view.warn ? kWarn : kOrange, view.message, textWidth);
    }
    if (!view.status.empty() || view.progress >= 0.0f) {
        ImVec2 const c{kMarginX + 24.0f, statusY + 24.0f};
        dl->AddCircle(c, 22.0f, kPaleFaint, 48, 4.0f);
        if (view.progress > 0.0f) {
            float const p = std::clamp(view.progress, 0.0f, 1.0f);
            dl->PathArcTo(c, 22.0f, -kPi * 0.5f, -kPi * 0.5f + 2.0f * kPi * p, 48);
            dl->PathStroke(kOrange, 6.0f);
        }
        Text(dl, font, 32.0f, {kMarginX + 64.0f, statusY + 6.0f}, view.progress > 0.0f ? kText : kTextDim, view.status.c_str());
    }

    for (Button const& b : buttons) {
        bool const hovered = b.id == view.hovered;
        bool const accent = b.id == kSave || b.id == kCalibrateNow;
        ImU32 const line = hovered ? kOrange : accent ? kOrange : kPaleDim;
        Bevel(dl, {b.x0, b.y0}, {b.x1, b.y1}, 10.0f, hovered ? kOrangeFill : kButtonFill, 0, 0.0f);
        if (hovered || accent) {
            GlowBevel(dl, {b.x0, b.y0}, {b.x1, b.y1}, 10.0f, line, 2.5f, hovered ? 1.5f : 0.8f);
        } else {
            Bevel(dl, {b.x0, b.y0}, {b.x1, b.y1}, 10.0f, 0, line, 2.0f);
        }
        Text(dl, font, kButtonText, {(b.x0 + b.x1) * 0.5f, (b.y0 + b.y1) * 0.5f - kButtonText * 0.55f}, hovered ? kOnOrange : kText,
            ButtonLabel(b.id), 0.5f);
    }

    // Where a controller points.
    if (view.pointer) {
        ImVec2 const p{view.pointerX, view.pointerY};
        dl->AddCircle(p, 15.0f, WithAlpha(kOrange, 0.35f), 32, 7.0f);
        dl->AddCircle(p, 13.0f, kOrange, 32, 3.0f);
        dl->AddCircleFilled(p, 3.5f, kPale, 12);
    }
}

void DrawBoard(ImDrawList* dl, ImFont* font, BoardView const& view) {
    float const S = static_cast<float>(kBoardSize);
    float const perMeter = S / cal::kBoardSize;
    // Faint corner brackets: the board's place in the room, so the targets don't float in nothing.
    float const m = 18.0f;
    float const arm = 90.0f;
    ImVec2 const corners[4] = {{m, m}, {S - m, m}, {S - m, S - m}, {m, S - m}};
    for (int i = 0; i < 4; ++i) {
        float const sx = i == 0 || i == 3 ? 1.0f : -1.0f;
        float const sy = i < 2 ? 1.0f : -1.0f;
        ImVec2 const c = corners[i];
        for (float t : {7.0f, 3.0f}) {
            ImU32 const col = t > 4.0f ? WithAlpha(kGlow, 0.12f) : WithAlpha(kPaleDim, 0.55f);
            dl->AddLine(c, {c.x + sx * arm, c.y}, col, t);
            dl->AddLine(c, {c.x, c.y + sy * arm}, col, t);
        }
    }
    float const r = cal::kTargetRadius * perMeter;
    for (int i = 0; i < cal::kTargets; ++i) {
        ImVec2 const c{S * 0.5f + cal::kTargetX[i] * perMeter, S * 0.5f - cal::kTargetY[i] * perMeter};
        bool const current = i == view.current;
        bool const done = view.done[i];
        float const pulse = current ? 0.5f + 0.5f * std::sin(view.time * 5.0f) : 0.0f;
        ImU32 const colour = current ? Mix(kOrange, kPale, view.flash) : done ? kPaleDim : kPale;
        float const strength = current ? 1.4f + pulse : 0.8f;
        // A dark disc behind, so the rings read against a bright scene.
        dl->AddCircleFilled(c, r * 1.12f, Rgba(6, 16, 30, 150), 64);
        GlowCircle(dl, c, r, colour, 4.0f, strength);
        GlowCircle(dl, c, r * 0.64f, colour, 3.0f, strength * 0.7f);
        dl->AddCircle(c, r * 0.30f, colour, 48, 3.0f);
        if (done && !current) {
            dl->AddCircleFilled(c, r * 0.30f, WithAlpha(kPale, 0.65f), 48);
        } else {
            dl->AddCircleFilled(c, r * 0.09f, colour, 24);
        }
        // Ticks outside the outer ring.
        for (int k = 0; k < 4; ++k) {
            float const a = static_cast<float>(k) * kPi * 0.5f;
            ImVec2 const d{std::cos(a), std::sin(a)};
            dl->AddLine({c.x + d.x * r * 1.15f, c.y + d.y * r * 1.15f}, {c.x + d.x * r * 1.55f, c.y + d.y * r * 1.55f}, colour, 3.0f);
        }
        if (current) {
            float const grow = r * (1.75f + 0.12f * pulse);
            dl->AddCircle(c, grow, WithAlpha(kOrange, 0.35f + 0.3f * pulse), 64, 3.0f);
            Text(dl, font, 46.0f, {c.x, c.y + r * 1.9f}, kOrange, "AIM HERE", 0.5f);
        } else {
            char number[8];
            std::snprintf(number, sizeof number, "%d", i + 1);
            Text(dl, font, 38.0f, {c.x, c.y + r * 1.65f}, done ? kPaleDim : kTextDim, number, 0.5f);
        }
    }
}

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
    // Linear colours: pale blue-white, or the orange accent.
    float const base[3] = {active ? 1.0f : 0.55f, active ? 0.42f : 0.78f, active ? 0.08f : 1.0f};
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
                // Soft edge (one pixel).
                a *= std::clamp((sphere - d) * static_cast<float>(n) * 0.5f, 0.0f, 1.0f);
            }
            // The ring and the glow, added over (light: they don't hide what's behind).
            float const ringLight = std::exp(-std::pow((d - ring) / 0.025f, 2.0f)) * 0.9f;
            float const glow = d > sphere * 0.9f ? std::pow(std::max(0.0f, 1.0f - (d - sphere * 0.9f) / (1.0f - sphere * 0.9f)), 2.2f) * 0.45f : 0.0f;
            float const add = ringLight + glow;
            if (add > 0.0f) {
                float const covered = a;
                a = covered + (1.0f - covered) * std::min(1.0f, add);
                float const k = a > 0.0f ? 1.0f / a : 0.0f;
                // Straight colour of the sum (premultiplied sums, divided by the new coverage).
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
} // namespace

ImFont* LoadFont(ImGuiIO& io, std::string& chosen) {
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    for (wchar_t const* name : {L"bahnschrift.ttf", L"segoeui.ttf"}) {
        std::filesystem::path const path = std::filesystem::path(windows) / L"Fonts" / name;
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            if (ImFont* font = io.Fonts->AddFontFromFileTTF(Utf8(path.wstring()).c_str(), 48.0f)) {
                chosen = Utf8(path.filename().wstring());
                return font;
            }
        }
    }
    chosen = "ImGui's default font";
    return io.Fonts->AddFontDefault();
}

} // namespace mevr::host::calibration_draw
