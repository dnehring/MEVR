// SPDX-License-Identifier: MIT
#pragma once

// The push-to-talk gesture (research/54 "Push-to-talk release makes a 300 ms budget realistic"): the "radio" of Onward and
// the speak-to-shoulder of the HCI study, the left hand raised to the ear with the grip squeezed; letting go ends the order.
// A squeeze that began within radioRadiusM of the ear starts a listen (the mod claims that squeeze before the support grip,
// the grenade slot and the flat orders' gesture see it); the hand may then go anywhere; letting go, a blip shorter than
// minHoldS, a hold longer than maxHoldS or the gesture becoming unusable (a menu, the Mako) ends it. Unit-tested.
namespace mevr::vr::voice_gesture {

struct Settings {
    bool enabled = true;
    float radioRadiusM = 0.20f;  // the grip this close to the ear point starts a listen
    float minHoldS = 0.15f;      // shorter: a blip, nothing was said
    float maxHoldS = 15.0f;      // longer: ended by itself
};

struct Input {
    float dt = 0.0f;
    bool usable = false;      // on foot in VR with a tracked left controller and the recognizer ready
    bool newSqueeze = false;  // a left grip squeeze began this tick that nobody else claimed
    bool squeezed = false;    // the left grip is held
    float handToEarM = 1.0f;  // the left grip's distance from the ear point
};

struct Step {
    bool started = false;
    bool ended = false;      // the player let go (or the hold ran out): the final transcript is due
    bool blip = false;       // with ended: too short to have said anything
    bool cancelled = false;  // the gesture stopped being usable mid-listen
    float heldS = 0.0f;
};

struct Machine {
    bool listening = false;
    float heldS = 0.0f;
    Step Advance(Input const& in, Settings const& s);
    void Reset();
};

}  // namespace mevr::vr::voice_gesture
