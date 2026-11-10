// SPDX-License-Identifier: MIT
// LE1 conversations in the world, first person: the game's side (game/ConversationWorld.hpp, research/59).
#include "game/ConversationWorld.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/Rigid.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/ConversationHooks.hpp"
#include "game/AuthoredMoment.hpp"
#include "engine/SettingsFile.hpp"
#include "engine/Skeleton.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/ConsoleRequest.hpp"
#include "game/Player.hpp"
#include "game/SquadWeapons.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mevr::game::conversation_world {

namespace {

namespace cw = vr::conversation_world;

constexpr std::uint8_t kModeConversation = 5;
constexpr double kUnitsPerMeter = 100.0;  // research/30: life-size at 100 units per metre
constexpr double kRadToDeg = 57.29577951308232;
constexpr double kRotToDeg = 360.0 / 65536.0;

// The live conversation (SFXGame.BioWorldInfo.m_oCurrentConversation) and what it holds.
ue3::FieldRef gActorWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gCurrentConversation{"SFXGame.BioWorldInfo", "m_oCurrentConversation"};
ue3::FieldRef gSpeaker{"SFXGame.BioConversation", "m_pSpeaker"};
ue3::FieldRef gListener{"SFXGame.BioConversation", "m_pListener"};
ue3::FieldRef gOwner{"SFXGame.BioConversation", "m_pConversationOwner"};
ue3::FieldRef gConvPlayer{"SFXGame.BioConversation", "m_pConversationPlayer"};
ue3::FieldRef gStage{"SFXGame.BioConversation", "m_stage"};
ue3::FieldRef gSpeakerList{"SFXGame.BioConversation", "m_SpeakerList"};
ue3::FieldRef gSpeakerActor{gSpeakerList, "aSpeaker"};
ue3::FieldRef gLookAtList{"SFXGame.BioConversation", "m_LookAtList"};
ue3::FieldRef gLookActor{gLookAtList, "pActor"};
ue3::FieldRef gLookTarget{gLookAtList, "pLookAtTarget"};
ue3::FieldRef gSavedPositions{"SFXGame.BioConversation", "m_aFSPrevPositions"};
ue3::FieldRef gSavedActor{gSavedPositions, "pActor"};
ue3::FieldRef gSavedPos{gSavedPositions, "vPos"};
ue3::FieldRef gSubtitle{"SFXGame.BioConversation", "m_sCurrentSubTitle"};
ue3::FieldRef gCurrentEntry{"SFXGame.BioConversation", "m_nCurrentEntry"};
ue3::FieldRef gUseFixedCameras{"SFXGame.BioConversation", "bUseFixedCameras"};
// The dialogue wheel: the conversation movie's handler on the GUI manager's panels (as game/WorldUi reads them).
ue3::FieldRef gCachedManager{"Engine.PlayerController", "m_oCachedScaleFormManager"};
ue3::FieldRef gPanels{"Engine.BioSFManager", "m_lstPanels"};
ue3::FieldRef gPanelHandler{"Engine.BioSFPanel", "m_DefaultHandler"};
ue3::ClassRef gConvHandlerClass{"SFXGame.BioSFHandler_Conversation"};
ue3::FieldRef gDisplayingWheel{"SFXGame.BioSFHandler_Conversation", "m_bDisplayingWheel"};
ue3::FieldRef gSlotsUsed{"SFXGame.BioSFHandler_Conversation", "m_bSlotsUsed"};  // byte[6]: which wheel locations hold a reply
ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera"};
ue3::FunctionRef gFastTrace{"Engine.Actor.FastTrace"};
ue3::FunctionRef gSelectEntry{"SFXGame.BioSFHandler_Conversation.SelectConversationEntry"};  // mevr convo pick (tests)
ue3::FunctionRef gSkipConversation{"SFXGame.BioSFHandler_Conversation.SkipConversation"};     // mevr convo skip (tests)
// Keeping people where they stand (research/59 "Keeping people in place"): the game's placements are native (no script call or
// event moves them, call trace 2026-11-06), so a jump is undone after it, in the camera update of the same tick, before the draw.
ue3::FunctionRef gSetLocation{"Engine.Actor.SetLocation"};
// The survey's `mevr convo end` (research/59 "The survey"): the conversation's own end, for one that loops past its time.
ue3::FunctionRef gEndConversation{"SFXGame.BioConversation.EndConversation"};
ue3::FunctionRef gSetRotation{"Engine.Actor.SetRotation"};
ue3::FunctionRef gTrace{"Engine.Actor.Trace"};  // the floor under an arc seat (Part B)
ue3::FieldRef gWalkController{"Engine.Pawn", "Controller"};
ue3::FieldRef gWalkVelocity{"Engine.Actor", "Velocity"};
ue3::ClassRef gWalkPawnClass{"Engine.Pawn"};
ue3::ClassRef gWalkAiClass{"SFXGame.BioAiController"};
ue3::FunctionRef gPushMove{"SFXGame.BioAiController.PushMoveToPoint"};
// Terminate only the movement action we pushed, never clear the actor's other AI actions.
ue3::ClassRef gMoveDataClass{"SFXGame.BioSeqData_MoveToPoint"};
ue3::FieldRef gMoveTerminate{"SFXGame.BioSeqData_MoveToPoint", "Terminate"};

// Walking and the level's triggers (research/69 item 5): Actor.SetLocation is UWorld::FarMoveActor, which begins and ends touches
// like any move, so a step may not change which triggers Shepard's cylinder overlaps.
ue3::ClassRef gTriggerClass{"Engine.Trigger"};
ue3::ClassRef gTriggerVolumeClass{"Engine.TriggerVolume"};
ue3::FieldRef gCollisionComponent{"Engine.Actor", "CollisionComponent"};
ue3::FunctionRef gPointCheck{"Engine.Actor.PointCheckComponent"};
ue3::FieldRef gPawnCylinder{"Engine.Pawn", "CylinderComponent"};
ue3::FieldRef gCylinderRadius{"Engine.CylinderComponent", "CollisionRadius"};
ue3::FieldRef gCylinderHeight{"Engine.CylinderComponent", "CollisionHeight"};
// Shepard not in the scene (research/69 item 3).
ue3::FieldRef gActorHidden{"Engine.Actor", "bHidden"};

// ---- Settings ([Cinematics] in LE1MEVR.ini, read every second) -----------------------------------------------------------------
struct Settings {
    cw::Mode mode = cw::Mode::Screen;  // the shipped default (research/52); world is opt-in
    bool follow = true;                // the player's place follows Shepard's head bone when it jumps (worldFollow)
    float subtitleHeightDeg = 4.0f;  // two rows of the line: 3 read small at 2 m (2026-11-06)
    float subtitleBelowM = 0.22f;
    float subtitleTowardM = 0.15f;
    int wheelHand = 2;                 // 0 left, 1 right, 2 in front (the default since the owner's QA, 2026-11-06: at the hand it
                                       // was out of sight with the arm down)
    float wheelWidthM = 0.40f;
    float wheelDistanceM = 0.12f;
    bool stay = true;                  // keep people where they stand: undo the conversation's jumps onto its marks (worldStay)
    bool staySource = true;            // ... and withhold the placements in the game's own placement function (worldStaySource)
    float absentM = 15.0f;             // Shepard this far from the conversation's owner and out of its sight: a cutscene (worldAbsentM, 0 off)
    bool moveTriggers = true;          // the walk never changes which triggers Shepard touches (worldMoveTriggers)
    int autoScreen = 1;                // a scene found at run time to be no place for the player: 0 off, 1 logged only, 2 shown on
                                       // the 3D screen (worldAutoScreen = off | log | on; research/69 item 3)
    int walk = 0;                     // prototype: 0 off, 1 AI (slide fallback), 2 slide (worldWalk)
    float walkSettleS = 2.5f;
    float walkMinM = 0.5f;
    float walkSpeedMS = 1.4f;
    float turnDegPerS = 120.0f;        // ... and turn them toward whom they look at, at most this fast (worldTurnDegPerS)
    bool setPiecesOnScreen = true;     // the scan's set pieces on the 3D screen, not in the world (worldSetPieces = screen | world)
    bool camera = true;                // the game's own camera at the player's place too, not only the main view (worldCamera)
    bool openingAccept = true;         // the stage's opening placements are accepted, the player put at Shepard's mark (worldOpening = accept|keep)
    cw::OpeningRule opening = cw::OpeningRule::Accept;  // worldOpening = accept | keep | auto (research/69: a cut only for another composition)
    bool arrange = true;               // everyone of the conversation (and the squad) brought into the player's view (worldArrange)
    float arcM = 2.5f;                 // the seats' distance from the player (worldArcM)
    float viewDeg = 60.0f;             // half the view's angle a person must be within (worldViewDeg)
    bool move = true;                  // the left stick walks Shepard during the conversation while the wheel is down (worldMove)
    bool moments = false;              // worldMoments = walk | carry: a level-script matinee that walks Shepard is held in first
                                       // person (research/69 "Authored scenes"; default log: measured only)
    bool momentCarry = false;          // ... carry: the track moves him and the view goes with him; walk: its move is withheld,
                                       // he walks it himself
    float momentPatienceS = 12.0f;     // walk: how long the script waits for him where it wants him before he is carried there
    float momentCarryMS = 3.0f;        // the speed he is carried at to where the script wants him (worldMomentCarryMS)
    float moveLeashM = 3.0f;           // how far from the conversation's owner the walk may go (worldMoveLeashM)
    float moveSpeedMS = 1.4f;          // the walk's speed at full deflection (worldMoveSpeedMS)
    float moveClearM = 0.7f;           // how close the walk may come to a person of the conversation or the squad (worldMoveClearM)
    float subtitleCrop[4] = {};
    float wheelCrop[4] = {};
};
Settings gSettings;
std::uint64_t gSettingsMs = 0;
std::string gSettingsText;
std::optional<cw::Mode> gModeOverride;  // `mevr convo world on|off|screen|vr` (tests); not saved

void ReadCrop(ini::File const& file, char const* key, float (&out)[4]) {
    std::fill(std::begin(out), std::end(out), 0.0f);
    if (auto const text = ini::Get(file, "Cinematics", key)) {
        std::istringstream words(*text);
        std::string word;
        for (int i = 0; i < 4 && words >> word; ++i) {
            if (auto const n = ini::Number(word)) {
                out[i] = static_cast<float>(std::clamp(*n, 0.0, 1.0));
            }
        }
    }
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
        s.mode = cw::ParseMode(text->c_str(), cw::Mode::Screen);
    }
    auto number = [&](char const* key, float fallback, float lo, float hi) {
        auto const v = ini::GetNumber(loaded.file, "Cinematics", key);
        return v ? static_cast<float>(std::clamp(*v, static_cast<double>(lo), static_cast<double>(hi))) : fallback;
    };
    s.follow = number("worldFollow", 1.0f, 0.0f, 1.0f) != 0.0f;
    s.subtitleHeightDeg = number("subtitleHeightDeg", s.subtitleHeightDeg, 0.5f, 20.0f);
    s.subtitleBelowM = number("subtitleBelowM", s.subtitleBelowM, 0.0f, 1.0f);
    s.subtitleTowardM = number("subtitleTowardM", s.subtitleTowardM, 0.0f, 1.0f);
    if (auto const text = ini::Get(loaded.file, "Cinematics", "wheelHand")) {
        std::string const w = ini::Lower(ini::Trim(*text));
        s.wheelHand = w == "right" || w == "1" ? 1 : w == "left" || w == "0" ? 0 : 2;
    }
    s.wheelWidthM = number("wheelWidthM", s.wheelWidthM, 0.15f, 1.5f);
    s.wheelDistanceM = number("wheelDistanceM", s.wheelDistanceM, 0.05f, 0.8f);
    s.stay = number("worldStay", 1.0f, 0.0f, 1.0f) != 0.0f;
    s.walk = static_cast<int>(number("worldWalk", 0.0f, 0.0f, 2.0f));
    s.walkSettleS = number("worldWalkSettleS", s.walkSettleS, 0.0f, 60.0f);
    s.walkMinM = number("worldWalkMinM", s.walkMinM, 0.0f, 6.0f);
    s.walkSpeedMS = number("worldWalkSpeedMS", s.walkSpeedMS, 0.1f, 3.0f);
    s.turnDegPerS = number("worldTurnDegPerS", s.turnDegPerS, 10.0f, 1000.0f);
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldSetPieces")) {
        s.setPiecesOnScreen = ini::Lower(ini::Trim(*text)) != "world";
    }
    s.camera = number("worldCamera", 1.0f, 0.0f, 1.0f) != 0.0f;
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldOpening")) {
        s.opening = cw::ParseOpeningRule(*text, cw::OpeningRule::Accept);
        s.openingAccept = s.opening != cw::OpeningRule::Keep;  // accept and auto let the stage's first placements stand, then decide
    }
    s.arrange = number("worldArrange", 1.0f, 0.0f, 1.0f) != 0.0f;
    s.arcM = number("worldArcM", s.arcM, 1.2f, 6.0f);
    s.viewDeg = number("worldViewDeg", s.viewDeg, 20.0f, 90.0f);
    s.move = number("worldMove", 1.0f, 0.0f, 1.0f) != 0.0f;
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldMoments")) {
        std::string const w = ini::Lower(ini::Trim(*text));
        s.moments = w == "walk" || w == "carry";
        s.momentCarry = w == "carry";
    }
    s.momentPatienceS = number("worldMomentPatienceS", 12.0f, 0.0f, 120.0f);
    s.momentCarryMS = number("worldMomentCarryMS", 3.0f, 0.5f, 10.0f);
    s.moveLeashM = number("worldMoveLeashM", s.moveLeashM, 0.0f, 20.0f);
    s.moveSpeedMS = number("worldMoveSpeedMS", s.moveSpeedMS, 0.2f, 4.0f);
    s.moveClearM = number("worldMoveClearM", s.moveClearM, 0.0f, 3.0f);
    s.staySource = number("worldStaySource", 1.0f, 0.0f, 1.0f) != 0.0f;
    s.absentM = number("worldAbsentM", s.absentM, 0.0f, 200.0f);
    s.moveTriggers = number("worldMoveTriggers", 1.0f, 0.0f, 1.0f) != 0.0f;
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldAutoScreen")) {
        std::string const v = ini::Lower(ini::Trim(*text));
        s.autoScreen = v == "off" || v == "0" ? 0 : v == "on" || v == "2" ? 2 : 1;
    }
    ReadCrop(loaded.file, "subtitleCrop", s.subtitleCrop);
    ReadCrop(loaded.file, "wheelCrop", s.wheelCrop);
    std::string const text = std::format("conversations = {}, worldFollow = {}, subtitles {:.1f} deg tall {:.2f} m under the face, the wheel "
                                         "{} {:.2f} m wide, set pieces {}, the game's camera {}, worldWalk = {} (settle {:.2f} s, "
                                         "min {:.2f} m, speed {:.2f} m/s), the opening {}, arrange {} ({:.1f} m, {:.0f} deg), the player's walk {}, "
                                         "placements {}, Shepard absent beyond {:.0f} m, worldAutoScreen = {}",
        cw::Name(s.mode), s.follow ? 1 : 0,
        s.subtitleHeightDeg, s.subtitleBelowM, s.wheelHand == 0 ? "at the left hand" : s.wheelHand == 1 ? "at the right hand" : "in front",
        s.wheelWidthM, s.setPiecesOnScreen ? "on the 3D screen" : "in the world", s.camera ? "at the player's place" : "the director's",
        s.walk, s.walkSettleS, s.walkMinM, s.walkSpeedMS, s.openingAccept ? "accepted" : "kept", s.arrange ? "on" : "off", s.arcM, s.viewDeg,
        s.move ? std::format("on (leash {:.1f} m, {:.2f} m/s, {:.2f} m clear of people, triggers {})", s.moveLeashM, s.moveSpeedMS, s.moveClearM,
                     s.moveTriggers ? "kept" : "free") : std::string("off"),
        s.staySource ? "withheld at the source" : "undone after the fact", s.absentM,
        s.autoScreen == 0 ? "off" : s.autoScreen == 2 ? "on" : "log");
    if (text != gSettingsText) {
        gSettingsText = text;
        log::Info("convo: [Cinematics] {}", text);
    }
    gSettings = s;
}

cw::Mode CurrentMode() {
    return gModeOverride.value_or(gSettings.mode);
}

