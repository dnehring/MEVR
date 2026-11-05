// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// The power wheel's icons (research/55; the user's concept of 2026-11-04), painted on the CPU from signed distance
// fields: glowing silhouettes, fields and particles in each discipline's colours (biotics violet, tech cyan, combat
// amber), made once when the wheel is first shown. Ours, not the game's art.
namespace mevr::host::power_icons {

inline constexpr unsigned kPixels = 256;  // square

enum class Icon : int {
    Generic,
    // Biotics
    Throw,
    Lift,
    Warp,
    Singularity,
    Stasis,
    Barrier,
    // Tech
    Overload,
    Damping,
    Sabotage,
    Hacking,
    NeuralShock,
    ShieldBoost,
    // Combat and weapon powers
    Adrenaline,
    Immunity,
    Unity,
    Marksman,
    Overkill,
    Carnage,
    Assassination,
    Count,
};
char const* Name(Icon icon);
// The icon for a power by its 2DA label (TK_Throw, SD_Singularity, EL_EMP_Delivery...); Generic if unknown.
Icon ForPower(std::string_view label);

// Straight-alpha sRGB RGBA8 (0xAABBGGRR), kPixels square: what ImGui's textures take.
std::vector<std::uint32_t> Picture(Icon icon);

}  // namespace mevr::host::power_icons
