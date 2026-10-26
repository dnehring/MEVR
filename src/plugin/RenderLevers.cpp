// SPDX-License-Identifier: MIT
#include "plugin/RenderLevers.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "plugin/DrawReplay.hpp"
#include "plugin/MevrMenu.hpp"
#include "halcyon/le1/mevr_bridge.h"

#include <Windows.h>
#include <d3d11.h>
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
// The same without the +0x8C word, every range starting on a 16-byte boundary of the view: the draw replay's
// substitution keys are the differing 16-byte chunks of these bytes, and a 4-byte offset at the start put every key
// astride two matrix rows (research/37, experiment 3: 0 substitutions).
constexpr ByteRange kViewCaptureRanges[] = {{0x90, 0x140}, {0x190, 0x330}, {0x480, 0x4B0}, {0x4C0, 0x4D0}};
constexpr std::size_t kViewCompareEnd = 0x8C;  // the view block compared for compatibility: +0x60 to here

// A view offset's place in the captured bytes (SIZE_MAX: outside the capture ranges).
constexpr std::size_t CapturedOffset(std::size_t viewOffset) {
    std::size_t at = 0;
    for (ByteRange const& r : kViewCaptureRanges) {
        if (viewOffset >= r.begin && viewOffset < r.end) {
            return at + (viewOffset - r.begin);
        }
        at += r.end - r.begin;
    }
    return SIZE_MAX;
}

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

// ---- A GPU flush after the first eye (research/35, attempt 38) --------------------------------------------------
// The second eye's Umbra resolve issues occluder queries and waits for their results inside the same resolve, so it
// waits for the GPU to get through the first eye's draws first (the D3D11 runtime batches them until something
// forces a kick). ID3D11DeviceContext::Flush right after the first eye's scene render starts the GPU on it at once,
// so by the time the second eye's resolve asks, less is queued ahead of its queries. [Performance] flushFirstEye.
std::atomic<int> gFlushFirstEye{0};
std::atomic<std::uint64_t> gFlushes{0};

// ---- The second eye's Umbra query results from the first eye (research/35, attempt 39) ---------------------------
// Umbra's resolve issues its occluder queries and, in the same resolve, reads their results one by one (0xD48380,
// a wait with a time budget) before it goes on: the first eye finds the GPU idle and waits ~0.1 ms, the second
// finds it busy with the first eye's draws and waits 1.3-2.8 ms (7 at worst). Both eyes test the same occluders in
// the same order from cameras 6 cm apart, so when the second eye's i-th read isn't ready, the first eye's i-th
// result of the same frame stands in (counted per render; a read past the first eye's count falls back to the
// wait). An unready first-eye read still waits. [Performance] umbraShareResults.
// ---- Bound shader states kept alive (research/35, attempt 42) ---------------------------------------------------
// The stack sampler put 20% of the render thread at the heavy spot (4,100 draws) inside the D3D11 bound-shader-state
// constructor (0x9A89F0: ID3D11Device::CreateInputLayout with the vertex shader's bytecode), always from the cache-miss
// path of RHICreateBoundShaderState (0x9AB3B0: the lookup by (vertex declaration, VS, PS, GS) in the cache at
// 0x1793518 missed). A bound shader state is a ref-counted object whose cache link dies with it, and the dynamic-mesh
// drawing policies (foliage, SpeedTrees, skeletal meshes) make theirs per draw, so the input layout is built again
// every draw of every eye. One extra reference per created object (the RHI's own AddRef, vtable slot 0x8a0 of
// GDynamicRHI, on the handle the constructor returns: the object + 0xC, as both the miss and the hit path return it)
// keeps it, and its cache link, alive: the lookups hit from then on. Bounded by the distinct combinations (a cap, logged).
// [Performance] keepBoundShaderStates.
constexpr std::uintptr_t kBoundShaderStateCtorRva = 0x9A89F0;
constexpr std::size_t kRhiAddRefBoundShaderStateSlot = 0x8A0;
constexpr std::size_t kBoundShaderStateHandle = 0xC;
constexpr std::uint64_t kKeepBoundShaderStatesCap = 200000;
using tBoundShaderStateCtor = void*(void* self, void* declaration, void* strides, void* vs, void* ps, void* a6, void* a7,
                                    void* a8, void* rhi);
tBoundShaderStateCtor* gBoundShaderStateCtorOrig = nullptr;
bool gBoundShaderStateHooked = false;
std::atomic<int> gKeepBoundShaderStates{0};
std::atomic<std::uint64_t> gBssCreated{0};
std::atomic<std::uint64_t> gBssKept{0};
std::atomic<std::uint64_t> gBssKeptTotal{0};
std::atomic<std::uint64_t> gBssFirstTime{0};   // creations whose (declaration, VS, PS) was never constructed before
std::atomic<std::uint64_t> gBssRepeat{0};      // creations of a key constructed before: the object had died
std::uint8_t* gBssExe = nullptr;
// The keys seen, as 64-bit hashes in a fixed open-addressing table (render thread only): a key constructed for the
// second time is a stable one whose object died, worth keeping; a key never seen again (a per-frame declaration or
// shader) is never kept, so it cannot leak. Collisions overwrite: a heuristic, not a record.
constexpr std::size_t kBssKeyTableSize = 1 << 16;
std::uint64_t gBssKeys[kBssKeyTableSize]{};

bool BssKeySeenBefore(void* declaration, void* vs, void* ps) {
    std::uint64_t h = reinterpret_cast<std::uintptr_t>(declaration) * 0x9E3779B97F4A7C15ull;
    h ^= reinterpret_cast<std::uintptr_t>(vs) * 0xC2B2AE3D27D4EB4Full;
    h ^= reinterpret_cast<std::uintptr_t>(ps) * 0x165667B19E3779F9ull;
    h ^= h >> 29;
    h |= 1;   // never the empty marker
    std::size_t slot = static_cast<std::size_t>(h) & (kBssKeyTableSize - 1);
    for (int probe = 0; probe < 8; ++probe) {
        std::uint64_t& entry = gBssKeys[(slot + probe) & (kBssKeyTableSize - 1)];
        if (entry == h) {
            return true;
        }
        if (entry == 0) {
            entry = h;
            return false;
        }
    }
    gBssKeys[slot] = h;   // a full run: overwrite
    return false;
}

void* BoundShaderStateCtorHook(void* self, void* declaration, void* strides, void* vs, void* ps, void* a6, void* a7, void* a8,
                               void* rhi) {
    void* const made = gBoundShaderStateCtorOrig(self, declaration, strides, vs, ps, a6, a7, a8, rhi);
    gBssCreated.fetch_add(1, std::memory_order_relaxed);
    bool const repeat = BssKeySeenBefore(declaration, vs, ps);
    (repeat ? gBssRepeat : gBssFirstTime).fetch_add(1, std::memory_order_relaxed);
    if (made == nullptr || !repeat || gKeepBoundShaderStates.load(std::memory_order_relaxed) == 0 || gBssExe == nullptr) {
        return made;
    }
    if (gBssKeptTotal.load(std::memory_order_relaxed) >= kKeepBoundShaderStatesCap) {
        return made;
    }
    void* const dynamicRhi = *reinterpret_cast<void**>(gBssExe + kDynamicRhiRva);
    if (dynamicRhi == nullptr) {
        return made;
    }
    auto* const addRef = reinterpret_cast<void (*)(void*, void*)>(
        (*reinterpret_cast<void***>(dynamicRhi))[kRhiAddRefBoundShaderStateSlot / sizeof(void*)]);
    auto* const object = static_cast<std::uint8_t*>(made);
    int const refsBefore = *reinterpret_cast<int*>(object + 8);
    addRef(dynamicRhi, object + kBoundShaderStateHandle);
    int const refsAfter = *reinterpret_cast<int*>(object + 8);
    if (gBssKeptTotal.fetch_add(1, std::memory_order_relaxed) == 0) {
        log::Info("render levers: bound shader states kept: the RHI's AddRef is at RVA 0x{:X}; the int at +8 went {} -> {} "
                  "on the first kept object",
            reinterpret_cast<std::uintptr_t>(addRef) - reinterpret_cast<std::uintptr_t>(gBssExe), refsBefore, refsAfter);
    }
    gBssKept.fetch_add(1, std::memory_order_relaxed);
    return made;
}

