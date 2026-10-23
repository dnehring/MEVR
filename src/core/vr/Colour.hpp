#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

// Colour correction for what the headset shows of the game: the world eye images and the game's UI on the
// HUD panel (research/27). Game-independent: the settings, the presets, which headset gets which, and a CPU
// reference of the shader (ShaderPrelude), so the math is tested offline (tests/Main.cpp) and the shader
// checked against it (tools/colour-preview).
//
// Every operation works on sRGB-ENCODED values in [0, 1]: the 8-bit numbers the game writes to its UNORM
// backbuffer, the eye swapchain holds, and a streaming encoder compresses. They are roughly perceptual, the
// space Virtual Desktop's gamma slider and OpenXR Toolkit (on a UNORM swapchain) work in, and the space the
// dither has to be in (the 8-bit quantisation happens there). A linear source (the float HDR backbuffer, an
// _SRGB view) is encoded first and decoded after, so the same settings look the same on both paths.
//
// In order:
//   1. black level   x = (x + black) / (1 + black)      white stays white; < 0 deepens (inputs at or below
//                                                       -black become black), > 0 lifts the blacks
//   2. brightness    x = x * brightness * gain[c]       a gain per channel (white balance) times one for all;
//                                                       above 1 the brightest steps clip
//   3. mid-tones     x = x ^ (1 / gamma)                gamma < 1 darkens the mid-tones (Virtual Desktop's
//                                                       direction: its 0.7 is a "deeper" picture)
//   4. contrast      x = x + t * (smoothstep(x) - x)    an S-curve through black, mid-grey (0.5) and white;
//                    t = 2 * (contrast - 1)             contrast = the slope at mid-grey (1.1: 10% steeper)
//                                                       (OpenXR Toolkit's idea, on luma there; NOTICE.md)
//   5. saturation    x = Y + (x - Y) * saturation       around Rec.709 luma Y' of the encoded values
// then clamp to [0, 1], and add triangular dither (+-`dither` 8-bit steps) before the 8-bit store.
// Neutral (every value at its identity) is detected and skips the correction entirely.
namespace mevr::vr::colour {

struct Settings {
    float brightness = 1.0f;  // 0.5 .. 1.5
    float contrast = 1.0f;    // 0.5 .. 1.5
    float saturation = 1.0f;  // 0 .. 2
    float gamma = 1.0f;       // mid-tones, 0.5 .. 2
    float black = 0.0f;       // black level, -0.2 .. 0.2
    std::array<float, 3> gain{1.0f, 1.0f, 1.0f};  // red, green, blue, 0.5 .. 1.5
};

// Within half a slider step of the identity everywhere: no correction at all (the raw path).
bool IsNeutral(Settings const& s);
// Into the ranges above (non-finite values become the identity).
Settings Clamped(Settings s);

enum class Profile {
    Auto,     // the headset's own: Quest for streamed Quest headsets, Neutral for the rest
    Neutral,  // no correction
    Quest,    // "Quest streamed": Quest 3, 3S, Pro, 2 over Link, Air Link, Steam Link or Virtual Desktop
    Vivid,    // stronger than Quest, for any headset
    Custom,   // the player's own values (per headset)
};
char const* Name(Profile p);   // "auto", "neutral", "quest", "vivid", "custom"
char const* Label(Profile p);  // for the menu
std::optional<Profile> ParseProfile(std::string_view text);  // case-insensitive; "quest streamed" too

// The built-in values of Neutral, Quest and Vivid (Auto and Custom: Neutral's).
Settings Preset(Profile p);

// ---- Which headset ------------------------------------------------------------------------------------
enum class Display {
    Unknown,        // nothing known about it: neutral
    QuestStreamed,  // a Quest (LCD) fed through a video stream (Link, Air Link, Steam Link, Virtual Desktop, ALVR)
    Oled,           // PS VR2 and other OLED panels: neutral
    Wired,          // a known headset that isn't streamed (Rift, Index, Vive...): neutral
};
struct Detection {
    Display display = Display::Unknown;
    Profile automatic = Profile::Neutral;  // what Auto means for it
    std::string why;                       // for the log and the menu
};
// From xrGetInstanceProperties' runtimeName and xrGetSystemProperties' systemName. SteamVR names the system
// "SteamVR/OpenXR : <tracking system>": playstation_vr2, oculus (a Meta headset through Link, Virtual Desktop's
// SteamVR driver or ALVR), vrlink (Steam Link), lighthouse...; Meta's runtime "Meta Quest 3", "Oculus Quest2";
// Virtual Desktop's (runtime VirtualDesktopXR) the headset's product name.
Detection Detect(std::string_view runtimeName, std::string_view systemName);

// The headset's own settings section, [Colour.<key>]: the system name made safe (as the calibration's key
// spells it), with the runtime in front when the system name doesn't already say it (Meta's runtime and
// Virtual Desktop's report the same Quest name). Empty when the system isn't known.
std::string HeadsetKey(std::string_view runtimeName, std::string_view systemName);

// ---- Settings file ----------------------------------------------------------------------------------------
// [Colour] (any headset) and [Colour.<key>] (this one): keys profile, brightness, contrast, saturation, gamma,
// black, gainR, gainG, gainB; and in [Colour] only: compare (1: show the uncorrected picture), dither (8-bit
// steps, 0: off). The headset's own section wins key by key. Keys are lower-case here.
using Section = std::map<std::string, std::string>;
struct Resolved {
    Profile chosen = Profile::Auto;      // what the file says
    Profile effective = Profile::Neutral; // Auto resolved, never Auto
    Settings settings;                   // what is applied (Clamped; Neutral's for Neutral)
    Settings custom;                     // the Custom values (also when another profile is chosen)
    bool compare = false;                // [Colour] compare
    float dither = 0.5f;                 // 8-bit steps, 0 .. 2
};
Resolved Resolve(Section const& common, Section const& headset, Detection const& detection);
// Lower-cased keys, trimmed values, from "key=value" lines (GetPrivateProfileSection's output, ';' comments).
Section ParseSection(std::string_view lines);

// ---- The shader and its CPU reference -----------------------------------------------------------------------
// cbuffer MevrColour : register(b1), as the shader reads it.
struct Constants {
    float tone[4];       // black, 1 / gamma, contrast weight t, saturation
    float gain[4];       // gain * brightness per channel, 0
    float misc[4];       // dither amplitude (in [0, 1] units), linear source (1/0), 0, 0
    std::uint32_t seed[4];  // dither seed (per eye), 0, 0, 0
};
static_assert(sizeof(Constants) == 64);
Constants MakeConstants(Settings const& s, float ditherSteps, bool linearSource, std::uint32_t seed);

// HLSL to put in front of a shader: the cbuffer at b1, MevrColourIn(colour, pixel, linear), and the hooks
// MEVR_EYE_OUT(c, pos) (Halcyon's eye blit: c from ToEye, linear per the cbuffer) and MEVR_PANEL_OUT(c, pos)
// (the HUD panel: unpremultiplied linear rgb). A shader without the prelude defines both as the identity.
char const* ShaderPrelude();

using Rgb = std::array<float, 3>;
float Encode(float linear);   // sRGB, exact piecewise
float Decode(float encoded);
// Steps 1-5 on encoded values, from the constants (MevrGrade).
Rgb Grade(Constants const& k, Rgb x);
// The dither's noise at a pixel: triangular in (-1, 1), mean 0 (MevrDither before the amplitude).
float Dither(std::uint32_t x, std::uint32_t y, std::uint32_t seed);
// The whole shader for one pixel (MevrColourIn): encode if linear, grade, dither, decode if linear.
Rgb Correct(Constants const& k, Rgb c, std::uint32_t x, std::uint32_t y, bool linear);
// The 8-bit store (round to nearest).
std::uint8_t ToUnorm8(float v);

} // namespace mevr::vr::colour
