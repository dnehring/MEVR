// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// Shepard at the player's size (research/58): the factor the bone writer scales his body by on foot in first person, so his
// eyes are where the view is. `[Body] scale` in ASI\LE1MEVR.ini: auto (the default: the view's standing height above the floor
// over Shepard's standing eye height, both measured live, a slow-falling maximum each, Shepard's only while holstered and not
// crouched, the view's only while the player isn't crouched), off, or a number (0.85..1.25). The factor moves at most 0.1 per
// second. Rules: core/ik/BodyScale. Game thread.
namespace mevr::game::body_scale {

// Arm IK, every tick it drives the body from the controllers on foot in first person: the view's height above Shepard's
// floor (component units, the floor of the bone writer's last unscaled pose) and the view's units per metre. Returns the
// factor to write (1: none).
double Step(double viewAboveFloor, double unitsPerMeter, double deltaSeconds);
// Not driving (third person, the Mako, no headset): the factor eases back to 1 (and Step resumes from there).
double Idle(double deltaSeconds);
// `mevr body scale <s> | auto | off`.
std::string Command(std::string const& args);
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::body_scale
