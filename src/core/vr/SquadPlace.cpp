// SPDX-License-Identifier: MIT
// Squad placement by an arc (core/vr/SquadPlace.hpp).

#include "core/vr/SquadPlace.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

namespace mevr::vr::squad_place {

Vec3 ArcAt(Vec3 const& origin, Vec3 const& direction, float t, ArcSettings const& settings) {
    float const v = settings.speedMS;
    return {origin.x + direction.x * v * t, origin.y + direction.y * v * t,
        origin.z + direction.z * v * t - 0.5f * settings.gravityMS2 * t * t};
}

std::vector<Vec3> ArcSegments(Vec3 const& origin, Vec3 const& direction, ArcSettings const& settings) {
    std::vector<Vec3> out{origin};
    float const step = std::max(settings.stepS, 0.01f);
    int const steps = static_cast<int>(std::ceil(std::max(settings.maxS, step) / step));
    for (int i = 1; i <= steps; ++i) {
        Vec3 const p = ArcAt(origin, direction, step * static_cast<float>(i), settings);
        out.push_back(p);
        float const flat = std::hypot(p.x - origin.x, p.y - origin.y);
        if (flat > settings.rangeM) break;
    }
    return out;
}

std::vector<Vec3> ArcSamples(Vec3 const& origin, Vec3 const& direction, float untilS, int count, ArcSettings const& settings) {
    count = std::max(count, 2);
    std::vector<Vec3> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        float const t = untilS * static_cast<float>(i) / static_cast<float>(count - 1);
        out.push_back(ArcAt(origin, direction, t, settings));
    }
    return out;
}

bool IsFloor(Vec3 const& normal, ArcSettings const& settings) {
    float const length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
    return length > 1e-4f && normal.z / length >= settings.minUpNormal;
}

char const* Name(Landing landing) {
    switch (landing) {
    case Landing::Floor: return "floor";
    case Landing::Wall: return "no floor (a wall or a slope)";
    case Landing::None: break;
    }
    return "nothing in reach";
}

char const* Name(Order order) {
    switch (order) {
    case Order::Move: return "move";
    case Order::Follow: return "follow";
    case Order::None: break;
    }
    return "none";
}

int TileCount(int members) {
    return members >= 2 ? 3 : 0;
}

int MemberOfTile(int tile, int members) {
    if (members < 2) return kSquadTile;
    return tile == 0 ? 0 : tile == 2 ? 1 : kSquadTile;
}

std::vector<float> WheelAngles() {
    return {180.0f, 90.0f, 0.0f};
}

void Machine::Reset() {
    holding_ = false;
    picking_ = false;
    heldS_ = 0.0f;
    tile_ = -1;
    landing_ = Landing::None;
}

Step Machine::Advance(Input const& in, Settings const& settings) {
    Step step;
    float const dt = std::max(in.dt, 0.0f);
    bool const pressed = in.button && !wasDown_;
    wasDown_ = in.button;
    if (!in.button) blocked_ = false;
    sinceBlipS_ += dt;
    if (holding_) {
        heldS_ += dt;
        if (!settings.enabled || !in.usable) {
            step.cancelled = true;
            step.why = "no longer usable here";
            step.member = Member();
            Reset();
            blocked_ = in.button;
            return step;
        }
        if (in.landing != landing_) {
            if (!picking_) step.landingChanged = true;
            landing_ = in.landing;
        }
        // The squad shrank under the wheel (a squadmate down): nothing left to pick, the arc for the squad.
        if (picking_ && in.members != members_) {
            members_ = in.members;
            if (TileCount(members_) == 0) {
                picking_ = false;
                tile_ = -1;
            }
        }
        // The squad wheel: the hand's travel from where B went down lights the tile it moves toward, and picks it further out.
        if (picking_ && in.handValid) {
            float const reach = std::hypot(in.hand.x, in.hand.y);
            if (reach >= settings.highlightM) {
                std::vector<float> const angles = WheelAngles();
                float const direction = std::atan2(in.hand.y, in.hand.x) * 57.2957795f;
                int best = -1;
                float bestDiff = 1e9f;
                for (std::size_t i = 0; i < angles.size(); ++i) {
                    float d = std::fmod(std::abs(direction - angles[i]), 360.0f);
                    d = d > 180.0f ? 360.0f - d : d;
                    if (d < bestDiff) {
                        bestDiff = d;
                        best = static_cast<int>(i);
                    }
                }
                // Straight down (toward the forearm) is no tile's: nothing lights or picks there.
                if (bestDiff > 60.0f) best = tile_ >= 0 ? tile_ : -1;
                // A lit tile keeps the light unless another is clearly nearer (no flicker on a border).
                if (tile_ >= 0 && best >= 0 && best != tile_) {
                    float d = std::fmod(std::abs(direction - angles[static_cast<std::size_t>(tile_)]), 360.0f);
                    d = d > 180.0f ? 360.0f - d : d;
                    if (d - bestDiff < settings.keepDeg) best = tile_;
                }
                if (best != tile_) {
                    tile_ = best;
                    step.highlightChanged = true;
                }
                if (reach >= settings.commitM && tile_ >= 0 && bestDiff <= 60.0f) {
                    picking_ = false;
                    step.picked = true;
                }
            }
        }
        if (in.button && heldS_ > settings.maxHoldS) {
            step.cancelled = true;
            step.why = "held too long";
            step.member = Member();
            Reset();
            blocked_ = true;
            return step;
        }
        if (!in.button) {
            step.member = Member();
            if (heldS_ < settings.minHoldS) {
                step.cancelled = true;
                step.why = "a blip";
                sinceBlipS_ = 0.0f;
            } else if (picking_) {
                step.cancelled = true;
                step.why = "let go before picking who";
            } else if (in.landing != Landing::Floor) {
                step.cancelled = true;
                step.why = Name(in.landing);
            } else {
                step.given = true;
                step.order = in.landingFromPlayerM <= settings.followWithinM ? Order::Follow : Order::Move;
            }
            Reset();
        }
        return step;
    }
    if (pressed && !blocked_ && settings.enabled && in.usable && sinceBlipS_ <= settings.doubleTapS) {
        // The second tap: the whole squad back to Shepard at once; this press never becomes a hold.
        step.regroup = true;
        step.order = Order::Follow;
        step.member = kSquadTile;
        sinceBlipS_ = 1e9f;
        blocked_ = true;
        return step;
    }
    if (pressed && !blocked_ && settings.enabled && in.usable) {
        holding_ = true;
        heldS_ = 0.0f;
        members_ = in.members;
        picking_ = TileCount(members_) > 0;
        tile_ = -1;
        landing_ = in.landing;
        step.started = true;
        step.member = Member();
    } else if (pressed) {
        blocked_ = true;  // pressed where it can't start: this press never becomes one
    }
    return step;
}

namespace {
std::mutex gMutex;
View gLatest;
std::chrono::steady_clock::time_point gLatestAt{};
}  // namespace

void Publish(View const& view) {
    std::lock_guard lock(gMutex);
    gLatest = view;
    gLatestAt = std::chrono::steady_clock::now();
}

View Latest(std::uint64_t maxAgeMs) {
    std::lock_guard lock(gMutex);
    View out = gLatest;
    auto const age = std::chrono::steady_clock::now() - gLatestAt;
    if (gLatestAt == std::chrono::steady_clock::time_point{} || age > std::chrono::milliseconds(maxAgeMs)) out.active = 0;
    return out;
}

}  // namespace mevr::vr::squad_place
