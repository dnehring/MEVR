// SPDX-License-Identifier: MIT
#include "game/OmniTool.hpp"

#include "core/Log.hpp"
#include "core/Ini.hpp"
#include "core/vr/OmniPower.hpp"
#include "engine/Engine.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <array>
#include <cstring>
#include <cmath>
#include <format>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace mevr::game::omni_tool {

bool gRequested = false;  // Request since the last Tick
namespace {

#if defined(MEVR_GAME_LE1)

// Our reflection layer (research/34); each has a row in engine/ReflectCheck.
ue3::ClassRef gTemplateClass{"SFXGame.BioVFXTemplate"};
ue3::ClassRef gEffectClass{"SFXGame.BioVisualEffect"};
ue3::FunctionRef gCreateCrust{"SFXGame.BioVisualEffect.CreateCrustEffect"};
ue3::FunctionRef gSetLifeTime{"SFXGame.BioVisualEffect.SetLifeTime"};
ue3::FunctionRef gSetState{"SFXGame.BioVisualEffect.SetState"};
ue3::ClassRef gEmitterClass{"Engine.Emitter"};
ue3::FunctionRef gActorDetach{"Engine.Actor.DetachComponent"};
ue3::FunctionRef gMeshAttach{"Engine.SkeletalMeshComponent.AttachComponent"};
ue3::FunctionRef gMeshDetach{"Engine.SkeletalMeshComponent.DetachComponent"};
ue3::FunctionRef gActivate{"Engine.ParticleSystemComponent.ActivateSystem"};
ue3::FunctionRef gDeactivate{"Engine.ParticleSystemComponent.DeactivateSystem"};
ue3::FieldRef gBaseBoneName{"Engine.Actor", "BaseBoneName"};
ue3::FieldRef gRelativeLocation{"Engine.Actor", "RelativeLocation"};
ue3::FieldRef gRelativeRotation{"Engine.Actor", "RelativeRotation"};
ue3::FieldRef gLocalToWorld{"Engine.PrimitiveComponent", "LocalToWorld"};

struct Matrix {
    float m[16];  // FMatrix, row-major; the translation is the last row
};
ue3::FieldRef gBase{"Engine.Actor", "Base"};
ue3::FieldRef gEmitterComponent{"Engine.Emitter", "ParticleSystemComponent"};
ue3::FieldRef gParticleTemplate{"Engine.ParticleSystemComponent", "Template"};
ue3::FunctionRef gSetScalar{"SFXGame.BioVisualEffect.SetScalarParameter"};
ue3::FieldRef gCachedParameters{"SFXGame.BioVisualEffect", "m_CachedInstanceParameters"};
ue3::FieldRef gParameterName{gCachedParameters, "ParameterName"};
ue3::FieldRef gParameterValue{gCachedParameters, "fCurrentValue"};
ue3::FieldRef gParameterInited{gCachedParameters, "bInited"};
ue3::FieldRef gParameterTargets{gCachedParameters, "OffsetInfo"};

// Discovery reads template data only. The selected writes go through the live component's script API.
ue3::ClassRef gFloatParticle{"Engine.DistributionFloatParticleParameter"};
ue3::ClassRef gVectorParticle{"Engine.DistributionVectorParticleParameter"};
ue3::ClassRef gMicClass{"Engine.MaterialInstanceConstant"};
ue3::ClassRef gMeshComponentClass{"Engine.MeshComponent"};
ue3::ClassRef gMeshTypeClass{"Engine.ParticleModuleTypeDataMesh"};
ue3::FieldRef gEmitters{"Engine.ParticleSystem", "Emitters"};
ue3::FieldRef gLods{"Engine.ParticleEmitter", "LODLevels"};
ue3::FieldRef gModules{"Engine.ParticleLODLevel", "Modules"};
ue3::FieldRef gRequired{"Engine.ParticleLODLevel", "RequiredModule"};
ue3::FieldRef gSpawn{"Engine.ParticleLODLevel", "SpawnModule"};
ue3::FieldRef gTypeData{"Engine.ParticleLODLevel", "TypeDataModule"};
ue3::FieldRef gRequiredMaterial{"Engine.ParticleModuleRequired", "Material"};
ue3::FieldRef gTypeMesh{"Engine.ParticleModuleTypeDataMesh", "Mesh"};
ue3::FieldRef gStaticLods{"Engine.StaticMesh", "LODInfo"};
ue3::FieldRef gStaticElements{gStaticLods, "Elements"};
ue3::FieldRef gStaticMaterial{gStaticElements, "Material"};
ue3::FieldRef gSMComponents{"Engine.ParticleSystemComponent", "SMComponents"};
ue3::FieldRef gSMMaterials{"Engine.ParticleSystemComponent", "SMMaterialInterfaces"};
ue3::ClassRef gMeshMaterialModule{"Engine.ParticleModuleMeshMaterial"};
ue3::FieldRef gModuleMeshMaterials{"Engine.ParticleModuleMeshMaterial", "MeshMaterials"};
ue3::ClassRef gMaterialByParameter{"Engine.ParticleModuleMaterialByParameter"};
ue3::FieldRef gDefaultMaterials{"Engine.ParticleModuleMaterialByParameter", "DefaultMaterials"};
ue3::FieldRef gMeshMaterials{"Engine.MeshComponent", "Materials"};
ue3::FieldRef gCrustMesh{"Engine.StaticMeshComponent", "StaticMesh"};
ue3::ClassRef gStaticComponentClass{"Engine.StaticMeshComponent"};
ue3::FieldRef gActorOwner{"Engine.Actor", "Owner"};
ue3::ClassRef gActorClass{"Engine.Actor"};
ue3::FieldRef gFloatName{"Engine.DistributionFloatParameterBase", "ParameterName"};
ue3::FieldRef gVectorName{"Engine.DistributionVectorParameterBase", "ParameterName"};
ue3::FieldRef gFloatConstant{"Engine.DistributionFloatConstant", "Constant"};
ue3::FieldRef gVectorConstant{"Engine.DistributionVectorConstant", "Constant"};
ue3::FieldRef gFloatMode{"Engine.DistributionFloatParameterBase", "ParamMode"};
ue3::FieldRef gVectorModes{"Engine.DistributionVectorParameterBase", "ParamModes"};
ue3::FieldRef gInstance{"Engine.ParticleSystemComponent", "InstanceParameters"};
ue3::FieldRef gInstanceName{gInstance, "Name"};
ue3::FieldRef gInstanceType{gInstance, "ParamType"};
ue3::FieldRef gInstanceScalar{gInstance, "Scalar"};
ue3::FieldRef gInstanceVector{gInstance, "Vector"};
ue3::FieldRef gInstanceColor{gInstance, "Color"};
ue3::FieldRef gInstanceMaterial{gInstance, "Material"};
ue3::FieldRef gScalarValues{"Engine.MaterialInstanceConstant", "ScalarParameterValues"};
ue3::FieldRef gScalarName{gScalarValues, "ParameterName"};
ue3::FieldRef gScalarValue{gScalarValues, "ParameterValue"};
ue3::FieldRef gVectorValues{"Engine.MaterialInstanceConstant", "VectorParameterValues"};
ue3::FieldRef gMatVectorName{gVectorValues, "ParameterName"};
ue3::FieldRef gMatVectorValue{gVectorValues, "ParameterValue"};
ue3::FieldRef gMaterialParent{"Engine.MaterialInstance", "Parent"};
ue3::FunctionRef gSetFloat{"Engine.ParticleSystemComponent.SetFloatParameter"};
ue3::FunctionRef gSetVector{"Engine.ParticleSystemComponent.SetVectorParameter"};
ue3::FunctionRef gSetColor{"Engine.ParticleSystemComponent.SetColorParameter"};
ue3::FunctionRef gSetMaterialScalar{"Engine.MaterialInstanceConstant.SetScalarParameterValue"};
bool gDiscoveryLogged = false; // once per session, survives a world/effect change
bool gCrustRoute = false;      // the first discovery's route was an owned material: later wakes look for the crust meshes again
struct RwVector { float x, y, z, w; };
struct Color { std::uint8_t b, g, r, a; };
struct LinearColor { float r, g, b, a; };
struct PowerBinding {
    ue3::Object* object = nullptr;
    std::uint64_t name = 0;
    std::uint8_t type = 0; // 1 scalar, 2 vector, 3 color, 4 owned MIC scalar
    float scalar = 0.0f;
    ue3::Vector vector{};
    Color color{};
};

constexpr std::string_view kTemplate = "BIOG_V_T_Omni_Tool_Z.Prefab.OmniTool_2PieceLoop";
constexpr std::uint64_t kClassDefaultFlag = 0x200;  // EObjectFlags: RF_ClassDefaultObject
constexpr float kLifeS = 3600.0f;     // as long as the screen is up; ended by SetLifeTime
constexpr float kFadeS = 0.3f;        // when it closes: the effect's particles finish (bAllowParticlesToFinish)
constexpr std::uint64_t kRetryMs = 2000;

struct State {
    void const* world = nullptr;
    ue3::Object* templ = nullptr;   // the game's omni-tool template, found once per world
    bool searched = false;
    ue3::Object* effect = nullptr;  // the BioVisualEffect on the pawn
    ue3::Object* pawn = nullptr;
    std::uint64_t lastTryMs = 0;
    int made = 0;
    int woken = 0;                  // its emitters given their tick back
    bool byHand = false;            // made by `mevr omnitool on`: only `off` (or a screen's own) ends it
    ue3::Object* mesh = nullptr;    // the pawn's mesh its particle systems were moved onto
    std::vector<ue3::Object*> adopted;
    ue3::Object* display = nullptr; // the hand piece (OmniHand_Loop) among them
    std::uint64_t adoptedMs = 0;    // when they moved onto the mesh
    struct Brightness { std::uint64_t name; float full; };
    std::vector<Brightness> brightness;
    std::vector<PowerBinding> particlePower;
    bool powerFound = false;
    bool powerDone = false;
    float luminance = 1.0f;
    std::string powerPath = "not checked";
    std::string why = "off";
};
State gS;
std::mutex gStatusMutex;
std::string gStatusJson = "{\"on\": false}";

void SetStatus() {
    std::lock_guard<std::mutex> const lock(gStatusMutex);
    gStatusJson = std::format("{{\"on\": {}, \"made\": {}, \"emitters\": {}, \"template\": {}, \"state\": \"{}\", "
        "\"powerPath\": \"{}\", \"onMs\": {}, \"luminance\": {:.4f}, \"powerDone\": {}}}",
        gS.effect != nullptr, gS.made, gS.woken, gS.templ != nullptr, gS.why, gS.powerPath, gS.adoptedMs, gS.luminance, gS.powerDone);
}

ue3::Object* DefaultObject(ue3::Class* cls) {
    ue3::Reflection& r = ue3::runtime::Get();
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && r.ClassOf(o) == cls && (r.FlagsOf(o) & kClassDefaultFlag) != 0) {
            return o;
        }
    }
    return nullptr;
}

