#include "game/HeadSim.hpp"

#include <chrono>
#include <cmath>
#include <numbers>

namespace mevr::game::head_sim {
namespace {

using Clock = std::chrono::steady_clock;

Pattern gPattern = Pattern::Off;
Clock::time_point gStart;

constexpr double kTwoPi = 2.0 * std::numbers::pi;

double Radians(double degrees) {
    return degrees * std::numbers::pi / 180.0;
}

// Turn by `yaw` (about +Y, positive = left), then look up by `pitch` (about the turned +X).
XrQuaternionf YawPitch(double yaw, double pitch) {
    double const cy = std::cos(yaw / 2.0), sy = std::sin(yaw / 2.0);
    double const cp = std::cos(pitch / 2.0), sp = std::sin(pitch / 2.0);
    // q = qYaw * qPitch, qYaw = (0, sy, 0, cy), qPitch = (sp, 0, 0, cp)
    return {
        static_cast<float>(cy * sp),
        static_cast<float>(sy * cp),
        static_cast<float>(-sy * sp),
        static_cast<float>(cy * cp),
    };
}

} // namespace

Pattern Cycle() {
    gPattern = gPattern == Pattern::Off ? Pattern::Sweep
        : gPattern == Pattern::Sweep    ? Pattern::Nod
                                        : Pattern::Off;
    gStart = Clock::now();
    return gPattern;
}

Pattern Current() {
    return gPattern;
}

char const* Name(Pattern pattern) {
    switch (pattern) {
    case Pattern::Sweep:
        return "sweep";
    case Pattern::Nod:
        return "nod";
    default:
        return "off";
    }
}

XrPosef PoseAt(Pattern pattern, double seconds) {
    double yaw = 0.0;
    double pitch = 0.0;
    if (pattern == Pattern::Sweep) {
        yaw = Radians(25.0) * std::sin(kTwoPi * seconds / 8.0);
        pitch = Radians(12.0) * std::sin(kTwoPi * seconds / 4.0);
    } else if (pattern == Pattern::Nod) {
        pitch = Radians(20.0) * std::sin(kTwoPi * seconds / 3.0);
    }
    XrPosef pose{};
    pose.orientation = YawPitch(yaw, pitch);
    return pose;
}

std::optional<XrPosef> CurrentPose() {
    if (gPattern == Pattern::Off) {
        return std::nullopt;
    }
    return PoseAt(gPattern, std::chrono::duration<double>(Clock::now() - gStart).count());
}

} // namespace mevr::game::head_sim
