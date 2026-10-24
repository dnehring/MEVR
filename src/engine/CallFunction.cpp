// SPDX-License-Identifier: MIT
#include "engine/CallFunction.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"

// Only the hook's target and context (BUILTIN_CALLFUNCTION_RVA, LESDK::Initializer): research/34 stage 4.
#include <LESDK/Init.hpp>

#include <atomic>
#include <vector>

struct FFrame;  // the script stack frame: passed through, never read

namespace mevr::engine::call_function {
namespace {

constexpr char const* kHookName = "UObject::CallFunction";

// UObject::CallFunction(Stack, Result, Function), as the engine calls it.
using tCallFunction = void(UObject* context, FFrame* stack, void* result, UFunction* function);

struct Entry {
    void const* function;
    Handler handler;              // LExSDKv2-typed modules
    ObjectHandler objectHandler;  // modules on ue3/
};

// Every script call passes through here, so the lookup is a short pointer scan.
std::vector<Entry> gHandlers;
bool gInstalled = false;

tCallFunction* gCallFunctionOrig = nullptr;

std::atomic<BeginCrumb> gBeginCrumb{nullptr};
std::atomic<EndCrumb> gEndCrumb{nullptr};

void CallFunctionHook(UObject* context, FFrame* stack, void* result, UFunction* function) {
    gCallFunctionOrig(context, stack, result, function);
    for (Entry const& entry : gHandlers) {
        if (entry.function == function) {
            BeginCrumb const begin = gBeginCrumb.load(std::memory_order_relaxed);
            EndCrumb const end = gEndCrumb.load(std::memory_order_relaxed);
            std::uint64_t const crumb = begin != nullptr ? begin(function, context) : 0;
            if (entry.handler != nullptr) {
                entry.handler(context, function, result);
            } else {
                entry.objectHandler(reinterpret_cast<ue3::Object*>(context), reinterpret_cast<ue3::Function*>(function), result);
            }
            if (begin != nullptr && end != nullptr) {
                end(crumb);
            }
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

void SetBreadcrumbs(BeginCrumb begin, EndCrumb end) {
    gEndCrumb.store(end);
    gBeginCrumb.store(begin);
}

bool Install(::LESDK::Initializer& init) {
    auto* const target = init.ResolveTyped<tCallFunction>(BUILTIN_CALLFUNCTION_RVA);
    if (target == nullptr) {
        log::Error("failed to resolve UObject::CallFunction");
        return false;
    }
    if (!hook::Install(init, kHookName, target, &CallFunctionHook, gCallFunctionOrig)) {
        log::Error("failed to hook UObject::CallFunction");
        return false;
    }
    gInstalled = true;
    log::Info("hooked UObject::CallFunction ({} handlers)", gHandlers.size());
    return true;
}

bool Uninstall(::LESDK::Initializer& init) {
    if (!gInstalled) {
        return true;
    }
    if (!hook::Uninstall(init, kHookName)) {
        return false;  // still live, and still calling gCallFunctionOrig
    }
    gInstalled = false;
    log::Info("unhooked UObject::CallFunction");
    return true;
}

} // namespace mevr::engine::call_function
