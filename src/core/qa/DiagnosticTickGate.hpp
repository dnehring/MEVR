// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <mutex>

namespace mevr::qa {

// Engine-independent dispatch policy. Caller establishes that the first tick comes from the
// host's engine-tick hook. A thread ID is an ownership check, not an engine liveness proof.
class DiagnosticTickGate {
public:
    using Callback = void (*)(float);
    enum class Result { NoConsumer, Called, WrongThread, Reentrant };

    bool Register(Callback callback) {
        std::lock_guard lock(mutex_);
        if (!callback || callback_ || thread_ != 0) return false;
        callback_ = callback;
        return true;
    }

    Result Tick(std::uint32_t thread, float deltaSeconds) {
        Callback callback;
        {
            std::lock_guard lock(mutex_);
            if (!thread || (thread_ && thread_ != thread)) return Result::WrongThread;
            if (running_) return Result::Reentrant;
            thread_ = thread;
            callback = callback_;
            if (!callback) return Result::NoConsumer;
            running_ = true;
        }
        // Never hold the gate lock while executing a consumer; reset/re-entry must fail rather
        // than deadlock. C++ exceptions restore the running flag and propagate to the caller.
        struct Finish {
            DiagnosticTickGate& gate;
            ~Finish() { std::lock_guard lock(gate.mutex_); gate.running_ = false; }
        } finish{*this};
        callback(deltaSeconds);
        return Result::Called;
    }

    bool Reset() {
        std::lock_guard lock(mutex_);
        if (running_) return false;
        callback_ = nullptr;
        thread_ = 0;
        return true;
    }

private:
    std::mutex mutex_;
    Callback callback_ = nullptr;
    std::uint32_t thread_ = 0;
    bool running_ = false;
};

} // namespace mevr::qa
