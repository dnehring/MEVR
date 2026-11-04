// SPDX-License-Identifier: MIT
#include "game/OmniTool.hpp"

#include "core/Log.hpp"
#include "engine/Engine.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace mevr::game::omni_tool {
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
    std::string why = "off";
};
State gS;
std::mutex gStatusMutex;
std::string gStatusJson = "{\"on\": false}";

void SetStatus() {
    std::lock_guard<std::mutex> const lock(gStatusMutex);
    gStatusJson = std::format("{{\"on\": {}, \"made\": {}, \"emitters\": {}, \"template\": {}, \"state\": \"{}\"}}",
                              gS.effect != nullptr ? "true" : "false", gS.made, gS.woken, gS.templ != nullptr ? "true" : "false", gS.why);
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
    ReleaseComponents();
    if (gS.effect != nullptr) {
        bool const ended = SetLifeTime(gS.effect, fadeS);
        log::Info("omni tool: off ({}){}", why, ended ? "" : ": the effect was already gone");
    }
    gS.effect = nullptr;
    gS.pawn = nullptr;
    gS.byHand = false;
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

void Tick(bool wanted) {
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
#else
    (void)wanted;
#endif
}

std::string Console(std::string_view args) {
#if defined(MEVR_GAME_LE1)
    std::istringstream words{std::string(args)};
    std::string sub;
    float seconds = 0.0f;
    words >> sub >> seconds;
    if (sub == "on") {
        gS.lastTryMs = 0;
        bool const ok = Show(seconds > 0.0f ? seconds : kLifeS);
        gS.byHand = ok;
        return ok ? std::format("omni tool on: {}", ue3::runtime::FullName(gS.effect)) : std::format("omni tool: not made ({})", gS.why);
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