// ---- Geometry helpers --------------------------------------------------------------------------------------------------------
ik::Vec3 V(ue3::Vector const& v) {
    return {v.x, v.y, v.z};
}
double Dist(ik::Vec3 a, ik::Vec3 b) {
    return ik::Length(a - b);
}
ik::Vec3 Forward(ue3::Rotator const& r) {
    double const pitch = r.pitch * kRotToDeg / kRadToDeg;
    double const yaw = r.yaw * kRotToDeg / kRadToDeg;
    return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
}
double YawDeg(ue3::Rotator const& r) {
    double y = std::fmod(r.yaw * kRotToDeg, 360.0);
    return y < 0.0 ? y + 360.0 : y;
}
double AngleDeg(ik::Vec3 a, ik::Vec3 b) {
    double const la = ik::Length(a);
    double const lb = ik::Length(b);
    if (!(la > 1e-6) || !(lb > 1e-6)) {
        return 0.0;
    }
    return std::acos(std::clamp(ik::Dot(a, b) / (la * lb), -1.0, 1.0)) * kRadToDeg;
}
double YawDiffDeg(double a, double b) {
    double d = std::fmod(a - b + 540.0, 360.0) - 180.0;
    return d;
}
std::string F(ik::Vec3 const& v) {
    return std::format("({:.0f}, {:.0f}, {:.0f})", v.x, v.y, v.z);
}

std::string ShortName(ue3::Object const* o) {
    if (o == nullptr) {
        return "none";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    return r.NameText(o);
}

// The head bone of a pawn's mesh: its world place and axes (cached per skeletal mesh asset).
struct Head {
    ik::Vec3 at;
    ik::Vec3 axis[3];
};
std::map<ue3::Object const*, int> gHeadBones;  // skeletal mesh asset -> its head bone (-1: none)

std::optional<Head> HeadOf(ue3::Object* actor) {
    if (actor == nullptr || !player::IsBioPawn(actor) || !ue3::runtime::IsAlive(actor)) {
        return std::nullopt;
    }
    ue3::Object* const mesh = player::Mesh(actor);
    ue3::Object* const asset = player::SkeletalMesh(mesh);
    if (asset == nullptr) {
        return std::nullopt;
    }
    auto it = gHeadBones.find(asset);
    if (it == gHeadBones.end()) {
        int index = -1;
        if (auto const skeleton = engine::skeleton::ReadRefSkeleton(mesh)) {
            for (std::size_t i = 0; i < skeleton->names.size() && index < 0; ++i) {
                if (ini::Lower(skeleton->names[i]) == "head") {
                    index = static_cast<int>(i);
                }
            }
        }
        it = gHeadBones.emplace(asset, index).first;
    }
    int const bone = it->second;
    auto const bases = engine::skeleton::SpaceBases(mesh);
    if (bone < 0 || static_cast<std::size_t>(bone) >= bases.size()) {
        return std::nullopt;
    }
    ik::Affine const toWorld = engine::skeleton::LocalToWorld(mesh);
    ik::Transform const t = engine::skeleton::ToTransform(bases[static_cast<std::size_t>(bone)]);
    Head h;
    h.at = ik::TransformPoint(toWorld, t.translation);
    ik::Vec3 const units[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int a = 0; a < 3; ++a) {
        h.axis[a] = ik::Normalize(ik::TransformDirection(toWorld, ik::Rotate(t.rotation, units[a])), units[a]);
    }
    return h;
}

// An actor's place for the logs: its head if it has one, else its location.
std::optional<ik::Vec3> FacePoint(ue3::Object* actor) {
    if (auto const head = HeadOf(actor)) {
        return head->at;
    }
    if (auto const at = actor != nullptr ? player::Location(actor) : std::nullopt) {
        return V(*at);
    }
    return std::nullopt;
}

bool LineOfSight(ue3::Object* from, ik::Vec3 start, ik::Vec3 end) {
    if (from == nullptr || gFastTrace.Get() == nullptr) {
        return true;
    }
    ue3::Params trace(ue3::runtime::Get(), gFastTrace.Get());
    trace.Set("TraceEnd", ue3::Vector{static_cast<float>(end.x), static_cast<float>(end.y), static_cast<float>(end.z)});
    trace.Set("TraceStart", ue3::Vector{static_cast<float>(start.x), static_cast<float>(start.y), static_cast<float>(start.z)});
    ue3::runtime::Call(from, gFastTrace.Get(), trace);
    return trace.ReturnBool().value_or(true);
}

// ---- The on-foot view and the POV (Engine.Camera.UpdateCamera, after the game's) ----------------------------------------------
struct Foot {
    bool have = false;
    ue3::Vector location{};
    ue3::Rotator rotation{};
    float fov = 90.0f;
    ue3::Vector pawn{};                              // Shepard's pawn at that camera update
    std::optional<engine::TrackingFrame> frame;      // the room as that view maps it
    vr::host_view::State floorView;
    std::uint64_t ms = 0;                            // when
    // The first-person eye from Shepard's head bone, in his yaw frame (forward, right, up; Unreal units): arm IK's log of the first
    // person camera against the head bone with the headset at the recenter point (2026-11-06: forward 8.9, right 1.7, up 29.3). A
    // measure on foot picks up body follow's lean (0.59 m from the head bone, 2026-11-06), so the constant.
    ik::Vec3 eyeFromHead{8.9, 1.7, 29.3};
};
Foot gFoot;

ik::Vec3 YawForward(double yawDeg) {
    double const y = yawDeg / kRadToDeg;
    return {std::cos(y), std::sin(y), 0.0};
}
ik::Vec3 YawRight(double yawDeg) {
    double const y = yawDeg / kRadToDeg;
    return {-std::sin(y), std::cos(y), 0.0};
}
struct PovAt {
    float* location = nullptr;
    ue3::Rotator* rotation = nullptr;
    float* fov = nullptr;
    std::uint64_t ms = 0;
    ue3::Vector lastLocation{};  // the director's camera as the game left it (the logs' shots)
    ue3::Rotator lastRotation{};
    float lastFov = 0.0f;
};
PovAt gPov;

void KeepInPlace(ue3::Object* pawn);

// The game's own camera at the player's place (worldCamera; the owner's QA, 2026-11-06): the host's swap held the main view's build
// only, so the engine's culling and texture streaming still followed the director's camera (in the comm room 5-9 m away behind
// Nihlus with a 10-degree lens: assets missing or blurred from where the player stands). Written into the camera's POV right after
// the game computes it each tick (the director's own POV is read first, for the measurements); set by the tick while the world
// view is up.
struct CameraHold {
    bool on = false;
    ue3::Vector location{};
    ue3::Rotator rotation{};
    float fov = 90.0f;
    std::uint64_t writes = 0;
};
CameraHold gCameraHold;

void OnUpdateCamera(ue3::Object* camera, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    ue3::Object* const controller = player::Controller();
    if (camera == nullptr || controller == nullptr || player::CameraOwner(camera) != controller) {
        return;
    }
    auto const pov = player::CameraPov(camera);
    if (!pov || pov->location == nullptr || pov->rotation == nullptr || pov->fov == nullptr) {
        return;
    }
    auto const mode = player::GameMode(controller);
    if (mode && *mode == 0) {
        gFoot.location = *pov->location;
        gFoot.rotation = *pov->rotation;
        // Store the corrected base, not the raw animated camera. Common framing is still applied by the held render.
        auto const view = vr::host_view::Latest();
        auto const basis = engine::ToBasis(engine::UnrealRotation{gFoot.rotation.pitch, gFoot.rotation.yaw, gFoot.rotation.roll});
        gFoot.location.z = static_cast<float>(vr::host_view::HoldBaseZ(view, pov->location->z,
            basis.right.z, basis.up.z, basis.forward.z));
        gFoot.fov = *pov->fov;
        if (auto const p = player::Location(player::Pawn(controller))) {
            gFoot.pawn = *p;
        }
        gFoot.have = true;
        gFoot.ms = GetTickCount64();
        return;
    }
    if (mode && *mode == kModeConversation) {
        gPov.location = reinterpret_cast<float*>(pov->location);
        gPov.rotation = pov->rotation;
        gPov.fov = pov->fov;
        gPov.ms = GetTickCount64();
        gPov.lastLocation = *pov->location;
        gPov.lastRotation = *pov->rotation;
        gPov.lastFov = *pov->fov;
        KeepInPlace(player::Pawn(controller));  // after the conversation's placements of this tick, before its draw
        if (gCameraHold.on) {
            *pov->location = gCameraHold.location;
            *pov->rotation = gCameraHold.rotation;
            *pov->fov = gCameraHold.fov;
            write_journal::Wrote("ConversationWorld.cameraPov", pov->location, sizeof(ue3::Vector));
            if (gCameraHold.writes++ == 0) {
                log::Info("convo: the game's camera held at the player's place too (culling, streaming, sound follow it)");
            }
        }
    }
}

// ---- One conversation, measured ------------------------------------------------------------------------------------------------
struct Tracked {
    ue3::Object* actor = nullptr;
    std::string name;
    ik::Vec3 start;
    double startYaw = 0.0;
    ik::Vec3 logged;    // the place last logged
    double loggedYaw = 0.0;
    double maxMove = 0.0;
    double maxTurn = 0.0;
    std::optional<ik::Vec3> headStart;  // its head bone at the start
    double headMax = 0.0;               // the head bone's largest move from there
};

struct Convo {
    bool on = false;
    ue3::Object* object = nullptr;
    ue3::Object* playerPawn = nullptr;  // retain Shepard's identity even if possession temporarily changes
    std::string name;
    std::uint64_t startMs = 0;
    std::uint32_t number = 0;
    bool world = false;          // shown in the world
    bool setPiece = false;       // one of the scan's set pieces: on the 3D screen instead (worldSetPieces)
    // The opening (research/59 "Part A"): the stage's first placements are the scene's design and the player hasn't seen the room
    // in this conversation yet, so they are accepted until the first drawn frame of the world view (or openingUntilMs), then the
    // player's place is taken from Shepard's mark and everyone is brought into view (Part B), under a dip from black (`cut`).
    std::uint64_t openingUntilMs = 0;
    bool openingDone = false;
    int openingMoves = 0;        // placements accepted during the opening
    cw::Opening opening = cw::Opening::Cut;  // how the opening ended (the summary's "opening")
    double markM = 0.0;          // how far Shepard's mark was from where the player stood
    std::optional<ik::Vec3> wantedMark;  // a walk-in: Shepard's mark, where the player is wanted (the walk's leash is around it)
    std::uint64_t settleUntilMs = 0;     // after an opening without a cut: his mesh catches up with the pawn; no jump is read into it
    int arranged = 0;            // people moved to a seat in view
    std::uint32_t cut = 0;       // the host dips from black when it changes
    // The player's place for this conversation: the view on foot if it was Shepard's just now, else his eyes from his head bone
    // (a conversation right after a level load or a cutscene); it follows his head bone when that jumps (a stage's placement).
    ik::Vec3 hold;
    ue3::Rotator holdRotation{};
    float holdFov = 90.0f;
    char const* holdFrom = "";
    std::optional<ik::Vec3> anchorHead;  // Shepard's head bone where the hold was last placed
    vr::conversation_world::FollowState follow;  // the hold and its anchor, for FollowHeadSettled
    int reanchors = 0;
    int lines = 0;      // lines spoken (the subtitle field changed to a new text)
    std::map<std::string, int> speakers;  // who spoke, how often
    bool stage = false;
    int kept = 0;       // jumps undone (people kept where they stood)
    int walks = 0;      // walks started (including AI calls that fell back to slide)
    int letGo = 0;      // jumps too long to undo (a scripted entrance)
    std::vector<Tracked> actors;
    ue3::Object* speaker = nullptr;
    std::uint32_t speakerSerial = 0;
    std::string subtitle;
    std::string lookAts;
    int entry = -1;
    std::vector<int> entryPath;        // the entries it passed through, in order (the summary's "entries": the sweep's judge)
    // Shots: each cut of the director's camera.
    int shots = 0;
    ue3::Vector shotLocation{};
    ue3::Rotator shotRotation{};
    bool haveShot = false;
    std::map<std::string, int> shotKinds;
    int shotsHidden = 0;   // shots whose subject can't be seen from Shepard's eyes
    int shotsFar = 0;      // shots framed from far off (the camera past kFarCameraM from Shepard's eyes): a set piece's framing
    double cameraFarM = 0; // the farthest any shot's camera stood from Shepard's eyes
    int wheelOpenings = 0;
    bool wheel = false;
    double eyeToViewMax = 0.0;  // Shepard's head bone from the held view's eyes (the player's), largest
    // The player's walk (research/59 "Walking during conversations"): the left stick moves Shepard's pawn and the held view
    // together, inside a leash around the owner, while the wheel is down.
    double movedUnits = 0.0;    // walked, summed
    int moveSteps = 0;          // ticks that moved
    int leashHits = 0;          // ticks the leash held the step back
    int clearHits = 0;          // ticks a person in the way held the step (worldMoveClearM)
    int triggerHits = 0;        // steps refused because they would begin or end a trigger's touch (worldMoveTriggers)
    std::string triggerName;    // the last such trigger
    // Placements withheld in the game's own placement function (engine/ConversationHooks; research/69 item 4).
    int sourceKept = 0;
    double sourceFarUnits = 0.0;       // the farthest mark a person was not put on
    bool sourceLogged = false;
    std::string absentWhy;             // why this scene is no place for the player, found at run time (research/69 item 3)
    bool autoScreened = false;         // ... and it was moved to the 3D screen for it (worldAutoScreen = on)
    int moveRefused = 0;        // steps refused: no floor, a ledge, a wall, SetLocation
    bool moving = false;        // a velocity is set on the pawn
    bool moveLogged = false;    // the first step logged
};
Convo gC;

// `mevr convo walk <x> <y> [seconds]` (tests without controllers, a flat game included): the stick the walk reads until the time
// is up; the head's facing is the held view's yaw when the host has no VR view.
struct WalkOverride {
    float x = 0.0f;
    float y = 0.0f;
    std::uint64_t untilMs = 0;
};
WalkOverride gWalkOverride;

// Where each person of the conversation is kept, and the yaw shown while they turn.
struct Stay {
    ue3::Object* actor = nullptr;
    std::string name;
    ik::Vec3 place;
    double shownYaw = 0.0;
    int kept = 0;
    cw::MarkWalk walk;
    int method = 0;               // the method chosen for this walk (1 AI, 2 slide)
    int requestedMethod = 0;      // detect a live setting change even after falling back to slide
    ue3::Object* moveAction = nullptr;
    double deadlineS = 0.0;
    double speedUnitsPerS = 0.0;
};
std::vector<Stay> gStay;
std::uint64_t gStayMs = 0;
bool gInWorldForStay = false;  // set by Tick: the world view is up

// The game's own placement function asks (engine/ConversationHooks; research/69 item 4): after the accepted opening a person of
// this conversation is not put on a stage mark; the call's facing still applies. Called on the game thread from inside the
// conversation's code: it reads the mod's own state and nothing of the engine. Not with the walk-to-marks prototype, which
// learns each mark from the jump.
bool KeepAtSource(void const* conversation, void const* actor, float const* location) {
    if (!gC.on || !gC.world || !gInWorldForStay || !gSettings.stay || !gSettings.staySource || gSettings.walk != 0 || !gC.openingDone ||
        conversation != gC.object) {
        return false;
    }
    for (Stay const& st : gStay) {
        if (st.actor == actor) {
            ++gC.sourceKept;
            gC.sourceFarUnits = std::max(gC.sourceFarUnits, std::hypot(location[0] - st.place.x, location[1] - st.place.y));
            return true;
        }
    }
    return false;
}

constexpr double kJumpUnits = 30.0;      // in one frame: a run moves under 3 units a frame at 90 Hz, a placement metres
constexpr double kStayMaxUnits = 600.0;  // farther: a scripted entrance, let through

int ToUnits(double degrees) {
    return static_cast<int>(std::lround(degrees * 65536.0 / 360.0));
}

double Seconds();

cw::Vec3 WalkPoint(ik::Vec3 v) {
    return {v.x, v.y, v.z};
}
ik::Vec3 WalkPoint(cw::Vec3 v) {
    return {v.x, v.y, v.z};
}
ue3::Vector EnginePoint(ik::Vec3 v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

// ---- The walk and the level's triggers (research/69 item 5) -------------------------------------------------------------------
// Actor.SetLocation is UWorld::FarMoveActor (0x3FFCE0), which, for an actor that collides, finds the actors it now touches
// and tells both sides, as any move does: a step into a trigger volume would fire its Kismet touch (a scene that then can't
// start during this conversation and may never fire again), a step out of a streaming volume would stream its area out. So
// a step may not change which triggers Shepard's cylinder overlaps: the triggers near the conversation are gathered (every
// Engine.Trigger and TriggerVolume whose collision is within the leash) and each step asks the engine's own point check,
// Actor.PointCheckComponent with the cylinder as a box, at both ends.
struct TriggerNear {
    ue3::Object* actor = nullptr;
    ue3::Object* component = nullptr;
    std::string name;
};
std::vector<TriggerNear> gTriggers;
std::uint64_t gTriggersMs = 0;
std::uint32_t gTriggersFor = 0;  // the conversation (its number) they were gathered for

bool Overlaps(TriggerNear const& t, ik::Vec3 at, ik::Vec3 extent) {
    ue3::Params p(ue3::runtime::Get(), gPointCheck.Get());
    if (!p.Valid() || !p.Set("InComponent", t.component) ||
        !p.Set("PointLocation", ue3::Vector{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)}) ||
        !p.Set("PointExtent", ue3::Vector{static_cast<float>(extent.x), static_cast<float>(extent.y), static_cast<float>(extent.z)})) {
        return false;
    }
    ue3::runtime::Call(t.actor, gPointCheck.Get(), p);
    return p.ReturnBool().value_or(false);
}

void GatherTriggers(ik::Vec3 anchor) {
    gTriggers.clear();
    if (gPointCheck.Get() == nullptr || gCollisionComponent.Get() == nullptr) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    double const reach = gSettings.moveLeashM * kUnitsPerMeter + 300.0;
    ik::Vec3 const box{reach, reach, 400.0};
    std::int32_t const n = r.ObjectCount();
    for (std::int32_t i = 0; i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !(ue3::IsA(o, gTriggerClass) || ue3::IsA(o, gTriggerVolumeClass)) ||
            r.NameText(o).rfind("Default__", 0) == 0) {
            continue;
        }
        ue3::Object* const component = gCollisionComponent.Read<ue3::Object*>(o).value_or(nullptr);
        if (component == nullptr) {
            continue;
        }
        TriggerNear t{o, component, ShortName(o)};
        if (Overlaps(t, anchor, box)) {  // a component not in the world (its level hidden) answers no
            gTriggers.push_back(std::move(t));
        }
    }
}

