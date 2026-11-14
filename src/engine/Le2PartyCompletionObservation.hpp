// SPDX-License-Identifier: MIT
#pragma once
#include "core/Json.hpp"
#include "engine/Le2InputContext.hpp"

namespace mevr::engine::le2_diagnostic {
#if defined(MEVR_GAME_LE2)
// Read-only, verified game thread only. Revalidates the supplied current context;
// never feeds pulse admission, calls an engine function or retains object pointers.
json::Value ObservePartyCompletion(std::uint32_t gameThreadId, input_context::Context const&);
#if defined(MEVR_LE2_OBSERVATION_TEST)
namespace party_completion_testing { void SetSampleHook(void (*hook)()); }
#endif
#endif
}