// The template, by class then name (once per world: its package is one of the game's global ones, BIOG_*).
ue3::Object* Template() {
    void const* const world = engine::World();
    if (gS.world != world) {
        gS = State{};
        gS.world = world;
    }
    if (gS.templ != nullptr && ue3::runtime::IsAlive(gS.templ)) {
        return gS.templ;
    }
    if (gS.searched) {
        return nullptr;
    }
    gS.searched = true;
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gTemplateClass.Get();
    for (std::int32_t i = 0, n = cls != nullptr ? r.ObjectCount() : 0; i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls)) {
            continue;
        }
        std::string const name = r.FullName(o);
        if (name.size() >= kTemplate.size() && name.compare(name.size() - kTemplate.size(), kTemplate.size(), kTemplate) == 0) {
            gS.templ = o;
            log::Info("omni tool: the game's omni-tool is {}", name);
            return o;
        }
    }
    log::Warn("omni tool: {} isn't loaded: no omni-tool on the arm (the screen still projects)", kTemplate);
    return nullptr;
}

bool Alive(ue3::Object* effect) {
    return effect != nullptr && ue3::runtime::IsAlive(effect) && ue3::IsA(effect, gEffectClass);
}

bool SetLifeTime(ue3::Object* effect, float seconds) {
    if (!Alive(effect) || gSetLifeTime.Get() == nullptr) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), gSetLifeTime.Get());
    return p.Set("fLife", seconds) && ue3::runtime::Call(effect, gSetLifeTime.Get(), p);
}

