// SPDX-License-Identifier: MIT
#include "game/GamepadBridge.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#if defined(MEVR_GAME_LE2) && defined(MEVR_LE2_DIAGNOSTIC)
#include "core/Exe.hpp"
#include <Windows.h>
#include <Psapi.h>
#include <intrin.h>
#include <limits>
#include <memory>
#include <stdexcept>
#else
#include "core/vr/Controllers.hpp"
#include "game/GamepadMapping.hpp"
#include "game/HeadTracking.hpp"
#include "game/SupportGrip.hpp"
#endif

#include <Windows.h>
#include <Xinput.h>

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

namespace mevr::game::gamepad_bridge {
namespace {

using tGetState = DWORD(WINAPI*)(DWORD index, XINPUT_STATE* state);

// The DLLs the game might use. XInputGetStateEx (ordinal 100) exists in 1_3 and 1_4 only.
constexpr char const* kDlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", "xinput1_2.dll", "xinput1_1.dll"};
constexpr std::size_t kMaxSlots = 2 * std::size(kDlls);

struct Slot {
    std::string name;  // hook name
    void* target = nullptr;
    tGetState original = nullptr;
};
std::array<Slot, kMaxSlots> gSlots;
std::size_t gSlotCount = 0;

std::mutex gMutex;  // the latches and packet numbering below
#if !defined(MEVR_GAME_LE2) || !defined(MEVR_LE2_DIAGNOSTIC)
gamepad::Latches gLatches;
XINPUT_GAMEPAD gLastPad{};
DWORD gPacket = 0;

std::atomic<unsigned> gCalls{0};
std::atomic<unsigned> gControllerFrames{0};
std::atomic<unsigned> gRealPadFrames{0};

#endif

#if defined(MEVR_GAME_LE2) && defined(MEVR_LE2_DIAGNOSTIC)
diagnostic_pad::Source gDiagnosticSource;
DiagnosticObservation gDiagnostic;
std::atomic<unsigned> gDiagnosticInFlight{};
std::atomic<std::uint64_t> gDiagnosticRecursive{};
thread_local unsigned gDiagnosticDepth{};
#if defined(MEVR_DIAGNOSTIC_PAD_BRIDGE_TEST)
testing::Platform gDiagnosticPlatform;
#endif
void Increment(std::uint64_t& n) { if(n!=std::numeric_limits<std::uint64_t>::max()) ++n; }
std::uint64_t Now() {
#if defined(MEVR_DIAGNOSTIC_PAD_BRIDGE_TEST)
    return gDiagnosticPlatform.nowMs();
#else
    return GetTickCount64();
#endif
}
bool Executable(std::uintptr_t address,std::uintptr_t owner) {
#if defined(MEVR_DIAGNOSTIC_PAD_BRIDGE_TEST)
    return gDiagnosticPlatform.executableInModule(address,owner);
#else
    MEMORY_BASIC_INFORMATION m{};
    if(!address || VirtualQuery(reinterpret_cast<void const*>(address),&m,sizeof m)!=sizeof m ||
       m.State!=MEM_COMMIT || m.Type!=MEM_IMAGE || reinterpret_cast<std::uintptr_t>(m.AllocationBase)!=owner ||
       (m.Protect&PAGE_GUARD)!=0) return false;
    auto const p=m.Protect&0xff;
    return p==PAGE_EXECUTE_READ || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
#endif
}
#if !defined(MEVR_DIAGNOSTIC_PAD_BRIDGE_TEST)
bool Describe(HMODULE module,DiagnosticModule& out) {
    MODULEINFO info{}; wchar_t path[1024]{};
    if(!module || !K32GetModuleInformation(GetCurrentProcess(),module,&info,sizeof info) || !info.SizeOfImage) return false;
    DWORD const count=GetModuleFileNameW(module,path,static_cast<DWORD>(std::size(path)));
    if(!count || count>=std::size(path)) return false;
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path,-1,out.path,static_cast<int>(sizeof out.path),nullptr,nullptr)) return false;
    out.base=reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll); out.size=info.SizeOfImage;
    return out.base && out.size<=std::numeric_limits<std::uintptr_t>::max()-out.base;
}
#endif
DiagnosticExport Lookup(char const* dll,bool ordinal) {
#if defined(MEVR_DIAGNOSTIC_PAD_BRIDGE_TEST)
    return gDiagnosticPlatform.lookup(dll,ordinal);
#else
    DiagnosticExport e; auto module=GetModuleHandleA(dll); if(!module) return e;
    e.moduleLoaded=true;
    if(!Describe(module,e.requested)) return e;
    auto target=GetProcAddress(module,ordinal?MAKEINTRESOURCEA(100):"XInputGetState");
    if(!target) return e;
    e.found=true; e.target=reinterpret_cast<std::uintptr_t>(target);
    HMODULE owner{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(target),&owner) || !Describe(owner,e.owner)) return e;
    if(e.target<e.owner.base || e.target-e.owner.base>=e.owner.size || !Executable(e.target,e.owner.base)) return e;
    // A process-lifetime detour cannot outlive a dynamically unloaded DLL.
    HMODULE pinned{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(module),&pinned) ||
       !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN|GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(target),&pinned)) return e;
    e.valid=true; return e;
