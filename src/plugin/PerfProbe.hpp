// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <cstdint>
#include "core/vr/FrameProbe.hpp"

struct ID3D11DeviceContext;

// The frame-breakdown probe (research/22): for a few seconds, where each frame's time goes. One module in all three
// games' hosts (research/sequel-render-levers.md "Shared module"); the per-game part is plugin/PerfProbeHost.hpp's
// adapter. LE1 has every step below (Halcyon's ME1 renderer carries the seams). The sequels have the game thread's ticks,
// the wrapped xrEndFrame (and xrWaitFrame, xrBeginFrame once their bridges hand over those slots) and the GPU timestamps
// at the frame's ends, with a frame bounded by xrEndFrame until their renderers call the Present seams below.
//
// Per frame of Halcyon's render thread (vr::frame_probe::Point): the time and the thread's own busy time
// at each step (the game's two eye passes and UI, xrWaitFrame, xrBeginFrame, our controllers and quads,
// the eye copies, xrEndFrame, the desktop mirror), GPU timestamps at the main steps mapped onto the CPU's
// clock, the draws issued and the access violations raised. Per engine tick on the game thread: the
// engine's tick and ours, the full hook and queued ProcessEvents, time and busy time. Written to
// %LOCALAPPDATA%\MEVR\perf\<stamp>[-tag]-frames.csv, -ticks.csv, -calls.csv and -probe.json (last);
// tools/perf-probe.ps1 asks for one, tools/perf-probe.py reads it. Call records use bounded preallocated
// storage and numeric identities; no per-call logging, formatting or object-name resolution while measuring.
//
// Triggers: perf.request ([seconds=N] [tag=word], plugin/TestLoop's request files) and the menu's VR hands
// tab. Idle, each step costs one atomic load. Running, a few timestamp queries a frame, plus one GPU
// drain at the start and one at the end (the clock syncs), so those two frames are slow.
namespace mevr::host::perf {

// ---- Render thread (Halcyon's Present thread) ----------------------------------------------------------
// A step of the frame (vr::frame_probe::Point). `context`: the game's immediate context, for the steps
// that take a GPU timestamp; nullptr uses the last one given.
void Mark(int point, ID3D11DeviceContext* context = nullptr);
void FrameSubmitted(std::int64_t period, bool shouldRender); // own frame's metadata, never N+1's wait
struct GpuTiming {
    double work = -1, sampledAt = 0; // seconds, QPC clock; unavailable until a complete, non-disjoint probe frame
};
GpuTiming LatestGpuTiming(); // render thread only; cached completed queries, never waits or starts a capture

// ---- A renderer's seams (the sequels' bridges; LE1's are MevrBridge::PerfPresentBegin/End, PerfPass0Captured, g_perfDraws) --
// The renderer's Present hook, its first and last action, with the game's immediate context: the frame's bounds (after the
// first SeamPresentBegin a frame is no longer bounded by xrEndFrame). SeamPresentEnd also closes plugin/RenderLevers'
// frame on the sequels. Render thread.
void SeamPresentBegin(ID3D11DeviceContext* context);
void SeamPresentEnd(ID3D11DeviceContext* context);
// Whether a Present seam bounds the frames (LE1 always; a sequel once its Present hook called SeamPresentBegin).
bool PresentSeamSeen();
// The first eye's image was captured: what came before it this frame was the first eye's render. Render thread.
void SeamFirstEyeCaptured(ID3D11DeviceContext* context);
// The sequels' draw counter (LE1's is Halcyon's g_perfDraws): from the renderer's D3D draw hooks, any draw call.
void NoteDraws(std::uint32_t count = 1) noexcept;

// Two marks around a scope.
class ScopedMarks {
public:
    ScopedMarks(int begin, int end) : end_(end) { Mark(begin); }
    ~ScopedMarks() { Mark(end_); }
    ScopedMarks(ScopedMarks const&) = delete;
    ScopedMarks& operator=(ScopedMarks const&) = delete;

private:
    int end_;
};

// From MevrBridge::WrapSessionFunctions: xrWaitSwapchainImage, xrWaitFrame, xrBeginFrame and xrEndFrame
// replaced in place (each argument points at a resolved function pointer) with timed wrappers.
void WrapFrameFunctions(void** waitSwapchainImage, void** waitFrame, void** beginFrame, void** endFrame);

// ---- Game thread ------------------------------------------------------------------------------------------
void GameTickBegin();  // first action in our hook, before queue draining or the original engine tick
void QueueDrainBegin();
vr::frame_probe::QueuedCall QueueCallBegin(void* object, void* function);
void QueueCallEnd(vr::frame_probe::QueuedCall call, int outcome);
void EngineTickBegin();
void EngineTickEnd();  // the engine's tick returned; ours (the host's and the mod's) follows
void HostTickEnd();    // ours too
// Arms a requested capture, and writes a finished one.
void Tick();

// ---- Any thread --------------------------------------------------------------------------------------------
// A capture of `seconds` (clamped to 1-120), starting at the first Present after `delaySeconds`. `tag` goes
// into the file names.
void Request(double seconds, std::string const& tag, char const* trigger, double delaySeconds = 0.0);

struct Status {
    bool pending = false;  // requested or running
    double seconds = 0.0;  // running for
    std::string lastCapture;  // the last capture's JSON
};
Status GetStatus();

} // namespace mevr::host::perf
