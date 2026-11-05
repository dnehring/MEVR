// SPDX-License-Identifier: MIT
#include "game/Vent.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/Vent.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/BonePose.hpp"
#include "engine/Skeleton.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/ArmIk.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/Player.hpp"
#include "game/SupportGrip.hpp"
#include "game/Throwables.hpp"
#include "game/ViewAnchor.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace mevr::game::vent {
namespace {

namespace vnt = ik::vent;

constexpr int kLeft = 0;
constexpr int kRight = 1;
// Grip latch, as the pad mapping's (game/gamepad_mapping).
constexpr float kGripOn = 0.6f;
constexpr float kGripOff = 0.4f;
// The writer's "hands crossed" distance (engine/bone_pose kCrossedMeters): no gun frame then.
constexpr double kCrossedMeters = 50.0;

[[maybe_unused]] double SteadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool gStarted = false;
bool gEnabled = false;
bool gActive = false;  // this tick's conditions held (on foot, view, controllers, a gun in the right hand)
std::string gWhy = "not started";
std::string gLoggedWhy;

vnt::Machine gMachine;
vnt::Params gParams;
vnt::OnGun gOnGun;
bool gOnGunValid = false;

// Squeezes.
bool gClaimed = false;     // this tick's new left squeeze is the vent's
bool gClaimQueue = false;  // ...until the machine has seen it
bool gOwnGrip = false;
bool gHaveCount = false;
std::uint32_t gLastCount = 0;
bool gToldOldGrip = false;
bool gToldNoHaptics = false;

// The held gun's heat this tick.
double gHeatRaw = -1.0;  // m_fCurrentHeat
double gHeat = -1.0;     // ...as a fraction of ventMaxHeat
bool gOverheated = false;
bool gWasOverheated = false;
double gMaxHeatSeen = 0.0;

// Heartbeat counters and the last event.
unsigned gGrabs = 0;
unsigned gVents = 0;
unsigned gRefusals = 0;
unsigned gSlow = 0;
unsigned gLapsed = 0;
std::string gLast = "none";

// ---- The visible rack (research/45 section 6) ----
// The part chosen for one gun model (per mesh asset, weapon type and setting).
struct PartModel {
    ue3::Object const* asset = nullptr;  // the gun's SkeletalMesh, compared by address
    int type = -1;
    std::string setting;
    std::string mesh;  // its name
    vnt::PartChoice choice;
    std::vector<std::string> names;    // the model's bones
    std::vector<std::uint64_t> keys;   // RefSkeleton names of choice.bones
    int boneCount = 0;
};
std::optional<PartModel> gPart;
std::set<std::string> gPartLogged;  // "<mesh>|<type>|<setting>": choices logged once
vnt::Rack gRack;
ue3::Object* gRackMesh = nullptr;  // the gun mesh component the part is published for (compared; checked alive before use)
bool gRackPublished = false;
double gRackLastNow = 0.0;
double gRackHomeUntil = 0.0;      // the rest place stays published until then (the last write puts the part home)
std::uint64_t gRackCallsSeen = 0;  // the stage's calls for the mesh at the last tick
int gRackQuietTicks = 0;           // ticks the stage saw no pose of the published mesh
bool gRackForced = false;          // ForceSkelUpdate poses the gun while the part moves (its own pose update didn't come)
int gRackForcedMisses = 0;
bool gRackFailed = false;          // not even ForceSkelUpdate reaches the stage: stands down for this mesh
ik::Vec3 gRackBack;                // back along the gun, the mesh's component space (unit)
double gRackUnitsPerMeter = 0.0;   // model units per meter of hand travel
double gRackMaxUnits = 0.0;
double gRackHandPeak = 0.0;        // the hand's deepest pull back this stroke (m)
std::string gRackStrokeEnd;        // how the machine ended the stroke (vented, no vent: why)
std::string gRackWhy = "not started";
std::string gRackLoggedWhy;
unsigned gRackStrokes = 0;  // this period
unsigned gRackStrokesTotal = 0;
std::string gRackLast = "none";
// `mevr vent rack`: the part pulled without the controllers.
struct TestRack {
    bool active = false;
    double start = 0.0;
    double fraction = 1.0;
    double holdS = 0.5;
};
TestRack gTestRack;
constexpr double kTestPullS = 0.08;  // the test's pull, like a fast hand
// `mevr vent heat <f> hold`: the heat kept at this (raw) value each tick (negative: off).
double gHeldHeat = -1.0;
// The game's cool-down effect, described once per VFX appearance (research/45 section 6).
std::set<std::uintptr_t> gEffectLogged;

[[maybe_unused]] void SetRackWhy(std::string why) {
    gRackWhy = std::move(why);
    if (gRackWhy != gRackLoggedWhy) {
        gRackLoggedWhy = gRackWhy;
        log::Info("vent: rack: {}", gRackWhy);
    }
}

void SetWhy(std::string why) {
    gWhy = std::move(why);
    if (gWhy != gLoggedWhy) {
        gLoggedWhy = gWhy;
        log::Info("vent: {}", gWhy);
    }
}

[[maybe_unused]] void Pulse(Link const& link, int hand, ik::throwing::Buzz const& buzz) {
    if (!(buzz.amplitude > 0.0f) || !(buzz.seconds > 0.0f)) {
        return;
    }
    if (link.hapticHz != nullptr) {
        link.hapticHz(hand, buzz.seconds, buzz.amplitude, buzz.frequency);
    } else if (link.haptic != nullptr) {
        link.haptic(hand, buzz.seconds, buzz.amplitude);
    } else if (!gToldNoHaptics) {
        gToldNoHaptics = true;
        log::Warn("vent: the host can't vibrate the controllers (services older than v9): no haptics");
    }
}

[[maybe_unused]] std::string Percent(double fraction) {
    return fraction >= 0.0 ? std::format("{:.0f}%", 100.0 * fraction) : std::string("?");
}

void Inactive(std::string why) {
    if (gMachine.Holding()) {
        log::Info("vent: the left hand's hold on the receiver is dropped ({})", why);
    }
    gMachine.Reset();
    gActive = false;
    gOnGunValid = false;
    gClaimQueue = false;
    gHeat = gHeatRaw = -1.0;
    gOverheated = false;
    SetWhy(std::move(why));
}

#if defined(MEVR_GAME_LE1)

// Our reflection layer (research/34): BioWeaponRanged.m_fCurrentHeat (+0x500) and m_bOverheated (+0x548 bit 0x10,
// LExSDKv2's headers; research/45), the event DoCoolDownEffect, BioPawn.IsSwitchingWeapons; engine/ReflectCheck
// compares each.
ue3::FieldRef gCurrentHeat{"SFXGame.BioWeaponRanged", "m_fCurrentHeat"};
ue3::FieldRef gOverheatedField{"SFXGame.BioWeaponRanged", "m_bOverheated"};
ue3::FunctionRef gCoolDownEffectFn{"SFXGame.BioWeaponRanged.DoCoolDownEffect"};
ue3::FunctionRef gIsSwitchingWeaponsFn{"SFXGame.BioPawn.IsSwitchingWeapons"};
ue3::Function* gCoolDownEffect = nullptr;
ue3::Function* gIsSwitchingWeapons = nullptr;
// The visible rack: a gun whose pose isn't updated by itself is posed by the engine's own ForceSkelUpdate.
ue3::FunctionRef gForceSkelUpdateFn{"Engine.SkeletalMeshComponent.ForceSkelUpdate"};
ue3::Function* gForceSkelUpdate = nullptr;
// What the game's cool-down effect is (logged once per VFX appearance): BioWeaponRanged.m_pCachedVFXAppearance (a
// BioWeaponVFXAppearance: nmCoolDown, the socket; vfxCoolDown, the template; fCooldownPerSecond) and its
// m_visualEffects[BIO_WEAPON_VISUAL_EFFECT_COOL_DOWN = 3] before and after the call.
ue3::FieldRef gVfxAppearance{"SFXGame.BioWeaponRanged", "m_pCachedVFXAppearance"};
ue3::FieldRef gVisualEffects{"SFXGame.BioWeaponRanged", "m_visualEffects"};
ue3::FieldRef gCoolDownSocket{"SFXGame.BioWeaponVFXAppearance", "nmCoolDown"};
ue3::FieldRef gCoolDownTemplate{"SFXGame.BioWeaponVFXAppearance", "vfxCoolDown"};
ue3::FieldRef gCooldownPerSecond{"SFXGame.BioWeaponVFXAppearance", "fCooldownPerSecond"};
constexpr std::int32_t kCoolDownEffect = 3;  // EBioWeaponVisualEffect BIO_WEAPON_VISUAL_EFFECT_COOL_DOWN

// Through ProcessEvent with the function's own parameter block (never a generated wrapper).
bool CallBool(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    return parms.ReturnBool().value_or(false);
}

void CallVoid(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
}

void ReadHeat(ue3::Object* weapon, hand_calibration::Vent const& settings) {
    std::optional<float> const heat = gCurrentHeat.Read<float>(weapon);
    gHeatRaw = heat && std::isfinite(*heat) ? *heat : -1.0;
    gHeat = gHeatRaw >= 0.0 ? gHeatRaw / settings.maxHeat : -1.0;
    gMaxHeatSeen = std::max(gMaxHeatSeen, gHeatRaw);
    gOverheated = gOverheatedField.ReadBool(weapon).value_or(false);
    if (gOverheated && !gWasOverheated) {
        // The scale check for ventMaxHeat (research/45 [verify]): what m_fCurrentHeat reads when the game overheats.
        log::Info("vent: the {} overheated at heat {:.3f} (ventMaxHeat {:.2f}{})", ue3::runtime::FullName(weapon), gHeatRaw,
            settings.maxHeat,
            gHeatRaw > 0.0 && std::abs(gHeatRaw - settings.maxHeat) > 0.15 * settings.maxHeat
                ? ": the setting doesn't match the game's scale; set ventMaxHeat to about this heat"
                : "");
    }
    gWasOverheated = gOverheated;
}

void DoVent(Link const& link, ue3::Object* weapon, hand_calibration::Vent const& settings, vnt::Step const& step) {
    double const before = gHeatRaw;
    bool const wasOverheated = gOverheated;
    bool wrote = false;
    if (float* const heat = gCurrentHeat.In<float>(weapon)) {
        *heat = 0.0f;
        write_journal::Wrote("Vent.heat", heat);
        wrote = true;
    }
    if (void* const word = gOverheatedField.Address(weapon)) {
        gOverheatedField.WriteBool(weapon, false);
        write_journal::Wrote("Vent.overheated", word, sizeof(std::uint32_t));
    }
    bool const effect = settings.effect && gCoolDownEffect != nullptr;
    ue3::Object** const slot = gVisualEffects.At<ue3::Object*>(weapon, kCoolDownEffect);
    ue3::Object* const effectBefore = slot != nullptr ? *slot : nullptr;
    if (effect) {
        CallVoid(weapon, gCoolDownEffect);  // the game's own cool-down feedback; game thread (the mod's tick)
    }
    // What that feedback is, once per VFX appearance (research/45 section 6: is there a steam or vent effect, and does it
    // play?): the appearance's cool-down template and socket, and the weapon's cool-down effect slot around the call.
    ue3::Object* const appearance = gVfxAppearance.Read<ue3::Object*>(weapon).value_or(nullptr);
    auto const appearanceKey = reinterpret_cast<std::uintptr_t>(appearance);
    if (!gEffectLogged.contains(appearanceKey)) {
        gEffectLogged.insert(appearanceKey);
        ue3::Object* const effectAfter = slot != nullptr ? *slot : nullptr;
        ue3::Object* const vfx = appearance != nullptr ? gCoolDownTemplate.Read<ue3::Object*>(appearance).value_or(nullptr) : nullptr;
        std::optional<ue3::Name> const socket = appearance != nullptr ? gCoolDownSocket.Read<ue3::Name>(appearance) : std::nullopt;
        std::optional<float> const perSecond = appearance != nullptr ? gCooldownPerSecond.Read<float>(appearance) : std::nullopt;
        log::Info("vent: the game's cool-down effect on the {}: {}{}; its effect slot {} -> {}{}",
            ue3::runtime::FullName(weapon),
            appearance == nullptr ? std::string("no VFX appearance cached (m_pCachedVFXAppearance none)")
                                  : std::format("{}: template {}, at the '{}' socket", ue3::runtime::FullName(appearance),
                                        vfx != nullptr ? ue3::runtime::FullName(vfx) : std::string("none"),
                                        socket ? ue3::runtime::NameText(*socket) : std::string("?")),
            perSecond ? std::format(", fCooldownPerSecond {:.3f}", *perSecond) : std::string(),
            effectBefore != nullptr ? ue3::runtime::FullName(effectBefore) : std::string("empty"),
            slot == nullptr ? std::string("?") : *slot != nullptr ? ue3::runtime::FullName(*slot) : std::string("empty"),
            !effect ? " (the call is off: [Vent] effect = 0)"
            : slot != nullptr && *slot != nullptr && effectAfter != effectBefore ? ": the call made one"
            : slot != nullptr && *slot != nullptr ? ": the slot already held it"
                                                  : ": the call made none (no visual effect; its sound only, if any)");
    }
    ++gVents;
    Pulse(link, kLeft, vnt::VentPulse(settings.hapticAmplitude));
    Pulse(link, kRight, vnt::VentPulse(settings.hapticAmplitude));
    gLast = std::format("vented at {:.1f} m/s ({:.2f} back, {:.2f} m), heat {:.2f} -> 0{}", step.release.speed, step.back, step.travel,
        before, wasOverheated ? " (overheated)" : "");
    log::Info("vent: VENTED the {}: heat {:.3f} ({} of the overheat{}) -> 0{} with a {:.1f} m/s stroke ({:.1f} m/s back along the gun, "
              "{:.2f} m pulled, fastest {:.0f} ms before the release); both hands pulse{}; next in {:.1f} s",
        ue3::runtime::FullName(weapon), before, Percent(gHeat), wasOverheated ? ", overheated" : "", wrote ? "" : " (the heat field didn't resolve: NOT written)",
        step.release.speed, step.back, step.travel, step.release.peakAgo * 1000.0, effect ? "; the game's cool-down effect" : "",
        settings.cooldownS);
    gHeatRaw = 0.0;
    gHeat = 0.0;
    gOverheated = false;
    gWasOverheated = false;
}

// ---- The visible rack (research/45 section 6) --------------------------------------------------------------------

char const* TypeName(int type) {
    switch (type) {
    case 0: return "pistol";
    case 1: return "shotgun";
    case 2: return "assault rifle";
    case 3: return "sniper rifle";
    default: return "gun";
    }
}

std::string BoneList(std::vector<std::string> const& names, std::vector<int> const& parents) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        int const parent = i < parents.size() ? parents[i] : -1;
        out += std::format("{}{} {}{}", out.empty() ? "" : ", ", i, names[i],
            parent >= 0 && static_cast<std::size_t>(parent) < names.size() ? std::format(" (under {})", names[static_cast<std::size_t>(parent)])
                                                                           : std::string(" (root)"));
    }
    return out;
}

