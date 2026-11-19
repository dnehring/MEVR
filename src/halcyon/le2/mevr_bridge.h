// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// MEVR (ME VR Attempt) addition, GPL-3.0: the seams only Halcyon's ME2 renderer has. The seams every renderer shares are
// MevrSeams:: (src/halcyon/MevrSeams.h: one vocabulary for LE1, LE2 and LE3); what is declared here is ME2's own, each
// behind a capability bit of plugin/Renderer.hpp. Declared without the OpenXR SDK headers, because xr_types.h defines the
// same names; implemented in src/plugin/HalcyonLe2Bridge.cpp and MenusLe2.cpp. Handles are passed as void*.

#include "../MevrSeams.h"

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

struct ID3D11DeviceContext;

namespace ME2VR::MevrBridge
{
// After a corrected view's ViewOrigin and PreViewTranslation were written (FloorViewTranslation returned non-zero): the
// crash breadcrumbs' journal. Game thread.
void JournalFloorViewWrite(void const* origin, void const* preTranslation) noexcept;

// Game thread, calcview_hook.cpp's SFR branch while the 3D cinema screen is up: half the two cameras' distance (game
// units) for a view whose horizontal half angle is `halfH` (radians, 0 unknown), our plan for this frame
// (core/vr/CineScreen: the scene [Cinematics] convergeM away on the screen plane once each eye is cropped by its depth).
// LE1 plans its passes through PlanStereo instead.
float CineScreenHalfSeparationUU(float halfH) noexcept;

// Present thread, d3d_capture.cpp's PresentHook: the swap chain this Present is for. The renderer copies the eyes from
// the FIRST swap chain it saw (CaptureSwapChainInfoOnce); a later one presenting is logged (once per swap chain). Also
// the host's per-Present work (the test loop, voice, our menu closing itself when nobody can see it).
void OnSwapChainPresent(void* swapChain, void* capturedSwapChain) noexcept;

// One of the game's screens stands on the wrist or in the room (research/le2-menus.md "ME2's screens on the wrist"): the
// common seam MevrSeams::WorldUiOwnsGui since ME3 took it (2026-11-16); ME2's name forwards to it (d3d_capture.cpp's
// GetMenuMode answers false for it, me2_menu.cpp's MenuOwnsLook keeps the stick the menu's).
inline bool WorldUiOwnsGui(int mode) noexcept
{
    return ::MevrSeams::WorldUiOwnsGui(mode);
}

// Game thread, calcview_hook.cpp's DrawDetour: whether a pass-1 Draw that returned in under 100 us is drawn again in the
// same frame ([NOOPRETRY]; [Performance] sfrReplayRetry, 1 upstream's behaviour, 0 our default since the owner's yes of
// 2026-11-17: plugin/RenderLeversTable). Such a Draw usually did render, and its retry is then the frame's third scene
// render (research/sequel-render-levers.md "Performance research").
// Defined by plugin/RenderLevers in the LE2 host.
bool SfrReplayRetry() noexcept;

// Halcyon's hotkey polls go through this: MevrSeams::KeyDown in our ASI, the plain poll in an upstream build.
inline bool HotkeyDown(int vk) noexcept
{
#if defined(MEVR_ASI)
    return MevrSeams::KeyDown(vk);
#else
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#endif
}
}
