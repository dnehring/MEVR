// SPDX-License-Identifier: MIT
// LE1 conversations in the world, first person: the game's side (game/ConversationWorld.hpp, research/59).
#include "game/ConversationWorld.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/ik/Rigid.hpp"
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
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
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
ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera"};
ue3::FunctionRef gFastTrace{"Engine.Actor.FastTrace"};

// ---- Settings ([Cinematics] in LE1MEVR.ini, read every second) -----------------------------------------------------------------
struct Settings {
    cw::Mode mode = cw::Mode::Screen;  // the shipped default (research/52); world is opt-in
    bool follow = true;                // the held view moves with Shepard's pawn
    float subtitleHeightDeg = 3.0f;
    float subtitleBelowM = 0.22f;
    float subtitleTowardM = 0.15f;
    int wheelHand = 0;                 // 0 left, 1 right, 2 in front
    float wheelWidthM = 0.40f;
    float wheelDistanceM = 0.12f;
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
        s.wheelHand = w == "right" || w == "1" ? 1 : w == "front" || w == "2" ? 2 : 0;
    }
    s.wheelWidthM = number("wheelWidthM", s.wheelWidthM, 0.15f, 1.5f);
    s.wheelDistanceM = number("wheelDistanceM", s.wheelDistanceM, 0.05f, 0.8f);
    ReadCrop(loaded.file, "subtitleCrop", s.subtitleCrop);
    ReadCrop(loaded.file, "wheelCrop", s.wheelCrop);
    std::string const text = std::format("conversations = {}, worldFollow = {}, subtitles {:.1f} deg tall {:.2f} m under the face, the wheel "
                                         "at the {} hand {:.2f} m wide", cw::Name(s.mode), s.follow ? 1 : 0, s.subtitleHeightDeg,
        s.subtitleBelowM, s.wheelHand == 0 ? "left" : s.wheelHand == 1 ? "right" : "front:", s.wheelWidthM);
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
};
Foot gFoot;
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
        gFoot.fov = *pov->fov;
        if (auto const p = player::Location(player::Pawn(controller))) {
            gFoot.pawn = *p;
        }
        gFoot.have = true;
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
    std::string name;
    std::uint64_t startMs = 0;
    std::uint32_t number = 0;
    bool world = false;          // shown in the world
    ue3::Vector pawnAtStart{};   // Shepard's pawn at the last on-foot camera update (the hold's reference)
    std::vector<Tracked> actors;
    ue3::Object* speaker = nullptr;
    std::uint32_t speakerSerial = 0;
    std::string subtitle;
    std::string lookAts;
    int entry = -1;
    // Shots: each cut of the director's camera.
    int shots = 0;
    ue3::Vector shotLocation{};
    ue3::Rotator shotRotation{};
    bool haveShot = false;
    std::map<std::string, int> shotKinds;
    int shotsHidden = 0;   // shots whose subject can't be seen from Shepard's eyes
    int wheelOpenings = 0;
    bool wheel = false;
    double eyeToViewMax = 0.0;  // Shepard's head bone from the held view's eyes (the player's), largest
};
Convo gC;
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

// The camera cut to a new shot: what it shows against Shepard's place.
void LogShot(ue3::Object* pawn, ik::Vec3 eyes) {
    ik::Vec3 const cam = V(gPov.lastLocation);
    ik::Vec3 const fwd = Forward(gPov.lastRotation);
    // The subject: the conversation's actor nearest the camera's centre ray (perpendicular distance at its distance).
    Tracked const* subject = nullptr;
    double best = 1e9;
    ik::Vec3 subjectFace{};
    for (Tracked const& t : gC.actors) {
        auto const face = FacePoint(t.actor);
        if (!face) {
            continue;
        }
        ik::Vec3 const d = *face - cam;
        double const along = ik::Dot(d, fwd);
        if (along <= 10.0) {
            continue;
        }
        double const off = ik::Length(d - fwd * along);
        if (off < best) {
            best = off;
            subject = &t;
            subjectFace = *face;
        }
    }
    double const camToEyes = Dist(cam, eyes) / kUnitsPerMeter;
    bool const subjectIsShepard = subject != nullptr && subject->actor == pawn;
    std::string kind;
    std::string seen;
    if (subject == nullptr || best > 120.0) {
        kind = "cutaway (no actor of the conversation near the frame's centre)";
        ++gC.shotsHidden;
    } else if (camToEyes < 0.4) {
        kind = "from Shepard's eyes";
    } else if (subjectIsShepard) {
        kind = "on Shepard (the reverse: not seen from his own eyes)";
        ++gC.shotsHidden;
    } else {
        bool const visible = LineOfSight(pawn, eyes, subjectFace);
        double const offAxis = AngleDeg(subjectFace - cam, subjectFace - eyes);
        kind = camToEyes < 1.5 && AngleDeg(fwd, subjectFace - eyes) < 25.0 ? "over Shepard's shoulder" : "on " + subject->name;
        seen = std::format(", {} from Shepard's eyes ({:.1f} m away), the camera {:.0f} deg off his line to it",
            visible ? "visible" : "BLOCKED", Dist(eyes, subjectFace) / kUnitsPerMeter, offAxis);
        if (!visible) {
            ++gC.shotsHidden;
        }
    }
    std::string const key = kind.substr(0, kind.find(' ', kind.find(' ') + 1));
    ++gC.shotKinds[kind.starts_with("on ") && !subjectIsShepard ? "on an NPC" : key];
    log::Info("convo: shot {} at {:.1f} s: {}{}; the camera at {} ({:.2f} m from Shepard's eyes), {:.0f} deg lens, subject {} {:.0f} units off "
              "the centre ray; speaker {}",
        gC.shots, Seconds(), kind, seen, F(cam), camToEyes, gPov.lastFov, subject != nullptr ? subject->name : "none", best,
        ShortName(gC.speaker));
}

