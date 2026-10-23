// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>
#include <vector>

// Recognising body parts in a skeleton's bone names, whatever the rig's naming convention
// (Hand_R, R_Hand, RightHand, Bip01_R_Hand, wrist_l...). Names are split into lower-case tokens
// at separators and camel-case boundaries, and matched by whole token, so "Handle_R" is not a
// hand and "HeadEnd" is not the head. Engine-agnostic; tested offline.
namespace mevr::ik {

std::vector<std::string> NameTokens(std::string_view name);

// -1: not a hand bone (or no clear side); 0: left; 1: right.
int HandSide(std::string_view boneName);

// The head bone itself (not its end/nub, eyes, jaw...).
bool IsHeadBone(std::string_view boneName);

} // namespace mevr::ik
