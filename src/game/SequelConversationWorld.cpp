// SPDX-License-Identifier: MIT
// ME3's slice of ME1's first-person conversations (game/SequelConversationWorld.hpp).
#include "game/SequelConversationWorld.hpp"

// The sequels (Mod.cpp and ConsoleRequest.cpp call it under MEVR_GAME_LE2 and MEVR_GAME_LE3; ME1 has its own,
// game/ConversationWorld). Each game's reader of the live conversation is below (ME2's BioConversation is ME1's object; ME3's
// is the BioConversationController); everything after it is one code path.
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/Rigid.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/ConversationWorld.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/SettingsFile.hpp"
#include "engine/GameAdapter.hpp"
#include "game/ArmIk.hpp"
#include "game/DepthOfFieldHold.hpp"
#include "game/HandCalibration.hpp"
#include "game/SquadWeapons.hpp"
#include "core/vr/Comfort.hpp"
#if defined(MEVR_GAME_LE3)
#include "game/Le3Gui.hpp"
#endif
#include "engine/ConversationHooks.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/SettingsFile.hpp"
#include "engine/Skeleton.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/Player.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace mevr::game::sequel_conversation_world {
namespace {

namespace cw = vr::conversation_world;

ue3::FieldRef gActorWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
#if defined(MEVR_GAME_LE3)
constexpr char kController[] = "SFXGame.BioConversationController";
ue3::FieldRef gManager{"SFXGame.BioWorldInfo", "m_pConversationManager", ue3::kEveryGame};
ue3::FunctionRef gGetFull{"SFXGame.BioConversationManager.GetFullConversation", ue3::kEveryGame};
ue3::FieldRef gEnded{kController, "m_bConversationEnded", ue3::kEveryGame};
ue3::FieldRef gPlayer{kController, "m_pPlayer", ue3::kEveryGame};
ue3::FieldRef gSpeakers{kController, "m_aSpeakerData", ue3::kEveryGame};
ue3::FieldRef gOwner{kController, "m_pOwner", ue3::kEveryGame};
ue3::ClassRef gControllerClass{kController, ue3::kEveryGame};
// Who speaks to whom now, and the entry (the stance records' roles and lines).
ue3::FieldRef gSpeakerNow{kController, "m_pSpeaker", ue3::kEveryGame};
ue3::FieldRef gListenerNow{kController, "m_pListener", ue3::kEveryGame};
ue3::FieldRef gEntryNow{kController, "m_nCurrentEntry", ue3::kEveryGame};
#else
// ME2: ME1's live conversation (BioWorldInfo.m_oCurrentConversation, a BioConversation) and its people, ME1's own fields
// (game/ConversationWorld's gCurrentConversation, gSpeakerList, gConvPlayer ...; research/le2-conversations-in-world.md).
ue3::FieldRef gCurrent{"SFXGame.BioWorldInfo", "m_oCurrentConversation", ue3::kEveryGame};
ue3::ClassRef gControllerClass{"SFXGame.BioConversation", ue3::kEveryGame};
ue3::FieldRef gPlayer{"SFXGame.BioConversation", "m_pConversationPlayer", ue3::kEveryGame};
ue3::FieldRef gOwner{"SFXGame.BioConversation", "m_pConversationOwner", ue3::kEveryGame};
ue3::FieldRef gSpeakerNow{"SFXGame.BioConversation", "m_pSpeaker", ue3::kEveryGame};
ue3::FieldRef gListenerNow{"SFXGame.BioConversation", "m_pListener", ue3::kEveryGame};
ue3::FieldRef gSpeakerList{"SFXGame.BioConversation", "m_SpeakerList", ue3::kEveryGame};
ue3::FieldRef gSpeakerActor{gSpeakerList, "aSpeaker"};
ue3::FieldRef gEntryNow{"SFXGame.BioConversation", "m_nCurrentEntry", ue3::kEveryGame};
#endif
// The director's near plane (ME1's worldNearPlane, research/69 "The first shot"): a conversation's camera writes
// LocalPlayer.NearClipPlane per shot (a metre or more), which from the player's eyes cuts away the floor underfoot, the
// player's own body and whoever stands near; while the view is held, the plane on foot is written back after each camera
// update (ME2's line matinees set it too: SFXInterpTrackSetPlayerNearClipPlane, 127 of them).
ue3::FieldRef gLocalPlayer{"Engine.PlayerController", "Player", ue3::kEveryGame};
ue3::FieldRef gNearClip{"Engine.LocalPlayer", "NearClipPlane", ue3::kEveryGame};
float gFootNear = 10.0f;      // the plane on foot (read each tick there)
float gNearReplaced = 0.0f;   // the director's plane last replaced (logged when it changes)
unsigned gNearWrites = 0;
float gDrawnNear = -1.0f;    // the near plane the last held frame was drawn with (after the write-back; the records')
ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera", ue3::kEveryGame};
ue3::FunctionRef gSetLocation{"Engine.Actor.SetLocation", ue3::kEveryGame};
// The conversation camera constrains the picture to its 16:9 band (a letterbox): the stereo capture never matches it and the
// renderer showed the band stretched and mono (the owner, 2026-11-19: "squashed horizontally ... not stereoscopic"). ME1's
// [NOLETTERBOX] cure: cleared after the camera update that sets it, before the frame's Draw consumes it, every update.
ue3::FieldRef gConstrainAspect{"Engine.Camera", "bConstrainAspectRatio", ue3::kEveryGame};
// The opening's "same space" (ME1's InSight, research/69): the mark is in sight of where the player stood if one of three lines
// is clear (to it, to 80 units above it, and that one from 60 units above the eyes); a wall between two rooms stops all three.
ue3::FunctionRef gFastTrace{"Engine.Actor.FastTrace", ue3::kEveryGame};
unsigned gLetterboxClears = 0;

#if defined(MEVR_GAME_LE3)
// FBioSpeakerData (SFXGame.BioConversationController.BioSpeakerData): nmSpeakerTag, pSpeakerActor.
struct SpeakerData {
    std::uint64_t tag = 0;
    ue3::Object* actor = nullptr;
};
static_assert(sizeof(SpeakerData) == 16);
#endif

// The scene's people: pawns, and on ME3 its SFXStuntActors (the hub's conversation characters are stunt actors, not pawns, and
// not on the world's pawn list: Khalisah at the embassies, 2026-11-20; game/Hair's StuntActors). Their skeletal mesh: a pawn's
// Mesh, a stunt actor's BodyMesh.
#if defined(MEVR_GAME_LE3)
ue3::ClassRef gStuntClass{"SFXGame.SFXStuntActor", ue3::kEveryGame};
ue3::FieldRef gStuntBody{"SFXGame.SFXStuntActor", "BodyMesh", ue3::kEveryGame};
#endif
bool IsPerson(ue3::Object const* actor) {
#if defined(MEVR_GAME_LE3)
    if (actor != nullptr && ue3::IsA(actor, gStuntClass)) return true;
#endif
    return player::IsBioPawn(actor);
}
ue3::Object* PersonMesh(ue3::Object* actor) {
    if (actor == nullptr || !ue3::runtime::IsAlive(actor)) return nullptr;
#if defined(MEVR_GAME_LE3)
    if (ue3::IsA(actor, gStuntClass)) return gStuntBody.Read<ue3::Object*>(actor).value_or(nullptr);
#endif
    return player::IsBioPawn(actor) ? player::Mesh(actor) : nullptr;
}

struct Settings {
    bool world = false;          // [Cinematics] conversations = world (ME1's key and word)
    bool stay = true;            // [Cinematics] worldStay: people kept where they stand
    double settleS = 0.35;       // the opening's placements settle this long before the player's place is taken
    double followUnits = 80.0;   // a lasting jump of Shepard's head past this moves the place (ME1's FollowHeadSettled)
    // Shepard kept on his mark like the others (ME1's rule: the player's place is never moved by the game; the owner,
    // 2026-11-19: "have Shepard not move around during this scene like the Mass Effect 1 rule set"); then nothing to follow.
    bool shepardStays = true;
    // ME1's worldBlackMoves (research/69 item 41; the owner, 2026-11-16, on all three games): the game's fade to black moves the
    // player with Shepard to where the scene wants him, curated walks excepted (worldCuratedWalks, cw::IsCuratedWalk).
    bool blackMoves = true;
    std::string curated;
    bool dofOff = true;          // [Cinematics] worldDof = 0 keeps it: the conversation's depth of field blurs the room
    bool nearPlane = true;       // [Cinematics] worldNearPlane (ME1's key): the on-foot near plane kept through the director's shots
    // ME1's walking during a conversation (research/59, worldMove): the left stick moves Shepard and the view inside the
    // boundary around where the conversation began, never into people (worldMoveLeashM 4, worldMoveSpeedMS 1.4,
    // worldMoveClearM 0.7).
    bool move = true;
    double leashUnits = 400.0;
    double speedUnitsPerS = 140.0;
    double clearUnits = 70.0;
    // ME1's worldLookEyes (research/69 "Heads that look past the player"): while Shepard's head stands away from the drawn eyes, a
    // look-at target at the eyes takes the game's looks at him (EyesStep below).
    bool lookEyes = true;
    // ME1's worldStances (research/69 "Stances from above"): the scene from above written by itself (StanceStep below).
    bool stances = false;
};

// Where Shepard stood on foot, the last game tick before a conversation (ME1's opening: he is put back there).
struct Foot {
    bool valid = false;
    ue3::Vector pawn{};
    std::uint64_t ms = 0;
};
Foot gFoot;
// The engine's depth of field is switched through the console (`scale set`), never around a load: a switch as the docking
// officer's conversation handed over to the Citadel's load (2026-11-19, 20:56) left that load spinning with no scene drawn,
// as ME2's scale set during a load hung its rendering (gotchas/me2-scale-set-during-load). Off only after 3 s on foot before
// the conversation; back only after 3 s on foot again.
std::uint64_t gOnFootSinceMs = 0;  // 0: not on foot now
std::uint64_t gLastFootRunMs = 0;  // how long the last stretch on foot lasted (kept when it ends)
bool gFootSettledAtStart = false;
// Only the game's cinematic and conversation modes since the last stretch on foot (no load, no movie between): a conversation
// after a level's cutscene is not one opened around a load (Tali's reintroduction on Freedom's Progress, 2026-11-20: its
// conversation follows a 13 s cutscene and was left on the 3D screen as if a load had opened it).
bool gOnlyScenesSinceFoot = false;
bool gDofBackOwed = false;

// The session's override from the console: -1 the file's, 0 off, 1 on.
int gOverride = -1;
Settings gSettings;
std::uint64_t gSettingsMs = 0;

struct Kept {
    ue3::Object* actor = nullptr;
    ue3::Vector at{};
    cw::Insistence insist;   // the game repeating a placement it just made is its own (ME1's cw::Insists)
    unsigned undone = 0;
    unsigned yielded = 0;
    unsigned refusals = 0;   // SetLocation said no: after three, left to the game (logged once)
    unsigned atSource = 0;   // stage placements withheld at the source (engine/ConversationHooks)
    bool loggedSource = false;
};

// The actors a matinee's move track moved lately (the move track's hook writes, the camera update reads; both on the game
// thread): an authored walk is the person's own, never a placement to undo (ME1's trackWalks).
struct TrackMove {
    void const* actor = nullptr;
    std::uint64_t ms = 0;
};
TrackMove gTrackMoves[24];
bool TrackMovedLately(void const* actor, std::uint64_t withinMs) {
    std::uint64_t const now = GetTickCount64();
    for (TrackMove const& m : gTrackMoves) {
        if (m.actor == actor && now - m.ms <= withinMs) return true;
    }
    return false;
}

struct Conversation {
    bool on = false;             // in the conversation mode with the world asked for
    // A conversation the game opened around a load (the player not 3 s on foot before it: the docking officer's at the Citadel's
    // arrival) is left on the 3D screen: holding the view at its mark is what the level streaming then saw rendered, and the
    // Citadel's load never finished (2026-11-19, two runs of two). Held: none of the below is published.
    bool skipped = false;
    bool placed = false;         // the player's place taken
    bool early = false;          // held at the view on foot while the opening settles (published, not placed yet)
    std::uint32_t serial = 0;
    ue3::Object* controller = nullptr;
    std::uint64_t startMs = 0;
    std::uint64_t placedMs = 0;
    // ME1's opening (cw::DecideOpening, worldOpening auto): Stay or WalkIn: Shepard put back where the player stood and kept
    // there (his head bone never moves the view after); Cut: another space, the place is his mark.
    cw::Opening opening = cw::Opening::Stay;
    ue3::Vector pin{};           // where Shepard's pawn is kept (the stick moves it)
    ue3::Vector start{};         // the boundary's centre
    // The held place from the pin (world, Unreal units): the view on foot's own (a Stay or WalkIn with the view on foot fresh:
    // the eyes stay where the player had them), else his head bone at the mark plus the first-person eyes from it (EyeAtHead);
    // its height then brought to the player's eye over his floor (StepEyeHeight). research/le2-conversations-in-world.md 7.1.
    ue3::Vector eyeOffset{};
    char const* eyesFrom = "";
    double eyeFixUnits = 0.0;    // how far StepEyeHeight moved the eyes (the end line)
    std::uint64_t eyeMs = 0;     // StepEyeHeight's last tick
    bool eyeFixLogged = false;
    std::uint64_t stepMs = 0;
    double walkedUnits = 0.0;
    unsigned pinned = 0;         // camera updates that put him back on the pin
    ue3::Vector held{};          // the view's place (the POV before the renderer's offsets): pin + eyeOffset, every tick
    ue3::Rotator facing{};
    float fov = 90.0f;
    cw::FollowState follow;
    unsigned follows = 0;
    std::vector<Kept> kept;
    unsigned undoneTotal = 0;
    unsigned yieldedTotal = 0;
    unsigned refusedMoves = 0;   // SetLocation said no
    // At the source (engine/ConversationHooks, ME1's worldStaySource and trackHeld): the stage's placements of Shepard and of
    // the people kept, and a move track's moves of Shepard, withheld before the engine makes them; what is left after the fact
    // (`pinned`, `undoneTotal`) should then be ME2's gesture walks' root motion alone [verify].
    unsigned shepardAtSource = 0;
    unsigned peopleAtSource = 0;
    unsigned trackHeld = 0;      // Shepard's track moves withheld
    unsigned trackWalks = 0;     // jumps of someone a track moves, left to the track
    bool sceneWantsShepard = false;
    unsigned sceneAsks = 0;      // Shepard's placements and track moves withheld (the black-move rule's "a new ask")
    unsigned sceneAsksTaken = 0; // ... the asks the black-move rule has looked at
    unsigned blackMoves = 0;     // the player moved with Shepard under the game's fade to black
    bool curated = false;        // a curated walk: the player walks on to him (cw::IsCuratedWalk)
    ue3::Vector sceneWants{};    // where the stage or a track last wanted Shepard (withheld)
    std::string why;             // why not placed (yet)
    // ME1's sticks (the owner's answer, 2026-11-19): the left stick walks only while the wheel is down and steers it when it is
    // up (latched after a pick until let go: cw::StickWalks); the right stick turns ([Comfort] turn, about the drawn eyes).
    cw::StickLatch latch;
    vr::comfort::SnapTurn snap;
    double turnedDeg = 0.0;
    unsigned wheelHeld = 0;      // ticks the wheel kept the left stick
    // The floor marker's black-move source (cw::StepMarker, MarkerNeed::BlackMove; the owner's yes of 2026-11-19, need (c)):
    // where the game's fade to black wanted Shepard and the player was not moved (a curated walk, or a step within "where he
    // stands", which ME1 walks by the stick and the sequels leave to the player), on the floor; until the player stands in it.
    std::optional<ue3::Vector> blackMarker;
    cw::MarkerState marker;
    unsigned markers = 0;
    std::uint64_t markerMs = 0;
};
Conversation gC;
bool gMomentHeld = false;  // a moment holds the view (the camera update's letterbox and near plane; set by TickMoment)
// The player's view on foot, the last on-foot tick (the held view while a new conversation's opening settles: never a frame of
// the director's first shot nor of the 3D screen; ME1's armed opening), and when a conversation shown in the world ended (ME1's
// handover: a GUI screen straight after it keeps the world's presentation, research/69 item 38).
struct FootPov {
    bool valid = false;
    ue3::Vector location{};
    ue3::Rotator rotation{};
    float fov = 90.0f;
};
FootPov gFootPov;
std::uint64_t gEndedShownMs = 0;
depth_of_field::Hold gDof{"conversation world"};
std::uint32_t gSerial = 0;
unsigned gNoPlaceScreens = 0;  // conversations the presence table sent to the 3D screen this session
// The host's dip from black (cw::State::cut, plugin/TransitionFade through MevrSeams::TransitionFadeLayer): counts the player's
// re-placements inside a scene held in the world (a Cut opening, a moment's end that joins Shepard past where he stands).
std::uint32_t gCutSerial = 0;
bool gStarted = false;
std::string gLast = "none yet";
// The player's pawn this tick: the providers below run inside the engine's own code (no script, no lookups).
ue3::Object* gShepardPawn = nullptr;
void const* gShepardTrack = nullptr;          // the move track walking Shepard in a moment, and its instance
void const* gShepardTrackInstance = nullptr;
// What Shepard stands on (RideBase, below): the base read last, where it was, how far it carried the player this scene.
ue3::FieldRef gActorBase{"Engine.Actor", "Base", ue3::kEveryGame};
// The game's fade to black (ME1's UnderGameBlack, its matinee-fade half: research/69 items 38 and 41): the camera's fade, read
// after each camera update of a held conversation. The script's GUI black screen (ME1's BioSeqAct_BlackScreen, ME3's
// BioSFHandler_BlackScreen) is not read on the sequels yet (research/le2-conversations-in-world.md 7.6).
ue3::FieldRef gCameraEnableFading{"Engine.Camera", "bEnableFading", ue3::kEveryGame};
ue3::FieldRef gCameraFadeAmount{"Engine.Camera", "FadeAmount", ue3::kEveryGame};
std::uint64_t gBlackMs = 0;  // the last camera update with the fade half black or more
struct Ride {
    ue3::Object* base = nullptr;
    ue3::Vector at{};
    std::uint64_t ms = 0;
    double units = 0.0;
    bool logged = false;
};
Ride gRide;
ue3::Vector gRideMomentDelta{};  // the ride since TickMoment last took it

// engine/ConversationHooks' placement question (ME3's controller's SetPositionRotator; ME2's function is not pinned yet): after
// the opening, the stage never moves Shepard (the player's place is the player's) nor anyone kept where they stand; the game's
// own facing-only path still turns them (ME1's KeepAtSource, worldStaySource). The opening's own placements stand (not placed
// yet). The conversation is null for ME3's per-key track placements: the actor decides.
bool KeepAtSource(void const* conversation, void const* actor, float const* location) {
    if (!gC.on || !gC.placed || gC.skipped || actor == nullptr || location == nullptr) return false;
    if (conversation != nullptr && conversation != gC.controller) return false;
    if (actor == gShepardPawn) {
        ++gC.shepardAtSource;
        gC.sceneWantsShepard = true;
        gC.sceneWants = {location[0], location[1], location[2]};
        ++gC.sceneAsks;
        return true;
    }
    if (!gSettings.stay) return false;
    for (Kept& k : gC.kept) {
        if (k.actor == actor) {
            ++k.atSource;
            ++gC.peopleAtSource;
            return true;
        }
    }
    return false;
}

// engine/ConversationHooks' move-track question: every actor a track moves is noted (its authored walk is its own), and in a
// conversation held in the world Shepard's track moves are withheld (ME1's trackHeld: his place is the player's); where the
// track wanted him is kept for the log.
bool OnMoveTrack(void const* actor, void const* track, void const* instance, float time) {
    if (actor == nullptr) return false;
    std::uint64_t const now = GetTickCount64();
    TrackMove* slot = &gTrackMoves[0];
    for (TrackMove& m : gTrackMoves) {
        if (m.actor == actor) {
            slot = &m;
            break;
        }
        if (m.ms < slot->ms) slot = &m;
    }
    slot->actor = actor;
    slot->ms = now;
    if (actor == gShepardPawn && gMomentHeld) {
        gShepardTrack = track;  // a moment's walk: where it ends is the floor marker's (TickMoment)
        gShepardTrackInstance = instance;
    }
    if (actor != gShepardPawn || !gC.on || !gC.placed || gC.skipped) return false;
    ++gC.trackHeld;
    float place[3];
    if (engine::conversation_hooks::MoveTrackPlace(track, instance, time, place)) {
        gC.sceneWantsShepard = true;
        gC.sceneWants = {place[0], place[1], place[2]};
        ++gC.sceneAsks;
    }
    return true;
}

void RefreshSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gSettingsMs != 0 && now - gSettingsMs < 2000) return;
    gSettingsMs = now;
    std::wstring const path = engine::SettingsFile().wstring();
    wchar_t text[32] = {};
    vr::settings_file::String(L"Cinematics", L"conversations", L"", text, static_cast<DWORD>(std::size(text)), path.c_str());
    std::string narrow;
    for (wchar_t const* p = text; *p != 0; ++p) narrow.push_back(static_cast<char>(*p & 0x7F));
    gSettings.world = cw::ParseMode(narrow.c_str(), cw::Mode::Screen) == cw::Mode::World;
    gSettings.stay = vr::settings_file::Int(L"Cinematics", L"worldStay", 1, path.c_str()) != 0;
    gSettings.shepardStays = vr::settings_file::Int(L"Cinematics", L"worldShepardStays", 1, path.c_str()) != 0;
    gSettings.dofOff = vr::settings_file::Int(L"Cinematics", L"worldDof", 0, path.c_str()) == 0;
    gSettings.nearPlane = vr::settings_file::Int(L"Cinematics", L"worldNearPlane", 1, path.c_str()) != 0;
    gSettings.blackMoves = vr::settings_file::Int(L"Cinematics", L"worldBlackMoves", 1, path.c_str()) != 0;
    gSettings.lookEyes = vr::settings_file::Int(L"Cinematics", L"worldLookEyes", 1, path.c_str()) != 0;
    gSettings.stances = vr::settings_file::Int(L"Cinematics", L"worldStances", 0, path.c_str()) != 0;
    wchar_t curated[256] = {};
    vr::settings_file::String(L"Cinematics", L"worldCuratedWalks", L"", curated, static_cast<DWORD>(std::size(curated)), path.c_str());
    gSettings.curated.clear();
    for (wchar_t const* p = curated; *p != 0; ++p) gSettings.curated.push_back(static_cast<char>(*p & 0x7F));
}

