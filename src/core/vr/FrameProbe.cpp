// SPDX-License-Identifier: MIT
#include "core/vr/FrameProbe.hpp"

#include <format>

namespace mevr::vr::frame_probe {
namespace {

constexpr char const* kNames[kPointCount] = {"start", "pass0", "present", "preamble", "frame_entry", "frame_reads", "frame_cine", "frame_prepared", "wait_begin", "wait_end", "begin_begin",
    "begin_end", "input_begin", "input_end", "frame_begun_end", "frame_ready", "overlay_begin", "overlay_end", "end_frame_begin",
    "end_frame_end", "next_wait_begin", "next_wait_end", "next_begin_begin", "next_begin_end",
    "next_input_begin", "next_input_end", "next_frame_begun_end", "end"};

double QpcMs(Clocks const& clocks, std::int64_t qpc) {
    return clocks.qpcFrequency > 0.0 ? static_cast<double>(qpc - clocks.originQpc) * 1000.0 / clocks.qpcFrequency : 0.0;
}

double CycleMs(Clocks const& clocks, std::uint64_t from, std::uint64_t to) {
    return clocks.cycleFrequency > 0.0 && to >= from ? static_cast<double>(to - from) * 1000.0 / clocks.cycleFrequency
                                                     : 0.0;
}

std::string Ms(double ms) {
    return std::format("{:.4f}", ms);
}

} // namespace

char const* PointName(int point) {
    return point >= 0 && point < kPointCount ? kNames[point] : "?";
}

int GpuIndex(int point) {
    for (std::size_t i = 0; i < kGpuPoints.size(); ++i) {
        if (kGpuPoints[i] == point) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

double GpuOffsetAt(Clocks const& clocks, std::int64_t qpc) {
    ClockSync const& a = clocks.syncBegin;
    ClockSync const& b = clocks.syncEnd;
    if (a.valid && b.valid && b.qpc != a.qpc) {
        double const f = static_cast<double>(qpc - a.qpc) / static_cast<double>(b.qpc - a.qpc);
        return a.gpuMinusCpu + (b.gpuMinusCpu - a.gpuMinusCpu) * f;
    }
    return a.valid ? a.gpuMinusCpu : b.valid ? b.gpuMinusCpu : 0.0;
}

double GpuMs(Clocks const& clocks, std::uint64_t ticks, std::int64_t qpc) {
    if (clocks.gpuFrequency <= 0.0 || clocks.qpcFrequency <= 0.0) {
        return 0.0;
    }
    double const cpuSeconds = static_cast<double>(ticks) / clocks.gpuFrequency - GpuOffsetAt(clocks, qpc);
    return (cpuSeconds - static_cast<double>(clocks.originQpc) / clocks.qpcFrequency) * 1000.0;
}

std::string FrameCsvHeader() {
    std::string out = "frame";
    for (int p = 0; p < kPointCount; ++p) {
        out += std::format(",t_{0},busy_{0},draws_{0}", kNames[p]);
    }
    for (int p : kGpuPoints) {
        out += std::format(",gpu_{}", kNames[p]);
    }
    out += ",gpu_valid,xr_frame,should_render,display_period_ms,swapchain_wait_ms,access_violations,pass0_marks\n";
    return out;
}

std::string FrameCsvRow(std::size_t index, Frame const& frame, Clocks const& clocks) {
    std::string out = std::to_string(index);
    bool const started = frame.qpc[kStart] != 0;
    for (int p = 0; p < kPointCount; ++p) {
        if (frame.qpc[p] == 0 || !started) {
            out += ",,,";
            continue;
        }
        out += ',' + Ms(QpcMs(clocks, frame.qpc[p]));
        out += ',' + Ms(CycleMs(clocks, frame.cycles[kStart], frame.cycles[p]));
        out += ',' + std::to_string(frame.draws[p] - frame.draws[kStart]);
    }
    for (std::size_t i = 0; i < kGpuPoints.size(); ++i) {
        out += ',';
        if (frame.gpuValid && frame.gpu[i] != 0) {
            out += Ms(GpuMs(clocks, frame.gpu[i], frame.qpc[kStart]));
        }
    }
    double const swapchainWaitMs =
        clocks.qpcFrequency > 0.0 ? static_cast<double>(frame.swapchainWaitQpc) * 1000.0 / clocks.qpcFrequency : 0.0;
    out += std::format(",{},{},{},{},{},{},{}\n", frame.gpuValid ? 1 : 0, frame.xrFrame ? 1 : 0, frame.shouldRender ? 1 : 0,
        Ms(static_cast<double>(frame.displayPeriodNs) * 1e-6), Ms(swapchainWaitMs), frame.accessViolations, frame.pass0Marks);
    return out;
}

std::string TickCsvHeader() {
    return "tick,t_begin,engine_ms,engine_busy_ms,host_ms,host_busy_ms,t_hook_begin,hook_ms,hook_busy_ms,t_drain_begin,drain_ms,drain_busy_ms,queued_calls\n";
}

std::string TickCsvRow(std::size_t index, Tick const& tick, Clocks const& clocks) {
    std::string out = std::format("{},{},{},{},{},{}", index, Ms(QpcMs(clocks, tick.beginQpc)),
        Ms(QpcMs(clocks, tick.engineEndQpc) - QpcMs(clocks, tick.beginQpc)),
        Ms(CycleMs(clocks, tick.beginCycles, tick.engineEndCycles)),
        Ms(QpcMs(clocks, tick.endQpc) - QpcMs(clocks, tick.engineEndQpc)),
        Ms(CycleMs(clocks, tick.engineEndCycles, tick.endCycles)));
    if (tick.hookBeginQpc != 0) {
        out += std::format(",{},{},{}", Ms(QpcMs(clocks, tick.hookBeginQpc)),
            Ms(QpcMs(clocks, tick.endQpc) - QpcMs(clocks, tick.hookBeginQpc)),
            Ms(CycleMs(clocks, tick.hookBeginCycles, tick.endCycles)));
    } else out += ",,,";
    if (tick.drainBeginQpc != 0) {
        out += std::format(",{},{},{}", Ms(QpcMs(clocks, tick.drainBeginQpc)),
            Ms(QpcMs(clocks, tick.beginQpc) - QpcMs(clocks, tick.drainBeginQpc)),
            Ms(CycleMs(clocks, tick.drainBeginCycles, tick.beginCycles)));
    } else out += ",,,";
    return out + std::format(",{}\n", tick.queuedCalls);
}

std::string QueuedCallCsvHeader() {
    return "call,tick,object,function,t_begin,t_end,wall_ms,busy_ms,outcome\n";
}

std::string QueuedCallCsvRow(std::size_t index, QueuedCall const& call, Clocks const& clocks) {
    return std::format("{},{},{},{},{},{},{},{},{}\n", index, call.tick, call.object, call.function,
        Ms(QpcMs(clocks, call.beginQpc)), Ms(QpcMs(clocks, call.endQpc)),
        Ms(QpcMs(clocks, call.endQpc) - QpcMs(clocks, call.beginQpc)),
        Ms(CycleMs(clocks, call.beginCycles, call.endCycles)), call.outcome);
}

} // namespace mevr::vr::frame_probe
