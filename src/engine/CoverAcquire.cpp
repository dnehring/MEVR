#include "engine/CoverAcquire.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <string>

namespace mevr::engine::cover_acquire {
namespace {

constexpr char const* kHookName = "ABioPawn::TryAcquireCover";

// research/23 (GhidraMCP on the LE1 project; .pdata function starts checked with tools/verify_rvas.py):
//   0xAB0FC0  BioPawn's cover tick (from its native tick, 0xAA0C20): every CoverPlayerInterval while the
//             player isn't in cover it calls
//   0xAB1430  the acquisition test (this, only RCX): the cover trace; then the move's angle off the cover
//             normal against CoverTestAngle and the camera's against CoverTestCameraAngle; on success
//             CoverType = the trace's type, bInCover, the cover state, ProcessEvent(PlayerAcquiredCover)
//   0xAA92F0  the cover trace (this, FVector* outNormal, BYTE* outType, BOOL keepNormal): line checks
//             CoverTestDistance along the move input (none without move input), left and right of it,
//             then up the wall: type 1 standing (high) cover, 2 mid-level (low). Writes only its outputs.
#if defined(SDK_TARGET_LE1)
constexpr std::size_t kTryAcquireCoverRva = 0xAB1430;
constexpr std::size_t kCoverTraceRva = 0xAA92F0;
static_assert(offsetof(AActor, Acceleration) == 0x144);
static_assert(offsetof(ABioPawn, CoverTestAngle) == 0x990);
#else
constexpr std::size_t kTryAcquireCoverRva = 0;
constexpr std::size_t kCoverTraceRva = 0;
#endif

using tTryAcquireCover = void(void* pawn);
using tCoverTrace = int(void* pawn, float* outNormal, unsigned char* outType, int keepNormal);

tTryAcquireCover* gOrig = nullptr;  // written before the hook goes live (core/Hook)
tCoverTrace* gTrace = nullptr;
bool gInstalled = false;
std::atomic<int> gInFlight{0};
std::atomic<void const*> gRefused{nullptr};

std::atomic<unsigned> gRefusedCount{0};
std::atomic<unsigned> gInReach{0};
std::atomic<unsigned> gLastType{0};
std::atomic<float> gLastAngle{-1.0f};

bool IsFunctionStart(void* address) {
    DWORD64 imageBase = 0;
    auto const* const entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &imageBase, nullptr);
    return entry != nullptr && imageBase + entry->BeginAddress == reinterpret_cast<DWORD64>(address);
}

std::string EntryBytes(void const* address) {
    auto const* const bytes = static_cast<unsigned char const*>(address);
    std::string text;
    for (int i = 0; i < 12; ++i) {
        text += std::format("{:02x} ", bytes[i]);
    }
    return text;
}

// What the refused test would have seen: the game's own trace, then the move's angle off the cover's
// normal as the test measures it (the camera's angle is left out). Never throws into the engine.
void Look(void* pawn) {
#if defined(SDK_TARGET_LE1)
    if (gTrace == nullptr) {
        return;
    }
    float normal[3] = {0.0f, 0.0f, 0.0f};
    unsigned char type = 0;
    int found = 0;
    __try {
        found = gTrace(pawn, normal, &type, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gTrace = nullptr;  // never again this load
        found = 0;
    }
    if (found == 0 || type == 0) {
        gLastType.store(0, std::memory_order_relaxed);
        gLastAngle.store(-1.0f, std::memory_order_relaxed);
        return;
    }
    auto const* const bio = static_cast<ABioPawn const*>(pawn);
    FVector const a = bio->Acceleration;
    double const length = std::hypot(static_cast<double>(a.X), static_cast<double>(a.Y));
    double angle = 180.0;
    if (length > 1e-6) {
        double const dot = -(a.X * normal[0] + a.Y * normal[1]) / length;
        angle = std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / std::numbers::pi;
    }
    gLastAngle.store(static_cast<float>(angle), std::memory_order_relaxed);
    bool const pushedInto = angle <= bio->CoverTestAngle;
    gLastType.store(pushedInto ? type : 0u, std::memory_order_relaxed);
    if (pushedInto) {
        gInReach.fetch_add(1, std::memory_order_relaxed);
    }
#else
    (void)pawn;
#endif
}

// No C++ objects with destructors here, so __finally keeps the in-flight count right even when an
// exception unwinds through the engine's call.
void TryAcquireCoverHook(void* pawn) {
    gInFlight.fetch_add(1, std::memory_order_acquire);
    __try {
        if (pawn != nullptr && pawn == gRefused.load(std::memory_order_relaxed)) {
            gRefusedCount.fetch_add(1, std::memory_order_relaxed);
            Look(pawn);
        } else {
            gOrig(pawn);
        }
    } __finally {
        gInFlight.fetch_sub(1, std::memory_order_release);
    }
}

} // namespace

bool Install(::LESDK::Initializer& init) {
    if (gInstalled) {
        return true;
    }
    if (kTryAcquireCoverRva == 0) {
        log::Warn("cover: no address for this game's cover test: cover can't be refused (only left once taken)");
        return false;
    }
    void* const target = init.Resolve(::LESDK::Address::FromOffset(kTryAcquireCoverRva));
    if (target == nullptr || !IsFunctionStart(target)) {
        log::Error("cover: the cover test at {} is not a function start; not hooking", target);
        return false;
    }
    void* const trace = init.Resolve(::LESDK::Address::FromOffset(kCoverTraceRva));
    gTrace = trace != nullptr && IsFunctionStart(trace) ? reinterpret_cast<tCoverTrace*>(trace) : nullptr;
    log::Info("cover: the cover test at {} (entry bytes {}); its trace at {}{}", target, EntryBytes(target), trace,
        gTrace != nullptr ? "" : " (not a function start: refused entries won't be described)");
    if (!hook::Install(init, kHookName, reinterpret_cast<tTryAcquireCover*>(target), &TryAcquireCoverHook, gOrig)) {
        log::Error("cover: the cover test hook failed");
        gTrace = nullptr;
        return false;
    }
    gInstalled = true;
    log::Info("hooked {}", kHookName);
    return true;
}

bool Uninstall(::LESDK::Initializer& init, bool waitForCalls) {
    gRefused.store(nullptr, std::memory_order_relaxed);
    if (!gInstalled) {
        return true;
    }
    if (!hook::Uninstall(init, kHookName)) {
        return false;  // still live, and still calling gOrig
    }
    gInstalled = false;
    // A call already inside the detour finishes with the old code: this module must outlive it.
    for (int i = 0; waitForCalls && i < 200 && gInFlight.load() > 0; ++i) {
        Sleep(1);
    }
    bool const drained = gInFlight.load() == 0;
    if (!drained && waitForCalls) {
        log::Error("cover: {} cover test call(s) still in the hook 200 ms after unhooking", gInFlight.load());
    }
    gTrace = nullptr;
    log::Info("unhooked {}", kHookName);
    return drained;
}

bool Installed() {
    return gInstalled;
}

void Refuse(void const* pawn) {
    gRefused.store(pawn, std::memory_order_relaxed);
}

Stats Take() {
    Stats stats;
    stats.refused = gRefusedCount.exchange(0, std::memory_order_relaxed);
    stats.inReach = gInReach.exchange(0, std::memory_order_relaxed);
    stats.lastType = static_cast<std::uint8_t>(gLastType.load(std::memory_order_relaxed));
    stats.lastAngle = gLastAngle.load(std::memory_order_relaxed);
    return stats;
}

} // namespace mevr::engine::cover_acquire
