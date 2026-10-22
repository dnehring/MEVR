// The squad weapon menu's look (plugin/SquadPanelDraw.hpp): ImGui draw lists only, no game images. The
// weapons are simple vector silhouettes and the squadmates get a coloured class badge with their
// initials. Styling after the user's concept (2026-10-22): dark translucent blue panels, bevelled cards,
// hexagonal tiles, cyan outlines, the highlight in amber.

#include "plugin/SquadPanelDraw.hpp"

#include "core/vr/SquadMenu.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <string>

namespace mevr::host::squad_panel {
namespace {

namespace menu = vr::squad_menu;

// ---- Colours ------------------------------------------------------------------------------------------
constexpr ImU32 Rgba(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}
constexpr ImU32 kBackdrop = Rgba(4, 12, 24, 120);
constexpr ImU32 kPanel = Rgba(8, 20, 38, 205);
constexpr ImU32 kCard = Rgba(10, 28, 50, 225);
constexpr ImU32 kCardSelected = Rgba(22, 38, 58, 235);
constexpr ImU32 kTile = Rgba(12, 34, 60, 228);
constexpr ImU32 kCyan = Rgba(92, 208, 255);
constexpr ImU32 kCyanDim = Rgba(70, 150, 200, 175);
constexpr ImU32 kCyanFaint = Rgba(70, 150, 200, 70);
constexpr ImU32 kAmber = Rgba(255, 180, 48);
constexpr ImU32 kAmberFill = Rgba(255, 166, 30, 238);
constexpr ImU32 kText = Rgba(224, 240, 255);
constexpr ImU32 kTextDim = Rgba(150, 186, 216);
constexpr ImU32 kTextOnAmber = Rgba(30, 18, 4);
constexpr ImU32 kHealth = Rgba(84, 182, 255);
constexpr ImU32 kHealthLow = Rgba(255, 92, 70);
constexpr ImU32 kHealthEmpty = Rgba(30, 52, 76, 220);

ImU32 WithAlpha(ImU32 colour, int alpha) {
    return (colour & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(alpha, 0, 255)) << IM_COL32_A_SHIFT);
}

ImU32 DisciplineColour(int discipline) {
    switch (discipline) {
    case 1: return Rgba(90, 170, 255);   // tech
    case 2: return Rgba(176, 118, 255);  // biotic
    default: return Rgba(232, 92, 70);   // combat
    }
}

// ---- Shapes -------------------------------------------------------------------------------------------
// A rectangle with its corners cut (convex, clockwise on screen).
void Bevel(ImDrawList* dl, ImVec2 a, ImVec2 b, float cut, ImU32 fill, ImU32 line, float thickness) {
    ImVec2 const p[8] = {{a.x + cut, a.y}, {b.x - cut, a.y}, {b.x, a.y + cut}, {b.x, b.y - cut},
        {b.x - cut, b.y}, {a.x + cut, b.y}, {a.x, b.y - cut}, {a.x, a.y + cut}};
    if ((fill & IM_COL32_A_MASK) != 0) {
        dl->AddConvexPolyFilled(p, 8, fill);
    }
    if ((line & IM_COL32_A_MASK) != 0 && thickness > 0.0f) {
        dl->AddPolyline(p, 8, line, thickness, ImDrawFlags_Closed);
    }
}

// A hexagon, wide, pointed left and right.
void Hex(ImDrawList* dl, ImVec2 c, float w, float h, ImU32 fill, ImU32 line, float thickness) {
    float const k = h * 0.38f;
    ImVec2 const p[6] = {{c.x - w * 0.5f, c.y}, {c.x - w * 0.5f + k, c.y - h * 0.5f}, {c.x + w * 0.5f - k, c.y - h * 0.5f},
        {c.x + w * 0.5f, c.y}, {c.x + w * 0.5f - k, c.y + h * 0.5f}, {c.x - w * 0.5f + k, c.y + h * 0.5f}};
    if ((fill & IM_COL32_A_MASK) != 0) {
        dl->AddConvexPolyFilled(p, 6, fill);
    }
    if ((line & IM_COL32_A_MASK) != 0 && thickness > 0.0f) {
        dl->AddPolyline(p, 6, line, thickness, ImDrawFlags_Closed);
    }
}

void Text(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 colour, char const* text, float alignX = 0.0f) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    ImVec2 const extent = font->CalcTextSizeA(size, 10000.0f, 0.0f, text);
    dl->AddText(font, size, {std::floor(at.x - extent.x * alignX), std::floor(at.y)}, colour, text);
}

