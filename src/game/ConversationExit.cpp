// SPDX-License-Identifier: MIT
// People leaving a first-person conversation, ME1's side (game/ConversationExit.hpp, research/conversation-exits.md).
#include "game/ConversationExit.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/ConversationExit.hpp"
#include "core/vr/ConversationWorld.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/ExitHooks.hpp"
#include "engine/SettingsFile.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/AiWalk.hpp"
#include "game/AuthoredMoment.hpp"
#include "game/CallTrace.hpp"
#include "game/ConversationWorld.hpp"
#include "game/Player.hpp"
#include "game/SquadWeapons.hpp"
#include "game/ViewAnchor.hpp"
#if !defined(MEVR_GAME_LE1)
#include "engine/GameAdapter.hpp"
#include "engine/ProcessEvent.hpp"
#include "game/SequelConversationWorld.hpp"
#endif
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace mevr::game::conversation_exit {

namespace {

namespace cx = vr::conversation_exit;
namespace cw = vr::conversation_world;
namespace walk = ai_walk;  // the shared walk (game/AiWalk): one line to switch when its interface moves

constexpr double kUnitsPerMeter = 100.0;
#if defined(MEVR_GAME_LE1)
constexpr std::uint8_t kModeFoot = 0;
constexpr std::uint8_t kModeConversation = 5;
constexpr std::uint8_t kModeCinematic = 6;

// ---- The engine's names (each has a row in engine/ReflectCheck's CheckConversationExit) ---------------------------------------
ue3::FunctionRef gSetActive{"SFXGame.BioActorBehavior.SetActive"};
ue3::FieldRef gSetActiveParam{gSetActive, "bSetActive"};
ue3::FieldRef gBehaviorActive{"SFXGame.BioActorBehavior", "bActive"};
ue3::FieldRef gBioComponent{"Engine.Actor", "oBioComponent"};
ue3::FieldRef gPawnBehavior{"SFXGame.BioPawn", "m_oBehavior"};
ue3::FieldRef gHidden{"Engine.Actor", "bHidden"};
ue3::FieldRef gTag{"Engine.Actor", "Tag"};
ue3::FieldRef gWorldOfActor{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gWorldPawns{"Engine.WorldInfo", "PawnList"};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn"};
ue3::ClassRef gLevelClass{"Engine.Level"};
ue3::FieldRef gStreamingLevels{"Engine.WorldInfo", "StreamingLevels"};
ue3::FieldRef gStreamingLoaded{"Engine.LevelStreaming", "LoadedLevel"};
ue3::FieldRef gStreamingShouldShow{"Engine.LevelStreaming", "bShouldBeVisible"};
ue3::FieldRef gStreamingVisible{"Engine.LevelStreaming", "bIsVisible"};
ue3::FieldRef gStreamingPackage{"Engine.LevelStreaming", "PackageName"};
ue3::FieldRef gSpeaker{"SFXGame.BioConversation", "m_pSpeaker"};
ue3::FieldRef gListener{"SFXGame.BioConversation", "m_pListener"};
ue3::FieldRef gOwner{"SFXGame.BioConversation", "m_pConversationOwner"};
ue3::FieldRef gSpeakerList{"SFXGame.BioConversation", "m_SpeakerList"};
ue3::FieldRef gSpeakerActor{gSpeakerList, "aSpeaker"};
ue3::FunctionRef gFastTrace{"Engine.Actor.FastTrace"};
ue3::FunctionRef gTrace{"Engine.Actor.Trace"};
ue3::FunctionRef gSetLocation{"Engine.Actor.SetLocation"};
ue3::FunctionRef gSetRotation{"Engine.Actor.SetRotation"};
ue3::FieldRef gController{"Engine.Pawn", "Controller"};
ue3::FunctionRef gPointReachable{"Engine.Controller.PointReachable"};
#else
// ---- The sequels' adapter (research/conversation-exits.md section 9; rows in engine/ReflectCheck's LE2 and LE3 checks) ---------
// The removal is Engine.Actor.SetActive on the pawn itself (ME2's ModifyPropertyPawn "Active" row; ME3's BioSeqAct_SetActive),
// so the hook's object is the pawn, kept as the "behaviour" ME1's code keeps. The scene's people come from the sequels' module
// (game/SequelConversationWorld's reader); the game's black screen from BioSeqAct_BlackScreen.Activated (ME1's route; ME3's
// line track and start flags are not read yet).
ue3::FunctionRef gSetActive{"Engine.Actor.SetActive", ue3::kEveryGame};
ue3::FieldRef gSetActiveParam{gSetActive, "bActive"};
#if defined(MEVR_GAME_LE2)
ue3::FieldRef gBehaviorActive{"SFXGame.BioActorBehavior", "bActive", ue3::kEveryGame};
ue3::FieldRef gBioComponent{"Engine.Actor", "oBioComponent", ue3::kEveryGame};
#else
ue3::FieldRef gPawnActive{"SFXGame.BioPawn", "bActive", ue3::kEveryGame};
ue3::FunctionRef gScriptSetActive{"SFXGame.BioSeqAct_SetActive.Activated", ue3::kEveryGame};
ue3::FieldRef gOpSetActive{"SFXGame.BioSeqAct_SetActive", "bSetActive", ue3::kEveryGame};
// ME3's hub people are mostly stunt actors (SFXStuntActor: an actor with a body and no AI; the spectre at the hanar plot), switched
// off by the same Actor.SetActive: one of the scene's people as a pawn is, held the same way, never walked (the look-away).
ue3::ClassRef gStuntClass{"SFXGame.SFXStuntActor", ue3::kEveryGame};
ue3::FieldRef gStuntActive{"SFXGame.SFXStuntActor", "m_bActive", ue3::kEveryGame};
ue3::FunctionRef gSkipNode{"SFXGame.BioConversationController.SkipNode", ue3::kEveryGame};  // `mevr convo exit skip` (a test aid)
ue3::FunctionRef gQueueReply{"SFXGame.BioConversationController.QueueReply", ue3::kEveryGame};  // `mevr convo exit reply <n>`
#endif
ue3::FieldRef gHidden{"Engine.Actor", "bHidden", ue3::kEveryGame};
ue3::FieldRef gTag{"Engine.Actor", "Tag", ue3::kEveryGame};
ue3::FieldRef gWorldOfActor{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gWorldPawns{"Engine.WorldInfo", "PawnList", ue3::kEveryGame};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn", ue3::kEveryGame};
ue3::ClassRef gLevelClass{"Engine.Level", ue3::kEveryGame};
ue3::FieldRef gStreamingLevels{"Engine.WorldInfo", "StreamingLevels", ue3::kEveryGame};
ue3::FieldRef gStreamingLoaded{"Engine.LevelStreaming", "LoadedLevel", ue3::kEveryGame};
ue3::FieldRef gStreamingShouldShow{"Engine.LevelStreaming", "bShouldBeVisible", ue3::kEveryGame};
ue3::FieldRef gStreamingVisible{"Engine.LevelStreaming", "bIsVisible", ue3::kEveryGame};
ue3::FieldRef gStreamingPackage{"Engine.LevelStreaming", "PackageName", ue3::kEveryGame};
ue3::FunctionRef gFastTrace{"Engine.Actor.FastTrace", ue3::kEveryGame};
ue3::FunctionRef gTrace{"Engine.Actor.Trace", ue3::kEveryGame};
ue3::FunctionRef gSetLocation{"Engine.Actor.SetLocation", ue3::kEveryGame};
ue3::FunctionRef gSetRotation{"Engine.Actor.SetRotation", ue3::kEveryGame};
ue3::FieldRef gController{"Engine.Pawn", "Controller", ue3::kEveryGame};
ue3::FunctionRef gPointReachable{"Engine.Controller.PointReachable", ue3::kEveryGame};
ue3::FunctionRef gBlackScreen{"SFXGame.BioSeqAct_BlackScreen.Activated", ue3::kEveryGame};
ue3::FieldRef gBlackScreenAction{"SFXGame.BioSeqAct_BlackScreen", "m_eBlackScreenAction", ue3::kEveryGame};
std::uint64_t gBlackOnMs = 0;              // the script's black screen last turned (or faded to) black
std::vector<ue3::Object*> gScenePeople;    // this tick's people of the live conversation (the sequels' reader)

bool IsMode(std::optional<std::uint8_t> mode, engine::adapter::Mode which) {
    return mode && engine::adapter::GameMode(*mode) == which;
}
#endif

// ---- Settings -------------------------------------------------------------------------------------------------------------------
struct Settings {
    bool world = false;          // [Cinematics] conversations = world
    cx::Mode mode = cx::Mode::Walk;  // [Cinematics] worldExit
    bool log = true;
};
Settings gSettings;
std::uint64_t gSettingsMs = 0;
std::optional<cx::Mode> gModeOverride;  // `mevr convo exit on|hold|off` (auto: the file's)
cx::Limits const gLimits{};

cx::Mode CurrentMode() {
    return gModeOverride.value_or(gSettings.mode);
}

void RefreshSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gSettingsMs != 0 && now - gSettingsMs < 1000) {
        return;
    }
    gSettingsMs = now;
    ini::Loaded const loaded = ini::Read(engine::SettingsFile());
    Settings s;
    if (auto const text = ini::Get(loaded.file, "Cinematics", "conversations")) {
        s.world = cw::ParseMode(text->c_str(), cw::Mode::Screen) == cw::Mode::World;
    }
#if !defined(MEVR_GAME_LE1)
    s.world = sequel_conversation_world::WantsHooks();  // the file's word, or the session's (`mevr convo world on|off|auto`)
#endif
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldExit")) {
        s.mode = cx::ParseMode(text->c_str(), cx::Mode::Walk);
    }
    static bool first = true;
    if (first || s.world != gSettings.world || s.mode != gSettings.mode) {
        log::Info("convo exit: [Cinematics] worldExit = {} ({})", cx::Name(s.mode),
            s.world ? "conversations = world: people the script removes in view leave first" : "not the first-person mode: idle");
    }
    first = false;
    gSettings = s;
}

double NowS() {
    return static_cast<double>(GetTickCount64()) / 1000.0;
}

ik::Vec3 V(ue3::Vector const& v) {
    return {v.x, v.y, v.z};
}

ue3::Vector U(ik::Vec3 const& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

cx::Vec3 C(ik::Vec3 const& v) {
    return {v.x, v.y, v.z};
}

ik::Vec3 I(cx::Vec3 const& v) {
    return {v.x, v.y, v.z};
}

double Dist(ik::Vec3 a, ik::Vec3 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

std::string Short(ue3::Object const* o) {
    return o == nullptr ? std::string("none") : ue3::runtime::Get().NameText(o);
}

std::string TagOf(ue3::Object* o) {
    auto const tag = o != nullptr ? gTag.Read<ue3::Name>(o) : std::nullopt;
    return tag ? ue3::runtime::Get().ToString(*tag) : std::string("?");
}

// ---- Traces ---------------------------------------------------------------------------------------------------------------------
bool Clear(ue3::Object* tracer, ik::Vec3 const& from, ik::Vec3 const& to) {
    if (tracer == nullptr || gFastTrace.Get() == nullptr) {
        return true;
    }
    ue3::Params p(ue3::runtime::Get(), gFastTrace.Get());
    p.Set("TraceEnd", U(to));
    p.Set("TraceStart", U(from));
    ue3::runtime::Call(tracer, gFastTrace.Get(), p);
    return p.ReturnBool().value_or(true);
}

std::optional<double> FloorZ(ue3::Object* tracer, ik::Vec3 const& at) {
    ue3::Function* const fn = gTrace.Get();
    if (fn == nullptr || tracer == nullptr) {
        return std::nullopt;
    }
    ue3::Params p(ue3::runtime::Get(), fn);
    if (!p.Valid()) {
        return std::nullopt;
    }
    p.Set("TraceStart", U(at + ik::Vec3{0.0, 0.0, 100.0}));
    p.Set("TraceEnd", U(at - ik::Vec3{0.0, 0.0, 300.0}));
    p.SetBool("bTraceActors", false);
    if (!ue3::runtime::Call(tracer, fn, p) || p.Return<ue3::Object*>().value_or(nullptr) == nullptr) {
        return std::nullopt;
    }
    auto const hit = p.Get<ue3::Vector>("HitLocation");
    return hit ? std::optional<double>(hit->z) : std::nullopt;
}

// ---- What the player sees ------------------------------------------------------------------------------------------------------
struct Eyes {
    ik::Vec3 at;
    engine::TrackingFrame frame;
};
std::optional<Eyes> gEyes;  // this tick's drawn eyes (view_anchor's frame)

// Inside the view: a field wider than the audit's 35 degrees (a headset's is about 50 each way), so nothing is taken out in the
// view's edge; out of it only well outside.
bool InView(ik::Vec3 const& p) {
    if (!gEyes) {
        return false;
    }
    XrVector3f const t = engine::ToTracking(gEyes->frame, engine::UnrealVector{p.x, p.y, p.z});
    XrPosef const& head = gEyes->frame.head;
    ik::Quat const toHead = ik::Conjugate({head.orientation.x, head.orientation.y, head.orientation.z, head.orientation.w});
    ik::Vec3 const v = ik::Rotate(toHead, {t.x - head.position.x, t.y - head.position.y, t.z - head.position.z});
    return v.z < -0.05 && std::abs(v.x) <= -1.25 * v.z && std::abs(v.y) <= -1.1 * v.z;
}

// Inside the view and in sight of the drawn eyes: the body's middle or the head clear of walls.
bool Seen(ue3::Object* pawn) {
    if (!gEyes || pawn == nullptr) {
        return false;
    }
    auto const at = player::Location(pawn);
    if (!at) {
        return false;
    }
    ik::Vec3 const middle = V(*at);
    ik::Vec3 const head = middle + ik::Vec3{0.0, 0.0, 70.0};
    if (!InView(middle) && !InView(head)) {
        return false;
    }
    ue3::Object* const tracer = player::Pawn(player::Controller());
    return Clear(tracer, gEyes->at, head) || Clear(tracer, gEyes->at, middle);
}

// The person's own level streamed out, or on its way out: the streaming level whose loaded level holds the pawn no longer meant
// to be (or no longer) visible. The script's streaming change takes them with everything else there, and is never held (the council's
// layer after Anderson's final chat: "Deactivate the captain", then "Stream out this layer" in the same tick, run 4). The package's
// name, or empty (the level stays, or is the persistent one).
std::string LevelGoing(ue3::Object* pawn) {
    ue3::Class* const levelClass = gLevelClass.Get();
    if (pawn == nullptr || levelClass == nullptr) {
        return {};
    }
    ue3::Object* const level = ue3::runtime::Get().OuterOfA(pawn, levelClass);
    ue3::Object* const world = gWorldOfActor.Read<ue3::Object*>(pawn).value_or(nullptr);
    auto const levels = level != nullptr && world != nullptr && ue3::runtime::IsAlive(world) ? gStreamingLevels.ElementsOf(world, 1024)
                                                                                           : std::nullopt;
    if (!levels) {
        return {};
    }
    for (std::int32_t i = 0; i < levels->count; ++i) {
        ue3::Object* const streaming = *static_cast<ue3::Object* const*>(levels->At(i));
        if (streaming == nullptr || gStreamingLoaded.Read<ue3::Object*>(streaming).value_or(nullptr) != level) {
            continue;
        }
        if (gStreamingShouldShow.ReadBool(streaming).value_or(true) && gStreamingVisible.ReadBool(streaming).value_or(true)) {
            return {};
        }
        auto const name = gStreamingPackage.Read<ue3::Name>(streaming);
        return name ? ue3::runtime::NameText(*name) : std::string("a streaming level");
    }
    return {};
}

bool Present(ue3::Object* pawn) {
    if (pawn == nullptr || !ue3::runtime::IsAlive(pawn) || gHidden.ReadBool(pawn).value_or(true) || !LevelGoing(pawn).empty()) {
        return false;
    }
#if defined(MEVR_GAME_LE3)
    if (ue3::IsA(pawn, gStuntClass)) {
        return gStuntActive.ReadBool(pawn).value_or(true);
    }
    return !player::IsBioPawn(pawn) || gPawnActive.ReadBool(pawn).value_or(true);
#else
    ue3::Object* const behavior = gBioComponent.Read<ue3::Object*>(pawn).value_or(nullptr);
    return behavior == nullptr || !ue3::runtime::IsAlive(behavior) || gBehaviorActive.ReadBool(behavior).value_or(true);
#endif
}

bool IsSquad(ue3::Object const* pawn) {
    for (squad_weapons::MemberView const& m : squad_weapons::Members()) {
        if (m.pawn != nullptr && m.pawn == pawn) {
            return true;
        }
    }
    return false;
}

// ---- The scene's people ---------------------------------------------------------------------------------------------------------
struct Person {
    ue3::Kept pawn;
    void const* behaviors[2] = {nullptr, nullptr};  // oBioComponent and m_oBehavior (the same object, read both ways)
    std::string name;
    std::string tag;                 // the character (a scene's double of the same character has the same tag)
    std::uint32_t conversation = 0;  // the number of the conversation it was last one of the people of
    double lastInSceneS = 0.0;
    double passedSeenS = -10.0;      // the script's deactivation of it was let through while the player saw it (the audit)
    double appliedS = -10.0;         // our own apply of its held change (out of sight or under a dip: not a pop)
    bool seen = false;               // this tick
    bool seenBefore = false;         // last tick
    bool present = false;
    bool presentBefore = false;
    ik::Vec3 at;
};
std::vector<Person> gPeople;
void const* gConversationObject = nullptr;
std::uint32_t gConversationNumber = 0;
double gSceneShownS = -1.0;  // the last tick a conversation (or a moment) was shown in the world
bool gSceneShown = false;

Person* FindPerson(void const* behavior) {
    for (Person& p : gPeople) {
        if (behavior != nullptr && (p.behaviors[0] == behavior || p.behaviors[1] == behavior)) {
            return &p;
        }
    }
    return nullptr;
}

Person* FindPersonPawn(ue3::Object const* pawn) {
    for (Person& p : gPeople) {
        if (pawn != nullptr && p.pawn.Raw() == pawn) {
            return &p;
        }
    }
    return nullptr;
}

void NotePerson(ue3::Object* actor, double now) {
#if defined(MEVR_GAME_LE3)
    bool const body = actor != nullptr && ue3::runtime::IsAlive(actor) && (player::IsBioPawn(actor) || ue3::IsA(actor, gStuntClass));
    if (!body || actor == player::Pawn(player::Controller())) {
        return;  // (a pawn or a stunt actor; never the player's own pawn)
    }
#else
    if (actor == nullptr || !ue3::runtime::IsAlive(actor) || !player::IsBioPawn(actor) || actor == player::Pawn(player::Controller())) {
        return;  // (the player's own pawn is never one of "the people": the hearing's end deactivates it twice)
    }
#endif
    Person* p = FindPersonPawn(actor);
    if (p == nullptr) {
        gPeople.push_back({});
        p = &gPeople.back();
        p->pawn = ue3::Kept(actor);
        p->tag = TagOf(actor);
        p->name = std::format("{} ({})", Short(actor), p->tag);
    }
#if defined(MEVR_GAME_LE1)
    p->behaviors[0] = gBioComponent.Read<ue3::Object*>(actor).value_or(nullptr);
    p->behaviors[1] = gPawnBehavior.Read<ue3::Object*>(actor).value_or(nullptr);
#else
    p->behaviors[0] = actor;  // the sequels' SetActive is the actor's own
    p->behaviors[1] = nullptr;
#endif
    p->conversation = gConversationNumber;
    p->lastInSceneS = now;
}

void ReadScene(ue3::Object* conv, double now) {
    if (conv != gConversationObject) {
        gConversationObject = conv;
        if (conv != nullptr) {
            ++gConversationNumber;
        }
    }
    if (conv == nullptr) {
        return;
    }
#if defined(MEVR_GAME_LE1)
    for (ue3::FieldRef* f : {&gSpeaker, &gListener, &gOwner}) {
        NotePerson(f->Read<ue3::Object*>(conv).value_or(nullptr), now);
    }
    if (auto const list = gSpeakerList.ElementsOf(conv, 64)) {
        for (std::int32_t i = 0; i < list->count; ++i) {
            NotePerson(gSpeakerActor.Read<ue3::Object*>(list->At(i)).value_or(nullptr), now);
        }
    }
#else
    for (ue3::Object* const actor : gScenePeople) {
        NotePerson(actor, now);
    }
#endif
}

// ---- What is held ---------------------------------------------------------------------------------------------------------------
struct HeldChange {
    std::uint32_t id = 0;
    cx::Kind kind = cx::Kind::Deactivate;
    ue3::Kept pawn;
    ue3::Kept behavior;
    std::string name;
    std::string tag;
    std::uint32_t conversation = 0;  // the scene it was held from
    ik::Vec3 at;                     // where it stood
    ue3::Rotator rotation{};
    cx::Held timer;
    std::optional<walk::Walk> walking;
    bool walkTried = false;
    bool retried = false;            // one more place asked after the AI gave the first up where it stood
    std::vector<ik::Vec3> refused;   // places the AI gave up (no way it knows)
    std::string walkWhy;
    std::optional<ik::Vec3> destination;
    double nearestUnits = 0.0;       // how far it walked
};
std::vector<HeldChange> gHeld;
std::uint32_t gNextId = 1;
bool gApplying = false;    // our own SetActive call: let through
bool gDipWanted = false;   // this tick: a change under a dip from black
double gLastDipS = -10.0;
std::uint32_t gCutSeen = 0;
double gCutChangedS = -10.0;
struct Activation {
    void const* behavior = nullptr;
    double atS = 0.0;
};
std::vector<Activation> gActivations;  // the script showed someone (SetActive(true) through ProcessEvent), this tick
std::optional<double> gAwaySinceS;     // the player has seen no world in first person since
constexpr double kAwaySettleS = 0.5;

struct Totals {
    unsigned held = 0, outOfSight = 0, underDip = 0, atOnce = 0, walks = 0, walkRefused = 0, swaps = 0, again = 0;
    unsigned passed = 0, passedSeen = 0, vanished = 0, vanishedOther = 0, elsewhere = 0, again2 = 0;
};
unsigned gUnmatchedLogged = 0;  // deactivations of nobody of a scene, logged (the first few per level)
bool gShots = false;            // `mevr convo exit shots on`: eye snapshots as a hold begins and is applied (tests)
bool gTraceWalks = false;       // `mevr convo exit trace on`: the AI's call trace for 4 s as each walk away begins

// A test aid: eye snapshots through the test loop's request file (the host polls it; development builds or [Debug] requests).
void AskSnapshots(int count, char const* tag) {
    if (!gShots) {
        return;
    }
    wchar_t base[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) == 0) {
        return;
    }
    std::wstring const path = std::wstring(base) + L"\\MEVR\\snapshot.request";
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return;  // one waiting already
    }
    std::string const text = std::format("count={} interval=1.0 delay=0 tag={}", count, tag);
    HANDLE const f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        WriteFile(f, text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr);
        CloseHandle(f);
    }
}

