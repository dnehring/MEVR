// SPDX-License-Identifier: MIT
#pragma once

#include <string>

struct ID3D11DeviceContext;

// The frame-breakdown probe (research/22): for a few seconds, where each frame's time goes. LE1, with
// Halcyon's renderer in the host.
//
// Per frame of Halcyon's render thread (vr::frame_probe::Point): the time and the thread's own busy time
// at each step (the game's two eye passes and UI, xrWaitFrame, xrBeginFrame, our controllers and quads,
// the eye copies, xrEndFrame, the desktop mirror), GPU timestamps at the main steps mapped onto the CPU's
// clock, the draws issued and the access violations raised. Per engine tick on the game thread: the
// engine's tick and ours, time and busy time. Written to %LOCALAPPDATA%\MEVR\perf\<stamp>[-tag]-frames.csv,
// -ticks.csv and -probe.json (last); tools/perf-probe.ps1 asks for one, tools/perf-probe.py reads it.
//
// Triggers: perf.request ([seconds=N] [tag=word], plugin/TestLoop's request files) and the menu's VR hands
// tab. Idle, each step costs one atomic load. Running, a few timestamp queries a frame, plus one GPU
// drain at the start and one at the end (the clock syncs), so those two frames are slow.
namespace mevr::host::perf {

// ---- Render thread (Halcyon's Present thread) ----------------------------------------------------------
// A step of the frame (vr::frame_probe::Point). `context`: the game's immediate context, for the steps
// that take a GPU timestamp; nullptr uses the last one given.
void Mark(int point, ID3D11DeviceContext* context = nullptr);

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
