// SPDX-License-Identifier: MIT
#include "engine/HairPose.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/BodySpheres.hpp"
#include "core/ik/Rigid.hpp"
#include "engine/Skeleton.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <format>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <utility>

namespace mevr::engine::hair_pose {
namespace {

namespace hair = ik::hair;
namespace hc = ik::hand_contact;

constexpr ULONGLONG kMaxRequestAgeMs = 500;
constexpr double kUnitsPerMeterRest = 100.0;  // the reference skeleton's units (the games' meshes: 100 per metre)
constexpr int kMaxBones = 512;

std::array<std::atomic<ue3::Object const*>, kMaxTargets> gWanted{};
SRWLOCK gLock = SRWLOCK_INIT;
Request gRequest;
DWORD gThread = 0;
ULONGLONG gTimeMs = 0;
std::atomic<bool> gBroken{false};
std::atomic<double> gTimeScale{1.0};
std::uint64_t gRejected = 0;

// One component's simulation (the game thread only).
struct Sim {
    ue3::Object const* asset = nullptr;
    int boneCount = 0;
    std::vector<hair::Chain> chains;
    std::vector<std::vector<std::uint64_t>> keys;  // per chain: its bones' RefSkeleton names
    std::vector<std::vector<std::string>> names;
    std::vector<std::string> anchorNames;
    struct Soft {
        int bone = -1;
        std::uint64_t key = 0;
        std::string name;
        bool glute = false;
        ik::jiggle::State state;
    };
    std::vector<Soft> soft;
    TargetInfo info;
    bool logged = false;
    LARGE_INTEGER lastCall{};  // the clock at the last call (the step's time when the engine passes none)
    std::uint64_t measuredSteps = 0;
};
std::unordered_map<ue3::Object const*, Sim> gSims;

// The trace (the game thread only).
std::ofstream gTrace;
ULONGLONG gTraceUntilMs = 0;
ULONGLONG gTraceStartMs = 0;

void Reject(TargetInfo* info, char const* why) {
    ++gRejected;
    if (info != nullptr) {
        info->lastReject = why;
    }
}

// ---- Guarded reads and writes: no C++ objects with destructors, so they can catch an access violation ----------

bool ReadPoseGuarded(ue3::Object* mesh, skeleton::BoneAtom* out, int capacity, int* count, ik::Affine* toWorld) {
    __try {
        int const n = skeleton::RefBoneCount(mesh);
        std::span<skeleton::BoneAtom> const bases = skeleton::SpaceBases(mesh);
        if (n <= 0 || n > capacity || static_cast<int>(bases.size()) != n) {
            return false;
        }
        for (int i = 0; i < n; ++i) {
            out[i] = bases[static_cast<std::size_t>(i)];
        }
        *count = n;
        *toWorld = skeleton::LocalToWorld(mesh);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gBroken.store(true);
        return false;
    }
}

bool KeysMatchGuarded(ue3::Object* mesh, int const* bones, std::uint64_t const* keys, int n) {
    __try {
        for (int i = 0; i < n; ++i) {
            if (skeleton::RefBoneKey(mesh, bones[i]) != keys[i]) {
                return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gBroken.store(true);
        return false;
    }
}

bool WriteGuarded(ue3::Object* mesh, int expectedCount, int const* bones, skeleton::BoneAtom const* atoms, int n) {
    __try {
        std::span<skeleton::BoneAtom> const bases = skeleton::SpaceBases(mesh);
        if (static_cast<int>(bases.size()) != expectedCount) {
            return false;
        }
        int lowest = expectedCount;
        int highest = -1;
        for (int i = 0; i < n; ++i) {
            int const b = bones[i];
            if (b < 0 || b >= expectedCount) {
                return false;
            }
            bases[static_cast<std::size_t>(b)] = atoms[i];
            lowest = (std::min)(lowest, b);
            highest = (std::max)(highest, b);
        }
        if (highest >= lowest) {
            write_journal::Wrote("HairPose.chains", &bases[static_cast<std::size_t>(lowest)],
                static_cast<std::size_t>(highest - lowest + 1) * sizeof(skeleton::BoneAtom));
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gBroken.store(true);
        return false;
    }
}

// ---- Poses in world space, metres ----------------------------------------------------------------------------------

struct WorldPose {
    std::vector<ik::Transform> bones;  // world, metres
    ik::Affine toWorld;
    ik::Quat rotation;  // the component's rotation (its transform without scale)
};

thread_local std::array<skeleton::BoneAtom, kMaxBones> tAtoms;

bool ReadWorld(ue3::Object* mesh, double unitsPerMeter, WorldPose& out) {
    int count = 0;
    ik::Affine toWorld;
    if (mesh == nullptr || !ReadPoseGuarded(mesh, tAtoms.data(), kMaxBones, &count, &toWorld)) {
        return false;
    }
    std::optional<ik::Quat> const rotation = ik::RotationOf(toWorld, 1e-2);
    if (!rotation) {
        return false;
    }
    out.toWorld = toWorld;
    out.rotation = *rotation;
    out.bones.resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        skeleton::BoneAtom const& a = tAtoms[static_cast<std::size_t>(i)];
        ik::Transform t;
        t.rotation = ik::Normalize(*rotation * ik::Quat{a.qx, a.qy, a.qz, a.qw});
        t.translation = ik::TransformPoint(toWorld, {a.tx, a.ty, a.tz}) * (1.0 / unitsPerMeter);
        t.scale = a.scale;
        out.bones[static_cast<std::size_t>(i)] = t;
    }
    return true;
}

void AddCapsules(ue3::Object* mesh, std::vector<hc::BonePart> const& parts, double unitsPerMeter, std::vector<hc::Capsule>& out) {
    if (mesh == nullptr || parts.empty()) {
        return;
    }
    WorldPose pose;
    if (!ReadWorld(mesh, unitsPerMeter, pose)) {
        return;
    }
    std::vector<ik::Vec3> positions(pose.bones.size());
    for (std::size_t i = 0; i < pose.bones.size(); ++i) {
        positions[i] = pose.bones[i].translation;
    }
    for (hc::BonePart const& part : parts) {
        if (auto const c = hc::CapsuleOf(part, positions, 1.0)) {
            out.push_back(*c);
        }
    }
}

// The chains of a newly seen hairstyle.
void Discover(ue3::Object* hairMesh, ue3::Object const* asset, int boneCount, Sim& sim) {
    sim = {};
    sim.asset = asset;
    sim.boneCount = boneCount;
    std::optional<skeleton::RefSkeleton> const skel = skeleton::ReadRefSkeleton(hairMesh);
    if (!skel || static_cast<int>(skel->names.size()) != boneCount) {
        return;
    }
    sim.chains = hair::Discover(skel->names, skel->parents);
    // The bind pose (W_child = W_parent * L) for followBody: each long chain's bones relative to the body bone as modelled.
    if (skel->local.size() == skel->names.size()) {
        std::vector<ik::Transform> rest(skel->local.size());
        for (std::size_t i = 0; i < rest.size(); ++i) {
            int const p = skel->parents[i];
            rest[i] = p >= 0 ? ik::Compose(skel->local[i], rest[static_cast<std::size_t>(p)]) : skel->local[i];
        }
        for (hair::Chain& c : sim.chains) {
            if (c.body < 0 || c.bones.size() < 2) {
                continue;
            }
            c.followRest.clear();
            for (int b : c.bones) {
                ik::Transform r = ik::Relative(rest[static_cast<std::size_t>(b)], rest[static_cast<std::size_t>(c.body)]);
                r.translation = r.translation * (1.0 / kUnitsPerMeterRest);
                c.followRest.push_back(r);
            }
        }
    }
    for (hair::Chain const& c : sim.chains) {
        std::vector<std::uint64_t> keys;
        std::vector<std::string> names;
        for (int b : c.bones) {
            keys.push_back(skeleton::RefBoneKey(hairMesh, b));
            names.push_back(skel->names[static_cast<std::size_t>(b)]);
        }
        sim.keys.push_back(std::move(keys));
        sim.names.push_back(std::move(names));
        sim.anchorNames.push_back(c.anchor >= 0 ? skel->names[static_cast<std::size_t>(c.anchor)] : std::string("?"));
    }
    // Soft-tissue bones (tools/rig's breast and glute bones), by name.
    for (std::size_t i = 0; i < skel->names.size(); ++i) {
        std::string const& n = skel->names[i];
        bool const breast = n.rfind("Breast", 0) == 0;
        bool const glute = n.rfind("Glute", 0) == 0;
        if (breast || glute) {
            Sim::Soft s;
            s.bone = static_cast<int>(i);
            s.key = skeleton::RefBoneKey(hairMesh, s.bone);
            s.name = n;
            s.glute = glute;
            sim.soft.push_back(std::move(s));
        }
    }
}

} // namespace

void Publish(Request request) {
    if (request.targets.size() > static_cast<std::size_t>(kMaxTargets)) {
        request.targets.resize(static_cast<std::size_t>(kMaxTargets));
    }
    std::array<ue3::Object const*, kMaxTargets> wanted{};
    for (std::size_t i = 0; i < request.targets.size(); ++i) {
        wanted[i] = request.targets[i].hair;
    }
    AcquireSRWLockExclusive(&gLock);
    gRequest = std::move(request);
    gThread = GetCurrentThreadId();
    gTimeMs = GetTickCount64();
    ReleaseSRWLockExclusive(&gLock);
    bool const broken = gBroken.load();
    for (int i = 0; i < kMaxTargets; ++i) {
        gWanted[static_cast<std::size_t>(i)].store(broken ? nullptr : wanted[static_cast<std::size_t>(i)], std::memory_order_release);
    }
    // Forget simulations of components no longer asked for.
    for (auto it = gSims.begin(); it != gSims.end();) {
        bool const kept = std::find(wanted.begin(), wanted.end(), it->first) != wanted.end();
        it = kept ? std::next(it) : gSims.erase(it);
    }
}

void Clear() {
    for (auto& w : gWanted) {
        w.store(nullptr, std::memory_order_release);
    }
    AcquireSRWLockExclusive(&gLock);
    gRequest = {};
    gTimeMs = 0;
    ReleaseSRWLockExclusive(&gLock);
    gSims.clear();
    if (gTrace.is_open()) {
        gTrace.close();
    }
}

bool Wants(ue3::Object const* component) {
    if (component == nullptr) {
        return false;
    }
    for (auto const& w : gWanted) {
        if (w.load(std::memory_order_acquire) == component) {
            return true;
        }
    }
    return false;
}

void AfterPose(ue3::Object* component, float deltaTime) {
    if (gBroken.load(std::memory_order_relaxed)) {
        return;
    }
    Target target;
    ik::hair::Settings settings;
    double unitsPerMeter = 100.0;
    ue3::Object* player = nullptr;
    std::vector<hc::BonePart> playerParts;
    bool playerPushes = false;
    bool jiggle = false;
    double jiggleIntensity = 0.0;
    DWORD thread = 0;
    ULONGLONG published = 0;
    bool found = false;
    AcquireSRWLockShared(&gLock);
    for (Target const& t : gRequest.targets) {
        if (t.hair == component) {
            target = t;
            found = true;
            break;
        }
    }
    if (found) {
        settings = gRequest.settings;
        unitsPerMeter = gRequest.unitsPerMeter;
        player = gRequest.player;
        playerParts = gRequest.playerParts;
        playerPushes = gRequest.playerPushes;
        jiggle = gRequest.jiggle;
        jiggleIntensity = gRequest.jiggleIntensity;
        thread = gThread;
        published = gTimeMs;
    }
    ReleaseSRWLockShared(&gLock);
    if (!found || GetCurrentThreadId() != thread) {
        return;  // the game thread only: the simulations live there
    }
    Sim& sim = gSims[component];
    ++sim.info.calls;
    sim.info.hair = component;
    if (GetTickCount64() - published > kMaxRequestAgeMs) {
        Reject(&sim.info, "request too old (the mod stopped publishing)");
        return;
    }

    WorldPose pose;
    if (!ReadWorld(component, unitsPerMeter, pose)) {
        Reject(&sim.info, gBroken.load() ? "access violation reading the hair's pose: stopped" : "the hair's pose isn't computed");
        return;
    }
    int const boneCount = static_cast<int>(pose.bones.size());
    if (sim.asset != target.asset || sim.boneCount != boneCount) {
        Discover(component, target.asset, boneCount, sim);
        sim.info.hair = component;
        if (!sim.logged) {
            sim.logged = true;
            std::string list;
            for (std::size_t c = 0; c < sim.chains.size(); ++c) {
                list += std::format("{}{} <- {}", c == 0 ? "" : "; ", sim.anchorNames[c], [&] {
                    std::string s;
                    for (std::string const& n : sim.names[c]) {
                        s += (s.empty() ? "" : " > ") + n;
                    }
                    return s;
                }());
            }
            std::string soft;
            for (Sim::Soft const& s : sim.soft) {
                soft += (soft.empty() ? "" : ", ") + s.name;
            }
            log::Info("hair stage: {:#x}: {} bones, {} chain(s){}{}; {} soft-tissue bone(s){}{}", reinterpret_cast<std::uintptr_t>(component),
                boneCount, sim.chains.size(), sim.chains.empty() ? "" : ": ", list, sim.soft.size(), soft.empty() ? "" : ": ", soft);
        }
    }
    if (sim.chains.empty() && (sim.soft.empty() || !jiggle)) {
        return;
    }
    // The step's time: the engine's for this pose; a follower posed with its parent may be passed none, then the clock's.
    LARGE_INTEGER nowTicks{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&nowTicks);
    QueryPerformanceFrequency(&frequency);
    double stepSeconds = std::isfinite(deltaTime) && deltaTime > 0.0f ? deltaTime : 0.0;
    if (stepSeconds <= 0.0 && sim.lastCall.QuadPart != 0 && frequency.QuadPart > 0) {
        stepSeconds = (std::min)(static_cast<double>(nowTicks.QuadPart - sim.lastCall.QuadPart) / frequency.QuadPart, 0.1);
        ++sim.measuredSteps;
    }
    sim.lastCall = nowTicks;
    stepSeconds *= gTimeScale.load(std::memory_order_relaxed);

    std::vector<hc::Capsule> body;
    AddCapsules(target.body, target.bodyParts, unitsPerMeter, body);
    // The body's surface fitted from its mesh (the rig tool's spheres): on the body's bones as posed now.
    std::vector<hc::Capsule> surface;
    if (!target.spheres.empty() && target.body != nullptr) {
        WorldPose bodyPose;
        if (ReadWorld(target.body, unitsPerMeter, bodyPose)) {
            ik::body_spheres::Place(target.spheres, bodyPose.bones, 1.0 / unitsPerMeter, surface);
        }
    }
    std::vector<hc::Capsule> others;
    if (playerPushes && player != target.body) {
        AddCapsules(player, playerParts, unitsPerMeter, others);
    }

    std::optional<ik::Affine> const toLocal = ik::InverseAffine(pose.toWorld);
    if (!toLocal) {
        Reject(&sim.info, "the hair's transform is singular");
        return;
    }
    ik::Quat const toLocalRotation = ik::Conjugate(pose.rotation);
    std::vector<int> bones;
    std::vector<skeleton::BoneAtom> atoms;
    std::vector<ik::Transform> posed;
    sim.info.chains.resize(sim.chains.size());
    bool const tracing = gTrace.is_open() && GetTickCount64() < gTraceUntilMs;
    if (gTrace.is_open() && !tracing) {
        gTrace.close();
        log::Info("hair stage: the trace ended");
    }
    for (std::size_t c = 0; c < sim.chains.size(); ++c) {
        hair::Chain& chain = sim.chains[c];
        std::vector<std::uint64_t> const& keys = sim.keys[c];
        if (!KeysMatchGuarded(component, chain.bones.data(), keys.data(), static_cast<int>(keys.size()))) {
            Reject(&sim.info, "the hair's skeleton changed (bone names)");
            sim.asset = nullptr;  // re-discover next call
            return;
        }
        hair::Input in;
        in.animated = pose.bones;
        in.body = body;
        in.surface = surface;
        in.colliders = others;
        in.dt = stepSeconds;
        hair::StepStats const st = hair::Step(chain, settings, in);
        sim.info.collisions += static_cast<std::uint64_t>(st.collisions);
        hair::Pose(chain, pose.bones, posed);
        if (posed.size() != chain.bones.size()) {
            continue;
        }
        for (std::size_t i = 0; i < posed.size(); ++i) {
            ik::Transform const& w = posed[i];
            ik::Quat const q = ik::Normalize(toLocalRotation * w.rotation);
            ik::Vec3 const t = ik::TransformPoint(*toLocal, w.translation * unitsPerMeter);
            skeleton::BoneAtom atom{};
            atom.qx = static_cast<float>(q.x);
            atom.qy = static_cast<float>(q.y);
            atom.qz = static_cast<float>(q.z);
            atom.qw = static_cast<float>(q.w);
            atom.tx = static_cast<float>(t.x);
            atom.ty = static_cast<float>(t.y);
            atom.tz = static_cast<float>(t.z);
            atom.scale = static_cast<float>(w.scale);
            bones.push_back(chain.bones[i]);
            atoms.push_back(atom);
        }
        ChainInfo& ci = sim.info.chains[c];
        ci.anchor = chain.anchor;
        ci.bones = sim.names[c];
        ci.tipOffsetM = hair::TipOffset(chain);
        ci.resets = chain.resets;
        sim.info.peakTipM = (std::max)(sim.info.peakTipM, ci.tipOffsetM);
        if (tracing) {
            double const t = (GetTickCount64() - gTraceStartMs) / 1000.0;
            for (std::size_t p = 0; p < chain.pos.size(); ++p) {
                ik::Vec3 const& s = chain.pos[p];
                ik::Vec3 const& a = chain.target[p];
                gTrace << std::format("{:.4f},{:#x},{},{},{:.5f},{:.5f},{:.5f},{:.5f},{:.5f},{:.5f},{},{}\n", t,
                    reinterpret_cast<std::uintptr_t>(component), c, p, s.x, s.y, s.z, a.x, a.y, a.z, stepSeconds, st.collisions);
            }
        }
    }
    // Soft tissue: each bone a spring in its own animated frame (its rest axes are the body's), its offset and turn written
    // over the animation's pose of it.
    sim.info.soft.resize(jiggle ? sim.soft.size() : 0);
    for (std::size_t s = 0; jiggle && s < sim.soft.size(); ++s) {
        Sim::Soft& soft = sim.soft[s];
        if (soft.bone < 0 || soft.bone >= boneCount ||
            !KeysMatchGuarded(component, &soft.bone, &soft.key, 1)) {
            Reject(&sim.info, "the body's skeleton changed (soft-tissue bone names)");
            sim.asset = nullptr;
            return;
        }
        ik::jiggle::Settings tissue = soft.glute ? ik::jiggle::Glute() : ik::jiggle::Breast();
        tissue.intensity = jiggleIntensity * target.jiggleScale;
        ik::Transform const& animated = pose.bones[static_cast<std::size_t>(soft.bone)];
        ik::jiggle::Input in;
        in.frame.rotation = animated.rotation;
        in.frame.translation = animated.translation;
        in.dt = stepSeconds;
        ik::jiggle::Output const out = ik::jiggle::Step(soft.state, tissue, in);
        ik::Quat const worldRotation = ik::Normalize(animated.rotation * out.rotation);
        ik::Vec3 const worldPlace = animated.translation + ik::Rotate(animated.rotation, out.translation);
        ik::Quat const q = ik::Normalize(toLocalRotation * worldRotation);
        ik::Vec3 const t = ik::TransformPoint(*toLocal, worldPlace * unitsPerMeter);
        skeleton::BoneAtom atom{};
        atom.qx = static_cast<float>(q.x);
        atom.qy = static_cast<float>(q.y);
        atom.qz = static_cast<float>(q.z);
        atom.qw = static_cast<float>(q.w);
        atom.tx = static_cast<float>(t.x);
        atom.ty = static_cast<float>(t.y);
        atom.tz = static_cast<float>(t.z);
        atom.scale = static_cast<float>(animated.scale);
        bones.push_back(soft.bone);
        atoms.push_back(atom);
        SoftInfo& si = sim.info.soft[s];
        si.bone = soft.name;
        si.offsetM = ik::Length(out.shown);
        si.peakM = (std::max)(si.peakM, si.offsetM);
        si.resets = soft.state.resets;
        si.clampHits = soft.state.clampHits;
        if (tracing) {
            double const tt = (GetTickCount64() - gTraceStartMs) / 1000.0;
            gTrace << std::format("{:.4f},{:#x},soft,{},{:.5f},{:.5f},{:.5f},{:.5f},{:.5f},{:.5f},{},0\n", tt,
                reinterpret_cast<std::uintptr_t>(component), soft.name, out.shown.x, out.shown.y, out.shown.z, animated.translation.x,
                animated.translation.y, animated.translation.z, stepSeconds);
        }
    }
    if (!bones.empty()) {
        if (WriteGuarded(component, boneCount, bones.data(), atoms.data(), static_cast<int>(bones.size()))) {
            ++sim.info.written;
        } else {
            Reject(&sim.info, gBroken.load() ? "access violation writing the hair's pose: stopped" : "the hair's pose changed size");
        }
    }
    if (gBroken.load()) {
        for (auto& w : gWanted) {
            w.store(nullptr, std::memory_order_release);
        }
    }
}

Stats GetStats() {
    Stats s;
    for (auto const& [component, sim] : gSims) {
        s.targets.push_back(sim.info);
    }
    s.rejected = gRejected;
    s.broken = gBroken.load();
    return s;
}

void Trace(double seconds, std::string const& path) {
    if (gTrace.is_open()) {
        gTrace.close();
    }
    if (seconds <= 0.0) {
        return;
    }
    gTrace.open(path, std::ios::out | std::ios::trunc);
    if (gTrace.is_open()) {
        gTrace << "t,component,chain,point,simX,simY,simZ,animX,animY,animZ,dt,collisions\n";
        gTraceStartMs = GetTickCount64();
        gTraceUntilMs = gTraceStartMs + static_cast<ULONGLONG>(seconds * 1000.0);
    }
}

bool Kick(ue3::Object const* component, ik::Vec3 velocityMS) {
    auto it = gSims.find(component);
    if (it == gSims.end() || it->second.chains.empty()) {
        return false;
    }
    // Verlet keeps velocity as pos - prev per 1/120 s substep.
    ik::Vec3 const perStep = velocityMS * (1.0 / 120.0);
    for (hair::Chain& c : it->second.chains) {
        for (std::size_t i = 1; i < c.prev.size(); ++i) {
            c.prev[i] = c.prev[i] - perStep;
        }
    }
    return true;
}

void SetTimeScale(double scale) {
    gTimeScale.store(std::clamp(scale, 0.0, 4.0));
}

void ResetPeriod() {
    for (auto& [component, sim] : gSims) {
        sim.info.peakTipM = 0.0;
    }
}

} // namespace mevr::engine::hair_pose
