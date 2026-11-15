// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

// The arm-length sample (research/18's T pose): both grips' and the head's positions in the renderer's app space, written to
// [Arms] measure* in <GAME>MEVR.ini with the counter [Arms] measureRequest raised last, so the mod (game/HandCalibration)
// reads a whole sample once it sees the counter change. LE1's host writes the same keys through Halcyon's VR hands tab
// (plugin/MevrMenuTab.cpp's WriteArmSample, MevrBridge::TakeArmSample); this is the game-independent writer for a host
// without that tab (LE2: the main menu's Measure and the guided calibration's T pose). Any thread.
namespace mevr::host::arm_sample {

// `head` may be null (not known). `why` names who took it, for the log.
void Write(XrVector3f const& left, XrVector3f const& right, XrVector3f const* head, char const* why);

} // namespace mevr::host::arm_sample
