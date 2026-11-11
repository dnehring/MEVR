// SPDX-License-Identifier: MIT
#pragma once

#include "ue3/Types.hpp"

#include <optional>
#include <string_view>

namespace mevr::hook {
class Context;
}

// An engine name from its text, through the engine's own constructor (FName::FName(const TCHAR*, EFindName), LE1 0xD1150,
// engine/Addresses.hpp) in find-only mode: an existing name or nothing; it never adds one. For calling script functions that
// take a name no object of the mod's carries (research/69 "People walking to their marks": BioActorBehavior.AbilityEnable's
// subtype 'ALL'). LE1 only (no address elsewhere: Find answers nothing).
namespace mevr::engine::name_lookup {

// The constructor's address, checked to be a function start. Game thread, at startup.
bool Resolve(hook::Context& init);
// The name, or nothing (not resolved, not an existing name, or the call faulted). Game thread.
std::optional<ue3::Name> Find(std::wstring_view text);

} // namespace mevr::engine::name_lookup
