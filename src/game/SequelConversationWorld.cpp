// SPDX-License-Identifier: MIT
// ME3's slice of ME1's first-person conversations (game/SequelConversationWorld.hpp).
#include "game/SequelConversationWorld.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/ConversationWorld.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/SettingsFile.hpp"
#include "engine/GameAdapter.hpp"
#include "game/DepthOfFieldHold.hpp"
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

constexpr char kController[] = "SFXGame.BioConversationController";
ue3::FieldRef gActorWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gManager{"SFXGame.BioWorldInfo", "m_pConversationManager", ue3::kEveryGame};
ue3::FunctionRef gGetFull{"SFXGame.BioConversationManager.GetFullConversation", ue3::kEveryGame};
ue3::FieldRef gEnded{kController, "m_bConversationEnded", ue3::kEveryGame};
ue3::FieldRef gPlayer{kController, "m_pPlayer", ue3::kEveryGame};
ue3::FieldRef gSpeakers{kController, "m_aSpeakerData", ue3::kEveryGame};
ue3::ClassRef gControllerClass{kController, ue3::kEveryGame};
ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera", ue3::kEveryGame};
ue3::FunctionRef gSetLocation{"Engine.Actor.SetLocation", ue3::kEveryGame};
// The conversation camera constrains the picture to its 16:9 band (a letterbox): the stereo capture never matches it and the
// renderer showed the band stretched and mono (the owner, 2026-11-19: "squashed horizontally ... not stereoscopic"). ME1's
// [NOLETTERBOX] cure: cleared after the camera update that sets it, before the frame's Draw consumes it, every update.
ue3::FieldRef gConstrainAspect{"Engine.Camera", "bConstrainAspectRatio", ue3::kEveryGame};
unsigned gLetterboxClears = 0;

// FBioSpeakerData (SFXGame.BioConversationController.BioSpeakerData): nmSpeakerTag, pSpeakerActor.
struct SpeakerData {
    std::uint64_t tag = 0;
    ue3::Object* actor = nullptr;
};
static_assert(sizeof(SpeakerData) == 16);

struct Settings {
    bool world = false;          // [Cinematics] conversations = world (ME1's key and word)
    bool stay = true;            // [Cinematics] worldStay: people kept where they stand
    double settleS = 0.35;       // the opening's placements settle this long before the player's place is taken
    double followUnits = 80.0;   // a lasting jump of Shepard's head past this moves the place (ME1's FollowHeadSettled)
    // Shepard kept on his mark like the others (ME1's rule: the player's place is never moved by the game; the owner,
    // 2026-11-19: "have Shepard not move around during this scene like the Mass Effect 1 rule set"); then nothing to follow.
    bool shepardStays = true;
    bool dofOff = true;          // [Cinematics] worldDof = 0 keeps it: the conversation's depth of field blurs the room
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
bool gDofBackOwed = false;

// The session's override from the console: -1 the file's, 0 off, 1 on.
int gOverride = -1;
Settings gSettings;
std::uint64_t gSettingsMs = 0;

struct Kept {
    ue3::Object* actor = nullptr;
    ue3::Vector at{};
    ue3::Vector refused{};   // the last placement undone (the game repeating it at once is its own)
    bool hasRefused = false;
    unsigned undone = 0;
    unsigned yielded = 0;
    unsigned refusals = 0;   // SetLocation said no: after three, left to the game (logged once)
};

struct Conversation {
    bool on = false;             // in the conversation mode with the world asked for
    // A conversation the game opened around a load (the player not 3 s on foot before it: the docking officer's at the Citadel's
    // arrival) is left on the 3D screen: holding the view at its mark is what the level streaming then saw rendered, and the
    // Citadel's load never finished (2026-11-19, two runs of two). Held: none of the below is published.
    bool skipped = false;
    bool placed = false;         // the player's place taken
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
    std::string why;             // why not placed (yet)
};
Conversation gC;
depth_of_field::Hold gDof{"conversation world"};
std::uint32_t gSerial = 0;
bool gStarted = false;
std::string gLast = "none yet";

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
}

bool WorldWanted() {
    return gOverride >= 0 ? gOverride == 1 : gSettings.world;
}

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

