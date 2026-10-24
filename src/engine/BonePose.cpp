// SPDX-License-Identifier: GPL-3.0-only
#include "engine/BonePose.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/TwoHand.hpp"
#include "engine/Addresses.hpp"
#include "engine/Engine.hpp"
#include "engine/Skeleton.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>
#include <intrin.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace mevr::engine::bone_pose {
namespace {

// The skeletal mesh component, as our reflection layer's pointer.
using Mesh = ue3::Object;

constexpr char const* kHookName = "USkeletalMeshComponent::UpdateSkelPose";

// Verified as a function start in .pdata and by its code (research/14): it sizes SpaceBases and
// LocalAtoms to the RefSkeleton with 0x20-byte elements, blends the AnimTree into LocalAtoms and
// calls ComposeSkeleton (0x580A60). USkeletalMeshComponent::Tick calls it at 0x58D709 with the
// frame's DeltaTime; Attach and the various force-update paths call it with 0. LE1 only (engine/Addresses.hpp).
constexpr std::uint32_t kUpdateSkelPoseRva = addresses::kUpdateSkelPose;

// (this, DeltaTime, bTickFaceFX); callers also set r9, which the function never reads. All four
// register arguments are passed through unchanged.
using tUpdateSkelPose = void(Mesh* self, float deltaTime, unsigned tickFaceFx, unsigned unused);
tUpdateSkelPose* gOrig = nullptr;  // written before the hook goes live (core/Hook)
bool gInstalled = false;
std::uintptr_t gModuleBase = 0;
std::atomic<int> gInFlight{0};

// A request is only good for the mesh it was made for: the same component address, owned by the
// same actor, in the same world, and recent. Across a level load the mod doesn't tick, and a new
// component with the same skeleton can land at a freed address (another actor's, or one in the
// new level); the owner and world catch that, the age catches anything else the mod missed.
// Published every mod tick, so a write normally uses the previous tick's request.
constexpr ULONGLONG kMaxRequestAgeMs = 500;

// What to write. The hook compares the mesh address lock-free, then checks the rest and copies
// the request under the lock and works on the copy.
std::atomic<Mesh const*> gMesh{nullptr};
SRWLOCK gRequestLock = SRWLOCK_INIT;
Request gRequest;
void const* gRequestOwner = nullptr;  // compared by address only, never dereferenced
void const* gRequestWorld = nullptr;  // engine::World() when published; compared by address only
ULONGLONG gRequestTimeMs = 0;
std::atomic<bool> gBroken{false};  // an access violation in the writer: writes stop for good

// Stats (any thread; the hook normally runs on the game thread).
std::atomic<unsigned> gCalls{0};
std::atomic<unsigned> gPlayerCalls{0};
std::atomic<unsigned> gWrites{0};
std::atomic<std::uint64_t> gTotalWrites{0};
std::atomic<unsigned> gRejects{0};
// Writes in which a solve moved a kept bone (Request::keep), which then kept its animated entry.
std::atomic<unsigned> gKeptMoves{0};
std::atomic<std::uint64_t> gTotalKeptMoves{0};
std::atomic<char const*> gLastReject{nullptr};
struct Slot {
    std::atomic<std::uintptr_t> key{0};
    std::atomic<unsigned> count{0};
};
Slot gCallers[4];
Slot gThreads[4];

struct Sample {
    bool valid = false;
    ik::ArmResult result;
    ik::Vec3 target;
    float deltaTime = 0.0f;
};
struct TorsoSample {
    bool valid = false;
    ik::TorsoResult result;
    double collars[2] = {0.0, 0.0};  // degrees each clavicle turned
    double stretch[2] = {1.0, 1.0};
};
SRWLOCK gSampleLock = SRWLOCK_INIT;
Sample gSamples[2];
TorsoSample gTorso;
// The torso's last result for LatestTorso (not cleared by the heartbeat).
Mesh const* gLatestTorsoMesh = nullptr;
ik::TorsoResult gLatestTorso;
ULONGLONG gLatestTorsoTimeMs = 0;
// The head bone before our writes (AnimatedHead).
Mesh const* gAnimatedMesh = nullptr;
ik::Vec3 gAnimatedHead;
ULONGLONG gAnimatedTimeMs = 0;
// Both hands before our writes (AnimatedHands), and the support grip's last result.
Mesh const* gHandsMesh = nullptr;
Hands gAnimatedHands;
ULONGLONG gHandsTimeMs = 0;
Mesh const* gSupportMesh = nullptr;
SupportSample gSupport;
ULONGLONG gSupportTimeMs = 0;
// Hands closer than Support::minSpanMeters: the distance reported, so the hold lets go.
constexpr double kCrossedMeters = 99.0;
bool gFirstWriteLogged = false;  // game thread
std::atomic<bool> gFirstWriteReady{false};

// Counts `key` in the first slot that holds it or is free (distinct callers / threads).
void Count(Slot (&slots)[4], std::uintptr_t key) {
    for (Slot& slot : slots) {
        std::uintptr_t current = slot.key.load(std::memory_order_relaxed);
        if (current == 0 && slot.key.compare_exchange_strong(current, key, std::memory_order_relaxed)) {
            current = key;
        }
        if (current == key) {
            slot.count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
}

void Reject(char const* why) {
    gRejects.fetch_add(1, std::memory_order_relaxed);
    gLastReject.store(why, std::memory_order_relaxed);
}

bool IsFunctionStart(void* address) {
    DWORD64 imageBase = 0;
    auto const* const entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &imageBase, nullptr);
    return entry != nullptr && imageBase + entry->BeginAddress == reinterpret_cast<DWORD64>(address);
}

std::string EntryBytes(void const* address) {
    auto const* const bytes = static_cast<unsigned char const*>(address);
    std::string text;
    for (int i = 0; i < 16; ++i) {
        text += std::format("{:02x} ", bytes[i]);
    }
    return text;
}

// The wrist target for an arm, in component space.
bool Goal(Request const& request, std::span<ik::Transform const> pose, int side, ik::Vec3& wrist) {
    Arm const& arm = request.arms[side];
    if (arm.target == Target::Component) {
        wrist = arm.wrist;
        return true;
    }
    // Chest: from the shoulders' midpoint (or this shoulder alone), in this arm's lengths.
    ik::Vec3 const shoulder = pose[arm.chain.upper].translation;
    double const length = ik::Length(pose[arm.chain.fore].translation - shoulder) +
        ik::Length(pose[arm.chain.hand].translation - pose[arm.chain.fore].translation);
    Arm const& other = request.arms[1 - side];
    ik::Vec3 outward = side == 1 ? request.body.right : request.body.right * -1.0;
    ik::Vec3 chest = shoulder - outward * (0.35 * length);  // about half the shoulder width
    if (other.chain.upper >= 0 && other.chain.upper < static_cast<int>(pose.size())) {
        chest = (shoulder + pose[other.chain.upper].translation) * 0.5;
    }
    wrist = chest + (request.body.forward * arm.wrist.x + outward * arm.wrist.y + request.body.up * arm.wrist.z) * length;
    return std::isfinite(wrist.x) && std::isfinite(wrist.y) && std::isfinite(wrist.z);
}

// ActorComponent.Owner, found through our reflection layer on the game thread (Install, Publish) and only
// read by the hook. -1: not found (no request is then published).
std::atomic<std::int32_t> gOwnerOffset{-1};

bool ResolveOwner() {
    if (gOwnerOffset.load(std::memory_order_acquire) >= 0) {
        return true;
    }
    if (!ue3::runtime::Ready()) {
        return false;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    std::optional<ue3::PropertyInfo> const owner = r.PropertyOf(r.FindClass("Engine.ActorComponent"), "Owner");
    ue3::runtime::FlushNotes();
    if (!owner || owner->elementSize != static_cast<std::int32_t>(sizeof(void*))) {
        return false;
    }
    gOwnerOffset.store(owner->offset, std::memory_order_release);
    return true;
}

void const* OwnerOf(Mesh const* mesh) {
    std::int32_t const offset = gOwnerOffset.load(std::memory_order_acquire);
    return mesh != nullptr && offset >= 0 ? ue3::Reflection::Read<void const*>(mesh, static_cast<std::uint32_t>(offset)) : nullptr;
}

void const* CurrentWorld() {
    return engine::World();
}

// The span captured when the current hold began (ik::CapturedSpan; the writer's thread).
thread_local std::uint32_t tCapturedHold = 0;
thread_local std::optional<ik::Vec3> tCapturedSpan;
thread_local bool tCapturedCalibrated = false;

// The support grip on this write's goals (Request::support). Measures the left goal's distance from
// the grab zone (a capsule along the gun and the foregrip; at the one-hand pose while not held; at the
// two-hand pose while held, like Overlord's grip_presenter: a correctly held rifle must not drop when
// the right wrist turns), then blends the right hand toward the two-hand rotation and the left wrist
// onto the foregrip. Adapted from Overlord's grip_presenter.hpp (https://github.com/Aeka0/Overlord,
// GPL-3.0, Aeka0).
SupportSample ApplySupport(Support const& s, double handScale, ik::ArmGoal& left, ik::ArmGoal& right) {
    SupportSample out;
    double const unitsPerMeter = s.unitsPerMeter > 1e-6 && std::isfinite(s.unitsPerMeter) ? s.unitsPerMeter : 50.0;
    double const blend = std::clamp(std::isfinite(s.blend) ? s.blend : 0.0, 0.0, 1.0);
    ik::Vec3 const span = s.foregrip.translation * handScale;
    ik::Quat const one = right.rotation;
    ik::Vec3 const controller = left.wrist;
    double const minSpan = s.minSpanMeters * unitsPerMeter;
    // Two-hand aim swings the line from the right hand to where the left took hold (captured at the
    // hold's first write, on the one-hand pose), not to the foregrip: no swing when taking hold. Kept
    // while the hold blends out after a release, so letting go doesn't swing it either.
    if (s.hold != 0 && s.hold != tCapturedHold) {
        tCapturedHold = s.hold;
        bool const calibrated = s.useCalibratedSpan && std::isfinite(s.calibratedSpan.x) && std::isfinite(s.calibratedSpan.y) &&
            std::isfinite(s.calibratedSpan.z) && ik::Length(s.calibratedSpan) > std::max(minSpan, 0.5);
        tCapturedCalibrated = s.twoHandAim && calibrated;
        tCapturedSpan = !s.twoHandAim ? std::nullopt
            : calibrated            ? std::optional<ik::Vec3>(s.calibratedSpan)
                                    : ik::CapturedSpan(one, right.wrist, controller, ik::Length(span), minSpan);
    } else if (s.hold == 0 && blend <= 0.0) {
        tCapturedHold = 0;
        tCapturedSpan.reset();
        tCapturedCalibrated = false;
    }
    ik::Vec3 const aimSpan = tCapturedSpan ? *tCapturedSpan : span;
    out.captured = tCapturedSpan.has_value();
    out.calibrated = out.captured && tCapturedCalibrated;
    std::optional<ik::Quat> two;
    if (s.twoHandAim) {
        two = ik::TwoHandRotation(one, aimSpan, right.wrist, controller, minSpan);
    }
    ik::Quat const check = blend > 0.0 && two ? *two : one;
    ik::Vec3 const foregripAt = right.wrist + ik::Rotate(check, span);
    out.foregripMeters = ik::Length(controller - foregripAt) / unitsPerMeter;
    if (s.twoHandAim && !two) {
        out.distanceMeters = kCrossedMeters;  // the hands define no axis
    } else {
        // The grab zone: the capsule along the gun (as posed for the check) and the foregrip.
        auto const zone = s.hasGun ? ik::MakeSupportZone(right.wrist, check, s.barrel, s.muzzle, s.foregrip.translation, handScale,
                                         s.frontMeters * unitsPerMeter, s.pastMeters * unitsPerMeter)
                                   : std::nullopt;
        out.distanceMeters = (zone ? ik::DistanceToZone(controller, *zone) : ik::Length(controller - foregripAt)) / unitsPerMeter;
        if (zone) {
            ik::Vec3 const axis = ik::Normalize(zone->end - zone->start, ik::Rotate(check, s.barrel));
            out.alongMeters = ik::Dot(controller - right.wrist, axis) / unitsPerMeter;
        }
    }
    out.blend = blend;
    if (blend <= 0.0) {
        return out;
    }
    ik::Quat const held = two ? *two : one;
    ik::Quat const rotation = blend >= 1.0 ? held : ik::Slerp(one, held, blend);
    right.rotation = rotation;
    out.twoHand = two.has_value();
    out.swingDegrees = ik::AngleBetween(one, rotation) * 180.0 / 3.14159265358979323846;
    ik::Vec3 const onGun = right.wrist + ik::Rotate(rotation, span);
    left.wrist = controller + (onGun - controller) * blend;
    if (s.hasRotation) {
        ik::Quat const hand = ik::Normalize(rotation * s.foregrip.rotation);
        left.rotation = left.hasRotation ? ik::Slerp(left.rotation, hand, blend) : hand;
        left.hasRotation = true;
    }
    return out;
}

// Copies the request for `self`, or false if there's nothing to write for it.
bool TakeRequest(Mesh const* self, Request& copy) {
    void const* const owner = OwnerOf(self);
    void const* const world = CurrentWorld();
    ULONGLONG const now = GetTickCount64();
    char const* stale = nullptr;
    AcquireSRWLockShared(&gRequestLock);
    bool matches = gRequest.mesh == self;
    if (matches) {
        if (gRequestOwner != owner) {
            stale = "the mesh's owner changed (another component at the same address)";
        } else if (gRequestWorld != world) {
            stale = "the world changed since the request (level load)";
        } else if (now - gRequestTimeMs > kMaxRequestAgeMs) {
            stale = "request too old (the mod stopped publishing)";
        }
        matches = stale == nullptr;
    }
    if (matches) {
        copy.mesh = gRequest.mesh;
        copy.boneCount = gRequest.boneCount;
        copy.parents.assign(gRequest.parents.begin(), gRequest.parents.end());
        copy.body = gRequest.body;
        copy.arms = gRequest.arms;
        copy.torso.enabled = gRequest.torso.enabled;
        copy.torso.chain.spine.assign(gRequest.torso.chain.spine.begin(), gRequest.torso.chain.spine.end());
        copy.torso.chain.collars = gRequest.torso.chain.collars;
        copy.torso.keys.assign(gRequest.torso.keys.begin(), gRequest.torso.keys.end());
        copy.torso.goal = gRequest.torso.goal;
        copy.torso.collars = gRequest.torso.collars;
        copy.maxStretch = gRequest.maxStretch;
        copy.head = gRequest.head;
        copy.support = gRequest.support;
        copy.keep.assign(gRequest.keep.begin(), gRequest.keep.end());
        copy.keepKeys.assign(gRequest.keepKeys.begin(), gRequest.keepKeys.end());
    }
    ReleaseSRWLockShared(&gRequestLock);
    if (stale != nullptr) {
        // Stop matching this address until the mod publishes again.
        Mesh const* expected = self;
        gMesh.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
        Reject(stale);
    }
    return matches;
}

void Write(Mesh* self, float deltaTime) {
    thread_local Request request;
    thread_local std::vector<ik::Transform> pose;
    if (!TakeRequest(self, request)) {
        return;  // republished for another mesh meanwhile
    }
    // The same skeleton as when the request was made: bone count, pose size and the chains' names.
    Mesh* const mesh = self;
    int const count = skeleton::RefBoneCount(mesh);
    std::span<skeleton::BoneAtom> const bases = skeleton::SpaceBases(mesh);
    if (count != request.boneCount || static_cast<int>(bases.size()) != count ||
        static_cast<int>(request.parents.size()) != count) {
        Reject(bases.empty() ? "pose not computed" : "skeleton changed (bone count)");
        return;
    }
    for (Arm const& arm : request.arms) {
        if (arm.target == Target::Off && arm.chain.upper < 0) {
            continue;
        }
        int const bones[3] = {arm.chain.upper, arm.chain.fore, arm.chain.hand};
        for (int i = 0; i < 3; ++i) {
            if (bones[i] < 0 || bones[i] >= count || skeleton::RefBoneKey(mesh, bones[i]) != arm.keys[i]) {
                Reject("skeleton changed (arm bone names)");
                return;
            }
        }
    }
    if (request.keepKeys.size() != request.keep.size()) {
        Reject("kept bones without names");
        return;
    }
    for (std::size_t k = 0; k < request.keep.size(); ++k) {
        int const bone = request.keep[k];
        if (bone < 0 || bone >= count || skeleton::RefBoneKey(mesh, bone) != request.keepKeys[k]) {
            Reject("skeleton changed (kept bone names)");
            return;
        }
    }
    pose.resize(bases.size());
    for (std::size_t i = 0; i < bases.size(); ++i) {
        pose[i] = skeleton::ToTransform(bases[i]);
    }
    // The kept bones as the animation left them, to tell whether a solve moved one.
    thread_local std::vector<ik::Transform> kept;
    kept.clear();
    for (int bone : request.keep) {
        kept.push_back(pose[bone]);
    }
    if (request.head >= 0 && request.head < count) {
        AcquireSRWLockExclusive(&gSampleLock);
        gAnimatedMesh = self;
        gAnimatedHead = pose[request.head].translation;
        gAnimatedTimeMs = GetTickCount64();
        ReleaseSRWLockExclusive(&gSampleLock);
    }
    int const leftHand = request.arms[0].chain.hand;
    int const rightHand = request.arms[1].chain.hand;
    if (leftHand >= 0 && leftHand < count && rightHand >= 0 && rightHand < count) {
        AcquireSRWLockExclusive(&gSampleLock);
        gHandsMesh = self;
        gAnimatedHands = {pose[leftHand], pose[rightHand]};
        gHandsTimeMs = GetTickCount64();
        ReleaseSRWLockExclusive(&gSampleLock);
    }
    int first = count;
    ik::BodyFrame body = request.body;
    TorsoSample torso;
    Torso const& t = request.torso;
    if (t.enabled) {
        // The same skeleton: the spine and clavicle names as when the request was made.
        bool same = t.keys.size() == t.chain.spine.size() + 2;
        for (std::size_t k = 0; same && k < t.chain.spine.size(); ++k) {
            int const bone = t.chain.spine[k];
            same = bone >= 0 && bone < count && skeleton::RefBoneKey(mesh, bone) == t.keys[k];
        }
        for (int side = 0; same && side < 2; ++side) {
            int const bone = t.chain.collars[side];
            same = bone < 0 || (bone < count && skeleton::RefBoneKey(mesh, bone) == t.keys[t.chain.spine.size() + side]);
        }
        if (!same) {
            Reject("skeleton changed (spine bone names)");
        } else {
            torso.valid = true;
            torso.result = ik::PoseTorso(pose, request.parents, t.chain, request.arms[0].chain, request.arms[1].chain, body, t.goal);
            if (!torso.result.ok) {
                Reject(torso.result.error);
            } else {
                first = (std::min)(first, torso.result.firstBone >= 0 ? torso.result.firstBone : t.chain.spine.front());
                body.forward = torso.result.facing;
                body.right = ik::Normalize(ik::Cross(body.up, body.forward), body.right);
            }
        }
    }
    // Both goals first (each clavicle moves only its own arm), so the support grip can tie them.
    std::array<ik::ArmGoal, 2> goals;
    std::array<bool, 2> haveGoal{};
    for (int side : {0, 1}) {
        Arm const& arm = request.arms[side];
        if (arm.target == Target::Off) {
            continue;
        }
        ik::ArmGoal& goal = goals[side];
        if (!Goal(request, pose, side, goal.wrist)) {
            Reject("target not finite");
            continue;
        }
        if (arm.target == Target::Component && t.enabled && t.collars && t.chain.collars[side] >= 0) {
            torso.collars[side] = ik::PoseCollar(pose, request.parents, t.chain.collars[side], arm.chain, goal.wrist);
            if (torso.collars[side] > 0.0) {
                first = (std::min)(first, t.chain.collars[side]);
            }
        }
        if (arm.target == Target::Component && arm.reachScale != 1.0 && std::isfinite(arm.reachScale) && arm.reachScale > 0.0) {
            ik::Vec3 const shoulder = pose[arm.chain.upper].translation;
            goal.wrist = shoulder + (goal.wrist - shoulder) * arm.reachScale;
        }
        goal.hasRotation = arm.hasRotation;
        goal.rotation = arm.rotation;
        goal.maxStretch = arm.target == Target::Component ? request.maxStretch : 1.0;
        haveGoal[side] = true;
    }
    bool const support = request.support.enabled && haveGoal[0] && haveGoal[1] && goals[1].hasRotation &&
        request.arms[0].target == Target::Component && request.arms[1].target == Target::Component;
    // The sight alignment assist: the gun (the right hand) moves, and the left hand with it as far as it
    // holds the gun, before the support grip ties them (so its line between the hands doesn't turn).
    ik::Vec3 const shift = request.gunShift;
    if (haveGoal[1] && request.arms[1].target == Target::Component && std::isfinite(shift.x) && std::isfinite(shift.y) &&
        std::isfinite(shift.z) && ik::Length(shift) > 0.0) {
        goals[1].wrist = goals[1].wrist + shift;
        if (support && haveGoal[0]) {
            // Held: all of it (the line between the hands, and the span captured at the grab, stay as they
            // are); blending out after a release: fading with the blend.
            double const blend = std::clamp(std::isfinite(request.support.blend) ? request.support.blend : 0.0, 0.0, 1.0);
            goals[0].wrist = goals[0].wrist + shift * (request.support.hold != 0 ? 1.0 : blend);
        }
    }
    SupportSample supportSample;
    if (support) {
        double const handScale = pose[rightHand].scale > 1e-6 ? pose[rightHand].scale : 1.0;
        supportSample = ApplySupport(request.support, handScale, goals[0], goals[1]);
    }
    for (int side : {0, 1}) {
        Arm const& arm = request.arms[side];
        if (!haveGoal[side]) {
            continue;
        }
        ik::ArmGoal const& goal = goals[side];
        ik::ArmResult const result = ik::PoseArm(pose, request.parents, arm.chain, side, body, goal);
        if (side == 0 && support) {
            supportSample.leftErrorUnits = ik::Length(pose[leftHand].translation - goal.wrist);
        }
        torso.stretch[side] = result.stretch;
        AcquireSRWLockExclusive(&gSampleLock);
        gSamples[side] = {true, result, goal.wrist, deltaTime};
        ReleaseSRWLockExclusive(&gSampleLock);
        if (!result.ok) {
            Reject(result.error);
            continue;
        }
        first = (std::min)(first, arm.chain.upper);
    }
    if (torso.valid) {
        AcquireSRWLockExclusive(&gSampleLock);
        gTorso = torso;
        gLatestTorsoMesh = self;
        gLatestTorso = torso.result;
        gLatestTorsoTimeMs = GetTickCount64();
        ReleaseSRWLockExclusive(&gSampleLock);
    }
    if (support) {
        AcquireSRWLockExclusive(&gSampleLock);
        gSupportMesh = self;
        gSupport = supportSample;
        gSupportTimeMs = GetTickCount64();
        ReleaseSRWLockExclusive(&gSampleLock);
    }
    if (first >= count) {
        return;
    }
    // The kept bones are never written: their entries stay the animation's, bit for bit, whatever
    // the solves did (they only move one that hangs under a bone they turn).
    bool movedKept = false;
    for (std::size_t k = 0; k < request.keep.size(); ++k) {
        ik::Transform const& now = pose[request.keep[k]];
        ik::Transform const& was = kept[k];
        movedKept = movedKept || now.translation.x != was.translation.x || now.translation.y != was.translation.y ||
            now.translation.z != was.translation.z || now.rotation.x != was.rotation.x || now.rotation.y != was.rotation.y ||
            now.rotation.z != was.rotation.z || now.rotation.w != was.rotation.w || now.scale != was.scale;
    }
    if (movedKept) {
        gKeptMoves.fetch_add(1, std::memory_order_relaxed);
        gTotalKeptMoves.fetch_add(1, std::memory_order_relaxed);
    }
    for (int i = first; i < count; ++i) {
        if (std::find(request.keep.begin(), request.keep.end(), i) == request.keep.end()) {
            bases[i] = skeleton::ToAtom(pose[i]);
        }
    }
    write_journal::Wrote("BonePose.SpaceBases", &bases[static_cast<std::size_t>(first)],
        static_cast<std::size_t>(count - first) * sizeof(skeleton::BoneAtom));
    gWrites.fetch_add(1, std::memory_order_relaxed);
    gTotalWrites.fetch_add(1, std::memory_order_relaxed);
    gFirstWriteReady.store(true, std::memory_order_release);
}

// No C++ objects here, so it can catch an access violation: writes then stop for good instead of
// taking the game down.
void WriteGuarded(Mesh* self, float deltaTime) {
    __try {
        Write(self, deltaTime);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gBroken.store(true);
        gMesh.store(nullptr);
        Reject("access violation in the writer: writes stopped");
    }
}

void AfterUpdateSkelPose(Mesh* self, float deltaTime, std::uintptr_t caller) {
    gCalls.fetch_add(1, std::memory_order_relaxed);
    if (self != nullptr && self == gMesh.load(std::memory_order_acquire) && !gBroken.load(std::memory_order_relaxed)) {
        gPlayerCalls.fetch_add(1, std::memory_order_relaxed);
        Count(gCallers, caller);
        Count(gThreads, GetCurrentThreadId());
        WriteGuarded(self, deltaTime);
    }
}

// No C++ objects with destructors here, so __finally keeps the in-flight count right even when an
// exception unwinds through the engine's call (a count left up would keep the mod pinned).
void UpdateSkelPoseHook(Mesh* self, float deltaTime, unsigned tickFaceFx, unsigned unused) {
    std::uintptr_t const caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    gInFlight.fetch_add(1, std::memory_order_acquire);
    __try {
        gOrig(self, deltaTime, tickFaceFx, unused);
        AfterUpdateSkelPose(self, deltaTime, caller);
    } __finally {
        gInFlight.fetch_sub(1, std::memory_order_release);
    }
}

std::string Format(ik::Vec3 v) {
    return std::format("({:.1f}, {:.1f}, {:.1f})", v.x, v.y, v.z);
}

std::string Describe(Sample const& sample) {
    ik::ArmResult const& r = sample.result;
    if (!r.ok) {
        return std::format("not written ({}), target {}", r.error != nullptr ? r.error : "?", Format(sample.target));
    }
    return std::format("target {}, wrist {} -> {} ({:.1f} from target{}), elbow {} -> {}, shoulder {}, upper {:.1f} lower {:.1f}, {} bones",
        Format(sample.target), Format(r.oldWrist), Format(r.newWrist), ik::Length(r.newWrist - sample.target),
        r.reached ? "" : ", out of reach", Format(r.oldElbow), Format(r.newElbow), Format(r.shoulder), r.upperLength,
        r.lowerLength, r.bonesWritten);
}

std::string DescribeTorso(TorsoSample const& sample) {
    ik::TorsoResult const& r = sample.result;
    if (!r.ok) {
        return std::format("not written ({})", r.error != nullptr ? r.error : "?");
    }
    return std::format("shoulders {} -> {} (wanted {}, {:.1f} short{}), twist {:.0f} deg, bend {:.0f} deg, body turn {:.0f} of {:.0f} deg, "
                       "body shift {:.1f} units{}, reference {:.1f} from its goal, clavicles {:.0f}/{:.0f} deg, arm stretch {:.2f}/{:.2f}, "
                       "{} bones",
        Format(r.oldShoulders), Format(r.newShoulders), Format(r.wantedShoulders), ik::Length(r.newShoulders - r.wantedShoulders),
        r.limited ? ", limited" : "", r.twistDegrees, r.bendDegrees, r.bodyTurnDegrees, r.wantedTurnDegrees, ik::Length(r.bodyShift),
        r.shiftLimited ? std::format(" (wanted {:.1f}: capped)", ik::Length(r.wantedBodyShift)) : std::string(),
        ik::Length(r.newReference - r.goalReference), sample.collars[0], sample.collars[1], sample.stretch[0], sample.stretch[1],
        r.bonesWritten);
}

std::string Slots(Slot (&slots)[4], bool rva) {
    std::string text;
    for (Slot& slot : slots) {
        std::uintptr_t const key = slot.key.exchange(0);
        unsigned const count = slot.count.exchange(0);
        if (key != 0) {
            text += rva ? std::format(" exe+0x{:X}x{}", key - gModuleBase, count) : std::format(" {}x{}", key, count);
        }
    }
    return text;
}

} // namespace

bool Install(hook::Context& init) {
    if (kUpdateSkelPoseRva == 0) {
        log::Warn("arm IK: no UpdateSkelPose address for this game; arm writeback unavailable");
        return false;
    }
    if (!skeleton::Supported()) {
        log::Warn("arm IK: bone layouts unknown for this game; arm writeback unavailable");
        return false;
    }
    void* const target = init.Resolve(kUpdateSkelPoseRva);
    if (target == nullptr || !IsFunctionStart(target)) {
        log::Error("arm IK: UpdateSkelPose at {} is not a function start; not hooking", target);
        return false;
    }
    gModuleBase = init.ModuleBase();
    if (!ResolveOwner()) {
        log::Warn("arm IK: Engine.ActorComponent.Owner not found through our reflection layer; arm writeback stays idle");
    }
    log::Info("arm IK: UpdateSkelPose at {} (entry bytes {})", target, EntryBytes(target));
    if (!hook::Install(init, kHookName, reinterpret_cast<tUpdateSkelPose*>(target), &UpdateSkelPoseHook, gOrig)) {
        log::Error("arm IK: UpdateSkelPose hook failed");
        return false;
    }
    gInstalled = true;
    log::Info("hooked {}", kHookName);
    return true;
}

bool Uninstall(hook::Context& init, bool waitForCalls) {
    if (!gInstalled) {
        return true;
    }
    Clear();
    if (!hook::Uninstall(init, kHookName)) {
        return false;  // still live, and still calling gOrig
    }
    gInstalled = false;
    // A call already inside the detour finishes with the old code: this module must outlive it.
    for (int i = 0; waitForCalls && i < 200 && gInFlight.load() > 0; ++i) {
        Sleep(1);
    }
    bool const drained = gInFlight.load() == 0;
    if (!drained && waitForCalls) {
        log::Error("arm IK: {} UpdateSkelPose call(s) still in the hook 200 ms after unhooking", gInFlight.load());
    }
    gBroken = false;
    gFirstWriteLogged = false;
    gFirstWriteReady = false;
    log::Info("unhooked {}", kHookName);
    return drained;
}

bool Installed() {
    return gInstalled;
}

void Publish(Request request) {
    Mesh const* const mesh = ResolveOwner() ? request.mesh : nullptr;  // without the owner check, nothing is written
    void const* const owner = OwnerOf(mesh);
    void const* const world = CurrentWorld();
    AcquireSRWLockExclusive(&gRequestLock);
    if (gRequest.mesh != request.mesh || gRequestOwner != owner) {
        gFirstWriteLogged = false;
        gFirstWriteReady = false;
    }
    gRequest = std::move(request);
    gRequestOwner = owner;
    gRequestWorld = world;
    gRequestTimeMs = GetTickCount64();
    ReleaseSRWLockExclusive(&gRequestLock);
    gMesh.store(gBroken.load() ? nullptr : mesh, std::memory_order_release);
}

void Clear() {
    gMesh.store(nullptr, std::memory_order_release);
    AcquireSRWLockExclusive(&gRequestLock);
    gRequest = {};
    gRequestOwner = gRequestWorld = nullptr;
    gRequestTimeMs = 0;
    ReleaseSRWLockExclusive(&gRequestLock);
}

void LogFirstWrite() {
    if (gFirstWriteLogged || !gFirstWriteReady.load(std::memory_order_acquire)) {
        return;
    }
    gFirstWriteLogged = true;
    AcquireSRWLockShared(&gSampleLock);
    Sample const samples[2] = {gSamples[0], gSamples[1]};
    TorsoSample const torso = gTorso;
    ReleaseSRWLockShared(&gSampleLock);
    if (torso.valid) {
        log::Info("arm IK: first write, torso (component space): {}", DescribeTorso(torso));
    }
    for (int side : {0, 1}) {
        if (samples[side].valid) {
            log::Info("arm IK: first write, {} arm (component space): {}", side == 0 ? "left" : "right", Describe(samples[side]));
        }
    }
}

std::optional<ik::Vec3> AnimatedHead() {
    Mesh const* const mesh = gMesh.load(std::memory_order_acquire);
    AcquireSRWLockShared(&gSampleLock);
    bool const fresh = mesh != nullptr && gAnimatedMesh == mesh && GetTickCount64() - gAnimatedTimeMs <= kMaxRequestAgeMs;
    ik::Vec3 const head = gAnimatedHead;
    ReleaseSRWLockShared(&gSampleLock);
    return fresh ? std::optional<ik::Vec3>(head) : std::nullopt;
}

std::optional<Hands> AnimatedHands() {
    Mesh const* const mesh = gMesh.load(std::memory_order_acquire);
    AcquireSRWLockShared(&gSampleLock);
    bool const fresh = mesh != nullptr && gHandsMesh == mesh && GetTickCount64() - gHandsTimeMs <= kMaxRequestAgeMs;
    Hands const hands = gAnimatedHands;
    ReleaseSRWLockShared(&gSampleLock);
    return fresh ? std::optional<Hands>(hands) : std::nullopt;
}

std::optional<SupportSample> LatestSupport() {
    Mesh const* const mesh = gMesh.load(std::memory_order_acquire);
    AcquireSRWLockShared(&gSampleLock);
    bool const fresh = mesh != nullptr && gSupportMesh == mesh && GetTickCount64() - gSupportTimeMs <= kMaxRequestAgeMs;
    SupportSample const sample = gSupport;
    ReleaseSRWLockShared(&gSampleLock);
    return fresh ? std::optional<SupportSample>(sample) : std::nullopt;
}

std::optional<ik::TorsoResult> LatestTorso() {
    Mesh const* const mesh = gMesh.load(std::memory_order_acquire);
    AcquireSRWLockShared(&gSampleLock);
    bool const fresh = mesh != nullptr && gLatestTorsoMesh == mesh && GetTickCount64() - gLatestTorsoTimeMs <= kMaxRequestAgeMs;
    ik::TorsoResult const result = gLatestTorso;
    ReleaseSRWLockShared(&gSampleLock);
    return fresh ? std::optional<ik::TorsoResult>(result) : std::nullopt;
}

std::uint64_t TotalWrites() {
    return gTotalWrites.load(std::memory_order_relaxed);
}

void LogStats() {
    if (!gInstalled) {
        return;
    }
    unsigned const calls = gCalls.exchange(0);
    unsigned const player = gPlayerCalls.exchange(0);
    unsigned const writes = gWrites.exchange(0);
    unsigned const rejects = gRejects.exchange(0);
    unsigned const keptMoves = gKeptMoves.exchange(0);
    char const* const lastReject = gLastReject.exchange(nullptr);
    std::string const callers = Slots(gCallers, true);
    std::string const threads = Slots(gThreads, false);
    if (player == 0 && rejects == 0 && gMesh.load() == nullptr) {
        log::Info("    arm IK writeback: idle ({} UpdateSkelPose calls){}", calls, gBroken.load() ? ", STOPPED after a fault" : "");
        return;
    }
    log::Info("    arm IK writeback: {} UpdateSkelPose calls, {} for the player mesh (from{}; threads{}), {} written, {} rejected{}{}",
        calls, player, callers, threads, writes, rejects, lastReject != nullptr ? std::format(" (last: {})", lastReject) : "",
        gBroken.load() ? ", STOPPED after a fault" : "");
    if (keptMoves > 0) {
        log::Warn("    arm IK writeback: {} writes moved a camera hook bone (Camera/God) in the solve; kept animated, not "
                  "written ({} since the hook went in)",
            keptMoves, gTotalKeptMoves.load(std::memory_order_relaxed));
    }
    AcquireSRWLockExclusive(&gSampleLock);
    Sample samples[2] = {gSamples[0], gSamples[1]};
    TorsoSample const torso = gTorso;
    gSamples[0].valid = gSamples[1].valid = false;
    gTorso.valid = false;
    ReleaseSRWLockExclusive(&gSampleLock);
    if (torso.valid) {
        log::Info("    arm IK torso (component space): {}", DescribeTorso(torso));
    }
    for (int side : {0, 1}) {
        if (samples[side].valid) {
            log::Info("    arm IK {} (component space, dt {:.4f}): {}", side == 0 ? "left" : "right", samples[side].deltaTime,
                Describe(samples[side]));
        }
    }
}

} // namespace mevr::engine::bone_pose
