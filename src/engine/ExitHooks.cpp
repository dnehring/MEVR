// SPDX-License-Identifier: MIT
#include "engine/ExitHooks.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "engine/Addresses.hpp"
#include "engine/GameAdapter.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace mevr::engine::exit_hooks {
namespace {

constexpr char const* kHookName = "UBioActorBehavior::execSetActive";
constexpr std::uint32_t kRva = addresses::kBehaviorExecSetActive;
constexpr std::size_t kFrameNode = adapter::kFrame.node;
constexpr std::size_t kFrameLocals = adapter::kFrame.locals;

using tExec = void(void* self, void* frame, void* result);

tExec* gOrig = nullptr;  // written before the hook goes live (core/Hook)
bool gInstalled = false;
std::atomic<int> gInFlight{0};
std::atomic<SetActiveProvider> gProvider{nullptr};
std::atomic<void const*> gFunction{nullptr};
std::atomic<unsigned> gCalls{0};
std::atomic<unsigned> gFromEvent{0};
std::atomic<unsigned> gWithheld{0};

bool IsFunctionStart(void* address) {
    DWORD64 imageBase = 0;
    auto const* const entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &imageBase, nullptr);
    return entry != nullptr && imageBase + entry->BeginAddress == reinterpret_cast<DWORD64>(address);
}

// The frame's Node and Locals, guarded (a frame is the engine's: never trust it blindly).
bool ReadFrame(void const* frame, void const** node, void const** locals) {
    __try {
        auto const* const f = static_cast<std::uint8_t const*>(frame);
        *node = *reinterpret_cast<void const* const*>(f + kFrameNode);
        *locals = *reinterpret_cast<void const* const*>(f + kFrameLocals);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Ask(SetActiveProvider provider, void const* self, void const* parms) {
    __try {
        return provider(self, parms);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gProvider.store(nullptr, std::memory_order_relaxed);  // never again this load: the game's own behaviour
        return false;
    }
}

// No C++ objects with destructors here: __finally keeps the in-flight count right whatever unwinds through the engine's call.
void ExecSetActiveHook(void* self, void* frame, void* result) {
    gInFlight.fetch_add(1, std::memory_order_acquire);
    __try {
        gCalls.fetch_add(1, std::memory_order_relaxed);
        bool withhold = false;
        SetActiveProvider const provider = gProvider.load(std::memory_order_relaxed);
        void const* const function = gFunction.load(std::memory_order_relaxed);
        void const* node = nullptr;
        void const* locals = nullptr;
        if (provider != nullptr && function != nullptr && self != nullptr && frame != nullptr && ReadFrame(frame, &node, &locals) &&
            node == function && locals != nullptr) {
            gFromEvent.fetch_add(1, std::memory_order_relaxed);
            withhold = Ask(provider, self, locals);
        }
        if (withhold) {
            gWithheld.fetch_add(1, std::memory_order_relaxed);  // not run: ProcessEvent returns as after it
        } else {
            gOrig(self, frame, result);
        }
    } __finally {
        gInFlight.fetch_sub(1, std::memory_order_release);
    }
}

} // namespace

bool Install(hook::Context& init) {
    if (gInstalled) {
        return true;
    }
    if (kRva == 0) {
        log::Warn("exit hooks: no address for this game: people leaving a conversation are not held");
        return false;
    }
    void* const target = init.Resolve(kRva);
    if (target == nullptr || !IsFunctionStart(target)) {
        log::Error("exit hooks: {} is not a function start; not hooking {}", target, kHookName);
        return false;
    }
    gInstalled = hook::Install(init, kHookName, reinterpret_cast<tExec*>(target), &ExecSetActiveHook, gOrig);
    if (gInstalled) {
        log::Info("hooked {}", kHookName);
    } else {
        log::Error("exit hooks: the {} hook failed", kHookName);
    }
    return gInstalled;
}

bool Uninstall(hook::Context& init, bool waitForCalls) {
    gProvider.store(nullptr, std::memory_order_relaxed);
    if (gInstalled) {
        if (!hook::Uninstall(init, kHookName)) {
            return false;  // still live, and still calling gOrig
        }
        gInstalled = false;
        log::Info("unhooked {}", kHookName);
    }
    // A call already inside the detour finishes with the old code: this module must outlive it.
    for (int i = 0; waitForCalls && i < 200 && gInFlight.load() > 0; ++i) {
        Sleep(1);
    }
    bool const drained = gInFlight.load() == 0;
    if (!drained && waitForCalls) {
        log::Error("exit hooks: {} call(s) still in the hook 200 ms after unhooking", gInFlight.load());
    }
    return drained;
}

bool Installed() {
    return gInstalled;
}

void SetProvider(SetActiveProvider provider, void const* function) {
    gFunction.store(function, std::memory_order_relaxed);
    gProvider.store(provider, std::memory_order_relaxed);
}

Stats Totals() {
    return {gCalls.load(std::memory_order_relaxed), gFromEvent.load(std::memory_order_relaxed), gWithheld.load(std::memory_order_relaxed)};
}

} // namespace mevr::engine::exit_hooks
