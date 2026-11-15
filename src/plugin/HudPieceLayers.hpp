// SPDX-License-Identifier: MIT
#pragma once

// The HUD pieces on the body (research/41, HostServices v22 setHudPieces), the host's half for both Halcyon hosts: the
// player's status cut out of the body-locked HUD panel onto a small quad at the back of the left wrist, and the weapon
// panel onto a quad over the held gun where the mod says it is (game/HudPieces). Moved unchanged out of LE1's
// plugin/HalcyonOverlays.cpp (its [Hud] settings reader, the mod's pieces, ShowPiece and the wrist and gun quads with their
// log lines); LE1's overlays decide when (with the wheels, labels and the Mako's cockpit), ME2's bridge with Frame below
// (research/le2-renderer-integration.md "Step 7: HUD pieces on ME2"). Present thread only.
//
// Each game's HUD has its own bands: ME1's measured on its near-square frame (vr::hud_pieces::DefaultWristCrop), ME2's on
// its 16:9 one (vr::hud_pieces::Layout::Me2); [Hud] wristCrop and gunCrop set either by hand.

#include "core/vr/HudPieces.hpp"
#include "plugin/HudPanel.hpp"
#include "plugin/QuadSwapchain.hpp"

#include <cstdint>

namespace mevr::host::hud_piece_layers {

namespace hp = vr::hud_pieces;

// [Hud] from our settings file (ASI\<GAME>MEVR.ini, the mod's file too): re-read within a second of a change.
hp::Settings const& Settings(std::uint64_t nowMs);
// The mod's latest pieces, or none (a struct of another size: host and mod from different builds, logged once).
hp::Pieces Wanted();
// The session changed: the pieces' swapchains went with the old one.
void ForgetSession();
// The runtime refused a piece's swapchain just now (plugin/Swapchains says when to try again).
bool WristRefused();
bool GunRefused();

// One piece's quad: its band `px` of the HUD panel blitted 1:1 into `swapchain` (with `options`), the quad `widthM` wide at
// `position` facing `head`, upright or leaning to `up`. False if anything failed (the quad isn't shown).
bool Show(XrSession session, ID3D11DeviceContext* context, ID3D11Device* device, QuadSwapchain& swapchain,
    XrCompositionLayerQuad& quad, char const* what, char const* name, hp::Pixels const& px, XrVector3f const& position,
    XrVector3f const& head, float widthM, XrVector3f const& up, XrSpace appSpace, float whiteScale,
    hud_panel::BlitOptions const& options);

// The wrist piece at `at` (hp::WristPoint of the left grip) and the gun piece at `anchor` this frame, each logged as LE1's;
// nullptr if not shown (the "back on the HUD panel" lines when one stops). `wanted` false: not this frame.
struct Piece {
    bool wanted = false;
    hp::Pixels px;
    XrVector3f position{};
    float widthM = 0.0f;
    XrVector3f up{};
    bool cropByHand = false;         // [Hud] wristCrop / gunCrop: for the log
    std::uint32_t serial = 0;        // the gun piece: the mod's weapon (logged again when it changes)
    bool leftTracked = true;         // the wrist piece: why it went back (the left controller isn't tracked)
};
struct Common {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    XrVector3f head{};
    float whiteScale = 1.0f;
    std::uint64_t nowMs = 0;
    hud_panel::BlitOptions options;  // the pieces' blit (colour seed 3; LE1: its overlays' options)
};
XrCompositionLayerQuad const* Wrist(Common const& common, Piece const& piece);
XrCompositionLayerQuad const* Gun(Common const& common, Piece const& piece);

// ME2's selected-object prompt, using InteractLabel's placement and this module's shared HUD blit.
// Call before drawing the panel. Adds its mask only after a successful copy. Requires an explicit [Interact] crop.
XrCompositionLayerQuad const* Interaction(Common const& common, bool wanted, hud_panel::Masks& masks);

// ---- ME2's frame (LE1's rules without the wheels, labels, world screens and the cockpit) --------------------------------
// Before the HUD panel is drawn: which pieces show (on foot, a head-tracked world frame with the HUD on the panel, the
// wrist with the left controller tracked, the gun where the mod says) and their bands as masks cut out of the panel.
struct Frame {
    bool gameplayOnFoot = false;     // the game's mode is on foot (0)
    bool worldFrame = false;         // a head-tracked world frame, not a menu
    bool headValid = false;
    bool panelReady = false;         // hud_panel::Ready() and a device
    std::uint32_t panelWidth = 0;    // hud_panel::Width(), Height()
    std::uint32_t panelHeight = 0;
    hp::Layout layout = hp::Layout::Me1;
};
struct Plan {
    Piece wrist;
    Piece gun;
};
Plan Decide(Frame const& frame, std::uint64_t nowMs, hud_panel::Masks& masks);
// After the panel was shown (`panelShown`): the planned pieces' quads appended to layersOut[count..capacity). Returns the
// new count.
unsigned Layers(Common const& common, Plan const& plan, bool panelShown, void const** layersOut, unsigned count,
    unsigned capacity);

} // namespace mevr::host::hud_piece_layers
