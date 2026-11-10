// SPDX-License-Identifier: MIT
#include "engine/ConversationHooks.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "engine/Addresses.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace mevr::engine::conversation_hooks {
namespace {

constexpr char const* kPlaceHookName = "UBioConversation::SetPositionRotator";
constexpr char const* kInterruptHookName = "ABioWorldInfo::InterruptConversation";

constexpr std::uint32_t kSetPositionRotatorRva = addresses::kConversationSetPositionRotator;
constexpr std::uint32_t kInterruptConversationRva = addresses::kInterruptConversation;

using tSetPositionRotator = void(void* conversation, void* actor, float* location, void* rotation);
using tInterruptConversation = void(void* worldInfo, void* newcomer);

tSetPositionRotator* gOrigPlace = nullptr;  // written before the hook goes live (core/Hook)
tInterruptConversation* gOrigInterrupt = nullptr;
bool gPlaceInstalled = false;
bool gInterruptInstalled = false;
std::atomic<int> gInFlight{0};
std::atomic<KeepProvider> gKeep{nullptr};
std::atomic<InterruptProvider> gInterrupt{nullptr};
std::atomic<unsigned> gPlacements{0};
std::atomic<unsigned> gKept{0};
std::atomic<unsigned> gFacings{0};
std::atomic<unsigned> gInterrupts{0};
std::atomic<unsigned> gRefused{0};

bool IsFunctionStart(void* address) {
    DWORD64 imageBase = 0;
    auto const* const entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &imageBase, nullptr);
    return entry != nullptr && imageBase + entry->BeginAddress == reinterpret_cast<DWORD64>(address);
}

// The providers' answers, guarded: a fault there means the game's own behaviour, never a fault in the engine's code.
bool AskKeep(KeepProvider provider, void const* conversation, void const* actor, float const* location) {
    __try {
        return provider(conversation, actor, location);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gKeep.store(nullptr, std::memory_order_relaxed);  // never again this load
        return false;
    }
}

bool AskInterrupt(InterruptProvider provider, void const* worldInfo, void const* newcomer) {
    __try {
        return provider(worldInfo, newcomer);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gInterrupt.store(nullptr, std::memory_order_relaxed);
        return false;
    }
}

// No C++ objects with destructors in the hooks, so __finally keeps the in-flight count right even when an exception
// unwinds through the engine's call.
void SetPositionRotatorHook(void* conversation, void* actor, float* location, void* rotation) {
    gInFlight.fetch_add(1, std::memory_order_acquire);
    __try {
        float zero[3] = {0.0f, 0.0f, 0.0f};
        float* place = location;
        if (location != nullptr && (location[0] != 0.0f || location[1] != 0.0f || location[2] != 0.0f)) {
            gPlacements.fetch_add(1, std::memory_order_relaxed);
            KeepProvider const provider = gKeep.load(std::memory_order_relaxed);
            if (provider != nullptr && conversation != nullptr && actor != nullptr && AskKeep(provider, conversation, actor, location)) {
                place = zero;  // the game's own facing-only path: stopped, turned, not moved
                gKept.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            gFacings.fetch_add(1, std::memory_order_relaxed);
        }
        gOrigPlace(conversation, actor, place, rotation);
    } __finally {
        gInFlight.fetch_sub(1, std::memory_order_release);
    }
}

void InterruptConversationHook(void* worldInfo, void* newcomer) {
    gInFlight.fetch_add(1, std::memory_order_acquire);
    __try {
        gInterrupts.fetch_add(1, std::memory_order_relaxed);
        InterruptProvider const provider = gInterrupt.load(std::memory_order_relaxed);
        if (provider != nullptr && worldInfo != nullptr && AskInterrupt(provider, worldInfo, newcomer)) {
            gRefused.fetch_add(1, std::memory_order_relaxed);
        } else {
            gOrigInterrupt(worldInfo, newcomer);
        }
    } __finally {
        gInFlight.fetch_sub(1, std::memory_order_release);
    }
}

} // namespace

bool Install(hook::Context& init) {
    if (gPlaceInstalled && gInterruptInstalled) {
        return true;
    }
    if (kSetPositionRotatorRva == 0 || kInterruptConversationRva == 0) {
        log::Warn("convo hooks: no addresses for this game: placements are undone after the fact, flowed chains unprotected");
        return false;
    }
    void* const place = init.Resolve(kSetPositionRotatorRva);
    void* const interrupt = init.Resolve(kInterruptConversationRva);
    if (place == nullptr || !IsFunctionStart(place) || interrupt == nullptr || !IsFunctionStart(interrupt)) {
        log::Error("convo hooks: {} or {} is not a function start; not hooking", place, interrupt);
        return false;
    }
    if (!gPlaceInstalled) {
        gPlaceInstalled = hook::Install(init, kPlaceHookName, reinterpret_cast<tSetPositionRotator*>(place), &SetPositionRotatorHook, gOrigPlace);
        if (gPlaceInstalled) {
            log::Info("hooked {}", kPlaceHookName);
        } else {
            log::Error("convo hooks: the {} hook failed", kPlaceHookName);
        }
    }
    if (!gInterruptInstalled) {
        gInterruptInstalled = hook::Install(init, kInterruptHookName, reinterpret_cast<tInterruptConversation*>(interrupt),
            &InterruptConversationHook, gOrigInterrupt);
        if (gInterruptInstalled) {
            log::Info("hooked {}", kInterruptHookName);
        } else {
            log::Error("convo hooks: the {} hook failed", kInterruptHookName);
        }
    }
    return gPlaceInstalled && gInterruptInstalled;
}

bool Uninstall(hook::Context& init, bool waitForCalls) {
    gKeep.store(nullptr, std::memory_order_relaxed);
    gInterrupt.store(nullptr, std::memory_order_relaxed);
    bool removed = true;
    if (gPlaceInstalled) {
        if (hook::Uninstall(init, kPlaceHookName)) {
            gPlaceInstalled = false;
            log::Info("unhooked {}", kPlaceHookName);
        } else {
            removed = false;  // still live, and still calling gOrigPlace
        }
    }
    if (gInterruptInstalled) {
        if (hook::Uninstall(init, kInterruptHookName)) {
            gInterruptInstalled = false;
            log::Info("unhooked {}", kInterruptHookName);
        } else {
            removed = false;
        }
    }
    if (!removed) {
        return false;
    }
    // A call already inside a detour finishes with the old code: this module must outlive it.
    for (int i = 0; waitForCalls && i < 200 && gInFlight.load() > 0; ++i) {
        Sleep(1);
    }
    bool const drained = gInFlight.load() == 0;
    if (!drained && waitForCalls) {
        log::Error("convo hooks: {} call(s) still in a hook 200 ms after unhooking", gInFlight.load());
    }
    return drained;
}

bool Installed() {
    return gPlaceInstalled && gInterruptInstalled;
}

void SetKeepProvider(KeepProvider provider) {
    gKeep.store(provider, std::memory_order_relaxed);
}

void SetInterruptProvider(InterruptProvider provider) {
    gInterrupt.store(provider, std::memory_order_relaxed);
}

Stats Totals() {
    Stats stats;
    stats.placements = gPlacements.load(std::memory_order_relaxed);
    stats.kept = gKept.load(std::memory_order_relaxed);
    stats.facings = gFacings.load(std::memory_order_relaxed);
    stats.interrupts = gInterrupts.load(std::memory_order_relaxed);
    stats.refused = gRefused.load(std::memory_order_relaxed);
    return stats;
}

} // namespace mevr::engine::conversation_hooks
