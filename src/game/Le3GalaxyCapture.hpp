// SPDX-License-Identifier: MIT
#pragma once
#include "core/vr/Floor.hpp"
#include "core/vr/GalaxyHologram.hpp"
#include <optional>
#include <string>
namespace mevr::ue3 { struct Object; }
namespace mevr::game::le3_galaxy_capture {
using Publish = void (*)(vr::galaxy_holo::Scene const*);
void Tick(Publish publish, std::optional<vr::floor::State> const& floor);
void Shutdown(bool exiting);
std::string Command(ue3::Object* pc, std::string const& action);
}
