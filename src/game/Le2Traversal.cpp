// SPDX-License-Identifier: MIT
#include "game/Le2Traversal.hpp"


#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CallFunction.hpp"
#include "engine/Engine.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/SettingsFile.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/ArmIk.hpp"
#include "game/ConsoleRequest.hpp"
#include "game/Player.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// ME2's native side of traversal (docs/trilogy-parity-ledger.md Forks 4, step 1; research/le2-traversal.md): the cover slots,
// the game's own admission and commit (CanPerformMantle / MantleCover), the move's observation and report. Moved here
// from game/Traversal.cpp unchanged on 2026-11-16; game/Traversal.cpp keeps the settings and the one command surface and
// calls this through the interface ME3's observer has (game/Le3Traversal.hpp).
#if defined(MEVR_GAME_LE2)
namespace mevr::game::traversal::le2 {
namespace {

namespace tv = vr::traversal;

constexpr double kUnitsPerTurn = 65536.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kSlotRefreshS = 2.0;       // the cover list walked again this often (a level streaming in adds links)
constexpr double kAdmitEveryS = 0.10;       // the game's own admission asked this often for the nearest slot
constexpr double kNearLogS = 1.0;           // a change of the nearest slot's verdict logged at most this often
constexpr std::size_t kMaxSlots = 512;
constexpr std::uint8_t kActionClimb = 9;    // SFXCustomActionTypes.EAICustomAction: AI_CustomAction_CoverClimb
constexpr std::uint8_t kActionMantle = 10;  // ... AI_CustomAction_CoverMantle
constexpr std::uint8_t kPhysWalking = 1;    // EPhysics PHYS_Walking

double Now() {
    static LARGE_INTEGER const frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

double Degrees(std::int32_t units) {
    return tv::SignedDegrees(static_cast<double>(units) * 360.0 / kUnitsPerTurn);
}

std::int32_t Units(double degrees) {
    return static_cast<std::int32_t>(std::lround(degrees * kUnitsPerTurn / 360.0));
}

tv::Vec3 V(ue3::Vector const& v) {
    return {v.x, v.y, v.z};
}

std::string Fmt(tv::Vec3 const& v) {
    return std::format("({:.0f}, {:.0f}, {:.0f})", v.x, v.y, v.z);
}

tv::Settings gSettings;       // game/Traversal's, as of the last TryStartup or Tick
std::string gSettingsText;
bool gStarted = false;
Link gLink;

// A cover slot that vaults or climbs, found on the cover list.
struct Slot {
    ue3::Object* link = nullptr;  // compared and called only while alive
    int index = -1;
    std::string name;             // "CoverLink_10 slot 0"
    tv::Vec3 at{};
    double yaw = 0.0;             // the slot's facing (into the cover), degrees
    tv::Kind kind = tv::Kind::None;
    std::string target;           // the far slot's name, if any
    std::optional<tv::Vec3> targetAt;
    std::optional<double> topZ;   // the cover's top under the cue point (a trace), once
    bool topTried = false;
};
std::vector<Slot> gSlots;
double gSlotsAt = -1e9;
void const* gSlotsWorld = nullptr;
bool gToldSlots = false;

// The nearest slot and the game's verdict on it.
struct Near {
    int slot = -1;            // into gSlots
    double distanceM = 0.0;   // the pawn from the slot
    double facingDot = 0.0;   // the pawn's facing on the slot's direction (the game's face test: >= its threshold)
    std::optional<double> headDot;  // the head's facing on it, when the game was asked with the head's ([Traversal] facing = head)
    bool asDrawn = false;     // the game was asked with the weapon away as if drawn ([Traversal] holstered)
    bool asked = false;
    bool admitted = false;    // CanPerformMantle / CanPerformClimb said yes
    double askedAt = -1e9;
    int loggedSlot = -2;
    bool loggedAdmitted = false;
    double loggedAt = -1e9;
};
Near gNear;

tv::PressTracker gPresses;
char const* gSceneWhyBefore = nullptr;  // the last tick's Link::sceneWhy (a literal)
tv::Press gLoggedPress{};

// One move.
struct Move {
    bool active = false;          // the custom action runs
    bool settling = false;        // ended; the level view hands back
    tv::Kind kind = tv::Kind::None;
    std::uint8_t action = 0;
    double startS = 0.0;
    double endS = 0.0;
    bool ours = false;            // the mod's double tap committed it
    tv::Commit commit{};
    std::string target;
    std::string stance = "?";
    tv::MoveRecord record;
    tv::LevelView view;
    tv::Vec3 pawnStart{};
    double pawnYawStart = 0.0;
    std::string weaponBefore;
    std::string weaponAfter;
    std::string cameraMode;
    std::array<double, 2> handsStart{-1.0, -1.0};
    std::array<double, 2> handsMax{-1.0, -1.0};
    std::array<unsigned, 2> handsTicks{0, 0};   // ticks with a drawn wrist measured, through the move and its blend-out
    std::array<unsigned, 2> handsNear{0, 0};    // ... of them within kHandsNearM of the grip
    std::array<double, 2> handsSum{0.0, 0.0};
    double lastCameraS = -1.0;    // the last camera update's time (the level view's step)
    XrVector3f headStart{};
    XrVector3f headEnd{};
    bool headValid = false;
    bool movementBack = false;
    bool aimBack = false;
    double movementBackS = -1.0;
    double aimBackS = -1.0;
    unsigned presses = 0;         // A presses during the move
    unsigned viewWrites = 0;
    double maskPeak = 0.0;
    bool reported = false;
    bool judged = false;
};
Move gMove;
double gOurCommitS = -1e9;  // when the double tap (or the console's go) last called MantleCover / ClimbCover
// The custom action's StartCustomAction handler runs before the move is seen (inside MantleCover): its body stance waits here.
std::string gPendingStance = "?";
double gPendingStanceS = -1e9;
constexpr double kHandsNearM = 0.15;  // a drawn wrist this close to its controller's grip is on it (first person: 0.07 at rest)
// A test pause (mevr traversal pause <s>): the game paused this long into the next move, for eye snapshots of its middle.
double gPauseAfterS = -1.0;
std::uint32_t gToldCueSerial = 0;  // the slot the cue was last logged for
bool gCueShown = false;            // the cue's last visibility, and why (logged on each change)
char const* gCueWhy = "";
std::string gLastSummary = "null";
unsigned gMoves = 0;

// The camera update's last view (the start of a move takes it as the view before).
struct LastPov {
    bool valid = false;
    tv::Pose pose{};
    tv::Vec3 pawn{};
    double pawnYaw = 0.0;
} gLastPov;

// Counters since the heartbeat (and in all).
struct Counters {
    unsigned admitAsks = 0;
    unsigned admitYes = 0;
    unsigned presses = 0;
    unsigned doubleTaps = 0;
    unsigned commits = 0;
    unsigned commitsRefused = 0;
    unsigned gameRequests = 0;     // the game's own TryClimbOrMantleCover (A in cover)
    unsigned gameRequestsTrue = 0;
    unsigned violations = 0;       // moves the rules wouldn't have committed
    unsigned faultsRestored = 0;
    unsigned zoomedAdmissions = 0;  // admissions asked with the weapon's zoom cleared for the call (the two-handed hold)
} gCounters, gTotals;
bool gToldZoomed = false;  // the two-handed hold's first admission with the zoom cleared, logged once a session


// Our reflection layer (ue3/, research/34), ME2's names (research/le2-traversal.md "The names").
ue3::FieldRef gCoverList{"Engine.WorldInfo", "CoverList", ue3::kEveryGame};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gNextCoverLink{"Engine.CoverLink", "NextCoverLink", ue3::kEveryGame};
ue3::FieldRef gLinkSlots{"Engine.CoverLink", "Slots", ue3::kEveryGame};
ue3::FieldRef gSlotCanMantle{gLinkSlots, "bCanMantle"};
ue3::FieldRef gSlotCanClimbUp{gLinkSlots, "bCanClimbUp"};
ue3::FieldRef gSlotEnabled{gLinkSlots, "bEnabled"};
ue3::FieldRef gSlotMantleTarget{gLinkSlots, "MantleTarget"};
ue3::FieldRef gTargetActor{gSlotMantleTarget, "Actor"};
ue3::FieldRef gTargetSlotIdx{gSlotMantleTarget, "SlotIdx"};
ue3::FunctionRef gGetSlotLocationRef{"Engine.CoverLink.GetSlotLocation", ue3::kEveryGame};
ue3::FunctionRef gGetSlotRotationRef{"Engine.CoverLink.GetSlotRotation", ue3::kEveryGame};
ue3::FunctionRef gTraceRef{"Engine.Actor.Trace", ue3::kEveryGame};
ue3::FunctionRef gSetLocationRef{"Engine.Actor.SetLocation", ue3::kEveryGame};
ue3::FunctionRef gSetRotationRef{"Engine.Actor.SetRotation", ue3::kEveryGame};

ue3::FieldRef gCanMantleOutside{"SFXGame.BioPlayerController", "m_bCanMantleOutsideOfCover", ue3::kEveryGame};
ue3::FieldRef gRemappedJoyUp{"SFXGame.BioPlayerController", "RemappedJoyUp", ue3::kEveryGame};
ue3::FieldRef gRemappedJoyRight{"SFXGame.BioPlayerController", "RemappedJoyRight", ue3::kEveryGame};
ue3::FieldRef gMantleDistance{"SFXGame.BioPlayerController", "m_fClimbMantleDistanceThreshold", ue3::kEveryGame};
ue3::FieldRef gMantleFace{"SFXGame.BioPlayerController", "m_fClimbMantleFaceAngleThreshold", ue3::kEveryGame};
ue3::FieldRef gPlayerSelection{"SFXGame.BioPlayerController", "m_oPlayerSelection", ue3::kEveryGame};
ue3::FieldRef gSelectionTarget{"SFXGame.BioPlayerSelection", "m_oCurrentSelectionTarget", ue3::kEveryGame};
ue3::FieldRef gSelectionCombat{"SFXGame.BioPlayerSelection", "m_bCurrentSelectionIsCombatTarget", ue3::kEveryGame};
ue3::FieldRef gCustomActions{"SFXGame.BioPlayerController", "CustomActions", ue3::kEveryGame};
ue3::FieldRef gCurrentCustomAction{"SFXGame.SFXPlayerController", "CurrentCustomAction", ue3::kEveryGame};
ue3::FieldRef gIgnoreMove{"Engine.PlayerController", "bIgnoreMoveInput", ue3::kEveryGame};
ue3::FieldRef gIgnoreLook{"Engine.PlayerController", "bIgnoreLookInput", ue3::kEveryGame};
ue3::FieldRef gPawnCurrentLink{"SFXGame.BioPawn", "CurrentLink", ue3::kEveryGame};
ue3::FieldRef gPawnWeapon{"Engine.Pawn", "Weapon", ue3::kEveryGame};
// The admission as the player stands (research/le2-traversal.md "The vault Freedom's Progress requires"): CanPerformMantle's
// facing is the pawn's Actor.Rotation (pawn +0x124 in 0xABD8E0), its combat test BioPawn.ActionState (+0xA9D == 1 in 0xABD700).
ue3::FieldRef gPawnRotation{"Engine.Actor", "Rotation", ue3::kEveryGame};
ue3::FieldRef gPawnActionState{"SFXGame.BioPawn", "ActionState", ue3::kEveryGame};
constexpr std::uint8_t kActionStateCombat = 1;  // EActionState.ActionState_Combat
// The two-handed hold (the owner, 2026-11-16; docs/trilogy-plan.md rule 8): the support grip's aim (LT) zooms the gun, and
// CanPerformMantle's pawn test (0xABD700) refuses a weapon whose IsZoomed is set (research/le2-vrik.md "Vaulting with the weapon
// held"). The admission clears the flag for its one call, as it sets m_bCanMantleOutsideOfCover; the game zooms on as before.
ue3::ClassRef gWeaponNativeBase{"SFXGame.SFXWeapon_NativeBase", ue3::kEveryGame};
ue3::FieldRef gWeaponZoomed{"SFXGame.SFXWeapon_NativeBase", "bIsZoomed", ue3::kEveryGame};
ue3::FieldRef gPhysics{"Engine.Actor", "Physics", ue3::kEveryGame};
ue3::FieldRef gBlockActors{"Engine.Actor", "bBlockActors", ue3::kEveryGame};
ue3::FieldRef gBodyStance{"SFXGame.BioCustomAction_CoverClimbMantleBase", "m_CurrentBodyStance", ue3::kEveryGame};
ue3::FieldRef gCollisionOffPeriod{"SFXGame.BioCustomAction_CoverClimbMantleBase", "m_fCollisionOffPerioud", ue3::kEveryGame};
ue3::ClassRef gMantleBase{"SFXGame.BioCustomAction_CoverClimbMantleBase", ue3::kEveryGame};
ue3::ClassRef gCameraNativeBase{"SFXGame.SFXCameraNativeBase", ue3::kEveryGame};
ue3::FieldRef gCurrentCameraMode{"SFXGame.SFXCameraNativeBase", "CurrentCameraMode", ue3::kEveryGame};
ue3::FieldRef gPlayerCamera{"Engine.PlayerController", "PlayerCamera", ue3::kEveryGame};

ue3::FunctionRef gCanPerformMantleRef{"SFXGame.BioPlayerController.CanPerformMantle", ue3::kEveryGame};
ue3::FunctionRef gCanPerformClimbRef{"SFXGame.BioPlayerController.CanPerformClimb", ue3::kEveryGame};
ue3::FieldRef gCpmInfo{gCanPerformMantleRef, "CoverInfo"};
ue3::FieldRef gCpcInfo{gCanPerformClimbRef, "CoverInfo"};
ue3::FunctionRef gMantleCoverRef{"SFXGame.SFXPlayerController.MantleCover", ue3::kEveryGame};
ue3::FunctionRef gClimbCoverRef{"SFXGame.SFXPlayerController.ClimbCover", ue3::kEveryGame};
ue3::FieldRef gMantleInfo{gMantleCoverRef, "CovInfo"};
ue3::FieldRef gClimbInfo{gClimbCoverRef, "CovInfo"};
ue3::FunctionRef gTryClimbOrMantleRef{"SFXGame.SFXPlayerController.TryClimbOrMantleCover", ue3::kEveryGame};
ue3::FunctionRef gDoCustomActionRef{"SFXGame.SFXPlayerController.DoCustomAction", ue3::kEveryGame};
ue3::FunctionRef gStartCustomActionRef{"SFXGame.BioCustomAction_CoverClimbMantleBase.StartCustomAction", ue3::kEveryGame};
ue3::FunctionRef gStopCustomActionRef{"SFXGame.BioCustomAction_CoverClimbMantleBase.StopCustomAction", ue3::kEveryGame};
ue3::FunctionRef gAnimEndRef{"SFXGame.BioCustomAction_CoverClimbMantleBase.BodyStanceAnimEndNotification", ue3::kEveryGame};
ue3::FunctionRef gUpdateCameraRef{"Engine.Camera.UpdateCamera", ue3::kEveryGame};
// The CovPosInfo's members (the same struct in each function's parameter).
ue3::FieldRef gInfoLocation{gCpmInfo, "Location"};
ue3::FieldRef gInfoNormal{gCpmInfo, "Normal"};
ue3::FieldRef gInfoTangent{gCpmInfo, "Tangent"};
ue3::FieldRef gInfoLink{gCpmInfo, "Link"};
ue3::FieldRef gInfoLt{gCpmInfo, "LtSlotIdx"};
ue3::FieldRef gInfoRt{gCpmInfo, "RtSlotIdx"};
ue3::FieldRef gInfoPct{gCpmInfo, "LtToRtPct"};

// The step to the vault (the owner's answer "C", 2026-11-19; research/le2-traversal.md "The step to the vault"): Shepard walked to the
// slot by the game's own walk, the left stick fed into the player's input after BioPlayerInput.PlayerInput read the pad (it copies
// RawJoyUp/RawJoyRight into the controller's RemappedJoyUp/Right and MoveStickMag; the native PlayerMove takes its direction and
// MoveMag from them, PlayerMoveCombatOrExplore its acceleration), so collision, the walk's speed and its animation are the game's.
ue3::FunctionRef gBioPlayerInputRef{"SFXGame.BioPlayerInput.PlayerInput", ue3::kEveryGame};
ue3::FieldRef gInputRawJoyUp{"Engine.PlayerInput", "RawJoyUp", ue3::kEveryGame};
ue3::FieldRef gInputRawJoyRight{"Engine.PlayerInput", "RawJoyRight", ue3::kEveryGame};
ue3::FieldRef gInputMoveStickMag{"SFXGame.BioPlayerInput", "MoveStickMag", ue3::kEveryGame};
tv::StepWalk gStep;
int gStepSlot = -1;                 // into gSlots
std::string gStepSlotName;          // the slot's name (gSlots is rebuilt every 2 s: the step follows the name)
double gStepAskedAt = -1e9;
bool gStepAdmitted = false;
double gStepFromViewM = 0.0, gStepFromPawnM = 0.0, gStepOffDeg = 0.0;
struct StepStick {
    bool active = false;
    tv::Stick stick;
    double atS = -1e9;   // when the tick last set it (the input handler writes it only while fresh)
    unsigned writes = 0;
} gStepStick;
unsigned gSteps = 0, gStepVaults = 0, gStepEnded = 0;  // in all

ue3::Object* ObjectField(ue3::Object const* object, ue3::FieldRef& field) {
    return object != nullptr ? field.Read<ue3::Object*>(object).value_or(nullptr) : nullptr;
}

std::string ShortName(ue3::Object const* object) {
    return object != nullptr ? ue3::runtime::Get().NameText(object) : std::string("None");
}

std::string ClassName(ue3::Object const* object) {
    return object != nullptr ? ue3::runtime::Get().NameText(ue3::runtime::Get().ClassOf(object)) : std::string("None");
}

std::uint8_t CurrentAction(ue3::Object* pc) {
    return pc != nullptr ? gCurrentCustomAction.Read<std::uint8_t>(pc).value_or(0) : 0;
}

tv::Kind KindOf(std::uint8_t action) {
    return action == kActionMantle ? tv::Kind::Vault : action == kActionClimb ? tv::Kind::ClimbUp : tv::Kind::None;
}

std::string CameraModeName(ue3::Object* pc) {
    ue3::Object* const base = ObjectField(pc, gPlayerCamera);
    if (base == nullptr || !ue3::IsA(base, gCameraNativeBase)) {
        return "none";
    }
    ue3::Object* const mode = ObjectField(base, gCurrentCameraMode);
    return mode != nullptr && ue3::runtime::IsAlive(mode) ? ClassName(mode) : std::string("none");
}

std::string WeaponName(ue3::Object* pawn) {
    ue3::Object* const weapon = ObjectField(pawn, gPawnWeapon);
    return weapon != nullptr ? ClassName(weapon) : std::string("none");
}

// A CoverLink's native GetSlotLocation / GetSlotRotation (the slot's world place, its offset applied).
std::optional<tv::Vec3> SlotLocation(ue3::Object* link, int index) {
    ue3::Function* const fn = gGetSlotLocationRef.Get();
    if (fn == nullptr || link == nullptr) {
        return std::nullopt;
    }
    ue3::Params parms(ue3::runtime::Get(), fn);
    parms.Set("SlotIdx", static_cast<std::int32_t>(index));
    ue3::runtime::Call(link, fn, parms);
    std::optional<ue3::Vector> const v = parms.Return<ue3::Vector>();
    return v ? std::optional<tv::Vec3>(V(*v)) : std::nullopt;
}

std::optional<double> SlotYaw(ue3::Object* link, int index) {
    ue3::Function* const fn = gGetSlotRotationRef.Get();
    if (fn == nullptr || link == nullptr) {
        return std::nullopt;
    }
    ue3::Params parms(ue3::runtime::Get(), fn);
    parms.Set("SlotIdx", static_cast<std::int32_t>(index));
    ue3::runtime::Call(link, fn, parms);
    std::optional<ue3::Rotator> const r = parms.Return<ue3::Rotator>();
    return r ? std::optional<double>(Degrees(r->yaw)) : std::nullopt;
}

// The cover's top under `at`: a downward trace from `above` units over the slot (the cover is often a mesh actor: actors traced too).
std::optional<double> CoverTop(ue3::Object* pawn, tv::Vec3 at, double above, double below) {
    ue3::Function* const fn = gTraceRef.Get();
    if (fn == nullptr || pawn == nullptr) {
        return std::nullopt;
    }
    ue3::Params parms(ue3::runtime::Get(), fn);
    parms.Set("TraceEnd", ue3::Vector{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z - below)});
    parms.Set("TraceStart", ue3::Vector{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z + above)});
    parms.SetBool("bTraceActors", true);
    ue3::runtime::Call(pawn, fn, parms);
    if (parms.Return<ue3::Object*>().value_or(nullptr) == nullptr) {
        return std::nullopt;
    }
    std::optional<ue3::Vector> const hit = parms.Get<ue3::Vector>("HitLocation");
    return hit ? std::optional<double>(hit->z) : std::nullopt;
}

// Walks the world's cover list for the slots that vault or climb.
void RefreshSlots(ue3::Object* pawn, double now) {
    void const* const world = engine::World();
    if (world == gSlotsWorld && now - gSlotsAt < kSlotRefreshS) {
        return;
    }
    gSlotsAt = now;
    bool const newWorld = world != gSlotsWorld;
    gSlotsWorld = world;
    ue3::Object* const worldInfo = ObjectField(pawn, gWorldInfo);
    ue3::Object* link = ObjectField(worldInfo, gCoverList);
    std::vector<Slot> slots;
    unsigned links = 0;
    while (link != nullptr && links < 4096 && slots.size() < kMaxSlots) {
        ++links;
        if (ue3::runtime::IsAlive(link)) {
            if (std::optional<ue3::Elements> const elements = gLinkSlots.ElementsOf(link, 256)) {
                for (std::int32_t i = 0; i < elements->count && slots.size() < kMaxSlots; ++i) {
                    void const* const slot = elements->At(i);
                    bool const mantle = gSlotCanMantle.ReadBool(slot).value_or(false);
                    bool const climb = gSlotCanClimbUp.ReadBool(slot).value_or(false);
                    if ((!mantle && !climb) || !gSlotEnabled.ReadBool(slot).value_or(true)) {
                        continue;
                    }
                    Slot s;
                    s.link = link;
                    s.index = i;
                    s.name = std::format("{} slot {}", ShortName(link), i);
                    s.kind = mantle ? tv::Kind::Vault : tv::Kind::ClimbUp;
                    std::optional<tv::Vec3> const at = SlotLocation(link, i);
                    std::optional<double> const yaw = SlotYaw(link, i);
                    if (!at || !yaw) {
                        continue;
                    }
                    s.at = *at;
                    s.yaw = *yaw;
                    void const* const target = gSlotMantleTarget.Address(slot);
                    ue3::Object* const targetLink = target != nullptr ? gTargetActor.Read<ue3::Object*>(target).value_or(nullptr) : nullptr;
                    int const targetIndex = target != nullptr ? gTargetSlotIdx.Read<std::int32_t>(target).value_or(-1) : -1;
                    if (targetLink != nullptr && targetIndex >= 0 && ue3::runtime::IsAlive(targetLink)) {
                        s.target = std::format("{} slot {}", ShortName(targetLink), targetIndex);
                        s.targetAt = SlotLocation(targetLink, targetIndex);
                    }
                    slots.push_back(std::move(s));
                }
            }
        }
        link = ObjectField(link, gNextCoverLink);
    }
    // Keep each slot's traced top across refreshes.
    for (Slot& s : slots) {
        for (Slot const& old : gSlots) {
            if (old.link == s.link && old.index == s.index) {
                s.topZ = old.topZ;
                s.topTried = old.topTried;
            }
        }
    }
    bool const changed = slots.size() != gSlots.size();
    gSlots = std::move(slots);
    if (newWorld || changed || !gToldSlots) {
        gToldSlots = true;
        unsigned vaults = 0;
        for (Slot const& s : gSlots) {
            vaults += s.kind == tv::Kind::Vault ? 1u : 0u;
        }
        log::Info("traversal: {} cover links in the world, {} slots that vault and {} that climb", links, vaults,
            gSlots.size() - vaults);
        gNear = Near{};
    }
}

// ---- The game's own admission, with the two one-call writes (journalled, put back in a __finally) ----------------------------
struct OneCallWrites {
    std::uint32_t* flagWord = nullptr;
    std::uint32_t flagMask = 0;
    std::uint32_t flagWas = 0;
    float* joyUp = nullptr;
    float joyUpWas = 0.0f;
    float* joyRight = nullptr;
    float joyRightWas = 0.0f;
    std::uint32_t* clearWord = nullptr;  // a flag cleared for the call (the held weapon's bIsZoomed)
    std::uint32_t clearMask = 0;
    std::uint32_t clearWas = 0;
    std::int32_t* yaw = nullptr;         // the pawn's Rotation.Yaw, the head's for the call ([Traversal] facing = head)
    std::int32_t yawWas = 0;
    std::int32_t yawSet = 0;
    std::uint8_t* state = nullptr;       // BioPawn.ActionState, combat for the call with the weapon away ([Traversal] holstered)
    std::uint8_t stateWas = 0;
    std::uint8_t stateSet = 0;
};
using CallThunk = void (*)(void* context);

// POD only (no unwinding objects): the writes, the call, and the writes put back whatever happens in the call.
void CallWithOneCallWrites(OneCallWrites const& w, CallThunk call, void* context) {
    __try {
        if (w.flagWord != nullptr) {
            *w.flagWord |= w.flagMask;
        }
        if (w.joyUp != nullptr) {
            *w.joyUp = 1.0f;
        }
        if (w.joyRight != nullptr) {
            *w.joyRight = 0.0f;
        }
        if (w.clearWord != nullptr) {
            *w.clearWord &= ~w.clearMask;
        }
        if (w.yaw != nullptr) {
            *w.yaw = w.yawSet;
        }
        if (w.state != nullptr) {
            *w.state = w.stateSet;
        }
        call(context);
    } __finally {
        if (w.state != nullptr) {
            *w.state = w.stateWas;
        }
        if (w.yaw != nullptr) {
            *w.yaw = w.yawWas;
        }
        if (w.flagWord != nullptr) {
            *w.flagWord = (*w.flagWord & ~w.flagMask) | (w.flagWas & w.flagMask);
        }
        if (w.joyUp != nullptr) {
            *w.joyUp = w.joyUpWas;
        }
        if (w.joyRight != nullptr) {
            *w.joyRight = w.joyRightWas;
        }
        if (w.clearWord != nullptr) {
            *w.clearWord = (*w.clearWord & ~w.clearMask) | (w.clearWas & w.clearMask);
        }
    }
}

struct CallContext {
    ue3::Object* object = nullptr;
    ue3::Function* function = nullptr;
    ue3::Params* params = nullptr;
};

void CallThunkImpl(void* context) {
    auto* const c = static_cast<CallContext*>(context);
    ue3::runtime::Call(c->object, c->function, *c->params);
}

bool FillInfo(ue3::FieldRef& info, ue3::Params& parms, Slot const& slot) {
    std::uint8_t* const base = static_cast<std::uint8_t*>(info.Address(parms.Data()));
    if (base == nullptr) {
        return false;
    }
    ue3::PropertyInfo const* const link = gInfoLink.Get();
    ue3::PropertyInfo const* const lt = gInfoLt.Get();
    ue3::PropertyInfo const* const rt = gInfoRt.Get();
    ue3::PropertyInfo const* const pct = gInfoPct.Get();
    ue3::PropertyInfo const* const location = gInfoLocation.Get();
    ue3::PropertyInfo const* const normal = gInfoNormal.Get();
    ue3::PropertyInfo const* const tangent = gInfoTangent.Get();
    if (link == nullptr || lt == nullptr || rt == nullptr || pct == nullptr || location == nullptr) {
        return false;
    }
    std::int32_t const index = slot.index;
    float const zero = 0.0f;
    double const y = slot.yaw * kPi / 180.0;
    ue3::Vector const at{static_cast<float>(slot.at.x), static_cast<float>(slot.at.y), static_cast<float>(slot.at.z)};
    ue3::Vector const out{static_cast<float>(-std::cos(y)), static_cast<float>(-std::sin(y)), 0.0f};  // away from the cover
    ue3::Vector const along{static_cast<float>(-std::sin(y)), static_cast<float>(std::cos(y)), 0.0f};
    std::memcpy(base + link->offset, &slot.link, sizeof(ue3::Object*));  // a 4-byte packed struct: memcpy, not a pointer store
    std::memcpy(base + lt->offset, &index, sizeof index);
    std::memcpy(base + rt->offset, &index, sizeof index);
    std::memcpy(base + pct->offset, &zero, sizeof zero);
    std::memcpy(base + location->offset, &at, sizeof at);
    if (normal != nullptr) {
        std::memcpy(base + normal->offset, &out, sizeof out);
    }
    if (tangent != nullptr) {
        std::memcpy(base + tangent->offset, &along, sizeof along);
    }
    return true;
}

// The head's flat yaw in the world (degrees), from the view the mod drew this tick; nullopt without one, or looking straight up or
// down.
std::optional<double> HeadYaw() {
    std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
    if (!frame) {
        return std::nullopt;
    }
    engine::Basis const axes = engine::OrientationToWorld(*frame, frame->head.orientation);
    if (std::hypot(axes.forward.x, axes.forward.y) < 1e-3) {
        return std::nullopt;
    }
    return std::atan2(axes.forward.y, axes.forward.x) * 180.0 / kPi;
}

// The game's CanPerformMantle / CanPerformClimb for `slot`, standing where the pawn stands: m_bCanMantleOutsideOfCover set and the
// stick's push (RemappedJoyUp 1, RemappedJoyRight 0) for this call only; with [Traversal] facing = head the pawn's yaw the head's,
// with [Traversal] holstered the weapon away asked as drawn (each for the call only). Nullopt if it can't be asked.
// `facingYaw` (the step to the vault): the pawn's yaw for the call is that instead of the head's (the commit turns the pawn to the
// slot's direction anyway; the player's facing was judged when the step began).
std::optional<bool> GameAdmits(ue3::Object* pc, Slot const& slot, std::optional<double> facingYaw = std::nullopt) {
    bool const climb = slot.kind == tv::Kind::ClimbUp;
    ue3::Function* const fn = climb ? gCanPerformClimbRef.Get() : gCanPerformMantleRef.Get();
    ue3::PropertyInfo const* const flag = gCanMantleOutside.Get();
    float* const joyUp = gRemappedJoyUp.In<float>(pc);
    float* const joyRight = gRemappedJoyRight.In<float>(pc);
    if (fn == nullptr || flag == nullptr || flag->boolMask == 0 || joyUp == nullptr || joyRight == nullptr || !ue3::runtime::IsAlive(slot.link)) {
        return std::nullopt;
    }
    ue3::Params parms(ue3::runtime::Get(), fn);
    if (!FillInfo(climb ? gCpcInfo : gCpmInfo, parms, slot)) {
        return std::nullopt;
    }
    OneCallWrites w;
    w.flagWord = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(pc) + flag->offset);
    w.flagMask = flag->boolMask;
    w.flagWas = *w.flagWord;
    w.joyUp = joyUp;
    w.joyUpWas = *joyUp;
    w.joyRight = joyRight;
    w.joyRightWas = *joyRight;
    write_journal::Wrote("Traversal.m_bCanMantleOutsideOfCover (one call)", w.flagWord, sizeof(std::uint32_t));
    write_journal::Wrote("Traversal.RemappedJoyUp (one call)", joyUp, sizeof(float));
    write_journal::Wrote("Traversal.RemappedJoyRight (one call)", joyRight, sizeof(float));
    // A zoomed weapon (the two-handed hold's LT): its bIsZoomed cleared for this call only.
    ue3::Object* const pawn = player::Pawn(pc);
    ue3::Object* const weapon = pawn != nullptr ? gPawnWeapon.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
    ue3::PropertyInfo const* const zoom = gWeaponZoomed.Get();
    if (weapon != nullptr && zoom != nullptr && zoom->boolMask != 0 && ue3::runtime::IsAlive(weapon) && ue3::IsA(weapon, gWeaponNativeBase)) {
        auto* const word = reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(weapon) + zoom->offset);
        if ((*word & zoom->boolMask) != 0) {
            w.clearWord = word;
            w.clearMask = zoom->boolMask;
            w.clearWas = *word;
            write_journal::Wrote("Traversal.bIsZoomed cleared (one call)", word, sizeof(std::uint32_t));
            ++gCounters.zoomedAdmissions;
            if (!gToldZoomed) {
                gToldZoomed = true;
                log::Info("traversal: the weapon is zoomed (the support grip's aim): its bIsZoomed cleared for the game's {} call "
                          "only, so a two-handed hold can vault", climb ? "CanPerformClimb" : "CanPerformMantle");
            }
        }
    }
    // The head's facing, not the pawn's (the stick's turn, invisible in first person): the pawn's yaw the head's for the call.
    if (!facingYaw) {
        gNear.headDot.reset();
    }
    if ((gSettings.headFacing || facingYaw) && pawn != nullptr) {
        ue3::Rotator* const rotation = gPawnRotation.In<ue3::Rotator>(pawn);
        if (std::optional<double> const head = facingYaw ? facingYaw : HeadYaw(); head && rotation != nullptr) {
            w.yaw = &rotation->yaw;
            w.yawWas = rotation->yaw;
            w.yawSet = Units(*head);
            write_journal::Wrote("Traversal.Rotation.Yaw (one call)", w.yaw, sizeof(std::int32_t));
            if (!facingYaw) {
                double const y = *head * kPi / 180.0;
                double const slotYaw = slot.yaw * kPi / 180.0;
                gNear.headDot = std::cos(y) * std::cos(slotYaw) + std::sin(y) * std::sin(slotYaw);
            }
        }
    }
    // The weapon away (only holsters draw, no snap's draw): asked as drawn, nothing drawn.
    gNear.asDrawn = false;
    if (gSettings.holstered && pawn != nullptr) {
        std::uint8_t* const state = gPawnActionState.In<std::uint8_t>(pawn);
        if (state != nullptr && *state != kActionStateCombat) {
            w.state = state;
            w.stateWas = *state;
            w.stateSet = kActionStateCombat;
            write_journal::Wrote("Traversal.ActionState (one call)", state, sizeof(std::uint8_t));
            gNear.asDrawn = true;
        }
    }
    CallContext context{pc, fn, &parms};
    CallWithOneCallWrites(w, &CallThunkImpl, &context);
    if (((*w.flagWord ^ w.flagWas) & w.flagMask) != 0 || *joyUp != w.joyUpWas || *joyRight != w.joyRightWas ||
        (w.clearWord != nullptr && ((*w.clearWord ^ w.clearWas) & w.clearMask) != 0) || (w.yaw != nullptr && *w.yaw != w.yawWas) ||
        (w.state != nullptr && *w.state != w.stateWas)) {
        ++gCounters.faultsRestored;  // never expected: the __finally put them back
    }
    return parms.ReturnBool();
}