// The part for the held model: chosen once per mesh asset, type and setting, logged once (with the bone list when
// nothing fits, so a later session can pick one by name).
void ResolvePart(ue3::Object* mesh, ue3::Object const* asset, int type, std::string const& setting) {
    PartModel m;
    m.asset = asset;
    m.type = type;
    m.setting = setting;
    m.mesh = asset != nullptr ? ue3::runtime::Get().NameText(asset) : std::string("unnamed");
    auto const bones = engine::skeleton::ReadRefSkeleton(mesh);
    if (!bones || bones->names.empty() || bones->names.size() != bones->parents.size() || bones->keys.size() != bones->names.size()) {
        m.choice.why = "the model's bones can't be read";
    } else {
        // The sockets' bones never move (the muzzle, the tracer, the cool-down effect's place: all on Root so far).
        std::vector<int> protectedBones;
        for (auto const& named : engine::skeleton::Sockets(mesh)) {
            if (named.socket.bone >= 0) {
                protectedBones.push_back(named.socket.bone);
            }
        }
        m.choice = vnt::ChoosePart(bones->names, bones->parents, protectedBones, setting, type);
        m.names = bones->names;
        m.boneCount = static_cast<int>(bones->names.size());
        if (m.choice.bone >= 0 && static_cast<int>(m.choice.bones.size()) > engine::bone_pose::kMaxWeaponPartBones) {
            m.choice.bone = -1;
            m.choice.why = "too big a part for the stage";
        }
        for (int b : m.choice.bones) {
            m.keys.push_back(bones->keys[static_cast<std::size_t>(b)]);
        }
    }
    std::string const key = std::format("{}|{}|{}", m.mesh, type, setting);
    if (!gPartLogged.contains(key)) {
        gPartLogged.insert(key);
        if (m.choice.bone >= 0) {
            std::string below;
            for (std::size_t i = 1; i < m.choice.bones.size(); ++i) {
                below += std::format("{}{}", below.empty() ? "" : ", ", m.names[static_cast<std::size_t>(m.choice.bones[i])]);
            }
            log::Info("vent: rack: the {} model {}: its '{}' bone{} is the part that racks ({}, [Vent] {} = {}), up to {:.1f} units back "
                      "along the gun. Its bones: {}",
                TypeName(type), m.mesh, m.names[static_cast<std::size_t>(m.choice.bone)],
                below.empty() ? std::string() : std::format(" (and {} below it)", below), m.choice.source,
                hand_calibration::VentPartKey(type), setting, m.choice.maxUnits, BoneList(m.names, bones->parents));
        } else {
            log::Info("vent: rack: the {} model {} has no part to rack ({}): it stays still on a vent. Its bones: {}. Set [Vent] {} = <bone> "
                      "to choose one",
                TypeName(type), m.mesh, m.choice.why, m.names.empty() ? std::string("none") : BoneList(m.names, bones ? bones->parents : std::vector<int>{}),
                type >= 0 && type < 4 ? hand_calibration::VentPartKey(type) : "<type>Part");
        }
    }
    gPart = std::move(m);
}

