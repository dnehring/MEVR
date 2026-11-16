// SPDX-License-Identifier: MIT
#pragma once

// The sequels' bridge: our side of the common seams (src/halcyon/MevrSeams.h) for Halcyon's ME2 and ME3 renderers, written
// once (plugin/HalcyonSequelBridge.cpp, and the layer composition in plugin/SequelOverlays.cpp), and the small adapter each
// game implements for what it alone has (plugin/HalcyonLe2Bridge.cpp, plugin/HalcyonLe3Bridge.cpp).
// research/trilogy-seams.md "One sequel bridge" lists which seam is where and what each adapter function does per game.
//
// A fix or a feature on the host's side of a seam goes in the shared files. A game's file holds only: its adapter (below),
// its PlacePanel (native panel placement), the eye-image seams (lane EY's, until its one pipeline lands) and that
// renderer's own seams (ME2VR::MevrBridge in its tree's mevr_bridge.h).

#include "halcyon/MevrSeams.h"
#include "core/vr/GalaxyHologram.hpp"
#include "plugin/HudPanel.hpp"
#include "plugin/SequelCinema.hpp"

#include <Windows.h>
#include <Xinput.h>

#include <mutex>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace mevr::host::sequel_bridge {

// ---- What the shared bridge keeps, for the adapters ---------------------------------------------------------------------

// This frame's 3D cinema screen as CineScreenLayers placed it (Present thread): OverlayLayers puts the game's UI on a layer
// in front of it and clears it; a new frame and a stopped session clear it too.
sequel_cinema::State& Screen() noexcept;

// The pad's lock: MergeMotionControllers holds it for its whole merge (XInput is polled from any thread), and an adapter
// takes it for whatever of its own the pad hooks below read (ME2's galaxy trigger latch, ME3's native map lease).
std::mutex& PadMutex() noexcept;

// The right controller aims weapons, LE1's ControllerAimActive (plugin/HalcyonBridge.cpp): the mod says it aims
// (HostServices::setHandAim, renderer::ModHandAim) and the right controller's aim pose was tracked within the last second.
// The hand reticle's condition on both sequels. Present thread.
bool ControllerAimActive() noexcept;

// [Galaxy] nativeTable and the table's geometry in our settings file (ME1's table, both sequels' native map image on it),
// read at most once a second. Opt in until the presentation and the native input are checked in each game. Present thread.
struct GalaxyTable {
    bool enabled = false;
    vr::galaxy_holo::TableSettings settings;
};
GalaxyTable const& GalaxyTableSettings() noexcept;

// ---- The overlay composition (plugin/SequelOverlays.cpp): MevrSeams::OverlayLayers for both sequels ---------------------
// One function, ME1's order (plugin/HalcyonOverlays.cpp), driven by renderer::Capabilities(); what only a game knows is in
// its adapter's Overlay* functions and Facts below.
struct OverlayContext {
    MevrSeams::OverlayFrame const* frame = nullptr;
    ID3D11Device* device = nullptr;
    // The gameplay layers stand down: the renderer's own menu, as LE1's. Ours and the calibration don't change what the
    // layers are given; while one is drawn plugin/MenusLe2 submits it alone, as LE1's HalcyonOverlays (research/trilogy-seams.md
    // "One overlay composition": the owner's rule, every game the same).
    bool menuOpen = false;
    bool onFoot = false;       // this frame's game mode is on-foot gameplay
    int lastGameMode = -1;     // the last readable game mode
    bool worldUiOwns = false;  // a game screen of ours has the HUD panel (capability worldUi)
};

