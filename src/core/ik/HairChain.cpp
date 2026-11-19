// SPDX-License-Identifier: MIT
#include "core/ik/HairChain.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace mevr::ik::hair {
namespace {

constexpr double kStep = 1.0 / 120.0;  // the fixed substep (s)
constexpr int kMaxSubsteps = 4;        // a long frame drops time rather than spiralling
constexpr int kIterations = 3;
constexpr double kResetJumpM = 0.5;
constexpr double kResetTurnRad = 60.0 * 3.14159265358979323846 / 180.0;

bool IsHair(std::string const& name) {
    std::string lower(name.size(), '\0');
    std::transform(name.begin(), name.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("hair") != std::string::npos;
}

bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

Vec3 Lerp(Vec3 a, Vec3 b, double t) {
    return a + (b - a) * t;
}

// The rotation of `angle` radians about unit `axis`.
Quat AxisAngle(Vec3 axis, double angle) {
    double const s = std::sin(angle * 0.5);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5)};
}

// The chain's points from the animated pose: each bone's origin, then the tip, which lies past the last bone by that bone's
// own offset from its parent, carried into its frame (the chain goes on as it went).
bool TargetsOf(Chain const& chain, std::span<Transform const> animated, std::vector<Vec3>& out) {
    out.clear();
    int const count = static_cast<int>(animated.size());
    for (int b : chain.bones) {
        if (b < 0 || b >= count) {
            return false;
        }
        out.push_back(animated[b].translation);
    }
    if (chain.bones.empty() || chain.anchor < 0 || chain.anchor >= count) {
        return false;
    }
    int const last = chain.bones.back();
    int const lastParent = chain.bones.size() >= 2 ? chain.bones[chain.bones.size() - 2] : chain.anchor;
    Transform const& l = animated[last];
    Transform const& p = animated[lastParent];
    Vec3 const offsetInParent = Rotate(Conjugate(p.rotation), l.translation - p.translation);
    out.push_back(l.translation + Rotate(l.rotation, offsetInParent));
    for (Vec3 const& v : out) {
        if (!Finite(v)) {
            return false;
        }
    }
    return true;
}

void ResetTo(Chain& chain) {
    chain.pos = chain.target;
    chain.prev = chain.target;
    chain.lastTarget = chain.target;
    chain.carry = 0.0;
}

// `p` pushed out of every capsule: by the capsule's radius plus the point's; with `keepRest` (the person's own body) never
// further than the animated point itself sits from the axis (hair at rest lies on the scalp, inside the skull's capsule).
int Collide(Vec3& p, Vec3 target, double radius, std::span<hand_contact::Capsule const> colliders, bool keepRest) {
    int pushes = 0;
    for (hand_contact::Capsule const& c : colliders) {
        Vec3 const onAxis = hand_contact::ClosestOnSegment(p, c.a, c.b);
        Vec3 const away = p - onAxis;
        double const d = Length(away);
        double reach = c.radius + radius;
        if (keepRest) {
            reach = (std::min)(reach, Length(target - hand_contact::ClosestOnSegment(target, c.a, c.b)));
        }
        if (d >= reach) {
            continue;
        }
        Vec3 dir = d > 1e-9 ? away * (1.0 / d) : Normalize(target - onAxis, {0.0, 0.0, 1.0});
        if (keepRest) {
            // Out on the rest point's side: a point that crossed the axis (a strand flicked through the neck) pushed out on
            // its own side would stay there, the pull home never getting it back across (Miranda's two neck strands held
            // 7 and 16 cm off, 2026-11-19).
            Vec3 const restAway = target - hand_contact::ClosestOnSegment(target, c.a, c.b);
            if (Length(restAway) > 1e-9 && Dot(dir, restAway) < 0.0) {
                dir = Normalize(restAway, dir);
            }
        }
        p = onAxis + dir * reach;
        ++pushes;
    }
    return pushes;
}

} // namespace