void ForceSkelUpdate(ue3::Object* mesh) {
    if (gForceSkelUpdate == nullptr || mesh == nullptr || !ue3::runtime::IsAlive(mesh)) {
        return;
    }
    ue3::Params parms(ue3::runtime::Get(), gForceSkelUpdate);
    ue3::runtime::Call(mesh, gForceSkelUpdate, parms);  // the native through ProcessEvent; game thread (the mod's tick)
}

void PublishPart(ue3::Object* mesh, double units) {
    engine::bone_pose::WeaponPart p;
    p.mesh = mesh;
    p.boneCount = gPart->boneCount;
    p.count = static_cast<int>(std::min<std::size_t>(gPart->choice.bones.size(), engine::bone_pose::kMaxWeaponPartBones));
    for (int i = 0; i < p.count; ++i) {
        p.bones[static_cast<std::size_t>(i)] = gPart->choice.bones[static_cast<std::size_t>(i)];
        p.keys[static_cast<std::size_t>(i)] = gPart->keys[static_cast<std::size_t>(i)];
    }
    p.offset = gRackBack * units;
    if (!gRackPublished) {
        gRackQuietTicks = 0;
        gRackCallsSeen = engine::bone_pose::GetWeaponPartStats().calls;
    }
    engine::bone_pose::PublishWeaponPart(p);
    gRackPublished = true;
}