template <class F>
bool CallOn(ue3::Object* object, ue3::FunctionRef& function, F&& fill) {
    if (object == nullptr || function.Get() == nullptr) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), function.Get());
    return fill(p) && ue3::runtime::Call(object, function.Get(), p);
}

// Reflected arrays are bounded even for diagnostics; null and unreadable are distinct from an empty array.
std::vector<ue3::Object*> Objects(ue3::FieldRef& field, void const* base, bool report) {
    std::vector<ue3::Object*> result;
    auto const values = field.ElementsOf(base);
    if (report) log::Info("omni tool: params {}: {}", field.Name(), values ? std::format("{} entries",values->count) : "reflection/read unavailable");
    if (!values || values->stride != sizeof(ue3::Object*) || values->count > 4096) return result;
    for (int i=0; i<values->count; ++i) {
        ue3::Object* object = nullptr;
        std::memcpy(&object,values->At(i),sizeof(object));
        if (object && ue3::runtime::IsAlive(object)) result.push_back(object);
    }
    return result;
}

bool EffectOwned(ue3::Object* object) {
    auto& r = ue3::runtime::Get();
    for (int depth=0; object && depth<16; ++depth,object=r.OuterOf(object)) {
        if (object == gS.effect || std::find(gS.adopted.begin(),gS.adopted.end(),object) != gS.adopted.end()) return true;
    }
    return false;
}

void MaterialParams(ue3::Object* material, bool report, std::vector<PowerBinding>* candidates, int depth=0) {
    if (!material || !ue3::runtime::IsAlive(material) || depth>8) return;
    if (report) log::Info("omni tool: params material {} (effect-owned {})", ue3::runtime::FullName(material),EffectOwned(material));
    if (!ue3::IsA(material,gMicClass)) return;
    auto const scalar = gScalarValues.ElementsOf(material);
    auto const vector = gVectorValues.ElementsOf(material);
    if (report) log::Info("omni tool: params material scalars {}, vectors {}", scalar ? std::format("{}",scalar->count) : "unavailable",
        vector ? std::format("{}",vector->count) : "unavailable");
    for (int i=0; scalar && i<scalar->count && i<4096; ++i) {
        auto const name = gScalarName.Read<std::uint64_t>(scalar->At(i));
        auto const value = gScalarValue.Read<float>(scalar->At(i));
        if (!name || !value) continue;
        std::string const text = ue3::runtime::NameText(*name);
        if (report) log::Info("omni tool: params material scalar '{}' = {:.4f}",text,*value);
        if (candidates && EffectOwned(material) && vr::omni_power::BrightnessParameter(text) && std::isfinite(*value) && *value>0 && gSetMaterialScalar.Get())
            candidates->push_back({material,*name,4,*value});
    }
    for (int i=0; vector && i<vector->count && i<4096; ++i) {
        auto const name = gMatVectorName.Read<std::uint64_t>(vector->At(i));
        auto const value = gMatVectorValue.Read<LinearColor>(vector->At(i));
        if (report && name && value) log::Info("omni tool: params material vector '{}' = ({:.4f},{:.4f},{:.4f},{:.4f})",
            ue3::runtime::NameText(*name),value->r,value->g,value->b,value->a);
    }
    MaterialParams(gMaterialParent.Read<ue3::Object*>(material).value_or(nullptr),report,candidates,depth+1);
}

void MeshParams(ue3::Object* mesh, bool report, std::vector<PowerBinding>* candidates) {
    auto const lods = gStaticLods.ElementsOf(mesh);
    if (report) log::Info("omni tool: params mesh {} LODs {}",ue3::runtime::FullName(mesh),
        lods ? std::format("{}",lods->count) : "reflection/read unavailable");
    auto const elements = lods && lods->count>0 ? gStaticElements.ElementsOf(lods->At(0)) : std::nullopt;
    for (int i=0; elements && i<elements->count && i<4096; ++i)
        MaterialParams(gStaticMaterial.Read<ue3::Object*>(elements->At(i)).value_or(nullptr),report,candidates);
}