bool WorldWanted() {
    return gOverride >= 0 ? gOverride == 1 : gSettings.world;
}

#if defined(MEVR_GAME_LE3)
// The live conversation (ME3: the world's conversation manager's full conversation, not ended), as game/Thespian reads it.
ue3::Object* LiveConversation(ue3::Object* controller) {
    auto* const world = gActorWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr);
    auto* const manager = world != nullptr && ue3::runtime::IsAlive(world) ? gManager.Read<ue3::Object*>(world).value_or(nullptr) : nullptr;
    ue3::Function* const function = gGetFull.Get();
    if (manager == nullptr || !ue3::runtime::IsAlive(manager) || function == nullptr) return nullptr;
    ue3::Params params(ue3::runtime::Get(), function);
    if (!params.Valid() || !ue3::runtime::Call(manager, function, params)) return nullptr;
    auto* const conversation = params.Return<ue3::Object*>().value_or(nullptr);
    return conversation != nullptr && ue3::runtime::IsAlive(conversation) && ue3::IsA(conversation, gControllerClass) &&
            !gEnded.ReadBool(conversation).value_or(true)
        ? conversation
        : nullptr;
}

// The dialogue wheel is up (game/Thespian's reader of the conversation handler, each game's own).
ue3::FieldRef gDisplayingWheel{"SFXGame.BioSFHandler_Conversation", "m_bDisplayingWheel", ue3::kEveryGame};
ue3::Object* WheelHandler(ue3::Object* controller) {
    static ue3::FunctionRef getHandler{"SFXGame.SFXGUIInteraction.GetConversationHandler", ue3::kEveryGame};
    auto* const manager = le3_gui::Manager();
    auto* const function = getHandler.Get();
    if (controller == nullptr || manager == nullptr || function == nullptr) return nullptr;
    ue3::Params params(ue3::runtime::Get(), function);
    if (!params.Valid() || !params.Set("oPlayerController", controller) || !ue3::runtime::Call(manager, function, params)) return nullptr;
    auto* const handler = params.Return<ue3::Object*>().value_or(nullptr);
    return handler != nullptr && ue3::runtime::IsAlive(handler) ? handler : nullptr;
}
bool WheelUp(ue3::Object* controller) {
    ue3::Object* const handler = WheelHandler(controller);
    return handler != nullptr && gDisplayingWheel.ReadBool(handler).value_or(false);
}

// One element's actor, guarded: the array's storage is the engine's.
bool ReadSpeaker(SpeakerData const* data, int i, ue3::Object** out) noexcept {
    __try {
        *out = data[i].actor;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The speakers' actors (the player's left out).
std::vector<ue3::Object*> Speakers(ue3::Object* conversation, ue3::Object* player) {
    std::vector<ue3::Object*> out;
    auto const list = gSpeakers.Read<ue3::TArray<SpeakerData>>(conversation);
    int const count = list && list->data != nullptr && list->count > 0 && list->count <= 32 ? list->count : 0;
    for (int i = 0; i < count; ++i) {
        ue3::Object* actor = nullptr;
        if (!ReadSpeaker(list->data, i, &actor)) break;
        if (actor != nullptr && actor != player && ue3::runtime::IsAlive(actor) &&
            std::find(out.begin(), out.end(), actor) == out.end()) {
            out.push_back(actor);
        }
    }
    // The owner and who speaks or listens now, as ME2's reader (ME1's): the speaker data may not hold them
    // (research/le3-conversations-in-world.md section 1).
    for (ue3::FieldRef* field : {&gOwner, &gSpeakerNow, &gListenerNow}) {
        ue3::Object* const actor = field->Read<ue3::Object*>(conversation).value_or(nullptr);
        if (actor != nullptr && actor != player && ue3::runtime::IsAlive(actor) && IsPerson(actor) &&
            std::find(out.begin(), out.end(), actor) == out.end()) {
            out.push_back(actor);
        }
    }
    return out;
}

// Shepard as the conversation has him (its player).
ue3::Object* ConversationPlayer(ue3::Object* conversation) {
    return gPlayer.Read<ue3::Object*>(conversation).value_or(nullptr);
}

// The conversation's data (its BioConversation: the name the presence table knows).
ue3::FieldRef gConvData{kController, "m_pConvData", ue3::kEveryGame};
ue3::Object* ConversationData(ue3::Object* controller) {
    ue3::Object* const data = gConvData.Read<ue3::Object*>(controller).value_or(nullptr);
    return data != nullptr && ue3::runtime::IsAlive(data) ? data : controller;
}
constexpr int kGameNumber = 3;
#else
// ME2: the world's current conversation (ME1's ConversationNow), in the conversation mode only (the caller asks the mode).
ue3::Object* LiveConversation(ue3::Object* controller) {
    auto* const world = gActorWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr);
    auto* const conversation = world != nullptr && ue3::runtime::IsAlive(world) ? gCurrent.Read<ue3::Object*>(world).value_or(nullptr)
                                                                               : nullptr;
    return conversation != nullptr && ue3::runtime::IsAlive(conversation) && ue3::IsA(conversation, gControllerClass)
        ? conversation
        : nullptr;
}

ue3::Object* ConversationPlayer(ue3::Object* conversation) {
    return gPlayer.Read<ue3::Object*>(conversation).value_or(nullptr);
}

// ME2's conversation is its data too.
ue3::Object* ConversationData(ue3::Object* conversation) {
    return conversation;
}
constexpr int kGameNumber = 2;

// The dialogue wheel is up (game/Thespian's ME2 reader: the GUI manager's conversation handler).
ue3::FieldRef gDisplayingWheel{"SFXGame.BioSFHandler_Conversation", "m_bDisplayingWheel", ue3::kEveryGame};
ue3::Object* WheelHandler(ue3::Object* controller) {
    static ue3::FieldRef managerField{"Engine.PlayerController", "m_oCachedScaleFormManager", ue3::kEveryGame};
    static ue3::FunctionRef getHandler{"SFXGame.MassEffectGuiManager.GetConversationHandler", ue3::kEveryGame};
    auto* const manager = controller != nullptr ? managerField.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    auto* const function = getHandler.Get();
    if (manager == nullptr || !ue3::runtime::IsAlive(manager) || function == nullptr) return nullptr;
    ue3::Params params(ue3::runtime::Get(), function);
    if (!ue3::runtime::Call(manager, function, params)) return nullptr;
    auto* const handler = params.Return<ue3::Object*>().value_or(nullptr);
    return handler != nullptr && ue3::runtime::IsAlive(handler) ? handler : nullptr;
}
bool WheelUp(ue3::Object* controller) {
    ue3::Object* const handler = WheelHandler(controller);
    return handler != nullptr && gDisplayingWheel.ReadBool(handler).value_or(false);
}

// The people of the conversation (ME1's m_SpeakerList, its owner, the speaker and listener now), the player's left out.
std::vector<ue3::Object*> Speakers(ue3::Object* conversation, ue3::Object* player) {
    std::vector<ue3::Object*> out;
    auto const add = [&](ue3::Object* actor) {
        if (actor != nullptr && actor != player && ue3::runtime::IsAlive(actor) && std::find(out.begin(), out.end(), actor) == out.end()) {
            out.push_back(actor);
        }
    };
    if (auto const list = gSpeakerList.ElementsOf(conversation, 32)) {
        for (std::int32_t i = 0; i < list->count; ++i) {
            add(gSpeakerActor.Read<ue3::Object*>(list->At(i)).value_or(nullptr));
        }
    }
    add(gOwner.Read<ue3::Object*>(conversation).value_or(nullptr));
    add(gSpeakerNow.Read<ue3::Object*>(conversation).value_or(nullptr));
    add(gListenerNow.Read<ue3::Object*>(conversation).value_or(nullptr));
    return out;
}
#endif

// Shepard's head bone in the world (his pawn's mesh), or nullopt.
std::optional<engine::UnrealVector> HeadOf(ue3::Object* pawn) {
    ue3::Object* const mesh = pawn != nullptr ? player::Mesh(pawn) : nullptr;
    if (mesh == nullptr) return std::nullopt;
    static ue3::Object const* boneMesh = nullptr;
    static int bone = -1;
    ue3::Object const* const asset = player::SkeletalMesh(mesh);
    if (asset != boneMesh) {
        boneMesh = asset;
        bone = -1;
        if (auto const skeleton = engine::skeleton::ReadRefSkeleton(mesh)) {
            for (std::size_t i = 0; i < skeleton->names.size(); ++i) {
                if (skeleton->names[i] == "Head") {
                    bone = static_cast<int>(i);
                    break;
                }
            }
        }
    }
    return bone >= 0 ? engine::skeleton::BoneLocation(mesh, bone, engine::skeleton::Space::World) : std::nullopt;
}

bool LineOfSight(ue3::Object* from, ue3::Vector const& start, ue3::Vector const& end) {
    ue3::Function* const trace = gFastTrace.Get();
    if (from == nullptr || trace == nullptr) return true;
    ue3::Params p(ue3::runtime::Get(), trace);
    if (!p.Valid() || !p.Set("TraceEnd", end) || !p.Set("TraceStart", start)) return true;
    ue3::runtime::Call(from, trace, p);
    return p.ReturnBool().value_or(true);
}

bool InSight(ue3::Object* from, ue3::Vector const& eyes, ue3::Vector const& target) {
    ue3::Vector const above{target.x, target.y, target.z + 80.0f};
    ue3::Vector const high{eyes.x, eyes.y, eyes.z + 60.0f};
    return LineOfSight(from, eyes, target) || LineOfSight(from, eyes, above) || LineOfSight(from, high, above);
}

double Distance(ue3::Vector const& a, ue3::Vector const& b) {
    double const dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Each game's look-at chain (whom a person looks at, the game's call to change it).
#include "game/SequelLook.inc"

// Where the player's eyes are drawn this tick (the held frame's head), in a held scene.
std::optional<ik::Vec3> gDrawnEyes;

ik::Vec3 V(ue3::Vector const& v) {
    return {v.x, v.y, v.z};
}

// ---- What is drawn and where heads point (ME1's records, research/69 "Obvious bugs, measured" and "Stances from above") -----
ue3::FieldRef gCompLastRender{"Engine.PrimitiveComponent", "LastRenderTime", ue3::kEveryGame};
ue3::FieldRef gWorldSeconds{"Engine.WorldInfo", "TimeSeconds", ue3::kEveryGame};
ue3::FieldRef gWorldPawns{"Engine.WorldInfo", "PawnList", ue3::kEveryGame};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn", ue3::kEveryGame};
ue3::FieldRef gActorHiddenRead{"Engine.Actor", "bHidden", ue3::kEveryGame};
ue3::FunctionRef gPrimitiveSetHidden{"Engine.PrimitiveComponent.SetHidden", ue3::kEveryGame};

// A pawn's head bone: its world place and axes (ME1's HeadOf; cached per skeletal mesh asset).
struct HeadPose {
    ik::Vec3 at;
    ik::Vec3 axis[3];
};
std::map<ue3::Object const*, int> gHeadBones;

std::optional<HeadPose> HeadPoseOf(ue3::Object* actor) {
    if (actor == nullptr || !ue3::runtime::IsAlive(actor) || !IsPerson(actor)) return std::nullopt;
    ue3::Object* const mesh = PersonMesh(actor);
    ue3::Object const* const asset = mesh != nullptr ? player::SkeletalMesh(mesh) : nullptr;
    if (asset == nullptr) return std::nullopt;
    auto it = gHeadBones.find(asset);
    if (it == gHeadBones.end()) {
        int index = -1;
        if (auto const skeleton = engine::skeleton::ReadRefSkeleton(mesh)) {
            for (std::size_t i = 0; i < skeleton->names.size() && index < 0; ++i) {
                if (ini::Lower(skeleton->names[i]) == "head") index = static_cast<int>(i);
            }
        }
        it = gHeadBones.emplace(asset, index).first;
    }
    int const bone = it->second;
    auto const bases = engine::skeleton::SpaceBases(mesh);
    if (bone < 0 || static_cast<std::size_t>(bone) >= bases.size()) return std::nullopt;
    ik::Affine const toWorld = engine::skeleton::LocalToWorld(mesh);
    ik::Transform const t = engine::skeleton::ToTransform(bases[static_cast<std::size_t>(bone)]);
    HeadPose h;
    h.at = ik::TransformPoint(toWorld, t.translation);
    ik::Vec3 const units[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int a = 0; a < 3; ++a) {
        h.axis[a] = ik::Normalize(ik::TransformDirection(toWorld, ik::Rotate(t.rotation, units[a])), units[a]);
    }
    return h;
}

// Shepard's head where the scene animates him (arm IK's animated head, before the body follower slides his drawn body under the
// player: ME1's MarkHeadOf), else his head bone as drawn.
std::optional<ik::Vec3> ShepardSceneHead(ue3::Object* pawn) {
    if (pawn == nullptr || !ue3::runtime::IsAlive(pawn)) return std::nullopt;
    if (auto const animated = arm_ik::AnimatedHeadComponent()) {
        if (ue3::Object* const mesh = player::Mesh(pawn)) return ik::TransformPoint(engine::skeleton::LocalToWorld(mesh), *animated);
    }
    if (auto const head = HeadPoseOf(pawn)) return head->at;
    return std::nullopt;
}

