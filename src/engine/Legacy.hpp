// SPDX-License-Identifier: MIT
#pragma once

// The LE2 diagnostic host's bridge to LExSDKv2 (MEVR_LE2_DIAGNOSTIC: an isolated development build, never a players'
// build), in our layer's types: the globals its observers (engine/Le2DiagnosticObservation) read through LExSDKv2's
// own headers, and the three reads they compare with.
//
// Until T04 (docs/trilogy-plan.md, 2026-11-16) this file was also LE3's engine access and LE2's fallback while its
// reflection layer was off (head tracking, the console request, the reflection fix, hand aim's aim query, the world).
// Those all read through our reflection layer (ue3/) on every game now, and stand down with a log line when the layer
// is off: LExSDKv2 has no licence, so no players' build may link it. Nothing outside the diagnostic host and
// engine/ReflectCheck's reference side includes LExSDKv2.
//
// Game thread only.

#include "ue3/Types.hpp"

#include <cstdint>

namespace mevr::hook {
class Context;
}

namespace mevr::engine::legacy {

#if defined(MEVR_GAME_LE2)

// Resolves the LExSDKv2 globals the observers read (GMalloc, GObjObjects, the name pools, GEngine, GWorld) with
// LExSDKv2's own resolver, built here from the hook context's loader interface. Mod startup; false (logged) if one
// fails.
bool Init(hook::Context& context);

// *GWorld, as an identity.
void const* World();
// GObjObjects' count.
std::int32_t ObjectCount();
// GEngine->GamePlayers[0]->Actor, or nullptr.
ue3::Object* LocalPlayerController();

#endif

} // namespace mevr::engine::legacy
