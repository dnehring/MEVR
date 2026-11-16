// SPDX-License-Identifier: MIT
#pragma once

// What the frame-breakdown probe (plugin/PerfProbe.cpp, one module for all three games) asks of the host it is built
// into: the per-game adapter of research/sequel-render-levers.md "Shared module". LE1's answers come from Halcyon's ME1
// renderer and our LE1-only modules (plugin/PerfProbeLe1.cpp); the sequels' from plugin/PerfProbeSequel.cpp, which has no
// seam inside Halcyon's ME2/ME3 Present path yet and bounds its frames by xrEndFrame instead.
//
// The two inline answers are types and calls the probe makes on every wrapped runtime call, so they are resolved at
// compile time: LE1 keeps exactly the calls it made before the split.

#include <cstdint>
#include <string>

#if defined(MEVR_WITH_HALCYON)
#include "halcyon/le1/mevr_bridge.h"
#include "plugin/QuestPacing.hpp"
#endif

struct ID3D11DeviceContext;
struct XrFrameEndInfo;

namespace mevr::host::perf::adapter {

#if defined(MEVR_WITH_HALCYON)
// LE1: the runtime's calls are taken out of QuestPacing's work measure, and with the early frame wait the calls made after
// xrEndFrame belong to the next frame (plugin/QuestPacing).
using RuntimeCall = quest_pacing::RuntimeCall;
inline bool NextFramePhase() noexcept {
    return quest_pacing::NextFramePhase();
}
// The draws Halcyon's D3D hooks counted (its seam's counter).
inline std::uint32_t Draws() noexcept {
    return MELEVR::MevrBridge::g_perfDraws;
}
// Halcyon's Present hook brackets the frame (MevrBridge::PerfPresentScope).
inline constexpr bool kFramesByEndFrame = false;
#else
// The sequels: no pacing module, no early wait.
struct RuntimeCall {
    RuntimeCall() noexcept {}
    ~RuntimeCall() {}
    RuntimeCall(RuntimeCall const&) = delete;
    RuntimeCall& operator=(RuntimeCall const&) = delete;
};
inline bool NextFramePhase() noexcept {
    return false;
}
// The draws the renderer's seam counted (perf::NoteDraws), 0 until its D3D hooks call it.
std::uint32_t Draws() noexcept;
// No seam in the renderer's Present hook: a frame is xrEndFrame's return to the next one's (PerfProbe.cpp
// EndFrameAsFrameBoundary), until perf::OnPresentBegin is called once (then the Present seam bounds it, as LE1's).
inline constexpr bool kFramesByEndFrame = true;
#endif

// The game's immediate context for a frame bounded by xrEndFrame: found once from the first projection layer's swapchain
// (its D3D11 texture's device). Null until found. Render thread. LE1 never asks.
ID3D11DeviceContext* FrameContext(XrFrameEndInfo const* info) noexcept;

// The end of a frame bounded by xrEndFrame, after the probe closed it: what LE1's MevrBridge::PerfPresentEnd does after
// its mark (plugin/RenderLevers' frame). Render thread. LE1 never asks.
void FrameClosed(ID3D11DeviceContext* context) noexcept;

// The capture JSON's renderer members, each line indented two spaces, the last without a comma or newline:
// LE1 `"halcyon": {...}, "renderLevers": {...}, "upscale": {...}, "resolution": {...}` exactly as before the split;
// the sequels `"renderer"`, `"renderLevers"`, `"resolution"`. Game thread.
std::string RendererJson();

} // namespace mevr::host::perf::adapter
