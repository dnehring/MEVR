// SPDX-License-Identifier: MIT
#include "game/SquadWeapons.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/SquadMenu.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/Player.hpp"
#include "game/SupportGrip.hpp"
#include "game/Throwables.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <format>
#include <limits>
#include <string>
#include <string_view>

namespace mevr::game::squad_weapons {
namespace {

namespace menu = vr::squad_menu;
namespace wheel = menu::wheel;
using menu::kMaxMembers;
using menu::kWeapons;

constexpr int kLeft = 0;
constexpr int kRight = 1;
// The left trigger's latch, as the pad mapping's (game/gamepad_mapping: the trigger as LB).
constexpr float kTriggerOn = 0.6f;
constexpr float kTriggerOff = 0.4f;
// Point at the chest: this far above the pawn's location (its collision centre), Unreal units.
constexpr double kChestUnits = 20.0;
// How long the game gets to show a given weapon in the squadmate's hands before we say it didn't.
constexpr double kResultSeconds = 3.0;
// Member details (health, quick slots) are re-read this often when the menu is closed; every tick open.
constexpr double kDetailsSeconds = 0.5;
constexpr float kPi = 3.14159265358979f;
constexpr float kDeg = kPi / 180.0f;

#if defined(MEVR_GAME_LE1)
// Our reflection layer (research/34): the fields research/19 read (BioEpicPawnBehavior.Squad +0x2F8,
// BioActorBehavior ActorGameName +0xFC and m_oActorType +0x18C, BioActorType.ActorGameName +0x6C,
// BioBaseSquad.Members +0x314; BioPawnBehavior.m_oEquipment +0x424, BioEquipment m_QuickSlotArray +0x94 and
// m_eQuickSlotCurrent +0xB8), found by name; engine/ReflectCheck compares each. A squad member's entry (what
// SquadOrderEquipWeapon 0xC77FD0 and the switch-weapon natives 0x9E0220/0x9E0240 index) is 0x74 bytes: the
// pawn at +0x48, the tactical action at +0x70, the switch weapon at +0x71; the natives read it, we don't.
ue3::ClassRef gBioWeaponRanged{"SFXGame.BioWeaponRanged"};
ue3::ClassRef gBioEquipment{"SFXGame.BioEquipment"};
ue3::ClassRef gBioPlayerInput{"SFXGame.BioPlayerInput"};
ue3::ClassRef gGameModeDefault{"SFXGame.SFXGameModeDefault"};
ue3::ClassRef gGameModeWeaponWheel{"SFXGame.SFXGameModeWeaponWheel"};
ue3::ClassRef gGuiManager{"SFXGame.MassEffectGuiManager"};
ue3::ClassRef gHudHandler{"SFXGame.BioSFHandler_HUD"};
ue3::FieldRef gBehavior{"SFXGame.BioPawn", "m_oBehavior"};
ue3::FieldRef gSquadOf{"SFXGame.BioEpicPawnBehavior", "Squad"};
ue3::FieldRef gEquipment{"SFXGame.BioPawnBehavior", "m_oEquipment"};
ue3::FieldRef gQuickSlots{"SFXGame.BioEquipment", "m_QuickSlotArray"};
ue3::FieldRef gQuickSlotCurrent{"SFXGame.BioEquipment", "m_eQuickSlotCurrent"};
ue3::FieldRef gActorGameName{"SFXGame.BioActorBehavior", "ActorGameName"};
ue3::FieldRef gActorType{"SFXGame.BioActorBehavior", "m_oActorType"};
ue3::FieldRef gTypeGameName{"SFXGame.BioActorType", "ActorGameName"};
ue3::FieldRef gTag{"Engine.Actor", "Tag"};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gPlayerInput{"Engine.PlayerController", "PlayerInput"};
ue3::FieldRef gCachedScaleFormManager{"Engine.PlayerController", "m_oCachedScaleFormManager"};
ue3::FieldRef gGameModeManager{"SFXGame.BioPlayerController", engine::adapter::kGameModeManager};
ue3::FieldRef gGameModes{"SFXGame.SFXGameModeManager", "GameModes"};
ue3::FieldRef gCurrentMode{"SFXGame.SFXGameModeManager", "CurrentMode"};
// The HUD's handler (MassEffectGuiManager.m_oSFHudHandler +0x1A4; BioSFHandler_HUD m_nShepardIndex +0x10C,
// m_nHench1Index +0x110, m_nHench2Index +0x114, nAngle +0x134, nLastAngle +0x138; research/19 section 11).
ue3::FieldRef gSfHudHandler{"SFXGame.MassEffectGuiManager", "m_oSFHudHandler"};
ue3::FieldRef gShepardIndex{"SFXGame.BioSFHandler_HUD", "m_nShepardIndex"};
ue3::FieldRef gHench1Index{"SFXGame.BioSFHandler_HUD", "m_nHench1Index"};
ue3::FieldRef gHench2Index{"SFXGame.BioSFHandler_HUD", "m_nHench2Index"};
ue3::FieldRef gAngle{"SFXGame.BioSFHandler_HUD", "nAngle"};
ue3::FieldRef gLastAngle{"SFXGame.BioSFHandler_HUD", "nLastAngle"};
ue3::FieldRef gHandlerPanel{"Engine.BioSFHandler", "oPanel"};
ue3::FieldRef gUsingGamepad{"Engine.BioSFPanel", "bUsingGamepad"};

ue3::FunctionRef gGetSquadSizeFn{"SFXGame.BioBaseSquad.GetSquadSize"};
ue3::FunctionRef gGetMemberFn{"SFXGame.BioBaseSquad.GetMember"};
ue3::FunctionRef gGetMemberSwitchWeaponFn{"SFXGame.BioBaseSquad.GetMemberSwitchWeapon"};
ue3::FunctionRef gSetMemberSwitchWeaponFn{"SFXGame.BioBaseSquad.SetMemberSwitchWeapon"};
ue3::FunctionRef gSquadOrderEquipWeaponFn{"SFXGame.BioPlayerController.SquadOrderEquipWeapon"};
ue3::FunctionRef gApplyTacticalOrdersFn{"SFXGame.BioPlayerController.ApplyTacticalOrders"};
ue3::FunctionRef gChangeToQuickSlotFn{"SFXGame.BioEquipment.ChangeToQuickSlot"};
ue3::FunctionRef gIsInCombatActionStateFn{"SFXGame.BioPawn.IsInCombatActionState"};
ue3::FunctionRef gGetRangedWeaponTypeFn{"SFXGame.BioWeaponRanged.GetRangedWeaponType"};
ue3::FunctionRef gGetClassFn{"SFXGame.BioPawnBehavior.GetClass"};
ue3::FunctionRef gGetPercentHealthFn{"SFXGame.BioEpicPawnBehavior.GetPercentHealth"};
ue3::FunctionRef gPauseGameFn{"Engine.WorldInfo.PauseGame"};
ue3::FunctionRef gEnterWeaponWheelFn{"SFXGame.SFXGameModeDefault.EnterWeaponWheel"};
ue3::FunctionRef gExitWeaponWheelFn{"SFXGame.SFXGameModeWeaponWheel.ExitWeaponWheel"};
ue3::FunctionRef gTryHolsterFn{"SFXGame.SFXGameModeDefault.TryHolster"};
ue3::FunctionRef gIsSwitchingWeaponsFn{"SFXGame.BioPawn.IsSwitchingWeapons"};
ue3::FunctionRef gGetScaleFormManagerFn{"Engine.PlayerController.GetScaleFormManager"};
ue3::FunctionRef gIsCombatEnabledFn{"SFXGame.BioPlayerInput.IsCombatEnabled"};

ue3::Function* gGetSquadSize = nullptr;
ue3::Function* gGetMember = nullptr;
ue3::Function* gGetMemberSwitchWeapon = nullptr;
ue3::Function* gSetMemberSwitchWeapon = nullptr;
ue3::Function* gSquadOrderEquipWeapon = nullptr;
ue3::Function* gApplyTacticalOrders = nullptr;
ue3::Function* gChangeToQuickSlot = nullptr;
ue3::Function* gIsInCombatActionState = nullptr;
ue3::Function* gGetRangedWeaponType = nullptr;
ue3::Function* gGetClass = nullptr;
ue3::Function* gGetPercentHealth = nullptr;
ue3::Function* gPauseGame = nullptr;
ue3::Function* gEnterWeaponWheel = nullptr;
ue3::Function* gExitWeaponWheel = nullptr;
ue3::Function* gTryHolster = nullptr;
ue3::Function* gIsSwitchingWeapons = nullptr;
// The game's wheel draws Shepard's weapon (EnterWeaponWheel does). If it was put away when the wheel
// opened, it goes back after the wheel closes (user, 2026-10-22), unless the right grip holds it then.
bool gWheelWasHolstered = false;
double gReholsterWait = -1.0;  // seconds waited for the drawn weapon; negative: not waiting
constexpr double kReholsterTimeout = 3.0;
ue3::Function* gGetScaleFormManager = nullptr;
ue3::Function* gIsCombatEnabled = nullptr;
#endif

bool gStarted = false;

// One squadmate (not Shepard), as read this tick. `key` is the pawn's address, for telling them apart
// from tick to tick only: pointers are fetched from the squad again every tick, never kept.
struct MemberInfo {
    std::uintptr_t key = 0;
    std::array<std::uintptr_t, kWeapons> weapons{};
    std::array<int, kWeapons> typeOfSlot{-1, -1, -1, -1};
    std::uint8_t carried = 0;
    int current = -1;
    int classId = -1;
    float health = -1.0f;
    std::string name = "SQUADMATE";
    std::string role;
    std::string initials = "?";
    std::string nameSource;
    int discipline = 0;
    menu::Target target;    // the chest, tracking space
    float angle = kPi;      // from the left hand's aim ray, radians
    float distance = -1.0f; // from the left hand, meters
};
std::array<MemberInfo, kMaxMembers> gMembers;
int gCount = 0;
std::array<int, kMaxMembers> gCardOrder{0, 1};  // card c shows gMembers[gCardOrder[c]]
std::array<std::uintptr_t, kMaxMembers> gOpenKeys{};
int gOpenCount = 0;
double gDetailsAge = 1e9;
std::string gLoggedSquad;

menu::Machine gMachine;
std::uint32_t gSerial = 0;
// The left trigger opens the wheel or menu (2026-10-22: never the left grip). Latched like the pad's;
// the squad has it only while gTriggerOwned (decided per pull: menu::OwnTrigger), and then the game sees
// no LB from it (AdjustPad).
bool gTrigger = false;
menu::TriggerOwner gTriggerOwner;
bool gTriggerOwned = false;
bool gToldTriggerSetting = false;
std::uintptr_t gLastUsed = 0;
bool gPausedByUs = false;
bool gPublishedOpen = false;
bool gEnabled = false;
bool gSupported = false;
bool gToldOldHost = false;
bool gToldGripLB = false;
bool gUsable = false;
int gPointedCard = -1;
std::string gWhy = "not started";
std::string gLoggedWhy;
std::string gLastResult;

struct Pending {
    std::uintptr_t key = 0;
    int weapon = -1;
    double age = 0.0;
    std::string who;
};
Pending gPending;

// style = native: the game's own weapon wheel, held open by the left trigger.
wheel::Machine gWheel;
bool gWheelPressA = false;       // the pad shows A this tick (confirming on release)
std::uint32_t gWheelSerial = 0;  // + 1 per opening (the host places its panel afresh)
wheel::Entry gWheelEntry;        // the HUD's highlighted entry, last tick
int gWheelMode = -1;             // the game's mode, last tick
int gWheelPointed = -1;          // the gMembers index pointed at when it opened
double gWheelOpenedAt = 0.0;     // seconds since the enter
bool gToldNoNative = false;
bool gStyleNative = false;
gamepad::WheelStick gWheelStick = gamepad::WheelStick::Right;

// Heartbeat counters.
unsigned gOpens = 0;
unsigned gConfirms = 0;
unsigned gCancels = 0;
unsigned gRefusals = 0;

void SetWhy(std::string why) {
    gWhy = std::move(why);
    if (gWhy != gLoggedWhy) {
        gLoggedWhy = gWhy;
        log::Info("squad: {}", gWhy);
    }
}

int CardOf(int member) {
    for (int c = 0; c < gCount; ++c) {
        if (gCardOrder[c] == member) {
            return c;
        }
    }
    return -1;
}

#if defined(MEVR_GAME_LE1)

std::string FullName(ue3::Object const* object) {
    return ue3::runtime::FullName(object);
}

ue3::Object* ObjectOf(ue3::FieldRef& field, ue3::Object const* object) {
    return object != nullptr ? field.Read<ue3::Object*>(object).value_or(nullptr) : nullptr;
}

// An FString field as UTF-8 (empty if unset or unresolved).
std::string StringOf(ue3::FieldRef& field, ue3::Object const* object) {
    ue3::FString const* const text = object != nullptr ? field.In<ue3::FString>(object) : nullptr;
    return text != nullptr && text->count > 0 ? ue3::memory::Narrow(*text) : std::string();
}

std::string Upper(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

// BioAttributesPawnParty.EBioPartyMemberClassBase (SFXGame.pcc): the henchmen have classes of their own,
// so the class says who it is. [inference from the enum names; the log shows the game's own strings]
char const* HenchmanByClass(int classId) {
    switch (classId) {
    case 6: return "LIARA";    // ASARI_SCIENTIST
    case 7: return "WREX";     // KROGAN_OLD_ONE (Krogan Battlemaster)
    case 8: return "GARRUS";   // TURIAN_SPECTRE (Turian Agent)
    case 9: return "TALI";     // QUARIAN_TINKER (Quarian Machinist)
    case 11: return "ASHLEY";  // WOMAN_VETERAN (Soldier)
    case 12: return "KAIDAN";  // MAN_THINKER (Sentinel)
    default: return nullptr;
    }
}

char const* RoleOf(int classId) {
    switch (classId) {
    case 0: case 10: case 11: return "SOLDIER";
    case 1: return "ENGINEER";
    case 2: return "ADEPT";
    case 3: return "INFILTRATOR";
    case 4: case 12: return "SENTINEL";
    case 5: return "VANGUARD";
    case 6: return "SCIENTIST";
    case 7: return "BATTLEMASTER";
    case 8: return "TURIAN AGENT";
    case 9: return "MACHINIST";
    default: return "SQUADMATE";
    }
}

// The badge colour: 0 combat, 1 tech, 2 biotic (the class's first discipline).
int DisciplineOf(int classId) {
    switch (classId) {
    case 1: case 3: case 9: return 1;
    case 2: case 4: case 5: case 6: case 12: return 2;
    default: return 0;
    }
}

// A henchman tag (Actor.Tag) as a name, if it is one. [inference: ME1's henchmen tags]
char const* HenchmanByTag(std::string const& tag) {
    std::string const t = Upper(tag);
    struct Pair {
        char const* part;
        char const* name;
    };
    static constexpr Pair kPairs[] = {{"ASHLEY", "ASHLEY"}, {"HUMANFEMALE", "ASHLEY"}, {"KAIDAN", "KAIDAN"},
        {"HUMANMALE", "KAIDAN"}, {"GARRUS", "GARRUS"}, {"TURIAN", "GARRUS"}, {"WREX", "WREX"}, {"KROGAN", "WREX"},
        {"TALI", "TALI"}, {"QUARIAN", "TALI"}, {"LIARA", "LIARA"}, {"ASARI", "LIARA"}};
    for (Pair const& p : kPairs) {
        if (t.find(p.part) != std::string::npos) {
            return p.name;
        }
    }
    return nullptr;
}

std::string InitialsOf(std::string const& name) {
    std::string out;
    bool start = true;
    for (char c : name) {
        if (std::isalpha(static_cast<unsigned char>(c)) != 0) {
            if (start && out.size() < 2) {
                out.push_back(c);
            }
            start = false;
        } else {
            start = true;
        }
    }
    if (out.size() < 2) {
        out.clear();
        for (char c : name) {
            if (std::isalpha(static_cast<unsigned char>(c)) != 0 && out.size() < 2) {
                out.push_back(c);
            }
        }
    }
    return out.empty() ? std::string("?") : Upper(out);
}

void Pulse(Link const& link, int hand, float seconds, float amplitude, float frequency) {
    double const strength = hand_calibration::GetSquad().hapticAmplitude;
    float const a = std::clamp(amplitude * static_cast<float>(strength / 0.5), 0.0f, 1.0f);
    if (!(a > 0.0f)) {
        return;
    }
    if (link.hapticHz != nullptr) {
        link.hapticHz(hand, seconds, a, frequency);
    } else if (link.haptic != nullptr) {
        link.haptic(hand, seconds, a);
    }
}
// Meta's haptics guide: a short sharp transient per highlight, a firmer click on confirm, softer
// and lower for a cancel or a refusal.
void TickHaptic(Link const& link, int hand) { Pulse(link, hand, 0.012f, 0.35f, 190.0f); }
void OpenHaptic(Link const& link) { Pulse(link, kLeft, 0.018f, 0.5f, 170.0f); }
void ConfirmHaptic(Link const& link) { Pulse(link, kLeft, 0.022f, 0.9f, 320.0f); }
void CancelHaptic(Link const& link) { Pulse(link, kLeft, 0.03f, 0.25f, 90.0f); }
void RefusedHaptic(Link const& link, int hand) { Pulse(link, hand, 0.08f, 0.25f, 60.0f); }

XrVector3f Rotate(XrQuaternionf const& q, XrVector3f const& v) {
    // v + 2w(q x v) + 2 q x (q x v)
    XrVector3f const u{q.x, q.y, q.z};
    auto const cross = [](XrVector3f const& a, XrVector3f const& b) {
        return XrVector3f{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    XrVector3f const t = cross(u, v);
    XrVector3f const t2{2.0f * t.x, 2.0f * t.y, 2.0f * t.z};
    XrVector3f const c = cross(u, t2);
    return {v.x + q.w * t2.x + c.x, v.y + q.w * t2.y + c.y, v.z + q.w * t2.z + c.z};
}

std::string WeaponList(std::uint8_t carried) {
    std::string out;
    for (int w = 0; w < kWeapons; ++w) {
        if ((carried & (1u << w)) != 0) {
            out += std::format("{}{}", out.empty() ? "" : ", ", menu::WeaponName(w));
        }
    }
    return out.empty() ? std::string("nothing") : out;
}

std::string Describe(MemberInfo const& m) {
    return std::format("{} ({}, class {}; health {}; in hand {}; carries {})", m.name, m.role.empty() ? "?" : m.role, m.classId,
        m.health < 0.0f ? std::string("unknown") : std::format("{:.0f}%", std::round(m.health * 10.0f) * 10.0f),
        menu::WeaponName(m.current), WeaponList(m.carried));
}

// Through ProcessEvent with the function's own parameter block (never a generated wrapper).
bool CallBool(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    return parms.ReturnBool().value_or(false);
}

int SquadSize(ue3::Object* squad) {
    ue3::Params parms(ue3::runtime::Get(), gGetSquadSize);
    ue3::runtime::Call(squad, gGetSquadSize, parms);
    return parms.Return<std::int32_t>().value_or(0);
}

ue3::Object* SquadMember(ue3::Object* squad, int index) {
    ue3::Params parms(ue3::runtime::Get(), gGetMember);
    parms.Set("nIndex", std::int32_t{index});
    ue3::runtime::Call(squad, gGetMember, parms);
    return parms.Return<ue3::Object*>().value_or(nullptr);
}

int SwitchWeaponOf(ue3::Object* squad, int index) {
    ue3::Params parms(ue3::runtime::Get(), gGetMemberSwitchWeapon);
    parms.Set("nIndex", std::int32_t{index});
    ue3::runtime::Call(squad, gGetMemberSwitchWeapon, parms);
    return parms.Return<std::uint8_t>().value_or(0);
}

int RangedType(ue3::Object* weapon) {
    if (weapon == nullptr || gGetRangedWeaponType == nullptr || !ue3::IsA(weapon, gBioWeaponRanged)) {
        return -1;
    }
    ue3::Params parms(ue3::runtime::Get(), gGetRangedWeaponType);
    ue3::runtime::Call(weapon, gGetRangedWeaponType, parms);
    std::optional<std::uint8_t> const type = parms.Return<std::uint8_t>();
    return type && *type <= 3 ? static_cast<int>(*type) : -1;
}

ue3::Object* QuickSlot(ue3::Object const* equipment, int i) {
    ue3::Object* const* const slot = gQuickSlots.At<ue3::Object*>(equipment, i);
    return slot != nullptr ? *slot : nullptr;
}

int CurrentQuickSlot(ue3::Object const* equipment) {
    return gQuickSlotCurrent.Read<std::uint8_t>(equipment).value_or(0xFF);
}

struct Game {
    ue3::Object* controller = nullptr;  // a BioPlayerController
    ue3::Object* player = nullptr;      // Shepard's BioPawn
    ue3::Object* squad = nullptr;       // a BioBaseSquad
    std::array<ue3::Object*, kMaxMembers> pawns{};  // this tick's squadmates (BioPawns), in squad order
    std::array<int, kMaxMembers> indices{};         // their squad indices
    int count = 0;
};

std::optional<Game> ReadGame(std::string& why) {
    if (!ue3::runtime::Ready()) {
        why = "our reflection layer is off (see the 'reflect:' log lines)";
        return std::nullopt;
    }
    ue3::Object* const pc = player::Controller();
    if (pc == nullptr || !player::IsBioPlayerController(pc)) {
        why = "no player controller";
        return std::nullopt;
    }
    Game game;
    game.controller = pc;
    ue3::Object* const pawn = player::BioPawn(pc);
    if (pawn == nullptr) {
        why = "no player pawn (vehicle or cutscene?)";
        return std::nullopt;
    }
    game.player = pawn;
    ue3::Object* const behavior = ObjectOf(gBehavior, pawn);
    game.squad = ObjectOf(gSquadOf, behavior);
    if (game.squad == nullptr) {
        why = "the player has no squad";
        return std::nullopt;
    }
    int const size = SquadSize(game.squad);
    for (int i = 0; i < size && game.count < kMaxMembers; ++i) {
        ue3::Object* const member = SquadMember(game.squad, i);
        if (member == nullptr || member == pawn || !player::IsBioPawn(member)) {
            continue;
        }
        game.pawns[game.count] = member;
        game.indices[game.count] = i;
        ++game.count;
    }
    return game;
}

void ReadName(ue3::Object* pawn, MemberInfo& m) {
    ue3::Object* const behavior = ObjectOf(gBehavior, pawn);
    std::string behaviorName;
    std::string typeName;
    if (behavior != nullptr) {
        behaviorName = StringOf(gActorGameName, behavior);
        typeName = StringOf(gTypeGameName, ObjectOf(gActorType, behavior));
    }
    std::string const tag = ue3::runtime::NameText(gTag.Read<ue3::Name>(pawn).value_or(ue3::Name{}));
    if (char const* byClass = HenchmanByClass(m.classId)) {
        m.name = byClass;
        m.nameSource = "its class";
    } else if (!behaviorName.empty()) {
        m.name = Upper(behaviorName);
        m.nameSource = "BioActorBehavior.ActorGameName";
    } else if (!typeName.empty()) {
        m.name = Upper(typeName);
        m.nameSource = "BioActorType.ActorGameName";
    } else if (char const* byTag = HenchmanByTag(tag)) {
        m.name = byTag;
        m.nameSource = "its tag";
    } else {
        m.name = "SQUADMATE";
        m.nameSource = "nothing readable";
    }
    if (m.name.size() > 23) {
        m.name.resize(23);
    }
    m.role = RoleOf(m.classId);
    m.discipline = DisciplineOf(m.classId);
    m.initials = InitialsOf(m.name);
    log::Info("squad: squadmate 0x{:X}: \"{}\" from {} (class {} = {}; ActorGameName \"{}\", actor type's \"{}\", tag \"{}\", {})",
        m.key, m.name, m.nameSource, m.classId, m.role, behaviorName, typeName, tag, FullName(pawn));
}

// The squadmate's details: class and name once, quick slots when they change, health.
void ReadDetails(ue3::Object* pawn, MemberInfo& m, bool fresh) {
    ue3::Object* const behavior = ObjectOf(gBehavior, pawn);
    if (fresh) {
        int classId = -1;
        if (behavior != nullptr && gGetClass != nullptr) {
            ue3::Params parms(ue3::runtime::Get(), gGetClass);
            ue3::runtime::Call(behavior, gGetClass, parms);
            classId = parms.Return<std::uint8_t>().value_or(0);
        }
        m.classId = classId;
        ReadName(pawn, m);
    }
    ue3::Object* const equipment = ObjectOf(gEquipment, behavior);
    if (equipment != nullptr) {
        std::array<std::uintptr_t, kWeapons> weapons{};
        for (int i = 0; i < kWeapons; ++i) {
            weapons[i] = reinterpret_cast<std::uintptr_t>(QuickSlot(equipment, i));
        }
        if (fresh || weapons != m.weapons) {
            m.weapons = weapons;
            m.carried = 0;
            bool matches = true;
            for (int i = 0; i < kWeapons; ++i) {
                m.typeOfSlot[i] = RangedType(QuickSlot(equipment, i));
                if (m.typeOfSlot[i] >= 0) {
                    m.carried = static_cast<std::uint8_t>(m.carried | (1u << m.typeOfSlot[i]));
                    matches = matches && m.typeOfSlot[i] == i;
                }
            }
            log::Info("squad: {}'s quick slots: {} ({})", m.name, WeaponList(m.carried),
                matches ? "slot = weapon type, as the weapon wheel assumes" : "NOT slot = weapon type");
        }
        int const slot = CurrentQuickSlot(equipment);
        m.current = slot >= 0 && slot < kWeapons ? m.typeOfSlot[slot] : -1;
    } else {
        m.carried = 0;
        m.current = -1;
    }
    m.health = -1.0f;
    if (behavior != nullptr && gGetPercentHealth != nullptr) {
        ue3::Params parms(ue3::runtime::Get(), gGetPercentHealth);
        ue3::runtime::Call(behavior, gGetPercentHealth, parms);
        float const health = parms.Return<float>().value_or(std::numeric_limits<float>::quiet_NaN());
        if (std::isfinite(health)) {
            m.health = std::clamp(health > 1.5f ? health / 100.0f : health, 0.0f, 1.0f);
        }
    }
}

void CallPauseGame(ue3::Object* world, bool pause) {
    ue3::Params parms(ue3::runtime::Get(), gPauseGame);
    parms.SetBool("bPause", pause);
    ue3::runtime::Call(world, gPauseGame, parms);
}

void PauseGame(Game const& game, bool pause) {
    ue3::Object* const world = ObjectOf(gWorldInfo, game.controller);
    if (world == nullptr || gPauseGame == nullptr) {
        return;
    }
    CallPauseGame(world, pause);  // what the weapon wheel's Activated/Deactivated call
    gPausedByUs = pause;
    log::Info("squad: the game {} (WorldInfo.PauseGame, as ME1's weapon wheel)", pause ? "paused" : "runs again");
}

// The menu closed some other way (the mode changed, the mod unloads) while it had paused the game.
void ResumeIfPaused(char const* why) {
    if (!gPausedByUs) {
        return;
    }
    gPausedByUs = false;
    ue3::Object* const world = ObjectOf(gWorldInfo, player::Controller());
    if (world == nullptr || gPauseGame == nullptr) {
        log::Warn("squad: the menu paused the game and closed ({}), but there's no world to unpause", why);
        return;
    }
    CallPauseGame(world, false);
    log::Info("squad: the game runs again ({})", why);
}

// What the weapon wheel does for one member when it closes (SFXGameModeWeaponWheel.Deactivated).
void Equip(Game const& game, int member, int weapon) {
    MemberInfo const& m = gMembers[member];
    ue3::Object* const pawn = game.pawns[member];
    int const index = game.indices[member];
    gLastUsed = m.key;
    if (weapon == m.current) {
        gLastResult = std::format("{} already holds the {}", m.name, menu::WeaponName(weapon));
        log::Info("squad: {}: nothing to do", gLastResult);
        return;
    }
    if ((m.carried & (1u << weapon)) == 0) {
        gLastResult = std::format("{} carries no {}", m.name, menu::WeaponName(weapon));
        log::Warn("squad: {}: not ordered", gLastResult);
        return;
    }
    if (m.typeOfSlot[weapon] != weapon) {
        log::Warn("squad: {}'s {} isn't in quick slot {} (the wheel passes the type as the slot, so do we)", m.name,
            menu::WeaponName(weapon), weapon);
    }
    ue3::Params order(ue3::runtime::Get(), gSquadOrderEquipWeapon);
    order.Set("nWeapon", static_cast<std::uint8_t>(weapon));
    order.Set("pPawn", pawn);
    ue3::runtime::Call(game.controller, gSquadOrderEquipWeapon, order);  // directly: never a generated wrapper
    int const recorded = SwitchWeaponOf(game.squad, index);
    if (recorded != weapon) {
        gLastResult = std::format("the game didn't take the order for {} (its squad entry says {})", m.name, recorded);
        log::Warn("squad: {}: SquadOrderEquipWeapon({}, {}) was refused (the pawn takes no orders now?)", gLastResult, weapon,
            m.name);
        return;
    }
    bool const shepardArmed = CallBool(game.player, gIsInCombatActionState);
    char const* path = nullptr;
    if (shepardArmed) {
        ue3::Params apply(ue3::runtime::Get(), gApplyTacticalOrders);
        ue3::runtime::Call(game.controller, gApplyTacticalOrders, apply);
        path = "SquadOrderEquipWeapon, then ApplyTacticalOrders (Shepard's weapon is out: the AI switches, as the wheel "
               "does in combat)";
    } else {
        ue3::Object* const equipment = ObjectOf(gEquipment, ObjectOf(gBehavior, pawn));
        if (equipment == nullptr || !ue3::IsA(equipment, gBioEquipment)) {
            gLastResult = std::format("{} has no equipment", m.name);
            log::Warn("squad: {}: not switched", gLastResult);
            return;
        }
        ue3::Params change(ue3::runtime::Get(), gChangeToQuickSlot);
        change.Set("Slot", static_cast<std::uint8_t>(recorded));
        ue3::runtime::Call(equipment, gChangeToQuickSlot, change);
        ue3::Params reset(ue3::runtime::Get(), gSetMemberSwitchWeapon);
        reset.Set("nIndex", std::int32_t{index});
        reset.Set("eWeapon", std::uint8_t{4});  // STW_None: applied
        ue3::runtime::Call(game.squad, gSetMemberSwitchWeapon, reset);
        path = "SquadOrderEquipWeapon, then ChangeToQuickSlot and SetMemberSwitchWeapon(4) (Shepard's weapon is away: at "
               "once, as the wheel does)";
    }
    gLastResult = std::format("{} ordered to the {} (was {})", m.name, menu::WeaponName(weapon), menu::WeaponName(m.current));
    log::Info("squad: {}: {}", gLastResult, path);
    gPending = {m.key, weapon, 0.0, m.name};
}

void FollowPending(double deltaSeconds) {
    if (gPending.key == 0) {
        return;
    }
    gPending.age += deltaSeconds;
    for (int i = 0; i < gCount; ++i) {
        if (gMembers[i].key == gPending.key && gMembers[i].current == gPending.weapon) {
            log::Info("squad: {} holds the {} after {:.2f} s", gPending.who, menu::WeaponName(gPending.weapon), gPending.age);
            gLastResult = std::format("{} holds the {}", gPending.who, menu::WeaponName(gPending.weapon));
            gPending = {};
            return;
        }
    }
    if (gPending.age > kResultSeconds) {
        int current = -1;
        for (int i = 0; i < gCount; ++i) {
            if (gMembers[i].key == gPending.key) {
                current = gMembers[i].current;
            }
        }
        log::Warn("squad: {} doesn't hold the {} {:.0f} s later (in hand: {})", gPending.who, menu::WeaponName(gPending.weapon),
            kResultSeconds, menu::WeaponName(current));
        gPending = {};
    }
}

// ---- The squad and the pointing (both styles) -----------------------------------------------------

struct Survey {
    int pointed = -1;    // gMembers index the left hand points at (within the cone), -1: none
    int nearest = -1;    // gMembers index nearest the left hand's ray, whatever the cone
    int preferred = -1;  // who the custom menu preselects when not pointing
    char const* preferredWhy = "";
};

// Reads the squad (names and classes once, quick slots and health now and then, every tick while
// `busy`), maps them into tracking space, and what the left hand points at.
Survey SurveySquad(Game const& game, double deltaSeconds, bool busy, bool newPull, vr::controllers::Hand const& left,
    vr::host_view::State const& view, hand_calibration::Squad const& settings) {
    Survey survey;
    gDetailsAge += deltaSeconds;
    bool const details = busy || newPull || gPending.key != 0 || gDetailsAge >= kDetailsSeconds;
    if (details) {
        gDetailsAge = 0.0;
    }
    for (int i = 0; i < game.count; ++i) {
        std::uintptr_t const key = reinterpret_cast<std::uintptr_t>(game.pawns[i]);
        bool const fresh = gMembers[i].key != key;
        if (fresh) {
            gMembers[i] = MemberInfo{};
            gMembers[i].key = key;
        }
        if (fresh || details) {
            ReadDetails(game.pawns[i], gMembers[i], fresh);
        }
    }
    for (int i = game.count; i < kMaxMembers; ++i) {
        gMembers[i] = MemberInfo{};
    }
    gCount = game.count;
    std::string squadText;
    for (int i = 0; i < gCount; ++i) {
        squadText += std::format("{}{}", i > 0 ? "; " : "", Describe(gMembers[i]));
    }
    if (squadText != gLoggedSquad) {
        gLoggedSquad = squadText;
        log::Info("squad: {}", gCount > 0 ? squadText : std::string("no squadmates"));
    }

    // Where they are, and what the left hand points at.
    auto const anchor = view_anchor::Current();
    XrVector3f const handOrigin = left.aim.position;
    XrVector3f const handDirection = Rotate(left.aim.orientation, {0.0f, 0.0f, -1.0f});
    std::array<menu::Target, kMaxMembers> targets{};
    for (int i = 0; i < gCount; ++i) {
        MemberInfo& m = gMembers[i];
        m.target.valid = false;
        m.angle = kPi;
        m.distance = -1.0f;
        std::optional<ue3::Vector> const location = anchor ? player::Location(game.pawns[i]) : std::nullopt;
        if (location) {
            ue3::Vector const& at = *location;
            m.target.position = engine::ToTracking(*anchor, engine::UnrealVector{at.x, at.y, at.z + kChestUnits});
            m.target.valid = std::isfinite(m.target.position.x) && std::isfinite(m.target.position.y) &&
                std::isfinite(m.target.position.z);
        }
        if (m.target.valid && left.aimValid) {
            m.angle = menu::AngleToRay(handOrigin, handDirection, m.target.position);
            XrVector3f const d{m.target.position.x - handOrigin.x, m.target.position.y - handOrigin.y,
                m.target.position.z - handOrigin.z};
            m.distance = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        }
        targets[i] = left.aimValid ? m.target : menu::Target{};
    }
    // The cards: the squadmate standing further left on the left (decided when the menu opens).
    if (!busy) {
        gCardOrder = {0, 1};
        if (gCount == 2 && gMembers[0].target.valid && gMembers[1].target.valid) {
            XrQuaternionf const& q = view.head.orientation;
            XrVector3f const right = Rotate(q, {1.0f, 0.0f, 0.0f});
            auto const side = [&](MemberInfo const& m) {
                XrVector3f const d{m.target.position.x - view.head.position.x, 0.0f, m.target.position.z - view.head.position.z};
                float const length = std::sqrt(d.x * d.x + d.z * d.z);
                return length > 1e-3f ? (d.x * right.x + d.z * right.z) / length : 0.0f;
            };
            if (side(gMembers[1]) < side(gMembers[0])) {
                gCardOrder = {1, 0};
            }
        }
    }
    survey.pointed = menu::PickPointed(handOrigin, handDirection, targets.data(), gCount,
        static_cast<float>(settings.pointConeDeg) * kDeg, static_cast<float>(settings.pointMaxM));
    survey.nearest = menu::NearestToRay(handOrigin, handDirection, targets.data(), gCount);
    for (int i = 0; i < gCount; ++i) {
        if (gMembers[i].key == gLastUsed && gLastUsed != 0) {
            survey.preferred = i;
        }
    }
    survey.preferredWhy = "the last squadmate you gave a weapon";
    if (survey.preferred < 0) {
        survey.preferred = survey.nearest >= 0 ? survey.nearest : 0;
        survey.preferredWhy = survey.nearest >= 0 ? "the one nearest your pointing" : "the first in the squad";
    }
    return survey;
}

void LogPull(Survey const& survey, hand_calibration::Squad const& settings) {
    log::Info("squad: left trigger pulled: {}",
        survey.pointed >= 0 ? std::format("pointing at {} ({:.1f} deg off, {:.1f} m)", gMembers[survey.pointed].name,
                                  gMembers[survey.pointed].angle / kDeg, gMembers[survey.pointed].distance)
        : survey.nearest >= 0 ? std::format("pointing at nobody (nearest {} at {:.0f} deg, cone {:.0f})", gMembers[survey.nearest].name,
                                    gMembers[survey.nearest].angle / kDeg, settings.pointConeDeg)
                              : std::string("no squadmate in view"));
}

// ---- style = native: the game's own weapon wheel -------------------------------------------------

std::optional<unsigned char> CurrentMode(ue3::Object* controller) {
    ue3::Object* const manager = ObjectOf(gGameModeManager, controller);
    if (manager == nullptr) {
        return std::nullopt;
    }
    return gCurrentMode.Read<std::uint8_t>(manager);
}

ue3::Object* FindMode(ue3::Object* controller, ue3::ClassRef& cls) {
    ue3::Object* const manager = ObjectOf(gGameModeManager, controller);
    if (manager == nullptr || cls.Get() == nullptr) {
        return nullptr;
    }
    ue3::TArray<ue3::Object*> const* const modes = gGameModes.In<ue3::TArray<ue3::Object*>>(manager);
    for (int i = 0; modes != nullptr && i < modes->Count(); ++i) {
        ue3::Object* const mode = modes->data[i];
        if (mode != nullptr && ue3::IsA(mode, cls)) {
            return mode;
        }
    }
    return nullptr;
}

// The HUD's script handler (the console HUD movie's, which draws and steers the wheel), or nullptr.
ue3::Object* HudHandler(ue3::Object* controller) {
    ue3::Object* manager = nullptr;
    if (gGetScaleFormManager != nullptr) {
        ue3::Params parms(ue3::runtime::Get(), gGetScaleFormManager);
        ue3::runtime::Call(controller, gGetScaleFormManager, parms);  // directly: never a generated wrapper
        manager = parms.Return<ue3::Object*>().value_or(nullptr);
    }
    if (manager == nullptr) {
        manager = ObjectOf(gCachedScaleFormManager, controller);
    }
    if (manager == nullptr || !ue3::IsA(manager, gGuiManager)) {
        return nullptr;
    }
    ue3::Object* const hud = ObjectOf(gSfHudHandler, manager);
    return hud != nullptr && ue3::IsA(hud, gHudHandler) ? hud : nullptr;
}

int IntOf(ue3::FieldRef& field, ue3::Object const* object, int fallback = -1) {
    return object != nullptr ? field.Read<std::int32_t>(object).value_or(fallback) : fallback;
}

// Whether the player may fight here (EnterWeaponWheel returns at once in a hub where it's false). True
// when it can't be told.
bool CombatEnabled(Game const& game) {
    ue3::Object* const input = ObjectOf(gPlayerInput, game.controller);
    if (gIsCombatEnabled == nullptr || input == nullptr || !ue3::IsA(input, gBioPlayerInput)) {
        return true;
    }
    return CallBool(input, gIsCombatEnabled);
}

bool CallNoParms(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);  // directly: never a generated wrapper
    return true;
}

// A wheel entry as the squad knows it.
struct Pick {
    bool valid = false;
    int squadIndex = -1;  // BioBaseSquad index
    int member = -1;      // gMembers index, -1 for Shepard
    int weapon = -1;      // ESwitchToWeapon
    bool carried = false;
    bool inHand = false;  // the weapon they hold now
    bool recorded = false;  // their squad entry's switch weapon is it (queued)
    std::string name;
};

std::uint8_t ShepardCarried(Game const& game, int& current) {
    std::uint8_t carried = 0;
    current = -1;
    ue3::Object* const equipment = ObjectOf(gEquipment, ObjectOf(gBehavior, game.player));
    if (equipment == nullptr) {
        return 0;
    }
    std::array<int, kWeapons> typeOfSlot{-1, -1, -1, -1};
    for (int i = 0; i < kWeapons; ++i) {
        typeOfSlot[i] = RangedType(QuickSlot(equipment, i));
        if (typeOfSlot[i] >= 0) {
            carried = static_cast<std::uint8_t>(carried | (1u << typeOfSlot[i]));
        }
    }
    int const slot = CurrentQuickSlot(equipment);
    current = slot >= 0 && slot < kWeapons ? typeOfSlot[slot] : -1;
    return carried;
}

Pick PickOf(Game const& game, ue3::Object const* hud, wheel::Entry entry) {
    Pick pick;
    if (hud == nullptr || !entry.Valid()) {
        return pick;
    }
    pick.squadIndex = entry.member == 0 ? IntOf(gShepardIndex, hud) : entry.member == 1 ? IntOf(gHench1Index, hud) : IntOf(gHench2Index, hud);
    pick.weapon = wheel::WeaponOfSlot(entry.slot);
    if (pick.squadIndex < 0 || pick.squadIndex >= SquadSize(game.squad) || pick.weapon < 0) {
        return pick;  // a henchman slot the squad doesn't have (one squadmate, or the HUD's indices stale)
    }
    if (entry.member == 0) {
        int current = -1;
        pick.carried = (ShepardCarried(game, current) & (1u << pick.weapon)) != 0;
        pick.inHand = current == pick.weapon;
        pick.name = "SHEPARD";
    } else {
        for (int i = 0; i < game.count; ++i) {
            if (game.indices[i] == pick.squadIndex) {
                pick.member = i;
            }
        }
        if (pick.member < 0) {
            return pick;  // not one of the squadmates we read (the HUD's indices are stale?)
        }
        MemberInfo const& m = gMembers[pick.member];
        pick.carried = (m.carried & (1u << pick.weapon)) != 0;
        pick.inHand = m.current == pick.weapon;
        pick.name = m.name;
    }
    pick.recorded = SwitchWeaponOf(game.squad, pick.squadIndex) == pick.weapon;
    pick.valid = true;
    return pick;
}

// The wheel's member for a squadmate (1 or 2: the HUD's first or second henchman), or -1.
int WheelMemberOf(Game const& game, ue3::Object const* hud, int member) {
    if (hud == nullptr || member < 0 || member >= game.count) {
        return -1;
    }
    int const squadIndex = game.indices[member];
    return squadIndex == IntOf(gHench1Index, hud) ? 1 : squadIndex == IntOf(gHench2Index, hud) ? 2 : -1;
}

std::string Recorded(Game const& game) {
    std::string out;
    int const size = SquadSize(game.squad);
    for (int i = 0; i < size; ++i) {
        int const recorded = SwitchWeaponOf(game.squad, i);
        if (recorded >= 0 && recorded < kWeapons) {
            ue3::Object* const member = SquadMember(game.squad, i);
            std::string name = member == game.player ? std::string("Shepard") : std::format("member {}", i);
            for (int m = 0; m < gCount; ++m) {
                if (game.indices[m] == i) {
                    name = gMembers[m].name;
                }
            }
            out += std::format("{}{}: {}", out.empty() ? "" : ", ", name, menu::WeaponName(recorded));
        }
    }
    return out.empty() ? std::string("nothing") : out;
}

Pick gWheelGiven;  // what the last confirm gave
bool gWheelClicked = false;  // this opening's confirm click was given

void Publish(Link const& link, bool open);

// Gives the game's wheel back if we hold it (the menu turned off, the style changed, the mod unloading).
void ReleaseWheel(char const* why) {
    if (!gWheel.Holding()) {
        gWheel.Reset();
        gWheelPressA = false;
        return;
    }
    gWheel.Reset();
    gWheelPressA = false;
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr || !player::IsBioPlayerController(controller)) {
        return;
    }
    auto const mode = CurrentMode(controller);
    if (mode && *mode == 3) {
        bool const called = CallNoParms(FindMode(controller, gGameModeWeaponWheel), gExitWeaponWheel);
        log::Info("squad: the game's weapon wheel let go ({}){}", why, called ? ": closed (ExitWeaponWheel)" : ": no wheel mode to close");
    }
}

void TickNative(double deltaSeconds, Link const& link, hand_calibration::Squad const& settings,
    vr::controllers::State const& controllers, bool newPull) {
    // Our own menu stays shut.
    if (gMachine.CurrentPhase() != menu::Phase::Idle) {
        log::Info("squad: menu closed (style is native now)");
        ResumeIfPaused("style is native now");
        gMachine.Reset();
    }
    Publish(link, false);
    gWheelStick = settings.wheelStick;

    std::string why;
    auto const game = ReadGame(why);
    auto const mode = game ? CurrentMode(game->controller) : std::nullopt;
    bool const wheelActive = mode && *mode == 3;
    bool const onFoot = mode && *mode == 0;
    vr::host_view::State const view = vr::host_view::Latest();
    bool const usable = game.has_value() && view.valid && controllers.active && !support_grip::Held();

    Survey survey;
    if (game) {
        survey = SurveySquad(*game, deltaSeconds, gWheel.Holding(), newPull, controllers.left, view, settings);
    } else {
        gCount = 0;
    }
    if (newPull && game && view.valid) {
        LogPull(survey, settings);
    }

    // What the wheel highlights (any time it's up: also when LB opened it).
    ue3::Object* const hud = (wheelActive || gWheel.Holding()) && game ? HudHandler(game->controller) : nullptr;
    std::optional<std::int32_t> const hudAngle = hud != nullptr ? gAngle.Read<std::int32_t>(hud) : std::nullopt;
    wheel::Entry const entry = wheelActive && hudAngle ? wheel::EntryAtStickAngle(*hudAngle) : wheel::Entry{};
    Pick const pick = game && wheelActive ? PickOf(*game, hud, entry) : Pick{};

    wheel::Input in;
    in.dt = static_cast<float>(deltaSeconds);
    in.usable = usable;
    in.triggerDown = gTrigger;
    in.newPull = newPull;
    in.pointed = survey.pointed >= 0;
    bool const combat = game && onFoot ? CombatEnabled(*game) : true;
    in.onFoot = onFoot && combat;
    in.wheelActive = wheelActive;
    in.confirmOnRelease = settings.confirmOnRelease;
    in.highlightValid = pick.valid;
    in.highlightCarried = pick.valid && pick.carried && !pick.inHand;  // their weapon in hand: nothing to give
    in.choiceRecorded = pick.valid && pick.recorded;
    in.b = controllers.right.secondary;
    wheel::Step const step = gWheel.Update(in);
    gWheelPressA = step.holdA;

    if (newPull && onFoot && !combat) {
        log::Info("squad: combat is off here (a hub): the game has no weapon wheel to open");
    } else if (newPull && !step.enter && view.valid) {  // (no world view: Halcyon's menu, whose pointer clicks with it)
        log::Info("squad: the left trigger can't open the wheel now ({})", !usable ? (support_grip::Held() ? "both hands hold the gun"
                                                                                       : "no world view or controllers")
                                                                         : "not on foot");
    }
    if (step.enter) {
        ++gWheelSerial;
        ++gOpens;
        gWheelPointed = step.openedPointing && survey.pointed >= 0 ? survey.pointed : -1;
        gWheelOpenedAt = 0.0;
        gWheelClicked = false;
        ue3::Object* const defaultMode = game ? FindMode(game->controller, gGameModeDefault) : nullptr;
        gWheelWasHolstered = game && !CallBool(game->player, gIsInCombatActionState);
        gReholsterWait = -1.0;
        bool const called = CallNoParms(defaultMode, gEnterWeaponWheel);
        log::Info("squad: wheel #{}: {} the game's weapon wheel{} (SFXGameModeDefault.EnterWeaponWheel, what holding LB does; "
                  "the left trigger, at once)",
            gWheelSerial, called ? "opening" : "can't open (no SFXGameModeDefault)",
            gWheelPointed >= 0 ? std::format(", pointed at {}", gMembers[gWheelPointed].name) : std::string(", pointed at nobody"));
    }
    if (gWheel.Holding()) {
        gWheelOpenedAt += deltaSeconds;
    }
    if (step.opened) {
        log::Info("squad: wheel #{} open after {:.2f} s (game mode 3; the game pauses itself); HUD handler {}{}", gWheelSerial,
            gWheelOpenedAt, hud != nullptr ? FullName(hud) : std::string("not found (no highlight, no preselect)"),
            ObjectOf(gHandlerPanel, hud) != nullptr
                ? std::format(", its movie {} the gamepad",
                      gUsingGamepad.ReadBool(ObjectOf(gHandlerPanel, hud)).value_or(false) ? "uses" : "doesn't use")
                : std::string());
        OpenHaptic(link);
        // Pointing preselects: the HUD keeps its last stick angle while the stick is centred, so set it to
        // the pointed squadmate's weapon in hand. [verify] in the headset
        int const member = game ? WheelMemberOf(*game, hud, gWheelPointed) : -1;
        if (settings.wheelPreselect && gWheelPointed >= 0 && member > 0) {
            int const weapon = gMembers[gWheelPointed].current;
            int const angle = wheel::StickAngleOf({member, wheel::SlotOfWeapon(weapon)});
            if (angle >= 0) {
                if (std::int32_t* const last = gLastAngle.In<std::int32_t>(hud)) {
                    *last = angle;
                    write_journal::Wrote("Squad.HUD nLastAngle", last);
                }
                if (std::int32_t* const now = gAngle.In<std::int32_t>(hud)) {
                    *now = angle;
                    write_journal::Wrote("Squad.HUD nAngle", now);
                }
                log::Info("squad: wheel #{}: preselected {}'s {} (wheel member {}, slot {}: the HUD's kept stick angle {} deg)", gWheelSerial,
                    gMembers[gWheelPointed].name, menu::WeaponName(weapon), member, wheel::SlotOfWeapon(weapon), angle);
            } else {
                log::Info("squad: wheel #{}: nothing to preselect for {} (weapon in hand unknown)", gWheelSerial, gMembers[gWheelPointed].name);
            }
        } else if (settings.wheelPreselect && gWheelPointed >= 0) {
            log::Info("squad: wheel #{}: {} isn't on the HUD's wheel (its henchmen indices {} and {}): no preselect", gWheelSerial,
                gMembers[gWheelPointed].name, IntOf(gHench1Index, hud), IntOf(gHench2Index, hud));
        }
    }
    if (step.openFailed) {
        log::Warn("squad: wheel #{}: the game didn't enter its weapon wheel within {:.1f} s (combat disabled here, a tutorial, "
                  "or a mode in the way: game mode {})", gWheelSerial, wheel::kOpenTimeout, mode ? std::to_string(*mode) : std::string("?"));
        RefusedHaptic(link, kLeft);
    }
    // The highlight: a tick in the steering hand on each change; a soft refusal on a weapon they don't carry.
    if (wheelActive && !(entry == gWheelEntry)) {
        if (entry.Valid()) {
            bool const leftSteers = settings.wheelStick == gamepad::WheelStick::Left ||
                (settings.wheelStick == gamepad::WheelStick::Either &&
                    controllers.left.stickX * controllers.left.stickX + controllers.left.stickY * controllers.left.stickY >
                        controllers.right.stickX * controllers.right.stickX + controllers.right.stickY * controllers.right.stickY);
            int const hand = leftSteers ? kLeft : kRight;
            if (pick.valid && pick.carried) {
                TickHaptic(link, hand);
            } else {
                RefusedHaptic(link, hand);
            }
            log::Info("squad: wheel highlight: member {} slot {} = {}'s {}{}", entry.member, entry.slot,
                pick.valid ? pick.name : std::string("?"), menu::WeaponName(wheel::WeaponOfSlot(entry.slot)),
                !pick.valid ? " (not one of the squad we read)" : !pick.carried ? " (not carried)" : pick.inHand ? " (in hand)" : "");
        }
        gWheelEntry = entry;
    }
    if (!wheelActive) {
        gWheelEntry = {};
    }
    if (step.confirmed) {
        ++gConfirms;
        log::Info("squad: wheel #{}: trigger let go: giving {} the {} (A for the game: it queues the highlighted entry)", gWheelSerial,
            pick.name, menu::WeaponName(pick.weapon));
        ConfirmHaptic(link);
        gWheelClicked = true;
        gWheelGiven = pick;
    }
    if (step.exit && !step.cancelled && !gWheelClicked) {
        ConfirmHaptic(link);  // closing on something queued by hand (A): the click all the same
        gWheelClicked = true;
    }
    if (step.exit && game) {
        bool const called = CallNoParms(FindMode(game->controller, gGameModeWeaponWheel), gExitWeaponWheel);
        log::Info("squad: wheel #{}: {} (ExitWeaponWheel, what letting go of LB does); queued: {}", gWheelSerial,
            called ? "closing" : "can't close (no SFXGameModeWeaponWheel)", Recorded(*game));
    }
    if (step.cancelled) {
        ++gCancels;
        log::Info("squad: wheel #{}: {}", gWheelSerial, controllers.right.secondary ? "B: closed without giving anything"
                                                          : "let go with nothing new to give");
        CancelHaptic(link);
        gWheelClicked = true;  // no click on a retried exit either
    }
    if (step.closed || step.closedByGame) {
        log::Info("squad: wheel #{} closed{} after {:.2f} s", gWheelSerial, step.closedByGame ? " by the game (LB, the pause menu...)" : "",
            gWheelOpenedAt);
        if (gWheelGiven.valid && gWheelGiven.member >= 0 && game) {
            MemberInfo const& m = gMembers[gWheelGiven.member];
            gLastUsed = m.key;
            gPending = {m.key, gWheelGiven.weapon, 0.0, m.name};
            gLastResult = std::format("{} given the {} (the game's wheel)", m.name, menu::WeaponName(gWheelGiven.weapon));
        }
        gWheelGiven = {};
        if (gWheelWasHolstered && gTryHolster != nullptr) {
            gReholsterWait = 0.0;  // put Shepard's weapon back once the game has finished drawing it
        }
        gWheelWasHolstered = false;
    }
    if (gReholsterWait >= 0.0 && game) {
        gReholsterWait += deltaSeconds;
        bool const armed = CallBool(game->player, gIsInCombatActionState);
        bool const switching = gIsSwitchingWeapons != nullptr && CallBool(game->player, gIsSwitchingWeapons);
        if (controllers.right.squeeze > 0.5f) {
            log::Info("squad: wheel #{}: Shepard's weapon stays out (the right grip holds it)", gWheelSerial);
            gReholsterWait = -1.0;
        } else if (armed && !switching) {
            ue3::Object* const defaultMode = FindMode(game->controller, gGameModeDefault);
            if (defaultMode != nullptr) {
                CallNoParms(defaultMode, gTryHolster);  // directly: never a generated wrapper
                log::Info("squad: wheel #{}: Shepard's weapon was away before the wheel, so it goes back "
                          "(SFXGameModeDefault.TryHolster, {:.2f} s after closing)", gWheelSerial, gReholsterWait);
            }
            gReholsterWait = -1.0;
        } else if (gReholsterWait > kReholsterTimeout) {
            log::Info("squad: wheel #{}: Shepard's weapon wasn't drawn within {:.0f} s of closing: nothing to put back",
                gWheelSerial, kReholsterTimeout);
            gReholsterWait = -1.0;
        }
    }
    if (step.exitStuck) {
        log::Warn("squad: wheel #{}: the game's weapon wheel didn't close after two ExitWeaponWheel calls: let go of it", gWheelSerial);
    }
    if (game) {
        FollowPending(deltaSeconds);
    }
    gWheelMode = mode ? *mode : -1;
    SetWhy(gWheel.Holding() ? std::format("the game's wheel: {}", wheel::PhaseName(gWheel.CurrentPhase()))
           : !game ? why
           : !onFoot ? std::format("ready (game mode {})", mode ? std::to_string(*mode) : std::string("?"))
                     : std::string("ready (native)"));
}

#endif  // MEVR_GAME_LE1

void Publish(Link const& link, bool open) {
    if (link.setMenu == nullptr) {
        return;
    }
    if (!open && !gPublishedOpen) {
        return;
    }
    hand_calibration::Squad const settings = hand_calibration::GetSquad();
    menu::View view;
    view.serial = gSerial;
    view.open = open;
    view.confirmOnRelease = settings.confirmOnRelease;
    view.count = static_cast<std::int8_t>(gCount);
    view.selected = static_cast<std::int8_t>(gMachine.Selected());
    view.highlighted = static_cast<std::int8_t>(gMachine.Highlighted());
    view.distance = static_cast<float>(settings.panelDistanceM);
    view.width = static_cast<float>(settings.panelWidthM);
    for (int c = 0; c < gCount && c < kMaxMembers; ++c) {
        MemberInfo const& m = gMembers[gCardOrder[c]];
        menu::MemberView& out = view.members[c];
        std::snprintf(out.name, sizeof out.name, "%s", m.name.c_str());
        std::snprintf(out.role, sizeof out.role, "%s", m.role.c_str());
        std::snprintf(out.initials, sizeof out.initials, "%s", m.initials.c_str());
        out.discipline = static_cast<std::int8_t>(m.discipline);
        out.current = static_cast<std::int8_t>(m.current);
        out.carried = m.carried;
        out.pointed = c == gPointedCard ? 1 : 0;
        out.health = m.health;
    }
    link.setMenu(&view);
    gPublishedOpen = open;
}

#if defined(MEVR_GAME_LE1)
void ReleaseWheel(char const* why);
#endif

void PublishWheelPanel(Link const& link, hand_calibration::Squad const& settings) {
    if (link.setWheelPanel == nullptr) {
        return;
    }
    wheel::Panel panel;
    panel.serial = gWheelSerial;
    panel.enabled = settings.wheelPanel;
    panel.modOpen = gWheel.Holding();
    panel.weaponPlace = static_cast<std::int8_t>(settings.weaponWheelAt);
    panel.powerPlace = static_cast<std::int8_t>(settings.powerWheelAt);
    panel.distance = static_cast<float>(settings.panelDistanceM);
    panel.width = static_cast<float>(settings.panelWidthM);
    for (int i = 0; i < 4; ++i) {
        panel.crop[i] = static_cast<float>(settings.wheelCrop[i]);
    }
    link.setWheelPanel(&panel);
}

void Inactive(std::string why, Link const& link) {
    if (gMachine.CurrentPhase() != menu::Phase::Idle) {
        log::Info("squad: menu closed ({})", why);
    }
#if defined(MEVR_GAME_LE1)
    ResumeIfPaused(why.c_str());
    ReleaseWheel(why.c_str());
#endif
    gMachine.Reset();
    gUsable = false;
    Publish(link, false);
    SetWhy(std::move(why));
}

} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE1)
    struct Want {
        ue3::Function** slot;
        ue3::FunctionRef* ref;
    };
    Want const wants[] = {
        {&gGetSquadSize, &gGetSquadSizeFn},
        {&gGetMember, &gGetMemberFn},
        {&gGetMemberSwitchWeapon, &gGetMemberSwitchWeaponFn},
        {&gSetMemberSwitchWeapon, &gSetMemberSwitchWeaponFn},
        {&gSquadOrderEquipWeapon, &gSquadOrderEquipWeaponFn},
        {&gApplyTacticalOrders, &gApplyTacticalOrdersFn},
        {&gChangeToQuickSlot, &gChangeToQuickSlotFn},
        {&gIsInCombatActionState, &gIsInCombatActionStateFn},
        {&gGetRangedWeaponType, &gGetRangedWeaponTypeFn},
        {&gGetClass, &gGetClassFn},
        {&gGetPercentHealth, &gGetPercentHealthFn},
        {&gPauseGame, &gPauseGameFn},
        {&gEnterWeaponWheel, &gEnterWeaponWheelFn},
        {&gExitWeaponWheel, &gExitWeaponWheelFn},
        {&gTryHolster, &gTryHolsterFn},
        {&gIsSwitchingWeapons, &gIsSwitchingWeaponsFn},
        {&gGetScaleFormManager, &gGetScaleFormManagerFn},
        {&gIsCombatEnabled, &gIsCombatEnabledFn},
    };
    std::string missing;
    for (Want const& w : wants) {
        *w.slot = w.ref->Get();
        if (*w.slot == nullptr) {
            missing += std::format("{}Function {}", missing.empty() ? "" : ", ", w.ref->Path());
        }
    }
    // Class, health and pause are nice to have; the rest is the wheel's own path.
    gStarted = gGetSquadSize != nullptr && gGetMember != nullptr && gGetMemberSwitchWeapon != nullptr &&
        gSetMemberSwitchWeapon != nullptr && gSquadOrderEquipWeapon != nullptr && gApplyTacticalOrders != nullptr &&
        gChangeToQuickSlot != nullptr && gIsInCombatActionState != nullptr && gGetRangedWeaponType != nullptr;
    log::Info("squad: script functions {}{}; the game's own wheel {}", gStarted ? "found" : "MISSING: the squad menu stays off",
        missing.empty() ? std::string() : std::format(" (not found: {})", missing),
        gEnterWeaponWheel != nullptr && gExitWeaponWheel != nullptr ? "can be opened and closed" : "CAN'T be opened (custom menu instead)");
    return gStarted;
#else
    return false;
#endif
}

