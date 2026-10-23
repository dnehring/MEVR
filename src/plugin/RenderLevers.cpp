#include "plugin/RenderLevers.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"

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

// research/22 (Ghidra; .pdata starts checked): SceneRenderB = FSceneRenderer::Render (one call per scene
// render, render thread); RenderPrePass = the depth-only pass of one DPG; 0x8BF11B is the return address of
// its unconditional call in RenderUmbraDepthPrepassView (0x8BEDB0), the pre-pass of the whole visible set.
constexpr std::uintptr_t kSceneRenderRva = 0x76EE20;
constexpr std::uintptr_t kRenderPrePassRva = 0x76C1D0;
constexpr std::uintptr_t kVisibleSetPrePassReturnRva = 0x8BF11B;

constexpr int kSlots = 6;  // render places tracked per frame (the last collects the rest)

// sharePass (research/32, lever 1): the second scene render of a frame runs on the first one's renderer.
// SceneRenderB's Umbra branch, after InitViews, as the game calls it (Ghidra, research/32):
constexpr std::uintptr_t kDeleteRendererRva = 0x4ED700;   // FSceneRenderer deleting destructor (this, flags)
constexpr std::uintptr_t kAllocateTargetsRva = 0x772EB0;  // GSceneRenderTargets.Allocate(this, x, y, 0)
constexpr std::uintptr_t kWireframeStateRva = 0x8C1AE0;   // (byte): 0 on the normal path
constexpr std::uintptr_t kPrepassViewsRva = 0x8BF8F0;     // RenderUmbraDepthPrepass(renderer)
constexpr std::uintptr_t kTranslucencyCheckRva = 0x7666A0;
constexpr std::uintptr_t kBeforeLightsRva = 0x759240;     // only when GSkipBeforeLights == 0
constexpr std::uintptr_t kDpgPassRva = 0x76D650;          // (renderer, dpg, uint* dirty)
constexpr std::uintptr_t kDpgLightsRva = 0x76D0A0;        // (renderer, dpg, flag, uint* dirty, arg5)
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
using tRenderPrePass = std::uint32_t(void* renderer, int dpg, void* view, int a4, int a5);
using tDeleteRenderer = void*(void* renderer, std::uint64_t flags);

tSceneRender* gSceneRenderOrig = nullptr;      // written before the hook goes live (core/Hook)
tRenderPrePass* gRenderPrePassOrig = nullptr;
tDeleteRenderer* gDeleteRendererOrig = nullptr;
void const* gVisibleSetPrePassReturn = nullptr;
std::uint8_t* gExe = nullptr;
bool gHooked = false;
bool gShareHooked = false;

std::atomic<int> gDepthPrepass{2};
std::atomic<int> gPCoresOnly{0};
std::atomic<int> gSharePass{0};
std::atomic<bool> gShareFailed{false};  // an exception in a replay: off for the session

// Render thread (the deleter can also run on the game thread, hence the atomics it reads).
std::atomic<void*> gKeepCandidate{nullptr};  // the renderer whose deletion to hold back, while it renders
std::atomic<DWORD> gRenderThread{0};
void* gKept = nullptr;  // a finished renderer kept for the next compatible render
std::atomic<std::uint64_t> gShared{0}, gShareMismatches{0}, gShareUnused{0}, gShareExceptions{0};

// Render thread.
std::atomic<int> gRenderIndex{0};  // scene renders so far this frame
thread_local int t_renderIndex = -1;

struct Counters {
    std::atomic<std::uint64_t> frames{0};
    std::array<std::atomic<std::uint64_t>, kSlots + 1> rendersPerFrame{};  // [n] frames with n renders (last: more)
    std::array<std::atomic<std::uint64_t>, kSlots> renderTicks{};
    std::array<std::atomic<std::uint64_t>, kSlots> renders{};
    std::array<std::atomic<std::uint64_t>, kSlots> prepassCalls{};
    std::array<std::atomic<std::uint64_t>, kSlots> prepassSkips{};
};
Counters gCounters;

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
        // The visible set's depth pre-pass from these matrices. The commander's resolve armed one flag per
        // view and the first pre-pass cleared it: arm it again.
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

