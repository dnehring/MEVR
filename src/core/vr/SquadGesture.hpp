// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/SquadContext.hpp"

#include <cstdint>

// The flat squad orders by gesture (research/54 "The flat orders"; the user, 2026-11-04: the game's own orders must
// work in VR without voice). A LEFT grip squeeze that nobody else claims (not the support grip's zone, not the grenade
// slot, not a hot gun's receiver) points an order: while it's held the left aim ray is the order's pointer, and
// letting go gives it to what the ray ends on: an enemy group = attack, the ground farther than `followWithinM` =
// move there, the ground near Shepard, a squadmate, or nothing (the sky) = follow / regroup. A squadmate the ray
// pointed at when the squeeze began makes the order theirs alone; otherwise it's for everyone. The rules only
// (unit-tested in tests/Main.cpp); game/SquadOrders reads the hands and calls the game.
namespace mevr::vr::squad_gesture {

struct Settings {
    bool enabled = true;
    float followWithinM = 2.0f;  // a point this close to Shepard means "on me"
    float minHoldS = 0.08f;      // a shorter squeeze is a blip, not an order
    float maxHoldS = 8.0f;       // held longer: called off (the hand is doing something else)
    float enemyConeDeg = 8.0f;   // the ray within this of an enemy: that group
    float memberConeDeg = 12.0f; // the ray within this of a squadmate: that squadmate
};

enum class Hit : std::uint8_t { None, Enemy, Ground, Squadmate };
char const* Name(Hit hit);

struct Input {
    double dt = 0.0;
    bool usable = false;       // on foot, the left controller tracked, a squad present
    bool newSqueeze = false;   // a new left squeeze this step that no other feature claimed
    bool squeezed = false;     // the left grip is down
    int pointedMember = -1;    // the squadmate the left ray points at now (-1: none)
    Hit hit = Hit::None;       // what the ray ends on now
    int hitGroup = 0;          // Enemy: the group's id
    float hitDistanceM = 0.0f; // Ground: how far from Shepard
};

struct Order {
    bool given = false;
    squad_context::Order order = squad_context::Order::None;
    int member = -1;    // the squadmate's index in the party (-1: everyone)
    int group = 0;      // attack: the group
    bool toPoint = false;  // move: to the ray's hit point
};

struct Step {
    bool started = false;    // a squeeze began pointing an order
    bool cancelled = false;  // the hold ended without an order
    char const* why = "";    // ...because
    bool hitChanged = false; // what the ray ends on changed while holding (feedback)
    Order order;
};

struct Machine {
    bool holding = false;
    double heldS = 0.0;
    int member = -1;        // the squadmate pointed at when the squeeze began
    Hit lastHit = Hit::None;
    int lastGroup = 0;
    Step Advance(Input const& in, Settings const& settings);
    void Reset();
};

}  // namespace mevr::vr::squad_gesture
