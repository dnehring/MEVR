// SPDX-License-Identifier: MIT
#include "engine/SelectionGaze.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "engine/Addresses.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace mevr::engine::selection_gaze {
namespace {

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
constexpr char const* kHookName = "UBioPlayerSelection::FindCurrentSelectionTarget";
using tUpdateSelection = void(void* selection);
#else
constexpr char const* kHookName = "UBioPlayerSelection::UpdateSelection";
using tUpdateSelection = int(void* selection, float deltaSeconds);
#endif

// research/40 (Ghidra on the LE1 project; the .pdata function start checked with tools/rva-patterns.py):
//   0xB7E850  execUpdateSelection, the script's native thunk: reads DeltaTime, calls
//   0xC79D70  the body (this, float DeltaTime) -> BOOL; also called natively from 0xC78C60. It reads the player
//             camera's CameraCache.POV (Camera +0x45C Location, +0x468 Rotation, +0x474 FOV) as the cone's origin and
//             axis, keeps candidates within half the FOV and the distance-scaled selection angle, and takes the one
//             with the smallest angle that the POV's location can see (IsInLineOfSightFrom).
constexpr std::uint32_t kUpdateSelectionRva = addresses::kUpdateSelection;

// LE2 and LE3 search in a separate void(this) body (LE2 0xAC0530, LE3 0xA9BF90). Its provider supplies the controller's
// rotation, not CameraCache.POV.Rotation. Both paths share the swap/restore lifetime.

tUpdateSelection* gOrig = nullptr;  // written before the hook goes live (core/Hook)
bool gInstalled = false;
std::atomic<int> gInFlight{0};
std::atomic<Provider> gProvider{nullptr};
std::atomic<unsigned> gCalls{0};
std::atomic<unsigned> gSwapped{0};

bool IsFunctionStart(void* address) {
    DWORD64 imageBase = 0;
    auto const* const entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &imageBase, nullptr);
    return entry != nullptr && imageBase + entry->BeginAddress == reinterpret_cast<DWORD64>(address);
}

// The provider's answer, guarded: a fault there means no swap, never a fault in the engine's tick.
bool Ask(Provider provider, void const* selection, Swap& swap) {
    __try {
        return provider(selection, swap) && swap.povLocation != nullptr && swap.povRotation != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gProvider.store(nullptr, std::memory_order_relaxed);  // never again this load
        return false;
    }
}

// No C++ objects with destructors here, so __finally keeps the in-flight count right and the camera put back even
// when an exception unwinds through the engine's call.
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
void UpdateSelectionHook(void* selection) {
#else
int UpdateSelectionHook(void* selection, float deltaSeconds) {
#endif
    gInFlight.fetch_add(1, std::memory_order_acquire);
    gCalls.fetch_add(1, std::memory_order_relaxed);
    Swap swap;
    Provider const provider = gProvider.load(std::memory_order_relaxed);
    bool const swapping = provider != nullptr && selection != nullptr && Ask(provider, selection, swap);
    ue3::Vector savedLocation;
    ue3::Rotator savedRotation;
    if (swapping) {
        savedLocation = *swap.povLocation;
        savedRotation = *swap.povRotation;
        *swap.povLocation = swap.location;
        *swap.povRotation = swap.rotation;
        write_journal::Wrote("SelectionGaze.povLocation", swap.povLocation);
        write_journal::Wrote("SelectionGaze.povRotation", swap.povRotation);
        gSwapped.fetch_add(1, std::memory_order_relaxed);
    }
#if !defined(MEVR_GAME_LE2) && !defined(MEVR_GAME_LE3)
    int result = 0;
#endif
    __try {
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
        gOrig(selection);
#else
        result = gOrig(selection, deltaSeconds);
#endif
    } __finally {
        if (swapping) {
            *swap.povLocation = savedLocation;
            *swap.povRotation = savedRotation;
            write_journal::Wrote("SelectionGaze.restore", swap.povRotation);
        }
        gInFlight.fetch_sub(1, std::memory_order_release);
    }
#if !defined(MEVR_GAME_LE2) && !defined(MEVR_GAME_LE3)
    return result;
#endif
}

} // namespace

bool Install(hook::Context& init) {
    if (gInstalled) {
        return true;
    }
    if constexpr (kUpdateSelectionRva == 0) {
        log::Warn("interact: no address for this game's selection update: the game chooses what A uses from its camera");
        return false;
    }
    void* const target = init.Resolve(kUpdateSelectionRva);
    if (target == nullptr || !IsFunctionStart(target)) {
        log::Error("interact: the selection update at {} is not a function start; not hooking", target);
        return false;
    }
    if (!hook::Install(init, kHookName, reinterpret_cast<tUpdateSelection*>(target), &UpdateSelectionHook, gOrig)) {
        log::Error("interact: the selection update hook failed: the game chooses what A uses from its camera");
        return false;
    }
    gInstalled = true;
    log::Info("hooked {}", kHookName);
    return true;
}

bool Uninstall(hook::Context& init, bool waitForCalls) {
    gProvider.store(nullptr, std::memory_order_relaxed);
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
        log::Error("interact: {} selection update call(s) still in the hook 200 ms after unhooking", gInFlight.load());
    }
    log::Info("unhooked {}", kHookName);
    return drained;
}

bool Installed() {
    return gInstalled;
}

void SetProvider(Provider provider) {
    gProvider.store(provider, std::memory_order_relaxed);
}

Stats Take() {
    Stats stats;
    stats.calls = gCalls.exchange(0, std::memory_order_relaxed);
    stats.swapped = gSwapped.exchange(0, std::memory_order_relaxed);
    return stats;
}

} // namespace mevr::engine::selection_gaze