bool UnderGameBlack() {
#if defined(MEVR_GAME_LE1)
    std::uint64_t const on = authored_moment::LastBlackScreenOnMs();
#else
    std::uint64_t const on = gBlackOnMs;
#endif
    return on != 0 && GetTickCount64() - on < 1500;
}
Totals gTotals;
std::string gLastPass;

void StopWalk(HeldChange& h, char const* why) {
    if (h.walking) {
        walk::End(*h.walking, why);
        h.walking.reset();
    }
}

// The game's change, made now as the script made it: BioActorBehavior.SetActive(false) through ProcessEvent.
void Apply(HeldChange& h, char const* why, bool underDip) {
    StopWalk(h, why);
    ue3::Object* const behavior = h.behavior.Live();
    ue3::Object* const pawn = h.pawn.Live();
    double const heldFor = NowS() - h.timer.heldS;
    if (behavior == nullptr || gSetActive.Get() == nullptr) {
        log::Info("convo exit: {}'s held deactivation dropped after {:.1f} s ({}): the object is gone (a load or the level's end took it)",
            h.name, heldFor, why);
        return;
    }
    if (underDip) {
        gDipWanted = true;
        conversation_world::DipFromBlack();
        gLastDipS = NowS();
        ++gTotals.underDip;
    }
    double walked = 0.0;
    if (pawn != nullptr) {
        if (auto const now = player::Location(pawn)) {
            walked = Dist(V(*now), h.at);
        }
    }
    if (Person* const person = FindPersonPawn(h.pawn.Raw())) {
        person->appliedS = NowS();
    }
    ue3::Params p(ue3::runtime::Get(), gSetActive.Get());
    bool called = false;
#if defined(MEVR_GAME_LE1)
    if (p.Valid() && p.SetBool("bSetActive", false)) {
#else
    if (p.Valid() && p.SetBool("bActive", false)) {
#endif
        gApplying = true;
        called = ue3::runtime::Call(behavior, gSetActive.Get(), p);
        gApplying = false;
    }
    bool const hidden = pawn != nullptr && gHidden.ReadBool(pawn).value_or(false);
    // Hidden as the script left it: back where it stood, so a later scene finds it where the level put it.
    bool putBack = false;
    if (pawn != nullptr && hidden && walked > 20.0 && gSetLocation.Get() != nullptr) {
        ue3::Params at(ue3::runtime::Get(), gSetLocation.Get());
        putBack = at.Valid() && at.Set("NewLocation", U(h.at)) && ue3::runtime::Call(pawn, gSetLocation.Get(), at);
        if (gSetRotation.Get() != nullptr) {
            ue3::Params r(ue3::runtime::Get(), gSetRotation.Get());
            if (r.Valid() && r.Set("NewRotation", h.rotation)) {
                ue3::runtime::Call(pawn, gSetRotation.Get(), r);
            }
        }
    }
    log::Info("convo exit: {}'s deactivation applied after {:.1f} s: {}{} (walked {:.2f} m{}){}; hidden now {}{}", h.name, heldFor, why,
        underDip ? ", under a dip from black" : "", walked / kUnitsPerMeter,
        h.destination ? std::format(" toward a place {:.1f} m off", Dist(*h.destination, h.at) / kUnitsPerMeter) : std::string(),
        called ? "" : " [the call FAILED]", hidden ? "yes" : "no", putBack ? ", put back where it stood" : "");
    AskSnapshots(2, "exit-applied");
}

void ApplyAll(char const* why) {
    for (HeldChange& h : gHeld) {
        Apply(h, why, false);
        ++gTotals.atOnce;
    }
    gHeld.clear();
}

// The AI's own first test of a walk: BioAiController's BioSeqAct_MoveToPoint state walks straight to a point PointReachable
// admits, else along a path FindPathTo finds, and with neither gives the move up in its first tick (research/conversation-exits.md
// section 6.3: Udina at the council, 12 m off, no path there). Controller.PointReachable from where the pawn stands (a
// simulated walk; it also refuses anything flat-farther than the engine's longest path, 1200 units). Unknown (no controller or
// function): nullopt.
std::optional<bool> Reachable(ue3::Object* pawn, ik::Vec3 const& to) {
    ue3::Object* const ai = pawn != nullptr ? gController.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
    if (ai == nullptr || !ue3::runtime::IsAlive(ai) || gPointReachable.Get() == nullptr) {
        return std::nullopt;
    }
    ue3::Params p(ue3::runtime::Get(), gPointReachable.Get());
    // The state's own target: the point 5 units up
    if (!p.Valid() || !p.Set("aPoint", U(to + ik::Vec3{0.0, 0.0, 5.0})) || !ue3::runtime::Call(ai, gPointReachable.Get(), p)) {
        return std::nullopt;
    }
    return p.ReturnBool();
}

#if defined(MEVR_GAME_LE2)
// ME2's walk follows a path (game/AiWalk: the Kismet's MoveToGoal to a navigation point), so the level's navigation points are
// places too: within the core's reach of the person on its floor, hidden from the eyes, with a path the person's AI finds
// (Engine.Controller.FindPathToward, MovingToGoal's own question, 2000 units of path at most), nearest first, at most kNavMax.
// Tali's room on Freedom's Progress had no hidden place with a straight way (lane EXIT2): a door's far side is one.
ue3::FieldRef gNavList{"Engine.WorldInfo", "NavigationPointList", ue3::kEveryGame};
ue3::FieldRef gNextNav{"Engine.NavigationPoint", "nextNavigationPoint", ue3::kEveryGame};
ue3::FunctionRef gFindPathToward{"Engine.Controller.FindPathToward", ue3::kEveryGame};
// UE3's path search runs on the constraints pushed onto the pawn before it (the native clears them after): MovingToGoal's own
// ApplyPathConstraints pushes Goal_AtActor.AtActor and Path_TowardGoal.TowardGoal (static functions, on the classes' default
// objects) before its FindPathToward; without them every search came back empty (in sight or not, 2026-11-20).
ue3::ClassRef gGoalAtActorClass{"Engine.Goal_AtActor", ue3::kEveryGame};
ue3::FunctionRef gAtActor{"Engine.Goal_AtActor.AtActor", ue3::kEveryGame};
ue3::ClassRef gTowardGoalClass{"Engine.Path_TowardGoal", ue3::kEveryGame};
ue3::FunctionRef gTowardGoal{"Engine.Path_TowardGoal.TowardGoal", ue3::kEveryGame};
ue3::Kept gAtActorDefaults;
ue3::Kept gTowardGoalDefaults;

// A class's default object (the static functions' context), found once by its name.
ue3::Object* DefaultsOf(ue3::ClassRef& cls, ue3::Kept& kept) {
    if (ue3::Object* const live = kept.Live()) {
        return live;
    }
    ue3::Class* const c = cls.Get();
    ue3::Reflection& r = ue3::runtime::Get();
    for (std::int32_t i = 0, n = c != nullptr ? r.ObjectCount() : 0; i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && r.IsAlive(o) && r.ClassOf(o) == c && r.NameText(o).starts_with("Default__")) {
            kept = ue3::Kept(o);
            return o;
        }
    }
    return nullptr;
}
constexpr int kNavMax = 6;          // candidates added
constexpr int kNavAsked = 24;       // hidden ones asked for a path at most
constexpr double kNavRiseUnits = 100.0;
std::string gNavWhy;                // the last search, for the walk's log line

