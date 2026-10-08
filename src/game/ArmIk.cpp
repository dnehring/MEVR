#include "game/ArmIk.hpp"

#include "core/Log.hpp"
#include "core/ik/Ik.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/Output.hpp"
#include "engine/Objects.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HeadTracking.hpp"

#include <LESDK/Headers.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <string>

namespace mevr::game::arm_ik {
namespace {

constexpr unsigned char kWorldSpace = 0;  // EBoneControlSpace::BCS_WorldSpace
enum Side { kLeft = 0, kRight = 1 };

// A limb control we drive, and its settings before we touched it.
struct Arm {
    USkelControlLimb* control = nullptr;
    std::string bone;
    FVector effector{};
    FVector jointTarget{};
    unsigned char effectorSpace = 0;
    unsigned char jointSpace = 0;
    float strength = 0.0f;
    float strengthTarget = 0.0f;
    bool saved = false;
};

USkeletalMeshComponent* gMesh = nullptr;  // the mesh the arms below belong to
UAnimNode* gTree = nullptr;  // and its AnimTree instance
std::array<Arm, 2> gArms;
bool gEnabled = false;
bool gLoggedMissing = false;
ik::BodyYaw gBodyYaw;
std::atomic<unsigned> gDrivenFrames{0};

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Which hand a bone name belongs to (-1: neither or unclear). Bone naming varies by rig, so this
// accepts the common conventions: Hand_R, R_Hand, RightHand, hand_l, Wrist_L...
int HandSide(std::string const& boneName) {
    std::string const name = Lower(boneName);
    if (name.find("hand") == std::string::npos && name.find("wrist") == std::string::npos) {
        return -1;
    }
    if (name.find("finger") != std::string::npos || name.find("thumb") != std::string::npos) {
        return -1;
    }
    bool const right = name.ends_with("_r") || name.starts_with("r_") || name.find("right") != std::string::npos;
    bool const left = name.ends_with("_l") || name.starts_with("l_") || name.find("left") != std::string::npos;
    if (right == left) {
        return -1;
    }
    return right ? kRight : kLeft;
}

void Restore(Arm& arm) {
    if (arm.saved && engine::IsAlive(arm.control)) {
        arm.control->EffectorLocation = arm.effector;
        arm.control->JointTargetLocation = arm.jointTarget;
        arm.control->EffectorLocationSpace = arm.effectorSpace;
        arm.control->JointTargetLocationSpace = arm.jointSpace;
        arm.control->ControlStrength = arm.strength;
        arm.control->StrengthTarget = arm.strengthTarget;
    }
    arm.saved = false;
}

void Save(Arm& arm) {
    if (arm.saved || arm.control == nullptr) {
        return;
    }
    arm.effector = arm.control->EffectorLocation;
    arm.jointTarget = arm.control->JointTargetLocation;
    arm.effectorSpace = arm.control->EffectorLocationSpace;
    arm.jointSpace = arm.control->JointTargetLocationSpace;
    arm.strength = arm.control->ControlStrength;
    arm.strengthTarget = arm.control->StrengthTarget;
    arm.saved = true;
}

// The first SkelControlLimb on each hand bone of this mesh's AnimTree.
void FindArms(USkeletalMeshComponent* mesh) {
    gArms = {};
    if (mesh->Animations == nullptr || !mesh->Animations->IsA(UAnimTree::StaticClass())) {
        return;
    }
    auto* const tree = static_cast<UAnimTree*>(mesh->Animations);
    for (FSkelControlListHead const& list : tree->SkelControlLists) {
        int const side = HandSide(engine::ToString(list.BoneName));
        if (side < 0 || gArms[side].control != nullptr) {
            continue;
        }
        int depth = 0;
        for (USkelControlBase* control = list.ControlHead; control != nullptr && depth < 32; control = control->NextControl, ++depth) {
            if (control->IsA(USkelControlLimb::StaticClass())) {
                gArms[side].control = static_cast<USkelControlLimb*>(control);
                gArms[side].bone = engine::ToString(list.BoneName);
                break;
            }
        }
    }
    for (int side : {kLeft, kRight}) {
        if (gArms[side].control != nullptr) {
            log::Info("arm IK: {} hand: {} on bone {}", side == kLeft ? "left" : "right",
                engine::ToString(gArms[side].control->ControlName), gArms[side].bone);
        }
    }
    if (gArms[kLeft].control == nullptr && gArms[kRight].control == nullptr && !gLoggedMissing) {
        log::Info("arm IK: no SkelControlLimb on the hand bones; route 1 unavailable (see the skeleton probe)");
        gLoggedMissing = true;
    }
}

USkeletalMeshComponent* PlayerMesh() {
    APlayerController* const controller = engine::LocalPlayerController();
    APawn* const pawn = controller != nullptr ? controller->Pawn : nullptr;
    if (pawn == nullptr || !pawn->IsA(ABioPawn::StaticClass())) {
        return nullptr;
    }
    return pawn->Mesh;
}

void Drive(float deltaSeconds) {
    auto const anchor = head_tracking::Anchor();
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!anchor || !controllers.active || !vr::output::WantsHeadTracking() || !head_tracking::OnFoot()) {
        for (Arm& arm : gArms) {
            Restore(arm);  // the game's own animation until we're tracking again
        }
        return;
    }
    XrPosef const& head = anchor->head;
    double const headYaw = ik::YawOf(head.orientation.x, head.orientation.y, head.orientation.z, head.orientation.w);
    ik::UpperBody const body = ik::EstimateUpperBody({head.position.x, head.position.y, head.position.z},
        gBodyYaw.Update(headYaw, deltaSeconds));

