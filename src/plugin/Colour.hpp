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

// For snapshots (plugin/TestLoop): profile, values, headset, whether it corrects, the measured cost.
std::string StatusJson();

// Inside the VR hands tab (MevrMenuTab.cpp): the profile, live sliders, hold to compare.
void MenuSection();

} // namespace mevr::host::colour