// The face's axis of a head bone (ME1's is the bone's X). [verify] on ME2's and ME3's skeletons: the records also give each
// axis's angle to where the game's look-at chain aims (`faceAxesToAimDeg`), the measure that names the face.
constexpr int kFaceAxis = 0;

double AngleDeg(ik::Vec3 const& a, ik::Vec3 const& b) {
    double const na = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z), nb = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
    if (na < 1e-6 || nb < 1e-6) return 0.0;
    double const c = std::clamp((a.x * b.x + a.y * b.y + a.z * b.z) / (na * nb), -1.0, 1.0);
    return std::acos(c) * (180.0 / 3.14159265358979323846);
}

// The world's clock (WorldInfo.TimeSeconds: what LastRenderTime is written in).
std::optional<double> WorldNowS(ue3::Object* controller) {
    ue3::Object* const world = controller != nullptr ? gActorWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    auto const t = world != nullptr && ue3::runtime::IsAlive(world) ? gWorldSeconds.Read<float>(world) : std::nullopt;
    return t ? std::optional<double>(*t) : std::nullopt;
}

// Seconds since a view last drew a mesh (PrimitiveComponent.LastRenderTime: the frustum, occlusion and hide flags all leave it
// behind).
std::optional<double> DrawnAgoS(ue3::Object* controller, ue3::Object* mesh) {
    if (mesh == nullptr || !ue3::runtime::IsAlive(mesh)) return std::nullopt;
    auto const now = WorldNowS(controller);
    auto const last = gCompLastRender.Read<float>(mesh);
    if (!now || !last) return std::nullopt;
    return *now - *last;
}

// Whether a world point stands inside the head's view (35 degrees each way, ME1's InHeadView); nullopt without a view frame.
std::optional<bool> InHeadView(ik::Vec3 const& p) {
    auto const frame = view_anchor::Current();
    if (!frame) return std::nullopt;
    XrVector3f const t = engine::ToTracking(*frame, engine::UnrealVector{p.x, p.y, p.z});
    XrPosef const& head = frame->head;
    ik::Quat const toHead = ik::Conjugate({head.orientation.x, head.orientation.y, head.orientation.z, head.orientation.w});
    ik::Vec3 const v = ik::Rotate(toHead, {t.x - head.position.x, t.y - head.position.y, t.z - head.position.z});
    return v.z < -0.05 && std::abs(v.x) <= -0.70 * v.z && std::abs(v.y) <= -0.70 * v.z;
}

// The world's pawns, stopping at the first dead entry (a level torn down under a scene leaves freed pawns in the list: ME1's
// LivePawns).
std::vector<ue3::Object*> LivePawns(ue3::Object* controller) {
    std::vector<ue3::Object*> out;
    ue3::Object* const world = controller != nullptr ? gActorWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    if (world == nullptr || !ue3::runtime::IsAlive(world)) return out;
    int guard = 0;
    for (ue3::Object* p = gWorldPawns.Read<ue3::Object*>(world).value_or(nullptr); p != nullptr && guard < 512;
         p = gNextPawn.Read<ue3::Object*>(p).value_or(nullptr), ++guard) {
        if (!ue3::runtime::IsAlive(p)) break;
        out.push_back(p);
    }
    return out;
}

// ME3's stunt actors in the player's world, found by a scan of the objects at most once a second (game/Hair's way).
std::vector<ue3::Object*> StuntActors(ue3::Object* controller) {
#if defined(MEVR_GAME_LE3)
    static std::vector<ue3::Object*> found;
    static std::uint64_t nextMs = 0;
    static ue3::Object const* scannedWorld = nullptr;
    ue3::Object* const world = controller != nullptr ? gActorWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    std::uint64_t const now = GetTickCount64();
    if (world != scannedWorld || now >= nextMs) {
        scannedWorld = world;
        nextMs = now + 1000;
        found.clear();
        ue3::Class* const cls = gStuntClass.Get();
        ue3::Reflection& r = ue3::runtime::Get();
        for (std::int32_t i = 0, n = cls != nullptr && world != nullptr ? r.ObjectCount() : 0; i < n; ++i) {
            ue3::Object* const o = r.ObjectAt(i);
            if (o != nullptr && r.IsAlive(o) && r.IsA(o, cls) && gActorWorldInfo.Read<ue3::Object*>(o).value_or(nullptr) == world) {
                found.push_back(o);
            }
        }
    }
    std::vector<ue3::Object*> out;
    for (ue3::Object* const o : found) {
        if (ue3::runtime::IsAlive(o)) out.push_back(o);
    }
    return out;
#else
    (void)controller;
    return {};
#endif
}

// The world's people: its pawns, and ME3's stunt actors.
std::vector<ue3::Object*> LivePeople(ue3::Object* controller) {
    std::vector<ue3::Object*> out = LivePawns(controller);
    for (ue3::Object* const o : StuntActors(controller)) out.push_back(o);
    return out;
}

// The people shown within `units` of a point (a moment's people: everyone near), nearest first, at most `most`.
std::vector<ue3::Object*> NearPawns(ue3::Object* controller, ik::Vec3 const& at, double units, std::size_t most) {
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    std::vector<std::pair<double, ue3::Object*>> found;
    for (ue3::Object* const o : LivePeople(controller)) {
        if (o == pawn || !IsPerson(o) || gActorHiddenRead.ReadBool(o).value_or(true)) continue;
        auto const p = player::Location(o);
        if (!p) continue;
        double const d = std::hypot(p->x - at.x, p->y - at.y);
        if (d <= units) found.push_back({d, o});
    }
    std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    std::vector<ue3::Object*> out;
    for (auto const& [d, o] : found) {
        if (out.size() >= most) break;
        out.push_back(o);
    }
    return out;
}

// ---- The eyes stand-in (ME1's StepEyes, worldLookEyes; core cw::EyesStandIn): while Shepard's head as the scene animates it
// stands half a metre (flat) from where the player's eyes are drawn, a look-at target of the game's own kind
// (SFXGame.BioLookAtTarget, made once, kept at the drawn eyes) takes his place: each look the game aims at his pawn is moved to
// it at its own rank (ME2: BioPawn.SetLookAtTarget; ME3: SFXModule_LookAt.ChangeTarget), and given back when his head is with
// the eyes again or the scene ends. Pinned under the player (every opening on the sequels) his head stays within ME1's 0.5 m:
// it acts in moments (the script walks him) and when the player's own steps or lean take the eyes away from his body.
ue3::ClassRef gEyesClass{"SFXGame.BioLookAtTarget", ue3::kEveryGame};
ue3::FunctionRef gActorSpawn{"Engine.Actor.Spawn", ue3::kEveryGame};
ue3::FieldRef gActorStatic{"Engine.Actor", "bStatic", ue3::kEveryGame};
ue3::FieldRef gActorNoDelete{"Engine.Actor", "bNoDelete", ue3::kEveryGame};
struct EyesState {
    ue3::Kept target;            // the BioLookAtTarget (alive across scenes; made again after a level change)
    bool failed = false;         // could not be made (logged once)
    bool on = false;             // this tick: his head is away and the eyes stand in for him
    unsigned swapped = 0;        // looks moved to the eyes (this scene)
    unsigned refused = 0;        // ... the game kept its own
    unsigned given = 0;          // ... given back to him
    double gapUnits = -1.0;      // his head from the drawn eyes, flat, last step
    std::vector<void const*> logged;
};
EyesState gEyesState;
std::optional<bool> gEyesOverride;  // `mevr convo eyes on|off` (an A/B); unset: [Cinematics] worldLookEyes

ue3::Object* EnsureEyes(ue3::Object* controller, ik::Vec3 const& at) {
    if (ue3::Object* const live = gEyesState.target.Live()) return live;
    if (gEyesState.failed) return nullptr;
    ue3::Class* const cls = gEyesClass.Get();
    ue3::Function* const spawn = gActorSpawn.Get();
    if (controller == nullptr || cls == nullptr || spawn == nullptr) {
        gEyesState.failed = true;
        log::Warn("conversation world: eyes: no BioLookAtTarget class or Actor.Spawn: heads keep looking at Shepard's body");
        return nullptr;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* defaults = nullptr;
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n && defaults == nullptr; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && r.ClassOf(o) == cls && (r.FlagsOf(o) & 0x200) != 0) defaults = o;  // RF_ClassDefaultObject
    }
    bool const wasStatic = defaults != nullptr && gActorStatic.ReadBool(defaults).value_or(false);
    bool const wasNoDelete = defaults != nullptr && gActorNoDelete.ReadBool(defaults).value_or(false);
    if (wasStatic) gActorStatic.WriteBool(defaults, false);      // journalled by FieldRef; put back right after the spawn
    if (wasNoDelete) gActorNoDelete.WriteBool(defaults, false);
    ue3::Params p(r, spawn);
    p.Set("SpawnClass", cls);
    p.Set("SpawnOwner", controller);
    p.Set("SpawnLocation", ue3::Vector{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)});
    p.SetBool("bNoCollisionFail", true);
    ue3::runtime::Call(controller, spawn, p);
    if (wasStatic) gActorStatic.WriteBool(defaults, true);
    if (wasNoDelete) gActorNoDelete.WriteBool(defaults, true);
    ue3::Object* const made = p.Return<ue3::Object*>().value_or(nullptr);
    if (made == nullptr || !ue3::IsA(made, gEyesClass)) {
        gEyesState.failed = true;
        log::Warn("conversation world: eyes: Actor.Spawn made no BioLookAtTarget (defaults{}{}): heads keep looking at Shepard's body",
            wasStatic ? " bStatic" : "", wasNoDelete ? " bNoDelete" : "");
        return nullptr;
    }
    gEyesState.target = ue3::Kept(made);
    log::Info("conversation world: eyes: a look-at target made for the player's eyes ({})", ue3::runtime::FullName(made));
    return made;
}

bool PutEyesAt(ue3::Object* eyes, ik::Vec3 const& at) {
    ue3::Function* const setLocation = gSetLocation.Get();
    if (eyes == nullptr || setLocation == nullptr) return false;
    ue3::Params p(ue3::runtime::Get(), setLocation);
    return p.Valid() && p.Set("NewLocation", ue3::Vector{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)}) &&
        ue3::runtime::Call(eyes, setLocation, p);
}

// Every look at the eyes back to Shepard's pawn, at its own rank.
void GiveBackEyes(std::vector<ue3::Object*> const& people, ue3::Object* shepard, char const* why) {
    ue3::Object* const eyes = gEyesState.target.Live();
    if (eyes == nullptr) return;
    unsigned given = 0;
    for (ue3::Object* const actor : people) {
        LookNow const look = LookOf(actor);
        if (!look.known || look.target != eyes) continue;
        if (shepard != nullptr && ue3::runtime::IsAlive(shepard) && SetLookRanked(actor, shepard, look.rank)) ++given;
    }
    gEyesState.given += given;
    if (given != 0) {
        log::Info("conversation world: eyes: {} look{} at the player's eyes given back to Shepard ({})", given, given == 1 ? "" : "s", why);
    }
}

// One step (each tick of a held conversation or moment, after the drawn eyes are known): `people` are whose looks it may move.
void EyesStep(ue3::Object* controller, std::vector<ue3::Object*> const& people, std::optional<ik::Vec3> const& drawnEyes,
    char const* scene, std::uint32_t serial) {
    ue3::Object* const shepard = controller != nullptr ? player::Pawn(controller) : nullptr;
    bool const wanted = gEyesOverride.value_or(gSettings.lookEyes);
    std::optional<ik::Vec3> const head = ShepardSceneHead(shepard);
    gEyesState.gapUnits = head && drawnEyes ? std::hypot(head->x - drawnEyes->x, head->y - drawnEyes->y) : -1.0;
    bool const away = wanted && head && drawnEyes && cw::EyesStandIn(gEyesState.on, gEyesState.gapUnits);
    if (!away) {
        if (gEyesState.on) {
            gEyesState.on = false;
            GiveBackEyes(people, shepard, wanted ? "Shepard's head is with the player's eyes again" : "the stand-in is off");
        }
        return;
    }
    ue3::Object* const eyes = EnsureEyes(controller, *drawnEyes);
    if (eyes == nullptr) {
        gEyesState.on = false;
        return;
    }
    PutEyesAt(eyes, *drawnEyes);  // where the player's eyes are drawn (the head's tracking in it), not the game's camera
    if (!gEyesState.on) {
        gEyesState.on = true;
        log::Info("conversation world: {} {}: eyes: Shepard's head stands {:.2f} m from the player's drawn eyes: heads that look at him "
                  "look at the player's eyes", scene, serial, gEyesState.gapUnits / 100.0);
    }
    for (ue3::Object* const actor : people) {
        if (actor == nullptr || actor == shepard || !ue3::runtime::IsAlive(actor)) continue;
        LookNow const look = LookOf(actor);
        if (!look.known || look.target == nullptr || look.target != shepard) continue;
        bool const moved = SetLookRanked(actor, eyes, look.rank) && LookOf(actor).target == eyes;
        moved ? ++gEyesState.swapped : ++gEyesState.refused;
        if (std::find(gEyesState.logged.begin(), gEyesState.logged.end(), actor) == gEyesState.logged.end()) {
            gEyesState.logged.push_back(actor);
            log::Info("conversation world: {} {}: eyes: {}'s look at Shepard (rank {}) {}", scene, serial, ue3::runtime::FullName(actor),
                look.rank, moved ? "moved to the player's eyes" : "could not be moved (the game kept its own)");
        }
    }
}

// The scene's end: every look given back, the counts logged.
void EyesEnd(ue3::Object* controller, std::vector<ue3::Object*> const& people, char const* scene, std::uint32_t serial) {
    if (gEyesState.on) GiveBackEyes(people, controller != nullptr ? player::Pawn(controller) : nullptr, "the scene ended");
    if (gEyesState.swapped + gEyesState.refused != 0) {
        log::Info("conversation world: {} {}: eyes: {} looks moved to the player's eyes, {} kept by the game, {} given back", scene, serial,
            gEyesState.swapped, gEyesState.refused, gEyesState.given);
    }
    gEyesState.on = false;
    gEyesState.swapped = gEyesState.refused = gEyesState.given = 0;
    gEyesState.logged.clear();
}

// The first-person eyes from Shepard's head bone (ME1's eyeFromHead; core cw::NoteEyeFromHead): learned on foot from the game's
// own first-person camera (its POV, before the renderer adds the head's lean from the recenter point) against his animated head
// bone, with the head at the recenter point. The slice's 8 forward and 4 up put the eyes inside Shepard's shoulders (Tali's
// conversation, research/le2-conversations-in-world.md 6.6). Each game's fallback is its on-foot measure on the fake headset
// (arm IK's "first person camera vs Shepard's head bone" with the headset at the recenter point): ME2 24 forward, 13 up
// (2026-11-14, lanes B to F); ME3 34 forward, 5 up (lane FPC4, 2026-11-20, the Citadel's embassies: 33.1..34.3 forward,
// -2.9..0.0 right, 4.9..9.5 up). The head is the animated one (ShepardSceneHead): the drawn head bone on foot is the body
// follower's and body scale's (the fake headset's 1.75 m eye against ME3's Shepard scaled it up: 976 samples read the camera
// 27 units under the drawn head, 2026-11-20).
#if defined(MEVR_GAME_LE3)
constexpr cw::Vec3 kEyeFromHeadFallback{34.0, 0.0, 5.0};
#else
constexpr cw::Vec3 kEyeFromHeadFallback{24.0, 0.0, 13.0};
#endif
cw::EyeFromHead gEyeFromHead;
std::uint64_t gEyeFromHeadLogMs = 0;

ue3::Vector EyeAtHead(engine::UnrealVector const& head, ue3::Rotator const& facing) {
    engine::Basis const basis = engine::ToBasis(engine::UnrealRotation{0, facing.yaw, 0});
    cw::Vec3 const e = cw::EyeFromHeadOr(gEyeFromHead, kEyeFromHeadFallback);
    return {static_cast<float>(head.x + basis.forward.x * e.x + basis.right.x * e.y),
        static_cast<float>(head.y + basis.forward.y * e.x + basis.right.y * e.y), static_cast<float>(head.z + e.z)};
}

// On foot (mode 0), the first-person camera the pawn's: one sample of the eyes from his head bone (the lean must be near zero:
// the body follower slides the body with it).
void LearnEyeFromHead(ue3::Object* pawn, ue3::Vector const& povLocation, ue3::Rotator const& povRotation,
    std::optional<engine::UnrealVector> const& head) {
    if (!head || pawn == nullptr || !player::FloorLockSample().cameraOwned) return;
    vr::host_view::State const view = vr::host_view::Latest();
    if (!view.valid) return;
    double const lean = std::sqrt(static_cast<double>(view.offsetForward) * view.offsetForward +
        static_cast<double>(view.offsetRight) * view.offsetRight + static_cast<double>(view.offsetUp) * view.offsetUp);
    engine::Basis const basis = engine::ToBasis(engine::UnrealRotation{0, povRotation.yaw, 0});
    double const dx = povLocation.x - head->x, dy = povLocation.y - head->y, dz = povLocation.z - head->z;
    cw::Vec3 const sample{dx * basis.forward.x + dy * basis.forward.y, dx * basis.right.x + dy * basis.right.y, dz};
    if (!cw::NoteEyeFromHead(gEyeFromHead, sample, lean)) return;
    std::uint64_t const now = GetTickCount64();
    if (gEyeFromHead.samples == 1 || now - gEyeFromHeadLogMs >= 60000) {
        gEyeFromHeadLogMs = now;
        log::Info("conversation world: on foot the first-person eyes are {:.1f} forward, {:.1f} right, {:.1f} up from Shepard's head "
                  "bone ({} samples at the recenter point; a scene held at his mark puts the eyes there)",
            gEyeFromHead.eye.x, gEyeFromHead.eye.y, gEyeFromHead.eye.z, gEyeFromHead.samples);
    }
}

