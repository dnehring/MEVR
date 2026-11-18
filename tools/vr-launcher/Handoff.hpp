// SPDX-License-Identifier: MIT
#pragma once

// The overlay must release its laser input before waiting for OpenXR FOCUSED. Rendering and focus are separate
// facts. A Steam request being accepted, an old log, or another scene application's frames prove neither.
namespace mevr::launcher {
enum class Phase { Waiting, Released, Complete, Failed };
struct Observation {
    double seconds = 0;
    bool gameRunning = false;
    bool gameRendering = false; // identified executable owns the compositor's latest changing frames
    bool focused = false;       // latest state in this launch's OpenXR log is FOCUSED
    bool dashboard = false;
};
class Handoff {
public:
    Phase Step(Observation const& o) {
        if (phase_ == Phase::Complete || phase_ == Phase::Failed) return phase_;
        if (o.gameRunning) { seen_ = true; goneSince_ = -1; }
        else if (seen_ && goneSince_ < 0) goneSince_ = o.seconds;
        // Steam/EA can briefly replace a process during authentication. Match Setup's 20-second gone grace.
        if ((goneSince_ >= 0 && o.seconds - goneSince_ >= 20) || o.seconds >= 120) return phase_ = Phase::Failed;
        if (phase_ == Phase::Waiting) {
            if (!o.gameRendering) stableSince_ = -1;
            else if (stableSince_ < 0) stableSince_ = o.seconds;
            else if (o.seconds - stableSince_ >= 0.25) phase_ = Phase::Released;
        } else if (o.gameRendering && o.focused && !o.dashboard) {
            phase_ = Phase::Complete;
        }
        return phase_;
    }
private:
    Phase phase_ = Phase::Waiting;
    bool seen_ = false;
    double stableSince_ = -1;
    double goneSince_ = -1;
};
} // namespace mevr::launcher
