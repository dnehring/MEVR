// Colour correction of the eyes and the game's UI (plugin/Colour.hpp, research/27): the settings, the headset,
// the corrected variants of Halcyon's eye shaders and their constants, the cost, the menu section.

#include "plugin/Colour.hpp"

#include "halcyon/le1/logger.h"
#include "halcyon/le1/mevr_bridge.h"
#include "plugin/MevrMenu.hpp"

#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"

#include "imgui.h"

#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <format>
#include <mutex>
#include <string>
#include <vector>

namespace mevr::host::colour {
namespace {

using Microsoft::WRL::ComPtr;
namespace cv = mevr::vr::colour;

// ---- What applies (Present thread writes; StatusJson may read from elsewhere) ------------------------------
std::mutex gMutex;
std::string gRuntime;
std::string gSystem;
std::uint32_t gVendor = 0;
cv::Detection gDetection = cv::Detect("", "");
std::string gKey;
std::atomic<bool> gHeadsetChanged{false};  // SetHeadset (any thread) -> Tick adopts it
// The Present thread's copy of the headset (adopted in Tick).
std::string tSystem;
std::string tKey;
cv::Detection tDetection = cv::Detect("", "");
cv::Resolved gResolved;
bool gLoaded = false;
FILETIME gStamp{};
std::uint64_t gNextCheckMs = 0;
bool gDirty = false;  // the menu's slider edits, not written yet (no re-read meanwhile)
std::atomic<bool> gNonNeutral{false};  // the profile's values aren't the identity and [Colour] compare is 0
std::atomic<bool> gCompareLatched{false};
std::atomic<std::uint64_t> gCompareUntilMs{0};
bool gComparingLogged = false;
std::string gLastLogged;

constexpr std::uint64_t kHoldLapseMs = 150;

std::wstring Wide(std::string const& text) {
    std::wstring out;
    for (char const c : text) {
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return out;
}

std::wstring SectionName() {
    return tKey.empty() ? std::wstring(L"Colour") : L"Colour." + Wide(tKey);
}

cv::Section ReadSection(std::wstring const& path, std::wstring const& name) {
    std::vector<wchar_t> buffer(16384);
    DWORD const n = GetPrivateProfileSectionW(name.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()), path.c_str());
    std::string text;
    text.reserve(n);
    for (DWORD i = 0; i < n; ++i) {
        wchar_t const c = buffer[i];
        text.push_back(c == L'\0' ? '\n' : (c < 128 ? static_cast<char>(c) : '?'));
    }
    return cv::ParseSection(text);
}

void WriteValue(wchar_t const* key, std::wstring const& value) {
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    if (!path.empty()) {
        WritePrivateProfileStringW(SectionName().c_str(), key, value.c_str(), path.c_str());
    }
}

void WriteNumber(wchar_t const* key, float value) {
    wchar_t text[32]{};
    std::swprintf(text, 32, L"%.3f", static_cast<double>(value));
    WriteValue(key, text);
}

std::string Values(cv::Settings const& s) {
    return std::format("brightness {:.2f}, contrast {:.2f}, saturation {:.2f}, mid-tones gamma {:.2f}, black {:+.3f}, "
                       "gain {:.2f}/{:.2f}/{:.2f}",
        s.brightness, s.contrast, s.saturation, s.gamma, s.black, s.gain[0], s.gain[1], s.gain[2]);
}

// After gResolved changed: what the eyes get, logged when it changes (ours and Halcyon's log).
void Publish() {
    bool const nonNeutral = !cv::IsNeutral(gResolved.settings) && !gResolved.compare;
    gNonNeutral.store(nonNeutral, std::memory_order_relaxed);
    std::string line = std::format("profile {}{} for {} [{}]: ", cv::Name(gResolved.chosen),
        gResolved.chosen == cv::Profile::Auto ? std::format(" (= {})", cv::Name(gResolved.effective)) : std::string(),
        tSystem.empty() ? std::string("a headset not known yet") : "'" + tSystem + "'",
        tKey.empty() ? std::string("Colour") : "Colour." + tKey);
    if (cv::IsNeutral(gResolved.settings)) {
        line += "neutral: no correction, the eyes and the HUD as before";
    } else {
        line += Values(gResolved.settings) + std::format(", dither {:.2f} steps", gResolved.dither) +
            (gResolved.compare ? "; [Colour] compare = 1: shown uncorrected" : ": correcting the eyes and the HUD panel");
    }
    if (line != gLastLogged) {
        gLastLogged = line;
        log::Info("colour: {}", line);
        MELEVR::Logger::LogLine("[MEVR-COLOUR] " + line);
    }
}

void Refresh(bool force) {
    std::uint64_t const now = GetTickCount64();
    if (!force && now < gNextCheckMs) {
        return;
    }
    gNextCheckMs = now + 1000;
    if (gDirty) {
        return;
    }
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    WIN32_FILE_ATTRIBUTE_DATA data{};
    bool const exists = !path.empty() && GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data);
    if (!force && gLoaded && exists && CompareFileTime(&data.ftLastWriteTime, &gStamp) == 0) {
        return;
    }
    cv::Section common;
    cv::Section headset;
    if (exists) {
        common = ReadSection(path, L"Colour");
        if (common.find("profile") == common.end()) {
            // The section the player edits by hand: written once (the mod writes the file on its first start).
            WritePrivateProfileStringW(L"Colour", L"profile", L"auto", path.c_str());
            WritePrivateProfileStringW(L"Colour", L"compare", L"0", path.c_str());
            WritePrivateProfileStringW(L"Colour", L"dither", L"0.5", path.c_str());
            common = ReadSection(path, L"Colour");
            GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data);
        }
        if (!tKey.empty()) {
            headset = ReadSection(path, SectionName());
        }
        gStamp = data.ftLastWriteTime;
    }
    {
        std::lock_guard lock(gMutex);
        gResolved = cv::Resolve(common, headset, tDetection);
        gLoaded = true;
    }
    Publish();
}

