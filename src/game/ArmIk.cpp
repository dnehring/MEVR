#include "game/ArmIk.hpp"

#include "core/Log.hpp"
#include "core/ik/ArmLength.hpp"
#include "core/ik/ArmPose.hpp"
#include "core/ik/BoneNames.hpp"
#include "core/ik/CameraHook.hpp"
#include "core/ik/HandFrame.hpp"
#include "core/ik/TorsoPose.hpp"
#include "core/ik/TouchReference.hpp"
#include "core/vr/Controllers.hpp"
#include "game/HandCalibration.hpp"
#include "engine/BonePose.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/Objects.hpp"
#include "engine/Skeleton.hpp"
#include "game/HandTargets.hpp"
#include "game/Sights.hpp"
#include "game/SupportGrip.hpp"
#include "game/Weapon.hpp"

#include <LESDK/Headers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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
// This tick's request asks the writer for the support grip (engine::bone_pose::Support).
bool gSupportRequested = false;
// The torso's heading (OpenXR yaw in tracking space): follows the head past a deadzone, pulled
// toward where the hands are (ik::BodyYaw). Reset whenever the torso isn't driven.
ik::BodyYaw gBodyYaw;
bool gBodyYawActive = false;
hand_targets::Anchor gLoggedAnchor = hand_targets::Anchor::None;
double gLoggedScale = 0.0;
// The torso heading leans this far toward the hands (both tracked, in front of the head).
constexpr double kHandsYawWeight = 0.5;
constexpr double kHandsMinMeters = 0.15;
constexpr double kHandsMaxDegrees = 100.0;
// Reach scaling from the arm-length calibration stays within these.
constexpr double kMinReachScale = 0.5;
constexpr double kMaxReachScale = 3.0;

// Per skeleton asset: names, parents, arm chains. Re-resolved when the asset or its bones change.
struct SkeletonInfo {
    UObject const* asset = nullptr;  // compared by address only
    int boneCount = 0;
    engine::skeleton::RefSkeleton bones;
    std::array<std::optional<ik::ArmChain>, 2> arms;
    int head = -1;
    // Per hand: the middle, index and pinky finger base bones (-1: not found), for the hand frame.
    std::array<std::array<int, 3>, 2> fingers{{{-1, -1, -1}, {-1, -1, -1}}};
    // The spine and clavicles (core/ik/TorsoPose) and the eyes (-1: not found).
    std::optional<ik::TorsoChain> torso;
    std::array<int, 2> eyes{-1, -1};
    // LHIK: the off-hand IK goal (under Prop01 under RightWrist; research/14), -1: not found.
    int lhik = -1;
    // The camera hook bones (`Camera`, `God`; research/18): the writer never writes them.
    std::vector<int> cameraHooks;
};
std::optional<SkeletonInfo> gSkeleton;

