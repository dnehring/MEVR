// SPDX-License-Identifier: MIT
// Our "VR hands" tab in Halcyon's menu (src/halcyon/le1/vr_menu.cpp calls BuildMenuTabs inside its
// tab bar). The menu lives in the host and the arm IK in the hot-reloaded mod, so they talk through
// the mod's settings file, <GAME>MEVR.ini next to the host ASI, which the mod re-reads within a
// second of a change (src/game/HandCalibration.cpp):
//
//   [Hands] handPitch, handYaw, handRoll   degrees; this tab shows and edits them.
//
//   [Arms]  measureRequest                 a counter: this tab adds 1 each time the player asks for
//                                          an arm-length measurement and has taken the sample below.
//           measureSpanM                   distance between the two grips at the sample, meters
//           measureLeftX/Y/Z, measureRightX/Y/Z, measureHeadX/Y/Z
//                                          the grip and head positions at the sample, meters, in
//                                          the tracking space the controllers are published in
//                                          (OpenXR: +Y up, -Z forward)
//           armLengthM                     shown here if present: the mod's result (the arm-length
//                                          calibration owns this key and how it's computed).
//
//   [Squad] style, wheelStick              squad weapons: the game's own wheel (native) or our menu
//                                          (custom), and the stick that steers the game's wheel.
//
//   [Sights] pistol, shotgun, assaultRifle, sniperRifle, source
//                                          gun sights: none | dot | holo | scope <zoom> per weapon type,
//                                          and which eye's picture a scope magnifies (research/21).
//
// The sample is taken in the T pose: both arms straight out to the sides, either trigger pulled.

#include "halcyon/le1/mevr_bridge.h"
#include "plugin/Calibration.hpp"
#include "plugin/Colour.hpp"
#include "plugin/MevrMenu.hpp"
#include "plugin/PerfProbe.hpp"
#include "plugin/TestLoop.hpp"
#include "halcyon/le1/vr_config.h"
#include "halcyon/le1/vr_menu.h"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "engine/SettingsFile.hpp"

#include "imgui.h"

#include <Windows.h>

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <format>
#include <optional>
#include <string>

