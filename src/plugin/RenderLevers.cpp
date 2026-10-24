// SPDX-License-Identifier: MIT
#include "plugin/RenderLevers.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "plugin/DrawReplay.hpp"

#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <format>
#include <string>
#include <vector>

namespace mevr::host::render_levers {
namespace {

// research/22 and /32 (Ghidra; .pdata starts checked): SceneRenderB = FSceneRenderer::Render (one call per scene
// render, render thread). The Umbra commander's command() (0x8C34C0) is BioWare's side of the resolve: code 0
// begins it (stamping appSeconds, as a float, at 0x1795D40), 3 reports visible instances, 5 flushes the new
// occluders into depth and issues queries, 6 reads query results (spinning while a needed one isn't ready until
// 10 ms after the stamp; the float's 2 s precision at that magnitude makes the wait anything up to a second), 1
// ends it. 0x76C1D0 is the opaque base pass of one view (not a depth pre-pass: research/32).
constexpr std::uintptr_t kSceneRenderRva = 0x76EE20;
constexpr std::uintptr_t kCommanderCommandRva = 0x8C34C0;
constexpr std::uintptr_t kUmbraResolveStartRva = 0x1795D40;
// FD3D11DynamicRHI::GetQueryData(this, query, out, size, bWait) (Ghidra, 2026-10-23): with bWait it polls GetData
// until the result is ready or 0.5 s passed. UE3's per-primitive occlusion asks for "last frame's" query with
// bWait; in Stereo 2 the second eye's "last frame" is this frame's first eye, still on the GPU (1.3 ms behind), so
// the second render spins for it: 1.3-1.6 ms a frame at a lighter spot, 0.5 at the heaviest (the fake-headset
// measurements). ue3QueryWait = 0 clears bWait in a frame's second render: an unready query counts as visible.
constexpr std::uintptr_t kGetQueryDataRva = 0x9A04B0;
// 0x8C1AE0(which): SceneRenderB picks the Umbra commander (0x1798E88[which], two of them) for a scene render; in
// gameplay always 0, so both eyes resolve on commander 0 and share its query pool. That works with the game's one
// view state (one Umbra camera double-buffering its queries across the two resolves) and breaks with a state per
// eye: two cameras on one pool re-issue each other's pending queries, so the second eye's results are never ready
// (7,000 polls a frame, 2026-10-23). Giving the second render commander 1 was tried and hangs: its resolve waits in
// the Umbra command ring for a resolve command the game thread only queues for commander 0 (research/35, 20).
constexpr std::uintptr_t kSetCommanderRva = 0x8C1AE0;

constexpr int kSlots = 6;  // render places tracked per frame (the last collects the rest)

// sharePass (research/32, lever 1): the second scene render of a frame runs on the first one's renderer.
// SceneRenderB's Umbra branch, after InitViews, as the game calls it (Ghidra, research/32):
constexpr std::uintptr_t kDeleteRendererRva = 0x4ED700;   // FSceneRenderer deleting destructor (this, flags)
constexpr std::uintptr_t kAllocateTargetsRva = 0x772EB0;  // GSceneRenderTargets.Allocate(this, x, y, 0)
constexpr std::uintptr_t kWireframeStateRva = 0x8C1AE0;   // (byte): 0 on the normal path
constexpr std::uintptr_t kDpgLightTargetsRva = 0x77BE00;  // (renderer, dpg): per light and view, into scene targets
constexpr std::uintptr_t kPrepassViewsRva = 0x8BF8F0;     // RenderUmbraBasePass(renderer): the opaque base pass per view
constexpr std::uintptr_t kTranslucencyCheckRva = 0x7666A0;
constexpr std::uintptr_t kBeforeLightsRva = 0x759240;     // only when GSkipBeforeLights == 0
constexpr std::uintptr_t kDpgPassRva = 0x76D650;          // (renderer, dpg, uint* dirty): the lights
constexpr std::uintptr_t kDpgLightsRva = 0x76D0A0;        // (renderer, dpg, flag, uint* dirty, arg5): translucency etc.
constexpr std::uintptr_t kForegroundRva = 0x76CA70;       // (renderer, 2, int* out, uint* dirty, arg5)
constexpr std::uintptr_t kFinishRva = 0x76DD10;           // (renderer, flag, dirty)
constexpr std::uintptr_t kSceneRenderTargetsRva = 0x167D0E0;
constexpr std::uintptr_t kDynamicRhiRva = 0x17938C8;      // GDynamicRHI; vtable +0x628 begin, +0x620 end
constexpr std::uintptr_t kSceneFrameRva = 0x178779C;      // incremented per scene render
constexpr std::uintptr_t kDepthDirtyRva = 0x1795D38;
constexpr std::uintptr_t kSkipBeforeLightsRva = 0x167D49C;
constexpr std::uintptr_t kCommandersRva = 0x1798E88;      // BioWare's Umbra commanders [index]
constexpr std::uintptr_t kCommanderIndexRva = 0x1795D28;
// FSceneRenderer (0x110 bytes): +0x00 scene, +0x08 the family copy (+0x18 render target, +0x28 show flags),
// +0x5C Umbra path, +0x60/+0x68 views (FViewInfo, 0x12F0 apart), +0xD8 the scene-begun flag.
// FSceneView (the first 0x4E4 bytes of FViewInfo; copy constructor 0x763450): plain matrices and vectors
// in these ranges, owned containers at 0x44, 0x140 (the frustum's planes) and 0x330 (never copied).
constexpr std::size_t kViewStride = 0x12F0;
struct ByteRange {
    std::size_t begin, end;
};
// The word at +0x8C differs between a frame's two eye passes (the fake-headset run, 2026-10-23: the only
// difference the pairing test found), so it goes with the per-pass values.
constexpr ByteRange kViewPlainRanges[] = {{0x8C, 0x140}, {0x190, 0x330}, {0x480, 0x4B0}, {0x4C0, 0x4D0}};
constexpr std::size_t kViewCompareEnd = 0x8C;  // the view block compared for compatibility: +0x60 to here

using tSceneRender = void(void* renderer);
using tDeleteRenderer = void*(void* renderer, std::uint64_t flags);
using tDpgLightTargets = void(void* renderer, int dpg);
using tCommand = void(void* commander, unsigned code);
using tGetQueryData = int(void* rhi, void* query, void* out, unsigned size, int wait);

tSceneRender* gSceneRenderOrig = nullptr;      // written before the hook goes live (core/Hook)
tDeleteRenderer* gDeleteRendererOrig = nullptr;
tDpgLightTargets* gDpgLightTargetsOrig = nullptr;
tCommand* gCommandOrig = nullptr;
tGetQueryData* gGetQueryDataOrig = nullptr;
bool gQueryDataHooked = false;
using tSetCommander = void(std::uint8_t which);
tSetCommander* gSetCommanderOrig = nullptr;
bool gSetCommanderHooked = false;
std::atomic<int> gUe3QueryWait{1};  // 1 the game's own; 0 a frame's second render never waits for a query result
std::atomic<std::uint64_t> gQueryWaitsSkipped{0};  // reads that would have waited, this period
std::atomic<int> gPerEyeViewState{0};  // 1: Halcyon renders each Stereo 2 eye through its own view state in gameplay too

// ---- The second eye's own occlusion history (research/35, attempts 34-35) ------------------------------------------
// FSceneViewState keeps the engine's per-primitive occlusion history as a TSet<FPrimitiveOcclusionHistory> at +0xD10:
// 0x48 bytes (the sparse array's data, num, max; its inline allocation bits, their pointer and counts; the free list;
// the hash's inline slot, pointer and size), elements of 0x28 (primitive, pending query, last visible time, last
// considered time, pixel fraction, occluded, hash links), read and written by UpdatePrimitiveOcclusion (0x762550) in
// every render's InitViews. Both Stereo 2 eyes render through one state, so a frame's second render finds the first
// eye's query of the SAME frame as "last frame's" and spins in GetQueryData (bWait) until the GPU has drawn it: 4-5 ms
// at a light spot (the first eye's whole render is 2.4). With a set of its own the second eye reads its own previous
// frame's query, long finished, and draws what it saw last frame rather than what the other eye sees. The set is
// swapped in around the frame's second scene render and out after it; the pool its queries come from (+0x88) stays
// shared, like Umbra's. A new view state (its set empty at the frame's first render) or another state pointer drops the
// second eye's set: its queries died with the pool (the set's memory leaks, once per level).
constexpr std::uintptr_t kStateOcclusionHistory = 0xD10;
constexpr std::size_t kOcclusionHistoryBytes = 0x48;
constexpr std::uintptr_t kRendererViews = 0x60;    // FSceneRenderer::Views data (FViewInfo[], 0x12F0 each), count at +0x68
constexpr std::uintptr_t kViewState = 0x8;         // FSceneView::State
// Off by default since 2026-10-24 (research/35, attempt 37): in the headset it made every ~10th frame draw three
// times the meshes (p90 8,500 draws against 2,600; 22-35 ms frames, GPU 23-30 ms), and the second eye's wait only
// moved into Umbra's own query reads (research/35 attempts 36-37). perEyeOcclusion = 1 for experiments.
std::atomic<int> gPerEyeOcclusion{0};
alignas(16) std::uint8_t gEyeOneHistory[kOcclusionHistoryBytes]{};  // the second eye's set while the first eye's is in the state
void* gEyeOneHistoryState = nullptr;  // the state it belongs to
void* gFrameFirstState = nullptr;     // render 0's state this frame
bool gFrameFirstStateFresh = false;   // its set was empty at render 0: a new state
std::atomic<std::uint64_t> gHistorySwaps{0}, gHistoryResets{0}, gHistorySkipped{0};

template <typename T>
T& Field(std::uint8_t* object, std::size_t offset);  // defined below

// ---- Umbra's occlusion queries drawn with the eye's own matrix (research/35, attempt 36) -------------------------
// UmbraCommander::IssueOcclusionQueries (0x8B6550) sets the RHI's view matrix once per render from the view's
// ViewProjectionMatrix at +0x260 (world space) and draws its occluder test geometry with it against the depth buffer
// the Umbra base pass drew with TranslatedViewProjection (+0x1D0). The engine's own occlusion test (0x759240) uses
// +0x1D0 as well. In the life-size stereo's mode 2 only +0x1D0 carries the eye's off-centre frustum and +0x260 keeps
// the symmetric window (research/30: the HUD markers), so Umbra's queries land up to 18 degrees from the geometry
// they test and objects come and go with every head motion. Around that call the hook puts the world-space
// equivalent of +0x1D0 into +0x260: V x P_eye = T(PreViewTranslation) x TVP, since V = T(PreViewTranslation) x TV
// (the sign of the translation is checked against V and TV themselves on the first call). The RHI copies the matrix
// into its constants at once, so the original bytes go back right after.
constexpr std::uintptr_t kUmbraIssueQueriesRva = 0x8B6550;
constexpr std::uintptr_t kCommanderView = 0x68;        // UmbraCommander's view (FViewInfo*)
constexpr std::uintptr_t kCommanderViewParamsSet = 0x5C;  // 0 until SetViewParameters was called this render
constexpr std::uintptr_t kViewViewMatrix = 0x90;
constexpr std::uintptr_t kViewTranslatedViewMatrix = 0x190;
constexpr std::uintptr_t kViewTranslatedViewProj = 0x1D0;
constexpr std::uintptr_t kViewPreViewTranslation = 0x250;
constexpr std::uintptr_t kViewViewProj = 0x260;
using tUmbraIssueQueries = void(void* commander);
tUmbraIssueQueries* gUmbraIssueQueriesOrig = nullptr;
bool gUmbraIssueQueriesHooked = false;
std::atomic<int> gUmbraEyeMatrix{1};
std::atomic<std::uint64_t> gUmbraMatrixFixed{0}, gUmbraMatrixSame{0};
int gTranslationSign = 0;  // +1: V = T(+pvt) x TV; -1: T(-pvt); 0: not known yet

// T(s * pvt) x M for row vectors: rows 0-2 of M unchanged, row 3 += s * (pvt.x row0 + pvt.y row1 + pvt.z row2).
void TranslateRows(float const* m, float const* pvt, float s, float* out) {
    std::memcpy(out, m, sizeof(float) * 16);
    for (int c = 0; c < 4; ++c) {
        out[12 + c] = m[12 + c] + s * (pvt[0] * m[c] + pvt[1] * m[4 + c] + pvt[2] * m[8 + c]);
    }
}

bool NearlySame(float const* a, float const* b) {
    for (int i = 0; i < 16; ++i) {
        float const scale = std::max(1.0f, std::max(std::abs(a[i]), std::abs(b[i])));
        if (std::abs(a[i] - b[i]) > 1e-3f * scale) {
            return false;
        }
    }
    return true;
}

void UmbraIssueQueriesHook(void* commander) {
    auto* const c = static_cast<std::uint8_t*>(commander);
    auto* const view = Field<std::uint8_t*>(c, kCommanderView);
    if (gUmbraEyeMatrix.load(std::memory_order_relaxed) == 0 || view == nullptr || Field<int>(c, kCommanderViewParamsSet) != 0) {
        gUmbraIssueQueriesOrig(commander);
        return;
    }
    auto* const V = reinterpret_cast<float const*>(view + kViewViewMatrix);
    auto* const TV = reinterpret_cast<float const*>(view + kViewTranslatedViewMatrix);
    auto* const TVP = reinterpret_cast<float const*>(view + kViewTranslatedViewProj);
    auto* const pvt = reinterpret_cast<float const*>(view + kViewPreViewTranslation);
    auto* const VP = reinterpret_cast<float*>(view + kViewViewProj);
    if (gTranslationSign == 0) {
        float plus[16], minus[16];
        TranslateRows(TV, pvt, 1.0f, plus);
        TranslateRows(TV, pvt, -1.0f, minus);
        gTranslationSign = NearlySame(plus, V) ? 1 : (NearlySame(minus, V) ? -1 : 0);
        log::Info("render levers: the view matrix is the translated one moved by {} the pre-view translation{}",
            gTranslationSign > 0 ? "plus" : (gTranslationSign < 0 ? "minus" : "neither"),
            gTranslationSign == 0 ? ": umbraEyeMatrix can't act this render" : "");
        if (gTranslationSign == 0) {
            gUmbraIssueQueriesOrig(commander);
            return;
        }
    }
    float eye[16];
    TranslateRows(TVP, pvt, static_cast<float>(gTranslationSign), eye);
    if (NearlySame(eye, VP)) {
        gUmbraMatrixSame.fetch_add(1, std::memory_order_relaxed);  // mode 1: nothing to fix
        gUmbraIssueQueriesOrig(commander);
        return;
    }
    float saved[16];
    std::memcpy(saved, VP, sizeof(saved));
    std::memcpy(VP, eye, sizeof(eye));
    gUmbraIssueQueriesOrig(commander);
    std::memcpy(VP, saved, sizeof(saved));
    gUmbraMatrixFixed.fetch_add(1, std::memory_order_relaxed);
}
std::uint8_t* gExe = nullptr;
bool gHooked = false;
bool gShareHooked = false;
bool gReplayHooked = false;     // the light-targets hook marks where the draw replay (sharePass = 2) starts
bool gCommanderHooked = false;  // the commander's command(): the resolve's parts timed, the query wait switch

std::atomic<int> gUmbraQueryWait{1};  // 1 the game's own wait for query results; 0 never (an unready query = visible)
std::atomic<int> gPCoresOnly{0};
std::atomic<int> gSharePass{0};
std::atomic<bool> gShareFailed{false};   // an exception in a replay: off for the session
std::atomic<bool> gReplayFailed{false};  // the draw replay failed once: the engine's passes from then on
std::atomic<std::uint64_t> gReplayed{0}, gReplayFallbacks{0};

// Render thread (the deleter can also run on the game thread, hence the atomics it reads).
std::atomic<void*> gKeepCandidate{nullptr};  // the renderer whose deletion to hold back, while it renders
std::atomic<DWORD> gRenderThread{0};
void* gKept = nullptr;  // a finished renderer kept for the next compatible render
std::atomic<std::uint64_t> gShared{0}, gShareMismatches{0}, gShareUnused{0}, gShareExceptions{0};
// Why a first render wasn't kept (diagnostics, 2026-10-23: the first VR run kept none): ineligible renders and the
// last one's fields; renders finished without the deleter seeing the candidate; the deleter's calls and those on
// the candidate with other flags or from another thread (the last such flags).
std::atomic<std::uint64_t> gShareIneligible{0}, gShareNotKept{0}, gDeleteCalls{0}, gDeleteCandidateMissed{0};
std::atomic<int> gLastIneligible5C{0}, gLastIneligibleViews{0}, gLastIneligibleFlags{0};
std::atomic<std::uint64_t> gLastMissedFlags{0};
std::atomic<int> gAttachFailures{0};  // Presents with a pending eye diff the recorder couldn't attach for

// Render thread.
std::atomic<int> gRenderIndex{0};  // scene renders so far this frame
thread_local int t_renderIndex = -1;

// The commander's commands, by what they do: 3 visible instances, 5 the occluder flush and queries, 6 the query
// results (where the wait is), the rest (begin, end, debug).
constexpr int kUmbraParts = 4;
char const* const kUmbraPartNames[kUmbraParts] = {"instances", "occluder flush + queries", "query results", "other"};

struct Counters {
    std::atomic<std::uint64_t> frames{0};
    std::array<std::atomic<std::uint64_t>, kSlots + 1> rendersPerFrame{};  // [n] frames with n renders (last: more)
    std::array<std::atomic<std::uint64_t>, kSlots> renderTicks{};
    std::array<std::atomic<std::uint64_t>, kSlots> renders{};
    std::array<std::array<std::atomic<std::uint64_t>, kUmbraParts>, kSlots> umbraTicks{};
    std::array<std::array<std::atomic<std::uint64_t>, kUmbraParts>, kSlots> umbraCalls{};
    std::array<std::atomic<std::uint64_t>, kSlots> umbraMaxReadTicks{};  // the longest single results read
};
Counters gCounters;

int UmbraPart(unsigned code) {
    return code == 3 ? 0 : code == 5 ? 1 : code == 6 ? 2 : 3;
}

std::uint64_t Get(std::atomic<std::uint64_t> const& a) {
    return a.load(std::memory_order_relaxed);
}
void Add(std::atomic<std::uint64_t>& a, std::uint64_t n) {
    a.fetch_add(n, std::memory_order_relaxed);
}
int Slot(int index) {
    return index < 0 ? 0 : (index < kSlots ? index : kSlots - 1);
}

template <typename T>
T At(std::uintptr_t rva) {
    return reinterpret_cast<T>(gExe + rva);
}
template <typename T>
T& Field(std::uint8_t* object, std::size_t offset) {
    return *reinterpret_cast<T*>(object + offset);
}

bool ShowFlag(std::uint8_t* renderer, unsigned bit) {
    unsigned const perWord = *At<std::uint32_t*>(0x164C720);  // the show flags' bits per word (32)
    if (perWord == 0) {
        return true;
    }
    return (Field<std::uint32_t>(renderer, 0x28 + (bit / perWord) * 4) & (1u << (bit % perWord))) != 0;
}

std::uint8_t* View(std::uint8_t* renderer, int index) {
    return Field<std::uint8_t*>(renderer, 0x60) + static_cast<std::size_t>(index) * kViewStride;
}

// A renderer SceneRenderB takes down the Umbra branch on (no hit proxies), with views. Show flag 8 was excluded
// as "wireframe" until the first VR run (2026-10-23) showed it set on every gameplay render: not that bit. A
// wireframe render can't pair anyway (Compatible wants the same show flags).
bool Eligible(std::uint8_t* renderer) {
    int const views = Field<int>(renderer, 0x68);
    return Field<int>(renderer, 0x5C) != 0 && views > 0 && views <= 4 && !ShowFlag(renderer, 0x33);
}

// Why the last pairing was refused (diagnostics, 2026-10-23: the first VR run refused every pair): a bit per test,
// and for the view block at +0x60 the mask of its 4-byte words that differed.
std::atomic<unsigned> gLastMismatch{0}, gLastMismatchWords{0};

// The same scene, target, flags and views (count, view state, rectangles): only the matrices differ.
bool Compatible(std::uint8_t* first, std::uint8_t* second) {
    unsigned why = 0;
    if (first == second) why |= 1;
    if (!Eligible(second)) why |= 2;
    if (Field<void*>(first, 0x00) != Field<void*>(second, 0x00)) why |= 4;
    if (Field<void*>(first, 0x18) != Field<void*>(second, 0x18)) why |= 8;
    if (std::memcmp(first + 0x28, second + 0x28, 12) != 0) why |= 16;
    if (Field<int>(first, 0x68) != Field<int>(second, 0x68)) why |= 32;
    if (why == 0) {
        for (int i = 0; i < Field<int>(first, 0x68); ++i) {
            std::uint8_t* const a = View(first, i);
            std::uint8_t* const b = View(second, i);
            if (Field<void*>(a, 0x08) != Field<void*>(b, 0x08)) why |= 64;
            unsigned words = 0;
            for (unsigned w = 0; w < (kViewCompareEnd - 0x60) / 4; ++w) {
                if (std::memcmp(a + 0x60 + w * 4, b + 0x60 + w * 4, 4) != 0) words |= 1u << w;
            }
            if (words != 0) {
                why |= 128;
                gLastMismatchWords.store(words, std::memory_order_relaxed);
            }
        }
    }
    if (why != 0) {
        gLastMismatch.store(why, std::memory_order_relaxed);
        return false;
    }
    return true;
}

void CopyViewMatrices(std::uint8_t* to, std::uint8_t* from) {
    for (int i = 0; i < Field<int>(to, 0x68); ++i) {
        std::uint8_t* const a = View(to, i);
        std::uint8_t* const b = View(from, i);
        for (ByteRange const& r : kViewPlainRanges) {
            std::memcpy(a + r.begin, b + r.begin, r.end - r.begin);
        }
    }
}

// SceneRenderB's Umbra branch after InitViews, on a renderer that already ran it, with this pass's matrices.
// Raw calls only (SEH). 0 done, 1 an exception (the caller turns the lever off).
int ReplaySEH(std::uint8_t* renderer) noexcept {
    __try {
        void* const rhi = *At<void**>(kDynamicRhiRva);
        void** const rhiTable = *reinterpret_cast<void***>(rhi);
        reinterpret_cast<void (*)(void*)>(rhiTable[0x628 / 8])(rhi);
        ++*At<int*>(kSceneFrameRva);
        Field<int>(renderer, 0xD8) = 1;
        At<void (*)(std::uint8_t)>(kWireframeStateRva)(0);
        void* const target = Field<void*>(renderer, 0x18);
        void** const targetTable = *reinterpret_cast<void***>(target);
        std::uint32_t const sizeY = reinterpret_cast<std::uint32_t (*)(void*)>(targetTable[0x20 / 8])(target);
        std::uint32_t const sizeX = reinterpret_cast<std::uint32_t (*)(void*)>(targetTable[0x18 / 8])(target);
        At<void (*)(void*, std::uint32_t, std::uint32_t, int)>(kAllocateTargetsRva)(
            At<void*>(kSceneRenderTargetsRva), sizeX, sizeY, 0);
        // InitViews' steps after the resolve that draw per view, in its order: the world DPG's lights into
        // the scene targets, then the world's opaque base pass, from these matrices.
        At<void (*)(void*, int)>(kDpgLightTargetsRva)(renderer, 1);
        // The resolve armed one base-pass flag per view (commander +0x34 + 8i; +0x38 is a second list only read on
        // the 0x1795288 path) and the first base pass cleared it: arm it again.
        std::uint8_t* const commander = At<std::uint8_t**>(kCommandersRva)[*At<int*>(kCommanderIndexRva)];
        for (int i = 0; i < Field<int>(renderer, 0x68); ++i) {
            Field<int>(commander, 0x34 + static_cast<std::size_t>(i) * 8) = 1;
        }
        At<void (*)(void*)>(kPrepassViewsRva)(renderer);
        int const translucency = static_cast<int>(At<std::uint64_t (*)(void*)>(kTranslucencyCheckRva)(renderer));
        std::uint32_t dirty = *At<std::uint32_t*>(kDepthDirtyRva);
        int foreground = 0;
        if (*At<int*>(kSkipBeforeLightsRva) == 0) {
            At<void (*)(void*)>(kBeforeLightsRva)(renderer);
        }
        auto const dpgPass = At<void (*)(void*, int, std::uint32_t*)>(kDpgPassRva);
        auto const dpgLights = At<void (*)(void*, int, int, std::uint32_t*, int)>(kDpgLightsRva);
        dpgPass(renderer, 1, &dirty);
        dpgLights(renderer, 1, translucency, &dirty, 1);
        Field<int>(renderer, 0x5C) = 0;
        At<std::uint64_t (*)(void*, int, int*, std::uint32_t*, int)>(kForegroundRva)(renderer, 2, &foreground, &dirty, 0);
        dpgPass(renderer, 2, &dirty);
        dpgLights(renderer, 2, translucency, &dirty, 0);
        Field<int>(renderer, 0x5C) = 1;
        At<void (*)(void*, int, std::uint32_t)>(kFinishRva)(renderer, translucency, dirty);
        reinterpret_cast<void (*)(void*)>(rhiTable[0x620 / 8])(rhi);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}

// The view fields that may differ between the eyes (the plain ranges), for the draw replay's substitution.
draw_replay::ViewFields CaptureViews(std::uint8_t* renderer) {
    draw_replay::ViewFields f;
    for (ByteRange const& r : kViewPlainRanges) {
        f.perView += r.end - r.begin;
    }
    f.views = Field<int>(renderer, 0x68);
    f.bytes.reserve(f.perView * static_cast<std::size_t>(f.views));
    for (int i = 0; i < f.views; ++i) {
        std::uint8_t const* const v = View(renderer, i);
        for (ByteRange const& r : kViewPlainRanges) {
            f.bytes.insert(f.bytes.end(), v + r.begin, v + r.end);
        }
    }
    return f;
}

// sharePass = 2: the first eye's recorded draw calls re-issued for this eye (plugin/DrawReplay), inside the
// engine's own scene bookkeeping as ReplaySEH does it. 0 done, else failed (the caller falls back to ReplaySEH).
int ReplayDrawsSEH(std::uint8_t* renderer, draw_replay::ViewFields const& first,
    draw_replay::ViewFields const& second) noexcept {
    __try {
        void* const rhi = *At<void**>(kDynamicRhiRva);
        void** const rhiTable = *reinterpret_cast<void***>(rhi);
        reinterpret_cast<void (*)(void*)>(rhiTable[0x628 / 8])(rhi);
        ++*At<int*>(kSceneFrameRva);
        Field<int>(renderer, 0xD8) = 1;
        At<void (*)(std::uint8_t)>(kWireframeStateRva)(0);
        int const result = draw_replay::Replay(first, second);
        reinterpret_cast<void (*)(void*)>(rhiTable[0x620 / 8])(rhi);
        return result;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}

void ReleaseKept() {
    if (gKept != nullptr) {
        gDeleteRendererOrig(gKept, 1);
        gKept = nullptr;
        gShareUnused.fetch_add(1, std::memory_order_relaxed);
    }
}

// The second eye on the kept renderer: the draw replay (mode 2: 0 done, 1 an exception, 2 nothing to replay this
// frame, so the game renders the eye itself), or the engine's passes (mode 1: 0 done, 1 an exception). Mode 1
// left the game thread blocked inside Umbra on its first real run (2026-10-23, fake-headset session: the Umbra
// base pass outside the commander's resolve bracket, by the stacks); it stays opt-in until that's read.
int SecondEye(std::uint8_t* first, int mode, draw_replay::ViewFields const& before, draw_replay::ViewFields const& after) {
    if (mode == 2) {
        std::string why;
        if (draw_replay::Attached() && !gReplayFailed.load(std::memory_order_relaxed) && draw_replay::TailReplayable(&why)) {
            int const rc = ReplayDrawsSEH(first, before, after);
            if (rc == 0) {
                gReplayed.fetch_add(1, std::memory_order_relaxed);
                return 0;
            }
            gReplayFailed.store(true, std::memory_order_relaxed);
            log::Warn("render levers: the draw replay failed ({}); the game renders both eyes for the rest of the session", rc);
        }
        gReplayFallbacks.fetch_add(1, std::memory_order_relaxed);
        return 2;
    }
    return ReplaySEH(first);
}

// sharePass: true if this render was handled (rendered while kept, or replayed on the kept one). `mode` 1 or 2;
// `diff` records this pair for the eye diff (the recorder is attached then).
bool ShareOrKeep(std::uint8_t* renderer, int mode, bool diff) {
    bool const recorder = draw_replay::Attached() && (mode == 2 || diff);
    if (gKept != nullptr) {
        auto* const first = static_cast<std::uint8_t*>(gKept);
        gKept = nullptr;
        if (Compatible(first, renderer)) {
            if (diff) {
                // The eye diff: both eyes rendered by the game as usual, each recorded, so the share path's own
                // faults can't colour the comparison; the first eye's stream was recorded around its render.
                draw_replay::ViewFields const a = CaptureViews(first);
                draw_replay::ViewFields const b = CaptureViews(renderer);
                gDeleteRendererOrig(first, 1);
                draw_replay::BeginRecording(1);
                gSceneRenderOrig(renderer);
                draw_replay::EndRecording();
                draw_replay::WriteDiff(a, b, draw_replay::TakeDiffTag());
                return true;
            }
            draw_replay::ViewFields before, after;
            if (recorder) {
                before = CaptureViews(first);
            }
            CopyViewMatrices(first, renderer);
            if (recorder) {
                after = CaptureViews(first);
            }
            int const rc = SecondEye(first, mode, before, after);
            if (rc == 2) {
                gDeleteRendererOrig(first, 1);  // nothing to replay this frame: the game renders this eye itself
                return false;
            }
            if (rc != 0) {
                // Both renderers may be half-way through anything: leak them rather than free them.
                gShareFailed.store(true, std::memory_order_relaxed);
                gShareExceptions.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            gDeleteRendererOrig(renderer, 1);  // as SceneRenderB ends
            gDeleteRendererOrig(first, 1);
            gShared.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        gShareMismatches.fetch_add(1, std::memory_order_relaxed);
        gDeleteRendererOrig(first, 1);
    }
    if (!Eligible(renderer)) {
        gShareIneligible.fetch_add(1, std::memory_order_relaxed);
        gLastIneligible5C.store(Field<int>(renderer, 0x5C), std::memory_order_relaxed);
        gLastIneligibleViews.store(Field<int>(renderer, 0x68), std::memory_order_relaxed);
        gLastIneligibleFlags.store((ShowFlag(renderer, 0x33) ? 1 : 0) | (ShowFlag(renderer, 8) ? 2 : 0), std::memory_order_relaxed);
        return false;
    }
    gKeepCandidate.store(renderer, std::memory_order_relaxed);  // SceneRenderB's delete at its end: DeleteRendererHook
    if (recorder) {
        draw_replay::BeginRecording(0);
    }
    gSceneRenderOrig(renderer);
    if (recorder) {
        draw_replay::EndRecording();
    }
    gKeepCandidate.store(nullptr, std::memory_order_relaxed);
    if (gKept == nullptr) {
        gShareNotKept.fetch_add(1, std::memory_order_relaxed);  // the render ended without its deleter's call
    }
    return true;
}

// 0x77BE00 (renderer, dpg): the world DPG's call is where the draw replay's tail starts (InitViews makes it right
// after the Umbra resolve, before the pre-pass; ReplaySEH makes it first).
void DpgLightTargetsHook(void* renderer, int dpg) {
    if (dpg == 1) {
        draw_replay::MarkTail();
    }
    gDpgLightTargetsOrig(renderer, dpg);
}

void* DeleteRendererHook(void* renderer, std::uint64_t flags) {
    gDeleteCalls.fetch_add(1, std::memory_order_relaxed);
    if (renderer != nullptr && renderer == gKeepCandidate.load(std::memory_order_relaxed)) {
        if ((flags & 1) != 0 && GetCurrentThreadId() == gRenderThread.load(std::memory_order_relaxed)) {
            gKept = renderer;
            return renderer;
        }
        gDeleteCandidateMissed.fetch_add(1, std::memory_order_relaxed);
        gLastMissedFlags.store(flags, std::memory_order_relaxed);
    }
    return gDeleteRendererOrig(renderer, flags);
}

// The scene view state the renderer's first view renders through (gameplay: the one state both eyes share).
void* ViewStateOf(void* renderer) {
    auto* const r = static_cast<std::uint8_t*>(renderer);
    auto* const views = Field<std::uint8_t*>(r, kRendererViews);
    if (views == nullptr || Field<int>(r, kRendererViews + 8) <= 0) {
        return nullptr;
    }
    return Field<void*>(views, kViewState);
}

// Before a frame's scene render `index`: true when the second eye's own occlusion history was swapped into the state
// (EndEyeHistory swaps it back out).
bool BeginEyeHistory(void* renderer, int index) {
    void* const state = ViewStateOf(renderer);
    if (index == 0) {
        gFrameFirstState = state;
        gFrameFirstStateFresh = state != nullptr && Field<int>(static_cast<std::uint8_t*>(state), kStateOcclusionHistory + 8) == 0;
        return false;
    }
    if (index != 1 || gPerEyeOcclusion.load(std::memory_order_relaxed) == 0 || state == nullptr) {
        return false;
    }
    if (state != gFrameFirstState) {
        gHistorySkipped.fetch_add(1, std::memory_order_relaxed);  // its own state already (Halcyon's cine states)
        return false;
    }
    if (state != gEyeOneHistoryState || gFrameFirstStateFresh) {
        std::memset(gEyeOneHistory, 0, sizeof(gEyeOneHistory));  // an empty TSet; the old one's memory is left behind
        gEyeOneHistoryState = state;
        gHistoryResets.fetch_add(1, std::memory_order_relaxed);
    }
    auto* const inState = static_cast<std::uint8_t*>(state) + kStateOcclusionHistory;
    std::uint8_t firstEye[kOcclusionHistoryBytes];
    std::memcpy(firstEye, inState, kOcclusionHistoryBytes);
    std::memcpy(inState, gEyeOneHistory, kOcclusionHistoryBytes);
    std::memcpy(gEyeOneHistory, firstEye, kOcclusionHistoryBytes);  // holds the first eye's set until the render ends
    gHistorySwaps.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void EndEyeHistory(bool swapped) {
    if (!swapped) {
        return;
    }
    auto* const inState = static_cast<std::uint8_t*>(gEyeOneHistoryState) + kStateOcclusionHistory;
    std::uint8_t secondEye[kOcclusionHistoryBytes];
    std::memcpy(secondEye, inState, kOcclusionHistoryBytes);
    std::memcpy(inState, gEyeOneHistory, kOcclusionHistoryBytes);
    std::memcpy(gEyeOneHistory, secondEye, kOcclusionHistoryBytes);
}

void SceneRenderHook(void* renderer) {
    int const index = gRenderIndex.fetch_add(1, std::memory_order_relaxed);
    int const outer = t_renderIndex;
    t_renderIndex = index;
    gRenderThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    bool const eyeHistory = BeginEyeHistory(renderer, index);
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    bool handled = false;
    int mode = gSharePass.load(std::memory_order_relaxed);
    bool const diff = draw_replay::DiffPending() && draw_replay::Attached();
    if (mode == 0 && diff) {
        mode = 1;  // the eye diff needs a paired frame: this one runs as sharePass = 1
    }
    if (gShareHooked && mode != 0 && !gShareFailed.load(std::memory_order_relaxed)) {
        handled = ShareOrKeep(static_cast<std::uint8_t*>(renderer), mode, diff);
    } else {
        ReleaseKept();
    }
    if (!handled) {
        gSceneRenderOrig(renderer);
    }
    QueryPerformanceCounter(&end);
    EndEyeHistory(eyeHistory);
    t_renderIndex = outer;
    int const slot = Slot(index);
    Add(gCounters.renderTicks[slot], static_cast<std::uint64_t>(end.QuadPart - begin.QuadPart));
    Add(gCounters.renders[slot], 1);
}

// The commander's command(): each call timed into its part for this render's place in the frame; with
// umbraQueryWait = 0 the resolve's start stamp is moved 4 s back as soon as it's written, so the results read
// (code 6) never spins for an unready query (it then counts as visible: a few more draws, no stall).
void CommandHook(void* commander, unsigned code) {
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    gCommandOrig(commander, code);
    if (code == 0 && gUmbraQueryWait.load(std::memory_order_relaxed) == 0) {
        *At<float*>(kUmbraResolveStartRva) -= 4.0f;
    }
    QueryPerformanceCounter(&end);
    int const slot = Slot(t_renderIndex);
    int const part = UmbraPart(code);
    auto const ticks = static_cast<std::uint64_t>(end.QuadPart - begin.QuadPart);
    Add(gCounters.umbraTicks[slot][part], ticks);
    Add(gCounters.umbraCalls[slot][part], 1);
    if (part == 2) {
        std::uint64_t seen = gCounters.umbraMaxReadTicks[slot].load(std::memory_order_relaxed);
        while (ticks > seen && !gCounters.umbraMaxReadTicks[slot].compare_exchange_weak(seen, ticks, std::memory_order_relaxed)) {
        }
    }
}

// FD3D11DynamicRHI::GetQueryData: in a frame's second scene render, with ue3QueryWait = 0, the read never waits
// (the engine then treats the primitive as visible, as it does for any unready query without the wait).
// Per render slot, this log period: reads, reads that asked to wait, reads that came back without a result.
std::atomic<std::uint64_t> gQueryReads[kSlots], gQueryReadsWait[kSlots], gQueryReadsFailed[kSlots];

int GetQueryDataHook(void* rhi, void* query, void* out, unsigned size, int wait) {
    int const slot = Slot(t_renderIndex);
    gQueryReads[slot].fetch_add(1, std::memory_order_relaxed);
    if (wait != 0) {
        gQueryReadsWait[slot].fetch_add(1, std::memory_order_relaxed);
        if (t_renderIndex >= 1 && gUe3QueryWait.load(std::memory_order_relaxed) == 0) {
            gQueryWaitsSkipped.fetch_add(1, std::memory_order_relaxed);
            wait = 0;
        }
    }
    int const rc = gGetQueryDataOrig(rhi, query, out, size, wait);
    if (rc == 0) {
        gQueryReadsFailed[slot].fetch_add(1, std::memory_order_relaxed);
    }
    return rc;
}

// SceneRenderB's choice of Umbra commander for this scene render: with perEyeViewState, a frame's second render
// takes commander 1 (its own query pool, matching its own view state's camera).
void SetCommanderHook(std::uint8_t which) {
    if (t_renderIndex >= 1 && gPerEyeViewState.load(std::memory_order_relaxed) != 0) {
        which = 1;
    }
    gSetCommanderOrig(which);
}

std::wstring const& IniPath() {
    static std::wstring path = [] {
        HMODULE module = nullptr;
        wchar_t buffer[MAX_PATH]{};
        std::wstring result;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&IniPath), &module) &&
            GetModuleFileNameW(module, buffer, MAX_PATH) != 0) {
            result = buffer;  // ...\ASI\LE1MEVR.asi -> LE1MEVR.ini
            std::size_t const dot = result.find_last_of(L'.');
            if (dot != std::wstring::npos) {
                result.erase(dot);
            }
            result += L".ini";
        }
        return result;
    }();
    return path;
}

// The logical processors of the most performant efficiency class (group 0); 0 if the CPU isn't hybrid.
DWORD_PTR PerformanceCoreMask() {
    ULONG length = 0;
    GetSystemCpuSetInformation(nullptr, 0, &length, GetCurrentProcess(), 0);
    if (length == 0) {
        return 0;
    }
    std::vector<std::uint8_t> buffer(length);
    auto* const first = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer.data());
    if (!GetSystemCpuSetInformation(first, length, &length, GetCurrentProcess(), 0)) {
        return 0;
    }
    BYTE best = 0, worst = 0xFF;
    for (ULONG offset = 0; offset < length;) {
        auto const* const info = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION const*>(buffer.data() + offset);
        if (info->Type == CpuSetInformation && info->CpuSet.Group == 0) {
            best = std::max(best, info->CpuSet.EfficiencyClass);
            worst = std::min(worst, info->CpuSet.EfficiencyClass);
        }
        offset += info->Size;
    }
    if (best == worst) {
        return 0;
    }
    DWORD_PTR mask = 0;
    for (ULONG offset = 0; offset < length;) {
        auto const* const info = reinterpret_cast<SYSTEM_CPU_SET_INFORMATION const*>(buffer.data() + offset);
        if (info->Type == CpuSetInformation && info->CpuSet.Group == 0 && info->CpuSet.EfficiencyClass == best &&
            info->CpuSet.LogicalProcessorIndex < sizeof(DWORD_PTR) * 8) {
            mask |= DWORD_PTR{1} << info->CpuSet.LogicalProcessorIndex;
        }
        offset += info->Size;
    }
    return mask;
}

DWORD_PTR gOriginalAffinity = 0;

void ApplyAffinity(bool pCoresOnly) {
    DWORD_PTR processMask = 0, systemMask = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask);
    if (gOriginalAffinity == 0) {
        gOriginalAffinity = processMask;
    }
    if (!pCoresOnly) {
        if (processMask != gOriginalAffinity && SetProcessAffinityMask(GetCurrentProcess(), gOriginalAffinity)) {
            log::Info("render levers: CPU affinity back to 0x{:X}", static_cast<std::uint64_t>(gOriginalAffinity));
        }
        return;
    }
    DWORD_PTR const performance = PerformanceCoreMask() & gOriginalAffinity;
    if (performance == 0) {
        log::Info("render levers: pCoresOnly: the CPU has one core class; affinity left at 0x{:X}",
            static_cast<std::uint64_t>(processMask));
        return;
    }
    if (SetProcessAffinityMask(GetCurrentProcess(), performance)) {
        log::Info("render levers: CPU affinity 0x{:X} (performance cores only; was 0x{:X})",
            static_cast<std::uint64_t>(performance), static_cast<std::uint64_t>(gOriginalAffinity));
    } else {
        log::Warn("render levers: SetProcessAffinityMask failed ({})", GetLastError());
    }
}

// The last logged totals, for the 10 s deltas.
struct Snapshot {
    std::uint64_t frames = 0;
    std::array<std::uint64_t, kSlots + 1> rendersPerFrame{};
    std::array<std::uint64_t, kSlots> renderTicks{}, renders{}, umbraMaxReadTicks{};
    std::array<std::array<std::uint64_t, kUmbraParts>, kSlots> umbraTicks{}, umbraCalls{};
    std::uint64_t shared = 0, mismatches = 0, unused = 0, exceptions = 0;
    std::uint64_t ineligible = 0, notKept = 0, deleteCalls = 0, candidateMissed = 0;
};

Snapshot Take() {
    Snapshot s;
    s.frames = Get(gCounters.frames);
    for (int i = 0; i <= kSlots; ++i) s.rendersPerFrame[i] = Get(gCounters.rendersPerFrame[i]);
    for (int i = 0; i < kSlots; ++i) {
        s.renderTicks[i] = Get(gCounters.renderTicks[i]);
        s.renders[i] = Get(gCounters.renders[i]);
        s.umbraMaxReadTicks[i] = gCounters.umbraMaxReadTicks[i].exchange(0, std::memory_order_relaxed);
        for (int p = 0; p < kUmbraParts; ++p) {
            s.umbraTicks[i][p] = Get(gCounters.umbraTicks[i][p]);
            s.umbraCalls[i][p] = Get(gCounters.umbraCalls[i][p]);
        }
    }
    s.shared = Get(gShared);
    s.mismatches = Get(gShareMismatches);
    s.unused = Get(gShareUnused);
    s.exceptions = Get(gShareExceptions);
    s.ineligible = Get(gShareIneligible);
    s.notKept = Get(gShareNotKept);
    s.deleteCalls = Get(gDeleteCalls);
    s.candidateMissed = Get(gDeleteCandidateMissed);
    return s;
}

void LogStats(Snapshot const& now, Snapshot const& before) {
    std::uint64_t const frames = now.frames - before.frames;
    if (frames == 0) {
        return;
    }
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    std::string perFrame, perRender;
    auto const ms = [&](std::uint64_t ticks) { return 1000.0 * static_cast<double>(ticks) / static_cast<double>(frequency.QuadPart); };
    for (int i = 0; i <= kSlots; ++i) {
        std::uint64_t const n = now.rendersPerFrame[i] - before.rendersPerFrame[i];
        if (n != 0) {
            perFrame += std::format("{}{}{} (x{})", perFrame.empty() ? "" : ", ", i, i == kSlots ? "+" : "", n);
        }
    }
    for (int i = 0; i < kSlots; ++i) {
        std::uint64_t const n = now.renders[i] - before.renders[i];
        if (n == 0) {
            continue;
        }
        double const renderMs = ms(now.renderTicks[i] - before.renderTicks[i]) / static_cast<double>(n);
        std::string umbra;
        std::uint64_t umbraTotal = 0;
        for (int p = 0; p < kUmbraParts; ++p) {
            std::uint64_t const ticks = now.umbraTicks[i][p] - before.umbraTicks[i][p];
            std::uint64_t const calls = now.umbraCalls[i][p] - before.umbraCalls[i][p];
            umbraTotal += ticks;
            if (calls != 0) {
                umbra += std::format("{}{} {:.2f} ms ({:.0f} calls)", umbra.empty() ? "" : ", ", kUmbraPartNames[p],
                    ms(ticks) / static_cast<double>(n), static_cast<double>(calls) / static_cast<double>(n));
            }
        }
        perRender += std::format("{}#{}{} {:.2f} ms", perRender.empty() ? "" : "; ", i, i == kSlots - 1 ? "+" : "", renderMs);
        if (!umbra.empty()) {
            perRender += std::format(" [Umbra resolve {:.2f} ms: {}; longest results read {:.2f} ms]",
                ms(umbraTotal) / static_cast<double>(n), umbra, ms(now.umbraMaxReadTicks[i]));
        }
    }
    std::string share;
    if (gSharePass.load(std::memory_order_relaxed) != 0 || now.shared != before.shared || now.exceptions != 0) {
        share = std::format("; sharePass: {} renders on the previous one's renderer, {} didn't match, {} kept "
            "renderers unused{}", now.shared - before.shared, now.mismatches - before.mismatches,
            now.unused - before.unused, now.exceptions != 0 ? "; OFF after an exception" : "");
        if (now.mismatches != before.mismatches) {
            share += std::format(" (last mismatch: tests 0x{:X} = {}{}{}{}{}{}{}{}; view words 0x{:X})",
                gLastMismatch.load(std::memory_order_relaxed),
                (gLastMismatch.load() & 1) ? "same renderer " : "", (gLastMismatch.load() & 2) ? "ineligible " : "",
                (gLastMismatch.load() & 4) ? "scene " : "", (gLastMismatch.load() & 8) ? "target " : "",
                (gLastMismatch.load() & 16) ? "show flags " : "", (gLastMismatch.load() & 32) ? "view count " : "",
                (gLastMismatch.load() & 64) ? "view state " : "", (gLastMismatch.load() & 128) ? "view block +0x60" : "",
                gLastMismatchWords.load(std::memory_order_relaxed));
        }
        std::uint64_t const ineligible = now.ineligible - before.ineligible;
        std::uint64_t const notKept = now.notKept - before.notKept;
        if (ineligible != 0 || notKept != 0) {
            share += std::format("; not kept: {} ineligible (last: +0x5C {}, {} views, show flags {}), {} finished without "
                "the deleter's call ({} deleter calls, {} on the candidate with other flags or thread, last flags 0x{:X})",
                ineligible, gLastIneligible5C.load(std::memory_order_relaxed), gLastIneligibleViews.load(std::memory_order_relaxed),
                gLastIneligibleFlags.load(std::memory_order_relaxed), notKept, now.deleteCalls - before.deleteCalls,
                now.candidateMissed - before.candidateMissed, gLastMissedFlags.load(std::memory_order_relaxed));
        }
    }
    std::uint64_t const skipped = gQueryWaitsSkipped.exchange(0, std::memory_order_relaxed);
    if (skipped != 0) {
        share += std::format("; ue3QueryWait: {:.0f} query reads a frame didn't wait", static_cast<double>(skipped) / static_cast<double>(frames));
    }
    std::string reads;
    for (int i = 0; i < kSlots; ++i) {
        std::uint64_t const n = gQueryReads[i].exchange(0, std::memory_order_relaxed);
        std::uint64_t const w = gQueryReadsWait[i].exchange(0, std::memory_order_relaxed);
        std::uint64_t const f = gQueryReadsFailed[i].exchange(0, std::memory_order_relaxed);
        if (n != 0) {
            reads += std::format("{}#{} {:.0f} ({:.0f} waiting, {:.0f} without a result)", reads.empty() ? "" : ", ", i,
                static_cast<double>(n) / static_cast<double>(frames), static_cast<double>(w) / static_cast<double>(frames),
                static_cast<double>(f) / static_cast<double>(frames));
        }
    }
    if (!reads.empty()) {
        share += "; query reads a frame: " + reads;
    }
    {
        std::uint64_t const swaps = gHistorySwaps.exchange(0, std::memory_order_relaxed);
        std::uint64_t const resets = gHistoryResets.exchange(0, std::memory_order_relaxed);
        std::uint64_t const skippedStates = gHistorySkipped.exchange(0, std::memory_order_relaxed);
        if (swaps != 0 || resets != 0 || skippedStates != 0) {
            share += std::format("; second eye's own occlusion history: {} renders, {} new sets, {} on another state",
                swaps, resets, skippedStates);
        }
        std::uint64_t const fixed = gUmbraMatrixFixed.exchange(0, std::memory_order_relaxed);
        std::uint64_t const same = gUmbraMatrixSame.exchange(0, std::memory_order_relaxed);
        if (fixed != 0 || same != 0) {
            share += std::format("; Umbra's queries with the eye's matrix: {} renders fixed, {} already right", fixed, same);
        }
    }
    draw_replay::Stats const d = draw_replay::TakeStats();
    std::uint64_t const fallbacks = gReplayFallbacks.exchange(0, std::memory_order_relaxed);
    if (gSharePass.load(std::memory_order_relaxed) == 2 || d.replays != 0 || d.failures != 0 || fallbacks != 0) {
        auto const per = [](std::uint64_t total, std::uint64_t n) { return n == 0 ? 0.0 : static_cast<double>(total) / static_cast<double>(n); };
        share += std::format("; draw replay: {} replays ({:.0f} calls, {:.0f} substitutions each), {} first eyes recorded "
            "({:.0f} calls, {:.0f} KB each), {} fell back to the engine's passes{}{}",
            d.replays, per(d.replayedCalls, d.replays), per(d.substitutions, d.replays), d.recorded,
            per(d.recordedCalls, d.recorded), per(d.recordedBytes, d.recorded) / 1024.0, fallbacks,
            d.notReplayable != 0 && !d.lastBlocker.empty() ? std::format(" ({} not replayable: {})", d.notReplayable, d.lastBlocker) : "",
            gReplayFailed.load(std::memory_order_relaxed) ? "; the draw replay is OFF after a failure" : "");
    }
    log::Info("render levers: last 10 s: {} frames; scene renders per frame {}; per render, by its place in the "
        "frame: {}{}", frames, perFrame.empty() ? "-" : perFrame, perRender.empty() ? "-" : perRender, share);
}

} // namespace

bool Install(hook::Context& init) {
    auto* const sceneRender = init.ResolveTyped<tSceneRender>(kSceneRenderRva);
    auto* const command = init.ResolveTyped<tCommand>(kCommanderCommandRva);
    if (sceneRender == nullptr || command == nullptr) {
        log::Warn("render levers: addresses not resolved; levers off");
        return false;
    }
    gExe = init.ResolveTyped<std::uint8_t>(0);
    auto* const deleteRenderer = init.ResolveTyped<tDeleteRenderer>(kDeleteRendererRva);
    // The deleter first: its detour passes everything through until sharePass keeps a renderer.
    gShareHooked = gExe != nullptr && deleteRenderer != nullptr &&
        hook::Install(init, "FSceneRenderer::DeletingDestructor", deleteRenderer, &DeleteRendererHook, gDeleteRendererOrig);
    if (!gShareHooked) {
        log::Warn("render levers: renderer deleter hook failed; sharePass has no effect");
    }
    if (!hook::Install(init, "FSceneRenderer::Render", sceneRender, &SceneRenderHook, gSceneRenderOrig)) {
        log::Warn("render levers: scene render hook failed; levers off");
        gShareHooked = false;
        return false;
    }
    gCommanderHooked = hook::Install(init, "UmbraCommander::command", command, &CommandHook, gCommandOrig);
    if (!gCommanderHooked) {
        log::Warn("render levers: commander hook failed; the Umbra resolve isn't timed and umbraQueryWait has no effect");
    }
    auto* const getQueryData = init.ResolveTyped<tGetQueryData>(kGetQueryDataRva);
    gQueryDataHooked = getQueryData != nullptr &&
        hook::Install(init, "FD3D11DynamicRHI::GetQueryData", getQueryData, &GetQueryDataHook, gGetQueryDataOrig);
    if (!gQueryDataHooked) {
        log::Warn("render levers: GetQueryData hook failed; ue3QueryWait has no effect");
    }
    auto* const umbraIssue = init.ResolveTyped<tUmbraIssueQueries>(kUmbraIssueQueriesRva);
    gUmbraIssueQueriesHooked = umbraIssue != nullptr &&
        hook::Install(init, "UmbraCommander::IssueOcclusionQueries", umbraIssue, &UmbraIssueQueriesHook, gUmbraIssueQueriesOrig);
    if (!gUmbraIssueQueriesHooked) {
        log::Warn("render levers: UmbraCommander::IssueOcclusionQueries hook failed; umbraEyeMatrix has no effect");
    }
    // Not installed: a second render on commander 1 blocks in Umbra's command ring (research/35, attempt 20).
    (void)&SetCommanderHook;
    auto* const lightTargets = init.ResolveTyped<tDpgLightTargets>(kDpgLightTargetsRva);
    gReplayHooked = gShareHooked && lightTargets != nullptr &&
        hook::Install(init, "FSceneRenderer::RenderDpgLightTargets", lightTargets, &DpgLightTargetsHook, gDpgLightTargetsOrig);
    if (!gReplayHooked) {
        log::Warn("render levers: light-targets hook failed; sharePass = 2 runs as 1 and the eye diff is off");
    }
    gHooked = true;
    log::Info("render levers: hooked the scene render, its deleter{}{}", gCommanderHooked ? ", the Umbra commander" : "",
        gReplayHooked ? " and the light targets (the draw replay's mark)" : "");
    return true;
}

void Tick() {
    static std::uint64_t nextReadMs = 0;
    static std::uint64_t nextLogMs = 0;
    static int umbraQueryWait = -1;
    static int pCoresOnly = -1;
    static int sharePass = -1;
    static Snapshot last;
    std::uint64_t const now = GetTickCount64();
    if (now >= nextReadMs) {
        nextReadMs = now + 1000;
        std::wstring const& path = IniPath();
        // An empty value (`sharePass=`) is the default, not 0: GetPrivateProfileInt would read it as 0.
        auto const readInt = [&](wchar_t const* key, int fallback) {
            if (path.empty()) {
                return fallback;
            }
            wchar_t buffer[32]{};
            DWORD const n = GetPrivateProfileStringW(L"Performance", key, L"", buffer, 32, path.c_str());
            if (n == 0) {
                return fallback;
            }
            wchar_t* end = nullptr;
            long const value = std::wcstol(buffer, &end, 10);
            return end == buffer ? fallback : static_cast<int>(value);
        };
        static int perEye = -1;
        int const perEyeRead = readInt(L"perEyeViewState", 0) != 0 ? 1 : 0;
        if (perEyeRead != perEye) {
            perEye = perEyeRead;
            gPerEyeViewState.store(perEyeRead, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] perEyeViewState = {})",
                perEyeRead != 0 ? "each Stereo 2 eye renders through its own scene view state in gameplay (its own "
                                  "occlusion history: no spin for the other eye's queries)"
                                : "both eyes render through the game's one scene view state (Halcyon's own; per-eye "
                                  "states in cine only)",
                perEyeRead);
        }
        static int umbraEyeMatrix = -1;
        int const umbraEyeMatrixRead = readInt(L"umbraEyeMatrix", 1) != 0 ? 1 : 0;
        if (umbraEyeMatrixRead != umbraEyeMatrix) {
            umbraEyeMatrix = umbraEyeMatrixRead;
            gUmbraEyeMatrix.store(umbraEyeMatrixRead, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] umbraEyeMatrix = {}){}",
                umbraEyeMatrixRead != 0 ? "Umbra's occlusion queries are drawn with the eye's own matrix (the one the "
                                          "depth buffer was drawn with) in the life-size stereo's mode 2"
                                        : "Umbra's occlusion queries are drawn with the view's CPU matrix (the game's "
                                          "own: the symmetric window in mode 2, off the eye's depth buffer)",
                umbraEyeMatrixRead, gUmbraIssueQueriesHooked ? "" : "; not hooked, so no effect");
        }
        static int perEyeOcclusion = -1;
        int const perEyeOcclusionRead = readInt(L"perEyeOcclusion", 0) != 0 ? 1 : 0;
        if (perEyeOcclusionRead != perEyeOcclusion) {
            perEyeOcclusion = perEyeOcclusionRead;
            gPerEyeOcclusion.store(perEyeOcclusionRead, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] perEyeOcclusion = {})",
                perEyeOcclusionRead != 0 ? "a frame's second scene render keeps its own per-primitive occlusion history "
                                           "(swapped into the shared view state around it: it reads its own previous "
                                           "frame's queries instead of spinning for the first eye's)"
                                         : "both scene renders share the view state's one occlusion history (the game's "
                                           "own: the second eye waits for the first eye's queries)",
                perEyeOcclusionRead);
        }
        static int ue3QueryWait = -1;
        int const ue3Wait = readInt(L"ue3QueryWait", 1) != 0 ? 1 : 0;
        if (ue3Wait != ue3QueryWait) {
            ue3QueryWait = ue3Wait;
            gUe3QueryWait.store(ue3Wait, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] ue3QueryWait = {}){}",
                ue3Wait != 0 ? "the engine's occlusion reads wait for the GPU as the game does (a frame's second render "
                               "spins for the first eye's queries)"
                             : "a frame's second scene render never waits for an occlusion query result (an unready one "
                               "counts as visible)",
                ue3Wait, gQueryDataHooked ? "" : "; not hooked, so no effect");
        }
        int const queryWait = readInt(L"umbraQueryWait", 1) != 0 ? 1 : 0;
        int const pCores = readInt(L"pCoresOnly", 0) != 0 ? 1 : 0;
        if (queryWait != umbraQueryWait) {
            umbraQueryWait = queryWait;
            gUmbraQueryWait.store(queryWait, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] umbraQueryWait = {}){}",
                queryWait != 0 ? "the Umbra resolve waits for occlusion query results as the game does (up to 10 ms after its "
                                 "start by a float stamp of 2 s precision: anything up to a second)"
                               : "the Umbra resolve never waits for a query result (an unready one counts as visible)",
                queryWait, gCommanderHooked ? "" : "; not hooked, so no effect");
        }
        if (pCores != pCoresOnly) {
            pCoresOnly = pCores;
            gPCoresOnly.store(pCores, std::memory_order_relaxed);
            ApplyAffinity(pCores != 0);
        }
        int const shareRaw = readInt(L"sharePass", 0);
        int const share = shareRaw < 0 ? 0 : (shareRaw > 2 ? 2 : shareRaw);
        if (share != sharePass) {
            sharePass = share;
            gSharePass.store(share, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] sharePass = {}){}",
                share == 2 ? "a frame's second scene render replays the first one's draw calls on its renderer (no second "
                             "visibility, no second engine pass; research/32 lever 4)"
                : share == 1 ? "a frame's second scene render runs on the first one's renderer (no second visibility)"
                             : "every scene render does its own visibility (the game's own)",
                share, !gShareHooked ? "; not hooked, so no effect" : (share == 2 && !gReplayHooked) ? "; its mark isn't hooked, so it runs as 1" : "");
        }
    }
    if (gHooked && now >= nextLogMs) {
        Snapshot const current = Take();
        if (nextLogMs != 0) {
            LogStats(current, last);
        }
        last = current;
        nextLogMs = now + 10000;
    }
}

