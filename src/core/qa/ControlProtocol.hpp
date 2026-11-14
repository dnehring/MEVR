// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace mevr::qa::control {
// Wire numbers are decimal strings, never JSON doubles. The process start is
// the exact Windows FILETIME value, not a rounded local-time timestamp.
struct Identity {
    std::string run, lease;
    std::uint32_t pid{};
    std::uint64_t start{};
    std::string exe, host, mod;
    bool operator==(Identity const&) const = default;
};
struct Frame {
    std::string kind; // request or result
    Identity identity;
    std::string id;
    std::uint64_t sequence{}, deadline{}; // UTC Unix milliseconds
    std::string verb, status, payload;
};
inline constexpr std::size_t kMaxPayload = 16384;
inline constexpr std::size_t kMaxFrame = kMaxPayload + 1024;
inline constexpr std::uint64_t kMaxLifetimeMs = 30000;
inline constexpr std::size_t kMaxRequests = 4096;

bool ValidIdentity(Identity const& identity);
std::optional<Frame> Decode(std::string_view bytes, std::string* error = nullptr);
std::optional<std::string> Encode(Frame const& frame);

// Single game-thread owner. Claim BEFORE dispatch; failures after claim must
// produce an error result (uncertain), never release the sequence for retry.
// Rejected means authoritative rejection before dispatch; dispatched records
// transport invocation only, never gameplay success. Error cannot clear pending
// client intent. A fresh Gate requires a fresh lease: retain this gate across
// hot reload or issue a new lease, never reset replay state under the old lease.
// Host startup, lease and file-path checks are the runtime adapter's job.
// probe-fov is a separate empty-input diagnostic opt-in, not console access.
// Its dispatched status requires native return plus host payload validation;
// completed is not a valid semantic result for it. The client retains intent
// after completed/error. Wire decoding retains such frames as evidence.
class Gate {
public:
    explicit Gate(Identity identity, bool allowConsole = false, bool allowProbeFov = false);
    std::optional<std::string> Claim(Frame const& request, std::uint64_t nowUtcMs);
    std::uint64_t LastSequence() const { return sequence_; }
private:
    Identity identity_;
    bool allowConsole_{};
    bool allowProbeFov_{};
    std::uint64_t sequence_{};
    std::set<std::string> ids_;
};
} // namespace mevr::qa::control