// The bone layout check, per mesh (addresses compared only), and which arms look like arms.
struct LayoutCheck {
    USkeletalMeshComponent const* mesh = nullptr;
    UObject const* asset = nullptr;
    bool passed = false;
    std::array<bool, 2> arms{};
    std::array<double, 2> lengths{};  // upper arm + forearm, animated (Unreal units)
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

// Once per grip (weapon): how the gun is held, against what its model's axes would have said.
std::optional<weapon::Grip> gLoggedGrip;

void LogGunHold(weapon::Grip const& grip, std::optional<ik::Vec3> const& upInHand) {
    if (gLoggedGrip && ik::Length(gLoggedGrip->muzzle - grip.muzzle) < 1e-6 && ik::Length(gLoggedGrip->barrel - grip.barrel) < 1e-9) {
        return;
    }
    gLoggedGrip = grip;
    if (!upInHand) {
        log::Warn("arm IK: gun held, but no thumb-side up for it ({}): the hand doesn't aim it",
            gAnatomy[kRight] ? "barrel along the thumb side" : "no right hand frame");
        return;
    }
    double const apart = std::acos(std::clamp(ik::Dot(*upInHand, grip.up), -1.0, 1.0)) * 180.0 / std::numbers::pi;
    log::Info("arm IK: gun held thumb side up, roll trim {:.1f} degrees: up in the hand's axes ({:.3f}, {:.3f}, {:.3f}); "
              "the weapon model's up is {:.0f} degrees from it (ignored)",
        hand_calibration::GunRoll(), upInHand->x, upInHand->y, upInHand->z, apart);
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

// LE1's camera hangs off the bone named `Camera` (`God` for a sniper crouch mode), read from
// SpaceBases (research/18): if we moved it, the view would follow our writes (489ef51's loop).
// The writer never writes these (Request::keep). Once per skeleton, whether any subtree we write
// (the bottom spine joint's, the clavicles', the upper arms') contains one: it shouldn't, since
// they hang off the root, not the spine.
void LogCameraHooks(SkeletonInfo& info) {
    info.cameraHooks = ik::FindCameraHookBones(info.bones.names);
    auto const& names = info.bones.names;
    auto const& parents = info.bones.parents;
    if (info.cameraHooks.empty()) {
        log::Warn("arm IK: no Camera or God bone in this skeleton: the camera hook can't be guarded (research/18)");
        return;
    }
    std::vector<int> roots;
    std::string written;
    auto const add = [&](int bone, char const* what) {
        if (bone >= 0) {
            roots.push_back(bone);
            written += std::format("{}{} {}({})", written.empty() ? "" : ", ", what, names[bone], bone);
        }
    };
    if (info.torso && !info.torso->spine.empty()) {
        add(info.torso->spine.front(), "spine");
        add(info.torso->collars[kLeft], "clavicle");
        add(info.torso->collars[kRight], "clavicle");
    }
    for (auto const& arm : info.arms) {
        add(arm ? arm->upper : -1, "arm");
    }
    std::string hooks;
    for (int bone : info.cameraHooks) {
        int const parent = parents[bone];
        hooks += std::format("{}{}({}, parent {})", hooks.empty() ? "" : ", ", names[bone], bone,
            parent >= 0 ? std::format("{}({})", names[parent], parent) : std::string("none: the root"));
    }
    std::vector<int> const inside = ik::InSubtrees(parents, roots, info.cameraHooks);
    if (inside.empty()) {
        log::Info("arm IK: camera hook bones {}: outside every subtree we write ({}), so the view can't follow our "
                  "writes; the writer skips them anyway",
            hooks, written.empty() ? "none" : written);
    } else {
        std::string text;
        for (int bone : inside) {
            text += std::format("{}{}({})", text.empty() ? "" : ", ", names[bone], bone);
        }
        log::Warn("arm IK: camera hook bones {}: {} INSIDE a subtree we write ({}): the writer keeps them animated, but the "
                  "bones around them move (research/18)",
            hooks, text, written);
    }
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
    for (int i = 0; i < count; ++i) {
        if (_stricmp(info.bones.names[i].c_str(), "LHIK") == 0) {
            info.lhik = i;
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
    // The eyes: a bone named just "eye" and a side (Eye_Left), not eyeBlink, underEye or lids.
    for (int i = 0; i < count; ++i) {
        std::vector<std::string> const tokens = ik::NameTokens(info.bones.names[i]);
        if (tokens.size() == 2 && tokens[0] == "eye") {
            int const side = tokens[1] == "left" || tokens[1] == "l" ? kLeft : tokens[1] == "right" || tokens[1] == "r" ? kRight : -1;
            if (side >= 0 && info.eyes[side] < 0) {
                info.eyes[side] = i;
            }
        }
    }
    if (info.arms[kLeft] && info.arms[kRight]) {
        info.torso = ik::FindTorsoChain(info.bones.names, info.bones.parents, *info.arms[kLeft], *info.arms[kRight]);
    }
    std::string spine;
    if (info.torso) {
        for (int bone : info.torso->spine) {
            spine += std::format("{}{}({})", spine.empty() ? "" : " > ", info.bones.names[bone], bone);
        }
    }
    auto const named = [&](int bone) {
        return bone >= 0 ? std::format("{}({})", info.bones.names[bone], bone) : std::string("none");
    };
    log::Info("arm IK: torso: spine {}; clavicles {} and {}; eyes {} and {}; off-hand IK goal {}",
        info.torso ? spine : std::string("not found"), named(info.torso ? info.torso->collars[kLeft] : -1),
        named(info.torso ? info.torso->collars[kRight] : -1), named(info.eyes[kLeft]), named(info.eyes[kRight]), named(info.lhik));
    LogCameraHooks(info);
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
        check.lengths[side] = upper + lower;
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
    gBodyYawActive = false;
    gSupportRequested = false;
    gStatus = std::move(status);
}

// The AnimTree's own off-hand IK, the SkelControlLimb named `LeftArm` (research/18): found by
// name once per AnimTree, then followed by address (the lists are walked each time, so a control
// that went away is never read).
#if defined(SDK_TARGET_LE1)
static_assert(offsetof(USkelControlBase, ControlStrength) == 0xA8);
static_assert(offsetof(USkelControlBase, StrengthTarget) == 0xB4);
#endif
constexpr int kMaxControlsPerList = 32;
UAnimTree const* gIkTree = nullptr;
USkelControlBase const* gIkControl = nullptr;
bool gIkSearched = false;

USkelControlBase* FindControl(UAnimTree* tree, USkelControlBase const* address, char const* name) {
    for (FSkelControlListHead const& list : tree->SkelControlLists) {
        int depth = 0;
        for (USkelControlBase* control = list.ControlHead; control != nullptr && depth < kMaxControlsPerList;
             control = control->NextControl, ++depth) {
            if (address != nullptr ? control == address : engine::ToString(control->ControlName) == name) {
                return control;
            }
        }
    }
    return nullptr;
}

std::optional<support_grip::NativeIkSample> NativeLeftArmIk(USkeletalMeshComponent* mesh) {
    UAnimNode* const animations = mesh->Animations;
    if (animations == nullptr || !animations->IsA(UAnimTree::StaticClass())) {
        return std::nullopt;
    }
    auto* const tree = static_cast<UAnimTree*>(animations);
    if (tree != gIkTree) {
        gIkTree = tree;
        gIkControl = nullptr;
        gIkSearched = false;
    }
    USkelControlBase* control = nullptr;
    if (gIkControl != nullptr) {
        control = FindControl(tree, gIkControl, nullptr);
        gIkSearched = gIkSearched && control != nullptr;  // it went away: look it up by name again
    }
    if (control == nullptr && !gIkSearched) {
        control = FindControl(tree, nullptr, "LeftArm");  // once per tree, unless it goes away
        gIkSearched = true;
    }
    gIkControl = control;
    if (control == nullptr) {
        return std::nullopt;
    }
    return support_grip::NativeIkSample{control->ControlStrength, control->StrengthTarget, control->bSetStrengthFromAnimNode != 0};
}

// The two-handed grip (game/support_grip): this tick's foregrip candidates, all in the right hand
// bone's axes, and, once a foregrip has settled and the left controller is followed, the writer's
// support request. LHIK hangs below the right hand (Prop01), so our write carries it rigidly: its
// place in the hand's axes is the animation's either way.
bool RequestSupport(USkeletalMeshComponent* mesh, SkeletonInfo const& info, std::span<engine::skeleton::BoneAtom const> pose,
    weapon::Grip const& grip, ik::Vec3 const& up, hand_targets::Targets const& targets, engine::bone_pose::Request& request) {
    int const right = info.arms[kRight]->hand;
    int const count = static_cast<int>(pose.size());
    if (right < 0 || right >= count) {
        return false;
    }
    support_grip::Candidates c;
    c.gun = weapon::Key();
    c.barrel = grip.barrel;
    c.muzzle = grip.muzzle;
    c.up = up;
    c.socket = weapon::OffHandSocketInHand();
    ik::Transform const hand = engine::skeleton::ToTransform(pose[right]);
    if (info.lhik >= 0 && info.lhik < count) {
        c.lhik = ik::Relative(engine::skeleton::ToTransform(pose[info.lhik]), hand).translation;
    }
    if (auto const animated = engine::bone_pose::AnimatedHands()) {
        c.animatedLeft = ik::Relative(animated->left, animated->right);
    }
    c.nativeIk = NativeLeftArmIk(mesh);
    support_grip::Measure(c);
    auto const foregrip = support_grip::Current();
    if (!foregrip || !targets.hands[kLeft].valid || request.arms[kLeft].target != engine::bone_pose::Target::Component ||
        !request.arms[kRight].hasRotation) {
        return false;
    }
    engine::bone_pose::Support& s = request.support;
    s.enabled = true;
    s.foregrip.translation = foregrip->position;
    s.foregrip.rotation = foregrip->rotation;
    s.foregrip.scale = 1.0;
    s.hasRotation = foregrip->hasRotation;
    s.twoHandAim = foregrip->twoHandAim;
    s.blend = support_grip::Blend();
    double const reach = request.arms[kLeft].reachScale;
    s.unitsPerMeter = targets.body.unitsPerMeter * (std::isfinite(reach) && reach > 0.0 ? reach : 1.0);
    // The grab zone: a capsule along the gun, not just a point at the foregrip (support_grip::Zone).
    support_grip::Zone const zone = support_grip::CurrentZone();
    s.hasGun = zone.capsule;
    s.barrel = grip.barrel;
    s.muzzle = grip.muzzle;
    s.frontMeters = zone.frontMeters;
    s.pastMeters = zone.pastMeters;
    s.hold = support_grip::HoldSerial();
    return true;
}

void LogStatusChange() {
    if (gStatus != gLastLoggedStatus) {
        log::Info("arm IK: {}", gStatus);
        gLastLoggedStatus = gStatus;
    }
}

// The torso's facing in the world: the headset's yaw (with a deadzone), pulled toward the hands.
std::optional<ik::Vec3> TorsoFacingWorld(hand_targets::Targets const& targets, double deltaSeconds) {
    XrQuaternionf const& q = targets.head.orientation;
    double const headYaw = ik::YawOf(q.x, q.y, q.z, q.w);
    double yaw = headYaw;
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (controllers.active && controllers.left.gripValid && controllers.right.gripValid) {
        XrVector3f const& l = controllers.left.grip.position;
        XrVector3f const& r = controllers.right.grip.position;
        XrVector3f const& h = targets.head.position;
        double const dx = 0.5 * (l.x + r.x) - h.x;
        double const dz = 0.5 * (l.z + r.z) - h.z;
        double const handsYaw = std::atan2(-dx, -dz);  // BodyYaw's convention: 0 = -Z, + = left
        double const apart = std::abs(ik::BlendYaw(0.0, handsYaw - headYaw, 1.0)) * 180.0 / std::numbers::pi;
        if (std::hypot(dx, dz) >= kHandsMinMeters && apart <= kHandsMaxDegrees) {
            yaw = ik::BlendYaw(headYaw, handsYaw, kHandsYawWeight);
        }
    }
    if (!gBodyYawActive) {
        gBodyYaw.Reset(yaw);
        gBodyYawActive = true;
    }
    double const body = gBodyYaw.Update(yaw, deltaSeconds);
    // Tracking-space forward for that yaw, into the world through the body frame.
    XrVector3f const& h = targets.head.position;
    XrVector3f const ahead{h.x - static_cast<float>(std::sin(body)), h.y, h.z - static_cast<float>(std::cos(body))};
    engine::UnrealVector const a = engine::ToWorld(targets.body, h);
    engine::UnrealVector const b = engine::ToWorld(targets.body, ahead);
    ik::Vec3 const forward{b.x - a.x, b.y - a.y, b.z - a.z};
    if (!(ik::Length(forward) > 1e-6)) {
        return std::nullopt;
    }
    return ik::Normalize(forward);
}

// First person, every few seconds: where Halcyon's camera sits relative to Shepard's head bone, in
// the view's own axes (the headset's yaw, world up), and the MELEVR.ini camera offsets that would
// cancel it. Halcyon's first-person camera sat about 44 units from his eyes (2026-10-08), so the
// player stood in front of Shepard's shoulders. Stand where you recentered for a clean reading.
std::uint64_t gNextCameraLogMs = 0;
void LogCameraOffset(hand_targets::Targets const& targets) {
    std::uint64_t const now = GetTickCount64();
    if (targets.anchor != hand_targets::Anchor::View || targets.viewToHeadBone < 0.0 || now < gNextCameraLogMs) {
        return;
    }
    gNextCameraLogMs = now + 5000;
    XrVector3f const& h = targets.head.position;
    XrQuaternionf const& q = targets.head.orientation;
    // The headset's forward (-Z), flattened, into the world.
    double const fx = -2.0 * (q.x * q.z + q.w * q.y), fz = -(1.0 - 2.0 * (q.x * q.x + q.y * q.y));
    double const flat = std::hypot(fx, fz);
    if (!(flat > 1e-6)) {
        return;
    }
    XrVector3f const ahead{h.x + static_cast<float>(fx / flat), h.y, h.z + static_cast<float>(fz / flat)};
    engine::UnrealVector const a = engine::ToWorld(targets.body, h);
    engine::UnrealVector const b = engine::ToWorld(targets.body, ahead);
    ik::Vec3 const up{0.0, 0.0, 1.0};
    ik::Vec3 const forward = ik::Normalize(ik::Vec3{b.x - a.x, b.y - a.y, 0.0});
    ik::Vec3 const right = ik::Cross(up, forward);
    ik::Vec3 const d{targets.viewFromHeadBone.x, targets.viewFromHeadBone.y, targets.viewFromHeadBone.z};
    double const f = ik::Dot(d, forward), r = ik::Dot(d, right), u = ik::Dot(d, up);
    // The same at the recenter point (the view without the player's own lean), for the menu.
    engine::UnrealVector const origin = engine::OriginWorld(targets.body);
    ik::Vec3 const n{origin.x - a.x + d.x, origin.y - a.y + d.y, origin.z - a.z + d.z};
    hand_calibration::WriteCameraOffset(ik::Dot(n, forward), ik::Dot(n, right), ik::Dot(n, up));
    // Halcyon's camera offsets are metres at 50 units each (kMetersToUU); flatScreenDistance + is back.
    log::Info("arm IK: first person camera vs Shepard's head bone: forward {:.1f}, right {:.1f}, up {:.1f} units "
              "(headset {:.2f}/{:.2f}/{:.2f} m from the recenter point); to cancel: flatScreenDistance = {:.3f}, "
              "cameraOffsetRight = {:.3f}, cameraOffsetUp = {:.3f} (plus the current values)",
        f, r, u, h.x, h.y, h.z, f / 50.0, -r / 50.0, -u / 50.0);
}

void LogAnchor(hand_targets::Targets const& targets) {
    if (targets.anchor == gLoggedAnchor && std::abs(targets.body.unitsPerMeter - gLoggedScale) < 1e-6) {
        return;
    }
    gLoggedAnchor = targets.anchor;
    gLoggedScale = targets.body.unitsPerMeter;
    log::Info("arm IK: body anchor: {}, view {} from Shepard's animated head bone, {:.1f} units per meter",
        hand_targets::Name(targets.anchor),
        targets.viewToHeadBone >= 0.0 ? std::format("{:.0f} units", targets.viewToHeadBone) : std::string("(no head bone)"),
        targets.body.unitsPerMeter);
}

void Drive(double deltaSeconds) {
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

    // The head bone as the animation left it: our spine writes move it, and the hands' anchor
    // mustn't chase them (engine::bone_pose::AnimatedHead). Before the first write, the pose is
    // the animation's anyway.
    std::optional<engine::UnrealVector> headBone;
    if (auto const animated = engine::bone_pose::AnimatedHead()) {
        ik::Vec3 const world = ik::TransformPoint(componentToWorld, *animated);
        headBone = engine::UnrealVector{world.x, world.y, world.z};
    } else if (info->head >= 0) {
        headBone = engine::skeleton::BoneLocation(mesh, info->head, engine::skeleton::Space::World);
    }
    hand_targets::Targets const targets = hand_targets::Current(headBone);
    gSource = targets.source;
    engine::bone_pose::Request request;
    request.mesh = mesh;
    request.boneCount = info->boneCount;
    request.parents = info->bones.parents;
    request.body = *frame;
    request.head = info->head;
    request.keep = info->cameraHooks;
    for (int bone : info->cameraHooks) {
        request.keepKeys.push_back(info->bones.keys[bone]);
    }
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
    bool supportRequested = false;
    if (targets.source == hand_targets::Source::DesktopTest) {
        if (!arms[kRight]) {
            return Stop("desktop test: no usable right arm chain in the skeleton");
        }
        request.arms[kRight].target = engine::bone_pose::Target::Chest;
        request.arms[kRight].wrist = kDesktopWrist;
        driven = "right hand held in front of the chest (desktop test)";
    } else if (targets.source == hand_targets::Source::Controllers) {
        LogAnchor(targets);
        LogCameraOffset(targets);
        // The shoulders under the headset: the spine turns to the body's heading and leans so
        // Shepard's eyes (first person) or head (third person) come to the headset.
        bool torso = false;
        if (hand_calibration::BodyFollowsHead() && info->torso && arms[kLeft] && arms[kRight]) {
            auto const facing = TorsoFacingWorld(targets, deltaSeconds);
            std::array<int, 2> reference{info->head, -1};
            if (targets.anchor == hand_targets::Anchor::View && info->eyes[kLeft] >= 0 && info->eyes[kRight] >= 0) {
                reference = info->eyes;
            }
            if (facing && reference[0] >= 0) {
                engine::bone_pose::Torso& t = request.torso;
                t.enabled = true;
                t.chain = *info->torso;
                for (int bone : t.chain.spine) {
                    t.keys.push_back(info->bones.keys[bone]);
                }
                for (int bone : t.chain.collars) {
                    t.keys.push_back(bone >= 0 ? info->bones.keys[bone] : 0);
                }
                t.goal.reference = reference;
                if (targets.anchor == hand_targets::Anchor::View) {
                    // First person: Shepard's eyes move as far as the headset moved from where it
                    // was recentered (ik::TorsoGoal::relative). The head goes with the spine: the
                    // camera hangs off the `Camera` bone, not the head (research/18), and the
                    // writer never writes that (Request::keep). Not absolute: the view sits where
                    // the Camera bone's animation and Halcyon's per-state offsets put it, which
                    // wanders against the head bone by tens of units as the animation plays, so
                    // aiming the eyes at it would bend Shepard after the animation, not the player.
                    engine::UnrealVector const origin = engine::OriginWorld(targets.body);
                    t.goal.head = ik::TransformDirection(*worldToComponent,
                        {targets.headWorld.x - origin.x, targets.headWorld.y - origin.y, targets.headWorld.z - origin.z});
                    t.goal.relative = true;
                } else {
                    t.goal.head = ik::TransformPoint(*worldToComponent, {targets.headWorld.x, targets.headWorld.y, targets.headWorld.z});
                }
                ik::Vec3 forward = ik::TransformDirection(*worldToComponent, *facing);
                forward = forward - frame->up * ik::Dot(forward, frame->up);
                t.goal.forward = ik::Normalize(forward, frame->forward);
                torso = true;
            }
        }
        if (!torso) {
            gBodyYawActive = false;
        }
        request.maxStretch = hand_calibration::ArmStretch();
        auto const componentRotation = ik::RotationOf(componentToWorld);
        std::span<engine::skeleton::BoneAtom const> const pose = engine::skeleton::SpaceBases(mesh);
        std::optional<weapon::Grip> heldGrip;
        std::optional<ik::Vec3> heldUp;
        vr::controllers::State const controllers = vr::controllers::Latest();
        std::array<double, 2> fullReach{1.0, 1.0};
        std::array<double, 2> reachWeight{1.0, 1.0};
        for (int side : {kLeft, kRight}) {
            hand_targets::Hand const& hand = targets.hands[side];
            auto const& chain = arms[side];
            if (!hand.valid || !chain) {
                continue;
            }
            engine::bone_pose::Arm& arm = request.arms[side];
            arm.target = engine::bone_pose::Target::Component;
            arm.wrist = ik::TransformPoint(*worldToComponent, {hand.position.x, hand.position.y, hand.position.z});
            // Arm-length calibration: the player's full reach is Shepard's full reach, except near the
            // eyes, where the hand sits on its controller (a gun at the cheek lines up with the eye).
            if (double const player = hand_calibration::ArmLength(); player > 0.0 && gLayout.lengths[side] > 0.0) {
                fullReach[side] = std::clamp(gLayout.lengths[side] / (player * targets.body.unitsPerMeter), kMinReachScale, kMaxReachScale);
                vr::controllers::Hand const& tracked = side == kLeft ? controllers.left : controllers.right;
                if (controllers.active && tracked.gripValid) {
                    XrVector3f const& g = tracked.grip.position;
                    XrVector3f const& h = targets.head.position;
                    reachWeight[side] = ik::ReachWeight(std::hypot(g.x - h.x, g.y - h.y, g.z - h.z), hand_calibration::ReachNearFace());
                }
                arm.reachScale = ik::ScaledReach(fullReach[side], reachWeight[side]);
            }
            if (!gAnatomyTried[side]) {
                gAnatomyTried[side] = true;
                gAnatomy[side] = HandAnatomy(*info, pose, side, chain->hand);
            }
            // Holding a gun: the gun sits at a fixed pose on the controller and the hand follows
            // it. Its barrel points along the controller's aim ray (without one, along the Touch
            // reference frame's forward), and its top is the hand's thumb side turned to the
            // controller's up, so its roll follows the wrist. The weapon model's own axes don't
            // decide the roll (core/ik GunUpInHand, HandForBarrel; game/weapon's grip).
            if (side == kRight && componentRotation && (hand.hasAim || hand.hasRotation)) {
                if (auto const grip = weapon::RightHandGrip()) {
                    auto const upInHand = gAnatomy[side]
                        ? ik::GunUpInHand(grip->barrel, *gAnatomy[side], hand_calibration::GunRoll())
                        : std::nullopt;
                    LogGunHold(*grip, upInHand);
                    ik::Quat const held = hand.hasAim ? hand.aim : ik::Normalize(hand.rotation * ik::TouchReferenceRotationUe());
                    ik::Quat const aim = ik::Normalize(ik::Conjugate(*componentRotation) * held);
                    auto const rotation = upInHand ? ik::HandForBarrel(grip->barrel, *upInHand,
                                                         ik::Rotate(aim, {1.0, 0.0, 0.0}), ik::Rotate(aim, {0.0, 0.0, 1.0}))
                                                   : std::nullopt;
                    if (rotation) {
                        arm.hasRotation = true;
                        arm.rotation = *rotation;
                        gunAimed = true;
                        heldGrip = grip;
                        heldUp = upInHand;
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
        // A left hand holding the gun scales like the gun hand, so the line between them (two-hand aim)
        // isn't bent by the face rule: at the cheek both are 1:1.
        if (double const blend = support_grip::Blend(); blend > 0.0 && request.arms[kLeft].target == engine::bone_pose::Target::Component) {
            double const weight = reachWeight[kLeft] + (reachWeight[kRight] - reachWeight[kLeft]) * blend;
            request.arms[kLeft].reachScale = ik::ScaledReach(fullReach[kLeft], weight);
        }
        if (heldGrip && heldUp && arms[kLeft] && arms[kRight]) {
            supportRequested = RequestSupport(mesh, *info, pose, *heldGrip, *heldUp, targets, request);
        }
        // The sight alignment assist (game/sights): the held gun slides so its sight line runs through
        // the aiming eye; tracking meters through the body frame into component units.
        XrVector3f const shift = sights::TakeGunShift();
        if (gunAimed && (shift.x != 0.0f || shift.y != 0.0f || shift.z != 0.0f)) {
            XrVector3f const& p = targets.head.position;
            engine::UnrealVector const a = engine::ToWorld(targets.body, p);
            engine::UnrealVector const b = engine::ToWorld(targets.body, {p.x + shift.x, p.y + shift.y, p.z + shift.z});
            request.gunShift = ik::TransformDirection(*worldToComponent, {b.x - a.x, b.y - a.y, b.z - a.z});
        }
        if (!driven.empty()) {
            driven += " following the controllers";
            if (request.torso.enabled) {
                driven += ", shoulders under the headset";
            }
            if (gunAimed) {
                driven += supportRequested && support_grip::Held()
                    ? " (both hands on the gun: the barrel between them)"
                    : " (the right hand points the gun's barrel along the controller's aim)";
            }
        }
    }
    if (driven.empty()) {
        return Stop(std::format("no hand targets (source {})", hand_targets::Name(targets.source)));
    }
    engine::bone_pose::Publish(std::move(request));
    gStatus = "driving: " + driven;
    gGunAimed = gunAimed;
    gSupportRequested = supportRequested;
}

} // namespace

void Tick(float deltaSeconds) {
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
    Drive(deltaSeconds);
    gGunAimedAndWritten = gGunAimed && written;
    LogStatusChange();
    engine::bone_pose::LogFirstWrite();
}

bool RightHandAimsGun() {
    return gGunAimedAndWritten;
}

bool SupportRequested() {
    return gEnabled && gSupportRequested;
}

std::optional<double> TorsoYaw() {
    if (!gEnabled || !gBodyYawActive) {
        return std::nullopt;
    }
    return gBodyYaw.Value();
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

std::string const& Status() {
    return gStatus;
}

char const* HandSource() {
    return hand_targets::Name(gSource);
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
    gSupportRequested = false;
    gLastWrites = 0;
    gStatus = "off";
    gLastLoggedStatus.clear();
    gSource = hand_targets::Source::None;
    gSkeleton.reset();
    gLayout = {};
    gAnatomy = {};
    gAnatomyTried = {};
    gLoggedGrip.reset();
    gIkTree = nullptr;
    gIkControl = nullptr;
    gIkSearched = false;
    gBodyYawActive = false;
    gLoggedAnchor = hand_targets::Anchor::None;
    gLoggedScale = 0.0;
}

} // namespace mevr::game::arm_ik
