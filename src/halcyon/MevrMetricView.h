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

// ---- The plan: published by the Present thread, latched by the game thread at pass 0 -----------------------------------
inline std::mutex g_planMutex;
inline Plan g_planNext;
inline Plan g_planLatched;                   // what the last pass 0 rendered with: the submit's routing and declared FOVs
inline bool g_planLatchedAny = false;
inline thread_local Plan t_latched;          // the game thread's copy for this Draw's two passes
inline std::atomic<unsigned> g_offsetLogs{0};
inline std::atomic<unsigned> g_frustumLogs{0};
inline std::atomic<unsigned> g_uncontained{0};

// Present thread, once per frame.
inline void Publish(Plan const& plan) noexcept
{
    std::lock_guard<std::mutex> lock(g_planMutex);
    g_planNext = plan;
}

// Game thread, at the head of each SFR pass: pass 0 takes the newest plan, pass 1 keeps it. Returns the pass's plan.
inline Plan const& Latch(bool replay) noexcept
{
    if (!replay)
    {
        std::lock_guard<std::mutex> lock(g_planMutex);
        t_latched = g_planNext;
        g_planLatched = t_latched;
        g_planLatchedAny = true;
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
// half eye) or the edit faulted (logged).
template <typename LogFn>
bool ApplyEyeOffset(std::uintptr_t sv, ViewFields const& f, Plan const& plan, bool replay, LogFn log) noexcept
{
    if (plan.mode == 0) return false;
    const int pass = replay ? 1 : 0;
    const Pass& p = plan.passes[pass];
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
    float orig[5] = {};
    const int wrote = WriteEyeFrustum(sv, f, p, zoom, orig);
    if (wrote <= 0)
    {
        if (wrote < 0) log("[MEVR-STEREO] eye frustum failed: SEH caught a fault");
        return;
    }
    // Containment: the CPU's window (culling) must hold this eye's frustum, or objects at that edge pop in. The CPU
    // half-window tangents are P[11] / P[0] and P[11] / P[5].
    const float w = (std::fabs(orig[2]) > 0.01f) ? orig[2] : 1.0f;
    const float z = (zoom > 0.01f) ? zoom : 1.0f;
    const float cpuTanH = (std::fabs(orig[3]) > 1e-6f) ? w / orig[3] : 0.0f;
    const float cpuTanV = (std::fabs(orig[4]) > 1e-6f) ? w / orig[4] : 0.0f;
    const float needH = std::fmax(std::fabs(p.tan[0]), std::fabs(p.tan[1])) * z;
    const float needV = std::fmax(std::fabs(p.tan[2]), std::fabs(p.tan[3])) * z;
    const bool contained = cpuTanH >= needH * 0.999f && cpuTanV >= needV * 0.999f;
    const unsigned n = g_frustumLogs.fetch_add(1, std::memory_order_relaxed);
    if (n < 4 || (n % 3600) == 0)
    {
        char line[400] = {};
        sprintf_s(line, "[MEVR-STEREO] pass %d frustum: tan L %.4f R %.4f U %.4f D %.4f x zoom %.3f -> p0 %.4f p5 %.4f p8 %.4f "
                        "p9 %.4f (game's own p8 %.4f p9 %.4f p11 %.4f); the CPU window +-%.4f x +-%.4f %s", pass, p.tan[0],
                  p.tan[1], p.tan[2], p.tan[3], z, p.p0 / z, p.p5 / z, p.p8, p.p9, orig[0], orig[1], orig[2], cpuTanH, cpuTanV,
                  contained ? "holds it" : "does NOT hold it");
        log(line);
    }
    if (!contained)
    {
        const unsigned k = g_uncontained.fetch_add(1, std::memory_order_relaxed);
        if (k < 4 || (k % 600) == 0)
        {
            char line[320] = {};
            sprintf_s(line, "[MEVR-STEREO] WARNING pass %d renders WIDER than the CPU's culling window: the eye needs +-%.4f x "
                            "+-%.4f (tangents), the CPU projection gives +-%.4f x +-%.4f: objects at that edge can pop in "
                            "(%u views so far)", pass, needH, needV, cpuTanH, cpuTanV, k + 1);
            log(line);
        }
    }
}

} // namespace MevrMetricView
