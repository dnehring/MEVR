// SPDX-License-Identifier: MIT
#pragma once
#include <string>

// Game-thread console entry points. Each request rebuilds its host/native context, refuses
// engine-probe/diagnostic-host mode, and checks control ownership immediately before dispatch.
namespace mevr::mod::gameplay_actions {
std::string MeleeCommand(std::string const& args);
std::string TraversalCommand(std::string const& args);
} // namespace mevr::mod::gameplay_actions
