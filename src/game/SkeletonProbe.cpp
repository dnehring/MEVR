#include "game/SkeletonProbe.hpp"

#include "core/Log.hpp"
#include "core/ik/ArmPose.hpp"
#include "engine/Objects.hpp"
#include "engine/Skeleton.hpp"
#include "game/HeadTracking.hpp"

#include <LESDK/Headers.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <vector>

namespace mevr::game::skeleton_probe {
namespace {

// Bones worth a position (lengths and proportions for the IK): matched case-insensitively.
constexpr char const* kKeyBoneParts[] = {"root", "pelvis", "spine", "neck", "head", "clav", "shoulder", "arm",
    "elbow", "wrist", "hand", "finger", "thumb", "weapon", "gun", "prop"};
constexpr std::size_t kNamesPerLine = 10;
constexpr unsigned kMaxProbes = 3;  // per session: each level load brings a new mesh

bool gHudProbed = false;
unsigned gHudAttempts = 0;
constexpr unsigned kMaxHudAttempts = 6;  // full object scans: don't keep paying for a class that isn't there
bool gReady = false;
USkeletalMeshComponent* gLastMesh = nullptr;  // compared only, never dereferenced
unsigned gProbes = 0;

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool IsKeyBone(std::string const& name) {
    std::string const lower = Lower(name);
    for (char const* part : kKeyBoneParts) {
        if (lower.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void LogControls(UAnimTree* tree) {
    log::Info("skeleton: AnimTree {} has {} skeletal control lists", engine::FullName(tree), tree->SkelControlLists.Count());
    for (FSkelControlListHead const& list : tree->SkelControlLists) {
        int depth = 0;
        for (USkelControlBase* control = list.ControlHead; control != nullptr && depth < 32; control = control->NextControl, ++depth) {
            std::string detail;
            if (control->IsA(USkelControlLimb::StaticClass())) {
                auto* const limb = static_cast<USkelControlLimb*>(control);
                detail = std::format(" [limb: effector space {} bone {}, joint space {} bone {}, bone axis {}, joint axis {}, invert joint {}]",
                    static_cast<int>(limb->EffectorLocationSpace), engine::ToString(limb->EffectorSpaceBoneName),
                    static_cast<int>(limb->JointTargetLocationSpace), engine::ToString(limb->JointTargetSpaceBoneName),
                    static_cast<int>(limb->BoneAxis), static_cast<int>(limb->JointAxis), limb->bInvertJointAxis ? 1 : 0);
            }
            log::Info("skeleton:   bone {}: control {} ({}) strength {:.2f}{}", engine::ToString(list.BoneName),
                engine::ToString(control->ControlName), engine::FullName(control->Class), control->ControlStrength, detail);
        }
    }
}

void Probe(APawn* pawn, USkeletalMeshComponent* mesh) {
    log::Info("skeleton: pawn {}", engine::FullName(pawn));
    log::Info("skeleton: mesh {}, AnimTree template {}, animations {}, pose parent {}", engine::FullName(mesh->SkeletalMesh),
        engine::FullName(mesh->AnimTreeTemplate), engine::FullName(mesh->Animations),
        engine::FullName(engine::skeleton::PoseParent(mesh)));
    // Names straight from the asset's RefSkeleton (the GetBoneNames native can't return them
    // through ProcessEvent; the diagnostic line shows what it does).
    log::Info("skeleton: {}", engine::skeleton::GetBoneNamesDiagnostic(mesh));
    auto const skeleton = engine::skeleton::ReadRefSkeleton(mesh);
    if (!skeleton) {
        log::Info("skeleton: RefSkeleton unreadable{}", engine::skeleton::Supported() ? "" : " (layout unknown for this game)");
        return;
    }
    std::vector<std::string> const& names = skeleton->names;
    log::Info("skeleton: {} bones (name:parent)", names.size());
    std::string line;
    for (std::size_t i = 0; i < names.size(); ++i) {
        line += std::format("{}:{}", names[i], skeleton->parents[i]);
        bool const last = (i + 1) % kNamesPerLine == 0 || i + 1 == names.size();
        line += last ? "" : ", ";
        if (last) {
            log::Info("skeleton:   [{}] {}", i + 1 - ((i % kNamesPerLine) + 1), line);
            line.clear();
        }
    }
    for (int side : {0, 1}) {
        auto const chain = ik::FindArmChain(names, skeleton->parents, side);
        log::Info("skeleton: {} arm {}", side == 0 ? "left" : "right",
            chain ? std::format("{}({}) > {}({}) > {}({})", names[chain->upper], chain->upper, names[chain->fore], chain->fore,
                        names[chain->hand], chain->hand)
                  : std::string("not found"));
    }
    // Positions from SpaceBases once the pose exists.
    std::size_t const posed = engine::skeleton::SpaceBases(mesh).size();
    if (posed != names.size()) {
        log::Info("skeleton: pose not computed yet ({} of {} bones); positions skipped", posed, names.size());
    }
    for (std::size_t i = 0; i < names.size() && posed == names.size(); ++i) {
        if (IsKeyBone(names[i])) {
            auto const at = engine::skeleton::BoneLocation(mesh, static_cast<int>(i), engine::skeleton::Space::Component);
            if (at) {
                log::Info("skeleton:   {}({}) at ({:.1f}, {:.1f}, {:.1f}) component space", names[i], i, at->x, at->y, at->z);
            }
        }
    }

    if (mesh->Animations != nullptr && mesh->Animations->IsA(UAnimTree::StaticClass())) {
        LogControls(static_cast<UAnimTree*>(mesh->Animations));
    } else {
        log::Info("skeleton: no AnimTree instance");
    }
}

// HUD research (research/15): what the gameplay HUD's Scaleform panel holds. If MovieRef is a
// GFxMovie, its RenderTexture can take the HUD off the game image. LE1 class names.
void ProbeHud() {
#if defined(SDK_TARGET_LE1)
    UClass* const hudClass = UBioSFHandler_PCHUD::StaticClass();
    auto* const objects = UObject::GObjObjects;
    for (int i = 0; i < static_cast<int>(objects->Count()) && hudClass != nullptr; ++i) {
        UObject* const object = objects->GetData()[i];
        if (object == nullptr || object->Class != hudClass || !engine::IsAlive(object)) {
            continue;
        }
        auto* const handler = static_cast<UBioSFHandler_PCHUD*>(object);
        UBioSFPanel* const panel = handler->oPanel;
        if (panel == nullptr || !engine::IsAlive(panel)) {
            continue;
        }
        UObject* const movie = panel->MovieRef;
        log::Info("hud: {} panel {} movie {} (class {}), aspect {:.3f}", engine::FullName(handler), engine::FullName(panel),
            engine::FullName(movie), movie != nullptr ? engine::FullName(movie->Class) : "None", panel->fMovieAspectRatio);
        gHudProbed = true;
        return;
    }
#else
    gHudProbed = true;
#endif
}

} // namespace

bool TryStartup() {
    gReady = engine::skeleton::TryStartup();
    return gReady;
}

void Tick() {
    if (!gReady) {
        return;
    }
    if (!gHudProbed && gHudAttempts < kMaxHudAttempts && head_tracking::OnFoot()) {
        ++gHudAttempts;
        ProbeHud();  // once the gameplay HUD exists
    }
    if (gProbes >= kMaxProbes) {
        return;
    }
    APlayerController* const controller = engine::LocalPlayerController();
    APawn* const pawn = controller != nullptr ? controller->Pawn : nullptr;
    // Shepard's own body, on foot: not the Mako (a vehicle pawn) or a menu's stand-in.
    if (pawn == nullptr || !pawn->IsA(ABioPawn::StaticClass()) || !head_tracking::OnFoot()) {
        return;
    }
    USkeletalMeshComponent* const mesh = pawn->Mesh;
    if (mesh == nullptr || mesh == gLastMesh || mesh->SkeletalMesh == nullptr) {
        return;
    }
    gLastMesh = mesh;
    ++gProbes;
    Probe(pawn, mesh);
}

void Shutdown() {
    gReady = false;
    engine::skeleton::Shutdown();
    gLastMesh = nullptr;
    gProbes = 0;
    gHudProbed = false;
    gHudAttempts = 0;
}

} // namespace mevr::game::skeleton_probe