// The part back at its rest place and the stage cleared. A gun that isn't posed by itself gets one more pose with the
// part at rest first (`pose`; never while the process exits).
void StopRack(bool pose) {
    if (gRackPublished) {
        if (pose && gRackForced && gPart && gRackMesh != nullptr && ue3::runtime::Ready() && ue3::runtime::IsAlive(gRackMesh)) {
            PublishPart(gRackMesh, 0.0);
            ForceSkelUpdate(gRackMesh);
        }
        engine::bone_pose::ClearWeaponPart();
        gRackPublished = false;
    }
    gRack.Reset();
    gTestRack.active = false;
    gRackHandPeak = 0.0;
    gRackStrokeEnd.clear();
}

void RackStandsDown(std::string why) {
    StopRack(true);
    SetRackWhy(std::move(why));
}

void TickRack(hand_calibration::Vent const& settings, double now) {
    double const dt = gRackLastNow > 0.0 ? now - gRackLastNow : 0.0;
    gRackLastNow = now;
    if (!settings.animate) {
        RackStandsDown("off ([Vent] animate = 0)");
        return;
    }
    if (!engine::bone_pose::Installed()) {
        RackStandsDown("no pose writer (the UpdateSkelPose hook isn't in)");
        return;
    }
    if (engine::bone_pose::GetWeaponPartStats().broken) {
        RackStandsDown("the pose stage stopped after a fault");
        return;
    }
    ue3::Object* const mesh = weapon::HeldMesh();
    if (mesh == nullptr) {
        RackStandsDown("no verified gun in the right hand");
        gRackMesh = nullptr;
        return;
    }
    if (mesh != gRackMesh) {
        StopRack(true);
        gRackMesh = mesh;
        gRackForced = gRackFailed = false;
        gRackForcedMisses = 0;
    }
    ue3::Object const* const asset = player::SkeletalMesh(mesh);
    int const type = static_cast<int>(weapon::Type());
    std::string const setting = type >= 0 && type < 4 ? settings.parts[static_cast<std::size_t>(type)] : std::string("auto");
    if (!gPart || gPart->asset != asset || gPart->type != type || gPart->setting != setting ||
        (gPart->choice.bone >= 0 && engine::skeleton::RefBoneCount(mesh) != gPart->boneCount)) {
        StopRack(true);
        ResolvePart(mesh, asset, type, setting);
    }
    if (gPart->choice.bone < 0) {
        RackStandsDown(std::format("the {} model {} has no part to rack ({})", TypeName(type), gPart->mesh, gPart->choice.why));
        return;
    }
    if (gRackFailed) {
        RackStandsDown(std::format("the {} model {}'s pose can't be moved (no pose update reaches the stage, not even ForceSkelUpdate's)",
            TypeName(type), gPart->mesh));
        return;
    }
    // Back along the gun in the mesh's component space (the part's offset), and the model's units per meter of hand travel.
    auto const muzzle = weapon::Current();
    ik::Affine const meshToWorld = engine::skeleton::LocalToWorld(mesh);
    auto const worldToMesh = ik::InverseAffine(meshToWorld);
    if (!muzzle || !worldToMesh) {
        RackStandsDown("no barrel direction for the held gun");
        return;
    }
    ik::Vec3 const barrel = ik::TransformDirection(*worldToMesh, {muzzle->barrel.x, muzzle->barrel.y, muzzle->barrel.z});
    double const scale = ik::Length(ik::TransformDirection(meshToWorld, {1.0, 0.0, 0.0}));  // world units per model unit
    if (!(ik::Length(barrel) > 1e-9) || !std::isfinite(scale) || !(scale > 1e-6)) {
        RackStandsDown("the gun mesh's transform is degenerate");
        return;
    }
    gRackBack = ik::Normalize(barrel * -1.0, {1.0, 0.0, 0.0});
    auto const frame = view_anchor::Current();
    double const worldPerMeter = frame && frame->unitsPerMeter > 0.0 ? frame->unitsPerMeter : engine::kUnitsPerMeter;
    gRackUnitsPerMeter = worldPerMeter / scale;
    vnt::RackParams params;
    params.maxUnits = settings.partTravelUnits > 0.0 ? settings.partTravelUnits : gPart->choice.maxUnits;
    params.returnHz = settings.partReturnHz;
    params.damping = settings.partDamping;
    gRackMaxUnits = params.maxUnits;

    bool holding = gActive && gMachine.Holding();
    double target = holding ? vnt::RackTarget(gMachine.Travel(), gRackUnitsPerMeter, params.maxUnits) : 0.0;
    if (holding) {
        gRackHandPeak = std::max(gRackHandPeak, gMachine.Travel());
    }
    if (gTestRack.active && !holding) {
        double const t = now - gTestRack.start;
        if (t < kTestPullS + gTestRack.holdS) {
            holding = true;
            target = params.maxUnits * std::clamp(gTestRack.fraction, 0.0, 1.0) * std::clamp(t / kTestPullS, 0.0, 1.0);
            gRackStrokeEnd = "the console's test (no vent)";
        } else {
            gTestRack.active = false;
        }
    }
    vnt::RackEvent const event = gRack.Update(dt, holding, target, params);
    if (event == vnt::RackEvent::Home || (event == vnt::RackEvent::Released && gRack.AtRest())) {
        ++gRackStrokes;
        ++gRackStrokesTotal;
        std::string const end = gRackStrokeEnd.empty() ? std::string("let go") : gRackStrokeEnd;
        gRackLast = std::format("{:.1f} of {:.1f} units back, home in {:.0f} ms ({})", gRack.Peak(), params.maxUnits, gRack.ReturnSeconds() * 1000.0, end);
        if (gRackStrokesTotal <= 20 || gRackStrokesTotal % 20 == 0) {
            log::Info("vent: rack: the {}'s '{}' went {:.1f} units back ({:.2f} m of hand pull at {:.0f} units per meter; at most {:.1f}) and "
                      "snapped forward in {:.0f} ms: {}{}",
                TypeName(type), gPart->names[static_cast<std::size_t>(gPart->choice.bone)], gRack.Peak(), gRackHandPeak, gRackUnitsPerMeter,
                params.maxUnits, gRack.ReturnSeconds() * 1000.0, end, gRackForced ? " (the gun posed by ForceSkelUpdate)" : "");
        }
        gRackHandPeak = 0.0;
        gRackStrokeEnd.clear();
    }
    bool const moving = gRack.Holding() || gRack.Offset() > 0.0;
    if (moving) {
        gRackHomeUntil = now + 0.25;
    }
    if (!moving && now > gRackHomeUntil) {
        if (gRackPublished) {
            engine::bone_pose::ClearWeaponPart();  // home for 0.25 s: the stage's last writes put the part at rest
            gRackPublished = false;
        }
        SetRackWhy(std::format("ready: the {} model {}'s '{}' racks, up to {:.1f} units{}", TypeName(type), gPart->mesh,
            gPart->names[static_cast<std::size_t>(gPart->choice.bone)], params.maxUnits,
            gRackForced ? "; the gun is posed by ForceSkelUpdate while it moves" : ""));
        return;
    }
    PublishPart(mesh, gRack.Offset());
    // Is the gun posed by itself? The stage's calls for it since the last tick; none for 3 ticks: the engine's own
    // ForceSkelUpdate poses it (this tick's publish is written at once then), and if not even that reaches the stage, the
    // rack stands down for this gun.
    std::uint64_t const calls = engine::bone_pose::GetWeaponPartStats().calls;
    gRackQuietTicks = calls == gRackCallsSeen ? gRackQuietTicks + 1 : 0;
    gRackCallsSeen = calls;
    if (!gRackForced && gRackQuietTicks >= 3) {
        gRackForced = true;
        log::Info("vent: rack: the {}'s pose didn't update for 3 ticks while its part moves: SkeletalMeshComponent.ForceSkelUpdate poses it "
                  "each tick then{}",
            TypeName(type), gForceSkelUpdate != nullptr ? "" : " (NOT FOUND: the rack stands down for this gun)");
        if (gForceSkelUpdate == nullptr) {
            gRackFailed = true;
            return;
        }
    }
    if (gRackForced && gRackQuietTicks > 0) {
        ForceSkelUpdate(mesh);
        std::uint64_t const after = engine::bone_pose::GetWeaponPartStats().calls;
        gRackForcedMisses = after == gRackCallsSeen ? gRackForcedMisses + 1 : 0;
        gRackCallsSeen = after;
        if (gRackForcedMisses >= 3) {
            gRackFailed = true;
            log::Warn("vent: rack: ForceSkelUpdate on the {} didn't reach the pose stage 3 times: the rack stands down for this gun",
                TypeName(type));
        }
    }
}