// The game's own move from where the pawn stands: SFXPlayerController.MantleCover / ClimbCover(the slot).
bool CallMove(ue3::Object* pc, Slot const& slot) {
    // Recheck immediately before mutation; a press/near-slot result can predate a scene transition.
    if (gLink.authoredSeated || gLink.menuOpen || player::GameMode(pc) != 0 ||
        gIgnoreMove.Read<std::uint8_t>(pc).value_or(1) != 0) return false;
    bool const climb = slot.kind == tv::Kind::ClimbUp;
    ue3::Function* const fn = climb ? gClimbCoverRef.Get() : gMantleCoverRef.Get();
    if (fn == nullptr || !ue3::runtime::IsAlive(slot.link)) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), fn);
    if (!FillInfo(climb ? gClimbInfo : gMantleInfo, parms, slot)) {
        return false;
    }
    // Admitted with the head's facing: the move plays along the pawn's, so the pawn is turned to the slot's direction first
    // (over the obstacle, not along the stick's last heading).
    if (gSettings.headFacing) {
        ue3::Object* const pawn = player::Pawn(pc);
        std::optional<ue3::Rotator> const was = pawn != nullptr ? player::Rotation(pawn) : std::nullopt;
        ue3::Function* const setRotation = gSetRotationRef.Get();
        if (was && setRotation != nullptr) {
            double const off = std::remainder(Degrees(was->yaw) - slot.yaw, 360.0);
            if (std::abs(off) > 1.0) {
                ue3::Params rp(ue3::runtime::Get(), setRotation);
                rp.Set("NewRotation", ue3::Rotator{0, Units(slot.yaw), 0});
                ue3::runtime::Call(pawn, setRotation, rp);
                log::Info("traversal: double tap: the pawn turned {:+.0f} deg to {}'s direction before the move (admitted with the head's "
                          "facing)", -off, slot.name);
            }
        }
    }
    ue3::runtime::Call(pc, fn, parms);
    return true;
}

