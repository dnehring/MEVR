// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// MEVR (ME VR Attempt) addition, GPL-3.0: life-size stereo's render-side edits for the sequels' Halcyon renderers
// (src/halcyon/le2, le3; research/30 "The sequels"). One copy for both trees: the FSceneView fields it edits sit at the
// same offsets in both (and in LE1, whose render_hook.cpp has its own older version of the same two edits:
// ApplyMetricEyeOffset, ApplyMetricFrustumSEH). Adapted from Halcyon's ApplyEyeOffset (the camera moved along its own
// axes, ViewOrigin + PreViewTranslation + the ViewMatrix translation row, the products rebuilt) and LE1's metric frustum.
//
// The plan (which eye each pass renders, its offset from the eye centre along the game camera's right/up/forward in UE
// units, and in mode 2 its own off-centre frustum) is made once per frame on the Present thread by our host
// (MevrSeams::PlanStereo, plugin/MetricStereo) and published here; the game thread latches it at pass 0 so both passes of
// one Draw render the same plan, and the submit routes the pair by the latched plan.
//
// What the latch takes (research/30 section 9.5, lane FXR): with life-size on, no pass ever renders upstream's stereo
// (half eye, convergence at the submit, SwapEyes):
//   - a plan made for stereo (mode 1 or 2) is used as published;
//   - a present shown flat expires the plan (Expire): the next pass 0 renders from the eyes' centre, mode 1, no frustum,
//     until a stereo present publishes again (a flat panel never shows one eye's off-centre frustum);
//   - a mode-0 plan (planned while the cinema screen was up, or nothing planned yet) is replaced by the last plan made
//     for stereo, else by the eyes' centre: the renderer's flags can change between the plan and the pass (the flags
//     flip on the Present thread before it publishes);
//   - a pass rendered for ME1's 3D cinema screen latches the screen's routing (pass 0 the left eye, no shift), so the
//     submit after the screen goes down routes that last screen pair as it was rendered.
// In mode 2, a view whose CPU window (culling) can't hold the eyes' frusta (a narrow view the renderer left unwidened)
// renders that pair on the symmetric window (mode 1) instead: the latch is lowered at pass 0 for both passes and the
// submit, so the edges are never culled and the declared window is the rendered one.

