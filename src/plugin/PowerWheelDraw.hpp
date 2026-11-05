// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/PowerWheel.hpp"

#include "imgui.h"

#include <filesystem>
#include <string>
#include <vector>

// The power wheel's look (research/55), modelled on the user's concept of 2026-11-04 (concept/power-wheel.png, local):
// glass tiles in a ring around the palm, each with its power's icon over its name; the chosen one
// orange, grown and pulled out, a double chevron beyond it and an orange beam from the palm to it; blue spokes to the
// others ending in a node on each tile's straight inner edge; a blue band and an orange dashed ring between the tiles;
// on the palm an orange ring around our N7 mark (or nothing) with thin rings and tick marks around it. The tiles' outer
// corners are cut, a second outline runs inside each border, a faint dot grid fills them. A hint panel (an orange
// framed card with a hand and three lines) explains it the first time. ImGui draw lists only; the glow is
// plugin/Glow's, applied to the canvas afterwards. Drawn the same way by the host and by tools/power-wheel-preview.
namespace mevr::host::power_wheel_draw {

inline constexpr unsigned kCanvas = 1024;      // the wheel's canvas (square)
inline constexpr float kCentreX = 512.0f;      // the wheel's centre on it: a little low, as the chosen tile grows and its
inline constexpr float kCentreY = 540.0f;      // chevron sits above it
inline constexpr unsigned kHintWidth = 920;    // the hint card's canvas
inline constexpr unsigned kHintHeight = 210;

struct Fonts {
    ImFont* label = nullptr;  // the tiles' names (Rajdhani Bold)
    ImFont* title = nullptr;  // the hint's title (Titillium Web SemiBold)
    ImFont* body = nullptr;   // the hint's text (Titillium Web Regular)
};
// Looks for Rajdhani-Bold.ttf, TitilliumWeb-SemiBold.ttf and TitilliumWeb-Regular.ttf in `folders` (SIL OFL,
// third_party/fonts, deployed to ASI\fonts), then Windows' Bahnschrift and Segoe UI. `chosen` names what was loaded.
Fonts LoadFonts(ImGuiIO& io, std::vector<std::filesystem::path> const& folders, std::string& chosen);

enum class Discipline { Biotic, Tech, Combat };

struct Entry {
    std::string label;          // as shown (upper case is applied when drawn)
    ImTextureID icon = 0;       // power_icons::Picture as a texture; 0: none
    Discipline discipline = Discipline::Biotic;
    float cooldown = 0.0f;      // the part of its cooldown still to run, 0..1 (0: ready)
    bool enabled = true;        // false: the game won't cast it now (shown dim)
};

// What sits on the palm inside the orange ring: our N7 mark, or nothing (the user, 2026-11-04: the concept's glyph was generic).
enum class Emblem { N7, None };

struct WheelView {
    std::vector<Entry> entries;
    int selected = -1;
    float selectedFor = 1.0f;     // seconds since the selection changed (its tile grows into place)
    float open = 1.0f;            // 0..1: the wheel unfolding from the palm
    float time = 0.0f;            // seconds (pulses)
    Emblem emblem = Emblem::N7;
    bool upperCase = false;       // the concept's capitals; false: the game's title case
    vr::power_wheel::Vec2 hand;   // the palm's offset from the wheel's centre, canvas pixels, y up (the emblem sits there)
    vr::power_wheel::LayoutSettings layout;
    vr::power_wheel::TileSettings tile;
};
void DrawWheel(ImDrawList* dl, Fonts const& fonts, WheelView const& view);

struct HintView {
    std::string title = "Biotic Powers (VR)";
    std::vector<std::string> lines = {"Use your left hand to select a power.", "Move hand toward a power to target it,",
        "then pinch/close to equip."};
    float open = 1.0f;
};
void DrawHint(ImDrawList* dl, Fonts const& fonts, HintView const& view);

// The lock's marker (research/55 section 4), on a square canvas the host puts at the locked hostile, facing the eyes: four
// corner brackets closing in as it locks, a diamond at its chest, the equipped power's icon in a cut-corner box below
// and right, and a ring whose orange part grows as the power's cooldown runs out. Ready: orange (the game's chosen
// colour); cooling down or blocked: the blue of the other tiles. Where a place power would land (no hostile): a dashed
// ring with a cross. A cast flashes an expanding ring.
inline constexpr unsigned kLockCanvas = 256;
inline constexpr float kLockBox = 0.50f;  // the brackets' box, closed, as a part of the canvas (the quad's angular size is that box's)
struct LockDrawView {
    bool ready = true;
    bool onActor = true;
    float lockedFor = 1.0f;   // seconds since it locked
    float cooldown = 0.0f;    // still to run, 0..1
    float sinceCast = -1.0f;  // seconds since the last cast (negative: none to show)
    float time = 0.0f;
    ImTextureID icon = 0;
};
void DrawLock(ImDrawList* dl, LockDrawView const& view);

}  // namespace mevr::host::power_wheel_draw