namespace MELEVR::MevrBridge {
namespace {

namespace controllers = mevr::vr::controllers;

struct Trims {
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
};

FILETIME gStamp{};
bool gLoaded = false;
std::uint64_t gNextCheckMs = 0;
Trims gTrims;
bool gHaveArmLength = false;
float gArmLength = 0.0f;
long gMeasureRequests = 0;
// [Camera]: where Halcyon's first-person camera sits from Shepard's head bone at the recenter
// point (written by the mod, game/HandCalibration WriteCameraOffset), and life-size movement.
bool gHaveCamera = false;
float gCamera[3] = {0.0f, 0.0f, 0.0f};  // forward, right, up (units)
float gCameraAppliedAt[3] = {1e9f, 1e9f, 1e9f};  // the measurement the last alignment used
// Life-size movement is Halcyon's lean gain of 2 in the live profile (since 2026-10-26; [Camera] lifeSize, a separate
// flag that could disagree with the profile, is no longer read).
bool LifeSize() noexcept {
    return std::fabs(MELEVR::Config::Get().leanGain - 2.0f) < 0.01f;
}
// The left-trigger alignment: armed from the tab (the menu closes, since the view isn't tracked
// while it's open); the player stands naturally and pulls the left trigger; it recenters, waits
// for the mod's next measurement, and moves the camera.
enum class Align { Idle, WaitRelease, Armed, Settling };
std::atomic<Align> gAlign{Align::Idle};
std::uint64_t gAlignSinceMs = 0;
constexpr std::uint64_t kAlignArmedTimeoutMs = 60000;
constexpr std::uint64_t kAlignSettleMs = 6000;  // the mod re-measures every 5 s
std::string gAlignResult;

// Arm-length sample: waiting for the T pose and a trigger pull.
enum class Measure { Idle, WaitRelease, WaitPull };
Measure gMeasure = Measure::Idle;
std::uint64_t gMeasureSinceMs = 0;
constexpr std::uint64_t kMeasureTimeoutMs = 30000;
std::string gMeasureResult;

std::wstring const& Path() {
    // <exe dir>\ASI\LE1MEVR.ini, the file the mod reads (engine/SettingsFile), not <host ASI name>.ini: a renamed ASI
    // kept the host and the mod on two different files. A function-local static: the game thread and the Present
    // thread both ask first.
    static std::wstring const path = mevr::engine::SettingsFile().wstring();
    return path;
}

bool ReadNumber(wchar_t const* section, wchar_t const* key, float& out) {
    wchar_t text[64]{};
    GetPrivateProfileStringW(section, key, L"", text, 64, Path().c_str());
    if (text[0] == L'\0') {
        return false;
    }
    std::string narrow;
    for (wchar_t const* c = text; *c != L'\0'; ++c) {
        narrow.push_back(*c < 128 ? static_cast<char>(*c) : '?');
    }
    std::optional<double> const value = mevr::ini::Number(narrow);  // locale-independent
    if (!value) {
        return false;
    }
    out = static_cast<float>(*value);
    return true;
}

// `decimals` places, locale-independent (std::to_chars).
void WriteNumber(wchar_t const* section, wchar_t const* key, double value, int decimals = 1) {
    std::string const text = mevr::ini::Fixed(value, decimals);
    WritePrivateProfileStringW(section, key, std::wstring(text.begin(), text.end()).c_str(), Path().c_str());
}

// Re-reads the file when it changed (by us, by the mod writing its defaults, or by hand).
void Reload(bool force) {
    std::uint64_t const now = GetTickCount64();
    if (!force && now < gNextCheckMs) {
        return;
    }
    gNextCheckMs = now + 1000;
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (Path().empty() || !GetFileAttributesExW(Path().c_str(), GetFileExInfoStandard, &data)) {
        return;  // not written yet: the mod writes its defaults on startup
    }
    if (gLoaded && CompareFileTime(&data.ftLastWriteTime, &gStamp) == 0) {
        return;
    }
    gStamp = data.ftLastWriteTime;
    gLoaded = true;
    Trims trims;
    ReadNumber(L"Hands", L"handPitch", trims.pitch);
    ReadNumber(L"Hands", L"handYaw", trims.yaw);
    ReadNumber(L"Hands", L"handRoll", trims.roll);
    gTrims = trims;
    gHaveArmLength = ReadNumber(L"Arms", L"armLengthM", gArmLength);
    float requests = 0.0f;
    gMeasureRequests = ReadNumber(L"Arms", L"measureRequest", requests) ? std::lround(requests) : 0;
    gHaveCamera = ReadNumber(L"Camera", L"measuredForward", gCamera[0]) && ReadNumber(L"Camera", L"measuredRight", gCamera[1]) &&
                  ReadNumber(L"Camera", L"measuredUp", gCamera[2]);
}

void WriteTrims(char const* why) {
    WriteNumber(L"Hands", L"handPitch", gTrims.pitch);
    WriteNumber(L"Hands", L"handYaw", gTrims.yaw);
    WriteNumber(L"Hands", L"handRoll", gTrims.roll);
    mevr::log::Info("menu: hand trims pitch {:.1f}, yaw {:.1f}, roll {:.1f} degrees ({})", gTrims.pitch, gTrims.yaw,
        gTrims.roll, why);
    Reload(true);
}

float Wrap(float degrees) {
    while (degrees > 180.0f) degrees -= 360.0f;
    while (degrees < -180.0f) degrees += 360.0f;
    return degrees;
}

// One trim: big step buttons (easy to hit with the pointer) around a slider.
void TrimRow(char const* label, char const* id, float& value) {
    ImGui::PushID(id);
    ImGui::TextUnformatted(label);
    bool changed = false;
    for (float const step : {-10.0f, -1.0f}) {
        char text[16];
        std::snprintf(text, sizeof text, "%+.0f", step);
        if (ImGui::Button(text)) {
            value = Wrap(value + step);
            changed = true;
        }
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    ImGui::SliderFloat("##value", &value, -180.0f, 180.0f, "%.1f deg");
    bool const dragged = ImGui::IsItemDeactivatedAfterEdit();
    for (float const step : {1.0f, 10.0f}) {
        ImGui::SameLine();
        char text[16];
        std::snprintf(text, sizeof text, "%+.0f", step);
        if (ImGui::Button(text)) {
            value = Wrap(value + step);
            changed = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("0")) {
        value = 0.0f;
        changed = true;
    }
    if (changed || dragged) {
        WriteTrims(label);
    }
    ImGui::PopID();
}

void WriteArmSample(XrVector3f const& l, XrVector3f const& r, XrVector3f const* head, char const* why) {
    Reload(true);  // the latest request counter
    float const dx = r.x - l.x, dy = r.y - l.y, dz = r.z - l.z;
    float const span = std::sqrt(dx * dx + dy * dy + dz * dz);
    WriteNumber(L"Arms", L"measureLeftX", l.x, 4);
    WriteNumber(L"Arms", L"measureLeftY", l.y, 4);
    WriteNumber(L"Arms", L"measureLeftZ", l.z, 4);
    WriteNumber(L"Arms", L"measureRightX", r.x, 4);
    WriteNumber(L"Arms", L"measureRightY", r.y, 4);
    WriteNumber(L"Arms", L"measureRightZ", r.z, 4);
    if (head != nullptr) {
        WriteNumber(L"Arms", L"measureHeadX", head->x, 4);
        WriteNumber(L"Arms", L"measureHeadY", head->y, 4);
        WriteNumber(L"Arms", L"measureHeadZ", head->z, 4);
    }
    WriteNumber(L"Arms", L"measureSpanM", span, 4);
    // The counter last, so a reader that sees it change finds the sample already written.
    WriteNumber(L"Arms", L"measureRequest", static_cast<double>(gMeasureRequests + 1), 0);
    char text[128];
    std::snprintf(text, sizeof text, "Sample %ld taken: %.2f m between the controllers.", gMeasureRequests + 1, span);
    gMeasureResult = text;
    mevr::log::Info("menu: arm-length sample {} ({}): grips {:.2f} m apart, left ({:.2f}, {:.2f}, {:.2f}), right ({:.2f}, "
                    "{:.2f}, {:.2f}), head {}",
        gMeasureRequests + 1, why, span, l.x, l.y, l.z, r.x, r.y, r.z, head != nullptr ? "recorded" : "unknown");
    Reload(true);
}

void TakeSample(controllers::State const& state) {
    XrPosef head{};
    bool const haveHead = LatestMenuHead(head, 1000);
    WriteArmSample(state.left.grip.position, state.right.grip.position, haveHead ? &head.position : nullptr, "the T-pose button");
}

void MeasureSection() {
    ImGui::SeparatorText("Arm length");
    if (gHaveArmLength) {
        ImGui::Text("Arm length: %.2f m", gArmLength);
    } else {
        ImGui::TextDisabled("No arm length measured yet.");
    }
    controllers::State const state = controllers::Latest();
    bool const anyTrigger = state.left.trigger > 0.6f || state.right.trigger > 0.6f;
    bool const noTrigger = state.left.trigger < 0.4f && state.right.trigger < 0.4f;
    std::uint64_t const now = GetTickCount64();
    if (gMeasure != Measure::Idle && now - gMeasureSinceMs > kMeasureTimeoutMs) {
        gMeasure = Measure::Idle;
        gMeasureResult = "Timed out: no trigger pull within 30 seconds.";
    }
    switch (gMeasure) {
    case Measure::Idle:
        if (ImGui::Button("Measure arm length")) {
            gMeasure = Measure::WaitRelease;  // the click itself may be a trigger pull
            gMeasureSinceMs = now;
            gMeasureResult.clear();
        }
        ImGui::TextDisabled("Hold your arms straight out to the sides, then press a trigger.");
        break;
    case Measure::WaitRelease:
    case Measure::WaitPull:
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
            "Hold both arms straight out to the sides (a T) and pull either trigger.");
        if (!state.active || !state.left.gripValid || !state.right.gripValid) {
            ImGui::TextDisabled("Waiting for both controllers to be tracked...");
        }
        if (gMeasure == Measure::WaitRelease && noTrigger) {
            gMeasure = Measure::WaitPull;
        } else if (gMeasure == Measure::WaitPull && anyTrigger && state.active && state.left.gripValid &&
                   state.right.gripValid) {
            TakeSample(state);
            gMeasure = Measure::Idle;
        }
        if (gMeasure != Measure::Idle && ImGui::Button("Cancel")) {
            gMeasure = Measure::Idle;
        }
        break;
    }
    if (!gMeasureResult.empty()) {
        ImGui::TextUnformatted(gMeasureResult.c_str());
    }
}

// Halcyon's view offsets are metres at 50 units each (xr_session kMetersToUU).
constexpr float kUnitsPerOffsetMeter = 50.0f;

// Moves Halcyon's camera offsets by the latest measurement (which includes the current offsets,
// so this adds the rest) and saves the profile.
void ApplyCameraAlignment(char const* why) {
    MELEVR::Config::VrConfig& c = MELEVR::Config::Get();
    c.screenDistanceEnabled = true;
    c.cameraOffsetEnabled = true;
    c.flatScreenDistance += gCamera[0] / kUnitsPerOffsetMeter;  // + moves the view back
    c.cameraOffsetRight -= gCamera[1] / kUnitsPerOffsetMeter;
    c.cameraOffsetUp -= gCamera[2] / kUnitsPerOffsetMeter;
    for (int i = 0; i < 3; ++i) {
        gCameraAppliedAt[i] = gCamera[i];
    }
    MELEVR::Config::SaveProfile(MELEVR::Config::ActiveProfileIndex());
    mevr::log::Info("menu: camera to Shepard's head ({}): measured {:.1f}/{:.1f}/{:.1f} units; Halcyon's offsets now back "
                    "{:.3f} m, right {:.3f} m, up {:.3f} m", why, gCamera[0], gCamera[1], gCamera[2], c.flatScreenDistance,
        c.cameraOffsetRight, c.cameraOffsetUp);
}

void CameraSection() {
    ImGui::SeparatorText("First-person camera");
    MELEVR::Config::VrConfig& c = MELEVR::Config::Get();
    if (gHaveCamera) {
        ImGui::Text("The camera is %.0f forward, %.0f right, %.0f up (units) from Shepard's head.", gCamera[0], gCamera[1],
            gCamera[2]);
    } else {
        ImGui::TextDisabled("No measurement yet: stand in first person (K) for a few seconds.");
    }
    bool const fresh = gHaveCamera && (std::abs(gCamera[0] - gCameraAppliedAt[0]) > 0.5f ||
                                       std::abs(gCamera[1] - gCameraAppliedAt[1]) > 0.5f ||
                                       std::abs(gCamera[2] - gCameraAppliedAt[2]) > 0.5f);
    ImGui::BeginDisabled(!fresh);
    if (ImGui::Button("Put the camera at Shepard's head")) {
        ApplyCameraAlignment("menu button");
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Press R first, standing naturally. Press again after a few seconds to refine.");
    if (ImGui::Button("Align with the left trigger")) {
        StartCameraAlign("the VR hands tab");
    }
    ImGui::TextDisabled("Closes the menu. Stand naturally, pull the left trigger, hold still about 6 seconds.");
    if (!gAlignResult.empty()) {
        ImGui::TextUnformatted(gAlignResult.c_str());
    }
    bool changed = false;
    bool done = false;
    changed |= ImGui::SliderFloat("Back (m)", &c.flatScreenDistance, -2.0f, 2.0f, "%.3f");
    done |= ImGui::IsItemDeactivatedAfterEdit();
    changed |= ImGui::SliderFloat("Right (m)", &c.cameraOffsetRight, -1.0f, 1.0f, "%.3f");
    done |= ImGui::IsItemDeactivatedAfterEdit();
    changed |= ImGui::SliderFloat("Up (m)", &c.cameraOffsetUp, -1.0f, 1.0f, "%.3f");
    done |= ImGui::IsItemDeactivatedAfterEdit();
    if (changed) {
        c.screenDistanceEnabled = true;
        c.cameraOffsetEnabled = true;
    }
    if (done) {
        MELEVR::Config::SaveProfile(MELEVR::Config::ActiveProfileIndex());
    }
    bool lifeSize = LifeSize();
    if (ImGui::Checkbox("Life-size movement (crouch, lean and step 1:1 with the body)", &lifeSize)) {
        c.leanGain = lifeSize ? 2.0f : 1.0f;  // 100 units per metre, Shepard's skeleton scale
        MELEVR::Config::SaveProfile(MELEVR::Config::ActiveProfileIndex());
        mevr::log::Info("menu: life-size movement {} (lean gain {:.1f}, forward emphasis {:.0f})", lifeSize ? "on" : "off",
            c.leanGain, lifeSize ? 1.0 : 3.0);
    }
    ImGui::TextDisabled("Off: Halcyon's half-scale movement, forward tripled.");
}

} // namespace

std::wstring const& SettingsPath() noexcept {
    return Path();
}

void TakeArmSample(XrVector3f const& left, XrVector3f const& right, XrVector3f const* head, char const* why) noexcept {
    WriteArmSample(left, right, head, why != nullptr ? why : "?");
}

void StartCameraAlign(char const* why) noexcept {
    gAlign.store(Align::WaitRelease);
    gAlignSinceMs = GetTickCount64();
    gAlignResult.clear();
    MELEVR::Menu::SetOpen(false);
    mevr::log::Info("menu: camera alignment armed ({}): stand naturally and pull the left trigger", why != nullptr ? why : "?");
}

std::string CameraAlignResult() noexcept {
    return gAlignResult;
}

bool CameraAlignBusy() noexcept {
    return gAlign.load(std::memory_order_relaxed) != Align::Idle;
}

bool CameraAlignOwnsLeftTrigger() noexcept {
    Align const state = gAlign.load(std::memory_order_relaxed);
    return state == Align::WaitRelease || state == Align::Armed;
}

void TickCameraAlign() noexcept {
    Align const state = gAlign.load();
    if (state == Align::Idle) {
        return;
    }
    std::uint64_t const now = GetTickCount64();
    float const trigger = controllers::Latest().left.trigger;
    switch (state) {
    case Align::WaitRelease:
        if (trigger < 0.4f) {
            gAlign.store(Align::Armed);
        }
        break;
    case Align::Armed:
        if (now - gAlignSinceMs > kAlignArmedTimeoutMs) {
            gAlign.store(Align::Idle);
            gAlignResult = "Camera alignment timed out (no left trigger within a minute).";
            mevr::log::Info("menu: camera alignment timed out");
        } else if (trigger > 0.6f) {
            MELEVR::Menu::RequestRecenter();
            gAlignSinceMs = now;
            gAlign.store(Align::Settling);
            mevr::log::Info("menu: camera alignment: recentered; hold still while the camera is measured");
        }
        break;
    case Align::Settling:
        if (now - gAlignSinceMs >= kAlignSettleMs) {
            gAlign.store(Align::Idle);
            Reload(true);
            if (gHaveCamera) {
                ApplyCameraAlignment("left trigger");
                gAlignResult = std::format("Aligned: the camera was {:.0f} forward, {:.0f} right, {:.0f} up from Shepard's head.",
                    gCamera[0], gCamera[1], gCamera[2]);
            } else {
                gAlignResult = "No camera measurement: be in first person (K) and try again.";
                mevr::log::Info("menu: camera alignment: no measurement (not in first person?)");
            }
        }
        break;
    case Align::Idle:
        break;
    }
}

// The test loop (plugin/TestLoop): snapshots, recordings and replays, for checking VR changes
// without describing what the headset shows.
static bool gReplayWithInputs = false;

static void TestLoopSection() {
    namespace testloop = mevr::host::testloop;
    ImGui::SeparatorText("Test loop (for Claude)");
    testloop::Status const s = testloop::GetStatus();
    if (ImGui::Button("Snapshot in 3 s")) {
        testloop::SnapshotRequest request;
        request.delaySeconds = 3.0;  // time to close this menu
        request.trigger = "menu";
        testloop::RequestSnapshot(request);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("close the menu; End, or tap both stick clicks together, takes one now");
    if (ImGui::Button(s.recording ? "Stop recording" : "Start recording")) {
        testloop::RequestRecording(s.recording ? 0 : 1);
    }
    if (s.recording) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "recording %.0f s", s.recordingSeconds);
    } else {
        ImGui::SameLine();
        ImGui::TextDisabled("or Home on the keyboard");
    }
    if (s.replaying) {
        if (ImGui::Button("Stop replay")) {
            testloop::StopReplay();
        }
        ImGui::SameLine();
        ImGui::Text("replaying %.1f / %.1f s%s", s.replaySeconds, s.replayDuration, s.replayLoop ? " (loop)" : "");
    } else {
        if (ImGui::Button("Replay the last recording")) {
            std::string error;
            if (!testloop::StartReplay(L"", false, gReplayWithInputs, false, &error)) {
                mevr::log::Warn("menu: replay: {}", error);
            }
        }
        ImGui::SameLine();
        ImGui::Checkbox("with buttons and sticks", &gReplayWithInputs);
    }
    namespace perf = mevr::host::perf;
    perf::Status const p = perf::GetStatus();
    if (p.pending) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Measuring frame timing: %.0f s of 20", p.seconds);
    } else if (ImGui::Button("Measure frame timing (20 s)")) {
        perf::Request(20.0, "", "menu", 3.0);  // time to close this menu: it changes what's measured
    }
    if (!p.pending) {
        ImGui::SameLine();
        ImGui::TextDisabled("starts 3 s after the click; close the menu and play normally");
    }
    if (!s.lastSnapshot.empty()) {
        ImGui::TextDisabled("Last snapshot: %s", s.lastSnapshot.c_str());
    }
    if (!s.lastRecording.empty()) {
        ImGui::TextDisabled("Last recording: %s", s.lastRecording.c_str());
    }
    if (!p.lastCapture.empty()) {
        ImGui::TextDisabled("Last frame timing: %s", p.lastCapture.c_str());
    }
}

// The guided calibration (plugin/Calibration, research/25): start it, see what it measured for these
// controllers, the aiming eye, and whether the mod applies it ([Calibration] eye, use).
static std::string ReadText(wchar_t const* section, wchar_t const* key, wchar_t const* fallback);

static void CalibrationSection() {
    namespace calibration = mevr::host::calibration;
    ImGui::SeparatorText("Calibration (aim, hands, body)");
    calibration::Summary const s = calibration::GetSummary();
    ImGui::Text("Controllers: %s", s.device.empty() ? "not known yet (once the controllers are bound)" : s.device.c_str());
    if (s.running) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Calibrating (step: %s). Close this menu to go on.", s.step.c_str());
        if (ImGui::Button("Cancel the calibration")) {
            calibration::Cancel("the VR hands tab");
        }
    } else {
        if (s.calibrated) {
            ImGui::TextWrapped("Calibrated %s: %s", s.when.c_str(), s.results.c_str());
        } else {
            ImGui::TextDisabled("Not calibrated for these controllers: the guns follow the controller's aim pose.");
        }
        if (ImGui::Button(s.calibrated ? "Calibrate again" : "Calibrate")) {
            MELEVR::Menu::SetOpen(false);  // it runs in the world, with the menu closed
            calibration::Start("the VR hands tab");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("about a minute; closes this menu. Stand somewhere safe and roomy.");
    }
    if (Path().empty()) {
        return;
    }
    std::string const eye = ReadText(L"Calibration", L"eye", L"auto");
    ImGui::TextUnformatted("Aiming eye:");
    struct Choice {
        char const* label;
        wchar_t const* value;
        char const* key;
    };
    static constexpr Choice kEyes[] = {{"measured", L"auto", "auto"}, {"left", L"left", "left"}, {"right", L"right", "right"}};
    bool const chosen = eye == "left" || eye == "right";
    for (Choice const& choice : kEyes) {
        ImGui::SameLine();
        bool const on = chosen ? eye == choice.key : std::string(choice.key) == "auto";
        if (ImGui::RadioButton(choice.label, on) && !on) {
            WritePrivateProfileStringW(L"Calibration", L"eye", choice.value, Path().c_str());
            mevr::log::Info("menu: calibration: aiming eye {} (next calibration)", choice.key);
        }
    }
    bool use = ReadText(L"Calibration", L"use", L"1") != "0";
    if (ImGui::Checkbox("Use the calibration (off: the guns follow the controller's aim pose, as before)", &use)) {
        WritePrivateProfileStringW(L"Calibration", L"use", use ? L"1" : L"0", Path().c_str());
        mevr::log::Info("menu: calibration {}", use ? "used" : "not used");
    }
}

// Squad weapons ([Squad] style and wheelStick): the game's own wheel or our menu, for A/B in the
// headset (game/SquadWeapons re-reads the file within a second).
static std::string ReadText(wchar_t const* section, wchar_t const* key, wchar_t const* fallback) {
    wchar_t text[64]{};
    GetPrivateProfileStringW(section, key, fallback, text, 64, Path().c_str());
    std::string out;
    for (wchar_t const* c = text; *c != L'\0'; ++c) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*c < 128 ? *c : '?'))));
    }
    return out;
}

