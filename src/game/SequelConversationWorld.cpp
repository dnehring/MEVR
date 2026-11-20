// SPDX-License-Identifier: MIT
// ME3's slice of ME1's first-person conversations (game/SequelConversationWorld.hpp).
#include "game/SequelConversationWorld.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/ConversationWorld.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/SettingsFile.hpp"
#include "engine/GameAdapter.hpp"
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
    double eyeForwardUnits = 8.0;  // the eyes ahead of his head bone (upstream ConvoFp's eyeFwd)
    double eyeUpUnits = 4.0;
};

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
};

struct Conversation {
    bool on = false;             // in the conversation mode with the world asked for
    bool placed = false;         // the player's place taken
    std::uint32_t serial = 0;
    ue3::Object* controller = nullptr;
    std::uint64_t startMs = 0;
    std::uint64_t placedMs = 0;
    ue3::Vector held{};          // the view's place (the POV before the renderer's offsets)
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

// The held place from Shepard's head: the eyes a little ahead of the head bone along his facing, minus the renderer's view
// offset (the host draws the eyes that far from the POV: the POV goes the other way, so the eyes land at his).
ue3::Vector PlaceFromHead(engine::UnrealVector const& head, ue3::Rotator const& facing) {
    engine::Basis const basis = engine::ToBasis(engine::UnrealRotation{0, facing.yaw, 0});
    vr::host_view::State const view = vr::host_view::Latest();
    double const f = gSettings.eyeForwardUnits - (view.valid ? view.offsetForward : 0.0f);
    double const r = view.valid ? -view.offsetRight : 0.0;
    double const u = gSettings.eyeUpUnits - (view.valid ? view.offsetUp : 0.0f);
    return {static_cast<float>(head.x + basis.forward.x * f + basis.right.x * r + basis.up.x * u),
        static_cast<float>(head.y + basis.forward.y * f + basis.right.y * r + basis.up.y * u),
        static_cast<float>(head.z + basis.forward.z * f + basis.right.z * r + basis.up.z * u)};
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
    gLast = std::format("conversation {}: {} after {:.1f} s; {}; {} re-placements by lasting jumps; people kept: {} undone, {} "
                        "yielded, {} refused by SetLocation",
        gC.serial, why, static_cast<double>(GetTickCount64() - gC.startMs) / 1000.0,
        gC.placed ? std::format("held at ({:.0f}, {:.0f}, {:.0f}) yaw {:.1f}", gC.held.x, gC.held.y, gC.held.z,
                        gC.facing.yaw * (360.0 / 65536.0))
                  : "never placed (" + gC.why + ")",
        gC.follows, gC.undoneTotal, gC.yieldedTotal, gC.refusedMoves);
    log::Info("conversation world: {}", gLast);
    if (gC.placed) view_anchor::Hold(std::nullopt);
    PublishOff(link);
    gC = {};
}

// Engine.Camera.UpdateCamera, after the game's (before the frame is drawn): the speakers the stage moved since the last update
// are put back (ME1's keep-in-place, game/ConversationWorld's undo in the same handler).
void OnUpdateCamera(ue3::Object* camera, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    if (!gC.on || !gC.placed || !gSettings.stay) return;
    ue3::Object* const controller = player::Controller();
    if (camera == nullptr || controller == nullptr || player::CameraOwner(camera) != controller) return;
    if (gC.controller == nullptr || !ue3::runtime::IsAlive(gC.controller)) return;
    ue3::Function* const setLocation = gSetLocation.Get();
    ue3::Object* const player = gPlayer.Read<ue3::Object*>(gC.controller).value_or(nullptr);
    for (ue3::Object* const actor : Speakers(gC.controller, player)) {
        auto const now = player::Location(actor);
        if (!now) continue;
        auto it = std::find_if(gC.kept.begin(), gC.kept.end(), [&](Kept const& k) { return k.actor == actor; });
        if (it == gC.kept.end()) {
            gC.kept.push_back({actor, *now});
            continue;
        }
        double const jump = Distance(*now, it->at);
        if (jump <= 30.0) {
            it->at = *now;  // its own small moves (a step, a turn's sway)
            continue;
        }
        if (jump >= 600.0 || setLocation == nullptr) {
            it->at = *now;  // another space: the scene's own
            continue;
        }
        // The game repeating the placement it just made (within 1 m of the one undone): its own (ME1's cw::Insists).
        if (it->hasRefused && Distance(*now, it->refused) < 100.0) {
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
        gC.on = true;
        gC.serial = ++gSerial;
        gC.controller = conversation;
        gC.startMs = now;
        gC.why = "the opening settling";
        log::Info("conversation world: conversation {} ({}) begins: shown in the world, first person", gC.serial,
            ue3::runtime::FullName(conversation));
    }
    ue3::Object* const pawn = player::Pawn(controller);
    auto const head = HeadOf(pawn);
    auto const rotation = pawn != nullptr ? player::Rotation(pawn) : std::nullopt;
    if (!gC.placed) {
        if (static_cast<double>(now - gC.startMs) < gSettings.settleS * 1000.0) return;  // the stage's first placements
        if (!head || !rotation) {
            gC.why = "Shepard's head bone not posed";
            return;
        }
        if (!vr::host_view::Latest().valid) {
            gC.why = "no head-tracked view from the host";
            return;
        }
        gC.facing = ue3::Rotator{0, rotation->yaw, 0};
        gC.held = PlaceFromHead(*head, gC.facing);
        ue3::Object* const camera = player::Camera(controller);
        auto const pov = camera != nullptr ? player::CameraPov(camera) : std::nullopt;
        gC.fov = pov && pov->fov != nullptr ? std::clamp(*pov->fov, 60.0f, 110.0f) : 90.0f;
        gC.follow = cw::FollowState{{gC.held.x, gC.held.y, gC.held.z}, {head->x, head->y, head->z}, 0};
        gC.placed = true;
        gC.placedMs = now;
        log::Info("conversation world: conversation {}: the player's place is Shepard's eyes on his mark ({:.0f}, {:.0f}, {:.0f}), "
                  "facing yaw {:.1f}, held for the conversation",
            gC.serial, gC.held.x, gC.held.y, gC.held.z, gC.facing.yaw * (360.0 / 65536.0));
    } else if (head) {
        // A cut that moves him to another mark and keeps him there: the place goes with him (ME1's FollowHeadSettled).
        if (cw::FollowHeadSettled(gC.follow, {head->x, head->y, head->z}, gSettings.followUnits, 3)) {
            ++gC.follows;
            gC.held = {static_cast<float>(gC.follow.place.x), static_cast<float>(gC.follow.place.y),
                static_cast<float>(gC.follow.place.z)};
            if (rotation) gC.facing = ue3::Rotator{0, rotation->yaw, 0};
            log::Info("conversation world: conversation {}: Shepard's head stayed on another mark: the place follows to ({:.0f}, "
                      "{:.0f}, {:.0f})",
                gC.serial, gC.held.x, gC.held.y, gC.held.z);
        }
    }
    Publish(link, controller);
    view_anchor::Hold(HeldFrame(), vr::host_view::Latest());
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