#endif  // MEVR_GAME_LE1

} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE1)
    if (!ue3::runtime::Ready()) {
        return false;
    }
    gCoolDownEffect = gCoolDownEffectFn.Get();
    gIsSwitchingWeapons = gIsSwitchingWeaponsFn.Get();
    gForceSkelUpdate = gForceSkelUpdateFn.Get();
    bool const fields = gCurrentHeat.Get() != nullptr && gOverheatedField.Get() != nullptr;
    gStarted = fields;
    log::Info("vent: {}{}{}{}", fields ? "BioWeaponRanged.m_fCurrentHeat and m_bOverheated found" : "the heat fields are MISSING: the vent stays off",
        gCoolDownEffect != nullptr ? "; DoCoolDownEffect found" : "; DoCoolDownEffect not found (no effect on a vent)",
        gIsSwitchingWeapons != nullptr ? "" : "; BioPawn.IsSwitchingWeapons not found",
        gForceSkelUpdate != nullptr ? "" : "; SkeletalMeshComponent.ForceSkelUpdate not found (a gun not posed by itself won't rack)");
    return gStarted;
#else
    return false;
#endif
}

void Claim(Link const& link) {
    gClaimed = false;
    hand_calibration::Refresh();  // at most once a second
    vr::controllers::State const controllers = vr::controllers::Latest();
    bool const was = gOwnGrip;
    gOwnGrip = controllers.active && (gOwnGrip ? controllers.left.squeeze > kGripOff : controllers.left.squeeze >= kGripOn);
    bool fresh = false;
    if (link.leftGrip) {
        // The host's count of left squeezes that began away from the gun (the support grip's zone, which game/support_grip
        // hides while the hand is at the receiver of a hot gun): each new one, while it's still the current squeeze.
        std::uint32_t const count = *link.leftGrip >> 1;
        fresh = gHaveCount && count != gLastCount && (*link.leftGrip & 1u) != 0;
        gLastCount = count;
        gHaveCount = true;
    } else {
        fresh = gOwnGrip && !was && !support_grip::Held();
        if (!gToldOldGrip && gEnabled) {
            gToldOldGrip = true;
            log::Warn("vent: the host doesn't report the left grip (services older than v11): the vent follows our own reading of "
                      "the grip, and the game may see that squeeze too; close the game and redeploy the host");
        }
    }
    if (!fresh || !gEnabled || !gActive || !gOnGunValid || throwables::ClaimedLeft() || support_grip::Held() ||
        !controllers.left.gripValid) {
        return;
    }
    if (gMachine.InZone()) {
        gClaimed = true;
        gClaimQueue = true;
    }
}

