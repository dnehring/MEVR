// The frame-breakdown probe (plugin/PerfProbe.hpp, research/22). Its seams in Halcyon's code are
// MevrBridge::PerfPresentScope, PerfPass0Captured and g_perfDraws (src/halcyon/le1/mevr_bridge.h), and the
// frame functions wrapped through MevrBridge::WrapSessionFunctions (plugin/TestLoop).

#include "plugin/PerfProbe.hpp"

#include "core/Log.hpp"
#include "core/vr/FrameProbe.hpp"
#include "core/vr/TestRequest.hpp"
#include "halcyon/le1/mevr_bridge.h"
#include "halcyon/le1/vr_config.h"
#include "halcyon/le1/xr_session.h"

#include <Windows.h>
#include <d3d11.h>
#include <intrin.h>
#include <wrl/client.h>

#include <openxr/openxr.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace mevr::host::perf {
namespace {

namespace fp = vr::frame_probe;
namespace fs = std::filesystem;
namespace req = vr::test_request;
using Microsoft::WRL::ComPtr;

// ---- Clocks ------------------------------------------------------------------------------------------------

std::int64_t Now() {
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

double QpcFrequency() {
    static double const frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    return frequency;
}

std::uint64_t ThreadCycles() {
    ULONG64 cycles = 0;
    QueryThreadCycleTime(GetCurrentThread(), &cycles);
    return cycles;
}

// The calling thread's user plus kernel time, in seconds.
double ThreadSeconds() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) {
        return 0.0;
    }
    auto const value = [](FILETIME const& t) {
        return static_cast<double>((static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime) * 1e-7;
    };
    return value(kernel) + value(user);
}

// ---- Control (any thread) ------------------------------------------------------------------------------------

enum class State : int { Idle, Armed, Running, Finished };
std::atomic<int> gState{static_cast<int>(State::Idle)};
std::atomic<bool> gRunning{false};
std::atomic<std::int64_t> gRunningSinceQpc{0};

std::mutex gControlMutex;
bool gRequested = false;
double gRequestSeconds = 20.0;
std::uint64_t gRequestDueMs = 0;
std::string gRequestTag;
std::string gRequestTrigger;
std::string gLastCapture;

State LoadState() {
    return static_cast<State>(gState.load(std::memory_order_acquire));
}

void StoreState(State state) {
    gState.store(static_cast<int>(state), std::memory_order_release);
}

// ---- The capture: the render thread's while running, the game thread's otherwise ---------------------------

struct Capture {
    std::string stamp;
    std::string tag;
    std::string trigger;
    double seconds = 0.0;
    std::vector<fp::Frame> frames;  // reserved when armed: never grows while running
    std::vector<fp::Tick> ticks;    // the game thread's
    fp::Clocks clocks;
    std::uint64_t tscBegin = 0;
    std::uint64_t tscEnd = 0;
    std::int64_t qpcEnd = 0;
    std::uint64_t cyclesBegin = 0;  // the render thread's, over the whole capture
    std::uint64_t cyclesEnd = 0;
    double threadSecondsBegin = 0.0;
    double threadSecondsEnd = 0.0;
    std::uint32_t accessViolations = 0;
    DWORD renderThread = 0;
    DWORD gameThread = 0;
    bool gpu = false;  // timestamps taken
    std::size_t gpuDropped = 0;
};
Capture gCapture;

// ---- Render thread ----------------------------------------------------------------------------------------------

ID3D11DeviceContext* gContext = nullptr;  // the game's immediate context: lives as long as the game
fp::Frame gFrame;                          // being filled

constexpr std::size_t kGpuPoints = fp::kGpuPoints.size();

struct GpuSlot {
    ComPtr<ID3D11Query> disjoint;
    std::array<ComPtr<ID3D11Query>, kGpuPoints> stamps;
    std::array<bool, kGpuPoints> issued{};
    std::size_t frame = SIZE_MAX;
    bool inFlight = false;
};
constexpr int kSlots = 16;
std::array<GpuSlot, kSlots> gSlots;
int gSlot = -1;  // the frame being filled
int gNextSlot = 0;
bool gQueriesMade = false;
ComPtr<ID3D11Query> gSyncDisjoint;
std::array<ComPtr<ID3D11Query>, 6> gSyncStamps;

std::atomic<std::uint32_t> gAccessViolations{0};
std::uint32_t gViolationsAtFrameStart = 0;
PVOID gHandler = nullptr;

LONG CALLBACK CountAccessViolation(EXCEPTION_POINTERS* info) {
    if (info != nullptr && info->ExceptionRecord != nullptr &&
        info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        gAccessViolations.fetch_add(1, std::memory_order_relaxed);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

bool MakeQueries(ID3D11DeviceContext* context) {
    if (gQueriesMade) {
        return true;
    }
    ComPtr<ID3D11Device> device;
    context->GetDevice(device.GetAddressOf());
    if (device == nullptr) {
        return false;
    }
    D3D11_QUERY_DESC const disjoint{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    D3D11_QUERY_DESC const stamp{D3D11_QUERY_TIMESTAMP, 0};
    bool ok = SUCCEEDED(device->CreateQuery(&disjoint, gSyncDisjoint.ReleaseAndGetAddressOf()));
    for (auto& q : gSyncStamps) {
        ok = ok && SUCCEEDED(device->CreateQuery(&stamp, q.ReleaseAndGetAddressOf()));
    }
    for (GpuSlot& slot : gSlots) {
        ok = ok && SUCCEEDED(device->CreateQuery(&disjoint, slot.disjoint.ReleaseAndGetAddressOf()));
        for (auto& q : slot.stamps) {
            ok = ok && SUCCEEDED(device->CreateQuery(&stamp, q.ReleaseAndGetAddressOf()));
        }
    }
    if (!ok) {
        log::Error("perf probe: creating the timestamp queries failed; CPU times only");
        return false;
    }
    gQueriesMade = true;
    return true;
}

// The query's answer, waiting for it (the commands were flushed); false after `timeoutMs`.
template <typename T>
bool Await(ID3D11Query* query, T& out, std::uint64_t timeoutMs) {
    std::uint64_t const start = GetTickCount64();
    for (;;) {
        HRESULT const hr = gContext->GetData(query, &out, sizeof(T), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (hr == S_OK) {
            return true;
        }
        if (FAILED(hr) || GetTickCount64() - start > timeoutMs) {
            return false;
        }
        SwitchToThread();
    }
}

// Reads the GPU clock against the CPU's: drains the GPU, then issues timestamps one at a time with nothing
// ahead of them. The smallest GPU-minus-CPU difference is the offset (plus the submit latency, which is
// what "the GPU caught up" looks like later anyway). Stalls this thread for about a frame.
fp::ClockSync SyncClocks(double* gpuFrequency) {
    fp::ClockSync out;
    gContext->Begin(gSyncDisjoint.Get());
    gContext->End(gSyncStamps[0].Get());
    gContext->Flush();
    UINT64 drained = 0;
    std::array<std::int64_t, 5> issued{};
    std::array<UINT64, 5> ticks{};
    int n = 0;
    if (Await(gSyncStamps[0].Get(), drained, 500)) {
        for (int i = 1; i < static_cast<int>(gSyncStamps.size()); ++i) {
            std::int64_t const before = Now();
            gContext->End(gSyncStamps[i].Get());
            gContext->Flush();
            UINT64 t = 0;
            if (!Await(gSyncStamps[i].Get(), t, 100)) {
                break;
            }
            issued[n] = before;
            ticks[n] = t;
            ++n;
        }
    }
    gContext->End(gSyncDisjoint.Get());
    gContext->Flush();
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    if (!Await(gSyncDisjoint.Get(), dj, 500) || dj.Disjoint || dj.Frequency == 0 || n == 0) {
        return out;
    }
    *gpuFrequency = static_cast<double>(dj.Frequency);
    double best = std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; ++i) {
        double const d = static_cast<double>(ticks[i]) / *gpuFrequency - static_cast<double>(issued[i]) / QpcFrequency();
        if (d < best) {
            best = d;
            out.qpc = issued[i];
        }
    }
    out.gpuMinusCpu = best;
    out.valid = true;
    return out;
}

// A finished frame's timestamps, if the GPU has them (never waits).
bool Collect(GpuSlot& slot) {
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    if (gContext->GetData(slot.disjoint.Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
        return false;
    }
    std::array<std::uint64_t, kGpuPoints> ticks{};
    for (std::size_t i = 0; i < kGpuPoints; ++i) {
        if (slot.issued[i] &&
            gContext->GetData(slot.stamps[i].Get(), &ticks[i], sizeof(UINT64), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
            return false;
        }
    }
    slot.inFlight = false;
    if (slot.frame < gCapture.frames.size()) {
        fp::Frame& frame = gCapture.frames[slot.frame];
        frame.gpu = ticks;
        frame.gpuValid = !dj.Disjoint && dj.Frequency != 0;
    }
    return true;
}

void CollectAll() {
    for (GpuSlot& slot : gSlots) {
        if (slot.inFlight) {
            Collect(slot);
        }
    }
}

void Record(int point, ID3D11DeviceContext* context) {
    gFrame.qpc[point] = Now();
    gFrame.cycles[point] = ThreadCycles();
    gFrame.draws[point] = MELEVR::MevrBridge::g_perfDraws;
    if (point == fp::kPass0) {
        ++gFrame.pass0Marks;
    }
    int const g = fp::GpuIndex(point);
    if (g > 0 && gSlot >= 0 && (context == nullptr || context == gContext)) {
        GpuSlot& slot = gSlots[gSlot];
        gContext->End(slot.stamps[g].Get());  // again for a repeated point: the last one counts
        slot.issued[g] = true;
    }
}

void OpenFrame(std::int64_t qpc, std::uint64_t cycles, std::uint32_t draws) {
    gFrame = {};
    gFrame.qpc[fp::kStart] = qpc;
    gFrame.cycles[fp::kStart] = cycles;
    gFrame.draws[fp::kStart] = draws;
    gViolationsAtFrameStart = gAccessViolations.load(std::memory_order_relaxed);
    gSlot = -1;
    if (!gCapture.gpu) {
        return;
    }
    GpuSlot& slot = gSlots[gNextSlot];
    if (slot.inFlight && !Collect(slot)) {
        slot.inFlight = false;  // 16 frames late: leave that frame without GPU times
        ++gCapture.gpuDropped;
    }
    gSlot = gNextSlot;
    gNextSlot = (gNextSlot + 1) % kSlots;
    slot.issued.fill(false);
    slot.frame = SIZE_MAX;
    gContext->Begin(slot.disjoint.Get());
    gContext->End(slot.stamps[0].Get());
    slot.issued[0] = true;
}

void CloseFrame() {
    gFrame.accessViolations = gAccessViolations.load(std::memory_order_relaxed) - gViolationsAtFrameStart;
    std::size_t const index = gCapture.frames.size();
    if (gSlot >= 0) {
        GpuSlot& slot = gSlots[gSlot];
        gContext->End(slot.disjoint.Get());
        slot.frame = index;
        slot.inFlight = true;
    }
    gCapture.frames.push_back(gFrame);
    CollectAll();
}

void Start(ID3D11DeviceContext* context) {
    gContext = context;
    gCapture.renderThread = GetCurrentThreadId();
    fp::Clocks& clocks = gCapture.clocks;
    clocks.qpcFrequency = QpcFrequency();
    gCapture.gpu = context != nullptr && MakeQueries(context);
    if (gCapture.gpu) {
        clocks.syncBegin = SyncClocks(&clocks.gpuFrequency);
        if (!clocks.syncBegin.valid) {
            log::Warn("perf probe: the GPU clock sync failed; CPU times only");
            gCapture.gpu = false;
        }
    }
    gAccessViolations.store(0, std::memory_order_relaxed);
    gHandler = AddVectoredExceptionHandler(1, &CountAccessViolation);
    gCapture.tscBegin = __rdtsc();
    gCapture.cyclesBegin = ThreadCycles();
    gCapture.threadSecondsBegin = ThreadSeconds();
    std::int64_t const now = Now();
    clocks.originQpc = now;
    gRunningSinceQpc.store(now, std::memory_order_relaxed);
    gNextSlot = 0;
    OpenFrame(now, gCapture.cyclesBegin, MELEVR::MevrBridge::g_perfDraws);
    gRunning.store(true, std::memory_order_release);
    StoreState(State::Running);
    log::Info("perf probe: measuring {:.0f} s from this frame (render thread {}{})", gCapture.seconds,
        gCapture.renderThread, gCapture.gpu ? ", GPU timestamps" : ", no GPU timestamps");
}

void Stop() {
    gRunning.store(false, std::memory_order_relaxed);
    gCapture.qpcEnd = Now();
    gCapture.tscEnd = __rdtsc();
    gCapture.cyclesEnd = ThreadCycles();
    gCapture.threadSecondsEnd = ThreadSeconds();
    if (gCapture.gpu) {
        double frequency = 0.0;
        gCapture.clocks.syncEnd = SyncClocks(&frequency);  // drains the GPU: every frame's answer is in
        for (GpuSlot& slot : gSlots) {
            if (slot.inFlight && !Collect(slot)) {
                slot.inFlight = false;
                ++gCapture.gpuDropped;
            }
        }
    }
    if (gHandler != nullptr) {
        RemoveVectoredExceptionHandler(gHandler);
        gHandler = nullptr;
    }
    gCapture.accessViolations = gAccessViolations.load(std::memory_order_relaxed);
    double const seconds = static_cast<double>(gCapture.qpcEnd - gCapture.clocks.originQpc) / QpcFrequency();
    if (seconds > 0.0) {
        gCapture.clocks.cycleFrequency = static_cast<double>(gCapture.tscEnd - gCapture.tscBegin) / seconds;
    }
    StoreState(State::Finished);
}

void OnPresentEnd(ID3D11DeviceContext* context) {
    State const state = LoadState();
    if (state == State::Armed) {
        if (context != nullptr) {
            Start(context);
        }
        return;
    }
    if (state != State::Running || GetCurrentThreadId() != gCapture.renderThread) {
        return;
    }
    Record(fp::kEnd, context);
    std::int64_t const qpc = gFrame.qpc[fp::kEnd];
    std::uint64_t const cycles = gFrame.cycles[fp::kEnd];
    std::uint32_t const draws = gFrame.draws[fp::kEnd];
    CloseFrame();
    double const elapsed = static_cast<double>(qpc - gCapture.clocks.originQpc) / QpcFrequency();
    if (elapsed >= gCapture.seconds || gCapture.frames.size() >= gCapture.frames.capacity()) {
        Stop();
        return;
    }
    OpenFrame(qpc, cycles, draws);
}

bool OnRenderThread() {
    return gRunning.load(std::memory_order_relaxed) && GetCurrentThreadId() == gCapture.renderThread;
}

// ---- The wrapped frame functions -------------------------------------------------------------------------------

PFN_xrWaitSwapchainImage gRealWaitSwapchainImage = nullptr;
PFN_xrWaitFrame gRealWaitFrame = nullptr;
PFN_xrBeginFrame gRealBeginFrame = nullptr;
PFN_xrEndFrame gRealEndFrame = nullptr;

XrResult XRAPI_CALL WrappedWaitSwapchainImage(XrSwapchain swapchain, XrSwapchainImageWaitInfo const* info) {
    if (!OnRenderThread()) {
        return gRealWaitSwapchainImage(swapchain, info);
    }
    std::int64_t const before = Now();
    XrResult const result = gRealWaitSwapchainImage(swapchain, info);
    gFrame.swapchainWaitQpc += Now() - before;
    return result;
}

XrResult XRAPI_CALL WrappedWaitFrame(XrSession session, XrFrameWaitInfo const* info, XrFrameState* state) {
    if (!OnRenderThread()) {
        return gRealWaitFrame(session, info, state);
    }
    Record(fp::kWaitBegin, nullptr);
    XrResult const result = gRealWaitFrame(session, info, state);
    Record(fp::kWaitEnd, nullptr);
    gFrame.xrFrame = true;
    if (XR_SUCCEEDED(result) && state != nullptr) {
        gFrame.displayPeriodNs = state->predictedDisplayPeriod;
        gFrame.shouldRender = state->shouldRender != XR_FALSE;
    }
    return result;
}

XrResult XRAPI_CALL WrappedBeginFrame(XrSession session, XrFrameBeginInfo const* info) {
    if (!OnRenderThread()) {
        return gRealBeginFrame(session, info);
    }
    Record(fp::kBeginBegin, nullptr);
    XrResult const result = gRealBeginFrame(session, info);
    Record(fp::kBeginEnd, nullptr);
    return result;
}

XrResult XRAPI_CALL WrappedEndFrame(XrSession session, XrFrameEndInfo const* info) {
    if (!OnRenderThread()) {
        return gRealEndFrame(session, info);
    }
    Record(fp::kEndFrameBegin, gContext);
    XrResult const result = gRealEndFrame(session, info);
    Record(fp::kEndFrameEnd, nullptr);
    return result;
}

// ---- Files (game thread) -----------------------------------------------------------------------------------------

fs::path PerfDir() {
    wchar_t buffer[MAX_PATH]{};
    DWORD const n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    std::error_code ec;
    fs::path const base = (n > 0 && n < MAX_PATH) ? fs::path(buffer) : fs::temp_directory_path(ec);
    return base / L"MEVR" / L"perf";
}

std::string Utf8(fs::path const& path) {
    std::u8string const u = path.u8string();
    return std::string(u.begin(), u.end());
}

std::string Stamp() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    return std::format("{:04}-{:02}-{:02}_{:02}-{:02}-{:02}", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

std::string SafeTag(std::string const& tag) {
    std::string out;
    for (char const c : tag) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_') {
            out += c;
        }
        if (out.size() >= 32) {
            break;
        }
    }
    return out;
}

bool WriteText(fs::path const& path, std::string const& text) {
    fs::path const temp = fs::path(path).concat(L".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            return false;
        }
    }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

std::string J(double value) {
    return req::JsonNumber(value);
}

std::string Sync(fp::ClockSync const& sync, fp::Clocks const& clocks) {
    double const atMs = clocks.qpcFrequency > 0.0
        ? static_cast<double>(sync.qpc - clocks.originQpc) * 1000.0 / clocks.qpcFrequency : 0.0;
    return std::format("{{\"valid\": {}, \"atMs\": {}, \"gpuMinusCpuMs\": {}}}", sync.valid ? "true" : "false", J(atMs),
        J(sync.gpuMinusCpu * 1000.0));
}

void WriteCapture() {
    Capture const& c = gCapture;
    std::error_code ec;
    fs::path const dir = PerfDir();
    fs::create_directories(dir, ec);
    std::string const base = c.stamp + (c.tag.empty() ? std::string() : "-" + c.tag);
    fs::path const framesPath = dir / (base + "-frames.csv");
    fs::path const ticksPath = dir / (base + "-ticks.csv");
    fs::path const jsonPath = dir / (base + "-probe.json");

    std::string frames = fp::FrameCsvHeader();
    for (std::size_t i = 0; i < c.frames.size(); ++i) {
        frames += fp::FrameCsvRow(i, c.frames[i], c.clocks);
    }
    std::string ticks = fp::TickCsvHeader();
    for (std::size_t i = 0; i < c.ticks.size(); ++i) {
        ticks += fp::TickCsvRow(i, c.ticks[i], c.clocks);
    }
    bool ok = WriteText(framesPath, frames) && WriteText(ticksPath, ticks);

    MELEVR::Config::VrConfig const& h = MELEVR::Config::Get();
    MELEVR::XrSession::ResolutionStats resolution;
    MELEVR::XrSession::GetResolutionStats(&resolution);
    double const seconds = c.clocks.qpcFrequency > 0.0
        ? static_cast<double>(c.qpcEnd - c.clocks.originQpc) / c.clocks.qpcFrequency : 0.0;
    double const cycleSeconds = c.clocks.cycleFrequency > 0.0
        ? static_cast<double>(c.cyclesEnd - c.cyclesBegin) / c.clocks.cycleFrequency : 0.0;
    std::string const json = std::format(
        "{{\n  \"stamp\": {},\n  \"tag\": {},\n  \"trigger\": {},\n  \"secondsRequested\": {},\n  \"seconds\": {},\n"
        "  \"frames\": {},\n  \"ticks\": {},\n  \"files\": {{\"frames\": {}, \"ticks\": {}}},\n"
        "  \"clocks\": {{\"qpcFrequency\": {}, \"cycleFrequency\": {}, \"gpuFrequency\": {}, \"syncBegin\": {}, "
        "\"syncEnd\": {}}},\n  \"gpuTimestamps\": {},\n  \"gpuFramesDropped\": {},\n  \"accessViolations\": {},\n"
        "  \"renderThread\": {},\n  \"gameThread\": {},\n"
        "  \"renderThreadBusySeconds\": {{\"fromCycles\": {}, \"fromThreadTimes\": {}}},\n"
        "  \"halcyon\": {{\"sfr2\": {}, \"dynResFraction\": {}, \"mirrorPresentEvery\": {}, \"stereoFramePacing\": {}, "
        "\"cineFpsCap\": {}, \"disableDof\": {}, \"hudPanel\": {}, \"handReticle\": {}}},\n"
        "  \"resolution\": {{\"render\": [{}, {}], \"eye\": [{}, {}], \"recommendedEye\": [{}, {}]}}\n}}\n",
        req::JsonString(c.stamp), req::JsonString(c.tag), req::JsonString(c.trigger), J(c.seconds), J(seconds),
        c.frames.size(), c.ticks.size(), req::JsonString(Utf8(framesPath.filename())),
        req::JsonString(Utf8(ticksPath.filename())), J(c.clocks.qpcFrequency), J(c.clocks.cycleFrequency),
        J(c.clocks.gpuFrequency), Sync(c.clocks.syncBegin, c.clocks), Sync(c.clocks.syncEnd, c.clocks),
        c.gpu ? "true" : "false", c.gpuDropped, c.accessViolations, c.renderThread, c.gameThread, J(cycleSeconds),
        J(c.threadSecondsEnd - c.threadSecondsBegin), h.sfr2Enabled ? "true" : "false", J(h.dynResFraction),
        h.mirrorPresentEvery, h.stereoFramePacing ? "true" : "false", h.cineFpsCap ? "true" : "false",
        h.disableDof ? "true" : "false", h.hudPanel ? "true" : "false", h.handReticle ? "true" : "false",
        resolution.renderWidth, resolution.renderHeight, resolution.eyeWidth, resolution.eyeHeight,
        resolution.recommendedEyeWidth, resolution.recommendedEyeHeight);
    ok = ok && WriteText(jsonPath, json);  // last: the scripts wait for it
    if (!ok) {
        log::Error("perf probe: writing {} failed", Utf8(dir / base));
        return;
    }
    log::Info("perf probe: {} frames and {} ticks in {:.1f} s ({} access violations{}) -> {}", c.frames.size(),
        c.ticks.size(), seconds, c.accessViolations,
        c.gpu ? std::format(", {} frames without GPU times", c.gpuDropped) : std::string(", no GPU times"),
        Utf8(jsonPath));
    std::lock_guard lock(gControlMutex);
    gLastCapture = Utf8(jsonPath);
}

// ---- Game thread --------------------------------------------------------------------------------------------------

fp::Tick gTick;
bool gTickOpen = false;

} // namespace

void Mark(int point, ID3D11DeviceContext* context) {
    if (point == fp::kEnd) {
        OnPresentEnd(context);
        return;
    }
    if (point <= fp::kStart || point >= fp::kPointCount || !OnRenderThread()) {
        return;
    }
    Record(point, context);
}

void WrapFrameFunctions(void** waitSwapchainImage, void** waitFrame, void** beginFrame, void** endFrame) {
    // Re-resolving hands us the real functions again; never wrap a wrapper.
    auto wrap = [](void** slot, auto& real, auto wrapper) {
        if (slot == nullptr || *slot == nullptr || *slot == reinterpret_cast<void*>(wrapper)) {
            return;
        }
        real = reinterpret_cast<std::remove_reference_t<decltype(real)>>(*slot);
        *slot = reinterpret_cast<void*>(wrapper);
    };
    wrap(waitSwapchainImage, gRealWaitSwapchainImage, &WrappedWaitSwapchainImage);
    wrap(waitFrame, gRealWaitFrame, &WrappedWaitFrame);
    wrap(beginFrame, gRealBeginFrame, &WrappedBeginFrame);
    wrap(endFrame, gRealEndFrame, &WrappedEndFrame);
}

void EngineTickBegin() {
    gTickOpen = gRunning.load(std::memory_order_relaxed);
    if (!gTickOpen) {
        return;
    }
    if (gCapture.gameThread == 0) {
        gCapture.gameThread = GetCurrentThreadId();
    }
    gTick = {};
    gTick.beginQpc = Now();
    gTick.beginCycles = ThreadCycles();
}

void EngineTickEnd() {
    if (gTickOpen) {
        gTick.engineEndQpc = Now();
        gTick.engineEndCycles = ThreadCycles();
    }
}

void HostTickEnd() {
    if (!gTickOpen) {
        return;
    }
    gTickOpen = false;
    gTick.endQpc = Now();
    gTick.endCycles = ThreadCycles();
    if (gCapture.ticks.size() < gCapture.ticks.capacity()) {
        gCapture.ticks.push_back(gTick);
    }
}

void Tick() {
    State const state = LoadState();
    if (state == State::Finished) {
        gTickOpen = false;  // this tick isn't part of it
        WriteCapture();
        gCapture = {};
        StoreState(State::Idle);
        return;
    }
    if (state != State::Idle) {
        return;
    }
    Capture next;
    {
        std::lock_guard lock(gControlMutex);
        if (!gRequested || GetTickCount64() < gRequestDueMs) {
            return;
        }
        gRequested = false;
        next.seconds = gRequestSeconds;
        next.tag = SafeTag(gRequestTag);
        next.trigger = gRequestTrigger;
    }
    next.stamp = Stamp();
    std::size_t const capacity = static_cast<std::size_t>(next.seconds * 250.0) + 64;  // up to 240 Hz
    next.frames.reserve(capacity);
    next.ticks.reserve(capacity);
    gCapture = std::move(next);
    StoreState(State::Armed);  // the render thread starts at the next Present's end
    log::Info("perf probe: armed for {:.0f} s ({})", gCapture.seconds, gCapture.trigger);
}

void Request(double seconds, std::string const& tag, char const* trigger, double delaySeconds) {
    std::lock_guard lock(gControlMutex);
    gRequested = true;
    gRequestSeconds = std::clamp(seconds, 1.0, 120.0);
    gRequestDueMs = GetTickCount64() + static_cast<std::uint64_t>(std::clamp(delaySeconds, 0.0, 600.0) * 1000.0);
    gRequestTag = tag;
    gRequestTrigger = trigger != nullptr ? trigger : "?";
}

Status GetStatus() {
    Status s;
    State const state = LoadState();
    s.pending = state != State::Idle;
    if (state == State::Running) {
        s.seconds = static_cast<double>(Now() - gRunningSinceQpc.load(std::memory_order_relaxed)) / QpcFrequency();
    }
    std::lock_guard lock(gControlMutex);
    s.pending = s.pending || gRequested;
    s.lastCapture = gLastCapture;
    return s;
}

} // namespace mevr::host::perf

namespace MELEVR::MevrBridge {

std::uint32_t g_perfDraws = 0;

void PerfPresentBegin(ID3D11DeviceContext* context) noexcept {
    mevr::host::perf::Mark(mevr::vr::frame_probe::kPresent, context);
}

void PerfPresentEnd(ID3D11DeviceContext* context) noexcept {
    mevr::host::perf::Mark(mevr::vr::frame_probe::kEnd, context);
}

void PerfPass0Captured(ID3D11DeviceContext* context) noexcept {
    mevr::host::perf::Mark(mevr::vr::frame_probe::kPass0, context);
}

} // namespace MELEVR::MevrBridge