#include <Windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace MevrMetricView
{
// The FSceneView fields the edits touch (byte offsets; the same in LE1, ME2 and ME3).
struct ViewFields
{
    std::uintptr_t viewMatrix = 0x90;
    std::uintptr_t projection = 0xD0;
    std::uintptr_t translatedView = 0x190;
    std::uintptr_t translatedViewProj = 0x1D0;
    std::uintptr_t preViewTranslation = 0x250;
    std::uintptr_t viewProjection = 0x260;
    std::uintptr_t viewOrigin = 0x320;
};

struct Pass
{
    float offsetUU[3] = {};                      // camera right, up, forward from the eye centre (UE units)
    float p0 = 1.0f, p5 = 1.0f, p8 = 0.0f, p9 = 0.0f;   // mode 2: UE3's off-centre terms (w = z)
    float tan[4] = {};                           // mode 2: the frustum's left, right, up, down tangents
};
struct Plan
{
    int mode = 0;        // 0 upstream (half eye along the camera's right, convergence at submit), 1 life-size on the
                         // symmetric window, 2 life-size with each eye's own frustum on the GPU matrix
    int pass0Eye = 0;    // the eye pass 0 renders (pass 1: the other)
    Pass passes[2];
};

// What the Draw renders, as the renderer's own flags say at pass 0.
enum class Frame : int
{
    World = 0,          // the world in stereo (the SFR branch's own path)
    CinemaScreen = 1,   // ME1's 3D cinema screen: the renderer's parallel cameras, the plan unused
};

// Where a latched plan came from (the latch's log and the tests).
enum class Source : int
{
    Published = 0,      // the newest plan, made for stereo (or upstream's, life-size off)
    LastStereo = 1,     // the newest was mode 0 (the screen's, or a flag flip before the publish): the last stereo plan
    Centre = 2,         // nothing planned for stereo yet: both passes from the eyes' centre
    Expired = 3,        // the last present was flat: from the eyes' centre
    CinemaScreen = 4,   // the cinema screen's routing
};

inline const char* SourceName(Source s) noexcept
{
    switch (s)
    {
    case Source::Published: return "the newest plan";
    case Source::LastStereo: return "the last plan made for stereo (the newest is upstream's mode 0)";
    case Source::Centre: return "the eyes' centre (no plan made for stereo yet)";
    case Source::Expired: return "the eyes' centre (the last present was flat: the plan expired)";
    case Source::CinemaScreen: return "the cinema screen's routing (pass 0 the left eye, no shift)";
    }
    return "?";
}

// Both passes from the eyes' centre: life-size, the symmetric window, no offset.
inline Plan CentrePlan() noexcept
{
    Plan p;
    p.mode = 1;
    p.pass0Eye = 0;
    return p;
}

// The pure decision (unit-tested): what pass 0 latches from the published state.
struct LatchInput
{
    Plan next;                // the newest published plan
    bool expired = false;     // a flat present since it was published
    Plan lastStereo;          // the newest plan published with mode 1 or 2
    bool lastStereoAny = false;
    Frame frame = Frame::World;
    bool lifeSize = true;     // MevrSeams::LifeSizeStereo's bit: false keeps upstream exactly (a measurement)
};
struct LatchChoice
{
    Plan plan;
    Source source = Source::Published;
};
inline LatchChoice ChooseLatch(LatchInput const& in) noexcept
{
    LatchChoice out;
    if (in.frame == Frame::CinemaScreen)
    {
        // The screen's pair: pass 0 the left camera, nothing shifted at the submit, whatever life-size says.
        out.plan = CentrePlan();
        out.source = Source::CinemaScreen;
        return out;
    }
    if (!in.lifeSize)
    {
        out.plan = in.next;   // upstream's stereo, exactly (a measurement switch)
        return out;
    }
    if (in.expired)
    {
        out.plan = CentrePlan();
        out.source = Source::Expired;
        return out;
    }
    if (in.next.mode != 0)
    {
        out.plan = in.next;
        return out;
    }
    if (in.lastStereoAny)
    {
        out.plan = in.lastStereo;
        out.source = Source::LastStereo;
        return out;
    }
    out.plan = CentrePlan();
    out.source = Source::Centre;
    return out;
}

// The CPU window (tangents, from the view's projection: P[11] / P[0], P[11] / P[5]) against both passes' frusta x zoom.
inline bool WindowHoldsPlan(float cpuTanH, float cpuTanV, Plan const& plan, float zoom) noexcept
{
    const float z = (zoom > 0.01f) ? zoom : 1.0f;
    float needH = 0.0f, needV = 0.0f;
    for (const Pass& p : plan.passes)
    {
        needH = std::fmax(needH, std::fmax(std::fabs(p.tan[0]), std::fabs(p.tan[1])) * z);
        needV = std::fmax(needV, std::fmax(std::fabs(p.tan[2]), std::fabs(p.tan[3])) * z);
    }
    return cpuTanH >= needH * 0.999f && cpuTanV >= needV * 0.999f;
}

// ---- The plan: published by the Present thread, latched by the game thread at pass 0 -----------------------------------
inline std::mutex g_planMutex;
inline Plan g_planNext;
inline bool g_planExpired = false;           // a flat present since the newest publish
inline Plan g_planLastStereo;                // the newest plan published with mode 1 or 2
inline bool g_planLastStereoAny = false;
inline Plan g_planLatched;                   // what the last pass 0 rendered with: the submit's routing and declared FOVs
inline bool g_planLatchedAny = false;
inline thread_local Plan t_latched;          // the game thread's copy for this Draw's two passes
inline std::atomic<unsigned> g_offsetLogs{0};
inline std::atomic<unsigned> g_frustumLogs{0};
inline std::atomic<unsigned> g_uncontained{0};
inline std::atomic<int> g_lastSource{-1};
inline std::atomic<unsigned> g_sourceLogs{0};
inline std::atomic<unsigned> g_sourceCounts[5] = {};
inline std::atomic<bool> g_narrowEpisode{false};

// Present thread, once per frame shown in stereo.
inline void Publish(Plan const& plan) noexcept
{
    std::lock_guard<std::mutex> lock(g_planMutex);
    g_planNext = plan;
    g_planExpired = false;
    if (plan.mode != 0)
    {
        g_planLastStereo = plan;
        g_planLastStereoAny = true;
    }
}

// Present thread, once per frame shown flat (a flat panel, a source wait): the next pass 0 renders from the eyes' centre.
inline void Expire() noexcept
{
    std::lock_guard<std::mutex> lock(g_planMutex);
    g_planExpired = true;
}

// Game thread, at the head of each SFR pass: pass 0 decides (ChooseLatch), pass 1 keeps it. Returns the pass's plan.
template <typename LogFn>
Plan const& Latch(bool replay, Frame frame, bool lifeSize, LogFn log) noexcept
{
    if (replay) return t_latched;
    LatchChoice choice;
    {
        std::lock_guard<std::mutex> lock(g_planMutex);
        LatchInput in;
        in.next = g_planNext;
        in.expired = g_planExpired;
        in.lastStereo = g_planLastStereo;
        in.lastStereoAny = g_planLastStereoAny;
        in.frame = frame;
        in.lifeSize = lifeSize;
        choice = ChooseLatch(in);
        t_latched = choice.plan;
        g_planLatched = t_latched;
        g_planLatchedAny = true;
    }
    const int s = static_cast<int>(choice.source);
    g_sourceCounts[s].fetch_add(1, std::memory_order_relaxed);
    if (g_lastSource.exchange(s, std::memory_order_relaxed) != s)
    {
        // A change of source: a few a minute (flat panels, the cinema screen); every one logged up to 200, then 1 in 50.
        const unsigned n = g_sourceLogs.fetch_add(1, std::memory_order_relaxed);
        if (n < 200 || (n % 50) == 0)
        {
            char line[400] = {};
            sprintf_s(line, "[MEVR-STEREO] pass 0 renders %s: mode %d (passes so far: published %u, last stereo %u, centre %u, "
                            "expired %u, screen %u)", SourceName(choice.source), choice.plan.mode,
                      g_sourceCounts[0].load(std::memory_order_relaxed), g_sourceCounts[1].load(std::memory_order_relaxed),
                      g_sourceCounts[2].load(std::memory_order_relaxed), g_sourceCounts[3].load(std::memory_order_relaxed),
                      g_sourceCounts[4].load(std::memory_order_relaxed));
            log(line);
        }
    }
    return t_latched;
}

// Present thread, at the submit: the plan the newest pair was rendered with (the game thread may already be a frame
// ahead: at a change of mode the pair can carry the next frame's plan for one frame). False before any pass rendered.
inline bool LatchedPlan(Plan* out) noexcept
{
    std::lock_guard<std::mutex> lock(g_planMutex);
    if (out != nullptr) *out = g_planLatched;
    return g_planLatchedAny;
}

// Game thread, pass 0 in mode 2: this Draw renders on the symmetric window (mode 1), both passes and the submit.
inline void LowerLatchToSymmetric() noexcept
{
    std::lock_guard<std::mutex> lock(g_planMutex);
    t_latched.mode = 1;
    g_planLatched.mode = 1;
}

// Tests only: the published state back to a fresh process's.
inline void ResetForTests() noexcept
{
    std::lock_guard<std::mutex> lock(g_planMutex);
    g_planNext = Plan{};
    g_planExpired = false;
    g_planLastStereo = Plan{};
    g_planLastStereoAny = false;
    g_planLatched = Plan{};
    g_planLatchedAny = false;
    t_latched = Plan{};
    g_lastSource.store(-1, std::memory_order_relaxed);
    g_narrowEpisode.store(false, std::memory_order_relaxed);
    for (auto& c : g_sourceCounts) c.store(0, std::memory_order_relaxed);
}

inline void Mul4x4(float* out, const float* a, const float* b) noexcept
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
        {
            float v = 0.0f;
            for (int k = 0; k < 4; ++k) v += a[r * 4 + k] * b[k * 4 + c];
            out[r * 4 + c] = v;
        }
}

