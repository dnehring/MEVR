// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

// A development trace of the game's script (`mevr calltrace <seconds> <word> [word...]`): for that long, every script call
// (UObject::CallFunction, before it runs, with the calling function) and every ProcessEvent (native code calling script,
// after it ran) whose function's path holds one of the words (case-insensitive; none: everything) and none of the `-word`s
// goes to the log as `call trace:`, at most 600 lines. For finding which route the game takes (e.g. what draws a weapon); costs nothing
// while off (one atomic load per call), and while on a name lookup per new function and a hash lookup per call.
namespace mevr::game::call_trace {

std::string Start(double seconds, std::vector<std::string> const& words);
std::string Stop();
void Shutdown();

} // namespace mevr::game::call_trace