bool CompareHeld() {
    return gCompareLatched.load(std::memory_order_relaxed) ||
        GetTickCount64() < gCompareUntilMs.load(std::memory_order_relaxed);
}

// Present thread: adopt a new headset, re-read the file (once a second), log a comparison starting or ending.
void Tick() {
    if (gHeadsetChanged.exchange(false)) {
        {
            std::lock_guard lock(gMutex);
            tSystem = gSystem;
            tKey = gKey;
            tDetection = gDetection;
        }
        gDirty = false;
        Refresh(true);
    } else {
        Refresh(false);
    }
    bool const comparing = gNonNeutral.load(std::memory_order_relaxed) && CompareHeld();
    if (comparing != gComparingLogged) {
        gComparingLogged = comparing;
        log::Info("colour: {}", comparing ? "comparing: the uncorrected picture while held" : "comparison let go: corrected again");
    }
}

// ---- Shaders and constants (Present thread) ---------------------------------------------------------------
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT,
    UINT, ID3DBlob**, ID3DBlob**);

D3DCompileFn Compiler() {
    static D3DCompileFn const compile = [] {
        HMODULE const module = LoadLibraryW(L"d3dcompiler_47.dll");
        return module != nullptr ? reinterpret_cast<D3DCompileFn>(GetProcAddress(module, "D3DCompile")) : nullptr;
    }();
    return compile;
}

struct EyeShaders {
    std::array<ComPtr<ID3D11PixelShader>, 3> ps;  // PSLeft, PSRight, PSFill with the correction
    bool ready = false;
    bool failed = false;  // compiling them failed: never correct the eyes (and keep the raw copy)
};
EyeShaders gEye;

enum Slot { kSlotLeft, kSlotRight, kSlotPanel, kSlots };
std::array<ComPtr<ID3D11Buffer>, kSlots> gConstants;
std::array<cv::Constants, kSlots> gUploaded{};
std::array<bool, kSlots> gHaveUploaded{};
ID3D11Buffer* gSavedB1 = nullptr;
bool gBound = false;

