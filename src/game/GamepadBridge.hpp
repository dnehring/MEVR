// SPDX-License-Identifier: MIT
#pragma once
#if defined(MEVR_GAME_LE2) && defined(MEVR_LE2_DIAGNOSTIC)
#include "core/qa/DiagnosticPad.hpp"
#include <array>
#include <cstdint>
#endif

namespace mevr::hook {
class Context;
}

// Feeds the motion controllers (game/gamepad mapping) to the game as Xbox pad 0, by hooking
// XInputGetState (and the undocumented ordinal-100 XInputGetStateEx) in whichever XInput DLLs
// the game loaded. A real gamepad keeps working alongside. On the render head path the left
// stick is turned by the head's yaw, so forward is where the player looks.
namespace mevr::game::gamepad_bridge {

// Hooks every XInput DLL loaded so far. False until at least one is; call again later.
bool TryInstall(hook::Context& init);
// False: a hook couldn't be removed, so this DLL must stay loaded.
bool Uninstall(hook::Context& init);

// Logs and resets call counts (calls, motion-controller frames, real pad seen).
void LogStats();

#if defined(MEVR_GAME_LE2) && defined(MEVR_LE2_DIAGNOSTIC)
namespace diagnostic_pad = qa::diagnostic_pad;
struct DiagnosticModule { std::uintptr_t base{}; std::uint32_t size{}; char path[1024]{}; };
struct DiagnosticExport {
    bool moduleLoaded{}, found{}, valid{};
    std::uintptr_t target{};
    DiagnosticModule requested{}, owner{};
};
struct DiagnosticMain { std::uintptr_t base{}, textBegin{}, textEnd{}; };
enum class DiagnosticHookStatus { MissingOptionalExport, InvalidExport, Installed, Alias, Failed };
struct DiagnosticHook {
    char dll[32]{}; bool ordinal100{};
    DiagnosticHookStatus status{DiagnosticHookStatus::Failed};
    DiagnosticExport mapping{};
    std::uintptr_t detour{}, original{};
    unsigned canonicalSlot{};
};
struct DiagnosticObservation {
    bool optedIn{}, disabled{}, installing{}, available{}, faulted{};
    DiagnosticMain main{};
    char backend[64]{};
    unsigned hookCount{}, recordCount{};
    std::array<DiagnosticHook,10> hooks{};
    std::uint64_t callbacks{}, recursivePassthrough{}, foreignCallerPassthrough{}, nullBufferPassthrough{}, staleCallbackPassthrough{};
    std::uint64_t generation{}; // invalidates callbacks started before admission/cancellation changes
    std::uint64_t outputWrites{}, neutralWrites{}, cancellations{};
    std::uint32_t callbacksInFlight{};
    std::uintptr_t lastEligibleCaller{};
    diagnostic_pad::Observation source{};
};
// Same bridge slots and detours; explicit opt-in, loaded DLLs only. Never install
// another XInput owner. Partial failure leaves diagnostic admission unavailable.
bool DiagnosticTryInstall(hook::Context& init, bool explicitOptIn);
diagnostic_pad::Admission DiagnosticBeginRun(std::uint64_t run);
diagnostic_pad::Admission DiagnosticPublish(std::uint64_t run, std::uint64_t sequence,
                                          diagnostic_pad::Pad pad, std::uint32_t durationMs);
bool DiagnosticCancelRun(std::uint64_t run);
DiagnosticObservation DiagnosticSnapshot();
void DiagnosticDisable(); // stop admission; retain process-lifetime hooks/drain
#if defined(MEVR_DIAGNOSTIC_PAD_BRIDGE_TEST)
namespace testing {
struct Platform {
    DiagnosticExport (*lookup)(char const*, bool){};
    DiagnosticMain (*mainRange)(){};
    bool (*executableInModule)(std::uintptr_t, std::uintptr_t){};
    std::uint64_t (*nowMs)(){};
};
void Reset(Platform platform); // no active callbacks; synthetic tests only
unsigned long Poll(unsigned slot, unsigned long index, void* xinputState, std::uintptr_t caller);
}
#endif
#endif

} // namespace mevr::game::gamepad_bridge