// True if the step from `from` to `to` begins or ends a trigger's touch; `which` and `enters` say which and how.
bool StepChangesTouch(ue3::Object* pawn, ik::Vec3 anchor, ik::Vec3 from, ik::Vec3 to, std::string& which, bool& enters) {
    std::uint64_t const now = GetTickCount64();
    if (gTriggersFor != gC.number || now - gTriggersMs > 5000) {
        bool const first = gTriggersFor != gC.number;
        GatherTriggers(anchor);
        gTriggersFor = gC.number;
        gTriggersMs = now;
        if (first) {
            std::string names;
            for (TriggerNear const& t : gTriggers) {
                names += (names.empty() ? "" : ", ") + t.name;
            }
            log::Info("convo: the walk's triggers: {} within the leash of the owner{}{}", gTriggers.size(), names.empty() ? "" : ": ", names);
        }
    }
    ue3::Object* const cylinder = gPawnCylinder.Read<ue3::Object*>(pawn).value_or(nullptr);
    double const radius = cylinder != nullptr ? gCylinderRadius.Read<float>(cylinder).value_or(34.0f) : 34.0f;
    double const half = cylinder != nullptr ? gCylinderHeight.Read<float>(cylinder).value_or(78.0f) : 78.0f;
    ik::Vec3 const extent{radius, radius, half};
    for (TriggerNear const& t : gTriggers) {
        if (!ue3::runtime::IsAlive(t.actor)) {
            continue;
        }
        bool const before = Overlaps(t, from, extent);
        bool const after = Overlaps(t, to, extent);
        if (before != after) {
            which = t.name;
            enters = after;
            return true;
        }
    }
    return false;
}

bool PutAt(ue3::Object* actor, ik::Vec3 point) {
    ue3::Params p(ue3::runtime::Get(), gSetLocation.Get());
    return p.Valid() && p.Set("NewLocation", EnginePoint(point)) && ue3::runtime::Call(actor, gSetLocation.Get(), p) &&
           p.ReturnBool().value_or(false);
}

bool WalkVelocity(ue3::Object* actor, ik::Vec3 velocity) {
    ue3::Vector* const out = gWalkVelocity.In<ue3::Vector>(actor);
    if (out == nullptr) {
        return false;  // the handle logs a missing field; the caller stands the walk down
    }
    *out = EnginePoint(velocity);
    write_journal::Wrote("ConversationWorld.walkVelocity", out, sizeof(*out));
    return true;
}

void EndWalk(Stay& st, char const* event, char const* why) {
    if (st.walk.Phase() == cw::WalkPhase::Idle) {
        return;
    }
    bool const running = st.walk.Phase() == cw::WalkPhase::Walking;
    if (st.moveAction != nullptr && ue3::runtime::IsAlive(st.moveAction) && ue3::IsA(st.moveAction, gMoveDataClass)) {
        if (!gMoveTerminate.WriteBool(st.moveAction, true)) {  // FieldRef journals the bitfield write
            log::Warn("convo: walk: could not terminate {}'s AI move", st.name);
        }
    }
    if (running && ue3::runtime::IsAlive(st.actor)) {
        WalkVelocity(st.actor, {});
        if (auto const at = player::Location(st.actor)) {
            st.place = V(*at);
        }
    }
    log::Info("convo: walk: {} {} at {} ({}, {})", event, st.name, F(st.place),
        running ? (st.method == 1 ? "ai" : "slide") : "pending", why);
    st.moveAction = nullptr;
    st.walk.End();
}

void EndWalks(char const* why) {
    for (Stay& st : gStay) {
        EndWalk(st, "cancel", why);
    }
}

// Returns the reason to use slide, or nullptr for a successful AI request. Game thread (camera post-handler) only.
char const* StartAiWalk(Stay& st) {
    if (gWalkPawnClass.Get() == nullptr || !ue3::IsA(st.actor, gWalkPawnClass)) return "actor is not an Engine.Pawn";
    if (gWalkController.Get() == nullptr) return "Engine.Pawn.Controller missing";
    ue3::Object* const ai = gWalkController.Read<ue3::Object*>(st.actor).value_or(nullptr);
    if (ai == nullptr || !ue3::runtime::IsAlive(ai)) return "no live controller";
    if (gWalkAiClass.Get() == nullptr || !ue3::IsA(ai, gWalkAiClass)) return "controller is not a BioAiController";
    ue3::Function* const fn = gPushMove.Get();
    if (fn == nullptr) return "PushMoveToPoint missing";
    ue3::Reflection& r = ue3::runtime::Get();
    if (r.FindFunction(r.ClassOf(ai), "PushMoveToPoint") != fn) return "controller class has no matching PushMoveToPoint";
    if (gMoveDataClass.Get() == nullptr || gMoveTerminate.Get() == nullptr) return "AI move termination reflection missing";
    ue3::Params p(r, fn);
    if (!p.Valid() || !p.Set("vLoc", EnginePoint(WalkPoint(st.walk.Wanted()))) || !p.Set("PointTolerance", 10.0f) ||
        !p.SetBool("bWalking", true) || !p.SetBool("bRequiresPrecisionMovement", true) || p.ReturnSpec() == nullptr ||
        p.ReturnSpec()->size != static_cast<std::int32_t>(sizeof(ue3::Object*))) return "PushMoveToPoint parameter lookup failed";
    if (!ue3::runtime::Call(ai, fn, p)) return "PushMoveToPoint call failed";
    st.moveAction = p.Return<ue3::Object*>().value_or(nullptr);
    if (st.moveAction == nullptr || !ue3::runtime::IsAlive(st.moveAction)) {
        st.moveAction = nullptr;
        return "PushMoveToPoint returned no live move action";
    }
    if (!ue3::IsA(st.moveAction, gMoveDataClass)) {
        st.moveAction = nullptr;
        return "PushMoveToPoint returned an unexpected action class";
    }
    return nullptr;
}

// Advances a walk and returns whether its travel heading owns the turn this frame.
bool WalkToMark(Stay& st, ik::Vec3 previousKept, double nowS, double dt, double& heading) {
    if (st.walk.Phase() == cw::WalkPhase::Waiting) {
        // Both methods need to zero velocity on exit and face the travel direction. Missing reflection leaves normal stay on.
        if (gWalkVelocity.In<ue3::Vector>(st.actor) == nullptr || gSetRotation.Get() == nullptr) {
            EndWalk(st, "end", "walk reflection missing; keeping in place");
            return false;
        }
        if (st.walk.StartIfSettled(WalkPoint(st.place), nowS, gSettings.walkSettleS, gSettings.walkMinM * kUnitsPerMeter)) {
            previousKept = st.place;  // start from the actor's actual place when the wait finishes
            st.method = st.requestedMethod = gSettings.walk;
            st.speedUnitsPerS = gSettings.walkSpeedMS * kUnitsPerMeter;
            double const distance = Dist(st.place, WalkPoint(st.walk.Wanted()));
            st.deadlineS = nowS + distance / st.speedUnitsPerS + 3.0;
            if (st.method == 1) {
                if (char const* why = StartAiWalk(st)) {
                    st.method = 2;
                    log::Info("convo: walk: {} ai unavailable: {}; falling back to slide for this walk", st.name, why);
                }
            }
            ++gC.walks;
            log::Info("convo: walk: start {} from {} to {}, {:.2f} m, method {}", st.name, F(st.place),
                F(WalkPoint(st.walk.Wanted())), distance / kUnitsPerMeter, st.method == 1 ? "ai" : "slide");
        } else if (st.walk.Phase() == cw::WalkPhase::Idle) {
            log::Info("convo: walk: cancel {} pending mark: already within worldWalkMinM", st.name);
        }
    }
    if (st.walk.Phase() != cw::WalkPhase::Walking) return false;
    ik::Vec3 const mark = WalkPoint(st.walk.Wanted());
    ik::Vec3 const travel = mark - st.place;
    if (Dist(st.place, mark) <= (st.method == 1 ? 10.0 : 0.01)) {
        // The AI has a small point tolerance; settle its last centimetres onto the exact mark if collision permits it.
        if (PutAt(st.actor, mark)) {
            auto const actual = player::Location(st.actor);
            if (actual) st.place = V(*actual);
            if (Dist(st.place, mark) <= 0.01) {
                EndWalk(st, "arrival", "reached the mark");
                return false;
            }
        }
    }
    if (nowS >= st.deadlineS) {
        EndWalk(st, "end", "distance / speed + 3 s elapsed; staying here");
        return false;
    }
    if (std::hypot(travel.x, travel.y) > 0.01) heading = std::atan2(travel.y, travel.x) * kRadToDeg;
    if (st.method == 2 && dt > 0.0) {
        // Velocity can also be integrated by the engine between camera updates. Step from the previous DRAWN/kept place,
        // so that engine motion and our SetLocation together advance one speed * dt, rather than doubling the slide speed.
        ik::Vec3 const before = previousKept;
        cw::WalkStep const step = cw::StepTowardMark(WalkPoint(before), st.walk.Wanted(), st.speedUnitsPerS, dt);
        bool const moved = PutAt(st.actor, WalkPoint(step.place));
        auto const actual = player::Location(st.actor);
        if (!actual) {
            EndWalk(st, "end", "actor location lookup failed");
            return false;
        }
        st.place = V(*actual);  // collision can refuse or shorten SetLocation: never assume we reached the requested point
        if (!WalkVelocity(st.actor, (st.place - before) * (1.0 / dt))) {
            EndWalk(st, "end", "velocity lookup failed");
            return false;
        }
        if (moved && Dist(st.place, mark) <= 0.01) EndWalk(st, "arrival", "reached the mark");
    }
    return true;
}

// After the frame's draw (the mod's tick): anyone still away from where we keep them was moved after this tick's camera update,
// so the drawn frame showed the jump. Undone at the next camera update; counted to measure whether the camera update is early
// enough.
int gDrawnJumps = 0;
void CheckDrawn() {
    for (Stay const& st : gStay) {
        auto const at = st.actor != nullptr && ue3::runtime::IsAlive(st.actor) ? player::Location(st.actor) : std::nullopt;
        if (at && Dist(V(*at), st.place) > kJumpUnits && Dist(V(*at), st.place) < kStayMaxUnits) {
            ++gDrawnJumps;
            log::Info("convo: a jump of {:.2f} m reached a drawn frame (after the camera update)", Dist(V(*at), st.place) / kUnitsPerMeter);
        }
    }
}

void KeepInPlace(ue3::Object* pawn) {
    if (!gC.on || !gInWorldForStay || !gSettings.stay || gSetLocation.Get() == nullptr || gC.object == nullptr ||
        !ue3::runtime::IsAlive(gC.object)) {
        EndWalks("keeping in place is unavailable");
        gStayMs = 0;
        return;
    }
    std::uint64_t const now = GetTickCount64();
    double const elapsed = gStayMs == 0 ? 0.0 : static_cast<double>(now - gStayMs) / 1000.0;
    double const dt = std::clamp(elapsed, 0.0, 0.1); // cap our own slide/turn step, not the engine's elapsed movement
    gStayMs = now;
    double const nowS = static_cast<double>(now) / 1000.0;
    // Whom each one looks at (the game's own look-at list), else the speaker.
    std::map<ue3::Object*, ue3::Object*> lookAt;
    if (auto const list = gLookAtList.ElementsOf(gC.object, 64)) {
        for (std::int32_t i = 0; i < list->count; ++i) {
            ue3::Object* const a = gLookActor.Read<ue3::Object*>(list->At(i)).value_or(nullptr);
            ue3::Object* const b = gLookTarget.Read<ue3::Object*>(list->At(i)).value_or(nullptr);
            if (a != nullptr && b != nullptr) {
                lookAt[a] = b;
            }
        }
    }
    ue3::Object* const speaker = gSpeaker.Read<ue3::Object*>(gC.object).value_or(nullptr);
    for (Tracked const& t : gC.actors) {
        auto it = std::find_if(gStay.begin(), gStay.end(), [&](Stay const& st) { return st.actor == t.actor; });
        if (t.actor == nullptr || !ue3::runtime::IsAlive(t.actor)) {
            if (it != gStay.end()) EndWalk(*it, "end", "actor no longer alive");
            continue;
        }
        auto const at = player::Location(t.actor);
        auto const rot = player::Rotation(t.actor);
        if (!at || !rot) {
            if (it != gStay.end()) EndWalk(*it, "end", "actor transform lookup failed");
            continue;
        }
        if (it == gStay.end()) {
            Stay st;
            st.actor = t.actor;
            st.name = t.name;
            st.place = V(*at);
            st.shownYaw = YawDeg(*rot);
            gStay.push_back(st);
            continue;
        }
        Stay& st = *it;
        ik::Vec3 const previousKept = st.place;
        double const d = Dist(V(*at), st.place);
        double const unexplained = st.walk.Phase() == cw::WalkPhase::Walking
            ? cw::BeyondWalk(WalkPoint(st.place), WalkPoint(V(*at)), st.walk.Wanted(), st.speedUnitsPerS, elapsed) : d;
        bool const isShepard = t.actor == pawn || t.actor == gC.playerPawn;
        if (!gC.openingDone && gSettings.openingAccept) {
            // The opening: the stage's placements stand (Part A); the kept place follows them.
            if (d >= kJumpUnits) {
                ++gC.openingMoves;
                log::Info("convo: at {:.1f} s the opening put {} {:.2f} m away: accepted", Seconds(), isShepard ? "Shepard" : t.name, d / kUnitsPerMeter);
            }
            st.place = V(*at);
            st.shownYaw = YawDeg(*rot);
            continue;
        }
        if (unexplained >= kJumpUnits && d < kStayMaxUnits) {
            ik::Vec3 const wanted = V(*at);  // capture BEFORE undoing the game's native placement
            bool const moved = PutAt(t.actor, st.place);
            EndWalk(st, "cancel", "a newer placement restarts the wait");
            ++st.kept;
            ++gC.kept;
            log::Info("convo: at {:.1f} s {} kept in place: the game put {} {:.2f} m away{}", Seconds(), isShepard ? "Shepard" : t.name,
                isShepard ? "him" : "it", d / kUnitsPerMeter, moved ? "" : " (SetLocation refused: it stays there)");
            if (!moved) {
                st.place = V(*at);
            }
            st.walk.Jump(WalkPoint(wanted), WalkPoint(st.place), nowS, gSettings.walkMinM * kUnitsPerMeter);
            if (!moved) EndWalk(st, "cancel", "SetLocation refused the undo");
            if (isShepard || gSettings.walk == 0) st.walk.End();  // remember the mark/time, but never walk Shepard
        } else {
            if (d >= kStayMaxUnits) {
                EndWalk(st, "cancel", "a scripted entrance was let through");
                ++gC.letGo;
                log::Info("convo: at {:.1f} s {} moved {:.1f} m at once: a scripted entrance, let through", Seconds(), t.name,
                    d / kUnitsPerMeter);
            }
            st.place = V(*at);  // its own walking, or a jump let through
        }
        if (isShepard || gSettings.walk == 0 ||
            (st.walk.Phase() == cw::WalkPhase::Walking && st.requestedMethod != gSettings.walk)) {
            EndWalk(st, "cancel", isShepard ? "the player's pawn stays kept" : "worldWalk changed or disabled");
        }
        if (isShepard || gSetRotation.Get() == nullptr) {
            EndWalk(st, "end", "turn reflection unavailable or player's pawn");
            continue;  // Shepard's body turns under the player's view (body follow)
        }
        // The yaw: toward whom it looks at (or the speaker), from where it stands, at most turnDegPerS.
        double target = YawDeg(*rot);
        bool const walking = gSettings.walk != 0 && WalkToMark(st, previousKept, nowS, dt, target);
        auto const look = lookAt.find(t.actor);
        ue3::Object* const other = look != lookAt.end() ? look->second : speaker != t.actor ? speaker : nullptr;
        if (!walking && other != nullptr && ue3::runtime::IsAlive(other)) {
            if (auto const o = player::Location(other)) {
                ik::Vec3 const to = V(*o) - st.place;
                if (std::hypot(to.x, to.y) > 20.0) {
                    target = std::atan2(to.y, to.x) * kRadToDeg;
                }
            }
        }
        double const diff = YawDiffDeg(target, st.shownYaw);
        double const step = gSettings.turnDegPerS * dt;
        st.shownYaw += std::clamp(diff, -step, step);
        if (std::abs(YawDiffDeg(st.shownYaw, YawDeg(*rot))) > 1.0) {
            ue3::Params p(ue3::runtime::Get(), gSetRotation.Get());
            ue3::Rotator r = *rot;
            r.yaw = ToUnits(st.shownYaw);
            if (p.Valid() && p.Set("NewRotation", r)) {
                ue3::runtime::Call(t.actor, gSetRotation.Get(), p);
            }
        }
    }
}
std::uint32_t gConversations = static_cast<std::uint32_t>(GetTickCount64() & 0xFFFF);
bool gHeld = false;          // view_anchor::Hold set by us
bool gPublished = false;
bool gInWorld = false;
std::uint64_t gNextSampleMs = 0;
std::uint64_t gWorldTicks = 0;