bool Bind(ID3D11DeviceContext* context, int slot, bool linearSource, std::uint32_t seed) {
    if (gConstants[slot] == nullptr) {
        ComPtr<ID3D11Device> device;
        context->GetDevice(device.GetAddressOf());
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = sizeof(cv::Constants);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (device == nullptr || FAILED(device->CreateBuffer(&desc, nullptr, gConstants[slot].GetAddressOf()))) {
            return false;
        }
        gHaveUploaded[slot] = false;
    }
    cv::Constants const k = cv::MakeConstants(gResolved.settings, gResolved.dither, linearSource, seed);
    if (!gHaveUploaded[slot] || std::memcmp(&k, &gUploaded[slot], sizeof k) != 0) {
        context->UpdateSubresource(gConstants[slot].Get(), 0, nullptr, &k, 0, 0);
        gUploaded[slot] = k;
        gHaveUploaded[slot] = true;
    }
    context->PSGetConstantBuffers(1, 1, &gSavedB1);
    ID3D11Buffer* const buffer = gConstants[slot].Get();
    context->PSSetConstantBuffers(1, 1, &buffer);
    gBound = true;
    return true;
}

void Unbind(ID3D11DeviceContext* context) {
    if (!gBound) {
        return;
    }
    context->PSSetConstantBuffers(1, 1, &gSavedB1);
    if (gSavedB1 != nullptr) {
        gSavedB1->Release();
        gSavedB1 = nullptr;
    }
    gBound = false;
}

// ---- Cost: GPU timestamps around the corrected draws (and the plain eye draws while comparing) --------------
enum Timer { kEyeCorrected, kEyePlain, kPanelCorrected, kTimers };
constexpr char const* kTimerNames[kTimers] = {"eyeCorrected", "eyePlain", "panelCorrected"};
struct QuerySet {
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> begin;
    ComPtr<ID3D11Query> end;
    int timer = -1;
    bool pending = false;
};
std::array<QuerySet, 12> gQueries;
int gOpenQuery = -1;
std::array<double, kTimers> gSumMs{};
std::array<std::uint64_t, kTimers> gCount{};
std::array<double, kTimers> gAverageMs{-1.0, -1.0, -1.0};  // the last report's (under gMutex for StatusJson)
std::uint64_t gReportAtMs = 0;
constexpr std::uint64_t kReportEveryMs = 10000;

void Report() {
    std::uint64_t const now = GetTickCount64();
    if (gReportAtMs == 0) {
        gReportAtMs = now + kReportEveryMs;
        return;
    }
    if (now < gReportAtMs) {
        return;
    }
    gReportAtMs = now + kReportEveryMs;
    if (gCount[kEyeCorrected] + gCount[kEyePlain] + gCount[kPanelCorrected] == 0) {
        return;
    }
    std::string line = "GPU time per draw, last 10 s:";
    char const* const labels[kTimers] = {"eye blit with the correction", "eye blit without (comparing)",
        "HUD panel blit with the correction"};
    std::lock_guard lock(gMutex);
    for (int t = 0; t < kTimers; ++t) {
        if (gCount[t] > 0) {
            gAverageMs[t] = gSumMs[t] / static_cast<double>(gCount[t]);
            line += std::format(" {} {:.3f} ms ({} draws);", labels[t], gAverageMs[t], gCount[t]);
        }
        gSumMs[t] = 0.0;
        gCount[t] = 0;
    }
    log::Info("colour: {}", line);
}