// Text shrunk (down to 60%) to fit `maxWidth`, kept on the same middle line.
void FitText(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 colour, char const* text, float maxWidth,
    float alignX = 0.0f) {
    if (text == nullptr || text[0] == '\0') {
        return;
    }
    float const width = font->CalcTextSizeA(size, 10000.0f, 0.0f, text).x;
    float const fitted = width > maxWidth && width > 0.0f ? std::max(size * maxWidth / width, size * 0.6f) : size;
    Text(dl, font, fitted, {at.x, at.y + (size - fitted) * 0.5f}, colour, text, alignX);
}

// ---- Weapon silhouettes: convex parts in a 120 x 50 box, drawn centred ------------------------------
struct Part {
    int count;
    float xy[8];
};

constexpr Part kPistol[] = {
    {4, {30, 12, 92, 12, 92, 24, 30, 24}},  // slide
    {4, {92, 15, 97, 15, 97, 21, 92, 21}},  // muzzle
    {4, {36, 24, 54, 24, 48, 46, 32, 46}},  // grip
    {4, {54, 24, 64, 24, 62, 31, 54, 31}},  // trigger guard
};
constexpr Part kShotgun[] = {
    {4, {2, 20, 26, 16, 26, 28, 6, 34}},     // stock
    {4, {26, 14, 56, 14, 56, 26, 26, 26}},   // receiver
    {4, {56, 14, 118, 14, 118, 19, 56, 19}}, // barrel
    {4, {64, 20, 96, 20, 96, 27, 64, 27}},   // pump
    {4, {30, 26, 40, 26, 36, 38, 28, 38}},   // grip
};
constexpr Part kRifle[] = {
    {4, {2, 16, 22, 14, 22, 28, 4, 32}},     // stock
    {4, {22, 10, 76, 10, 76, 26, 22, 26}},   // receiver
    {4, {76, 12, 96, 12, 96, 24, 76, 24}},   // handguard
    {4, {96, 15, 114, 15, 114, 20, 96, 20}}, // barrel
    {4, {40, 6, 56, 6, 56, 10, 40, 10}},     // sight
    {4, {52, 26, 62, 26, 58, 42, 48, 42}},   // magazine
    {4, {30, 26, 39, 26, 35, 40, 27, 40}},   // grip
};
constexpr Part kSniper[] = {
    {4, {0, 18, 26, 16, 26, 28, 2, 34}},     // stock
    {4, {26, 16, 62, 16, 62, 26, 26, 26}},   // receiver
    {4, {62, 19, 120, 19, 120, 23, 62, 23}}, // barrel
    {4, {32, 7, 68, 7, 68, 14, 32, 14}},     // scope
    {4, {28, 8, 32, 6, 32, 15, 28, 13}},     // eyepiece
    {4, {42, 14, 46, 14, 46, 16, 42, 16}},   // mounts
    {4, {56, 14, 60, 14, 60, 16, 56, 16}},
    {4, {32, 26, 40, 26, 37, 38, 30, 38}},   // grip
};

void Weapon(ImDrawList* dl, int type, ImVec2 centre, float scale, ImU32 colour) {
    Part const* parts = nullptr;
    int count = 0;
    switch (type) {
    case 0: parts = kPistol; count = static_cast<int>(std::size(kPistol)); break;
    case 1: parts = kShotgun; count = static_cast<int>(std::size(kShotgun)); break;
    case 2: parts = kRifle; count = static_cast<int>(std::size(kRifle)); break;
    case 3: parts = kSniper; count = static_cast<int>(std::size(kSniper)); break;
    default: return;
    }
    for (int i = 0; i < count; ++i) {
        ImVec2 p[4];
        for (int k = 0; k < parts[i].count && k < 4; ++k) {
            p[k] = {centre.x + (parts[i].xy[2 * k] - 60.0f) * scale, centre.y + (parts[i].xy[2 * k + 1] - 25.0f) * scale};
        }
        dl->AddConvexPolyFilled(p, parts[i].count, colour);
    }
    if (type == 3) {  // the sniper rifle's bipod
        dl->AddLine({centre.x + 30.0f * scale, centre.y - 2.0f * scale}, {centre.x + 24.0f * scale, centre.y + 15.0f * scale}, colour,
            2.0f * scale);
        dl->AddLine({centre.x + 32.0f * scale, centre.y - 2.0f * scale}, {centre.x + 38.0f * scale, centre.y + 15.0f * scale}, colour,
            2.0f * scale);
    }
}

