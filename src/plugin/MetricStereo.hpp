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

} // namespace mevr::host::metric_stereo
