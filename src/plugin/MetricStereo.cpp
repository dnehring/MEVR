// SPDX-License-Identifier: MIT
// Life-size (metric) stereo for Halcyon's two-pass stereo on all three games (research/30; the sequels: section 9):
// MevrSeams::PlanStereo and NoteStereoSubmit (LE1: [MEVR] seams in xr_session.cpp's RunFrame; ME2 and ME3: their
// me2_xr.cpp's SFR frame), MevrSeams::LifeSizeStereo, the [MEVR-STEREO] log, the snapshot's "stereo" block.

#include "plugin/MetricStereo.hpp"
#include "plugin/Renderer.hpp"
#include "plugin/CineScreen.hpp"
#include "plugin/HudPanel.hpp"
#include "plugin/RenderLevers.hpp"
#include "plugin/RenderLeversSeams.hpp"

#include "halcyon/MevrSeams.h"

#include "core/DevChannels.hpp"
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/MetricStereo.hpp"
#include "core/vr/SettingsFile.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>

namespace mevr::host::metric_stereo {
namespace {

namespace ms = vr::metric_stereo;

constexpr float kDeg = 57.29578f;
// Shepard's skeleton puts a metre at about 100 units (research/13, "World scale"): the life-size scale.
constexpr float kLifeSizeUnitsPerMeter = 100.0f;

struct Status {
    bool planned = false;
    MevrSeams::StereoPlan plan;
    float unitsPerMeter = 0.0f;
    float depth = 1.0f;
    float eyeFov[2][4] = {};        // the runtime's, radians
    float fillH = 1.0f, fillV = 1.0f;
    float cpuHalfH = 0.0f, cpuHalfV = 0.0f;   // the render's last projection (radians; 0 = none seen)
    float cantDeg = 0.0f;
    bool screen = false;            // the last plan was the 3D cinema screen's (research/52): the whole window is shown
    bool submitted = false;
    int submitMode = 0;
    float declaredPosition[2][3] = {};
    float declaredOrientation[2][4] = {};
    float declaredFov[2][4] = {};
};
std::mutex gMutex;
Status gStatus;

// What the last [MEVR-STEREO] plan line said (a new line when any of it changes).
int gLoggedMode = -1;
float gLoggedIpd = -1.0f;
float gLoggedScale = -1.0f;
bool gLoggedFrustumWhy = false;
int gLoggedSubmitMode = -1;
std::uint64_t gLastPlanLogMs = 0;
int gUncontainedPlans = 0;
std::uint64_t gLastContainWarnMs = 0;
int gPlanChangesUnlogged = 0;
std::uint64_t gLastSubmitLogMs = 0;

void LogBoth(std::string const& line) {
    log::Info("{}", line);
    mevr::host::renderer::LogLine(line);
}

std::string FovDeg(float const f[4]) {
    return std::format("({:.1f}, {:.1f}, {:.1f}, {:.1f})", f[0] * kDeg, f[1] * kDeg, f[2] * kDeg, f[3] * kDeg);
}

char const* ModeName(int mode) {
    switch (mode) {
    case 1: return "life-size, the symmetric window";
    case 2: return "life-size, each eye's own frustum";
    default: return "upstream (eye separation and convergence)";
    }
}

ms::Fov ToFov(float const f[4]) {
    return {f[0], f[1], f[2], f[3]};
}

std::string J(float v) {
    return std::isfinite(v) ? std::format("{:.5f}", v) : std::string("null");
}

std::string J3(float const v[3]) {
    return std::format("[{}, {}, {}]", J(v[0]), J(v[1]), J(v[2]));
}

std::string J4(float const v[4]) {
    return std::format("[{}, {}, {}, {}]", J(v[0]), J(v[1]), J(v[2]), J(v[3]));
}

} // namespace

std::string StatusJson() {
    std::lock_guard lock(gMutex);
    Status const& s = gStatus;
    if (!s.planned) {
        return "null";
    }
    MevrSeams::StereoPlan const& p = s.plan;
    std::string passes;
    for (int pass = 0; pass < 2; ++pass) {
        int const eye = pass == 0 ? p.pass0Eye : 1 - p.pass0Eye;
        passes += std::format("{}{{\"eye\": \"{}\", \"offsetUU\": {}, \"terms\": {}, \"tangents\": {}}}", pass == 0 ? "" : ", ",
            eye == 0 ? "left" : "right", J3(p.passOffsetUU[pass]), J4(p.passTerms[pass]), J4(p.passTan[pass]));
    }
    std::string submit = "null";
    if (s.submitted) {
        submit = std::format("{{\"mode\": {}, \"leftPosition\": {}, \"rightPosition\": {}, \"leftOrientation\": {}, "
                             "\"rightOrientation\": {}, \"leftFov\": {}, \"rightFov\": {}}}",
            s.submitMode, J3(s.declaredPosition[0]), J3(s.declaredPosition[1]), J4(s.declaredOrientation[0]),
            J4(s.declaredOrientation[1]), J4(s.declaredFov[0]), J4(s.declaredFov[1]));
    }
    return std::format("{{\"mode\": {}, \"modeName\": \"{}\", \"ipdM\": {}, \"ipdUU\": {}, \"unitsPerMeter\": {}, "
                       "\"depth\": {}, \"cantDeg\": {}, \"runtimeFov\": [{}, {}], \"passes\": [{}], \"submitted\": {}}}",
        p.mode, ModeName(p.mode), J(p.ipdM), J(p.ipdUU), J(s.unitsPerMeter), J(s.depth), J(s.cantDeg), J4(s.eyeFov[0]),
        J4(s.eyeFov[1]), passes, submit);
}

bool PassShownFraction(int pass, float out[4]) {
    namespace ms = mevr::vr::metric_stereo;
    if (out == nullptr || pass < 0 || pass > 1) {
        return false;
    }
    std::lock_guard lock(gMutex);
    if (!gStatus.planned || gStatus.plan.mode != 1 || gStatus.screen) {
        return false;
    }
    int const eye = pass == 0 ? gStatus.plan.pass0Eye : 1 - gStatus.plan.pass0Eye;
    ms::Fov const window = ms::UnionWindow(ToFov(gStatus.eyeFov[0]), ToFov(gStatus.eyeFov[1]), gStatus.fillH, gStatus.fillV);
    ms::Tangents const w = ms::TangentsOf(window);
    float const width = w.right - w.left, height = w.up - w.down;
    if (width <= 0.0f || height <= 0.0f) {
        return false;
    }
    if (gStatus.cpuHalfH > 0.0f && gStatus.cpuHalfV > 0.0f) {   // the render's own window, once seen, must be that union
        float const th = std::tan(gStatus.cpuHalfH), tv = std::tan(gStatus.cpuHalfV);
        if (std::abs(th - w.right) > 0.02f * th || std::abs(tv - w.up) > 0.02f * tv) {
            return false;
        }
    }
    ms::Tangents const t = ms::TangentsOf(ms::Filled(ToFov(gStatus.eyeFov[eye]), gStatus.fillH, gStatus.fillV));
    constexpr float margin = 0.005f;
    out[0] = std::clamp((t.left - w.left) / width - margin, 0.0f, 1.0f);
    out[1] = std::clamp((w.up - t.up) / height - margin, 0.0f, 1.0f);
    out[2] = std::clamp((t.right - w.left) / width + margin, 0.0f, 1.0f);
    out[3] = std::clamp((w.up - t.down) / height + margin, 0.0f, 1.0f);
    return out[0] > 0.0f || out[1] > 0.0f || out[2] < 1.0f || out[3] < 1.0f;
}

} // namespace mevr::host::metric_stereo