ue3::Object* SelectedForUse(ue3::Object* pc) {
    ue3::Object* const selection = ObjectField(pc, gPlayerSelection);
    ue3::Object* const target = ObjectField(selection, gSelectionTarget);
    if (target == nullptr || gSelectionCombat.ReadBool(selection).value_or(false)) {
        return nullptr;
    }
    return target;
}

// The nearest slot to the pawn and, every kAdmitEveryS, the game's verdict on it.
void UpdateNear(ue3::Object* pc, ue3::Object* pawn, double now) {
    std::optional<ue3::Vector> const at = player::Location(pawn);
    std::optional<ue3::Rotator> const rot = player::Rotation(pawn);
    if (!at || !rot) {
        gNear = Near{};
        return;
    }
    tv::Vec3 const p = V(*at);
    double const upm = 100.0;
    int best = -1;
    double bestD = 1e18;
    for (std::size_t i = 0; i < gSlots.size(); ++i) {
        Slot const& s = gSlots[i];
        tv::Vec3 const d = s.at - p;
        if (std::abs(d.z) > 120.0) {
            continue;
        }
        double const flat = tv::LengthXY(d);
        if (flat < bestD) {
            bestD = flat;
            best = static_cast<int>(i);
        }
    }
    if (best < 0 || bestD > gSettings.reachM * upm) {
        if (gNear.slot >= 0 && gNear.loggedSlot != -1) {
            log::Info("traversal: no slot that vaults or climbs within {:.1f} m any more", gSettings.reachM);
            gNear.loggedSlot = -1;
        }
        gNear.slot = -1;
        gNear.admitted = false;
        return;
    }
    if (best != gNear.slot) {
        gNear.slot = best;
        gNear.askedAt = -1e9;
        gNear.admitted = false;
    }
    Slot const& s = gSlots[static_cast<std::size_t>(best)];
    gNear.distanceM = tv::Length(s.at - p) / upm;
    double const yaw = Degrees(rot->yaw) * kPi / 180.0;
    double const slotYaw = s.yaw * kPi / 180.0;
    gNear.facingDot = std::cos(yaw) * std::cos(slotYaw) + std::sin(yaw) * std::sin(slotYaw);
    if (now - gNear.askedAt >= kAdmitEveryS) {
        gNear.askedAt = now;
        std::optional<bool> const yes = GameAdmits(pc, s);
        gNear.asked = yes.has_value();
        gNear.admitted = yes.value_or(false);
        ++gCounters.admitAsks;
        gCounters.admitYes += gNear.admitted ? 1u : 0u;
    }
    if ((gNear.loggedSlot != best || gNear.loggedAdmitted != gNear.admitted) && now - gNear.loggedAt >= kNearLogS) {
        gNear.loggedSlot = best;
        gNear.loggedAdmitted = gNear.admitted;
        gNear.loggedAt = now;
        float const distance = gMantleDistance.Read<float>(pc).value_or(-1.0f);
        float const face = gMantleFace.Read<float>(pc).value_or(-1.0f);
        std::string asked;
        if (gNear.headDot) {
            asked += std::format("; asked with the head's facing, {:.0f} deg off", std::acos(std::clamp(*gNear.headDot, -1.0, 1.0)) * 180.0 / kPi);
        }
        if (gNear.asDrawn) {
            asked += "; the weapon away: asked as drawn";
        }
        log::Info("traversal: next to {} ({}{}): the pawn {:.2f} m from the slot, facing {:.0f} deg off its direction (dot {:.2f}); "
                  "the game {} ({}: within {:.0f} uu, dot >= {:.2f}, the landing clear){}{}",
            s.name, tv::Name(s.kind), s.target.empty() ? std::string() : " over to " + s.target, gNear.distanceM,
            std::acos(std::clamp(gNear.facingDot, -1.0, 1.0)) * 180.0 / kPi, gNear.facingDot,
            !gNear.asked ? "can't be asked" : gNear.admitted ? "admits it" : "refuses it",
            s.kind == tv::Kind::ClimbUp ? "CanPerformClimb" : "CanPerformMantle", distance, face, asked,
            SelectedForUse(pc) != nullptr ? "; A would use " + ShortName(SelectedForUse(pc)) + " first" : std::string());
    }
}