// ---- Umbra's visibility resolved once a frame (research/35, attempt 46) ---------------------------------------
// Both eyes share ONE Umbra camera (attempt 21: ULocalPlayer's, copied into each view at +0x4D4), so the second eye's
// resolve traverses the same cells from the same camera and reports the same instances, then issues and waits for its
// own occluder queries: 21% of the heavy frame, half of it pure repetition. The resolve itself must run for each eye
// (attempts 10-11: it drains Umbra's command ring; a render that skips it deadlocks the game thread), so this keeps the
// second eye's resolve and makes it cheap instead: the camera's parameter word (camera +0x10 -> impl, impl +0x420 ->
// the word Umbra::Camera::resolveVisibility writes into its resolve command, 0xD4BBA0) loses the bits in
// [Performance] umbraSecondEyeClearBits (default 2, which should be OCCLUSION_CULLING in Umbra OB 2.6; the log says
// what the word is) for the second eye's render, so Umbra does a frustum pass and no occlusion, and the visible-
// instance hook then lets through only the primitives the first eye's resolve reported this frame (their
// FPrimitiveSceneInfo pointers, record +0, in a per-frame set): the second eye gets the first eye's visible set exactly.
// [Performance] umbraSharedVisibility.
extern thread_local int t_renderIndex;   // defined below (the render's index within the frame)
template <typename T>
T& Field(std::uint8_t* object, std::size_t offset);   // defined below
constexpr std::size_t kViewUmbraCamera = 0x4D4;
constexpr std::size_t kUmbraCameraImpl = 0x10;
constexpr std::size_t kUmbraCameraImplParams = 0x420;
std::atomic<int> gUmbraSharedVisibility{0};
std::atomic<int> gUmbraSecondEyeClearBits{2};
std::atomic<int> gUmbraParamsLogged{0};
std::atomic<std::uint64_t> gSecondEyeReported{0};
std::atomic<std::uint64_t> gSecondEyeFiltered{0};
std::atomic<std::uint64_t> gSecondEyeRendersCheap{0};
constexpr std::size_t kMaxQueryResults = 4096;
std::uint64_t gFirstEyeQueryResults[kMaxQueryResults];
std::size_t gFirstEyeQueryCount = 0;
std::size_t gSecondEyeQueryIndex = 0;
constexpr std::size_t kVisibleSetSize = 1 << 14;   // 16K slots for ~1,500 pointers a frame
void* gFirstEyeVisible[kVisibleSetSize];
std::size_t gFirstEyeVisibleCount = 0;

std::size_t VisibleSlot(void* key) {
    std::uint64_t h = reinterpret_cast<std::uintptr_t>(key) * 0x9E3779B97F4A7C15ull;
    h ^= h >> 31;
    return static_cast<std::size_t>(h) & (kVisibleSetSize - 1);
}
void VisibleSetClear() {
    std::memset(gFirstEyeVisible, 0, sizeof(gFirstEyeVisible));
    gFirstEyeVisibleCount = 0;
}
void VisibleSetAdd(void* key) {
    if (key == nullptr || gFirstEyeVisibleCount >= kVisibleSetSize / 2) {
        return;
    }
    std::size_t slot = VisibleSlot(key);
    while (gFirstEyeVisible[slot] != nullptr) {
        if (gFirstEyeVisible[slot] == key) {
            return;
        }
        slot = (slot + 1) & (kVisibleSetSize - 1);
    }
    gFirstEyeVisible[slot] = key;
    ++gFirstEyeVisibleCount;
}
bool VisibleSetHas(void* key) {
    if (key == nullptr) {
        return false;
    }
    std::size_t slot = VisibleSlot(key);
    while (gFirstEyeVisible[slot] != nullptr) {
        if (gFirstEyeVisible[slot] == key) {
            return true;
        }
        slot = (slot + 1) & (kVisibleSetSize - 1);
    }
    return false;
}

// The Umbra camera's parameter word of the renderer's first view, or nullptr.
std::uint32_t* UmbraCameraParams(std::uint8_t* renderer) {
    auto* const views = Field<std::uint8_t*>(renderer, 0x60);
    if (views == nullptr || Field<int>(renderer, 0x68) <= 0) {
        return nullptr;
    }
    auto* const camera = Field<std::uint8_t*>(views, kViewUmbraCamera);
    if (camera == nullptr) {
        return nullptr;
    }
    auto* const impl = Field<std::uint8_t*>(camera, kUmbraCameraImpl);
    if (impl == nullptr) {
        return nullptr;
    }
    return Field<std::uint32_t*>(impl, kUmbraCameraImplParams);
}

// ---- Screen-size cull of Umbra's visible instances (research/35, attempt 45) ------------------------------------
// The engine's [SystemSettings] cannot cut static mesh draws (2,500 of the 4,500 at the heavy spot, attempt 44) and it
// has no cull-distance scale; it does honour each primitive's own MinDrawDistance / MaxDrawDistance in the function the
// Umbra commander calls for every visible instance (0x8BE0B0: (commander, compact primitive record, distance squared,
// ...): the record is the FPrimitiveSceneInfo's bounds (origin +0x20, extent +0x2C, sphere radius +0x38, from the scene
// info's +0x54..+0x6C), the distance is from the view origin to the box, and a primitive outside its range is not
// marked visible). This lever adds a size test in front of it: the sphere radius projected at that distance, in pixels
// of a 2160-high eye (radius / distance x 1080 x the view's projection [1][1], the view at commander +0x68 and its
// projection matrix at +0xD0), and an instance smaller than [Performance] minScreenRadiusPx is not marked visible: no
// draws for it in either pass. The log carries the distribution of the projected sizes, so the threshold's cost in
// draws is known before it is set. 0 (default) measures only.
template <typename T>
T& Field(std::uint8_t* object, std::size_t offset);   // defined below
constexpr std::uintptr_t kUmbraInstanceVisibleRva = 0x8BE0B0;
constexpr std::size_t kInstanceRecordRadius = 0x38;
constexpr std::size_t kCommanderViewField = 0x68;
constexpr std::size_t kViewProjectionMatrix = 0xD0;
using tUmbraInstanceVisible = int(void* commander, void* record, float distanceSq, std::uint32_t a4, std::uint32_t a5,
                                  std::uint32_t a6, int a7);
tUmbraInstanceVisible* gUmbraInstanceVisibleOrig = nullptr;
bool gUmbraInstanceVisibleHooked = false;
std::atomic<int> gMinScreenRadiusTenths{0};   // the threshold in tenths of a pixel (an int for the atomic)
std::atomic<std::uint64_t> gInstancesSeen{0};
std::atomic<std::uint64_t> gInstancesCulled{0};
constexpr int kSizeBins = 8;   // < 1, < 2, < 4, < 8, < 16, < 32, < 64, the rest (pixels of radius)
std::atomic<std::uint64_t> gSizeBins[kSizeBins];
std::atomic<int> gProjectionLogged{0};

int UmbraInstanceVisibleHook(void* commander, void* record, float distanceSq, std::uint32_t a4, std::uint32_t a5,
                             std::uint32_t a6, int a7) {
    gInstancesSeen.fetch_add(1, std::memory_order_relaxed);
    if (record != nullptr && gUmbraSharedVisibility.load(std::memory_order_relaxed) != 0) {
        void* const primitive = *static_cast<void**>(record);   // the FPrimitiveSceneInfo the record was built from
        if (t_renderIndex == 0) {
            VisibleSetAdd(primitive);
        } else if (t_renderIndex == 1) {
            gSecondEyeReported.fetch_add(1, std::memory_order_relaxed);
            if (!VisibleSetHas(primitive)) {
                gSecondEyeFiltered.fetch_add(1, std::memory_order_relaxed);
                return 0;
            }
        }
    }
    auto* const c = static_cast<std::uint8_t*>(commander);
    auto* const view = Field<std::uint8_t*>(c, kCommanderViewField);
    if (view != nullptr && record != nullptr && distanceSq > 1.0f) {
        float const radius = Field<float>(static_cast<std::uint8_t*>(record), kInstanceRecordRadius);
        float const proj11 = Field<float>(view, kViewProjectionMatrix + 0x14);
        if (radius > 0.0f && proj11 > 0.0f && proj11 < 10.0f) {
            float const pixels = radius / std::sqrt(distanceSq) * 1080.0f * proj11;
            int bin = 0;
            for (float edge = 1.0f; bin < kSizeBins - 1 && pixels >= edge; edge *= 2.0f) {
                ++bin;
            }
            gSizeBins[bin].fetch_add(1, std::memory_order_relaxed);
            if (gProjectionLogged.exchange(1, std::memory_order_relaxed) == 0) {
                log::Info("render levers: screen-size cull sees the view's projection [1][1] = {:.3f}, first instance radius {:.1f} "
                          "at {:.1f} UU = {:.1f} px of a 2160-high eye", proj11, radius, std::sqrt(distanceSq), pixels);
            }
            int const tenths = gMinScreenRadiusTenths.load(std::memory_order_relaxed);
            if (tenths > 0 && pixels * 10.0f < static_cast<float>(tenths)) {
                gInstancesCulled.fetch_add(1, std::memory_order_relaxed);
                return 0;
            }
        }
    }
    return gUmbraInstanceVisibleOrig(commander, record, distanceSq, a4, a5, a6, a7);
}

constexpr std::uintptr_t kUmbraReadResultRva = 0xD48380;
using tUmbraReadResult = void(void* ctx, int head, int count, std::uint8_t wait);
tUmbraReadResult* gUmbraReadResultOrig = nullptr;
bool gUmbraReadResultHooked = false;
std::atomic<int> gUmbraShareResults{0};
thread_local bool t_inUmbraRead = false;
thread_local int t_umbraQueryIndex = 0;  // results delivered so far in this render
constexpr int kMaxUmbraResults = 8192;
std::uint64_t gFirstEyeResults[kMaxUmbraResults];
int gFirstEyeResultCount = 0;
std::atomic<std::uint64_t> gUmbraShared{0}, gUmbraShareMissing{0};

void UmbraReadResultHook(void* ctx, int head, int count, std::uint8_t wait) {
    t_inUmbraRead = true;
    gUmbraReadResultOrig(ctx, head, count, wait);
    t_inUmbraRead = false;
}
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

