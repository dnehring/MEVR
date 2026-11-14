// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <mutex>

namespace mevr::qa::diagnostic_pad {
// XInput values, not engine addresses, key names or an arbitrary command API.
struct Pad {
    std::uint16_t buttons{};
    std::uint8_t leftTrigger{}, rightTrigger{};
    std::int16_t leftX{}, leftY{}, rightX{}, rightY{};
    bool operator==(Pad const&) const = default;
};
inline constexpr std::uint16_t kButtons = 0xF3FF; // standard XInput, excludes Guide/reserved bits
inline constexpr std::uint32_t kMaxPulseMs = 1000;
inline constexpr std::uint32_t kActivationWaitMs = 5000;
enum class PhysicalState { Connected, Disconnected, Error };
struct Physical { PhysicalState state{PhysicalState::Error}; Pad pad{}; std::uint32_t packet{}; };
enum class Phase { Idle, Published, Consumed, DrainPending, NeutralEmitted };
enum class End { None, Expired, RunCancelled, PhysicalConflict, PhysicalError, ClockRegressed, StartTimedOut };
enum class Admission { Accepted, InvalidRun, RunAlreadyOpen, Busy, InvalidPulse, StaleSequence, ClockRegressed };
struct Observation {
    std::uint64_t run{}, sequence{}, publishedMs{}, expiresMs{};
    // expiresMs stays zero until the first eligible nonconflicting poll starts
    // the hold. Publication only reserves this bounded activation window.
    std::uint64_t activationDeadlineMs{};
    std::uint32_t requestedDurationMs{};
    std::uint64_t pulsePolls{}, firstPulseMs{}, lastPulseMs{}, neutralPolls{}, neutralMs{}, conflictingPolls{};
    Phase phase{Phase::Idle}; End end{End::None};
    Pad requested{};
    bool runOpen{};
    bool physicalObserved{};
    Physical lastPhysical{};
    std::uint64_t lastPhysicalMs{};
};
struct PollResult {
    // False means preserve the original result AND every byte of its state.
    // True means return success with this complete pad/packet to this poll.
    bool replace{};
    Pad pad{};
    std::uint32_t packet{};
};

// Pure bounded source. No hooks, OS/engine calls, pointer handles or allocation.
// All methods serialize primitive state. nowMs uses ONE monotonic clock supplied
// by the adapter, never request-controlled time. A regressing sample fails closed.
class Source {
public:
    Admission BeginRun(std::uint64_t run);
    Admission Publish(std::uint64_t run, std::uint64_t sequence, Pad pad,
                      std::uint32_t durationMs, std::uint64_t nowMs);
    bool CancelRun(std::uint64_t run, std::uint64_t nowMs);
    PollResult Poll(std::uint32_t index, Physical physical, std::uint64_t nowMs);
    Observation Snapshot() const;
private:
    bool Outstanding() const;
    void Stop(End reason);
    bool Time(std::uint64_t nowMs);
    PollResult Emit(Pad pad, std::uint32_t originalPacket);
    mutable std::mutex mutex_;
    Observation state_{};
    std::uint64_t lastClock_{};
    bool haveClock_{}, havePacket_{};
    std::uint32_t packet_{};
    Pad lastEmitted_{};
};
}
