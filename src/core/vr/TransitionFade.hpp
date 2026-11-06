// SPDX-License-Identifier: MIT
#pragma once

// A dip from black when the view changes between the world and a cinematic (the owner, 2026-11-06: "is there anything we
// can do to make it smoother"): the game cuts its camera at once, so the switch can't be faded out in advance; the frame
// after it starts black, holds a moment and eases into the new view. Between the world, the 3D cinema screen and a
// cinematic's flat movie (any direction); menus and the other flat panels keep their cut. Game-independent (unit-tested,
// tests/Main.cpp TestTransitionFade); the host draws it (plugin/TransitionFade).
namespace mevr::vr::transition_fade {

enum class View {
    Unknown,     // no frame decided yet (start-up)
    World,       // the head-tracked world
    Screen,      // the 3D cinema screen
    CinemaFlat,  // a cinematic on the flat panel (its movies)
    Other,       // menus, the galaxy map, the front end's loads: not faded
};

// Whether a change from `from` to `to` dips from black.
bool Fades(View from, View to);

inline constexpr double kHoldS = 0.04;  // fully black this long before easing out

class Fade {
public:
    // Each frame: the view shown, the time (seconds), the ease's length (seconds; 0 or less: never fades). Returns the
    // black's opacity, 0..1, to draw over everything.
    float Update(View view, double nowS, float fadeS);
    bool Fading() const { return fading_; }
    unsigned Count() const { return count_; }  // fades started so far
    View Last() const { return last_; }

private:
    View last_ = View::Unknown;
    double startS_ = 0.0;
    bool fading_ = false;
    unsigned count_ = 0;
};

} // namespace mevr::vr::transition_fade