// ME1's StepEyeHeight (research/69 item 44; core cw::EyeHeightStep): while the conversation is held, the drawn eye centre (the
// host's view.centreZ: the held place plus the head's own lean) is brought to Shepard's cylinder floor plus the player's live
// standing eye (the floor lock's STAGE eye), 1 m/s for the opening's first 1.5 s, 0.3 m/s after. The fake headset reads no
// standing eye: nothing moves there.
ue3::FieldRef gPawnCylinder{"Engine.Pawn", "CylinderComponent", ue3::kEveryGame};
ue3::FieldRef gCylinderHeight{"Engine.CylinderComponent", "CollisionHeight", ue3::kEveryGame};
void StepEyeHeight(ue3::Object* pawn, std::uint64_t now, double dt) {
    if (pawn == nullptr) return;
    vr::host_view::State const view = vr::host_view::Latest();
    if (!view.valid || !view.centreValid) return;
    ue3::Object* const cylinder = gPawnCylinder.Read<ue3::Object*>(pawn).value_or(nullptr);
    auto const height = cylinder != nullptr ? gCylinderHeight.Read<float>(cylinder) : std::nullopt;
    if (!height || !(*height > 0.0f && *height < 300.0f)) return;
    cw::EyeHeightFacts facts;
    facts.floorZ = static_cast<double>(gC.pin.z) - *height;  // Shepard is kept on the pin (his pawn's place)
    facts.eyeM = view.stageEyeM;
    facts.drawnZ = view.centreZ;
    facts.unitsPerMeter = view.unitsPerMeter > 0.0f ? view.unitsPerMeter : 100.0;
    facts.dtS = dt;
    facts.opening = now - gC.placedMs < 1500;
    double const step = cw::EyeHeightStep(facts);
    if (step == 0.0) return;
    gC.eyeOffset.z += static_cast<float>(step);
    gC.eyeFixUnits += std::abs(step);
    double const err = facts.floorZ + facts.eyeM * facts.unitsPerMeter - facts.drawnZ;
    if (!gC.eyeFixLogged && std::abs(err) > 3.0) {
        gC.eyeFixLogged = true;
        log::Info("conversation world: conversation {}: the drawn eyes are {:.2f} m over the floor under Shepard, the player's {:.2f} m: "
                  "the view goes {} by {:.2f} m", gC.serial, (facts.drawnZ - facts.floorZ) / facts.unitsPerMeter, facts.eyeM,
            err > 0 ? "up" : "down", std::abs(err) / facts.unitsPerMeter);
    }
}

// The tracking frame of the held view (view_anchor's FromHost with the held POV in the game camera's place).
std::optional<engine::TrackingFrame> HeldFrame() {
    vr::host_view::State const view = vr::host_view::Latest();
    if (!view.valid) return std::nullopt;
    return engine::FromCameraView(engine::UnrealVector{gC.held.x, gC.held.y, gC.held.z},
        engine::UnrealRotation{gC.facing.pitch, gC.facing.yaw, gC.facing.roll}, {view.lookPitch, view.lookYaw, 0},
        {view.offsetForward, view.offsetRight, view.offsetUp}, view.head, view.unitsPerMeter);
}

// The floor marker for the host (tracking space), when one shows.
struct MarkerOut {
    bool on = false;
    cw::MarkerNeed need = cw::MarkerNeed::None;
    XrVector3f at{};
    float alpha = 0.0f;
};

// The host's state for a view held in the world (a conversation, or a moment's: the same HostServices v33 state).
void PublishHeld(conversation_world::Link const& link, ue3::Object* controller, std::uint32_t serial, std::uint32_t cut,
    ue3::Vector const& held, ue3::Rotator const& facing, float fov, MarkerOut const& marker = {}) {
    if (link.publish == nullptr) return;
    cw::State s{};
    if (marker.on) {
        s.marker = 1;
        s.markerNeed = static_cast<std::uint8_t>(marker.need);
        s.markerAt[0] = marker.at.x;
        s.markerAt[1] = marker.at.y;
        s.markerAt[2] = marker.at.z;
        s.markerAlpha = marker.alpha;
        s.markerRadiusM = 0.30f;
    }
    s.active = 1;
    s.wheelHand = 2;
    s.conversation = serial;
    s.cut = cut;
    ue3::Object* const camera = player::Camera(controller);
    auto const pov = camera != nullptr ? player::CameraPov(camera) : std::nullopt;
    if (pov && pov->location != nullptr && pov->rotation != nullptr && pov->fov != nullptr) {
        s.holdRender = 1;
        s.holdLocation[0] = held.x;
        s.holdLocation[1] = held.y;
        s.holdLocation[2] = held.z;
        s.holdRotation[0] = facing.pitch;
        s.holdRotation[1] = facing.yaw;
        s.holdRotation[2] = facing.roll;
        s.holdFov = fov;
        s.povLocationAddress = reinterpret_cast<std::uint64_t>(pov->location);
        s.povRotationAddress = reinterpret_cast<std::uint64_t>(pov->rotation);
        s.povFovAddress = reinterpret_cast<std::uint64_t>(pov->fov);
    }
    link.publish(&s);
}

void Publish(conversation_world::Link const& link, ue3::Object* controller, MarkerOut const& marker = {}) {
    PublishHeld(link, controller, gC.serial, gCutSerial, gC.held, gC.facing, gC.fov, marker);
}

// On foot with the world mode wanted (and 3 s on foot: never around a load), the host is told a conversation may begin this
// instant (ME1's Arm: active 0, the first pad byte), so the renderer keeps the world for its first frame; the second pad byte is
// the handover after one shown in the world.
void PublishArmed(conversation_world::Link const& link, bool armed, bool handover) {
    static bool s_published = false;
    static std::uint32_t s_cut = 0;  // the cut counter last published on foot: a newer one is a dip (game/ConversationExit)
    bool const dip = gCutSerial != s_cut;
    if (link.publish == nullptr || (!armed && !handover && !dip && !s_published)) return;
    cw::State s{};
    s.active = 0;
    s.cut = gCutSerial;  // the host's FootCut (its dip on foot, as LE1's)
    s.holdPad[0] = armed ? 1 : 0;
    s.holdPad[1] = handover ? 1 : 0;
    link.publish(&s);
    s_cut = gCutSerial;
    s_published = armed || handover || dip;
}

// The right stick turns a held facing about the drawn eyes ([Comfort] turn: smooth or snap): the place under the eyes moves so
// the drawn eyes stay where they are (ME1's turn about the eyes).
void TurnByStick(ue3::Rotator& facing, ue3::Vector& place, vr::comfort::SnapTurn& snapState, double& turnedDeg, float x, double dt,
    std::uint64_t now, char const* what, std::uint32_t serial) {
    vr::host_view::State const view = vr::host_view::Latest();
    if (!view.valid) return;
    vr::comfort::Settings const comfort = hand_calibration::GetComfort();
    bool const snap = comfort.turn == vr::comfort::TurnStyle::Snap;
    double const degrees = snap ? vr::comfort::StepSnap(snapState, x, static_cast<double>(now) / 1000.0, comfort)
                                : vr::comfort::SmoothTurnDegrees(x, dt, comfort);
    if (degrees == 0.0) return;
    engine::Basis const before = engine::ToBasis(engine::UnrealRotation{0, facing.yaw, 0});
    facing.yaw += static_cast<std::int32_t>(degrees * 65536.0 / 360.0);
    engine::Basis const after = engine::ToBasis(engine::UnrealRotation{0, facing.yaw, 0});
    double const f = view.offsetForward, r = view.offsetRight;
    place.x += static_cast<float>((before.forward.x - after.forward.x) * f + (before.right.x - after.right.x) * r);
    place.y += static_cast<float>((before.forward.y - after.forward.y) * f + (before.right.y - after.right.y) * r);
    if (turnedDeg == 0.0) {
        log::Info("conversation world: {} {}: the right stick turns you ({})", what, serial, snap ? "snap" : "smooth");
    }
    turnedDeg += std::abs(degrees);
}

void PublishOff(conversation_world::Link const& link) {
    if (link.publish == nullptr) return;
    cw::State s{};  // active 0: the host's presentation goes back to the game's own at once
    link.publish(&s);
}

// (Below, with the moments' state.)
std::string WriteStance(std::string const& tag, bool moment);
bool StancesOn();
void ShowTestHidden();

void End(conversation_world::Link const& link, char const* why) {
    if (!gC.on) return;
    if (gC.placed && !gC.skipped) {
        if (StancesOn()) log::Info("conversation world: {}", WriteStance("end", false));
        ue3::Object* const controller = player::Controller();
        ue3::Object* const conversation = gC.controller != nullptr && ue3::runtime::IsAlive(gC.controller) ? gC.controller : nullptr;
        EyesEnd(controller, conversation != nullptr ? Speakers(conversation, ConversationPlayer(conversation)) : std::vector<ue3::Object*>{},
            "conversation", gC.serial);
    }
    ShowTestHidden();
    gDrawnEyes.reset();
    gLast = std::format("conversation {}: {} after {:.1f} s; opening {}, walked {:.0f} units, Shepard put back {} times after the "
                        "fact; {}; {} re-placements by lasting jumps; at the source: Shepard's placements {}, people's {}, Shepard's "
                        "track moves {}{}; people kept after the fact: {} undone, {} yielded, {} refused by SetLocation, {} track "
                        "walks left to their tracks; the eyes from {}, their height brought {:.2f} m to the player's",
        gC.serial, why, static_cast<double>(GetTickCount64() - gC.startMs) / 1000.0, cw::Name(gC.opening), gC.walkedUnits, gC.pinned,
        gC.placed ? std::format("held at ({:.0f}, {:.0f}, {:.0f}) yaw {:.1f}", gC.held.x, gC.held.y, gC.held.z,
                        gC.facing.yaw * (360.0 / 65536.0))
                  : "never placed (" + gC.why + ")",
        gC.follows, gC.shepardAtSource, gC.peopleAtSource, gC.trackHeld,
        gC.sceneWantsShepard ? std::format(" (last wanted at ({:.0f}, {:.0f}, {:.0f}), {:.0f} units from the player's place)",
                                   gC.sceneWants.x, gC.sceneWants.y, gC.sceneWants.z, Distance(gC.sceneWants, gC.pin))
                             : std::string(),
        gC.undoneTotal, gC.yieldedTotal, gC.refusedMoves, gC.trackWalks, gC.eyesFrom[0] != 0 ? gC.eyesFrom : "nowhere yet",
        gC.eyeFixUnits / 100.0);
    if (gRide.units > 0.0) {
        gLast += std::format("; the player rode {:.2f} m with what Shepard stood on", gRide.units / 100.0);
    }
    if (gC.blackMoves > 0) gLast += std::format("; moved with Shepard under the game's fade to black {} times", gC.blackMoves);
    if (gC.markers > 0) gLast += std::format("; {} floor marker{}", gC.markers, gC.markers == 1 ? "" : "s");
    gRide.units = 0.0;
    gRide.logged = false;
    log::Info("conversation world: {}", gLast);
    if (gC.placed) view_anchor::Hold(std::nullopt);
    if (gC.placed || gC.early) gEndedShownMs = GetTickCount64();
    if (gC.placed) {
        // Its held place is the place on foot for what follows (a moment straight after it, the next conversation's opening).
        gFoot = {true, gC.pin, GetTickCount64()};
        gFootPov = {true, gC.held, gC.facing, gC.fov};
    }
    if (gDof.IsOff()) gDofBackOwed = true;  // put back after 3 s on foot (Tick)
    PublishOff(link);
    gC = {};
}

// What Shepard stands on carries the player (ME1's RideBase, research/69 item 42; the ME3 audit's row 23: "36 full ME3
// conversations move a shuttle, car or lift"): his pawn's Engine.Actor.Base (a lift's or a shuttle's InterpActor; the world's
// own floor never moves), and how far it moved since the last read moves every place the scene keeps in the world: the pin
// (else the pin put him back where the ride began, off the lift), the walk's boundary, where the scene wanted him, the kept
// places of those standing on the same floor, and a moment's view. Read before the pin each camera update and each tick (both
// read only the move since the last read). Not followed: a turning floor.

void RideBase(ue3::Object* pawn) {
    std::uint64_t const now = GetTickCount64();
    ue3::Object* const base = pawn != nullptr && ue3::runtime::IsAlive(pawn) ? gActorBase.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
    auto const baseAt = base != nullptr && ue3::runtime::IsAlive(base) ? player::Location(base) : std::nullopt;
    bool const same = base != nullptr && base == gRide.base && baseAt && gRide.ms != 0 && now - gRide.ms < 250;
    ue3::Vector const was = gRide.at;
    gRide.base = baseAt ? base : nullptr;
    gRide.at = baseAt ? *baseAt : ue3::Vector{};
    gRide.ms = now;
    if (!same) return;
    float const dx = gRide.at.x - was.x, dy = gRide.at.y - was.y, dz = gRide.at.z - was.z;
    double const len = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy + static_cast<double>(dz) * dz);
    if (len < 0.01 || len > 500.0) return;  // still, or a jump of the floor itself (the scene's own rules)
    gRide.units += len;
    if (!gRide.logged) {
        gRide.logged = true;
        log::Info("conversation world: what Shepard stands on moves ({}, {:.2f} m this tick): the player rides with it",
            ue3::runtime::FullName(base), len / 100.0);
    }
    auto const shift = [&](ue3::Vector& p) {
        p.x += dx;
        p.y += dy;
        p.z += dz;
    };
    if (gC.on && gC.placed) {
        shift(gC.pin);
        shift(gC.start);
        if (gC.sceneWantsShepard) shift(gC.sceneWants);
        for (Kept& k : gC.kept) {
            ue3::Object* const on = k.actor != nullptr && ue3::runtime::IsAlive(k.actor) ? gActorBase.Read<ue3::Object*>(k.actor).value_or(nullptr) : nullptr;
            if (on == base) shift(k.at);
        }
    }
    if (gMomentHeld) shift(gRideMomentDelta);  // a moment's view (TickMoment takes it)
}

// ME1's worldBlackMoves in a held conversation (research/69 items 40 and 41: the elevator the script put Shepard in left without
// the player): a placement or track move of Shepard the source hook withheld (where the scene wants him) more than "where he
// stands" (cw::OpeningLimits stayUnits, 1.5 m) from the pin, under the game's fade to black or within 1.5 s of it, moves the pin
// there (the camera update's pin puts his pawn there) under the host's dip; not in a curated walk. A step within 1.5 m stays the
// pin's (ME1 walks it by the stick, which the sequels lack).
void StepBlackMove(std::uint64_t now) {
    if (gC.sceneAsks == gC.sceneAsksTaken) return;
    gC.sceneAsksTaken = gC.sceneAsks;
    if (!gC.sceneWantsShepard || gBlackMs == 0 || now - gBlackMs > 1500) return;
    double const away = std::hypot(gC.sceneWants.x - gC.pin.x, gC.sceneWants.y - gC.pin.y);
    if (away <= cw::MarkerLimits{}.inUnits) return;  // he is there already
    if (!gSettings.blackMoves || gC.curated || away <= cw::OpeningLimits{}.stayUnits) {
        // Not moved (a curated walk keeps its walk, worldBlackMoves = 0, or a step within where he stands): the floor marker shows
        // where the black wanted him (need (c)), for the player to step to.
        gC.blackMarker = gC.sceneWants;
        log::Info("convo: marker: conversation {}: under the game's fade to black the scene wants Shepard {:.2f} m away and the player "
                  "stays ({}): a marker there", gC.serial, away / 100.0,
            gC.curated ? "a curated walk" : !gSettings.blackMoves ? "worldBlackMoves = 0" : "within where he stands");
        return;
    }
    log::Info("conversation world: conversation {}: under the game's fade to black the scene wants Shepard {:.2f} m away: the player "
              "goes with him under a dip (ME1's worldBlackMoves)", gC.serial, away / 100.0);
    gC.pin = gC.sceneWants;  // the eyes keep their offset over his place; StepEyeHeight brings them to the new floor
    gC.start = gC.pin;
    ++gC.blackMoves;
    ++gCutSerial;
}

