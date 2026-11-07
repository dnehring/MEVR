// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>

namespace mevr::host::quest_pacing {
void PresentBegin();
void PresentEnd();
// CPU cycles spent in runtime calls are excluded even when the compositor blocks there.
class RuntimeCall {
public:
    RuntimeCall();
    ~RuntimeCall();
private:
    std::uint64_t begin_;
};
std::string StatusJson(); // Present thread, also used by snapshots
bool NextFramePhase();
void SetNextFramePhase(bool next);
} // namespace mevr::host::quest_pacing