void Begin(ue3::Object* conv, ue3::Object* controller) {
    gC = {};
    gC.on = true;
    gC.object = conv;
    gC.name = ue3::runtime::FullName(conv);
    gC.startMs = GetTickCount64();
    gC.number = ++gConversations;
    gC.pawnAtStart = gFoot.pawn;
    gC.world = CurrentMode() == cw::Mode::World;
    ue3::Object* const pawn = player::Pawn(controller);
    ue3::Object* const stage = gStage.Read<ue3::Object*>(conv).value_or(nullptr);
    log::Info("convo: conversation {} began: {} (owner {}, player {}, stage {}{}{}); shown {}", gC.number, gC.name,
        ShortName(gOwner.Read<ue3::Object*>(conv).value_or(nullptr)), ShortName(gConvPlayer.Read<ue3::Object*>(conv).value_or(nullptr)),
        ShortName(stage), stage != nullptr && player::Location(stage) ? " at " + F(V(*player::Location(stage))) : std::string(),
        gUseFixedCameras.ReadBool(conv).value_or(false) ? ", fixed cameras" : "", gC.world ? "in the world (first person)" : cw::Name(CurrentMode()));
    if (gFoot.have) {
        auto const nowPawn = player::Location(pawn);
        log::Info("convo: Shepard's last view on foot at {} yaw {:.0f}, his pawn then at {}{}", F(V(gFoot.location)), YawDeg(gFoot.rotation),
            F(V(gFoot.pawn)), nowPawn ? std::format(", now at {} ({:.2f} m from there)", F(V(*nowPawn)), Dist(V(*nowPawn), V(gFoot.pawn)) / kUnitsPerMeter)
                                      : std::string());
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
        log::Info("convo: in the world: Shepard's head bone was at most {:.2f} m from the player's held eyes", gC.eyeToViewMax / kUnitsPerMeter);
    }
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

void StopWorld(Link const& link, char const* why) {
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
}

} // namespace

void TryStartup() {
    ue3::Function* const update = ue3::runtime::Ready() ? gUpdateCamera.Get() : nullptr;
    if (update == nullptr) {
        log::Warn("convo: Engine.Camera.UpdateCamera not found: conversations can't be shown in the world (measurements only)");
        return;
    }
    engine::process_event::AddPostHandler(update, &OnUpdateCamera);
}

