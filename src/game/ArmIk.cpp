#include "game/ArmIk.hpp"

#include "core/Log.hpp"
#include "core/ik/ArmPose.hpp"
#include "core/ik/BoneNames.hpp"
#include "core/ik/HandFrame.hpp"
#include "game/HandCalibration.hpp"
#include "engine/BonePose.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/Objects.hpp"
#include "engine/Skeleton.hpp"
#include "game/HandTargets.hpp"
#include "game/Weapon.hpp"

#include <LESDK/Headers.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace mevr::game::arm_ik {
namespace {

enum Side { kLeft = 0, kRight = 1 };
constexpr char const* kSideNames[2] = {"left", "right"};
constexpr unsigned char kGameModeOnFoot = 0;  // SFXGameModeManager.EGameModes, all three games
// Desktop test: the right wrist this far from the shoulders' midpoint, in arm lengths
// (forward, outward, up): in front of the chest at about shoulder height, elbow bent.
constexpr ik::Vec3 kDesktopWrist{0.70, 0.15, -0.05};
// Our SpaceBases reading and the engine's GetBoneLocation must agree this closely (Unreal units).
constexpr double kLayoutTolerance = 0.5;
// An arm chain whose segments fall outside these (Unreal units, ratio) isn't an arm: not driven.
constexpr double kMinSegment = 3.0;
constexpr double kMaxSegment = 150.0;
constexpr double kMaxSegmentRatio = 2.5;

bool gEnabled = false;
std::string gStatus = "off";
std::string gLastLoggedStatus;
hand_targets::Source gSource = hand_targets::Source::None;
// This tick's request turns the right hand to aim the held gun (game/weapon's grip), and the
// writer has been writing the pose (engine::bone_pose::TotalWrites moved since the last tick).
bool gGunAimed = false;
bool gGunAimedAndWritten = false;
std::uint64_t gLastWrites = 0;

// Per skeleton asset: names, parents, arm chains. Re-resolved when the asset or its bones change.
struct SkeletonInfo {
    UObject const* asset = nullptr;  // compared by address only
    int boneCount = 0;
    engine::skeleton::RefSkeleton bones;
    std::array<std::optional<ik::ArmChain>, 2> arms;
    int head = -1;
    // Per hand: the middle, index and pinky finger base bones (-1: not found), for the hand frame.
    std::array<std::array<int, 3>, 2> fingers{{{-1, -1, -1}, {-1, -1, -1}}};
};
std::optional<SkeletonInfo> gSkeleton;

// The bone layout check, per mesh (addresses compared only), and which arms look like arms.
struct LayoutCheck {
    USkeletalMeshComponent const* mesh = nullptr;
    UObject const* asset = nullptr;
    bool passed = false;
    std::array<bool, 2> arms{};
};
LayoutCheck gLayout;

// VR: each hand's anatomical frame in its bone's own axes (core/ik/HandFrame), measured once per
// skeleton from the finger bases, so a controller grip maps to the hand the same way every time.
std::array<std::optional<ik::Quat>, 2> gAnatomy;
std::array<bool, 2> gAnatomyTried{};

// A hand's anatomical frame in its bone's axes, from the finger bases' positions relative to the
// hand bone (rigid children, so any pose will do). Logged once per skeleton and hand.
std::optional<ik::Quat> HandAnatomy(SkeletonInfo const& info, std::span<engine::skeleton::BoneAtom const> pose,
    int side, int handBone) {
    std::array<int, 3> const& fingers = info.fingers[side];
    char const* const sideName = side == kLeft ? "left" : "right";
    int const count = static_cast<int>(pose.size());
    for (int bone : fingers) {
        if (bone < 0 || bone >= count || handBone < 0 || handBone >= count) {
            log::Warn("arm IK: {} hand: finger base bones not found; the hand keeps its animated rotation", sideName);
            return std::nullopt;
        }
    }
    ik::Transform const hand = engine::skeleton::ToTransform(pose[handBone]);
    auto const local = [&](int bone) {
        return ik::Rotate(ik::Conjugate(hand.rotation), engine::skeleton::ToTransform(pose[bone]).translation - hand.translation);
    };
    ik::Vec3 const middle = local(fingers[0]), index = local(fingers[1]), pinky = local(fingers[2]);
    auto anatomy = ik::AnatomyInHand(middle, index, pinky);
    if (!anatomy) {
        log::Warn("arm IK: {} hand: the finger bases don't span a hand frame; the hand keeps its animated rotation", sideName);
        return std::nullopt;
    }
    log::Info("arm IK: {} hand frame from {}, {}, {} (local middle ({:.1f}, {:.1f}, {:.1f}), index ({:.1f}, {:.1f}, {:.1f}), "
              "pinky ({:.1f}, {:.1f}, {:.1f}))", sideName, info.bones.names[fingers[0]], info.bones.names[fingers[1]],
        info.bones.names[fingers[2]], middle.x, middle.y, middle.z, index.x, index.y, index.z, pinky.x, pinky.y, pinky.z);
    return anatomy;
}

struct Body {
    APawn* pawn = nullptr;
    USkeletalMeshComponent* mesh = nullptr;
};

std::optional<Body> PlayerBody() {
    APlayerController* const controller = engine::LocalPlayerController();
    APawn* const pawn = controller != nullptr ? controller->Pawn : nullptr;
    if (pawn == nullptr || !pawn->IsA(ABioPawn::StaticClass()) || pawn->Mesh == nullptr) {
        return std::nullopt;
    }
    USkeletalMeshComponent* const mesh = pawn->Mesh;
    if (mesh->SkeletalMesh == nullptr) {
        return std::nullopt;
    }
    return Body{pawn, mesh};
}

// On-foot gameplay: no conversation, cinematic, GUI or vehicle.
bool OnFoot() {
    APlayerController* const controller = engine::LocalPlayerController();
    if (controller == nullptr || !controller->IsA(ABioPlayerController::StaticClass())) {
        return false;
    }
    USFXGameModeManager const* const modes = engine::adapter::GameModes(static_cast<ABioPlayerController*>(controller));
    return modes != nullptr && modes->CurrentMode == kGameModeOnFoot;
}

std::string Format(engine::UnrealVector const& v) {
    return std::format("({:.1f}, {:.1f}, {:.1f})", v.x, v.y, v.z);
}

std::string ChainText(SkeletonInfo const& info, std::optional<ik::ArmChain> const& chain) {
    if (!chain) {
        return "not found";
    }
    auto const& names = info.bones.names;
    return std::format("{}({}) > {}({}) > {}({})", names[chain->upper], chain->upper, names[chain->fore], chain->fore,
        names[chain->hand], chain->hand);
}

// The asset's skeleton and arm chains, resolved once per asset (logged). Nullopt: unreadable.
SkeletonInfo const* Skeleton(USkeletalMeshComponent* mesh) {
    UObject const* const asset = mesh->SkeletalMesh;
    int const count = engine::skeleton::RefBoneCount(mesh);
    if (gSkeleton && gSkeleton->asset == asset && gSkeleton->boneCount == count) {
        // Same address and size; also the same arm bones (a level load can put another asset there).
        bool same = true;
        for (auto const& arm : gSkeleton->arms) {
            for (int bone : {arm ? arm->upper : -1, arm ? arm->fore : -1, arm ? arm->hand : -1}) {
                same = same && (bone < 0 || engine::skeleton::RefBoneKey(mesh, bone) == gSkeleton->bones.keys[bone]);
            }
        }
        if (same) {
            return &*gSkeleton;
        }
    }
    gSkeleton.reset();
    gLayout = {};
    gAnatomy = {};
    gAnatomyTried = {};
    auto bones = engine::skeleton::ReadRefSkeleton(mesh);
    if (!bones) {
        return nullptr;
    }
    SkeletonInfo info;
    info.asset = asset;
    info.boneCount = count;
    info.bones = std::move(*bones);
    for (int side : {kLeft, kRight}) {
        info.arms[side] = ik::FindArmChain(info.bones.names, info.bones.parents, side);
    }
    for (int i = 0; i < count; ++i) {
        if (ik::IsHeadBone(info.bones.names[i])) {
            info.head = i;
            break;
        }
    }
    for (int side : {kLeft, kRight}) {
        std::string const prefix = side == kLeft ? "Left" : "Right";
        std::array<char const*, 3> const fingers{"MiddleFinger", "IndexFinger", "PinkFinger"};
        for (std::size_t f = 0; f < fingers.size(); ++f) {
            for (int i = 0; i < count; ++i) {
                if (info.bones.names[i] == prefix + fingers[f]) {
                    info.fingers[side][f] = i;
                    break;
                }
            }
        }
    }
    log::Info("arm IK: skeleton {} ({} bones): left arm {}; right arm {}; head {}", engine::FullName(mesh->SkeletalMesh), count,
        ChainText(info, info.arms[kLeft]), ChainText(info, info.arms[kRight]),
        info.head >= 0 ? std::format("{}({})", info.bones.names[info.head], info.head) : "not found");
    gSkeleton = std::move(info);
    return &*gSkeleton;
}

// Our reading of SpaceBases against the engine's GetBoneLocation (component and world space),
// for the head and both hands. Once per mesh, before the first write.
LayoutCheck CheckLayout(USkeletalMeshComponent* mesh, SkeletonInfo const& info) {
    using engine::skeleton::Space;
    std::vector<int> bones;
    if (info.head >= 0) {
        bones.push_back(info.head);
    }
    for (auto const& arm : info.arms) {
        if (arm) {
            bones.push_back(arm->hand);
        }
    }
    bool ok = !bones.empty();
    for (int bone : bones) {
        for (Space space : {Space::Component, Space::World}) {
            auto const ours = engine::skeleton::BoneLocation(mesh, bone, space);
            auto const native = engine::skeleton::NativeBoneLocation(mesh, engine::skeleton::NameOf(info.bones.keys[bone]), space);
            double const error = ours && native ? std::hypot(ours->x - native->x, ours->y - native->y, ours->z - native->z) : -1.0;
            bool const match = error >= 0.0 && error <= kLayoutTolerance;
            log::Info("arm IK: layout check {} {}: SpaceBases {} vs GetBoneLocation {}: {}", info.bones.names[bone],
                space == Space::World ? "world" : "component", ours ? Format(*ours) : "none", native ? Format(*native) : "none",
                match ? "match" : "MISMATCH");
            ok = ok && match;
        }
    }
    log::Info("arm IK: bone layout {} for {}", ok ? "verified" : "NOT verified: no writes", engine::FullName(mesh));
    LayoutCheck check{mesh, info.asset, ok, {}};
    // Segment lengths from the animated pose: a wrong chain shows up here.
    std::span<engine::skeleton::BoneAtom const> const pose = engine::skeleton::SpaceBases(mesh);
    for (int side : {kLeft, kRight}) {
        auto const& chain = info.arms[side];
        if (!chain || !ok) {
            continue;
        }
        auto const at = [&](int bone) { return engine::skeleton::ToTransform(pose[bone]).translation; };
        double const upper = ik::Length(at(chain->fore) - at(chain->upper));
        double const lower = ik::Length(at(chain->hand) - at(chain->fore));
        bool const plausible = upper >= kMinSegment && upper <= kMaxSegment && lower >= kMinSegment && lower <= kMaxSegment &&
            upper <= lower * kMaxSegmentRatio && lower <= upper * kMaxSegmentRatio;
        check.arms[side] = plausible;
        log::Info("arm IK: {} arm upper {:.1f}, forearm {:.1f} units: {}", kSideNames[side], upper, lower,
            plausible ? "plausible" : "IMPLAUSIBLE, not driven");
    }
    return check;
}

// The body's axes in component space, from the pawn's facing.
std::optional<ik::BodyFrame> BodyAxes(APawn const* pawn, ik::Affine const& worldToComponent) {
    double const yaw = pawn->Rotation.Yaw * std::numbers::pi / 32768.0;
    ik::Vec3 const forwardWorld{std::cos(yaw), std::sin(yaw), 0.0};
    ik::Vec3 const rightWorld{-std::sin(yaw), std::cos(yaw), 0.0};
    ik::Vec3 const upWorld{0.0, 0.0, 1.0};
    ik::BodyFrame frame;
    frame.up = ik::Normalize(ik::TransformDirection(worldToComponent, upWorld));
    ik::Vec3 forward = ik::TransformDirection(worldToComponent, forwardWorld);
    forward = forward - frame.up * ik::Dot(forward, frame.up);
    ik::Vec3 right = ik::TransformDirection(worldToComponent, rightWorld);
    right = right - frame.up * ik::Dot(right, frame.up);
    if (ik::Length(forward) < 1e-6 || ik::Length(right) < 1e-6) {
        return std::nullopt;
    }
    frame.forward = ik::Normalize(forward);
    right = right - frame.forward * ik::Dot(right, frame.forward);
    frame.right = ik::Normalize(right);
    return frame;
}

void Stop(std::string status) {
    engine::bone_pose::Clear();
    gStatus = std::move(status);
}

void LogStatusChange() {
    if (gStatus != gLastLoggedStatus) {
        log::Info("arm IK: {}", gStatus);
        gLastLoggedStatus = gStatus;
    }
}

void Drive() {
    hand_calibration::Refresh();
    if (!engine::bone_pose::Installed()) {
        return Stop("writeback hook not installed (see the log above)");
    }
    auto const body = PlayerBody();
    if (!body) {
        return Stop("no player body");
    }
    if (!OnFoot()) {
        return Stop("not on foot");
    }
    USkeletalMeshComponent* const mesh = body->mesh;
    if (engine::skeleton::PoseParent(mesh) != nullptr) {
        return Stop("the body mesh takes its pose from a ParentAnimComponent: not supported");
    }
    SkeletonInfo const* const info = Skeleton(mesh);
    if (info == nullptr) {
        return Stop("skeleton unreadable");
    }
    if (!info->arms[kLeft] && !info->arms[kRight]) {
        return Stop("no arm chains found in the skeleton");
    }
    if (static_cast<int>(engine::skeleton::SpaceBases(mesh).size()) != info->boneCount) {
        return Stop("pose not computed yet");
    }
    if (gLayout.mesh != mesh || gLayout.asset != info->asset) {
        gLayout = CheckLayout(mesh, *info);
    }
    if (!gLayout.passed) {
        return Stop("bone layout not verified (see the layout check lines)");
    }
    ik::Affine const componentToWorld = engine::skeleton::LocalToWorld(mesh);
    auto const worldToComponent = ik::InverseAffine(componentToWorld);
    auto const frame = worldToComponent ? BodyAxes(body->pawn, *worldToComponent) : std::nullopt;
    if (!frame) {
        return Stop("component transform unusable");
    }

    auto const headBone = info->head >= 0
        ? engine::skeleton::BoneLocation(mesh, info->head, engine::skeleton::Space::World)
        : std::nullopt;
    hand_targets::Targets const targets = hand_targets::Current(headBone);
    gSource = targets.source;
    engine::bone_pose::Request request;
    request.mesh = mesh;
    request.boneCount = info->boneCount;
    request.parents = info->bones.parents;
    request.body = *frame;
    // Arms that resolved and look like arms; the others stay with the animation.
    std::array<std::optional<ik::ArmChain>, 2> arms;
    for (int side : {kLeft, kRight}) {
        if (info->arms[side] && gLayout.arms[side]) {
            arms[side] = info->arms[side];
        }
    }
    for (int side : {kLeft, kRight}) {
        if (auto const& chain = arms[side]) {
            engine::bone_pose::Arm& arm = request.arms[side];
            arm.chain = *chain;
            arm.keys[0] = info->bones.keys[chain->upper];
            arm.keys[1] = info->bones.keys[chain->fore];
            arm.keys[2] = info->bones.keys[chain->hand];
        }
    }

    std::string driven;
    bool gunAimed = false;
    if (targets.source == hand_targets::Source::DesktopTest) {
        if (!arms[kRight]) {
            return Stop("desktop test: no usable right arm chain in the skeleton");
        }
        request.arms[kRight].target = engine::bone_pose::Target::Chest;
        request.arms[kRight].wrist = kDesktopWrist;
        driven = "right hand held in front of the chest (desktop test)";
    } else if (targets.source == hand_targets::Source::Controllers) {
        auto const componentRotation = ik::RotationOf(componentToWorld);
        std::span<engine::skeleton::BoneAtom const> const pose = engine::skeleton::SpaceBases(mesh);
        for (int side : {kLeft, kRight}) {
            hand_targets::Hand const& hand = targets.hands[side];
            auto const& chain = arms[side];
            if (!hand.valid || !chain) {
                continue;
            }
            engine::bone_pose::Arm& arm = request.arms[side];
            arm.target = engine::bone_pose::Target::Component;
            arm.wrist = ik::TransformPoint(*worldToComponent, {hand.position.x, hand.position.y, hand.position.z});
            if (!gAnatomyTried[side]) {
                gAnatomyTried[side] = true;
                gAnatomy[side] = HandAnatomy(*info, pose, side, chain->hand);
            }
            // Holding a gun: the hand turns so the gun's barrel points along the controller's
            // aim ray, upright with the controller (core/ik HandForBarrel, game/weapon's grip).
            if (side == kRight && hand.hasAim && componentRotation) {
                if (auto const grip = weapon::RightHandGrip()) {
                    ik::Quat const aim = ik::Normalize(ik::Conjugate(*componentRotation) * hand.aim);
                    if (auto const rotation = ik::HandForBarrel(grip->barrel, grip->up, ik::Rotate(aim, {1.0, 0.0, 0.0}),
                            ik::Rotate(aim, {0.0, 0.0, 1.0}))) {
                        arm.hasRotation = true;
                        arm.rotation = *rotation;
                        gunAimed = true;
                    }
                }
            }
            if (!arm.hasRotation && hand.hasRotation && componentRotation && gAnatomy[side]) {
                ik::Quat const grip = ik::Normalize(ik::Conjugate(*componentRotation) * hand.rotation);
                arm.hasRotation = true;
                arm.rotation = ik::HandFromGrip(grip, hand_calibration::Trim(side), *gAnatomy[side]);
            }
            driven += driven.empty() ? kSideNames[side] : std::string(" and ") + kSideNames[side];
        }
        if (!driven.empty()) {
            driven += " following the controllers";
            if (gunAimed) {
                driven += " (the right hand points the gun's barrel along the controller's aim)";
            }
        }
    }
    if (driven.empty()) {
        return Stop(std::format("no hand targets (source {})", hand_targets::Name(targets.source)));
    }
    engine::bone_pose::Publish(std::move(request));
    gStatus = "driving: " + driven;
    gGunAimed = gunAimed;
}

} // namespace

