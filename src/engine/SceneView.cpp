#include "engine/SceneView.hpp"

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
    std::size_t primaryReturn;  // RVA its call in FViewportClient::Draw returns to
};

#if defined(SDK_TARGET_LE1)
// Both are function starts / inside FViewportClient::Draw (0x4C71A0) per the exe's .pdata
// (tools/verify_rvas.py). Source of the facts: Halcyon's MassEffect1-VR (research/13).
constexpr Addresses kAddresses{0x4C5300, 0x4C7A4E};
#else
constexpr Addresses kAddresses{0, 0};  // LE2/LE3: not found yet
#endif

using tCalcSceneView = void*(void* player, void* viewFamily, void* viewLocation, void* viewRotation,
    void* viewport, void* viewDrawer);
tCalcSceneView* gOrig = nullptr;
BeforePrimaryView gBefore = nullptr;
std::uintptr_t gModuleBase = 0;
std::uintptr_t gPrimaryReturn = 0;
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
    if (caller != gPrimaryReturn || tInPrimary) {
        gOtherCalls.fetch_add(1, std::memory_order_relaxed);
        gLastOtherCaller.store(caller, std::memory_order_relaxed);
        return gOrig(player, viewFamily, viewLocation, viewRotation, viewport, viewDrawer);
    }
    gPrimaryCalls.fetch_add(1, std::memory_order_relaxed);
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
    gPrimaryReturn = reinterpret_cast<std::uintptr_t>(init.Resolve(::LESDK::Address::FromOffset(kAddresses.primaryReturn)));
    gBefore = before;
    gOrig = reinterpret_cast<tCalcSceneView*>(init.InstallHook(kHookName, target,
        reinterpret_cast<void*>(&CalcSceneViewHook)));
    if (gOrig == nullptr) {
        log::Error("CalcSceneView: hook failed");
        gBefore = nullptr;
        return false;
    }
    gInstalled = true;
    log::Info("hooked {}", kHookName);
    return true;
}

void Uninstall(::LESDK::Initializer& init) {
    if (!gInstalled) {
        return;
    }
    init.UninstallHook(kHookName);
    gInstalled = false;
    gOrig = nullptr;
    gBefore = nullptr;
    log::Info("unhooked {}", kHookName);
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
    if (other > 0) {
        log::Info("    scene views: {} primary, {} other (last other caller exe+0x{:X})", primary, other,
            lastOther - gModuleBase);
    } else {
        log::Info("    scene views: {} primary", primary);
    }
}

} // namespace mevr::engine::scene_view
