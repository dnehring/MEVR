// SPDX-License-Identifier: MIT
#pragma once

#include "core/Json.hpp"
#include <cstdint>

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
#endif
} // namespace mevr::engine::le2_diagnostic