// ---- A game's adapter ---------------------------------------------------------------------------------------------------
// Implemented once per sequel host. The thread is the calling seam's (MevrSeams.h).
namespace game {

struct Facts {
    char const* log;          // the log lines' prefix ("halcyon le2 bridge")
    // OnViewBuilt publishes a view that is not the world's at once, instead of waiting for the floor lock to have been
    // quiet 100 ms. ME3: yes (its flat and menu paths stop calling the floor lock's pass without a last invalid view).
    bool invalidViewAtOnce;
    // OverlayLayers (what our menu, the ring and the traversal cue do is ME1's on both sequels: no fact for them,
    // research/trilogy-seams.md "One overlay composition"):
    // The game's UI on the HUD panel gets the eyes' colour correction (plugin/Colour), as LE1's. ME2: yes. ME3: not yet
    // (lane EY's one eye-image pipeline decides it).
    bool panelColour;
    // The HUD's own layout for the wrist and gun pieces (capability hudPieces): 0 ME1's, 1 ME2's (vr::hud_pieces::Layout).
    int hudLayout;
};
Facts const& Get() noexcept;

// A new session: the game's own per-session state (the native map's table, input leases). Before the shared resets.
void SessionCreated(void* instance, void* session) noexcept;
// The session stopped. Before the shared resets.
void SessionStopped() noexcept;
// A frame began, after the controllers were sampled and before the test loop may replace them. `focused`: the session has
// input focus; `tracked`: the player's own controllers are active.
void FrameBegun(void* session, void* space, bool focused, bool tracked) noexcept;
// After the frame's controllers were published.
void FramePublished() noexcept;
// The renderer recentred: the game's own state that was placed in the old app space (ME2's galaxy table; ME3's galaxy
// table, its spoken-reply lease and its traversal presentation). Before the shared part. Nothing published is dropped here:
// every game's recenter is the one rule of research/trilogy-seams.md "One recenter rule".
void Recentered() noexcept;

// MergeMotionControllers, all four with PadMutex() held.
// The pad is ours although no controller is tracked and neither our menu nor the calibration wants it (ME3: its native
// map's lease, a traversal presentation). Called first, on every poll of pad 0.
bool PadKeeps(int gameMode) noexcept;
// The triggers go to the game as LT and RT, not to the squad or the hands (ME2: its orbital scanner; ME3: both map modes).
bool PadOrbitalTriggers(int gameMode) noexcept;
// After the controllers were merged over the real pad and before our menus filter it.
void PadMerged(int gameMode, bool controllersActive, XINPUT_GAMEPAD& pad) noexcept;
// After our menus filtered it, before it is compared with the last one.
void PadFiltered(int gameMode, bool ownMenuOpen, XINPUT_GAMEPAD& pad) noexcept;

// HudPanelDraw: one UI draw is about to be redirected (ME2's draw-order trace for the log).
void UiDraw(ID3D11DeviceContext* context, bool afterLeftEye) noexcept;

// OverlayLayers (plugin/SequelOverlays.cpp), Present thread.
// Before anything is composed, also on a frame that can't be (`usable` false: no session, context or room): release what
// the last frame published (a native panel's pointer surface, a lease).
void OverlayBegin(MevrSeams::OverlayFrame const& frame, bool usable) noexcept;
// The game has something for the interaction label on this on-foot frame (ME2: no screen of ours on the panel; ME3: the
// mod's label is visible and has a measured crop).
bool OverlayLabel(OverlayContext const& context) noexcept;
// The hand reticle's context for plugin/SequelWeaponLayers: 0 on foot, 1 a verified turret aim ray, -1 disallowed.
int OverlayReticleMode(OverlayContext const& context) noexcept;
// The game's own part between the squad's layers and our menus' (ME2: its galaxy table as the pointer's surface; ME3: its
// galaxy window's layers). Returns the new count.
unsigned OverlayGameLayers(OverlayContext const& context, hud_panel::Frame const& panel, void const** layersOut,
    unsigned count, unsigned capacity) noexcept;
// The spoken-reply panel (Thespian) stays hidden for a reason only the game knows (ME3: the calibration, no input focus).
bool OverlaySpokenBlocked(OverlayContext const& context) noexcept;
// The spoken-reply panel's context was decided: `eligible` false revokes whatever lease the game keeps on it.
void OverlaySpokenContext(bool eligible) noexcept;
// Everything is composed, our menus included: publish what depends on the whole frame (ME3's native map lease).
void OverlayEnd(OverlayContext const& context) noexcept;

} // namespace game

} // namespace mevr::host::sequel_bridge
