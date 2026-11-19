// SPDX-License-Identifier: MIT
#pragma once
#include "game/Traversal.hpp"

namespace mevr::game::traversal::le3 {
// Called through the existing Traversal lifecycle/command surface. Register before engine hooks.
bool TryStartup();
void Tick(Link const&, vr::traversal::Settings const&);
bool Moving();
bool FloorMoving();  // Moving, but not an evade (a roll keeps its floor)
std::string StatusJson();
std::string Command(std::string const&, Link const&);
void Shutdown();
} // namespace mevr::game::traversal::le3