std::mutex gStatusMutex;
std::string gStatusJson = "{\"on\": false}";

double Seconds() {
    return gC.startMs == 0 ? 0.0 : static_cast<double>(GetTickCount64() - gC.startMs) / 1000.0;
}

void Track(ue3::Object* actor) {
    if (actor == nullptr || !ue3::runtime::IsAlive(actor)) {
        return;
    }
    for (Tracked const& t : gC.actors) {
        if (t.actor == actor) {
            return;
        }
    }
    auto const at = player::Location(actor);
    auto const rot = player::Rotation(actor);
    if (!at || !rot) {
        return;
    }
    Tracked t;
    t.actor = actor;
    t.name = ShortName(actor);
    t.start = t.logged = V(*at);
    t.startYaw = t.loggedYaw = YawDeg(*rot);
    if (auto const head = HeadOf(actor)) {
        t.headStart = head->at;
    }
    gC.actors.push_back(t);
    log::Info("convo: actor {} at {} facing {:.0f} deg{}", t.name, F(t.start), t.startYaw,
        t.headStart ? std::format(", head bone at {} ({:.2f} m above its location)", F(*t.headStart), (t.headStart->z - t.start.z) / kUnitsPerMeter)
                    : std::string(", no head bone"));
}

ue3::Object* ConversationNow(ue3::Object* controller) {
    ue3::Object* const info = gActorWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr);
    ue3::Object* const conv = info != nullptr ? gCurrentConversation.Read<ue3::Object*>(info).value_or(nullptr) : nullptr;
    return conv != nullptr && ue3::runtime::IsAlive(conv) ? conv : nullptr;
}

std::string Utf8(ue3::FString const* s) {
    std::string out;
    if (s == nullptr) {
        return out;
    }
    for (wchar_t const* p = s->Chars(); *p != L'\0' && out.size() < 120; ++p) {
        out.push_back(*p < 128 && *p >= 32 ? static_cast<char>(*p) : '?');
    }
    return out;
}

// The conversation movie's handler, from the GUI manager's panels.
ue3::Object* WheelHandler(ue3::Object* controller) {
    ue3::Object* const manager = gCachedManager.Read<ue3::Object*>(controller).value_or(nullptr);
    auto const* const panels = manager != nullptr ? gPanels.In<ue3::TArray<ue3::Object*>>(manager) : nullptr;
    if (panels == nullptr || panels->data == nullptr || panels->count <= 0 || panels->count > 64) {
        return nullptr;
    }
    for (std::int32_t i = 0; i < panels->count; ++i) {
        ue3::Object* const panel = panels->data[i];
        if (panel == nullptr || !ue3::runtime::IsAlive(panel)) {
            continue;
        }
        ue3::Object* const handler = gPanelHandler.Read<ue3::Object*>(panel).value_or(nullptr);
        if (handler != nullptr && ue3::IsA(handler, gConvHandlerClass) && ue3::runtime::IsAlive(handler)) {
            return handler;
        }
    }
    return nullptr;
}

// The camera cut to a new shot: what it shows against Shepard's place. Every actor of the conversation whose face is inside the
// frame (the lens's horizontal angle, 16:9) is listed; the nearest of them is the shot's subject.
void LogShot(ue3::Object* pawn, ik::Vec3 eyes) {
    ik::Vec3 const cam = V(gPov.lastLocation);
    ik::Vec3 const fwd = Forward(gPov.lastRotation);
    ik::Vec3 const right = YawRight(YawDeg(gPov.lastRotation));
    ik::Vec3 const up = ik::Normalize(ik::Cross(right, fwd), {0.0, 0.0, 1.0});
    double const halfH = std::clamp(static_cast<double>(gPov.lastFov), 1.0, 170.0) * 0.5 / kRadToDeg;
    double const halfV = std::atan(std::tan(halfH) * 9.0 / 16.0);
    Tracked const* subject = nullptr;
    double nearest = 1e9;
    ik::Vec3 subjectFace{};
    bool shepardInFrame = false;
    double shepardFromCamera = 0.0;
    std::string inFrame;
    for (Tracked const& t : gC.actors) {
        auto const face = FacePoint(t.actor);
        if (!face) {
            continue;
        }
        ik::Vec3 const d = *face - cam;
        double const along = ik::Dot(d, fwd);
        if (along <= 10.0 || std::abs(std::atan2(ik::Dot(d, right), along)) > halfH || std::abs(std::atan2(ik::Dot(d, up), along)) > halfV) {
            continue;
        }
        double const distance = ik::Length(d);
        inFrame += std::format("{}{} {:.1f} m", inFrame.empty() ? "" : ", ", t.actor == pawn ? "Shepard" : t.name, distance / kUnitsPerMeter);
        if (t.actor == pawn) {
            shepardInFrame = true;
            shepardFromCamera = distance;
        }
        if (distance < nearest) {
            nearest = distance;
            subject = &t;
            subjectFace = *face;
        }
    }
    double const camToEyes = Dist(cam, eyes) / kUnitsPerMeter;
    // A director's camera far from Shepard (a boss's entrance, a wide shot of the room) frames what his eyes can't: the survey's
    // "framed" scenes, which first person shows from a place the scene wasn't composed for.
    constexpr double kFarCameraM = 8.0;
    if (camToEyes > kFarCameraM) {
        ++gC.shotsFar;
    }
    gC.cameraFarM = std::max(gC.cameraFarM, camToEyes);
    bool const subjectIsShepard = subject != nullptr && subject->actor == pawn;
    std::string kind;
    std::string key;
    std::string seen;
    if (subject == nullptr) {
        kind = "a cutaway (no actor of the conversation in the frame)";
        key = "cutaway";
        ++gC.shotsHidden;
    } else if (camToEyes < 0.4) {
        kind = "from Shepard's eyes";
        key = "from Shepard's eyes";
    } else if (subjectIsShepard) {
        kind = "on Shepard (a reverse shot: his own face, not seen from his eyes)";
        key = "on Shepard";
        ++gC.shotsHidden;
    } else {
        bool const visible = LineOfSight(pawn, eyes, subjectFace);
        double const offAxis = AngleDeg(subjectFace - cam, subjectFace - eyes);
        bool const shoulder = shepardInFrame && shepardFromCamera < 150.0;
        kind = shoulder ? "over Shepard's shoulder onto " + subject->name : "on " + subject->name;
        key = shoulder ? "over Shepard's shoulder" : "on an NPC";
        seen = std::format(", {} from Shepard's eyes ({:.1f} m away, the camera {:.0f} deg off his line to it)",
            visible ? "visible" : "BLOCKED", Dist(eyes, subjectFace) / kUnitsPerMeter, offAxis);
        if (!visible) {
            ++gC.shotsHidden;
        }
    }
    ++gC.shotKinds[key];
    log::Info("convo: shot {} at {:.1f} s: {}{}; in frame: {}; the camera at {} ({:.2f} m from Shepard's eyes), {:.0f} deg lens; speaker {}",
        gC.shots, Seconds(), kind, seen, inFrame.empty() ? "nobody" : inFrame, F(cam), camToEyes, gPov.lastFov, ShortName(gC.speaker));
}

// The floor under a point: a trace straight down from 1 m above to 3 m below it (level geometry only). The seat's z is the floor
// plus the actor's own height above its floor.
std::optional<double> FloorZ(ue3::Object* tracer, ik::Vec3 const& at) {
    ue3::Function* const fn = gTrace.Get();
    if (fn == nullptr || tracer == nullptr) {
        return std::nullopt;
    }
    ue3::Params p(ue3::runtime::Get(), fn);
    if (!p.Valid()) {
        return std::nullopt;
    }
    p.Set("TraceStart", ue3::Vector{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z + 100.0)});
    p.Set("TraceEnd", ue3::Vector{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z - 300.0)});
    p.SetBool("bTraceActors", false);
    if (!ue3::runtime::Call(tracer, fn, p) || p.Return<ue3::Object*>().value_or(nullptr) == nullptr) {
        return std::nullopt;
    }
    auto const hit = p.Get<ue3::Vector>("HitLocation");
    return hit ? std::optional<double>(hit->z) : std::nullopt;
}

bool FaceYaw(ue3::Object* actor, double yawDeg) {
    ue3::Function* const fn = gSetRotation.Get();
    if (fn == nullptr) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), fn);
    return p.Valid() && p.Set("NewRotation", ue3::Rotator{0, ToUnits(yawDeg), 0}) && ue3::runtime::Call(actor, fn, p);
}

// The end of the opening (research/59 "Part A" and "Part B"): the player's place is taken from Shepard's mark (his head bone, the
// first-person eye offset), facing the owner; then everyone of the conversation and the squad is checked from there (within
// worldViewDeg of the facing, 1.2 m to 6 m away, a clear line of sight) and whoever fails is put on a seat of an arc around the
// player, centred on the owner, facing the player. The host dips from black on `cut`, so both moves read as a cut, not a teleport.
// The opening without a cut (worldOpening = auto, research/69 "Authored scenes"): where the stage put Shepard against where
// the player stood on foot. Near, or within a walk in the same space: Shepard is put back where he stood, the player's view
// stays the one he had, nobody is arranged and nothing dips; his mark is where he is wanted and he walks there himself.
// Returns false for another composition (the cut below).
bool OpeningWithoutCut(ue3::Object* conv, ue3::Object* pawn) {
    cw::OpeningFacts facts;
    auto const markAt = player::Location(pawn);
    bool const footFresh = gFoot.have && gC.startMs >= gFoot.ms && gC.startMs - gFoot.ms < 5000;
    facts.known = footFresh && markAt.has_value();
    ik::Vec3 const stood = V(gFoot.pawn);
    ik::Vec3 const eyes = V(gFoot.location);
    if (facts.known) {
        ik::Vec3 const mark = V(*markAt);
        facts.markUnits = std::hypot(mark.x - stood.x, mark.y - stood.y);
        facts.riseUnits = mark.z - stood.z;
        auto const head = HeadOf(pawn);
        facts.sightToMark = LineOfSight(pawn, eyes, head ? head->at : mark + ik::Vec3{0.0, 0.0, 60.0});
        facts.floorAtMark = FloorZ(pawn, mark).has_value();
        if (auto const hidden = gActorHidden.ReadBool(pawn)) {
            facts.shepardHidden = *hidden;
        }
        // The people the player was looking at: how far the stage moved them (the conversation saved their places first).
        if (auto const saved = gSavedPositions.ElementsOf(conv, 64)) {
            double const facing = YawDeg(gFoot.rotation);
            for (std::int32_t i = 0; i < saved->count; ++i) {
                ue3::Object* const a = gSavedActor.Read<ue3::Object*>(saved->At(i)).value_or(nullptr);
                auto const was = gSavedPos.Read<ue3::Vector>(saved->At(i));
                auto const now = a != nullptr && a != pawn && ue3::runtime::IsAlive(a) ? player::Location(a) : std::nullopt;
                if (was && now && cw::InView({eyes.x, eyes.y, eyes.z}, facing, {was->x, was->y, was->z}, gSettings.viewDeg, 0.0, 3000.0)) {
                    facts.peopleMovedUnits = std::max(facts.peopleMovedUnits, Dist(V(*now), V(*was)));
                }
            }
        }
    }
    cw::Opening const opening = cw::DecideOpening(facts, gSettings.opening);
    gC.opening = opening;
    gC.markM = facts.markUnits / kUnitsPerMeter;
    log::Info("convo: the opening ({}): Shepard's mark is {:.2f} m from where the player stood ({:+.2f} m up), {}, {}, people in view moved "
              "at most {:.2f} m{}: {}", cw::Name(gSettings.opening), facts.markUnits / kUnitsPerMeter, facts.riseUnits / kUnitsPerMeter,
        facts.sightToMark ? "in sight" : "out of sight", facts.floorAtMark ? "a floor there" : "no floor there",
        facts.peopleMovedUnits / kUnitsPerMeter, facts.known ? "" : " (no fresh place on foot)", cw::Name(opening));
    if (opening == cw::Opening::Cut || !facts.known) {
        return false;
    }
    // Shepard back where he stood, facing as the player did; the view the one on foot. (His head bone's place is read
    // before the move: read after it, it already has the move in it, and the follower below took the difference for a jump
    // of his and moved the view to the mark after all, 2026-11-09.)
    ik::Vec3 const mark = V(*markAt);
    auto const headAtMark = HeadOf(pawn);
    bool const put = PutAt(pawn, stood);
    FaceYaw(pawn, YawDeg(gFoot.rotation));
    for (Stay& st : gStay) {
        if (st.actor == pawn) {
            st.place = put ? stood : mark;
            st.shownYaw = YawDeg(gFoot.rotation);
        }
    }
    if (!put) {
        log::Info("convo: the opening: Shepard could not be put back where he stood (SetLocation refused): the cut after all");
        gC.opening = cw::Opening::Cut;
        return false;
    }
    gC.hold = eyes;
    gC.holdRotation = gFoot.rotation;
    gC.holdFov = gFoot.fov;
    gC.holdFrom = "the view on foot";
    if (headAtMark) {
        gC.anchorHead = headAtMark->at + (stood - mark);
    }
    // His head bone is no measure for a moment: the mesh was up to 1.3 m from the pawn when the stage had just placed him
    // (body follow, a pose not yet updated), and the follower read its catching up as a jump of his and moved the view
    // 0.8 m (2026-11-09). For a second and a half the anchor just takes the bone's place.
    gC.settleUntilMs = GetTickCount64() + 1500;
    gC.follow = {{gC.hold.x, gC.hold.y, gC.hold.z}, gC.anchorHead ? cw::Vec3{gC.anchorHead->x, gC.anchorHead->y, gC.anchorHead->z} : cw::Vec3{}, 0};
    if (opening == cw::Opening::WalkIn) {
        gC.wantedMark = mark;
    }
    log::Info("convo: the opening: no cut; the player stays at {} facing {:.0f} deg{}", F(gC.hold), YawDeg(gC.holdRotation),
        opening == cw::Opening::WalkIn ? std::format("; he is wanted at Shepard's mark {} and walks there himself", F(mark)) : std::string());
    return true;
}

