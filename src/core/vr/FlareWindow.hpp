// SPDX-License-Identifier: MIT
#pragma once

// Lens flares in life-size stereo's mode 2 (research/30 section 9.6, lane FL, 2026-11-17).
//
// UE3's lens flare draws its reflection elements (the ghosts and glows along the line through the screen's centre) from
// the CPU: FLensFlareDynamicData's reflection pass (ME3 0x26B780) projects the source with the view's ViewProjectionMatrix
// (FSceneView +0x260, FSceneView::WorldToScreen 0x34EF00), places each element on the screen along the line through the
// centre, and turns that screen point back into a world point through InvTranslatedViewProjectionMatrix (+0x210) plus
// ViewOrigin (+0x320), at clip depth 0.1. The GPU then draws the element at that world point through the per-pass
// TranslatedViewProjection (+0x1D0).
//
// In mode 2 the GPU's matrix (+0x1D0) is the eye's own off-centre frustum and Halcyon's [INVMAT] refreshes +0x210 from it,
// while +0x260 and the projection (+0xD0) keep the CPU's symmetric window. The round trip then lands every element at the
// symmetric window's screen coordinates in the eye's off-centre image: the same pixel of both eyes, which look different
// ways (lane FXR's lamp: at x 1233 of 3072, its flare at 920, the symmetric window's prediction 934).
//
// The fix: for that pass only, +0x210 holds the inverse of the CPU's own window, Inv(TV x P) (TV the translated view
// +0x190, P the CPU's projection +0xD0), so the element's world point is the one the CPU's projection described: the
// source element on the ray to its lamp in each eye, the ghosts along the same directions in both eyes (mode 1's flares,
// in mode 2). Matrices are UE3's: row-major, row vectors (v x M).
#include "core/vr/ScreenMatrix.hpp"

#include <optional>

namespace mevr::vr::flare_window {

using Mat4 = screen_matrix::Mat4;

// Inv(translatedView x projection): the inverse the CPU's own projection (+0x260 = T(PreViewTranslation) x TV x P) pairs
// with in translated space. Empty when the product isn't invertible or isn't finite.
std::optional<Mat4> WindowInverse(Mat4 const& translatedView, Mat4 const& projection);

// Whether two matrices differ beyond float noise (each element against 1e-4 of the larger magnitude, at least 1e-4):
// false in mode 1, where the refreshed inverse already is the window's.
bool Differs(Mat4 const& a, Mat4 const& b);

// The engine's placement of one reflection element, for the tests and the research note: the screen point (x, y in the
// CPU window's normalised device coordinates) at clip depth z, unprojected through `inverse` (translated space, w
// divided). Empty when w is 0.
std::optional<std::array<float, 3>> Unproject(Mat4 const& inverse, float x, float y, float z);

} // namespace mevr::vr::flare_window
