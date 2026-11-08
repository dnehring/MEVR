// SPDX-License-Identifier: MIT
// Exercise the actual host dispatcher against a fake XR function table, with no game or headset.
#include "plugin/QuestPacing.hpp"
#include "plugin/PerfProbe.hpp"
#include "plugin/Colour.hpp"
#include "halcyon/le1/mevr_bridge.h"
#include "tests/TestTemp.hpp"
#include <openxr/openxr.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
int gFailures = 0;
std::wstring gSettings;
std::string gLogs;
float gHz = 72;
std::vector<float> gRates{72, 80}, gRequests;
std::vector<XrPerfSettingsLevelEXT> gHints;
bool gReject = false;
void Check(bool ok, int line) {
    if (!ok) { std::printf("FAIL QuestPacingHostTests.cpp:%d\n", line); ++gFailures; }
}
#define CHECK(x) Check((x), __LINE__)
XrResult XRAPI_CALL GetRate(XrSession, float* hz) { *hz = gHz; return XR_SUCCESS; }
XrResult XRAPI_CALL Rates(XrSession, uint32_t capacity, uint32_t* count, float* rates) {
    *count = static_cast<uint32_t>(gRates.size());
    if (capacity != 0) {
        if (capacity < gRates.size()) return XR_ERROR_SIZE_INSUFFICIENT;
        for (std::size_t i = 0; i < gRates.size(); ++i) rates[i] = gRates[i];
    }
    return XR_SUCCESS;
}
XrResult XRAPI_CALL Request(XrSession, float hz) {
    gRequests.push_back(hz);
    if (gReject) return XR_ERROR_DISPLAY_REFRESH_RATE_UNSUPPORTED_FB;
    gHz = hz;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL Performance(XrSession, XrPerfSettingsDomainEXT, XrPerfSettingsLevelEXT hint) {
    gHints.push_back(hint);
    return XR_SUCCESS;
}
XrResult XRAPI_CALL Resolve(XrInstance, char const* name, PFN_xrVoidFunction* fn) {
    if (std::strcmp(name, "xrGetDisplayRefreshRateFB") == 0) *fn = reinterpret_cast<PFN_xrVoidFunction>(GetRate);
    else if (std::strcmp(name, "xrEnumerateDisplayRefreshRatesFB") == 0) *fn = reinterpret_cast<PFN_xrVoidFunction>(Rates);
    else if (std::strcmp(name, "xrRequestDisplayRefreshRateFB") == 0) *fn = reinterpret_cast<PFN_xrVoidFunction>(Request);
    else if (std::strcmp(name, "xrPerfSettingsSetPerformanceLevelEXT") == 0) *fn = reinterpret_cast<PFN_xrVoidFunction>(Performance);
    else { *fn = nullptr; return XR_ERROR_FUNCTION_UNSUPPORTED; }
    return XR_SUCCESS;
}
void Frame(bool session) {
    mevr::host::quest_pacing::PresentBegin();
    if (session) MELEVR::MevrBridge::QuestPacingFrame(reinterpret_cast<void*>(1), XR_SESSION_STATE_FOCUSED,
        false, 1, 13'888'889, true, 100, 1, XR_SUCCESS);
    mevr::host::quest_pacing::PresentEnd();
}
} // namespace

// Host dependencies: settings, headset classifier, logging and the idle perf probe.
namespace MELEVR::MevrBridge { std::wstring const& SettingsPath() noexcept { return gSettings; } }
namespace mevr::host::colour { bool StreamedHeadset() { return false; } MenuState GetMenuState() { return {}; } }
namespace mevr::host::perf {
void FrameSubmitted(std::int64_t, bool) {}
GpuTiming LatestGpuTiming() { return {}; }
}
namespace mevr::log { void Write(std::string_view, std::string_view message) { gLogs += message; gLogs += '\n'; } }

int main(int argc, char** argv) {
    std::string const test = argc > 1 ? argv[1] : "no-headset";
    auto const path = mevr::tests::TempRoot() / "pacing.ini";
    gSettings = path.wstring();
    bool const performance = test == "performance" || test == "perf-unsupported";
    bool const cycle = test == "cycle" || test == "single-rate" || test == "rejected" || test == "stop-restore";
    bool const automatic = test == "psvr2-auto";
    {
        std::ofstream file(path);
        file << "[Performance]\npacingRecovery=" << (automatic ? "auto" : performance ? "performance" : cycle ? "refresh-cycle" : "refresh")
             << "\nframeLoop=halcyon\n[Debug]\npacingRecoveryRequest=" << (automatic ? 0 : 1) << '\n';
    }
    bool const session = test != "no-headset";
    bool const refreshEnabled = test != "unsupported";
    MELEVR::MevrBridge::QuestPacingInit(reinterpret_cast<void*>(1), reinterpret_cast<void*>(Resolve), refreshEnabled,
        performance && test != "perf-unsupported");
    if (test == "single-rate") gRates = {72};
    if (test == "rejected") gReject = true;
    Frame(session); Frame(session);
    CHECK(!MELEVR::MevrBridge::FullFrameLoop());
    if (automatic) {
        CHECK(gRequests.empty() && gHints.empty());
        CHECK(!MELEVR::MevrBridge::QuestPacingEnabled());
    } else {
        CHECK(gLogs.find("entering Stuck") != std::string::npos);
        CHECK(gLogs.find("recovering try 1/3") != std::string::npos);
    }
    if (test == "no-headset") CHECK(gLogs.find("no running XR session") != std::string::npos);
    if (test == "unsupported" || test == "single-rate" || test == "perf-unsupported" || test == "no-headset") {
        CHECK(gRequests.empty() && gHints.empty());
        CHECK(gLogs.find("unavailable") != std::string::npos);
    }
    if (test == "refresh") CHECK(gRequests == std::vector<float>({72}));
    if (cycle) {
        if (test == "single-rate") CHECK(gHz == 72);
        else {
            CHECK(gRequests == std::vector<float>({80}));
            if (test == "rejected") CHECK(gHz == 72);
            else {
                CHECK(gHz == 80);
                if (test == "stop-restore") MELEVR::MevrBridge::QuestPacingStopped();
                else { Sleep(550); Frame(session); }
                CHECK(gHz == 72 && gRequests == std::vector<float>({80, 72}));
                CHECK(mevr::host::quest_pacing::StatusJson().find("\"restoreHz\":0") != std::string::npos);
            }
        }
    }
    if (test == "performance") {
        CHECK(gHints == std::vector<XrPerfSettingsLevelEXT>({XR_PERF_SETTINGS_LEVEL_BOOST_EXT, XR_PERF_SETTINGS_LEVEL_BOOST_EXT}));
        Sleep(550); Frame(session);
        CHECK(gHints.size() == 4 && gHints[2] == XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT && gHints[3] == XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
    }
    mevr::tests::RemoveTempRoot();
    if (gFailures == 0) std::printf("quest pacing host: %s passed\n", test.c_str());
    return gFailures == 0 ? 0 : 1;
}