void ReleaseKept() {
    if (gKept != nullptr) {
        gDeleteRendererOrig(gKept, 1);
        gKept = nullptr;
        gShareUnused.fetch_add(1, std::memory_order_relaxed);
    }
}

// sharePass: true if this render was handled (rendered while kept, or replayed on the kept one).
bool ShareOrKeep(std::uint8_t* renderer) {
    if (gKept != nullptr) {
        auto* const first = static_cast<std::uint8_t*>(gKept);
        gKept = nullptr;
        if (Compatible(first, renderer)) {
            CopyViewMatrices(first, renderer);
            if (ReplaySEH(first) != 0) {
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
    gSceneRenderOrig(renderer);
    gKeepCandidate.store(nullptr, std::memory_order_relaxed);
    return true;
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
    if (gShareHooked && gSharePass.load(std::memory_order_relaxed) != 0 && !gShareFailed.load(std::memory_order_relaxed)) {
        handled = ShareOrKeep(static_cast<std::uint8_t*>(renderer));
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

std::uint32_t RenderPrePassHook(void* renderer, int dpg, void* view, int a4, int a5) {
    if (_ReturnAddress() == gVisibleSetPrePassReturn) {
        int const slot = Slot(t_renderIndex);
        Add(gCounters.prepassCalls[slot], 1);
        int const keep = gDepthPrepass.load(std::memory_order_relaxed);
        if (keep == 0 || (keep == 1 && t_renderIndex >= 1)) {
            Add(gCounters.prepassSkips[slot], 1);
            return 0;  // nothing drawn: the caller ORs this into its "depth written" flag
        }
    }
    return gRenderPrePassOrig(renderer, dpg, view, a4, a5);
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

char const* PrepassText(int value) {
    switch (value) {
    case 0: return "no scene render pre-draws its visible set (no early-Z)";
    case 1: return "only the first scene render of each frame pre-draws its visible set";
    default: return "every scene render pre-draws its visible set (the game's own)";
    }
}

// The last logged totals, for the 10 s deltas.
struct Snapshot {
    std::uint64_t frames = 0;
    std::array<std::uint64_t, kSlots + 1> rendersPerFrame{};
    std::array<std::uint64_t, kSlots> renderTicks{}, renders{}, prepassCalls{}, prepassSkips{};
    std::uint64_t shared = 0, mismatches = 0, unused = 0, exceptions = 0;
};

Snapshot Take() {
    Snapshot s;
    s.frames = Get(gCounters.frames);
    for (int i = 0; i <= kSlots; ++i) s.rendersPerFrame[i] = Get(gCounters.rendersPerFrame[i]);
    for (int i = 0; i < kSlots; ++i) {
        s.renderTicks[i] = Get(gCounters.renderTicks[i]);
        s.renders[i] = Get(gCounters.renders[i]);
        s.prepassCalls[i] = Get(gCounters.prepassCalls[i]);
        s.prepassSkips[i] = Get(gCounters.prepassSkips[i]);
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
        double const ms = 1000.0 * static_cast<double>(now.renderTicks[i] - before.renderTicks[i]) /
            static_cast<double>(frequency.QuadPart) / static_cast<double>(n);
        perRender += std::format("{}#{}{} {:.2f} ms (pre-pass {} calls, {} skipped)", perRender.empty() ? "" : "; ", i,
            i == kSlots - 1 ? "+" : "", ms, now.prepassCalls[i] - before.prepassCalls[i],
            now.prepassSkips[i] - before.prepassSkips[i]);
    }
    std::string share;
    if (gSharePass.load(std::memory_order_relaxed) != 0 || now.shared != before.shared || now.exceptions != 0) {
        share = std::format("; sharePass: {} renders on the previous one's renderer, {} didn't match, {} kept "
            "renderers unused{}", now.shared - before.shared, now.mismatches - before.mismatches,
            now.unused - before.unused, now.exceptions != 0 ? "; OFF after an exception" : "");
    }
    log::Info("render levers: last 10 s: {} frames; scene renders per frame {}; per render, by its place in the "
        "frame: {}{}", frames, perFrame.empty() ? "-" : perFrame, perRender.empty() ? "-" : perRender, share);
}

} // namespace

bool Install(::LESDK::Initializer& init) {
    auto* const sceneRender = init.ResolveTyped<tSceneRender>(::LESDK::Address::FromOffset(kSceneRenderRva));
    auto* const prePass = init.ResolveTyped<tRenderPrePass>(::LESDK::Address::FromOffset(kRenderPrePassRva));
    auto const* const prePassReturn =
        init.ResolveTyped<std::uint8_t>(::LESDK::Address::FromOffset(kVisibleSetPrePassReturnRva));
    if (sceneRender == nullptr || prePass == nullptr || prePassReturn == nullptr) {
        log::Warn("render levers: addresses not resolved; levers off");
        return false;
    }
    gVisibleSetPrePassReturn = prePassReturn;
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
    if (!hook::Install(init, "FSceneRenderer::RenderPrePass", prePass, &RenderPrePassHook, gRenderPrePassOrig)) {
        log::Warn("render levers: pre-pass hook failed; depthPrepass has no effect");
        return false;
    }
    gHooked = true;
    log::Info("render levers: hooked the scene render, its deleter and the depth pre-pass");
    return true;
}

void Tick() {
    static std::uint64_t nextReadMs = 0;
    static std::uint64_t nextLogMs = 0;
    static int depthPrepass = -1;
    static int pCoresOnly = -1;
    static int sharePass = -1;
    static Snapshot last;
    std::uint64_t const now = GetTickCount64();
    if (now >= nextReadMs) {
        nextReadMs = now + 1000;
        std::wstring const& path = IniPath();
        int const prepass = path.empty() ? 2 : static_cast<int>(GetPrivateProfileIntW(L"Performance", L"depthPrepass", 2, path.c_str()));
        int const pCores = path.empty() ? 0 : (GetPrivateProfileIntW(L"Performance", L"pCoresOnly", 0, path.c_str()) != 0 ? 1 : 0);
        int const clamped = prepass < 0 ? 0 : (prepass > 2 ? 2 : prepass);
        if (clamped != depthPrepass) {
            depthPrepass = clamped;
            gDepthPrepass.store(clamped, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] depthPrepass = {}){}", PrepassText(clamped), clamped,
                gHooked ? "" : "; not hooked, so no effect");
        }
        if (pCores != pCoresOnly) {
            pCoresOnly = pCores;
            gPCoresOnly.store(pCores, std::memory_order_relaxed);
            ApplyAffinity(pCores != 0);
        }
        int const share = path.empty() ? 0 : (GetPrivateProfileIntW(L"Performance", L"sharePass", 0, path.c_str()) != 0 ? 1 : 0);
        if (share != sharePass) {
            sharePass = share;
            gSharePass.store(share, std::memory_order_relaxed);
            log::Info("render levers: {} ([Performance] sharePass = {}){}",
                share != 0 ? "a frame's second scene render runs on the first one's renderer (no second visibility)"
                           : "every scene render does its own visibility (the game's own)",
                share, gShareHooked ? "" : "; not hooked, so no effect");
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

void OnPresentEnd() {
    int const renders = gRenderIndex.exchange(0, std::memory_order_relaxed);
    Add(gCounters.frames, 1);
    Add(gCounters.rendersPerFrame[renders < kSlots ? renders : kSlots], 1);
    ReleaseKept();  // a kept renderer never pairs across frames
}

std::string SettingsJson() {
    return std::format("{{\"depthPrepass\": {}, \"pCoresOnly\": {}, \"sharePass\": {}, \"shareFailed\": {}, \"hooked\": {}}}",
        gDepthPrepass.load(std::memory_order_relaxed), gPCoresOnly.load(std::memory_order_relaxed),
        gSharePass.load(std::memory_order_relaxed), gShareFailed.load(std::memory_order_relaxed) ? "true" : "false",
        gHooked ? "true" : "false");
}

} // namespace mevr::host::render_levers