void FinishOpening(ue3::Object* conv, ue3::Object* pawn) {
    gC.openingDone = true;
    if (gSettings.opening == cw::OpeningRule::Auto && OpeningWithoutCut(conv, pawn)) {
        return;
    }
    ue3::Object* const owner = gOwner.Read<ue3::Object*>(conv).value_or(nullptr);
    auto const ownerAt = owner != nullptr && owner != pawn ? FacePoint(owner) : std::nullopt;
    // Part A: the player at Shepard's mark.
    auto const head = HeadOf(pawn);
    auto const rot = player::Rotation(pawn);
    ik::Vec3 const before = gC.hold;
    if (head) {
        double const yaw = ownerAt ? std::atan2(ownerAt->y - head->at.y, ownerAt->x - head->at.x) * kRadToDeg : rot ? YawDeg(*rot) : 0.0;
        ik::Vec3 const& e = gFoot.eyeFromHead;
        gC.hold = head->at + YawForward(yaw) * e.x + YawRight(yaw) * e.y + ik::Vec3{0.0, 0.0, e.z};
        gC.holdRotation = {0, ToUnits(yaw), 0};
        gC.holdFrom = "Shepard's mark (the opening accepted)";
        gC.anchorHead = head->at;
        gC.follow = {{gC.hold.x, gC.hold.y, gC.hold.z}, {head->at.x, head->at.y, head->at.z}, 0};
        auto const view = vr::host_view::Latest();
        if (view.valid && view.floorLocked) {
            auto const basis = engine::ToBasis(engine::UnrealRotation{gC.holdRotation.pitch, gC.holdRotation.yaw, gC.holdRotation.roll});
            gC.hold.z = view.centreZ - (basis.forward.z * view.offsetForward + basis.right.z * view.offsetRight + basis.up.z * view.offsetUp);
        }
    }
    double const moved = Dist(before, gC.hold) / kUnitsPerMeter;
    log::Info("convo: the opening ended after {:.1f} s: {} placements accepted; the player's place {} at {} facing {:.0f} deg{} ({:.2f} m from where it was)",
        Seconds(), gC.openingMoves, head ? "re-placed" : "kept (no head bone)", F(gC.hold), YawDeg(gC.holdRotation),
        ownerAt ? std::format(", toward {}", ShortName(owner)) : std::string(), moved);
    // Part B: everyone in view.
    if (!gSettings.arrange) {
        if (moved > 0.05) ++gC.cut;
        return;
    }
    double const facing = YawDeg(gC.holdRotation);
    double const halfAngle = gSettings.viewDeg;
    double const minUnits = 120.0, maxUnits = 600.0;
    std::vector<std::pair<ue3::Object*, std::string>> people;
    for (Tracked const& t : gC.actors) {
        if (t.actor != nullptr && t.actor != pawn && ue3::runtime::IsAlive(t.actor)) {
            people.emplace_back(t.actor, t.name);
        }
    }
    for (squad_weapons::MemberView const& m : squad_weapons::Members()) {
        if (m.pawn != nullptr && ue3::runtime::IsAlive(m.pawn) &&
            std::none_of(people.begin(), people.end(), [&](auto const& pr) { return pr.first == m.pawn; })) {
            people.emplace_back(m.pawn, m.name.empty() ? ShortName(m.pawn) : m.name);
        }
    }
    std::vector<std::pair<ue3::Object*, std::string>> out;
    for (auto const& [actor, name] : people) {
        if (actor == owner) {
            continue;  // the owner keeps its mark: the arc is centred on it
        }
        auto const at = player::Location(actor);
        auto const face = FacePoint(actor);
        if (!at || !face) {
            continue;
        }
        cw::Vec3 const p{at->x, at->y, at->z};
        bool const inView = cw::InView({gC.hold.x, gC.hold.y, gC.hold.z}, facing, p, halfAngle, minUnits, maxUnits);
        bool const seen = inView && LineOfSight(pawn, gC.hold, *face);
        if (!seen) {
            double const bearing = std::atan2(at->y - gC.hold.y, at->x - gC.hold.x) * kRadToDeg;
            double off = bearing - facing;
            while (off > 180.0) off -= 360.0;
            while (off < -180.0) off += 360.0;
            log::Info("convo: {} is {} ({:.1f} m away, {:.0f} deg off your facing)", name, !inView ? (std::abs(off) > halfAngle ? "out of view" : "too near or too far") : "blocked",
                Dist(V(*at), gC.hold) / kUnitsPerMeter, off);
            out.emplace_back(actor, name);
        }
    }
    if (!out.empty()) {
        ik::Vec3 const centre = ownerAt ? *ownerAt : gC.hold + YawForward(facing) * 250.0;
        std::vector<cw::ArcSeat> const seats = cw::ArcSeats({gC.hold.x, gC.hold.y, gC.hold.z}, {centre.x, centre.y, centre.z},
            static_cast<int>(out.size()), gSettings.arcM * kUnitsPerMeter, 25.0, halfAngle);
        for (std::size_t i = 0; i < out.size() && i < seats.size(); ++i) {
            auto const [actor, name] = out[i];
            auto const at = player::Location(actor);
            if (!at) {
                continue;
            }
            ik::Vec3 seat{seats[i].at.x, seats[i].at.y, at->z};
            auto const floorHere = FloorZ(pawn, V(*at));
            auto const floorThere = FloorZ(pawn, seat);
            if (!floorThere) {
                log::Info("convo: no floor under the seat for {} at {}: left where it is", name, F(seat));
                continue;
            }
            seat.z = *floorThere + (floorHere ? at->z - *floorHere : 0.0);
            bool const put = PutAt(actor, seat);
            FaceYaw(actor, seats[i].yawDeg);
            auto it = std::find_if(gStay.begin(), gStay.end(), [&](Stay const& st) { return st.actor == actor; });
            if (it != gStay.end()) {
                if (auto const now = player::Location(actor)) {
                    it->place = V(*now);
                }
                it->shownYaw = seats[i].yawDeg;
            }
            if (put) ++gC.arranged;
            log::Info("convo: {} brought into view: seat {} at {} ({:.1f} m from you), facing you{}", name, i + 1, F(seat),
                Dist(seat, gC.hold) / kUnitsPerMeter, put ? "" : " (SetLocation refused)");
        }
    }
    if (moved > 0.05 || gC.arranged > 0) {
        ++gC.cut;
    }
}

// The player's walk during the conversation (research/59 "Walking during conversations"): the left stick moves Shepard's pawn
// along the head's facing at worldMoveSpeedMS, inside worldMoveLeashM of the conversation's owner (cw::StepPlayer), never
// within worldMoveClearM of a person of the conversation or the squad (cw::KeepClear: pushed out, so he walks around them), on the
// floor (a trace; a drop or rise past 60 units is a ledge or a step: refused), not through walls (the shots' line of sight at
// waist height), while the wheel is down (its stick is the wheel's) and after the opening. The held view, its anchors and the
// kept place move with the pawn, so keep-in-place never undoes the walk and the head bone's jump detector never fires on it.
bool gMomentWalk = false;  // a moment's view is up (set by TickMoment): the walk's trigger guard stands down

void MovePlayer(ue3::Object* pawn, engine::TrackingFrame& frame, vr::host_view::State const& view, double dt) {
    auto stop = [&] {
        if (gC.moving) {
            WalkVelocity(pawn, {});
            gC.moving = false;
        }
    };
    if (!gSettings.move || !gC.openingDone || gC.wheel || pawn == nullptr || !ue3::runtime::IsAlive(pawn) || gSetLocation.Get() == nullptr) {
        stop();
        return;
    }
    // The stick: `mevr convo walk` while it lasts, else the left controller's.
    float stickX = 0.0f, stickY = 0.0f;
    if (gWalkOverride.untilMs > GetTickCount64()) {
        stickX = gWalkOverride.x;
        stickY = gWalkOverride.y;
    } else {
        vr::controllers::State const controllers = vr::controllers::Latest();
        if (!controllers.active) {
            stop();
            return;
        }
        stickX = controllers.left.stickX;
        stickY = controllers.left.stickY;
    }
    auto const at = player::Location(pawn);
    if (!at) {
        stop();
        return;
    }
    ue3::Object* const owner = gOwner.Read<ue3::Object*>(gC.object).value_or(nullptr);
    auto const ownerAt = owner != nullptr && ue3::runtime::IsAlive(owner) ? player::Location(owner) : std::nullopt;
    // The leash is around the owner; after a walk-in opening, around Shepard's mark (where the stage wanted him) until he is there.
    if (gC.wantedMark && std::hypot(at->x - gC.wantedMark->x, at->y - gC.wantedMark->y) <= gSettings.moveLeashM * kUnitsPerMeter * 0.5) {
        log::Info("convo: at {:.1f} s the player reached Shepard's mark ({:.2f} m walked so far)", Seconds(), gC.movedUnits / kUnitsPerMeter);
        gC.wantedMark.reset();
    }
    ik::Vec3 const anchor = gC.wantedMark ? *gC.wantedMark : ownerAt ? V(*ownerAt) : gC.hold;
    // Where the player faces: the head in the world through the held frame; without a VR view (a flat game), the held view's yaw.
    double headYaw = YawDeg(gC.holdRotation);
    if (view.valid) {
        engine::Basis const head = engine::OrientationToWorld(frame, view.head.orientation);
        headYaw = std::atan2(head.forward.y, head.forward.x) * kRadToDeg;
    }
    cw::PlayerStep const step = cw::StepPlayer(WalkPoint(V(*at)), WalkPoint(anchor), gSettings.moveLeashM * kUnitsPerMeter,
        stickX, stickY, headYaw, gSettings.moveSpeedMS * kUnitsPerMeter, std::clamp(dt, 0.0, 0.1));
    if (step.atLeash) {
        ++gC.leashHits;
    }
    if (!step.moved) {
        stop();
        return;
    }
    // Not through people: the conversation's actors, the squad and the owner, at their live places.
    std::vector<cw::Vec3> people;
    auto addPerson = [&](ue3::Object* actor) {
        if (actor == nullptr || actor == pawn || !ue3::runtime::IsAlive(actor)) return;
        if (auto const where = player::Location(actor)) {
            people.push_back(WalkPoint(V(*where)));
        }
    };
    for (Stay const& st : gStay) addPerson(st.actor);
    for (Tracked const& t : gC.actors) {
        if (std::none_of(gStay.begin(), gStay.end(), [&](Stay const& st) { return st.actor == t.actor; })) addPerson(t.actor);
    }
    for (squad_weapons::MemberView const& m : squad_weapons::Members()) addPerson(m.pawn);
    if (ownerAt && std::none_of(gStay.begin(), gStay.end(), [&](Stay const& st) { return st.actor == owner; })) addPerson(owner);
    cw::ClearStep const clear = cw::KeepClear(WalkPoint(V(*at)), step.place, people, gSettings.moveClearM * kUnitsPerMeter);
    if (clear.held) {
        ++gC.clearHits;
    }
    if (!clear.moved) {
        stop();
        return;
    }
    ik::Vec3 target = WalkPoint(clear.place);
    // The floor under the target, the pawn's height above its own floor kept; a ledge or a step refused.
    auto const floorHere = FloorZ(pawn, V(*at));
    auto const floorThere = FloorZ(pawn, target);
    if (!floorHere || !floorThere || std::abs(*floorThere - *floorHere) > 60.0) {
        ++gC.moveRefused;
        stop();
        return;
    }
    target.z = at->z + (*floorThere - *floorHere);
    if (!LineOfSight(pawn, V(*at), target)) {
        ++gC.moveRefused;
        stop();
        return;
    }
    // (Not during a moment: the script wants him through the triggers on the way, the council's streaming volume among them.)
    if (gSettings.moveTriggers && !gMomentWalk) {
        std::string which;
        bool enters = false;
        if (StepChangesTouch(pawn, anchor, V(*at), target, which, enters)) {
            ++gC.triggerHits;
            if (gC.triggerName != which) {
                gC.triggerName = which;
                log::Info("convo: at {:.1f} s the walk stops at {}: the step would {} it, and a trigger's touch belongs to the level's script",
                    Seconds(), which, enters ? "enter" : "leave");
            }
            stop();
            return;
        }
    }
    if (!PutAt(pawn, target)) {
        ++gC.moveRefused;
        stop();
        return;
    }
    auto const actual = player::Location(pawn);
    ik::Vec3 const delta = (actual ? V(*actual) : target) - V(*at);
    gC.hold = gC.hold + delta;
    frame.headWorld.x += delta.x;
    frame.headWorld.y += delta.y;
    frame.headWorld.z += delta.z;
    if (gC.anchorHead) {
        *gC.anchorHead = *gC.anchorHead + delta;
    }
    gC.follow.place = {gC.follow.place.x + delta.x, gC.follow.place.y + delta.y, gC.follow.place.z + delta.z};
    gC.follow.anchor = {gC.follow.anchor.x + delta.x, gC.follow.anchor.y + delta.y, gC.follow.anchor.z + delta.z};
    for (Stay& st : gStay) {
        if (st.actor == pawn) {
            st.place = actual ? V(*actual) : target;  // the kept place walks along: keep-in-place puts him back here, not at the start
        }
    }
    double const stepLen = std::hypot(delta.x, delta.y);
    gC.movedUnits += stepLen;
    ++gC.moveSteps;
    if (dt > 0.0) {
        ik::Vec3 const velocity{delta.x / dt, delta.y / dt, 0.0};
        gC.moving = WalkVelocity(pawn, velocity);  // the legs' animation reads the pawn's velocity (body follow's legs)
    }
    if (!gC.moveLogged) {
        gC.moveLogged = true;
        log::Info("convo: at {:.1f} s the player walks: the stick ({:.2f}, {:.2f}){} along the facing {:.0f} deg, {:.2f} m from {} (leash {:.1f} m, "
                  "{:.2f} m clear of {} people)",
            Seconds(), stickX, stickY, gWalkOverride.untilMs > GetTickCount64() ? " (mevr convo walk)" : "", headYaw,
            Dist(target, anchor) / kUnitsPerMeter, ShortName(owner), gSettings.moveLeashM, gSettings.moveClearM, people.size());
    }
}

void Begin(ue3::Object* conv, ue3::Object* controller) {
    gC = {};
    gStay.clear();
    gStayMs = 0;
    gDrawnJumps = 0;
    gC.on = true;
    gC.object = conv;
    gC.name = ue3::runtime::FullName(conv);
    gC.startMs = GetTickCount64();
    gC.number = ++gConversations;
    gC.setPiece = CurrentMode() == cw::Mode::World && gSettings.setPiecesOnScreen && cw::IsSetPiece(gC.name);
    gC.world = CurrentMode() == cw::Mode::World && !gC.setPiece;
    ue3::Object* const pawn = player::Pawn(controller);
    gC.playerPawn = pawn;
    // Shepard not in the scene (research/69 item 3; the owner: "some which don't have Shepard at all should be the typical cut
    // scene"), read from the live scene whatever the name list says: his pawn hidden, or farther than worldAbsentM from the
    // conversation's owner and out of its sight (the stage has placed everyone by now). Logged; shown on the screen only with
    // worldAutoScreen = on, until the sweep has shown which scenes the rules name.
    if (gC.world && gSettings.autoScreen != 0 && pawn != nullptr) {
        ue3::Object* const owner = gOwner.Read<ue3::Object*>(conv).value_or(nullptr);
        auto const pawnAt = player::Location(pawn);
        auto const ownerAt = owner != nullptr && owner != pawn ? player::Location(owner) : std::nullopt;
        if (gActorHidden.ReadBool(pawn).value_or(false)) {
            gC.absentWhy = "Shepard's pawn is hidden";
        } else if (gSettings.absentM > 0.0f && pawnAt && ownerAt && Dist(V(*pawnAt), V(*ownerAt)) > gSettings.absentM * kUnitsPerMeter &&
                   !LineOfSight(pawn, V(*pawnAt) + ik::Vec3{0.0, 0.0, 60.0}, V(*ownerAt) + ik::Vec3{0.0, 0.0, 60.0})) {
            gC.absentWhy = std::format("Shepard is {:.0f} m from {} and out of its sight", Dist(V(*pawnAt), V(*ownerAt)) / kUnitsPerMeter,
                ShortName(owner));
        }
        if (!gC.absentWhy.empty()) {
            gC.autoScreened = gSettings.autoScreen == 2;
            log::Info("convo: {}: no place for the player in this scene; {}", gC.absentWhy,
                gC.autoScreened ? "shown on the 3D screen (worldAutoScreen = on)" : "logged only (worldAutoScreen = log), shown in the world");
            if (gC.autoScreened) {
                gC.setPiece = true;
                gC.world = false;
            }
        }
    }
    ue3::Object* const stage = gStage.Read<ue3::Object*>(conv).value_or(nullptr);
    gC.stage = stage != nullptr;
    log::Info("convo: conversation {} began: {} (owner {}, player {}, stage {}{}{}); shown {}", gC.number, gC.name,
        ShortName(gOwner.Read<ue3::Object*>(conv).value_or(nullptr)), ShortName(gConvPlayer.Read<ue3::Object*>(conv).value_or(nullptr)),
        ShortName(stage), stage != nullptr && player::Location(stage) ? " at " + F(V(*player::Location(stage))) : std::string(),
        gUseFixedCameras.ReadBool(conv).value_or(false) ? ", fixed cameras" : "", gC.world ? "in the world (first person)" : gC.setPiece ? "on the 3D screen (a set piece: research/59 \"The scan\")" : cw::Name(CurrentMode()));
    if (gFoot.have) {
        auto const nowPawn = player::Location(pawn);
        log::Info("convo: Shepard's last view on foot at {} yaw {:.0f}, his pawn then at {}{}", F(V(gFoot.location)), YawDeg(gFoot.rotation),
            F(V(gFoot.pawn)), nowPawn ? std::format(", now at {} ({:.2f} m from there)", F(V(*nowPawn)), Dist(V(*nowPawn), V(gFoot.pawn)) / kUnitsPerMeter)
                                      : std::string());
    }
    // The player's place: the view on foot if it was Shepard's a moment ago and his pawn is still there, else his eyes from his head
    // bone facing where his pawn faces (a conversation straight after a level load or a cutscene: the view on foot is elsewhere).
    {
        auto const nowPawn = player::Location(pawn);
        auto const head = HeadOf(pawn);
        bool const footFresh = gFoot.have && GetTickCount64() - gFoot.ms < 5000 && nowPawn && Dist(V(*nowPawn), V(gFoot.pawn)) < 300.0;
        if (footFresh) {
            gC.hold = V(gFoot.location) + (V(*nowPawn) - V(gFoot.pawn));
            gC.holdRotation = gFoot.rotation;
            gC.holdFov = gFoot.fov;
            gC.holdFrom = "the view on foot";
        } else if (head) {
            auto const rot = player::Rotation(pawn);
            double const yaw = rot ? YawDeg(*rot) : 0.0;
            ik::Vec3 const& e = gFoot.eyeFromHead;
            gC.hold = head->at + YawForward(yaw) * e.x + YawRight(yaw) * e.y + ik::Vec3{0.0, 0.0, e.z};
            gC.holdRotation = {0, rot ? rot->yaw : 0, 0};
            gC.holdFov = gFoot.have ? gFoot.fov : 90.0f;
            gC.holdFrom = "Shepard's head bone";
            // No recent on-foot POV: use the same cylinder-floor eye reference if it is available, retaining XY staging.
            auto const view = vr::host_view::Latest();
            if (view.valid && view.floorLocked) {
                auto const basis = engine::ToBasis(engine::UnrealRotation{gC.holdRotation.pitch, gC.holdRotation.yaw, gC.holdRotation.roll});
                double const commonZ = basis.forward.z * view.offsetForward + basis.right.z * view.offsetRight + basis.up.z * view.offsetUp;
                gC.hold.z = view.centreZ - commonZ;
            }
        } else {
            gC.hold = V(gFoot.location);
            gC.holdRotation = gFoot.rotation;
            gC.holdFov = gFoot.fov;
            gC.holdFrom = "the last view on foot (no head bone)";
        }
        if (head) {
            gC.anchorHead = head->at;
        }
        gC.follow = {{gC.hold.x, gC.hold.y, gC.hold.z}, gC.anchorHead ? cw::Vec3{gC.anchorHead->x, gC.anchorHead->y, gC.anchorHead->z} : cw::Vec3{}, 0};
        gC.openingUntilMs = gC.startMs + 500;
        gC.openingDone = !(gC.world && gSettings.openingAccept);
        log::Info("convo: the player's place: {} at {} facing {:.0f} deg{}", gC.holdFrom, F(gC.hold), YawDeg(gC.holdRotation),
            head ? std::format(" ({:.2f} m from Shepard's head bone)", Dist(gC.hold, head->at) / kUnitsPerMeter) : std::string());
    }
    Track(pawn);
    Track(gOwner.Read<ue3::Object*>(conv).value_or(nullptr));
    if (auto const list = gSpeakerList.ElementsOf(conv, 64)) {
        for (std::int32_t i = 0; i < list->count; ++i) {
            Track(gSpeakerActor.Read<ue3::Object*>(list->At(i)).value_or(nullptr));
        }
    }
    // Where the stage put actors from (the conversation saved their places before it moved them).
    if (auto const saved = gSavedPositions.ElementsOf(conv, 64)) {
        for (std::int32_t i = 0; i < saved->count; ++i) {
            ue3::Object* const a = gSavedActor.Read<ue3::Object*>(saved->At(i)).value_or(nullptr);
            auto const p = gSavedPos.Read<ue3::Vector>(saved->At(i));
            auto const now = a != nullptr ? player::Location(a) : std::nullopt;
            if (p) {
                log::Info("convo: saved place of {}: {}{}", ShortName(a), F(V(*p)),
                    now ? std::format(" (now {:.2f} m from it)", Dist(V(*now), V(*p)) / kUnitsPerMeter) : std::string());
            }
        }
    }
}

