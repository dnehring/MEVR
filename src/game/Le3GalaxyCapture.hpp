// SPDX-License-Identifier: MIT
#pragma once
#include "core/vr/Floor.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include <optional>
#include <string>
namespace mevr::ue3 { struct Object; }
// ME3's galaxy map over the CIC's projector (ME1's design, research/51 section 8.1; docs/galaxy-projector-handoff.md): ME3's
// adapter of the sequels' one flow (game/GalaxyProjectorFlow, as ME2's game/Le2GalaxyCapture). ME3's facts: the CIC's art is
// BioA_Nor_210CIC, kept loaded through the map by the room's retention (game/Le3GalaxyRetention: the map's entry unloads it),
// the map's own level BioA_Nor_203aGalaxyMap, its modes 11 and 12 (engine/GameAdapter). research/le3-galaxy.md. Game thread.
namespace mevr::game::le3_galaxy_capture {
using Publish = void (*)(vr::galaxy_holo::Scene const*);
// The Camera.UpdateCamera post-handler (before the ProcessEvent hook goes in).
void TryStartup();
void Tick(Publish publish, std::optional<vr::floor::State> const& floor);
void Shutdown(bool exiting);
// `mevr galaxy capture <action>` (game/GalaxyProjectorFlow's Command).
std::string Command(ue3::Object* pc, std::string const& action);
// `mevr galaxy ray ...` (game/GalaxyRay).
std::string RayCommand(ue3::Object* pc, std::string const& args);
std::string StatusJson();
}
