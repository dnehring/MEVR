#include "game/SkeletonProbe.hpp"

#include "core/Log.hpp"
#include "engine/Objects.hpp"
#include "game/HeadTracking.hpp"

#include <LESDK/Headers.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace mevr::game::skeleton_probe {
namespace {

constexpr char const* kGetBoneNames = "Function Engine.SkeletalMeshComponent.GetBoneNames";
constexpr char const* kGetBoneLocation = "Function Engine.SkeletalMeshComponent.GetBoneLocation";
constexpr int kComponentSpace = 1;  // GetBoneLocation's Space: 0 world, 1 component
// Bones worth a position (lengths and proportions for the IK): matched case-insensitively.
constexpr char const* kKeyBoneParts[] = {"root", "pelvis", "spine", "neck", "head", "clav", "shoulder", "arm",
    "elbow", "wrist", "hand", "finger", "thumb", "weapon", "gun", "prop"};
constexpr std::size_t kNamesPerLine = 12;
constexpr unsigned kMaxProbes = 3;  // per session: each level load brings a new mesh

bool gHudProbed = false;
unsigned gHudAttempts = 0;
constexpr unsigned kMaxHudAttempts = 6;  // full object scans: don't keep paying for a class that isn't there
UFunction* gGetBoneNames = nullptr;
UFunction* gGetBoneLocation = nullptr;
USkeletalMeshComponent* gLastMesh = nullptr;
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

// Called through ProcessEvent directly: the SDK's generated wrappers corrupt FunctionFlags.
std::vector<SFXName> BoneNames(USkeletalMeshComponent* mesh) {
    USkeletalMeshComponent_execGetBoneNames_Parms parms;
    mesh->ProcessEvent(gGetBoneNames, &parms, nullptr);
    std::vector<SFXName> names(parms.BoneNames.GetData(), parms.BoneNames.GetData() + parms.BoneNames.Count());
    parms.BoneNames.~TArray();  // the parms union doesn't free it
    return names;
}

FVector BoneLocation(USkeletalMeshComponent* mesh, SFXName const& bone) {
    USkeletalMeshComponent_execGetBoneLocation_Parms parms;
    parms.BoneName = bone;
    parms.Space = kComponentSpace;
    mesh->ProcessEvent(gGetBoneLocation, &parms, nullptr);
    return parms.ReturnValue;
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
    log::Info("skeleton: mesh {}, AnimTree template {}, animations {}", engine::FullName(mesh->SkeletalMesh),
        engine::FullName(mesh->AnimTreeTemplate), engine::FullName(mesh->Animations));

    std::vector<SFXName> const names = BoneNames(mesh);
    log::Info("skeleton: {} bones", names.size());
    std::string line;
    for (std::size_t i = 0; i < names.size(); ++i) {
        line += engine::ToString(names[i]);
        line += (i + 1) % kNamesPerLine == 0 || i + 1 == names.size() ? "" : ", ";
        if ((i + 1) % kNamesPerLine == 0 || i + 1 == names.size()) {
            log::Info("skeleton:   {}", line);
            line.clear();
        }
    }
    // GetBoneLocation in component space indexes SpaceBases unchecked: only once the pose exists.
    bool const posed = mesh->SpaceBases.Count() >= names.size();
    if (!posed) {
        log::Info("skeleton: pose not computed yet ({} of {} bones); positions skipped", mesh->SpaceBases.Count(), names.size());
    }
    for (SFXName const& bone : names) {
        std::string const name = engine::ToString(bone);
        if (posed && IsKeyBone(name)) {
            FVector const at = BoneLocation(mesh, bone);
            log::Info("skeleton:   {} at ({:.1f}, {:.1f}, {:.1f}) component space", name, at.X, at.Y, at.Z);
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
    gGetBoneNames = engine::FindFunction(L"GetBoneNames", kGetBoneNames);
    gGetBoneLocation = engine::FindFunction(L"GetBoneLocation", kGetBoneLocation);
    return gGetBoneNames != nullptr && gGetBoneLocation != nullptr;
}

void Tick() {
    if (gGetBoneNames == nullptr || gGetBoneLocation == nullptr) {
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
    gGetBoneNames = nullptr;
    gGetBoneLocation = nullptr;
    gLastMesh = nullptr;
    gProbes = 0;
    gHudProbed = false;
    gHudAttempts = 0;
}

} // namespace mevr::game::skeleton_probe
