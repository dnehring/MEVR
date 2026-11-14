// SPDX-License-Identifier: MIT
#pragma once

#include "core/Json.hpp"
#include <cstdint>

namespace mevr::engine::le2_diagnostic {
#if defined(MEVR_GAME_LE2)
// Bounded read-only diagnostic after Legacy::Init, on the verified game thread
// after engine tick. The caller supplies the hook-owned thread ID, never an ID
// obtained speculatively at this call site. Zero/wrong thread performs no reads.
//
// Availability means SDK-based structural checks passed for this sample, NOT
// reflection-layout validation, save/load/gameplay readiness, or a durable object
// generation. No script calls, engine allocations, writes, caching or XR work.
// Addresses are diagnostic strings, never reusable handles. Native proof pending.
// ConsoleCommand metadata reports bounded reflection reads and an SDK-layout
// comparison only; a matching schema never enables or proves native dispatch.
json::Value Observe(std::uint32_t gameThreadId);
#endif
} // namespace mevr::engine::le2_diagnostic
