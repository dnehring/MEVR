// SPDX-License-Identifier: MIT
#include "engine/ProcessEvent.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"

// Only the hook's target and context (BUILTIN_PROCESSEVENT_RVA, LESDK::Initializer): research/34 stage 4.
#include <LESDK/Init.hpp>

#include <vector>

namespace mevr::engine::process_event {
namespace {

constexpr char const* kHookName = "UObject::ProcessEvent";

struct Entry {
    void const* function;
    Handler handler;              // LExSDKv2-typed modules
    ObjectHandler objectHandler;  // modules on ue3/
};

void Run(Entry const& entry, UObject* context, UFunction* function, void* parms, void* result) {
    if (entry.handler != nullptr) {
        entry.handler(context, function, parms, result);
    } else {
        entry.objectHandler(reinterpret_cast<ue3::Object*>(context), reinterpret_cast<ue3::Function*>(function), parms, result);
    }
}

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
            Run(entry, context, function, parms, result);
        }
    }
}

void ProcessEventHook(UObject* context, UFunction* function, void* parms, void* result) {
    gProcessEventOrig(context, function, parms, result);
    for (Entry const& entry : gHandlers) {
        if (entry.function == function) {
            Run(entry, context, function, parms, result);
        }
    }
}

} // namespace

void AddPostHandler(UFunction* function, Handler handler) {
    gHandlers.push_back({function, handler, nullptr});
}

void AddPostHandler(ue3::Function* function, ObjectHandler handler) {
    gHandlers.push_back({function, nullptr, handler});
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
