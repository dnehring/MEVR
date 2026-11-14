// SPDX-License-Identifier: MIT
#pragma once
#include "core/Json.hpp"
#include <cstdint>

namespace mevr::engine::le2_diagnostic {
#if defined(MEVR_GAME_LE2)
// Single verified game-thread owner. Read-only structural samples; no calls,
// reload eligibility, event counts, durable identity or persistence proof.
json::Value ObserveWeapon(std::uint32_t gameThreadId);
#if defined(MEVR_LE2_OBSERVATION_TEST)
namespace testing { void SetWeaponSampleHook(void (*hook)()); }
#endif
#endif
}
