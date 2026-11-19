// SPDX-License-Identifier: MIT
#pragma once
#include "game/Traversal.hpp"

// ME2's native side of traversal, called through game/Traversal's lifecycle and command surface (as ME3's, game/
// Le3Traversal.hpp). Register before the engine hooks. Game thread.
#if defined(MEVR_GAME_LE2)
namespace mevr::game::traversal::le2 {
// The settings are game/Traversal's ([Traversal], the console's overrides), handed over at each call; their one-line text is
// for the status.
bool TryStartup(vr::traversal::Settings const&, std::string const& settingsText);
bool Started();
void Tick(Link const&, vr::traversal::Settings const&, std::string const& settingsText);
bool Moving();
bool Stepping();
std::string StatusJson();
void LogStats();
// slots | near | status | place | go | try | pause: empty when the verb isn't one of ME2's.
std::string Command(std::string const& args, Link const&);
void Shutdown();
} // namespace mevr::game::traversal::le2
#endif