bool ClaimedLeft() {
    return gClaimed;
}

namespace {

#if defined(MEVR_GAME_LE1)
// `mevr vent heat <f> hold`: the held gun's heat kept (journalled), so a test has time to squeeze.
void KeepHeat() {
    if (gHeldHeat < 0.0) {
        return;
    }
    if (ue3::Object* const weapon = weapon::HeldObject()) {
        if (float* const heat = gCurrentHeat.In<float>(weapon)) {
            *heat = static_cast<float>(gHeldHeat);
            write_journal::Wrote("Vent.testHeat", heat);
        }
    }
}

void TickVent(double now, Link const& link, hand_calibration::Vent const& settings) {
    if (!settings.enabled) {
        Inactive("off ([Vent] enabled = 0)");
        return;
    }
    if (!gStarted) {
        Inactive("waiting for the script fields");
        return;
    }
    auto const mode = head_tracking::GameMode();
    if (!mode || *mode != 0) {
        Inactive(std::format("not on foot (game mode {})", mode ? std::to_string(*mode) : std::string("unknown")));
        return;
    }
    vr::host_view::State const view = vr::host_view::Latest();
    if (!view.valid) {
        Inactive("no head-tracked world view (menu, flat screen or VR off)");
        return;
    }
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!controllers.active) {
        Inactive("the controllers aren't active");
        return;
    }
    ue3::Object* const weapon = weapon::HeldObject();
    if (weapon == nullptr) {
        Inactive("no ranged weapon in the right hand (holstered, or not verified yet)");
        return;
    }
    gActive = true;
    SetWhy("active");
    ReadHeat(weapon, settings);

    // The left hand in the gun's frame, as the pose writer measured it for the support grip (arm IK asked for it).
    auto const sample = arm_ik::SupportRequested() ? engine::bone_pose::LatestSupport() : std::nullopt;
    gOnGunValid = sample && sample->gunFrame && sample->distanceMeters >= 0.0 && sample->distanceMeters < kCrossedMeters &&
        controllers.left.gripValid;
    if (gOnGunValid) {
        gOnGun = {sample->alongMeters, sample->upMeters, sample->sideMeters};
    }

    vnt::Input input;
    input.now = now;
    input.onGunValid = gOnGunValid;
    input.onGun = gOnGun;
    input.newSqueeze = gClaimQueue;
    input.squeeze = controllers.left.squeeze;
    input.heat = gHeat;
    ue3::Object* const pawn = player::BioPawn(player::Controller());
    input.canVent = pawn != nullptr && !CallBool(pawn, gIsSwitchingWeapons);
    gClaimQueue = false;
    vnt::Step const step = gMachine.Update(input, settings.zone, gParams);
    if (step.entered) {
        bool const hot = vnt::Refusal(gHeat, gMachine.CooldownLeft(input.now), gParams) == nullptr;
        log::Info("vent: the left hand is at the receiver ({:.2f} m along the gun, {:.2f} above, {:.2f} beside; heat {}{})", gOnGun.along,
            gOnGun.up, gOnGun.side, Percent(gHeat), hot ? ": a squeeze and a pull back vents it" : ": nothing to vent; a squeeze is the two-handed grip");
        if (hot) {
            Pulse(link, kLeft, vnt::EdgeTick(settings.hapticAmplitude));
        }
    }
    if (step.left) {
        log::Info("vent: the left hand left the receiver ({:.2f} along, {:.2f} above, {:.2f} beside)", gOnGun.along, gOnGun.up, gOnGun.side);
    }
    switch (step.event) {
    case vnt::Event::Grabbed:
        ++gGrabs;
        Pulse(link, kLeft, ik::throwing::GrabClick(settings.hapticAmplitude));
        log::Info("vent: the left hand took the receiver (heat {}): pull it back and away, letting go, to vent", Percent(gHeat));
        break;
    case vnt::Event::Vented:
        DoVent(link, weapon, settings, step);
        gRackStrokeEnd = "vented";
        break;
    case vnt::Event::Refused: {
        ++gRefusals;
        Pulse(link, kLeft, ik::throwing::RefusedBuzz(settings.hapticAmplitude));
        std::string detail = step.why != nullptr ? step.why : "?";
        if (step.why != nullptr && std::string_view(step.why) == "cooldown") {
            detail += std::format(", {:.1f} s left", gMachine.CooldownLeft(input.now));
        } else if (step.why != nullptr && std::string_view(step.why) == "the gun is cool") {
            detail += std::format(" (heat {} under ventMinHeat {:.2f})", Percent(gHeat), settings.minHeat);
        }
        gLast = std::format("refused: {}", detail);
        log::Info("vent: a squeeze at the receiver: no vent ({})", detail);
        break;
    }
    case vnt::Event::Slow:
        ++gSlow;
        gLast = std::format("let go without a rack: {}", step.why != nullptr ? step.why : "?");
        gRackStrokeEnd = std::format("no vent, {}", step.why != nullptr ? step.why : "?");
        log::Info("vent: the left hand let go at {:.2f} m/s ({:.2f} m/s back along the gun, {:.2f} m pulled; at least {:.1f} m/s, "
                  "{:.0f}% back, {:.2f} m): no vent ({})",
            step.release.valid ? step.release.speed : 0.0, step.back, step.travel, settings.speedMin, 100.0 * gParams.backFraction,
            settings.travel, step.why != nullptr ? step.why : "?");
        break;
    case vnt::Event::Lapsed:
        ++gLapsed;
        gLast = "held too long";
        gRackStrokeEnd = "no vent, held too long";
        log::Info("vent: the left grip stayed on the receiver over {:.1f} s: a grip, not a rack; nothing when it opens", settings.holdMax);
        break;
    case vnt::Event::Lost:
        gLast = "lost";
        gRackStrokeEnd = "no vent, the hold was lost";
        log::Info("vent: the hold on the receiver is lost ({})", step.why != nullptr ? step.why : "?");
        break;
    case vnt::Event::None:
        break;
    }
}
#endif  // MEVR_GAME_LE1

} // namespace

