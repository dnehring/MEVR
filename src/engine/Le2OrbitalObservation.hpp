// SPDX-License-Identifier: MIT
#pragma once
#include "core/Json.hpp"
#include <cstdint>

namespace mevr::engine::le2_diagnostic {
#if defined(MEVR_GAME_LE2)
// Verified game thread only. Current orbital context, no engine calls/writes.
// Source IDs/conditional declarations are not evaluated discovery or save IDs.
json::Value ObserveOrbital(std::uint32_t gameThreadId);
#if defined(MEVR_LE2_OBSERVATION_TEST)
namespace testing { void SetOrbitalSampleHook(void (*hook)()); }
#endif
#endif
}
