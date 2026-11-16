// SPDX-License-Identifier: MIT
#pragma once

// World-locked flat panels (plugin/WorldPanels.cpp), both Halcyon hosts: the flat screen and the 3D cinema screen stay put in
// the app space where they appeared. LE1's MevrBridge::PlacePanel and the two calls below forward to world_panels; LE2's bridge
// places the 3D cinema screen with it (research/le2-cinema.md). Present thread.
namespace mevr::host::world_panels {

// What the panel shows (LE1's MevrBridge::PanelKind, the same values): the Insert menu has a slot of its own, the rest share
// one (the flat screen), and conversations and cutscenes are one showing (core/vr/OverlayPlacement's ScreenPanelSettings).
enum class Kind {
    HalcyonMenu = 0,   // Halcyon's own menu (LE1's PanelKind::InsertMenu; not named so: a Windows.h macro)
    GameMenu = 1,
    GalaxyMap = 2,
    Conversation = 3,
    Cutscene = 4,
};

struct Pose {
    void* space = nullptr;                             // XrSpace to submit the quad in
    float orientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // xyzw, in `space`
    float position[3] = {0.0f, 0.0f, 0.0f};            // metres, in `space`
    bool headLocked = true;                            // `space` is VIEW (the setting off, or no app space yet)
};

// Each frame a panel quad is submitted. `viewPosition`: where it would stand head-locked (VIEW space, oriented like VIEW).
// `head`: the eye centre in the app space this frame (orientation xyzw, position; ignored while !headValid). Returns VIEW and
// `viewPosition` unchanged with the setting off (renderer::Panels().worldLockedPanels) or no app space yet, else the app space
// and the pose fixed in it.
Pose Place(Kind kind, float const viewPosition[3], void* viewSpace, void* appSpace, bool headValid,
    float const headOrientation[4], float const headPosition[3]) noexcept;

// Each frame, right after xrBeginFrame: counts frames, so a panel can tell whether it was shown the frame before.
void NoteFrame() noexcept;

// Recentering re-origined the app space: every world-locked panel is placed afresh.
void Recenter() noexcept;

} // namespace mevr::host::world_panels

// Between WorldPanels.cpp (MevrBridge::PlacePanel) and the rest of LE1's host bridge. Present thread.
namespace MELEVR::MevrBridge {

// Each frame, right after xrBeginFrame (OnFrameBegun): world_panels::NoteFrame.
void NoteWorldPanelFrame() noexcept;

// Recentering re-origined the app space (OnRecentered): world_panels::Recenter.
void RecenterWorldPanels() noexcept;

} // namespace MELEVR::MevrBridge
