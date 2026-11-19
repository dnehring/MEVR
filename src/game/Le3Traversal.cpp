// SPDX-License-Identifier: MIT
#include "game/Le3Traversal.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "engine/GameAdapter.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CallFunction.hpp"
#include "engine/ProcessEvent.hpp"
#include "game/Player.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include <Windows.h>
#include <cmath>
#include <cstring>
#include <format>
#include <sstream>

namespace mevr::game::traversal::le3 {
namespace {
namespace tv = vr::traversal;
bool gStarted = false, gRequests = false;
std::string gNote = "LE3 traversal off";
tv::ActionLifecycle gLife;
tv::MoveRecord gRecord;
tv::PressTracker gPresses;
bool gEvadeDown = true;
bool gRecovered = false;
Link gLink;
std::string gTarget;
std::string gRequested;
std::string gActionName;
std::int32_t gActionId = 0;
double gRecoveredAt = 0;
std::optional<double> gCameraYawStart;
double gCameraYawDelta = 0;
tv::Settings gSettings;
tv::LevelView gLevel;
tv::AuthoredYaw gYaw;
std::optional<tv::Pose> gRawPose;
double gViewCameraAt = 0, gViewEndedAt = 0;
bool gViewWasOwned = false;
std::uint64_t gViewGeneration = 0, gViewWrites = 0;
bool gViewAccepted = false;
#if defined(MEVR_GAME_LE3)
ue3::ClassRef gModeClass{"SFXGame.SFXGameModeDefault", ue3::kEveryGame};
ue3::ClassRef gInputClass{"SFXGame.BioPlayerInput", ue3::kEveryGame};
ue3::ClassRef gWeaponClass{"SFXGame.SFXWeapon", ue3::kEveryGame};
ue3::ClassRef gLadderClass{"SFXGame.SFXNav_LadderNode", ue3::kEveryGame};
ue3::ClassRef gSpecClass{"SFXGame.SFXLadderReachSpec", ue3::kEveryGame};
ue3::ClassRef gActionClass{"SFXGame.BioCustomAction", ue3::kEveryGame};
ue3::FieldRef gWorld{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gCurrent{"SFXGame.BioPawn", "CurrentCustomAction", ue3::kEveryGame};
ue3::FieldRef gActionPawn{"SFXGame.BioCustomAction", "m_oPawn", ue3::kEveryGame};
ue3::FieldRef gCoverType{"SFXGame.BioPawn", "CoverType", ue3::kEveryGame};
ue3::FieldRef gCoverAction{"SFXGame.BioPawn", "CoverAction", ue3::kEveryGame};
ue3::FieldRef gStorming{"SFXGame.BioPawn", "bStorming", ue3::kEveryGame};
ue3::FieldRef gCombatPawn{"SFXGame.BioPawn", "bCombatPawn", ue3::kEveryGame};
ue3::FieldRef gWeapon{"Engine.Pawn", "Weapon", ue3::kEveryGame};
ue3::FieldRef gPhysics{"Engine.Actor", "Physics", ue3::kEveryGame};
ue3::FieldRef gCollision{"Engine.Actor", "bBlockActors", ue3::kEveryGame};
ue3::FieldRef gInput{"Engine.PlayerController", "PlayerInput", ue3::kEveryGame};
ue3::FieldRef gJoyUp{"SFXGame.BioPlayerInput", "RawJoyUp", ue3::kEveryGame};
ue3::FieldRef gJoyRight{"SFXGame.BioPlayerInput", "RawJoyRight", ue3::kEveryGame};
ue3::FieldRef gSpecStart{"Engine.ReachSpec", "Start", ue3::kEveryGame};
ue3::FieldRef gSpecEnd{"Engine.ReachSpec", "End", ue3::kEveryGame};
ue3::FieldRef gEndActor{gSpecEnd, "Actor"};
ue3::FieldRef gLadderDest{"SFXGame.SFXNav_LadderNode", "LadderDest", ue3::kEveryGame};
ue3::FieldRef gSelection{"SFXGame.BioPlayerController", "m_oPlayerSelection", ue3::kEveryGame};
ue3::FieldRef gSelectionTarget{"SFXGame.BioPlayerSelection", "m_oCurrentSelectionTarget", ue3::kEveryGame};
ue3::FieldRef gSelectionCombat{"SFXGame.BioPlayerSelection", "m_bCurrentSelectionIsCombatTarget", ue3::kEveryGame};
ue3::FunctionRef gGetAction{"SFXGame.BioPawn.GetCurrentCustomAction", ue3::kEveryGame};
ue3::FunctionRef gGetMode{"SFXGame.BioPlayerController.GetGameModeDefault", ue3::kEveryGame};
ue3::FunctionRef gIgnored{"Engine.PlayerController.IsMoveInputIgnored", ue3::kEveryGame};
ue3::FunctionRef gDead{"Engine.Pawn.IsDead", ue3::kEveryGame};
ue3::FunctionRef gReload{"SFXGame.BioPlayerController.IsReloading", ue3::kEveryGame};
ue3::FunctionRef gZoom{"SFXGame.BioPlayerController.IsZoomed", ue3::kEveryGame};
ue3::FunctionRef gWeaponState{"Core.Object.GetStateName", ue3::kEveryGame};
ue3::FunctionRef gFindLadder{"SFXGame.BioPlayerController.FindLadderNode", ue3::kEveryGame};
ue3::FunctionRef gCanLadder{"SFXGame.BioPlayerController.CanUseLadder", ue3::kEveryGame};
ue3::FunctionRef gUseLadder{"SFXGame.BioPlayerController.TryUseLadder", ue3::kEveryGame};
ue3::FunctionRef gVault{"SFXGame.SFXGameModeDefault.MantleCover", ue3::kEveryGame};
ue3::FunctionRef gClimb{"SFXGame.SFXGameModeDefault.ClimbCover", ue3::kEveryGame};
ue3::FunctionRef gFindCover{"SFXGame.SFXGameModeDefault.FindCover", ue3::kEveryGame};
ue3::FunctionRef gCanClimb{"SFXGame.BioPlayerController.CanPerformClimb", ue3::kEveryGame};
ue3::FieldRef gFoundCover{gFindCover, "FoundCover"};
ue3::FieldRef gClimbCoverInfo{gCanClimb, "CoverInfo"};
ue3::FunctionRef gRoll{"SFXGame.SFXGameModeDefault.TryRoll", ue3::kEveryGame};
ue3::FunctionRef gForwardRoll{"SFXGame.SFXGameModeDefault.TryForwardRoll", ue3::kEveryGame};
ue3::FunctionRef gStart{"SFXGame.BioPawn.StartCustomAction", ue3::kEveryGame};
ue3::FunctionRef gEnd{"SFXGame.BioCustomAction.EndThisCustomAction", ue3::kEveryGame};
ue3::FunctionRef gActionInterrupt{"SFXGame.BioCustomAction.InterruptThisCustomAction", ue3::kEveryGame};
ue3::FunctionRef gCameraUpdate{"Engine.Camera.UpdateCamera", ue3::kEveryGame};
struct Context {
    ue3::Object* pc = nullptr;
    ue3::Object* pawn = nullptr;
    ue3::Object* world = nullptr;
    std::uint64_t generation = 0;
    std::uint8_t mode = 255;
    bool operator==(Context const&) const = default;
};
Context gContext;
// View lifetime differs from input lifetime: a GUI/focus pause must not throw away
// the persistent basis correction. Only real scene/actor/space changes reset it.
struct ViewContext {
    ue3::Object* world = nullptr;
    ue3::Object* pc = nullptr;
    ue3::Object* pawn = nullptr;
    std::uint32_t recenters = 0, sessions = 0;
    bool operator==(ViewContext const&) const = default;
};
ViewContext gViewContext;
double Now() { return static_cast<double>(GetTickCount64()) * .001; }
struct ParamShape { char const* name; ue3::Kind kind; int offset, bytes; };
bool Signature(ue3::FunctionRef& ref, int bytes, std::initializer_list<ParamShape> shapes) {
    ue3::Params p(ue3::runtime::Get(), ref.Get());
    if (!p.Valid() || p.Size() != bytes || p.Specs().size() != shapes.size()) return false;
    for (auto const& shape : shapes) {
        auto const* found = p.Find(shape.name);
        if (!found || found->kind != shape.kind || found->offset != shape.offset || found->size != shape.bytes) return false;
    }
    return true;
}
bool FieldShape(ue3::FieldRef& field, ue3::Kind kind, int bytes) {
    auto const* f = field.Get();
    return f && f->kind == kind && f->elementSize == bytes && f->arrayDim == 1;
}
ue3::Object* Object(ue3::FieldRef& field, void const* owner) {
    auto* object = field.Read<ue3::Object*>(owner).value_or(nullptr);
    return ue3::runtime::IsAlive(object) ? object : nullptr;
}
std::optional<bool> Bool(ue3::Object* object, ue3::FunctionRef& ref) {
    auto* fn = ref.Get();
    ue3::Params p(ue3::runtime::Get(), fn);
    return p.Valid() && ue3::runtime::Call(object, fn, p) ? p.ReturnBool() : std::nullopt;
}
Context ReadContext(Link const& link) {
    Context c;
    c.generation = link.sceneGeneration;
    c.pc = player::Controller();
    if (!ue3::runtime::IsAlive(c.pc)) { c.pc = nullptr; return c; }
    c.pawn = player::BioPawn(c.pc);
    if (!ue3::runtime::IsAlive(c.pawn)) c.pawn = nullptr;
    c.world = Object(gWorld, c.pc);
    c.mode = player::GameMode(c.pc).value_or(255);
    return c;
}
void Refresh(Link const& link) {
    auto const c = ReadContext(link);
    if (!(c == gContext)) {
        if (gLife.Busy()) gNote = "scene/actor/mode changed; observation cancelled, native action untouched";
        gLife.Cancel();
        if (gRecord.Active()) gRecord.End(Now() - gLife.startedAt);
        gPresses.Reset();
        tv::PressInput held;
        held.nowS = Now(); held.down = link.controllers && link.rightA; held.focused = false; held.onFoot = false;
        gPresses.Step(held); // load/mode changes consume a held A instead of inventing a first tap
        gEvadeDown = true;
        gRecovered = false;
        gContext = c;
    }
}
tv::Kind KindOf(std::int32_t id) {
    switch (id) {
    case 31: case 32: return tv::Kind::Vault;
    case 33: return tv::Kind::ClimbUp;
    case 46: return tv::Kind::LadderUp;
    case 47: return tv::Kind::LadderDown;
    case 30: return tv::Kind::JumpDown;
    case 54: case 55: case 56: case 57: return tv::Kind::Evade;
    default: return tv::Kind::None;
    }
}
std::optional<ue3::Object*> Current() {
    auto* fn = gGetAction.Get();
    ue3::Params p(ue3::runtime::Get(), fn);
    if (gContext.pawn == nullptr || !p.Valid() || !ue3::runtime::Call(gContext.pawn, fn, p)) return std::nullopt;
    auto has = p.ReturnBool();
    if (!has) return std::nullopt;
    if (!*has) return static_cast<ue3::Object*>(nullptr);
    auto* action = p.Get<ue3::Object*>("pAction").value_or(nullptr);
    if (!ue3::runtime::IsAlive(action) || !ue3::IsA(action, gActionClass) || Object(gActionPawn, action) != gContext.pawn)
        return std::nullopt;
    return action;
}
void Observe() {
    if (!gLife.Busy()) return;
    auto const current = Current();
    if (!current) { gLife.Cancel(); gNote = "current action query failed"; return; }
    auto const old = gLife.phase;
    auto kind = KindOf(gCurrent.Read<std::int32_t>(gContext.pawn).value_or(-1));
    bool const expected = gRequested == "native" ? kind != tv::Kind::None :
        gRequested == "vault" ? kind == tv::Kind::Vault : gRequested == "climb" ? kind == tv::Kind::ClimbUp :
        gRequested == "evade" ? kind == tv::Kind::Evade :
        kind == tv::Kind::LadderUp || kind == tv::Kind::LadderDown || kind == tv::Kind::JumpDown;
    if (!expected && gLife.phase == tv::ActionPhase::Requested) kind = tv::Kind::None;
    gLife.Observe(gContext.generation, reinterpret_cast<std::uintptr_t>(*current), kind, Now());
    if (old != tv::ActionPhase::Started && gLife.phase == tv::ActionPhase::Started) {
        gRecord.Begin(gLife.kind);
        gActionName = ue3::runtime::FullName(*current);
        gActionId = gCurrent.Read<std::int32_t>(gContext.pawn).value_or(-1);
        gNote = "native action observed: " + gActionName;
        log::Info("traversal LE3: #{} {}", gLife.serial, gNote);
    }
    if (old == tv::ActionPhase::Started && !gLife.Busy()) gRecord.End(Now() - gLife.startedAt);
}
bool SameScene() {
    return gStarted && ue3::runtime::Up() && ReadContext(gLink) == gContext;
}
void OnStart(ue3::Object* object, ue3::Function*, void*) {
    if (!SameScene() || object != gContext.pawn) return;
    Observe();
    // Native controls are a comparison, not a second request. Observe a natural traversal without issuing anything.
    if (!gLife.Busy() && KindOf(gCurrent.Read<std::int32_t>(object).value_or(-1)) != tv::Kind::None) {
        gLife.Request(gContext.generation, Now());
        gRequested = "native";
        gRecovered = false; gRecoveredAt = 0; gTarget.clear(); gCameraYawStart.reset(); gCameraYawDelta = 0;
        Observe();
    }
}
void OnStartPe(ue3::Object* object, ue3::Function* fn, void*, void* result) { OnStart(object, fn, result); }
void End(bool interrupted, ue3::Object* owner) {
    if (!SameScene() || gLife.action == 0 || gRecovered) return;
    if (reinterpret_cast<std::uintptr_t>(owner) != gLife.action) return;
    if (gLife.phase != tv::ActionPhase::Started &&
        !(interrupted && gLife.phase == tv::ActionPhase::Completed && Now() - gLife.endedAt < .05) &&
        !(gLife.phase == tv::ActionPhase::EndedUnknown && Now() - gLife.endedAt < .05)) return;
    gLife.End(gContext.generation, gLife.action, interrupted, Now());
    if (gRecord.Active()) gRecord.End(Now() - gLife.startedAt);
    gNote = tv::Name(gLife.phase);
}
void OnEnd(ue3::Object* object, ue3::Function*, void*) { End(false, object); }
void OnInterrupt(ue3::Object* object, ue3::Function*, void*) { End(true, object); }
void OnEndPe(ue3::Object* object, ue3::Function* fn, void*, void* result) { OnEnd(object, fn, result); }
void OnInterruptPe(ue3::Object* object, ue3::Function* fn, void*, void* result) { OnInterrupt(object, fn, result); }
void ClearPresentation(bool resetBasis) {
    if (gLink.setPresentation) gLink.setPresentation(nullptr);
    gViewAccepted = false;
    if (resetBasis) {
        gYaw.Reset(); gLevel.Reset(); gRawPose.reset();
        gViewCameraAt = gViewEndedAt = 0; gViewWasOwned = false;
    }
}
bool RefreshPresentation() {
    auto const c = ReadContext(gLink);
    auto const mode = engine::adapter::GameMode(c.mode);
    bool const transient = mode == engine::adapter::Mode::Gui || mode == engine::adapter::Mode::PowerWheel ||
        mode == engine::adapter::Mode::WeaponWheel || mode == engine::adapter::Mode::Command;
    if (!c.world || !c.pc || !c.pawn || !gLink.viewSpaceKnown ||
        (mode != engine::adapter::Mode::OnFoot && !transient)) {
        ClearPresentation(true); gViewContext = {}; return false;
    }
    ViewContext const next{c.world, c.pc, c.pawn, gLink.viewRecenters, gLink.viewSessions};
    if (!(next == gViewContext)) { ClearPresentation(true); gViewContext = next; ++gViewGeneration; }
    if (!gSettings.enabled || gSettings.view != tv::View::Level) {
        ClearPresentation(true); return false;
    }
    if (transient || !gLink.headTrackedView || !gLink.setPresentation ||
        !gLink.allowPresentation || !gLink.allowPresentation()) {
        ClearPresentation(false); return false;
    }
    return true;
}
void Present(tv::Pose const& native, double now) {
    if (!std::isfinite(native.location.x) || !std::isfinite(native.location.y) || !std::isfinite(native.location.z)) {
        ClearPresentation(true); return;
    }
    if (!RefreshPresentation()) return;
    auto const current = Current();
    bool const owned = current && *current != nullptr &&
        KindOf(gCurrent.Read<std::int32_t>(gContext.pawn).value_or(-1)) != tv::Kind::None;
    // The prior native camera is captured before the first authored action update.
    // If it isn't available (entering a scene mid-action), do not invent a heading.
    if (owned && !gViewWasOwned && gRawPose) {
        gYaw.Begin(gRawPose->yaw);
        gLevel.Begin(*gRawPose);
        gViewEndedAt = 0;
    }
    if (!owned && gViewWasOwned) { gLevel.End(); gViewEndedAt = now; }
    double const dt = gViewCameraAt > 0 ? now - gViewCameraAt : 0;
    auto yaw = gYaw; // a declined publication must not advance the correction transaction
    tv::Pose result = native;
    if (owned && gLevel.Started()) result = gLevel.During(native, dt, gSettings.levelTauS);
    else if (gLevel.Started() && gViewEndedAt > 0) {
        if (auto const pose = gLevel.After(native, now - gViewEndedAt, gSettings.settleS)) result = *pose;
    }
    // Recovery never drags the view behind native horizontal movement.
    result.location.x = native.location.x; result.location.y = native.location.y;
    result.yaw = yaw.Update(native.yaw, owned);
    tv::Presentation message;
    message.flags = tv::kPresentationActive | (owned ? tv::kPresentationActionOwned : 0u);
    message.yawOffsetUU = static_cast<std::int32_t>(std::lround(yaw.Offset() * 65536 / 360));
    message.recenters = gViewContext.recenters; message.sessions = gViewContext.sessions;
    message.sceneGeneration = gViewGeneration; message.timeMs = GetTickCount64();
    if (!gLink.setPresentation(&message)) { gViewAccepted = false; return; }
    auto const pov = player::CameraPov(player::Camera(gViewContext.pc));
    if (!pov || !pov->location || !pov->rotation) { ClearPresentation(false); return; }
    auto units = [](double value) { return static_cast<std::int32_t>(std::lround(tv::SignedDegrees(value) * 65536 / 360)); };
    // No pawn/controller writes. Horizontal/metric translation is the native camera's;
    // LevelView only supplies its existing optional vertical filter and recovery blend.
    *pov->location = {static_cast<float>(result.location.x), static_cast<float>(result.location.y), static_cast<float>(result.location.z)};
    *pov->rotation = {units(result.pitch), units(result.yaw), units(result.roll)};
    write_journal::Wrote("LE3 traversal presentation POV", pov->location, sizeof(ue3::Vector));
    write_journal::Wrote("LE3 traversal presentation rotation", pov->rotation, sizeof(ue3::Rotator));
    gYaw = yaw; gViewAccepted = true; ++gViewWrites;
    gRawPose = native; gViewCameraAt = now; gViewWasOwned = owned;
}
void OnCamera(ue3::Object* camera, ue3::Function*, void*, void*) {
    if (!gStarted || !ue3::runtime::Up()) return;
    // Refresh input observation separately; presentation keeps its basis through transient UI/focus.
    Refresh(gLink);
    if (player::CameraOwner(camera) != gContext.pc) return;
    Observe();
    auto const pawn = player::Location(gContext.pawn);
    auto const rotation = player::Rotation(gContext.pawn);
    auto const pov = player::CameraPov(camera);
    if (!pawn || !rotation || !pov || !pov->location || !pov->rotation) { ClearPresentation(false); return; }
    auto deg = [](std::int32_t value) { return tv::SignedDegrees(static_cast<double>(value) * 360 / 65536); };
    tv::Pose const native{{pov->location->x, pov->location->y, pov->location->z},
        deg(pov->rotation->pitch), deg(pov->rotation->yaw), deg(pov->rotation->roll)};
    if (gLife.action != 0 && (gLife.phase == tv::ActionPhase::Started || !gRecovered)) {
        tv::Sample sample;
        sample.t = Now() - gLife.startedAt;
        sample.pawn = {pawn->x, pawn->y, pawn->z}; sample.pawnYaw = deg(rotation->yaw);
        sample.camera = native.location;
        sample.pitch = native.pitch; sample.yaw = native.yaw; sample.roll = native.roll;
        if (!gCameraYawStart) gCameraYawStart = sample.yaw;
        gCameraYawDelta = tv::SignedDegrees(sample.yaw - *gCameraYawStart);
        sample.collision = gCollision.ReadBool(gContext.pawn).value_or(false);
        sample.physics = gPhysics.Read<std::uint8_t>(gContext.pawn).value_or(0);
        sample.running = gLife.phase == tv::ActionPhase::Started;
        gRecord.Add(sample); // always raw native values, before presentation writes
    }
    Present(native, Now());
}
char const* Guard(Link const& link) {
    if (!gStarted || !ue3::runtime::Up()) return "reflection adapter not ready";
    if (!gRequests || !gSettings.enabled) return "native requests/settings off (traversal requests on)";
    if (link.authoredSeated || link.menuOpen || !link.allowNativeTraversal || !link.allowNativeTraversal())
        return "scene/menu/focus/hand ownership denies traversal";
    if (!gContext.pc || !gContext.pawn || !gContext.world || gContext.mode != 0) return "not ordinary Shepard gameplay";
    // The movement facts through the one admission read (game/Player `MovementRefused`), in this guard's order of reasons.
    namespace admit = player::admission;
    auto const admitted = [](admit::Facts facts) { return player::MovementAdmitted(gContext.pc, gContext.pawn, facts); };
    if (!admitted(admit::kMoveInput | admit::kAlive)) return "native movement ignored/dead/unknown";
    if (!admitted(admit::kOutOfCover | admit::kNoCoverAction | admit::kNotStorming))
        return "cover/storm action needs separate intentional support";
    if (!admitted(admit::kWalking)) return "not native walking physics";
    if (!admitted(admit::kNoAction) || gLife.Busy()) return "native action/pending request owns movement";
    if (Bool(gContext.pc, gReload) != false || Bool(gContext.pc, gZoom) != false) return "reload/zoom/unknown blocks request";
    return nullptr;
}
ue3::Object* Mode() {
    auto* fn = gGetMode.Get();
    ue3::Params p(ue3::runtime::Get(), fn);
    if (!p.Valid() || !ue3::runtime::Call(gContext.pc, fn, p)) return nullptr;
    auto* mode = p.Return<ue3::Object*>().value_or(nullptr);
    return ue3::runtime::IsAlive(mode) && ue3::IsA(mode, gModeClass) && ue3::runtime::Get().OuterOf(mode) == gContext.pc ? mode : nullptr;
}
struct Ladder { ue3::Object* spec = nullptr; ue3::Object* node = nullptr; ue3::Object* end = nullptr; };
std::optional<Ladder> FindLadder() {
    auto* fn = gFindLadder.Get();
    ue3::Params p(ue3::runtime::Get(), fn);
    if (!p.Valid() || !p.Set("MinDistance", 75.0f) || !ue3::runtime::Call(gContext.pc, fn, p) || p.ReturnBool() != true) return std::nullopt;
    Ladder l;
    l.spec = p.Get<ue3::Object*>("out_LadderSpec").value_or(nullptr);
    if (!ue3::runtime::IsAlive(l.spec) || !ue3::IsA(l.spec, gSpecClass)) return std::nullopt;
    l.node = Object(gSpecStart, l.spec);
    l.end = Object(gEndActor, gSpecEnd.Address(l.spec));
    if (!ue3::IsA(l.node, gLadderClass) || !l.end || Object(gLadderDest, l.node) != l.end) return std::nullopt;
    // Discovery is native. An additional bounded distance check prevents a bad/wrong-side spec becoming a remote use.
    auto const here = player::Location(gContext.pawn);
    auto const at = player::Location(l.node);
    if (!here || !at || !std::isfinite(at->x) || !std::isfinite(at->y) || !std::isfinite(at->z) ||
        std::hypot(static_cast<double>(at->x - here->x), static_cast<double>(at->y - here->y), static_cast<double>(at->z - here->z)) > 150)
        return std::nullopt;
    return l;
}
std::string Request(std::string const& kind, Link const& link) {
    Refresh(link);
    if (auto const* why = Guard(link)) return std::string("traversal LE3: refused: ") + why;
    auto* mode = Mode();
    if (!mode) return "traversal LE3: default mode missing/owner mismatch";
    ue3::Object* receiver = mode;
    ue3::Function* fn = nullptr;
    ue3::Object* node = nullptr;
    gTarget.clear();
    if (kind == "ladder") {
        auto const ladder = FindLadder();
        if (!ladder) return "traversal LE3: no native nearby ladder/reach match";
        auto* can = gCanLadder.Get();
        ue3::Params p(ue3::runtime::Get(), can);
        if (!p.Valid() || !p.Set("LadderNode", ladder->node) || !ue3::runtime::Call(gContext.pc, can, p) || p.ReturnBool() != true)
            return "traversal LE3: native CanUseLadder refused";
        node = ladder->node;
        gTarget = ue3::runtime::FullName(node) + " -> " + ue3::runtime::FullName(ladder->end);
        receiver = gContext.pc;
        fn = gUseLadder.Get();
    } else if (kind == "vault") {
        // StartCustomAction owns InternalCanDoCustomAction -> CanPerformMantleSlow -> FindMantleDistance/VerifyLandingClear.
        // Let the action own its MantleInfo and embedded arrays; do not allocate/copy a synthetic CoverInfo or force a slot.
        fn = gVault.Get();
    } else if (kind == "climb") {
        // The actual ME3 native query produces the input to its own admission function. We do not
        // synthesize a slot, copy LE2 layouts, force the out-of-cover flag or spoof forward input.
        auto* find = gFindCover.Get();
        auto* can = gCanClimb.Get();
        ue3::Params found(ue3::runtime::Get(), find), admission(ue3::runtime::Get(), can);
        if (!found.SetBool("bWideCheck", true) || !ue3::runtime::Call(mode, find, found) || found.ReturnBool() != true)
            return "traversal LE3: native FindCover found no climb approach";
        auto const* source = gFoundCover.Get();
        auto const* dest = gClimbCoverInfo.Get();
        if (!source || !dest || source->kind != ue3::Kind::Struct || !source->typed || source->typed != dest->typed ||
            source->elementSize != 56 || dest->elementSize != 56) return "traversal LE3: native cover query types disagree";
        // CovPosInfo is the verified POD query result (no arrays/strings); the game's output is passed unchanged.
        std::memcpy(gClimbCoverInfo.Address(admission.Data()), gFoundCover.Address(found.Data()), 56);
        if (!ue3::runtime::Call(gContext.pc, can, admission) || admission.ReturnBool() != true)
            return "traversal LE3: native CanPerformClimb refused; no flag/stick override";
        // PlayerMantleUp then resolves the authored reach path and owns its lifetime.
        fn = gClimb.Get();
    } else if (kind == "evade") {
        auto* input = Object(gInput, gContext.pc);
        auto* weapon = Object(gWeapon, gContext.pawn);
        if (!ue3::IsA(input, gInputClass) || !ue3::IsA(weapon, gWeaponClass) || gCombatPawn.ReadBool(gContext.pawn) != true)
            return "traversal LE3: evade requires native combat pawn/weapon/input";
        auto* stateFn = gWeaponState.Get();
        ue3::Params state(ue3::runtime::Get(), stateFn);
        if (!state.Valid() || !ue3::runtime::Call(weapon, stateFn, state)) return "traversal LE3: weapon state unavailable";
        auto const stateName = state.Return<ue3::Name>();
        if (!stateName) return "traversal LE3: weapon state unavailable";
        auto const name = ue3::runtime::NameText(*stateName);
        if (ue3::names::EqualsNoCase(name, "WeaponEquipping") || ue3::names::EqualsNoCase(name, "WeaponPuttingDown") ||
            ue3::names::EqualsNoCase(name, "Inactive") || ue3::names::EqualsNoCase(name, "None")) return "traversal LE3: weapon not ready";
        auto const up = gJoyUp.Read<float>(input), right = gJoyRight.Read<float>(input);
        if (!up || !right || !std::isfinite(*up) || !std::isfinite(*right)) return "traversal LE3: native direction unavailable";
        // Preserve native direction/precedence; no temporary stick writes, storm/cover guarded above.
        if (std::abs(*right) > .707f || *up < -.707f) fn = gRoll.Get();
        else if (*up > .707f) fn = gForwardRoll.Get();
        else return "traversal LE3: hold a native movement direction before explicit evade";
    } else return "traversal LE3: request must be ladder, vault, climb or evade";
    if (!(ReadContext(link) == gContext)) return "traversal LE3: scene changed during admission";
    if (auto const* why = Guard(link)) return std::string("traversal LE3: refused before dispatch: ") + why;
    ue3::Params p(ue3::runtime::Get(), fn);
    if (!p.Valid() || (node != nullptr && !p.Set("LadderNode", node))) return "traversal LE3: request parameters unavailable";
    if (!gLife.Request(gContext.generation, Now())) return "traversal LE3: request already pending";
    gRequested = kind;
    gActionName.clear(); gActionId = 0; gRecoveredAt = 0;
    gCameraYawStart.reset(); gCameraYawDelta = 0;
    if (auto const pov = player::CameraPov(player::Camera(gContext.pc)); pov && pov->rotation)
        gCameraYawStart = tv::SignedDegrees(static_cast<double>(pov->rotation->yaw) * 360 / 65536);
    gRecovered = false;
    if (!ue3::runtime::Call(receiver, fn, p)) { gLife.Cancel(); return "traversal LE3: dispatch failed"; }
    auto const returned = p.ReturnBool();
    gNote = kind + " dispatched" + (returned ? (*returned ? "; outer return true (not action acceptance)" : "; outer return false") : "");
    Observe();
    return "traversal LE3: " + gNote + "; " + tv::Name(gLife.phase);
}
#endif
} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE3)
    if (gStarted) return true;
    if (!ue3::runtime::Up() || engine::call_function::Installed()) return false;
    for (auto* cls : {&gModeClass, &gInputClass, &gWeaponClass, &gLadderClass, &gSpecClass, &gActionClass})
        if (!cls->Get()) return false;
    for (auto* field : {&gWorld, &gCurrent, &gActionPawn, &gCoverType, &gCoverAction, &gStorming, &gCombatPawn, &gWeapon, &gPhysics,
        &gCollision, &gInput, &gJoyUp, &gJoyRight, &gSpecStart, &gSpecEnd, &gEndActor, &gLadderDest,
        &gSelection, &gSelectionTarget, &gSelectionCombat})
        if (!field->Get()) return false;
    for (auto* fn : {&gGetAction, &gGetMode, &gIgnored, &gDead, &gReload, &gZoom, &gWeaponState, &gFindLadder, &gCanLadder,
        &gUseLadder, &gVault, &gClimb, &gFindCover, &gCanClimb, &gRoll, &gForwardRoll, &gStart, &gEnd, &gActionInterrupt, &gCameraUpdate})
        if (!fn->Get()) return false;
    auto const* current = gCurrent.Get();
    if (current->kind != ue3::Kind::Int || current->elementSize != 4 || current->arrayDim != 1) return false;
    for (auto* f : {&gWorld, &gActionPawn, &gWeapon, &gInput, &gSpecStart, &gEndActor, &gLadderDest, &gSelection, &gSelectionTarget})
        if (!FieldShape(*f, ue3::Kind::Object, 8)) return false;
    for (auto* f : {&gCoverType, &gCoverAction, &gPhysics}) if (!FieldShape(*f, ue3::Kind::Byte, 1)) return false;
    for (auto* f : {&gStorming, &gCombatPawn, &gCollision, &gSelectionCombat}) if (!FieldShape(*f, ue3::Kind::Bool, 4)) return false;
    for (auto* f : {&gJoyUp, &gJoyRight}) if (!FieldShape(*f, ue3::Kind::Float, 4)) return false;
    if (!FieldShape(gSpecEnd, ue3::Kind::Struct, 24)) return false;
    using K = ue3::Kind;
    for (auto* f : {&gIgnored, &gDead, &gReload, &gZoom, &gRoll, &gForwardRoll})
        if (!Signature(*f, 4, {{"ReturnValue", K::Bool, 0, 4}})) return false;
    for (auto* f : {&gVault, &gClimb, &gEnd, &gActionInterrupt}) if (!Signature(*f, 0, {})) return false;
    if (!Signature(gGetAction, 12, {{"pAction", K::Object, 0, 8}, {"ReturnValue", K::Bool, 8, 4}}) ||
        !Signature(gGetMode, 8, {{"ReturnValue", K::Object, 0, 8}}) ||
        !Signature(gWeaponState, 8, {{"ReturnValue", K::Name, 0, 8}}) ||
        !Signature(gFindLadder, 16, {{"out_LadderSpec", K::Object, 0, 8}, {"MinDistance", K::Float, 8, 4}, {"ReturnValue", K::Bool, 12, 4}}))
        return false;
    for (auto* f : {&gCanLadder, &gUseLadder})
        if (!Signature(*f, 12, {{"LadderNode", K::Object, 0, 8}, {"ReturnValue", K::Bool, 8, 4}})) return false;
    if (!Signature(gFindCover, 64, {{"FoundCover", K::Struct, 0, 56}, {"bWideCheck", K::Bool, 56, 4}, {"ReturnValue", K::Bool, 60, 4}}) ||
        !Signature(gCanClimb, 60, {{"CoverInfo", K::Struct, 0, 56}, {"ReturnValue", K::Bool, 56, 4}})) return false;
    engine::call_function::AddPostHandler(gStart.Get(), &OnStart);
    engine::process_event::AddPostHandler(gStart.Get(), &OnStartPe);
    engine::call_function::AddPostHandler(gEnd.Get(), &OnEnd);
    engine::process_event::AddPostHandler(gEnd.Get(), &OnEndPe);
    engine::call_function::AddPostHandler(gActionInterrupt.Get(), &OnInterrupt);
    engine::process_event::AddPostHandler(gActionInterrupt.Get(), &OnInterruptPe);
    engine::process_event::AddPostHandler(gCameraUpdate.Get(), &OnCamera);
    gStarted = true;
    gNote = "observer ready; requests off; native/VR acceptance pending";
#endif
    return gStarted;
}
void Tick(Link const& link, tv::Settings const& settings) {
    gLink = link;
    gSettings = settings;
#if defined(MEVR_GAME_LE3)
    if (!gStarted || !ue3::runtime::Up()) return;
    Refresh(link);
    RefreshPresentation();
    Observe();
    if (!gLife.Busy() && gLife.action != 0 && !gRecovered && gCurrent.Read<std::int32_t>(gContext.pawn) == 0 &&
        Bool(gContext.pc, gIgnored) == false && gPhysics.Read<std::uint8_t>(gContext.pawn) == 1) {
        gRecovered = true;
        gRecoveredAt = Now();
    }
    bool const allowed = Guard(link) == nullptr;
    tv::PressInput input;
    input.nowS = Now(); input.down = link.controllers && link.rightA; input.focused = link.controllers && allowed;
    input.menuOpen = link.menuOpen; input.onFoot = gContext.mode == 0; input.authoredSeated = link.authoredSeated;
    input.traversing = Moving(); input.eligible = allowed; // eligibility of a REQUEST, native owns action admission
    auto* selection = Object(gSelection, gContext.pc);
    auto* target = Object(gSelectionTarget, selection);
    input.useFirst = selection == nullptr || (target != nullptr && !ue3::IsA(target, gLadderClass) &&
        gSelectionCombat.ReadBool(selection) != true);
    auto const tap = gPresses.Step(input, gSettings.doubleTapS);
    if (tap == tv::Tap::Double && link.le3DoubleTap && allowed && gSettings.commit) {
        auto const ladder = FindLadder();
        log::Info("{}", Request(ladder ? "ladder" : "vault", link));
    }
    bool const evadeEdge = link.evadeDown && !gEvadeDown;
    gEvadeDown = allowed ? link.evadeDown : true; // release after interrupted ownership, never replay a menu-held evade
    if (evadeEdge && allowed) log::Info("{}", Request("evade", link));
    tv::Cue cue;
    float const mask = tv::MaskAmount(gSettings.mask, gSettings, gLife.phase == tv::ActionPhase::Started,
        Now() - gLife.startedAt, Now() - gLife.endedAt);
    if (gSettings.enabled && gLife.action != 0 && link.headTrackedView && !link.menuOpen && !link.authoredSeated) {
        cue.blink = gSettings.mask == tv::Mask::Blink ? mask : 0;
        if (link.setVignette) link.setVignette(gSettings.mask == tv::Mask::Vignette ? mask : 0);
    } else if (link.setVignette) link.setVignette(0);
    // A ladder cue promises a discovered approach, not an admitted action. Native use rechecks collision at commit.
    if (gSettings.cue && allowed && !Moving() && !input.useFirst) {
        auto const ladder = FindLadder();
        auto const frame = view_anchor::Current();
        if (ladder && frame) {
            auto const at = player::Location(ladder->node), exit = player::Location(ladder->end);
            if (at && exit) {
                cue.visible = true;
                cue.kind = tv::Kind::Ladder;
                cue.position = engine::ToTracking(*frame, {at->x, at->y, at->z + 80.0});
                cue.exit = engine::ToTracking(*frame, {exit->x, exit->y, exit->z + 80.0});
                cue.exitValid = true; cue.heightDeg = gSettings.cueHeightDeg;
                cue.serial = static_cast<std::uint32_t>(ue3::runtime::Get().IndexOf(ladder->node));
            }
        }
    }
    tv::Publish(cue);
    if (link.setCue) link.setCue(&cue);
#endif
}
bool Moving() { return gLife.Busy() || (gLife.action != 0 && !gRecovered); }
bool FloorMoving() { return Moving() && gLife.kind != tv::Kind::Evade; }
std::string StatusJson() {
    namespace json = vr::test_request;
    auto const sample = gRecord.Current();
    return std::format("{{\"adapter\":\"LE3\",\"started\":{},\"requests\":{},\"phase\":{},\"serial\":{},\"kind\":{},"
        "\"recovered\":{},\"target\":{},\"note\":{},\"samples\":{},\"horizontalM\":{},\"riseM\":{},\"collisionOffS\":{},"
        "\"action\":{},\"actionId\":{},\"request\":{},\"recoveryAfterEndS\":{},\"cameraYawTurnDeg\":{},\"cameraPitchMin\":{},\"cameraPitchMax\":{},"
        "\"viewAccepted\":{},\"viewWrites\":{},\"yawOffsetDeg\":{}}}",
        gStarted ? "true" : "false", gRequests ? "true" : "false", json::JsonString(tv::Name(gLife.phase)), gLife.serial,
        json::JsonString(tv::Name(gLife.kind)), gRecovered ? "true" : "false", json::JsonString(gTarget), json::JsonString(gNote),
        sample.samples, json::JsonNumber(sample.horizontalM), json::JsonNumber(sample.riseM), json::JsonNumber(sample.collisionOffS),
        json::JsonString(gActionName), gActionId, json::JsonString(gRequested),
        json::JsonNumber(gRecovered && gLife.endedAt > 0 ? gRecoveredAt - gLife.endedAt : -1),
        json::JsonNumber(gCameraYawDelta), json::JsonNumber(sample.pitchMinDeg), json::JsonNumber(sample.pitchMaxDeg),
        gViewAccepted ? "true" : "false", gViewWrites, json::JsonNumber(gYaw.Offset()));
}
std::string Command(std::string const& args, Link const& link) {
    std::istringstream words(args);
    std::string verb, value;
    words >> verb >> value;
    if (verb == "requests" && (value == "on" || value == "off")) {
        gRequests = value == "on";
        gPresses.Reset(); gEvadeDown = true;
#if defined(MEVR_GAME_LE3)
        tv::PressInput held;
        held.nowS = Now(); held.down = link.controllers && link.rightA; held.focused = false; held.onFoot = false;
        gPresses.Step(held);
#endif
        return "traversal LE3: requests " + value;
    }
    if (verb.empty() || verb == "status") return StatusJson();
#if defined(MEVR_GAME_LE3)
    if (!gStarted || !ue3::runtime::Up()) return "traversal LE3: adapter not ready";
    gLink = link;
    if (verb == "ladder" || verb == "vault" || verb == "climb" || verb == "evade") return Request(verb, link);
    if (verb == "probe") {
        Refresh(link);
        if (auto const* why = Guard(link)) return std::string("traversal LE3: ") + why;
        auto const ladder = FindLadder();
        return ladder ? "traversal LE3: native ladder " + ue3::runtime::FullName(ladder->node) + " -> " + ue3::runtime::FullName(ladder->end)
            : "traversal LE3: no nearby native ladder; mantle admission is evaluated only on an intentional vault request";
    }
#else
    (void)link;
#endif
    return "traversal LE3: status | requests on/off | probe | ladder | vault | climb | evade (uses native movement direction)";
}
void Shutdown() {
#if defined(MEVR_GAME_LE3)
    ClearPresentation(true);
    gViewContext = {};
#endif
    gViewGeneration = gViewWrites = 0;
    gStarted = gRequests = false;
    if (gLink.setVignette) gLink.setVignette(0);
    tv::Cue cue;
    if (gLink.setCue) gLink.setCue(&cue);
    tv::Reset();
    gLife = {}; gRecord = {}; gPresses.Reset(); gLink = {}; gEvadeDown = true; gRecovered = false; gTarget.clear(); gRequested.clear();
    gActionName.clear(); gActionId = 0; gRecoveredAt = 0;
    gCameraYawStart.reset(); gCameraYawDelta = 0;
    gNote = "LE3 traversal off";
#if defined(MEVR_GAME_LE3)
    gContext = {};
#endif
}
} // namespace mevr::game::traversal::le3