// Engine.Camera.UpdateCamera, after the game's (before the frame is drawn): the speakers the stage moved since the last update
// are put back (ME1's keep-in-place, game/ConversationWorld's undo in the same handler).
void OnUpdateCamera(ue3::Object* camera, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    if (!(gC.on && gC.placed) && !gMomentHeld) return;
    ue3::Object* const controller = player::Controller();
    if (camera == nullptr || controller == nullptr || player::CameraOwner(camera) != controller) return;
    RideBase(player::Pawn(controller));  // a lift or a shuttle under Shepard moves the pin first (else the pin pulls him off it)
    // The game's fade to black (a matinee's or the director's InterpTrackFade drives the camera's fade): half black or more.
    if (gCameraEnableFading.ReadBool(camera).value_or(false) && gCameraFadeAmount.Read<float>(camera).value_or(0.0f) >= 0.5f) {
        gBlackMs = GetTickCount64();
    }
    if (gConstrainAspect.ReadBool(camera).value_or(false) && gConstrainAspect.WriteBool(camera, false)) {
        write_journal::Wrote("ConversationWorld.noLetterbox", camera, 4);
        if (gLetterboxClears++ == 0) {
            log::Info("conversation world: the conversation camera's letterbox (Camera.bConstrainAspectRatio) cleared each "
                      "update while the conversation is held: the full frame, in stereo");
        }
    }
    // ME1's worldNearPlane: the director's near plane never cuts the player's view (the body, the floor, the people near).
    {
        ue3::Object* const local = gLocalPlayer.Read<ue3::Object*>(controller).value_or(nullptr);
        float* const plane = local != nullptr ? gNearClip.In<float>(local) : nullptr;
        if (gSettings.nearPlane && plane != nullptr && std::isfinite(*plane) && *plane != gFootNear) {
            if (*plane != gNearReplaced) {
                log::Info("conversation world: the director's near plane is {:.1f} units in this shot: the on-foot plane ({:.1f}) "
                          "kept instead ({} writes so far)", *plane, gFootNear, gNearWrites);
                gNearReplaced = *plane;
            }
            *plane = gFootNear;
            write_journal::Wrote("ConversationWorld.nearClipPlane", plane, sizeof(float));
            ++gNearWrites;
        }
        if (plane != nullptr && std::isfinite(*plane)) gDrawnNear = *plane;  // what this frame draws with (the records')
    }
    if (!gC.on || !gC.placed) return;  // (a moment: the script's Shepard walks, nobody is kept)
    if (!gSettings.stay) return;
    if (gC.controller == nullptr || !ue3::runtime::IsAlive(gC.controller)) return;
    ue3::Function* const setLocation = gSetLocation.Get();
    ue3::Object* const player = gPlayer.Read<ue3::Object*>(gC.controller).value_or(nullptr);
    // Shepard on the pin (where the player stood, moved by the stick only): ME1's "the player is never moved by the game". The
    // stage's placements and a track's moves of him are withheld at the source (KeepAtSource, OnMoveTrack); what is left to put
    // back after the fact is what no hook covers: ME2's gesture walks (root motion) and ME2's stage placements while its
    // placement function is not pinned.
    if (ue3::Object* const pawn = player::Pawn(controller); gSettings.shepardStays && pawn != nullptr && setLocation != nullptr) {
        if (auto const at = player::Location(pawn); at && Distance(*at, gC.pin) > 2.0) {
            ue3::Params move(ue3::runtime::Get(), setLocation);
            move.Set("NewLocation", gC.pin);
            ue3::runtime::Call(pawn, setLocation, move);
            write_journal::Wrote("ConversationWorld.pin", pawn, 0);
            if (move.Return<bool>().value_or(false) && gC.pinned++ == 0) {
                log::Info("conversation world: conversation {}: Shepard moved {:.0f} units by what no source hook withholds ({}): "
                          "put back where the player is", gC.serial, Distance(*at, gC.pin),
                    TrackMovedLately(pawn, 500) ? "a move track" : "a gesture's root motion or a placement");
            }
        }
    }
    std::vector<ue3::Object*> actors = Speakers(gC.controller, player);
    for (ue3::Object* const actor : actors) {
        auto const now = player::Location(actor);
        if (!now) continue;
        auto it = std::find_if(gC.kept.begin(), gC.kept.end(), [&](Kept const& k) { return k.actor == actor; });
        if (it == gC.kept.end()) {
            gC.kept.push_back({actor, *now});
            continue;
        }
        if (it->atSource > 0 && !it->loggedSource) {
            it->loggedSource = true;
            log::Info("conversation world: conversation {}: the stage's placement of {} withheld at the source (it keeps its place, "
                      "turned as the scene wants)", gC.serial, ue3::runtime::FullName(actor));
        }
        if (it->refusals >= 3) continue;
        double const jump = Distance(*now, it->at);
        if (jump <= 30.0) {
            it->at = *now;  // its own small moves (a step, a turn's sway)
            cw::Calm(it->insist);
            continue;
        }
        if (jump >= 600.0 || setLocation == nullptr) {
            it->at = *now;  // another space: the scene's own
            continue;
        }
        // A matinee's move track walks it: the authored walk is its own (ME1's trackWalks).
        if (TrackMovedLately(actor, 500)) {
            ++gC.trackWalks;
            it->at = *now;
            continue;
        }
        // The game repeating the placement it just made (ME1's cw::Insists: two ticks at the same place, within 1 m): its own.
        if (cw::Insists(it->insist, {now->x, now->y, now->z}, jump)) {
            ++it->yielded;
            ++gC.yieldedTotal;
            log::Info("conversation world: {} put again {:.0f} units from where it stood: the game's own, kept",
                ue3::runtime::FullName(actor), jump);
            it->at = *now;
            cw::Calm(it->insist);
            continue;
        }
        ue3::Params move(ue3::runtime::Get(), setLocation);
        move.Set("NewLocation", it->at);
        ue3::runtime::Call(actor, setLocation, move);
        write_journal::Wrote("ConversationWorld.keep", actor, 0);
        if (!move.Return<bool>().value_or(false)) {
            ++gC.refusedMoves;
            if (++it->refusals == 3) {
                log::Info("conversation world: {} can't be put back (SetLocation refused it three times): left where the game puts it",
                    ue3::runtime::FullName(actor));
            }
            it->at = *now;
            continue;
        }
        ++it->undone;
        ++gC.undoneTotal;
        if (it->undone <= 3) {
            log::Info("conversation world: {} moved {:.0f} units by the stage: put back where it stood", ue3::runtime::FullName(actor),
                jump);
        }
    }
}

// ---- Moments: a level cutscene that walks Shepard (ME1's worldMoments = view; research/69 "A moment in the world",
// research/le2-conversations-in-world.md section 6; the owner, 2026-11-19: "as many as possible to help with immersion") -------
// In the cinematic mode, once a matinee's move track walks Shepard's pawn (cw::StepProvisional: wait for the track while the
// game's camera is still the player's, a quarter of a second once a camera of the scene's own has the view), the script's
// Shepard walks as authored (nothing withheld: the level counts on where it puts him) and the view stays the player's where the
// player stood: head-tracked, both eyes, the left stick to walk (the boundary around Shepard's place, as ME1's moment) and the
// right stick to turn; his pawn hidden while the view stands apart from it, his body the player's (BodyAway). At the end the
// view joins him (a cut, the host's dip, when farther than where he stands: ME1's opening limit). Left on the 3D screen by rule:
// a cutscene no track walks Shepard in, one whose pawn is hidden (no place for the player), one not begun from the player's own
// view (around a load, a movie, a screen).
ue3::FieldRef gViewTargetField{"Engine.Camera", "ViewTarget", ue3::kEveryGame};
ue3::FieldRef gViewTargetActor{gViewTargetField, "Target"};
ue3::FieldRef gHiddenField{"Engine.Actor", "bHidden", ue3::kEveryGame};
ue3::FunctionRef gSetHidden{"Engine.Actor.SetHidden", ue3::kEveryGame};

struct Moment {
    bool on = false;
    bool provisional = true;    // no track of Shepard's yet
    bool declined = false;      // this cinematic stretch is the screen's (until the mode changes)
    std::uint32_t serial = 0;
    std::uint32_t cut = 0;
    std::uint64_t startMs = 0;
    std::uint64_t stepMs = 0;
    ue3::Vector feet{};         // the player's place, as a pawn's location
    ue3::Vector eyeOffset{};    // the eyes above it (the view on foot against the pawn)
    ue3::Rotator facing{};
    float fov = 90.0f;
    ue3::Vector held{};
    bool hid = false;           // his pawn hidden by us while apart (shown again by us)
    double walkedUnits = 0.0;
    double apartMaxUnits = 0.0;
    double turnedDeg = 0.0;
    vr::comfort::SnapTurn snap;
    // The floor marker (cw::StepMarker): where the script's walk ends (the track's last key), the ring there until the player
    // stands in it.
    void const* endTrack = nullptr;
    std::optional<ue3::Vector> walkEnd;   // on the floor (world)
    cw::MarkerState marker;
    unsigned markers = 0;
};
Moment gM;

// ---- Stances from above (ME1's records, research/69 "Stances from above" and "Auditing scenes by rule"): the same JSON, so
// tools/scene_audit.py's rules and tools/conversation-stance.py (--heads, --findings, the sheets) judge the sequels' scenes as they
// judge ME1's. `mevr convo stance [tag]`, or by itself with `mevr convo stances on` / [Cinematics] worldStances = 1: after the
// opening, after a dip, at each new line, when someone moved a metre or turned 40 degrees, every 2 s, at the end; a moment's
// every 1.5 s. %LOCALAPPDATA%\MEVR\stances\<stamp>-<number>-<scene>-<tag>.json. No wall scan (ME1's `walls`), no circle yet.
std::optional<bool> gStancesOverride;  // `mevr convo stances on|off` over [Cinematics] worldStances
struct Stances {
    double lastS = -10.0;           // the scene's seconds at the last record
    bool opening = false;           // the opening's record written
    std::int32_t entry = -2;
    std::uint32_t cut = 0;
    double cutS = -1.0;             // a dip seen at this time, its record 0.5 s later
    std::map<void const*, std::pair<ik::Vec3, double>> seen;  // places and facings at the last record
    ik::Vec3 eyes;
    unsigned written = 0;
};
Stances gStances;

// `mevr convo hide body|person on|off` (the broken runs, research/69 "Broken runs the audit must catch"): the player's body mesh, or
// the conversation owner's, hidden while a scene plays in the world, so the audit's body-not-drawn / person-not-drawn must catch it.
// Shown again when switched off and at the scene's end; never set otherwise.
bool gHideBodyTest = false;
bool gHidePersonTest = false;
ue3::Kept gHiddenForTest[2];

bool StancesOn() {
    return gStancesOverride.value_or(gSettings.stances);
}

std::string JsonText(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) < 32) {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string P3(ik::Vec3 const& v) {
    return std::format("[{:.0f}, {:.0f}, {:.0f}]", v.x, v.y, v.z);
}

double YawDeg(ue3::Rotator const& r) {
    return r.yaw * (360.0 / 65536.0);
}

std::string ShortName(ue3::Object const* o) {
    return o != nullptr ? ue3::runtime::Get().NameText(o) : std::string("none");
}

std::string DisplayName(ue3::Object* actor, ue3::Object* pawn) {
    if (actor != nullptr && actor == pawn) return "Shepard";
    for (squad_weapons::MemberView const& m : squad_weapons::Members()) {
        if (m.pawn == actor && !m.name.empty()) return m.name;
    }
    return ShortName(actor);
}

bool IsSquad(ue3::Object* actor) {
    for (squad_weapons::MemberView const& m : squad_weapons::Members()) {
        if (m.pawn == actor) return true;
    }
    return false;
}

// One record. `moment`: a moment's (the people everyone shown within 20 m); else the held conversation's.
std::string WriteStance(std::string const& tag, bool moment) {
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    if (moment ? !gM.on : !(gC.on && gC.placed && !gC.skipped)) return "stance: no scene held in the world";
    if (controller == nullptr || pawn == nullptr) return "stance: no player";
    ue3::Object* const conversation = moment ? nullptr : gC.controller;
    if (!moment && (conversation == nullptr || !ue3::runtime::IsAlive(conversation))) return "stance: the conversation is gone";
    std::uint64_t const now = GetTickCount64();
    double const seconds = static_cast<double>(now - (moment ? gM.startMs : gC.startMs)) / 1000.0;
    ue3::Vector const heldPlace = moment ? gM.held : gC.held;
    ue3::Rotator const facing = moment ? gM.facing : gC.facing;
    ik::Vec3 const held = V(heldPlace);
    ik::Vec3 const eyes = gDrawnEyes.value_or(held);
    // Shepard's head as drawn (ME1's records: HeadOf), the body the player sees; the stand-in decides by the animated one.
    std::optional<HeadPose> const shepPose = HeadPoseOf(pawn);
    std::optional<ik::Vec3> const shepHead = shepPose ? std::optional<ik::Vec3>(shepPose->at) : ShepardSceneHead(pawn);
    ue3::Object* const speaker = conversation != nullptr ? gSpeakerNow.Read<ue3::Object*>(conversation).value_or(nullptr) : nullptr;
    ue3::Object* const listener = conversation != nullptr ? gListenerNow.Read<ue3::Object*>(conversation).value_or(nullptr) : nullptr;
    ue3::Object* const owner = conversation != nullptr ? gOwner.Read<ue3::Object*>(conversation).value_or(nullptr) : nullptr;
    ue3::Object* const eyesTarget = gEyesState.target.Live();
    std::vector<ue3::Object*> who;
    std::string name;
    if (moment) {
        who = NearPawns(controller, eyes, 2000.0, 24);
        name = std::format("moment_{}", gM.serial);
    } else {
        who = Speakers(conversation, ConversationPlayer(conversation));
        name = ue3::runtime::FullName(ConversationData(conversation));
    }
    who.push_back(pawn);
    std::string people;
    for (ue3::Object* const actor : who) {
        if (actor == nullptr || !ue3::runtime::IsAlive(actor)) continue;
        auto const at = player::Location(actor);
        auto const rot = player::Rotation(actor);
        if (!at || !rot) continue;
        bool const isShepard = actor == pawn;
        std::optional<HeadPose> const pose = HeadPoseOf(actor);
        std::optional<ik::Vec3> const head = isShepard ? shepHead : pose ? std::optional<ik::Vec3>(pose->at) : std::nullopt;
        std::string const role = actor == speaker ? "speaker" : actor == listener ? "listener" : "bystander";
        LookNow const look = isShepard ? LookNow{} : LookOf(actor);
        std::string heading = "null";
        if (pose && !isShepard) {
            ik::Vec3 const f = pose->axis[kFaceAxis];
            heading = std::format("{{\"yaw\": {:.0f}, \"pitch\": {:.0f}, \"toYouDeg\": {:.0f}, \"toHeldDeg\": {:.0f}",
                std::atan2(f.y, f.x) * (180.0 / 3.14159265358979323846),
                std::atan2(f.z, std::hypot(f.x, f.y)) * (180.0 / 3.14159265358979323846), AngleDeg(f, eyes - pose->at),
                AngleDeg(f, held - pose->at));
            if (shepHead) heading += std::format(", \"toShepardDeg\": {:.0f}", AngleDeg(f, *shepHead - pose->at));
            if (look.aim) {  // which of the head bone's axes points where the chain aims (the face's axis, [verify] per skeleton)
                ik::Vec3 const aim = V(*look.aim) - pose->at;
                heading += std::format(", \"axesToAimDeg\": [{:.0f}, {:.0f}, {:.0f}]", AngleDeg(pose->axis[0], aim),
                    AngleDeg(pose->axis[1], aim), AngleDeg(pose->axis[2], aim));
            }
            heading += "}";
        }
        std::string gameLook = "null";
        if (look.known) {
            std::string const target = look.target == nullptr ? std::string()
                : look.target == eyesTarget && eyesTarget != nullptr ? std::string("you")
                : look.target == pawn ? std::string("Shepard")
                : DisplayName(look.target, pawn);
            gameLook = std::format("{{\"target\": \"{}\", \"rank\": {}, \"aim\": {}}}", JsonText(target), look.rank,
                look.aim ? P3(V(*look.aim)) : std::string("null"));
        }
        auto const drawnAgo = DrawnAgoS(controller, PersonMesh(actor));
        bool const inSight = !isShepard && head && std::sqrt((head->x - eyes.x) * (head->x - eyes.x) + (head->y - eyes.y) * (head->y - eyes.y) +
            (head->z - eyes.z) * (head->z - eyes.z)) <= 1200.0 && InHeadView(*head).value_or(false) &&
            LineOfSight(pawn, ue3::Vector{static_cast<float>(eyes.x), static_cast<float>(eyes.y), static_cast<float>(eyes.z)},
                ue3::Vector{static_cast<float>(head->x), static_cast<float>(head->y), static_cast<float>(head->z)});
        people += std::format("{}\n  {{\"name\": \"{}\", \"id\": \"{}\", \"at\": {}, \"yaw\": {:.0f}, \"head\": {}, \"start\": {}, "
                              "\"role\": \"{}\", \"owner\": {}, \"squad\": {}, \"shepard\": {}, \"hidden\": {}, \"walks\": false, "
                              "\"walk\": null, \"held\": null, \"looksAt\": \"\", \"face\": {}, \"gameLook\": {}, \"inSight\": {}, "
                              "\"drawnAgoS\": {}}}",
            people.empty() ? "" : ",", JsonText(DisplayName(actor, pawn)), JsonText(ShortName(actor)), P3(V(*at)), YawDeg(*rot),
            head ? P3(*head) : std::string("null"), P3(V(*at)), role, actor == owner ? "true" : "false", IsSquad(actor) ? "true" : "false",
            isShepard ? "true" : "false", gActorHiddenRead.ReadBool(actor).value_or(false) ? "true" : "false", heading, gameLook,
            inSight ? "true" : "false", drawnAgo ? std::format("{:.2f}", std::min(*drawnAgo, 600.0)) : std::string("null"));
    }
    std::string others;
    int listed = 0;
    for (ue3::Object* const o : LivePeople(controller)) {
        if (listed >= 64) break;
        if (o == pawn || !IsPerson(o) || gActorHiddenRead.ReadBool(o).value_or(true) ||
            std::find(who.begin(), who.end(), o) != who.end()) {
            continue;
        }
        auto const at = player::Location(o);
        auto const rot = player::Rotation(o);
        if (!at || !rot || std::hypot(at->x - held.x, at->y - held.y) > 2000.0) continue;
        others += std::format("{}{{\"name\": \"{}\", \"at\": {}, \"yaw\": {:.0f}}}", others.empty() ? "" : ", ",
            JsonText(DisplayName(o, pawn)), P3(V(*at)), YawDeg(*rot));
        ++listed;
    }
    gStances.seen.clear();
    for (ue3::Object* const actor : who) {
        if (actor == nullptr || !ue3::runtime::IsAlive(actor)) continue;
        auto const at = player::Location(actor);
        auto const rot = player::Rotation(actor);
        if (at && rot) gStances.seen[actor] = {V(*at), YawDeg(*rot)};
    }
    gStances.eyes = eyes;
    ue3::Object* const cylinder = gPawnCylinder.Read<ue3::Object*>(pawn).value_or(nullptr);
    auto const halfHeight = cylinder != nullptr ? gCylinderHeight.Read<float>(cylinder) : std::nullopt;
    ue3::Vector const feet = moment ? gM.feet : gC.pin;
    double const floor = feet.z - (halfHeight ? *halfHeight : 90.0f);
    auto const bodyDrawnAgo = DrawnAgoS(controller, player::Mesh(pawn));
    std::int32_t const entry = conversation != nullptr ? gEntryNow.Read<std::int32_t>(conversation).value_or(-1) : -1;
    std::string const json = std::format(
        "{{\"game\": \"" MEVR_GAME_NAME_A "\", \"conversation\": \"{}\", \"number\": {}, \"tag\": \"{}\", \"seconds\": {:.1f}, "
        "\"staged\": false, \"opening\": \"{}\", \"entry\": {}, \"line\": \"\", \"unitsPerMeter\": 100, \"walkAlong\": null, \"cut\": {},\n"
        "\"player\": {{\"at\": {}, \"yaw\": {:.0f}, \"eyes\": {}, \"eyesDrawn\": {}, \"bodyFromEyesM\": {}, \"detached\": {}, "
        "\"turnedDeg\": {:.0f}, \"boundary\": {}, \"boundaryM\": {:.2f}, \"floor\": {:.0f}, \"eyesTarget\": {}, \"sceneWants\": {}, "
        "\"steering\": false, \"armIk\": \"{}\", \"hands\": {}, \"bodyDrawnAgoS\": {}, \"nearPlane\": {:.0f}}},\n"
        "\"people\": [{}],\n\"others\": [{}],\n\"circle\": null,\n\"ahead\": {{\"now\": [], \"later\": []}}}}\n",
        JsonText(name), moment ? gM.serial : gC.serial, JsonText(tag), seconds, moment ? "moment" : cw::Name(gC.opening), entry, gCutSerial,
        P3(held), YawDeg(facing), P3(eyes), gDrawnEyes ? "true" : "false",
        shepHead ? std::format("{:.2f}", std::hypot(shepHead->x - eyes.x, shepHead->y - eyes.y) / 100.0) : std::string("null"),
        moment && gM.hid ? "true" : "false", moment ? gM.turnedDeg : gC.turnedDeg, P3(V(moment ? gM.feet : gC.start)),
        gSettings.leashUnits / 100.0, floor, gEyesState.on ? "true" : "false",
        !moment && gC.sceneWantsShepard ? P3(V(gC.sceneWants)) : std::string("null"), JsonText(arm_ik::Status().substr(0, 40)),
        arm_ik::HandsJson(), bodyDrawnAgo ? std::format("{:.2f}", std::min(*bodyDrawnAgo, 600.0)) : std::string("null"),
        moment ? -1.0f : gDrawnNear, people, others);
    wchar_t buffer[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) == 0) return "stance: no LOCALAPPDATA";
    std::filesystem::path const folder = std::filesystem::path(buffer) / L"MEVR" / L"stances";
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    SYSTEMTIME t{};
    GetLocalTime(&t);
    std::string safeName = name.substr(name.rfind('.') == std::string::npos ? 0 : name.rfind('.') + 1);
    std::string safeTag = tag;
    for (std::string* s : {&safeName, &safeTag}) {
        for (char& c : *s) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
        }
    }
    std::filesystem::path const file = folder / std::format("{:04}-{:02}-{:02}_{:02}-{:02}-{:02}-{:03}-{}-{}-{}.json", t.wYear, t.wMonth,
        t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, moment ? gM.serial : gC.serial, safeName, safeTag);
    FILE* out = _wfopen(file.c_str(), L"wb");
    if (out == nullptr) return "stance: could not write " + ini::Utf8(file);
    std::fwrite(json.data(), 1, json.size(), out);
    std::fclose(out);
    gStances.lastS = seconds;
    ++gStances.written;
    return std::format("stance: {} ({})", ini::Utf8(file), tag);
}

