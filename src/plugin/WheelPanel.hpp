// SPDX-License-Identifier: MIT
#pragma once

// The game's weapon and power wheels on a panel of their own at the hand (HostServices::setWheelPanel, v12;
// docs/squad-weapons-design.md), the host's half for both Halcyon hosts: while the game's wheel is up, its part of the UI
// (core/vr/WheelCrop: a default guess, then measured the first time a wheel opens) is cut out of the body-locked HUD panel
// and shown on a quad above a hand, facing the eyes. Moved unchanged out of LE1's plugin/HalcyonOverlays.cpp (its grid
// measurement, the mod's settings, the placement and the quad, its log lines word for word); LE1's overlays and the sequels'
// (plugin/SequelOverlays.cpp) decide when. Which wheel is up is the game's mode through engine/GameAdapter.hpp (ME1's power
// wheel 2 and weapon wheel 3, ME2's 1 and 2, ME3's 3 and 4). Present thread only.

#include "core/vr/SquadMenu.hpp"
#include "core/vr/WheelCrop.hpp"
#include "plugin/HudPanel.hpp"
#include "plugin/QuadSwapchain.hpp"

#include <openxr/openxr.h>

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace mevr::host::wheel_panel {

// The session changed: the wheel's swapchain went with the old one.
void ForgetSession();
// The player recentered: an open wheel's panel is placed again (any thread).
void Recentered();
// The wheel's swapchain and quad: LE1's conversation in the world shows its dialogue wheel on them (research/59).
QuadSwapchain& Swapchain();
XrCompositionLayerQuad& Quad();
// Where a wheel's panel goes when it opens: `place` 0 above the left hand, 1 the right, 2 in front of the head (or no hand),
// `distance` from it, facing the eyes; `from` names which, for the log.
XrPosef Place(int place, float distance, XrPosef const& head, char const*& from);

// ME1's numbers for the wheel up in `gameMode` (the game's own): 2 the power wheel, 3 the weapon wheel, -1 none.
int WheelOf(int gameMode);

struct Plan {
    vr::squad_menu::wheel::Panel settings;  // the mod's, or the defaults
    int mode = -1;                // 2 the power wheel, 3 the weapon wheel, -1 none
    int place = -1;               // where it goes (-1: it stays on the HUD panel)
    bool wanted = false;          // its part of the UI goes on its own quad this frame (cut out of the HUD panel)
    bool autoCrop = true;         // no [Squad] wheelCrop: measured
    vr::wheel_crop::Pixels px;    // its part of the HUD panel texture
    char const* cropFrom = "the default guess";
};
// Before the HUD panel is drawn: the grids read back, the mod's settings (logged once) and whether the wheel goes on its own
// quad. `allowed`: the HUD panel takes this frame's gameplay UI (engaged, gameplay, a world frame, a device). Add `px` to the
// panel's masks when `wanted`.
Plan Decide(ID3D11DeviceContext* context, int gameMode, bool allowed);

struct Common {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    XrPosef head{};               // the head this frame (the panel is placed facing it)
    float whiteScale = 1.0f;
    std::uint64_t nowMs = 0;
    hud_panel::BlitOptions options;  // the quad's blit (a quad of ours: colour seed 3)
};
// While the HUD panel texture holds this frame's UI (after it's drawn, before it's cleared; `panelShown`): a HUD-only picture of
// where the UI is now and then on foot (`onFoot`, the game's mode 0) until the wheel's place is measured, and the wheel's quad
// placed (when it opens, when the mod opens it again, on recenter) and blitted. nullptr: none this frame.
XrCompositionLayerQuad const* Layer(Common const& c, Plan const& plan, bool panelShown, bool onFoot);
// The layer went out: logged once per opening.
void Submitted();

} // namespace mevr::host::wheel_panel