// ---- One move ---------------------------------------------------------------------------------------------------------------------
std::optional<double> HandFromGrip(int hand) {
    std::optional<arm_ik::HandsPlaced> const placed = arm_ik::MeasureHands();
    if (!placed || !placed->valid[static_cast<std::size_t>(hand)]) {
        return std::nullopt;
    }
    return placed->fromGripM[static_cast<std::size_t>(hand)];
}

void BeginMove(ue3::Object* pc, ue3::Object* pawn, std::uint8_t action, double now) {
    gMove = Move{};
    gMove.active = true;
    gMove.action = action;
    gMove.kind = KindOf(action);
    gMove.startS = now;
    gMove.weaponBefore = WeaponName(pawn);
    gMove.cameraMode = CameraModeName(pc);
    gMove.record.Begin(gMove.kind);
    std::optional<ue3::Vector> const at = player::Location(pawn);
    std::optional<ue3::Rotator> const rot = player::Rotation(pawn);
    gMove.pawnStart = at ? V(*at) : tv::Vec3{};
    gMove.pawnYawStart = rot ? Degrees(rot->yaw) : 0.0;
    if (now - gPendingStanceS < 0.5) {
        gMove.stance = gPendingStance;
    }
    if (gLastPov.valid) {
        // The view before the move (the camera update before the custom action began).
        gMove.view.Begin(gLastPov.pose);
        gMove.lastCameraS = now;
    }
    for (int hand = 0; hand < 2; ++hand) {
        gMove.handsStart[static_cast<std::size_t>(hand)] = HandFromGrip(hand).value_or(-1.0);
        gMove.handsMax[static_cast<std::size_t>(hand)] = gMove.handsStart[static_cast<std::size_t>(hand)];
    }
    vr::host_view::State const view = vr::host_view::Latest();
    gMove.headValid = view.valid;
    gMove.headStart = view.head.position;
    // Which press began it is judged in the mod's tick (JudgeMove), after that tick's presses: a move the game begins by itself starts
    // inside the engine tick, before the mod has seen the press.
    gMove.ours = now - gOurCommitS < 0.5;
    if (gNear.slot >= 0) {
        Slot const& s = gSlots[static_cast<std::size_t>(gNear.slot)];
        gMove.target = s.name + (s.target.empty() ? std::string() : " -> " + s.target);
    }
    ue3::Object* const link = ObjectField(pawn, gPawnCurrentLink);
    ++gMoves;
    log::Info("traversal: {} began (custom action {}, {}): the pawn at {} yaw {:.0f}, {} cover, weapon {}, camera mode {}, the head at "
              "({:.2f}, {:.2f}, {:.2f}) m; target {}",
        tv::Name(gMove.kind), action, gMove.ours ? "the double tap's" : "the game's own", Fmt(gMove.pawnStart), gMove.pawnYawStart,
        link != nullptr ? "in " + ShortName(link) : std::string("out of"), gMove.weaponBefore, gMove.cameraMode, gMove.headStart.x,
        gMove.headStart.y, gMove.headStart.z, gMove.target.empty() ? std::string("unknown") : gMove.target);
}

// The rules' verdict on the press that began the move (core/vr/Traversal Judge): once, in the mod's tick after its presses.
void JudgeMove() {
    if (!gMove.active || gMove.judged) {
        return;
    }
    gMove.judged = true;
    gMove.commit = tv::Judge(gPresses, gMove.startS);
    if (gMove.commit.why != tv::CommitWhy::Allowed) {
        ++gCounters.violations;
        ++gTotals.violations;
    }
    log::Info("traversal: the {} began {:+.3f} s after press #{} went down: {}", tv::Name(gMove.kind), gMove.commit.sincePressS,
        gMove.commit.press, tv::Name(gMove.commit.why));
}

void EndMove(ue3::Object* pawn, double now) {
    gMove.active = false;
    gMove.settling = gMove.view.Started();
    gMove.endS = now;
    gMove.record.End(now - gMove.startS);
    gMove.view.End();
    gMove.weaponAfter = WeaponName(pawn);
    vr::host_view::State const view = vr::host_view::Latest();
    gMove.headEnd = view.head.position;
}

std::string HandText(int hand) {
    std::size_t const h = static_cast<std::size_t>(hand);
    if (gMove.handsTicks[h] == 0) {
        return "-";
    }
    return std::format("{} of {} ticks within {:.2f} m, mean {:.2f}, max {:.2f} (at the start {:.2f})", gMove.handsNear[h], gMove.handsTicks[h],
        kHandsNearM, gMove.handsSum[h] / gMove.handsTicks[h], gMove.handsMax[h], gMove.handsStart[h]);
}