// Whether the scene moved since the last record: someone of it a metre from where it stood then or turned 40 degrees, or the
// player's eyes 0.75 m.
bool MovedSinceStance(std::vector<ue3::Object*> const& people) {
    if (gDrawnEyes && std::hypot(gDrawnEyes->x - gStances.eyes.x, gDrawnEyes->y - gStances.eyes.y) > 75.0) return true;
    for (ue3::Object* const actor : people) {
        auto const seen = gStances.seen.find(actor);
        if (actor == nullptr || !ue3::runtime::IsAlive(actor) || seen == gStances.seen.end()) continue;
        auto const at = player::Location(actor);
        auto const rot = player::Rotation(actor);
        if (!at || !rot) continue;
        double const turned = std::abs(std::remainder(YawDeg(*rot) - seen->second.second, 360.0));
        if (std::hypot(at->x - seen->second.first.x, at->y - seen->second.first.y) > 100.0 || turned > 40.0) return true;
    }
    return false;
}

// Each tick of a held conversation (after its place is taken): the records ME1's StanceTick writes.
void StanceStep(std::vector<ue3::Object*> const& people) {
    if (!StancesOn()) return;
    std::uint64_t const now = GetTickCount64();
    double const s = static_cast<double>(now - gC.startMs) / 1000.0;
    char const* tag = nullptr;
    if (!gStances.opening) {
        if (now - gC.placedMs < 1500) return;  // the opening settles (ME1: after its first drawn frames)
        gStances.opening = true;
        gStances.cut = gCutSerial;
        gStances.entry = gEntryNow.Read<std::int32_t>(gC.controller).value_or(-1);
        tag = "opening";
    } else if (gCutSerial != gStances.cut) {
        gStances.cut = gCutSerial;
        gStances.cutS = s;
        return;
    } else if (gStances.cutS >= 0.0 && s - gStances.cutS >= 0.5) {
        gStances.cutS = -1.0;
        tag = "after-dip";
    } else if (s - gStances.lastS >= 1.0) {
        std::int32_t const entry = gEntryNow.Read<std::int32_t>(gC.controller).value_or(-1);
        if (entry != gStances.entry) {
            gStances.entry = entry;
            tag = "line";
        } else if (MovedSinceStance(people)) {
            tag = "moved";
        } else if (s - gStances.lastS >= 2.0) {
            tag = "tick";
        }
    }
    if (tag != nullptr) log::Info("conversation world: {}", WriteStance(tag, false));
}

// The test hides (`mevr convo hide`), each tick of a held scene: hidden while switched on, shown again when off.
void SetTestHidden(int slot, ue3::Object* mesh, bool hide) {
    ue3::Function* const fn = gPrimitiveSetHidden.Get();
    if (mesh == nullptr || !ue3::runtime::IsAlive(mesh) || fn == nullptr) {
        gHiddenForTest[slot] = ue3::Kept();
        return;
    }
    ue3::Params p(ue3::runtime::Get(), fn);
    if (!p.Valid() || !p.SetBool("NewHidden", hide)) return;
    ue3::runtime::Call(mesh, fn, p);
    gHiddenForTest[slot] = hide ? ue3::Kept(mesh) : ue3::Kept();
    log::Info("conversation world: {} {}'s mesh for a broken run", hide ? "hid" : "showed again", slot == 0 ? "the player's body" : "a person");
}

void ShowTestHidden() {
    for (int slot : {0, 1}) {
        if (ue3::Object* const mesh = gHiddenForTest[slot].Live()) SetTestHidden(slot, mesh, false);
        gHiddenForTest[slot] = ue3::Kept();
    }
}

void ApplyTestHides(ue3::Object* controller, ue3::Object* owner) {
    if (gHideBodyTest && !gHiddenForTest[0]) {
        SetTestHidden(0, player::Mesh(player::Pawn(controller)), true);
    } else if (!gHideBodyTest && gHiddenForTest[0]) {
        if (ue3::Object* const mesh = gHiddenForTest[0].Live()) SetTestHidden(0, mesh, false);
        gHiddenForTest[0] = ue3::Kept();
    }
    if (gHidePersonTest && !gHiddenForTest[1] && owner != nullptr && IsPerson(owner)) {
        SetTestHidden(1, PersonMesh(owner), true);
    } else if (!gHidePersonTest && gHiddenForTest[1]) {
        if (ue3::Object* const mesh = gHiddenForTest[1].Live()) SetTestHidden(1, mesh, false);
        gHiddenForTest[1] = ue3::Kept();
    }
}

// Where a move track's walk ends (engine/ConversationHooks' MoveTrackEnd: its position curve's last key). Game thread.
std::optional<ue3::Vector> TrackEnd(void const* track, void const* instance) {
    float place[3];
    if (!engine::conversation_hooks::MoveTrackEnd(track, instance, place)) return std::nullopt;
    return ue3::Vector{place[0], place[1], place[2]};
}
// The counts of this session (the lane's report): held in first person, left on the 3D screen and why.
unsigned gMomentsHeld = 0;
unsigned gMomentsNoTrack = 0;
unsigned gMomentsHidden = 0;
unsigned gMomentsNotFromView = 0;

bool HideShepard(ue3::Object* pawn, bool hide) {
    ue3::Function* const function = gSetHidden.Get();
    if (pawn == nullptr || !ue3::runtime::IsAlive(pawn) || function == nullptr) return false;
    ue3::Params p(ue3::runtime::Get(), function);
    return p.Valid() && p.SetBool("bNewHidden", hide) && ue3::runtime::Call(pawn, function, p);
}

void EndMoment(conversation_world::Link const& link, char const* why, bool publishOff) {
    if (!gM.on) return;
    if (!gM.provisional) {
        if (StancesOn()) log::Info("conversation world: {}", WriteStance("moment-end", true));
        ue3::Object* const controller = player::Controller();
        EyesEnd(controller, NearPawns(controller, gDrawnEyes.value_or(V(gM.held)), 2000.0, 24), "moment", gM.serial);
    }
    ShowTestHidden();
    gDrawnEyes.reset();
    std::uint64_t const now = GetTickCount64();
    ue3::Object* const pawn = gShepardPawn;
    auto const at = pawn != nullptr && ue3::runtime::IsAlive(pawn) ? player::Location(pawn) : std::nullopt;
    if (gM.hid) HideShepard(pawn, false);
    if (!gM.provisional && at) {
        double const apart = std::hypot(at->x - gM.feet.x, at->y - gM.feet.y);
        bool const cut = apart > cw::OpeningLimits{}.stayUnits;
        if (cut) ++gCutSerial;  // the next scene's state (the conversation that follows) dips from black
        log::Info("conversation world: moment {}: {} after {:.1f} s: the view joins Shepard {:.2f} m away{} (walked {:.2f} m, at most "
                  "{:.2f} m apart, turned {:.0f} deg; {} floor marker{})", gM.serial, why, static_cast<double>(now - gM.startMs) / 1000.0,
            apart / 100.0, cut ? " under a dip" : "", gM.walkedUnits / 100.0, gM.apartMaxUnits / 100.0, gM.turnedDeg, gM.markers,
            gM.markers == 1 ? "" : "s");
        // Where the player now stands, for what follows (a conversation's opening judges from there; ME1: the moment leaves its
        // place as the place on foot).
        gFoot = {true, *at, now};
        gFootPov = {true, ue3::Vector{at->x + gM.eyeOffset.x, at->y + gM.eyeOffset.y, at->z + gM.eyeOffset.z}, gM.facing, gM.fov};
        gEndedShownMs = now;
    }
    view_anchor::Hold(std::nullopt);
    if (gDof.IsOff()) gDofBackOwed = true;
    if (publishOff) PublishOff(link);
    if (gRide.units > 0.0) log::Info("conversation world: moment {}: the player rode {:.2f} m with what Shepard stood on", gM.serial, gRide.units / 100.0);
    gRide.units = 0.0;
    gRide.logged = false;
    gRideMomentDelta = {};
    bool const declined = gM.declined;
    gM = {};
    gM.declined = declined;
    gMomentHeld = false;
    gShepardTrack = nullptr;
    gShepardTrackInstance = nullptr;
}

// One tick of the cinematic mode with the world wanted: true while the moment holds the view (the caller returns).
bool TickMoment(conversation_world::Link const& link, ue3::Object* controller, std::optional<std::uint8_t> mode) {
    bool const cinematic = mode && engine::adapter::GameMode(*mode) == engine::adapter::Mode::Cinematic;
    if (!cinematic || !WorldWanted() || link.publish == nullptr) {
        // Into a conversation shown in the world, its opening publishes this tick: no "off" between the two.
        bool const intoConversation = mode && engine::adapter::IsConversationMode(*mode) && WorldWanted();
        EndMoment(link, !cinematic ? "the cinematic mode ended" : "the world mode is off", !intoConversation);
        if (!cinematic) gM.declined = false;
        return false;
    }
    if (gM.declined) return false;
    std::uint64_t const now = GetTickCount64();
    ue3::Object* const pawn = gShepardPawn;
    if (!gM.on) {
        // Begun from the player's own view: on foot a moment ago, or a conversation shown in the world just before.
        bool const fromView = gFootPov.valid && gFoot.valid && now - gFoot.ms < 1500 &&
            (gLastFootRunMs >= 3000 || (gEndedShownMs != 0 && now - gEndedShownMs < 1500));
        if (!fromView || pawn == nullptr) {
            gM.declined = true;
            ++gMomentsNotFromView;
            log::Info("conversation world: a cutscene begins not from the player's own view (around a load, a movie or a screen): "
                      "the 3D screen");
            return false;
        }
        gM.on = true;
        gM.provisional = true;
        gM.serial = ++gSerial;
        gStances = {};
        gM.startMs = now;
        gM.stepMs = now;
        gM.feet = gFoot.pawn;
        gM.eyeOffset = {gFootPov.location.x - gFoot.pawn.x, gFootPov.location.y - gFoot.pawn.y, gFootPov.location.z - gFoot.pawn.z};
        gM.facing = ue3::Rotator{0, gFootPov.rotation.yaw, 0};
        gM.fov = gFootPov.fov;
    }
    double const sinceS = static_cast<double>(now - gM.startMs) / 1000.0;
    auto const at = pawn != nullptr && ue3::runtime::IsAlive(pawn) ? player::Location(pawn) : std::nullopt;
    if (gM.provisional) {
        // ME1's presence rule: Shepard's pawn hidden by the script has no place for the player in this scene.
        if (gHiddenField.ReadBool(pawn).value_or(false)) {
            ++gMomentsHidden;
            log::Info("conversation world: moment {}: Shepard's pawn is hidden in this cutscene (no place for the player): the 3D screen",
                gM.serial);
            gM.declined = true;
            EndMoment(link, "Shepard hidden", true);
            return false;
        }
        bool cameraOfItsOwn = false;
        if (ue3::Object* const camera = player::Camera(controller)) {
            if (void* const target = gViewTargetField.Address(camera)) {
                ue3::Object* const actor = gViewTargetActor.Read<ue3::Object*>(target).value_or(nullptr);
                cameraOfItsOwn = actor != nullptr && actor != pawn && actor != controller;
            }
        }
        switch (cw::StepProvisional(TrackMovedLately(pawn, 300), cameraOfItsOwn, sinceS)) {
        case cw::Provisional::Moment:
            gM.provisional = false;
            ++gMomentsHeld;
            log::Info("conversation world: moment {}: a matinee's move track walks Shepard {:.1f} s into the cutscene: held in first "
                      "person (the script's Shepard walks, the view is the player's)", gM.serial, sinceS);
            if (gSettings.dofOff) gDof.Off("a moment held in the world (the matinee's camera focuses for its own shot)");
            break;
        case cw::Provisional::NotOne:
            ++gMomentsNoTrack;
            log::Info("conversation world: moment {}: no track walks Shepard ({}): a cutscene for the 3D screen", gM.serial,
                cameraOfItsOwn ? "the scene's camera has the view" : "6 s");
            gM.declined = true;
            EndMoment(link, "not a moment", true);
            return false;
        case cw::Provisional::Wait:
            break;
        }
    }
    double const dt = std::clamp(static_cast<double>(now - gM.stepMs) / 1000.0, 0.0, 0.1);
    gM.stepMs = now;
    if (!gM.provisional) {
        RideBase(pawn);  // ME1's RideBase in a moment: what Shepard stands on carries the view
        gM.feet.x += gRideMomentDelta.x;
        gM.feet.y += gRideMomentDelta.y;
        gM.feet.z += gRideMomentDelta.z;
    }
    gRideMomentDelta = {};
    if (!gM.provisional && at) {
        vr::controllers::State const pads = vr::controllers::Latest();
        vr::host_view::State const view = vr::host_view::Latest();
        if (pads.active && view.valid && dt > 0.0) {
            TurnByStick(gM.facing, gM.feet, gM.snap, gM.turnedDeg, pads.right.stickX, dt, now, "moment", gM.serial);
            // The head's facing in the world through the held frame (as the conversation's walk takes it).
            ue3::Vector const eyes{gM.feet.x + gM.eyeOffset.x, gM.feet.y + gM.eyeOffset.y, gM.feet.z + gM.eyeOffset.z};
            engine::TrackingFrame const frame = engine::FromCameraView(engine::UnrealVector{eyes.x, eyes.y, eyes.z},
                engine::UnrealRotation{0, gM.facing.yaw, 0}, {view.lookPitch, view.lookYaw, 0},
                {view.offsetForward, view.offsetRight, view.offsetUp}, view.head, view.unitsPerMeter);
            engine::UnrealVector const forward = engine::Apply(frame.rotation, engine::ToBasis(view.head.orientation).forward);
            double const yawDeg = std::atan2(forward.y, forward.x) * (180.0 / 3.14159265358979323846);
            cw::Vec3 const from{gM.feet.x, gM.feet.y, gM.feet.z};
            cw::PlayerStep const step = cw::StepPlayer(from, {at->x, at->y, at->z}, gSettings.leashUnits, pads.left.stickX,
                pads.left.stickY, yawDeg, gSettings.speedUnitsPerS, dt);
            if (step.moved) {
                gM.walkedUnits += std::hypot(step.place.x - from.x, step.place.y - from.y);
                gM.feet.x = static_cast<float>(step.place.x);
                gM.feet.y = static_cast<float>(step.place.y);
            }
        }
        // His pawn hidden while the view stands apart from it (the script's walk, seen from the player's place, would be a
        // headless body walking off), shown again near (hysteresis), never shown where the script hid it.
        double const apart = std::hypot(at->x - gM.feet.x, at->y - gM.feet.y);
        gM.apartMaxUnits = std::max(gM.apartMaxUnits, apart);
        if (!gM.hid && apart > 60.0 && !gHiddenField.ReadBool(pawn).value_or(false)) {
            gM.hid = HideShepard(pawn, true);
        } else if (gM.hid && apart < 40.0) {
            HideShepard(pawn, false);
            gM.hid = false;
        }
        // The view at the floor he walks on once it is under the player (a stair in the walk): his height, near him.
        if (apart < 120.0) gM.feet.z = at->z;
    }
    gM.held = {gM.feet.x + gM.eyeOffset.x, gM.feet.y + gM.eyeOffset.y, gM.feet.z + gM.eyeOffset.z};
    gMomentHeld = true;
    vr::host_view::State const view = vr::host_view::Latest();
    std::optional<engine::TrackingFrame> frame;
    if (view.valid) {
        frame = engine::FromCameraView(engine::UnrealVector{gM.held.x, gM.held.y, gM.held.z}, engine::UnrealRotation{0, gM.facing.yaw, 0},
            {view.lookPitch, view.lookYaw, 0}, {view.offsetForward, view.offsetRight, view.offsetUp}, view.head, view.unitsPerMeter);
    }
    // The floor marker (cw::StepMarker): where Shepard's walk ends, while the player follows it.
    MarkerOut marker;
    if (!gM.provisional && at) {
        vr::floor_lock::PawnSample const sample = player::FloorLockSample();
        double const half = sample.floorValid && sample.floorZ < at->z ? at->z - sample.floorZ : 90.0;
        if (gShepardTrack != nullptr && gShepardTrack != gM.endTrack && TrackMovedLately(pawn, 500)) {
            gM.endTrack = gShepardTrack;
            auto const end = TrackEnd(gShepardTrack, gShepardTrackInstance);
            // A place the track can't have meant (the origin, or far from where it walks him now) is no walk's end: logged, no marker.
            double const fromHim = end ? std::hypot(end->x - at->x, end->y - at->y) : 0.0;
            if (end && (std::abs(end->x) + std::abs(end->y) + std::abs(end->z) > 1.0) && fromHim < 6000.0) {
                gM.walkEnd = ue3::Vector{end->x, end->y, static_cast<float>(end->z - half)};
            } else {
                log::Info("convo: marker: moment {}: the track's last key gave {} ({:.0f} units from Shepard): no walk's end, no marker",
                    gM.serial, end ? std::format("({:.0f}, {:.0f}, {:.0f})", end->x, end->y, end->z) : std::string("nothing"), fromHim);
            }
        }
        cw::MarkerFacts facts;
        facts.scene = true;
        if (gM.walkEnd) facts.walkEnd = cw::Vec3{gM.walkEnd->x, gM.walkEnd->y, gM.walkEnd->z};
        facts.player = cw::Vec3{gM.feet.x, gM.feet.y, gM.feet.z - half};
        cw::Marker const m = cw::StepMarker(gM.marker, facts, dt);
        if (m.changed) {
            ++gM.markers;
            log::Info("convo: marker: moment {}: {} ({:.0f}, {:.0f}, {:.0f}), {:.2f} m from the player", gM.serial, cw::Name(m.need),
                m.at.x, m.at.y, m.at.z, std::hypot(m.at.x - facts.player.x, m.at.y - facts.player.y) / 100.0);
        }
        if (m.alpha > 0.0 && frame) {
            marker.on = true;
            marker.need = m.need;
            marker.at = engine::ToTracking(*frame, engine::UnrealVector{m.at.x, m.at.y, m.at.z});
            marker.alpha = static_cast<float>(m.alpha);
        }
    }
    PublishHeld(link, controller, gM.serial, gCutSerial, gM.held, gM.facing, gM.fov, marker);
    if (frame) view_anchor::Hold(*frame, view);
    if (!gM.provisional) {
        // The looks at the player's drawn eyes (ME1's StepEyes in a moment: everyone shown within 20 m), the records from above.
        gDrawnEyes = frame ? std::optional<ik::Vec3>{ik::Vec3{frame->headWorld.x, frame->headWorld.y, frame->headWorld.z}} : std::nullopt;
        EyesStep(controller, NearPawns(controller, gDrawnEyes.value_or(V(gM.held)), 2000.0, 24), gDrawnEyes, "moment", gM.serial);
        if (StancesOn()) {
            double const s = static_cast<double>(now - gM.startMs) / 1000.0;
            if (gStances.written == 0) {
                log::Info("conversation world: {}", WriteStance("moment-begin", true));
            } else if (s - gStances.lastS >= 1.5) {
                log::Info("conversation world: {}", WriteStance("moment", true));
            }
        }
    }
    return true;
}

} // namespace

