// SPDX-License-Identifier: MIT
#pragma once

// Sprint as a faster run (2026-11-06, the user: the game's sprint "could be uncomfortable given the animation style"; "we
// can still shoot while sprinting, but there is a limit"). The game's sprint (SFXGameModeDefault.StormOn: the storm posture,
// its camera, the gun lowered) is skipped; while the sprint button is held and Shepard runs, the run speed goes up to the
// game's own sprint speed, the gun stays up, and a stamina of our own runs down. Game-independent (unit-tested,
// tests/Main.cpp TestSprint).
namespace mevr::vr::sprint {

struct Settings {
    bool enabled = true;         // [Sprint] style = speed (false: the game's own sprint)
    float staminaS = 6.0f;       // a full stamina lasts this long at speed
    float refillS = 8.0f;        // ...and refills from empty in this long
    float refillDelayS = 1.0f;   // the refill starts this long after the last sprinting tick
    float restartFraction = 0.3f;  // emptied, sprint again only past this much (and a new press)
    bool operator==(Settings const&) const = default;
};

struct State {
    float stamina = 1.0f;      // 0..1
    bool boosting = false;     // this tick runs faster
    bool exhausted = false;    // emptied: waits for restartFraction and a new press
    bool wasHeld = false;
    float sinceBoostS = 1e9f;  // since the last boosting tick
    unsigned emptied = 0;      // for the log
};

// One tick: `held` the sprint button, `moving` Shepard running (not standing, not walking slowly), `dt` seconds. Returns
// whether this tick runs faster.
bool Step(State& state, Settings const& settings, bool held, bool moving, float dt);

// The run speed's factor: the game's sprint speed over its run speed (BioPawn StormSpeed / CombatStormSpeed against its
// ground speed), within 1..2; 1 for anything implausible.
float SpeedFactor(float runSpeed, float sprintSpeed);

} // namespace mevr::vr::sprint
