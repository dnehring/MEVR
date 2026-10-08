#include "engine/ProcessEvent.hpp"

#include "core/Log.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include <vector>

namespace mevr::engine::process_event {
namespace {

constexpr char const* kHookName = "UObject::ProcessEvent";

struct Entry {
    UFunction* function;
    Handler handler;
};

// Matched by pointer: no per-call name lookups (docs/trilogy-design.md, rule 5).
std::vector<Entry> gHandlers;
bool gInstalled = false;
SetHostCallback gHostSet = nullptr;
bool gViaHost = false;

using tProcessEvent = void(UObject* context, UFunction* function, void* parms, void* result);
tProcessEvent* gProcessEventOrig = nullptr;

void Dispatch(UObject* context, UFunction* function, void* parms, void* result) {
    for (Entry const& entry : gHandlers) {
        if (entry.function == function) {
            entry.handler(context, function, parms, result);
        }
    }
}

void ProcessEventHook(UObject* context, UFunction* function, void* parms, void* result) {
    gProcessEventOrig(context, function, parms, result);
    for (Entry const& entry : gHandlers) {
        if (entry.function == function) {
            entry.handler(context, function, parms, result);
        }
    }
}

} // namespace

void AddPostHandler(UFunction* function, Handler handler) {
    gHandlers.push_back({function, handler});
}

void ClearHandlers() {
    gHandlers.clear();
}

void UseHost(SetHostCallback set) {
    gHostSet = set;
}

bool Install(::LESDK::Initializer& init) {
    if (gHostSet != nullptr) {
        gHostSet(&Dispatch);
        gInstalled = gViaHost = true;
        log::Info("ProcessEvent handlers live on the host's hook ({} handlers)", gHandlers.size());
        return true;
    }
    auto* const target = init.ResolveTyped<tProcessEvent>(BUILTIN_PROCESSEVENT_RVA);
    if (target == nullptr) {
        log::Error("failed to resolve UObject::ProcessEvent");
        return false;
    }
    gProcessEventOrig = reinterpret_cast<tProcessEvent*>(init.InstallHook(kHookName,
        reinterpret_cast<void*>(target), reinterpret_cast<void*>(&ProcessEventHook)));
    if (gProcessEventOrig == nullptr) {
        log::Error("failed to hook UObject::ProcessEvent");
        return false;
    }
    gInstalled = true;
    log::Info("hooked UObject::ProcessEvent ({} handlers)", gHandlers.size());
    return true;
}

void Uninstall(::LESDK::Initializer& init) {
    if (!gInstalled) {
        return;
    }
    if (gViaHost) {
        gHostSet(nullptr);  // waits for in-flight handlers
        gInstalled = gViaHost = false;
        return;
    }
    init.UninstallHook(kHookName);
    gInstalled = false;
    gProcessEventOrig = nullptr;
    log::Info("unhooked UObject::ProcessEvent");
}

} // namespace mevr::engine::process_event
