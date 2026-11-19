// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace mevr::engine::bone_pose {
struct Request;
}
namespace mevr::ik {
struct Affine;
}
namespace mevr::ue3 {
struct Object;
}

// Hand contact with other characters, every game (research/73; the owner 2026-11-18: the hand shouldn't go through people,
// the design "the drawn hand stops at their body and slides along it, with a buzz; nobody is pushed"). Each tick arm IK drives
// the hands first person, the people near the player (every other visible BioPawn within [HandContact] rangeM of Shepard,
// nearest first, at most maxPeople) become capsules from their own bones (core/ik/HandContact's PartsFor, cached per mesh
// asset), put into arm IK's request in its component space; engine/BonePose pushes each hand's final goal out of them before
// the arm is solved. The controller is untouched: the aim ray, the shots and the holsters follow it as before. The hand that
// touches buzzes (a click on contact, a hum while pressed, stronger the deeper the controller goes). Settings `[HandContact]`
// in `ASI\<GAME>MEVR.ini` (appended once): on, handRadiusM, rangeM, maxPeople, haptic. Logs `hand contact:`; snapshot
// `"mod"."handContact"`; console `mevr contact [on|off|auto|people]`. Game thread.
namespace mevr::game::hand_contact {

// Arm IK (game/arm_ik's Drive), once the hands' goals are set and before the request is published: fills request.contact
// (or leaves it off). `firstPerson`: the hands follow the controllers in the view (not the desktop test, not third person).
void Fill(engine::bone_pose::Request& request, ue3::Object* playerPawn, ik::Affine const& worldToComponent, bool firstPerson);

// Each mod tick after arm IK: the pulses for the writer's last contact. The host's haptic functions (null: none).
struct Haptics {
    void (*pulse)(int hand, float seconds, float amplitude) = nullptr;
    void (*pulseHz)(int hand, float seconds, float amplitude, float frequencyHz) = nullptr;
    void (*continuous)(int hand, float amplitude, float frequencyHz, float seconds) = nullptr;
};
void Tick(double deltaSeconds, Haptics const& haptics);

// `mevr contact [on|off|auto|people]`.
std::string Command(std::string const& args);
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::hand_contact
