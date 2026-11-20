// SPDX-License-Identifier: GPL-3.0-only
#include "engine/BonePose.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/CombatStance.hpp"
#include "core/ik/PoseGraft.hpp"
#include "core/ik/TwoHand.hpp"
#include "engine/Addresses.hpp"
#include "engine/Engine.hpp"
#include "engine/ClipPose.hpp"
#include "engine/HairPose.hpp"
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
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mevr::engine::bone_pose {
namespace {

// The skeletal mesh component, as our reflection layer's pointer.
using Mesh = ue3::Object;

constexpr char const* kHookName = "USkeletalMeshComponent::UpdateSkelPose";

// Verified as a function start in .pdata and by its code (research/14): it sizes SpaceBases and
// LocalAtoms to the RefSkeleton with 0x20-byte elements, blends the AnimTree into LocalAtoms and
// calls ComposeSkeleton (0x580A60). USkeletalMeshComponent::Tick calls it at 0x58D709 with the
// frame's DeltaTime; Attach and the various force-update paths call it with 0. LE1 and LE2 (ME2: 0x79B8A0, the same
// function; research/le2-vrik.md "UpdateSkelPose"); LE3 0x796EB0 has the same call ABI and atom layout
// (research/le3-body-pose.md). The game-specific hook address remains owned by engine/Addresses.hpp.
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
// The unscaled body (research/58): the last write's eye height and floor (UnscaledBody), under gSampleLock.
Unscaled gUnscaled;
std::uint64_t gUnscaledMs = 0;
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
// Hand contact's last result (LatestContact).
Mesh const* gContactMesh = nullptr;
ContactSample gContact;
ULONGLONG gContactTimeMs = 0;
// The fingers' last result (LatestFingers).
Mesh const* gFingersMesh = nullptr;
FingersSample gFingers;
ULONGLONG gFingersTimeMs = 0;
// The gun kick each call left in the pose (WrittenGunKicks). tKick is this call's, set by Write once it wrote (the hook's thread).
KickSamples gKicks;
thread_local KickSample tKick;
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
    if (!ue3::runtime::Up()) {
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

// The explore stance's left hand on the gun for this call (StanceHands::leftInRight; set by the stance's hands stage,
// read by Write for AnimatedHands; the hook's thread). Reset at the start of every hook call.
thread_local bool tStanceLeftOnGun = false;
thread_local int tStanceRightHand = -1;
thread_local ik::Transform tStanceLeftInRight;

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
            ik::Vec3 const rel = controller - right.wrist;
            out.alongMeters = ik::Dot(rel, axis) / unitsPerMeter;
            // The gun's frame for the vent's receiver zone (core/ik/Vent): its top squared against the axis, its
            // right from the two (component space is left-handed: right = up x forward).
            if (s.hasUp && ik::Length(s.up) > 1e-6) {
                ik::Vec3 top = ik::Rotate(check, s.up);
                top = top - axis * ik::Dot(top, axis);
                if (ik::Length(top) > 1e-6) {
                    top = ik::Normalize(top);
                    ik::Vec3 const side = ik::Cross(top, axis);
                    out.upMeters = ik::Dot(rel, top) / unitsPerMeter;
                    out.sideMeters = ik::Dot(rel, side) / unitsPerMeter;
                    out.gunFrame = true;
                }
            }
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

// ---- Root-motion steps between the goals and the write (GoalFrame, StepRule; research/le2-vrik.md) ----------------------

SRWLOCK gStepLock = SRWLOCK_INIT;
StepSettings gStepSettings;  // under gStepLock
StepStats gStepStats;        // under gStepLock
std::atomic<int> gStepLines{0};
constexpr int kMaxStepLines = 60;  // detailed lines per process (a vault has three big steps), then only the heartbeat's counts
constexpr double kStepLineUnits = 20.0;  // a detailed line for a step past this (the vault's: 20-113 units); every one is counted

std::string Text(ik::Vec3 const& v) {
    return std::format("({:.1f}, {:.1f}, {:.1f})", v.x, v.y, v.z);
}

// The step between the goals' frame and this call (component now <- component then), or nullopt: none, or below the rule's
// `units`, or the rule off. Logs the first kMaxStepLines steps seen in detail, whatever the rule.
std::optional<ik::Transform> StepSince(Request const& request, Mesh const* mesh, std::span<ik::Transform const> pose) {
    GoalFrame const& f = request.frame;
    if (!f.valid) {
        return std::nullopt;
    }
    AcquireSRWLockShared(&gStepLock);
    StepSettings const settings = gStepSettings;
    ReleaseSRWLockShared(&gStepLock);
    int const count = static_cast<int>(pose.size());
    ik::StepInput in;
    in.rule = settings.rule;
    in.units = settings.units;
    in.viewOnAnchor = f.viewOnAnchor;
    in.componentThen = f.componentToWorld;
    in.componentNow = skeleton::LocalToWorld(mesh);
    in.anchorKnown = f.anchor >= 0 && f.anchor < count && skeleton::RefBoneKey(mesh, f.anchor) == f.anchorKey;
    in.anchorThen = f.anchorAt;
    in.anchorNow = in.anchorKnown ? pose[static_cast<std::size_t>(f.anchor)].translation : ik::Vec3{};
    ik::Step const step = ik::FindStep(in);
    AcquireSRWLockExclusive(&gStepLock);
    ++gStepStats.writes;
    gStepStats.steps += step.seen ? 1 : 0;
    gStepStats.corrected += step.toNow ? 1 : 0;
    if (std::isfinite(step.anchorUnits)) {
        gStepStats.maxAnchorUnits = (std::max)(gStepStats.maxAnchorUnits, step.anchorUnits);
        ++gStepStats.byAnchor[step.anchorUnits < 1.0 ? 0 : step.anchorUnits < 5.0 ? 1 : step.anchorUnits < 20.0 ? 2 : 3];
    }
    if (std::isfinite(step.componentUnits)) {
        gStepStats.maxComponentUnits = (std::max)(gStepStats.maxComponentUnits, step.componentUnits);
    }
    ReleaseSRWLockExclusive(&gStepLock);
    if (step.seen && step.anchorUnits > kStepLineUnits && gStepLines.fetch_add(1, std::memory_order_relaxed) < kMaxStepLines) {
        bool const rooted = f.root >= 0 && f.root < count;
        ik::Vec3 const rootMove = rooted ? pose[static_cast<std::size_t>(f.root)].translation - f.rootAt : ik::Vec3{};
        log::Info("arm IK: a step between the goals and the write: the camera's bone moved {:.1f} units {} and the root {} in "
                  "component space, the mesh {:.1f} units in the world; rule {}{}: {}",
            step.anchorUnits, Text(step.anchorMove), rooted ? Text(rootMove) : std::string("(unknown)"), step.componentUnits,
            StepRuleName(settings.rule), f.viewOnAnchor ? "" : " (the view isn't on it)",
            step.toNow ? std::format("the goals solved {:.1f} units off, in their own frame", ik::Length(step.toNow->translation))
                       : std::string("the goals as published"));
    }
    return step.toNow;
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
        // Never copied since 24fed58 (2026-10-22): the writer's thread-local copy kept its zero, so the sight alignment
        // assist and the calibrated stance's move of the gun never reached the pose (found 2026-10-31, research/43).
        copy.gunShift = gRequest.gunShift;
        copy.gunKick = gRequest.gunKick;  // a new field must be copied here (see gunShift above)
        copy.fingers = gRequest.fingers;
        copy.legs = gRequest.legs;
        copy.keep.assign(gRequest.keep.begin(), gRequest.keep.end());
        copy.keepKeys.assign(gRequest.keepKeys.begin(), gRequest.keepKeys.end());
        copy.bodyScale = gRequest.bodyScale;
        copy.eyes = gRequest.eyes;
        copy.frame = gRequest.frame;
        copy.contact.enabled = gRequest.contact.enabled;
        copy.contact.capsules.assign(gRequest.contact.capsules.begin(), gRequest.contact.capsules.end());
        copy.contact.handRadius = gRequest.contact.handRadius;
        copy.contact.hands = gRequest.contact.hands;
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

// Hand contact (Request::contact, research/73): each enabled hand's final goal pushed out of the people near the player,
// sliding along them. A left hand the support grip holds on the gun moves by as much as the right (one gun); its own contact
// isn't measured. The point each hand was pushed to is kept for the next call (the side it came from: core/ik/HandContact).
void ApplyContact(Request const& request, std::span<ik::Transform const> pose, bool support, std::array<bool, 2> const& haveGoal,
    std::array<ik::ArmGoal, 2>& goals, ContactSample& sample) {
    namespace hc = ik::hand_contact;
    thread_local std::array<std::optional<ik::Vec3>, 2> tPrevious;
    Contact const& contact = request.contact;
    bool const leftOnGun = support && request.support.hold != 0;
    double const radius = contact.handRadius;
    for (int side : {1, 0}) {
        bool const usable = haveGoal[side] && request.arms[side].target == Target::Component && contact.hands[side] &&
                            !contact.capsules.empty() && radius > 0.0;
        if (!usable || (side == 0 && leftOnGun)) {
            tPrevious[side].reset();
            continue;
        }
        // The palm, not the wrist joint: halfway to the middle finger's knuckle as the animation holds the hand, turned with the
        // goal's rotation (the fingers module's bones; without them the wrist itself).
        ik::Vec3 palm;
        int const handBone = request.arms[side].chain.hand;
        int const knuckle = request.fingers.hands[side].bones.fingers[2].joints[0];
        if (goals[side].hasRotation && handBone >= 0 && knuckle >= 0 && static_cast<std::size_t>(handBone) < pose.size() &&
            static_cast<std::size_t>(knuckle) < pose.size()) {
            ik::Transform const& h = pose[static_cast<std::size_t>(handBone)];
            ik::Vec3 const local = ik::Rotate(ik::Conjugate(h.rotation), pose[static_cast<std::size_t>(knuckle)].translation - h.translation);
            if (ik::Length(local) < radius * 4.0) {
                palm = ik::Rotate(goals[side].rotation, local) * 0.5;
            }
        }
        ik::Vec3 const wanted = goals[side].wrist + palm;
        std::optional<ik::Vec3> previous = tPrevious[side];
        if (previous && ik::Length(*previous - wanted) > radius * 8.0) {
            previous.reset();  // far from where the hand is now (a jump, a teleport): no side to keep
        }
        hc::Result const result = hc::PushOut(wanted, radius, contact.capsules, previous);
        ContactHand& hand = sample.hands[side];
        hand.handRadius = radius;
        if (!result.touching) {
            tPrevious[side].reset();
            continue;
        }
        ik::Vec3 const delta = result.point - wanted;
        goals[side].wrist = result.point - palm;
        tPrevious[side] = result.point;
        hand.touching = true;
        hand.depth = result.depth;
        hand.moved = ik::Length(delta);
        hand.part = result.capsule >= 0 ? contact.capsules[static_cast<std::size_t>(result.capsule)].part : hc::Part::None;
        if (side == 1 && leftOnGun && haveGoal[0]) {
            goals[0].wrist = goals[0].wrist + delta;
        }
    }
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
    for (Leg const& leg : request.legs) {
        if (!leg.enabled) {
            continue;
        }
        int const bones[3] = {leg.chain.upper, leg.chain.fore, leg.chain.hand};
        for (int i = 0; i < 3; ++i) {
            if (bones[i] < 0 || bones[i] >= count || skeleton::RefBoneKey(mesh, bones[i]) != leg.keys[i]) {
                Reject("skeleton changed (leg bone names)");
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
    // The body's scale (research/58). The animated pose is unscaled (the animation recomposed it): its floor (the lowest
    // bone not kept) and Shepard's eyes above it go to the mod for the factor. The goals are in the scaled world: through the
    // inverse they land where they were meant to once the finished pose is scaled (below, after the fingers).
    double animatedFloorZ = std::numeric_limits<double>::infinity();  // the lowest bone not kept (the standing legs' floor)
    {
        double floorZ = std::numeric_limits<double>::infinity();
        for (int i = 0; i < count; ++i) {
            if (std::find(request.keep.begin(), request.keep.end(), i) == request.keep.end()) {
                floorZ = (std::min)(floorZ, pose[static_cast<std::size_t>(i)].translation.z);
            }
        }
        animatedFloorZ = floorZ;
        int const eyeL = request.eyes[0];
        int const eyeR = request.eyes[1];
        if (std::isfinite(floorZ) && eyeL >= 0 && eyeL < count && eyeR >= 0 && eyeR < count) {
            double const eyeZ = 0.5 * (pose[static_cast<std::size_t>(eyeL)].translation.z + pose[static_cast<std::size_t>(eyeR)].translation.z);
            AcquireSRWLockExclusive(&gSampleLock);
            gUnscaled = {eyeZ - floorZ, floorZ,
                (pose[static_cast<std::size_t>(eyeL)].translation + pose[static_cast<std::size_t>(eyeR)].translation) * 0.5};
            gUnscaledMs = GetTickCount64();
            ReleaseSRWLockExclusive(&gSampleLock);
        }
    }
    ik::body_scale::Scale const scale = request.bodyScale;
    if (scale.Active()) {
        namespace bs = ik::body_scale;
        for (Arm& arm : request.arms) {
            if (arm.target == Target::Component) {
                arm.wrist = bs::Unscale(scale, arm.wrist);
            }
        }
        ik::TorsoGoal& goal = request.torso.goal;
        goal.head = goal.relative ? bs::UnscaleVector(scale, goal.head) : bs::Unscale(scale, goal.head);
        goal.bodyAt = bs::Unscale(scale, goal.bodyAt);
        goal.maxBodyShift = bs::UnscaleLength(scale, goal.maxBodyShift);
        goal.neckDown = bs::UnscaleLength(scale, goal.neckDown);
        goal.crouchSlack = bs::UnscaleLength(scale, goal.crouchSlack);
        goal.crouchMaxDrop = bs::UnscaleLength(scale, goal.crouchMaxDrop);
        // The support grip's left-controller meters become fewer unscaled units; its foregrip, barrel and muzzle are in the
        // hand bone's own axes, already without the bone's scale (game/weapon divides it out); the calibrated span is in
        // drawn component units.
        request.support.unitsPerMeter = bs::UnscaleLength(scale, request.support.unitsPerMeter);
        request.support.calibratedSpan = bs::UnscaleVector(scale, request.support.calibratedSpan);
        request.gunShift = bs::UnscaleVector(scale, request.gunShift);
        request.gunKick.backUnits = bs::UnscaleLength(scale, request.gunKick.backUnits);
        for (Leg& leg : request.legs) {
            leg.ankle = bs::Unscale(scale, leg.ankle);
            leg.knee = bs::Unscale(scale, leg.knee);
        }
        for (ik::hand_contact::Capsule& capsule : request.contact.capsules) {
            capsule.a = bs::Unscale(scale, capsule.a);
            capsule.b = bs::Unscale(scale, capsule.b);
            capsule.radius = bs::UnscaleLength(scale, capsule.radius);
        }
        request.contact.handRadius = bs::UnscaleLength(scale, request.contact.handRadius);
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
        // Composed as explore with the arms put back: the left hand where the AnimTree's off-hand IK held it on the gun
        // in the combat pose (the explore pose's off-hand IK reached for where the explore animation had LHIK).
        ik::Transform const left = tStanceLeftOnGun && tStanceRightHand == rightHand ? ik::Compose(tStanceLeftInRight, pose[rightHand])
                                                                                      : pose[leftHand];
        AcquireSRWLockExclusive(&gSampleLock);
        gHandsMesh = self;
        gAnimatedHands = {left, pose[rightHand]};
        gHandsTimeMs = GetTickCount64();
        ReleaseSRWLockExclusive(&gSampleLock);
    }
    // A step between the goals and this call (GoalFrame, StepRule; research/le2-vrik.md "Arm IK across root-motion steps"):
    // everything below is solved in the goals' own frame (the animated pose moved there) and moved back before the write. The
    // samples above stay in this call's frame (the mod reads them with the next tick's LocalToWorld).
    std::optional<ik::Transform> const step = StepSince(request, mesh, pose);
    if (step) {
        ik::Transform const back = ik::Inverse(*step);
        for (ik::Transform& bone : pose) {
            bone = ik::Compose(bone, back);
        }
        animatedFloorZ = std::numeric_limits<double>::infinity();
        for (int i = 0; i < count; ++i) {
            if (std::find(request.keep.begin(), request.keep.end(), i) == request.keep.end()) {
                animatedFloorZ = (std::min)(animatedFloorZ, pose[static_cast<std::size_t>(i)].translation.z);
            }
        }
    }
    // The kept bones as the animation left them, to tell whether a solve moved one.
    thread_local std::vector<ik::Transform> kept;
    kept.clear();
    for (int bone : request.keep) {
        kept.push_back(pose[bone]);
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
            // The body down to the player's head (TorsoGoal::crouch, research/29 "The eyes inside the body"): only as far as
            // both legs, posed below, keep the feet on the floor; without both legs in the request, not at all.
            ik::TorsoGoal goal = t.goal;
            goal.maxDrop = goal.maxRise = 0.0;
            if (goal.crouch && request.legs[0].enabled && request.legs[1].enabled) {
                std::array<ik::ArmChain, 2> const legChains{request.legs[0].chain, request.legs[1].chain};
                ik::LegRoom const room = ik::LegsRoom(pose, legChains, body.up);
                goal.maxDrop = room.drop;
                goal.maxRise = goal.crouchUp ? room.rise : 0.0;
            }
            torso.result = ik::PoseTorso(pose, request.parents, t.chain, request.arms[0].chain, request.arms[1].chain, body, goal);
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
    // The gun kick (research/24 section 8): after the support grip, so its zone, its two-hand aim and the vent's receiver zone
    // measured the gun where the controller holds it; the left hand on the gun moves with it, like the sight assist's shift.
    std::optional<ik::gun_kick::Motion> kick;
    GunKick const& kickRequest = request.gunKick;
    if (kickRequest.enabled && haveGoal[1] && goals[1].hasRotation && request.arms[1].target == Target::Component &&
        (kickRequest.angleRadians != 0.0 || kickRequest.backUnits != 0.0)) {
        kick = ik::gun_kick::HandMotion(goals[1].rotation, kickRequest.barrel, kickRequest.up, kickRequest.angleRadians, kickRequest.backUnits);
        if (kick) {
            ik::Vec3 const pivot = goals[1].wrist;
            ik::gun_kick::ApplyToHand(*kick, goals[1].wrist, goals[1].rotation);
            if (support && haveGoal[0]) {
                double const blend = std::clamp(std::isfinite(request.support.blend) ? request.support.blend : 0.0, 0.0, 1.0);
                ik::gun_kick::ApplyToHeld(*kick, pivot, request.support.hold != 0 ? 1.0 : blend, goals[0].wrist,
                    goals[0].hasRotation ? &goals[0].rotation : nullptr);
            }
        }
    }
    // Hand contact with other characters (research/73): the final goals pushed out of the bodies near the player.
    if (request.contact.enabled) {
        ContactSample contactSample;
        contactSample.capsules = static_cast<int>(request.contact.capsules.size());
        ApplyContact(request, pose, support, haveGoal, goals, contactSample);
        AcquireSRWLockExclusive(&gSampleLock);
        gContactMesh = self;
        gContact = contactSample;
        gContactTimeMs = GetTickCount64();
        ReleaseSRWLockExclusive(&gSampleLock);
    }
    bool rightPosed = false;
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
        rightPosed = rightPosed || side == 1;
    }
    // What this call leaves of the kick in the pose (set as the call's sample once the pose is written, below).
    KickSample kickSample;
    if (rightPosed && rightHand >= 0 && rightHand < count) {
        kickSample.posed = true;
        kickSample.hand = pose[rightHand].rotation;
        if (kick) {
            kickSample.kicked = true;
            kickSample.kick = ik::gun_kick::ToWritten(*kick, pose[rightHand].translation);
        }
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
    // The legs (the seated driver): the ankle to its place, the knee bending toward its point.
    for (int side : {0, 1}) {
        Leg const& leg = request.legs[side];
        if (!leg.enabled) {
            continue;
        }
        ik::ArmGoal goal;
        goal.wrist = leg.ankle;
        goal.hasPole = true;
        goal.pole = leg.knee - pose[leg.chain.upper].translation;
        if (leg.stand) {
            // Standing: the ankle under the hip on the floor the body stands on now, as far down as the leg reaches.
            ik::Vec3 const hip = pose[leg.chain.upper].translation;
            ik::Vec3 const animated = pose[leg.chain.hand].translation;
            double const length = ik::Length(pose[leg.chain.fore].translation - hip) + ik::Length(animated - pose[leg.chain.fore].translation);
            double const shiftZ = torso.valid && torso.result.ok ? torso.result.bodyShift.z : 0.0;
            double const floor = std::isfinite(animatedFloorZ) ? animatedFloorZ + shiftZ + leg.floorUp : hip.z - length * 0.98;
            ik::Vec3 const stood{hip.x, hip.y, (std::max)(floor, hip.z - length * 0.995)};
            double const w = std::clamp(leg.weight, 0.0, 1.0);
            // The animation's foot on its floor: the body's lift down to the player's head (TorsoGoal::crouch) undone.
            double const lift = torso.valid && torso.result.ok ? torso.result.bodyLift : 0.0;
            ik::Vec3 const walked = animated - body.up * lift;
            goal.wrist = walked + (stood - walked) * w;
            goal.pole = body.forward;
        } else if (leg.planted) {
            // Planted: the foot goes back where the idle animation planted it (the body's slide under the view carried it
            // with the animation's sway) and down to the floor the body was lifted off or lowered into (the body down to the
            // player's head, TorsoGoal::crouch), its angle to the floor kept, the knee bending the way it bends.
            bool const solved = torso.valid && torso.result.ok;
            ik::Vec3 const move = solved ? torso.result.feetCorrection - body.up * torso.result.bodyLift : ik::Vec3{};
            auto const under = ik::LegUnderBody(pose, leg.chain, move, body);
            if (!under) {
                continue;   // nothing to correct: the leg as animated
            }
            goal = *under;
        }
        ik::ArmResult const result = ik::PoseArm(pose, request.parents, leg.chain, side, body, goal);
        if (!result.ok) {
            Reject(result.error);
            continue;
        }
        first = (std::min)(first, leg.chain.upper);
    }
    // The fingers, on the hands as solved (research/43): each joint turned in place, its children carried.
    bool anyFingers = false;
    FingersSample fingersSample;
    for (int side : {0, 1}) {
        FingerHand const& hand = request.fingers.hands[side];
        if (!hand.enabled) {
            continue;
        }
        anyFingers = true;
        std::array<int, ik::fingers::kMaxBones> bones{};
        int const n = hand.bones.All(bones);
        bool same = n == hand.keyCount && n > 0;
        for (int i = 0; same && i < n; ++i) {
            int const bone = bones[static_cast<std::size_t>(i)];
            same = bone >= 0 && bone < count && skeleton::RefBoneKey(mesh, bone) == hand.keys[static_cast<std::size_t>(i)];
        }
        if (!same) {
            fingersSample.skipped[side] = true;
            Reject("skeleton changed (finger bone names)");
            continue;
        }
        int const written = ik::fingers::Apply(pose, request.parents, hand.bones, hand.axes, hand.curl, request.fingers.fullCurlDegrees);
        fingersSample.bonesWritten[side] = written;
        if (written > 0) {
            for (int f = 0; f < ik::fingers::kFingerCount; ++f) {
                int const knuckle = hand.bones.fingers[static_cast<std::size_t>(f)].joints[0];
                if (knuckle >= 0 && hand.bones.fingers[static_cast<std::size_t>(f)].count > 0) {
                    first = (std::min)(first, knuckle);
                }
            }
        }
    }
    if (anyFingers) {
        AcquireSRWLockExclusive(&gSampleLock);
        gFingersMesh = self;
        gFingers = fingersSample;
        gFingersTimeMs = GetTickCount64();
        ReleaseSRWLockExclusive(&gSampleLock);
    }
    // The body's scale (research/58): every bone but the kept ones (Camera and God) scaled about the pivot, its own scale
    // too, so the skinned mesh and what's attached to it grow; the goals went through the inverse above.
    if (scale.Active()) {
        int root = -1;
        for (int i = 0; i < count; ++i) {
            if (std::find(request.keep.begin(), request.keep.end(), i) != request.keep.end()) {
                continue;
            }
            pose[static_cast<std::size_t>(i)] = ik::body_scale::Apply(scale, pose[static_cast<std::size_t>(i)]);
            root = root < 0 ? i : root;
        }
        if (root >= 0) {
            first = (std::min)(first, root);
        }
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
    if (step) {
        // Back into this call's frame: what is written (the bones from `first` on) and what this call's kick sample says.
        for (int i = first; i < count; ++i) {
            pose[static_cast<std::size_t>(i)] = ik::Compose(pose[static_cast<std::size_t>(i)], *step);
        }
        if (kickSample.posed) {
            kickSample.hand = ik::Normalize(step->rotation * kickSample.hand);
        }
        if (kickSample.kicked) {
            ik::gun_kick::Written& w = kickSample.kick;
            w.rotation = ik::Normalize(step->rotation * w.rotation * ik::Conjugate(step->rotation));
            w.pivot = ik::Apply(*step, w.pivot);
            w.translation = ik::Rotate(step->rotation, w.translation);
        }
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
    tKick = kickSample;
}

// The kick this call left in the pose (none unless Write wrote one) becomes the latest sample, the last one the previous.
void NoteKick(Mesh const* self) {
    AcquireSRWLockExclusive(&gSampleLock);
    if (gKicks.mesh != self) {
        gKicks.mesh = self;
        gKicks.latest = {};
    }
    gKicks.previous = gKicks.latest;
    gKicks.latest = tKick;
    if (tKick.kicked) {
        ++gKicks.kickedWrites;
    }
    ReleaseSRWLockExclusive(&gSampleLock);
}

// No C++ objects here, so it can catch an access violation: writes then stop for good instead of
// taking the game down.
void WriteGuarded(Mesh* self, float deltaTime) {
    tKick = {};
    __try {
        Write(self, deltaTime);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gBroken.store(true);
        gMesh.store(nullptr);
        Reject("access violation in the writer: writes stopped");
        tKick = {};
    }
    NoteKick(self);
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

// ---- The combat stance's pre-stage (research/38 A) -------------------------------------------------------------

std::atomic<Mesh const*> gStanceMesh{nullptr};
SRWLOCK gStanceLock = SRWLOCK_INIT;
Stance gStance;
DWORD gStanceThread = 0;
ULONGLONG gStanceTimeMs = 0;
std::atomic<bool> gStanceBroken{false};
// The hook's own state: touched only on the publishing (game) thread, read by GetStanceStats on it too.
vr::combat_stance::OrderCheck gOrder;
StanceStats gStanceStats;

// One call's state, from before the engine's call to after it.
struct StanceCall {
    bool active = false;
    Stance s;
    bool valid[4] = {false, false, false, false};
    bool wrote[4] = {false, false, false, false};
    float saved[4][2] = {};
    StanceProbe probe;
};

void StanceReject(char const* why) {
    ++gStanceStats.rejected;
    gStanceStats.lastReject = why;
}

float* ChildWeight(Stance const& s, std::uint8_t const* children, int index) {
    return reinterpret_cast<float*>(const_cast<std::uint8_t*>(children) + static_cast<std::size_t>(index) * s.childStride + s.weightOffset);
}

HandCapture gHandCapture;  // game thread

// The arms' bones as published: in range, parents first, the same names (no C++ objects: called under __try).
bool HandsMatch(Mesh const* self, StanceHands const& h) {
    int const count = skeleton::RefBoneCount(self);
    if (h.count <= 0 || h.count > kMaxStanceHandBones || count <= 0) {
        return false;
    }
    for (int i = 0; i < h.count; ++i) {
        int const bone = h.bones[static_cast<std::size_t>(i)];
        int const parent = h.parents[static_cast<std::size_t>(i)];
        if (bone < 0 || bone >= count || parent < 0 || parent >= bone || skeleton::RefBoneKey(self, bone) != h.keys[static_cast<std::size_t>(i)]) {
            return false;
        }
    }
    return h.leftHand >= 0 && h.leftHand < count && h.rightHand >= 0 && h.rightHand < count &&
        skeleton::RefBoneKey(self, h.leftHand) == h.leftKey && skeleton::RefBoneKey(self, h.rightHand) == h.rightKey;
}

// Reads and writes the game's memory: no C++ objects with destructors, so it can catch an access violation.
void StancePreGuarded(Mesh* self, float deltaTime, StanceCall& c) {
    __try {
        Stance const& s = c.s;
        if (ue3::Reflection::Read<void const*>(self, s.animationsOffset) != s.tree) {
            c.active = false;
            StanceReject("the mesh's AnimTree changed");
            return;
        }
        // Only with the arms' combat pose to put back after the call (research/38 A: without it the gun's bones follow
        // the explore animations).
        bool write = s.apply && gOrder.Result() == vr::combat_stance::OrderCheck::Order::Before;
        if (write && !(s.hands.apply && HandsMatch(self, s.hands))) {
            write = false;
            StanceReject("the arms' bones changed (not composed as explore)");
        }
        c.probe.deltaTime = deltaTime;
        for (int i = 0; i < s.nodeCount; ++i) {
            StanceNode const& n = s.nodes[i];
            auto const* const array = reinterpret_cast<std::uint8_t const*>(n.node) + s.childrenOffset;
            std::uint8_t const* const data = ue3::Reflection::Read<std::uint8_t const*>(array, 0);
            std::int32_t const count = ue3::Reflection::Read<std::int32_t>(array, sizeof(void*));
            if (data != n.children || count != n.childCount) {
                StanceReject("a CombatMode node's children changed");
                continue;
            }
            c.valid[i] = true;
            float* const nonCombat = ChildWeight(s, data, n.nonCombat);
            float* const combat = ChildWeight(s, data, n.combat);
            c.saved[i][0] = *nonCombat;
            c.saved[i][1] = *combat;
            if (i == 0) {
                c.probe.startWeights[0] = *nonCombat;
                c.probe.startWeights[1] = *combat;
                c.probe.startTag = ue3::Reflection::Read<std::int32_t>(n.node, s.tickTagOffset);
                c.probe.valid = true;
            }
            if (write) {
                *nonCombat = 1.0f;
                *combat = 0.0f;
                write_journal::Wrote("BonePose.combatStance", nonCombat, sizeof(float));
                write_journal::Wrote("BonePose.combatStance", combat, sizeof(float));
                c.wrote[i] = true;
                c.probe.wrote = true;
            }
        }
        if (s.drawSequence != nullptr) {
            c.probe.drawPlaying = (ue3::Reflection::Read<std::uint32_t>(s.drawSequence, s.playingOffset) & s.playingMask) != 0;
            c.probe.drawStart = ue3::Reflection::Read<float>(s.drawSequence, s.currentTimeOffset);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gStanceBroken.store(true);
        gStanceMesh.store(nullptr);
        c.active = false;
        StanceReject("access violation in the pre-stage: stopped");
    }
}

void StancePostGuarded(StanceCall& c) {
    __try {
        Stance const& s = c.s;
        for (int i = 0; i < s.nodeCount; ++i) {
            if (!c.valid[i]) {
                continue;
            }
            StanceNode const& n = s.nodes[i];
            float* const nonCombat = ChildWeight(s, n.children, n.nonCombat);
            float* const combat = ChildWeight(s, n.children, n.combat);
            if (i == 0) {
                c.probe.endWeights[0] = *nonCombat;
                c.probe.endWeights[1] = *combat;
                c.probe.endTag = ue3::Reflection::Read<std::int32_t>(n.node, s.tickTagOffset);
            }
            if (!c.wrote[i]) {
                continue;
            }
            if (*nonCombat == 1.0f && *combat == 0.0f) {
                *nonCombat = c.saved[i][0];
                *combat = c.saved[i][1];
                write_journal::Wrote("BonePose.combatStance(restore)", nonCombat, sizeof(float));
                write_journal::Wrote("BonePose.combatStance(restore)", combat, sizeof(float));
            } else {
                ++gStanceStats.overridden;  // the call changed them: its values stay
            }
        }
        if (s.drawSequence != nullptr) {
            c.probe.drawEnd = ue3::Reflection::Read<float>(s.drawSequence, s.currentTimeOffset);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gStanceBroken.store(true);
        gStanceMesh.store(nullptr);
        StanceReject("access violation after the call: stopped");
    }
}

// After the call (the pose composed, skeletal controls applied) and before arm IK: the arms back at their combat pose
// (a call composed as explore), or the combat pose learned (a call composed as combat, CombatMode fully on Combat).
void StanceHandsWork(Mesh* self, StanceCall& c) {
    StanceHands const& h = c.s.hands;
    bool const graft = c.probe.wrote && h.apply;
    bool const capture = !c.probe.wrote && h.capture && c.valid[0] && c.saved[0][0] <= 0.001f && c.saved[0][1] >= 0.999f;
    if (!graft && !capture) {
        return;
    }
    std::span<skeleton::BoneAtom> const bases = skeleton::SpaceBases(self);
    if (static_cast<int>(bases.size()) != skeleton::RefBoneCount(self) || !HandsMatch(self, h)) {
        StanceReject(bases.empty() ? "the pose isn't computed (the arms not put back)" : "the arms' bones changed after the call");
        gStanceStats.notGrafted += graft ? 1 : 0;
        return;
    }
    thread_local std::vector<ik::Transform> pose;
    pose.resize(bases.size());
    for (std::size_t i = 0; i < bases.size(); ++i) {
        pose[i] = skeleton::ToTransform(bases[i]);
    }
    std::span<int const> const bones(h.bones.data(), static_cast<std::size_t>(h.count));
    std::span<int const> const parents(h.parents.data(), static_cast<std::size_t>(h.count));
    if (capture) {
        std::vector<ik::Transform> const relative = ik::CaptureRelative(pose, bones, parents);
        if (relative.size() != bones.size()) {
            return;
        }
        std::copy(relative.begin(), relative.end(), gHandCapture.relative.begin());
        gHandCapture.count = h.count;
        gHandCapture.leftInRight = ik::Relative(pose[static_cast<std::size_t>(h.leftHand)], pose[static_cast<std::size_t>(h.rightHand)]);
        ++gHandCapture.serial;
        ++gStanceStats.captured;
        return;
    }
    if (!ik::ApplyRelative(pose, bones, parents, std::span<ik::Transform const>(h.relative.data(), static_cast<std::size_t>(h.count)))) {
        ++gStanceStats.notGrafted;
        StanceReject("the arms' combat pose didn't fit");
        return;
    }
    int lowest = h.bones[0];
    int highest = h.bones[0];
    for (int bone : bones) {
        bases[static_cast<std::size_t>(bone)] = skeleton::ToAtom(pose[static_cast<std::size_t>(bone)]);
        lowest = (std::min)(lowest, bone);
        highest = (std::max)(highest, bone);
    }
    write_journal::Wrote("BonePose.combatStanceArms", &bases[static_cast<std::size_t>(lowest)],
        static_cast<std::size_t>(highest - lowest + 1) * sizeof(skeleton::BoneAtom));
    ++gStanceStats.grafted;
    if (h.hasLeftInRight) {
        tStanceLeftOnGun = true;
        tStanceRightHand = h.rightHand;
        tStanceLeftInRight = h.leftInRight;
    }
}

void StanceHandsGuarded(Mesh* self, StanceCall& c) {
    __try {
        StanceHandsWork(self, c);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gStanceBroken.store(true);
        gStanceMesh.store(nullptr);
        StanceReject("access violation putting the arms back: stopped");
    }
}

void StancePre(Mesh* self, float deltaTime, StanceCall& c) {
    if (self == nullptr || self != gStanceMesh.load(std::memory_order_acquire) || gStanceBroken.load(std::memory_order_relaxed)) {
        return;
    }
    DWORD thread = 0;
    ULONGLONG published = 0;
    AcquireSRWLockShared(&gStanceLock);
    bool const same = gStance.mesh == self;
    if (same) {
        c.s = gStance;
        thread = gStanceThread;
        published = gStanceTimeMs;
    }
    ReleaseSRWLockShared(&gStanceLock);
    if (!same || c.s.nodeCount <= 0) {
        return;
    }
    if (GetCurrentThreadId() != thread) {
        // Only on the game thread: the hook's state (the order check, the stats) lives there.
        return;
    }
    ++gStanceStats.calls;
    if (GetTickCount64() - published > kMaxRequestAgeMs) {
        StanceReject("request too old (the mod stopped publishing)");
        return;
    }
    c.active = true;
    StancePreGuarded(self, deltaTime, c);
}

void StancePost(StanceCall& c) {
    if (!c.active) {
        return;
    }
    StancePostGuarded(c);
    if (c.probe.valid) {
        gOrder.Feed(c.probe.deltaTime, c.probe.startTag, c.probe.endTag);
        gStanceStats.last = c.probe;
        if (c.probe.drawPlaying && !gStanceStats.firstDraw.valid) {
            gStanceStats.firstDraw = c.probe;
        }
    }
    if (c.probe.wrote) {
        ++gStanceStats.applied;
    }
}

// ---- The Mako's turret (research/44): the controls' DesiredBoneRotation re-asserted before the vehicle's pose ----
std::atomic<Mesh const*> gTurretMesh{nullptr};
SRWLOCK gTurretLock = SRWLOCK_INIT;
Turret gTurret;
DWORD gTurretThread = 0;
ULONGLONG gTurretTimeMs = 0;
std::atomic<bool> gTurretBroken{false};
TurretStats gTurretStats;  // the publishing (game) thread only

void TurretReject(char const* why) {
    ++gTurretStats.rejected;
    gTurretStats.lastReject = why;
}

void TurretPreGuarded(Mesh* self, Turret const& t) {
    __try {
        if (ue3::Reflection::Read<void const*>(self, t.animationsOffset) != t.tree) {
            TurretReject("the vehicle mesh's AnimTree changed");
            return;
        }
        for (int i = 0; i < t.count; ++i) {
            TurretControl const& c = t.controls[i];
            if (c.control == nullptr) {
                continue;
            }
            auto* const rotation = reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(c.control) + t.rotationOffset);
            rotation[0] = c.pitch;
            rotation[1] = c.yaw;
            rotation[2] = c.roll;
            write_journal::Wrote("BonePose.makoTurret", rotation, 3 * sizeof(std::int32_t));
        }
        ++gTurretStats.written;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gTurretBroken.store(true);
        gTurretMesh.store(nullptr);
        TurretReject("access violation in the turret stage: stopped");
    }
}

void TurretPre(Mesh* self) {
    if (self == nullptr || self != gTurretMesh.load(std::memory_order_acquire) || gTurretBroken.load(std::memory_order_relaxed)) {
        return;
    }
    Turret t;
    DWORD thread = 0;
    ULONGLONG published = 0;
    AcquireSRWLockShared(&gTurretLock);
    bool const same = gTurret.mesh == self;
    if (same) {
        t = gTurret;
        thread = gTurretThread;
        published = gTurretTimeMs;
    }
    ReleaseSRWLockShared(&gTurretLock);
    if (!same || t.count <= 0 || GetCurrentThreadId() != thread) {
        return;  // only on the game thread: the stats live there
    }
    ++gTurretStats.calls;
    if (GetTickCount64() - published > kMaxRequestAgeMs) {
        TurretReject("request too old (the mod stopped publishing)");
        return;
    }
    TurretPreGuarded(self, t);
}

// ---- The vent's visible rack (research/45 section 6): one part of the held gun moved after its pose --------------
std::atomic<Mesh const*> gPartMesh{nullptr};
SRWLOCK gPartLock = SRWLOCK_INIT;
WeaponPart gPart;
DWORD gPartThread = 0;
ULONGLONG gPartTimeMs = 0;
std::atomic<bool> gPartBroken{false};
WeaponPartStats gPartStats;  // the publishing (game) thread only
// What the stage wrote last (the game thread): an entry that still holds our write wasn't recomposed by the call, so the
// part moves from the animation's place remembered then, not again from our own write.
struct PartMemory {
    Mesh const* mesh = nullptr;
    int count = 0;
    std::array<int, kMaxWeaponPartBones> bones{};
    std::array<std::array<float, 3>, kMaxWeaponPartBones> base{};
    std::array<std::array<float, 3>, kMaxWeaponPartBones> written{};
};
PartMemory gPartMemory;

void PartReject(char const* why) {
    ++gPartStats.rejected;
    gPartStats.lastReject = why;
}

// Reads and writes the gun's pose: no C++ objects with destructors, so it can catch an access violation.
void PartPostGuarded(Mesh* self, WeaponPart const& p) {
    __try {
        int const count = skeleton::RefBoneCount(self);
        std::span<skeleton::BoneAtom> const bases = skeleton::SpaceBases(self);
        if (count != p.boneCount || static_cast<int>(bases.size()) != count || p.count <= 0 || p.count > kMaxWeaponPartBones) {
            PartReject(bases.empty() ? "the gun's pose isn't computed" : "the gun's skeleton changed (bone count)");
            return;
        }
        for (int i = 0; i < p.count; ++i) {
            int const bone = p.bones[static_cast<std::size_t>(i)];
            if (bone < 0 || bone >= count || skeleton::RefBoneKey(self, bone) != p.keys[static_cast<std::size_t>(i)]) {
                PartReject("the gun's skeleton changed (the part's bone names)");
                return;
            }
        }
        bool same = gPartMemory.mesh == self && gPartMemory.count == p.count;
        for (int i = 0; same && i < p.count; ++i) {
            same = gPartMemory.bones[static_cast<std::size_t>(i)] == p.bones[static_cast<std::size_t>(i)];
        }
        if (!same) {
            gPartMemory.mesh = self;
            gPartMemory.count = p.count;
            gPartMemory.bones = p.bones;
        }
        float const offset[3] = {static_cast<float>(p.offset.x), static_cast<float>(p.offset.y), static_cast<float>(p.offset.z)};
        bool stale = false;
        int lowest = p.bones[0];
        int highest = p.bones[0];
        for (int i = 0; i < p.count; ++i) {
            std::size_t const k = static_cast<std::size_t>(i);
            int const bone = p.bones[k];
            skeleton::BoneAtom& atom = bases[static_cast<std::size_t>(bone)];
            float base[3] = {atom.tx, atom.ty, atom.tz};
            if (same && atom.tx == gPartMemory.written[k][0] && atom.ty == gPartMemory.written[k][1] && atom.tz == gPartMemory.written[k][2]) {
                // Not recomposed since our write: from the animation's place then.
                base[0] = gPartMemory.base[k][0];
                base[1] = gPartMemory.base[k][1];
                base[2] = gPartMemory.base[k][2];
                stale = stale || offset[0] != 0.0f || offset[1] != 0.0f || offset[2] != 0.0f;
            }
            atom.tx = base[0] + offset[0];
            atom.ty = base[1] + offset[1];
            atom.tz = base[2] + offset[2];
            gPartMemory.base[k] = {base[0], base[1], base[2]};
            gPartMemory.written[k] = {atom.tx, atom.ty, atom.tz};
            lowest = (std::min)(lowest, bone);
            highest = (std::max)(highest, bone);
        }
        write_journal::Wrote("BonePose.ventPart", &bases[static_cast<std::size_t>(lowest)],
            static_cast<std::size_t>(highest - lowest + 1) * sizeof(skeleton::BoneAtom));
        ++gPartStats.written;
        gPartStats.stale += stale ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gPartBroken.store(true);
        gPartMesh.store(nullptr);
        PartReject("access violation in the vent's part stage: stopped");
    }
}

void PartPost(Mesh* self) {
    if (self == nullptr || self != gPartMesh.load(std::memory_order_acquire) || gPartBroken.load(std::memory_order_relaxed)) {
        return;
    }
    WeaponPart p;
    DWORD thread = 0;
    ULONGLONG published = 0;
    AcquireSRWLockShared(&gPartLock);
    bool const same = gPart.mesh == self;
    if (same) {
        p = gPart;
        thread = gPartThread;
        published = gPartTimeMs;
    }
    ReleaseSRWLockShared(&gPartLock);
    if (!same || p.count <= 0 || GetCurrentThreadId() != thread) {
        return;  // only on the game thread: the stats and the memory live there
    }
    ++gPartStats.calls;
    if (GetTickCount64() - published > kMaxRequestAgeMs) {
        PartReject("request too old (the mod stopped publishing)");
        return;
    }
    PartPostGuarded(self, p);
}

// No C++ objects with destructors here, so __finally keeps the in-flight count right even when an
// exception unwinds through the engine's call (a count left up would keep the mod pinned).
// The stance's call state is several KB: only the player's mesh, while the stance is published, pays for it (every
// other skeletal mesh of the frame goes straight to the engine's call).
__declspec(noinline) void UpdateSkelPoseWithStance(Mesh* self, float deltaTime, unsigned tickFaceFx, unsigned unused) {
    StanceCall stance;
    StancePre(self, deltaTime, stance);
    gOrig(self, deltaTime, tickFaceFx, unused);
    StancePost(stance);
    if (stance.active && stance.s.hands.count > 0) {
        StanceHandsGuarded(self, stance);
    }
}

void UpdateSkelPoseHook(Mesh* self, float deltaTime, unsigned tickFaceFx, unsigned unused) {
    std::uintptr_t const caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    gInFlight.fetch_add(1, std::memory_order_acquire);
    __try {
        tStanceLeftOnGun = false;
        if (self != nullptr && self == gTurretMesh.load(std::memory_order_acquire)) {
            TurretPre(self);  // the Mako's turret controls, before its pose (research/44)
        }
        if (self != nullptr && self == gStanceMesh.load(std::memory_order_acquire)) {
            UpdateSkelPoseWithStance(self, deltaTime, tickFaceFx, unused);
        } else {
            gOrig(self, deltaTime, tickFaceFx, unused);
        }
        if (self != nullptr && self == gPartMesh.load(std::memory_order_acquire)) {
            PartPost(self);  // the vent's rack: one part of the held gun moved (research/45 section 6)
        }
        if (clip_pose::Wants(self)) {
        clip_pose::AfterPose(self, deltaTime);  // a generated clip first: the hair then reacts to it
    }
    if (hair_pose::Wants(self)) {
            hair_pose::AfterPose(self, deltaTime);  // hair on simulated bone chains (docs/trilogy-hair-plan.md)
        }
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
                       "body shift {:.1f} units{}, body lift {:.1f} units (gap {:.1f}{}), reference {:.1f} from its goal, clavicles "
                       "{:.0f}/{:.0f} deg, arm stretch {:.2f}/{:.2f}, {} bones",
        Format(r.oldShoulders), Format(r.newShoulders), Format(r.wantedShoulders), ik::Length(r.newShoulders - r.wantedShoulders),
        r.limited ? ", limited" : "", r.twistDegrees, r.bendDegrees, r.bodyTurnDegrees, r.wantedTurnDegrees, ik::Length(r.bodyShift),
        r.shiftLimited ? std::format(" (wanted {:.1f}: capped)", ik::Length(r.wantedBodyShift)) : std::string(), r.bodyLift,
        r.crouchGap, r.liftByHead ? ", held at the player's own head drop" : r.liftLimited ? ", held by the legs" : "",
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
    ClearStance();
    ClearTurret();
    ClearWeaponPart();
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
    gStanceBroken = false;
    gPartBroken = false;
    gPartStats = {};
    gPartMemory = {};
    gOrder.Reset();
    gStanceStats = {};
    gHandCapture = {};
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

void SetStepSettings(StepSettings settings) {
    AcquireSRWLockExclusive(&gStepLock);
    gStepSettings = settings;
    ReleaseSRWLockExclusive(&gStepLock);
}

StepSettings GetStepSettings() {
    AcquireSRWLockShared(&gStepLock);
    StepSettings const settings = gStepSettings;
    ReleaseSRWLockShared(&gStepLock);
    return settings;
}

char const* StepRuleName(StepRule rule) {
    switch (rule) {
    case StepRule::Off: return "off";
    case StepRule::Camera: return "camera";
    }
    return "?";
}

StepStats GetStepStats() {
    AcquireSRWLockShared(&gStepLock);
    StepStats const stats = gStepStats;
    ReleaseSRWLockShared(&gStepLock);
    return stats;
}

void Clear() {
    gMesh.store(nullptr, std::memory_order_release);
    AcquireSRWLockExclusive(&gRequestLock);
    gRequest = {};
    gRequestOwner = gRequestWorld = nullptr;
    gRequestTimeMs = 0;
    ReleaseSRWLockExclusive(&gRequestLock);
}

void PublishStance(Stance const& stance) {
    AcquireSRWLockExclusive(&gStanceLock);
    gStance = stance;
    gStanceThread = GetCurrentThreadId();
    gStanceTimeMs = GetTickCount64();
    ReleaseSRWLockExclusive(&gStanceLock);
    gStanceMesh.store(gBroken.load() || gStanceBroken.load() || stance.nodeCount <= 0 ? nullptr : stance.mesh, std::memory_order_release);
}

void ClearStance() {
    gStanceMesh.store(nullptr, std::memory_order_release);
    AcquireSRWLockExclusive(&gStanceLock);
    gStance = {};
    gStanceTimeMs = 0;
    ReleaseSRWLockExclusive(&gStanceLock);
}

HandCapture const& LatestHandCapture() {
    return gHandCapture;
}

void PublishTurret(Turret const& turret) {
    AcquireSRWLockExclusive(&gTurretLock);
    gTurret = turret;
    gTurretThread = GetCurrentThreadId();
    gTurretTimeMs = GetTickCount64();
    ReleaseSRWLockExclusive(&gTurretLock);
    gTurretMesh.store(gTurretBroken.load() || turret.count <= 0 ? nullptr : turret.mesh, std::memory_order_release);
}

void ClearTurret() {
    gTurretMesh.store(nullptr, std::memory_order_release);
    AcquireSRWLockExclusive(&gTurretLock);
    gTurret = {};
    gTurretTimeMs = 0;
    ReleaseSRWLockExclusive(&gTurretLock);
}

TurretStats GetTurretStats() {
    TurretStats stats = gTurretStats;
    stats.broken = gTurretBroken.load();
    return stats;
}

void PublishWeaponPart(WeaponPart const& part) {
    AcquireSRWLockExclusive(&gPartLock);
    gPart = part;
    gPartThread = GetCurrentThreadId();
    gPartTimeMs = GetTickCount64();
    ReleaseSRWLockExclusive(&gPartLock);
    gPartMesh.store(gPartBroken.load() || part.count <= 0 ? nullptr : part.mesh, std::memory_order_release);
}

void ClearWeaponPart() {
    gPartMesh.store(nullptr, std::memory_order_release);
    AcquireSRWLockExclusive(&gPartLock);
    gPart = {};
    gPartTimeMs = 0;
    ReleaseSRWLockExclusive(&gPartLock);
    gPartMemory = {};  // the game thread (the hook runs there for the part's mesh)
}

WeaponPartStats GetWeaponPartStats() {
    WeaponPartStats stats = gPartStats;
    stats.broken = gPartBroken.load();
    return stats;
}

StanceStats GetStanceStats() {
    StanceStats stats = gStanceStats;
    stats.order = static_cast<int>(gOrder.Result());
    stats.confirmations = gOrder.Confirmations();
    stats.insideCalls = gOrder.InsideCalls();
    stats.broken = gStanceBroken.load();
    return stats;
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

std::optional<ContactSample> LatestContact() {
    Mesh const* const mesh = gMesh.load(std::memory_order_acquire);
    AcquireSRWLockShared(&gSampleLock);
    bool const fresh = mesh != nullptr && gContactMesh == mesh && GetTickCount64() - gContactTimeMs <= kMaxRequestAgeMs;
    ContactSample const sample = gContact;
    ReleaseSRWLockShared(&gSampleLock);
    return fresh ? std::optional<ContactSample>(sample) : std::nullopt;
}

std::optional<FingersSample> LatestFingers() {
    Mesh const* const mesh = gMesh.load(std::memory_order_acquire);
    AcquireSRWLockShared(&gSampleLock);
    bool const fresh = mesh != nullptr && gFingersMesh == mesh && GetTickCount64() - gFingersTimeMs <= kMaxRequestAgeMs;
    FingersSample const sample = gFingers;
    ReleaseSRWLockShared(&gSampleLock);
    return fresh ? std::optional<FingersSample>(sample) : std::nullopt;
}

std::optional<Unscaled> UnscaledBody(std::uint64_t maxAgeMs) {
    AcquireSRWLockShared(&gSampleLock);
    Unscaled const copy = gUnscaled;
    std::uint64_t const at = gUnscaledMs;
    ReleaseSRWLockShared(&gSampleLock);
    if (at == 0 || GetTickCount64() - at > maxAgeMs) {
        return std::nullopt;
    }
    return copy;
}

KickSamples WrittenGunKicks() {
    Mesh const* const mesh = gMesh.load(std::memory_order_acquire);
    AcquireSRWLockShared(&gSampleLock);
    KickSamples samples = gKicks;
    ReleaseSRWLockShared(&gSampleLock);
    // Only while the writer still writes that mesh: once it stops (cleared, republished for another mesh, broken) the engine
    // composes the pose without us from the next call on.
    if (mesh == nullptr || samples.mesh != mesh || gBroken.load(std::memory_order_relaxed)) {
        std::uint64_t const total = samples.kickedWrites;
        samples = {};
        samples.kickedWrites = total;
    }
    return samples;
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
    static std::uint64_t loggedSteps = 0;  // the heartbeat's thread
    if (StepStats const steps = GetStepStats(); steps.steps != loggedSteps) {
        loggedSteps = steps.steps;
        StepSettings const settings = GetStepSettings();
        log::Info("    arm IK writeback: {} steps between the goals and the write in {} writes since the hook went in (the camera's "
                  "bone up to {:.1f} units, the mesh up to {:.1f}; by the bone's move: {} under 1 unit, {} 1-5, {} 5-20, {} past 20), {} "
                  "solved in the goals' frame (rule {}, past {:.0f} units)",
            steps.steps, steps.writes, steps.maxAnchorUnits, steps.maxComponentUnits, steps.byAnchor[0], steps.byAnchor[1],
            steps.byAnchor[2], steps.byAnchor[3], steps.corrected, StepRuleName(settings.rule), settings.units);
    }
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