void End(char const* why) {
    if (!gC.on) {
        return;
    }
    EndWalks(why);
    std::string moves;
    for (Tracked const& t : gC.actors) {
        auto const at = ue3::runtime::IsAlive(t.actor) ? player::Location(t.actor) : std::nullopt;
        auto const rot = ue3::runtime::IsAlive(t.actor) ? player::Rotation(t.actor) : std::nullopt;
        moves += std::format("{}{}: moved at most {:.2f} m, turned at most {:.0f} deg{}{}", moves.empty() ? "" : "; ", t.name,
            t.maxMove / kUnitsPerMeter, t.maxTurn,
            at ? std::format(", ends {:.2f} m from its start", Dist(V(*at), t.start) / kUnitsPerMeter) : std::string(),
            t.headStart ? std::format(", head bone moved at most {:.2f} m", t.headMax / kUnitsPerMeter) : std::string());
        (void)rot;
    }
    std::string kinds;
    for (auto const& [k, n] : gC.shotKinds) {
        kinds += std::format("{}{} {}", kinds.empty() ? "" : ", ", n, k);
    }
    log::Info("convo: conversation {} ended ({}) after {:.1f} s: {} shots ({}), {} not seeable from Shepard's eyes; the wheel opened {} times; {}",
        gC.number, why, Seconds(), gC.shots, kinds.empty() ? "none" : kinds, gC.shotsHidden, gC.wheelOpenings, moves);
    if (gC.world) {
        log::Info("convo: in the world: Shepard's head bone was at most {:.2f} m from the player's held eyes; {} jumps onto marks undone "
                  "(people kept in place, {} of them seen in a drawn frame), {} long moves let through", gC.eyeToViewMax / kUnitsPerMeter, gC.kept,
            gDrawnJumps, gC.letGo);
        if (gC.sourceKept > 0) {
            log::Info("convo: {} placements onto marks withheld at their source (the farthest mark {:.2f} m away)", gC.sourceKept,
                gC.sourceFarUnits / kUnitsPerMeter);
        }
        if (gC.moveSteps > 0 || gC.moveRefused > 0 || gC.leashHits > 0 || gC.clearHits > 0 || gC.triggerHits > 0) {
            log::Info("convo: the player walked {:.2f} m in {} steps; the leash held {} steps back, people in the way {}, a trigger's edge {}{}, "
                      "{} refused (no floor, a ledge, a wall, SetLocation)",
                gC.movedUnits / kUnitsPerMeter, gC.moveSteps, gC.leashHits, gC.clearHits, gC.triggerHits,
                gC.triggerName.empty() ? std::string() : " (" + gC.triggerName + ")", gC.moveRefused);
        }
    }
    if (gC.moving && ue3::runtime::IsAlive(gC.playerPawn)) {
        WalkVelocity(gC.playerPawn, {});
    }
    // One line for the survey (tools/conversation-survey.ps1) and the sweep's judge: everything above as numbers.
    std::string entryPath;
    for (int const e : gC.entryPath) {
        entryPath += std::format("{}{}", entryPath.empty() ? "" : ", ", e);
    }
    double maxMove = 0.0;
    for (Tracked const& t : gC.actors) {
        maxMove = std::max(maxMove, t.maxMove);
    }
    log::Info("convo: summary {{\"name\": \"{}\", \"why\": \"{}\", \"seconds\": {:.1f}, \"stage\": {}, \"actors\": {}, \"lines\": {}, "
              "\"speakers\": {}, \"wheel\": {}, \"shots\": {}, \"onShepard\": {}, \"onNpc\": {}, \"shoulder\": {}, \"cutaway\": {}, "
              "\"notSeeable\": {}, \"world\": {}, \"kept\": {}, \"drawn\": {}, \"letGo\": {}, \"reanchors\": {}, \"maxMoveM\": {:.2f}, "
              "\"eyeToViewM\": {:.2f}, \"farShots\": {}, \"cameraFarM\": {:.1f}, \"walks\": {}, \"openingMoves\": {}, \"arranged\": {}, "
              "\"playerWalkedM\": {:.2f}, \"leashHits\": {}, \"clearHits\": {}, \"moveRefused\": {}, \"triggerHits\": {}, "
              "\"sourceKept\": {}, \"noPlace\": \"{}\", \"autoScreened\": {}, \"opening\": \"{}\", \"markM\": {:.2f}, \"cuts\": {}, "
              "\"entries\": [{}]}}",
        gC.name, why, Seconds(), gC.stage ? "true" : "false", gC.actors.size(), gC.lines, gC.speakers.size(), gC.wheelOpenings, gC.shots,
        gC.shotKinds.count("on Shepard") != 0 ? gC.shotKinds.at("on Shepard") : 0, gC.shotKinds.count("on an NPC") != 0 ? gC.shotKinds.at("on an NPC") : 0,
        gC.shotKinds.count("over Shepard's shoulder") != 0 ? gC.shotKinds.at("over Shepard's shoulder") : 0,
        gC.shotKinds.count("cutaway") != 0 ? gC.shotKinds.at("cutaway") : 0, gC.shotsHidden, gC.world ? "true" : "false", gC.kept, gDrawnJumps,
        gC.letGo, gC.reanchors, maxMove / kUnitsPerMeter, gC.eyeToViewMax / kUnitsPerMeter, gC.shotsFar, gC.cameraFarM, gC.walks, gC.openingMoves, gC.arranged,
        gC.movedUnits / kUnitsPerMeter, gC.leashHits, gC.clearHits, gC.moveRefused, gC.triggerHits, gC.sourceKept, gC.absentWhy,
        gC.autoScreened ? "true" : "false", gC.world ? cw::Name(gC.opening) : "-", gC.markM, gC.cut, entryPath);
    gC = {};
}

void Sample(ue3::Object* conv, ue3::Object* controller, ue3::Object* pawn, ik::Vec3 eyes) {
    // Who speaks, the line, who looks at whom.
    ue3::Object* const speaker = gSpeaker.Read<ue3::Object*>(conv).value_or(nullptr);
    if (speaker != gC.speaker) {
        gC.speaker = speaker;
        ++gC.speakerSerial;
        Track(speaker);
        ue3::Object* const listener = gListener.Read<ue3::Object*>(conv).value_or(nullptr);
        Track(listener);
        std::string facing;
        if (auto const head = HeadOf(speaker); head && speaker != pawn) {
            ik::Vec3 const toEyes = eyes - head->at;
            facing = std::format("; its head bone's axes are {:.0f}/{:.0f}/{:.0f} deg off its line to Shepard's eyes",
                AngleDeg(head->axis[0], toEyes), AngleDeg(head->axis[1], toEyes), AngleDeg(head->axis[2], toEyes));
            if (auto const rot = player::Rotation(speaker)) {
                facing += std::format(", its body {:.0f} deg", std::abs(YawDiffDeg(YawDeg(*rot), std::atan2(toEyes.y, toEyes.x) * kRadToDeg)));
            }
        }
        log::Info("convo: at {:.1f} s {} speaks to {}{}", Seconds(), ShortName(speaker), ShortName(listener), facing);
    }
    std::string const subtitle = Utf8(gSubtitle.In<ue3::FString>(conv));
    if (subtitle != gC.subtitle) {
        gC.subtitle = subtitle;
        if (!subtitle.empty()) {
            ++gC.lines;
            ++gC.speakers[ShortName(speaker)];
            log::Info("convo: at {:.1f} s the line: \"{}\"", Seconds(), subtitle);
        }
    }
    std::string looks;
    if (auto const list = gLookAtList.ElementsOf(conv, 64)) {
        for (std::int32_t i = 0; i < list->count; ++i) {
            looks += std::format("{}{} -> {}", looks.empty() ? "" : ", ", ShortName(gLookActor.Read<ue3::Object*>(list->At(i)).value_or(nullptr)),
                ShortName(gLookTarget.Read<ue3::Object*>(list->At(i)).value_or(nullptr)));
        }
    }
    if (looks != gC.lookAts) {
        gC.lookAts = looks;
        log::Info("convo: at {:.1f} s who looks at whom: {}", Seconds(), looks.empty() ? "nobody listed" : looks);
    }
    int const entry = gCurrentEntry.Read<int>(conv).value_or(-1);
    if (entry != gC.entry) {
        gC.entry = entry;
        if (entry >= 0 && gC.entryPath.size() < 400) {
            gC.entryPath.push_back(entry);
        }
    }
    // The wheel.
    ue3::Object* const handler = WheelHandler(controller);
    bool const wheel = handler != nullptr && gDisplayingWheel.ReadBool(handler).value_or(false);
    if (wheel != gC.wheel) {
        gC.wheel = wheel;
        if (wheel) {
            ++gC.wheelOpenings;
        }
        log::Info("convo: at {:.1f} s the dialogue wheel {}", Seconds(), wheel ? "is up" : "is gone");
    }
    // The actors' moves and turns.
    for (Tracked& t : gC.actors) {
        if (!ue3::runtime::IsAlive(t.actor)) {
            continue;
        }
        auto const at = player::Location(t.actor);
        auto const rot = player::Rotation(t.actor);
        if (!at || !rot) {
            continue;
        }
        ik::Vec3 const p = V(*at);
        double const yaw = YawDeg(*rot);
        t.maxMove = std::max(t.maxMove, Dist(p, t.start));
        t.maxTurn = std::max(t.maxTurn, std::abs(YawDiffDeg(yaw, t.startYaw)));
        if (t.headStart) {
            if (auto const head = HeadOf(t.actor)) {
                t.headMax = std::max(t.headMax, Dist(head->at, *t.headStart));
            }
        }
        double const moved = Dist(p, t.logged);
        double const turned = std::abs(YawDiffDeg(yaw, t.loggedYaw));
        if (moved > 5.0 || turned > 5.0) {
            log::Info("convo: at {:.1f} s {} {} to {} facing {:.0f} deg ({:.2f} m and {:.0f} deg from its start)", Seconds(), t.name,
                moved > 30.0 ? "JUMPED" : "moved", F(p), yaw, Dist(p, t.start) / kUnitsPerMeter, YawDiffDeg(yaw, t.startYaw));
            t.logged = p;
            t.loggedYaw = yaw;
        }
    }
    // A cut: the director's camera jumped.
    if (GetTickCount64() - gPov.ms < 500) {
        bool const cut = !gC.haveShot || Dist(V(gPov.lastLocation), V(gC.shotLocation)) > 30.0 ||
                         AngleDeg(Forward(gPov.lastRotation), Forward(gC.shotRotation)) > 12.0;
        if (cut) {
            ++gC.shots;
            gC.haveShot = true;
            LogShot(pawn, eyes);
        }
        gC.shotLocation = gPov.lastLocation;
        gC.shotRotation = gPov.lastRotation;
    }
}

void SetStatus(std::string json) {
    std::lock_guard lock(gStatusMutex);
    gStatusJson = std::move(json);
}

// The depth of field: the conversation camera's is focused for the director's shot (the door behind Chakwas blurred, then the
// whole room when the focus moved on, 2026-11-06), whatever its behaviour's m_bDOFSettingDisabled says (tried: no effect). The
// engine's own switch through the local player's console while the world view lasts (`scale set`, a boolean through its opposite
// first, research/35), and back to what the game's BIOEngine.ini said before (read first: the game writes the live value there
// during play, 2026-11-06, so a read afterwards finds our False).
bool gDofOff = false;
bool gDofWas = true;  // BIOEngine.ini's value read before we switched it (the game writes its live value there during play)

bool DofInIni() {
    // <exe dir>\..\..\BioGame\Config\BIOEngine.ini's [SystemSettings] (it wins over GamerSettings.ini, research/35).
    std::filesystem::path const ini = engine::SettingsFile().parent_path().parent_path().parent_path().parent_path() / L"BioGame" /
        L"Config" / L"BIOEngine.ini";
    ini::Loaded const loaded = ini::Read(ini);
    auto const v = ini::Get(loaded.file, "SystemSettings", "DepthOfField");
    return !v || ini::Lower(ini::Trim(*v)) != "false";
}

void DofOff() {
    if (gDofOff) {
        return;
    }
    gDofOff = true;
    gDofWas = DofInIni();
    std::string ignored;
    bool const ok = console_request::RunCommand("scale set DepthOfField True", &ignored) &&
                    console_request::RunCommand("scale set DepthOfField False", &ignored);
    log::Info("convo: depth of field off for the world view{}", ok ? "" : " (the console wasn't there: it stays as it is)");
}

void DofBack() {
    if (!gDofOff) {
        return;
    }
    if (gDofWas) {
        // Kept owed until both commands ran: StopWorld comes every tick outside a conversation, so a restore that
        // found no console (no local controller in a transition) is tried again, instead of DOF staying off for good
        // (the game writes the live value back into BIOEngine.ini; Codex review 2026-11-06, gameA#7).
        std::string ignored;
        bool const ok = console_request::RunCommand("scale set DepthOfField False", &ignored) &&
                        console_request::RunCommand("scale set DepthOfField True", &ignored);
        if (!ok) {
            return;
        }
        log::Info("convo: depth of field on again (BIOEngine.ini's [SystemSettings])");
    }
    gDofOff = false;
}

