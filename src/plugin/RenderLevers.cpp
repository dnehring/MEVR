// SPDX-License-Identifier: MIT
#include "plugin/RenderLevers.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "plugin/DrawReplay.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
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
constexpr ByteRange kViewPlainRanges[] = {{0x90, 0x140}, {0x190, 0x330}, {0x480, 0x4B0}, {0x4C0, 0x4D0}};

using tSceneRender = void(void* renderer);
using tDeleteRenderer = void*(void* renderer, std::uint64_t flags);
using tDpgLightTargets = void(void* renderer, int dpg);
using tCommand = void(void* commander, unsigned code);

tSceneRender* gSceneRenderOrig = nullptr;      // written before the hook goes live (core/Hook)
tDeleteRenderer* gDeleteRendererOrig = nullptr;
tDpgLightTargets* gDpgLightTargetsOrig = nullptr;
tCommand* gCommandOrig = nullptr;
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

// A renderer SceneRenderB takes down the Umbra branch on (no hit proxies, no wireframe), with views.
bool Eligible(std::uint8_t* renderer) {
    int const views = Field<int>(renderer, 0x68);
    return Field<int>(renderer, 0x5C) != 0 && views > 0 && views <= 4 && !ShowFlag(renderer, 0x33) &&
        !ShowFlag(renderer, 8);
}

// The same scene, target, flags and views (count, view state, rectangles): only the matrices differ.
bool Compatible(std::uint8_t* first, std::uint8_t* second) {
    if (first == second || !Eligible(second) || Field<void*>(first, 0x00) != Field<void*>(second, 0x00) ||
        Field<void*>(first, 0x18) != Field<void*>(second, 0x18) ||
        std::memcmp(first + 0x28, second + 0x28, 12) != 0 || Field<int>(first, 0x68) != Field<int>(second, 0x68)) {
        return false;
    }
    for (int i = 0; i < Field<int>(first, 0x68); ++i) {
        std::uint8_t* const a = View(first, i);
        std::uint8_t* const b = View(second, i);
        if (Field<void*>(a, 0x08) != Field<void*>(b, 0x08) || std::memcmp(a + 0x60, b + 0x60, 0x30) != 0) {
            return false;
        }
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

// The second eye on the kept renderer: the engine's passes (mode 1), the draw replay (mode 2), or, for the eye
// diff, the engine's passes recorded for the comparison. 0 done.
int SecondEye(std::uint8_t* first, int mode, bool diff, draw_replay::ViewFields const& before,
    draw_replay::ViewFields const& after) {
    if (diff) {
        draw_replay::BeginRecording(1);
        int const rc = ReplaySEH(first);
        draw_replay::EndRecording();
        std::string const tag = draw_replay::TakeDiffTag();
        if (rc == 0) {
            draw_replay::WriteDiff(before, after, tag);
        }
        return rc;
    }
    std::string why;
    if (mode == 2 && draw_replay::Attached() && !gReplayFailed.load(std::memory_order_relaxed) &&
        draw_replay::TailReplayable(&why)) {
        int const rc = ReplayDrawsSEH(first, before, after);
        if (rc == 0) {
            gReplayed.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        gReplayFailed.store(true, std::memory_order_relaxed);
        log::Warn("render levers: the draw replay failed ({}); the engine's passes render the second eye for the rest of "
            "the session", rc);
    }
    if (mode == 2) {
        gReplayFallbacks.fetch_add(1, std::memory_order_relaxed);
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
            draw_replay::ViewFields before, after;
            if (recorder) {
                before = CaptureViews(first);
            }
            CopyViewMatrices(first, renderer);
            if (recorder) {
                after = CaptureViews(first);
            }
            if (SecondEye(first, mode, diff, before, after) != 0) {
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
    if (renderer != nullptr && renderer == gKeepCandidate.load(std::memory_order_relaxed) && (flags & 1) != 0 &&
        GetCurrentThreadId() == gRenderThread.load(std::memory_order_relaxed)) {
        gKept = renderer;
        return renderer;
    }
    return gDeleteRendererOrig(renderer, flags);
}

void SceneRenderHook(void* renderer) {
    int const index = gRenderIndex.fetch_add(1, std::memory_order_relaxed);
    int const outer = t_renderIndex;
    t_renderIndex = index;
    gRenderThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
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

bool Install(::LESDK::Initializer& init) {
    auto* const sceneRender = init.ResolveTyped<tSceneRender>(::LESDK::Address::FromOffset(kSceneRenderRva));
    auto* const command = init.ResolveTyped<tCommand>(::LESDK::Address::FromOffset(kCommanderCommandRva));
    if (sceneRender == nullptr || command == nullptr) {
        log::Warn("render levers: addresses not resolved; levers off");
        return false;
    }
    gExe = init.ResolveTyped<std::uint8_t>(::LESDK::Address::FromOffset(0));
    auto* const deleteRenderer = init.ResolveTyped<tDeleteRenderer>(::LESDK::Address::FromOffset(kDeleteRendererRva));
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
    auto* const lightTargets = init.ResolveTyped<tDpgLightTargets>(::LESDK::Address::FromOffset(kDpgLightTargetsRva));
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
        int const queryWait = path.empty() ? 1 : (GetPrivateProfileIntW(L"Performance", L"umbraQueryWait", 1, path.c_str()) != 0 ? 1 : 0);
        int const pCores = path.empty() ? 0 : (GetPrivateProfileIntW(L"Performance", L"pCoresOnly", 0, path.c_str()) != 0 ? 1 : 0);
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
        int const shareRaw = path.empty() ? 0 : static_cast<int>(GetPrivateProfileIntW(L"Performance", L"sharePass", 0, path.c_str()));
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
        draw_replay::Attach(context);  // once wanted; the hooks then stay
    }
    draw_replay::OnPresentEnd();
}

std::string SettingsJson() {
    return std::format("{{\"umbraQueryWait\": {}, \"pCoresOnly\": {}, \"sharePass\": {}, \"shareFailed\": {}, \"replayFailed\": {}, "
        "\"drawReplay\": {}, \"hooked\": {}}}",
        gUmbraQueryWait.load(std::memory_order_relaxed), gPCoresOnly.load(std::memory_order_relaxed),
        gSharePass.load(std::memory_order_relaxed), gShareFailed.load(std::memory_order_relaxed) ? "true" : "false",
        gReplayFailed.load(std::memory_order_relaxed) ? "true" : "false", draw_replay::StatsJson(), gHooked ? "true" : "false");
}

} // namespace mevr::host::render_levers
