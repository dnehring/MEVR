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
// The sample is taken in the T pose: both arms straight out to the sides, either trigger pulled.

#include "halcyon/le1/mevr_bridge.h"
#include "plugin/MevrMenu.hpp"
#include "halcyon/le1/vr_config.h"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"

#include "imgui.h"

#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>

namespace MELEVR::MevrBridge {
namespace {

namespace controllers = mevr::vr::controllers;

struct Trims {
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
};

std::wstring gPath;            // ...\Binaries\Win64\ASI\LE1MEVR.ini (the host ASI's name)
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
std::atomic<bool> gLifeSize{false};

// Arm-length sample: waiting for the T pose and a trigger pull.
enum class Measure { Idle, WaitRelease, WaitPull };
Measure gMeasure = Measure::Idle;
std::uint64_t gMeasureSinceMs = 0;
constexpr std::uint64_t kMeasureTimeoutMs = 30000;
std::string gMeasureResult;

std::wstring const& Path() {
    if (gPath.empty()) {
        HMODULE module = nullptr;
        wchar_t buffer[MAX_PATH]{};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&Path), &module) &&
            GetModuleFileNameW(module, buffer, MAX_PATH) != 0) {
            gPath = buffer;
            auto const dot = gPath.find_last_of(L'.');
            auto const slash = gPath.find_last_of(L"\\/");
            if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
                gPath.erase(dot);
            }
            gPath += L".ini";
        }
    }
    return gPath;
}

bool ReadNumber(wchar_t const* section, wchar_t const* key, float& out) {
    wchar_t text[64]{};
    GetPrivateProfileStringW(section, key, L"", text, 64, Path().c_str());
    if (text[0] == L'\0') {
        return false;
    }
    wchar_t* end = nullptr;
    double const value = std::wcstod(text, &end);
    if (end == text || !std::isfinite(value)) {
        return false;
    }
    out = static_cast<float>(value);
    return true;
}

void WriteNumber(wchar_t const* section, wchar_t const* key, double value, wchar_t const* format = L"%.1f") {
    wchar_t text[64]{};
    std::swprintf(text, 64, format, value);
    WritePrivateProfileStringW(section, key, text, Path().c_str());
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
    float lifeSize = 0.0f;
    gLifeSize.store(ReadNumber(L"Camera", L"lifeSize", lifeSize) && lifeSize != 0.0f);
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

void TakeSample(controllers::State const& state) {
    XrPosef head{};
    bool const haveHead = LatestMenuHead(head, 1000);
    XrVector3f const l = state.left.grip.position;
    XrVector3f const r = state.right.grip.position;
    float const dx = r.x - l.x, dy = r.y - l.y, dz = r.z - l.z;
    float const span = std::sqrt(dx * dx + dy * dy + dz * dz);
    WriteNumber(L"Arms", L"measureLeftX", l.x, L"%.4f");
    WriteNumber(L"Arms", L"measureLeftY", l.y, L"%.4f");
    WriteNumber(L"Arms", L"measureLeftZ", l.z, L"%.4f");
    WriteNumber(L"Arms", L"measureRightX", r.x, L"%.4f");
    WriteNumber(L"Arms", L"measureRightY", r.y, L"%.4f");
    WriteNumber(L"Arms", L"measureRightZ", r.z, L"%.4f");
    if (haveHead) {
        WriteNumber(L"Arms", L"measureHeadX", head.position.x, L"%.4f");
        WriteNumber(L"Arms", L"measureHeadY", head.position.y, L"%.4f");
        WriteNumber(L"Arms", L"measureHeadZ", head.position.z, L"%.4f");
    }
    WriteNumber(L"Arms", L"measureSpanM", span, L"%.4f");
    // The counter last, so a reader that sees it change finds the sample already written.
    WriteNumber(L"Arms", L"measureRequest", static_cast<double>(gMeasureRequests + 1), L"%.0f");
    char text[128];
    std::snprintf(text, sizeof text, "Sample %ld taken: %.2f m between the controllers.", gMeasureRequests + 1, span);
    gMeasureResult = text;
    mevr::log::Info("menu: arm-length sample {}: grips {:.2f} m apart, left ({:.2f}, {:.2f}, {:.2f}), right ({:.2f}, "
                    "{:.2f}, {:.2f}), head {}",
        gMeasureRequests + 1, span, l.x, l.y, l.z, r.x, r.y, r.z, haveHead ? "recorded" : "unknown");
    Reload(true);
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
        // The measurement already includes the current offsets, so this adds the rest.
        c.screenDistanceEnabled = true;
        c.cameraOffsetEnabled = true;
        c.flatScreenDistance += gCamera[0] / kUnitsPerOffsetMeter;  // + moves the view back
        c.cameraOffsetRight -= gCamera[1] / kUnitsPerOffsetMeter;
        c.cameraOffsetUp -= gCamera[2] / kUnitsPerOffsetMeter;
        for (int i = 0; i < 3; ++i) {
            gCameraAppliedAt[i] = gCamera[i];
        }
        MELEVR::Config::SaveProfile(MELEVR::Config::ActiveProfileIndex());
        mevr::log::Info("menu: camera to Shepard's head: back {:.3f} m, right {:.3f} m, up {:.3f} m (Halcyon's offsets)",
            c.flatScreenDistance, c.cameraOffsetRight, c.cameraOffsetUp);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Press R first, standing naturally. Press again after a few seconds to refine.");
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
    bool lifeSize = gLifeSize.load();
    if (ImGui::Checkbox("Life-size movement (crouch, lean and step 1:1 with the body)", &lifeSize)) {
        gLifeSize.store(lifeSize);
        c.leanGain = lifeSize ? 2.0f : 1.0f;  // 100 units per metre, Shepard's skeleton scale
        WriteNumber(L"Camera", L"lifeSize", lifeSize ? 1.0 : 0.0, L"%.0f");
        MELEVR::Config::SaveProfile(MELEVR::Config::ActiveProfileIndex());
        mevr::log::Info("menu: life-size movement {} (lean gain {:.1f}, forward emphasis {:.0f})", lifeSize ? "on" : "off",
            c.leanGain, lifeSize ? 1.0 : 3.0);
    }
    ImGui::TextDisabled("Off: Halcyon's half-scale movement, forward tripled.");
}

} // namespace

float ForwardLeanEmphasis() noexcept {
    Reload(false);  // Present thread, like the menu; once a second at most
    return gLifeSize.load(std::memory_order_relaxed) ? 1.0f : 3.0f;
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
    MeasureSection();
    CameraSection();
    ImGui::EndTabItem();
}

} // namespace MELEVR::MevrBridge