char const* WeaponLabel(int type) {
    switch (type) {
    case 0: return "PISTOL";
    case 1: return "SHOTGUN";
    case 2: return "ASSAULT RIFLE";
    case 3: return "SNIPER RIFLE";
    default: return "UNARMED";
    }
}

// A stick: a ring with a letter, and arrows where it may be pushed.
void Stick(ImDrawList* dl, ImFont* font, ImVec2 c, char const* letter, bool horizontalOnly) {
    dl->AddCircle(c, 20.0f, kCyan, 32, 2.5f);
    Text(dl, font, 24.0f, {c.x, c.y - 13.0f}, kText, letter, 0.5f);
    auto const arrow = [&](float dx, float dy) {
        ImVec2 const tip{c.x + dx * 34.0f, c.y + dy * 34.0f};
        ImVec2 const base{c.x + dx * 27.0f, c.y + dy * 27.0f};
        ImVec2 const side{-dy * 6.0f, dx * 6.0f};
        dl->AddTriangleFilled(tip, {base.x + side.x, base.y + side.y}, {base.x - side.x, base.y - side.y}, kCyan);
    };
    arrow(-1.0f, 0.0f);
    arrow(1.0f, 0.0f);
    if (!horizontalOnly) {
        arrow(0.0f, -1.0f);
        arrow(0.0f, 1.0f);
    }
}

void Button(ImDrawList* dl, ImFont* font, ImVec2 c, char const* letter, ImU32 colour) {
    dl->AddCircleFilled(c, 17.0f, Rgba(8, 20, 34, 230), 32);
    dl->AddCircle(c, 17.0f, colour, 32, 2.5f);
    Text(dl, font, 22.0f, {c.x, c.y - 12.0f}, colour, letter, 0.5f);
}

void Card(ImDrawList* dl, ImFont* font, menu::MemberView const& m, float x0, float x1, bool selected) {
    float const y0 = 74.0f;
    float const y1 = 470.0f;
    if (selected) {
        for (int i = 3; i >= 1; --i) {  // glow
            float const g = 4.0f * static_cast<float>(i);
            Bevel(dl, {x0 - g, y0 - g}, {x1 + g, y1 + g}, 20.0f + g, 0, WithAlpha(kAmber, 40 / i), 3.0f);
        }
    }
    Bevel(dl, {x0, y0}, {x1, y1}, 20.0f, selected ? kCardSelected : kCard, selected ? kAmber : kCyanDim, selected ? 3.0f : 2.0f);
    // The class badge: a hexagon in the discipline's colour, with the initials.
    ImU32 const badge = DisciplineColour(m.discipline);
    ImVec2 const bc{x0 + 52.0f, 130.0f};
    dl->AddNgonFilled(bc, 36.0f, WithAlpha(badge, 90), 6);
    dl->AddNgon(bc, 36.0f, badge, 6, 3.0f);
    Text(dl, font, 30.0f, {bc.x, bc.y - 17.0f}, kText, m.initials, 0.5f);
    // Name and class.
    float const textX = x0 + 98.0f;
    float const textWidth = x1 - 16.0f - textX;
    FitText(dl, font, 32.0f, {textX, 98.0f}, kText, m.name[0] != '\0' ? m.name : "SQUADMATE", textWidth);
    dl->AddNgonFilled({textX + 6.0f, 151.0f}, 5.0f, badge, 6);
    FitText(dl, font, 18.0f, {textX + 16.0f, 140.0f}, kTextDim, m.role, textWidth - 16.0f);
    // Health: ten segments.
    Text(dl, font, 18.0f, {x0 + 18.0f, 190.0f}, kTextDim, "HEALTH");
    float const bx0 = x0 + 18.0f;
    float const bx1 = x1 - 18.0f;
    float const segment = (bx1 - bx0 + 4.0f) / 10.0f;
    float const health = m.health;
    int const lit = health < 0.0f ? -1 : static_cast<int>(std::ceil(std::clamp(health, 0.0f, 1.0f) * 10.0f - 0.01f));
    for (int i = 0; i < 10; ++i) {
        ImVec2 const a{bx0 + segment * static_cast<float>(i), 216.0f};
        ImVec2 const b{a.x + segment - 4.0f, 232.0f};
        ImU32 const fill = lit < 0 ? kHealthEmpty : i < lit ? (health < 0.3f ? kHealthLow : kHealth) : kHealthEmpty;
        dl->AddRectFilled(a, b, fill);
    }
    if (health < 0.0f) {
        Text(dl, font, 16.0f, {(bx0 + bx1) * 0.5f, 236.0f}, kTextDim, "unknown", 0.5f);
    }
    dl->AddLine({x0 + 18.0f, 262.0f}, {x1 - 18.0f, 262.0f}, kCyanFaint, 2.0f);
    // The weapon in hand.
    Text(dl, font, 16.0f, {(x0 + x1) * 0.5f, 272.0f}, kTextDim, "IN HAND", 0.5f);
    float const cx = (x0 + x1) * 0.5f;
    if (m.current >= 0) {
        Weapon(dl, m.current, {cx, 340.0f}, 1.6f, selected ? kAmber : kCyan);
    }
    FitText(dl, font, 24.0f, {cx, 404.0f}, m.current >= 0 ? kText : kTextDim, WeaponLabel(m.current), x1 - x0 - 32.0f, 0.5f);
}

