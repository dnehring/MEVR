// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// The frame-breakdown probe's records and their CSV form (plugin/PerfProbe, research/22). Pure; tested
// in tests/Main.cpp. Times go out in milliseconds since the capture began, on the CPU's clock; GPU
// timestamps are mapped onto that clock with the probe's clock syncs.
namespace mevr::vr::frame_probe {

// Steps of one frame on Halcyon's render thread, in the order they happen. A frame runs from the end of
// one Present (its start) to the end of the next.
enum Point : int {
    kStart = 0,      // the previous Present returned
    kPass0,          // Stereo 2's first eye captured, at the clear that starts the second (the last one)
    kPresent,        // Present entered: the game's work for both eyes and the UI is issued
    kPreamble,       // Halcyon's Present hook preamble done (GPU timing tick, the pass publish, its logs)
    kFrameEntry,     // its frame function entered, after the runtime's event poll
    kFrameReads,     // its game-state reads done (camera mode name, game mode)
    kFrameCine,      // its cine decision published
    kWaitBegin,      // xrWaitFrame
    kWaitEnd,
    kBeginBegin,     // xrBeginFrame
    kBeginEnd,
    kInputBegin,     // our controllers: xrSyncActions, the action reads, the locates, haptics, floor
    kInputEnd,
    kFrameBegunEnd,  // the rest of our OnFrameBegun (test loop, publishing)
    kOverlayBegin,   // our quads: HUD panel, reticle, sights, squad menu, wheel
    kOverlayEnd,
    kEndFrameBegin,  // xrEndFrame: the eye copies are issued by now
    kEndFrameEnd,
    kEnd,            // Present returns (after the desktop mirror's present, on the frames it runs)
    kPointCount
};

// The column stem for a point: "start", "pass0", "present", ...
char const* PointName(int point);

// The points that also take a GPU timestamp, and a point's index among them (-1: none).
inline constexpr std::array<int, 5> kGpuPoints{kStart, kPass0, kPresent, kEndFrameBegin, kEnd};
int GpuIndex(int point);

struct Frame {
    std::array<std::int64_t, kPointCount> qpc{};       // QueryPerformanceCounter; 0: not reached
    std::array<std::uint64_t, kPointCount> cycles{};   // the render thread's cycle count there
    std::array<std::uint32_t, kPointCount> draws{};    // immediate-context draws so far (a wrapping counter)
    std::array<std::uint64_t, kGpuPoints.size()> gpu{};  // GPU timestamp ticks; 0: none
    bool gpuValid = false;             // the timestamps arrived and the GPU clock held steady
    std::int64_t swapchainWaitQpc = 0;  // spent in xrWaitSwapchainImage, summed
    std::int64_t displayPeriodNs = 0;   // xrWaitFrame's predictedDisplayPeriod
    bool shouldRender = true;
    bool xrFrame = false;               // xrWaitFrame ran (a headset frame)
    std::uint32_t accessViolations = 0;  // raised on any thread during the frame
    std::uint32_t pass0Marks = 0;       // how often the first eye was captured (the last one counts)
};

// One engine tick on the game thread: the engine's own tick, then ours (the host's and the mod's).
struct Tick {
    std::int64_t beginQpc = 0;
    std::int64_t engineEndQpc = 0;
    std::int64_t endQpc = 0;
    std::uint64_t beginCycles = 0;
    std::uint64_t engineEndCycles = 0;
    std::uint64_t endCycles = 0;
};

// One reading of the GPU clock against the CPU's, taken with the GPU idle: GPU time minus CPU time, in
// seconds, at a QPC instant.
struct ClockSync {
    std::int64_t qpc = 0;
    double gpuMinusCpu = 0.0;
    bool valid = false;
};

struct Clocks {
    double qpcFrequency = 0.0;    // QPC ticks per second
    double cycleFrequency = 0.0;  // thread cycle counts per second
    double gpuFrequency = 0.0;    // GPU timestamp ticks per second
    std::int64_t originQpc = 0;   // the capture's start: time 0 in the CSVs
    ClockSync syncBegin;
    ClockSync syncEnd;
};

// The GPU-minus-CPU offset at `qpc`: interpolated between the two syncs (the clocks drift a little),
// or the one that is valid; 0 with none.
double GpuOffsetAt(Clocks const& clocks, std::int64_t qpc);

// A GPU timestamp on the CPU's clock, in ms since the capture began (`qpc`: when, roughly, for the drift).
double GpuMs(Clocks const& clocks, std::uint64_t ticks, std::int64_t qpc);

// CSV, one line per frame: index; per point t_<name> (ms since the capture began), busy_<name> (the
// render thread's busy ms since the frame's start) and draws_<name> (since the frame's start), empty for
// points not reached; gpu_<name> (ms, CPU clock) for the GPU points; then the frame's extras. Lines end
// in "\n".
std::string FrameCsvHeader();
std::string FrameCsvRow(std::size_t index, Frame const& frame, Clocks const& clocks);

// CSV, one line per tick: index, t_begin, engine_ms, engine_busy_ms, host_ms, host_busy_ms.
std::string TickCsvHeader();
std::string TickCsvRow(std::size_t index, Tick const& tick, Clocks const& clocks);

} // namespace mevr::vr::frame_probe