void Tick(double deltaSeconds, Link const& link) {
    (void)deltaSeconds;  // the rack's spring runs on the steady clock
    hand_calibration::Vent const settings = hand_calibration::GetVent();
    gEnabled = settings.enabled;
    gParams.minSpeed = settings.speedMin;
    gParams.minTravel = settings.travel;
    gParams.maxHoldSeconds = settings.holdMax;
    gParams.cooldownSeconds = settings.cooldownS;
    gParams.minHeat = settings.minHeat;
#if defined(MEVR_GAME_LE1)
    double const now = SteadySeconds();
    KeepHeat();
    TickVent(now, link, settings);
    if (gStarted) {
        TickRack(settings, now);  // after the machine: its hold, its travel and how the stroke ended
    }
#else
    (void)link;
    Inactive("LE1 only");
#endif
}

bool MasksSupportZone() {
    if (!gEnabled || !gActive || !gOnGunValid || !gMachine.InZone()) {
        return false;
    }
    // A hot gun's receiver is the vent's; a cool one's is the two-handed grip as before.
    return gMachine.Holding() || vnt::Refusal(gHeat, gMachine.CooldownLeft(SteadySeconds()), gParams) == nullptr;
}

namespace {

std::string RackJson() {
    namespace json = vr::test_request;
    bool const part = gPart && gPart->choice.bone >= 0;
    return std::format("{{\"animate\": {}, \"mesh\": {}, \"part\": {}, \"partBones\": {}, \"source\": {}, \"maxUnits\": {}, "
                       "\"unitsPerMeter\": {}, \"offset\": {}, \"holding\": {}, \"published\": {}, \"forced\": {}, \"strokes\": {}, "
                       "\"why\": {}, \"last\": {}}}",
        hand_calibration::GetVent().animate ? "true" : "false", json::JsonString(gPart ? gPart->mesh : std::string()),
        json::JsonString(part ? gPart->names[static_cast<std::size_t>(gPart->choice.bone)] : std::string()),
        part ? static_cast<int>(gPart->choice.bones.size()) : 0, json::JsonString(part ? std::string(gPart->choice.source) : std::string()),
        json::JsonNumber(gRackMaxUnits), json::JsonNumber(gRackUnitsPerMeter), json::JsonNumber(gRack.Offset()),
        gRack.Holding() ? "true" : "false", gRackPublished ? "true" : "false", gRackForced ? "true" : "false", gRackStrokesTotal,
        json::JsonString(gRackWhy), json::JsonString(gRackLast));
}

} // namespace

std::string StatusJson() {
    namespace json = vr::test_request;
    double const now = SteadySeconds();
    return std::format("{{\"enabled\": {}, \"active\": {}, \"why\": {}, \"onGun\": {}, \"inZone\": {}, \"holding\": {}, \"masksSupportZone\": {}, "
                       "\"heat\": {}, \"heatRaw\": {}, \"overheated\": {}, \"cooldownLeft\": {}, \"vents\": {}, \"last\": {}, \"rack\": {}}}",
        gEnabled ? "true" : "false", gActive ? "true" : "false", json::JsonString(gWhy),
        gOnGunValid ? std::format("[{}, {}, {}]", json::JsonNumber(gOnGun.along), json::JsonNumber(gOnGun.up), json::JsonNumber(gOnGun.side))
                    : std::string("null"),
        gMachine.InZone() ? "true" : "false", gMachine.Holding() ? "true" : "false", MasksSupportZone() ? "true" : "false",
        json::JsonNumber(gHeat), json::JsonNumber(gHeatRaw), gOverheated ? "true" : "false", json::JsonNumber(gMachine.CooldownLeft(now)),
        gMachine.Vents(), json::JsonString(gLast), RackJson());
}

void LogStats() {
    if (!gEnabled && gVents == 0 && gGrabs == 0) {
        return;
    }
    log::Info("    vent: {}; heat {} (raw {:.3f}{}; max seen {:.3f}); the left hand {} the receiver{}; this period: {} grabs, {} vents, {} refused, "
              "{} slow, {} lapsed; last: {}",
        gWhy, Percent(gHeat), gHeatRaw, gOverheated ? ", overheated" : "", gMaxHeatSeen, gMachine.InZone() ? "at" : "off",
        gOnGunValid ? std::format(" ({:.2f} along, {:.2f} above, {:.2f} beside)", gOnGun.along, gOnGun.up, gOnGun.side) : std::string(" (no gun frame)"),
        gGrabs, gVents, gRefusals, gSlow, gLapsed, gLast);
    gGrabs = gVents = gRefusals = gSlow = gLapsed = 0;
    engine::bone_pose::WeaponPartStats const stage = engine::bone_pose::GetWeaponPartStats();
    log::Info("    vent: rack: {}; {} strokes this period ({} in all), last: {}; the pose stage: {} poses of the gun seen, {} written ({} not "
              "recomposed), {} rejected{}{}",
        gRackWhy, gRackStrokes, gRackStrokesTotal, gRackLast, stage.calls, stage.written, stage.stale, stage.rejected,
        stage.lastReject != nullptr ? std::format(" (last: {})", stage.lastReject) : std::string(), stage.broken ? ", STOPPED after a fault" : "");
    gRackStrokes = 0;
}

