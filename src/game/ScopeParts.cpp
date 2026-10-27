// SPDX-License-Identifier: MIT
#include "game/ScopeParts.hpp"

#include "core/Log.hpp"
#include "core/vr/Sights.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/Skeleton.hpp"
#include "game/Player.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <set>
#include <string>
#include <vector>

namespace mevr::game::scope_parts {
namespace {

namespace sv = vr::sights;

#if defined(MEVR_GAME_LE1)
// Our reflection layer (research/34); rows in engine/ReflectCheck.
ue3::FunctionRef gHideBoneRef{"Engine.SkeletalMeshComponent.HideBone"};
ue3::FunctionRef gUnHideBoneRef{"Engine.SkeletalMeshComponent.UnHideBone"};
ue3::FunctionRef gIsBoneHiddenRef{"Engine.SkeletalMeshComponent.IsBoneHidden"};
// The material log.
ue3::FieldRef gComponentMaterials{"Engine.MeshComponent", "Materials"};
ue3::FieldRef gMeshMaterials{"Engine.SkeletalMesh", "Materials"};
ue3::ClassRef gMaterialInstance{"Engine.MaterialInstance"};
ue3::ClassRef gMaterialInstanceConstant{"Engine.MaterialInstanceConstant"};
ue3::FieldRef gParent{"Engine.MaterialInstance", "Parent"};
ue3::FieldRef gScalars{"Engine.MaterialInstanceConstant", "ScalarParameterValues"};
ue3::FieldRef gScalarName{gScalars, "ParameterName"};
ue3::FieldRef gScalarValue{gScalars, "ParameterValue"};
ue3::FieldRef gVectors{"Engine.MaterialInstanceConstant", "VectorParameterValues"};
ue3::FieldRef gVectorName{gVectors, "ParameterName"};
ue3::FieldRef gVectorValue{gVectors, "ParameterValue"};
ue3::FieldRef gTextures{"Engine.MaterialInstanceConstant", "TextureParameterValues"};
ue3::FieldRef gTextureName{gTextures, "ParameterName"};
ue3::FieldRef gTextureValue{gTextures, "ParameterValue"};
#endif

struct LinearColor {
    float r, g, b, a;
};

constexpr ULONGLONG kUnhideAfterMs = 150;  // the picture gone this long: the parts come back (no flicker at its edge)

ue3::Function* gHideBone = nullptr;
ue3::Function* gUnHideBone = nullptr;
ue3::Function* gIsBoneHidden = nullptr;
bool gLookedUp = false;
bool gAvailable = false;

ue3::Object* gMesh = nullptr;          // the component the bones below were hidden on
std::vector<int> gHidden;              // bones this module hid (not ones the game had hidden)
std::vector<std::string> gHiddenNames;
std::string gWantedKey;                // the list and sight bone the hidden set was chosen for
ULONGLONG gDownSinceMs = 0;            // the picture went at this time (0: up, or nothing hidden)
std::set<std::uintptr_t> gLoggedChoice;     // mesh assets whose choice was logged
std::set<std::uintptr_t> gLoggedMaterials;  // mesh assets whose materials were logged
unsigned gHides = 0;
unsigned gRestores = 0;
std::string gLastWhat = "nothing hidden";

bool LookUp() {
#if defined(MEVR_GAME_LE1)
    if (gLookedUp) {
        return gAvailable;
    }
    if (!ue3::runtime::Ready()) {
        return false;
    }
    gLookedUp = true;
    gHideBone = gHideBoneRef.Get();
    gUnHideBone = gUnHideBoneRef.Get();
    gIsBoneHidden = gIsBoneHiddenRef.Get();
    gAvailable = gHideBone != nullptr && gUnHideBone != nullptr;
    if (!gAvailable) {
        log::Warn("sights: SkeletalMeshComponent.HideBone/UnHideBone not found: the scope's lens part stays visible");
    }
    return gAvailable;
#else
    return false;
#endif
}

#if defined(MEVR_GAME_LE1)
bool IsHidden(ue3::Object* mesh, int bone) {
    if (gIsBoneHidden == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), gIsBoneHidden);
    parms.Set("BoneIndex", static_cast<std::int32_t>(bone));
    ue3::runtime::Call(mesh, gIsBoneHidden, parms);
    return parms.ReturnBool().value_or(false);
}

void Hide(ue3::Object* mesh, int bone) {
    ue3::Params parms(ue3::runtime::Get(), gHideBone);
    parms.Set("BoneIndex", static_cast<std::int32_t>(bone));
    parms.Set("PhysBodyOption", std::uint8_t{0});  // PBO_None: the physics bodies are left as they are
    ue3::runtime::Call(mesh, gHideBone, parms);
}

void Unhide(ue3::Object* mesh, int bone) {
    ue3::Params parms(ue3::runtime::Get(), gUnHideBone);
    parms.Set("BoneIndex", static_cast<std::int32_t>(bone));
    ue3::runtime::Call(mesh, gUnHideBone, parms);
}

std::string ObjectText(ue3::Object const* object) {
    return object != nullptr ? ue3::runtime::FullName(object) : std::string("none");
}

// One material: its name, and for a material instance its parent and parameters (a parent instance's too, 3 deep).
std::string MaterialText(ue3::Object* material) {
    std::string out = ObjectText(material);
    ue3::Object* at = material;
    for (int depth = 0; depth < 3 && at != nullptr && ue3::IsA(at, gMaterialInstance); ++depth) {
        if (ue3::IsA(at, gMaterialInstanceConstant)) {
            std::string scalars;
            if (auto const list = gScalars.ElementsOf(at, 256)) {
                for (std::int32_t i = 0; i < list->count; ++i) {
                    void* const e = list->At(i);
                    scalars += std::format("{}{} = {:.3f}", scalars.empty() ? "" : ", ",
                        ue3::runtime::NameText(gScalarName.Read<ue3::Name>(e).value_or(ue3::Name{})), gScalarValue.Read<float>(e).value_or(0.0f));
                }
            }
            std::string vectors;
            if (auto const list = gVectors.ElementsOf(at, 256)) {
                for (std::int32_t i = 0; i < list->count; ++i) {
                    void* const e = list->At(i);
                    LinearColor const c = gVectorValue.Read<LinearColor>(e).value_or(LinearColor{});
                    vectors += std::format("{}{} = ({:.3f}, {:.3f}, {:.3f}, {:.3f})", vectors.empty() ? "" : ", ",
                        ue3::runtime::NameText(gVectorName.Read<ue3::Name>(e).value_or(ue3::Name{})), c.r, c.g, c.b, c.a);
                }
            }
            std::string textures;
            if (auto const list = gTextures.ElementsOf(at, 256)) {
                for (std::int32_t i = 0; i < list->count; ++i) {
                    void* const e = list->At(i);
                    textures += std::format("{}{} = {}", textures.empty() ? "" : ", ",
                        ue3::runtime::NameText(gTextureName.Read<ue3::Name>(e).value_or(ue3::Name{})),
                        ObjectText(gTextureValue.Read<ue3::Object*>(e).value_or(nullptr)));
                }
            }
            out += std::format("{}: scalars [{}], vectors [{}], textures [{}]", depth == 0 ? "" : " (its parent)", scalars, vectors, textures);
        }
        ue3::Object* const parent = gParent.Read<ue3::Object*>(at).value_or(nullptr);
        if (parent == nullptr || parent == at) {
            break;
        }
        out += std::format("; parent {}", ObjectText(parent));
        at = parent;
    }
    return out;
}

// Once per weapon model: the component's material overrides and the mesh asset's materials, one line each.
void LogMaterials(ue3::Object* mesh, char const* weapon) {
    ue3::Object* const asset = player::SkeletalMesh(mesh);
    auto const key = reinterpret_cast<std::uintptr_t>(asset);
    if (asset == nullptr || gLoggedMaterials.contains(key)) {
        return;
    }
    gLoggedMaterials.insert(key);
    auto const list = [&](ue3::FieldRef& field, ue3::Object* owner, char const* what) {
        auto const elements = field.ElementsOf(owner, 64);
        if (!elements) {
            log::Info("sights: the {} model's {}: none (or the field wasn't found)", weapon, what);
            return;
        }
        for (std::int32_t i = 0; i < elements->count; ++i) {
            auto* const slot = static_cast<ue3::Object**>(elements->At(i));
            ue3::Object* const material = slot != nullptr ? *slot : nullptr;
            log::Info("sights: the {} model's {} [{}]: {}", weapon, what, i, material != nullptr ? MaterialText(material) : std::string("none"));
        }
    };
    log::Info("sights: the {} model is {} (its materials follow, to find what glows; research/21 section 14)", weapon, ObjectText(asset));
    list(gComponentMaterials, mesh, "component's material overrides");
    list(gMeshMaterials, asset, "mesh materials");
}
#endif

void RestoreNow(char const* why) {
#if defined(MEVR_GAME_LE1)
    if (gMesh != nullptr && !gHidden.empty()) {
        bool const alive = ue3::runtime::Ready() && ue3::runtime::IsAlive(gMesh) && gUnHideBone != nullptr;
        if (alive) {
            for (int bone : gHidden) {
                Unhide(gMesh, bone);
            }
            ++gRestores;
        }
        if (gRestores <= 3 || !alive) {
            std::string names;
            for (std::string const& n : gHiddenNames) {
                names += std::format("{}'{}'", names.empty() ? "" : ", ", n);
            }
            log::Info("sights: {} {} again ({})", alive ? "showed" : "couldn't show (the gun's mesh is gone)", names, why);
        }
    }
#else
    (void)why;
#endif
    gMesh = nullptr;
    gHidden.clear();
    gHiddenNames.clear();
    gWantedKey.clear();
    gDownSinceMs = 0;
    gLastWhat = "nothing hidden";
}

} // namespace

