// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <deque>
#include <string_view>
#include <vector>

namespace mevr::vr::quest_pacing {
enum class Action { Refresh, RefreshCycle, Performance };
// Unknown tokens fail closed. List order is significant; auto is headset-specific.
std::vector<Action> Actions(std::string_view value, bool streamed);
char const* Name(Action action);
struct Sample {
    double now = 0, interval = 0, work = 0, period = 0;
    bool eligible = false;
    std::uint64_t level = 0; // stable world identity; zero means unknown, never replenishes the budget
    double gpuWork = -1, gpuAge = -1; // seconds; completed GPU span and age at this sample, negative means unavailable
};
enum class Event { None, Entering, Recovering, Recovered, GaveUp };
struct Result {
    Event event = Event::None;
    double rate = 0, workP90 = 0, elapsed = 0;
    unsigned tries = 0;
};
class Detector {
public:
    Result Step(Sample const& sample, unsigned maxTries, bool force = false);
    void Suspend(); // focus/mode/invalid sample: fresh evidence, same level budget and cooldown
private:
    struct Reading { double time, interval, work, period; };
    std::deque<Reading> readings_;
    std::uint64_t level_ = 0;
    unsigned tries_ = 0;
    bool stuck_ = false, gaveUp_ = false;
    double entered_ = 0, lastTry_ = -1e30, healthySince_ = 0;
};

// Ownership of the cached frame prediction. Failed begin keeps the waited ticket; a load must
// finish a begun ticket, never throw it away and wait/begin a second frame over it.
struct FrameSlot {
    enum class State { Empty, Waited, Begun } state = State::Empty;
    bool Pending() const { return state != State::Empty; }
    bool NeedsBegin() const { return state == State::Waited; }
    void Waited() { state = State::Waited; }
    void Begun() { state = State::Begun; }
    void Clear() { state = State::Empty; } // after ownership transferred, or session STOPPING
};
} // namespace mevr::vr::quest_pacing