void StopWorld(Link const& link, char const* why) {
    EndWalks(why);
    gStayMs = 0;
    gCameraHold.on = false;
    DofBack();
    if (gPublished && link.publish != nullptr) {
        link.publish(nullptr);
    }
    if (gHeld) {
        view_anchor::Hold(std::nullopt);
        gHeld = false;
    }
    if (gInWorld) {
        log::Info("convo: the world view ends ({})", why);
    }
    gPublished = false;
    gInWorld = false;
    gInWorldForStay = false;
}

// ---- A moment in the world (research/69 "Authored scenes: a treatment per kind of track") --------------------------------------
// A level-script matinee that walks Shepard (the walk to the council with Anderson) runs in the cinematic game mode, with
// the director's camera. With worldMoments = walk the mod holds the view where the player stood on foot, as in a
// conversation; the track's move of his pawn is withheld (engine/ConversationHooks, asked through game/AuthoredMoment) and
// the stick walks him. What the script then does to his pawn (its teleport, the matinee putting him back at its end) is
// judged by the opening's rule: in the same space he is put back where his own walk has him, nothing is cut; to another
// composition the view goes with him under a dip. The on-foot record is kept fresh, so the conversation that follows
// opens from where he stands.
struct MomentView {
    bool on = false;
    std::uint64_t startMs = 0;
    std::uint64_t cinematicMs = 0;  // the last tick in the cinematic mode
    ik::Vec3 pawnAt{};              // where the player's own walk has Shepard
    std::optional<ik::Vec3> wanted; // where the script wants him (its teleport's destination in the same space): he walks there
    std::uint64_t wantedMs = 0;     // ... since when
    int arrivals = 0;               // times he got there (walked, or carried after the wait)
    double carriedUnits = 0.0;
    int putBack = 0;                // the script's moves of him undone (the same space)
    int cuts = 0;                   // ... accepted under a dip (another composition)
    std::uint32_t number = 0;
};
MomentView gMoment;
std::uint32_t gMoments = 0;

bool FootFresh(std::uint64_t now) {
    return gFoot.have && now >= gFoot.ms && now - gFoot.ms < 3000;
}

void EndMomentView(Link const& link, char const* why, bool stopWorld) {
    log::Info("moment: the first-person view of the moment ends ({}) after {:.1f} s: the player walked {:.2f} m and was carried {:.2f} m; "
              "the script's moves of Shepard: {} within the same space (he got there {} times), {} accepted under a dip",
        why, static_cast<double>(GetTickCount64() - gMoment.startMs) / 1000.0, gC.movedUnits / kUnitsPerMeter,
        gMoment.carriedUnits / kUnitsPerMeter, gMoment.putBack, gMoment.arrivals, gMoment.cuts);
    log::Info("moment: view summary {{\"seconds\": {:.1f}, \"walkedM\": {:.2f}, \"carriedM\": {:.2f}, \"sameSpace\": {}, \"arrivals\": {}, \"dips\": {}, "
              "\"why\": \"{}\"}}", static_cast<double>(GetTickCount64() - gMoment.startMs) / 1000.0, gC.movedUnits / kUnitsPerMeter,
        gMoment.carriedUnits / kUnitsPerMeter, gMoment.putBack, gMoment.arrivals, gMoment.cuts, why);
    if (ue3::Object* const controller = player::Controller()) {
        if (ue3::Object* const pawn = player::Pawn(controller); pawn != nullptr && gC.moving && ue3::runtime::IsAlive(pawn)) {
            WalkVelocity(pawn, {});
        }
    }
    gMoment = {};
    gMomentWalk = false;
    gC = {};
    if (stopWorld) {
        StopWorld(link, why);
    }
}

// True while the moment's view is up this tick (the caller returns).
bool TickMoment(Link const& link, ue3::Object* pawn, std::optional<std::uint8_t> mode, double deltaSeconds) {
    std::uint64_t const now = GetTickCount64();
    bool const cinematic = mode && *mode == 6;
    if (!gSettings.moments || CurrentMode() != cw::Mode::World || link.publish == nullptr || pawn == nullptr || !ue3::runtime::IsAlive(pawn)) {
        return false;
    }
    if (!gMoment.on) {
        std::uint64_t const track = authored_moment::LastPlayerTrackMs();
        auto const at = player::Location(pawn);
        if (!cinematic || track == 0 || now - track > 500 || !FootFresh(now) || !at) {
            return false;
        }
        gC = {};
        gMoment = {};
        gMoment.on = true;
        gMomentWalk = true;
        gMoment.startMs = now;
        gMoment.number = 900000 + ++gMoments;
        gMoment.pawnAt = V(*at);
        gC.world = true;
        gC.openingDone = true;
        gC.startMs = now;
        gC.number = gMoment.number;
        gC.hold = V(gFoot.location) + (V(*at) - V(gFoot.pawn));
        gC.holdRotation = gFoot.rotation;
        gC.holdFov = gFoot.fov;
        gC.holdFrom = "the view on foot";
        log::Info("moment: a matinee walks Shepard: held in first person at {} facing {:.0f} deg; {}", F(gC.hold), YawDeg(gC.holdRotation),
            gSettings.momentCarry ? "the track carries him, the view goes with him" : "its move of him is withheld, the stick walks him");
    }
    if (cinematic) {
        gMoment.cinematicMs = now;
    } else if (!(mode && *mode == 0 && now - gMoment.cinematicMs < 1000)) {
        return false;  // the cinematic mode is over (the caller ends the view)
    }
    // The script moved Shepard: the track carrying him (small steps: the view goes with him), a teleport, or the matinee
    // putting him back at its end (a jump).
    if (auto const at = player::Location(pawn)) {
        ik::Vec3 const here = V(*at);
        if (double const d = Dist(here, gMoment.pawnAt); d > 0.01 && d <= kJumpUnits) {
            gC.hold = gC.hold + (here - gMoment.pawnAt);
            gMoment.carriedUnits += d;
            gMoment.pawnAt = here;
        }
        if (Dist(here, gMoment.pawnAt) > kJumpUnits) {
            cw::OpeningFacts facts;
            facts.known = true;
            facts.markUnits = std::hypot(here.x - gMoment.pawnAt.x, here.y - gMoment.pawnAt.y);
            facts.riseUnits = here.z - gMoment.pawnAt.z;
            facts.sightToMark = LineOfSight(pawn, gC.hold, here + ik::Vec3{0.0, 0.0, 60.0});
            facts.floorAtMark = FloorZ(pawn, here).has_value();
            cw::Opening const rule = cw::DecideOpening(facts, cw::OpeningRule::Auto);
            if (rule != cw::Opening::Cut && PutAt(pawn, gMoment.pawnAt)) {
                // The same space: nothing is cut. But the script wants him there (the place may be what streams the next
                // area in, as at the council), so it is where he is wanted: he walks, and past the wait he is carried.
                ++gMoment.putBack;
                gMoment.wanted = here;
                gMoment.wantedMs = now;
                gC.wantedMark = here;
                log::Info("moment: the script moved Shepard {:.2f} m ({:+.2f} m up, {}): the same space, no cut; he is wanted at {} and {}",
                    facts.markUnits / kUnitsPerMeter, facts.riseUnits / kUnitsPerMeter, facts.sightToMark ? "in sight" : "out of sight", F(here),
                    gSettings.momentCarry ? "is carried there" : std::format("walks there (carried after {:.0f} s)", gSettings.momentPatienceS));
            } else {
                gC.hold = gC.hold + (here - gMoment.pawnAt);
                gMoment.pawnAt = here;
                ++gMoment.cuts;
                ++gC.cut;
                log::Info("moment: the script moved Shepard {:.2f} m ({:+.2f} m up, {}): another composition, the view goes with him under a dip",
                    facts.markUnits / kUnitsPerMeter, facts.riseUnits / kUnitsPerMeter, facts.sightToMark ? "in sight" : "out of sight");
            }
        }
    }
    // Where the script wants him: there when he has walked within a step of it, or carried once the wait is over (or at
    // once with worldMoments = carry), in a straight line at worldMomentCarryMS; then he stands exactly there, as its
    // teleport would have put him.
    if (gMoment.wanted) {
        ik::Vec3 const to = *gMoment.wanted - gMoment.pawnAt;
        double const left = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
        double const flat = std::hypot(to.x, to.y);
        bool const close = flat <= 150.0;
        bool const waited = static_cast<double>(now - gMoment.wantedMs) / 1000.0 >= gSettings.momentPatienceS;
        if (close || waited || gSettings.momentCarry) {
            double const step = gSettings.momentCarryMS * kUnitsPerMeter * std::clamp(deltaSeconds, 0.0, 0.1);
            ik::Vec3 const next = left <= step ? *gMoment.wanted : gMoment.pawnAt + to * (step / left);
            if (PutAt(pawn, next)) {
                gC.hold = gC.hold + (next - gMoment.pawnAt);
                gMoment.carriedUnits += Dist(next, gMoment.pawnAt);
                gMoment.pawnAt = next;
            } else if (PutAt(pawn, *gMoment.wanted)) {
                // Something in the straight line: the last resort, there at once under a dip.
                gC.hold = gC.hold + (*gMoment.wanted - gMoment.pawnAt);
                gMoment.pawnAt = *gMoment.wanted;
                ++gMoment.cuts;
                ++gC.cut;
                log::Info("moment: the way to where the script wants Shepard is blocked: put there under a dip");
            }
            if (Dist(gMoment.pawnAt, *gMoment.wanted) < 1.0) {
                ++gMoment.arrivals;
                log::Info("moment: Shepard is where the script wants him ({}): {}", F(*gMoment.wanted),
                    close && !waited && !gSettings.momentCarry ? "he walked there" : "carried there");
                gMoment.wanted.reset();
                gC.wantedMark.reset();
            }
        }
    }
    std::optional<engine::TrackingFrame> frame;
    vr::host_view::State const view = vr::host_view::Latest();
    if (view.valid && gInWorld) {
        frame = engine::FromCameraView(engine::UnrealVector{gC.hold.x, gC.hold.y, gC.hold.z},
            engine::UnrealRotation{gC.holdRotation.pitch, gC.holdRotation.yaw, gC.holdRotation.roll}, {view.lookPitch, view.lookYaw, 0},
            {view.offsetForward, view.offsetRight, view.offsetUp}, view.head, view.unitsPerMeter);
        if (view.centreValid) frame->headWorld.z = view.centreZ;
        MovePlayer(pawn, *frame, view, deltaSeconds);
    } else if (gFoot.frame) {
        frame = gFoot.frame;
        frame->headWorld.x += gC.hold.x - gFoot.location.x;
        frame->headWorld.y += gC.hold.y - gFoot.location.y;
        frame->headWorld.z += gC.hold.z - gFoot.location.z;
        MovePlayer(pawn, *frame, view, deltaSeconds);
    }
    if (auto const at = player::Location(pawn)) {
        gMoment.pawnAt = V(*at);  // after his own step
    }
    if (frame) {
        view_anchor::Hold(frame, gFoot.floorView);
        gHeld = true;
    }
    cw::State state;
    state.active = 1;
    state.conversation = gMoment.number;
    state.cut = gC.cut;
    if (gPov.location != nullptr && now - gPov.ms < 500) {
        state.holdRender = 1;
        state.holdLocation[0] = static_cast<float>(gC.hold.x);
        state.holdLocation[1] = static_cast<float>(gC.hold.y);
        state.holdLocation[2] = static_cast<float>(gC.hold.z);
        state.holdRotation[0] = gC.holdRotation.pitch;
        state.holdRotation[1] = gC.holdRotation.yaw;
        state.holdRotation[2] = gC.holdRotation.roll;
        state.holdFov = gC.holdFov;
        state.povLocationAddress = reinterpret_cast<std::uint64_t>(gPov.location);
        state.povRotationAddress = reinterpret_cast<std::uint64_t>(gPov.rotation);
        state.povFovAddress = reinterpret_cast<std::uint64_t>(gPov.fov);
    }
    gCameraHold.on = gSettings.camera && state.holdRender != 0;
    gCameraHold.location = {static_cast<float>(gC.hold.x), static_cast<float>(gC.hold.y), static_cast<float>(gC.hold.z)};
    gCameraHold.rotation = gC.holdRotation;
    gCameraHold.fov = gC.holdFov;
    link.publish(&state);
    gPublished = true;
    ++gWorldTicks;
    if (!gInWorld) {
        gInWorld = true;
        log::Info("moment: in the world: the view held at {} (yaw {:.0f}); arm IK and body follow {}", F(gC.hold), YawDeg(gC.holdRotation),
            frame ? "on" : "waiting for the host's world view");
    }
    // The on-foot record stays fresh: the conversation that follows opens from where the player stands.
    gFoot.location = EnginePoint(gC.hold);
    gFoot.rotation = gC.holdRotation;
    gFoot.pawn = EnginePoint(gMoment.pawnAt);
    gFoot.have = true;
    gFoot.ms = now;
    SetStatus(std::format("{{\"on\": true, \"world\": true, \"moment\": {}, \"held\": [{:.1f}, {:.1f}, {:.1f}], \"putBack\": {}, \"cuts\": {}}}",
        gMoment.number, gC.hold.x, gC.hold.y, gC.hold.z, gMoment.putBack, gMoment.cuts));
    return true;
}

} // namespace

// The move track's question, from game/AuthoredMoment's provider (inside the engine's physics step: reads only): the track's
// move of the player's pawn is withheld while a moment's view is up, and from its first call when one can start (the
// cinematic game mode, a fresh place on foot).
bool MomentViewOn() {
    return gMoment.on;
}

bool MomentWithholds() {
    if (!gSettings.moments || CurrentMode() != cw::Mode::World) {
        return false;
    }
    if (gSettings.momentCarry) {
        return false;  // the track carries him; the view goes with him
    }
    if (gMoment.on) {
        return true;
    }
    ue3::Object* const controller = player::Controller();
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    return mode && *mode == 6 && FootFresh(GetTickCount64()) && !gC.on;
}

void TryStartup() {
    ue3::Function* const update = ue3::runtime::Ready() ? gUpdateCamera.Get() : nullptr;
    if (update == nullptr) {
        log::Warn("convo: Engine.Camera.UpdateCamera not found: conversations can't be shown in the world (measurements only)");
        return;
    }
    engine::process_event::AddPostHandler(update, &OnUpdateCamera);
}

// A scene whose shots are framed from far off is composed for a camera somewhere else (research/59's survey: the framed set
// pieces; research/69 item 3): at least three shots and two thirds of them from more than 8 m away. Once per conversation.
void CheckFarShots() {
    if (!gC.world || gSettings.autoScreen == 0 || !gC.absentWhy.empty() || gC.shots < 3 || gC.shotsFar * 3 < gC.shots * 2) {
        return;
    }
    gC.absentWhy = std::format("{} of the first {} shots are framed from more than 8 m away (the farthest {:.0f} m)", gC.shotsFar, gC.shots,
        gC.cameraFarM);
    gC.autoScreened = gSettings.autoScreen == 2;
    log::Info("convo: at {:.1f} s {}: a scene composed for a camera elsewhere; {}", Seconds(), gC.absentWhy,
        gC.autoScreened ? "moved to the 3D screen (worldAutoScreen = on)" : "logged only (worldAutoScreen = log), still in the world");
    if (gC.autoScreened) {
        gC.setPiece = true;
        gC.world = false;  // people are no longer kept: the lines' facing tracks put them back on their marks
    }
}

bool WantsHooks() {
    return CurrentMode() == cw::Mode::World;
}