    for (int side : {kLeft, kRight}) {
        Arm& arm = gArms[side];
        vr::controllers::Hand const& hand = side == kLeft ? controllers.left : controllers.right;
        if (!engine::IsAlive(arm.control)) {
            continue;
        }
        if (!hand.gripValid) {
            Restore(arm);
            continue;
        }
        ik::Vec3 const pole = ik::ElbowPole(body, side == kLeft ? -1 : +1);
        engine::UnrealVector const effector = engine::ToWorld(hand.grip.position, head.position, anchor->headWorld, anchor->gameYaw);
        engine::UnrealVector const joint = engine::ToWorld(
            {static_cast<float>(pole.x), static_cast<float>(pole.y), static_cast<float>(pole.z)}, head.position,
            anchor->headWorld, anchor->gameYaw);
        Save(arm);
        arm.control->EffectorLocationSpace = kWorldSpace;
        arm.control->JointTargetLocationSpace = kWorldSpace;
        arm.control->EffectorLocation = {static_cast<float>(effector.x), static_cast<float>(effector.y), static_cast<float>(effector.z)};
        arm.control->JointTargetLocation = {static_cast<float>(joint.x), static_cast<float>(joint.y), static_cast<float>(joint.z)};
        arm.control->ControlStrength = 1.0f;
        arm.control->StrengthTarget = 1.0f;
        gDrivenFrames.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace

void Tick(float deltaSeconds) {
    USkeletalMeshComponent* const mesh = PlayerMesh();
    UAnimNode* const tree = mesh != nullptr ? mesh->Animations : nullptr;
    if (mesh != gMesh || tree != gTree || !engine::IsAlive(tree)) {
        // A new body or AnimTree (level load, spawn): the old controls may be gone, so they're
        // forgotten without being touched.
        if (mesh != gMesh || tree != gTree) {
            gArms = {};
            gMesh = mesh;
            gTree = tree;
            if (mesh != nullptr && mesh->SkeletalMesh != nullptr && engine::IsAlive(tree)) {
                FindArms(mesh);
            }
        }
    }
    if (!gEnabled || gMesh == nullptr) {
        return;
    }
    Drive(deltaSeconds);
}

bool Toggle() {
    gEnabled = !gEnabled;
    if (!gEnabled && gMesh != nullptr && gMesh == PlayerMesh()) {
        for (Arm& arm : gArms) {
            Restore(arm);
        }
    }
    return gEnabled;
}

void LogStats() {
    unsigned const frames = gDrivenFrames.exchange(0);
    if (gEnabled) {
        log::Info("    arm IK: on, {} arm-frames driven (left {}, right {})", frames,
            gArms[kLeft].control != nullptr ? gArms[kLeft].bone : "none", gArms[kRight].control != nullptr ? gArms[kRight].bone : "none");
    }
}

void Shutdown() {
    if (gMesh != nullptr && gMesh == PlayerMesh()) {
        for (Arm& arm : gArms) {
            Restore(arm);
        }
    }
    gArms = {};
    gMesh = nullptr;
    gTree = nullptr;
    gEnabled = false;
    gLoggedMissing = false;
    gBodyYaw = ik::BodyYaw();
}

} // namespace mevr::game::arm_ik
