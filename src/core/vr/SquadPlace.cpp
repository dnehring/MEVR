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
    return members >= 2 ? 3 : members == 1 ? 1 : 0;
}

int SquadTileIndex(int members) {
    return members >= 2 ? 1 : 0;
}

int MemberOfTile(int tile, int members) {
    if (members < 2) return kSquadTile;
    return tile == 0 ? 0 : tile == 2 ? 1 : kSquadTile;
}

std::vector<int> WeaponTypes(std::uint8_t mask) {
    std::vector<int> out;
    for (int w = 0; w < 4; ++w) {
        if ((mask & (1u << w)) != 0) out.push_back(w);
    }
    return out;
}

int Machine::Weapon() const {
    return row_ == Row::Weapon && weaponTile_ >= 0 && weaponTile_ < static_cast<int>(weapons_.size()) ? weapons_[static_cast<std::size_t>(weaponTile_)]
                                                                                                    : -1;
}

void Machine::Reset() {
    holding_ = false;
    heldS_ = 0.0f;
    tile_ = SquadTileIndex(members_);
    armed_ = true;
    row_ = Row::Who;
    weapons_.clear();
    weaponTile_ = 0;
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
            step.landingChanged = true;
            landing_ = in.landing;
        }
        // The squad shrank or grew under the hold: the selector starts over on the squad.
        if (in.members != members_) {
            members_ = in.members;
            tile_ = SquadTileIndex(members_);
            row_ = Row::Who;
            weapons_.clear();
            weaponTile_ = 0;
            step.selectionChanged = true;
        }
        // A flick of the right stick, re-armed near the centre: sideways steps the lit row's tiles, down opens the weapon row
        // under the names, up goes back to them. The stronger axis decides.
        bool const sideways = std::abs(in.stickX) >= std::abs(in.stickY);
        float const along = sideways ? in.stickX : in.stickY;
        if (armed_ && std::abs(along) >= settings.flickOn) {
            armed_ = false;
            if (sideways && row_ == Row::Who) {
                int const next = std::clamp(tile_ + (along > 0.0f ? 1 : -1), 0, std::max(TileCount(members_) - 1, 0));
                if (next != tile_) {
                    tile_ = next;
                    step.selectionChanged = true;
                }
            } else if (sideways) {
                int const next = std::clamp(weaponTile_ + (along > 0.0f ? 1 : -1), 0, std::max(static_cast<int>(weapons_.size()) - 1, 0));
                if (next != weaponTile_) {
                    weaponTile_ = next;
                    step.weaponChanged = true;
                }
            } else if (along < 0.0f && row_ == Row::Who && settings.weapons) {
                weapons_ = WeaponTypes(in.weaponMask);
                if (!weapons_.empty()) {
                    row_ = Row::Weapon;
                    weaponTile_ = 0;
                    for (std::size_t i = 0; i < weapons_.size(); ++i) {
                        if (weapons_[i] == in.currentWeapon) weaponTile_ = static_cast<int>(i);
                    }
                    step.rowChanged = true;
                }
            } else if (along > 0.0f && row_ == Row::Weapon) {
                row_ = Row::Who;
                weapons_.clear();
                weaponTile_ = 0;
                step.rowChanged = true;
            }
        } else if (!armed_ && std::abs(in.stickX) <= settings.flickOff && std::abs(in.stickY) <= settings.flickOff) {
            armed_ = true;
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
            } else if (row_ == Row::Weapon) {
                step.weapon = Weapon();
                step.equip = step.weapon >= 0;
                step.cancelled = !step.equip;
                step.why = step.equip ? "" : "no weapon lit";
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
        tile_ = SquadTileIndex(members_);
        armed_ = std::abs(in.stickX) <= settings.flickOff && std::abs(in.stickY) <= settings.flickOff;  // a pushed stick waits
        row_ = Row::Who;
        weapons_.clear();
        weaponTile_ = 0;
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