struct DistributionUse { ue3::Object* object; std::string field; };
// Read the actual reflected raw-distribution references, including BioWare's RwVector wrappers and dynamic arrays.
// This also avoids treating an orphaned distribution whose Outer is a module as a live input.
void DistributionUses(void const* base, ue3::Struct* owner, std::string const& path, std::vector<DistributionUse>& uses, int depth=0) {
    auto& r = ue3::runtime::Get();
    if (!base || !owner || depth>4) return;
    for (auto* cls=owner; cls; cls=r.SuperOf(cls)) {
        int fields=0;
        for (auto* field=r.ChildrenOf(cls); field && fields++<1024; field=r.NextOf(field)) {
            if (!r.IsA(field,r.PropertyClass())) continue;
            auto const info = r.Describe(static_cast<ue3::Property*>(field));
            if (!info || info->offset<0 || info->elementSize<=0 || info->arrayDim<1 || info->arrayDim>512 ||
                static_cast<std::int64_t>(info->offset)+static_cast<std::int64_t>(info->elementSize)*info->arrayDim>r.PropertySize(owner)) continue;
            auto const name = r.NameText(field);
            auto const full = path.empty() ? name : path+"."+name;
            auto const* address = static_cast<std::uint8_t const*>(base)+info->offset;
            for (int i=0; i<info->arrayDim; ++i) {
                auto const* value = address+static_cast<std::ptrdiff_t>(i)*info->elementSize;
                if (info->kind==ue3::Kind::Object && name=="Distribution" && info->elementSize==sizeof(ue3::Object*)) {
                    ue3::Object* object = nullptr;
                    std::memcpy(&object,value,sizeof(object));
                    if (object && r.IsAlive(object)) uses.push_back({object,full});
                } else if (info->kind==ue3::Kind::Struct) {
                    DistributionUses(value,r.StructOf(*info),full,uses,depth+1);
                } else if (info->kind==ue3::Kind::Array && info->elementSize==sizeof(ue3::TArray<std::uint8_t>)) {
                    auto const inner = r.Describe(static_cast<ue3::Property*>(info->typed));
                    ue3::TArray<std::uint8_t> array;
                    std::memcpy(&array,value,sizeof(array));
                    if (!inner || inner->kind!=ue3::Kind::Struct || inner->elementSize<=0 || !array.data || array.count<0 || array.count>512) continue;
                    for (int j=0; j<array.count; ++j) DistributionUses(array.data+static_cast<std::ptrdiff_t>(j)*inner->elementSize,
                        r.StructOf(*inner),full+"[]",uses,depth+1);
                }
            }
        }
    }
}

