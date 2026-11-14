// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace mevr::engine::le2_control {

// Immutable process facts. Production obtains these from Win32, never an invitation.
// The explicit constructor permits offline tests with temporary fake files only.
struct ProcessFiles {
    std::uint32_t pid{};
    std::uint64_t startFiletime{};
    std::filesystem::path exe, module;
};

class Endpoint {
public:
    using UtcClock = std::function<std::uint64_t()>;
    Endpoint(std::filesystem::path localAppData, ProcessFiles actual, UtcClock clock = {});
    ~Endpoint();
    Endpoint(Endpoint const&) = delete;
    Endpoint& operator=(Endpoint const&) = delete;
    // Serialized game-thread callback. Every exception disables the endpoint;
    // consumed requests and partial publications remain as recovery evidence.
    // Full file hashes/ownership reads can stall this diagnostic tick. These runs
    // are excluded from performance acceptance; the clock is read AFTER that IO.
    void Poll(std::uint64_t tick, std::uint32_t thread) noexcept;
    bool Attached() const;
    bool Disabled() const;
    std::string const& LastError() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Registers with Le2DiagnosticHost before its first tick; monolithic diagnostic only.
// Neither function issues engine calls, starts XR or consumes shared console files.
bool Start() noexcept;
void Shutdown() noexcept;

} // namespace mevr::engine::le2_control
