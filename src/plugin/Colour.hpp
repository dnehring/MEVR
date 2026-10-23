// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Colour.hpp"

#include <cstdint>
#include <string>

struct ID3D11DeviceContext;

// Colour correction of the world eye images and the game's UI on the HUD panel (research/27; the math in
// core/vr/Colour). Our own quads (reticle, sights, squad menu, calibration) and Halcyon's menu stay as they are.
//
// The eyes: Halcyon's eye blit (src/halcyon/le1/xr_session.cpp) asks through the [MEVR] colour seams
// (mevr_bridge.h, EyeColour*) for a variant of its own pixel shaders with the correction at the end, compiled
// here from its source with core/vr/Colour's prelude in front. While the correction is on, an eye whose source
// would be copied raw (same size, 8-bit) goes through that blit instead. Neutral leaves everything as before:
// Halcyon's own shaders, its raw copy.
// The HUD panel (and the game's wheel on its own panel): HalcyonOverlays' blit uses its shader's variant.
//
// Settings: [Colour] and [Colour.<headset>] in <GAME>MEVR.ini (core/vr/Colour Resolve), re-read within a second
// of a change; the VR hands tab's Colour section edits the headset's own section. Present thread unless noted.
namespace mevr::host::colour {

// The headset (OnSessionCreated): xrGetInstanceProperties' runtime name, xrGetSystemProperties' system name and
// vendor id. Logged (ours and Halcyon's log), and picks the [Colour.<key>] section and what Auto means.
void SetHeadset(std::string const& runtimeName, std::string const& systemName, std::uint32_t vendorId);

// The correction is on now: a non-neutral profile, no comparison held.
bool Correcting();

// Show the uncorrected picture: while HoldCompare() keeps being called (each frame; it lapses 150 ms after the
// last call), or while SetCompare(true) holds (until SetCompare(false)). Any thread. [Colour] compare = 1 in the
// file does the same. For the coming menu's "hold A".
void HoldCompare();
void SetCompare(bool on);

// The HUD panel's blit (HalcyonOverlays): right before its draw with the corrected variant, bind the constants
// at b1 (saving what was there); false: no correction (draw the plain shader, don't call EndPanel). `seed`:
// the dither pattern (each panel its own).
bool BeginPanel(ID3D11DeviceContext* context, std::uint32_t seed);
void EndPanel(ID3D11DeviceContext* context);

// The eye upscale's last pass (plugin/Upscale), which stands in for Halcyon's eye blit: true when the correction is
// on and its constants are bound at b1 for `eye` (the pass then draws its corrected variant), false for the plain
// one; EndEye after the draw either way. Feeds the eye blit's watchdog as EyeColourShader does.
bool BeginEye(ID3D11DeviceContext* context, int eye, bool linearSource);
void EndEye(ID3D11DeviceContext* context);

// For snapshots (plugin/TestLoop): profile, values, headset, whether it corrects, the measured cost.
std::string StatusJson();

// Inside the VR hands tab (MevrMenuTab.cpp): the profile, live sliders, hold to compare.
void MenuSection();

// For our main menu's Display page (plugin/MainMenuSettings).
struct MenuState {
    vr::colour::Profile chosen = vr::colour::Profile::Auto;
    vr::colour::Profile effective = vr::colour::Profile::Neutral;
    vr::colour::Settings settings;   // what applies
    vr::colour::Settings custom;     // the headset's Custom values
    vr::colour::Detection detection;
    std::string runtime;             // xrGetInstanceProperties' runtime name ("" before VR starts)
    std::string system;              // xrGetSystemProperties' system name
    bool compareInFile = false;      // [Colour] compare = 1
};
MenuState GetMenuState();
// Writes [Colour.<headset>] profile = p (as the VR hands tab's radio buttons).
void SelectProfile(vr::colour::Profile p);
// The headset's Custom values: applied at once; with `save`, written to the file too (profile = custom).
void SetCustom(vr::colour::Settings const& settings, bool save);

} // namespace mevr::host::colour