void TryStartup() {
    if (gStarted || !ue3::runtime::Up()) return;
    ue3::Function* const update = gUpdateCamera.Get();
    if (update == nullptr) return;
    engine::process_event::AddPostHandler(update, &OnUpdateCamera);
    gStarted = true;
}

void Tick(conversation_world::Link const& link) {
    TryStartup();
    RefreshSettings();
    ue3::Object* const controller = player::Controller();
    gShepardPawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    // The source hooks' questions are ours while the world mode is wanted (Mod.cpp installs the hooks then).
    engine::conversation_hooks::SetKeepProvider(WorldWanted() ? &KeepAtSource : nullptr);
    engine::conversation_hooks::SetMoveTrackProvider(WorldWanted() ? &OnMoveTrack : nullptr);
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    bool const conversationMode = mode && engine::adapter::IsConversationMode(*mode);
    if (mode && *mode == 0) {
        if (auto const at = player::Location(player::Pawn(controller))) gFoot = {true, *at, GetTickCount64()};
        if (ue3::Object* const local = gLocalPlayer.Read<ue3::Object*>(controller).value_or(nullptr)) {
            if (auto const plane = gNearClip.Read<float>(local); plane && std::isfinite(*plane) && *plane > 0.0f && *plane < 50.0f) {
                gFootNear = *plane;  // the player's plane on foot (a conversation's is a metre or more)
            }
        }
        if (gOnFootSinceMs == 0) gOnFootSinceMs = GetTickCount64();
        gLastFootRunMs = GetTickCount64() - gOnFootSinceMs;
        gOnlyScenesSinceFoot = true;
        ue3::Object* const camera = player::Camera(controller);
        auto const pov = camera != nullptr ? player::CameraPov(camera) : std::nullopt;
        if (pov && pov->location != nullptr && pov->rotation != nullptr && pov->fov != nullptr) {
            // The eyes on foot where the renderer draws them, at the recenter point (the held view's frame origin: the drawn
            // eye midpoint without the player's own lean), else the game's camera. ME3's renderer draws the first-person eyes
            // about 28 units above the game's camera on foot and nothing above a held view (2026-11-20: a conversation held
            // from the camera drew the eyes 0.32 m low), so a held place taken from the camera starts low; from the drawn eyes it
            // is the view on foot. ME2's drawn eyes are its camera (FPC3's runs), so nothing changes there.
            ue3::Vector eyes = *pov->location;
            if (auto const frame = view_anchor::Current(); frame && vr::host_view::Latest().valid) {
                engine::UnrealVector const origin = engine::OriginWorld(*frame);
                ue3::Vector const drawn{static_cast<float>(origin.x), static_cast<float>(origin.y), static_cast<float>(origin.z)};
                double const apart = Distance(drawn, *pov->location);
                if (apart < 80.0) {
                    eyes = drawn;
                    static double s_loggedApart = -1.0;
                    if (std::abs(apart - s_loggedApart) > 5.0) {
                        s_loggedApart = apart;
                        log::Info("conversation world: on foot the eyes are drawn {:.0f} units from the game's camera ({:+.0f}, {:+.0f}, {:+.0f}): "
                                  "a scene held from the view on foot keeps the drawn eyes", apart, drawn.x - pov->location->x,
                            drawn.y - pov->location->y, drawn.z - pov->location->z);
                    }
                }
            }
            gFootPov = {true, eyes, *pov->rotation, *pov->fov};
            if (WorldWanted()) {
                ue3::Object* const pawn = player::Pawn(controller);
                std::optional<ik::Vec3> const animated = ShepardSceneHead(pawn);
                LearnEyeFromHead(pawn, eyes, *pov->rotation,
                    animated ? std::optional<engine::UnrealVector>(engine::UnrealVector{animated->x, animated->y, animated->z}) : std::nullopt);
            }
        }
    } else {
        gOnFootSinceMs = 0;
        if (mode) {
            auto const context = engine::adapter::GameMode(*mode);
            if (context != engine::adapter::Mode::Cinematic && context != engine::adapter::Mode::Conversation) gOnlyScenesSinceFoot = false;
        } else {
            gOnlyScenesSinceFoot = false;
        }
    }
    if (gDofBackOwed && gOnFootSinceMs != 0 && GetTickCount64() - gOnFootSinceMs >= 3000) {
        gDofBackOwed = false;
        gDof.Back("the conversation in the world ended (after 3 s on foot)");
    }
    if (!conversationMode || !WorldWanted() || link.publish == nullptr) {
        End(link, !conversationMode ? "the game left its conversation mode" : !WorldWanted() ? "the world mode is off" : "an older host");
        // A level cutscene that walks Shepard (a moment, ME1's worldMoments = view): its own held view.
        if (TickMoment(link, controller, mode)) return;
        std::uint64_t const nowMs = GetTickCount64();
        bool const armed = WorldWanted() && mode && *mode == 0 && gOnFootSinceMs != 0 && nowMs - gOnFootSinceMs >= 3000 && gFootPov.valid;
        bool const handover = gEndedShownMs != 0 && nowMs - gEndedShownMs < 300 && mode && !conversationMode;
        PublishArmed(link, armed, handover);
        return;
    }
    EndMoment(link, "a conversation began", false);  // the conversation's opening holds the view from the moment's place
    ue3::Object* const conversation = LiveConversation(controller);
    if (conversation == nullptr) {
        End(link, "no live conversation");
        return;
    }
    std::uint64_t const now = GetTickCount64();
    if (!gC.on || conversation != gC.controller) {
        End(link, "another conversation began");
        gFootSettledAtStart = gLastFootRunMs >= 3000 && gFoot.valid && (GetTickCount64() - gFoot.ms < 2000 || gOnlyScenesSinceFoot);
        gC.on = true;
        gC.serial = ++gSerial;
        gC.controller = conversation;
        gC.startMs = now;
        gStances = {};
        gC.why = "the opening settling";
        gC.skipped = !gFootSettledAtStart;
        // No place for the player in it (the generated presence table, cw::NoPlaceForPlayer): the 3D screen, by rule.
        std::string const name = ue3::runtime::FullName(ConversationData(conversation));
        bool const noPlace = cw::NoPlaceForPlayer(name, kGameNumber);
        gC.curated = cw::IsCuratedWalk(name, gSettings.curated);
        if (noPlace) {
            gC.skipped = true;
            ++gNoPlaceScreens;
        }
        log::Info("conversation world: conversation {} ({}) begins: {}", gC.serial, name,
            noPlace      ? "none of its lines acts on the player (the presence table): no place for the player, on the 3D screen"
            : gC.skipped ? "the game opened it around a load (not 3 s on foot before it): left on the 3D screen"
                         : "shown in the world, first person");
    }
    if (gC.skipped) return;
    ue3::Object* const pawn = player::Pawn(controller);
    // His head as the scene animates it (arm IK's animated head when fresh, else the bone: before our writes, the same), the
    // measure the eyes from it were learned against.
    std::optional<ik::Vec3> const sceneHead = ShepardSceneHead(pawn);
    auto const head = sceneHead ? std::optional<engine::UnrealVector>(engine::UnrealVector{sceneHead->x, sceneHead->y, sceneHead->z}) : HeadOf(pawn);
    auto const rotation = pawn != nullptr ? player::Rotation(pawn) : std::nullopt;
    if (!gC.placed) {
        if (static_cast<double>(now - gC.startMs) < gSettings.settleS * 1000.0) {
            // The stage's first placements settle; meanwhile the view on foot is held (ME1's armed opening: no frame of the
            // director's first shot, no 3D screen).
            if (gFootPov.valid) {
                gC.held = gFootPov.location;
                gC.facing = ue3::Rotator{0, gFootPov.rotation.yaw, 0};
                gC.fov = gFootPov.fov;
                gC.early = true;
                Publish(link, controller);
            }
            return;
        }
        auto const pawnAt = pawn != nullptr ? player::Location(pawn) : std::nullopt;
        if (!head || !rotation || !pawnAt) {
            gC.why = "Shepard's head bone not posed";
            return;
        }
        // ME1's opening rule: where the player stood against where the stage put Shepard.
        cw::OpeningFacts facts;
        facts.known = gFoot.valid && now - gFoot.ms < 10000;
        double const dx = pawnAt->x - gFoot.pawn.x, dy = pawnAt->y - gFoot.pawn.y;
        facts.markUnits = std::sqrt(dx * dx + dy * dy);
        facts.riseUnits = pawnAt->z - gFoot.pawn.z;
        facts.sightToMark = InSight(pawn, ue3::Vector{gFoot.pawn.x, gFoot.pawn.y, gFoot.pawn.z + 64.0f},
            ue3::Vector{pawnAt->x, pawnAt->y, pawnAt->z + 64.0f});
        gC.opening = cw::DecideOpening(facts, cw::OpeningRule::Auto);
        gC.pin = gC.opening == cw::Opening::Cut ? *pawnAt : gFoot.pawn;
        gC.start = gC.pin;
        // The eyes (ME1's opening: the view on foot when it was Shepard's a moment ago, else his eyes from his head bone facing
        // where his pawn faces): Stay and WalkIn keep the player's own eyes and facing, never turned at the opening.
        if (gC.opening != cw::Opening::Cut && facts.known && gFootPov.valid) {
            gC.eyeOffset = {gFootPov.location.x - gFoot.pawn.x, gFootPov.location.y - gFoot.pawn.y, gFootPov.location.z - gFoot.pawn.z};
            gC.facing = ue3::Rotator{0, gFootPov.rotation.yaw, 0};
            gC.eyesFrom = "the view on foot";
        } else {
            // ME1's FinishOpening: at his mark the eyes face the conversation's owner (the one the player came to talk to),
            // else where his pawn faces (Samara's starter on the Normandy faced the window: 2026-11-20).
            gC.facing = ue3::Rotator{0, rotation->yaw, 0};
            ue3::Object* const owner = gOwner.Read<ue3::Object*>(conversation).value_or(nullptr);
            auto const ownerAt = owner != nullptr && owner != pawn && ue3::runtime::IsAlive(owner) ? player::Location(owner) : std::nullopt;
            if (ownerAt && std::hypot(ownerAt->x - head->x, ownerAt->y - head->y) > 10.0) {
                double const yawDeg = std::atan2(ownerAt->y - head->y, ownerAt->x - head->x) * (180.0 / 3.14159265358979323846);
                gC.facing.yaw = static_cast<std::int32_t>(yawDeg * 65536.0 / 360.0);
            }
            ue3::Vector const eyes = EyeAtHead(*head, gC.facing);
            gC.eyeOffset = {eyes.x - pawnAt->x, eyes.y - pawnAt->y, eyes.z - pawnAt->z};
            gC.eyesFrom = gEyeFromHead.learned ? "his head bone (the eyes as learned on foot)" : "his head bone (the game's measure)";
        }
        ue3::Object* const camera = player::Camera(controller);
        auto const pov = camera != nullptr ? player::CameraPov(camera) : std::nullopt;
        gC.fov = pov && pov->fov != nullptr ? std::clamp(*pov->fov, 60.0f, 110.0f) : 90.0f;
        gC.placed = true;
        gC.placedMs = now;
        if (gC.opening == cw::Opening::Cut) ++gCutSerial;  // another space: the player is put at his mark under a dip (ME1's)
        gC.stepMs = now;
        // Only for a conversation begun after 3 s on foot (not one the game opens around a load, the docking officer's).
        if (gSettings.dofOff) {
            gDofBackOwed = false;
            gDof.Off("a conversation held in the world (the director's depth of field blurs the room)");
        }
        log::Info("conversation world: conversation {}: opening {} (the stage put Shepard {:.0f} units from where the player stood, "
                  "{:+.0f} up{}): the player's place is {}, Shepard kept there; the eyes from {} ({:.0f}, {:.0f}, {:.0f} from his "
                  "pawn); the left stick walks him within {:.0f} units",
            gC.serial, cw::Name(gC.opening), facts.markUnits, facts.riseUnits, facts.known ? "" : "; where he stood isn't known",
            gC.opening == cw::Opening::Cut ? "his mark (another space)" : "where the player stood", gC.eyesFrom, gC.eyeOffset.x,
            gC.eyeOffset.y, gC.eyeOffset.z, gSettings.leashUnits);
    } else if (gSettings.move) {
        // ME1's worldMove: the left stick along the head's facing, inside the boundary, never into people.
        double const dt = std::clamp(static_cast<double>(now - gC.stepMs) / 1000.0, 0.0, 0.1);
        vr::controllers::State const pads = vr::controllers::Latest();
        vr::host_view::State const view = vr::host_view::Latest();
        auto const frame = HeldFrame();
        bool const wheelUp = WheelUp(controller);
        double const magnitude = pads.active ? std::hypot(pads.left.stickX, pads.left.stickY) : 0.0;
        bool const walks = cw::StickWalks(gC.latch, wheelUp, magnitude);
        if (!walks) ++gC.wheelHeld;
        // ME1's turn: the right stick turns the held facing about the drawn eyes ([Comfort] turn: smooth or snap).
        if (pads.active && frame && view.valid && dt > 0.0) {
            TurnByStick(gC.facing, gC.pin, gC.snap, gC.turnedDeg, pads.right.stickX, dt, now, "conversation", gC.serial);
        }
        if (walks && pads.active && frame && view.valid && dt > 0.0) {
            engine::Basis const headAxes = engine::ToBasis(view.head.orientation);
            engine::UnrealVector const forward = engine::Apply(frame->rotation, headAxes.forward);
            double const yawDeg = std::atan2(forward.y, forward.x) * (180.0 / 3.14159265358979323846);
            cw::Vec3 const from{gC.pin.x, gC.pin.y, gC.pin.z};
            cw::PlayerStep const step = cw::StepPlayer(from, {gC.start.x, gC.start.y, gC.start.z}, gSettings.leashUnits,
                pads.left.stickX, pads.left.stickY, yawDeg, gSettings.speedUnitsPerS, dt);
            if (step.moved) {
                std::vector<cw::Vec3> people;
                ue3::Object* const player = gPlayer.Read<ue3::Object*>(gC.controller).value_or(nullptr);
                for (ue3::Object* const actor : Speakers(gC.controller, player)) {
                    if (auto const at = player::Location(actor)) people.push_back({at->x, at->y, at->z});
                }
                cw::ClearStep const clear = cw::KeepClear(from, {step.place.x, step.place.y, from.z}, people, gSettings.clearUnits);
                if (clear.moved) {
                    double const sx = clear.place.x - from.x, sy = clear.place.y - from.y;
                    gC.walkedUnits += std::sqrt(sx * sx + sy * sy);
                    gC.pin.x = static_cast<float>(clear.place.x);
                    gC.pin.y = static_cast<float>(clear.place.y);
                }
            }
        }
        gC.stepMs = now;
    }
    if (gC.placed) RideBase(pawn);  // what Shepard stands on carries the place (ME1's RideBase)
    if (gC.placed) StepBlackMove(now);  // the game's fade to black moves the player with Shepard (ME1's worldBlackMoves)
    if (gC.placed && now > gC.placedMs) {
        // The eyes at the player's own height over the floor under Shepard (ME1's StepEyeHeight).
        double const dt = gC.eyeMs != 0 ? std::clamp(static_cast<double>(now - gC.eyeMs) / 1000.0, 0.0, 0.1) : 0.0;
        StepEyeHeight(pawn, now, dt);
    }
    gC.eyeMs = now;
    gC.held = {gC.pin.x + gC.eyeOffset.x, gC.pin.y + gC.eyeOffset.y, gC.pin.z + gC.eyeOffset.z};
    MarkerOut marker;
    if (gC.placed) {
        // The floor marker (cw::StepMarker): the black-move source above; the player's feet are the pin's floor.
        ue3::Object* const cylinder = gPawnCylinder.Read<ue3::Object*>(pawn).value_or(nullptr);
        auto const half = cylinder != nullptr ? gCylinderHeight.Read<float>(cylinder) : std::nullopt;
        double const h = half && *half > 0.0f && *half < 300.0f ? *half : 90.0;
        cw::MarkerFacts facts;
        facts.scene = true;
        if (gC.blackMarker) facts.blackMove = cw::Vec3{gC.blackMarker->x, gC.blackMarker->y, gC.blackMarker->z - h};
        facts.player = cw::Vec3{gC.pin.x, gC.pin.y, gC.pin.z - h};
        double const dt = gC.markerMs != 0 ? std::clamp(static_cast<double>(now - gC.markerMs) / 1000.0, 0.0, 0.1) : 0.0;
        gC.markerMs = now;
        cw::Marker const m = cw::StepMarker(gC.marker, facts, dt);
        if (m.changed) {
            ++gC.markers;
            log::Info("convo: marker: conversation {}: {} ({:.0f}, {:.0f}, {:.0f}), {:.2f} m from the player", gC.serial, cw::Name(m.need),
                m.at.x, m.at.y, m.at.z, std::hypot(m.at.x - facts.player.x, m.at.y - facts.player.y) / 100.0);
        }
        if (gC.marker.stoodIn && gC.blackMarker) gC.blackMarker.reset();  // reached: the need passes (it fades out)
        if (m.alpha > 0.0) {
            if (auto const markerFrame = HeldFrame()) {
                marker.on = true;
                marker.need = m.need;
                marker.at = engine::ToTracking(*markerFrame, engine::UnrealVector{m.at.x, m.at.y, m.at.z});
                marker.alpha = static_cast<float>(m.alpha);
            }
        }
    }
    Publish(link, controller, marker);
    // Arm IK in the held frame once the host's view is the held one (a head-tracked view; nothing while it is still cinematic).
    auto const frame = HeldFrame();
    if (frame) view_anchor::Hold(frame, vr::host_view::Latest());
    gDrawnEyes = frame ? std::optional<ik::Vec3>{ik::Vec3{frame->headWorld.x, frame->headWorld.y, frame->headWorld.z}} : std::nullopt;
    if (gC.placed) {
        // The looks at the player's drawn eyes (ME1's worldLookEyes), the broken runs' hides, the records from above.
        std::vector<ue3::Object*> const people = Speakers(gC.controller, ConversationPlayer(gC.controller));
        EyesStep(controller, people, gDrawnEyes, "conversation", gC.serial);
        ApplyTestHides(controller, gOwner.Read<ue3::Object*>(gC.controller).value_or(nullptr));
        StanceStep(people);
    }
}

