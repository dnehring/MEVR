// SPDX-License-Identifier: MIT
// Conversations and cutscenes on a 3D cinema screen (research/52): settings, each frame's plan, logs, snapshot status.
#include "plugin/CineScreen.hpp"

#include "halcyon/le1/mevr_bridge.h"
#include "plugin/MevrMenu.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/ConversationWorld.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>

namespace mevr::host::cine_screen {
namespace {

namespace cs = mevr::vr::cine_screen;

constexpr std::uint64_t kSettingsEveryMs = 1000;
constexpr std::uint64_t kActiveForMs = 500;      // a submit within this: a scene is on the screen
constexpr std::uint64_t kSummaryEveryMs = 10000;

std::mutex gMutex;
cs::Settings gSettings;
bool gSettingsLogged = false;
std::uint64_t gSettingsReadMs = 0;
cs::Frame gFrame;                // the last plan
float gFrameHalfH = 0.0f;
float gFrameIpdM = 0.0f;
bool gPlanned = false;
std::uint64_t gSubmitMs = 0;     // the last submit on the screen
bool gSubmitConversation = false;
float gHeightM = 0.0f;
std::uint64_t gSceneFrames = 0;  // submits since this scene started on the screen
std::uint64_t gSummaryMs = 0;
float gMinBaseline = 0.0f, gMaxBaseline = 0.0f;
bool gCapLogged = false;

float ReadFloat(wchar_t const* key, float fallback, std::wstring const& path) {
    wchar_t text[64] = {};
    GetPrivateProfileStringW(L"Cinematics", key, L"", text, static_cast<DWORD>(std::size(text)), path.c_str());
    std::string narrow;
    for (wchar_t const* p = text; *p != L'\0' && *p < 128; ++p) {
        narrow.push_back(static_cast<char>(*p));
    }
    std::optional<double> const value = mevr::ini::Number(narrow);  // never strtod (the C locale)
    return value && std::isfinite(*value) ? static_cast<float>(*value) : fallback;
}

bool Same(cs::Settings const& a, cs::Settings const& b) {
    return a.conversations == b.conversations && a.cutscenes == b.cutscenes && a.frontEnd == b.frontEnd && a.distanceM == b.distanceM &&
           a.widthM == b.widthM &&
           a.depth == b.depth && a.convergeM == b.convergeM && a.aspect == b.aspect && a.fullRate == b.fullRate;
}

// Under gMutex.
void RefreshSettings(std::uint64_t now) {
    if (gSettingsReadMs != 0 && now - gSettingsReadMs < kSettingsEveryMs) {
        return;
    }
    gSettingsReadMs = now;
    cs::Settings s;
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    if (!path.empty()) {
        {   // 0/vr, 1/screen, or world (research/59: the mod shows it in the world; this screen stays off)
            wchar_t text[32] = {};
            GetPrivateProfileStringW(L"Cinematics", L"conversations", L"", text, static_cast<DWORD>(std::size(text)), path.c_str());
            std::string narrow;
            for (wchar_t const* p = text; *p != L'\0' && *p < 128; ++p) {
                narrow.push_back(static_cast<char>(*p));
            }
            namespace cw = mevr::vr::conversation_world;
            cw::Mode const mode = cw::ParseMode(narrow.c_str(), s.conversations ? cw::Mode::Screen : cw::Mode::Vr);
            s.conversations = mode == cw::Mode::Screen;
        }
        s.cutscenes = GetPrivateProfileIntW(L"Cinematics", L"cutscenes", s.cutscenes ? 1 : 0, path.c_str()) != 0;
        s.frontEnd = GetPrivateProfileIntW(L"Cinematics", L"frontEnd", s.frontEnd ? 1 : 0, path.c_str()) != 0;
        s.distanceM = ReadFloat(L"distanceM", s.distanceM, path);
        s.widthM = ReadFloat(L"widthM", s.widthM, path);
        s.depth = ReadFloat(L"depth", s.depth, path);
        s.convergeM = ReadFloat(L"convergeM", s.convergeM, path);
        s.aspect = ReadFloat(L"aspect", s.aspect, path);
        s.fullRate = GetPrivateProfileIntW(L"Cinematics", L"fullRate", s.fullRate ? 1 : 0, path.c_str()) != 0;
    }
    s = cs::Clamped(s);
    if (!gSettingsLogged || !Same(s, gSettings)) {
        gSettingsLogged = true;
        gCapLogged = false;
        mevr::log::Info("cine screen: [Cinematics] conversations = {}, cutscenes = {} (on the 3D screen while Halcyon's VR conversations / "
                        "cutscenes are on; off: in VR as before), frontEnd = {} (the title and its menus); the screen {:.2f} m wide at "
                        "{:.2f} m, depth {:.2f}% of its width "
                        "(at most {:.2f}% for a 64 mm eye distance), the scene {:.2f} m away on the screen plane, {}, {}",
            s.conversations ? 1 : 0, s.cutscenes ? 1 : 0, s.frontEnd ? 1 : 0, s.widthM, s.distanceM, s.depth * 100.0f,
            cs::MaxDepth(s.widthM, cs::kDefaultIpdM) * 100.0f, s.convergeM,
            s.aspect > 0.0f ? std::format("the eye image's middle band at {:.2f}:1 (aspect)", s.aspect)
                            : std::string("the whole eye image (aspect = 0)"),
            s.fullRate ? "at the headset's rate (fullRate)" : "at Halcyon's cinematic cap, half the display rate (fullRate = 0)");
    }
    gSettings = s;
}

} // namespace

cs::Frame PlanFrame(float halfH, float ipdM, float unitsPerMeter) {
    std::lock_guard lock(gMutex);
    RefreshSettings(GetTickCount64());
    gFrame = cs::Plan(gSettings, ipdM, unitsPerMeter, halfH);
    gFrameHalfH = halfH;
    gFrameIpdM = ipdM;
    gPlanned = true;
    if (gFrame.depthCapped && !gCapLogged) {
        gCapLogged = true;
        mevr::log::Info("cine screen: depth {:.2f}% asked, {:.2f}% used: on a {:.2f} m screen more would put infinity's disparity past "
                        "{:.0f}% of your {:.1f} mm eye distance (the eyes would diverge)",
            gSettings.depth * 100.0f, gFrame.depth * 100.0f, gSettings.widthM, cs::kDivergenceShare * 100.0f,
            (ipdM > 0.0f ? ipdM : cs::kDefaultIpdM) * 1000.0f);
    }
    return gFrame;
}

std::string StatusJson() {
    std::lock_guard lock(gMutex);
    std::uint64_t const now = GetTickCount64();
    bool const active = gSubmitMs != 0 && now - gSubmitMs < kActiveForMs;
    auto j = [](float v) { return std::isfinite(v) ? std::format("{:.5g}", v) : std::string("null"); };
    return std::format("{{\"active\": {}, \"kind\": \"{}\", \"sceneFrames\": {}, \"settings\": {{\"conversations\": {}, \"cutscenes\": {}, "
                       "\"distanceM\": {}, \"widthM\": {}, \"depth\": {}, \"convergeM\": {}, \"aspect\": {}, \"fullRate\": {}}}, \"plan\": {{\"valid\": {}, \"depth\": {}, "
                       "\"crop\": {}, \"baselineUU\": {}, \"halfHDeg\": {}, \"ipdM\": {}, \"depthCapped\": {}}}, \"heightM\": {}}}",
        active ? "true" : "false", gSubmitConversation ? "conversation" : "cutscene", gSceneFrames, gSettings.conversations ? "true" : "false",
        gSettings.cutscenes ? "true" : "false", j(gSettings.distanceM), j(gSettings.widthM), j(gSettings.depth), j(gSettings.convergeM),
        j(gSettings.aspect), gSettings.fullRate ? "true" : "false",
        gPlanned ? "true" : "false", j(gFrame.depth), j(gFrame.crop), j(gFrame.baselineUU), j(gFrameHalfH * 57.29578f), j(gFrameIpdM),
        gFrame.depthCapped ? "true" : "false", j(gHeightM));
}

bool FrontEndWanted() noexcept {
    std::lock_guard lock(gMutex);
    RefreshSettings(GetTickCount64());
    return gSettings.frontEnd;
}

} // namespace mevr::host::cine_screen

