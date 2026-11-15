// SPDX-License-Identifier: MIT
#pragma once
#include "ue3/Types.hpp"
#include <cstdint>
#include <string>

// LE3's GFx movie manager replaces BioSFManager/BioSFPanel. Game thread only.
// Contracts: installed SFXGame.pcc + LE3 SDK metadata; research/le3-native-gui.md.
namespace mevr::game::le3_gui {
ue3::Object* Manager();
ue3::Object* FocusMovie();
ue3::Object* Browser(ue3::Object* manager);
std::string Tag(ue3::Object* movie);
bool PageAllowed(ue3::Object* browser, std::uint8_t type);
}