void Shutdown(conversation_world::Link const& link) {
    End(link, "the mod is shutting down");
    engine::conversation_hooks::SetKeepProvider(nullptr);
    engine::conversation_hooks::SetMoveTrackProvider(nullptr);
    gShepardPawn = nullptr;
    gStarted = false;
}

bool InWorld() {
    return gC.on && gC.placed;
}

bool WantsHooks() {
    RefreshSettings();
    return WorldWanted();
}

bool MomentViewOn() {
    return gM.on && !gM.provisional;
}

conversation_world::BodyAwayState BodyAway() {
    // ME1's worldBody = player (research/69 "The body in scenes in the world", "The body that stayed at the mark"): in every
    // conversation shown in the world and in a moment, the body is the player's, slid by the body follower to where the view
    // stands; the legs stand by leg IK while the view rests, and keep the animation's walk while the stick walks it (ME1's
    // worldLegs = stand).
    conversation_world::BodyAwayState away;
    bool const moment = gM.on && !gM.provisional;
    if (!(gC.on && gC.placed) && !moment) return away;
    away.on = true;
    if (moment) {
        auto const at = gShepardPawn != nullptr && ue3::runtime::IsAlive(gShepardPawn) ? player::Location(gShepardPawn) : std::nullopt;
        away.riseUnits = at ? gM.feet.z - at->z : 0.0;
    }
    vr::controllers::State const pads = vr::controllers::Latest();
    bool const walking = pads.active && std::hypot(pads.left.stickX, pads.left.stickY) > 0.2;
    away.legsStand = walking ? 0.0 : 1.0;
    return away;
}

namespace {
// One vtable slot of a live object: the function it holds, as an exe offset, whether it is a function start (.pdata) and its
// first bytes (ME2's placement and interruption are virtuals whose class vtable holds _purecall: the live objects' tell which
// function the game calls; research/le2-conversations-in-world.md section 2). Guarded reads.
bool ReadSlot(void const* object, std::size_t slot, std::uintptr_t* table, std::uintptr_t* function, unsigned char* head) noexcept {
    __try {
        *table = *reinterpret_cast<std::uintptr_t const*>(object);
        *function = reinterpret_cast<std::uintptr_t const*>(*table)[slot / sizeof(void*)];
        for (int i = 0; i < 16; ++i) head[i] = reinterpret_cast<unsigned char const*>(*function)[i];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string SlotOf(char const* what, void const* object, std::size_t slot) {
    if (object == nullptr) return std::format("{}: none", what);
    auto const image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    std::uintptr_t table = 0, function = 0;
    unsigned char head[16] = {};
    if (!ReadSlot(object, slot, &table, &function, head)) return std::format("{}: unreadable", what);
    DWORD64 base = 0;
    auto const* const entry = RtlLookupFunctionEntry(static_cast<DWORD64>(function), &base, nullptr);
    bool const start = entry != nullptr && base + entry->BeginAddress == function;
    std::string bytes;
    for (unsigned char const b : head) bytes += std::format("{:02X} ", b);
    return std::format("{}: vtable exe+0x{:X}, slot 0x{:X} -> exe+0x{:X}{} {}", what, table - image, slot, function - image,
        start ? " (a function start)" : " (not a function start)", bytes);
}
} // namespace

std::string Console(std::string_view args) {
    std::string a(args);
    a.erase(0, a.find_first_not_of(' '));
    if (a == "vtables") {
        ue3::Object* const controller = player::Controller();
        ue3::Object* const world = controller != nullptr ? gActorWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
        ue3::Object* const conversation = controller != nullptr ? LiveConversation(controller) : nullptr;
        return SlotOf("the live conversation's placement", conversation, 0x2B8) + "; " +
            SlotOf("the world info's interruption", world, 0x8D0) + "; the move track hook " +
            (engine::conversation_hooks::MoveTrackInstalled() ? "in" : "not in") + ", the placement hook " +
            (engine::conversation_hooks::Installed() ? "in" : "not in");
    }
    // The records from above and the tests' switches (ME1's console words; research/69 "Stances from above", "Broken runs").
    if (a.rfind("stances", 0) == 0) {
        std::string v = a.substr(7);
        v.erase(0, v.find_first_not_of(' '));
        if (v == "on" || v == "off") gStancesOverride = v == "on";
        if (v == "auto") gStancesOverride.reset();
        RefreshSettings();
        return std::format("conversation world: stances {} ({}); {} written this scene", StancesOn() ? "on" : "off",
            gStancesOverride ? "mevr convo stances" : "[Cinematics] worldStances", gStances.written);
    }
    if (a.rfind("stance", 0) == 0) {
        std::string tag = a.size() > 6 ? a.substr(6) : std::string();
        tag.erase(0, tag.find_first_not_of(' '));
        bool const moment = !gC.on && gM.on && !gM.provisional;
        return WriteStance(tag.empty() ? "asked" : tag, moment);
    }
    if (a.rfind("eyes", 0) == 0) {
        std::string v = a.size() > 4 ? a.substr(4) : std::string();
        v.erase(0, v.find_first_not_of(' '));
        if (v == "on" || v == "off") gEyesOverride = v == "on";
        if (v == "auto") gEyesOverride.reset();
        return std::format("conversation world: eyes stand-in {} ({}); now {} (Shepard's head {:.2f} m from the drawn eyes); {} looks moved, "
                           "{} kept by the game", gEyesOverride.value_or(gSettings.lookEyes) ? "on" : "off",
            gEyesOverride ? "mevr convo eyes" : "[Cinematics] worldLookEyes", gEyesState.on ? "standing in for Shepard's body" : "not needed",
            gEyesState.gapUnits / 100.0, gEyesState.swapped, gEyesState.refused);
    }
    if (a == "people") {
        // What the reader and the world's pawn list give now (a diagnostic): the conversation's people, then every pawn within 20 m.
        ue3::Object* const controller = player::Controller();
        ue3::Object* const conversation = controller != nullptr ? LiveConversation(controller) : nullptr;
        ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
        std::string out = std::format("conversation world: people: the conversation {}", conversation != nullptr ? ue3::runtime::FullName(conversation) : "none");
        if (conversation != nullptr) {
            for (auto const& [label, field] : {std::pair<char const*, ue3::FieldRef*>{"owner", &gOwner}, {"speaker", &gSpeakerNow}, {"listener", &gListenerNow}, {"player", &gPlayer}}) {
                ue3::Object* const o = field->Read<ue3::Object*>(conversation).value_or(nullptr);
                out += std::format("; {} {}{}", label, o != nullptr ? ue3::runtime::FullName(o) : "none", o != nullptr && IsPerson(o) ? " (a person)" : "");
            }
            for (ue3::Object* const o : Speakers(conversation, ConversationPlayer(conversation))) out += "; speaker list: " + ue3::runtime::FullName(o);
        }
        auto const at = pawn != nullptr ? player::Location(pawn) : std::nullopt;
        int n = 0;
        for (ue3::Object* const o : LivePeople(controller)) {
            ++n;
            auto const p = player::Location(o);
            if (!at || !p || std::hypot(p->x - at->x, p->y - at->y) > 2000.0) continue;
            out += std::format("\n  {} person={} hidden={} {:.1f} m", ue3::runtime::FullName(o), IsPerson(o) ? 1 : 0,
                gActorHiddenRead.ReadBool(o).has_value() ? (gActorHiddenRead.ReadBool(o).value() ? 1 : 0) : -1, std::hypot(p->x - at->x, p->y - at->y) / 100.0);
        }
        return out + std::format("\n  {} people in the world (pawns, ME3's stunt actors)", n);
    }
    if (a.rfind("pick", 0) == 0) {
        // A test aid (ME1's `mevr convo pick`): the reply at a position taken as the stick and A take it, the wheel's own
        // SelectConversationEntry on the location that holds it (m_nSlotsUsed[location] == position); only with the wheel up.
        // ME3's spoken-reply commands wait for the host's input lease, which the fake headset never gives (no input focus).
        static ue3::FieldRef slotsUsed{"SFXGame.BioSFHandler_Conversation", "m_nSlotsUsed", ue3::kEveryGame};
        static ue3::FunctionRef select{"SFXGame.BioSFHandler_Conversation.SelectConversationEntry", ue3::kEveryGame};
        int position = -1;
        std::istringstream(a.substr(4)) >> position;
        ue3::Object* const controller = player::Controller();
        ue3::Object* const handler = WheelHandler(controller);
        if (handler == nullptr || !gDisplayingWheel.ReadBool(handler).value_or(false) || select.Get() == nullptr) {
            return "conversation world: no wheel up (mevr convo pick <position> picks only on a wheel)";
        }
        std::string slots;
        for (int location = 0; location < 6; ++location) {
            std::int32_t const* const slot = slotsUsed.At<std::int32_t>(handler, location);
            slots += std::format(" {}", slot != nullptr ? *slot : -9);
            if (slot != nullptr && *slot == position && position >= 0) {
                ue3::Params p(ue3::runtime::Get(), select.Get());
                bool const called = p.Valid() && p.Set("nWheelLocation", static_cast<std::uint8_t>(location)) &&
                    ue3::runtime::Call(handler, select.Get(), p);
                log::Info("conversation world: mevr convo pick {}: SelectConversationEntry({}) called {} (a test's pick)", position, location, called);
                return std::format("conversation world: picked position {} at wheel location {}: {}", position, location, called ? "called" : "not called");
            }
        }
        return std::format("conversation world: no wheel location holds position {} (the locations hold:{})", position, slots);
    }
    if (a.rfind("hide", 0) == 0) {
        std::istringstream words(a.substr(4));
        std::string what, v;
        words >> what >> v;
        bool* const flag = what == "body" ? &gHideBodyTest : what == "person" ? &gHidePersonTest : nullptr;
        if (flag == nullptr || (v != "on" && v != "off")) return "conversation world: mevr convo hide body|person on|off (a broken run)";
        *flag = v == "on";
        return std::format("conversation world: {} {} hidden in a scene held in the world (a broken run: the audit's "
                           "body-not-drawn / person-not-drawn must catch it)", what == "body" ? "the player's body" : "the owner", *flag ? "is" : "is no longer");
    }
    if (a == "on" || a == "off") {
        gOverride = a == "on" ? 1 : 0;
        // Asked for by hand: the conversation now (one a hot reload found mid-way, or the game opened around a load) is held too.
        if (a == "on" && gC.on && gC.skipped) {
            gC.skipped = false;
            log::Info("conversation world: conversation {} held in the world on request (mevr convo world on)", gC.serial);
        }
    } else if (a == "auto") {
        gOverride = -1;
    }
    RefreshSettings();
    engine::conversation_hooks::Stats const hooks = engine::conversation_hooks::Totals();
    return std::format("conversations in the world (the sequels' module, " MEVR_GAME_NAME_A "): {} ({}); people kept {}; near plane kept {} "
                       "({} writes); source hooks: placement {} ({} calls, {} withheld), move track {} ({} calls, {} withheld); this "
                       "session: {} conversations with no place for the player on the 3D screen, moments {} held, {} without a "
                       "track of Shepard's, {} with his pawn hidden, {} not from the player's view{}; now: {}; last: {}",
        WorldWanted() ? "on" : "off", gOverride < 0 ? "[Cinematics] conversations" : "this session's override",
        gSettings.stay ? "on" : "off", gSettings.nearPlane ? "on" : "off", gNearWrites,
        engine::conversation_hooks::Installed() ? "in" : "not in", hooks.placements, hooks.kept,
        engine::conversation_hooks::MoveTrackInstalled() ? "in" : "not in", hooks.trackMoves, hooks.trackKept, gNoPlaceScreens,
        gMomentsHeld, gMomentsNoTrack, gMomentsHidden, gMomentsNotFromView,
        gM.on ? std::format(" (moment {} {}, {:.2f} m walked)", gM.serial, gM.provisional ? "waiting for a track" : "held",
                    gM.walkedUnits / 100.0)
              : std::string(),
        gC.on ? std::format("conversation {} {}", gC.serial,
                    gC.placed ? std::format("held at ({:.0f}, {:.0f}, {:.0f}), {} kept actors, {} undone", gC.held.x, gC.held.y,
                                    gC.held.z, gC.kept.size(), gC.undoneTotal)
                              : "not placed yet: " + gC.why)
              : std::string("none"),
        gLast);
}

// ---- game/ConversationExit's questions (people leaving a conversation; research/conversation-exits.md section 9) ---------------
ue3::Object* ScenePeople(std::vector<ue3::Object*>& people) {
    people.clear();
    ue3::Object* const controller = player::Controller();
    ue3::Object* const conversation = controller != nullptr ? LiveConversation(controller) : nullptr;
    if (conversation == nullptr) return nullptr;
    people = Speakers(conversation, ConversationPlayer(conversation));
    return conversation;
}

void DipFromBlack() {
    ++gCutSerial;  // published with the held scene's state, or on foot with the armed word (PublishArmed)
}

std::uint32_t CutCount() {
    return gCutSerial;
}

} // namespace mevr::game::sequel_conversation_world

#endif  // MEVR_GAME_LE2 || MEVR_GAME_LE3