void DiscoverPower(bool report, std::vector<PowerBinding>* candidates, bool scanCrust) {
    auto& r = ue3::runtime::Get();
    if (report) log::Info("omni tool: params discovery (read only): {} adopted systems, effect {}",gS.adopted.size(),r.FullName(gS.effect));
    if (report) {
        auto const cached = gCachedParameters.ElementsOf(gS.effect);
        log::Info("omni tool: params cached parameters: {}",cached ? std::format("{} entries",cached->count) : "reflection/read unavailable");
        for (int i=0; cached && i<cached->count; ++i) {
            auto* value = cached->At(i);
            auto const name = gParameterName.Read<std::uint64_t>(value);
            auto const full = gParameterValue.Read<float>(value);
            auto const targets = gParameterTargets.ElementsOf(value);
            log::Info("omni tool: params cached '{}' value {} initialized {} targets {}",name ? ue3::runtime::NameText(*name) : "unreadable",
                full ? std::format("{:.4f}",*full) : "unreadable",gParameterInited.ReadBool(value).value_or(false),
                targets ? std::format("{}",targets->count) : "reflection/read unavailable");
        }
    }
    for (auto* component : gS.adopted) {
        if (!r.IsAlive(component)) continue;
        auto* const system = gParticleTemplate.Read<ue3::Object*>(component).value_or(nullptr);
        if (report) log::Info("omni tool: params component {} template {}",r.FullName(component),r.FullName(system));
        for (auto* material : Objects(gSMMaterials,component,report)) MaterialParams(material,report,candidates);
        for (auto* meshComponent : Objects(gSMComponents,component,report)) {
            if (report) log::Info("omni tool: params live mesh component {}",r.FullName(meshComponent));
            for (auto* material : Objects(gMeshMaterials,meshComponent,report)) MaterialParams(material,report,candidates);
            if (ue3::IsA(meshComponent,gStaticComponentClass)) MeshParams(gCrustMesh.Read<ue3::Object*>(meshComponent).value_or(nullptr),report,candidates);
        }
        auto const instances = gInstance.ElementsOf(component);
        if (report) log::Info("omni tool: params InstanceParameters: {}",instances ? std::format("{} entries",instances->count) : "reflection/read unavailable");
        for (int i=0; instances && i<instances->count && i<4096; ++i) {
            auto* value = instances->At(i);
            auto const name = gInstanceName.Read<std::uint64_t>(value);
            auto const type = gInstanceType.Read<std::uint8_t>(value);
            auto const scalar = gInstanceScalar.Read<float>(value);
            auto const vector = gInstanceVector.Read<RwVector>(value);
            auto const color = gInstanceColor.Read<Color>(value);
            if (report && name && type) log::Info("omni tool: params instance '{}' type {} scalar {} vector ({},{},{}) color ({},{},{},{})",
                ue3::runtime::NameText(*name),*type,scalar.value_or(0),vector ? vector->x : 0,vector ? vector->y : 0,vector ? vector->z : 0,
                color ? color->r : 0,color ? color->g : 0,color ? color->b : 0,color ? color->a : 0);
        }
        // A material parameter in InstanceParameters is a live material override too (PSPT_Material = 5).
        for (int i=0; instances && i<instances->count && i<4096; ++i) {
            auto* value = instances->At(i);
            if (gInstanceType.Read<std::uint8_t>(value).value_or(0)==5)
                MaterialParams(gInstanceMaterial.Read<ue3::Object*>(value).value_or(nullptr),report,candidates);
        }
        for (auto* emitter : Objects(gEmitters,system,report)) {
            if (report) log::Info("omni tool: params emitter {}",r.FullName(emitter));
            auto const lods = Objects(gLods,emitter,report);
            if (lods.empty()) continue;
            auto* const lod = lods.front();
            auto modules = Objects(gModules,lod,report);
            for (auto* field : {&gRequired,&gSpawn,&gTypeData}) {
                auto* const module = field->Read<ue3::Object*>(lod).value_or(nullptr);
                if (module && r.IsAlive(module) && std::find(modules.begin(),modules.end(),module)==modules.end()) modules.push_back(module);
            }
            MaterialParams(gRequiredMaterial.Read<ue3::Object*>(gRequired.Read<ue3::Object*>(lod).value_or(nullptr)).value_or(nullptr),report,candidates);
            for (auto* module : modules) {
                if (report) log::Info("omni tool: params LOD 0 module {}",r.FullName(module));
                if (ue3::IsA(module,gMeshMaterialModule))
                    for (auto* material : Objects(gModuleMeshMaterials,module,report)) MaterialParams(material,report,candidates);
                if (ue3::IsA(module,gMaterialByParameter))
                    for (auto* material : Objects(gDefaultMaterials,module,report)) MaterialParams(material,report,candidates);
                if (ue3::IsA(module,gMeshTypeClass)) MeshParams(gTypeMesh.Read<ue3::Object*>(module).value_or(nullptr),report,candidates);
                std::vector<DistributionUse> uses;
                DistributionUses(module,r.ClassOf(module),"",uses);
                unsigned found = 0;
                for (auto const& use : uses) {
                    auto* const distribution = use.object;
                    if (!ue3::IsA(distribution,gFloatParticle) && !ue3::IsA(distribution,gVectorParticle)) {
                        if (report) log::Info("omni tool: params {} = {} (not a particle parameter)",use.field,r.FullName(distribution));
                        continue;
                    }
                    ++found;
                    bool const scalar = ue3::IsA(distribution,gFloatParticle);
                    auto const name = (scalar ? gFloatName : gVectorName).Read<std::uint64_t>(distribution);
                    auto const full = scalar ? gFloatConstant.Read<float>(distribution) : std::nullopt;
                    auto const fullVector = !scalar ? gVectorConstant.Read<ue3::Vector>(distribution) : std::nullopt;
                    auto const mode = scalar ? gFloatMode.Read<std::uint8_t>(distribution) : std::nullopt;
                    auto const modes = !scalar ? gVectorModes.Read<std::array<std::uint8_t,3>>(distribution) : std::nullopt;
                    if (report) log::Info("omni tool: params distribution {} ({}) ParameterName '{}' default scalar {} vector ({},{},{}) modes ({},{},{})",
                        r.FullName(distribution),use.field,name ? ue3::runtime::NameText(*name) : "reflection/read unavailable",full.value_or(0),
                        fullVector ? fullVector->x : 0,fullVector ? fullVector->y : 0,fullVector ? fullVector->z : 0,
                        mode ? *mode : modes ? (*modes)[0] : -1,modes ? (*modes)[1] : -1,modes ? (*modes)[2] : -1);
                    if (!candidates || !name) continue;
                    std::string const text = ue3::runtime::NameText(*name);
                    bool const colorModule = r.NameText(r.ClassOf(module)).find("Color") != std::string::npos;
                    bool const direct = scalar ? mode && *mode==2 : modes && (*modes)[0]==2 && (*modes)[1]==2 && (*modes)[2]==2;
                    if (!direct || (!vr::omni_power::BrightnessParameter(text) && !(colorModule && !scalar))) continue;
                    PowerBinding binding{component,*name,static_cast<std::uint8_t>(scalar ? 1 : 2),full.value_or(0),fullVector.value_or(ue3::Vector{})};
                    for (int i=0; instances && i<instances->count && i<4096; ++i) {
                        auto* value = instances->At(i);
                        if (gInstanceName.Read<std::uint64_t>(value) != name) continue;
                        auto const type = gInstanceType.Read<std::uint8_t>(value).value_or(0);
                        if (scalar && type==1) binding.scalar = gInstanceScalar.Read<float>(value).value_or(0);
                        else if (!scalar && type==2) {
                            auto const v = gInstanceVector.Read<RwVector>(value);
                            binding.vector = v ? ue3::Vector{v->x,v->y,v->z} : ue3::Vector{};
                        } else if (!scalar && type==3) {
                            binding.type = 3;
                            binding.color = gInstanceColor.Read<Color>(value).value_or(Color{});
                        } else binding.type = 0;
                    }
                    bool const finiteVector = std::isfinite(binding.vector.x) && std::isfinite(binding.vector.y) && std::isfinite(binding.vector.z);
                    bool const usable = binding.type==1 ? std::isfinite(binding.scalar) && binding.scalar>0 && gSetFloat.Get()
                        : binding.type==2 ? finiteVector && (binding.vector.x>0 || binding.vector.y>0 || binding.vector.z>0) && gSetVector.Get()
                        : binding.type==3 && (binding.color.r || binding.color.g || binding.color.b) && gSetColor.Get();
                    bool const duplicate = std::any_of(candidates->begin(),candidates->end(),[&](auto const& b) { return b.object==component && b.name==*name; });
                    if (usable && !duplicate) candidates->push_back(binding);
                }
                if (report) log::Info("omni tool: params module particle-parameter distributions: {}",found);
            }
        }
    }
    // Crust meshes attached to this pawn by the effect. Overrides first, then the authored mesh's materials. This walks every
    // object (about 170,000), milliseconds on the game thread at the moment a screen opens: only when asked (the session's first
    // wake, `mevr omnitool params`, or while an owned material found that way is the route).
    for (int i=0,n=scanCrust ? r.ObjectCount() : 0; i<n; ++i) {
        auto* const component = r.ObjectAt(i);
        if (!component || !r.IsAlive(component) || !ue3::IsA(component,gMeshComponentClass)) continue;
        auto* const actor = r.OuterOf(component);
        bool const crust = actor && ue3::IsA(actor,gActorClass) &&
            (actor==gS.effect || gActorOwner.Read<ue3::Object*>(actor).value_or(nullptr)==gS.effect ||
             (gBase.Read<ue3::Object*>(actor).value_or(nullptr)==gS.pawn && r.FullName(actor).find("Omni")!=std::string::npos));
        if (!crust) continue;
        if (report) log::Info("omni tool: params crust mesh component {}",r.FullName(component));
        for (auto* material : Objects(gMeshMaterials,component,report)) MaterialParams(material,report,candidates);
        if (ue3::IsA(component,gStaticComponentClass)) MeshParams(gCrustMesh.Read<ue3::Object*>(component).value_or(nullptr),report,candidates);
    }
}

