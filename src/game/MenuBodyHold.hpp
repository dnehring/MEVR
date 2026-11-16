// SPDX-License-Identifier: MIT
#pragma once

#include "engine/BonePose.hpp"
#include <array>
#include <cmath>

namespace mevr::game::menu_body {
// Values only. The caller revalidates current engine objects before constructing this identity;
// the cache never dereferences an old pointer or turns it back into a live object.
struct Identity {
    std::array<std::uintptr_t, 8> objects{}; // world, PC, pawn, mesh, asset and their class identities
    std::array<std::uint64_t, 4> names{};   // PC, pawn, mesh, asset
    std::array<double, 16> componentToWorld{};
    std::array<double, 6> camera{}; // native POV position and rotation, excluding live headset lean/look
    bool epochKnown = false;
    bool floorLocked = false; // a changed eye-correction policy is not the same presentation space
    std::uint32_t sessions = 0;
    std::uint32_t recenters = 0;
    bool operator==(Identity const&) const = default;
    // Owner/reference-space identity for a fresh solve. Ordinary head, camera
    // and component motion must not reset the live body follower every frame.
    bool SameTrackingSpace(Identity const& other) const {
        return objects == other.objects && names == other.names && epochKnown == other.epochKnown &&
            sessions == other.sessions && recenters == other.recenters && floorLocked == other.floorLocked;
    }
    bool Valid() const {
        for (auto value : objects) if (!value) return false;
        for (auto value : componentToWorld) if (!std::isfinite(value)) return false;
        for (auto value : camera) if (!std::isfinite(value)) return false;
        return true;
    }
};

inline engine::bone_pose::Request TorsoOnly(engine::bone_pose::Request request) {
    for (auto& arm : request.arms) {
        arm.target = engine::bone_pose::Target::Off;
        arm.hasRotation = false;
    } // Keep chain/name metadata: PoseTorso still needs the shoulders.
    request.torso.collars = false;
    request.support = {};
    request.fingers = {};
    request.gunShift = {};
    request.gunKick = {};
    request.legs = {};
    return request;
}

// A fresh, first-person body placement may bridge a shared overlay menu. Renew only after
// the caller's same-tick native/menu/camera gates, with a stationary component and same XR epoch.
class Hold {
public:
    void Clear() { request_.reset(); holding_ = false; checkedAt_ = 0; }
    void Capture(engine::bone_pose::Request const& request, Identity const& identity,
        bool eligible, std::uint64_t now) {
        if (!eligible || !identity.Valid() || !request.mesh || !request.torso.enabled ||
            reinterpret_cast<std::uintptr_t>(request.mesh) != identity.objects[3] ||
            !request.torso.goal.bodyMoves || now == 0) { Clear(); return; }
        request_ = TorsoOnly(request);
        identity_ = identity;
        checkedAt_ = now;
        holding_ = false;
    }
    engine::bone_pose::Request const* Get(Identity const& identity, bool eligible, std::uint64_t now) {
        if (!eligible || !request_ || !identity.Valid() || !(identity == identity_) ||
            now < checkedAt_ || now - checkedAt_ > 250) { Clear(); return nullptr; }
        checkedAt_ = now;
        holding_ = true;
        return &*request_;
    }
    void Close() { if (holding_) Clear(); }
private:
    std::optional<engine::bone_pose::Request> request_;
    Identity identity_;
    bool holding_ = false;
    std::uint64_t checkedAt_ = 0;
};
} // namespace mevr::game::menu_body