void PutPartBack(bool exiting) {
#if defined(MEVR_GAME_LE1)
    StopRack(!exiting);
#else
    (void)exiting;
#endif
    gRackMesh = nullptr;
}

std::string Command(std::string const& args) {
#if defined(MEVR_GAME_LE1)
    std::istringstream words(args);
    std::string verb;
    words >> verb;
    hand_calibration::Vent const settings = hand_calibration::GetVent();
    ue3::Object* const weapon = weapon::HeldObject();
    if (verb == "heat") {
        std::string value;
        std::string hold;
        words >> value >> hold;
        if (value == "off") {
            gHeldHeat = -1.0;
            return "mevr vent heat: no longer kept";
        }
        std::optional<double> const fraction = ini::Number(value);
        if (!fraction || weapon == nullptr) {
            return weapon == nullptr ? "mevr vent heat: no verified ranged weapon in the right hand (mevr weapon <type> draws one)"
                                     : "usage: mevr vent heat <0..1> [hold] | mevr vent heat off";
        }
        float* const heat = gCurrentHeat.In<float>(weapon);
        if (heat == nullptr) {
            return "mevr vent heat: BioWeaponRanged.m_fCurrentHeat not found";
        }
        double const raw = std::clamp(*fraction, 0.0, 1.0) * settings.maxHeat;
        *heat = static_cast<float>(raw);
        write_journal::Wrote("Vent.testHeat", heat);
        gHeldHeat = hold == "hold" ? raw : -1.0;
        return std::format("mevr vent heat: the {} at heat {:.3f} ({:.0f}% of ventMaxHeat {:.2f}){}", ue3::runtime::FullName(weapon), raw,
            100.0 * std::clamp(*fraction, 0.0, 1.0), settings.maxHeat, gHeldHeat >= 0.0 ? ", kept each tick until 'mevr vent heat off'" : "");
    }
    if (verb == "rack") {
        double fraction = 1.0;
        double seconds = 0.5;
        words >> fraction >> seconds;
        if (!gPart || gPart->choice.bone < 0 || gRackMesh == nullptr) {
            return std::format("mevr vent rack: no part to rack now ({})", gRackWhy);
        }
        gTestRack = {true, SteadySeconds(), std::clamp(std::isfinite(fraction) ? fraction : 1.0, 0.0, 1.0),
            std::clamp(std::isfinite(seconds) ? seconds : 0.5, 0.0, 30.0)};
        return std::format("mevr vent rack: the '{}' of {} goes {:.0f}% of its {:.1f} units back for {:.1f} s, then snaps forward",
            gPart->names[static_cast<std::size_t>(gPart->choice.bone)], gPart->mesh, 100.0 * gTestRack.fraction, gRackMaxUnits, gTestRack.holdS);
    }
    if (verb == "part" || verb.empty()) {
        if (!gPart) {
            return std::format("mevr vent part: no model looked at yet ({})", gRackWhy);
        }
        std::vector<int> parents;
        if (gRackMesh != nullptr && ue3::runtime::IsAlive(gRackMesh)) {
            if (auto const bones = engine::skeleton::ReadRefSkeleton(gRackMesh)) {
                parents = bones->parents;
            }
        }
        return std::format("{} ({}, [Vent] {} = {}): {}; back along the gun ({:.3f}, {:.3f}, {:.3f}) in the mesh's space, {:.0f} units per meter "
                           "of hand travel; {}; bones: {}",
            gPart->mesh, TypeName(gPart->type), gPart->type >= 0 && gPart->type < 4 ? hand_calibration::VentPartKey(gPart->type) : "?",
            gPart->setting,
            gPart->choice.bone >= 0 ? std::format("the part is '{}' ({} bones, {}), up to {:.1f} units", gPart->names[static_cast<std::size_t>(gPart->choice.bone)],
                                          gPart->choice.bones.size(), gPart->choice.source, gRackMaxUnits)
                                    : std::format("no part ({})", gPart->choice.why),
            gRackBack.x, gRackBack.y, gRackBack.z, gRackUnitsPerMeter, gRackWhy, BoneList(gPart->names, parents));
    }
    return "usage: mevr vent heat <0..1> [hold] | heat off | rack [fraction] [seconds] | part";
#else
    (void)args;
    return "mevr vent: LE1 only";
#endif
}

void Shutdown() {
    gMachine.ResetAll();
    gParams = {};
    gOnGun = {};
    gOnGunValid = false;
    gActive = false;
    gEnabled = false;
    gClaimed = gClaimQueue = false;
    gOwnGrip = false;
    gHaveCount = false;
    gLastCount = 0;
    gToldOldGrip = gToldNoHaptics = false;
    gHeatRaw = gHeat = -1.0;
    gOverheated = gWasOverheated = false;
    gMaxHeatSeen = 0.0;
    gGrabs = gVents = gRefusals = gSlow = gLapsed = 0;
    gLast = "none";
    gStarted = false;
    gWhy = "not started";
    gLoggedWhy.clear();
    gPart.reset();
    gPartLogged.clear();
    gRack.Reset();
    gRackMesh = nullptr;
    gRackPublished = false;
    gRackLastNow = gRackHomeUntil = 0.0;
    gRackCallsSeen = 0;
    gRackQuietTicks = gRackForcedMisses = 0;
    gRackForced = gRackFailed = false;
    gRackBack = {};
    gRackUnitsPerMeter = gRackMaxUnits = gRackHandPeak = 0.0;
    gRackStrokeEnd.clear();
    gRackWhy = "not started";
    gRackLoggedWhy.clear();
    gRackStrokes = gRackStrokesTotal = 0;
    gRackLast = "none";
    gTestRack = {};
    gHeldHeat = -1.0;
    gEffectLogged.clear();
#if defined(MEVR_GAME_LE1)
    gCoolDownEffect = gIsSwitchingWeapons = gForceSkelUpdate = nullptr;
#endif
}

} // namespace mevr::game::vent