void Tick(double /*deltaSeconds*/, Link const& link) {
    RefreshSettings();
    ue3::Object* const controller = player::Controller();
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    if (mode && *mode == 0) {
        if (auto const frame = view_anchor::Current()) {
            gFoot.frame = frame;
        }
    }
    ue3::Object* const conv = controller != nullptr && mode && *mode == kModeConversation ? ConversationNow(controller) : nullptr;
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
    // Shepard's eyes: the held view's (the player's) in the world mode, else his head bone.
    ik::Vec3 eyes = V(gFoot.location);
    auto const pawnNow = pawn != nullptr ? player::Location(pawn) : std::nullopt;
    cw::Vec3 held{gFoot.location.x, gFoot.location.y, gFoot.location.z};
    if (pawnNow) {
        held = cw::HeldLocation({gFoot.location.x, gFoot.location.y, gFoot.location.z}, {gC.pawnAtStart.x, gC.pawnAtStart.y, gC.pawnAtStart.z},
            {pawnNow->x, pawnNow->y, pawnNow->z}, gSettings.follow);
        eyes = {held.x, held.y, held.z};
    }
    auto const shepardHead = HeadOf(pawn);
    if (!gFoot.have && shepardHead) {
        eyes = shepardHead->at;
    }
    std::uint64_t const now = GetTickCount64();
    if (now >= gNextSampleMs) {
        gNextSampleMs = now + 100;
        Sample(conv, controller, pawn, eyes);
    }
    // ---- The world mode ----
    bool const world = gC.world && CurrentMode() == cw::Mode::World && link.publish != nullptr && gFoot.have && gFoot.frame.has_value();
    if (!world) {
        StopWorld(link, gC.world && link.publish == nullptr ? "an older host" : gC.world && !gFoot.have ? "Shepard's view on foot isn't known"
                                                                                                       : "not in the world mode");
        SetStatus(std::format("{{\"on\": true, \"world\": false, \"conversation\": {}, \"shots\": {}, \"speaker\": \"{}\"}}", gC.number,
            gC.shots, ShortName(gC.speaker)));
        return;
    }
    // The held frame: the on-foot one, moved with the pawn.
    engine::TrackingFrame frame = *gFoot.frame;
    double const dx = held.x - gFoot.location.x;
    double const dy = held.y - gFoot.location.y;
    double const dz = held.z - gFoot.location.z;
    frame.headWorld.x += dx;
    frame.headWorld.y += dy;
    frame.headWorld.z += dz;
    view_anchor::Hold(frame);
    gHeld = true;
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
    if (face != nullptr && face != pawn) {
        if (auto const point = FacePoint(face)) {
            XrVector3f const faceT = engine::ToTracking(frame, engine::UnrealVector{point->x, point->y, point->z});
            XrVector3f out{};
            if (cw::SubtitlePoint(faceT, frame.head.position, gSettings.subtitleBelowM, gSettings.subtitleTowardM, out)) {
                state.subtitle = 1;
                state.subtitleAt[0] = out.x;
                state.subtitleAt[1] = out.y;
                state.subtitleAt[2] = out.z;
            }
        }
    }
    if (gPov.location != nullptr && now - gPov.ms < 500) {
        state.holdRender = 1;
        state.holdLocation[0] = static_cast<float>(held.x);
        state.holdLocation[1] = static_cast<float>(held.y);
        state.holdLocation[2] = static_cast<float>(held.z);
        state.holdRotation[0] = gFoot.rotation.pitch;
        state.holdRotation[1] = gFoot.rotation.yaw;
        state.holdRotation[2] = gFoot.rotation.roll;
        state.holdFov = gFoot.fov;
        state.povLocationAddress = reinterpret_cast<std::uint64_t>(gPov.location);
        state.povRotationAddress = reinterpret_cast<std::uint64_t>(gPov.rotation);
        state.povFovAddress = reinterpret_cast<std::uint64_t>(gPov.fov);
    }
    link.publish(&state);
    gPublished = true;
    ++gWorldTicks;
    if (!gInWorld) {
        gInWorld = true;
        log::Info("convo: conversation {} in the world: the view held at Shepard's on foot {} (yaw {:.0f}){}, arm IK and body follow on",
            gC.number, F(V(gFoot.location)), YawDeg(gFoot.rotation), gSettings.follow ? ", moved with his pawn" : "");
    }
    SetStatus(std::format("{{\"on\": true, \"world\": true, \"conversation\": {}, \"shots\": {}, \"speaker\": \"{}\", \"wheel\": {}, "
                          "\"subtitle\": {}, \"subtitleAt\": [{:.3f}, {:.3f}, {:.3f}], \"held\": [{:.1f}, {:.1f}, {:.1f}], \"heldRender\": {}, "
                          "\"pawnMovedM\": {:.3f}}}",
        gC.number, gC.shots, ShortName(gC.speaker), state.wheel != 0 ? "true" : "false", state.subtitle != 0 ? "true" : "false",
        state.subtitleAt[0], state.subtitleAt[1], state.subtitleAt[2], held.x, held.y, held.z, state.holdRender != 0 ? "true" : "false",
        std::sqrt(dx * dx + dy * dy + dz * dz) / kUnitsPerMeter));
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
        return std::format("convo: conversations {} until `mevr convo world off` (the next conversation)", cw::Name(*gModeOverride));
    }
    if (verb == "dump" || verb.empty()) {
        std::string out = std::format("convo: mode {}; ", cw::Name(CurrentMode()));
        if (!gC.on) {
            return out + "no conversation";
        }
        out += std::format("conversation {} ({}), {:.1f} s, {} shots, speaker {}; actors:", gC.number, gC.name, Seconds(), gC.shots,
            ShortName(gC.speaker));
        for (Tracked const& t : gC.actors) {
            auto const at = ue3::runtime::IsAlive(t.actor) ? player::Location(t.actor) : std::nullopt;
            out += std::format(" {} {}", t.name, at ? F(V(*at)) : std::string("gone"));
        }
        return out;
    }
    return "usage: mevr convo [dump | world on|off|screen|vr]";
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
    End("the mod unloads");
    StopWorld(link, "the mod unloads");
}

} // namespace mevr::game::conversation_world
