// SPDX-License-Identifier: MIT
#include "engine/CallFunction.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "engine/Addresses.hpp"

#include <atomic>
#include <vector>

struct FFrame;  // the script stack frame: passed through, never read

namespace mevr::engine::call_function {
namespace {

constexpr char const* kHookName = "UObject::CallFunction";

// UObject::CallFunction(Stack, Result, Function), as the engine calls it.
using tCallFunction = void(ue3::Object* context, FFrame* stack, void* result, ue3::Function* function);

struct Entry {
    void const* function;
    Handler handler;
};

// Every script call passes through here, so the lookup is a short pointer scan.
std::vector<Entry> gHandlers;
bool gInstalled = false;

tCallFunction* gCallFunctionOrig = nullptr;

std::atomic<BeginCrumb> gBeginCrumb{nullptr};
std::atomic<EndCrumb> gEndCrumb{nullptr};

void CallFunctionHook(ue3::Object* context, FFrame* stack, void* result, ue3::Function* function) {
    gCallFunctionOrig(context, stack, result, function);
    for (Entry const& entry : gHandlers) {
        if (entry.function == function) {
            BeginCrumb const begin = gBeginCrumb.load(std::memory_order_relaxed);
            EndCrumb const end = gEndCrumb.load(std::memory_order_relaxed);
            std::uint64_t const crumb = begin != nullptr ? begin(function, context) : 0;
            entry.handler(context, function, result);
            if (begin != nullptr && end != nullptr) {
                end(crumb);
            }
        }
    }
}

} // namespace

void AddPostHandler(ue3::Function* function, Handler handler) {
    gHandlers.push_back({function, handler});
}

void ClearHandlers() {
    gHandlers.clear();
}

void SetBreadcrumbs(BeginCrumb begin, EndCrumb end) {
    gEndCrumb.store(end);
    gBeginCrumb.store(begin);
}

bool Install(hook::Context& init) {
    auto* const target = init.ResolveTyped<tCallFunction>(addresses::kCallFunction);
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

bool Uninstall(hook::Context& init) {
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