// The cache belongs to this effect, not its shared prefab/materials. Only initialized parameters with resolved targets count.
// SetScalarParameter updates those targets through the engine. A successful call alone cannot prove the emitted look.
void FindPowerPath() {
    gS.brightness.clear();
    std::string seen;
    auto const cached = gCachedParameters.ElementsOf(gS.effect);
    log::Info("omni tool: cached parameters: {}",cached ? std::format("{} entries",cached->count) : "reflection/read unavailable");
    if (auto const values = gCachedParameters.ElementsOf(gS.effect)) {
        for (int i = 0; i < values->count; ++i) {
            void* const value = values->At(i);
            auto const name = gParameterName.Read<std::uint64_t>(value);
            auto const full = gParameterValue.Read<float>(value);
            if (!name) continue;
            std::string const text = ue3::runtime::NameText(*name);
            if (!seen.empty()) seen += ", ";
            seen += text;
            auto const targets = gParameterTargets.ElementsOf(value);
            if (vr::omni_power::BrightnessParameter(text) && full && std::isfinite(*full) && *full > 0.0f &&
                gParameterInited.ReadBool(value).value_or(false) && targets && targets->count > 0 && gSetScalar.Get() != nullptr) {
                gS.brightness.push_back({*name, *full});
            }
        }
    }
    gS.particlePower.clear();
    bool const first = !gDiscoveryLogged;
    DiscoverPower(first,&gS.particlePower,first || gCrustRoute);
    gDiscoveryLogged = true;
    // Prefer a live particle input to an owned MIC. Never mutate a package material.
    bool const hasParticle = std::any_of(gS.particlePower.begin(),gS.particlePower.end(),[](auto const& b) { return b.type!=4; });
    if (first) {
        // Later wakes scan every object for the crust meshes only while an owned material is the route.
        gCrustRoute = !hasParticle && gS.brightness.empty() &&
            std::any_of(gS.particlePower.begin(),gS.particlePower.end(),[](auto const& b) { return b.type==4; });
    }
    std::vector<PowerBinding> unique;
    for (auto const& binding : gS.particlePower) {
        if (std::none_of(unique.begin(),unique.end(),[&](auto const& b) { return b.object==binding.object && b.name==binding.name; })) unique.push_back(binding);
    }
    gS.particlePower = std::move(unique);
    if (hasParticle) std::erase_if(gS.particlePower,[](auto const& b) { return b.type==4; });
    if (!gS.brightness.empty()) gS.particlePower.clear();
    gS.powerFound = !gS.brightness.empty() || !gS.particlePower.empty();
    if (gS.powerFound) {
        gS.powerPath = !gS.brightness.empty() ? "BioVisualEffect.SetScalarParameter" : hasParticle
            ? "ParticleSystemComponent parameter" : "effect-owned MaterialInstanceConstant.SetScalarParameterValue";
        for (auto const& binding : gS.particlePower) log::Info("omni tool: power-up through {} '{}' type {} on {} (coverage needs headset checking)",
            gS.powerPath,ue3::runtime::NameText(binding.name),binding.type,ue3::runtime::FullName(binding.object));
        for (auto const& binding : gS.brightness) {
            log::Info("omni tool: power-up through {} '{}' (full {:.4f}, {} adopted systems; visual coverage needs checking)",
                gS.powerPath, ue3::runtime::NameText(binding.name), binding.full, gS.woken);
        }
    } else {
        gS.powerPath = "nothing found: full at once";
        log::Warn("omni tool: power-up through nothing found: full at once (no initialized cached target, direct particle brightness/color input, or effect-owned MIC brightness scalar; cached: {})",
            seen.empty() ? (cached ? (cached->count==0 ? "empty" : "entries present; names unreadable") : "reflection/read unavailable") : seen);
    }
}

bool SetBrightness(float luminance) {
    bool ok = true;
    for (auto const& binding : gS.brightness) {
        ok = CallOn(gS.effect, gSetScalar, [&](ue3::Params& p) {
            return p.Set("sParameter", binding.name) && p.Set("fValue", binding.full * luminance);
        }) && ok;
    }
    for (auto const& b : gS.particlePower) {
        if (!ue3::runtime::IsAlive(b.object) || (b.type==4 && !EffectOwned(b.object))) { ok=false; continue; }
        bool called = false;
        if (b.type==1 || b.type==4) {
            auto& function = b.type==1 ? gSetFloat : gSetMaterialScalar;
            called = CallOn(b.object,function,[&](ue3::Params& p) {
                return p.Set("ParameterName",b.name) && p.Set(b.type==1 ? "Param" : "Value",b.scalar*luminance);
            });
        } else if (b.type==2) {
            called = CallOn(b.object,gSetVector,[&](ue3::Params& p) {
                return p.Set("ParameterName",b.name) && p.Set("Param",ue3::Vector{b.vector.x*luminance,b.vector.y*luminance,b.vector.z*luminance});
            });
        } else if (b.type==3) {
            auto scale = [luminance](std::uint8_t c) { return static_cast<std::uint8_t>(std::lround(static_cast<float>(c)*luminance)); };
            called = CallOn(b.object,gSetColor,[&](ue3::Params& p) {
                return p.Set("ParameterName",b.name) && p.Set("Param",Color{scale(b.color.b),scale(b.color.g),scale(b.color.r),b.color.a});
            });
        }
        // Script APIs own their engine writes; Params only writes our local parameter buffer.
        ok = called && ok;
    }
    return ok;
}

void PowerStep() {
    if (!Alive(gS.effect) || gS.powerDone) return;
    float const age = vr::omni_power::Age(gS.adoptedMs, GetTickCount64());
    gS.luminance = gS.powerFound ? vr::omni_power::Luminance(age) : 1.0f;
    if (gS.powerFound && !SetBrightness(gS.luminance)) {
        SetBrightness(1.0f);
        gS.powerPath += " failed: restoring full brightness";
        gS.luminance = 1.0f;
        gS.powerFound = false;
        log::Warn("omni tool: power-up through {}", gS.powerPath);
    }
    gS.powerDone = !gS.powerFound || age >= vr::omni_power::kRiseS;
    SetStatus();
}