void Tick(float /*deltaSeconds*/) {
    // The writer wrote the player's pose since the last tick: what was published is being shown.
    std::uint64_t const writes = engine::bone_pose::TotalWrites();
    bool const written = writes != gLastWrites;
    gLastWrites = writes;
    gGunAimed = false;
    if (!gEnabled) {
        gGunAimedAndWritten = false;
        if (gStatus != "off") {
            Stop("off");
            LogStatusChange();
        }
        return;
    }
    Drive();
    gGunAimedAndWritten = gGunAimed && written;
    LogStatusChange();
    engine::bone_pose::LogFirstWrite();
}

bool RightHandAimsGun() {
    return gGunAimedAndWritten;
}

bool Toggle() {
    gEnabled = !gEnabled;
    return gEnabled;
}

void SetEnabled(bool on) {
    gEnabled = on;
}

bool Enabled() {
    return gEnabled;
}

void LogStats() {
    if (gEnabled) {
        log::Info("    arm IK: {} (hand source {})", gStatus, hand_targets::Name(gSource));
    }
    engine::bone_pose::LogStats();
}

void Shutdown() {
    engine::bone_pose::Clear();
    gEnabled = false;
    gGunAimed = gGunAimedAndWritten = false;
    gLastWrites = 0;
    gStatus = "off";
    gLastLoggedStatus.clear();
    gSource = hand_targets::Source::None;
    gSkeleton.reset();
    gLayout = {};
    gAnatomy = {};
    gAnatomyTried = {};
}

} // namespace mevr::game::arm_ik