int AddNavCandidates(ue3::Object* pawn, ik::Vec3 const& at, std::vector<ik::Vec3> const& refused, std::vector<cx::Candidate>& out) {
    ue3::Object* const ai = gController.Read<ue3::Object*>(pawn).value_or(nullptr);
    ue3::Object* const world = gWorldOfActor.Read<ue3::Object*>(pawn).value_or(nullptr);
    ue3::Function* const find = gFindPathToward.Get();
    if (!gEyes || ai == nullptr || !ue3::runtime::IsAlive(ai) || world == nullptr || find == nullptr) {
        gNavWhy = "navigation points not searched (no eyes, AI, world or FindPathToward)";
        return 0;
    }
    struct Spot {
        ue3::Object* nav;
        ik::Vec3 spot;
        double flat;
    };
    std::vector<Spot> spots;
    ue3::Object* nav = gNavList.Read<ue3::Object*>(world).value_or(nullptr);
    for (int guard = 0; nav != nullptr && guard < 50000 && ue3::runtime::IsAlive(nav); ++guard) {
        if (auto const p = player::Location(nav)) {
            ik::Vec3 const spot = V(*p);
            double const flat = std::hypot(spot.x - at.x, spot.y - at.y);
            bool const kept = std::none_of(refused.begin(), refused.end(), [&](ik::Vec3 const& r) { return Dist(r, spot) <= 150.0; });
            if (flat >= gLimits.minUnits && flat <= gLimits.maxUnits && std::abs(spot.z - at.z) <= kNavRiseUnits && kept) {
                spots.push_back({nav, spot, flat});
            }
        }
        nav = gNextNav.Read<ue3::Object*>(nav).value_or(nullptr);
    }
    std::sort(spots.begin(), spots.end(), [](Spot const& a, Spot const& b) { return a.flat < b.flat; });
    ue3::Object* const tracer = pawn;
    ue3::Object* const atActorDefaults = gAtActor.Get() != nullptr ? DefaultsOf(gGoalAtActorClass, gAtActorDefaults) : nullptr;
    ue3::Object* const towardDefaults = gTowardGoal.Get() != nullptr ? DefaultsOf(gTowardGoalClass, gTowardGoalDefaults) : nullptr;
    if (atActorDefaults == nullptr || towardDefaults == nullptr) {
        gNavWhy = "navigation points not searched (no Goal_AtActor or Path_TowardGoal)";
        return 0;
    }
    auto const pathTo = [&](ue3::Object* goal) {
        ue3::Params at(ue3::runtime::Get(), gAtActor.Get());
        if (!at.Valid() || !at.Set("P", pawn) || !at.Set("Goal", goal) || !ue3::runtime::Call(atActorDefaults, gAtActor.Get(), at) ||
            !at.ReturnBool().value_or(false)) {
            return false;
        }
        ue3::Params toward(ue3::runtime::Get(), gTowardGoal.Get());
        if (toward.Valid() && toward.Set("P", pawn) && toward.Set("Goal", goal)) {
            ue3::runtime::Call(towardDefaults, gTowardGoal.Get(), toward);
        }
        ue3::Params p(ue3::runtime::Get(), find);
        return p.Valid() && p.Set("anActor", goal) && p.Set("MaxPathLength", std::int32_t{2000}) && ue3::runtime::Call(ai, find, p) &&
            p.Return<ue3::Object*>().value_or(nullptr) != nullptr;
    };
    int added = 0, asked = 0, seenCount = 0, seenAsked = 0, seenPathed = 0;
    for (Spot const& n : spots) {
        if (added >= kNavMax || asked >= kNavAsked) {
            break;
        }
        if (Clear(tracer, gEyes->at, n.spot + ik::Vec3{0.0, 0.0, 70.0}) || Clear(tracer, gEyes->at, n.spot)) {
            ++seenCount;
            if (seenAsked < 2) {  // a control: the path search answers for places in sight
                ++seenAsked;
                seenPathed += pathTo(n.nav) ? 1 : 0;
            }
            continue;  // in sight: the straight candidates cover walking off in view
        }
        ++asked;
        if (!pathTo(n.nav)) {
            continue;
        }
        cx::Candidate c;
        c.at = C(n.spot);
        c.path = true;
        c.hidden = true;
        out.push_back(c);
        ++added;
    }
    gNavWhy = std::format("navigation points: {} within reach on the floor, {} of them in sight ({} of {} asked with a path), {} "
                          "hidden asked for a path, {} with one", spots.size(), seenCount, seenPathed, seenAsked, asked, added);
    return added;
}
#endif

