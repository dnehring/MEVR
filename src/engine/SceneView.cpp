// SPDX-License-Identifier: MIT
#include "engine/SceneView.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"

#include <LESDK/Init.hpp>

#include <Windows.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace mevr::engine::scene_view {
namespace {

constexpr char const* kHookName = "ULocalPlayer::CalcSceneView";

struct Addresses {
    std::size_t calcSceneView;  // RVA of ULocalPlayer::CalcSceneView
    std::size_t primaryReturn;  // RVA its gameplay call in FViewportClient::Draw returns to (0: unknown)
    std::size_t drawStart;      // FViewportClient::Draw's range: with no known return address,
    std::size_t drawEnd;        // any call from inside it counts as the player view
};

// All verified as function starts (and the LE1 return address as inside Draw) in each exe's
// .pdata (tools/verify_rvas.py). Source of the facts: Halcyon's MassEffect1/2/3-VR (research/13).
#if defined(MEVR_GAME_LE1)
constexpr Addresses kAddresses{0x4C5300, 0x4C7A4E, 0x4C71A0, 0x4C9434};
#elif defined(MEVR_GAME_LE2)
constexpr Addresses kAddresses{0x6CEB70, 0, 0x6D0A00, 0x6D2C99};
#elif defined(MEVR_GAME_LE3)
constexpr Addresses kAddresses{0x6CFFD0, 0, 0x6D1E30, 0x6D40DB};
#else
constexpr Addresses kAddresses{0, 0, 0, 0};
#endif

using tCalcSceneView = void*(void* player, void* viewFamily, void* viewLocation, void* viewRotation,
    void* viewport, void* viewDrawer);
tCalcSceneView* gOrig = nullptr;
BeforePrimaryView gBefore = nullptr;
std::uintptr_t gModuleBase = 0;
std::uintptr_t gPrimaryReturn = 0;
std::uintptr_t gDrawStart = 0;
std::uintptr_t gDrawEnd = 0;
// Distinct player-view callers and their counts (LE2/LE3: pins the call site on the first run;
// two sites in one Draw would show up here). Game thread.
struct CallerCount {
    std::uintptr_t caller = 0;
    unsigned count = 0;
};
CallerCount gPrimaryCallers[4];
bool gInstalled = false;

thread_local bool tInPrimary = false;

std::atomic<unsigned> gPrimaryCalls{0};
std::atomic<unsigned> gOtherCalls{0};
std::atomic<std::uintptr_t> gLastOtherCaller{0};

// Clears the flag even if the engine call unwinds.
struct PrimaryScope {
    PrimaryScope() { tInPrimary = true; }
    ~PrimaryScope() { tInPrimary = false; }
    PrimaryScope(PrimaryScope const&) = delete;
    PrimaryScope& operator=(PrimaryScope const&) = delete;
};

void* CalcSceneViewHook(void* player, void* viewFamily, void* viewLocation, void* viewRotation,
    void* viewport, void* viewDrawer) {
    auto const caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    bool const primary = gPrimaryReturn != 0 ? caller == gPrimaryReturn : caller >= gDrawStart && caller < gDrawEnd;
    if (!primary || tInPrimary) {
        gOtherCalls.fetch_add(1, std::memory_order_relaxed);
        gLastOtherCaller.store(caller, std::memory_order_relaxed);
        return gOrig(player, viewFamily, viewLocation, viewRotation, viewport, viewDrawer);
    }
    gPrimaryCalls.fetch_add(1, std::memory_order_relaxed);
    for (CallerCount& slot : gPrimaryCallers) {
        if (slot.caller == caller || slot.caller == 0) {
            slot.caller = caller;
            ++slot.count;
            break;
        }
    }
    if (gBefore != nullptr) {
        gBefore();
    }
    PrimaryScope const scope;
    return gOrig(player, viewFamily, viewLocation, viewRotation, viewport, viewDrawer);
}

// The exe's code is encrypted on disk, so the only static check is the unwind table: the
// address must be where a function begins.
bool IsFunctionStart(void* address) {
    DWORD64 imageBase = 0;
    auto const* const entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &imageBase, nullptr);
    return entry != nullptr && imageBase + entry->BeginAddress == reinterpret_cast<DWORD64>(address);
}

// The decrypted entry bytes, logged once so a byte signature can be pinned later.
std::string EntryBytes(void const* address) {
    auto const* const bytes = static_cast<unsigned char const*>(address);
    std::string text;
    for (int i = 0; i < 16; ++i) {
        text += std::format("{:02x} ", bytes[i]);
    }
    return text;
}

} // namespace

bool Install(::LESDK::Initializer& init, BeforePrimaryView before) {
    if (kAddresses.calcSceneView == 0) {
        log::Warn("CalcSceneView: no address for this game; render-only head tracking unavailable");
        return false;
    }
    void* const target = init.Resolve(::LESDK::Address::FromOffset(kAddresses.calcSceneView));
    if (target == nullptr || !IsFunctionStart(target)) {
        log::Error("CalcSceneView: {} is not a function start; not hooking", target);
        return false;
    }
    log::Info("CalcSceneView at {} (entry bytes {})", target, EntryBytes(target));
    gModuleBase = reinterpret_cast<std::uintptr_t>(init.ModuleBase);
    gPrimaryReturn = kAddresses.primaryReturn != 0 ? gModuleBase + kAddresses.primaryReturn : 0;
    gDrawStart = gModuleBase + kAddresses.drawStart;
    gDrawEnd = gModuleBase + kAddresses.drawEnd;
    if (gPrimaryReturn == 0) {
        log::Info("CalcSceneView: player view = any call from FViewportClient::Draw (call site not pinned yet)");
    }
    gBefore = before;
    if (!hook::Install(init, kHookName, reinterpret_cast<tCalcSceneView*>(target), &CalcSceneViewHook, gOrig)) {
        log::Error("CalcSceneView: hook failed");
        gBefore = nullptr;
        return false;
    }
    gInstalled = true;
    log::Info("hooked {}", kHookName);
    return true;
}

bool Uninstall(::LESDK::Initializer& init) {
    if (!gInstalled) {
        return true;
    }
    gBefore = nullptr;  // the handler belongs to a mod that's shutting down
    if (!hook::Uninstall(init, kHookName)) {
        return false;  // still live, and still calling gOrig
    }
    gInstalled = false;
    log::Info("unhooked {}", kHookName);
    return true;
}

bool InPrimaryView() {
    return tInPrimary;
}

unsigned OtherCalls() {
    return gOtherCalls.load(std::memory_order_relaxed);
}

void LogStats() {
    if (!gInstalled) {
        return;
    }
    unsigned const primary = gPrimaryCalls.exchange(0);
    unsigned const other = gOtherCalls.exchange(0);
    std::uintptr_t const lastOther = gLastOtherCaller.load();
    std::string callers;
    for (CallerCount& slot : gPrimaryCallers) {
        if (slot.caller != 0) {
            callers += std::format(" exe+0x{:X}x{}", slot.caller - gModuleBase, slot.count);
        }
        slot = {};
    }
    if (other > 0) {
        log::Info("    scene views: {} primary (from{}), {} other (last other caller exe+0x{:X})", primary, callers, other,
            lastOther - gModuleBase);
    } else {
        log::Info("    scene views: {} primary (from{})", primary, callers);
    }
}

} // namespace mevr::engine::scene_view
