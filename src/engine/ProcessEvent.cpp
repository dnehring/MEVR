#include "engine/ProcessEvent.hpp"

#include "core/Hook.hpp"
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
    if (!hook::Install(init, kHookName, target, &ProcessEventHook, gProcessEventOrig)) {
        log::Error("failed to hook UObject::ProcessEvent");
        return false;
    }
    gInstalled = true;
    log::Info("hooked UObject::ProcessEvent ({} handlers)", gHandlers.size());
    return true;
}

bool Uninstall(::LESDK::Initializer& init) {
    if (!gInstalled) {
        return true;
    }
    if (gViaHost) {
        // Waits (bounded) for in-flight handlers; the host keeps this DLL loaded if one stays.
        gHostSet(nullptr);
        gInstalled = gViaHost = false;
        return true;
    }
    if (!hook::Uninstall(init, kHookName)) {
        return false;  // still live, and still calling gProcessEventOrig
    }
    gInstalled = false;
    log::Info("unhooked UObject::ProcessEvent");
    return true;
}

} // namespace mevr::engine::process_event
