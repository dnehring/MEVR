// SPDX-License-Identifier: MIT
#include "core/ik/PoseGraft.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace mevr::ik {

std::vector<int> Subtree(std::span<int const> parents, int root, std::span<int const> exclude) {
    std::vector<int> out;
    int const count = static_cast<int>(parents.size());
    if (root < 0 || root >= count) {
        return out;
    }
    auto const excluded = [&](int bone) { return std::find(exclude.begin(), exclude.end(), bone) != exclude.end(); };
    if (excluded(root)) {
        return out;
    }
    std::vector<char> in(static_cast<std::size_t>(count), 0);
    in[static_cast<std::size_t>(root)] = 1;
    out.push_back(root);
    for (int i = root + 1; i < count; ++i) {
        int const parent = parents[static_cast<std::size_t>(i)];
        if (parent >= 0 && parent < i && in[static_cast<std::size_t>(parent)] != 0 && !excluded(i)) {
            in[static_cast<std::size_t>(i)] = 1;
            out.push_back(i);
        }
    }
    return out;
}

std::vector<int> ParentsOf(std::span<int const> parents, std::span<int const> bones) {
    std::vector<int> out;
    out.reserve(bones.size());
    for (int bone : bones) {
        out.push_back(bone >= 0 && bone < static_cast<int>(parents.size()) ? parents[static_cast<std::size_t>(bone)] : -1);
    }
    return out;
}

namespace {

bool Valid(std::size_t poseSize, std::span<int const> bones, std::span<int const> boneParents) {
    if (bones.size() != boneParents.size()) {
        return false;
    }
    int const count = static_cast<int>(poseSize);
    for (std::size_t i = 0; i < bones.size(); ++i) {
        if (bones[i] < 0 || bones[i] >= count || boneParents[i] < 0 || boneParents[i] >= count) {
            return false;
        }
    }
    return true;
}

} // namespace

std::vector<Transform> CaptureRelative(std::span<Transform const> pose, std::span<int const> bones, std::span<int const> boneParents) {
    if (!Valid(pose.size(), bones, boneParents)) {
        return {};
    }
    std::vector<Transform> out;
    out.reserve(bones.size());
    for (std::size_t i = 0; i < bones.size(); ++i) {
        out.push_back(Relative(pose[static_cast<std::size_t>(bones[i])], pose[static_cast<std::size_t>(boneParents[i])]));
    }
    return out;
}

bool ApplyRelative(std::span<Transform> pose, std::span<int const> bones, std::span<int const> boneParents,
    std::span<Transform const> relative) {
    if (bones.size() != relative.size() || !Valid(pose.size(), bones, boneParents)) {
        return false;
    }
    for (std::size_t i = 0; i < bones.size(); ++i) {
        pose[static_cast<std::size_t>(bones[i])] = Compose(relative[i], pose[static_cast<std::size_t>(boneParents[i])]);
    }
    return true;
}

PoseDifference Difference(std::span<Transform const> a, std::span<Transform const> b) {
    if (a.size() != b.size()) {
        double const inf = std::numeric_limits<double>::infinity();
        return {inf, inf};
    }
    PoseDifference d;
    for (std::size_t i = 0; i < a.size(); ++i) {
        d.units = std::max(d.units, Length(a[i].translation - b[i].translation));
        d.degrees = std::max(d.degrees, AngleBetween(a[i].rotation, b[i].rotation) * 180.0 / std::numbers::pi);
    }
    return d;
}

StillPose::State StillPose::Feed(std::span<Transform const> sample, double deltaSeconds) {
    if (state_ != State::Learning) {
        return state_;
    }
    if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0) {
        seconds_ += deltaSeconds;
    }
    if (!latest_.empty()) {
        last_ = Difference(sample, latest_);
        bool const still = last_.units <= limits_.units && last_.degrees <= limits_.degrees;
        still_ = still ? still_ + 1 : 0;
    }
    latest_.assign(sample.begin(), sample.end());
    if (still_ >= limits_.frames) {
        state_ = State::Still;
    } else if (seconds_ >= limits_.maxSeconds) {
        state_ = State::TimedOut;
    }
    return state_;
}

void StillPose::Reset() {
    state_ = State::Learning;
    latest_.clear();
    still_ = 0;
    seconds_ = 0.0;
    last_ = {};
}

} // namespace mevr::ik
