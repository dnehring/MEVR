// SPDX-License-Identifier: MIT
#pragma once

#include <optional>

// The game's GUI cursor steered onto the pointer's point (research/56 section 6). LE1's Flash screens move their mouse by raw
// relative mouse motion only (0.82 stage units per count at a 2344-pixel-wide window, measured 2026-11-05); SetCursorPos and posted
// WM_MOUSEMOVE don't move it, posted clicks land wherever it is. So the mod reads where the focused movie's mouse is
// (_root._xmouse/_ymouse, stage units) and injects the relative motion that closes the gap, one move at a time: after a move it
// waits until the movie's mouse has moved (or a few ticks), learns the gain from what the move did, then sends the next.
namespace mevr::vr::gui_cursor {

struct Settings {
    float gain = 0.82f;          // stage units per count to start from (measured)
    float minGain = 0.05f;       // the learned gain's bounds
    float maxGain = 20.0f;
    float learn = 0.5f;          // how far a move's measured gain pulls the estimate (0..1)
    float approach = 0.9f;       // the share of the gap one move closes
    float onTarget = 0.6f;       // stage units: close enough, nothing sent
    int maxCounts = 400;         // a move's counts per axis at most
    int waitTicks = 4;           // ticks to wait for a move to show before sending another
};

// One axis pair of counts to inject this tick (0, 0 for none).
struct Move {
    int dx = 0;
    int dy = 0;
};

class Steer {
public:
    explicit Steer(Settings const& settings = Settings{});
    // One tick: the movie's mouse now and where it should be (stage units); nullopt target: nothing to do (forgets the wait).
    Move Step(float mouseX, float mouseY, std::optional<float> targetX, std::optional<float> targetY);
    void Reset();
    float Gain(int axis) const { return gain_[axis]; }
    bool OnTarget() const { return onTarget_; }

private:
    Settings settings_;
    float gain_[2];
    bool waiting_ = false;       // a move was sent and hasn't shown yet
    int waited_ = 0;
    int sent_[2] = {0, 0};       // the move waited for
    float from_[2] = {0.0f, 0.0f};  // the movie's mouse when it was sent
    bool onTarget_ = false;
};

} // namespace mevr::vr::gui_cursor