#endif
}
DiagnosticMain ReadMain() {
#if defined(MEVR_DIAGNOSTIC_PAD_BRIDGE_TEST)
    return gDiagnosticPlatform.mainRange();
#else
    auto const base=exe::Base(); auto const image=exe::LiveImage();
    if(!base || image.text.empty() || image.textRva>std::numeric_limits<std::uintptr_t>::max()-base) return {};
    auto const begin=base+image.textRva;
    if(image.text.size()>std::numeric_limits<std::uintptr_t>::max()-begin ||
       reinterpret_cast<std::uintptr_t>(image.text.data())!=begin || !Executable(begin,base)) return {};
    return {base,begin,begin+image.text.size()};
#endif
}
bool MainValid(DiagnosticMain const& m) { return m.base && m.textBegin>=m.base && m.textEnd>m.textBegin; }
bool CallerEligible(std::uintptr_t caller) {
    return MainValid(gDiagnostic.main) && caller>=gDiagnostic.main.textBegin && caller<gDiagnostic.main.textEnd && Executable(caller,gDiagnostic.main.base);
}
diagnostic_pad::Pad ReadPad(XINPUT_GAMEPAD const& p) {
    return {p.wButtons,p.bLeftTrigger,p.bRightTrigger,p.sThumbLX,p.sThumbLY,p.sThumbRX,p.sThumbRY};
}
void WritePad(XINPUT_STATE& state,diagnostic_pad::PollResult const& p) {
    state.dwPacketNumber=p.packet;
    auto& v=state.Gamepad; v.wButtons=p.pad.buttons; v.bLeftTrigger=p.pad.leftTrigger; v.bRightTrigger=p.pad.rightTrigger;
    v.sThumbLX=p.pad.leftX; v.sThumbLY=p.pad.leftY; v.sThumbRX=p.pad.rightX; v.sThumbRY=p.pad.rightY;
}
struct DiagnosticInFlight {
    DiagnosticInFlight() { ++gDiagnosticInFlight; ++gDiagnosticDepth; }
    ~DiagnosticInFlight() { --gDiagnosticDepth; --gDiagnosticInFlight; }
};
DWORD DiagnosticOutput(DWORD index, XINPUT_STATE* state, DWORD result, std::uintptr_t caller, std::uint64_t generation) {
    std::lock_guard lock(gMutex);
    Increment(gDiagnostic.callbacks);
    if (!state) { Increment(gDiagnostic.nullBufferPassthrough); return result; }
    if (index != 0) return result;
    if (!CallerEligible(caller)) { Increment(gDiagnostic.foreignCallerPassthrough); return result; }
    gDiagnostic.lastEligibleCaller = caller;
    if (generation != gDiagnostic.generation) { Increment(gDiagnostic.staleCallbackPassthrough); return result; }
    if (gDiagnostic.installing) return result;
    diagnostic_pad::Physical physical;
    physical.state = result == ERROR_SUCCESS ? diagnostic_pad::PhysicalState::Connected :
        result == ERROR_DEVICE_NOT_CONNECTED ? diagnostic_pad::PhysicalState::Disconnected : diagnostic_pad::PhysicalState::Error;
    if (result == ERROR_SUCCESS) { physical.pad = ReadPad(state->Gamepad); physical.packet = state->dwPacketNumber; }
    // Same mutex as admission/cancellation, held through the actual output write.
    // A callback returning from original after cancellation sees only drain state.
    auto const response = gDiagnosticSource.Poll(index, physical, Now());
    if (!response.replace) return result;
    WritePad(*state, response); Increment(gDiagnostic.outputWrites);
    if (response.pad == diagnostic_pad::Pad{}) Increment(gDiagnostic.neutralWrites);
    return ERROR_SUCCESS;
}
bool DiagnosticReady() {
    return gDiagnostic.optedIn && !gDiagnostic.disabled && !gDiagnostic.faulted &&
        !gDiagnostic.installing && gDiagnostic.hookCount > 0 && MainValid(gDiagnostic.main);
}
void InvalidateDiagnosticCallbacks() {
    if (gDiagnostic.generation == std::numeric_limits<std::uint64_t>::max()) gDiagnostic.faulted = true;
    else ++gDiagnostic.generation;
}
#endif

