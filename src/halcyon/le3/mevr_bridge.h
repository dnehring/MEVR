// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// MEVR (ME VR Attempt) addition, GPL-3.0: the seams only Halcyon's ME3 renderer has. The seams every renderer shares are
// MevrSeams:: (src/halcyon/MevrSeams.h: one vocabulary for LE1, LE2 and LE3); what is declared here is ME3's own, each
// behind a capability bit of plugin/Renderer.hpp. Narrow ABI boundary: upstream xr_types.h stays separate from the
// official OpenXR headers. Implemented in src/plugin/HalcyonLe3Bridge.cpp.

#include "../MevrSeams.h"

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

namespace ME2VR::MevrBridge {
// Game thread, calcview_hook.cpp's SFR branch while the 3D cinema screen is up: half the two cameras' distance (game
// units) for a view whose horizontal half angle is `halfH` (radians, 0 unknown). As ME2's.
float CineScreenHalfSeparationUU(float halfH) noexcept;
// Present thread, every Present (also when XR frames stopped): the host's per-Present work (the test loop, voice, our
// menu closing itself when nobody can see it).
void OnPresent() noexcept;
}
