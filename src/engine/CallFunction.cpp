// SPDX-License-Identifier: MIT
#include "engine/CallFunction.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "engine/Addresses.hpp"

#include <Windows.h>

#include <atomic>
#include <vector>

struct FFrame;  // the script stack frame: passed through; a skip reads Node and Code and moves Code

namespace mevr::engine::call_function {
namespace {

constexpr char const* kHookName = "UObject::CallFunction";

// FFrame (FOutputDevice's vtable, three DWORDs (four on LE3, still ending at 0x18), then Node, Object, Code): the same
// offsets in all three games (plugin/CrashBreadcrumbs uses them too).
constexpr std::size_t kFrameNode = 0x18;
constexpr std::size_t kFrameCode = 0x28;
// UE3's token after a call's last parameter.
constexpr std::uint8_t kEndFunctionParms = 0x16;

struct SkipEntry {
    void const* function;
    SkipHandler handler;
};
std::vector<SkipEntry> gSkips;
std::atomic<bool> gToldNoEndToken{false};

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

// The frame's caller and the next bytecode byte; false if the frame can't be read.
bool ReadFrame(FFrame const* stack, ue3::Function** caller, std::uint8_t const** code, std::uint8_t* next) {
    __try {
        auto const* const frame = reinterpret_cast<std::uint8_t const*>(stack);
        *caller = *reinterpret_cast<ue3::Function* const*>(frame + kFrameNode);
        *code = *reinterpret_cast<std::uint8_t const* const*>(frame + kFrameCode);
        *next = *code != nullptr ? **code : 0;
        return *code != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// True: the call was skipped (its EX_EndFunctionParms consumed).
bool TrySkip(ue3::Object* context, FFrame* stack, ue3::Function* function, SkipHandler handler) {
    ue3::Function* caller = nullptr;
    std::uint8_t const* code = nullptr;
    std::uint8_t next = 0;
    if (stack == nullptr || !ReadFrame(stack, &caller, &code, &next)) {
        return false;
    }
    if (next != kEndFunctionParms) {
        if (!gToldNoEndToken.exchange(true)) {
            log::Warn("call function: a call to skip wasn't followed by EX_EndFunctionParms (0x{:02X}): it runs as normal", next);
        }
        return false;
    }
    if (!handler(context, function, caller)) {
        return false;
    }
    auto** const codeSlot = reinterpret_cast<std::uint8_t const**>(reinterpret_cast<std::uint8_t*>(stack) + kFrameCode);
    *codeSlot = code + 1;
    write_journal::Wrote("CallFunction.skip", codeSlot, sizeof(*codeSlot));
    return true;
}

void CallFunctionHook(ue3::Object* context, FFrame* stack, void* result, ue3::Function* function) {
    for (SkipEntry const& entry : gSkips) {
        if (entry.function == function && TrySkip(context, stack, function, entry.handler)) {
            return;
        }
    }
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

void AddSkipHandler(ue3::Function* function, SkipHandler handler) {
    gSkips.push_back({function, handler});
}

void ClearHandlers() {
    gHandlers.clear();
    gSkips.clear();
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
