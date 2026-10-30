// SPDX-License-Identifier: MIT
#include "game/Interact.hpp"

#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/SelectionGaze.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>

namespace mevr::game::interact {
namespace {

namespace il = vr::interact;
namespace json = vr::test_request;

ue3::ClassRef gSelectionClass{"SFXGame.BioPlayerSelection"};
ue3::FieldRef gPlayerSelection{"SFXGame.BioPlayerController", "m_oPlayerSelection"};
ue3::FieldRef gCurrentTarget{"SFXGame.BioPlayerSelection", "m_oCurrentSelectionTarget"};
ue3::FieldRef gIsCombatTarget{"SFXGame.BioPlayerSelection", "m_bCurrentSelectionIsCombatTarget"};
ue3::FieldRef gCollision{"Engine.Actor", "CollisionComponent"};
ue3::FieldRef gBounds{"Engine.PrimitiveComponent", "Bounds"};
ue3::FieldRef gBoundsOrigin{gBounds, "Origin"};
ue3::FieldRef gBoundsExtent{gBounds, "BoxExtent"};

// Where the label sits over an object (Unreal units): above the top of its bounds, the top taken at most this far over
// their middle (a door or a wall panel's label stays in view), never this far over the eyes.
constexpr double kRaise = 8.0;
constexpr double kMaxRise = 50.0;
constexpr double kAboveEye = 10.0;

// This tick's gaze, for the hook's calls until the next tick (engine/SelectionGaze asks per call).
struct Gaze {
    bool valid = false;
    void const* selection = nullptr;
    ue3::Object const* camera = nullptr;  // the POV's owner: still alive at the call (a level change frees it)
    ue3::Vector* povLocation = nullptr;
    ue3::Rotator* povRotation = nullptr;
    ue3::Vector location;
    ue3::Rotator rotation;
};
Gaze gGaze;

il::Settings gSettings;
bool gToldNoHook = false;
bool gToldNoFields = false;
bool gToldNoHost = false;
bool gLabelSent = false;

// What A would use, as last seen.
ue3::Object const* gTarget = nullptr;
bool gCombat = false;
std::uint32_t gSerial = 0;
std::string gTargetName;
double gTargetMeters = 0.0;
bool gLabelVisible = false;
il::Label gLastLabel;

// Heartbeat counts.
unsigned gTargetChanges = 0;
unsigned gTicksWithGaze = 0;

bool Provide(void const* selection, engine::selection_gaze::Swap& out) {
    if (!gGaze.valid || selection != gGaze.selection || !ue3::runtime::IsAlive(gGaze.camera)) {
        return false;
    }
    out.povLocation = gGaze.povLocation;
    out.povRotation = gGaze.povRotation;
    out.location = gGaze.location;
    out.rotation = gGaze.rotation;
    return true;
}

ue3::Object* Selection(ue3::Object const* controller) {
    if (controller == nullptr || !player::IsBioPlayerController(controller)) {
        return nullptr;
    }
    auto const selection = gPlayerSelection.Read<ue3::Object*>(controller);
    ue3::Class const* const cls = gSelectionClass.Get();
    if (!selection || *selection == nullptr || cls == nullptr || !ue3::runtime::Get().IsA(*selection, cls)) {
        return nullptr;
    }
    return *selection;
}

// The middle of the target's collision bounds and their half height, or its location.
std::optional<std::pair<il::Point, double>> TargetBounds(ue3::Object const* target) {
    if (auto const component = gCollision.Read<ue3::Object*>(target); component && *component != nullptr) {
        void const* const bounds = gBounds.Address(*component);
        auto const origin = gBoundsOrigin.Read<ue3::Vector>(bounds);
        auto const extent = gBoundsExtent.Read<ue3::Vector>(bounds);
        if (origin && extent && std::isfinite(origin->z) && extent->z >= 0.0f && extent->z < 2000.0f) {
            return std::pair{il::Point{origin->x, origin->y, origin->z}, static_cast<double>(extent->z)};
        }
    }
    if (auto const at = player::Location(target)) {
        return std::pair{il::Point{at->x, at->y, at->z}, 0.0};
    }
    return std::nullopt;
}

void NoteTarget(ue3::Object const* target, bool combat, double meters) {
    if (target == gTarget && combat == gCombat) {
        return;
    }
    gTarget = target;
    gCombat = combat;
    ++gSerial;
    ++gTargetChanges;
    gTargetName = target != nullptr ? ue3::runtime::FullName(target) : std::string();
    if (target != nullptr) {
        log::Info("interact: A would use {}{}, {:.1f} m away{}", gTargetName, combat ? " (a combat target)" : "", meters,
            gSettings.gaze ? " (chosen from where you look)" : "");
    } else {
        log::Info("interact: nothing to use");
    }
}

void Publish(Link const& link, il::Label const& label) {
    gLastLabel = label;
    gLabelVisible = label.visible;
    if (link.publish != nullptr) {
        link.publish(&label);
        gLabelSent = true;
    }
}

} // namespace

bool WantsHook() {
    return gSettings.gaze || hand_calibration::GetInteract().gaze;
}

void Tick(float /*deltaSeconds*/, bool hooked, Link const& link) {
    gSettings = hand_calibration::GetInteract();
    gGaze.valid = false;
    engine::selection_gaze::SetProvider(hooked ? &Provide : nullptr);

    ue3::Object* const controller = player::Controller();
    std::optional<std::uint8_t> const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    bool const onFoot = mode && *mode == 0;
    std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
    ue3::Object* const selection = Selection(controller);
    if (controller != nullptr && selection == nullptr && !gToldNoFields && player::IsBioPlayerController(controller)) {
        gToldNoFields = true;
        log::Warn("interact: no BioPlayerSelection on the player controller (our reflection layer found no m_oPlayerSelection): "
                  "neither the gaze nor the label");
    }

    // The gaze for this tick's (and the next's) selection updates: the head's place and its forward in the world.
    if (gSettings.gaze && hooked && onFoot && frame && selection != nullptr) {
        ue3::Object* const camera = player::Camera(controller);
        if (auto const pov = player::CameraPov(camera); pov && pov->location != nullptr && pov->rotation != nullptr) {
            engine::UnrealVector const forward = engine::OrientationToWorld(*frame, frame->head.orientation).forward;
            engine::UnrealRotation const look = engine::ToRotation(forward);
            gGaze.valid = std::isfinite(frame->headWorld.x) && std::isfinite(frame->headWorld.y) && std::isfinite(frame->headWorld.z);
            gGaze.selection = selection;
            gGaze.camera = camera;
            gGaze.povLocation = pov->location;
            gGaze.povRotation = pov->rotation;
            gGaze.location = {static_cast<float>(frame->headWorld.x), static_cast<float>(frame->headWorld.y),
                static_cast<float>(frame->headWorld.z)};
            // The camera's own winding kept, so nothing that compares rotators sees a whole-turn jump.
            gGaze.rotation = {engine::Unwind(look.pitch, pov->rotation->pitch), engine::Unwind(look.yaw, pov->rotation->yaw), 0};
            gTicksWithGaze += gGaze.valid ? 1u : 0u;
        }
    }
    if (gSettings.gaze && !hooked && !gToldNoHook) {
        gToldNoHook = true;
        log::Warn("interact: the selection update isn't hooked: A uses what the game's camera faces, not what you look at");
    }

    // What A would use now, and where its label goes.
    ue3::Object const* target = nullptr;
    bool combat = false;
    if (selection != nullptr) {
        if (auto const t = gCurrentTarget.Read<ue3::Object*>(selection); t && *t != nullptr && ue3::runtime::IsAlive(*t)) {
            target = *t;
            combat = gIsCombatTarget.ReadBool(selection).value_or(false);
        }
    }
    il::Label label;
    label.heightDeg = gSettings.heightDeg;
    std::copy(gSettings.crop, gSettings.crop + 4, label.crop);
    double meters = 0.0;
    if (target != nullptr && frame) {
        if (auto const bounds = TargetBounds(target)) {
            il::Point const at = il::LabelPoint(bounds->first, bounds->second, frame->headWorld.z, kRaise, kMaxRise, kAboveEye);
            XrVector3f const position = engine::ToTracking(*frame, {at.x, at.y, at.z});
            XrVector3f const& head = frame->head.position;
            meters = std::sqrt((position.x - head.x) * (position.x - head.x) + (position.y - head.y) * (position.y - head.y) +
                (position.z - head.z) * (position.z - head.z));
            label.position = position;
            label.combat = combat;
            label.visible = gSettings.label && onFoot && (!combat || gSettings.combatLabel) && std::isfinite(meters);
        }
    }
    NoteTarget(target, combat, meters);
    gTargetMeters = meters;
    label.serial = gSerial;
    if (link.publish == nullptr && gSettings.label && !gToldNoHost) {
        gToldNoHost = true;
        log::Warn("interact: the host (services older than v21) can't show the prompt at the object: close the game and "
                  "redeploy the host");
    }
    Publish(link, label);
}

std::string StatusJson() {
    il::Label const& l = gLastLabel;
    return std::format("{{\"gaze\": {}, \"label\": {}, \"target\": {}, \"combat\": {}, \"meters\": {}, \"labelVisible\": {}, "
                       "\"position\": [{}, {}, {}], \"heightDeg\": {}, \"serial\": {}}}",
        gSettings.gaze ? "true" : "false", gSettings.label ? "true" : "false",
        gTarget != nullptr ? json::JsonString(gTargetName) : std::string("null"), gCombat ? "true" : "false", json::JsonNumber(gTargetMeters),
        gLabelVisible ? "true" : "false", json::JsonNumber(l.position.x), json::JsonNumber(l.position.y), json::JsonNumber(l.position.z),
        json::JsonNumber(l.heightDeg), l.serial);
}

void LogStats() {
    engine::selection_gaze::Stats const stats = engine::selection_gaze::Take();
    if (stats.calls == 0 && gTargetChanges == 0) {
        return;
    }
    log::Info("    interact: {} selection updates ({} from your gaze, {} ticks with a gaze), {} target changes; now {}", stats.calls,
        stats.swapped, gTicksWithGaze, gTargetChanges,
        gTarget != nullptr ? std::format("{} at {:.1f} m{}", gTargetName, gTargetMeters, gLabelVisible ? ", labelled" : "") : std::string("nothing"));
    gTargetChanges = 0;
    gTicksWithGaze = 0;
}

void Shutdown(Link const& link) {
    engine::selection_gaze::SetProvider(nullptr);
    gGaze = {};
    if (gLabelSent && link.publish != nullptr) {
        il::Label const none;
        link.publish(&none);
    }
    gLabelSent = false;
    gTarget = nullptr;
    gCombat = false;
    gTargetName.clear();
    gLabelVisible = false;
    gToldNoHook = gToldNoFields = gToldNoHost = false;
    gSettings = {};
}

} // namespace mevr::game::interact