// The move's line once the view has come to rest and movement and aim are back (or 3 s after the custom action's end).
void Report(double now) {
    tv::Summary const s = gMove.record.Finish();
    double const fromEnd = now - gMove.endS;
    double const headMoved = std::sqrt(std::pow(gMove.headEnd.x - gMove.headStart.x, 2.0) + std::pow(gMove.headEnd.y - gMove.headStart.y, 2.0) +
        std::pow(gMove.headEnd.z - gMove.headStart.z, 2.0));
    log::Info("traversal: {} over: the custom action {:.2f} s (stance {}), the view at rest {} ({} camera updates); the pawn went {:.2f} m "
              "flat, rose {:.2f} m, ended {:+.2f} m, turned {:+.0f} deg, in steps of up to {:.2f} m; it blocked no actors for {:.2f} s; the "
              "game's camera went {:.2f} m, rose {:.2f} m and ended {:+.2f} m, at most {:.2f} m/s ({:.2f} m/s vertically), bob {:.3f} m, largest "
              "step {:.3f} m, pitched {:+.1f}..{:+.1f} deg (from {:+.1f}), rolled {:+.1f}..{:+.1f}; movement back {}, aim back {}; weapon {} before, "
              "{} after: {}; left wrist {}; right wrist {}; the head moved {:.2f} m in the room; view {} ({} writes), mask {} (peak {:.2f})",
        tv::Name(gMove.kind), s.seconds, gMove.stance, s.settledS >= 0.0 ? std::format("{:.2f} s after it", s.settledS) : std::string("not seen"),
        s.samples, s.horizontalM, s.riseM, s.dropM, s.yawTurnDeg, s.pawnStepM, s.collisionOffS, s.cameraTravelM, s.cameraRiseM, s.cameraDropM,
        s.cameraPeakSpeedMS, s.cameraPeakVerticalMS, s.cameraBobM, s.maxStepM, s.pitchMinDeg, s.pitchMaxDeg, s.pitchStartDeg, s.rollMinDeg,
        s.rollMaxDeg,
        gMove.movementBackS >= 0.0 ? std::format("{:.2f} s after the end", gMove.movementBackS) : std::string("not seen"),
        gMove.aimBackS >= 0.0 ? std::format("{:.2f} s after the end", gMove.aimBackS) : std::string("not seen"), gMove.weaponBefore,
        gMove.weaponAfter, gMove.weaponBefore == gMove.weaponAfter ? "the same weapon" : "THE WEAPON CHANGED", HandText(0), HandText(1),
        headMoved, tv::Name(gSettings.view), gMove.viewWrites, tv::Name(gSettings.mask), gMove.maskPeak);
    namespace json = vr::test_request;
    auto const hand = [](int i) {
        std::size_t const h = static_cast<std::size_t>(i);
        return std::format("{{\"ticks\": {}, \"near\": {}, \"meanM\": {}, \"maxM\": {}, \"startM\": {}}}", gMove.handsTicks[h], gMove.handsNear[h],
            json::JsonNumber(gMove.handsTicks[h] > 0 ? gMove.handsSum[h] / gMove.handsTicks[h] : -1.0), json::JsonNumber(gMove.handsMax[h]),
            json::JsonNumber(gMove.handsStart[h]));
    };
    gLastSummary = std::format(
        "{{\"kind\": {}, \"seconds\": {}, \"settledS\": {}, \"samples\": {}, \"stance\": {}, \"horizontalM\": {}, \"riseM\": {}, \"dropM\": {}, "
        "\"yawTurnDeg\": {}, \"pawnStepM\": {}, \"collisionOffS\": {}, \"cameraTravelM\": {}, \"cameraRiseM\": {}, \"cameraDropM\": {}, "
        "\"cameraPeakSpeedMS\": {}, \"cameraPeakVerticalMS\": {}, \"cameraBobM\": {}, \"maxStepM\": {}, \"pitchMinDeg\": {}, \"pitchMaxDeg\": {}, "
        "\"rollMinDeg\": {}, \"rollMaxDeg\": {}, \"movementBackS\": {}, \"aimBackS\": {}, \"weaponBefore\": {}, \"weaponAfter\": {}, "
        "\"hands\": [{}, {}], \"headMovedM\": {}, \"view\": {}, \"viewWrites\": {}, \"mask\": {}, \"maskPeak\": {}, \"commit\": {}, \"press\": {}, "
        "\"target\": {}, \"reportedAfterS\": {}}}",
        json::JsonString(tv::Name(gMove.kind)), json::JsonNumber(s.seconds), json::JsonNumber(s.settledS), s.samples, json::JsonString(gMove.stance),
        json::JsonNumber(s.horizontalM), json::JsonNumber(s.riseM), json::JsonNumber(s.dropM), json::JsonNumber(s.yawTurnDeg),
        json::JsonNumber(s.pawnStepM), json::JsonNumber(s.collisionOffS), json::JsonNumber(s.cameraTravelM), json::JsonNumber(s.cameraRiseM),
        json::JsonNumber(s.cameraDropM), json::JsonNumber(s.cameraPeakSpeedMS), json::JsonNumber(s.cameraPeakVerticalMS),
        json::JsonNumber(s.cameraBobM), json::JsonNumber(s.maxStepM), json::JsonNumber(s.pitchMinDeg), json::JsonNumber(s.pitchMaxDeg),
        json::JsonNumber(s.rollMinDeg), json::JsonNumber(s.rollMaxDeg), json::JsonNumber(gMove.movementBackS), json::JsonNumber(gMove.aimBackS),
        json::JsonString(gMove.weaponBefore), json::JsonString(gMove.weaponAfter), hand(0), hand(1), json::JsonNumber(headMoved),
        json::JsonString(tv::Name(gSettings.view)), gMove.viewWrites, json::JsonString(tv::Name(gSettings.mask)), json::JsonNumber(gMove.maskPeak),
        json::JsonString(tv::Name(gMove.commit.why)), gMove.commit.press, json::JsonString(gMove.target), json::JsonNumber(fromEnd));
    gMove.reported = true;
    gMove.settling = false;
}

// The custom action, polled (the mod's tick and each camera update): a move's start, end, and what comes back after.
void Observe(ue3::Object* pc, ue3::Object* pawn, double now) {
    std::uint8_t const action = CurrentAction(pc);
    bool const traversal = action == kActionMantle || action == kActionClimb;
    if (traversal && !gMove.active) {
        BeginMove(pc, pawn, action, now);
    } else if (!traversal && gMove.active) {
        EndMove(pawn, now);
    }
    if (!gMove.active && gMove.endS > 0.0 && !gMove.reported) {
        bool const move = gIgnoreMove.Read<std::uint8_t>(pc).value_or(0) == 0 && gPhysics.Read<std::uint8_t>(pawn).value_or(0) == kPhysWalking;
        bool const look = gIgnoreLook.Read<std::uint8_t>(pc).value_or(0) == 0;
        if (move && gMove.movementBackS < 0.0) {
            gMove.movementBackS = now - gMove.endS;
        }
        if (look && gMove.aimBackS < 0.0) {
            gMove.aimBackS = now - gMove.endS;
        }
        bool const back = gMove.movementBackS >= 0.0 && gMove.aimBackS >= 0.0 && now - gMove.endS >= gSettings.settleS;
        if ((back && gMove.record.Settled()) || now - gMove.endS > 3.0) {
            Report(now);
        }
    }
}

// ---- The commit, and the step to the vault ------------------------------------------------------------------------------------------
// The game's own move on the slot, from where the pawn stands (`how`: the double tap, or the step that walked there).
void Commit(ue3::Object* pc, ue3::Object* pawn, Slot const& s, double now, char const* how) {
    gOurCommitS = now;
    bool const called = CallMove(pc, s);
    std::uint8_t const action = CurrentAction(pc);
    bool const began = action == kActionMantle || action == kActionClimb;
    ++gCounters.commits;
    ++gTotals.commits;
    if (!began) {
        ++gCounters.commitsRefused;
        ++gTotals.commitsRefused;
    }
    log::Info("traversal: {}: {}({}) {}", how, s.kind == tv::Kind::ClimbUp ? "ClimbCover" : "MantleCover", s.name,
        !called ? "couldn't be called" : began ? std::format("began the game's custom action {}", action)
                                               : "was called, but no custom action began");
    if (began) {
        Observe(pc, pawn, Now());
        JudgeMove();
    }
}

tv::StepSettings StepSettingsNow() {
    tv::StepSettings s;
    s.enabled = gSettings.stepTo && gBioPlayerInputRef.Get() != nullptr;
    s.reachM = gSettings.stepReachM;
    s.facingDeg = gSettings.stepFacingDeg;
    return s;
}

// Where the player stands for the step's reach: the view's place (the pawn can sit 0.4 m from it), else the pawn's; facing: the head's.
tv::StepReach NearStepReach(ue3::Object* pawn) {
    if (gNear.slot < 0 || static_cast<std::size_t>(gNear.slot) >= gSlots.size()) {
        return {};
    }
    Slot const& s = gSlots[static_cast<std::size_t>(gNear.slot)];
    std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
    std::optional<ue3::Vector> const at = player::Location(pawn);
    std::optional<ue3::Rotator> const rot = player::Rotation(pawn);
    std::optional<double> const head = HeadYaw();
    if ((!frame && !at) || (!head && !rot)) {
        return {};
    }
    tv::Vec3 const from = frame ? tv::Vec3{frame->headWorld.x, frame->headWorld.y, frame->headWorld.z} : V(*at);
    return tv::MayStepTo(from, head ? *head : Degrees(rot->yaw), s.at, s.yaw, StepSettingsNow());
}

bool StartStep(ue3::Object* pawn, double now, tv::StepReach const& reach) {
    std::optional<ue3::Vector> const at = player::Location(pawn);
    if (!reach.ok || gNear.slot < 0 || !at) {
        return false;
    }
    Slot const& s = gSlots[static_cast<std::size_t>(gNear.slot)];
    double const fromPawn = tv::LengthXY(s.at - V(*at)) / 100.0;
    gStep.Start(now, fromPawn);
    gStepSlot = gNear.slot;
    gStepSlotName = s.name;
    gStepAskedAt = -1e9;
    gStepAdmitted = false;
    gStepFromViewM = reach.distanceM;
    gStepFromPawnM = fromPawn;
    gStepOffDeg = reach.offDeg;
    gStepStick = {};
    ++gSteps;
    log::Info("traversal: double tap {:.2f} m from {} (the pawn {:.2f} m from it), the head {:.0f} deg off its direction: the game doesn't "
              "admit the {} from here, so Shepard steps to it first (the game's own walk, the mod's stick; yours takes over)",
        reach.distanceM, s.name, fromPawn, reach.offDeg, tv::Name(s.kind));
    return true;
}

void EndStep(char const* why, double now, std::optional<double> fromPawnM, Slot const* slot = nullptr) {
    gStep.Cancel();
    gStepStick.active = false;
    ++gStepEnded;
    // Where the game admitted (or not): from the pawn (its measure: 50 uu in 3D from the pawn's centre) and from the view.
    std::string view;
    if (std::optional<engine::TrackingFrame> const frame = view_anchor::Current(); frame && slot != nullptr) {
        view = std::format(", the view {:.2f} m", tv::LengthXY(slot->at - tv::Vec3{frame->headWorld.x, frame->headWorld.y, frame->headWorld.z}) / 100.0);
    }
    log::Info("traversal: the step to {} ended after {:.2f} s, {:.2f} m walked{}{}, the stick written {} times: {}", gStepSlotName,
        gStep.Elapsed(now), gStep.Walked(), fromPawnM ? std::format(", the pawn {:.2f} m from the slot", *fromPawnM) : std::string(), view,
        gStepStick.writes, why);
}