void Publish(conversation_world::Link const& link, ue3::Object* controller) {
    if (link.publish == nullptr) return;
    cw::State s{};
    s.active = 1;
    s.wheelHand = 2;
    s.conversation = gC.serial;
    s.cut = gC.follows;
    ue3::Object* const camera = player::Camera(controller);
    auto const pov = camera != nullptr ? player::CameraPov(camera) : std::nullopt;
    if (pov && pov->location != nullptr && pov->rotation != nullptr && pov->fov != nullptr) {
        s.holdRender = 1;
        s.holdLocation[0] = gC.held.x;
        s.holdLocation[1] = gC.held.y;
        s.holdLocation[2] = gC.held.z;
        s.holdRotation[0] = gC.facing.pitch;
        s.holdRotation[1] = gC.facing.yaw;
        s.holdRotation[2] = gC.facing.roll;
        s.holdFov = gC.fov;
        s.povLocationAddress = reinterpret_cast<std::uint64_t>(pov->location);
        s.povRotationAddress = reinterpret_cast<std::uint64_t>(pov->rotation);
        s.povFovAddress = reinterpret_cast<std::uint64_t>(pov->fov);
    }
    link.publish(&s);
}

void PublishOff(conversation_world::Link const& link) {
    if (link.publish == nullptr) return;
    cw::State s{};  // active 0: the host's presentation goes back to the game's own at once
    link.publish(&s);
}

void End(conversation_world::Link const& link, char const* why) {
    if (!gC.on) return;
    gLast = std::format("conversation {}: {} after {:.1f} s; opening {}, walked {:.0f} units, Shepard put back {} times; {}; {} "
                        "re-placements by lasting jumps; people kept: {} undone, {} yielded, {} refused by SetLocation",
        gC.serial, why, static_cast<double>(GetTickCount64() - gC.startMs) / 1000.0, cw::Name(gC.opening), gC.walkedUnits, gC.pinned,
        gC.placed ? std::format("held at ({:.0f}, {:.0f}, {:.0f}) yaw {:.1f}", gC.held.x, gC.held.y, gC.held.z,
                        gC.facing.yaw * (360.0 / 65536.0))
                  : "never placed (" + gC.why + ")",
        gC.follows, gC.undoneTotal, gC.yieldedTotal, gC.refusedMoves);
    log::Info("conversation world: {}", gLast);
    if (gC.placed) view_anchor::Hold(std::nullopt);
    if (gDof.IsOff()) gDofBackOwed = true;  // put back after 3 s on foot (Tick)
    PublishOff(link);
    gC = {};
}