static void SquadSection() {
    ImGui::SeparatorText("Squad weapons (point, pull the left trigger)");
    if (Path().empty()) {
        return;
    }
    std::string const style = ReadText(L"Squad", L"style", L"native");
    bool const native = style != "custom";
    if (ImGui::RadioButton("The game's own weapon wheel", native) && !native) {
        WritePrivateProfileStringW(L"Squad", L"style", L"native", Path().c_str());
        mevr::log::Info("menu: squad weapons: the game's own wheel");
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Our menu", !native) && native) {
        WritePrivateProfileStringW(L"Squad", L"style", L"custom", Path().c_str());
        mevr::log::Info("menu: squad weapons: our menu");
    }
    if (native) {
        std::string const stick = ReadText(L"Squad", L"wheelStick", L"right");
        ImGui::TextUnformatted("Steer the wheel with:");
        struct Choice {
            char const* label;
            wchar_t const* value;
            char const* key;
        };
        static constexpr Choice kChoices[] = {
            {"right stick", L"right", "right"}, {"left stick", L"left", "left"}, {"either", L"either", "either"}};
        for (Choice const& choice : kChoices) {
            ImGui::SameLine();
            bool const on = stick == choice.key;
            if (ImGui::RadioButton(choice.label, on) && !on) {
                WritePrivateProfileStringW(L"Squad", L"wheelStick", choice.value, Path().c_str());
                mevr::log::Info("menu: squad weapons: the wheel is steered with {}", choice.key);
            }
        }
    }
}

