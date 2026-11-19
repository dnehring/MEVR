// SPDX-License-Identifier: MIT
// Offline tests for hair on simulated bone chains (core/ik/HairChain, docs/trilogy-hair-plan.md section 4.1): chains found by
// name, rest kept, gravity bounded by the bend limit, the lag and settle after a head turn, collisions, the same motion at
// every frame rate, resets. Skeletons are made-up name and parent tables in the shape of ME3's rigged hairs (no game data).

#include "core/ik/HairChain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/HairChainTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace ik = mevr::ik;
namespace hair = mevr::ik::hair;
namespace hc = mevr::ik::hand_contact;

constexpr double kPi = 3.14159265358979323846;

// A head on a neck with two chains: a back strand of two bones hanging down behind the head, and a fringe of one bone in
// front; plus a hair-named bone at the root (never a chain) and a face bone (never hair).
struct Rig {
    std::vector<std::string> names{"Root", "Neck", "Head", "HairBackTop", "HairBackBottom", "HairBangTop", "jawBone"};
    std::vector<int> parents{-1, 0, 1, 2, 3, 2, 2};
    // Each bone's offset from its parent in the parent's frame (metres), rest rotations identity.
    std::vector<ik::Vec3> local{{0, 0, 0}, {0, 0, 1.5}, {0, 0, 0.15}, {-0.10, 0, -0.02}, {0, 0, -0.12}, {0.09, 0, 0.05}, {0.05, 0, -0.05}};
};

// The rig's world pose with the head turned by `head` about the head bone and the whole moved by `shift`.
std::vector<ik::Transform> PoseOf(Rig const& rig, ik::Quat head, ik::Vec3 shift = {}) {
    std::vector<ik::Transform> world(rig.names.size());
    for (std::size_t i = 0; i < rig.names.size(); ++i) {
        ik::Transform local;
        local.translation = rig.local[i];
        if (rig.names[i] == "Head") {
            local.rotation = head;
        }
        if (rig.parents[i] < 0) {
            local.translation = local.translation + shift;
            world[i] = local;
        } else {
            world[i] = ik::Compose(local, world[static_cast<std::size_t>(rig.parents[i])]);
        }
    }
    return world;
}

ik::Quat Yaw(double radians) {
    return {0.0, 0.0, std::sin(radians * 0.5), std::cos(radians * 0.5)};
}

hair::Chain const* Find(std::vector<hair::Chain> const& chains, int firstBone) {
    for (hair::Chain const& c : chains) {
        if (!c.bones.empty() && c.bones.front() == firstBone) {
            return &c;
        }
    }
    return nullptr;
}

void TestDiscover() {
    Rig rig;
    std::vector<hair::Chain> const chains = hair::Discover(rig.names, rig.parents);
    CHECK(chains.size() == 2);
    hair::Chain const* back = Find(chains, 3);
    hair::Chain const* bang = Find(chains, 5);
    CHECK(back != nullptr && back->anchor == 2 && back->bones.size() == 2 && back->bones[1] == 4);
    CHECK(bang != nullptr && bang->anchor == 2 && bang->bones.size() == 1);

    // Jack's ponytail and strands (ME3's names): four two-bone chains on the head; a branch's second child starts its own.
    std::vector<std::string> const jack{"Head", "HairRightStrandA_Top", "HairRightStrandA_Bottom", "HairPonyTop", "HairPonyBottom",
        "HairRightStrandB_Top", "HairRightStrandB_Bottom", "HairLeftStrand_Top", "HairLeftStrand_Bottom"};
    std::vector<int> const jackParents{-1, 0, 1, 0, 3, 0, 5, 0, 7};
    std::vector<hair::Chain> const jc = hair::Discover(jack, jackParents);
    CHECK(jc.size() == 4);
    for (hair::Chain const& c : jc) {
        CHECK(c.anchor == 0 && c.bones.size() == 2);
    }
    std::vector<std::string> const branch{"Head", "HairTop", "HairLeft", "HairRight"};
    std::vector<int> const branchParents{-1, 0, 1, 1};
    std::vector<hair::Chain> const bc = hair::Discover(branch, branchParents);
    CHECK(bc.size() == 2);
    CHECK(bc.size() == 2 && bc[0].bones.size() == 2 && bc[1].anchor == 1 && bc[1].bones.size() == 1);

    // No hair bones: no chains (Miranda's shipped mesh).
    std::vector<std::string> const plain{"God", "Root", "Neck", "Head"};
    std::vector<int> const plainParents{-1, 0, 1, 2};
    CHECK(hair::Discover(plain, plainParents).empty());
}