// Move the view's camera by (right, up, forward) UE units along its own axes (the ViewMatrix's columns: right, up,
// forward in world space): PreViewTranslation and ViewOrigin, the ViewMatrix's translation row (view space = -C . basis),
// then both view-projection products from the current projection. SEH-guarded; false on a fault. `world`: the move in
// world units, for the log.
inline bool MoveAlongCamera(std::uintptr_t sv, ViewFields const& f, float right, float up, float forward,
    float world[3]) noexcept
{
    __try
    {
        auto* view = reinterpret_cast<float*>(sv + f.viewMatrix);
        const float rX = view[0], rY = view[4], rZ = view[8];
        const float uX = view[1], uY = view[5], uZ = view[9];
        const float fX = view[2], fY = view[6], fZ = view[10];
        const float wx = right * rX + up * uX + forward * fX;
        const float wy = right * rY + up * uY + forward * fY;
        const float wz = right * rZ + up * uZ + forward * fZ;
        volatile float* pvt = reinterpret_cast<volatile float*>(sv + f.preViewTranslation);
        volatile float* vo = reinterpret_cast<volatile float*>(sv + f.viewOrigin);
        pvt[0] -= wx; pvt[1] -= wy; pvt[2] -= wz;
        vo[0] += wx;  vo[1] += wy;  vo[2] += wz;
        view[12] -= right;
        view[13] -= up;
        view[14] -= forward;
        const auto* proj = reinterpret_cast<const float*>(sv + f.projection);
        const auto* tview = reinterpret_cast<const float*>(sv + f.translatedView);
        Mul4x4(reinterpret_cast<float*>(sv + f.viewProjection), view, proj);
        Mul4x4(reinterpret_cast<float*>(sv + f.translatedViewProj), tview, proj);
        if (world != nullptr) { world[0] = wx; world[1] = wy; world[2] = wz; }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The eye offset of this pass (modes 1 and 2), in place of Halcyon's half eye along the camera's right. Before the lean
// and head look (both translations are along the same pre-look camera axes, so the order doesn't matter; the head
// rotation then turns the view about the eye's own place). False when the plan is upstream's (the caller applies its
// half eye) or the edit faulted (logged). A pass from the eyes' centre (no offset) leaves the view untouched.
template <typename LogFn>
bool ApplyEyeOffset(std::uintptr_t sv, ViewFields const& f, Plan const& plan, bool replay, LogFn log) noexcept
{
    if (plan.mode == 0) return false;
    const int pass = replay ? 1 : 0;
    const Pass& p = plan.passes[pass];
    if (p.offsetUU[0] == 0.0f && p.offsetUU[1] == 0.0f && p.offsetUU[2] == 0.0f) return true;
    float world[3] = {};
    if (!MoveAlongCamera(sv, f, p.offsetUU[0], p.offsetUU[1], p.offsetUU[2], world))
    {
        log("[MEVR-STEREO] eye offset failed: SEH caught a fault");
        return true;
    }
    const unsigned n = g_offsetLogs.fetch_add(1, std::memory_order_relaxed);
    if (n < 8 || (n % 3600) == 0)
    {
        char line[320] = {};
        sprintf_s(line, "[MEVR-STEREO] pass %d (eye %d) from its eye: right %.2f up %.2f forward %.2f uu (world %.1f,%.1f,%.1f), "
                        "mode %d", pass, pass == 0 ? plan.pass0Eye : 1 - plan.pass0Eye, p.offsetUU[0], p.offsetUU[1],
                  p.offsetUU[2], world[0], world[1], world[2], plan.mode);
        log(line);
    }
    return true;
}

// The CPU's half-window tangents from the view's projection (P[11] / P[0], P[11] / P[5]) and the game's p8, p9, p11, p0,
// p5. Returns 1 read, 0 orthographic, -1 fault.
inline int ReadCpuWindow(std::uintptr_t sv, ViewFields const& f, float* tanH, float* tanV, float orig[5]) noexcept
{
    __try
    {
        const float* P = reinterpret_cast<const float*>(sv + f.projection);
        if (std::fabs(P[15]) > 0.01f) return 0;
        orig[0] = P[8]; orig[1] = P[9]; orig[2] = P[11]; orig[3] = P[0]; orig[4] = P[5];
        const float w = (std::fabs(P[11]) > 0.01f) ? P[11] : 1.0f;
        *tanH = (std::fabs(P[0]) > 1e-6f) ? w / P[0] : 0.0f;
        *tanV = (std::fabs(P[5]) > 1e-6f) ? w / P[5] : 0.0f;
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Mode 2: the GPU's matrix (TranslatedViewProjection, what the shaders read) gets this eye's own off-centre frustum,
// scaled by `zoom` (the renderer's ADS magnification: the tangent of the rendered window over the declared one, 1 when
// not zoomed). The CPU's projection and ViewProjection keep the renderer's symmetric window (culling, the HUD's projected
// markers). After the last edit that rebuilds the products (the renderer's FOV fill) and before its [INVMAT] refresh.
// Returns 1 written, 0 orthographic or not mode 2 (untouched), -1 fault. `orig`: the game's p8, p9, p11, p0, p5.
inline int WriteEyeFrustum(std::uintptr_t sv, ViewFields const& f, Pass const& p, float zoom, float orig[5]) noexcept
{
    __try
    {
        const float* PM = reinterpret_cast<const float*>(sv + f.projection);
        if (std::fabs(PM[15]) > 0.01f) return 0;
        const float* TVM = reinterpret_cast<const float*>(sv + f.translatedView);
        float* TVPM = reinterpret_cast<float*>(sv + f.translatedViewProj);
        float P[16];
        for (int i = 0; i < 16; ++i) P[i] = PM[i];
        orig[0] = P[8]; orig[1] = P[9]; orig[2] = P[11]; orig[3] = P[0]; orig[4] = P[5];
        const float w = (std::fabs(P[11]) > 0.01f) ? P[11] : 1.0f;
        const float z = (zoom > 0.01f) ? zoom : 1.0f;
        // A frustum's tangents scaled by z: p0 and p5 divide by z, the off-centre terms (ratios of tangents) stay.
        P[0] = p.p0 / z * w;
        P[5] = p.p5 / z * w;
        P[8] = p.p8 * w;
        P[9] = p.p9 * w;
        Mul4x4(TVPM, TVM, P);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

template <typename LogFn>
void ApplyEyeFrustum(std::uintptr_t sv, ViewFields const& f, Plan const& plan, bool replay, float zoom, LogFn log) noexcept
{
    if (plan.mode != 2) return;
    const int pass = replay ? 1 : 0;
    const Pass& p = plan.passes[pass];
    const float z = (zoom > 0.01f) ? zoom : 1.0f;
    // Containment, before writing (pass 0 decides for the Draw): the CPU's window (culling) must hold both eyes' frusta,
    // or objects at that edge are culled. A view the renderer left narrow (ME2's narrow view without a weapon: a
    // conversation's first frames, a camera blending back from one) renders this pair on the symmetric window instead.
    if (!replay)
    {
        float cpuTanH = 0.0f, cpuTanV = 0.0f, read[5] = {};
        const int got = ReadCpuWindow(sv, f, &cpuTanH, &cpuTanV, read);
        if (got < 0)
        {
            log("[MEVR-STEREO] eye frustum failed: SEH caught a fault reading the view's projection");
            return;
        }
        if (got == 0) return;   // orthographic: untouched
        if (!WindowHoldsPlan(cpuTanH, cpuTanV, plan, z))
        {
            LowerLatchToSymmetric();
            const unsigned k = g_uncontained.fetch_add(1, std::memory_order_relaxed);
            if (!g_narrowEpisode.exchange(true, std::memory_order_relaxed) || (k % 600) == 0)
            {
                float needH = 0.0f, needV = 0.0f;
                for (const Pass& q : plan.passes)
                {
                    needH = std::fmax(needH, std::fmax(std::fabs(q.tan[0]), std::fabs(q.tan[1])) * z);
                    needV = std::fmax(needV, std::fmax(std::fabs(q.tan[2]), std::fabs(q.tan[3])) * z);
                }
                char line[400] = {};
                sprintf_s(line, "[MEVR-STEREO] this view's CPU window +-%.4f x +-%.4f (tangents) doesn't hold the eyes' frusta "
                                "+-%.4f x +-%.4f: the pair renders on the symmetric window (mode 1), nothing culled at the "
                                "edges (%u such views so far)", cpuTanH, cpuTanV, needH, needV, k + 1);
                log(line);
            }
            return;
        }
        if (g_narrowEpisode.exchange(false, std::memory_order_relaxed))
            log("[MEVR-STEREO] the CPU window holds the eyes' frusta again: each eye's own frustum (mode 2)");
    }
    float orig[5] = {};
    const int wrote = WriteEyeFrustum(sv, f, p, zoom, orig);
    if (wrote <= 0)
    {
        if (wrote < 0) log("[MEVR-STEREO] eye frustum failed: SEH caught a fault");
        return;
    }
    const float w = (std::fabs(orig[2]) > 0.01f) ? orig[2] : 1.0f;
    const float cpuTanH = (std::fabs(orig[3]) > 1e-6f) ? w / orig[3] : 0.0f;
    const float cpuTanV = (std::fabs(orig[4]) > 1e-6f) ? w / orig[4] : 0.0f;
    const unsigned n = g_frustumLogs.fetch_add(1, std::memory_order_relaxed);
    if (n < 4 || (n % 3600) == 0)
    {
        char line[400] = {};
        sprintf_s(line, "[MEVR-STEREO] pass %d frustum: tan L %.4f R %.4f U %.4f D %.4f x zoom %.3f -> p0 %.4f p5 %.4f p8 %.4f "
                        "p9 %.4f (game's own p8 %.4f p9 %.4f p11 %.4f); the CPU window +-%.4f x +-%.4f holds it", pass,
                  p.tan[0], p.tan[1], p.tan[2], p.tan[3], z, p.p0 / z, p.p5 / z, p.p8, p.p9, orig[0], orig[1], orig[2],
                  cpuTanH, cpuTanV);
        log(line);
    }
}

} // namespace MevrMetricView
