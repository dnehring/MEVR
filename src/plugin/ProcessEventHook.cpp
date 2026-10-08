#include "plugin/ProcessEventHook.hpp"

#include "core/Log.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include <atomic>

namespace mevr::host::process_event {
namespace {

using tProcessEvent = void(UObject* context, UFunction* function, void* parms, void* result);
tProcessEvent* gOriginal = nullptr;

std::atomic<Callback> gCallback{nullptr};
// Threads inside the callback. A counter rather than a lock: handlers call ProcessEvent
// themselves, and a reader-writer lock taken again while a writer waits would deadlock.
std::atomic<int> gInFlight{0};

void ProcessEventHook(UObject* context, UFunction* function, void* parms, void* result) {
    gOriginal(context, function, parms, result);
    gInFlight.fetch_add(1);
    if (Callback const callback = gCallback.load()) {
        callback(context, function, parms, result);
    }
    gInFlight.fetch_sub(1);
}

} // namespace

bool Install(::LESDK::Initializer& init) {
    auto* const target = init.ResolveTyped<tProcessEvent>(BUILTIN_PROCESSEVENT_RVA);
    if (target == nullptr) {
        log::Error("failed to resolve UObject::ProcessEvent");
        return false;
    }
    gOriginal = reinterpret_cast<tProcessEvent*>(init.InstallHook("UObject::ProcessEvent",
        reinterpret_cast<void*>(target), reinterpret_cast<void*>(&ProcessEventHook)));
    if (gOriginal == nullptr) {
        log::Error("failed to hook UObject::ProcessEvent");
        return false;
    }
    log::Info("hooked UObject::ProcessEvent (host-owned)");
    return true;
}

void SetCallback(Callback callback) {
    gCallback.store(callback);
    if (callback == nullptr) {
        // A thread that read the old callback has already counted itself in.
        while (gInFlight.load() != 0) {
            SwitchToThread();
        }
    }
}

} // namespace mevr::host::process_event