void Tick(double deltaSeconds, Link const& link) {
    hand_calibration::Refresh();  // at most once a second
    hand_calibration::Squad const settings = hand_calibration::GetSquad();
    gEnabled = settings.enabled;
    gSupported = link.supported;

    vr::controllers::State const controllers = vr::controllers::Latest();
    vr::controllers::Hand const& left = controllers.left;
    // The left trigger (2026-10-22, the user's design): a new pull opens the wheel or menu at once; letting
    // go gives the highlighted weapon. The squad takes it per pull (menu::OwnTrigger) where it can use it
    // (on foot or in a wheel, the feature on, [Controls] leftTrigger = lb); elsewhere it stays the game's LB
    // (the Mako's boost, menus).
    bool const wasDown = gTrigger;
    gTrigger = controllers.active && (gTrigger ? left.trigger > kTriggerOff : left.trigger >= kTriggerOn);
    hand_calibration::Controls const controls = hand_calibration::GetControls();
    auto const mode = head_tracking::GameMode();
    bool const modeTakesIt = mode && (*mode == 0 || *mode == 2 || *mode == 3);  // on foot, the power wheel, the weapon wheel
    bool const wanted = settings.enabled && link.supported && gStarted && controls.leftTriggerLB && modeTakesIt;
    if (settings.enabled && link.supported && !controls.leftTriggerLB && !gToldTriggerSetting) {
        gToldTriggerSetting = true;
        log::Warn("squad: the left trigger opens the squad's weapon wheel only with [Controls] leftTrigger = lb (it is {} now): "
                  "no squad wheel", controls.leftTrigger ? "lt" : "none");
    } else if (controls.leftTriggerLB) {
        gToldTriggerSetting = false;
    }
    bool const ownedBefore = gTriggerOwned;
    gTriggerOwned = menu::OwnTrigger(gTriggerOwner, gTrigger, wanted);
    if (gTriggerOwned != ownedBefore) {
        log::Info("squad: the left trigger {}", gTriggerOwned ? "opens the squad's weapon wheel (no LB to the game)"
                                                              : "is the game's LB again (not on foot, or the squad wheel is off)");
    }
    bool const newPull = gTrigger && !wasDown && gTriggerOwned;
    PublishWheelPanel(link, settings);  // the game's wheels at the hand, whichever way they open

    if (!settings.enabled) {
        Inactive("off ([Squad] enabled = 0)", link);
        return;
    }
    if (!link.supported) {
        if (!gToldOldHost) {
            gToldOldHost = true;
            log::Warn("squad: the host can't draw the squad menu or keep the sticks from the game (services older than v11): "
                      "the menu stays off; close the game and redeploy the host");
        }
        Inactive("the host is older than v11 (redeploy it)", link);
        return;
    }
#if defined(MEVR_GAME_LE1)
    if (!gStarted) {
        Inactive("waiting for the script functions", link);
        return;
    }
    bool const nativeReady = gEnterWeaponWheel != nullptr && gExitWeaponWheel != nullptr;
    if (settings.native && (!link.nativeSupported || !nativeReady) && !gToldNoNative) {
        gToldNoNative = true;
        log::Warn("squad: style = native, but {}: our drawn menu instead", !link.nativeSupported
            ? "the host can't show the game's wheel at the hand (services older than v12: close the game and redeploy the host)"
            : "the game's EnterWeaponWheel/ExitWeaponWheel weren't found");
    }
    bool const native = settings.native && link.nativeSupported && nativeReady;
    if (native != gStyleNative) {
        gStyleNative = native;
        log::Info("squad: style: {}", native ? "the game's own weapon wheel at the hand" : "our drawn menu at the hand");
    }
    if (native) {
        TickNative(deltaSeconds, link, settings, controllers, newPull);
        return;
    }
    if (gWheel.Holding()) {
        ReleaseWheel("style is custom now");
    }
    if (!mode || *mode != 0) {
        Inactive(std::format("not on foot (game mode {})", mode ? std::to_string(*mode) : std::string("unknown")), link);
        return;
    }
    vr::host_view::State const view = vr::host_view::Latest();
    if (!view.valid) {
        Inactive("no head-tracked world view (Halcyon's menu, a flat screen or VR off)", link);
        return;
    }
    if (!controllers.active) {
        Inactive("the controllers aren't active", link);
        return;
    }
    std::string why;
    auto const game = ReadGame(why);
    if (!game) {
        Inactive(why, link);
        return;
    }

    // The squad, this tick, and what the left hand points at.
    bool const busy = gMachine.CurrentPhase() != menu::Phase::Idle;
    Survey const survey = SurveySquad(*game, deltaSeconds, busy, newPull, left, view, settings);
    int const pointed = survey.pointed;
    int const preferred = survey.preferred;
    char const* const preferredWhy = survey.preferredWhy;

    // The squad mustn't change under an open menu.
    bool sameSquad = true;
    if (busy) {
        sameSquad = gCount == gOpenCount;
        for (int i = 0; i < gCount && sameSquad; ++i) {
            sameSquad = gMembers[i].key == gOpenKeys[i];
        }
    }
    gUsable = sameSquad;  // an empty squad still answers a pull (a refusal); the machine closes on it

    menu::Input in;
    in.dt = static_cast<float>(deltaSeconds);
    in.usable = gUsable && !support_grip::Held();
    in.triggerDown = gTrigger;
    in.newPull = newPull;
    in.pointed = pointed >= 0 ? CardOf(pointed) : -1;
    in.preferred = CardOf(preferred);
    in.memberCount = gCount;
    for (int c = 0; c < gCount; ++c) {
        in.members[c].carried = gMembers[gCardOrder[c]].carried;
        in.members[c].current = gMembers[gCardOrder[c]].current;
    }
    in.leftX = left.stickX;
    in.leftY = left.stickY;
    in.rightX = controllers.right.stickX;
    in.rightY = controllers.right.stickY;
    in.a = controllers.right.primary;
    in.b = controllers.right.secondary;
    in.confirmOnRelease = settings.confirmOnRelease;
    int const selectedBefore = gMachine.Selected();
    menu::Step const step = gMachine.Update(in);

    auto const nameOfCard = [&](int card) {
        return card >= 0 && card < gCount ? gMembers[gCardOrder[card]].name : std::string("-");
    };
    if (newPull) {
        LogPull(survey, settings);
    }
    if (step.refused) {
        ++gRefusals;
        log::Info("squad: no squadmates to give weapons to");
        RefusedHaptic(link, kLeft);
    }
    if (step.opened) {
        ++gSerial;
        ++gOpens;
        gOpenCount = gCount;
        for (int i = 0; i < kMaxMembers; ++i) {
            gOpenKeys[i] = gMembers[i].key;
        }
        gPointedCard = step.openedPointing ? gMachine.Selected() : -1;
        log::Info("squad: menu #{} opened{}: {} chosen{}; cards: left {}, right {}", gSerial,
            step.openedPointing ? " (pointed)" : " (pointing at nobody)", nameOfCard(gMachine.Selected()),
            step.openedPointing ? std::string() : std::format(" ({})", preferredWhy), nameOfCard(0), nameOfCard(1));
        OpenHaptic(link);
        if (settings.pause) {
            PauseGame(*game, true);
        }
    }
    if (step.cardChanged) {
        log::Info("squad: card: {} (left stick, was {})", nameOfCard(gMachine.Selected()), nameOfCard(selectedBefore));
        TickHaptic(link, kLeft);
    }
    if (step.tileChanged) {
        log::Info("squad: highlight: {}", gMachine.Highlighted() >= 0
            ? std::format("the {} ({})", menu::WeaponName(gMachine.Highlighted()), menu::DirectionName(menu::DirectionOf(gMachine.Highlighted())))
            : std::string("none (the new squadmate doesn't carry it)"));
        if (gMachine.Highlighted() >= 0) {
            TickHaptic(link, kRight);
        }
    }
    if (step.tileRefused) {
        log::Info("squad: flick toward a weapon {} doesn't carry", nameOfCard(gMachine.Selected()));
        RefusedHaptic(link, kRight);
    }
    if (step.closed != menu::Close::None) {
        if (gPausedByUs) {
            PauseGame(*game, false);  // first, as the wheel's Deactivated does
        }
        if (step.closed == menu::Close::Confirmed && step.member >= 0 && step.member < gCount) {
            ++gConfirms;
            int const member = gCardOrder[step.member];
            log::Info("squad: menu #{} confirmed ({}): {} gets the {}", gSerial,
                wasDown && !gTrigger ? "trigger let go" : "A", gMembers[member].name, menu::WeaponName(step.weapon));
            ConfirmHaptic(link);
            Equip(*game, member, step.weapon);
        } else {
            ++gCancels;
            log::Info("squad: menu #{} {}", gSerial, menu::CloseName(step.closed));
            CancelHaptic(link);
        }
        gPointedCard = -1;
    }
    SetWhy(gMachine.IsOpen() ? "open" : "ready");
    Publish(link, gMachine.IsOpen());
    FollowPending(deltaSeconds);
#else
    (void)deltaSeconds;
    (void)newPull;
    (void)wasDown;
    (void)mode;
    Inactive("LE1 only", link);
#endif
}