void TickStep(ue3::Object* pc, ue3::Object* pawn, double now, Link const& link, bool onFoot, bool traversing) {
    if (!gStep.Active()) {
        gStepStick.active = false;
        return;
    }
    // gSlots is walked again every 2 s: the step follows its slot by name.
    if (gStepSlot < 0 || static_cast<std::size_t>(gStepSlot) >= gSlots.size() || gSlots[static_cast<std::size_t>(gStepSlot)].name != gStepSlotName) {
        gStepSlot = -1;
        for (std::size_t i = 0; i < gSlots.size(); ++i) {
            if (gSlots[i].name == gStepSlotName) {
                gStepSlot = static_cast<int>(i);
                break;
            }
        }
        if (gStepSlot < 0) {
            EndStep("its slot is gone", now, std::nullopt);
            return;
        }
    }
    char const* const cancel = link.authoredSeated ? (link.sceneWhy != nullptr ? link.sceneWhy : "the scene blocks it")
        : link.menuOpen ? "a menu opened"
        : !onFoot ? "not on foot"
        : traversing ? "a move began"
        : !link.headTrackedView ? "no head-tracked view"
        : nullptr;
    std::optional<ue3::Vector> const at = player::Location(pawn);
    std::optional<ue3::Rotator> const camera = player::Rotation(pc);
    if (cancel != nullptr || !at || !camera) {
        EndStep(cancel != nullptr ? cancel : "Shepard's or the camera's place unknown", now, std::nullopt);
        return;
    }
    Slot const& s = gSlots[static_cast<std::size_t>(gStepSlot)];
    double const fromPawn = tv::LengthXY(s.at - V(*at)) / 100.0;
    if (now - gStepAskedAt >= 0.05) {
        gStepAskedAt = now;
        gStepAdmitted = GameAdmits(pc, s, s.yaw).value_or(false);  // facing the slot: the commit turns the pawn to it
    }
    vr::controllers::State const controllers = vr::controllers::Latest();
    double const own = controllers.active ? std::hypot(controllers.left.stickX, controllers.left.stickY) : 0.0;
    tv::StepSettings const settings = StepSettingsNow();
    tv::StepEnd const end = gStep.Step(now, fromPawn, gStepAdmitted, own, settings);
    if (end == tv::StepEnd::None) {
        gStepStick.active = true;
        gStepStick.stick = tv::StickToward(V(*at), s.at, Degrees(camera->yaw), settings);
        gStepStick.atS = now;
        return;
    }
    EndStep(tv::Name(end), now, fromPawn, &s);
    if (end == tv::StepEnd::Admitted) {
        ++gStepVaults;
        Commit(pc, pawn, s, now, "the step's end");
    }
}

// ---- Handlers ----------------------------------------------------------------------------------------------------------------------
// Engine.Camera.UpdateCamera, after the game's: the move's sample, and the level view written into the POV.
void OnUpdateCamera(ue3::Object* camera, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    if (!gStarted || !gSettings.enabled) {
        return;
    }
    ue3::Object* const pc = player::Controller();
    if (camera == nullptr || pc == nullptr || player::CameraOwner(camera) != pc) {
        return;
    }
    ue3::Object* const pawn = player::BioPawn(pc);
    std::optional<player::Pov> const pov = player::CameraPov(camera);
    if (pawn == nullptr || !pov || pov->location == nullptr || pov->rotation == nullptr) {
        return;
    }
    double const now = Now();
    Observe(pc, pawn, now);
    std::optional<ue3::Vector> const at = player::Location(pawn);
    std::optional<ue3::Rotator> const rot = player::Rotation(pawn);
    if (!at || !rot) {
        return;
    }
    tv::Pose game;
    game.location = V(*pov->location);
    game.pitch = Degrees(pov->rotation->pitch);
    game.yaw = Degrees(pov->rotation->yaw);
    game.roll = Degrees(pov->rotation->roll);
    bool const recording = gMove.active || (gMove.endS > 0.0 && !gMove.reported);
    if (!recording && !gMove.settling) {
        gLastPov.valid = true;
        gLastPov.pose = game;
        gLastPov.pawn = V(*at);
        gLastPov.pawnYaw = Degrees(rot->yaw);
        return;
    }
    if (recording) {
        // Through the custom action and its blend-out (the body's animation goes on about a second after it), the game's camera as the
        // game left it: what the level view (if on) replaces below.
        tv::Sample s;
        s.t = now - gMove.startS;
        s.pawn = V(*at);
        s.pawnYaw = Degrees(rot->yaw);
        s.camera = game.location;
        s.pitch = game.pitch;
        s.yaw = game.yaw;
        s.roll = game.roll;
        s.collision = gBlockActors.ReadBool(pawn).value_or(true);
        s.physics = gPhysics.Read<std::uint8_t>(pawn).value_or(0);
        s.running = gMove.active;
        gMove.record.Add(s);
        if (gSettings.trace) {
            std::optional<arm_ik::HandsPlaced> const hands = arm_ik::MeasureHands();
            log::Info("traversal: trace {:.3f} s{}: pawn {} yaw {:.1f}, camera {} pitch {:+.1f} yaw {:.1f} roll {:+.1f}, blocks actors {}, physics {}, "
                      "wrists from the grips {}",
                s.t, s.running ? "" : " (blend-out)", Fmt(s.pawn), s.pawnYaw, Fmt(s.camera), s.pitch, s.yaw, s.roll, s.collision ? "yes" : "no",
                s.physics,
                hands && hands->valid[0] && hands->valid[1] ? std::format("L {:.2f} R {:.2f} m", hands->fromGripM[0], hands->fromGripM[1])
                                                            : std::string("-"));
        }
    }
    if (gSettings.view != tv::View::Level || !gMove.view.Started()) {
        gMove.settling = false;
        return;
    }
    std::optional<tv::Pose> pose;
    double const dt = gMove.lastCameraS > 0.0 ? now - gMove.lastCameraS : 0.0;
    gMove.lastCameraS = now;
    if (gMove.active) {
        pose = gMove.view.During(game, dt, gSettings.levelTauS);
    } else {
        pose = gMove.view.After(game, now - gMove.endS, gSettings.settleS);
        if (!pose) {
            gMove.settling = false;
            return;
        }
    }
    *pov->location = ue3::Vector{static_cast<float>(pose->location.x), static_cast<float>(pose->location.y), static_cast<float>(pose->location.z)};
    *pov->rotation = ue3::Rotator{Units(pose->pitch), Units(pose->yaw), Units(pose->roll)};
    write_journal::Wrote("Traversal.POV (the level view)", pov->location, sizeof(ue3::Vector));
    write_journal::Wrote("Traversal.POV rotation (the level view)", pov->rotation, sizeof(ue3::Rotator));
    ++gMove.viewWrites;
}

// SFXPlayerController.TryClimbOrMantleCover: the game's own in-cover A (its binding's second command).
// The step to the vault's stick, into the player's input right after BioPlayerInput.PlayerInput read the pad (both routes: script
// calls it from PlayerTick, the engine through ProcessEvent). Only the local player's input, only while the tick keeps it fresh.
void WriteStepStick(ue3::Object* input) {
    if (!gStepStick.active || Now() - gStepStick.atS > 0.25 || input == nullptr) {
        return;
    }
    ue3::Object* const pc = player::Controller();
    if (pc == nullptr || ue3::runtime::Get().OuterOf(input) != pc) {
        return;
    }
    float* const up = gInputRawJoyUp.In<float>(input);
    float* const right = gInputRawJoyRight.In<float>(input);
    float* const mag = gInputMoveStickMag.In<float>(input);
    float* const remappedUp = gRemappedJoyUp.In<float>(pc);
    float* const remappedRight = gRemappedJoyRight.In<float>(pc);
    if (up == nullptr || right == nullptr || remappedUp == nullptr || remappedRight == nullptr) {
        return;
    }
    *up = *remappedUp = gStepStick.stick.up;
    *right = *remappedRight = gStepStick.stick.right;
    write_journal::Wrote("Traversal.step RawJoyUp", up, sizeof(float));
    write_journal::Wrote("Traversal.step RawJoyRight", right, sizeof(float));
    write_journal::Wrote("Traversal.step RemappedJoyUp", remappedUp, sizeof(float));
    write_journal::Wrote("Traversal.step RemappedJoyRight", remappedRight, sizeof(float));
    if (mag != nullptr) {
        *mag = std::hypot(gStepStick.stick.up, gStepStick.stick.right);
        write_journal::Wrote("Traversal.step MoveStickMag", mag, sizeof(float));
    }
    ++gStepStick.writes;
}

void OnPlayerInputCf(ue3::Object* context, ue3::Function* /*function*/, void* /*result*/) {
    WriteStepStick(context);
}

void OnPlayerInputPe(ue3::Object* context, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    WriteStepStick(context);
}

void OnTryClimbOrMantle(ue3::Object* context, ue3::Function* /*function*/, void* parms, void* /*result*/) {
    if (context != player::Controller() || parms == nullptr) {
        return;
    }
    ++gCounters.gameRequests;
    ++gTotals.gameRequests;
    bool const yes = (*static_cast<std::uint32_t const*>(parms) & 1u) != 0;  // ReturnValue, the block's first word
    gCounters.gameRequestsTrue += yes ? 1u : 0u;
    if (yes) {
        log::Info("traversal: the game's own TryClimbOrMantleCover (A in cover) began a move");
    }
}

void OnTryClimbOrMantleCf(ue3::Object* context, ue3::Function* function, void* result) {
    OnTryClimbOrMantle(context, function, result, nullptr);
}

// BioCustomAction_CoverClimbMantleBase.StartCustomAction (script): which body stance it played (1 explore, 2 combat, 3 cover).
void OnStartCustomAction(ue3::Object* context, ue3::Function* /*function*/, void* /*result*/) {
    if (context == nullptr || !ue3::IsA(context, gMantleBase)) {
        return;
    }
    std::uint8_t const stance = gBodyStance.Read<std::uint8_t>(context).value_or(0);
    char const* const names[] = {"none (it refused: not exploring or in combat)", "FromExplore", "FromCombat", "FromCover"};
    gPendingStance = stance < 4 ? names[stance] : std::to_string(stance);
    gPendingStanceS = Now();
    if (gMove.active) {
        gMove.stance = gPendingStance;
    }
    float const period = gCollisionOffPeriod.Read<float>(context).value_or(-1.0f);
    log::Info("traversal: {}.StartCustomAction: body stance {}; the pawn blocks no actors for {:.2f} s", ClassName(context), gPendingStance,
        period);
}

void OnStopCustomAction(ue3::Object* context, ue3::Function* /*function*/, void* /*result*/) {
    if (context == nullptr || !ue3::IsA(context, gMantleBase)) {
        return;
    }
    log::Info("traversal: {}.StopCustomAction", ClassName(context));
}

void OnAnimEnd(ue3::Object* context, ue3::Function* /*function*/, void* /*result*/) {
    if (context == nullptr || !ue3::IsA(context, gMantleBase)) {
        return;
    }
    log::Info("traversal: {}: its body stance's animation ended ({:.2f} s after the move began)", ClassName(context),
        gMove.startS > 0.0 ? Now() - gMove.startS : -1.0);
}

void OnAnimEndPe(ue3::Object* context, ue3::Function* function, void* /*parms*/, void* result) {
    OnAnimEnd(context, function, result);
}


} // namespace

