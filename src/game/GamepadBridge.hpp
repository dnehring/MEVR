#pragma once

namespace LESDK {
class Initializer;
}

// Feeds the motion controllers (game/gamepad mapping) to the game as Xbox pad 0, by hooking
// XInputGetState (and the undocumented ordinal-100 XInputGetStateEx) in whichever XInput DLLs
// the game loaded. A real gamepad keeps working alongside. On the render head path the left
// stick is turned by the head's yaw, so forward is where the player looks.
namespace mevr::game::gamepad_bridge {

// Hooks every XInput DLL loaded so far. False until at least one is; call again later.
bool TryInstall(::LESDK::Initializer& init);
void Uninstall(::LESDK::Initializer& init);

// Logs and resets call counts (calls, motion-controller frames, real pad seen).
void LogStats();

} // namespace mevr::game::gamepad_bridge