// The prefab's emitters come out of CreateCrustEffect attached to the pawn's bones (the gauntlet on LeftElbowTwist2, the hand's
// display on LeftWrist: the nearest bones to where the prefab places them) with bTickIsDisabled copied from the prefab's
// archetypes, so their particle systems never tick and nothing spawns (measured 2026-11-04: AccumTickTime 0, bJustAttached
// stays set while LastRenderTime moves). Ticking them isn't enough: the bypass minigame and the GUI screens set bPlayersOnly,
// and then the engine ticks components only of actors the player controls (the actor tick group's loop, 0x40D630, through the
// component tick 0x41C220). So each particle system moves onto the pawn's own mesh at the emitter's bone and offset
// (SkeletalMeshComponent.AttachComponent): it ticks with Shepard, on foot or frozen. The emitters stay, empty.
int AdoptComponents(ue3::Object* pawn, ue3::Object* mesh) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gEmitterClass.Get();
    if (cls == nullptr || mesh == nullptr) {
        return 0;
    }
    int adopted = 0;
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls) || gBase.Read<ue3::Object*>(o).value_or(nullptr) != pawn) {
            continue;
        }
        ue3::Object* const component = gEmitterComponent.Read<ue3::Object*>(o).value_or(nullptr);
        ue3::Object* const system = component != nullptr ? gParticleTemplate.Read<ue3::Object*>(component).value_or(nullptr) : nullptr;
        if (system == nullptr || r.FullName(system).find("BIOG_V_T_Omni_Tool_Z.") == std::string::npos ||
            std::find(gS.adopted.begin(), gS.adopted.end(), component) != gS.adopted.end()) {
            continue;
        }
        auto const bone = gBaseBoneName.Read<std::uint64_t>(o);
        auto const at = gRelativeLocation.Read<ue3::Vector>(o);
        auto const turn = gRelativeRotation.Read<ue3::Rotator>(o);
        if (!bone || !at || !turn) {
            continue;
        }
        bool const detached = CallOn(o, gActorDetach, [&](ue3::Params& p) { return p.Set("ExComponent", component); });
        bool const attached = detached && CallOn(mesh, gMeshAttach, [&](ue3::Params& p) {
            return p.Set("Component", component) && p.Set("BoneName", *bone) && p.Set("RelativeLocation", *at) &&
                   p.Set("RelativeRotation", *turn) && p.Set("RelativeScale", ue3::Vector{1.0f, 1.0f, 1.0f});
        });
        if (!attached) {
            log::Warn("omni tool: {} couldn't move onto Shepard's mesh (detach {}, attach {})", r.NameText(system), detached, attached);
            continue;
        }
        CallOn(component, gActivate, [](ue3::Params& p) { return p.SetBool("bFlagAsJustAttached", false); });
        gS.adopted.push_back(component);
        if (r.NameText(system).find("OmniHand") != std::string::npos) {
            gS.display = component;
        }
        ++adopted;
        log::Info("omni tool: {} on Shepard's mesh at bone {} ({:.1f}, {:.1f}, {:.1f})", r.NameText(system), ue3::runtime::NameText(*bone),
                  at->x, at->y, at->z);
    }
    return adopted;
}

// The particle systems leave Shepard's mesh (the screen closed): they stop at once.
void ReleaseComponents() {
    for (ue3::Object* const component : gS.adopted) {
        if (!ue3::runtime::IsAlive(component)) {
            continue;
        }
        CallOn(component, gDeactivate, [](ue3::Params&) { return true; });
        if (gS.mesh != nullptr && ue3::runtime::IsAlive(gS.mesh)) {
            CallOn(gS.mesh, gMeshDetach, [&](ue3::Params& p) { return p.Set("Component", component); });
        }
    }
    gS.adopted.clear();
    gS.display = nullptr;
    gS.mesh = nullptr;
}

bool SetState(ue3::Object* effect, std::uint8_t state) {
    if (!Alive(effect) || gSetState.Get() == nullptr) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), gSetState.Get());
    return p.Set("eState", state) && p.SetBool("bForceSetState", true) && p.SetBool("bPlayAudio", true) && p.SetBool("bForceKill", false) &&
           ue3::runtime::Call(effect, gSetState.Get(), p);
}

// The effect's two particle systems onto Shepard's mesh, started.
void Wake() {
    gS.mesh = player::Mesh(gS.pawn);
    gS.adoptedMs = GetTickCount64();
    gS.woken = AdoptComponents(gS.pawn, gS.mesh);
    log::Info("omni tool: {} of its particle systems on Shepard's mesh", gS.woken);
    gS.powerDone = false;
    FindPowerPath();
    PowerStep();
}

// BioVisualEffect.CreateCrustEffect on the class's default object (script's `class'BioVisualEffect'.static.`).
ue3::Object* Create(ue3::Object* pawn, float lifeS) {
    ue3::Object* const templ = Template();
    ue3::Class* const cls = gEffectClass.Get();
    ue3::Object* const defaults = cls != nullptr ? DefaultObject(cls) : nullptr;
    if (templ == nullptr || defaults == nullptr || gCreateCrust.Get() == nullptr) {
        gS.why = templ == nullptr ? "no template" : "BioVisualEffect.CreateCrustEffect not found";
        return nullptr;
    }
    ue3::Params p(ue3::runtime::Get(), gCreateCrust.Get());
    bool const ok = p.Set("a_pTemplate", templ) && p.Set("Target", pawn) && p.Set("fLifeTime", lifeS) && p.SetBool("bUseVFXPool", false);
    if (!ok || !ue3::runtime::Call(defaults, gCreateCrust.Get(), p)) {
        gS.why = "the call failed";
        return nullptr;
    }
    ue3::Object* const effect = p.Return<ue3::Object*>().value_or(nullptr);
    return Alive(effect) ? effect : nullptr;
}