namespace MevrSeams {

void PlanStereo(const StereoPlanInput& in, StereoPlan* out) noexcept {
    namespace ms = mevr::vr::metric_stereo;
    using namespace mevr::host::metric_stereo;
    if (out == nullptr) {
        return;
    }
    *out = StereoPlan{};
    float cantDeg = 0.0f;
    // [Cinematics] the 3D cinema screen (research/52): two parallel cameras on the director's camera, half the separation to
    // each side, through the render's own (native) projection: mode 1 whatever sfr2Metric says, so the submit routes pass 0
    // to the left eye and applies no convergence shift (the quads' crop sets the depth instead).
    if (in.screen && in.sfr2Active) {
        float ipdM = 0.0f;
        if (in.headValid) {
            float const dx = in.eyePosition[1][0] - in.eyePosition[0][0];
            float const dy = in.eyePosition[1][1] - in.eyePosition[0][1];
            float const dz = in.eyePosition[1][2] - in.eyePosition[0][2];
            ipdM = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        // The lens is the previous frame's render: the render hook scales the offsets to the lens it draws with (a cut, a
        // scene's start from gameplay: research/52 "Cross-eyed"). Unknown, it is planned at the largest lens and scaled.
        float const maxHalfH = mevr::vr::cine_screen::kMaxLensHalfH;
        float const planHalfH = in.cpuHalfH > 0.0f ? std::min(in.cpuHalfH, maxHalfH) : maxHalfH;
        mevr::vr::cine_screen::Frame const frame = mevr::host::cine_screen::PlanFrame(planHalfH, ipdM,
            mevr::vr::cine_screen::kSceneUnitsPerMeter);   // convergeM is a distance in the scene, at its own scale
        out->mode = 1;
        out->pass0Eye = 0;
        out->passOffsetUU[0][0] = -0.5f * frame.baselineUU;
        out->passOffsetUU[1][0] = 0.5f * frame.baselineUU;
        out->ipdM = ipdM;
        out->ipdUU = frame.baselineUU;
        out->lensTan = frame.baselineUU > 0.0f ? std::tan(planHalfH) : 0.0f;
        out->lensTanMax = std::tan(maxHalfH);
        std::lock_guard lock(gMutex);
        gStatus.planned = true;
        gStatus.plan = *out;
        gStatus.unitsPerMeter = in.unitsPerMeter;
        gStatus.depth = frame.depth;
        gStatus.cpuHalfH = in.cpuHalfH;
        gStatus.cpuHalfV = in.cpuHalfV;
        gStatus.screen = true;
        return;
    }
    bool const metric = in.metric && in.sfr2Active && in.headValid && in.unitsPerMeter > 0.0f;
    // Upstream's swap exchanges which pass renders which eye (and, through its pass-bound convergence sign, made the
    // shift divergent). Each metric pass renders its own eye's real viewpoint and the submit routes by this plan, so
    // swap has nothing left to fix: metric ignores it (pass 0 is always the left eye).
    out->pass0Eye = (!metric && in.swapEyes) ? 1 : 0;
    if (metric) {
        ms::EyeInputs e;
        for (int i = 0; i < 2; ++i) {
            e.orientation[i] = {in.eyeOrientation[i][0], in.eyeOrientation[i][1], in.eyeOrientation[i][2], in.eyeOrientation[i][3]};
            e.position[i] = {in.eyePosition[i][0], in.eyePosition[i][1], in.eyePosition[i][2]};
        }
        e.viewOrientation = {in.viewOrientation[0], in.viewOrientation[1], in.viewOrientation[2], in.viewOrientation[3]};
        e.viewFollowsHead = in.viewFollowsHead;
        e.unitsPerMeter = in.unitsPerMeter;
        e.depth = in.depth;
        ms::EyeOffsets const offsets = ms::BodyOffsets(e);
        cantDeg = offsets.cantDeg;
        out->ipdM = offsets.ipdM;
        out->ipdUU = offsets.ipdUU;
        // Each eye's own frustum only where nothing screen-space is drawn into the eye images: the same pixel then
        // looks a different way in each eye (+-24 degrees at the centre on a PS VR2), so full-frame UI would split.
        // One Draw a frame (research/37, [Performance] sharePass = 5): the frame's only render decides visibility for
        // both eyes, and its occlusion tests (the engine's own and Umbra's, bounding boxes drawn with the GPU matrix
        // against the depth buffer) see only that render's frustum: an object inside the other eye's outer wedge
        // (18 degrees on a PS VR2) would stay "occluded" and never appear in the replayed eye. Both eyes then render
        // on the symmetric window, which holds both frusta, at that mode's angular resolution.
        bool const oneDraw = mevr::host::render_levers::seam::g_oneDrawStereo.load(std::memory_order_relaxed) &&
                             mevr::host::render_levers::OneDrawWindow();
        // The HUD panel taking the game's UI is the same question on every game (LE1 passes it in as well).
        bool const uiInEyes = in.uiInEyes || !mevr::host::hud_panel::Armed();
        // The sequels' window is aspect-derived and needn't hold the eyes (research/30 9.2): mode 2 only where it does.
        out->mode = (in.eyeFrustum && !uiInEyes && in.fovOverride && in.windowHoldsEyes && !oneDraw) ? 2 : 1;
        for (int pass = 0; pass < 2; ++pass) {
            int const eye = pass == 0 ? out->pass0Eye : 1 - out->pass0Eye;
            out->passOffsetUU[pass][0] = offsets.body[eye].x;
            out->passOffsetUU[pass][1] = offsets.body[eye].y;
            out->passOffsetUU[pass][2] = offsets.body[eye].z;
            if (out->mode == 2) {
                ms::Tangents const t = ms::TangentsOf(ms::Filled(ToFov(in.eyeFov[eye]), in.fillH, in.fillV));
                ms::OffCentre const o = ms::Projection(t);
                float const terms[4] = {o.p0, o.p5, o.p8, o.p9};
                float const tans[4] = {t.left, t.right, t.up, t.down};
                for (int k = 0; k < 4; ++k) {
                    out->passTerms[pass][k] = terms[k];
                    out->passTan[pass][k] = tans[k];
                }
            }
        }
    }

    {
        std::lock_guard lock(gMutex);
        gStatus.planned = true;
        gStatus.screen = false;
        gStatus.plan = *out;
        gStatus.unitsPerMeter = in.unitsPerMeter;
        gStatus.depth = in.depth;
        gStatus.cantDeg = cantDeg;
        gStatus.fillH = in.fillH;
        gStatus.fillV = in.fillV;
        gStatus.cpuHalfH = in.cpuHalfH;
        gStatus.cpuHalfV = in.cpuHalfV;
        for (int i = 0; i < 2; ++i) {
            for (int k = 0; k < 4; ++k) {
                gStatus.eyeFov[i][k] = in.eyeFov[i][k];
            }
        }
    }

    // Containment (the performance review): in mode 2 the CPU's window (culling, Umbra; the render's last projection)
    // must hold each eye's own frustum, or objects at that edge pop in. Warned when it hasn't for 10 plans in a row
    // (a fill change takes a frame to reach the render), at most every 10 s.
    float const cpuTanH = in.cpuHalfH > 0.0f ? std::tan(in.cpuHalfH) : 0.0f;
    float const cpuTanV = in.cpuHalfV > 0.0f ? std::tan(in.cpuHalfV) : 0.0f;
    float needH = 0.0f, needV = 0.0f;
    for (int pass = 0; pass < 2; ++pass) {
        needH = std::max(needH, std::max(std::fabs(out->passTan[pass][0]), std::fabs(out->passTan[pass][1])));
        needV = std::max(needV, std::max(std::fabs(out->passTan[pass][2]), std::fabs(out->passTan[pass][3])));
    }
    bool const cpuKnown = cpuTanH > 0.0f && cpuTanV > 0.0f;
    bool const contained = !cpuKnown || (cpuTanH >= needH * 0.999f && cpuTanV >= needV * 0.999f);
    if (out->mode == 2 && !contained) {
        std::uint64_t const nowMs = GetTickCount64();
        if (++gUncontainedPlans >= 10 && nowMs - gLastContainWarnMs >= 10000) {
            gLastContainWarnMs = nowMs;
            LogBoth(std::format("[MEVR-STEREO] WARNING the eyes' own frusta are WIDER than the CPU's culling window: they need "
                                "+-{:.4f} x +-{:.4f} (tangents), the render's projection gives +-{:.4f} x +-{:.4f} (1/P0, 1/P5): "
                                "objects at that edge can pop in ({} plans in a row). Halcyon's FOV override off or "
                                "narrower than the eyes? sfr2MetricEyeFrustum = 0 avoids it",
                needH, needV, cpuTanH, cpuTanV, gUncontainedPlans));
        }
    } else {
        gUncontainedPlans = 0;
    }

    // The [MEVR-STEREO] plan line: at the first plan and whenever the mode, the eye distance or the scale changes.
    float const scale = in.unitsPerMeter * in.depth;
    bool const uiNow = in.uiInEyes || !mevr::host::hud_panel::Armed();
    bool const frustumWhy = in.eyeFrustum && (uiNow || !in.windowHoldsEyes);
    bool const changed = out->mode != gLoggedMode || (metric && (std::fabs(out->ipdM - gLoggedIpd) > 0.0005f ||
                                                                 std::fabs(scale - gLoggedScale) > 0.01f)) ||
        (metric && frustumWhy != gLoggedFrustumWhy);
    if (!changed || (!metric && !in.metric && gLoggedMode == -1 && !in.sfr2Active)) {
        return;
    }
    // At most one line a second: a mode that flaps shows as a count on the next line instead of a flood.
    std::uint64_t const nowMs = GetTickCount64();
    if (nowMs - gLastPlanLogMs < 1000) {
        ++gPlanChangesUnlogged;
        return;
    }
    gLastPlanLogMs = nowMs;
    int const unlogged = gPlanChangesUnlogged;
    gPlanChangesUnlogged = 0;
    gLoggedMode = out->mode;
    gLoggedIpd = out->ipdM;
    gLoggedScale = scale;
    gLoggedFrustumWhy = frustumWhy;
    if (!metric) {
        LogBoth(std::format("[MEVR-STEREO] stereo mode 0: {}{}{}", ModeName(0),
            !in.metric ? " (sfr2Metric = 0)" : !in.sfr2Active ? " (Stereo 2 isn't rendering the world: flat screen or another mode)"
                                                              : " (no head pose)",
            unlogged > 0 ? std::format(" ({} more changes in the last second)", unlogged) : std::string()));
        return;
    }
    ms::Fov const unionFov = ms::UnionWindow(ToFov(in.eyeFov[0]), ToFov(in.eyeFov[1]), in.fillH, in.fillV);
    std::string line = std::format(
        "[MEVR-STEREO] stereo mode {}: {}; eye distance {:.4f} m = {:.2f} uu at {:.1f} uu/m x {:.2f} extra depth; "
        "runtime FOV left {} right {} deg; the symmetric window +-{:.1f} x +-{:.1f} deg (fill {:.2f} x {:.2f}); "
        "the eyes' orientations {:.2f} deg apart",
        out->mode, ModeName(out->mode), out->ipdM, out->ipdUU, in.unitsPerMeter, in.depth, FovDeg(in.eyeFov[0]),
        FovDeg(in.eyeFov[1]), unionFov.right * kDeg, unionFov.up * kDeg, in.fillH, in.fillV, cantDeg);
    for (int pass = 0; pass < 2; ++pass) {
        int const eye = pass == 0 ? out->pass0Eye : 1 - out->pass0Eye;
        line += std::format("; pass {} = {} eye from ({:.2f}, {:.2f}, {:.2f}) uu", pass, eye == 0 ? "left" : "right",
            out->passOffsetUU[pass][0], out->passOffsetUU[pass][1], out->passOffsetUU[pass][2]);
        if (out->mode == 2) {
            line += std::format(" through tan ({:.3f}, {:.3f}, {:.3f}, {:.3f})", out->passTan[pass][0], out->passTan[pass][1],
                out->passTan[pass][2], out->passTan[pass][3]);
        }
    }
    if (out->mode == 2) {
        line += cpuKnown ? std::format("; the CPU window (1/P0, 1/P5) +-{:.4f} x +-{:.4f} {} the eyes' +-{:.4f} x +-{:.4f}", cpuTanH,
                               cpuTanV, contained ? "holds" : "does NOT hold", needH, needV)
                         : std::string("; the CPU window: no render seen yet");
    }
    if (out->mode == 1 && in.eyeFrustum) {
        line += uiNow ? "; the symmetric window because the game's UI is in the eye images (no HUD panel: "
                        "conversations, cutscenes, menus over the world)"
            : (mevr::host::render_levers::seam::g_oneDrawStereo.load(std::memory_order_relaxed) &&
               mevr::host::render_levers::OneDrawWindow())
                ? "; the symmetric window because one Draw a frame renders both eyes from the first render's visibility "
                  "(sharePass = 5, research/37: its occlusion tests see only its own frustum)"
            : !in.windowHoldsEyes
                ? "; the symmetric window because it doesn't hold the eyes' own frusta (the renderer's window is the left "
                  "eye's height at the image's aspect: an eye's own frustum would be culled at its outer edge; research/30 "
                  "section 9)"
                : "; the symmetric window because Halcyon's FOV override is off";
    } else if (out->mode == 1) {
        // Each eye's own frustum not asked for: LE1's sfr2MetricEyeFrustum = 0, or ME3's MetricEyeFrustum = 0 (1 by default
        // since 2026-11-17, with plugin/FlareWindow for its lamps' lens flares: research/le3-flares-and-window.md).
        line += "; the symmetric window by choice (each eye's own frustum off: sfr2MetricEyeFrustum / MetricEyeFrustum = 0)";
    }
    if (std::fabs(in.unitsPerMeter - kLifeSizeUnitsPerMeter) > 1.0f) {
        line += std::format("; NOT life-size: Shepard's skeleton is about {:.0f} uu/m, so the world reads {:.2f} times "
                            "its size (Halcyon's lean gain should be 2: life-size movement)",
            kLifeSizeUnitsPerMeter, kLifeSizeUnitsPerMeter / in.unitsPerMeter);
    }
    if (cantDeg > 0.5f) {
        line += "; the runtime's eyes are canted: each pass still renders facing the head (research/30, risks)";
    }
    if (unlogged > 0) {
        line += std::format(" ({} more changes in the last second)", unlogged);
    }
    LogBoth(line);
}

void NoteStereoSubmit(int mode, float const declaredPosition[2][3], float const declaredOrientation[2][4],
                      float const declaredFov[2][4]) noexcept {
    using namespace mevr::host::metric_stereo;
    {
        std::lock_guard lock(gMutex);
        gStatus.submitted = true;
        gStatus.submitMode = mode;
        for (int e = 0; e < 2; ++e) {
            for (int k = 0; k < 3; ++k) {
                gStatus.declaredPosition[e][k] = declaredPosition[e][k];
            }
            for (int k = 0; k < 4; ++k) {
                gStatus.declaredOrientation[e][k] = declaredOrientation[e][k];
                gStatus.declaredFov[e][k] = declaredFov[e][k];
            }
        }
    }
    std::uint64_t const now = GetTickCount64();
    if (mode == gLoggedSubmitMode && now - gLastSubmitLogMs < 60000) {
        return;
    }
    gLoggedSubmitMode = mode;
    gLastSubmitLogMs = now;
    float const dx = declaredPosition[1][0] - declaredPosition[0][0];
    float const dy = declaredPosition[1][1] - declaredPosition[0][1];
    float const dz = declaredPosition[1][2] - declaredPosition[0][2];
    LogBoth(std::format("[MEVR-STEREO] submit (pair rendered in mode {}): left eye declared at ({:.4f}, {:.4f}, {:.4f}) "
                        "fov {} deg, right eye at ({:.4f}, {:.4f}, {:.4f}) fov {} deg, {:.4f} m apart; orientation "
                        "({:.4f}, {:.4f}, {:.4f}, {:.4f})",
        mode, declaredPosition[0][0], declaredPosition[0][1], declaredPosition[0][2], FovDeg(declaredFov[0]),
        declaredPosition[1][0], declaredPosition[1][1], declaredPosition[1][2], FovDeg(declaredFov[1]),
        std::sqrt(dx * dx + dy * dy + dz * dz), declaredOrientation[0][0], declaredOrientation[0][1],
        declaredOrientation[0][2], declaredOrientation[0][3]));
}

int LifeSizeStereo() noexcept {
    using namespace mevr::host::metric_stereo;
    // Read once, at the renderer's first ini load: a measurement switch, not a player's setting.
    static int const answer = [] {
        namespace ini = mevr::ini;
        ini::File const file = mevr::vr::settings_file::Load(std::filesystem::path(mevr::host::SettingsPath())).file;
        bool upstream = ini::GetNumber(file, "Debug", "upstreamStereo").value_or(0.0) != 0.0;
        // The window holding both eyes is ME1's (its window is their union) and lets each eye render its own frustum (mode 2);
        // measured on ME2 it leaves the HUD panel pixel for pixel (research/30 section 9.3). 0 keeps the renderer's
        // aspect-derived window (mode 2 then only on Meta's runtime, where the renderer covers the eyes itself).
        bool cover = ini::GetNumber(file, "Debug", "stereoWindowCoversEyes").value_or(1.0) != 0.0;
        // The window as the eyes' union, each axis on its own (research/30 section 9.6, lane FL; off by default): the
        // sequels' aspect is the old side-by-side half, whose uniform cover overshoots the eyes' height.
        bool eyes = ini::GetNumber(file, "Stereo", "windowEyes").value_or(0.0) != 0.0;
        // A development build also takes %LOCALAPPDATA%\MEVR\stereo.measure ([Stereo] mode = upstream | nocover, window =
        // eyes | aspect, read at start), for a run whose ini a runner keeps exact (tools/le2-halcyon-run's ME3 preview
        // refuses an edited LE3MEVR.ini).
        wchar_t appData[MAX_PATH] = {};
        if (mevr::dev_channels::kDevelopmentBuild && GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH) > 0) {
            ini::Loaded const measure = ini::Read(std::filesystem::path(appData) / L"MEVR" / L"stereo.measure");
            std::string const text = ini::Lower(ini::Trim(ini::Get(measure.file, "Stereo", "mode").value_or("")));
            upstream = upstream || text == "upstream";
            cover = cover && text != "nocover";
            std::string const window = ini::Lower(ini::Trim(ini::Get(measure.file, "Stereo", "window").value_or("")));
            eyes = window == "eyes" || (eyes && window != "aspect");
        }
        int const bits = upstream ? 0 : (1 | (cover ? 2 : 0) | (cover && eyes ? 4 : 0));
        LogBoth(bits == 0 ? std::string("[MEVR-STEREO] life-size stereo OFF for this run ([Debug] upstreamStereo = 1): the "
                                        "renderer's upstream eye separation, convergence and lean")
                          : std::format("[MEVR-STEREO] life-size stereo: each eye from its real place, no convergence, the "
                                        "lean from the eyes' centre at 1:1 forward; {}",
                                (bits & 4) != 0 ? "the render window is the eyes' union, each axis on its own ([Stereo] "
                                                  "windowEyes = 1; each eye's own frustum, mode 2, where the renderer asks "
                                                  "for it)"
                                : (bits & 2) != 0 ? "the render window widened to hold both eyes' frusta (for each eye's own "
                                                    "frustum, mode 2, where the renderer asks for it)"
                                                  : "the renderer's own window ([Debug] stereoWindowCoversEyes = 0)"));
        return bits;
    }();
    return answer;
}

} // namespace MevrSeams