void AdjustPad(gamepad::Options& options) {
    if (!gEnabled || !gSupported) {
        return;
    }
    if (options.gripShoulder && !gToldGripLB) {
        gToldGripLB = true;
        log::Info("squad: a left grip away from the gun would be LB ([Controls] leftGripAway = lb, or supportGrip = 0), but the "
                  "weapon wheel is on the left trigger now: the left grip only holds the gun ([Squad] enabled = 0 gives LB back)");
    }
    options.gripShoulder = false;
    // The squad has the left trigger for this pull: the game sees neither LB (no 0.3 s hold, no tap that
    // swaps Shepard's weapon) nor LT from it; our path opens and closes the wheel.
    if (gTriggerOwned) {
        options.leftTriggerShoulder = false;
        options.leftTrigger = false;
    }
    options.squadMenu = gMachine.IsOpen();
    options.nativeWheel = gWheel.Holding();
    options.wheelStick = gWheelStick;
    options.wheelPressA = gWheelPressA && gWheel.Holding();
}

bool IsOpen() {
    return gMachine.IsOpen() || gWheel.Holding();
}

std::string StatusJson() {
    namespace json = vr::test_request;
    char const* phase = "idle";
    switch (gMachine.CurrentPhase()) {
    case menu::Phase::Idle: phase = "idle"; break;
    case menu::Phase::Open: phase = "open"; break;
    }
    std::string members;
    for (int i = 0; i < gCount; ++i) {
        MemberInfo const& m = gMembers[i];
        std::string carried;
        for (int w = 0; w < kWeapons; ++w) {
            if ((m.carried & (1u << w)) != 0) {
                carried += std::format("{}{}", carried.empty() ? "" : ", ", json::JsonString(menu::WeaponName(w)));
            }
        }
        members += std::format("{}{{\"name\": {}, \"nameSource\": {}, \"class\": {}, \"role\": {}, \"health\": {}, \"current\": {}, "
                               "\"carried\": [{}], \"card\": {}, \"target\": {}, \"angleDeg\": {}, \"distance\": {}}}",
            i > 0 ? ", " : "", json::JsonString(m.name), json::JsonString(m.nameSource), m.classId, json::JsonString(m.role),
            json::JsonNumber(m.health), json::JsonString(menu::WeaponName(m.current)), carried, CardOf(i),
            m.target.valid ? std::format("[{}, {}, {}]", json::JsonNumber(m.target.position.x), json::JsonNumber(m.target.position.y),
                                 json::JsonNumber(m.target.position.z))
                           : std::string("null"),
            json::JsonNumber(m.angle / kDeg), json::JsonNumber(m.distance));
    }
    return std::format("{{\"enabled\": {}, \"hostSupports\": {}, \"style\": {}, \"why\": {}, \"phase\": {}, \"serial\": {}, "
                       "\"selectedCard\": {}, \"highlighted\": {}, \"pausedGame\": {}, \"wheel\": {{\"phase\": {}, \"serial\": {}, "
                       "\"gameMode\": {}, \"entry\": [{}, {}], \"pressingA\": {}}}, \"lastResult\": {}, \"members\": [{}]}}",
        gEnabled ? "true" : "false", gSupported ? "true" : "false", json::JsonString(gStyleNative ? "native" : "custom"),
        json::JsonString(gWhy), json::JsonString(phase), gSerial, gMachine.Selected(),
        json::JsonString(menu::WeaponName(gMachine.Highlighted())), gPausedByUs ? "true" : "false",
        json::JsonString(wheel::PhaseName(gWheel.CurrentPhase())), gWheelSerial, gWheelMode, gWheelEntry.member, gWheelEntry.slot,
        gWheelPressA ? "true" : "false", json::JsonString(gLastResult), members);
}