// Steps the back strand for `seconds` at `hz` with the head yaw given by `yawAt(t)`; returns the tip offsets at each
// half second, and the largest offset seen.
template <class YawAt>
std::vector<double> Run(hair::Settings const& s, double hz, double seconds, YawAt yawAt, double* peak = nullptr,
                        std::vector<ik::Vec3>* tips = nullptr, std::vector<hc::Capsule> const& colliders = {}) {
    Rig rig;
    std::vector<hair::Chain> chains = hair::Discover(rig.names, rig.parents);
    hair::Chain chain = *Find(chains, 3);
    std::vector<double> offsets;
    double const dt = 1.0 / hz;
    int const ticks = static_cast<int>(std::lround(seconds * hz));
    double nextMark = 0.5;
    if (peak != nullptr) {
        *peak = 0.0;
    }
    for (int k = 0; k <= ticks; ++k) {
        double const t = k * dt;
        std::vector<ik::Transform> const pose = PoseOf(rig, Yaw(yawAt(t)));
        hair::Input in;
        in.animated = pose;
        in.colliders = colliders;
        in.dt = k == 0 ? 0.0 : dt;
        hair::Step(chain, s, in);
        double const off = hair::TipOffset(chain);
        if (peak != nullptr) {
            *peak = (std::max)(*peak, off);
        }
        if (t + 1e-9 >= nextMark) {
            offsets.push_back(off);
            if (tips != nullptr) {
                tips->push_back(chain.pos.back());
            }
            nextMark += 0.5;
        }
    }
    return offsets;
}

void TestRestHolds() {
    hair::Settings s;
    s.gravity = 0.0;
    double peak = 0.0;
    Run(s, 90.0, 10.0, [](double) { return 0.0; }, &peak);
    CHECK(peak < 1e-4);
}

void TestGravityBounded() {
    // A strand straight out behind the head (rest horizontal): gravity bends it, at most by the bend limit per joint, and it
    // settles.
    Rig rig;
    rig.local[4] = {-0.12, 0, 0};
    std::vector<hair::Chain> chains = hair::Discover(rig.names, rig.parents);
    hair::Chain chain = *Find(chains, 3);
    hair::Settings s;
    s.stiffness = 0.0;
    s.maxBendDeg = 30.0;
    std::vector<ik::Transform> const pose = PoseOf(rig, {});
    ik::Vec3 lastTip;
    double speed = 1.0;
    for (int k = 0; k <= 90 * 4; ++k) {
        hair::Input in;
        in.animated = pose;
        in.dt = k == 0 ? 0.0 : 1.0 / 90.0;
        hair::Step(chain, s, in);
        if (k > 0) {
            speed = ik::Length(chain.pos.back() - lastTip) * 90.0;
        }
        lastTip = chain.pos.back();
    }
    CHECK(speed < 0.001);
    // The first segment points down by at most 30 degrees from horizontal...
    ik::Vec3 const d1 = ik::Normalize(chain.pos[1] - chain.pos[0]);
    double const a1 = std::acos(std::clamp(ik::Dot(d1, ik::Vec3{-1, 0, 0}), -1.0, 1.0)) * 180.0 / kPi;
    CHECK(a1 <= 30.5 && a1 > 20.0);
    // ...and the tip segment by at most 30 more.
    ik::Vec3 const d2 = ik::Normalize(chain.pos[2] - chain.pos[1]);
    double const a2 = std::acos(std::clamp(ik::Dot(d2, ik::Vec3{-1, 0, 0}), -1.0, 1.0)) * 180.0 / kPi;
    CHECK(a2 <= 60.5 && a2 > a1);
    // No stretch.
    CHECK(chain.pos.size() == 3);
    CHECK(std::abs(ik::Length(chain.pos[1] - chain.pos[0]) - 0.12) < 0.002);
    CHECK(std::abs(ik::Length(chain.pos[2] - chain.pos[1]) - 0.12) < 0.002);
}