void PollQueries(ID3D11DeviceContext* context) {
    for (QuerySet& q : gQueries) {
        if (!q.pending) {
            continue;
        }
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        UINT64 t0 = 0;
        UINT64 t1 = 0;
        if (context->GetData(q.disjoint.Get(), &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            context->GetData(q.begin.Get(), &t0, sizeof t0, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            context->GetData(q.end.Get(), &t1, sizeof t1, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
            continue;
        }
        q.pending = false;
        if (!dj.Disjoint && dj.Frequency != 0 && t1 >= t0 && q.timer >= 0 && q.timer < kTimers) {
            gSumMs[q.timer] += static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(dj.Frequency);
            ++gCount[q.timer];
        }
    }
    Report();
}

void TimerBegin(ID3D11DeviceContext* context, int timer) {
    PollQueries(context);
    gOpenQuery = -1;
    for (int i = 0; i < static_cast<int>(gQueries.size()); ++i) {
        QuerySet& q = gQueries[i];
        if (q.pending) {
            continue;
        }
        if (q.disjoint == nullptr) {
            ComPtr<ID3D11Device> device;
            context->GetDevice(device.GetAddressOf());
            D3D11_QUERY_DESC const disjoint{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
            D3D11_QUERY_DESC const stamp{D3D11_QUERY_TIMESTAMP, 0};
            if (device == nullptr || FAILED(device->CreateQuery(&disjoint, q.disjoint.GetAddressOf())) ||
                FAILED(device->CreateQuery(&stamp, q.begin.GetAddressOf())) ||
                FAILED(device->CreateQuery(&stamp, q.end.GetAddressOf()))) {
                q = QuerySet{};
                return;
            }
        }
        q.timer = timer;
        context->Begin(q.disjoint.Get());
        context->End(q.begin.Get());
        gOpenQuery = i;
        return;
    }
}

void TimerEnd(ID3D11DeviceContext* context) {
    if (gOpenQuery < 0) {
        return;
    }
    QuerySet& q = gQueries[gOpenQuery];
    context->End(q.end.Get());
    context->End(q.disjoint.Get());
    q.pending = true;
    gOpenQuery = -1;
}

bool gEyeDrawOpen = false;  // EyeColourShader began something EyeColourDone ends
// EyeColourActive answers since the eye blit last drew (EyeColourShader): the blit path must draw.
int gActiveWithoutDraw = 0;
constexpr int kActiveWithoutDrawLimit = 240;

void SetProfile(cv::Profile p) {
    WriteValue(L"profile", Wide(cv::Name(p)));
    log::Info("menu: colour profile {} ([{}])", cv::Name(p), tKey.empty() ? std::string("Colour") : "Colour." + tKey);
    gDirty = false;
    Refresh(true);
}

void SaveCustom() {
    cv::Settings s;
    {
        std::lock_guard lock(gMutex);
        s = gResolved.custom;
    }
    WriteValue(L"profile", L"custom");
    WriteNumber(L"brightness", s.brightness);
    WriteNumber(L"contrast", s.contrast);
    WriteNumber(L"saturation", s.saturation);
    WriteNumber(L"gamma", s.gamma);
    WriteNumber(L"black", s.black);
    WriteNumber(L"gainR", s.gain[0]);
    WriteNumber(L"gainG", s.gain[1]);
    WriteNumber(L"gainB", s.gain[2]);
    log::Info("menu: colour custom values saved: {}", Values(s));
    gDirty = false;
    Refresh(true);
}

void SetCustomLive(cv::Settings const& s) {
    {
        std::lock_guard lock(gMutex);
        gResolved.custom = cv::Clamped(s);
        gResolved.chosen = cv::Profile::Custom;
        gResolved.effective = cv::Profile::Custom;
        gResolved.settings = gResolved.custom;
    }
    gDirty = true;
    Publish();
}

char const* DisplayName(cv::Display d) {
    switch (d) {
    case cv::Display::QuestStreamed: return "questStreamed";
    case cv::Display::Oled: return "oled";
    case cv::Display::Wired: return "wired";
    case cv::Display::Unknown: break;
    }
    return "unknown";
}

} // namespace

void SetHeadset(std::string const& runtimeName, std::string const& systemName, std::uint32_t vendorId) {
    cv::Detection const detection = cv::Detect(runtimeName, systemName);
    std::string const key = cv::HeadsetKey(runtimeName, systemName);
    {
        std::lock_guard lock(gMutex);
        gRuntime = runtimeName;
        gSystem = systemName;
        gVendor = vendorId;
        gDetection = detection;
        gKey = key;
    }
    std::string const line = std::format("headset: runtime '{}', system '{}', vendor 0x{:04X} -> [Colour.{}]; automatic: {} ({})",
        runtimeName, systemName, vendorId, key, cv::Name(detection.automatic), detection.why);
    log::Info("colour: {}", line);
    MELEVR::Logger::LogLine("[MEVR-COLOUR] " + line);
    gHeadsetChanged.store(true);  // the Present thread reads its section on its next tick
}

bool Correcting() {
    return gNonNeutral.load(std::memory_order_relaxed) && !CompareHeld();
}

void HoldCompare() {
    gCompareUntilMs.store(GetTickCount64() + kHoldLapseMs, std::memory_order_relaxed);
}

void SetCompare(bool on) {
    gCompareLatched.store(on, std::memory_order_relaxed);
}

bool BeginPanel(ID3D11DeviceContext* context, std::uint32_t seed) {
    Tick();
    if (context == nullptr || !Correcting() || !Bind(context, kSlotPanel, true, seed)) {
        return false;
    }
    TimerBegin(context, kPanelCorrected);
    return true;
}

void EndPanel(ID3D11DeviceContext* context) {
    TimerEnd(context);
    Unbind(context);
}

std::string StatusJson() {
    namespace req = mevr::vr::test_request;
    std::lock_guard lock(gMutex);
    cv::Settings const& s = gResolved.settings;
    auto ms = [](double v) { return v < 0.0 ? std::string("null") : std::format("{:.4f}", v); };
    return std::format("{{\"profile\": \"{}\", \"effective\": \"{}\", \"correcting\": {}, \"compare\": {}, \"headset\": "
                       "{{\"runtime\": {}, \"system\": {}, \"vendorId\": {}, \"key\": {}, \"display\": \"{}\", \"why\": {}}}, "
                       "\"settings\": {{\"brightness\": {:.3f}, \"contrast\": {:.3f}, \"saturation\": {:.3f}, \"gamma\": {:.3f}, "
                       "\"black\": {:.3f}, \"gain\": [{:.3f}, {:.3f}, {:.3f}]}}, \"dither\": {:.2f}, \"eyeShaders\": {}, "
                       "\"costMs\": {{\"{}\": {}, \"{}\": {}, \"{}\": {}}}, \"pngs\": \"{}\"}}",
        cv::Name(gResolved.chosen), cv::Name(gResolved.effective), Correcting() && gEye.ready ? "true" : "false",
        CompareHeld() || gResolved.compare ? "true" : "false", req::JsonString(gRuntime), req::JsonString(gSystem), gVendor,
        req::JsonString(gKey), DisplayName(gDetection.display), req::JsonString(gDetection.why), s.brightness, s.contrast,
        s.saturation, s.gamma, s.black, s.gain[0], s.gain[1], s.gain[2], gResolved.dither, gEye.ready ? "true" : "false",
        kTimerNames[0], ms(gAverageMs[0]), kTimerNames[1], ms(gAverageMs[1]), kTimerNames[2], ms(gAverageMs[2]),
        "corrected when correcting is true (the swapchain images are copied after the correction)");
}

MenuState GetMenuState() {
    Tick();
    MenuState s;
    std::lock_guard lock(gMutex);
    s.chosen = gResolved.chosen;
    s.effective = gResolved.effective;
    s.settings = gResolved.settings;
    s.custom = gResolved.custom;
    s.detection = gDetection;
    s.runtime = gRuntime;
    s.system = gSystem;
    s.compareInFile = gResolved.compare;
    return s;
}

void SelectProfile(cv::Profile p) {
    SetProfile(p);
}

void SetCustom(cv::Settings const& settings, bool save) {
    SetCustomLive(settings);
    if (save) {
        SaveCustom();
    }
}

void MenuSection() {
    Tick();
    ImGui::SeparatorText("Colour (the game's picture and its HUD)");
    std::string system;
    cv::Detection detection;
    cv::Resolved r;
    std::array<double, kTimers> cost{};
    {
        std::lock_guard lock(gMutex);
        system = gSystem;
        detection = gDetection;
        r = gResolved;
        cost = gAverageMs;
    }
    ImGui::Text("Headset: %s", system.empty() ? "not known yet (VR hasn't started)" : system.c_str());
    ImGui::TextDisabled("Automatic: %s, for %s", cv::Label(detection.automatic), detection.why.c_str());
    static constexpr cv::Profile kProfiles[] = {cv::Profile::Auto, cv::Profile::Neutral, cv::Profile::Quest, cv::Profile::Vivid,
        cv::Profile::Custom};
    for (cv::Profile const p : kProfiles) {
        if (p != cv::Profile::Auto) {
            ImGui::SameLine();
        }
        bool const on = r.chosen == p;
        if (ImGui::RadioButton(cv::Label(p), on) && !on) {
            SetProfile(p);
        }
    }
    // The values that apply; moving one makes them the headset's Custom values.
    cv::Settings s = r.settings;
    bool changed = false;
    bool done = false;
    auto slider = [&](char const* label, float* value, float lo, float hi, char const* format) {
        changed |= ImGui::SliderFloat(label, value, lo, hi, format);
        done |= ImGui::IsItemDeactivatedAfterEdit();
    };
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    slider("Brightness", &s.brightness, 0.5f, 1.5f, "%.2f");
    slider("Contrast", &s.contrast, 0.5f, 1.5f, "%.2f");
    slider("Saturation", &s.saturation, 0.0f, 2.0f, "%.2f");
    slider("Mid-tones (gamma; below 1 darker)", &s.gamma, 0.5f, 2.0f, "%.2f");
    slider("Black level (below 0 deeper)", &s.black, -0.2f, 0.2f, "%.3f");
    slider("Red", &s.gain[0], 0.5f, 1.5f, "%.2f");
    slider("Green", &s.gain[1], 0.5f, 1.5f, "%.2f");
    slider("Blue", &s.gain[2], 0.5f, 1.5f, "%.2f");
    ImGui::PopItemWidth();
    if (changed) {
        SetCustomLive(s);
    }
    if (done) {
        SaveCustom();
    }
    ImGui::Button("Hold to compare");
    if (ImGui::IsItemActive()) {
        HoldCompare();
    }
    ImGui::SameLine();
    if (ImGui::Button("Custom: back to neutral")) {
        SetCustomLive(cv::Settings{});
        SaveCustom();
    }
    if (r.compare) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "[Colour] compare = 1 in the file: shown uncorrected.");
    } else if (Correcting() && gEye.ready) {
        ImGui::TextDisabled("Correcting the world and the HUD (%s).",
            cost[kEyeCorrected] >= 0.0 ? std::format("eye blit {:.3f} ms on the GPU", cost[kEyeCorrected]).c_str() : "cost not measured yet");
    } else if (!cv::IsNeutral(r.settings) && !gEye.ready) {
        ImGui::TextDisabled("Waiting for the eye shaders (VR not started, or they failed: see the log).");
    } else {
        ImGui::TextDisabled("No correction (neutral: the picture as the game makes it).");
    }
}

bool gBoundForEye = false;

bool BeginEye(ID3D11DeviceContext* context, int eye, bool linearSource) {
    gEyeDrawOpen = false;
    gActiveWithoutDraw = 0;  // this draw is the eye blit's
    gBoundForEye = false;
    if (context == nullptr) {
        return false;
    }
    Tick();
    if (!gNonNeutral.load(std::memory_order_relaxed) || !Correcting()) {
        return false;
    }
    gBoundForEye = Bind(context, eye == 1 ? kSlotRight : kSlotLeft, linearSource, eye == 1 ? 1u : 0u);
    return gBoundForEye;
}

void EndEye(ID3D11DeviceContext* context) {
    if (gBoundForEye && context != nullptr) {
        Unbind(context);
    }
    gBoundForEye = false;
}

} // namespace mevr::host::colour