std::atomic<std::uint64_t> gSecondEyeQueriesSkipped{0};
std::atomic<std::uint64_t> gSecondEyeFakeResults{0};

void UmbraIssueQueriesHook(void* commander) {
    auto* const c = static_cast<std::uint8_t*>(commander);
    // umbraSharedVisibility = 2: the second eye issues no occluder queries (its visible set is the first eye's anyway);
    // the results read (command 6) then answers Umbra's pending queries as "visible" without touching the GPU.
    if (t_renderIndex == 1 && gUmbraSharedVisibility.load(std::memory_order_relaxed) >= 2) {
        gSecondEyeQueriesSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
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
thread_local bool t_skipPasses = false;   // sharePass = 3: the second render's pass functions return at once
thread_local bool t_replayToRightHalf = false;   // sharePass = 3: the replay's side-by-side rects go to the right half
draw_replay::ViewFields gFirstEyeViews;        // sharePass = 3: the first render's view fields, read at its deleter
bool gFirstEyeViewsValid = false;
std::atomic<void*> gCaptureCandidate{nullptr};  // the first render, whose view fields its deleter captures

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
    for (ByteRange const& r : kViewCaptureRanges) {
        f.perView += r.end - r.begin;
    }
    f.views = Field<int>(renderer, 0x68);
    f.bytes.reserve(f.perView * static_cast<std::size_t>(f.views));
    for (int i = 0; i < f.views; ++i) {
        std::uint8_t const* const v = View(renderer, i);
        for (ByteRange const& r : kViewCaptureRanges) {
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
        int const result = draw_replay::Replay(first, second, t_replayToRightHalf);
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
    if (t_skipPasses) {
        return;  // mode 3: the replay re-issues it (the tail starts here)
    }
    gDpgLightTargetsOrig(renderer, dpg);
}

void* DeleteRendererHook(void* renderer, std::uint64_t flags) {
    if (gCaptureCandidate.load(std::memory_order_relaxed) == renderer) {
        gFirstEyeViews = CaptureViews(static_cast<std::uint8_t*>(renderer));
        gFirstEyeViewsValid = true;
        gCaptureCandidate.store(nullptr, std::memory_order_relaxed);
    }
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

// ---- sharePass = 3: the second render keeps its resolve, the replay takes its passes (research/37) ---------------
// Umbra's command streams are 512-byte buffers drained by the resolve, one frame behind the game thread's pushes: two
// FViewportClient::Draw calls a frame need two resolves (experiment 1 froze the game with one). So the second render
// runs the engine's InitViews (CalcSceneView was the game thread's; the resolve drains the stream) with the pass
// functions after the resolve returning at once for it, and then re-issues the first render's recorded tail with the
// second render's view bytes substituted. The first render is recorded (stream 0) and its view fields kept.
std::atomic<std::uint64_t> gMode3Replays{0};
std::atomic<std::uint64_t> gFirstDrawsIssued{0}, gFirstDrawsRecorded{0}, gFirstRendersShort{0};
std::atomic<std::uint64_t> gMode3NotReplayable{0};
std::atomic<std::uint64_t> gMode3PassesSkipped{0};
std::string gMode3LastBlocker;
bool gPassHooksInstalled = false;

using tPassVoid = void(void* renderer);
using tDpgPass = void(void* renderer, int dpg, std::uint32_t* dirty);
using tDpgLights = void(void* renderer, int dpg, int translucency, std::uint32_t* dirty, int flag);
using tForeground = std::uint64_t(void* renderer, int dpg, int* out, std::uint32_t* dirty, int flag);
using tFinish = void(void* renderer, int translucency, std::uint32_t dirty);
tPassVoid* gPrepassOrig = nullptr;
tPassVoid* gBeforeLightsOrig = nullptr;
tDpgPass* gDpgPassOrig = nullptr;
tDpgLights* gDpgLightsOrig = nullptr;
tForeground* gForegroundOrig = nullptr;
tFinish* gFinishOrig = nullptr;

void PrepassHook(void* renderer) {
    if (t_skipPasses) { gMode3PassesSkipped.fetch_add(1, std::memory_order_relaxed); return; }
    gPrepassOrig(renderer);
}
void BeforeLightsHook(void* renderer) {
    if (t_skipPasses) { return; }
    gBeforeLightsOrig(renderer);
}
void DpgPassHook(void* renderer, int dpg, std::uint32_t* dirty) {
    if (t_skipPasses) { return; }
    gDpgPassOrig(renderer, dpg, dirty);
}
void DpgLightsHook(void* renderer, int dpg, int translucency, std::uint32_t* dirty, int flag) {
    if (t_skipPasses) { return; }
    gDpgLightsOrig(renderer, dpg, translucency, dirty, flag);
}
std::uint64_t ForegroundHook(void* renderer, int dpg, int* out, std::uint32_t* dirty, int flag) {
    if (t_skipPasses) { if (out != nullptr) *out = 0; return 0; }
    return gForegroundOrig(renderer, dpg, out, dirty, flag);
}
void ReplaySecondEyeInFinish(std::uint8_t* renderer);
int OneDrawSecondEyeGuarded(std::uint8_t* renderer) noexcept;
int OneDrawCheckGuarded(std::uint8_t* renderer) noexcept;   // the fallback's health check (below)
thread_local bool t_oneDrawFinish = false;   // sharePass = 5: this render's finish ends the recording and replays
thread_local bool t_oneDrawCheck = false;    // sharePass = 5 in the fallback: this render's finish checks the record match
void FinishHook(void* renderer, int translucency, std::uint32_t dirty) {
    if (t_skipPasses) {
        ReplaySecondEyeInFinish(static_cast<std::uint8_t*>(renderer));
        return;
    }
    gFinishOrig(renderer, translucency, dirty);
    if (t_oneDrawFinish) {
        // The first eye is finished and in the backbuffer; its views are still alive here (Render frees them before
        // it returns). The recording ends now (the replay's own calls must not join it), then the second eye.
        t_oneDrawFinish = false;
        draw_replay::EndRecording();
        OneDrawSecondEyeGuarded(static_cast<std::uint8_t*>(renderer));
    } else if (t_oneDrawCheck) {
        t_oneDrawCheck = false;
        OneDrawCheckGuarded(static_cast<std::uint8_t*>(renderer));
    }
}

// The frame's second render in mode 3: the engine's InitViews with its resolve, the pass functions returning at once,
// and in its finish (the renderer alive, its view fields final) the replay of the first eye's tail. True = handled.

void ReplaySecondEyeInFinish(std::uint8_t* renderer) {
    draw_replay::ViewFields const second = CaptureViews(renderer);
    std::string why;
    if (!gFirstEyeViewsValid || gFirstEyeViews.views != second.views || gFirstEyeViews.perView != second.perView) {
        gMode3NotReplayable.fetch_add(1, std::memory_order_relaxed);
        gMode3LastBlocker = std::format("first-eye views {}x{} vs {}x{}", gFirstEyeViews.views, gFirstEyeViews.perView, second.views, second.perView);
        return;
    }
    if (!draw_replay::Attached() || !draw_replay::TailReplayable(&why)) {
        gMode3NotReplayable.fetch_add(1, std::memory_order_relaxed);
        gMode3LastBlocker = why.empty() ? "recorder not attached" : why;
        return;
    }
    t_skipPasses = false;   // the replay's own calls go through the pass hooks' originals untouched (none are called)
    t_replayToRightHalf = false;  // the eyes render in turn into the whole backbuffer (Halcyon's pass-0 snapshot takes the first): no half
    int const rc = ReplayDrawsSEH(renderer, gFirstEyeViews, second);
    t_replayToRightHalf = false;
    t_skipPasses = true;
    if (rc == 0) {
        gMode3Replays.fetch_add(1, std::memory_order_relaxed);
    } else {
        gMode3NotReplayable.fetch_add(1, std::memory_order_relaxed);
        gMode3LastBlocker = std::format("replay returned {}", rc);
    }
}

bool SecondRenderMode3(std::uint8_t* renderer) {
    t_skipPasses = true;
    gSceneRenderOrig(renderer);
    t_skipPasses = false;
    return true;
}

// ---- sharePass = 5: one FViewportClient::Draw a frame, the second eye's view synthesised (research/37) ----------
std::atomic<std::uint64_t> gMode5Replays{0}, gMode5NoRecord{0}, gMode5NotReplayable{0}, gMode5SecondRenders{0};
std::string gMode5LastBlocker;

void Mul4(float const* a, float const* b, float* out) {  // row-major, row vectors: out = a x b
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float v = 0.0f;
            for (int k = 0; k < 4; ++k) {
                v += a[r * 4 + k] * b[k * 4 + c];
            }
            out[r * 4 + c] = v;
        }
    }
}

bool Invert4(float const* m, float* out) {  // Gauss-Jordan in double
    double a[4][8];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            a[r][c] = m[r * 4 + c];
            a[r][4 + c] = r == c ? 1.0 : 0.0;
        }
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r) {
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) pivot = r;
        }
        if (std::abs(a[pivot][col]) < 1e-12) return false;
        if (pivot != col) {
            for (int c = 0; c < 8; ++c) std::swap(a[pivot][c], a[col][c]);
        }
        double const inv = 1.0 / a[col][col];
        for (int c = 0; c < 8; ++c) a[col][c] *= inv;
        for (int r = 0; r < 4; ++r) {
            if (r == col) continue;
            double const f = a[r][col];
            if (f == 0.0) continue;
            for (int c = 0; c < 8; ++c) a[r][c] -= f * a[col][c];
        }
    }
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) out[r * 4 + c] = static_cast<float>(a[r][4 + c]);
    }
    return true;
}

