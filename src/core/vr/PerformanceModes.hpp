// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The Display page's Performance row (research/60): three modes, each a bundle of the real levers that decide LE1's frame
// time in VR, measured at Eden Prime's heaviest cells on the fake headset. Balanced is the shipped defaults (the owner's
// tuned settings); Performance goes lighter, Quality heavier. A setting changed by hand that no mode has shows "Custom".
// Game-independent: the menu (plugin/MainMenuSettings) reads the files and writes what a mode sets; unit-tested in
// tests/PerformanceModesTests.cpp.
namespace mevr::vr::perf_modes {

// What a mode sets. Every one is a key in ASI\LE1MEVR.ini read live within a second (plugin/RenderLevers' [Performance],
// plugin/Upscale's [Upscale], game/SystemSettings' [SystemSettings] through `scale set`), except the render resolution:
// Halcyon's target (MELEVR.ini sfr2ResX/Y, GamerSettings.ini ResX/ResY), which a restart applies.
struct Levers {
    std::string resolution = "headset85";  // a Render resolution chip: "headset", "headset85", "headset75"
    bool fsr = true;                        // [Upscale] method = fsr (acts only while the target is below the eye)
    int minScreenRadiusPx = 8;             // [Performance] minScreenRadiusPx (research/35 attempt 45)
    float foliage = 0.5f;                   // [SystemSettings] FoliageDrawRadiusMultiplier (research/35 attempt 24)
    bool dynamicShadows = false;            // [SystemSettings] DynamicShadows
    bool ambientOcclusion = true;           // [SystemSettings] AmbientOcclusion
};

struct Mode {
    char const* key;          // "quality", "balanced", "performance"
    char const* name;         // the chip
    char const* description;  // what it trades, one line
    Levers levers;
};

// Quality, Balanced, Performance, in that order.
std::vector<Mode> const& Modes();
Mode const* Find(std::string_view key);
// The shipped defaults' mode.
inline constexpr char const* kDefault = "balanced";

// The levers as the files hold them now. `get(section, key)` is the file's value with any pending menu edit on top
// (nullopt: no such key); `resolution` is the Render resolution chip the current target shows (core/vr/MenuSettings
// ResolutionChoice: "" for a size no chip makes). A missing key is what its module does without one: minScreenRadiusPx
// 8, FSR on, the grass at 0.5, dynamic shadows off (game/SystemSettings' built-in values), ambient occlusion the game's
// own (on). The FSR sharpness is no mode's: a matter of taste, not of frame time.
using Getter = std::function<std::optional<std::string>(std::string_view section, std::string_view key)>;
Levers Current(Getter const& get, std::string resolution);

// The mode these levers are exactly (floats to the menu's resolution), or "custom".
std::string ModeFor(Levers const& levers);
bool Same(Levers const& a, Levers const& b);

// What differs from `mode`, as the Custom line reads it: "render size Headset × 0.75, grass at 75%" ("" when nothing does).
std::string Differences(Levers const& levers, Levers const& mode);
// The mode with the fewest levers different from these (a tie: Balanced, the standard, else the earlier one).
Mode const& Nearest(Levers const& levers);
// The Performance row's line: the mode's description, or for Custom "Your own mix: Balanced with grass at 75%".
std::string Description(Levers const& levers);

// The settings-file edits a mode makes, (section, key, value), in a fixed order. The resolution is not among them: the
// caller sets the target as the Render resolution row does.
struct Edit {
    std::string section;
    std::string key;
    std::string value;
};
std::vector<Edit> Edits(Levers const& levers);

// The row's note: which part waits for a restart. `pendingResolution`: the target the files hold differs from the size
// the game renders now.
std::string RestartNote(bool pendingResolution);

// The Advanced > Performance rows' values: the cull's chip ("0", "4", "8", "16", "24", "32": the nearest) and the grass
// distance as a percentage of the game's own (0.5 -> 50).
std::string CullChoice(int px);
float FoliagePercent(float multiplier);
float FoliageFromPercent(float percent);

} // namespace mevr::vr::perf_modes