// Where to: the candidates traced from the person's place (core's CandidatePoints), the best picked (PickDestination); only places
// the AI would walk to (Reachable) and none it gave up before (`refused`).
std::optional<ik::Vec3> Destination(ue3::Object* pawn, ik::Vec3 const& at, std::string& why, bool& hidden,
    std::vector<ik::Vec3> const& refused = {}) {
    hidden = false;
    if (!gEyes) {
        why = "no view to judge sight from";
        return std::nullopt;
    }
    ue3::Object* const tracer = pawn;
    auto const floor = FloorZ(tracer, at);
    if (!floor) {
        why = "no floor under the person";
        return std::nullopt;
    }
    double const lift = at.z - *floor;  // the pawn's location above its floor
    std::vector<cx::Vec3> const points = cx::CandidatePoints(C(at), C(gEyes->at), gLimits);
    std::vector<cx::Candidate> candidates;
    int hiddenCount = 0, pathCount = 0, unreachable = 0, unknown = 0;
    for (cx::Vec3 const& q : points) {
        cx::Candidate c;
        ik::Vec3 const guess = I(q);
        auto const z = FloorZ(tracer, guess);
        if (!z || std::abs(*z - *floor) > gLimits.sameSpaceUnits) {
            candidates.push_back(c);  // no floor there, or another storey: no way
            continue;
        }
        ik::Vec3 const spot{guess.x, guess.y, *z + lift};
        c.at = C(spot);
        ik::Vec3 const knee{0.0, 0.0, -lift + 40.0};
        ik::Vec3 const chest{0.0, 0.0, 30.0};
        c.path = Clear(tracer, at + knee, spot + knee) && Clear(tracer, at + chest, spot + chest);
        for (ik::Vec3 const& r : refused) {
            c.path = c.path && Dist(r, spot) > 150.0;
        }
        if (c.path) {
            std::optional<bool> const reach = Reachable(pawn, spot);
            unknown += reach ? 0 : 1;
            if (reach && !*reach) {
                c.path = false;
                ++unreachable;
            }
        }
        ik::Vec3 const head = spot + ik::Vec3{0.0, 0.0, 70.0};
        c.hidden = !Clear(tracer, gEyes->at, head) && !Clear(tracer, gEyes->at, spot);
        hiddenCount += c.hidden ? 1 : 0;
        pathCount += c.path ? 1 : 0;
        candidates.push_back(c);
    }
#if defined(MEVR_GAME_LE2)
    std::size_t const straight = candidates.size();
    int const navAdded = AddNavCandidates(pawn, at, refused, candidates);
#endif
    auto const pick = cx::PickDestination(candidates, C(at), C(gEyes->at), gLimits);
    if (!pick) {
        why = std::format("nowhere to walk away to within {:.0f}-{:.0f} m with a clear way ({} of {} points with a way, {} hidden, {} "
                          "clear but not reachable for the AI{})",
            gLimits.minUnits / kUnitsPerMeter, gLimits.maxUnits / kUnitsPerMeter, pathCount, candidates.size(), hiddenCount, unreachable,
            unknown > 0 ? std::format(", {} not asked", unknown) : std::string());
#if defined(MEVR_GAME_LE2)
        why += "; " + gNavWhy;
        (void)navAdded;
#endif
        return std::nullopt;
    }
    hidden = pick->hidden;
    why = std::format("{} of {} points with a way, {} hidden, {} clear but not reachable for the AI{}", pathCount, candidates.size(),
        hiddenCount, unreachable, unknown > 0 ? std::format(", {} not asked", unknown) : std::string());
#if defined(MEVR_GAME_LE2)
    why += std::format("; {}{}", gNavWhy, navAdded > 0 && pick->index >= straight ? ": one picked (the walk follows the AI's path)" : "");
#endif
    return I(candidates[pick->index].at);
}