void TestHeadTurn() {
    // A 90-degree head turn in 0.2 s: the tip lags behind the head, then settles back.
    hair::Settings s;
    auto yaw = [](double t) {
        if (t < 1.0) {
            return 0.0;
        }
        double const u = std::clamp((t - 1.0) / 0.2, 0.0, 1.0);
        return u * kPi * 0.5;
    };
    double peak = 0.0;
    std::vector<double> const offs = Run(s, 90.0, 4.0, yaw, &peak);
    CHECK(peak > 0.02);
    // At 2.5 s (1.3 s after the turn ended): back within 1 cm.
    CHECK(offs.size() >= 5 && offs[4] < 0.01);
    // Riding rigidly (inertia 0, no gravity): no lag at all.
    hair::Settings rigid;
    rigid.inertia = 0.0;
    rigid.gravity = 0.0;
    double rigidPeak = 1.0;
    Run(rigid, 90.0, 2.0, yaw, &rigidPeak);
    CHECK(rigidPeak < 0.002);
}

void TestCollision() {
    // A sphere (a capsule of zero length: a hand) swept through where the strand hangs: no point ends inside it.
    Rig rig;
    std::vector<hair::Chain> chains = hair::Discover(rig.names, rig.parents);
    hair::Chain chain = *Find(chains, 3);
    hair::Settings s;
    std::vector<ik::Transform> const pose = PoseOf(rig, {});
    double const r = 0.045;
    int pushes = 0;
    double worst = 1.0;
    for (int k = 0; k <= 180; ++k) {
        double const t = k / 90.0;
        // From 30 cm to the strand's right through it to 30 cm on its left, at the lower bone's height.
        ik::Vec3 const c{-0.10, 0.30 - 0.30 * t, 1.65 - 0.14};
        std::vector<hc::Capsule> const colliders{{c, c, r, hc::Part::Hand}};
        hair::Input in;
        in.animated = pose;
        in.colliders = colliders;
        in.dt = k == 0 ? 0.0 : 1.0 / 90.0;
        pushes += hair::Step(chain, s, in).collisions;
        for (std::size_t i = 1; i < chain.pos.size(); ++i) {
            worst = (std::min)(worst, ik::Length(chain.pos[i] - c) - (r + s.radiusM));
        }
    }
    CHECK(pushes > 0);
    CHECK(worst > -1e-6);
    // Hair resting inside a capsule (the scalp in the skull's) stays where it rests.
    hair::Chain rest = *Find(chains, 3);
    std::vector<hc::Capsule> const skull{{{0, 0, 1.6}, {0, 0, 1.75}, 0.2, hc::Part::Head}};
    hair::Settings still;
    still.gravity = 0.0;
    double drift = 0.0;
    for (int k = 0; k <= 180; ++k) {
        hair::Input in;
        in.animated = pose;
        in.body = skull;
        in.dt = k == 0 ? 0.0 : 1.0 / 90.0;
        hair::Step(rest, still, in);
        drift = (std::max)(drift, hair::TipOffset(rest));
    }
    CHECK(drift < 1e-4);
}