void Tile(ImDrawList* dl, ImFont* font, int weapon, ImVec2 c, float w, float h, bool highlighted, bool inHand) {
    if (highlighted) {
        for (int i = 3; i >= 1; --i) {
            float const g = 5.0f * static_cast<float>(i);
            Hex(dl, c, w + 2.0f * g, h + 2.0f * g, 0, WithAlpha(kAmber, 50 / i), 3.0f);
        }
    }
    Hex(dl, c, w, h, highlighted ? kAmberFill : kTile, highlighted ? kAmber : kCyan, highlighted ? 3.5f : 2.5f);
    ImU32 const ink = highlighted ? kTextOnAmber : kText;
    if (inHand) {
        Text(dl, font, 14.0f, {c.x, c.y - h * 0.5f + 5.0f}, highlighted ? kTextOnAmber : kCyan, "IN HAND", 0.5f);
    }
    Weapon(dl, weapon, {c.x, c.y - 2.0f}, 0.95f, highlighted ? kTextOnAmber : kCyan);
    // The label sits where the hexagon narrows: fit it to the width there.
    float const k = h * 0.38f;
    float const labelWidth = w - 2.0f * k * (30.0f / (h * 0.5f)) - 12.0f;
    FitText(dl, font, 20.0f, {c.x, c.y + 20.0f}, ink, WeaponLabel(weapon), labelWidth, 0.5f);
}

// A weapon this squadmate doesn't carry: only a faint outline, so the four places stay recognisable.
void EmptySlot(ImDrawList* dl, ImVec2 c, float w, float h) {
    Hex(dl, c, w, h, Rgba(8, 20, 36, 90), kCyanFaint, 1.5f);
}

} // namespace

