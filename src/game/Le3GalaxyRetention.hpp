// SPDX-License-Identifier: MIT
#pragma once
#include <string>

namespace mevr::hook { class Context; }
namespace mevr::ue3 { struct Object; }
namespace mevr::game::le3_galaxy_retention {
// A bounded diagnostic until the projector renderer can own the same lifecycle.
// Uses the engine's streaming requests, never plot states or streamlevelin.
void Startup(hook::Context& hooks);
bool Armed();
bool Active(); // current map, same live scope; game thread only
bool Shutdown(hook::Context& hooks, bool exiting);
std::string Command(ue3::Object* controller, std::string const& action);
}
