#pragma once

#include <openxr/openxr.h>

#include <cstdint>

// Motion controller state, read on the render thread (which owns OpenXR) once per frame and
// handed to whichever thread needs it (the game thread's input hooks). Engine-agnostic.
namespace mevr::vr::controllers {

struct Hand {
    float trigger = 0.0f;  // 0..1
    float squeeze = 0.0f;  // grip, 0..1
    float stickX = 0.0f;   // -1..1, + right
    float stickY = 0.0f;   // -1..1, + forward
    bool stickClick = false;
    bool primary = false;    // A (right) / X (left)
    bool secondary = false;  // B (right) / Y (left)
    bool menu = false;       // left hand only on Touch
    bool gripValid = false;
    XrPosef grip{};  // LOCAL space
    bool aimValid = false;
    XrPosef aim{};   // LOCAL space
};

struct State {
    Hand left;
    Hand right;
    // True while the session has input focus and at least one controller is active.
    bool active = false;
    std::uint64_t sequence = 0;  // increments with every published sample
    std::uint64_t timeMs = 0;    // GetTickCount64 when sampled
};

void Publish(State const& state);
// Stores a sample published elsewhere (e.g. by the host, for the hot-reloadable mod), keeping its
// sequence and time so Latest still ages it correctly.
void Mirror(State const& state);
// The latest sample; `active` is forced false if it's older than `maxAgeMs` (VR stopped,
// session lost focus) so callers fall back to the real gamepad.
State Latest(std::uint64_t maxAgeMs = 250);
void Reset();

} // namespace mevr::vr::controllers