void Draw(ImDrawList* dl, ImFont* font, menu::View const& view) {
    float const W = static_cast<float>(kWidth);
    float const H = static_cast<float>(kHeight);
    Bevel(dl, {4.0f, 4.0f}, {W - 4.0f, H - 4.0f}, 28.0f, kBackdrop, kCyanFaint, 2.0f);
    // Title.
    Bevel(dl, {352.0f, 12.0f}, {672.0f, 56.0f}, 12.0f, kPanel, kCyanDim, 2.0f);
    Text(dl, font, 30.0f, {W * 0.5f, 18.0f}, kText, "SQUAD WEAPONS", 0.5f);
    Text(dl, font, 17.0f, {W * 0.5f, 62.0f}, kTextDim,
        view.confirmOnRelease ? "release the grip to confirm" : "A confirms, B cancels", 0.5f);

    int const count = std::clamp<int>(view.count, 0, menu::kMaxMembers);
    int const selected = std::clamp<int>(view.selected, 0, std::max(count - 1, 0));
    // Cards: the left squadmate left, the right one right (by where they stood when it opened).
    if (count >= 1) {
        Card(dl, font, view.members[0], 16.0f, 258.0f, selected == 0);
    }
    if (count >= 2) {
        Card(dl, font, view.members[1], 766.0f, 1008.0f, selected == 1);
    }

    // The radial: the selected squadmate's weapons, at fixed places.
    ImVec2 const centre{W * 0.5f, 296.0f};
    menu::MemberView const* const m = count > 0 ? &view.members[selected] : nullptr;
    struct Place {
        menu::Direction direction;
        ImVec2 at;
        float w;
    };
    // Up and down wide, left and right clear of the dial and the cards.
    Place const places[] = {
        {menu::Direction::Up, {centre.x, 168.0f}, 210.0f},
        {menu::Direction::Left, {centre.x - 158.0f, centre.y}, 176.0f},
        {menu::Direction::Right, {centre.x + 158.0f, centre.y}, 176.0f},
        {menu::Direction::Down, {centre.x, 424.0f}, 210.0f},
    };
    // The dial in the middle, pointing at the chosen tile.
    dl->AddCircleFilled(centre, 44.0f, Rgba(8, 22, 40, 230), 48);
    dl->AddCircle(centre, 44.0f, kCyanDim, 48, 3.0f);
    dl->AddCircle(centre, 30.0f, kCyanFaint, 48, 2.0f);
    dl->AddCircleFilled(centre, 6.0f, kCyan, 16);
    for (Place const& p : places) {
        float const dx = (p.at.x - centre.x) == 0.0f ? 0.0f : (p.at.x > centre.x ? 1.0f : -1.0f);
        float const dy = (p.at.y - centre.y) == 0.0f ? 0.0f : (p.at.y > centre.y ? 1.0f : -1.0f);
        dl->AddLine({centre.x + dx * 33.0f, centre.y + dy * 33.0f}, {centre.x + dx * 41.0f, centre.y + dy * 41.0f}, kCyanDim, 3.0f);
    }
    if (m != nullptr) {
        for (Place const& p : places) {
            int const weapon = menu::WeaponAt(p.direction);
            if (weapon < 0 || (m->carried & (1u << weapon)) == 0) {
                EmptySlot(dl, p.at, p.w, 104.0f);  // only what this squadmate carries
                continue;
            }
            bool const lit = view.highlighted == weapon;
            Tile(dl, font, weapon, p.at, p.w, 104.0f, lit, m->current == weapon);
            if (lit) {
                float const dx = p.at.x == centre.x ? 0.0f : (p.at.x > centre.x ? 1.0f : -1.0f);
                float const dy = p.at.y == centre.y ? 0.0f : (p.at.y > centre.y ? 1.0f : -1.0f);
                ImVec2 const tip{centre.x + dx * 40.0f, centre.y + dy * 40.0f};
                ImVec2 const base{centre.x + dx * 16.0f, centre.y + dy * 16.0f};
                ImVec2 const side{-dy * 12.0f, dx * 12.0f};
                dl->AddTriangleFilled(tip, {base.x + side.x, base.y + side.y}, {base.x - side.x, base.y - side.y}, kAmber);
            }
        }
    }

    // How to use it.
    if (count > 1) {  // one squadmate: nothing to choose
        Stick(dl, font, {66.0f, 528.0f}, "L", true);
        Text(dl, font, 20.0f, {112.0f, 506.0f}, kText, "SELECT SQUADMATE");
        Text(dl, font, 16.0f, {112.0f, 530.0f}, kTextDim, "(left stick)");
    }
    Stick(dl, font, {958.0f, 528.0f}, "R", false);
    Text(dl, font, 20.0f, {910.0f, 506.0f}, kText, "SELECT WEAPON", 1.0f);
    Text(dl, font, 16.0f, {910.0f, 530.0f}, kTextDim, "(right stick)", 1.0f);
    Bevel(dl, {330.0f, 540.0f}, {694.0f, 590.0f}, 12.0f, kPanel, kCyanDim, 2.0f);
    Button(dl, font, {372.0f, 565.0f}, "A", Rgba(96, 220, 120));
    Text(dl, font, 22.0f, {398.0f, 552.0f}, kText, "CONFIRM");
    dl->AddLine({W * 0.5f + 2.0f, 548.0f}, {W * 0.5f + 2.0f, 582.0f}, kCyanFaint, 2.0f);
    Button(dl, font, {550.0f, 565.0f}, "B", Rgba(255, 96, 84));
    Text(dl, font, 22.0f, {576.0f, 552.0f}, kText, "CANCEL");
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
    // A Windows font with a technical look (Bahnschrift, Windows 10+), else Segoe UI, else ImGui's own.
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, MAX_PATH);
    for (wchar_t const* name : {L"bahnschrift.ttf", L"segoeui.ttf"}) {
        std::filesystem::path const path = std::filesystem::path(windows) / L"Fonts" / name;
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            if (ImFont* font = io.Fonts->AddFontFromFileTTF(Utf8(path.wstring()).c_str(), 32.0f)) {
                chosen = Utf8(path.filename().wstring());
                return font;
            }
        }
    }
    chosen = "ImGui's default font";
    return io.Fonts->AddFontDefault();
}

} // namespace mevr::host::squad_panel
