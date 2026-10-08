#pragma once

#include "core/ik/Rigid.hpp"

// Hand angle trims for arm IK, from <GAME>MEVR.ini next to the host ASI ([Hands] handPitch,
// handYaw, handRoll, degrees; written with defaults if missing). Re-read whenever the file changes,
// so they can be tuned while the game runs. One set for both hands, mirrored for the left (yaw and
// roll flip sign) so it stays symmetric. Game thread.
namespace mevr::game::hand_calibration {

// The trim for one hand (0 left, 1 right), in the grip's own axes (ik::GripTrim).
ik::Quat Trim(int side);
// Checks the file at most once a second; logs when the values change.
void Refresh();

} // namespace mevr::game::hand_calibration
