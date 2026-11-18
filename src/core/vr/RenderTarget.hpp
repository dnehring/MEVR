// SPDX-License-Identifier: MIT
#pragma once

// The sequels' render target (ME2, ME3: the renderer's [VR] RenderW/RenderH, the backbuffer both eyes render at), from the
// headset as ME1's (the owner, 2026-11-18: "Follows headset like me1"; docs/trilogy-plan.md "Answered 2026-11-18").
//
// In mode 2 every game renders each eye's own field into the whole target (ME1's sfr2ResX/Y, the sequels' backbuffer: the
// projection layer's image rect is the whole image at that eye's FOV), so 0.85 of the runtime's recommended eye size covers
// what ME1's covers; only the engine's constraints on the target differ (`Shape`). ME1's own rule is core/vr/MenuSettings'
// HeadsetSize / FirstStartSize (even sides, the exact recommendation for "headset"); this file is compiled only where the
// sequels' hosts, the tests and the menu preview need it, so ME1's binaries are unchanged by it.

#include "core/vr/MenuSettings.hpp"

#include <string>
#include <string_view>

namespace mevr::vr::render_target {

using Size = menu_settings::Size;

struct Shape {
    int multiple = 8;      // each side a multiple of this (ME3's scene targets are allocated in multiples of 8, and a
                           // backbuffer of any other width never matched them: research/le3-flares-and-window.md section 3)
    float minAspect = 0.0f;  // > 0: a narrower frame loses part of the game (ME2 draws no HUD at all on a near-square
                             // backbuffer: 30 UI draws in 4 s at 1624x1656 against 13,710 at 2208x1656 and 14,910 at
                             // 2944x1656, lane AU4 2026-11-18), so a narrower target is widened to this width:height at the
                             // chip's height (no axis below what the chip asks)
};

// The nearest multiple of `multiple` (at least one multiple).
int RoundToMultiple(float value, int multiple);
// A headset chip ("headset", "headset85", "headset75": menu_settings::HeadsetChoices) at that shape; {0, 0} for an unknown
// key or no recommendation yet.
Size HeadsetSize(Shape shape, std::string_view key, int recommendedWidth, int recommendedHeight);
// The chip a target shows as selected (a headset chip only: the sequels have no ladder), else "".
std::string ResolutionChoice(Shape shape, int width, int height, int recommendedWidth, int recommendedHeight);
// The headset row's line, as menu_settings::HeadsetSizesText, with this shape's sizes.
std::string HeadsetSizesText(Shape shape, int recommendedWidth, int recommendedHeight);
// The first VR start: 0.85 of the eye at this shape (FSR, on by default, scales it up to the eye), once (`alreadySet`: done
// before); {0, 0} when there is nothing to do (done, no recommendation yet, or the target is that size already). No setup's
// stand-in to wait for: the sequels' renderer ships a fixed size (ME2 4096x2304, ME3 3072x2216) that this replaces from the
// next launch.
Size FirstStartSize(Shape shape, bool alreadySet, Size current, int recommendedWidth, int recommendedHeight);

} // namespace mevr::vr::render_target