std::vector<Chain> Discover(std::span<std::string const> names, std::span<int const> parents) {
    std::vector<Chain> chains;
    int const count = static_cast<int>((std::min)(names.size(), parents.size()));
    std::vector<bool> hair(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        hair[static_cast<std::size_t>(i)] = IsHair(names[static_cast<std::size_t>(i)]);
    }
    std::vector<bool> used(static_cast<std::size_t>(count), false);
    // Bones come parents first in a RefSkeleton, so walking by index meets every chain's start before its bones.
    for (int i = 0; i < count; ++i) {
        if (!hair[static_cast<std::size_t>(i)] || used[static_cast<std::size_t>(i)]) {
            continue;
        }
        int const parent = parents[static_cast<std::size_t>(i)];
        if (parent < 0 || parent >= count) {
            continue;  // a hair bone at the root: nothing poses it
        }
        Chain chain;
        chain.anchor = parent;
        int bone = i;
        while (bone >= 0) {
            chain.bones.push_back(bone);
            used[static_cast<std::size_t>(bone)] = true;
            int next = -1;
            for (int j = bone + 1; j < count; ++j) {
                if (parents[static_cast<std::size_t>(j)] == bone && hair[static_cast<std::size_t>(j)] && !used[static_cast<std::size_t>(j)]) {
                    next = j;
                    break;
                }
            }
            bone = next;
        }
        chains.push_back(std::move(chain));
    }
    return chains;
}