DWORD Bridge(tGetState original, DWORD index, XINPUT_STATE* state, std::uintptr_t caller = 0) {
#if defined(MEVR_GAME_LE2) && defined(MEVR_LE2_DIAGNOSTIC)
    DiagnosticInFlight inFlight;
    std::uint64_t generation{};
    if (gDiagnosticDepth == 1) { std::lock_guard lock(gMutex); generation = gDiagnostic.generation; }
#else
    (void)caller;
#endif
    DWORD const result = original != nullptr ? original(index, state) : static_cast<DWORD>(ERROR_DEVICE_NOT_CONNECTED);
#if defined(MEVR_GAME_LE2) && defined(MEVR_LE2_DIAGNOSTIC)
    if (gDiagnosticDepth > 1) { ++gDiagnosticRecursive; return result; }
    return DiagnosticOutput(index, state, result, caller, generation);
#else
    if (index != 0 || state == nullptr) {
        return result;
    }
    gCalls.fetch_add(1, std::memory_order_relaxed);
    bool const realPad = result == ERROR_SUCCESS;
    if (realPad) {
        gRealPadFrames.fetch_add(1, std::memory_order_relaxed);
    }
    vr::controllers::State const controllers = vr::controllers::Latest();
    int const yaw = head_tracking::RenderYawOffset();
    if (!controllers.active && (!realPad || yaw == 0)) {
        return result;  // nothing to add or change
    }

    std::lock_guard lock(gMutex);
    XINPUT_GAMEPAD pad = realPad ? state->Gamepad : XINPUT_GAMEPAD{};
    if (controllers.active) {
        pad = gamepad::Merge(pad, gamepad::FromControllers(controllers, gLatches, support_grip::PadOptions()));
        support_grip::SetHeld(gLatches.left == gamepad::LeftGrip::Support);
        gControllerFrames.fetch_add(1, std::memory_order_relaxed);
    }
    gamepad::RotateStick(pad.sThumbLX, pad.sThumbLY, yaw);
    if (std::memcmp(&pad, &gLastPad, sizeof pad) != 0) {
        gLastPad = pad;
        ++gPacket;  // the game only re-reads a pad whose packet number changed
    }
    state->dwPacketNumber = gPacket;
    state->Gamepad = pad;
    return ERROR_SUCCESS;
#endif
}

template <std::size_t N>
DWORD WINAPI Detour(DWORD index, XINPUT_STATE* state) {
#if defined(MEVR_GAME_LE2) && defined(MEVR_LE2_DIAGNOSTIC)
    return Bridge(gSlots[N].original, index, state, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
#else
    return Bridge(gSlots[N].original, index, state);
#endif
}

template <std::size_t... N>
constexpr std::array<tGetState, sizeof...(N)> MakeDetours(std::index_sequence<N...>) {
    return {&Detour<N>...};
}
constexpr auto kDetours = MakeDetours(std::make_index_sequence<kMaxSlots>{});

bool AlreadyHooked(void* target) {
    for (std::size_t i = 0; i < gSlotCount; ++i) {
        if (gSlots[i].target == target) {
            return true;
        }
    }
    return false;
}

bool Hook(hook::Context& init, void* target, std::string name) {
    if (AlreadyHooked(target)) return true;
    if (target == nullptr || gSlotCount >= kMaxSlots) return false;
    Slot& slot = gSlots[gSlotCount];
    slot.name = std::move(name);
    slot.target = target;
    // LEBinkProxy writes `original` before the detour goes live (core/Hook); XInput is polled
    // from any thread. (Bridge still treats a null one as a disconnected pad.)
    if (!hook::Install(init, slot.name.c_str(), target, reinterpret_cast<void*>(kDetours[gSlotCount]),
            reinterpret_cast<void**>(&slot.original))) {
        log::Warn("gamepad: hooking {} failed", slot.name);
        slot = {};
        return false;
    }
    ++gSlotCount;
    log::Info("gamepad: hooked {}", slot.name);
    return true;
}

} // namespace

