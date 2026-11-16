// SPDX-License-Identifier: MIT
#pragma once
#include "halcyon/SequelPresentation.h"
#include "plugin/HudPanel.hpp"

// Shared LE2/LE3 stereo cinema presentation. Renderer hooks supply actual lens and eye captures.
namespace mevr::host::sequel_cinema {
using Frame = ME2VR::MevrBridge::CineScreenFrame;
struct State {
    bool active = false;
    void* space = nullptr;
    bool headLocked = false;
    float orientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float position[3] = {};
    float widthM = 0.0f;
    float heightM = 0.0f;
    float crop = 0.0f;
    float rows[2] = {0.0f, 1.0f};   // the shown band's rows, fractions of the eye image's height
};

float HalfSeparationUU(float halfH) noexcept;
unsigned Layers(Frame const& frame, void const** out, unsigned capacity, State& screen) noexcept;
void FillUi(State const& screen, float const headPosition[3], hud_panel::Frame& panel) noexcept;
}
