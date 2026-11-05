// SPDX-License-Identifier: MIT
#pragma once

#include <optional>
#include <string>

// The Mako's hazard lights (research/47 "Hazard lights", 2026-11-04): with the hull's health low, the cockpit's interior turns
// red and pulses like a rotating warning beacon: its lamps flash bright red and the rest of the cabin washes red in time with
// them. The rules, engine-agnostic and unit-tested; game/cockpit_probe reads the health and drives the materials.
namespace mevr::vr::cockpit_hazard {

struct Settings {
    bool on = true;         // [Cockpit] hazard
    float below = 0.30f;    // hazardBelow: the alarm starts with the hull's health under this fraction...
    float clearAbove = 0.40f;  // ...and stops once it is back over this one (repaired), so it never flickers at the edge
    float periodS = 1.1f;   // hazardPeriodS: one flash per period
    bool wash = true;       // hazardWash: the whole cabin goes red with the lamps (0: only the lamps)
    bool operator==(Settings const&) const = default;
};

// The alarm this tick: `was` last tick's, `health` the hull's fraction (0..1; nullopt: unknown, which keeps `was` only while
// it was on and the vehicle is still there: `vehicle`). Off whenever the settings are off.
bool Alarm(bool was, std::optional<float> health, bool vehicle, Settings const& settings);

// The beacon's sweep at `seconds` into the alarm: 1 at the flash, 0 between, a sharp peak (a cosine cubed) once per period;
// the first flash at once.
float Flash(double seconds, float periodS);

// How bright the cabin is at a flash value: the lamps' LightmapBrightness (the package's glow is 7) and the wash's (1 is the
// cabin's own brightness).
struct Levels {
    float lamp = 0.0f;
    float wash = 0.0f;
};
Levels LevelsAt(float flash);
inline constexpr float kLampLow = 0.8f;
inline constexpr float kLampHigh = 10.0f;
inline constexpr float kWashLow = 0.22f;
inline constexpr float kWashHigh = 1.05f;
// The red the cabin's light turns (linear): the lamps' and the wash's LightTexture.
inline constexpr float kRed[4] = {1.0f, 0.045f, 0.03f, 1.0f};

// Clamped and ordered (clearAbove at least below + 0.02, the period 0.2..5 s).
Settings Sanitized(Settings settings);
std::string Describe(Settings const& settings);

} // namespace mevr::vr::cockpit_hazard
