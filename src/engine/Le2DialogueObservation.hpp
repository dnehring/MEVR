// SPDX-License-Identifier: MIT
#pragma once
#include "core/Json.hpp"
#include <cstdint>
namespace mevr::engine::le2_diagnostic {
#if defined(MEVR_GAME_LE2)
// Verified game-thread, direct guarded reads only. Snapshot, not event history,
// legal-input determination, accepted reply proof or durable identity.
json::Value ObserveDialogue(std::uint32_t gameThreadId);
#if defined(MEVR_LE2_OBSERVATION_TEST)
namespace testing { void SetDialogueSampleHook(void (*hook)()); }
#endif
#endif
}
