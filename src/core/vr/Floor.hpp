#pragma once

#include <cstdint>

// The head's height above the floor, measured by the host (core/xr/Floor in a STAGE or LOCAL_FLOOR
// space, Halcyon's renderer) and handed to the mod (HostServices::floor, v10) for the body
// holsters' scale (research/19, "Eye height from the floor"). Plain data: it crosses the host/mod
// boundary, so fields are only ever appended (HostServices::floorStateSize).
namespace mevr::vr::floor {

enum class Source : std::uint32_t {
    None = 0,        // no floor space (the runtime offers none, or no session yet)
    Stage = 1,       // XR_REFERENCE_SPACE_TYPE_STAGE: y = 0 is the play area's floor
    LocalFloor = 2,  // XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR: LOCAL dropped to the floor
};

inline char const* Name(Source source) {
    switch (source) {
    case Source::Stage: return "STAGE";
    case Source::LocalFloor: return "LOCAL_FLOOR";
    case Source::None: break;
    }
    return "none";
}

struct State {
    bool valid = false;        // eyeHeight was located in the last frame
    float eyeHeight = 0.0f;    // the VIEW origin (between the eyes) above the floor, meters
    Source source = Source::None;
    std::uint32_t recenters = 0;  // Halcyon's recenters (R, the menu, double-click R3) so far
    std::uint32_t sessions = 0;   // XR sessions created so far (a new one may have a new floor)
    std::uint64_t sequence = 0;   // increments with every frame that published
    std::uint64_t timeMs = 0;     // GetTickCount64 when sampled
};

} // namespace mevr::vr::floor
