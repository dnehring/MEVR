// SPDX-License-Identifier: MIT
#pragma once

// What plugin/RenderLevers asks of the renderer it runs beside, as `render_levers::seam` (research/sequel-render-levers.md
// "Shared module"). LE1: Halcyon's ME1 renderer's own seams (src/halcyon/le1/mevr_bridge.h, MELEVR::MevrBridge), exactly
// the names the module has always called. The sequels: Halcyon's ME2 and ME3 renderers have none of them yet (the one
// seam vocabulary is lane U1's, src/halcyon/MevrSeams.h), so the same names answer "no such seam": no stereo frame is
// announced (every scene render counts by its place in the frame, none is taken for a capture by its target), no
// pass-1 view is recorded (one Draw a frame can't synthesise a second eye) and the one-Draw switch is a flag nobody
// reads. When a sequel's renderer gets a seam, its answer replaces the one here and nothing in RenderLevers.cpp changes.

#include <atomic>
#include <cstdint>

#if defined(MEVR_WITH_HALCYON)

#include "halcyon/le1/mevr_bridge.h"

namespace mevr::host::render_levers {
namespace seam = MELEVR::MevrBridge;
// Immediate-context draws so far, as the renderer's D3D hooks count them (a wrapping counter).
inline std::uint32_t Draws() noexcept {
    return MELEVR::MevrBridge::g_perfDraws;
}
inline constexpr bool kOneDrawSeams = true;   // RecordPass1View in the stereo branch, g_oneDrawStereo read by the second Draw
} // namespace mevr::host::render_levers

#else

#include "plugin/PerfProbeHost.hpp"

namespace mevr::host::render_levers {
namespace seam {

// As MELEVR::MevrBridge::Pass1View (src/halcyon/le1/mevr_bridge.h).
struct Pass1View {
    float viewOrigin[3] = {};
    float forward[3] = {};
    float delta[3] = {};
    int mode = 0;
    float p0 = 1.0f, p5 = 1.0f, p8 = 0.0f, p9 = 0.0f;
};
inline bool FindPass1View(float const*, float const*, Pass1View*) noexcept {
    return false;
}
inline bool NewestPass1View(Pass1View*) noexcept {
    return false;
}
inline std::uint64_t Pass1Records() noexcept {
    return 0;
}
inline bool StereoFrameActive() noexcept {
    return false;
}
inline std::atomic<bool> g_oneDrawStereo{false};

} // namespace seam
inline std::uint32_t Draws() noexcept {
    return perf::adapter::Draws();
}
inline constexpr bool kOneDrawSeams = false;
} // namespace mevr::host::render_levers

#endif
