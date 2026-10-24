// SPDX-License-Identifier: MIT
#include "game/SkeletonProbe.hpp"

#include "core/Log.hpp"
#include "core/ik/ArmPose.hpp"
#include "engine/Skeleton.hpp"
#include "game/HeadTracking.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"

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
ue3::Object* gLastMesh = nullptr;  // compared only, never dereferenced
unsigned gProbes = 0;

// Our reflection layer (research/34). LE1: LE2/LE3 have no layout yet, so the probe finds no player there.
ue3::ClassRef gAnimTree{"Engine.AnimTree"};
ue3::ClassRef gSkelControlLimb{"Engine.SkelControlLimb"};
ue3::ClassRef gPcHud{"SFXGame.BioSFHandler_PCHUD"};
ue3::FieldRef gAnimTreeTemplate{"Engine.SkeletalMeshComponent", "AnimTreeTemplate"};
ue3::FieldRef gAnimations{"Engine.SkeletalMeshComponent", "Animations"};
ue3::FieldRef gSkelControlLists{"Engine.AnimTree", "SkelControlLists"};
ue3::FieldRef gListBoneName{gSkelControlLists, "BoneName"};
ue3::FieldRef gControlHead{gSkelControlLists, "ControlHead"};
ue3::FieldRef gNextControl{"Engine.SkelControlBase", "NextControl"};
ue3::FieldRef gControlName{"Engine.SkelControlBase", "ControlName"};
ue3::FieldRef gControlStrength{"Engine.SkelControlBase", "ControlStrength"};
ue3::FieldRef gEffectorLocationSpace{"Engine.SkelControlLimb", "EffectorLocationSpace"};
ue3::FieldRef gEffectorSpaceBoneName{"Engine.SkelControlLimb", "EffectorSpaceBoneName"};
ue3::FieldRef gJointTargetLocationSpace{"Engine.SkelControlLimb", "JointTargetLocationSpace"};
ue3::FieldRef gJointTargetSpaceBoneName{"Engine.SkelControlLimb", "JointTargetSpaceBoneName"};
ue3::FieldRef gBoneAxis{"Engine.SkelControlLimb", "BoneAxis"};
ue3::FieldRef gJointAxis{"Engine.SkelControlLimb", "JointAxis"};
ue3::FieldRef gInvertJointAxis{"Engine.SkelControlLimb", "bInvertJointAxis"};
ue3::FieldRef gHandlerPanel{"Engine.BioSFHandler", "oPanel"};
ue3::FieldRef gPanelMovie{"Engine.BioSFPanel", "MovieRef"};
ue3::FieldRef gPanelAspect{"Engine.BioSFPanel", "fMovieAspectRatio"};

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

std::string FullName(ue3::Object const* object) {
    return ue3::runtime::FullName(object);
}

std::string NameOf(ue3::FieldRef& field, void const* base) {
    return ue3::runtime::NameText(field.Read<ue3::Name>(base).value_or(ue3::Name{}));
}

ue3::Object* ObjectOf(ue3::FieldRef& field, void const* base) {
    return field.Read<ue3::Object*>(base).value_or(nullptr);
}

int ByteOf(ue3::FieldRef& field, void const* base) {
    return static_cast<int>(field.Read<std::uint8_t>(base).value_or(0));
}

void LogControls(ue3::Object* tree) {
    auto const lists = gSkelControlLists.ElementsOf(tree);
    log::Info("skeleton: AnimTree {} has {} skeletal control lists", FullName(tree), lists ? lists->count : 0);
    for (std::int32_t i = 0; lists && i < lists->count; ++i) {
        void* const list = lists->At(i);
        int depth = 0;
        for (ue3::Object* control = ObjectOf(gControlHead, list); control != nullptr && depth < 32;
             control = ObjectOf(gNextControl, control), ++depth) {
            std::string detail;
            if (ue3::IsA(control, gSkelControlLimb)) {
                detail = std::format(" [limb: effector space {} bone {}, joint space {} bone {}, bone axis {}, joint axis {}, invert joint {}]",
                    ByteOf(gEffectorLocationSpace, control), NameOf(gEffectorSpaceBoneName, control),
                    ByteOf(gJointTargetLocationSpace, control), NameOf(gJointTargetSpaceBoneName, control), ByteOf(gBoneAxis, control),
                    ByteOf(gJointAxis, control), gInvertJointAxis.ReadBool(control).value_or(false) ? 1 : 0);
            }
            log::Info("skeleton:   bone {}: control {} ({}) strength {:.2f}{}", NameOf(gListBoneName, list), NameOf(gControlName, control),
                FullName(ue3::runtime::Get().ClassOf(control)), gControlStrength.Read<float>(control).value_or(0.0f), detail);
        }
    }
}

void Probe(ue3::Object* pawn, ue3::Object* mesh) {
    log::Info("skeleton: pawn {}", FullName(pawn));
    log::Info("skeleton: mesh {}, AnimTree template {}, animations {}, pose parent {}", FullName(player::SkeletalMesh(mesh)),
        FullName(ObjectOf(gAnimTreeTemplate, mesh)), FullName(ObjectOf(gAnimations, mesh)),
        FullName(engine::skeleton::PoseParent(mesh)));
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

    ue3::Object* const animations = ObjectOf(gAnimations, mesh);
    if (animations != nullptr && ue3::IsA(animations, gAnimTree)) {
        LogControls(animations);
    } else {
        log::Info("skeleton: no AnimTree instance");
    }
}

// HUD research (research/15): what the gameplay HUD's Scaleform panel holds. If MovieRef is a
// GFxMovie, its RenderTexture can take the HUD off the game image. LE1 class names.
void ProbeHud() {
#if defined(MEVR_GAME_LE1)
    ue3::Class* const hudClass = gPcHud.Get();
    ue3::Reflection& r = ue3::runtime::Get();
    int const count = ue3::runtime::Ready() ? r.ObjectCount() : 0;
    for (int i = 0; i < count && hudClass != nullptr; ++i) {
        ue3::Object* const handler = r.ObjectAt(i);
        if (handler == nullptr || r.ClassOf(handler) != hudClass || !ue3::runtime::IsAlive(handler)) {
            continue;
        }
        ue3::Object* const panel = ObjectOf(gHandlerPanel, handler);
        if (panel == nullptr || !ue3::runtime::IsAlive(panel)) {
            continue;
        }
        ue3::Object* const movie = ObjectOf(gPanelMovie, panel);
        log::Info("hud: {} panel {} movie {} (class {}), aspect {:.3f}", FullName(handler), FullName(panel), FullName(movie),
            movie != nullptr ? FullName(r.ClassOf(movie)) : "None", gPanelAspect.Read<float>(panel).value_or(0.0f));
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
    // Shepard's own body, on foot: not the Mako (a vehicle pawn) or a menu's stand-in.
    ue3::Object* const pawn = player::BioPawn(player::Controller());
    if (pawn == nullptr || !head_tracking::OnFoot()) {
        return;
    }
    ue3::Object* const mesh = player::Mesh(pawn);
    if (mesh == nullptr || mesh == gLastMesh || player::SkeletalMesh(mesh) == nullptr) {
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
