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

using tSceneRender = void(void* renderer);
using tRenderPrePass = std::uint32_t(void* renderer, int dpg, void* view, int a4, int a5);

tSceneRender* gSceneRenderOrig = nullptr;      // written before the hook goes live (core/Hook)
tRenderPrePass* gRenderPrePassOrig = nullptr;
void const* gVisibleSetPrePassReturn = nullptr;
bool gHooked = false;

std::atomic<int> gDepthPrepass{2};
std::atomic<int> gPCoresOnly{0};

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

void SceneRenderHook(void* renderer) {
    int const index = gRenderIndex.fetch_add(1, std::memory_order_relaxed);
    int const outer = t_renderIndex;
    t_renderIndex = index;
    LARGE_INTEGER begin{}, end{};
    QueryPerformanceCounter(&begin);
    gSceneRenderOrig(renderer);
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
    log::Info("render levers: last 10 s: {} frames; scene renders per frame {}; per render, by its place in the "
        "frame: {}", frames, perFrame.empty() ? "-" : perFrame, perRender.empty() ? "-" : perRender);
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
    if (!hook::Install(init, "FSceneRenderer::Render", sceneRender, &SceneRenderHook, gSceneRenderOrig)) {
        log::Warn("render levers: scene render hook failed; levers off");
        return false;
    }
    if (!hook::Install(init, "FSceneRenderer::RenderPrePass", prePass, &RenderPrePassHook, gRenderPrePassOrig)) {
        log::Warn("render levers: pre-pass hook failed; depthPrepass has no effect");
        return false;
    }
    gHooked = true;
    log::Info("render levers: hooked the scene render and the depth pre-pass");
    return true;
}

void Tick() {
    static std::uint64_t nextReadMs = 0;
    static std::uint64_t nextLogMs = 0;
    static int depthPrepass = -1;
    static int pCoresOnly = -1;
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
}

std::string SettingsJson() {
    return std::format("{{\"depthPrepass\": {}, \"pCoresOnly\": {}, \"hooked\": {}}}",
        gDepthPrepass.load(std::memory_order_relaxed), gPCoresOnly.load(std::memory_order_relaxed),
        gHooked ? "true" : "false");
}

} // namespace mevr::host::render_levers
