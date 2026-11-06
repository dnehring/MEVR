// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// `mevr body` (research/58, the body's scale): Shepard's drawn dimensions against the player's, measured live on foot in
// first person. His key bones (feet, hips, shoulders, elbows, wrists, head, eyes) in tracking space and component space,
// his lowest bone as the floor, the arm segments and the shoulder width, the headset's height above that floor and the
// player's calibration. Reads only (no new engine handles: the skeleton, the view frame, the hand targets). Game thread.
namespace mevr::game::body_probe {

std::string Command(std::string const& args);

} // namespace mevr::game::body_probe