bool TryStartup(tv::Settings const& settings, std::string const& settingsText) {
    gSettings = settings;
    gSettingsText = settingsText;
    ue3::Function* const update = gUpdateCameraRef.Get();
    ue3::Function* const mantle = gMantleCoverRef.Get();
    ue3::Function* const admit = gCanPerformMantleRef.Get();
    if (update == nullptr || mantle == nullptr || admit == nullptr || gCurrentCustomAction.Get() == nullptr || gCanMantleOutside.Get() == nullptr ||
        gLinkSlots.Get() == nullptr || gCoverList.Get() == nullptr) {
        log::Warn("traversal: the game's camera update, MantleCover, CanPerformMantle, CurrentCustomAction, m_bCanMantleOutsideOfCover or the "
                  "cover list isn't there (see the 'reflect:' lines): no vault");
        return false;
    }
    engine::process_event::AddPostHandler(update, &OnUpdateCamera);
    if (ue3::Function* const tryFn = gTryClimbOrMantleRef.Get()) {
        engine::process_event::AddPostHandler(tryFn, &OnTryClimbOrMantle);  // the A binding's exec command (ScriptConsoleExec)
        engine::call_function::AddPostHandler(tryFn, &OnTryClimbOrMantleCf);
    }
    if (ue3::Function* const start = gStartCustomActionRef.Get()) {
        engine::call_function::AddPostHandler(start, &OnStartCustomAction);
    }
    if (ue3::Function* const stop = gStopCustomActionRef.Get()) {
        engine::call_function::AddPostHandler(stop, &OnStopCustomAction);
    }
    if (ue3::Function* const end = gAnimEndRef.Get()) {
        engine::call_function::AddPostHandler(end, &OnAnimEnd);
        engine::process_event::AddPostHandler(end, &OnAnimEndPe);
    }
    if (ue3::Function* const input = gBioPlayerInputRef.Get();
        input != nullptr && gInputRawJoyUp.Get() != nullptr && gInputRawJoyRight.Get() != nullptr && gRemappedJoyUp.Get() != nullptr) {
        engine::call_function::AddPostHandler(input, &OnPlayerInputCf);
        engine::process_event::AddPostHandler(input, &OnPlayerInputPe);
    } else {
        log::Warn("traversal: BioPlayerInput.PlayerInput or the input's stick fields aren't there: no step to the vault (a double A "
                  "vaults only where the game admits it)");
    }
    gStarted = true;
    log::Info("traversal: on (ME2's out-of-cover mantle through its own CanPerformMantle and MantleCover, the double A; handlers on "
              "UpdateCamera, TryClimbOrMantleCover and the climb/mantle custom action)");
    return true;
}

bool Started() {
    return gStarted;
}

void Tick(Link const& link, tv::Settings const& settings, std::string const& settingsText) {
    gLink = link;
    gSettings = settings;
    gSettingsText = settingsText;
    if (!gSettings.enabled) {
        return;
    }
    ue3::Object* const pc = player::Controller();
    ue3::Object* const pawn = player::BioPawn(pc);
    if (pc == nullptr || pawn == nullptr) {
        gNear = Near{};
        return;
    }
    double const now = Now();
    bool const onFoot = player::GameMode(pc).value_or(255) == 0;
    RefreshSlots(pawn, now);
    Observe(pc, pawn, now);
    if (gMove.active || (gMove.endS > 0.0 && !gMove.reported)) {
        // Shepard's drawn wrists against the controllers through the move and its blend-out (arm IK's own measure).
        for (int hand = 0; hand < 2; ++hand) {
            if (std::optional<double> const d = HandFromGrip(hand)) {
                std::size_t const h = static_cast<std::size_t>(hand);
                gMove.handsMax[h] = std::max(gMove.handsMax[h], *d);
                ++gMove.handsTicks[h];
                gMove.handsNear[h] += *d <= kHandsNearM ? 1u : 0u;
                gMove.handsSum[h] += *d;
            }
        }
    }
    if (gPauseAfterS >= 0.0 && gMove.active && now - gMove.startS >= gPauseAfterS) {
        // The test pause: the world frozen this far into the move (the console's own `pause`), for eye snapshots of its middle.
        gPauseAfterS = -1.0;
        std::string printed;
        console_request::RunCommand("pause", &printed);
        log::Info("traversal: paused {:.2f} s into the {} (a test: `pause` again resumes)", now - gMove.startS, tv::Name(gMove.kind));
    }
    bool const traversing = gMove.active;
    if (onFoot && !link.authoredSeated && !traversing) {
        UpdateNear(pc, pawn, now);
    } else if (!onFoot || link.authoredSeated) {
        gNear.slot = -1;
        gNear.admitted = false;
    }
    bool const useFirst = SelectedForUse(pc) != nullptr;
    tv::PressInput in;
    in.nowS = now;
    in.down = link.controllers && link.rightA;
    in.focused = link.controllers;
    in.menuOpen = link.menuOpen;
    in.onFoot = onFoot;
    // Eligible where the game admits the vault from where the pawn stands, or where a double A would step to it first.
    tv::StepReach const reach = onFoot && !link.authoredSeated && !traversing && !gStep.Active() ? NearStepReach(pawn) : tv::StepReach{};
    in.eligible = (gNear.slot >= 0 && gNear.admitted) || reach.ok;
    in.useFirst = useFirst;
    in.traversing = traversing;
    in.authoredSeated = link.authoredSeated;
    tv::Tap const tap = gPresses.Step(in, gSettings.doubleTapS);
    if (tap != tv::Tap::None) {
        ++gCounters.presses;
        ++gTotals.presses;
        if (traversing) {
            ++gMove.presses;
        }
        tv::Press const& p = gPresses.Last();
        // A press the scene refused names what refused it (a menu, a wheel, not on foot, a matinee moving Shepard), not the old
        // "authored seated scene" for all of them (research/le2-traversal.md "The refusal that named the wrong reason").
        // The verdict reads the tick before too (a press that closed a menu): its reason then.
        char const* const scene = link.sceneWhy != nullptr ? link.sceneWhy : gSceneWhyBefore;
        std::string why = p.why == tv::PressWhy::AuthoredSeated && scene != nullptr
            ? std::format("the scene blocks traversal: {}", scene)
            : std::string(tv::Name(p.why));
        if (p.why == tv::PressWhy::NotEligible && gNear.slot >= 0 && *reach.why != '\0') {
            // Next to a slot the game refuses from here and no step to it: say which test failed.
            why += std::format(" ({}: the game refuses it from here, no step to it: {}, {:.2f} m from where you stand, the head {:.0f} deg "
                               "off its direction)", gSlots[static_cast<std::size_t>(gNear.slot)].name, reach.why, reach.distanceM,
                reach.offDeg);
        }
        log::Info("traversal: A pressed (#{}): {}: {}{}", p.serial, tv::Name(tap), why,
            gPresses.Previous().valid ? std::format(" ({:.2f} s after the one before)", p.atS - gPresses.Previous().atS) : std::string());
    }
    gSceneWhyBefore = link.sceneWhy;
    JudgeMove();  // a move the game began itself this tick, now that its press is in
    if (tap == tv::Tap::Double) {
        ++gCounters.doubleTaps;
        ++gTotals.doubleTaps;
        if (!gSettings.commit) {
            log::Info("traversal: double tap: not committed ([Traversal] commit = 0)");
        } else if (CurrentAction(pc) == kActionMantle || CurrentAction(pc) == kActionClimb) {
            log::Info("traversal: double tap: the game already began its own move on this press");
        } else if (gNear.slot >= 0 && gNear.admitted && link.headTrackedView) {
            Commit(pc, pawn, gSlots[static_cast<std::size_t>(gNear.slot)], now, "double tap");
        } else if (link.headTrackedView && reach.ok && StartStep(pawn, now, reach)) {
            // the step's lines say the rest
        } else {
            log::Info("traversal: double tap with nothing the game admits here{}{}", link.headTrackedView ? "" : " (no head-tracked view)",
                gNear.slot >= 0 && *reach.why != '\0' ? std::format(" (no step to {}: {}, {:.2f} m from where you stand, the head {:.0f} deg "
                                                                    "off its direction)",
                                                            gSlots[static_cast<std::size_t>(gNear.slot)].name, reach.why,
                                                            reach.distanceM, reach.offDeg)
                                                      : std::string());
        }
    }
    TickStep(pc, pawn, now, link, onFoot, traversing);
    // The cue at the interaction point, and the blink, to the host.
    tv::Cue cue;
    if (gMove.active || gMove.settling || (gMove.endS > 0.0 && now - gMove.endS < 1.0)) {
        double const sinceStart = now - gMove.startS;
        double const sinceEnd = gMove.active ? -1.0 : now - gMove.endS;
        float const amount = tv::MaskAmount(gSettings.mask, gSettings, gMove.active, sinceStart, sinceEnd);
        gMove.maskPeak = std::max(gMove.maskPeak, static_cast<double>(amount));
        if (gSettings.mask == tv::Mask::Vignette && link.setVignette != nullptr && (gMove.active || amount > 0.0f)) {
            link.setVignette(amount);
        }
        if (gSettings.mask == tv::Mask::Blink) {
            cue.blink = amount;
        }
    }
    if (gNear.slot >= 0) {
        Slot& s = gSlots[static_cast<std::size_t>(gNear.slot)];
        if (!s.topTried) {
            s.topTried = true;
            tv::Vec3 const mid = s.targetAt ? tv::Vec3{0.5 * (s.at.x + s.targetAt->x), 0.5 * (s.at.y + s.targetAt->y), s.at.z} : s.at;
            s.topZ = CoverTop(pawn, mid, 150.0, 40.0);
            std::optional<ue3::Vector> const at = player::Location(pawn);
            log::Info("traversal: {}'s cover top {} (the slot at z {:.0f}, the pawn at z {:.0f}): the cue goes {}", s.name,
                s.topZ ? std::format("at z {:.0f}", *s.topZ) : std::string("not found by a trace"), s.at.z, at ? at->z : 0.0f,
                Fmt(tv::CuePoint(s.at, s.targetAt, s.topZ)));
        }
        std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
        if (frame) {
            tv::Vec3 const point = tv::CuePoint(s.at, s.targetAt, s.topZ);
            XrVector3f const p = engine::ToTracking(*frame, {point.x, point.y, point.z});
            XrVector3f const head = frame->head.position;
            double const distance = std::sqrt(std::pow(p.x - head.x, 2.0) + std::pow(p.y - head.y, 2.0) + std::pow(p.z - head.z, 2.0));
            tv::CueInput ci;
            ci.enabled = gSettings.cue;
            ci.onFoot = onFoot;
            ci.focused = link.controllers;
            ci.menuOpen = link.menuOpen;
            // Exactly where a double A would vault: the game admits it from here, or the step to it would begin (reach.ok); and while
            // the step walks there.
            ci.eligible = gNear.admitted || reach.ok || gStep.Active();
            ci.useFirst = useFirst;
            ci.traversing = traversing;
            ci.distanceM = distance;
            ci.rangeM = gSettings.cueRangeM;
            cue.visible = tv::CueShows(ci);
            cue.kind = s.kind;
            cue.position = p;
            cue.heightDeg = gSettings.cueHeightDeg;
            if (s.targetAt) {
                cue.exitValid = true;
                cue.exit = engine::ToTracking(*frame, {s.targetAt->x, s.targetAt->y, s.targetAt->z});
            }
            cue.serial = static_cast<std::uint32_t>(gNear.slot + 1);
            // Each change of whether it shows, with why: the cue promises exactly what a double A would do.
            char const* const cueWhy = !cue.visible ? (!ci.eligible ? (*reach.why != '\0' ? reach.why : "the game refuses it from here")
                                                                     : "not now (a menu, no focus, A uses something, a move, too far from the head)")
                : gNear.admitted ? "the game admits the vault from here"
                : gStep.Active() ? "the step to it walks"
                                 : "a double A steps to it first";
            if (cue.visible != gCueShown || (cue.visible && cueWhy != gCueWhy)) {
                gCueShown = cue.visible;
                gCueWhy = cueWhy;
                log::Info("traversal: the cue over {} {}: {} (the slot {:.2f} m from where you stand, the head {:.0f} deg off its direction)",
                    s.name, cue.visible ? "shows" : "hides", cueWhy, reach.distanceM, reach.offDeg);
            }
            if (cue.visible && cue.serial != gToldCueSerial) {
                gToldCueSerial = cue.serial;
                log::Info("traversal: the cue '{}' shows over {}: the world point {} is ({:+.2f}, {:+.2f}, {:+.2f}) m from the head in the room "
                          "(x right, y up, -z ahead), {:.2f} m away{}",
                    tv::Label(s.kind), s.name, Fmt(point), p.x - head.x, p.y - head.y, p.z - head.z, distance,
                    link.setCue != nullptr ? "" : "; the host can't draw it (services before v40)");
            }
        }
    } else if (gCueShown) {
        gCueShown = false;
        gCueWhy = "";
        log::Info("traversal: the cue hides: no slot that vaults or climbs within {:.1f} m", gSettings.reachM);
    }
    if (link.setCue != nullptr && (cue.visible || cue.blink > 0.0f)) {
        link.setCue(&cue);
    }
}