// The view pass 1 would have had, from pass 0's final view and what pass 0 recorded (MevrBridge::Pass1View): the
// camera moved by the recorded delta (view origin + d, pre-view translation - d, the view and world
// view-projection moved to match), the GPU matrix rebuilt from the translated view and pass 1's own frustum terms
// (mode 2) or kept (symmetric), its inverse recomputed. False when pass 0 recorded nothing this frame.
bool SynthesiseSecondView(std::uint8_t* renderer, draw_replay::ViewFields const& first, draw_replay::ViewFields& second) {
    constexpr std::size_t kV = CapturedOffset(0x90), kP = CapturedOffset(0xD0), kTV = CapturedOffset(0x190),
        kTVP = CapturedOffset(0x1D0), kInv = CapturedOffset(0x210), kPvt = CapturedOffset(0x250),
        kVP = CapturedOffset(0x260), kVo = CapturedOffset(0x320);
    static_assert(kV != SIZE_MAX && kP != SIZE_MAX && kTV != SIZE_MAX && kTVP != SIZE_MAX && kInv != SIZE_MAX &&
        kPvt != SIZE_MAX && kVP != SIZE_MAX && kVo != SIZE_MAX, "the capture ranges must hold every matrix used");
    if (first.views < 1 || first.perView < kVo + 16) {
        return false;
    }
    auto const* const vo0 = reinterpret_cast<float const*>(View(renderer, 0) + 0x320);
    auto const* const vm0 = reinterpret_cast<float const*>(View(renderer, 0) + 0x90);
    float const fwd0[3] = {vm0[2], vm0[6], vm0[10]};   // the view matrix's third column, as pass 0 recorded it
    MELEVR::MevrBridge::Pass1View p{};
    if (!MELEVR::MevrBridge::FindPass1View(vo0, fwd0, &p)) {
        return false;
    }
    second = first;
    float const d[3] = {p.delta[0], p.delta[1], p.delta[2]};
    for (int i = 0; i < first.views; ++i) {
        auto* const b = second.bytes.data() + static_cast<std::size_t>(i) * first.perView;
        auto const* const a = first.bytes.data() + static_cast<std::size_t>(i) * first.perView;
        auto* const vo = reinterpret_cast<float*>(b + kVo);
        auto* const pvt = reinterpret_cast<float*>(b + kPvt);
        for (int k = 0; k < 3; ++k) {
            vo[k] += d[k];
            pvt[k] -= d[k];
        }
        TranslateRows(reinterpret_cast<float const*>(a + kV), d, -1.0f, reinterpret_cast<float*>(b + kV));
        TranslateRows(reinterpret_cast<float const*>(a + kVP), d, -1.0f, reinterpret_cast<float*>(b + kVP));
        auto* const tvp = reinterpret_cast<float*>(b + kTVP);
        if (p.mode == 2) {
            float P[16];
            std::memcpy(P, a + kP, sizeof(P));
            if (std::abs(P[15]) <= 0.01f) {  // perspective: the off-centre terms for w = z, as Halcyon writes them
                float const w = std::abs(P[11]) > 0.01f ? P[11] : 1.0f;
                P[0] = p.p0 * w;
                P[5] = p.p5 * w;
                P[8] = p.p8 * w;
                P[9] = p.p9 * w;
                Mul4(reinterpret_cast<float const*>(a + kTV), P, tvp);
            }
        }
        float inv[16];
        if (Invert4(tvp, inv)) {
            std::memcpy(b + kInv, inv, sizeof(inv));
        }
    }
    return true;
}

std::atomic<bool> gOneDrawFaulted{false};

// One Draw's health (research/37, the sweep): after a level load into the game's intro the rendered view sat 70 uu from
// every record pass 0 had made (a view edited after the record), so no second eye could be made and the right eye
// showed the left view. While the render thread finds no matching record for 3 frames running, Halcyon's second Draw
// resumes (g_oneDrawStereo cleared; the engine renders the right eye as in sharePass 0) and the host only checks each
// frame, with the views alive, whether a record would match; after 30 matches running one Draw resumes.
std::atomic<bool> gOneDrawWanted{false};     // [Performance] sharePass = 5 and the hooks in place
std::atomic<int> gOneDrawWindow{1};          // [Performance] oneDrawWindow: both eyes on the symmetric window (1) or each on its own frustum (0)
std::atomic<bool> gOneDrawFallback{false};   // Halcyon's second Draw resumed by the health check
unsigned gOneDrawMisses = 0, gOneDrawMatches = 0;   // render thread
std::atomic<std::uint64_t> gOneDrawFallbacks{0}, gOneDrawRearms{0};
void NoteOneDrawMatch(bool matched) {
    if (matched) {
        gOneDrawMisses = 0;
        if (gOneDrawFallback.load(std::memory_order_relaxed) && ++gOneDrawMatches >= 300) {
            gOneDrawMatches = 0;
            gOneDrawFallback.store(false, std::memory_order_relaxed);
            MELEVR::MevrBridge::g_oneDrawStereo.store(gOneDrawWanted.load(std::memory_order_relaxed), std::memory_order_relaxed);
            gOneDrawRearms.fetch_add(1, std::memory_order_relaxed);
            log::Info("render levers: one Draw: pass 1's records match the rendered view again (300 frames running): one Draw a frame resumes");
        }
    } else {
        gOneDrawMatches = 0;
        if (!gOneDrawFallback.load(std::memory_order_relaxed) && ++gOneDrawMisses >= 3) {
            gOneDrawMisses = 0;
            gOneDrawFallback.store(true, std::memory_order_relaxed);
            MELEVR::MevrBridge::g_oneDrawStereo.store(false, std::memory_order_relaxed);
            gOneDrawFallbacks.fetch_add(1, std::memory_order_relaxed);
            log::Info("render levers: one Draw: no second eye for 3 frames running (no matching pass-1 record, or the tail not replayable: {}): Halcyon's second Draw resumes until the frames replay again", gMode5LastBlocker.empty() ? std::string("-") : gMode5LastBlocker);
        }
    }
}
void OneDrawSecondEye(std::uint8_t* renderer);
// In the fallback: would a record match this render's view? (The views alive: the finish hook.)
void OneDrawCheck(std::uint8_t* renderer) {
    auto const* const vo0 = reinterpret_cast<float const*>(View(renderer, 0) + 0x320);
    auto const* const vm0 = reinterpret_cast<float const*>(View(renderer, 0) + 0x90);
    float const fwd0[3] = {vm0[2], vm0[6], vm0[10]};
    MELEVR::MevrBridge::Pass1View p{};
    NoteOneDrawMatch(MELEVR::MevrBridge::FindPass1View(vo0, fwd0, &p));
}
int OneDrawCheckGuarded(std::uint8_t* renderer) noexcept {
    if (gOneDrawFaulted.load(std::memory_order_relaxed)) {
        return 2;
    }
    __try {
        OneDrawCheck(renderer);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gOneDrawFaulted.store(true, std::memory_order_relaxed);
        log::Warn("render levers: one Draw: the health check faulted; no more replays this session");
        return 1;
    }
}
int OneDrawSecondEyeGuarded(std::uint8_t* renderer) noexcept {
    if (gOneDrawFaulted.load(std::memory_order_relaxed)) {
        return 2;
    }
    __try {
        OneDrawSecondEye(renderer);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // A fault on the render thread inside a hook ends the thread behind the engine's own handler and the game
        // thread then waits forever (research/37): never let one out.
        gOneDrawFaulted.store(true, std::memory_order_relaxed);
        log::Warn("render levers: one Draw: the second eye faulted; no more replays this session");
        return 1;
    }
}