void LogStats() {
    if (!gEnabled && gOpens == 0) {
        return;
    }
    std::string names;
    for (int i = 0; i < gCount; ++i) {
        names += std::format("{}{} {:.0f} deg from the left hand's ray", i > 0 ? ", " : "", gMembers[i].name,
            gMembers[i].angle / kDeg);
    }
    log::Info("    squad: {}; {} squadmate(s){}; this period: {} opened, {} confirmed, {} cancelled, {} refused; last: {}", gWhy, gCount,
        names.empty() ? std::string() : std::format(" ({})", names), gOpens, gConfirms, gCancels, gRefusals,
        gLastResult.empty() ? std::string("-") : gLastResult);
    gOpens = gConfirms = gCancels = gRefusals = 0;
}

void Shutdown(bool processExiting) {
#if defined(MEVR_GAME_LE1)
    if (!processExiting) {
        ResumeIfPaused("the mod is unloading with the menu open");
        ReleaseWheel("the mod is unloading");
    }
    gGetSquadSize = gGetMember = gGetMemberSwitchWeapon = gSetMemberSwitchWeapon = gSquadOrderEquipWeapon = nullptr;
    gApplyTacticalOrders = gChangeToQuickSlot = gIsInCombatActionState = gGetRangedWeaponType = nullptr;
    gGetClass = gGetPercentHealth = gPauseGame = nullptr;
    gEnterWeaponWheel = gExitWeaponWheel = gGetScaleFormManager = gIsCombatEnabled = gTryHolster = gIsSwitchingWeapons = nullptr;
    gWheelWasHolstered = false;
    gReholsterWait = -1.0;
    gWheelGiven = {};
#else
    (void)processExiting;
#endif
    gPausedByUs = false;
    gMachine.Reset();
    gMembers = {};
    gCount = 0;
    gCardOrder = {0, 1};
    gOpenKeys = {};
    gOpenCount = 0;
    gDetailsAge = 1e9;
    gLoggedSquad.clear();
    gTrigger = false;
    gTriggerOwner = {};
    gTriggerOwned = false;
    gToldTriggerSetting = false;
    gPublishedOpen = false;
    gEnabled = false;
    gSupported = false;
    gToldOldHost = false;
    gToldGripLB = false;
    gUsable = false;
    gPointedCard = -1;
    gPending = {};
    gStarted = false;
    gWheel.Reset();
    gWheelPressA = false;
    gWheelEntry = {};
    gWheelMode = -1;
    gWheelPointed = -1;
    gWheelOpenedAt = 0.0;
    gToldNoNative = false;
    gStyleNative = false;
    gWhy = "not started";
    gLoggedWhy.clear();
}

} // namespace mevr::game::squad_weapons
