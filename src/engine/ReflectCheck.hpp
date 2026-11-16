// SPDX-License-Identifier: MIT
#pragma once

namespace mevr::hook {
class Context;
}

// The transition's proof (research/34 section 5): while LExSDKv2 and our reflection layer (ue3/)
// both exist, resolve every class, field, parameter and function the mod uses through ours and
// compare it with LExSDKv2's compiled offsets and its own lookups. Logs
// "reflect: N of M checks agree; mismatches: ..." and writes the whole table to
// %LOCALAPPDATA%\MEVR\reflect-check.json. Once per mod load, after the script packages are in and
// ue3::runtime::Init ran. Every check is guarded: a wrong layout logs a fault, it never crashes.
// Development cross-checks are available for all three game adapters.
namespace mevr::engine::reflect_check {

// `context`: the mod's hook context (its loader interface makes LExSDKv2's resolver for the reference side).
// True only when actual SDK comparisons ran and none mismatched.
bool Run(hook::Context& context);

} // namespace mevr::engine::reflect_check