// Engine.Camera.UpdateCamera, after the game's (before the frame is drawn): the speakers the stage moved since the last update
// are put back (ME1's keep-in-place, game/ConversationWorld's undo in the same handler).
void OnUpdateCamera(ue3::Object* camera, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    if (!gC.on || !gC.placed) return;
    ue3::Object* const controller = player::Controller();
    if (camera == nullptr || controller == nullptr || player::CameraOwner(camera) != controller) return;
    if (gConstrainAspect.ReadBool(camera).value_or(false) && gConstrainAspect.WriteBool(camera, false)) {
        write_journal::Wrote("ConversationWorld.noLetterbox", camera, 4);
        if (gLetterboxClears++ == 0) {
            log::Info("conversation world: the conversation camera's letterbox (Camera.bConstrainAspectRatio) cleared each "
                      "update while the conversation is held: the full frame, in stereo");
        }
    }
    if (!gSettings.stay) return;
    if (gC.controller == nullptr || !ue3::runtime::IsAlive(gC.controller)) return;
    ue3::Function* const setLocation = gSetLocation.Get();
    ue3::Object* const player = gPlayer.Read<ue3::Object*>(gC.controller).value_or(nullptr);
    // Shepard on the pin (where the player stood, moved by the stick only): ME1's "the player is never moved by the game".
    if (ue3::Object* const pawn = player::Pawn(controller); gSettings.shepardStays && pawn != nullptr && setLocation != nullptr) {
        if (auto const at = player::Location(pawn); at && Distance(*at, gC.pin) > 2.0) {
            ue3::Params move(ue3::runtime::Get(), setLocation);
            move.Set("NewLocation", gC.pin);
            ue3::runtime::Call(pawn, setLocation, move);
            write_journal::Wrote("ConversationWorld.pin", pawn, 0);
            if (move.Return<bool>().value_or(false) && gC.pinned++ == 0) {
                log::Info("conversation world: conversation {}: the stage moved Shepard {:.0f} units: kept where the player is",
                    gC.serial, Distance(*at, gC.pin));
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
        if (it->refusals >= 3) continue;
        double const jump = Distance(*now, it->at);
        bool const shepard = actor == player;
        if (jump <= (shepard ? 2.0 : 30.0)) {
            if (!shepard) it->at = *now;  // its own small moves (a step, a turn's sway); Shepard stays exactly on his mark
            continue;
        }
        if (jump >= 600.0 || setLocation == nullptr) {
            it->at = *now;  // another space: the scene's own
            continue;
        }
        // The game repeating the placement it just made (within 1 m of the one undone): its own (ME1's cw::Insists). Not for
        // Shepard: his place is the player's.
        if (!shepard && it->hasRefused && Distance(*now, it->refused) < 100.0) {
            ++it->yielded;
            ++gC.yieldedTotal;
            log::Info("conversation world: {} put again {:.0f} units from where it stood: the game's own, kept",
                ue3::runtime::FullName(actor), jump);
            it->at = *now;
            it->hasRefused = false;
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
        it->refused = *now;
        it->hasRefused = true;
        ++it->undone;
        ++gC.undoneTotal;
        if (it->undone <= 3) {
            log::Info("conversation world: {} moved {:.0f} units by the stage: put back where it stood", ue3::runtime::FullName(actor),
                jump);
        }
    }
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
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    bool const conversationMode = mode && engine::adapter::IsConversationMode(*mode);
    if (mode && *mode == 0) {
        if (auto const at = player::Location(player::Pawn(controller))) gFoot = {true, *at, GetTickCount64()};
        if (gOnFootSinceMs == 0) gOnFootSinceMs = GetTickCount64();
        gLastFootRunMs = GetTickCount64() - gOnFootSinceMs;
    } else {
        gOnFootSinceMs = 0;
    }
    if (gDofBackOwed && gOnFootSinceMs != 0 && GetTickCount64() - gOnFootSinceMs >= 3000) {
        gDofBackOwed = false;
        gDof.Back("the conversation in the world ended (after 3 s on foot)");
    }
    if (!conversationMode || !WorldWanted() || link.publish == nullptr) {
        End(link, !conversationMode ? "the game left its conversation mode" : !WorldWanted() ? "the world mode is off" : "an older host");
        return;
    }
    ue3::Object* const conversation = LiveConversation(controller);
    if (conversation == nullptr) {
        End(link, "no live conversation");
        return;
    }
    std::uint64_t const now = GetTickCount64();
    if (!gC.on || conversation != gC.controller) {
        End(link, "another conversation began");
        gFootSettledAtStart = gLastFootRunMs >= 3000 && gFoot.valid && GetTickCount64() - gFoot.ms < 2000;
        gC.on = true;
        gC.serial = ++gSerial;
        gC.controller = conversation;
        gC.startMs = now;
        gC.why = "the opening settling";
        gC.skipped = !gFootSettledAtStart;
        log::Info("conversation world: conversation {} ({}) begins: {}", gC.serial, ue3::runtime::FullName(conversation),
            gC.skipped ? "the game opened it around a load (not 3 s on foot before it): left on the 3D screen"
                       : "shown in the world, first person");
    }
    if (gC.skipped) return;
    ue3::Object* const pawn = player::Pawn(controller);
    auto const head = HeadOf(pawn);
    auto const rotation = pawn != nullptr ? player::Rotation(pawn) : std::nullopt;
    if (!gC.placed) {
        if (static_cast<double>(now - gC.startMs) < gSettings.settleS * 1000.0) return;  // the stage's first placements
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
        facts.sightToMark = true;
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
        if (pads.active && frame && view.valid && dt > 0.0) {
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
    gStarted = false;
}

bool InWorld() {
    return gC.on && gC.placed;
}

std::string Console(std::string_view args) {
    std::string a(args);
    a.erase(0, a.find_first_not_of(' '));
    if (a == "on" || a == "off") {
        gOverride = a == "on" ? 1 : 0;
    } else if (a == "auto") {
        gOverride = -1;
    }
    RefreshSettings();
    return std::format("conversations in the world (ME3's slice): {} ({}); people kept {}; now: {}; last: {}",
        WorldWanted() ? "on" : "off", gOverride < 0 ? "[Cinematics] conversations" : "this session's override",
        gSettings.stay ? "on" : "off",
        gC.on ? std::format("conversation {} {}", gC.serial,
                    gC.placed ? std::format("held at ({:.0f}, {:.0f}, {:.0f}), {} kept actors, {} undone", gC.held.x, gC.held.y,
                                    gC.held.z, gC.kept.size(), gC.undoneTotal)
                              : "not placed yet: " + gC.why)
              : std::string("none"),
        gLast);
}

} // namespace mevr::game::sequel_conversation_world
