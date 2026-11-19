// SPDX-License-Identifier: MIT
// LE1's side of the frame-breakdown probe (plugin/PerfProbe.cpp is the shared module; plugin/PerfProbeHost.hpp the
// adapter it asks): the capture JSON's renderer members from Halcyon's ME1 renderer and our LE1-only modules, and the
// seams Halcyon's ME1 code calls (src/halcyon/le1/mevr_bridge.h: PerfPresentBegin/End, PerfPass0Captured, PerfPoint,
// g_perfDraws, EarlyFrameWait, PerEyeViewState), moved here unchanged from PerfProbe.cpp when the module was shared
// (2026-11-16, research/sequel-render-levers.md).

#include "plugin/PerfProbe.hpp"
#include "plugin/PerfProbeHost.hpp"

#include "plugin/QuestPacing.hpp"
#include "plugin/Colour.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/RenderLevers.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/MainMenu.hpp"
#endif
#include "plugin/Upscale.hpp"
#include "plugin/Voice.hpp"

#include "core/Log.hpp"
#include "core/vr/FrameProbe.hpp"
#include "core/vr/TestRequest.hpp"
#include "halcyon/le1/mevr_bridge.h"
#include "halcyon/le1/vr_config.h"
#include "halcyon/le1/xr_session.h"
#include "core/vr/SettingsFile.hpp"

#include <Windows.h>

#include <cstdint>
#include <format>
#include <iterator>
#include <string>

namespace mevr::host::perf::adapter {

ID3D11DeviceContext* FrameContext(XrFrameEndInfo const*) noexcept {
    return nullptr;  // never asked: Halcyon's Present hook hands the probe its context
}

void FrameClosed(ID3D11DeviceContext*) noexcept {}  // never asked: MevrBridge::PerfPresentEnd below does the rest

std::string RendererJson() {
    auto const J = [](double value) { return vr::test_request::JsonNumber(value); };
    MELEVR::Config::VrConfig const& h = MELEVR::Config::Get();
    MELEVR::XrSession::ResolutionStats resolution;
    MELEVR::XrSession::GetResolutionStats(&resolution);
    return std::format(
        "  \"halcyon\": {{\"sfr2\": {}, \"dynResFraction\": {}, \"mirrorPresentEvery\": {}, \"stereoFramePacing\": {}, "
        "\"cineFpsCap\": {}, \"disableDof\": {}, \"hudPanel\": {}, \"handReticle\": {}, \"sfr2Metric\": {}, "
        "\"sfr2MetricEyeFrustum\": {}}},\n  \"renderLevers\": {},\n  \"upscale\": {},\n"
        "  \"resolution\": {{\"render\": [{}, {}], \"eye\": [{}, {}], \"recommendedEye\": [{}, {}]}}",
        h.sfr2Enabled ? "true" : "false", J(h.dynResFraction),
        h.mirrorPresentEvery, h.stereoFramePacing ? "true" : "false", h.cineFpsCap ? "true" : "false",
        h.disableDof ? "true" : "false", h.hudPanel ? "true" : "false", h.handReticle ? "true" : "false",
        h.sfr2Metric ? "true" : "false", h.sfr2MetricEyeFrustum ? "true" : "false", render_levers::SettingsJson(),
        upscale::StatusJson(), resolution.renderWidth, resolution.renderHeight, resolution.eyeWidth, resolution.eyeHeight,
        resolution.recommendedEyeWidth, resolution.recommendedEyeHeight);
}

} // namespace mevr::host::perf::adapter

namespace MELEVR::MevrBridge {

std::uint32_t g_perfDraws = 0;

bool PerEyeViewState() noexcept {
    return mevr::host::render_levers::PerEyeViewState();
}

bool EarlyFrameWait() noexcept {
    static std::wstring path;
    static std::uint64_t nextReadMs = 0;
    static int value = -1;
    std::uint64_t const now = GetTickCount64();
    if (now >= nextReadMs) {
        nextReadMs = now + 1000;
        if (path.empty()) {
            path = mevr::host::SettingsPath();  // <exe dir>\ASI\LE1MEVR.ini, as the mod
        }
        // `auto` (the default, or the key absent): the late wait for a streamed Meta headset, the early one for the rest. The
        // owner's QA (2026-11-07, a Quest 3 over Steam Link and Quest Link): with the early wait the Quest dropped to half rate at
        // a loading screen and never came back, although the game kept handing it 72 frames a second (SteamVR's CPU frame time
        // read 13.76 of 13.89 ms); switched to the late wait live it climbed back to 72 at once, and after a later load it
        // dropped and recovered. The PS VR2 needed the early wait to reach 90 (research/22, 2026-10-22).
        wchar_t text[16] = L"";
        if (!path.empty()) {
            mevr::vr::settings_file::String(L"Performance", L"earlyFrameWait", L"auto", text, static_cast<DWORD>(std::size(text)), path.c_str());
        }
        bool const automatic = text[0] == L'\0' || _wcsicmp(text, L"auto") == 0;
        bool const streamed = mevr::host::colour::StreamedHeadset();
        int const read = automatic ? (streamed ? 0 : 1) : (_wtoi(text) != 0 ? 1 : 0);
        if (read != value) {
            value = read;
            mevr::log::Info("frame loop: {} ([Performance] earlyFrameWait = {}{})",
                value != 0 ? "xrWaitFrame for the next frame right after xrEndFrame, before the game renders it"
                           : "xrWaitFrame at Present, after the game rendered the frame (upstream)",
                automatic ? "auto: " : "", automatic ? (streamed ? "a streamed Meta headset" : "not a streamed Meta headset")
                                                     : (value != 0 ? "1" : "0"));
        }
    }
    return value != 0;
}

void PerfPresentBegin(ID3D11DeviceContext* context) noexcept {
    mevr::host::quest_pacing::PresentBegin();
    mevr::host::perf::Mark(mevr::vr::frame_probe::kPresent, context);
}

void PerfPresentEnd(ID3D11DeviceContext* context) noexcept {
    mevr::host::quest_pacing::PresentEnd();
    mevr::host::perf::Mark(mevr::vr::frame_probe::kEnd, context);
    mevr::host::render_levers::OnPresentEnd(context);
#if defined(MEVR_WITH_MAIN_MENU)
    mevr::host::main_menu::OnPresent();  // every Present, VR frame or not: an open menu nobody can see is closed
#endif
    mevr::host::voice::OnPresent();  // the push-to-talk key, the voice.request channel, the listen state
}

void PerfPass0Captured(ID3D11DeviceContext* context) noexcept {
    mevr::host::perf::Mark(mevr::vr::frame_probe::kPass0, context);
}

void PerfPoint(int point) noexcept {
    switch (point) {
    case kPerfPreamble: mevr::host::perf::Mark(mevr::vr::frame_probe::kPreamble); break;
    case kPerfFrameEntry: mevr::host::perf::Mark(mevr::vr::frame_probe::kFrameEntry); break;
    case kPerfFrameReads: mevr::host::perf::Mark(mevr::vr::frame_probe::kFrameReads); break;
    case kPerfFrameCine: mevr::host::perf::Mark(mevr::vr::frame_probe::kFrameCine); break;
    case kPerfFramePrepared: mevr::host::perf::Mark(mevr::vr::frame_probe::kFramePrepared); break;
    case kPerfFrameReady: mevr::host::perf::Mark(mevr::vr::frame_probe::kFrameReady); break;
    default: break;
    }
}

} // namespace MELEVR::MevrBridge