void StartTrace();

void StartWalk(HeldChange& h) {
    h.walkTried = true;
    ue3::Object* const pawn = h.pawn.Live();
    if (pawn == nullptr) {
        h.walkWhy = "the pawn is gone";
        return;
    }
#if defined(MEVR_GAME_LE3)
    if (ue3::IsA(pawn, gStuntClass)) {
        h.walkWhy = "a stunt actor (no AI walks it)";
        ++gTotals.walkRefused;
        log::Info("convo exit: {} can't walk away: a stunt actor, no AI walks it; the change waits for the player to look away", h.name);
        return;
    }
#endif
    std::string why;
    bool hidden = false;
    auto const to = Destination(pawn, h.at, why, hidden, h.refused);
    if (!to) {
        h.walkWhy = why;
        ++gTotals.walkRefused;
        log::Info("convo exit: {} can't walk away: {}; the change waits for the player to look away", h.name, why);
        return;
    }
    std::string const traced = why;
    h.destination = *to;
    StartTrace();
    auto w = walk::Start(pawn, U(*to), why);
    if (!w) {
        h.walkWhy = why;
        ++gTotals.walkRefused;
        log::Info("convo exit: {} can't walk away (the game's AI: {}); the change waits for the player to look away", h.name, why);
        return;
    }
    h.walking = *w;
    ++gTotals.walks;
    log::Info("convo exit: {} walks away {:.1f} m {} with the game's AI ({})", h.name, Dist(*to, h.at) / kUnitsPerMeter,
        hidden ? "to a place the player can't see" : "straight away from the player (nowhere out of sight within reach: it leaves the view)",
        traced);
}

// `mevr convo exit test <tag>`: the walk away of anyone now, without a hold (does the game's AI walk this person here?), 8 s
// at most, with the AI's call trace; its progress each second (TickTest).
struct TestRun {
    std::optional<walk::Walk> walking;
    ue3::Kept pawn;
    std::string name;
    ik::Vec3 from;
    double startS = 0.0;
    double nextLogS = 0.0;
};
TestRun gTest;

void StartTrace() {
    if (gTraceWalks) {
        log::Info("convo exit: {}", call_trace::Start(4.0, {"PushMoveToPoint", "MoveTo", "PointReachable", "FindPathTo", "GetNextPathNode",
            "PrecisionMoveTo", "ReachedDestination", "GetMoveTolerance", "NeedPrecisionMovement", "InitState", "PopState", "CanUseStack",
            "DoesGameModeAllowMovement", "HandleTeleport"}));
    }
}

std::string TestWalk(std::string const& who) {
    ue3::Object* const player = player::Pawn(player::Controller());
    ue3::Object* const world = player != nullptr ? gWorldOfActor.Read<ue3::Object*>(player).value_or(nullptr) : nullptr;
    ue3::Object* found = nullptr;
    ue3::Object* o = world != nullptr ? gWorldPawns.Read<ue3::Object*>(world).value_or(nullptr) : nullptr;
    for (int guard = 0; o != nullptr && guard < 512 && ue3::runtime::IsAlive(o); ++guard) {
        if (o != player && (ini::Lower(TagOf(o)) == who || ini::Lower(Short(o)) == who)) {
            found = o;
            break;
        }
        o = gNextPawn.Read<ue3::Object*>(o).value_or(nullptr);
    }
    if (found == nullptr) {
        return std::format("convo exit: test: no pawn tagged or named '{}' in the world", who);
    }
    auto const at = player::Location(found);
    if (!at) {
        return "convo exit: test: no place for it";
    }
    if (gTest.walking) {
        walk::End(*gTest.walking, "a new test");
        gTest.walking.reset();
    }
    std::string why;
    bool hidden = false;
    auto const to = Destination(found, V(*at), why, hidden);
    if (!to) {
        return "convo exit: test: " + why;
    }
    StartTrace();
    auto w = walk::Start(found, U(*to), why);
    if (!w) {
        return "convo exit: test: the game's AI refused: " + why;
    }
    gTest.walking = *w;
    gTest.pawn = ue3::Kept(found);
    gTest.name = std::format("{} ({})", Short(found), TagOf(found));
    gTest.from = V(*at);
    gTest.startS = NowS();
    gTest.nextLogS = gTest.startS + 1.0;
    return std::format("convo exit: test: {} walks {:.1f} m {} ({})", gTest.name, Dist(*to, V(*at)) / kUnitsPerMeter,
        hidden ? "to a place the player can't see" : "straight away from the player", why);
}

void TickTest(double now) {
    if (!gTest.walking) {
        return;
    }
    ue3::Object* const pawn = gTest.pawn.Live();
    walk::Phase const ph = pawn != nullptr ? walk::Step(*gTest.walking) : walk::Phase::Stalled;
    double const moved = pawn != nullptr ? Dist(V(player::Location(pawn).value_or(U(gTest.from))), gTest.from) : 0.0;
    if (now >= gTest.nextLogS) {
        gTest.nextLogS = now + 1.0;
        log::Info("convo exit: test: {} {} {:.2f} m after {:.1f} s", gTest.name, walk::Name(ph), moved / kUnitsPerMeter, now - gTest.startS);
    }
    if (ph != walk::Phase::Walking || now - gTest.startS > 8.0) {
        log::Info("convo exit: test: {} ends {} after {:.2f} m in {:.1f} s", gTest.name, walk::Name(ph), moved / kUnitsPerMeter, now - gTest.startS);
        walk::End(*gTest.walking, "the test's end");
        gTest.walking.reset();
    }
}

cx::Context ContextNow() {
    cx::Context c;
    c.mode = CurrentMode();
    c.worldMode = gSettings.world;
    c.sceneShown = gSceneShown;
    c.sinceSceneEndS = gSceneShownS < 0.0 ? -1.0 : NowS() - gSceneShownS;
    c.underBlack = UnderGameBlack();
    return c;
}

