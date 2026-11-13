// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/ManualCalibration.hpp"

namespace mevr::vr::live_calibration {

enum class Action { None, Done, Cancel };
inline constexpr char const* kAxes[] = {"Out / in", "Up / down", "Forward / back", "Pitch", "Yaw", "Roll"};

// A draft only. The host commits on Done; losing the session/profile or Cancel discards it.
struct Session {
    manual_calibration::Settings draft;
    int target = 1;
    int axis = 3;
    bool armed = false;
    bool axisHeld = false;
    bool resetHeld = false;
    bool cancelHeld = true; // release first; Cancel remains available if the target hand loses tracking

    std::size_t Index() const { return (target == 0 ? 1u : 7u) + static_cast<std::size_t>(axis); }
    int Editor() const { return 1 - target; }
    void Begin(manual_calibration::Settings const& initial, int hand) {
        *this = {};
        draft = initial;
        target = hand == 0 ? 0 : 1;
    }
    Action Tick(controllers::State const& state, float dt, bool eligible) {
        auto const& h = Editor() == 0 ? state.left : state.right;
        if (eligible && state.active && h.gripValid) {
            bool const cancel = h.secondary && !cancelHeld;
            cancelHeld = h.secondary;
            if (cancel) return Action::Cancel;
        } else {
            cancelHeld = true;
        }
        if (!eligible || !state.active || !state.left.gripValid || !state.right.gripValid ||
            !std::isfinite(h.stickX) || !std::isfinite(h.stickY)) {
            armed = false;
            axisHeld = resetHeld = false;
            return Action::None;
        }
        if (!armed) {
            armed = std::abs(h.stickX) < 0.2f && std::abs(h.stickY) < 0.2f && h.trigger < 0.2f &&
                !h.primary && !h.secondary && !h.stickClick;
            return Action::None;
        }
        if (h.primary) return Action::Done;
        if (std::abs(h.stickY) < 0.2f) axisHeld = false;
        if (std::abs(h.stickY) > 0.65f) {
            if (!axisHeld) axis = (axis + (h.stickY > 0 ? 5 : 1)) % 6;
            axisHeld = true;
        } else if (!axisHeld && std::abs(h.stickX) > 0.25f && std::isfinite(dt) && dt > 0) {
            auto const i = Index();
            auto const& field = manual_calibration::kFields[i];
            float const x = std::clamp(h.stickX, -1.0f, 1.0f);
            float const speed = std::copysign((std::abs(x) - 0.25f) / 0.75f, x);
            float const rate = field.angle ? 12.0f : 0.03f; // degrees/s or metres/s
            draft.values[i] = manual_calibration::Bounded(draft.values[i] + speed * rate *
                (h.trigger > 0.5f ? 0.2f : 1.0f) * std::min(dt, 0.05f), field.limit);
        }
        if (h.stickClick && !resetHeld) draft.values[Index()] = 0;
        resetHeld = h.stickClick;
        return Action::None;
    }
};

// Keep tracking for the visible hands/weapon, but reserve all controls for the editor.
inline controllers::State PosesOnly(controllers::State state) {
    for (auto* h : {&state.left, &state.right}) {
        h->trigger = h->squeeze = h->stickX = h->stickY = 0;
        h->stickClick = h->primary = h->secondary = h->menu = false;
        h->triggerTouch = h->thumbTouch = false;
    }
    return state;
}

} // namespace mevr::vr::live_calibration
