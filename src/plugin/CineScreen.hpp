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

} // namespace mevr::host::cine_screen