// ---- The hook's provider (inside the level script's update, the game thread) -----------------------------------------------------
bool OnSetActive(void const* behavior, void const* parms) {
    if (gApplying) {
        return false;  // ours
    }
    bool const active = gSetActiveParam.ReadBool(parms).value_or(true);
    double const now = NowS();
    if (active) {
        // A held one shown again by the script: its held change first, then this one, in the script's order.
        for (std::size_t i = 0; i < gHeld.size(); ++i) {
            if (gHeld[i].behavior.Raw() == behavior) {
                HeldChange h = std::move(gHeld[i]);
                gHeld.erase(gHeld.begin() + static_cast<std::ptrdiff_t>(i));
                ++gTotals.again;
                Apply(h, "the script shows them again", false);
                return false;
            }
        }
        if (!gHeld.empty()) {
            gActivations.push_back({behavior, now});  // the tick checks it for a swap
        }
        return false;
    }
    // The same behaviour deactivated again while held (the hearing's end has two ops for Anderson): withheld again, one hold.
    for (HeldChange const& h : gHeld) {
        if (h.behavior.Raw() == behavior) {
            ++gTotals.again2;
            return true;
        }
    }
    Person* const person = FindPerson(behavior);
    cx::Subject s;
    ue3::Object* pawn = person != nullptr ? person->pawn.Live() : nullptr;
    if (pawn == nullptr) {
        // Nobody of a scene: the game's own business (logged, a few per level, while the first-person mode is on).
        ++gTotals.elsewhere;
        if (gSettings.world && gUnmatchedLogged < 12) {
            ++gUnmatchedLogged;
            log::Info("convo exit: the script deactivates {} (nobody of a scene): its own business",
                ue3::runtime::FullName(static_cast<ue3::Object const*>(behavior)));
        }
        return false;
    }
    s.person = now - person->lastInSceneS <= gLimits.windowS || person->conversation == gConversationNumber;
#if defined(MEVR_GAME_LE3)
    // ME3's hubs have no party: a squadmate there is the crew's own stunt actor (BioH_<name>_00_NC), tagged hench_<name> (James
    // hench_marine, Javik hench_prothean), never in the live party ME1's and ME2's rule asks.
    s.squad = IsSquad(pawn) || person->tag.rfind("hench_", 0) == 0;
#else
    s.squad = IsSquad(pawn);
#endif
    s.player = pawn == player::Pawn(player::Controller());
    s.seen = person->seen;
    cx::Verdict const v = cx::Classify(cx::Kind::Deactivate, s, ContextNow(), gLimits);
    if (!v.hold) {
        ++gTotals.passed;
        gTotals.passedSeen += s.seen ? 1 : 0;
        if (s.seen) {
            person->passedSeenS = now;  // the audit: a pop the player saw, unless something covers it
        }
        gLastPass = std::format("{}: {}", person->name, v.why);
        if (gSettings.log) {
            log::Info("convo exit: the script deactivates {}: let through ({})", person->name, v.why);
        }
        return false;
    }
    HeldChange h;
    h.id = gNextId++;
    h.kind = cx::Kind::Deactivate;
    h.pawn = ue3::Kept(pawn);
    h.behavior = ue3::Kept(const_cast<ue3::Object*>(static_cast<ue3::Object const*>(behavior)));
    h.name = person->name;
    h.tag = person->tag;
    h.conversation = person->conversation;
    h.at = person->at;
    h.rotation = player::Rotation(pawn).value_or(ue3::Rotator{});
    h.timer.heldS = now;
    ++gTotals.held;
    log::Info("convo exit: the script deactivates {} in the player's view ({:.1f} m from the eyes): held back ({}), worldExit = {}", h.name,
        gEyes ? Dist(gEyes->at, h.at) / kUnitsPerMeter : -1.0, v.why, cx::Name(CurrentMode()));
    gHeld.push_back(std::move(h));
    AskSnapshots(3, "exit-held");
    return true;
}

#if defined(MEVR_GAME_LE3)
// ME3's BioSeqAct_SetActive: its script calls the target's SetActive with the op's bSetActive (engine/ExitHooks' script path);
// the same question as ProcessEvent's, with the value as SetActive's parameter block (one UBOOL, bActive at offset 0).
bool OnScriptSetActive(void const* actor, void const* op) {
    auto const active = gOpSetActive.ReadBool(op);
    if (!active) {
        return false;
    }
    std::uint32_t const parms = *active ? 1u : 0u;
    return OnSetActive(actor, &parms);
}
#endif

#if !defined(MEVR_GAME_LE1)
// The script's black screen turning (or fading to) black (ME1's EBlackScreenActionSet: 0 on, 2 fade to black): it covers a
// removal in the same tick (the hold lets it through).
void OnBlackScreen(ue3::Object* context, ue3::Function*, void*, void*) {
    int const action = context != nullptr ? static_cast<int>(gBlackScreenAction.Read<std::uint8_t>(context).value_or(255)) : 255;
    if (action == 0 || action == 2) {
        gBlackOnMs = GetTickCount64();
    }
}
bool gBlackScreenWatched = false;
#endif

bool gHookFunctionSet = false;

// ---- The live audit ---------------------------------------------------------------------------------------------------------------
// The rule (`convo: audit: vanished-in-view`, which fails an audit run) is for what this module governs: a deactivation the
// player saw go through (worldExit off, or a rule's exception) with no dip, cut or game black over it. Someone gone from the view
// by another route (a line's hide, a hologram switched off, a matinee) is noticed, counted and logged, not failed: those routes
// are the next steps (research/conversation-exits.md section 7).
void Audit(double now) {
    bool const dipped = now - gLastDipS < 0.6 || now - gCutChangedS < 0.6 || UnderGameBlack();
    for (Person& p : gPeople) {
        bool const held = std::any_of(gHeld.begin(), gHeld.end(), [&](HeldChange const& h) { return h.pawn.Raw() == p.pawn.Raw(); });
        if (held || now - p.appliedS < 1.0 || !cx::VanishedInView(p.seenBefore, p.presentBefore, p.present, dipped) ||
            now - p.lastInSceneS > gLimits.windowS + 1.0) {
            continue;
        }
        double const fromEyes = gEyes ? Dist(gEyes->at, p.at) / kUnitsPerMeter : -1.0;
        if (now - p.passedSeenS < 1.0) {
            ++gTotals.vanished;
            log::Info("convo: audit: vanished-in-view: {} was in the player's view and in sight, {:.1f} m from the eyes, and the script's "
                      "deactivation took it away without a dip from black (at {:.1f} s)", p.name, fromEyes, now);
        } else {
            ++gTotals.vanishedOther;
            log::Info("convo exit: noticed: {} was in the player's view and in sight, {:.1f} m from the eyes, and is gone by another route "
                      "than a deactivation (a hide, a hologram, a matinee): not held", p.name, fromEyes);
        }
    }
}

} // namespace

void TryStartup() {
#if defined(MEVR_GAME_LE1)
    if (!ue3::runtime::Ready()) {
        return;
    }
#else
    if (!ue3::runtime::Up()) {
        return;
    }
#endif
    if (gSetActive.Get() != nullptr && gSetActiveParam.Get() != nullptr) {
        engine::exit_hooks::SetProvider(&OnSetActive, gSetActive.Get());
        gHookFunctionSet = true;
    } else {
        log::Warn("convo exit: SFXGame.BioActorBehavior.SetActive or its bSetActive not found: people leaving a conversation are not held");
    }
#if defined(MEVR_GAME_LE3)
    if (gHookFunctionSet && gScriptSetActive.Get() != nullptr && gOpSetActive.Get() != nullptr) {
        engine::exit_hooks::SetScriptProvider(&OnScriptSetActive, gScriptSetActive.Get());
    } else {
        log::Warn("convo exit: SFXGame.BioSeqAct_SetActive.Activated or its bSetActive not found: ME3's deactivations are not held");
    }
#endif
#if !defined(MEVR_GAME_LE1)
    if (!gBlackScreenWatched && gBlackScreen.Get() != nullptr) {
        engine::process_event::AddPostHandler(gBlackScreen.Get(), &OnBlackScreen);
        gBlackScreenWatched = true;
    }
#endif
}

bool WantsHook() {
    RefreshSettings();
    return gSettings.world && CurrentMode() != cx::Mode::Off && gHookFunctionSet;
}

bool WantsWalks() {
    return WantsHook() && CurrentMode() == cx::Mode::Walk;
}

