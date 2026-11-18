// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Floor.hpp"
#include "core/vr/GalaxyHologram.hpp"

#include <optional>
#include <string>

namespace mevr::ue3 { struct Object; }

// ME2's galaxy map over the CIC's projector (ME1's design, research/51 section 8.1; docs/galaxy-projector-handoff.md step 5):
// ME2's adapter of the sequels' one flow (game/GalaxyProjectorFlow). ME2's facts: the CIC's art is BioA_Nor_110 (its
// GalaxyMapProxy InterpActor_0, the console's SFXPointOfInterest_6 tagged placu_galaxyMap in BioD_Nor_103CIC), the map's modes
// 9 and 10 (engine/GameAdapter). research/le2-galaxy-table.md "The projector flow". Game thread.
namespace mevr::game::le2_galaxy_capture {
using Publish = void (*)(vr::galaxy_holo::Scene const*);
void TryStartup();
void Tick(Publish publish, std::optional<vr::floor::State> const& floor);
void Shutdown(bool exiting);
// `mevr galaxy state | projector | capture on|on-post|on-default|on-ui|off|state` on ME2.
std::string Command(ue3::Object* pc, std::string const& line);
std::string StatusJson();
} // namespace mevr::game::le2_galaxy_capture