bool Moving() {
    return gMove.active || gMove.settling;
}

bool Stepping() {
    return gStep.Active();
}

std::string StatusJson() {
    namespace json = vr::test_request;
    std::string nearText = "null";
    if (gNear.slot >= 0 && static_cast<std::size_t>(gNear.slot) < gSlots.size()) {
        Slot const& s = gSlots[static_cast<std::size_t>(gNear.slot)];
        nearText = std::format("{{\"slot\": {}, \"kind\": {}, \"target\": {}, \"distanceM\": {}, \"facingDot\": {}, \"admitted\": {}}}",
            json::JsonString(s.name), json::JsonString(tv::Name(s.kind)), json::JsonString(s.target), json::JsonNumber(gNear.distanceM),
            json::JsonNumber(gNear.facingDot), gNear.admitted ? "true" : "false");
    }
    return std::format("{{\"started\": {}, \"settings\": {}, \"slots\": {}, \"near\": {}, \"moving\": {}, \"moves\": {}, \"presses\": {}, "
                       "\"doubleTaps\": {}, \"commits\": {}, \"commitsRefused\": {}, \"violations\": {}, \"stepping\": {}, \"steps\": {}, "
                       "\"stepVaults\": {}, \"last\": {}}}",
        gStarted ? "true" : "false", json::JsonString(gSettingsText), gSlots.size(), nearText, Moving() ? "true" : "false", gMoves,
        gTotals.presses, gTotals.doubleTaps, gTotals.commits, gTotals.commitsRefused, gTotals.violations, gStep.Active() ? "true" : "false",
        gSteps, gStepVaults, gLastSummary);
}

void LogStats() {
    if (!gStarted) {
        return;
    }
    log::Info("    traversal: {} slots that vault or climb; nearest {}; the game asked {} times ({} admitted); {} A presses, {} double taps, {} "
              "commits ({} began nothing), the game's own in-cover A {} times ({} began a move); {} moves in all, {} the rules wouldn't have "
              "committed; steps to the vault {} in all ({} vaulted, {} ended){}{}",
        gSlots.size(), gNear.slot >= 0 ? gSlots[static_cast<std::size_t>(gNear.slot)].name : std::string("none"), gCounters.admitAsks,
        gCounters.admitYes, gCounters.presses, gCounters.doubleTaps, gCounters.commits, gCounters.commitsRefused, gCounters.gameRequests,
        gCounters.gameRequestsTrue, gMoves, gTotals.violations, gSteps, gStepVaults, gStepEnded,
        gCounters.zoomedAdmissions > 0 ? std::format("; {} asked with the weapon's zoom cleared (the two-handed hold)", gCounters.zoomedAdmissions)
                                       : std::string(),
        gCounters.faultsRestored > 0 ? std::format("; {} ONE-CALL WRITES NOT PUT BACK", gCounters.faultsRestored) : std::string());
    gCounters = Counters{};
}

std::string Command(std::string const& args, Link const& link) {
    gLink = link;
    std::istringstream words(args);
    std::string verb;
    words >> verb;
    verb = ini::Lower(verb);
    if (verb == "pause") {
        double seconds = 0.5;
        words >> seconds;
        gPauseAfterS = std::max(0.0, seconds);
        return std::format("traversal: the next move pauses the game {:.2f} s in (a test; the console's `pause` resumes)", gPauseAfterS);
    }
    ue3::Object* const pc = player::Controller();
    ue3::Object* const pawn = player::BioPawn(pc);
    if (pc == nullptr || pawn == nullptr) {
        return "traversal: no player pawn";
    }
    if ((verb == "place" || verb == "go" || verb == "try") &&
        (gLink.authoredSeated || gLink.menuOpen || player::GameMode(pc) != 0 ||
         gIgnoreMove.Read<std::uint8_t>(pc).value_or(1) != 0)) {
        return "traversal: scene/menu/native movement guard blocks mutation";
    }
    if (verb == "slots") {
        RefreshSlots(pawn, Now());
        std::optional<ue3::Vector> const at = player::Location(pawn);
        std::string out = std::format("traversal: {} slots that vault or climb", gSlots.size());
        for (Slot const& s : gSlots) {
            out += std::format("\n  {}: {} at {} yaw {:.0f}{}{}", s.name, tv::Name(s.kind), Fmt(s.at), s.yaw,
                s.target.empty() ? std::string() : " -> " + s.target + (s.targetAt ? " at " + Fmt(*s.targetAt) : std::string()),
                at ? std::format(", {:.1f} m from the pawn", tv::Length(s.at - V(*at)) / 100.0) : std::string());
        }
        return out;
    }
    if (verb == "near" || verb == "status") {
        RefreshSlots(pawn, Now());
        gNear.askedAt = -1e9;
        gNear.loggedSlot = -2;
        UpdateNear(pc, pawn, Now());
        std::string face;
        if (gNear.slot >= 0) {
            // The obstacle's face in front of the slot (a level trace along the slot's direction at the slot point's height, which is
            // below a low cover's top; the pawn's centre is above it).
            Slot const& s = gSlots[static_cast<std::size_t>(gNear.slot)];
            std::optional<ue3::Vector> const at = player::Location(pawn);
            ue3::Function* const fn = gTraceRef.Get();
            if (at && fn != nullptr) {
                double const y = s.yaw * kPi / 180.0;
                ue3::Params parms(ue3::runtime::Get(), fn);
                ue3::Vector const start{static_cast<float>(s.at.x), static_cast<float>(s.at.y), static_cast<float>(s.at.z)};
                parms.Set("TraceStart", start);
                parms.Set("TraceEnd", ue3::Vector{start.x + static_cast<float>(std::cos(y) * 200.0), start.y + static_cast<float>(std::sin(y) * 200.0),
                    start.z});
                parms.SetBool("bTraceActors", true);
                ue3::runtime::Call(pawn, fn, parms);
                std::optional<ue3::Vector> const hit = parms.Get<ue3::Vector>("HitLocation");
                if (parms.Return<ue3::Object*>().value_or(nullptr) != nullptr && hit) {
                    double const d = std::hypot(hit->x - start.x, hit->y - start.y) / 100.0;
                    double const pawnFace = std::hypot(hit->x - at->x, hit->y - at->y) / 100.0;
                    face = std::format("; the obstacle's face {:.2f} m ahead of the slot, {:.2f} m from the pawn's centre", d, pawnFace);
                }
            }
        }
        return "traversal: " + StatusJson() + face;
    }
    if (verb == "place") {
        // "place <link name> <slot> [backM] [sideM] [yawDeg]": the pawn in front of that slot, facing it turned yawDeg (a test placement
        // for the fixture).
        std::string linkName;
        int index = -1;
        double back = 0.25;
        double side = 0.0;
        double turn = 0.0;
        words >> linkName >> index >> back >> side >> turn;
        RefreshSlots(pawn, Now());
        for (Slot const& s : gSlots) {
            if (ShortName(s.link) == linkName && s.index == index) {
                double const y = s.yaw * kPi / 180.0;
                tv::Vec3 const to{s.at.x - std::cos(y) * back * 100.0 - std::sin(y) * side * 100.0,
                    s.at.y - std::sin(y) * back * 100.0 + std::cos(y) * side * 100.0, s.at.z + 2.0};
                ue3::Function* const setLocation = gSetLocationRef.Get();
                ue3::Function* const setRotation = gSetRotationRef.Get();
                if (setLocation == nullptr || setRotation == nullptr) {
                    return "traversal: Actor.SetLocation or SetRotation not found";
                }
                ue3::Params lp(ue3::runtime::Get(), setLocation);
                lp.Set("NewLocation", ue3::Vector{static_cast<float>(to.x), static_cast<float>(to.y), static_cast<float>(to.z)});
                ue3::runtime::Call(pawn, setLocation, lp);
                write_journal::Wrote("Traversal.place (SetLocation)", pawn, 0);
                ue3::Rotator const facing{0, Units(s.yaw + turn), 0};
                for (ue3::Object* actor : {pawn, pc}) {
                    ue3::Params rp(ue3::runtime::Get(), setRotation);
                    rp.Set("NewRotation", facing);
                    ue3::runtime::Call(actor, setRotation, rp);
                    write_journal::Wrote("Traversal.place (SetRotation)", actor, 0);
                }
                std::optional<ue3::Vector> const now = player::Location(pawn);
                return std::format("traversal: the pawn placed {:.2f} m in front of {} ({:+.2f} m aside), facing it turned {:+.0f} deg (yaw {:.0f}): "
                    "now {}", back, s.name, side, turn, s.yaw + turn, now ? Fmt(V(*now)) : std::string("?"));
            }
        }
        return std::format("traversal: no slot '{} {}' that vaults or climbs (traversal slots)", linkName, index);
    }
    if (verb == "go") {
        // "go": as the double tap would (without controllers): the nearest slot's move if the game admits it.
        RefreshSlots(pawn, Now());
        gNear.askedAt = -1e9;
        UpdateNear(pc, pawn, Now());
        if (gNear.slot < 0) {
            return "traversal: go: no slot near";
        }
        Slot const& s = gSlots[static_cast<std::size_t>(gNear.slot)];
        if (!gNear.admitted) {
            return std::format("traversal: go: the game refuses {} ({:.2f} m, facing dot {:.2f})", s.name, gNear.distanceM, gNear.facingDot);
        }
        gOurCommitS = Now();
        bool const called = CallMove(pc, s);
        std::uint8_t const action = CurrentAction(pc);
        ++gTotals.commits;
        log::Info("traversal: go (console): {}({}) -> custom action {}", s.kind == tv::Kind::ClimbUp ? "ClimbCover" : "MantleCover", s.name, action);
        return std::format("traversal: go: {} {} -> custom action {}", called ? "called" : "couldn't call", s.name, action);
    }
    if (verb == "try") {
        // The game's own in-cover A: TryClimbOrMantleCover (an exec function), for the comparison with the cover path.
        ue3::Function* const fn = gTryClimbOrMantleRef.Get();
        if (fn == nullptr) {
            return "traversal: TryClimbOrMantleCover not found";
        }
        ue3::Params parms(ue3::runtime::Get(), fn);
        ue3::runtime::Call(pc, fn, parms);
        return std::format("traversal: TryClimbOrMantleCover returned {}; custom action {}; CurrentLink {}", parms.ReturnBool().value_or(false),
            CurrentAction(pc), ShortName(ObjectField(pawn, gPawnCurrentLink)));
    }
    return {};
}

void Shutdown() {
    gStarted = false;
    gLink = {};
    gSlots.clear();
    gSlotsAt = -1e9;
    gSlotsWorld = nullptr;
    gToldSlots = false;
    gToldZoomed = false;
    gNear = Near{};
    gPresses.Reset();
    gMove = Move{};
    gLastPov = LastPov{};
    gCounters = Counters{};
    gSettingsText.clear();
    gStep.Cancel();
    gStepStick = {};
    gStepSlot = -1;
    gStepSlotName.clear();
}

} // namespace mevr::game::traversal::le2
#endif
