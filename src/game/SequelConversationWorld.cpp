// SPDX-License-Identifier: MIT
// ME3's slice of ME1's first-person conversations (game/SequelConversationWorld.hpp).
#include "game/SequelConversationWorld.hpp"

// The sequels (Mod.cpp and ConsoleRequest.cpp call it under MEVR_GAME_LE2 and MEVR_GAME_LE3; ME1 has its own,
// game/ConversationWorld). Each game's reader of the live conversation is below (ME2's BioConversation is ME1's object; ME3's
// is the BioConversationController); everything after it is one code path.
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/ConversationWorld.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/SettingsFile.hpp"
#include "engine/GameAdapter.hpp"
#include "game/DepthOfFieldHold.hpp"
#include "game/HandCalibration.hpp"
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
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
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
ue3::ClassRef gControllerClass{kController, ue3::kEveryGame};
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

struct Settings {
    bool world = false;          // [Cinematics] conversations = world (ME1's key and word)
    bool stay = true;            // [Cinematics] worldStay: people kept where they stand
    double settleS = 0.35;       // the opening's placements settle this long before the player's place is taken
    double followUnits = 80.0;   // a lasting jump of Shepard's head past this moves the place (ME1's FollowHeadSettled)
    // Shepard kept on his mark like the others (ME1's rule: the player's place is never moved by the game; the owner,
    // 2026-11-19: "have Shepard not move around during this scene like the Mass Effect 1 rule set"); then nothing to follow.
    bool shepardStays = true;
    bool dofOff = true;          // [Cinematics] worldDof = 0 keeps it: the conversation's depth of field blurs the room
    bool nearPlane = true;       // [Cinematics] worldNearPlane (ME1's key): the on-foot near plane kept through the director's shots
    double eyeForwardUnits = 8.0;  // the eyes ahead of his head bone (upstream ConvoFp's eyeFwd)
    double eyeUpUnits = 4.0;
    // ME1's walking during a conversation (research/59, worldMove): the left stick moves Shepard and the view inside the
    // boundary around where the conversation began, never into people (worldMoveLeashM 4, worldMoveSpeedMS 1.4,
    // worldMoveClearM 0.7).
    bool move = true;
    double leashUnits = 400.0;
    double speedUnitsPerS = 140.0;
    double clearUnits = 70.0;
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
    engine::UnrealVector headOffset{};  // his head bone from his pawn's place at the opening
    std::uint64_t stepMs = 0;
    double walkedUnits = 0.0;
    unsigned pinned = 0;         // camera updates that put him back on the pin
    engine::UnrealVector head{}; // Shepard's head where the view stands (pin + headOffset)
    ue3::Vector held{};          // the view's place (the POV before the renderer's offsets): PlaceFromHead(head), every tick
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
    ue3::Vector sceneWants{};    // where the stage or a track last wanted Shepard (withheld)
    std::string why;             // why not placed (yet)
    // ME1's sticks (the owner's answer, 2026-11-19): the left stick walks only while the wheel is down and steers it when it is
    // up (latched after a pick until let go: cw::StickWalks); the right stick turns ([Comfort] turn, about the drawn eyes).
    cw::StickLatch latch;
    vr::comfort::SnapTurn snap;
    double turnedDeg = 0.0;
    unsigned wheelHeld = 0;      // ticks the wheel kept the left stick
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
bool WheelUp(ue3::Object* controller) {
    static ue3::FunctionRef getHandler{"SFXGame.SFXGUIInteraction.GetConversationHandler", ue3::kEveryGame};
    static ue3::FieldRef displaying{"SFXGame.BioSFHandler_Conversation", "m_bDisplayingWheel", ue3::kEveryGame};
    auto* const manager = le3_gui::Manager();
    auto* const function = getHandler.Get();
    if (controller == nullptr || manager == nullptr || function == nullptr) return false;
    ue3::Params params(ue3::runtime::Get(), function);
    if (!params.Valid() || !params.Set("oPlayerController", controller) || !ue3::runtime::Call(manager, function, params)) return false;
    auto* const handler = params.Return<ue3::Object*>().value_or(nullptr);
    return handler != nullptr && ue3::runtime::IsAlive(handler) && displaying.ReadBool(handler).value_or(false);
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
    if (!list || list->data == nullptr || list->count <= 0 || list->count > 32) return out;
    for (int i = 0; i < list->count; ++i) {
        ue3::Object* actor = nullptr;
        if (!ReadSpeaker(list->data, i, &actor)) break;
        if (actor != nullptr && actor != player && ue3::runtime::IsAlive(actor) &&
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
bool WheelUp(ue3::Object* controller) {
    static ue3::FieldRef managerField{"Engine.PlayerController", "m_oCachedScaleFormManager", ue3::kEveryGame};
    static ue3::FunctionRef getHandler{"SFXGame.MassEffectGuiManager.GetConversationHandler", ue3::kEveryGame};
    static ue3::FieldRef displaying{"SFXGame.BioSFHandler_Conversation", "m_bDisplayingWheel", ue3::kEveryGame};
    auto* const manager = controller != nullptr ? managerField.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    auto* const function = getHandler.Get();
    if (manager == nullptr || !ue3::runtime::IsAlive(manager) || function == nullptr) return false;
    ue3::Params params(ue3::runtime::Get(), function);
    if (!ue3::runtime::Call(manager, function, params)) return false;
    auto* const handler = params.Return<ue3::Object*>().value_or(nullptr);
    return handler != nullptr && ue3::runtime::IsAlive(handler) && displaying.ReadBool(handler).value_or(false);
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

// The held place from Shepard's head: the eyes a little ahead of the head bone along his facing. ME3's renderer adds the
// player's own head movement (the host view's offsets: 6DOF from the recentre, not a framing offset as LE1's) at the POV.
ue3::Vector PlaceFromHead(engine::UnrealVector const& head, ue3::Rotator const& facing) {
    engine::Basis const basis = engine::ToBasis(engine::UnrealRotation{0, facing.yaw, 0});
    double const f = gSettings.eyeForwardUnits;
    double const u = gSettings.eyeUpUnits;
    return {static_cast<float>(head.x + basis.forward.x * f + basis.up.x * u),
        static_cast<float>(head.y + basis.forward.y * f + basis.up.y * u),
        static_cast<float>(head.z + basis.forward.z * f + basis.up.z * u)};
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

void Publish(conversation_world::Link const& link, ue3::Object* controller) {
    PublishHeld(link, controller, gC.serial, gCutSerial, gC.held, gC.facing, gC.fov);
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

void End(conversation_world::Link const& link, char const* why) {
    if (!gC.on) return;
    gLast = std::format("conversation {}: {} after {:.1f} s; opening {}, walked {:.0f} units, Shepard put back {} times after the "
                        "fact; {}; {} re-placements by lasting jumps; at the source: Shepard's placements {}, people's {}, Shepard's "
                        "track moves {}{}; people kept after the fact: {} undone, {} yielded, {} refused by SetLocation, {} track "
                        "walks left to their tracks",
        gC.serial, why, static_cast<double>(GetTickCount64() - gC.startMs) / 1000.0, cw::Name(gC.opening), gC.walkedUnits, gC.pinned,
        gC.placed ? std::format("held at ({:.0f}, {:.0f}, {:.0f}) yaw {:.1f}", gC.held.x, gC.held.y, gC.held.z,
                        gC.facing.yaw * (360.0 / 65536.0))
                  : "never placed (" + gC.why + ")",
        gC.follows, gC.shepardAtSource, gC.peopleAtSource, gC.trackHeld,
        gC.sceneWantsShepard ? std::format(" (last wanted at ({:.0f}, {:.0f}, {:.0f}), {:.0f} units from the player's place)",
                                   gC.sceneWants.x, gC.sceneWants.y, gC.sceneWants.z, Distance(gC.sceneWants, gC.pin))
                             : std::string(),
        gC.undoneTotal, gC.yieldedTotal, gC.refusedMoves, gC.trackWalks);
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

// Engine.Camera.UpdateCamera, after the game's (before the frame is drawn): the speakers the stage moved since the last update
// are put back (ME1's keep-in-place, game/ConversationWorld's undo in the same handler).
void OnUpdateCamera(ue3::Object* camera, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    if (!(gC.on && gC.placed) && !gMomentHeld) return;
    ue3::Object* const controller = player::Controller();
    if (camera == nullptr || controller == nullptr || player::CameraOwner(camera) != controller) return;
    if (gConstrainAspect.ReadBool(camera).value_or(false) && gConstrainAspect.WriteBool(camera, false)) {
        write_journal::Wrote("ConversationWorld.noLetterbox", camera, 4);
        if (gLetterboxClears++ == 0) {
            log::Info("conversation world: the conversation camera's letterbox (Camera.bConstrainAspectRatio) cleared each "
                      "update while the conversation is held: the full frame, in stereo");
        }
    }
    // ME1's worldNearPlane: the director's near plane never cuts the player's view (the body, the floor, the people near).
    if (gSettings.nearPlane) {
        ue3::Object* const local = gLocalPlayer.Read<ue3::Object*>(controller).value_or(nullptr);
        float* const plane = local != nullptr ? gNearClip.In<float>(local) : nullptr;
        if (plane != nullptr && std::isfinite(*plane) && *plane != gFootNear) {
            if (*plane != gNearReplaced) {
                log::Info("conversation world: the director's near plane is {:.1f} units in this shot: the on-foot plane ({:.1f}) "
                          "kept instead ({} writes so far)", *plane, gFootNear, gNearWrites);
                gNearReplaced = *plane;
            }
            *plane = gFootNear;
            write_journal::Wrote("ConversationWorld.nearClipPlane", plane, sizeof(float));
            ++gNearWrites;
        }
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
            gFootPov = {true, *pov->location, *pov->rotation, *pov->fov};
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
        gC.why = "the opening settling";
        gC.skipped = !gFootSettledAtStart;
        // No place for the player in it (the generated presence table, cw::NoPlaceForPlayer): the 3D screen, by rule.
        std::string const name = ue3::runtime::FullName(ConversationData(conversation));
        bool const noPlace = cw::NoPlaceForPlayer(name, kGameNumber);
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
    auto const head = HeadOf(pawn);
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
        gC.headOffset = {head->x - pawnAt->x, head->y - pawnAt->y, head->z - pawnAt->z};
        gC.facing = ue3::Rotator{0, rotation->yaw, 0};
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
                  "{:+.0f} up{}): the player's place is {}, Shepard kept there; the left stick walks him within {:.0f} units",
            gC.serial, cw::Name(gC.opening), facts.markUnits, facts.riseUnits, facts.known ? "" : "; where he stood isn't known",
            gC.opening == cw::Opening::Cut ? "his mark (another space)" : "where the player stood", gSettings.leashUnits);
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
    gC.head = {gC.pin.x + gC.headOffset.x, gC.pin.y + gC.headOffset.y, gC.pin.z + gC.headOffset.z};
    gC.held = PlaceFromHead(gC.head, gC.facing);
    Publish(link, controller);
    // Arm IK in the held frame once the host's view is the held one (a head-tracked view; nothing while it is still cinematic).
    if (auto const frame = HeldFrame()) view_anchor::Hold(frame, vr::host_view::Latest());
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