void OnPresentEnd(ID3D11DeviceContext* context) {
    int const renders = gRenderIndex.exchange(0, std::memory_order_relaxed);
    Add(gCounters.frames, 1);
    Add(gCounters.rendersPerFrame[renders < kSlots ? renders : kSlots], 1);
    ReleaseKept();  // a kept renderer never pairs across frames
    if (gReplayHooked && context != nullptr && !draw_replay::Attached() &&
        (gSharePass.load(std::memory_order_relaxed) == 2 || draw_replay::DiffPending())) {
        if (draw_replay::Attach(context)) {  // once wanted; the hooks then stay
            gAttachFailures.store(0, std::memory_order_relaxed);
        } else if (draw_replay::DiffPending() && gAttachFailures.fetch_add(1, std::memory_order_relaxed) + 1 >= 300) {
            // About 3 s of Presents without the recorder: the request can't be served; drop it rather than keep it
            // armed for the rest of the session (the reason is in the 'draw replay:' line above).
            std::string const tag = draw_replay::TakeDiffTag();
            gAttachFailures.store(0, std::memory_order_relaxed);
            log::Warn("eye diff '{}': dropped, the draw recorder couldn't attach to the context", tag);
        }
    }
    draw_replay::OnPresentEnd();
}

bool PerEyeViewState() {
    return gPerEyeViewState.load(std::memory_order_relaxed) != 0;
}

std::string SettingsJson() {
    return std::format("{{\"umbraQueryWait\": {}, \"ue3QueryWait\": {}, \"perEyeViewState\": {}, \"perEyeOcclusion\": {}, \"umbraEyeMatrix\": {}, \"pCoresOnly\": {}, \"sharePass\": {}, \"shareFailed\": {}, \"replayFailed\": {}, "
        "\"drawReplay\": {}, \"hooked\": {}}}",
        gUmbraQueryWait.load(std::memory_order_relaxed), gUe3QueryWait.load(std::memory_order_relaxed),
        gPerEyeViewState.load(std::memory_order_relaxed), gPerEyeOcclusion.load(std::memory_order_relaxed),
        gUmbraEyeMatrix.load(std::memory_order_relaxed), gPCoresOnly.load(std::memory_order_relaxed),
        gSharePass.load(std::memory_order_relaxed), gShareFailed.load(std::memory_order_relaxed) ? "true" : "false",
        gReplayFailed.load(std::memory_order_relaxed) ? "true" : "false", draw_replay::StatsJson(), gHooked ? "true" : "false");
}

} // namespace mevr::host::render_levers