#if defined(MEVR_GAME_LE2) && defined(MEVR_LE2_DIAGNOSTIC)
bool DiagnosticTryInstall(hook::Context& init, bool explicitOptIn) {
    {
        std::lock_guard lock(gMutex);
        if (!explicitOptIn || gDiagnostic.disabled || gDiagnostic.faulted || gDiagnostic.installing) return false;
        gDiagnostic.optedIn = true; gDiagnostic.installing = true;
        gDiagnostic.main = ReadMain();
        if (!MainValid(gDiagnostic.main)) { gDiagnostic.faulted = true; gDiagnostic.installing = false; return false; }
        strncpy_s(gDiagnostic.backend, init.Backend(), _TRUNCATE);
    }
    // Reuse the existing DLL list, Hook, slots, deduplication and detours. The
    // extra records explain aliases/forwarded exports rather than installing a
    // second hook. Do not hold the output lock while enabling a detour.
    for (auto dll : kDlls) for (bool ordinal : {false,true}) {
        bool seen = false;
        {
            std::lock_guard lock(gMutex);
            for (unsigned i = 0; i < gDiagnostic.recordCount; ++i)
                if (std::strcmp(gDiagnostic.hooks[i].dll,dll) == 0 && gDiagnostic.hooks[i].ordinal100 == ordinal) seen = true;
        }
        if (seen) continue;
        auto const mapping = Lookup(dll,ordinal);
        if (!mapping.moduleLoaded) continue; // retry only after a DLL is loaded by the game
        unsigned record{};
        {
            std::lock_guard lock(gMutex);
            record = gDiagnostic.recordCount++;
            auto& item = gDiagnostic.hooks[record]; strncpy_s(item.dll,dll,_TRUNCATE);
            item.ordinal100 = ordinal; item.mapping = mapping;
            if (!mapping.found && ordinal) { item.status = DiagnosticHookStatus::MissingOptionalExport; continue; }
            if (!mapping.valid) { item.status = DiagnosticHookStatus::InvalidExport; gDiagnostic.faulted = true; continue; }
        }
        auto* target = reinterpret_cast<void*>(mapping.target);
        bool const alias = AlreadyHooked(target);
        std::string const name = std::string(ordinal ? "XInputGetStateEx@" : "XInputGetState@") + dll;
        bool const installed = Hook(init,target,name);
        {
            std::lock_guard lock(gMutex);
            auto& item = gDiagnostic.hooks[record];
            item.status = installed ? (alias ? DiagnosticHookStatus::Alias : DiagnosticHookStatus::Installed) : DiagnosticHookStatus::Failed;
            for (unsigned i = 0; i < gSlotCount; ++i) if (gSlots[i].target == target) {
                item.canonicalSlot = i; item.detour = reinterpret_cast<std::uintptr_t>(kDetours[i]);
                item.original = reinterpret_cast<std::uintptr_t>(gSlots[i].original); break;
            }
            if (!installed || !item.original) { item.status = DiagnosticHookStatus::Failed; gDiagnostic.faulted = true; }
            gDiagnostic.hookCount = static_cast<unsigned>(gSlotCount);
        }
        log::Info("diagnostic pad: {} requested {} at 0x{:X}, export 0x{:X}, mapped owner {} at 0x{:X}, installed {}, alias {}",
            name, mapping.requested.path, mapping.requested.base, mapping.target, mapping.owner.path, mapping.owner.base, installed, alias);
    }
    std::lock_guard lock(gMutex);
    gDiagnostic.installing = false;
    if (gDiagnostic.faulted) { auto s = gDiagnosticSource.Snapshot(); if (s.runOpen) gDiagnosticSource.CancelRun(s.run,Now()); }
    return DiagnosticReady();
}
diagnostic_pad::Admission DiagnosticBeginRun(std::uint64_t run) {
    std::lock_guard lock(gMutex);
    if (!DiagnosticReady() || gDiagnostic.generation == std::numeric_limits<std::uint64_t>::max()) return diagnostic_pad::Admission::InvalidRun;
    auto const result = gDiagnosticSource.BeginRun(run);
    if (result == diagnostic_pad::Admission::Accepted) InvalidateDiagnosticCallbacks();
    return result;
}
diagnostic_pad::Admission DiagnosticPublish(std::uint64_t run, std::uint64_t sequence, diagnostic_pad::Pad p, std::uint32_t duration) {
    std::lock_guard lock(gMutex);
    if (!DiagnosticReady() || gDiagnostic.generation == std::numeric_limits<std::uint64_t>::max()) return diagnostic_pad::Admission::InvalidRun;
    auto const result = gDiagnosticSource.Publish(run,sequence,p,duration,Now());
    if (result == diagnostic_pad::Admission::Accepted) InvalidateDiagnosticCallbacks();
    return result;
}
bool DiagnosticCancelRun(std::uint64_t run) {
    std::lock_guard lock(gMutex);
    bool const cancelled = gDiagnosticSource.CancelRun(run,Now());
    if (cancelled) { Increment(gDiagnostic.cancellations); InvalidateDiagnosticCallbacks(); }
    return cancelled;
}
void DiagnosticDisable() {
    std::lock_guard lock(gMutex); gDiagnostic.disabled = true;
    InvalidateDiagnosticCallbacks();
    auto s = gDiagnosticSource.Snapshot();
    if (s.runOpen && gDiagnosticSource.CancelRun(s.run,Now())) Increment(gDiagnostic.cancellations);
}
DiagnosticObservation DiagnosticSnapshot() {
    std::lock_guard lock(gMutex); auto out = gDiagnostic;
    out.available = DiagnosticReady(); out.source = gDiagnosticSource.Snapshot();
    out.callbacksInFlight = gDiagnosticInFlight.load(); out.recursivePassthrough = gDiagnosticRecursive.load();
    return out;
}
#if defined(MEVR_DIAGNOSTIC_PAD_BRIDGE_TEST)
void testing::Reset(Platform platform) {
    std::lock_guard lock(gMutex);
    if (gDiagnosticInFlight.load()) throw std::logic_error("callback active");
    std::destroy_at(&gDiagnosticSource); std::construct_at(&gDiagnosticSource);
    gDiagnostic = {}; gDiagnosticRecursive = 0; gDiagnosticPlatform = platform;
    for (auto& slot : gSlots) slot = {};
    gSlotCount = 0;
}
unsigned long testing::Poll(unsigned slot, unsigned long index, void* state, std::uintptr_t caller) {
    if (slot >= gSlotCount) throw std::logic_error("no test slot");
    return Bridge(gSlots[slot].original,index,static_cast<XINPUT_STATE*>(state),caller);
}
#endif
#endif

