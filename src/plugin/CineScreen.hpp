// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/CineScreen.hpp"

#include <string>

// Conversations and cutscenes on a 3D cinema screen (research/52; the math is core/vr/CineScreen): the settings
// ([Cinematics] in ASI\LE1MEVR.ini, re-read each second), each frame's plan (the two cameras' separation and the crop,
// made in MevrBridge::PlanStereo and kept for the submit), the `cine screen:` log lines and the snapshot's "cineScreen".
// Halcyon's side is its [MEVR] cine-screen seams in xr_session.cpp (MevrBridge::CineScreenWanted, CineScreenQuadsNow,
// NoteCineScreenSubmit). Present thread.
namespace mevr::host::cine_screen {

// For PlanStereo (plugin/MetricStereo), a frame on the screen: `halfH` the render's horizontal half angle (radians, 0 =
// unknown), `ipdM` the headset's eye distance, `unitsPerMeter` the view's scale. Kept for CineScreenQuadsNow.
vr::cine_screen::Frame PlanFrame(float halfH, float ipdM, float unitsPerMeter);

// For snapshots: the settings, whether a scene is on the screen, its last plan and quads.
std::string StatusJson();

// [Cinematics] frontEnd: the start-up's front end (the title, its main menu) on the 3D screen (plugin/PresentationHold's
// MevrBridge::FrontEndOnScreen asks).
bool FrontEndWanted() noexcept;

// [Cinematics] backdrop and backdropBrightness, for plugin/CinemaBackdrop.
struct Backdrop {
    bool on = true;
    float brightness = 1.0f;
};
Backdrop BackdropWanted() noexcept;

// [Cinematics] uiLayer and uiForwardM, for plugin/HalcyonOverlays: the game's UI on a layer of its own in front of the screen
// instead of in the eye images, where the crop put it behind the scene it covers (research/52 "Cross-eyed").
struct UiLayer {
    bool on = true;
    float forwardM = 0.5f;
};
UiLayer UiLayerWanted() noexcept;

// [Cinematics] fadeS, for plugin/TransitionFade (0: no fades).
float FadeSeconds() noexcept;

// ---- Both renderers (LE1's MevrBridge cine-screen seams forward here; LE2's bridge calls them itself) -------------------
// Whether this conversation (or cutscene) goes on the 3D screen ([Cinematics] conversations / cutscenes; `world` counts as
// the screen: what the mod doesn't show in the world comes here).
bool Wanted(bool conversation) noexcept;
// [Cinematics] fullRate: the screen at the headset's rate instead of a renderer's cinematic cap.
bool FullRate() noexcept;
// At submit: the screen's size and distance, the last plan's crop and each eye's shown part of an image `eyeWidth` x
// `eyeHeight` (rect: x, y, width, height). `planned` false before any plan (nothing cropped then).
struct Quads {
    float distanceM = 3.5f;
    float widthM = 2.9f;
    float heightM = 0.0f;   // the shown part's aspect at widthM
    float crop = 0.0f;      // each eye's dropped share of its image's width (the left eye its left edge)
    int rect[2][4] = {};
    bool planned = false;
};
Quads QuadsNow(unsigned eyeWidth, unsigned eyeHeight) noexcept;
// Each frame the two quads are submitted: the log (`cine screen: a conversation on the 3D screen ...`) and the snapshot.
void NoteSubmit(bool conversation, float heightM) noexcept;

} // namespace mevr::host::cine_screen
