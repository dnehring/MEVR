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

#if defined(MEVR_GAME_LE1)
constexpr char const* kHookName = "UBioActorBehavior::execSetActive";
constexpr std::uint32_t kRva = addresses::kBehaviorExecSetActive;
#else
constexpr char const* kHookName = "AActor::execSetActive";
constexpr std::uint32_t kRva = addresses::kActorExecSetActive;
#endif
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
#if defined(MEVR_GAME_LE3)
// ME3's script caller (BioSeqAct_SetActive.Activated: `oTarget.SetActive(bSetActive)`): the native runs on that script's frame.
constexpr std::size_t kFrameObject = adapter::kFrame.object;
constexpr std::size_t kFrameCode = adapter::kFrame.code;
constexpr std::uint8_t kDebugInfo = 0x41;  // EX_DebugInfo: the token P_FINISH also steps over when it follows
using tNative = void(void* object, void* frame, void* result);
tNative* const* gNatives = nullptr;  // GNatives, resolved at Install
std::atomic<ScriptProvider> gScriptProvider{nullptr};
std::atomic<void const*> gScriptCaller{nullptr};
std::atomic<unsigned> gFromScript{0};

void const* ReadObject(void const* frame) {
    __try {
        return *reinterpret_cast<void const* const*>(static_cast<std::uint8_t const*>(frame) + kFrameObject);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool AskScript(ScriptProvider provider, void const* self, void const* op) {
    __try {
        return provider(self, op);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gScriptProvider.store(nullptr, std::memory_order_relaxed);
        return false;
    }
}

// The thunk's own steps without its last call (the actor's C++ SetActive): P_GET_UBOOL through the engine's GNatives, P_FINISH
// (the EX_EndFunctionParms token, and an EX_DebugInfo after it), exactly as AActor::execSetActive makes them (Ghidra, ME3
// 0x4BFAF0), so the calling script goes on from where the call would have left it.
void StepOverCall(void* frame) {
    auto* const f = static_cast<std::uint8_t*>(frame);
    std::uint8_t*& code = *reinterpret_cast<std::uint8_t**>(f + kFrameCode);
    void* const object = *reinterpret_cast<void**>(f + kFrameObject);
    std::uint32_t value = 0;
    std::uint8_t const token = *code++;
    gNatives[token](object, frame, &value);
    ++code;
    if (*code == kDebugInfo) {
        ++code;
        gNatives[kDebugInfo](object, frame, nullptr);
    }
}
#endif

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
#if defined(MEVR_GAME_LE3)
        // ME3: the call from BioSeqAct_SetActive's script (its own frame; never ProcessEvent's).
        bool stepped = false;
        ScriptProvider const scriptProvider = gScriptProvider.load(std::memory_order_relaxed);
        void const* const caller = gScriptCaller.load(std::memory_order_relaxed);
        if (!withhold && scriptProvider != nullptr && caller != nullptr && gNatives != nullptr && self != nullptr && frame != nullptr &&
            (node != nullptr || ReadFrame(frame, &node, &locals)) && node == caller) {
            gFromScript.fetch_add(1, std::memory_order_relaxed);
            void const* const op = ReadObject(frame);
            if (op != nullptr && AskScript(scriptProvider, self, op)) {
                gWithheld.fetch_add(1, std::memory_order_relaxed);
                StepOverCall(frame);
                stepped = true;
            }
        }
        if (stepped) {
            // withheld: its parameter read and the end of its parameters stepped over, the script goes on as after the call
        } else
#endif
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
#if defined(MEVR_GAME_LE3)
    gNatives = static_cast<tNative* const*>(init.Resolve(addresses::kGNatives));
    if (gNatives == nullptr) {
        log::Warn("exit hooks: no GNatives: BioSeqAct_SetActive's calls can't be held (only ProcessEvent's)");
    }
#endif
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
#if defined(MEVR_GAME_LE3)
    gScriptProvider.store(nullptr, std::memory_order_relaxed);
#endif
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

#if defined(MEVR_GAME_LE3)
void SetScriptProvider(ScriptProvider provider, void const* caller) {
    gScriptCaller.store(caller, std::memory_order_relaxed);
    gScriptProvider.store(provider, std::memory_order_relaxed);
}

unsigned FromScript() {
    return gFromScript.load(std::memory_order_relaxed);
}
#endif

Stats Totals() {
    return {gCalls.load(std::memory_order_relaxed), gFromEvent.load(std::memory_order_relaxed), gWithheld.load(std::memory_order_relaxed)};
}

} // namespace mevr::engine::exit_hooks
