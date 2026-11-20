// SPDX-License-Identifier: MIT
#pragma once

// A conversation shown in the world, first person: its subtitle under the speaker's face and the game's own dialogue wheel in
// front of the player (or at a hand), cut out of the HUD panel onto quads of their own (ME1's research/59 placement, LE1's
// HalcyonOverlays: the subtitle at the mod's `subtitleAt` the interaction label's way, the same angular height at any distance;
// the wheel placed where it opens, the weapon wheel's way). Game-independent; the sequels' overlays use it (lane FPC5,
// research/le2-conversations-in-world.md section 10); LE1's HalcyonOverlays has its own copy of the same steps until an ME1
// batch moves it here. The conversation movie's bands are ME1's (vr::conversation_world::DefaultSubtitleCrop/DefaultWheelCrop:
// its 1280x720 stage across the frame's width, centred top to bottom), measured the same on ME2's and ME3's frames.
// Present thread only.

#include "core/vr/ConversationWorld.hpp"
#include "plugin/HudPanel.hpp"

#include <openxr/openxr.h>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace mevr::host::conversation_ui {

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    XrPosef head{};
    float whiteScale = 1.0f;
    hud_panel::BlitOptions options;
    bool allowed = false;   // a held conversation's world frame, the panel ready, no menu over it
    bool wheelAllowed = true;  // not while Thespian mode's panel stands with the game's wheel hidden (research/70)
};

// Before the HUD panel is drawn (this frame's UI is in its texture): the subtitle's and the wheel's bands blitted onto their quads
// and cut out of the panel (`masks`); the quads written to out[0..), at most 2. Returns how many.
unsigned Layers(Frame const& frame, vr::conversation_world::State const& convo, hud_panel::Masks& masks,
    XrCompositionLayerQuad const** out);
// The session changed: the subtitle's swapchain went with the old one (the wheel's is plugin/WheelPanel's).
void ForgetSession();

} // namespace mevr::host::conversation_ui
