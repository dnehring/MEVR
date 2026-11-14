// SPDX-License-Identifier: MIT
#pragma once

#include "core/Json.hpp"
#include <cstdint>
#include <string>

namespace mevr::host::process_event::diagnostic {
struct Snapshot;
struct TargetInspection;
struct ControllerWatch;
struct WitnessSnapshot;
}

namespace mevr::engine::le2_diagnostic {
#if defined(MEVR_GAME_LE2)
struct ObserverServices {
    host::process_event::diagnostic::TargetInspection (*inspectTarget)(std::uintptr_t) noexcept = nullptr;
    host::process_event::diagnostic::WitnessSnapshot (*observeController)(
        host::process_event::diagnostic::ControllerWatch const&) noexcept = nullptr;
    host::process_event::diagnostic::Snapshot (*readHookSnapshot)() noexcept = nullptr;
};
// Bounded read-only diagnostic after Legacy::Init, on the verified game thread
// after engine tick. The caller supplies the hook-owned thread ID, never an ID
// obtained speculatively at this call site. Zero/wrong thread performs no reads.
//
// Object availability means SDK-based structural checks passed for this sample;
// slot availability only means nonnull pointer values were read. Neither means
// call-target validation, gameplay readiness, or a durable object generation.
// No script calls, engine allocations/object writes, SDK pointer caches or XR.
// Addresses are diagnostic strings, never reusable handles. Native proof pending.
// ConsoleCommand/GetFOVAngle metadata and controller/allocator vtable slots are
// observations only. Executable memory and matching metadata grant no calls.
// Optional host-owned install/chain snapshot avoids a dependency on the live
// hook implementation; omitted provenance is explicitly unavailable. Optional
// providers inspect observed targets and maintain a bounded passive controller
// witness. Invalid current-thread context disarms it; wrong thread calls none.
json::Value Observe(std::uint32_t gameThreadId,
    host::process_event::diagnostic::Snapshot const* hostSnapshot = nullptr,
    ObserverServices const* services = nullptr);

// Independent bounded snapshot of the reachable normal save-menu context.
// No menu calls; descriptor content keys are diagnostic comparisons, not handles
// or proof of a file hash. Incomplete/ambiguous lists cannot select a fixture.
json::Value ObserveSaveMenu(std::uint32_t gameThreadId);

struct ProbeResult {
    std::string status;
    json::Value payload;
};
// One admitted entry per process, hardcoded GetFOVAngle only. Rejections before
// entry do not spend that entry; once entered, errors are uncertain, never retried.
// The native call is deliberately NOT inside the read-only SEH boundary.
ProbeResult ProbeFov(std::uint32_t gameThreadId, ObserverServices const& services);
// Normal UI transitions only, gated by the producer's separate menu-action opt-in.
// Each action admits at most one entry/process; no file/row/index input or loading.
ProbeResult AdvanceSplash(std::uint32_t gameThreadId, ObserverServices const& services);
ProbeResult OpenLoadMenu(std::uint32_t gameThreadId, ObserverServices const& services);
// Host-owned, independently proved file-to-descriptor mapping only. No pathname,
// wire-supplied index or UObject address enters this API. Index is the native
// descriptor identity, never the UI row passed to LoadGame.
struct FrozenDescriptor {
    std::string career, saveName;
    int index{};
    unsigned type{};
};
json::Value ObserveFixtureCandidate(std::uint32_t gameThreadId, ObserverServices const& services,
    FrozenDescriptor const& descriptor);
ProbeResult LoadFixture(std::uint32_t gameThreadId, ObserverServices const& services,
    FrozenDescriptor const& descriptor);
#if defined(MEVR_LE2_OBSERVATION_TEST)
namespace testing {
using FovDispatcher = void (*)(void* controller, void* function, void* parameters);
void ResetFovProbe(FovDispatcher dispatcher);
using MenuDispatcher = void (*)(void* handler, void* function, void* parameters);
void ResetMenuActions(MenuDispatcher dispatcher);
void ResetFixtureLoad(MenuDispatcher dispatcher);
}
#endif
#endif
} // namespace mevr::engine::le2_diagnostic
