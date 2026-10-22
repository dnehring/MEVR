#include "game/SquadWeapons.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/SquadMenu.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/Engine.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/Objects.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/SupportGrip.hpp"
#include "game/ViewAnchor.hpp"

#include <LESDK/Headers.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

namespace mevr::game::squad_weapons {
namespace {

namespace menu = vr::squad_menu;
using menu::kMaxMembers;
using menu::kWeapons;

constexpr int kLeft = 0;
constexpr int kRight = 1;
// The left grip's latch, as the pad mapping's (game/gamepad_mapping).
constexpr float kGripOn = 0.6f;
constexpr float kGripOff = 0.4f;
// Point at the chest: this far above the pawn's location (its collision centre), Unreal units.
constexpr double kChestUnits = 20.0;
// How long the game gets to show a given weapon in the squadmate's hands before we say it didn't.
constexpr double kResultSeconds = 3.0;
// Member details (health, quick slots) are re-read this often when the menu is closed; every tick open.
constexpr double kDetailsSeconds = 0.5;
constexpr float kPi = 3.14159265358979f;
constexpr float kDeg = kPi / 180.0f;

#if defined(SDK_TARGET_LE1)
static_assert(offsetof(UBioEpicPawnBehavior, Squad) == 0x2F8);
static_assert(offsetof(UBioActorBehavior, ActorGameName) == 0xFC);
static_assert(offsetof(UBioActorBehavior, m_oActorType) == 0x18C);
static_assert(offsetof(UBioActorType, ActorGameName) == 0x6C);
static_assert(offsetof(ABioBaseSquad, Members) == 0x314);
// What SquadOrderEquipWeapon (0xC77FD0) and the switch-weapon natives (0x9E0220/0x9E0240) index: a
// 0x74-byte entry, the pawn at +0x48, the tactical action at +0x70, the switch weapon at +0x71.
static_assert(sizeof(FMemberData) == 0x74);
static_assert(offsetof(FMemberData, SquadMember) == 0x48);
static_assert(offsetof(FMemberData, eSwitchWeapon) == 0x71);
static_assert(offsetof(UBioPawnBehavior, m_oEquipment) == 0x424);
static_assert(offsetof(UBioEquipment, m_QuickSlotArray) == 0x94);
static_assert(offsetof(UBioEquipment, m_eQuickSlotCurrent) == 0xB8);

UFunction* gGetSquadSize = nullptr;
UFunction* gGetMember = nullptr;
UFunction* gGetMemberSwitchWeapon = nullptr;
UFunction* gSetMemberSwitchWeapon = nullptr;
UFunction* gSquadOrderEquipWeapon = nullptr;
UFunction* gApplyTacticalOrders = nullptr;
UFunction* gChangeToQuickSlot = nullptr;
UFunction* gIsInCombatActionState = nullptr;
UFunction* gGetRangedWeaponType = nullptr;
UFunction* gGetClass = nullptr;
UFunction* gGetPercentHealth = nullptr;
UFunction* gPauseGame = nullptr;
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
bool gOwnGrip = false;
bool gHaveCount = false;
std::uint32_t gLastCount = 0;
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

#if defined(SDK_TARGET_LE1)

std::string Narrow(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    int const n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(n, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    while (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
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

bool CallBool(UObject* object, UFunction* function) {
    if (object == nullptr || function == nullptr) {
        return false;
    }
    struct {
        unsigned long ReturnValue;
        unsigned char pad[60];
    } parms{};
    object->ProcessEvent(function, &parms, nullptr);  // directly: never the SDK's wrapper
    return parms.ReturnValue != 0;
}

int SquadSize(ABioBaseSquad* squad) {
    ABioBaseSquad_execGetSquadSize_Parms parms;
    squad->ProcessEvent(gGetSquadSize, &parms, nullptr);
    return parms.ReturnValue;
}

APawn* SquadMember(ABioBaseSquad* squad, int index) {
    ABioBaseSquad_execGetMember_Parms parms;
    parms.nIndex = index;
    squad->ProcessEvent(gGetMember, &parms, nullptr);
    return parms.ReturnValue;
}

int SwitchWeaponOf(ABioBaseSquad* squad, int index) {
    ABioBaseSquad_execGetMemberSwitchWeapon_Parms parms;
    parms.nIndex = index;
    squad->ProcessEvent(gGetMemberSwitchWeapon, &parms, nullptr);
    return parms.ReturnValue;
}

int RangedType(ABioWeapon* weapon) {
    if (weapon == nullptr || gGetRangedWeaponType == nullptr || !weapon->IsA(ABioWeaponRanged::StaticClass())) {
        return -1;
    }
    ABioWeaponRanged_execGetRangedWeaponType_Parms parms;
    weapon->ProcessEvent(gGetRangedWeaponType, &parms, nullptr);
    return parms.ReturnValue <= 3 ? static_cast<int>(parms.ReturnValue) : -1;
}

struct Game {
    ABioPlayerController* controller = nullptr;
    ABioPawn* player = nullptr;
    ABioBaseSquad* squad = nullptr;
    std::array<ABioPawn*, kMaxMembers> pawns{};  // this tick's squadmates, in squad order
    std::array<int, kMaxMembers> indices{};      // their squad indices
    int count = 0;
};

std::optional<Game> ReadGame(std::string& why) {
    APlayerController* const pc = engine::LocalPlayerController();
    if (pc == nullptr || !pc->IsA(ABioPlayerController::StaticClass())) {
        why = "no player controller";
        return std::nullopt;
    }
    Game game;
    game.controller = static_cast<ABioPlayerController*>(pc);
    APawn* const pawn = pc->Pawn;
    if (pawn == nullptr || !pawn->IsA(ABioPawn::StaticClass())) {
        why = "no player pawn (vehicle or cutscene?)";
        return std::nullopt;
    }
    game.player = static_cast<ABioPawn*>(pawn);
    UBioPawnBehavior* const behavior = game.player->m_oBehavior;
    game.squad = behavior != nullptr ? behavior->Squad : nullptr;
    if (game.squad == nullptr) {
        why = "the player has no squad";
        return std::nullopt;
    }
    int const size = SquadSize(game.squad);
    for (int i = 0; i < size && game.count < kMaxMembers; ++i) {
        APawn* const member = SquadMember(game.squad, i);
        if (member == nullptr || member == pawn || !member->IsA(ABioPawn::StaticClass())) {
            continue;
        }
        game.pawns[game.count] = static_cast<ABioPawn*>(member);
        game.indices[game.count] = i;
        ++game.count;
    }
    return game;
}

void ReadName(ABioPawn* pawn, MemberInfo& m) {
    UBioPawnBehavior* const behavior = pawn->m_oBehavior;
    std::string behaviorName;
    std::string typeName;
    if (behavior != nullptr) {
        if (behavior->ActorGameName.Any()) {
            behaviorName = Narrow(static_cast<std::wstring_view>(behavior->ActorGameName));
        }
        if (behavior->m_oActorType != nullptr && behavior->m_oActorType->ActorGameName.Any()) {
            typeName = Narrow(static_cast<std::wstring_view>(behavior->m_oActorType->ActorGameName));
        }
    }
    std::string const tag = engine::ToString(pawn->Tag);
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
        m.key, m.name, m.nameSource, m.classId, m.role, behaviorName, typeName, tag, engine::FullName(pawn));
}

// The squadmate's details: class and name once, quick slots when they change, health.
void ReadDetails(ABioPawn* pawn, MemberInfo& m, bool fresh) {
    UBioPawnBehavior* const behavior = pawn->m_oBehavior;
    if (fresh) {
        int classId = -1;
        if (behavior != nullptr && gGetClass != nullptr) {
            UBioPawnBehavior_execGetClass_Parms parms;
            behavior->ProcessEvent(gGetClass, &parms, nullptr);
            classId = parms.ReturnValue;
        }
        m.classId = classId;
        ReadName(pawn, m);
    }
    UBioEquipment* const equipment = behavior != nullptr ? behavior->m_oEquipment : nullptr;
    if (equipment != nullptr) {
        std::array<std::uintptr_t, kWeapons> weapons{};
        for (int i = 0; i < kWeapons; ++i) {
            weapons[i] = reinterpret_cast<std::uintptr_t>(equipment->m_QuickSlotArray[i]);
        }
        if (fresh || weapons != m.weapons) {
            m.weapons = weapons;
            m.carried = 0;
            bool matches = true;
            for (int i = 0; i < kWeapons; ++i) {
                m.typeOfSlot[i] = RangedType(equipment->m_QuickSlotArray[i]);
                if (m.typeOfSlot[i] >= 0) {
                    m.carried = static_cast<std::uint8_t>(m.carried | (1u << m.typeOfSlot[i]));
                    matches = matches && m.typeOfSlot[i] == i;
                }
            }
            log::Info("squad: {}'s quick slots: {} ({})", m.name, WeaponList(m.carried),
                matches ? "slot = weapon type, as the weapon wheel assumes" : "NOT slot = weapon type");
        }
        int const slot = equipment->m_eQuickSlotCurrent;
        m.current = slot >= 0 && slot < kWeapons ? m.typeOfSlot[slot] : -1;
    } else {
        m.carried = 0;
        m.current = -1;
    }
    m.health = -1.0f;
    if (behavior != nullptr && gGetPercentHealth != nullptr) {
        UBioEpicPawnBehavior_execGetPercentHealth_Parms parms;
        behavior->ProcessEvent(gGetPercentHealth, &parms, nullptr);
        if (std::isfinite(parms.ReturnValue)) {
            m.health = std::clamp(parms.ReturnValue > 1.5f ? parms.ReturnValue / 100.0f : parms.ReturnValue, 0.0f, 1.0f);
        }
    }
}

void PauseGame(Game const& game, bool pause) {
    AWorldInfo* const world = game.controller->WorldInfo;
    if (world == nullptr || gPauseGame == nullptr) {
        return;
    }
    AWorldInfo_execPauseGame_Parms parms;
    parms.bPause = pause ? 1 : 0;
    world->ProcessEvent(gPauseGame, &parms, nullptr);  // what the weapon wheel's Activated/Deactivated call
    gPausedByUs = pause;
    log::Info("squad: the game {} (WorldInfo.PauseGame, as ME1's weapon wheel)", pause ? "paused" : "runs again");
}

// The menu closed some other way (the mode changed, the mod unloads) while it had paused the game.
void ResumeIfPaused(char const* why) {
    if (!gPausedByUs) {
        return;
    }
    gPausedByUs = false;
    APlayerController* const pc = engine::LocalPlayerController();
    if (pc == nullptr || pc->WorldInfo == nullptr || gPauseGame == nullptr) {
        log::Warn("squad: the menu paused the game and closed ({}), but there's no world to unpause", why);
        return;
    }
    AWorldInfo_execPauseGame_Parms parms;
    parms.bPause = 0;
    pc->WorldInfo->ProcessEvent(gPauseGame, &parms, nullptr);
    log::Info("squad: the game runs again ({})", why);
}

// What the weapon wheel does for one member when it closes (SFXGameModeWeaponWheel.Deactivated).
void Equip(Game const& game, int member, int weapon) {
    MemberInfo const& m = gMembers[member];
    ABioPawn* const pawn = game.pawns[member];
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
    ABioPlayerController_execSquadOrderEquipWeapon_Parms order;
    order.nWeapon = static_cast<unsigned char>(weapon);
    order.pPawn = pawn;
    game.controller->ProcessEvent(gSquadOrderEquipWeapon, &order, nullptr);  // directly: never the SDK's wrapper
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
        ABioPlayerController_execApplyTacticalOrders_Parms apply;
        game.controller->ProcessEvent(gApplyTacticalOrders, &apply, nullptr);
        path = "SquadOrderEquipWeapon, then ApplyTacticalOrders (Shepard's weapon is out: the AI switches, as the wheel "
               "does in combat)";
    } else {
        UBioPawnBehavior* const behavior = pawn->m_oBehavior;
        UBioEquipment* const equipment = behavior != nullptr ? behavior->m_oEquipment : nullptr;
        if (equipment == nullptr || !equipment->IsA(UBioEquipment::StaticClass())) {
            gLastResult = std::format("{} has no equipment", m.name);
            log::Warn("squad: {}: not switched", gLastResult);
            return;
        }
        UBioEquipment_execChangeToQuickSlot_Parms change;
        change.Slot = static_cast<unsigned char>(recorded);
        equipment->ProcessEvent(gChangeToQuickSlot, &change, nullptr);
        ABioBaseSquad_execSetMemberSwitchWeapon_Parms reset;
        reset.nIndex = index;
        reset.eWeapon = 4;  // STW_None: applied
        game.squad->ProcessEvent(gSetMemberSwitchWeapon, &reset, nullptr);
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

#endif  // SDK_TARGET_LE1

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

void Inactive(std::string why, Link const& link) {
    if (gMachine.CurrentPhase() != menu::Phase::Idle) {
        log::Info("squad: menu closed ({})", why);
    }
#if defined(SDK_TARGET_LE1)
    ResumeIfPaused(why.c_str());
#endif
    gMachine.Reset();
    gUsable = false;
    Publish(link, false);
    SetWhy(std::move(why));
}

} // namespace

bool TryStartup() {
#if defined(SDK_TARGET_LE1)
    struct Want {
        UFunction** slot;
        wchar_t const* name;
        char const* full;
    };
    Want const wants[] = {
        {&gGetSquadSize, L"GetSquadSize", "Function SFXGame.BioBaseSquad.GetSquadSize"},
        {&gGetMember, L"GetMember", "Function SFXGame.BioBaseSquad.GetMember"},
        {&gGetMemberSwitchWeapon, L"GetMemberSwitchWeapon", "Function SFXGame.BioBaseSquad.GetMemberSwitchWeapon"},
        {&gSetMemberSwitchWeapon, L"SetMemberSwitchWeapon", "Function SFXGame.BioBaseSquad.SetMemberSwitchWeapon"},
        {&gSquadOrderEquipWeapon, L"SquadOrderEquipWeapon", "Function SFXGame.BioPlayerController.SquadOrderEquipWeapon"},
        {&gApplyTacticalOrders, L"ApplyTacticalOrders", "Function SFXGame.BioPlayerController.ApplyTacticalOrders"},
        {&gChangeToQuickSlot, L"ChangeToQuickSlot", "Function SFXGame.BioEquipment.ChangeToQuickSlot"},
        {&gIsInCombatActionState, L"IsInCombatActionState", "Function SFXGame.BioPawn.IsInCombatActionState"},
        {&gGetRangedWeaponType, L"GetRangedWeaponType", "Function SFXGame.BioWeaponRanged.GetRangedWeaponType"},
        {&gGetClass, L"GetClass", "Function SFXGame.BioPawnBehavior.GetClass"},
        {&gGetPercentHealth, L"GetPercentHealth", "Function SFXGame.BioEpicPawnBehavior.GetPercentHealth"},
        {&gPauseGame, L"PauseGame", "Function Engine.WorldInfo.PauseGame"},
    };
    std::string missing;
    for (Want const& w : wants) {
        *w.slot = engine::FindFunction(w.name, w.full);
        if (*w.slot == nullptr) {
            missing += std::format("{}{}", missing.empty() ? "" : ", ", w.full);
        }
    }
    // Class, health and pause are nice to have; the rest is the wheel's own path.
    gStarted = gGetSquadSize != nullptr && gGetMember != nullptr && gGetMemberSwitchWeapon != nullptr &&
        gSetMemberSwitchWeapon != nullptr && gSquadOrderEquipWeapon != nullptr && gApplyTacticalOrders != nullptr &&
        gChangeToQuickSlot != nullptr && gIsInCombatActionState != nullptr && gGetRangedWeaponType != nullptr;
    log::Info("squad: script functions {}{}", gStarted ? "found" : "MISSING: the squad menu stays off",
        missing.empty() ? std::string() : std::format(" (not found: {})", missing));
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
    bool const wasDown = gOwnGrip;
    gOwnGrip = controllers.active && (gOwnGrip ? left.squeeze > kGripOff : left.squeeze >= kGripOn);
    // A new left squeeze that began away from the gun: the host's count.
    bool newAway = false;
    if (link.leftGripAway) {
        std::uint32_t const count = *link.leftGripAway >> 1;
        newAway = gHaveCount && count != gLastCount && (*link.leftGripAway & 1u) != 0;
        gLastCount = count;
        gHaveCount = true;
    }
    (void)wasDown;

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
#if defined(SDK_TARGET_LE1)
    if (!gStarted) {
        Inactive("waiting for the script functions", link);
        return;
    }
    auto const mode = head_tracking::GameMode();
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

    // The squad, this tick.
    bool const busy = gMachine.CurrentPhase() != menu::Phase::Idle;
    gDetailsAge += deltaSeconds;
    bool const details = busy || newAway || gPending.key != 0 || gDetailsAge >= kDetailsSeconds;
    if (details) {
        gDetailsAge = 0.0;
    }
    for (int i = 0; i < game->count; ++i) {
        std::uintptr_t const key = reinterpret_cast<std::uintptr_t>(game->pawns[i]);
        bool const fresh = gMembers[i].key != key;
        if (fresh) {
            gMembers[i] = MemberInfo{};
            gMembers[i].key = key;
        }
        if (fresh || details) {
            ReadDetails(game->pawns[i], gMembers[i], fresh);
        }
    }
    for (int i = game->count; i < kMaxMembers; ++i) {
        gMembers[i] = MemberInfo{};
    }
    gCount = game->count;
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
        if (anchor) {
            FVector const& at = engine::adapter::Location(game->pawns[i]);
            m.target.position = engine::ToTracking(*anchor, engine::UnrealVector{at.X, at.Y, at.Z + kChestUnits});
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
    int const pointed = menu::PickPointed(handOrigin, handDirection, targets.data(), gCount,
        static_cast<float>(settings.pointConeDeg) * kDeg, static_cast<float>(settings.pointMaxM));
    int const nearest = menu::NearestToRay(handOrigin, handDirection, targets.data(), gCount);
    int preferred = -1;
    for (int i = 0; i < gCount; ++i) {
        if (gMembers[i].key == gLastUsed && gLastUsed != 0) {
            preferred = i;
        }
    }
    char const* preferredWhy = "the last squadmate you gave a weapon";
    if (preferred < 0) {
        preferred = nearest >= 0 ? nearest : 0;
        preferredWhy = nearest >= 0 ? "the one nearest your pointing" : "the first in the squad";
    }

    // The squad mustn't change under an open menu.
    bool sameSquad = true;
    if (busy) {
        sameSquad = gCount == gOpenCount;
        for (int i = 0; i < gCount && sameSquad; ++i) {
            sameSquad = gMembers[i].key == gOpenKeys[i];
        }
    }
    gUsable = sameSquad;  // an empty squad still answers a squeeze (a refusal); the machine closes on it

    // The two-handed grip keeps priority: a squeeze near the foregrip that the mapping didn't take is a
    // missed grab, not the menu (unless the hand points at someone).
    gamepad::Options const pad = support_grip::PadOptions();
    hand_calibration::Controls const controls = hand_calibration::GetControls();
    float const gunDistance = pad.support.distance;
    bool const nearGun = std::isfinite(gunDistance) && gunDistance >= 0.0f && gunDistance <= controls.releaseMeters;

    menu::Input in;
    in.dt = static_cast<float>(deltaSeconds);
    in.usable = gUsable && !support_grip::Held();
    in.gripDown = gOwnGrip;
    in.newAwaySqueeze = newAway;
    in.nearGun = nearGun;
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
    in.openHoldSeconds = static_cast<float>(settings.openHoldSeconds);
    int const selectedBefore = gMachine.Selected();
    menu::Step const step = gMachine.Update(in);

    auto const nameOfCard = [&](int card) {
        return card >= 0 && card < gCount ? gMembers[gCardOrder[card]].name : std::string("-");
    };
    if (newAway) {
        log::Info("squad: left grip squeezed away from the gun{}: {}", nearGun ? std::format(" ({:.2f} m from the foregrip)", gunDistance) : "",
            pointed >= 0 ? std::format("pointing at {} ({:.1f} deg off, {:.1f} m)", gMembers[pointed].name, gMembers[pointed].angle / kDeg,
                               gMembers[pointed].distance)
            : nearest >= 0 ? std::format("pointing at nobody (nearest {} at {:.0f} deg, cone {:.0f})", gMembers[nearest].name,
                                 gMembers[nearest].angle / kDeg, settings.pointConeDeg)
                           : std::string("no squadmate in view"));
    }
    if (step.refused) {
        ++gRefusals;
        log::Info("squad: no squadmates to give weapons to");
        RefusedHaptic(link, kLeft);
    }
    if (step.missedGrab) {
        log::Info("squad: a squeeze at nobody next to the gun is a missed foregrip grab, not the squad menu");
    }
    if (step.armed) {
        log::Info("squad: keep squeezing {:.2f} s to open the menu anyway ({})", settings.openHoldSeconds, preferredWhy);
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
            step.openedPointing ? " (pointed)" : std::format(" (held {:.2f} s)", settings.openHoldSeconds),
            nameOfCard(gMachine.Selected()), step.openedPointing ? std::string() : std::format(" ({})", preferredWhy), nameOfCard(0),
            nameOfCard(1));
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
                wasDown && !gOwnGrip ? "grip let go" : "A", gMembers[member].name, menu::WeaponName(step.weapon));
            ConfirmHaptic(link);
            Equip(*game, member, step.weapon);
        } else {
            ++gCancels;
            log::Info("squad: menu #{} {}", gSerial, menu::CloseName(step.closed));
            CancelHaptic(link);
        }
        gPointedCard = -1;
    }
    SetWhy(gMachine.IsOpen() ? "open" : gMachine.CurrentPhase() == menu::Phase::Arming ? "arming (held at nobody)" : "ready");
    Publish(link, gMachine.IsOpen());
    FollowPending(deltaSeconds);
#else
    (void)deltaSeconds;
    (void)newAway;
    Inactive("LE1 only", link);
#endif
}

void AdjustPad(gamepad::Options& options) {
    if (!gEnabled || !gSupported) {
        return;
    }
    if (options.gripShoulder && !gToldGripLB) {
        gToldGripLB = true;
        log::Info("squad: a left grip away from the gun would be LB ([Controls] leftGripAway = lb, or supportGrip = 0), but it "
                  "opens the squad menu now: no LB from it ([Squad] enabled = 0 gives it back)");
    }
    options.gripShoulder = false;
    options.squadMenu = gMachine.IsOpen();
}

bool IsOpen() {
    return gMachine.IsOpen();
}

std::string StatusJson() {
    namespace json = vr::test_request;
    char const* phase = "idle";
    switch (gMachine.CurrentPhase()) {
    case menu::Phase::Idle: phase = "idle"; break;
    case menu::Phase::Arming: phase = "arming"; break;
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
    return std::format("{{\"enabled\": {}, \"hostSupports\": {}, \"why\": {}, \"phase\": {}, \"serial\": {}, \"selectedCard\": {}, "
                       "\"highlighted\": {}, \"pausedGame\": {}, \"lastResult\": {}, \"members\": [{}]}}",
        gEnabled ? "true" : "false", gSupported ? "true" : "false", json::JsonString(gWhy), json::JsonString(phase), gSerial,
        gMachine.Selected(), json::JsonString(menu::WeaponName(gMachine.Highlighted())), gPausedByUs ? "true" : "false",
        json::JsonString(gLastResult), members);
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
#if defined(SDK_TARGET_LE1)
    if (!processExiting) {
        ResumeIfPaused("the mod is unloading with the menu open");
    }
    gGetSquadSize = gGetMember = gGetMemberSwitchWeapon = gSetMemberSwitchWeapon = gSquadOrderEquipWeapon = nullptr;
    gApplyTacticalOrders = gChangeToQuickSlot = gIsInCombatActionState = gGetRangedWeaponType = nullptr;
    gGetClass = gGetPercentHealth = gPauseGame = nullptr;
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
    gOwnGrip = false;
    gHaveCount = false;
    gLastCount = 0;
    gPublishedOpen = false;
    gEnabled = false;
    gSupported = false;
    gToldOldHost = false;
    gToldGripLB = false;
    gUsable = false;
    gPointedCard = -1;
    gPending = {};
    gStarted = false;
    gWhy = "not started";
    gLoggedWhy.clear();
}

} // namespace mevr::game::squad_weapons