StepStats Step(Chain& chain, Settings const& settings, Input const& input) {
    StepStats stats;
    std::vector<Vec3> target;
    if (!TargetsOf(chain, input.animated, target)) {
        return stats;  // the pose doesn't hold this chain's bones: leave it
    }
    Transform const anchor = input.animated[static_cast<std::size_t>(chain.anchor)];
    bool reset = input.reset || !chain.started || chain.pos.size() != target.size();
    if (!reset) {
        double const jump = Length(anchor.translation - chain.lastAnchor.translation);
        double const turn = AngleBetween(anchor.rotation, chain.lastAnchor.rotation);
        reset = !(jump <= kResetJumpM) || !(turn <= kResetTurnRad);
    }
    chain.lastAnchor = anchor;
    chain.target = target;
    if (reset) {
        ResetTo(chain);
        chain.started = true;
        ++chain.resets;
        stats.reset = true;
        return stats;
    }
    if (!(input.dt > 0.0) || !std::isfinite(input.dt)) {
        chain.lastTarget = target;
        return stats;  // paused
    }

    std::size_t const n = target.size();
    double total = chain.carry + (std::min)(input.dt, 0.25);
    int steps = static_cast<int>(std::floor(total / kStep));
    if (steps > kMaxSubsteps) {
        steps = kMaxSubsteps;
        total = steps * kStep;
    }
    chain.carry = total - steps * kStep;
    stats.substeps = steps;

    double const keep = std::pow(1.0 - std::clamp(settings.damping, 0.0, 1.0), kStep * 60.0);
    double const pull = 1.0 - std::pow(1.0 - std::clamp(settings.stiffness, 0.0, 1.0), kStep * 60.0);
    double const felt = std::clamp(settings.inertia, 0.0, 1.0);
    double const maxBend = std::clamp(settings.maxBendDeg, 0.0, 179.0) * 3.14159265358979323846 / 180.0;
    Vec3 const gravity = Normalize(settings.down, {0.0, 0.0, -1.0}) * (settings.gravity * kStep * kStep);
    double const drag = std::clamp(settings.windDrag * kStep, 0.0, 1.0);
    Vec3 const windStep = settings.windMS * kStep;

    std::vector<Vec3> before = chain.lastTarget.size() == n ? chain.lastTarget : target;
    std::vector<Vec3> now(n);
    std::vector<double> lengths(n, 0.0);
    std::vector<Vec3> animDir(n);
    for (int s = 1; s <= steps; ++s) {
        double const t = static_cast<double>(s) / steps;
        for (std::size_t i = 0; i < n; ++i) {
            now[i] = Lerp(chain.lastTarget.size() == n ? chain.lastTarget[i] : target[i], target[i], t);
        }
        // The share of the head's motion the hair doesn't feel moves it along rigidly.
        for (std::size_t i = 1; i < n; ++i) {
            Vec3 const carried = (now[i] - before[i]) * (1.0 - felt);
            chain.pos[i] = chain.pos[i] + carried;
            chain.prev[i] = chain.prev[i] + carried;
        }
        chain.pos[0] = now[0];
        chain.prev[0] = now[0];
        // Verlet: velocity, wind and air, gravity, the pull home.
        for (std::size_t i = 1; i < n; ++i) {
            Vec3 velocity = (chain.pos[i] - chain.prev[i]) * keep;
            velocity = velocity + (windStep - velocity) * drag;
            chain.prev[i] = chain.pos[i];
            chain.pos[i] = chain.pos[i] + velocity + gravity;
            chain.pos[i] = chain.pos[i] + (now[i] - chain.pos[i]) * pull;
        }
        for (std::size_t i = 1; i < n; ++i) {
            lengths[i] = Length(now[i] - now[i - 1]);
            animDir[i] = Normalize(now[i] - now[i - 1], {0.0, 0.0, -1.0});
        }
        for (int it = 0; it < kIterations; ++it) {
            // No stretch, and each joint within maxBend of the animated bend (relative to its parent's simulated turn).
            Quat parentTurn;
            for (std::size_t i = 1; i < n; ++i) {
                Vec3 const ref = Rotate(parentTurn, animDir[i]);
                Vec3 dir = Normalize(chain.pos[i] - chain.pos[i - 1], ref);
                double const angle = std::acos(std::clamp(Dot(dir, ref), -1.0, 1.0));
                if (angle > maxBend) {
                    Vec3 axis = Cross(ref, dir);
                    double const axisLength = Length(axis);
                    axis = axisLength > 1e-9 ? axis * (1.0 / axisLength) : Normalize(Cross(ref, {0.0, 0.0, 1.0}), {1.0, 0.0, 0.0});
                    dir = Rotate(AxisAngle(axis, maxBend), ref);
                }
                chain.pos[i] = chain.pos[i - 1] + dir * lengths[i];
                parentTurn = FromTo(animDir[i], dir);
            }
            for (std::size_t i = 1; i < n; ++i) {
                stats.collisions += Collide(chain.pos[i], now[i], settings.radiusM, input.body, true);
                stats.collisions += Collide(chain.pos[i], now[i], settings.radiusM, input.colliders, false);
            }
        }
        before = now;
    }
    chain.lastTarget = target;
    for (std::size_t i = 0; i < n; ++i) {
        if (!Finite(chain.pos[i]) || !Finite(chain.prev[i])) {
            ResetTo(chain);
            ++chain.resets;
            stats.reset = true;
            break;
        }
    }
    return stats;
}

void Pose(Chain const& chain, std::span<Transform const> animated, std::vector<Transform>& out) {
    out.clear();
    std::size_t const bones = chain.bones.size();
    if (chain.pos.size() != bones + 1 || chain.target.size() != bones + 1) {
        return;
    }
    for (std::size_t i = 0; i < bones; ++i) {
        int const b = chain.bones[i];
        if (b < 0 || static_cast<std::size_t>(b) >= animated.size()) {
            out.clear();
            return;
        }
        Transform t = animated[static_cast<std::size_t>(b)];
        Vec3 const from = chain.target[i + 1] - chain.target[i];
        Vec3 const to = chain.pos[i + 1] - chain.pos[i];
        if (Length(from) > 1e-9 && Length(to) > 1e-9) {
            t.rotation = Normalize(FromTo(from, to) * t.rotation);
        }
        t.translation = i == 0 ? chain.target[0] : chain.pos[i];
        out.push_back(t);
    }
}

double TipOffset(Chain const& chain) {
    if (chain.pos.empty() || chain.pos.size() != chain.target.size()) {
        return 0.0;
    }
    return Length(chain.pos.back() - chain.target.back());
}

} // namespace mevr::ik::hair
