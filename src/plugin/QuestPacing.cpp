// SPDX-License-Identifier: MIT
#include "plugin/QuestPacing.hpp"
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/QuestPacing.hpp"
#include "core/vr/TestRequest.hpp"
#include "plugin/Colour.hpp"
#include "plugin/PerfProbe.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/MainMenu.hpp"
#endif
#include "halcyon/le1/mevr_bridge.h"
#include "plugin/MevrMenu.hpp"
#include <Windows.h>
#include <intrin.h>
#include <openxr/openxr.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <vector>

namespace mevr::host::quest_pacing {
namespace {
namespace qp = vr::quest_pacing;
qp::Detector gDetector;
std::vector<qp::Action> gActions;
unsigned gMaxTries = 3;
bool gFull = false, gNext = false, gForce = false;
bool gMeasure = false;
double gNextRead = 0, gLastPresent = 0, gInterval = 0, gWork = 0;
std::uint64_t gCycles = 0, gExcluded = 0, gTsc = 0;
double gCycleHz = 0;
XrInstance gInstance = XR_NULL_HANDLE;
XrSession gSession = XR_NULL_HANDLE;
PFN_xrGetInstanceProcAddr gGetProc = nullptr;
bool gPerfEnabled = false, gRefreshEnabled = false;
bool gHaveNativeQuery = false, gRestoreUrgent = false;
unsigned gBoostDomains = 0;
double gBoostRestoreAt = 0;
bool gBoostUrgent = false;
bool gEligible = false, gFrameSeen = false;
std::uint64_t gLevel = 0, gFrames = 0, gEntered = 0, gRecoveries = 0, gRecovered = 0, gGaveUp = 0;
double gPeriod = 0, gNativePeriod = 0, gHeartbeat = 0, gRestoreAt = 0, gNextRateQuery = 0;
float gRestoreHz = 0;
qp::Result gResult;
int gState = 0, gLayers = 0, gEndResult = 0;
std::int64_t gPrediction = 0;
std::string gSetting = "auto", gLastAction = "none";
bool gAwaitingOutcome = false;

double Now() {
    LARGE_INTEGER q{}, f{};
    QueryPerformanceCounter(&q);
    QueryPerformanceFrequency(&f);
    return static_cast<double>(q.QuadPart) / static_cast<double>(f.QuadPart);
}
std::uint64_t Cycles() {
    ULONG64 cycles = 0;
    QueryThreadCycleTime(GetCurrentThread(), &cycles);
    return cycles;
}
template<class T> T Proc(char const* name) {
    PFN_xrVoidFunction p = nullptr;
    if (gGetProc && gInstance && XR_SUCCEEDED(gGetProc(gInstance, name, &p))) return reinterpret_cast<T>(p);
    return nullptr;
}
void Read(double now) {
    if (now < gNextRead) return;
    gNextRead = now + 1;
    auto const file = ini::Read(std::filesystem::path(MELEVR::MevrBridge::SettingsPath())).file;
    gSetting = ini::Lower(ini::Trim(ini::Get(file, "Performance", "pacingRecovery").value_or("auto")));
    gActions = qp::Actions(gSetting, colour::StreamedHeadset());
    gMaxTries = static_cast<unsigned>(std::clamp(ini::GetNumber(file, "Performance", "pacingRecoveryTries").value_or(3), 0.0, 16.0));
    bool const full = ini::Lower(ini::Trim(ini::Get(file, "Performance", "frameLoop").value_or("halcyon"))) == "full";
    if (full != gFull) log::Info("quest pacing: frameLoop = {} (applied at the next frame boundary)", full ? "full" : "halcyon");
    gFull = full;
    static std::string loggedSetting;
    std::string const effective = std::format("{}:{}:{}", gSetting, gActions.size(), gMaxTries);
    if (effective != loggedSetting) {
        loggedSetting = effective;
        log::Info("quest pacing: pacingRecovery={} effectiveActions={} maxTriesPerLevel={} (10 s cooldown)", gSetting, gActions.size(), gMaxTries);
    }
    static double lastRequest = 0;
    double const request = ini::GetNumber(file, "Debug", "pacingRecoveryRequest").value_or(0);
    if (request > 0 && request != lastRequest) {
        gForce = true;
        log::Info("quest pacing: debug force request {} (eligibility bypass; real extension checks still apply)", request);
    }
    lastRequest = request;
    bool const measure = !gActions.empty() || gForce;
    if (measure != gMeasure) {
        gMeasure = measure;
        gDetector.Suspend();
        gCycles = gTsc = gExcluded = 0;
        gLastPresent = 0;
    }
}
void Restore(double now, bool immediate = false) {
    if (!gSession) return; // no proc resolution against a stopped/destroyed session's instance
    if (gBoostDomains != 0) {
        if (immediate && !gBoostUrgent) { gBoostRestoreAt = now; gBoostUrgent = true; }
        if (now >= gBoostRestoreAt) {
            auto perf = gPerfEnabled ? Proc<PFN_xrPerfSettingsSetPerformanceLevelEXT>("xrPerfSettingsSetPerformanceLevelEXT") : nullptr;
            if (perf && gSession) {
                for (unsigned bit : {1u, 2u}) {
                    if ((gBoostDomains & bit) == 0) continue;
                    auto const result = perf(gSession, bit == 1 ? XR_PERF_SETTINGS_DOMAIN_CPU_EXT : XR_PERF_SETTINGS_DOMAIN_GPU_EXT,
                        XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
                    log::Info("quest pacing: performance restore SUSTAINED_HIGH {}: {}", bit == 1 ? "CPU" : "GPU", static_cast<int>(result));
                    if (XR_SUCCEEDED(result)) gBoostDomains &= ~bit;
                }
                gBoostRestoreAt = now + 1;
            }
        }
    }
    if (gRestoreHz == 0) return;
    if (immediate && !gRestoreUrgent) { gRestoreAt = now; gRestoreUrgent = true; }
    if (now < gRestoreAt) return;
    auto request = Proc<PFN_xrRequestDisplayRefreshRateFB>("xrRequestDisplayRefreshRateFB");
    auto get = Proc<PFN_xrGetDisplayRefreshRateFB>("xrGetDisplayRefreshRateFB");
    if (!request || !get || !gSession) return;
    float actual = 0;
    XrResult const result = request(gSession, gRestoreHz);
    XrResult const queried = get(gSession, &actual);
    log::Info("quest pacing: refresh-cycle restore {} Hz: request={} query={} actual={}", gRestoreHz, static_cast<int>(result), static_cast<int>(queried), actual);
    if (XR_SUCCEEDED(queried) && std::abs(actual - gRestoreHz) < .1f) gRestoreHz = 0;
    else gRestoreAt = now + 1; // asynchronous request: retain until the runtime confirms the original rate
}
void Act(qp::Action action, double now) {
    gLastAction = qp::Name(action);
    gAwaitingOutcome = false;
    if (!gSession) { log::Info("quest pacing: {} unavailable (no running XR session)", gLastAction); return; }
    if (action == qp::Action::Performance) {
        auto perf = gPerfEnabled ? Proc<PFN_xrPerfSettingsSetPerformanceLevelEXT>("xrPerfSettingsSetPerformanceLevelEXT") : nullptr;
        if (!perf) { log::Info("quest pacing: performance unavailable (XR_EXT_performance_settings not enabled)"); return; }
        // A short session-local hint pulse; HIGH is the specified default. No persistent runtime setting.
        auto const cpu = perf(gSession, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_BOOST_EXT);
        auto const gpu = perf(gSession, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_BOOST_EXT);
        gBoostDomains = (XR_SUCCEEDED(cpu) ? 1u : 0u) | (XR_SUCCEEDED(gpu) ? 2u : 0u);
        gAwaitingOutcome = gBoostDomains != 0;
        gBoostRestoreAt = now + .5; gBoostUrgent = false;
        log::Info("quest pacing: performance BOOST for 0.5 s: cpu={} gpu={} (cadence result pending)", static_cast<int>(cpu), static_cast<int>(gpu));
        return;
    }
    if (!gRefreshEnabled) { log::Info("quest pacing: {} unavailable (XR_FB_display_refresh_rate not enabled)", gLastAction); return; }
    auto get = Proc<PFN_xrGetDisplayRefreshRateFB>("xrGetDisplayRefreshRateFB");
    auto enumerate = Proc<PFN_xrEnumerateDisplayRefreshRatesFB>("xrEnumerateDisplayRefreshRatesFB");
    auto request = Proc<PFN_xrRequestDisplayRefreshRateFB>("xrRequestDisplayRefreshRateFB");
    if (!get || !enumerate || !request) { log::Info("quest pacing: {} unavailable (refresh functions not enabled/resolved)", gLastAction); return; }
    float current = 0;
    uint32_t count = 0;
    XrResult const queried = get(gSession, &current);
    XrResult listed = enumerate(gSession, 0, &count, nullptr);
    if (XR_FAILED(queried) || XR_FAILED(listed) || count == 0 || count > 64) {
        log::Info("quest pacing: {} failed: get={} enumerate={} count={}", gLastAction, static_cast<int>(queried), static_cast<int>(listed), count); return;
    }
    std::vector<float> rates(count);
    listed = enumerate(gSession, count, &count, rates.data());
    if (XR_FAILED(listed) || count > rates.size()) { log::Info("quest pacing: enumerate rates failed {}", static_cast<int>(listed)); return; }
    rates.resize(count);
    // Re-request only enumerated rates. Zero is not a guessed recovery frequency.
    if (std::find(rates.begin(), rates.end(), current) == rates.end()) {
        log::Info("quest pacing: current rate {} not enumerated; refusing request", current); return;
    }
    float target = current;
    if (action == qp::Action::RefreshCycle) {
        float distance = 1e6f;
        for (float rate : rates) {
            if (std::isfinite(rate) && rate > 20 && rate < 1000 && rate != current && std::abs(rate - current) < distance) {
                target = rate; distance = std::abs(rate - current);
            }
        }
        if (target == current) { log::Info("quest pacing: refresh-cycle unavailable (only one listed rate)"); return; }
    }
    XrResult const result = request(gSession, target);
    gAwaitingOutcome = XR_SUCCEEDED(result);
    log::Info("quest pacing: {} request {} -> {} Hz: {} (cadence result pending)", gLastAction, current, target, static_cast<int>(result));
    if (XR_SUCCEEDED(result) && target != current) { gRestoreHz = current; gRestoreAt = now + .5; gRestoreUrgent = false; }
}
} // namespace
RuntimeCall::RuntimeCall() : begin_(gMeasure ? Cycles() : 0) {}
RuntimeCall::~RuntimeCall() { if (begin_ != 0) gExcluded += Cycles() - begin_; }
bool NextFramePhase() { return gNext; }
void SetNextFramePhase(bool next) { gNext = next; }
void PresentBegin() {
    double const now = Now();
    gFrameSeen = false;
    Read(now);
    if (!gMeasure) { gLastPresent = now; gInterval = gWork = 0; return; }
    auto const cycles = Cycles();
    auto const tsc = __rdtsc();
    gInterval = gLastPresent > 0 ? now - gLastPresent : 0;
    // Same cycle-clock estimate as the perf probe. Sleeps are absent from QueryThreadCycleTime.
    if (gInterval > 0 && tsc > gTsc) {
        double const hz = static_cast<double>(tsc - gTsc) / gInterval;
        gCycleHz = gCycleHz == 0 ? hz : .95 * gCycleHz + .05 * hz;
    }
    auto const used = cycles >= gCycles ? cycles - gCycles : 0;
    gWork = gCycleHz > 0 ? static_cast<double>(used > gExcluded ? used - gExcluded : 0) / gCycleHz : 0;
    gExcluded = 0; gCycles = cycles; gTsc = tsc; gLastPresent = now;
}
void PresentEnd() {
    double const now = Now();
    bool const eligible = gFrameSeen && gEligible;
    Restore(now, !eligible || gActions.empty());
    ++gFrames;
    unsigned const budget = gActions.empty() && !gForce ? 0 : gMaxTries;
    auto const gpu = perf::LatestGpuTiming(); // only already-completed probe results; no GPU query or clock sync here
    gResult = gDetector.Step({gLastPresent, gInterval, gWork, gPeriod,
        eligible && !gActions.empty() && gRestoreHz == 0 && gBoostDomains == 0, gLevel, gpu.work, gLastPresent - gpu.sampledAt},
        budget, gForce);
    switch (gResult.event) {
    case qp::Event::Entering:
        ++gEntered;
        log::Info("quest pacing: entering Stuck rate={:.3f} workP90={:.3f} periods snapshot={}", gResult.rate, gResult.workP90, StatusJson());
        break;
    case qp::Event::Recovering:
        ++gRecoveries;
        gForce = false;
        if (gAwaitingOutcome) log::Info("quest pacing: {} did not restore cadence (rate={:.3f}); continuing", gLastAction, gResult.rate);
        log::Info("quest pacing: recovering try {}/{} after {:.2f} s", gResult.tries, budget, gResult.elapsed);
        if (!gActions.empty()) Act(gActions[(gResult.tries - 1) % gActions.size()], now);
        else Act(qp::Action::Refresh, now); // explicit Debug hook also exercises the no-headset refusal
        break;
    case qp::Event::Recovered:
        ++gRecovered;
        gAwaitingOutcome = false;
        log::Info("quest pacing: recovered after {:.2f} s; last action={} rate={:.3f}", gResult.elapsed, gLastAction, gResult.rate);
        break;
    case qp::Event::GaveUp:
        ++gGaveUp; gForce = false;
        gAwaitingOutcome = false;
        log::Info("quest pacing: gave up after {} tries this level; last action={} snapshot={}", gResult.tries, gLastAction, StatusJson());
        break;
    default: break;
    }
    if (now >= gHeartbeat) {
        gHeartbeat = now + 10;
        log::Info("quest pacing: heartbeat frames={} entering={} recovering={} recovered={} gaveUp={} snapshot={}",
            gFrames, gEntered, gRecoveries, gRecovered, gGaveUp, StatusJson());
    }
    // gFrameSeen guards Presents without RunFrame; keep the last eligibility for snapshot readers.
}
std::string StatusJson() {
    return std::format("{{\"setting\":{},\"full\":{},\"eligible\":{},\"state\":{},\"level\":{},\"periodMs\":{:.3f},"
        "\"intervalMs\":{:.3f},\"workMs\":{:.3f},\"rate\":{:.3f},\"workP90Periods\":{:.3f},\"tries\":{},"
        "\"prediction\":{},\"layers\":{},\"endResult\":{},\"restoreHz\":{},\"boostDomains\":{},\"recoveries\":{}}}",
        vr::test_request::JsonString(gSetting), gFull, gEligible, gState, gLevel, gPeriod * 1000, gInterval * 1000, gWork * 1000,
        gResult.rate, gResult.workP90, gResult.tries, gPrediction, gLayers, gEndResult, gRestoreHz, gBoostDomains, gRecoveries);
}
} // namespace mevr::host::quest_pacing

namespace MELEVR::MevrBridge {
void QuestPacingInit(void* instance, void* getProc, bool refreshEnabled, bool performanceEnabled) noexcept {
    using namespace mevr::host::quest_pacing;
    gInstance = reinterpret_cast<XrInstance>(instance);
    gGetProc = reinterpret_cast<PFN_xrGetInstanceProcAddr>(getProc);
    gPerfEnabled = performanceEnabled;
    gRefreshEnabled = refreshEnabled;
}
bool FullFrameLoop() noexcept {
    mevr::host::quest_pacing::Read(mevr::host::quest_pacing::Now());
    return mevr::host::quest_pacing::gFull;
}
bool QuestPacingEnabled() noexcept { return mevr::host::quest_pacing::gMeasure; }
void QuestPacingNextFrame(bool next) noexcept { mevr::host::quest_pacing::SetNextFramePhase(next); }
void QuestPacingFrame(void* session, int state, bool capped, std::uint64_t level,
    std::int64_t period, bool shouldRender, std::int64_t prediction, unsigned layers, int result) noexcept {
    using namespace mevr::host::quest_pacing;
    gSession = reinterpret_cast<XrSession>(session);
    gFrameSeen = true;
    mevr::host::perf::FrameSubmitted(period, shouldRender);
    gState = state; gLevel = level;
    double const predictedPeriod = static_cast<double>(period) * 1e-9;
    if (!gHaveNativeQuery && predictedPeriod > .001 && predictedPeriod < .050 && (gNativePeriod == 0 || predictedPeriod < gNativePeriod))
        gNativePeriod = predictedPeriod;
    double const now = Now();
    if (gMeasure && gRefreshEnabled && now >= gNextRateQuery) {
        gNextRateQuery = now + 1;
        auto get = Proc<PFN_xrGetDisplayRefreshRateFB>("xrGetDisplayRefreshRateFB");
        float hz = 0;
        if (get && XR_SUCCEEDED(get(gSession, &hz)) && std::isfinite(hz) && hz > 20 && hz < 1000) {
            gNativePeriod = 1.0 / static_cast<double>(hz);
            gHaveNativeQuery = true;
        }
    }
    gPeriod = gNativePeriod; // runtime prediction may be a throttled MULTIPLE of the real display period
    gPrediction = prediction; gLayers = static_cast<int>(layers); gEndResult = result;
    gEligible = state == XR_SESSION_STATE_FOCUSED && !capped && shouldRender && layers > 0 && XR_SUCCEEDED(result);
#if defined(MEVR_WITH_MAIN_MENU)
    gEligible = gEligible && !mevr::host::main_menu::IsOpen();
#endif
}
void QuestPacingStopped() noexcept {
    using namespace mevr::host::quest_pacing;
    Restore(Now(), true);
    gSession = XR_NULL_HANDLE;
    gEligible = false;
    gFrameSeen = false;
    gDetector.Suspend();
    gNativePeriod = 0;
    gHaveNativeQuery = false;
}
} // namespace MELEVR::MevrBridge
