// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Rigid.hpp"

#include <span>
#include <vector>

// Part of one pose put onto another (research/38 A, "the arms keep the combat pose"): the explore stance composes
// Shepard's body from the explore branch, but the arms, hands, fingers and the bones the gun hangs on (Prop01, LHIK)
// must stay as the combat pose has them, relative to the bone they hang from. Learned from poses composed as combat
// (Capture, settled by StillPose), then put onto each pose composed as explore (Apply), parents first. Poses are
// component-space bone transforms indexed like the skeleton, parents before children (UE3's RefSkeleton order).
// Engine-agnostic; tested offline.
namespace mevr::ik {

// `root` and every bone below it, in index order, without the bones in `exclude` and anything below them.
// `parents[i]` is bone i's parent (-1 for the root); a bone whose parent doesn't come before it is never included.
std::vector<int> Subtree(std::span<int const> parents, int root, std::span<int const> exclude = {});

// `bones` with each one's parent (`boneParents[i]` is bones[i]'s), e.g. from Subtree.
std::vector<int> ParentsOf(std::span<int const> parents, std::span<int const> bones);

// Each bone's transform in its parent's frame (bones[i] in pose[boneParents[i]]). Empty if the sizes differ, an index
// is out of range or a bone has no parent.
std::vector<Transform> CaptureRelative(std::span<Transform const> pose, std::span<int const> bones, std::span<int const> boneParents);

// Puts each bone back at `relative[i]` in its parent's frame, in the order given (parents first, so a grafted bone's
// children follow it). False (nothing written) if the sizes differ or an index is out of range.
bool ApplyRelative(std::span<Transform> pose, std::span<int const> bones, std::span<int const> boneParents,
    std::span<Transform const> relative);

// How far apart two lists of transforms are: the largest translation distance and rotation angle of any element.
// Infinite if the sizes differ.
struct PoseDifference {
    double units = 0.0;
    double degrees = 0.0;
};
PoseDifference Difference(std::span<Transform const> a, std::span<Transform const> b);

// A pose that holds still: Feed it one sample per frame (the same bones each time); it is Still once `frames`
// samples in a row lie within the limits of the one before, or TimedOut (the latest sample taken anyway) once
// samples have come in for `maxSeconds` without that.
class StillPose {
public:
    struct Limits {
        double units = 0.3;
        double degrees = 1.5;
        int frames = 10;
        double maxSeconds = 2.0;
    };
    enum class State { Learning, Still, TimedOut };

    explicit StillPose(Limits limits = {}) : limits_(limits) {}
    State Feed(std::span<Transform const> sample, double deltaSeconds);
    void Reset();
    State Current() const { return state_; }
    std::vector<Transform> const& Latest() const { return latest_; }
    int StillFrames() const { return still_; }
    double Seconds() const { return seconds_; }
    // The last sample against the one before it.
    PoseDifference LastDifference() const { return last_; }

private:
    Limits limits_;
    State state_ = State::Learning;
    std::vector<Transform> latest_;
    int still_ = 0;
    double seconds_ = 0.0;
    PoseDifference last_;
};

} // namespace mevr::ik