bool TryInstall(hook::Context& init) {
#if defined(MEVR_GAME_LE2) && defined(MEVR_LE2_DIAGNOSTIC)
    (void)init;
    return false; // only DiagnosticTryInstall can opt in this build
#else
    for (char const* dll : kDlls) {
        HMODULE const module = GetModuleHandleA(dll);
        if (module == nullptr) {
            continue;
        }
        Hook(init, reinterpret_cast<void*>(GetProcAddress(module, "XInputGetState")), std::string("XInputGetState@") + dll);
        Hook(init, reinterpret_cast<void*>(GetProcAddress(module, MAKEINTRESOURCEA(100))), std::string("XInputGetStateEx@") + dll);
    }
    return gSlotCount > 0;
#endif
}

bool Uninstall(hook::Context& init) {
#if defined(MEVR_GAME_LE2) && defined(MEVR_LE2_DIAGNOSTIC)
    (void)init; DiagnosticDisable();
    std::lock_guard lock(gMutex);
    return gSlotCount == 0; // installed diagnostic hook code must remain resident
#else
    bool removed = true;
    for (std::size_t i = 0; i < gSlotCount; ++i) {
        if (!hook::Uninstall(init, gSlots[i].name.c_str())) {
            removed = false;  // still live: its slot (and original) must stay as they are
            continue;
        }
        log::Info("gamepad: unhooked {}", gSlots[i].name);
        gSlots[i] = {};
    }
    if (removed) {
        gSlotCount = 0;
    }
    std::lock_guard lock(gMutex);
    gLatches = {};
    gLastPad = {};
    return removed;
#endif
}

void LogStats() {
#if defined(MEVR_GAME_LE2) && defined(MEVR_LE2_DIAGNOSTIC)
    auto const s = DiagnosticSnapshot();
    log::Info("diagnostic pad: {} output writes, {} neutral writes; game consumption unproved", s.outputWrites, s.neutralWrites);
#else
    if (gSlotCount == 0) {
        return;
    }
    unsigned const calls = gCalls.exchange(0);
    unsigned const controllerFrames = gControllerFrames.exchange(0);
    unsigned const realPadFrames = gRealPadFrames.exchange(0);
    log::Info("    gamepad: {} XInput polls of pad 0, {} with motion controllers, {} with a real pad; stick turned by {} units",
        calls, controllerFrames, realPadFrames, head_tracking::RenderYawOffset());
#endif
}

} // namespace mevr::game::gamepad_bridge