// Gun sights ([Sights] <type> = none | dot | holo | scope <zoom>, and the scope's source eye), for A/B
// in the headset (game/HandCalibration re-reads the file within a second; research/21).
static void SightsSection() {
    ImGui::SeparatorText("Gun sights (look through the sight's ring; scopes at the eyepiece)");
    if (Path().empty()) {
        return;
    }
    struct Gun {
        char const* label;
        wchar_t const* key;
        wchar_t const* fallback;
    };
    static constexpr Gun kGuns[] = {{"Pistol", L"pistol", L"dot"}, {"Shotgun", L"shotgun", L"holo"},
        {"Assault rifle", L"assaultRifle", L"holo"}, {"Sniper rifle", L"sniperRifle", L"scope 4"}};
    static constexpr char const* kKinds[] = {"none", "dot", "holo", "scope"};
    for (Gun const& gun : kGuns) {
        std::string const value = ReadText(L"Sights", gun.key, gun.fallback);
        auto const space = value.find(' ');
        std::string const kind = value.substr(0, space);
        float zoom = space != std::string::npos ? static_cast<float>(mevr::ini::Number(std::string_view(value).substr(space + 1)).value_or(0.0)) : 4.0f;
        if (!(zoom >= 1.0f && zoom <= 12.0f)) {
            zoom = 4.0f;
        }
        ImGui::PushID(gun.label);
        ImGui::TextUnformatted(gun.label);
        int chosen = -1;
        for (int k = 0; k < 4; ++k) {
            ImGui::SameLine(k == 0 ? 130.0f : 0.0f);
            if (ImGui::RadioButton(kKinds[k], kind == kKinds[k]) && kind != kKinds[k]) {
                chosen = k;
            }
        }
        if (kind == "scope") {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            // Written as it moves (the slider reads its value back from the file each frame).
            float edited = zoom;
            if (ImGui::SliderFloat("zoom", &edited, 1.0f, 12.0f, "%.1fx") && std::abs(edited - zoom) >= 0.05f) {
                chosen = 3;
                zoom = edited;
            }
        }
        if (chosen >= 0) {
            std::wstring text;
            for (char const* c = kKinds[chosen]; *c != '\0'; ++c) {
                text.push_back(static_cast<wchar_t>(*c));
            }
            if (chosen == 3) {
                std::string const number = " " + mevr::ini::Fixed(zoom, 1);  // locale-independent
                text += std::wstring(number.begin(), number.end());
            }
            WritePrivateProfileStringW(L"Sights", gun.key, text.c_str(), Path().c_str());
            mevr::log::Info("menu: sights: {} = {}{}", gun.label, kKinds[chosen], chosen == 3 ? std::format(" {:.1f}x", zoom) : "");
        }
        ImGui::PopID();
    }
    std::string const source = ReadText(L"Sights", L"source", L"other");
    ImGui::TextUnformatted("A scope magnifies:");
    ImGui::SameLine();
    if (ImGui::RadioButton("the other eye's picture", source != "same") && source == "same") {
        WritePrivateProfileStringW(L"Sights", L"source", L"other", Path().c_str());
        mevr::log::Info("menu: sights: scopes magnify the other eye's picture");
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("the aiming eye's", source == "same") && source != "same") {
        WritePrivateProfileStringW(L"Sights", L"source", L"same", Path().c_str());
        mevr::log::Info("menu: sights: scopes magnify the aiming eye's own picture");
    }
}

