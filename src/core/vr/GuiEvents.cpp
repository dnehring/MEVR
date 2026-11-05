// SPDX-License-Identifier: MIT
#include "core/vr/GuiEvents.hpp"

#include <algorithm>
#include <cstdlib>

namespace mevr::vr::gui_events {

namespace {

bool IsButton(std::uint8_t event) {
    return event == kLeftDown || event == kLeftUp;
}

} // namespace

void Queue::Push(std::uint8_t event) {
    if (events_.size() >= kCapacity) {
        // Full: drop the oldest move or wheel notch; buttons stay (a down without its up would hold the screen's button).
        auto const droppable = std::find_if(events_.begin(), events_.end(), [](std::uint8_t e) { return !IsButton(e); });
        if (droppable == events_.end()) {
            return;
        }
        events_.erase(droppable);
    }
    events_.push_back(event);
}

void Queue::Move() {
    std::lock_guard lock(mutex_);
    if (!events_.empty() && events_.back() == kMouseMove) {
        return;  // one move in a row is enough: the screen reads the cursor's place when it gets it
    }
    Push(kMouseMove);
}

void Queue::Button(bool down) {
    std::lock_guard lock(mutex_);
    Push(down ? kLeftDown : kLeftUp);
}

void Queue::Wheel(int notches) {
    std::lock_guard lock(mutex_);
    for (int i = 0; i < std::abs(notches) && i < static_cast<int>(kCapacity); ++i) {
        Push(notches > 0 ? kWheelUp : kWheelDown);
    }
}

std::uint32_t Queue::Take(std::uint8_t* out, std::uint32_t capacity) {
    std::lock_guard lock(mutex_);
    std::uint32_t taken = 0;
    while (taken < capacity && !events_.empty() && out != nullptr) {
        out[taken++] = events_.front();
        events_.pop_front();
    }
    return taken;
}

std::size_t Queue::Size() const {
    std::lock_guard lock(mutex_);
    return events_.size();
}

void Queue::Clear() {
    std::lock_guard lock(mutex_);
    events_.clear();
}

void Scheduler::Add(std::uint8_t const* events, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count && events != nullptr; ++i) {
        pending_.push_back(events[i]);
    }
}

std::vector<std::uint8_t> Scheduler::Tick() {
    std::vector<std::uint8_t> out;
    bool moved = false;
    while (!pending_.empty() && pending_.front() == kMouseMove) {
        pending_.pop_front();
        moved = true;
    }
    if (moved) {
        out.push_back(kMouseMove);
    }
    if (!pending_.empty()) {
        if (!moved) {
            out.push_back(kMouseMove);  // the place the button or notch is for
        }
        out.push_back(pending_.front());
        pending_.pop_front();
    }
    return out;
}

} // namespace mevr::vr::gui_events
