// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <optional>
#include <cstdint>

// Shepard at the player's size (research/58): the factor the bone writer scales his body by on foot in first person, so his
// eyes match the player's standing height. `[Body] scale`: auto (calibrated standing eye, else frozen STAGE standing,
// over Shepard's frozen standing eye above the cylinder floor; both physical and game crouch excluded), off, or a number
// (0.85..1.25). Without floor lock the legacy filtered view/lowest-bone reference remains. At most 0.1 per second.
// Rules: core/ik/BodyScale and core/vr/FloorLock. Game thread.
namespace mevr::game::body_scale {

// Arm IK, every tick it drives the body from the controllers on foot in first person: the view's height above Shepard's
// floor (component units, the floor of the bone writer's last unscaled pose) and the view's units per metre. Returns the
// factor to write (1: none). With floor lock, pass Shepard's unscaled eyes above the pawn floor in metres and skeleton identity.
double Step(double viewAboveFloor, double unitsPerMeter, double deltaSeconds,
    std::optional<double> shepardPawnEyeM = std::nullopt, std::uintptr_t skeleton = 0);
// Not driving (third person, the Mako, no headset): the factor eases back to 1 (and Step resumes from there).
double Idle(double deltaSeconds);
// Shared seated reference for floor lock and body slots (never the live seated eye height).
std::optional<double> StandingEyeHeight();
// Shepard's own standing eye height above his floor, unscaled, metres (the reference the factor is measured against); nullopt
// until measured.
std::optional<double> ShepardStandingEyeM();
// `mevr body scale <s> | auto | off`.
std::string Command(std::string const& args);
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::body_scale