void Tick(double /*deltaSeconds*/) {
    RefreshSettings();
    double const now = NowS();
    ue3::Object* const controller = player::Controller();
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
#if defined(MEVR_GAME_LE1)
    ue3::Object* const conv = conversation_world::LiveConversation();
#else
    // In the conversation mode only, as ME1's reader (ME2's m_oCurrentConversation can outlive its end; ME3's reader asks).
    // (ME3's reading of the scene's people, beyond its speaker data, is the sequels' module's own since lane FPC4: one reading.)
    ue3::Object* const conv = IsMode(mode, engine::adapter::Mode::Conversation) ? sequel_conversation_world::ScenePeople(gScenePeople) : nullptr;
#endif
    bool const inWorld = conversation_world::InWorld() || conversation_world::MomentViewOn();
    gSceneShown = inWorld;
    if (inWorld) {
        gSceneShownS = now;
    }
    if (std::uint32_t const cut = conversation_world::CutCount(); cut != gCutSeen) {
        gCutSeen = cut;
        gCutChangedS = now;
    }
    // The drawn eyes and what they see, for the hook's next questions and the audit.
    gEyes.reset();
    if (auto const frame = view_anchor::Current()) {
        gEyes = Eyes{{frame->headWorld.x, frame->headWorld.y, frame->headWorld.z}, *frame};
    }
    ReadScene(conv, now);
    TickTest(now);
    for (Person& p : gPeople) {
        p.seenBefore = p.seen;
        p.presentBefore = p.present;
        ue3::Object* const pawn = p.pawn.Live();
        p.present = Present(pawn);
        p.seen = p.present && Seen(pawn);
        if (pawn != nullptr) {
            if (auto const at = player::Location(pawn)) {
                p.at = V(*at);
            }
        }
    }
    if (gSettings.world) {
        Audit(now);
    }
    // People no longer of any scene nor held, gone or long out of one: forgotten.
    std::erase_if(gPeople, [&](Person const& p) {
        bool const held = std::any_of(gHeld.begin(), gHeld.end(), [&](HeldChange const& h) { return h.pawn.Raw() == p.pawn.Raw(); });
        return !held && (p.pawn.Live() == nullptr || now - p.lastInSceneS > 30.0);
    });
    // What the player sees now: the world in first person on foot, a conversation in it, a moment in it.
    // Held half a second: between two conversations the world view lets go for a tick or two (Udina's walk goes on while
    // Anderson's final chat opens), which is no screen the player sees instead.
#if defined(MEVR_GAME_LE1)
    bool const foot = mode && *mode == kModeFoot;
    bool const awayNow = !gEyes || !(foot || (mode && (*mode == kModeConversation || *mode == kModeCinematic) && inWorld));
#else
    bool const foot = IsMode(mode, engine::adapter::Mode::OnFoot);
    bool const awayNow = !gEyes || !(foot || ((IsMode(mode, engine::adapter::Mode::Conversation) ||
                                                  IsMode(mode, engine::adapter::Mode::Cinematic)) && inWorld));
#endif
    if (!awayNow) {
        gAwaySinceS.reset();
    } else if (!gAwaySinceS) {
        gAwaySinceS = now;
    }
    bool const viewAway = gAwaySinceS && now - *gAwaySinceS >= kAwaySettleS;
    if (!gSettings.world || CurrentMode() == cx::Mode::Off) {
        if (!gHeld.empty()) {
            ApplyAll("the first-person mode or worldExit is off now");
        }
        gActivations.clear();
        return;
    }
    // Swaps: someone shown where a held one stands, within a second of the hold (Benezia's conversation pawn and her combat one).
    for (Activation const& a : gActivations) {
        ue3::Object* shown = nullptr;
        if (ue3::Object* const pawn = player::Pawn(controller); pawn != nullptr) {
            ue3::Object* const world = gWorldOfActor.Read<ue3::Object*>(pawn).value_or(nullptr);
            ue3::Object* o = world != nullptr ? gWorldPawns.Read<ue3::Object*>(world).value_or(nullptr) : nullptr;
            for (int guard = 0; o != nullptr && guard < 512 && ue3::runtime::IsAlive(o); ++guard) {
#if defined(MEVR_GAME_LE1)
                if (gBioComponent.Read<ue3::Object*>(o).value_or(nullptr) == a.behavior ||
                    gPawnBehavior.Read<ue3::Object*>(o).value_or(nullptr) == a.behavior) {
#else
                if (o == a.behavior) {
#endif
                    shown = o;
                    break;
                }
                o = gNextPawn.Read<ue3::Object*>(o).value_or(nullptr);
            }
        }
        auto const shownAt = shown != nullptr ? player::Location(shown) : std::nullopt;
        for (std::size_t i = 0; shownAt && i < gHeld.size();) {
            if (cx::IsSwap(C(gHeld[i].at), C(V(*shownAt)), a.atS - gHeld[i].timer.heldS, gLimits)) {
                HeldChange h = std::move(gHeld[i]);
                gHeld.erase(gHeld.begin() + static_cast<std::ptrdiff_t>(i));
                ++gTotals.swaps;
                log::Info("convo exit: the script shows {} where {} stands: one actor swapped for another", Short(shown), h.name);
                Apply(h, "a swap: another actor shown in its place", false);
                continue;
            }
            ++i;
        }
    }
    gActivations.clear();
    // Each held change: the walk under way, then the rule's word.
    for (std::size_t i = 0; i < gHeld.size();) {
        HeldChange& h = gHeld[i];
        ue3::Object* const pawn = h.pawn.Live();
        if (pawn == nullptr || h.behavior.Live() == nullptr) {
            Apply(h, "the object is gone", false);
            gHeld.erase(gHeld.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        // The next scene needs them: a conversation that began since lists them, or a double of the same character (the same
        // tag: the hearing's Anderson and the one of the conversation after it are two pawns).
        bool needed = false;
        if (conv != nullptr && gConversationNumber != h.conversation) {
            for (Person const& p : gPeople) {
                if (p.conversation == gConversationNumber && now - p.lastInSceneS < 0.5 &&
                    (p.pawn.Raw() == pawn || (!h.tag.empty() && h.tag != "?" && p.tag == h.tag))) {
                    needed = true;
                }
            }
        }
        if (needed) {
            Apply(h, "the next scene needs them (or their double)", false);
            ++gTotals.atOnce;
            gHeld.erase(gHeld.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        // Their level is streamed out (never held): it takes them anyway, with everything around them.
        if (std::string const going = LevelGoing(pawn); !going.empty()) {
            Apply(h, std::format("their level {} is streamed out (the script's streaming change takes them anyway)", going).c_str(), false);
            ++gTotals.atOnce;
            gHeld.erase(gHeld.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        if (CurrentMode() == cx::Mode::Walk && !h.walkTried && !viewAway) {
            StartWalk(h);
        }
        bool walkingNow = false;
        if (h.walking) {
            walk::Phase const ph = walk::Step(*h.walking);
            if (auto const at = player::Location(pawn)) {
                h.nearestUnits = Dist(V(*at), h.at);
            }
            if (ph == walk::Phase::Walking && !cx::WalkOverdue(h.timer, now, gLimits)) {
                walkingNow = true;
            } else if (ph == walk::Phase::Stalled && h.nearestUnits < 50.0 && !h.retried && h.destination) {
                // Given up where it stood (the AI found no way it knows): once more, to another place.
                StopWalk(h, "the AI gave the place up");
                h.refused.push_back(*h.destination);
                h.walkTried = false;
                h.retried = true;
                walkingNow = true;
                log::Info("convo exit: {}: the game's AI gave the walk to ({:.0f}, {:.0f}, {:.0f}) up where it stood: another place is "
                          "asked", h.name, h.destination->x, h.destination->y, h.destination->z);
            } else {
                log::Info("convo exit: {}'s walk {} after {:.2f} m ({})", h.name, walk::Name(ph), h.nearestUnits / kUnitsPerMeter,
                    ph == walk::Phase::Walking ? "not out of sight in time" : "the game's AI");
                StopWalk(h, ph == walk::Phase::Arrived ? "arrived" : "it no longer walks away");
            }
        }
        Person const* person = FindPersonPawn(pawn);
        cx::Facts f;
        f.nowS = now;
        f.seen = person != nullptr ? person->seen : Seen(pawn);
        f.walking = walkingNow;
        f.viewAway = viewAway;
        cx::StepResult const r = cx::Step(h.timer, f, gLimits);
        if (r.act == cx::Act::Keep) {
            ++i;
            continue;
        }
        if (r.act == cx::Act::Apply) {
            ++gTotals.outOfSight;
        }
        Apply(h, r.why, r.act == cx::Act::ApplyUnderDip);
        gHeld.erase(gHeld.begin() + static_cast<std::ptrdiff_t>(i));
    }
    gDipWanted = false;
}

std::string Command(std::string const& rest) {
    std::string const w = ini::Lower(ini::Trim(rest));
    if (w == "on" || w == "walk") {
        gModeOverride = cx::Mode::Walk;
    } else if (w == "hold") {
        gModeOverride = cx::Mode::Hold;
    } else if (w == "off") {
        gModeOverride = cx::Mode::Off;
    } else if (w == "auto") {
        gModeOverride.reset();
    } else if (w == "shots on" || w == "shots off") {
        gShots = w == "shots on";
        return std::format("convo exit: eye snapshots as a hold begins and is applied {}", gShots ? "on" : "off");
    } else if (w == "trace on" || w == "trace off") {
        gTraceWalks = w == "trace on";
        return std::format("convo exit: a call trace of the AI's move for each walk away {}", gTraceWalks ? "on" : "off");
#if defined(MEVR_GAME_LE2)
    } else if (w == "end") {
        // A test aid (ME1's `mevr convo end`; ME2 only: ME3 has no BioWorldInfo.EndCurrentConversation): the world's own early
        // end of the conversation now, so its start action's outputs run (the script's removals at its end) without every line.
        static ue3::FunctionRef endCurrent{"SFXGame.BioWorldInfo.EndCurrentConversation", ue3::kEveryGame};
        ue3::Object* const pawn = player::Pawn(player::Controller());
        ue3::Object* const world = pawn != nullptr ? gWorldOfActor.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
        if (world == nullptr || endCurrent.Get() == nullptr) {
            return "convo exit: end: no world or no BioWorldInfo.EndCurrentConversation";
        }
        ue3::Params p(ue3::runtime::Get(), endCurrent.Get());
        bool const called = p.Valid() && ue3::runtime::Call(world, endCurrent.Get(), p);
        log::Info("convo exit: mevr convo exit end: BioWorldInfo.EndCurrentConversation called {}", called);
        return std::format("convo exit: BioWorldInfo.EndCurrentConversation called {}", called);
    } else if (w.rfind("walkby", 0) == 0) {
        // An A/B of ME2's walks: the Kismet's MoveToGoal to a navigation point (the default) or the precise step.
        return std::format("convo exit: walks by {}", walk::SetLe2Move(ini::Trim(w.substr(6))));
#endif
#if defined(MEVR_GAME_LE3)
    } else if (w == "skip") {
        // A test aid (ME3 has no BioWorldInfo.EndCurrentConversation): the player's skip of the current line
        // (BioConversationController.SkipNode), so a script reaches the conversation's end, and its start's outputs, sooner.
        std::vector<ue3::Object*> people;
        ue3::Object* const conversation = sequel_conversation_world::ScenePeople(people);
        if (conversation == nullptr || gSkipNode.Get() == nullptr) {
            return "convo exit: skip: no live conversation, or no BioConversationController.SkipNode";
        }
        ue3::Params p(ue3::runtime::Get(), gSkipNode.Get());
        bool const called = p.Valid() && ue3::runtime::Call(conversation, gSkipNode.Get(), p);
        return std::format("convo exit: SkipNode called {}, returned {}", called, p.ReturnBool().value_or(false));
    } else if (w.rfind("reply", 0) == 0) {
        // ... and the wheel's reply at a position queued (BioConversationController.QueueReply, what the movie's pick ends in;
        // game/Thespian's own pick waits for a focused session, which the fake headset never has).
        int const position = static_cast<int>(ini::Number(ini::Trim(w.substr(5))).value_or(-1.0));
        std::vector<ue3::Object*> people;
        ue3::Object* const conversation = sequel_conversation_world::ScenePeople(people);
        if (conversation == nullptr || gQueueReply.Get() == nullptr || position < 0) {
            return "convo exit: reply <position>: no live conversation, no BioConversationController.QueueReply, or no position";
        }
        ue3::Params p(ue3::runtime::Get(), gQueueReply.Get());
        bool const called = p.Valid() && p.Set("nReply", static_cast<std::int32_t>(position)) &&
            ue3::runtime::Call(conversation, gQueueReply.Get(), p);
        return std::format("convo exit: QueueReply({}) called {}, returned {}", position, called, p.ReturnBool().value_or(false));
#endif
#if !defined(MEVR_GAME_LE1)
    } else if (w == "people") {
        // A test aid: each person noted, where it stands from the drawn eyes (bearing right of the view, elevation, metres),
        // whether present, in view and seen (the sequels' fixtures aim the replayed head by it).
        std::string out = std::format("convo exit: {} people, eyes {}", gPeople.size(), gEyes ? "known" : "unknown");
        for (Person const& p : gPeople) {
            double bearing = 0.0, elevation = 0.0;
            if (gEyes) {
                XrVector3f const t = engine::ToTracking(gEyes->frame, engine::UnrealVector{p.at.x, p.at.y, p.at.z});
                XrPosef const& head = gEyes->frame.head;
                ik::Quat const toHead = ik::Conjugate({head.orientation.x, head.orientation.y, head.orientation.z, head.orientation.w});
                ik::Vec3 const v = ik::Rotate(toHead, {t.x - head.position.x, t.y - head.position.y, t.z - head.position.z});
                bearing = std::atan2(v.x, -v.z) * 180.0 / 3.14159265358979;
                elevation = std::atan2(v.y, std::hypot(v.x, v.z)) * 180.0 / 3.14159265358979;
            }
            out += std::format("\n  {}: {:.1f} m, {:.0f} deg right, {:.0f} deg up; present {}, in view {}, seen {}", p.name,
                gEyes ? Dist(gEyes->at, p.at) / kUnitsPerMeter : -1.0, bearing, elevation, p.present, InView(p.at), p.seen);
        }
        return out;
#endif
    } else if (w.rfind("test", 0) == 0) {
        return TestWalk(ini::Trim(w.substr(4)));
    } else if (!w.empty() && w != "status") {
#if defined(MEVR_GAME_LE3)
        return "mevr convo exit [on | walk | hold | off | auto | status | shots on|off | trace on|off | test <tag> | skip | reply <n>]";
#else
        return "mevr convo exit [on | walk | hold | off | auto | status | shots on|off | trace on|off | test <tag>]";
#endif
    }
    if (!w.empty() && w != "status") {
        log::Info("convo exit: worldExit {} from the console (mevr convo exit {})", cx::Name(CurrentMode()), w);
    }
    return "convo exit: " + StatusJson();
}

std::string StatusJson() {
    std::string held = "[";
    double const now = NowS();
    for (HeldChange const& h : gHeld) {
        if (held.size() > 1) {
            held += ", ";
        }
        held += std::format("{{\"name\": {}, \"kind\": \"{}\", \"heldS\": {:.1f}, \"walking\": {}, \"walkedM\": {:.2f}, \"why\": {}}}",
            vr::test_request::JsonString(h.name), cx::Name(h.kind), now - h.timer.heldS, h.walking ? "true" : "false",
            h.nearestUnits / kUnitsPerMeter, vr::test_request::JsonString(h.walkWhy));
    }
    held += "]";
    engine::exit_hooks::Stats const hook = engine::exit_hooks::Totals();
#if defined(MEVR_GAME_LE3)
    // ME3's calls from BioSeqAct_SetActive's own script frame, apart from ProcessEvent's (`fromEvent`): the hook's script path.
#define MEVR_EXIT_FROM_SCRIPT "\"fromScript\": {}, "
#define MEVR_EXIT_FROM_SCRIPT_ARG , engine::exit_hooks::FromScript()
#else
#define MEVR_EXIT_FROM_SCRIPT ""
#define MEVR_EXIT_FROM_SCRIPT_ARG
#endif
    return std::format("{{\"mode\": \"{}\", \"world\": {}, \"hooked\": {}, \"held\": {}, \"people\": {}, \"totals\": {{\"held\": {}, "
                       "\"outOfSight\": {}, \"underDip\": {}, \"atOnce\": {}, \"walks\": {}, \"walkRefused\": {}, \"swaps\": {}, \"shownAgain\": {}, "
                       "\"passed\": {}, \"passedSeen\": {}, \"vanished\": {}, \"vanishedOther\": {}, \"elsewhere\": {}, \"heldAgain\": {}}}, \"hook\": {{\"calls\": {}, \"fromEvent\": {}, " MEVR_EXIT_FROM_SCRIPT "\"withheld\": {}}}, "
                       "\"lastPass\": {}, \"aiWalk\": {}}}",
        cx::Name(CurrentMode()), gSettings.world ? "true" : "false", engine::exit_hooks::Installed() ? "true" : "false", held,
        gPeople.size(), gTotals.held, gTotals.outOfSight, gTotals.underDip, gTotals.atOnce, gTotals.walks, gTotals.walkRefused,
        gTotals.swaps, gTotals.again, gTotals.passed, gTotals.passedSeen, gTotals.vanished, gTotals.vanishedOther, gTotals.elsewhere, gTotals.again2, hook.calls, hook.fromEvent MEVR_EXIT_FROM_SCRIPT_ARG, hook.withheld,
        vr::test_request::JsonString(gLastPass), walk::StatusJson());
}
#undef MEVR_EXIT_FROM_SCRIPT
#undef MEVR_EXIT_FROM_SCRIPT_ARG

void LogStats() {
    // The heartbeat's line only when something changed since the last one (it comes every 5 s in development builds).
    static std::string last;
    std::string const now = gSettings.world ? StatusJson() : std::string();
    if (now.empty() || now == last) {
        return;
    }
    last = now;
    log::Info("convo exit: {}", now);
}

void Shutdown(bool exiting) {
    engine::exit_hooks::SetProvider(nullptr, nullptr);
#if defined(MEVR_GAME_LE3)
    engine::exit_hooks::SetScriptProvider(nullptr, nullptr);
#endif
    gHookFunctionSet = false;
    if (gTest.walking) {
        walk::End(*gTest.walking, "the mod unloads");
        gTest.walking.reset();
    }
    if (!exiting && !gHeld.empty()) {
        ApplyAll("the mod unloads");
    }
    for (HeldChange& h : gHeld) {
        StopWalk(h, "the process exits");
    }
    gHeld.clear();
    gPeople.clear();
    gConversationObject = nullptr;
}

} // namespace mevr::game::conversation_exit