float ForwardLeanEmphasis() noexcept {
    return LifeSize() ? 1.0f : 3.0f;
}

void BuildMenuTabs() noexcept {
    if (!ImGui::BeginTabItem("VR hands")) {
        return;
    }
    Reload(false);
    ImGui::TextWrapped("How Shepard's hands sit on the controllers, in degrees, the same for both hands "
                       "(mirrored for the left). Saved to %ls; the arms follow within a second.",
        Path().empty() ? L"LE1MEVR.ini" : Path().c_str() + (Path().find_last_of(L"\\/") + 1));
    if (!gLoaded) {
        ImGui::TextDisabled("The settings file isn't there yet (the mod writes it once it runs).");
    }
    ImGui::SeparatorText("Hand angles");
    TrimRow("Pitch (+ tilts the fingers down)", "pitch", gTrims.pitch);
    TrimRow("Yaw (+ turns the right hand's fingers outward)", "yaw", gTrims.yaw);
    TrimRow("Roll (+ rolls the right hand's thumb side outward)", "roll", gTrims.roll);
    CalibrationSection();
    MeasureSection();
    CameraSection();
    SquadSection();
    SightsSection();
    TestLoopSection();
    mevr::host::colour::MenuSection();  // plugin/Colour (research/27)
    ImGui::EndTabItem();
}

} // namespace MELEVR::MevrBridge
