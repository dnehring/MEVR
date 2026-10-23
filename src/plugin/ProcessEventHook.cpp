// SPDX-License-Identifier: MIT
#include "plugin/ProcessEventHook.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include <atomic>

namespace mevr::host::process_event {
namespace {

using tProcessEvent = void(UObject* context, UFunction* function, void* parms, void* result);
// Written by LEBinkProxy before the hook is enabled (core/Hook): attach runs on LEBinkProxy's
// thread while the game thread is already calling ProcessEvent.
tProcessEvent* gOriginal = nullptr;

std::atomic<Callback> gCallback{nullptr};
// Threads inside the callback. A counter rather than a lock: handlers call ProcessEvent
// themselves, and a reader-writer lock taken again while a writer waits would deadlock.
std::atomic<int> gInFlight{0};

// No C++ objects with destructors here, so __finally can keep the count right when an exception
// unwinds through the callback (Halcyon calls ProcessEvent under __except in places).
void ProcessEventHook(UObject* context, UFunction* function, void* parms, void* result) {
    gOriginal(context, function, parms, result);
    gInFlight.fetch_add(1);
    __try {
        if (Callback const callback = gCallback.load()) {
            callback(context, function, parms, result);
        }
    } __finally {
        gInFlight.fetch_sub(1);
    }
}

} // namespace

bool Install(::LESDK::Initializer& init) {
    auto* const target = init.ResolveTyped<tProcessEvent>(BUILTIN_PROCESSEVENT_RVA);
    if (target == nullptr) {
        log::Error("failed to resolve UObject::ProcessEvent");
        return false;
    }
    if (!hook::Install(init, "UObject::ProcessEvent", target, &ProcessEventHook, gOriginal)) {
        log::Error("failed to hook UObject::ProcessEvent");
        return false;
    }
    log::Info("hooked UObject::ProcessEvent (host-owned)");
    return true;
}

bool SetCallback(Callback callback, std::uint32_t waitMs) {
    gCallback.store(callback);
    if (callback != nullptr) {
        return true;
    }
    // A thread that read the old callback has already counted itself in.
    ULONGLONG const start = GetTickCount64();
    while (gInFlight.load() != 0) {
        if (GetTickCount64() - start >= waitMs) {
            if (waitMs > 0) {
                log::Error("ProcessEvent: {} call(s) still inside the old callback after {} ms", gInFlight.load(), waitMs);
            }
            return false;
        }
        SwitchToThread();
    }
    return true;
}

} // namespace mevr::host::process_event