void Tick(double deltaSeconds, Link const& link) {
    RefreshSettings();
    engine::conversation_hooks::SetKeepProvider(CurrentMode() == cw::Mode::World ? &KeepAtSource : nullptr);
    if (!gSettings.stay || gSettings.walk == 0) {
        EndWalks("worldStay or worldWalk disabled");
    }
    ue3::Object* const controller = player::Controller();
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    if (mode && *mode == 0) {
        if (auto const frame = view_anchor::Current()) {
            gFoot.frame = frame;
            gFoot.floorView = vr::host_view::Latest();
        }
    }
    ue3::Object* const conv = controller != nullptr && mode && *mode == kModeConversation ? ConversationNow(controller) : nullptr;
    // A moment in the world (a matinee that walks Shepard): its own view, until the cinematic mode ends or a conversation begins.
    if (conv == nullptr && TickMoment(link, pawn, mode, deltaSeconds)) {
        return;
    }
    if (gMoment.on) {
        EndMomentView(link, conv != nullptr ? "a conversation began" : "the cinematic mode ended", conv == nullptr);
    }
    if (conv == nullptr) {
        End(mode && *mode == kModeConversation ? "no conversation object" : "the game left its conversation mode");
        StopWorld(link, "the conversation ended");
        SetStatus(std::format("{{\"on\": false, \"mode\": \"{}\"}}", cw::Name(CurrentMode())));
        return;
    }
    if (!gC.on || gC.object != conv) {
        End("another conversation began");
        Begin(conv, controller);
    }
    // The player's place follows Shepard's head bone when it jumps (a stage's placement, a cut to a new mark): re-anchored past
    // kReanchorUnits, never for the head's own small moves (gestures, breathing), which the player's real head replaces.
    constexpr double kReanchorUnits = 80.0;  // 0.4 m: his own steps in the comm room moved the player 4 times (2026-11-06)
    auto const shepardHead = HeadOf(pawn);
    // Settled over 3 ticks: a pawn put back (people kept in place) drags his mesh there and back within a tick.
    constexpr int kSettleTicks = 3;
    if (shepardHead && gC.settleUntilMs != 0) {
        if (GetTickCount64() < gC.settleUntilMs) {
            gC.anchorHead = shepardHead->at;
            gC.follow = {{gC.hold.x, gC.hold.y, gC.hold.z}, {shepardHead->at.x, shepardHead->at.y, shepardHead->at.z}, 0};
        } else {
            gC.settleUntilMs = 0;
            gC.eyeToViewMax = 0.0;  // measured from here on: before, the stage had him at the mark and his mesh elsewhere
        }
    } else if (shepardHead && gC.anchorHead && gSettings.follow && gC.opening != cw::Opening::Cut &&
               Dist(shepardHead->at, *gC.anchorHead) < kStayMaxUnits) {
        // After an opening without a cut the player's place is his own: Shepard's body is where the player walked him, and what
        // his head bone does there (the hearing's animation moved it 0.88 m twice and the view went with it, 2026-11-10) is not
        // the player's move. Only a move past 6 m (the script put him elsewhere: a scripted entrance) is followed, below.
        gC.anchorHead = shepardHead->at;
        gC.follow = {{gC.hold.x, gC.hold.y, gC.hold.z}, {shepardHead->at.x, shepardHead->at.y, shepardHead->at.z}, 0};
    } else if (shepardHead && gC.anchorHead && gSettings.follow) {
        ik::Vec3 const a = *gC.anchorHead;
        ik::Vec3 const& h = shepardHead->at;
        if (cw::FollowHeadSettled(gC.follow, {h.x, h.y, h.z}, kReanchorUnits, kSettleTicks)) {
            log::Info("convo: at {:.1f} s Shepard's head bone JUMPED {:.2f} m: the player's place moves with it to ({:.0f}, {:.0f}, {:.0f})",
                Seconds(), Dist(h, a) / kUnitsPerMeter, gC.follow.place.x, gC.follow.place.y, gC.follow.place.z);
            gC.hold = {gC.follow.place.x, gC.follow.place.y, gC.follow.place.z};
            gC.anchorHead = h;
            ++gC.reanchors;
            if (gC.opening != cw::Opening::Cut) {
                ++gC.cut;  // the script moved him to another place in a scene that had no cut so far: under a dip
                gC.wantedMark.reset();
            }
        }
    }
    ik::Vec3 const eyes = gC.hold;
    std::uint64_t const now = GetTickCount64();
    if (now >= gNextSampleMs) {
        gNextSampleMs = now + 100;
        Sample(conv, controller, pawn, eyes);
        CheckFarShots();
    }
    if (gC.sourceKept > 0 && !gC.sourceLogged) {
        gC.sourceLogged = true;
        log::Info("convo: at {:.1f} s the game's placement of a person onto a mark {:.2f} m away was withheld at its source "
                  "(UBioConversation::SetPositionRotator; the facing applied): nobody jumps, nothing to undo", Seconds(),
            gC.sourceFarUnits / kUnitsPerMeter);
    }
    // ---- The world mode ----
    bool const world = CurrentMode() == cw::Mode::World && !gC.setPiece && link.publish != nullptr;  // live, as the 3D screen's setting
    if (!world) {
        StopWorld(link, gC.setPiece ? "a set piece: on the 3D screen" : CurrentMode() == cw::Mode::World ? "an older host" : "not in the world mode");
        SetStatus(std::format("{{\"on\": true, \"world\": false, \"conversation\": {}, \"shots\": {}, \"speaker\": \"{}\"}}", gC.number,
            gC.shots, ShortName(gC.speaker)));
        return;
    }
    if (!gC.openingDone && (gInWorld || now >= gC.openingUntilMs)) {
        FinishOpening(conv, pawn);
    }
    // The room as the held view maps it: the host's view (its head pose and head look) placed on the held view, as view_anchor does
    // on the game camera; before the host shows the world (its first frames), the frame on foot moved to the held place.
    std::optional<engine::TrackingFrame> frame;
    vr::host_view::State const view = vr::host_view::Latest();
    if (view.valid && gInWorld) {
        frame = engine::FromCameraView(engine::UnrealVector{gC.hold.x, gC.hold.y, gC.hold.z},
            engine::UnrealRotation{gC.holdRotation.pitch, gC.holdRotation.yaw, gC.holdRotation.roll}, {view.lookPitch, view.lookYaw, 0},
            {view.offsetForward, view.offsetRight, view.offsetUp}, view.head, view.unitsPerMeter);
        if (view.centreValid) frame->headWorld.z = view.centreZ;
        MovePlayer(pawn, *frame, view, deltaSeconds);  // the player's walk: the pawn, the hold and this frame move together
    } else if (gFoot.frame && gC.holdFrom == std::string_view("the view on foot")) {
        frame = gFoot.frame;
        frame->headWorld.x += gC.hold.x - gFoot.location.x;
        frame->headWorld.y += gC.hold.y - gFoot.location.y;
        frame->headWorld.z += gC.hold.z - gFoot.location.z;
        MovePlayer(pawn, *frame, view, deltaSeconds);  // without a VR view (a flat game): the held view's yaw, `mevr convo walk`
    }
    if (frame) {
        view_anchor::Hold(frame, gFoot.floorView);
        gHeld = true;
    }
    if (shepardHead) {
        gC.eyeToViewMax = std::max(gC.eyeToViewMax, Dist(shepardHead->at, eyes));
    }
    cw::State state;
    state.active = 1;
    state.conversation = gC.number;
    state.speakerSerial = gC.speakerSerial;
    state.wheel = gC.wheel ? 1 : 0;
    state.wheelHand = static_cast<std::uint8_t>(gSettings.wheelHand);
    state.wheelWidthM = gSettings.wheelWidthM;
    state.wheelDistanceM = gSettings.wheelDistanceM;
    state.subtitleHeightDeg = gSettings.subtitleHeightDeg;
    std::copy(std::begin(gSettings.subtitleCrop), std::end(gSettings.subtitleCrop), std::begin(state.subtitleCrop));
    std::copy(std::begin(gSettings.wheelCrop), std::end(gSettings.wheelCrop), std::begin(state.wheelCrop));
    // The subtitle under the speaker's face; Shepard's own lines under the one he talks to.
    ue3::Object* face = gC.speaker;
    if (face == pawn || face == nullptr) {
        face = gListener.Read<ue3::Object*>(conv).value_or(nullptr);
    }
    if (face != nullptr && face != pawn && frame) {
        if (auto const point = FacePoint(face)) {
            XrVector3f const faceT = engine::ToTracking(*frame, engine::UnrealVector{point->x, point->y, point->z});
            XrVector3f out{};
            if (cw::SubtitlePoint(faceT, frame->head.position, gSettings.subtitleBelowM, gSettings.subtitleTowardM, out)) {
                state.subtitle = 1;
                state.subtitleAt[0] = out.x;
                state.subtitleAt[1] = out.y;
                state.subtitleAt[2] = out.z;
            }
        }
    }
    state.cut = gC.cut;
    if (gPov.location != nullptr && now - gPov.ms < 500) {
        state.holdRender = 1;
        state.holdLocation[0] = static_cast<float>(gC.hold.x);
        state.holdLocation[1] = static_cast<float>(gC.hold.y);
        state.holdLocation[2] = static_cast<float>(gC.hold.z);
        state.holdRotation[0] = gC.holdRotation.pitch;
        state.holdRotation[1] = gC.holdRotation.yaw;
        state.holdRotation[2] = gC.holdRotation.roll;
        state.holdFov = gC.holdFov;
        state.povLocationAddress = reinterpret_cast<std::uint64_t>(gPov.location);
        state.povRotationAddress = reinterpret_cast<std::uint64_t>(gPov.rotation);
        state.povFovAddress = reinterpret_cast<std::uint64_t>(gPov.fov);
    }
    gCameraHold.on = gSettings.camera && state.holdRender != 0;
    gCameraHold.location = {static_cast<float>(gC.hold.x), static_cast<float>(gC.hold.y), static_cast<float>(gC.hold.z)};
    gCameraHold.rotation = gC.holdRotation;
    gCameraHold.fov = gC.holdFov;
    DofOff();
    link.publish(&state);
    gPublished = true;
    ++gWorldTicks;
    if (gInWorldForStay && gSettings.stay) {
        CheckDrawn();
    }
    gInWorldForStay = true;
    if (!gInWorld) {
        gInWorld = true;
        auto const& v = gFoot.floorView;
        auto const basis = engine::ToBasis(engine::UnrealRotation{gC.holdRotation.pitch, gC.holdRotation.yaw, gC.holdRotation.roll});
        double const commonZ = basis.right.z * v.offsetRight + basis.up.z * v.offsetUp + basis.forward.z * v.offsetForward;
        log::Info("floor lock: conversation hold start: locked {}, held base Z {:.3f}, common Z {:+.3f}, "
                  "render centre Z {:.3f}, IK head Z {:.3f}, floor Z {:.3f}",
            v.floorLocked, gC.hold.z, commonZ, gC.hold.z + commonZ, frame ? frame->headWorld.z : 0.0, v.pawnFloorZ);
        log::Info("convo: conversation {} in the world: the view held at {} ({}, yaw {:.0f}){}; arm IK and body follow {}", gC.number,
            F(gC.hold), gC.holdFrom, YawDeg(gC.holdRotation), gSettings.follow ? ", following his head bone's jumps" : "",
            frame ? "on" : "waiting for the host's world view");
    }
    SetStatus(std::format("{{\"on\": true, \"world\": true, \"conversation\": {}, \"shots\": {}, \"speaker\": \"{}\", \"wheel\": {}, "
                          "\"subtitle\": {}, \"subtitleAt\": [{:.3f}, {:.3f}, {:.3f}], \"held\": [{:.1f}, {:.1f}, {:.1f}], \"heldRender\": {}, "
                          "\"reanchors\": {}, \"frame\": {}, \"from\": \"{}\", \"kept\": {}, \"letGo\": {}, \"walks\": {}}}",
        gC.number, gC.shots, ShortName(gC.speaker), state.wheel != 0 ? "true" : "false", state.subtitle != 0 ? "true" : "false",
        state.subtitleAt[0], state.subtitleAt[1], state.subtitleAt[2], gC.hold.x, gC.hold.y, gC.hold.z, state.holdRender != 0 ? "true" : "false",
        gC.reanchors, frame ? "true" : "false", gC.holdFrom, gC.kept, gC.letGo, gC.walks));
}

bool InWorld() {
    return gInWorld;
}

std::string Command(std::string const& rest) {
    std::istringstream words(rest);
    std::string verb;
    words >> verb;
    if (verb == "world") {
        std::string what;
        words >> what;
        if (what == "off" || what == "file") {
            gModeOverride.reset();
            return std::format("convo: the mode from the file again: {}", cw::Name(gSettings.mode));
        }
        gModeOverride = what == "on" || what.empty() ? cw::Mode::World : cw::ParseMode(what.c_str(), cw::Mode::World);
        return std::format("convo: conversations {} until `mevr convo world off` (now)", cw::Name(*gModeOverride));
    }
    if (verb == "walk") {
        // Tests without controllers: `mevr convo walk <x> <y> [seconds]` is the left stick for that long (default 3 s); `walk off`.
        std::string x;
        words >> x;
        if (x == "off" || x.empty()) {
            gWalkOverride = {};
            return x.empty() ? std::string("usage: mevr convo walk <x> <y> [seconds] | off") : std::string("convo: the walk override is off");
        }
        double sx = 0.0, sy = 0.0, seconds = 3.0;
        std::string y, s;
        words >> y >> s;
        auto const px = ini::Number(x), py = ini::Number(y);
        if (!px || !py) {
            return "usage: mevr convo walk <x> <y> [seconds] | off";
        }
        sx = std::clamp(*px, -1.0, 1.0);
        sy = std::clamp(*py, -1.0, 1.0);
        if (auto const ps = s.empty() ? std::nullopt : ini::Number(s)) {
            seconds = std::clamp(*ps, 0.1, 60.0);
        }
        gWalkOverride = {static_cast<float>(sx), static_cast<float>(sy), GetTickCount64() + static_cast<std::uint64_t>(seconds * 1000.0)};
        return std::format("convo: the stick is ({:.2f}, {:.2f}) for {:.1f} s (worldMove {}, world mode {}, conversation {})", sx, sy, seconds,
            gSettings.move ? "on" : "off", gInWorld ? "up" : "not up", gC.on ? "running" : "none");
    }
    if (verb == "pick" || verb == "skip") {
        // Tests without controllers: the wheel's own reply pick (a wheel location 0..5, as the stick and A choose) or its skip
        // of the current line (what A / X do while a line plays).
        ue3::Object* const handler = WheelHandler(player::Controller());
        ue3::Function* const f = verb == "pick" ? gSelectEntry.Get() : gSkipConversation.Get();
        if (handler == nullptr || f == nullptr) {
            return std::format("convo: no conversation wheel handler{}", f == nullptr ? " (or its function wasn't found)" : "");
        }
        // Only on the game's own terms: a pick while the wheel is up and on a slot it uses, a skip while a line plays. A pick
        // between lines crashed the game inside the script (2026-11-06 12:23, research/59: it reads an empty reply list).
        bool const wheelUp = gDisplayingWheel.ReadBool(handler).value_or(false);
        int location = 0;
        if (verb == "pick") {
            words >> location;
            location = std::clamp(location, 0, 5);
            std::uint8_t const* const used = gSlotsUsed.At<std::uint8_t>(handler, location);
            if (!wheelUp || used == nullptr || *used == 0) {
                return std::format("convo: no pick: {}", !wheelUp ? "the wheel isn't up" : std::format("slot {} isn't used", location));
            }
        } else if (wheelUp) {
            return "convo: no skip: the wheel is up (pick a reply)";
        }
        ue3::Params parms(ue3::runtime::Get(), f);
        if (verb == "pick") {
            parms.Set("nWheelLocation", static_cast<std::uint8_t>(location));
        }
        bool const called = ue3::runtime::Call(handler, f, parms);
        return std::format("convo: {} called {}{}", verb == "pick" ? std::format("SelectConversationEntry({})", location) : "SkipConversation",
            called, parms.ReturnBool() ? std::format(", returned {}", *parms.ReturnBool()) : std::string());
    }
    if (verb == "end") {
        // The conversation's own end (tests: a survey's conversation that loops past its time).
        if (!gC.on || gC.object == nullptr || !ue3::runtime::IsAlive(gC.object) || gEndConversation.Get() == nullptr) {
            return "convo: no conversation to end";
        }
        ue3::Params p(ue3::runtime::Get(), gEndConversation.Get());
        bool const called = ue3::runtime::Call(gC.object, gEndConversation.Get(), p);
        return std::format("convo: EndConversation called {}", called);
    }
    if (verb == "dump" || verb.empty()) {
        auto const gameMode = player::GameMode(player::Controller());
        std::string out = std::format("convo: mode {}; game mode {}; ", cw::Name(CurrentMode()), gameMode ? static_cast<int>(*gameMode) : -1);
        if (!gC.on) {
            return out + "no conversation";
        }
        out += std::format("conversation {} ({}), {:.1f} s, {} shots, speaker {}, entry {}, wheel {}; actors:", gC.number, gC.name,
            Seconds(), gC.shots, ShortName(gC.speaker), gC.entry, gC.wheel ? "up" : "down");
        for (Tracked const& t : gC.actors) {
            auto const at = ue3::runtime::IsAlive(t.actor) ? player::Location(t.actor) : std::nullopt;
            out += std::format(" {} {}", t.name, at ? F(V(*at)) : std::string("gone"));
        }
        return out;
    }
    return "usage: mevr convo [dump | world on|off|screen|vr | pick <0..5> | skip | end]";
}

std::string StatusJson() {
    std::lock_guard lock(gStatusMutex);
    return gStatusJson;
}

void LogStats() {
    if (gInWorld) {
        log::Info("convo: in the world for {} ticks (conversation {}, {} shots so far)", gWorldTicks, gC.number, gC.shots);
    }
}

void Shutdown(Link const& link) {
    engine::conversation_hooks::SetKeepProvider(nullptr);
    End("the mod unloads");
    StopWorld(link, "the mod unloads");
}

} // namespace mevr::game::conversation_world
