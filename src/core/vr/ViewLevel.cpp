#include "core/vr/ViewLevel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

namespace mevr::vr::view_level {

int Signed(int units) {
    int const wrapped = units & (kUnitsPerTurn - 1);
    return wrapped >= kUnitsPerTurn / 2 ? wrapped - kUnitsPerTurn : wrapped;
}

double Degrees(int units) {
    return units * 360.0 / kUnitsPerTurn;
}

int Units(double degrees) {
    return static_cast<int>(std::lround(degrees * kUnitsPerTurn / 360.0));
}

char const* Name(Why why) {
    switch (why) {
    case Why::Holding: return "holding level";
    case Why::Off: return "off ([Hands] levelCamera = 0)";
    case Why::NoView: return "no head-tracked view";
    case Why::HeadAim: return "the host's head aim turns the camera";
    case Why::NoHandAim: return "the hand doesn't aim";
    case Why::NotOnFoot: return "not on foot";
    case Why::NoPawn: return "no pawn";
    case Why::Cover: return "in cover";
    case Why::Ladder: return "on a ladder";
    case Why::CameraMode: return "a camera mode with its own pitch";
    }
    return "?";
}

Why Decide(Inputs const& in) {
    if (!in.enabled) {
        return Why::Off;
    }
    if (!in.headTrackedView) {
        return Why::NoView;
    }
    if (in.headAim) {
        return Why::HeadAim;
    }
    if (!in.handAim) {
        return Why::NoHandAim;
    }
    if (!in.onFoot) {
        return Why::NotOnFoot;
    }
    if (!in.pawn) {
        return Why::NoPawn;
    }
    if (in.cover) {
        return Why::Cover;
    }
    if (in.ladder) {
        return Why::Ladder;
    }
    if (!in.cameraModeAllowed) {
        return Why::CameraMode;
    }
    return Why::Holding;
}

bool CameraModeAllows(std::string_view className) {
    // Exact class names (a substring test would let VehicleTightAim through as TightAim).
    constexpr std::string_view kAllowed[] = {
        "SFXCameraMode_Explore",
        "SFXCameraMode_ExploreStorm",
        "SFXCameraMode_Combat",
        "SFXCameraMode_CombatStorm",
        "SFXCameraMode_TightAim",
    };
    return std::find(std::begin(kAllowed), std::end(kAllowed), className) != std::end(kAllowed);
}

int Correction(int units) {
    return -Signed(units);
}

char const* Name(AssistWhy why) {
    switch (why) {
    case AssistWhy::Removed: return "friction and adhesion removed (the hand aims; the stick's turn is the player's own)";
    case AssistWhy::Kept: return "the game's friction and adhesion kept ([Hands] aimAssist = 1)";
    case AssistWhy::NoView: return "the game's friction and adhesion (no head-tracked view)";
    case AssistWhy::HeadAim: return "the game's friction and adhesion (the host's head aim turns the camera)";
    case AssistWhy::NoHandAim: return "the game's friction and adhesion (the hand doesn't aim)";
    case AssistWhy::NotOnFoot: return "the game's friction and adhesion (not on foot)";
    case AssistWhy::NoPawn: return "the game's friction and adhesion (no pawn)";
    case AssistWhy::CameraMode: return "the game's friction and adhesion (a camera mode that keeps them: the sniper scope...)";
    }
    return "?";
}

AssistWhy DecideAssist(AssistInputs const& in) {
    if (in.keep) {
        return AssistWhy::Kept;
    }
    if (!in.headTrackedView) {
        return AssistWhy::NoView;
    }
    if (in.headAim) {
        return AssistWhy::HeadAim;
    }
    if (!in.handAim) {
        return AssistWhy::NoHandAim;
    }
    if (!in.onFoot) {
        return AssistWhy::NotOnFoot;
    }
    if (!in.pawn) {
        return AssistWhy::NoPawn;
    }
    if (!in.cameraModeRemoves) {
        return AssistWhy::CameraMode;
    }
    return AssistWhy::Removed;
}

bool CameraModeRemovesAssist(std::string_view className) {
    // Exact class names, as CameraModeAllows (VehicleTightAim isn't TightAim). Cover is in: the hand aims
    // there too, and the stick still turns.
    constexpr std::string_view kRemoves[] = {
        "SFXCameraMode_Explore",
        "SFXCameraMode_ExploreStorm",
        "SFXCameraMode_Combat",
        "SFXCameraMode_CombatStorm",
        "SFXCameraMode_TightAim",
        "SFXCameraMode_EnterCover",
        "SFXCameraMode_HipAimCover",
    };
    return std::find(std::begin(kRemoves), std::end(kRemoves), className) != std::end(kRemoves);
}

Window::Bucket& Window::At(double now) {
    long long const second = static_cast<long long>(std::floor(now));
    Bucket& bucket = buckets_[static_cast<std::size_t>(((second % 64) + 64) % 64)];
    if (bucket.second != second) {
        bucket = Bucket{};
        bucket.second = second;
    }
    return bucket;
}

void Window::Frame(double now, int pitch, int roll, int gameDelta, int correction, bool held) {
    Bucket& b = At(now);
    int const p = Signed(pitch);
    int const r = Signed(roll);
    if (!b.any) {
        b.minPitch = b.maxPitch = p;
        b.minRoll = b.maxRoll = r;
        b.any = true;
    } else {
        b.minPitch = std::min(b.minPitch, p);
        b.maxPitch = std::max(b.maxPitch, p);
        b.minRoll = std::min(b.minRoll, r);
        b.maxRoll = std::max(b.maxRoll, r);
    }
    b.gameDrift += gameDelta;
    b.corrected += correction;
    ++b.frames;
    b.heldFrames += held ? 1 : 0;
}

void Window::Shot(double now, int kick) {
    Bucket& b = At(now);
    ++b.shots;
    b.recoil += kick;
}

void Window::ZoomSnap(double now) {
    ++At(now).zoomSnaps;
}

void Window::Friction(double now, double factor, bool turning, bool removed) {
    if (!(factor > 0.0)) {
        return;  // no target in the soft margin (or friction off for the weapon): nothing would have happened
    }
    Bucket& b = At(now);
    ++b.frictionTicks;
    b.frictionTurning += turning ? 1 : 0;
    b.frictionRemoved += removed ? 1 : 0;
    b.frictionSum += factor;
    b.frictionMax = std::max(b.frictionMax, factor);
}

void Window::Adhesion(double now, int pitch, int yaw, bool removed) {
    int const p = Signed(pitch);
    int const y = Signed(yaw);
    if (p == 0 && y == 0) {
        return;
    }
    Bucket& b = At(now);
    ++b.adhesionTicks;
    b.adhesionRemoved += removed ? 1 : 0;
    b.adhesionYaw += std::abs(y);
    b.adhesionPitch += std::abs(p);
}

Summary Window::Last(double now, double seconds) const {
    Summary s;
    long long const current = static_cast<long long>(std::floor(now));
    long long const span = std::clamp(static_cast<long long>(std::ceil(seconds)), 1LL, 60LL);
    long long first = current;
    for (Bucket const& b : buckets_) {
        if (b.second < 0 || b.second > current || b.second <= current - span) {
            continue;
        }
        if (b.any) {
            if (!s.any) {
                s.minPitch = b.minPitch;
                s.maxPitch = b.maxPitch;
                s.minRoll = b.minRoll;
                s.maxRoll = b.maxRoll;
                s.any = true;
            } else {
                s.minPitch = std::min(s.minPitch, b.minPitch);
                s.maxPitch = std::max(s.maxPitch, b.maxPitch);
                s.minRoll = std::min(s.minRoll, b.minRoll);
                s.maxRoll = std::max(s.maxRoll, b.maxRoll);
            }
            first = std::min(first, b.second);
        }
        s.gameDrift += b.gameDrift;
        s.corrected += b.corrected;
        s.recoil += b.recoil;
        s.shots += b.shots;
        s.frames += b.frames;
        s.heldFrames += b.heldFrames;
        s.zoomSnaps += b.zoomSnaps;
        s.frictionTicks += b.frictionTicks;
        s.frictionTurning += b.frictionTurning;
        s.frictionRemoved += b.frictionRemoved;
        s.frictionSum += b.frictionSum;
        s.frictionMax = std::max(s.frictionMax, b.frictionMax);
        s.adhesionTicks += b.adhesionTicks;
        s.adhesionRemoved += b.adhesionRemoved;
        s.adhesionYaw += b.adhesionYaw;
        s.adhesionPitch += b.adhesionPitch;
    }
    s.seconds = s.any ? static_cast<double>(current - first + 1) : 0.0;
    return s;
}

void Window::Clear() {
    buckets_.fill(Bucket{});
}

bool ChangeLog::Feed(double now, int gameDelta, int roll) {
    if (!started_) {
        started_ = true;
        lineAt_ = now;
        rollAtLine_ = Signed(roll);
    }
    pitch_ += gameDelta;
    roll_ = Signed(roll);
    bool const moved = std::llabs(pitch_) >= threshold_ || std::abs(roll_ - rollAtLine_) >= threshold_;
    return moved && now - lineAt_ >= minSeconds_;
}

void ChangeLog::Shot(int kick) {
    ++shots_;
    recoil_ += kick;
}

ChangeLog::Line ChangeLog::Take(double now) {
    Line line;
    line.pitch = pitch_;
    line.rollFrom = rollAtLine_;
    line.rollTo = roll_;
    line.shots = shots_;
    line.recoil = recoil_;
    line.seconds = now - lineAt_;
    lineAt_ = now;
    pitch_ = 0;
    rollAtLine_ = roll_;
    shots_ = 0;
    recoil_ = 0;
    return line;
}

Pulse RecoilPulse(double kickDegrees, double scale) {
    Pulse pulse;
    if (!(scale > 0.0)) {
        return pulse;
    }
    // A rifle's kick (0.1-0.2 degrees) is a short tap, a shotgun's or a sniper rifle's (2-5) a thump.
    double const strength = std::clamp(std::isfinite(kickDegrees) ? kickDegrees / 2.0 : 0.0, 0.0, 1.0);
    pulse.amplitude = static_cast<float>(std::clamp(scale, 0.0, 1.0) * (0.6 + 0.4 * strength));
    pulse.seconds = static_cast<float>(0.03 + 0.04 * strength);
    return pulse;
}

} // namespace mevr::vr::view_level