void OneDrawSecondEye(std::uint8_t* renderer) {
    draw_replay::ViewFields const first = CaptureViews(renderer);
    draw_replay::ViewFields second;
    std::string why;
    if (!draw_replay::TailReplayable(&why)) {
        gMode5NotReplayable.fetch_add(1, std::memory_order_relaxed);
        gMode5LastBlocker = why;
        NoteOneDrawMatch(false);   // no second eye this frame: a miss for the health check
        return;
    }
    bool const synthesised = SynthesiseSecondView(renderer, first, second);
    NoteOneDrawMatch(synthesised);
    if (!synthesised) {
        gMode5NoRecord.fetch_add(1, std::memory_order_relaxed);
        static std::uint64_t nextLog = 0;
        std::uint64_t const now = GetTickCount64();
        if (now >= nextLog) {
            nextLog = now + 5000;
            auto const* const vo = reinterpret_cast<float const*>(View(renderer, 0) + 0x320);
            MELEVR::MevrBridge::Pass1View newest{};
            bool const have = MELEVR::MevrBridge::NewestPass1View(&newest);
            log::Info("render levers: one Draw: no pass-1 record for the view at ({:.1f}, {:.1f}, {:.1f}); {} records made, newest at ({:.1f}, {:.1f}, {:.1f}) delta ({:.2f}, {:.2f}, {:.2f}) mode {}",
                vo[0], vo[1], vo[2], MELEVR::MevrBridge::Pass1Records(), have ? newest.viewOrigin[0] : 0.0f, have ? newest.viewOrigin[1] : 0.0f,
                have ? newest.viewOrigin[2] : 0.0f, have ? newest.delta[0] : 0.0f, have ? newest.delta[1] : 0.0f, have ? newest.delta[2] : 0.0f, have ? newest.mode : -1);
        }
        return;
    }
    {
        // Research/37: the synthesised view, every 5 s (a frozen head gave a sheared right eye, a moving one a right one).
        static std::uint64_t nextDump = 0;
        std::uint64_t const now = GetTickCount64();
        if (now >= nextDump) {
            nextDump = now + 5000;
            auto const row = [](std::uint8_t const* b, std::size_t at) {
                auto const* f = reinterpret_cast<float const*>(b + at);
                return std::format("({:.4g}, {:.4g}, {:.4g}, {:.4g})", f[0], f[1], f[2], f[3]);
            };
            std::uint8_t const* const a = first.bytes.data();
            std::uint8_t const* const b = second.bytes.data();
            constexpr std::size_t kTVP = CapturedOffset(0x1D0), kInv = CapturedOffset(0x210), kPvt = CapturedOffset(0x250),
                kVo = CapturedOffset(0x320), kP = CapturedOffset(0xD0), kTV = CapturedOffset(0x190);
            log::Info("render levers: one Draw synth: VO {} -> {}; pvt {} -> {}; TVP rows {} {} {} {} -> {} {} {} {}; inv row0 {} -> {}; P row0 {} row2 {} row3 {}; TVM row3 {}",
                row(a, kVo), row(b, kVo), row(a, kPvt), row(b, kPvt),
                row(a, kTVP), row(a, kTVP + 16), row(a, kTVP + 32), row(a, kTVP + 48),
                row(b, kTVP), row(b, kTVP + 16), row(b, kTVP + 32), row(b, kTVP + 48),
                row(a, kInv), row(b, kInv), row(a, kP), row(a, kP + 32), row(a, kP + 48), row(a, kTV + 48));
        }
    }
    t_replayToRightHalf = false;
    int const rc = ReplayDrawsSEH(renderer, first, second);
    if (rc == 0) {
        gMode5Replays.fetch_add(1, std::memory_order_relaxed);
    } else {
        gMode5NotReplayable.fetch_add(1, std::memory_order_relaxed);
        gMode5LastBlocker = std::format("replay returned {}", rc);
    }
}

std::atomic<std::uint64_t> gSecondRenderSkipped{0};