// ---- The [MEVR] colour seams in Halcyon's eye blit (src/halcyon/le1/mevr_bridge.h) ----------------------------
namespace MELEVR::MevrBridge {

void EyeColourShaders(ID3D11Device* device, const char* shaderSource) noexcept {
    using namespace mevr::host::colour;
    gEye = {};
    gEye.failed = true;  // until all three are made
    auto const compile = Compiler();
    if (device == nullptr || shaderSource == nullptr || compile == nullptr) {
        mevr::log::Error("colour: no eye shaders (no device, source or d3dcompiler_47.dll): no colour correction");
        return;
    }
    std::string const source = std::string(cv::ShaderPrelude()) + shaderSource;
    static constexpr char const* kEntries[3] = {"PSLeft", "PSRight", "PSFill"};
    for (int i = 0; i < 3; ++i) {
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> errors;
        HRESULT hr = compile(source.data(), source.size(), "MEVR_EyeColour", nullptr, nullptr, kEntries[i], "ps_4_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.GetAddressOf(), errors.GetAddressOf());
        if (SUCCEEDED(hr)) {
            hr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, gEye.ps[i].GetAddressOf());
        }
        if (FAILED(hr)) {
            mevr::log::Error("colour: eye shader {} failed (0x{:08X}): {}: no colour correction", kEntries[i],
                static_cast<std::uint32_t>(hr), errors ? static_cast<char const*>(errors->GetBufferPointer()) : "(no message)");
            gEye = {};
            gEye.failed = true;
            return;
        }
    }
    gEye.ready = true;
    gEye.failed = false;
    mevr::log::Info("colour: corrected variants of Halcyon's eye shaders ready");
}

bool EyeColourActive() noexcept {
    using namespace mevr::host::colour;
    Tick();
    // Not gEye.ready: the variants are compiled with the blit's resources, which the blit path makes. If the blit
    // never draws (its resources failed), the raw copy must come back rather than leave the eyes unfilled.
    if (gEye.failed || !Correcting()) {
        return false;
    }
    if (++gActiveWithoutDraw > kActiveWithoutDrawLimit) {
        gEye.failed = true;
        mevr::log::Error("colour: the eye blit hasn't drawn in {} eye copies: the eyes go back to the raw copy, uncorrected",
            kActiveWithoutDrawLimit);
        return false;
    }
    return true;
}

ID3D11PixelShader* EyeColourShader(ID3D11DeviceContext* context, int which, ID3D11PixelShader* plain, bool linearSource,
    int eye) noexcept {
    using namespace mevr::host::colour;
    gEyeDrawOpen = false;
    gActiveWithoutDraw = 0;
    if (context == nullptr || which < 0 || which > 2) {
        return plain;
    }
    Tick();
    if (!gNonNeutral.load(std::memory_order_relaxed) || !gEye.ready) {
        return plain;  // neutral (or no variants): Halcyon's own shader, nothing else
    }
    if (Correcting() && Bind(context, eye == 1 ? kSlotRight : kSlotLeft, linearSource, eye == 1 ? 1u : 0u)) {
        TimerBegin(context, kEyeCorrected);
        gEyeDrawOpen = true;
        return gEye.ps[which].Get();
    }
    TimerBegin(context, kEyePlain);  // comparing: the plain blit's own cost, for the difference
    gEyeDrawOpen = true;
    return plain;
}

void EyeColourDone(ID3D11DeviceContext* context) noexcept {
    using namespace mevr::host::colour;
    if (!gEyeDrawOpen || context == nullptr) {
        return;
    }
    gEyeDrawOpen = false;
    TimerEnd(context);
    Unbind(context);
}

} // namespace MELEVR::MevrBridge