void Update(bool up, std::vector<std::string> const& wanted, std::optional<std::size_t> sightBone, int weaponType) {
#if defined(MEVR_GAME_LE1)
    if (!ue3::runtime::Ready()) {
        return;
    }
    char const* const weapon = sv::WeaponName(weaponType);
    ue3::Object* const mesh = weapon::HeldMesh();
    if (mesh != nullptr) {
        LogMaterials(mesh, weapon);
    }
    if (!LookUp()) {
        return;
    }
    if (mesh != gMesh) {
        if (!gHidden.empty()) {
            RestoreNow(mesh == nullptr ? "the gun was put away" : "another gun");
        }
        gMesh = nullptr;
        gWantedKey.clear();
    }
    std::string key;
    for (std::string const& w : wanted) {
        key += w + ",";
    }
    key += sightBone ? std::to_string(*sightBone) : std::string("-");
    if (!gWantedKey.empty() && key != gWantedKey) {
        if (!gHidden.empty()) {
            RestoreNow("the [Sights] hide list or the sight's bone changed");
        }
        gWantedKey.clear();
    }
    ULONGLONG const now = GetTickCount64();
    if (!up) {
        if (!gHidden.empty()) {
            if (gDownSinceMs == 0) {
                gDownSinceMs = now;
            } else if (now - gDownSinceMs >= kUnhideAfterMs) {
                RestoreNow("the scope's picture went");
            }
        }
        return;
    }
    gDownSinceMs = 0;
    if (!gHidden.empty() || mesh == nullptr || wanted.empty() || gWantedKey == key) {
        return;  // already hidden, or nothing to hide (gWantedKey set and nothing hidden: the choice found nothing)
    }
    auto const bones = engine::skeleton::ReadRefSkeleton(mesh);
    if (!bones) {
        return;
    }
    sv::HideChoice const choice = sv::PartsToHide(bones->names, bones->parents, sightBone, wanted);
    gWantedKey = key;
    gMesh = mesh;
    std::vector<std::string> already;
    for (std::size_t index : choice.bones) {
        int const bone = static_cast<int>(index);
        if (IsHidden(mesh, bone)) {
            already.push_back(bones->names[index]);  // the game's own: not ours to show again
            continue;
        }
        Hide(mesh, bone);
        gHidden.push_back(bone);
        gHiddenNames.push_back(bones->names[index]);
    }
    ++gHides;
    gLastWhat.clear();
    for (std::string const& n : gHiddenNames) {
        gLastWhat += std::format("{}{}", gLastWhat.empty() ? "" : ", ", n);
    }
    if (gLastWhat.empty()) {
        gLastWhat = "nothing hidden";
    }
    auto const assetKey = reinterpret_cast<std::uintptr_t>(player::SkeletalMesh(mesh));
    if (!gLoggedChoice.contains(assetKey)) {
        gLoggedChoice.insert(assetKey);
        std::string skipped;
        for (std::string const& s : choice.skipped) {
            skipped += std::format("{}{}", skipped.empty() ? "" : ", ", s);
        }
        for (std::string const& s : already) {
            skipped += std::format("{}{} (the game had hidden it)", skipped.empty() ? "" : ", ", s);
        }
        log::Info("sights: the {}'s scope picture is up: hid {} while it shows ([Sights] {}Hide){}", weapon,
            gHiddenNames.empty() ? std::string("nothing") : std::format("'{}'", gLastWhat), sv::WeaponKey(weaponType),
            skipped.empty() ? std::string() : std::format("; not hidden: {}", skipped));
    }
#else
    (void)up;
    (void)wanted;
    (void)sightBone;
    (void)weaponType;
#endif
}

void Restore(char const* why) {
    RestoreNow(why);
}

std::string StatusJson() {
    namespace json = vr::test_request;
    return std::format("{{\"hidden\": {}, \"hides\": {}, \"restores\": {}}}", json::JsonString(gHidden.empty() ? std::string() : gLastWhat),
        gHides, gRestores);
}

void Shutdown(bool exiting) {
    if (!exiting) {
        RestoreNow("the mod unloads");
    }
    gMesh = nullptr;
    gHidden.clear();
    gHiddenNames.clear();
    gWantedKey.clear();
    gDownSinceMs = 0;
    gLoggedChoice.clear();
    gLoggedMaterials.clear();
    gHides = gRestores = 0;
    gLastWhat = "nothing hidden";
    gHideBone = gUnHideBone = gIsBoneHidden = nullptr;
    gLookedUp = gAvailable = false;
#if defined(MEVR_GAME_LE1)
    gHideBoneRef.Reset();
    gUnHideBoneRef.Reset();
    gIsBoneHiddenRef.Reset();
#endif
}

} // namespace mevr::game::scope_parts
