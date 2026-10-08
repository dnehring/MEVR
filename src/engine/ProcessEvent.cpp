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

using tProcessEvent = void(UObject* context, UFunction* function, void* parms, void* result);
tProcessEvent* gProcessEventOrig = nullptr;

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

bool Install(::LESDK::Initializer& init) {
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
    init.UninstallHook(kHookName);
    gInstalled = false;
    gProcessEventOrig = nullptr;
    log::Info("unhooked UObject::ProcessEvent");
}

} // namespace mevr::engine::process_event