namespace MELEVR::MevrBridge {

bool CineScreenWanted(bool conversation) noexcept {
    namespace host = mevr::host::cine_screen;
    std::lock_guard lock(host::gMutex);
    host::RefreshSettings(GetTickCount64());
    return conversation ? host::gSettings.conversations : host::gSettings.cutscenes;
}


bool CineScreenFullRate() noexcept {
    namespace host = mevr::host::cine_screen;
    std::lock_guard lock(host::gMutex);
    host::RefreshSettings(GetTickCount64());
    return host::gSettings.fullRate;
}

bool CineScreenQuadsNow(unsigned eyeWidth, unsigned eyeHeight, CineScreenQuads* out) noexcept {
    namespace host = mevr::host::cine_screen;
    if (out == nullptr) {
        return false;
    }
    std::lock_guard lock(host::gMutex);
    out->distanceM = host::gSettings.distanceM;
    out->widthM = host::gSettings.widthM;
    out->crop = host::gPlanned ? host::gFrame.crop : 0.0f;
    host::cs::EyeRects const rects =
        host::cs::Rects(static_cast<int>(eyeWidth), static_cast<int>(eyeHeight), out->crop, host::gSettings.aspect);
    host::cs::Rect const* const eyes[2] = {&rects.left, &rects.right};
    for (int e = 0; e < 2; ++e) {
        out->rect[e][0] = eyes[e]->x;
        out->rect[e][1] = eyes[e]->y;
        out->rect[e][2] = eyes[e]->width;
        out->rect[e][3] = eyes[e]->height;
    }
    out->heightM = host::cs::HeightM(rects, out->widthM);
    return host::gPlanned;
}

void NoteCineScreenSubmit(bool conversation, float heightM) noexcept {
    namespace host = mevr::host::cine_screen;
    std::lock_guard lock(host::gMutex);
    std::uint64_t const now = GetTickCount64();
    bool const starting = host::gSubmitMs == 0 || now - host::gSubmitMs >= host::kActiveForMs || conversation != host::gSubmitConversation;
    host::gSubmitMs = now;
    host::gSubmitConversation = conversation;
    host::gHeightM = heightM;
    float const b = host::gFrame.baselineUU;
    if (starting) {
        host::gSceneFrames = 0;
        host::gSummaryMs = now;
        host::gMinBaseline = host::gMaxBaseline = b;
        mevr::log::Info("cine screen: a {} on the 3D screen: {:.2f} x {:.2f} m at {:.2f} m, depth {:.2f}% (each eye drops {:.2f}% of its "
                        "image), the cameras {:.2f} units apart for a {:.1f}-degree lens",
            conversation ? "conversation" : "cutscene", host::gSettings.widthM, heightM, host::gSettings.distanceM,
            host::gFrame.depth * 100.0f, host::gFrame.crop * 100.0f, b, host::gFrameHalfH * 2.0f * 57.29578f);
    }
    ++host::gSceneFrames;
    host::gMinBaseline = std::min(host::gMinBaseline, b);
    host::gMaxBaseline = std::max(host::gMaxBaseline, b);
    if (now - host::gSummaryMs >= host::kSummaryEveryMs) {
        mevr::log::Info("cine screen: {} frames of this {} on the screen; the cameras {:.2f}..{:.2f} units apart (the lens now {:.1f} degrees)",
            host::gSceneFrames, conversation ? "conversation" : "cutscene", host::gMinBaseline, host::gMaxBaseline,
            host::gFrameHalfH * 2.0f * 57.29578f);
        host::gSummaryMs = now;
        host::gMinBaseline = host::gMaxBaseline = b;
    }
}

} // namespace MELEVR::MevrBridge