void TestFrameRates() {
    hair::Settings s;
    auto yaw = [](double t) { return 0.6 * std::sin(t * 2.0 * kPi * 0.7); };
    std::vector<ik::Vec3> a, b, c;
    Run(s, 30.0, 4.0, yaw, nullptr, &a);
    Run(s, 72.0, 4.0, yaw, nullptr, &b);
    Run(s, 120.0, 4.0, yaw, nullptr, &c);
    CHECK(a.size() == c.size() && b.size() == c.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < (std::min)({a.size(), b.size(), c.size()}); ++i) {
        worst = (std::max)({worst, ik::Length(a[i] - c[i]), ik::Length(b[i] - c[i])});
    }
    CHECK(worst < 0.01);
}

void TestResets() {
    Rig rig;
    std::vector<hair::Chain> chains = hair::Discover(rig.names, rig.parents);
    hair::Chain chain = *Find(chains, 3);
    hair::Settings s;
    std::vector<ik::Transform> pose = PoseOf(rig, {});
    hair::Input in;
    in.animated = pose;
    in.dt = 0.0;
    CHECK(hair::Step(chain, s, in).reset);  // the first step puts it on the pose
    in.dt = 1.0 / 90.0;
    CHECK(!hair::Step(chain, s, in).reset);
    // A 2 m jump (a teleport, a cut's re-placement): back on the pose, at rest.
    std::vector<ik::Transform> const moved = PoseOf(rig, {}, {2.0, 0.0, 0.0});
    in.animated = moved;
    CHECK(hair::Step(chain, s, in).reset);
    CHECK(hair::TipOffset(chain) < 1e-9);
    // A pose that isn't finite is left alone; the state stays finite and the next good pose goes on.
    std::vector<ik::Transform> bad = moved;
    bad[4].translation.x = std::numeric_limits<double>::quiet_NaN();
    in.animated = bad;
    hair::Step(chain, s, in);
    for (ik::Vec3 const& p : chain.pos) {
        CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    }
    in.animated = moved;
    CHECK(!hair::Step(chain, s, in).reset);
    // Paused (dt 0): nothing moves.
    std::vector<ik::Vec3> const before = chain.pos;
    in.dt = 0.0;
    hair::Step(chain, s, in);
    double moveWhilePaused = 0.0;
    for (std::size_t i = 0; i < before.size(); ++i) {
        moveWhilePaused = (std::max)(moveWhilePaused, ik::Length(chain.pos[i] - before[i]));
    }
    CHECK(moveWhilePaused < 1e-12);
}

void TestPose() {
    // The posed bones keep their lengths and point at the simulated points.
    Rig rig;
    rig.local[4] = {-0.12, 0, 0};
    std::vector<hair::Chain> chains = hair::Discover(rig.names, rig.parents);
    hair::Chain chain = *Find(chains, 3);
    hair::Settings s;
    std::vector<ik::Transform> const pose = PoseOf(rig, {});
    for (int k = 0; k <= 90; ++k) {
        hair::Input in;
        in.animated = pose;
        in.dt = k == 0 ? 0.0 : 1.0 / 90.0;
        hair::Step(chain, s, in);
    }
    std::vector<ik::Transform> out;
    hair::Pose(chain, pose, out);
    CHECK(out.size() == 2);
    if (out.size() == 2) {
        CHECK(ik::Length(out[0].translation - pose[3].translation) < 1e-9);  // the first bone stays on the head
        CHECK(ik::Length(out[1].translation - chain.pos[1]) < 1e-9);
        // Bone 0 carried its child's rest offset onto the simulated child.
        ik::Vec3 const child = ik::Apply(out[0], rig.local[4]);
        CHECK(ik::Length(child - chain.pos[1]) < 0.002);
        CHECK(std::abs(ik::Norm(out[1].rotation) - 1.0) < 1e-9);
    }
}

} // namespace

int RunHairChainTests() {
    gFailures = 0;
    TestDiscover();
    TestRestHolds();
    TestGravityBounded();
    TestHeadTurn();
    TestCollision();
    TestFrameRates();
    TestResets();
    TestPose();
    return gFailures;
}
