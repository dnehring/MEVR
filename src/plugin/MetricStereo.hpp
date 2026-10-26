// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// Life-size (metric) stereo for Halcyon's Stereo 2 (research/30): the plan for each frame's two render passes
// (MevrBridge::PlanStereo) and what each frame submitted (MevrBridge::NoteStereoSubmit), with the
// [MEVR-STEREO] log lines. The math is core/vr/MetricStereo; the render-side edits are Halcyon's render_hook.cpp.
// Present thread.
namespace mevr::host::metric_stereo {

// For snapshots: the mode, the eye distance (metres, game units), the units per metre, each pass's offset and
// frustum, and what the last frame declared per eye.
std::string StatusJson();

// research/37 (the shown columns): the region of the symmetric window that render pass `pass`'s eye shows, as
// fractions of the viewport (left, top, right, bottom; with a 0.5% margin outward), from the last plan. False when
// nothing is cut (mode 2: each eye renders its own frustum; a symmetric headset; no plan; or the render's own
// window is not the union of the eyes' filled frusta).
bool PassShownFraction(int pass, float out[4]);

} // namespace mevr::host::metric_stereo
