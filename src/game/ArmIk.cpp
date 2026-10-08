#include "game/ArmIk.hpp"

#include "core/Log.hpp"
#include "core/ik/BoneNames.hpp"
#include "core/ik/Ik.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/Output.hpp"
#include "core/vr/ViewSync.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/Objects.hpp"
#include "engine/Skeleton.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HeadTracking.hpp"

#include <LESDK/Headers.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>

namespace mevr::game::arm_ik {
namespace {

constexpr unsigned char kWorldSpace = 0;  // EBoneControlSpace::BCS_WorldSpace
constexpr double kMaxHeadDistance = 300.0;  // head bone to pawn origin, Unreal units (3 m)
enum Side { kLeft = 0, kRight = 1 };

// A hand's limb control as found this tick. Nothing here is kept across ticks: a level load
// frees objects and can hand their addresses to unrelated ones, so every tick starts from the
// live player pawn and its AnimTree.
struct Found {
    USkelControlLimb* control = nullptr;
    SFXName bone{};
};

// The settings a control had before we drove it, matched to it by pointer *and* bone name (and
// only restored to a control found again this tick).
struct Saved {
    bool valid = false;
    USkelControlLimb const* control = nullptr;
    std::uint64_t bone = 0;
    FVector effector{};
    FVector jointTarget{};
    unsigned char effectorSpace = 0;
    unsigned char jointSpace = 0;
    float strength = 0.0f;
    float strengthTarget = 0.0f;
};

bool gEnabled = false;
std::array<Saved, 2> gSaved;
ik::BodyYaw gBodyYaw;
// Bone-name analysis per skeleton asset (names are values, safe to keep).
UObject const* gSkeletonAsset = nullptr;
std::optional<SFXName> gHeadBone;
std::unordered_map<std::uint64_t, int> gHandSides;  // bone name -> side (-1: not a hand)
std::string gStatus = "off";
std::atomic<unsigned> gDrivenFrames{0};

std::uint64_t Key(SFXName const& name) {
    std::uint64_t key = 0;
    std::memcpy(&key, &name, sizeof key);
    return key;
}

int SideOf(SFXName const& bone) {
    auto const [it, inserted] = gHandSides.try_emplace(Key(bone), -1);
    if (inserted) {
        it->second = ik::HandSide(engine::ToString(bone));
    }
    return it->second;
}

// The player's own body on foot: its pawn and skeletal mesh, read fresh.
struct Body {
    APawn* pawn = nullptr;
    USkeletalMeshComponent* mesh = nullptr;
    UAnimTree* tree = nullptr;
};

std::optional<Body> PlayerBody() {
    APlayerController* const controller = engine::LocalPlayerController();
    APawn* const pawn = controller != nullptr ? controller->Pawn : nullptr;
    if (pawn == nullptr || !pawn->IsA(ABioPawn::StaticClass()) || pawn->Mesh == nullptr) {
        return std::nullopt;
    }
    USkeletalMeshComponent* const mesh = pawn->Mesh;
    if (mesh->SkeletalMesh == nullptr || mesh->Animations == nullptr || !mesh->Animations->IsA(UAnimTree::StaticClass())) {
        return std::nullopt;
    }
    return Body{pawn, mesh, static_cast<UAnimTree*>(mesh->Animations)};
}

std::array<Found, 2> FindArms(UAnimTree* tree) {
    std::array<Found, 2> found;
    for (FSkelControlListHead const& list : tree->SkelControlLists) {
        int const side = SideOf(list.BoneName);
        if (side < 0 || found[side].control != nullptr) {
            continue;
        }
        int depth = 0;
        for (USkelControlBase* control = list.ControlHead; control != nullptr && depth < 32; control = control->NextControl, ++depth) {
            if (control->IsA(USkelControlLimb::StaticClass())) {
                found[side] = {static_cast<USkelControlLimb*>(control), list.BoneName};
                break;
            }
        }
    }
    return found;
}

bool Matches(Saved const& saved, Found const& found) {
    return saved.valid && found.control != nullptr && saved.control == found.control && saved.bone == Key(found.bone);
}

void Save(Saved& saved, Found const& found) {
    USkelControlLimb const* const c = found.control;
    saved = {true, c, Key(found.bone), c->EffectorLocation, c->JointTargetLocation, c->EffectorLocationSpace,
        c->JointTargetLocationSpace, c->ControlStrength, c->StrengthTarget};
}

// Puts the saved settings back if this tick found the same control; otherwise just forgets them.
void Restore(Saved& saved, Found const& found) {
    if (Matches(saved, found)) {
        USkelControlLimb* const c = found.control;
        c->EffectorLocation = saved.effector;
        c->JointTargetLocation = saved.jointTarget;
        c->EffectorLocationSpace = saved.effectorSpace;
        c->JointTargetLocationSpace = saved.jointSpace;
        c->ControlStrength = saved.strength;
        c->StrengthTarget = saved.strengthTarget;
    }
    saved.valid = false;
}

void RestoreAll(std::array<Found, 2> const& found) {
    for (int side : {kLeft, kRight}) {
        Restore(gSaved[side], found[side]);
    }
}

// The body's head bone, re-resolved whenever the skeleton asset changes.
std::optional<SFXName> HeadBone(USkeletalMeshComponent* mesh) {
    UObject const* const asset = mesh->SkeletalMesh;
    if (asset != gSkeletonAsset) {
        gSkeletonAsset = asset;
        gHeadBone.reset();
        gHandSides.clear();
        for (SFXName const& bone : engine::skeleton::BoneNames(mesh)) {
            if (ik::IsHeadBone(engine::ToString(bone))) {
                gHeadBone = bone;
                break;
            }
        }
        log::Info("arm IK: head bone {}", gHeadBone ? engine::ToString(*gHeadBone) : "not found");
    }
    return gHeadBone;
}

void Drive(Body const& body, std::array<Found, 2> const& found, float deltaSeconds) {
    vr::controllers::State const controllers = vr::controllers::Latest();
    auto const head = vr::view_sync::LatestHead();
    auto const headBone = HeadBone(body.mesh);
    auto headWorld = headBone ? engine::skeleton::BoneLocation(body.mesh, *headBone, engine::skeleton::Space::World)
                              : std::nullopt;
    if (headWorld) {
        // The head must be on the body: anything else is a stale bone or an unposed mesh.
        FVector const& at = engine::adapter::Location(body.pawn);
        double const dx = headWorld->x - at.X, dy = headWorld->y - at.Y, dz = headWorld->z - at.Z;
        if (dx * dx + dy * dy + dz * dz > kMaxHeadDistance * kMaxHeadDistance) {
            headWorld.reset();
        }
    }
    bool const ready = controllers.active && head && headWorld && vr::output::WantsHeadTracking() && head_tracking::OnFoot();
    gStatus = !headBone ? "no head bone" : !headWorld ? "head bone not posed" : !ready ? "waiting for tracking" : "driving";
    if (!ready) {
        RestoreAll(found);  // the game's own animation until we're tracking again
        return;
    }
    // Hands relative to Shepard's head, in the frame of the body's facing (LOCAL forward).
    int const bodyYaw = body.pawn->Rotation.Yaw;
    double const headYaw = ik::YawOf(head->orientation.x, head->orientation.y, head->orientation.z, head->orientation.w);
    ik::UpperBody const torso = ik::EstimateUpperBody({head->position.x, head->position.y, head->position.z},
        gBodyYaw.Update(headYaw, deltaSeconds));

    for (int side : {kLeft, kRight}) {
        Found const& arm = found[side];
        vr::controllers::Hand const& hand = side == kLeft ? controllers.left : controllers.right;
        if (arm.control == nullptr) {
            gSaved[side].valid = false;
            continue;
        }
        if (!hand.gripValid) {
            Restore(gSaved[side], arm);
            continue;
        }
        if (!Matches(gSaved[side], arm)) {
            Save(gSaved[side], arm);  // fresh snapshot each time we start driving this control
        }
        ik::Vec3 const pole = ik::ElbowPole(torso, side == kLeft ? -1 : +1);
        engine::UnrealVector const effector = engine::ToWorld(hand.grip.position, head->position, *headWorld, bodyYaw);
        engine::UnrealVector const joint = engine::ToWorld(
            {static_cast<float>(pole.x), static_cast<float>(pole.y), static_cast<float>(pole.z)}, head->position, *headWorld, bodyYaw);
        USkelControlLimb* const c = arm.control;
        c->EffectorLocationSpace = kWorldSpace;
        c->JointTargetLocationSpace = kWorldSpace;
        c->EffectorLocation = {static_cast<float>(effector.x), static_cast<float>(effector.y), static_cast<float>(effector.z)};
        c->JointTargetLocation = {static_cast<float>(joint.x), static_cast<float>(joint.y), static_cast<float>(joint.z)};
        c->ControlStrength = 1.0f;
        c->StrengthTarget = 1.0f;
        gDrivenFrames.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace

void Tick(float deltaSeconds) {
    if (!gEnabled && !gSaved[kLeft].valid && !gSaved[kRight].valid) {
        return;  // off and nothing to put back
    }
    auto const body = PlayerBody();
    if (!body) {
        // In the Mako, mid-load...: nothing to drive or restore now. The snapshots stay, so the
        // controls get their own settings back once a walk of the live body finds them again.
        gStatus = gEnabled ? "no player body" : "off";
        return;
    }
    std::array<Found, 2> const found = FindArms(body->tree);
    if (!gEnabled) {
        RestoreAll(found);
        gStatus = "off";
        return;
    }
    if (found[kLeft].control == nullptr && found[kRight].control == nullptr) {
        // Keep the snapshots: the body may have swapped AnimTrees (e.g. a cinematic one) and will
        // swap back; a walk that finds a different control re-saves anyway.
        gStatus = "no SkelControlLimb on the hand bones (route 1 unavailable)";
        return;
    }
    Drive(*body, found, deltaSeconds);
}

bool Toggle() {
    gEnabled = !gEnabled;  // restoring happens in the next Tick, from a fresh walk
    return gEnabled;
}

void SetEnabled(bool on) {
    gEnabled = on;
}

void LogStats() {
    unsigned const frames = gDrivenFrames.exchange(0);
    if (gEnabled) {
        log::Info("    arm IK: {}, {} arm-frames driven", gStatus, frames);
    }
}

void Shutdown() {
    if (gSaved[kLeft].valid || gSaved[kRight].valid) {
        if (auto const body = PlayerBody()) {
            RestoreAll(FindArms(body->tree));
        }
    }
    gSaved = {};
    gEnabled = false;
    gBodyYaw = ik::BodyYaw();
    gSkeletonAsset = nullptr;
    gHeadBone.reset();
    gHandSides.clear();
    gStatus = "off";
}

} // namespace mevr::game::arm_ik