void SceneRenderHook(void* renderer) {
    int const index = gRenderIndex.fetch_add(1, std::memory_order_relaxed);
    int const outer = t_renderIndex;
    t_renderIndex = index;
    gRenderThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    bool const eyeHistory = BeginEyeHistory(renderer, index);
    t_umbraQueryIndex = 0;
    if (index == 0) {
        gFirstEyeResultCount = 0;
    }
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    bool handled = false;
    int mode = gSharePass.load(std::memory_order_relaxed);
    bool const diff = draw_replay::DiffPending() && draw_replay::Attached();
    if (diff && (mode == 0 || mode == 3)) {
        mode = 1;  // the eye diff needs a paired frame rendered by the engine: this one runs as sharePass = 1
    }
    // sharePass = 4 (research/37, experiment 1): the frame's second scene render is simply not run (no renderer kept,
    // nothing replayed, the second eye's image stale). The test of the deadlock theory of attempts 10-11: if two
    // FViewportClient::Draw calls with one Umbra resolve a frame deadlock the game thread in Umbra's command ring,
    // this hangs within seconds; if it runs, the hang was the kept renderer's and the replay can take the second
    // render's slot without a second resolve.
    if (mode == 3 && gPassHooksInstalled && gReplayHooked) {
        ReleaseKept();
        if (index == 0) {
            bool const recorder = draw_replay::Attached() && MELEVR::MevrBridge::StereoFrameActive();
            if (recorder) {
                draw_replay::BeginRecording(0);
            }
            gFirstEyeViewsValid = false;
            gCaptureCandidate.store(recorder ? renderer : nullptr, std::memory_order_relaxed);
            std::uint32_t const drawsBefore = MELEVR::MevrBridge::g_perfDraws;
            gSceneRenderOrig(renderer);
            gCaptureCandidate.store(nullptr, std::memory_order_relaxed);
            if (recorder) {
                draw_replay::EndRecording();
                // Research/37, experiment 6: how many of the render's draws (Halcyon's hooks count them all) the
                // recorder saw; a shortfall is a frame whose vtable entries were taken from under it mid-render.
                std::uint32_t const issued = MELEVR::MevrBridge::g_perfDraws - drawsBefore;
                unsigned const recorded = draw_replay::RecordedDraws(0);
                gFirstDrawsIssued.fetch_add(issued, std::memory_order_relaxed);
                gFirstDrawsRecorded.fetch_add(recorded, std::memory_order_relaxed);
                if (recorded < issued) {
                    gFirstRendersShort.fetch_add(1, std::memory_order_relaxed);
                }
            }
            handled = true;
        } else if (index == 1) {
            handled = SecondRenderMode3(static_cast<std::uint8_t*>(renderer));
        }
    } else if (mode == 5 && gReplayHooked) {
        // One Draw a frame (research/37): Halcyon makes no second Draw while g_oneDrawStereo is set; the first
        // render is recorded and, with the renderer still alive, replayed at once as the second eye.
        ReleaseKept();
        if (index == 0) {
            bool const stereo = MELEVR::MevrBridge::StereoFrameActive();
            bool const fallback = gOneDrawFallback.load(std::memory_order_relaxed);
            bool const recorder = draw_replay::Attached() && stereo && !fallback;
            if (recorder) {
                draw_replay::BeginRecording(0);
                t_oneDrawFinish = true;   // the finish hook ends the recording and replays, with the views alive
            } else if (stereo && fallback) {
                t_oneDrawCheck = true;    // the finish hook asks whether a record would match (the health check)
            }
            std::uint32_t const drawsBefore = MELEVR::MevrBridge::g_perfDraws;
            gSceneRenderOrig(renderer);
            t_oneDrawCheck = false;
            if (recorder) {
                if (t_oneDrawFinish) {   // the finish never ran (the render bailed): nothing to replay
                    t_oneDrawFinish = false;
                    draw_replay::EndRecording();
                    gMode5NotReplayable.fetch_add(1, std::memory_order_relaxed);
                    gMode5LastBlocker = "the render's finish never ran";
                }
                std::uint32_t const issued = MELEVR::MevrBridge::g_perfDraws - drawsBefore;
                unsigned const recorded = draw_replay::RecordedDraws(0);
                gFirstDrawsIssued.fetch_add(issued, std::memory_order_relaxed);
                gFirstDrawsRecorded.fetch_add(recorded, std::memory_order_relaxed);
                if (recorded < issued) {
                    gFirstRendersShort.fetch_add(1, std::memory_order_relaxed);
                }
            }
            handled = true;
        } else {
            gMode5SecondRenders.fetch_add(1, std::memory_order_relaxed);  // Halcyon drew twice anyway: the engine renders it
        }
    } else if (mode == 4) {
        ReleaseKept();
        if (index == 1) {
            gSecondRenderSkipped.fetch_add(1, std::memory_order_relaxed);
            handled = true;
        }
    } else if (gShareHooked && mode != 0 && !gShareFailed.load(std::memory_order_relaxed)) {
        handled = ShareOrKeep(static_cast<std::uint8_t*>(renderer), mode, diff);
    } else {
        ReleaseKept();
    }
    std::uint32_t* params = nullptr;
    std::uint32_t paramsBefore = 0;
    if (gUmbraSharedVisibility.load(std::memory_order_relaxed) != 0 && gUmbraInstanceVisibleHooked) {
        if (index == 0) {
            VisibleSetClear();
            gFirstEyeQueryCount = 0;
        } else if (index == 1) {
            gSecondEyeQueryIndex = 0;
            params = UmbraCameraParams(static_cast<std::uint8_t*>(renderer));
            if (params != nullptr) {
                paramsBefore = *params;
                if (gUmbraParamsLogged.exchange(1, std::memory_order_relaxed) == 0) {
                    log::Info("render levers: the Umbra camera's parameter word is 0x{:X}; the second eye's render clears 0x{:X} of it",
                        paramsBefore, static_cast<unsigned>(gUmbraSecondEyeClearBits.load(std::memory_order_relaxed)));
                }
                *params = paramsBefore & ~static_cast<std::uint32_t>(gUmbraSecondEyeClearBits.load(std::memory_order_relaxed));
                gSecondEyeRendersCheap.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    if (!handled) {
        gSceneRenderOrig(renderer);
    }
    if (params != nullptr) {
        *params = paramsBefore;
    }
    QueryPerformanceCounter(&end);
    EndEyeHistory(eyeHistory);
    if (index == 0 && gFlushFirstEye.load(std::memory_order_relaxed) != 0) {
        if (ID3D11DeviceContext* const context = draw_replay::Context()) {
            context->Flush();
            gFlushes.fetch_add(1, std::memory_order_relaxed);
        }
    }
    t_renderIndex = outer;
    int const slot = Slot(index);
    Add(gCounters.renderTicks[slot], static_cast<std::uint64_t>(end.QuadPart - begin.QuadPart));
    Add(gCounters.renders[slot], 1);
}

// The commander's command(): each call timed into its part for this render's place in the frame; with
// umbraQueryWait = 0 the resolve's start stamp is moved 4 s back as soon as it's written, so the results read
// (code 6) never spins for an unready query (it then counts as visible: a few more draws, no stall).
// Umbra's pending occluder queries live in a ring in the resolve context (commander +8): count at +0x1C8, head at
// +0x1CC, entries of 0x19 bytes from the pointer at +0x2168, each result a 64-bit word at +9 whose top bit is kept.
// Mode 2 answered every pending query "visible" and that poisoned Umbra's shared occlusion state for both eyes (8,000
// draws a frame: attempt 46). Mode 3 answers the second eye's i-th pending query with the first eye's i-th result of the
// same frame (the reads come in many calls per resolve, so a running index per render; same occluders, same order, as
// attempt 39 assumed), and "visible" only past the first eye's count.

struct QueryRing {
    std::uint8_t* base = nullptr;
    int count = 0;
    int head = 0;
};
QueryRing PendingQueries(std::uint8_t* commander) {
    QueryRing ring;
    auto* const context = Field<std::uint8_t*>(commander, 8);
    if (context == nullptr) {
        return ring;
    }
    ring.count = Field<int>(context, 0x1C8);
    ring.head = Field<int>(context, 0x1CC);
    ring.base = Field<std::uint8_t*>(context, 0x2168);
    if (ring.base == nullptr || ring.count < 0 || ring.count > 1000 || ring.head < 0) {
        ring.base = nullptr;
    }
    return ring;
}
std::uint64_t QueryResult(QueryRing const& ring, int i) {
    std::uint64_t result = 0;
    std::memcpy(&result, ring.base + static_cast<std::ptrdiff_t>(ring.head + i) * 0x19 + 9, sizeof(result));
    return result;
}
void SetQueryResult(QueryRing const& ring, int i, std::uint64_t value) {
    std::uint8_t* const at = ring.base + static_cast<std::ptrdiff_t>(ring.head + i) * 0x19 + 9;
    std::uint64_t result = 0;
    std::memcpy(&result, at, sizeof(result));
    result = (result & 0x8000000000000000ull) | (value & 0x7FFFFFFFFFFFFFFFull);
    std::memcpy(at, &result, sizeof(result));
}
// After the first eye's real read: keep what it got, in order.
void RecordFirstEyeQueryResults(std::uint8_t* commander) {
    QueryRing const ring = PendingQueries(commander);
    if (ring.base == nullptr) {
        return;
    }
    for (int i = 0; i < ring.count && gFirstEyeQueryCount < kMaxQueryResults; ++i) {
        gFirstEyeQueryResults[gFirstEyeQueryCount++] = QueryResult(ring, i) & 0x7FFFFFFFFFFFFFFFull;
    }
}
bool FakeSecondEyeQueryResults(std::uint8_t* commander, int mode) {
    QueryRing const ring = PendingQueries(commander);
    if (ring.base == nullptr) {
        return false;
    }
    for (int i = 0; i < ring.count; ++i) {
        std::uint64_t value = 1;   // visible, when the first eye had no i-th result
        if (mode == 3 && gSecondEyeQueryIndex < gFirstEyeQueryCount) {
            value = gFirstEyeQueryResults[gSecondEyeQueryIndex];
        }
        ++gSecondEyeQueryIndex;
        SetQueryResult(ring, i, value);
    }
    Field<int>(commander, 0x94) = 0;   // as the game's read does first
    gSecondEyeFakeResults.fetch_add(static_cast<std::uint64_t>(ring.count), std::memory_order_relaxed);
    return true;
}

void CommandHook(void* commander, unsigned code) {
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    int const sharedMode = gUmbraSharedVisibility.load(std::memory_order_relaxed);
    if (code == 6 && t_renderIndex == 1 && sharedMode >= 2 &&
        FakeSecondEyeQueryResults(static_cast<std::uint8_t*>(commander), sharedMode)) {
        // no GPU read for the second eye
    } else {
        gCommandOrig(commander, code);
        if (code == 6 && t_renderIndex == 0 && sharedMode == 3) {
            RecordFirstEyeQueryResults(static_cast<std::uint8_t*>(commander));
        }
    }
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
    if (t_inUmbraRead && gUmbraShareResults.load(std::memory_order_relaxed) != 0 && out != nullptr && size >= 4) {
        int const i = t_umbraQueryIndex;
        if (t_renderIndex == 0) {
            int const rc = gGetQueryDataOrig(rhi, query, out, size, wait);
            if (rc != 0) {
                if (i < kMaxUmbraResults) {
                    std::uint64_t value = 0;
                    std::memcpy(&value, out, size >= 8 ? 8 : 4);
                    gFirstEyeResults[i] = value;
                    gFirstEyeResultCount = i + 1;
                }
                ++t_umbraQueryIndex;
            } else {
                gQueryReadsFailed[slot].fetch_add(1, std::memory_order_relaxed);
            }
            return rc;
        }
        if (t_renderIndex == 1) {
            int rc = gGetQueryDataOrig(rhi, query, out, size, 0);
            if (rc != 0) {
                ++t_umbraQueryIndex;
                return rc;
            }
            if (i < gFirstEyeResultCount) {
                std::memcpy(out, &gFirstEyeResults[i], size >= 8 ? 8 : 4);
                ++t_umbraQueryIndex;
                gUmbraShared.fetch_add(1, std::memory_order_relaxed);
                return 1;
            }
            gUmbraShareMissing.fetch_add(1, std::memory_order_relaxed);
            rc = gGetQueryDataOrig(rhi, query, out, size, wait);
            if (rc != 0) {
                ++t_umbraQueryIndex;
            } else {
                gQueryReadsFailed[slot].fetch_add(1, std::memory_order_relaxed);
            }
            return rc;
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
    static std::wstring path = MELEVR::MevrBridge::SettingsPath();  // <exe dir>\ASI\LE1MEVR.ini, as the mod
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
        std::uint64_t const flushes = gFlushes.exchange(0, std::memory_order_relaxed);
        if (flushes != 0) {
            share += std::format("; GPU flushed after the first eye: {} frames", flushes);
        }
        std::uint64_t const bssCreated = gBssCreated.exchange(0, std::memory_order_relaxed);
        std::uint64_t const bssKept = gBssKept.exchange(0, std::memory_order_relaxed);
        std::uint64_t const bssFirst = gBssFirstTime.exchange(0, std::memory_order_relaxed);
        std::uint64_t const bssRepeat = gBssRepeat.exchange(0, std::memory_order_relaxed);
        if (bssCreated != 0 || bssKept != 0) {
            share += std::format("; bound shader states: {:.1f} created a frame ({} new keys, {} keys seen before), {} kept this "
                                 "period ({} in all)",
                static_cast<double>(bssCreated) / static_cast<double>(frames), bssFirst, bssRepeat, bssKept,
                gBssKeptTotal.load(std::memory_order_relaxed));
        }
        std::uint64_t const seen = gInstancesSeen.exchange(0, std::memory_order_relaxed);
        std::uint64_t const culled = gInstancesCulled.exchange(0, std::memory_order_relaxed);
        if (seen != 0) {
            std::uint64_t bins[kSizeBins];
            for (int b = 0; b < kSizeBins; ++b) {
                bins[b] = gSizeBins[b].exchange(0, std::memory_order_relaxed);
            }
            double const f = static_cast<double>(frames);
            share += std::format("; visible instances a frame {:.0f}, culled {:.0f}; by projected radius (px): <1 {:.0f}, <2 {:.0f}, "
                                 "<4 {:.0f}, <8 {:.0f}, <16 {:.0f}, <32 {:.0f}, <64 {:.0f}, more {:.0f}",
                static_cast<double>(seen) / f, static_cast<double>(culled) / f, bins[0] / f, bins[1] / f, bins[2] / f,
                bins[3] / f, bins[4] / f, bins[5] / f, bins[6] / f, bins[7] / f);
        }
        std::uint64_t const reported = gSecondEyeReported.exchange(0, std::memory_order_relaxed);
        std::uint64_t const filtered = gSecondEyeFiltered.exchange(0, std::memory_order_relaxed);
        std::uint64_t const cheap = gSecondEyeRendersCheap.exchange(0, std::memory_order_relaxed);
        std::uint64_t const issuesSkipped = gSecondEyeQueriesSkipped.exchange(0, std::memory_order_relaxed);
        std::uint64_t const faked = gSecondEyeFakeResults.exchange(0, std::memory_order_relaxed);
        if (reported != 0 || cheap != 0 || issuesSkipped != 0) {
            share += std::format("; shared visibility: {} second-eye renders with the camera bits cleared, {:.0f} instances reported "
                                 "a frame, {:.0f} not in the first eye's set, {} query issues skipped, {:.0f} results answered a frame",
                cheap, static_cast<double>(reported) / static_cast<double>(frames), static_cast<double>(filtered) / static_cast<double>(frames),
                issuesSkipped, static_cast<double>(faked) / static_cast<double>(frames));
        }
        std::uint64_t const m3 = gMode3Replays.exchange(0, std::memory_order_relaxed);
        std::uint64_t const m3no = gMode3NotReplayable.exchange(0, std::memory_order_relaxed);
        if (m3 != 0 || m3no != 0) {
            share += std::format("; second eye replayed after its resolve: {} frames, {} not replayable ({})", m3, m3no,
                m3no != 0 ? gMode3LastBlocker : std::string("-"));
            std::uint64_t const issued = gFirstDrawsIssued.exchange(0, std::memory_order_relaxed);
            std::uint64_t const recorded = gFirstDrawsRecorded.exchange(0, std::memory_order_relaxed);
            std::uint64_t const shortRenders = gFirstRendersShort.exchange(0, std::memory_order_relaxed);
            share += std::format("; the first render's draws a frame: {:.0f} issued, {:.0f} recorded ({} renders short)",
                static_cast<double>(issued) / static_cast<double>(frames), static_cast<double>(recorded) / static_cast<double>(frames), shortRenders);
        }
        std::uint64_t const m5 = gMode5Replays.exchange(0, std::memory_order_relaxed);
        std::uint64_t const m5none = gMode5NoRecord.exchange(0, std::memory_order_relaxed);
        std::uint64_t const m5no = gMode5NotReplayable.exchange(0, std::memory_order_relaxed);
        std::uint64_t const m5second = gMode5SecondRenders.exchange(0, std::memory_order_relaxed);
        if (m5 != 0 || m5none != 0 || m5no != 0 || m5second != 0) {
            share += std::format("; one Draw a frame: {} second eyes replayed, {} frames without pass 1's record, {} not replayable ({}), "
                                 "{} second renders Halcyon still made; its health: {} fallbacks to Halcyon's second Draw, {} re-arms{}",
                m5, m5none, m5no, m5no != 0 ? gMode5LastBlocker : std::string("-"), m5second,
                gOneDrawFallbacks.exchange(0, std::memory_order_relaxed), gOneDrawRearms.exchange(0, std::memory_order_relaxed),
                gOneDrawFallback.load(std::memory_order_relaxed) ? ", in the fallback now" : "");
            std::uint64_t const issued = gFirstDrawsIssued.exchange(0, std::memory_order_relaxed);
            std::uint64_t const recorded = gFirstDrawsRecorded.exchange(0, std::memory_order_relaxed);
            std::uint64_t const shortRenders = gFirstRendersShort.exchange(0, std::memory_order_relaxed);
            share += std::format("; the render's draws a frame: {:.0f} issued, {:.0f} recorded ({} renders short)",
                static_cast<double>(issued) / static_cast<double>(frames), static_cast<double>(recorded) / static_cast<double>(frames), shortRenders);
        }
        std::uint64_t const skippedRenders = gSecondRenderSkipped.exchange(0, std::memory_order_relaxed);
        if (skippedRenders != 0) {
            share += std::format("; second scene renders not run: {}", skippedRenders);
        }
        std::uint64_t const shared = gUmbraShared.exchange(0, std::memory_order_relaxed);
        std::uint64_t const missing = gUmbraShareMissing.exchange(0, std::memory_order_relaxed);
        if (shared != 0 || missing != 0) {
            share += std::format("; Umbra results from the first eye: {:.0f} a frame, {:.1f} a frame waited past its count",
                static_cast<double>(shared) / static_cast<double>(frames), static_cast<double>(missing) / static_cast<double>(frames));
        }
    }
    draw_replay::Stats const d = draw_replay::TakeStats();
    std::uint64_t const fallbacks = gReplayFallbacks.exchange(0, std::memory_order_relaxed);
    if (gSharePass.load(std::memory_order_relaxed) == 2 || d.replays != 0 || d.failures != 0 || fallbacks != 0) {
        auto const per = [](std::uint64_t total, std::uint64_t n) { return n == 0 ? 0.0 : static_cast<double>(total) / static_cast<double>(n); };
        share += std::format("; draw replay: {} replays ({:.0f} calls, {:.0f} draws, {:.0f} substitutions of which {:.0f} matrices, {:.0f} rects shifted each), {} first eyes recorded "
            "({:.0f} calls, {:.0f} KB each; {:.0f} calls through Halcyon's seams, {:.0f} DrawIndexed through our own), {} re-hooks, matrix hits a replay: TVP {:.1f}, transposed {:.1f}, inverse {:.1f}, transposed {:.1f}; {} fell back to the engine's passes{}{}",
            d.replays, per(d.replayedCalls, d.replays), per(d.replayedDraws, d.replays), per(d.substitutions, d.replays), per(d.matrixSubstitutions, d.replays), per(d.shiftedRects, d.replays), d.recorded,
            per(d.recordedCalls, d.recorded), per(d.recordedBytes, d.recorded) / 1024.0, per(d.notedByHalcyon, d.recorded), per(d.seenByOurs, d.recorded), d.rehooks,
            per(d.matrixHits[0], d.replays), per(d.matrixHits[1], d.replays), per(d.matrixHits[2], d.replays), per(d.matrixHits[3], d.replays), fallbacks,
            d.notReplayable != 0 && !d.lastBlocker.empty() ? std::format(" ({} not replayable: {})", d.notReplayable, d.lastBlocker) : "",
            gReplayFailed.load(std::memory_order_relaxed) ? "; the draw replay is OFF after a failure" : "");
        if (d.recorded != 0) {
            // Research/37: the recording's map copies (the driver's mapped memory read back at Unmap).
            share += std::format("; recording: {:.0f} maps ({:.0f} KB) copied in {:.2f} ms a frame, {:.1f} large maps not copied; "
                                 "large buffers discarded inside the tail: {} frames ({} discards, {} shadowed)",
                per(d.mapsCopied, d.recorded), per(d.mapCopyBytes, d.recorded) / 1024.0, d.mapCopyMs / static_cast<double>(d.recorded),
                per(d.mapsSkipped, d.recorded), d.tailDiscardFrames, d.tailDiscards, d.shadowCopies);
        }
    }
    log::Info("render levers: last 10 s: {} frames; scene renders per frame {}; per render, by its place in the "
        "frame: {}{}", frames, perFrame.empty() ? "-" : perFrame, perRender.empty() ? "-" : perRender, share);
}

} // namespace

hook::Context* gHookInit = nullptr;  // the host's (Entry.cpp's static), for the draw recorder's inline hooks

bool Install(hook::Context& init) {
    gHookInit = &init;
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
    auto* const umbraRead = init.ResolveTyped<tUmbraReadResult>(kUmbraReadResultRva);
    gUmbraReadResultHooked = umbraRead != nullptr &&
        hook::Install(init, "Umbra::ReadQueryResult", umbraRead, &UmbraReadResultHook, gUmbraReadResultOrig);
    if (!gUmbraReadResultHooked) {
        log::Warn("render levers: Umbra's query result read hook failed; umbraShareResults has no effect");
    }
    gBssExe = init.ResolveTyped<std::uint8_t>(0);
    auto* const bssCtor = init.ResolveTyped<tBoundShaderStateCtor>(kBoundShaderStateCtorRva);
    gBoundShaderStateHooked = bssCtor != nullptr &&
        hook::Install(init, "FD3D11BoundShaderState::FD3D11BoundShaderState", bssCtor, &BoundShaderStateCtorHook, gBoundShaderStateCtorOrig);
    if (!gBoundShaderStateHooked) {
        log::Warn("render levers: the bound-shader-state constructor hook failed; keepBoundShaderStates has no effect");
    }
    auto* const instanceVisible = init.ResolveTyped<tUmbraInstanceVisible>(kUmbraInstanceVisibleRva);
    gUmbraInstanceVisibleHooked = instanceVisible != nullptr &&
        hook::Install(init, "UmbraCommander::InstanceVisible", instanceVisible, &UmbraInstanceVisibleHook, gUmbraInstanceVisibleOrig);
    if (!gUmbraInstanceVisibleHooked) {
        log::Warn("render levers: the Umbra visible-instance hook failed; minScreenRadiusPx has no effect");
    }
    // Not installed: a second render on commander 1 blocks in Umbra's command ring (research/35, attempt 20).
    (void)&SetCommanderHook;
    auto* const lightTargets = init.ResolveTyped<tDpgLightTargets>(kDpgLightTargetsRva);
    gReplayHooked = gShareHooked && lightTargets != nullptr &&
        hook::Install(init, "FSceneRenderer::RenderDpgLightTargets", lightTargets, &DpgLightTargetsHook, gDpgLightTargetsOrig);
    {
        auto* const prepass = init.ResolveTyped<tPassVoid>(kPrepassViewsRva);
        auto* const beforeLights = init.ResolveTyped<tPassVoid>(kBeforeLightsRva);
        auto* const dpgPass = init.ResolveTyped<tDpgPass>(kDpgPassRva);
        auto* const dpgLights = init.ResolveTyped<tDpgLights>(kDpgLightsRva);
        auto* const foreground = init.ResolveTyped<tForeground>(kForegroundRva);
        auto* const finish = init.ResolveTyped<tFinish>(kFinishRva);
        gPassHooksInstalled = prepass != nullptr && beforeLights != nullptr && dpgPass != nullptr && dpgLights != nullptr &&
            foreground != nullptr && finish != nullptr &&
            hook::Install(init, "RenderUmbraBasePass", prepass, &PrepassHook, gPrepassOrig) &&
            hook::Install(init, "before-lights pass", beforeLights, &BeforeLightsHook, gBeforeLightsOrig) &&
            hook::Install(init, "DPG pass", dpgPass, &DpgPassHook, gDpgPassOrig) &&
            hook::Install(init, "DPG lights", dpgLights, &DpgLightsHook, gDpgLightsOrig) &&
            hook::Install(init, "foreground pass", foreground, &ForegroundHook, gForegroundOrig) &&
            hook::Install(init, "scene render finish", finish, &FinishHook, gFinishOrig);
        if (!gPassHooksInstalled) {
            log::Warn("render levers: the pass hooks failed; sharePass = 3 has no effect");
        }
    }
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
        static int umbraShare = -1;
        int const umbraShareRead = readInt(L"umbraShareResults", 0) != 0 ? 1 : 0;
        if (umbraShareRead != umbraShare) {
            umbraShare = umbraShareRead;
            gUmbraShareResults.store(umbraShareRead, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] umbraShareResults = {}){}",
                umbraShareRead != 0 ? "the second eye's unready Umbra query results are answered with the first eye's "
                                      "results of the same frame (same occluders, same order)"
                                    : "each eye waits for its own Umbra query results (the game's own)",
                umbraShareRead, gUmbraReadResultHooked ? "" : "; not hooked, so no effect");
        }
        static int flushFirstEye = -1;
        int const flushRead = readInt(L"flushFirstEye", 0) != 0 ? 1 : 0;
        if (flushRead != flushFirstEye) {
            flushFirstEye = flushRead;
            gFlushFirstEye.store(flushRead, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] flushFirstEye = {})",
                flushRead != 0 ? "the GPU is flushed right after the first eye's scene render (its work starts before "
                                 "the second eye's Umbra resolve asks for its query results)"
                               : "no flush after the first eye (the game's own: the D3D11 runtime kicks the GPU when "
                                 "it likes)",
                flushRead);
        }
        static int keepBss = -1;
        int const keepBssRead = readInt(L"keepBoundShaderStates", 1) != 0 ? 1 : 0;
        if (keepBssRead != keepBss) {
            keepBss = keepBssRead;
            gKeepBoundShaderStates.store(keepBssRead, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] keepBoundShaderStates = {}){}",
                keepBssRead != 0 ? "every D3D11 bound shader state the game creates is kept alive (one extra reference), so the "
                                   "cache lookups hit and the input layouts aren't rebuilt per draw"
                                 : "bound shader states live as long as the game's own references (the game's own: dynamic "
                                   "meshes rebuild theirs, and the input layout, every draw)",
                keepBssRead, gBoundShaderStateHooked ? "" : "; not hooked, so no effect");
        }
        static int minScreenTenths = -1;
        int const minScreenRead = readInt(L"minScreenRadiusTenths", readInt(L"minScreenRadiusPx", 0) * 10);
        if (minScreenRead != minScreenTenths) {
            minScreenTenths = minScreenRead;
            gMinScreenRadiusTenths.store(minScreenRead, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] minScreenRadiusPx = {:.1f}){}",
                minScreenRead > 0 ? "Umbra's visible instances smaller than the threshold (sphere radius projected, pixels of "
                                    "a 2160-high eye) are not marked visible: no draws for them"
                                  : "every visible instance is drawn (the game's own); the sizes are only counted",
                static_cast<double>(minScreenRead) / 10.0, gUmbraInstanceVisibleHooked ? "" : "; not hooked, so no effect");
        }
        static int umbraShared = -1;
        int const umbraSharedRead = std::clamp(readInt(L"umbraSharedVisibility", 0), 0, 3);
        int const clearBits = readInt(L"umbraSecondEyeClearBits", 0);
        gUmbraSecondEyeClearBits.store(clearBits, std::memory_order_relaxed);
        if (umbraSharedRead != umbraShared) {
            umbraShared = umbraSharedRead;
            gUmbraSharedVisibility.store(umbraSharedRead, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] umbraSharedVisibility = {}, umbraSecondEyeClearBits = 0x{:X}){}",
                umbraSharedRead == 3 ? "the second eye issues no occluder queries (its pending ones get the first eye's results) and "
                                       "only the first eye's visible primitives pass its visible-instance call"
                : umbraSharedRead == 2 ? "the second eye issues no occluder queries (its pending ones are answered visible: poisons "
                                         "Umbra, attempt 46) and only the first eye's visible primitives pass its visible-instance call"
                : umbraSharedRead == 1 ? "only the first eye's visible primitives pass the second eye's visible-instance call"
                                       : "each eye resolves its own Umbra visibility (the game's own)",
                umbraSharedRead, static_cast<unsigned>(clearBits), gUmbraInstanceVisibleHooked ? "" : "; not hooked, so no effect");
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
        draw_replay::SetInversePair(readInt(L"replayInverse", 1) != 0);
        {
            int const window = readInt(L"oneDrawWindow", 1) != 0 ? 1 : 0;
            if (gOneDrawWindow.exchange(window, std::memory_order_relaxed) != window) {
                log::Info("render levers: one Draw a frame renders both eyes on {} ([Performance] oneDrawWindow = {})",
                    window != 0 ? "the symmetric window" : "each eye's own frustum (an object wholly inside the other eye's outer wedge can stay occluded)", window);
            }
        }
        int const shareRaw = readInt(L"sharePass", 0);
        int const share = shareRaw < 0 ? 0 : (shareRaw > 5 ? 5 : shareRaw);
        if (share != sharePass) {
            sharePass = share;
            gSharePass.store(share, std::memory_order_relaxed);
            gOneDrawWanted.store(share == 5 && gReplayHooked, std::memory_order_relaxed);
            gOneDrawFallback.store(false, std::memory_order_relaxed);   // a fresh start for the health check
            MELEVR::MevrBridge::g_oneDrawStereo.store(share == 5 && gReplayHooked, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] sharePass = {}){}",
                share == 2 ? "a frame's second scene render replays the first one's draw calls on its renderer (no second "
                             "visibility, no second engine pass; research/32 lever 4)"
                : share == 1 ? "a frame's second scene render runs on the first one's renderer (no second visibility)"
                : share == 5 ? "one FViewportClient::Draw a frame: the second eye is the first render's recorded calls replayed with the view pass 1 would have had (research/37)"
                : share == 3 ? "the second render keeps its Umbra resolve, its passes are the first eye's recorded calls replayed (research/37)"
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
        (gSharePass.load(std::memory_order_relaxed) == 2 || gSharePass.load(std::memory_order_relaxed) == 3 ||
         gSharePass.load(std::memory_order_relaxed) == 5 || draw_replay::DiffPending())) {
        if (draw_replay::Attach(context, gHookInit)) {  // once wanted; the hooks then stay
            gAttachFailures.store(0, std::memory_order_relaxed);
            draw_replay::SetViewLayout({CapturedOffset(0x1D0), CapturedOffset(0x210), CapturedOffset(0x250)});
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

bool OneDrawWindow() {
    return gOneDrawWindow.load(std::memory_order_relaxed) != 0;
}

bool PerEyeViewState() {
    return gPerEyeViewState.load(std::memory_order_relaxed) != 0;
}

std::string SettingsJson() {
    return std::format("{{\"umbraQueryWait\": {}, \"ue3QueryWait\": {}, \"perEyeViewState\": {}, \"perEyeOcclusion\": {}, \"umbraEyeMatrix\": {}, \"flushFirstEye\": {}, \"umbraShareResults\": {}, \"keepBoundShaderStates\": {}, \"minScreenRadiusPx\": {}, \"umbraSharedVisibility\": {}, \"pCoresOnly\": {}, \"sharePass\": {}, \"shareFailed\": {}, \"replayFailed\": {}, "
        "\"drawReplay\": {}, \"hooked\": {}}}",
        gUmbraQueryWait.load(std::memory_order_relaxed), gUe3QueryWait.load(std::memory_order_relaxed),
        gPerEyeViewState.load(std::memory_order_relaxed), gPerEyeOcclusion.load(std::memory_order_relaxed),
        gUmbraEyeMatrix.load(std::memory_order_relaxed), gFlushFirstEye.load(std::memory_order_relaxed), gUmbraShareResults.load(std::memory_order_relaxed), gKeepBoundShaderStates.load(std::memory_order_relaxed), gMinScreenRadiusTenths.load(std::memory_order_relaxed) / 10.0, gUmbraSharedVisibility.load(std::memory_order_relaxed), gPCoresOnly.load(std::memory_order_relaxed),
        gSharePass.load(std::memory_order_relaxed), gShareFailed.load(std::memory_order_relaxed) ? "true" : "false",
        gReplayFailed.load(std::memory_order_relaxed) ? "true" : "false", draw_replay::StatsJson(), gHooked ? "true" : "false");
}

} // namespace mevr::host::render_levers
