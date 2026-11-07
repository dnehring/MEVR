// SPDX-License-Identifier: MIT
#pragma once
#include <string>

namespace mevr::game::screen_reticle {
// Game thread only. The host request expires when XR frames stop.
void Tick(bool requested);
std::string Command();
void Shutdown(bool exiting);
} // namespace mevr::game::screen_reticle