void End(char const* why, float fadeS) {
    if (Alive(gS.effect)) SetBrightness(1.0f);  // leave the game's existing close fade at its authored brightness
    ReleaseComponents();
    if (gS.effect != nullptr) {
        bool const ended = SetLifeTime(gS.effect, fadeS);
        log::Info("omni tool: off ({}){}", why, ended ? "" : ": the effect was already gone");
    }
    gS.effect = nullptr;
    gS.pawn = nullptr;
    gS.byHand = false;
    gS.adoptedMs = 0;
    gS.brightness.clear();
    gS.particlePower.clear();
    gS.why = why;
    SetStatus();
}

bool Show(float lifeS) {
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    if (pawn == nullptr) {
        gS.why = "no pawn";
        return false;
    }
    if (Alive(gS.effect) && gS.pawn == pawn) {
        return true;
    }
    if (gS.effect != nullptr) {
        End("the pawn changed or the effect ended", kFadeS);
    }
    std::uint64_t const now = GetTickCount64();
    if (now - gS.lastTryMs < kRetryMs) {
        return false;
    }
    gS.lastTryMs = now;
    gS.effect = Create(pawn, lifeS);
    if (gS.effect == nullptr) {
        log::Warn("omni tool: CreateCrustEffect gave no effect ({})", gS.why);
        SetStatus();
        return false;
    }
    gS.pawn = pawn;
    ++gS.made;
    Wake();
    gS.why = gS.woken > 0 ? "on" : "on, but no emitter of it found to wake";
    log::Info("omni tool: on Shepard's arm ({}, {})", ue3::runtime::FullName(gS.effect), ue3::runtime::FullName(pawn));
    SetStatus();
    return true;
}

#endif

} // namespace

void Request() {
    gRequested = true;
}

void Tick(bool wanted) {
    wanted = wanted || gRequested;  // the power wheel's request (research/55)
    gRequested = false;
#if defined(MEVR_GAME_LE1)
    if (!ue3::runtime::Ready()) {
        return;
    }
    if (engine::World() != gS.world && gS.world != nullptr) {
        gS = State{};  // its actors went with the old world
        SetStatus();
    }
    if (wanted) {
        gS.byHand = false;
        Show(kLifeS);
    } else if (gS.effect != nullptr && !gS.byHand) {
        End("the screen closed", kFadeS);
    }
    PowerStep();
#else
    (void)wanted;
#endif
}

std::string Console(std::string_view args) {
#if defined(MEVR_GAME_LE1)
    std::istringstream words{std::string(args)};
    std::string sub;
    std::string number;
    words >> sub >> number;
    float const seconds = static_cast<float>(ini::Number(number).value_or(0.0));
    if (sub == "on") {
        if (Alive(gS.effect)) End("restart power-up by hand", kFadeS);
        gS.lastTryMs = 0;
        bool const ok = Show(seconds > 0.0f ? seconds : kLifeS);
        gS.byHand = ok;
        for (float t : {0.0f, 0.03f, 0.06f, 0.10f, 0.15f, 0.30f, vr::omni_power::kRiseS}) {
            log::Info("omni tool: power-up curve {:.2f} s = {:.4f} luminance (engine path: {})", t,
                vr::omni_power::Luminance(t), gS.powerPath);
        }
        return ok ? std::format("omni tool on: {}", ue3::runtime::FullName(gS.effect)) : std::format("omni tool: not made ({})", gS.why);
    }
    if (sub == "params") {
        if (!Alive(gS.effect)) {
            log::Info("omni tool: params discovery: no live effect; open the wrist screen or use 'mevr omnitool on' first");
            return "omni tool: no live effect to inspect";
        }
        DiscoverPower(true,nullptr,true);
        auto const cached = gCachedParameters.ElementsOf(gS.effect);
        log::Info("omni tool: cached parameters: {}",cached ? std::format("{} entries",cached->count) : "reflection/read unavailable (or no live effect)");
        return "omni tool: read-only params discovery written to the log";
    }
    if (sub == "off") {
        End("by hand", seconds > 0.0f ? seconds : kFadeS);
        return "omni tool off";
    }
    if (sub == "wake") {
        Wake();
        SetStatus();
        return std::format("omni tool: {} emitters woken", gS.woken);
    }
    if (sub == "setstate") {
        bool const ok = SetState(gS.effect, static_cast<std::uint8_t>(seconds));
        return std::format("omni tool: SetState({}) {}", static_cast<int>(seconds), ok ? "called" : "failed");
    }
    return StatusJson();
#else
    (void)args;
    return "omni tool: LE1 only";
#endif
}

std::uint64_t OnMs() {
#if defined(MEVR_GAME_LE1)
    return Alive(gS.effect) ? gS.adoptedMs : 0;
#else
    return 0;
#endif
}

std::optional<ue3::Vector> DisplayWorld() {
#if defined(MEVR_GAME_LE1)
    // Its first transforms are the animation's arm (arm IK takes the arms the tick a wrist screen opens, the attached component
    // updates with the mesh after it): the screen's opening place came out 0.9 m off (2026-11-04).
    constexpr std::uint64_t kSettleMs = 150;
    if (gS.display == nullptr || !ue3::runtime::IsAlive(gS.display) || GetTickCount64() - gS.adoptedMs < kSettleMs) {
        return std::nullopt;
    }
    auto const m = gLocalToWorld.Read<Matrix>(gS.display);
    if (!m) {
        return std::nullopt;
    }
    return ue3::Vector{m->m[12], m->m[13], m->m[14]};
#else
    return std::nullopt;
#endif
}

std::string StatusJson() {
#if defined(MEVR_GAME_LE1)
    std::lock_guard<std::mutex> const lock(gStatusMutex);
    return gStatusJson;
#else
    return "{\"on\": false}";
#endif
}

void Shutdown() {
#if defined(MEVR_GAME_LE1)
    if (gS.effect != nullptr && engine::World() == gS.world) {
        End("the mod unloads", kFadeS);
    }
#endif
}

} // namespace mevr::game::omni_tool
