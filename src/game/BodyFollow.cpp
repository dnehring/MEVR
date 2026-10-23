#include "game/BodyFollow.hpp"

#include "core/Log.hpp"
#include "core/ik/BodyFollow.hpp"
#include "core/ik/Ik.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/Objects.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HandCalibration.hpp"
#include "engine/BonePose.hpp"

#include <LESDK/Headers.hpp>

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>

namespace mevr::game::body_follow {
namespace {

constexpr double kDegrees = 180.0 / std::numbers::pi;
// The torso heading leans this far toward the hands (both tracked, in front of the head).
constexpr double kHandsYawWeight = 0.5;
constexpr double kHandsMinMeters = 0.15;
constexpr double kHandsMaxDegrees = 100.0;
// With the whole body turning, the spine twists at most this much; the hips take the rest.
constexpr double kSpineTwistWithBodyDegrees = 30.0;
// The `torso:` line: at most this often, when the result is this far off the player.
constexpr std::uint64_t kLogEveryMs = 2000;
constexpr std::uint64_t kAnchorLogEveryMs = 1000;
constexpr double kYawGapDegrees = 15.0;
constexpr double kReferenceGapMeters = 0.10;
constexpr double kBigTurnDegrees = 60.0;
constexpr double kBigShiftMeters = 0.35;

ik::BodyFollow gFollow;
bool gDriven = false;
std::optional<vr::floor::State> gFloor;
std::uint64_t gNextLogMs = 0;
std::uint64_t gNextAnchorLogMs = 0;
unsigned gAnchors = 0;
unsigned gOffLines = 0;

// This tick's inputs, for the log and the snapshot (world yaws in degrees, UE: + turns right).
struct Last {
    bool valid = false;
    char const* mode = "off";
    ik::FollowState state;
    double headYaw = 0.0;      // tracking space (BodyYaw convention: + left), degrees
    double facingYaw = 0.0;    // what the body turns toward, tracking, degrees
    double worldHead = 0.0;    // the view's yaw in the world
    double worldBody = 0.0;    // the body's yaw in the world
    double pawnYaw = 0.0;
    double controlYaw = 0.0;
    double cameraYaw = 0.0;
    double unitsPerMeter = 50.0;  // component units per meter of head motion
    ik::FollowAnchor lastAnchor = ik::FollowAnchor::None;
    std::uint32_t recenters = 0;
    bool haveRecenters = false;
};
Last gLast;

double UnitsToDegrees(int units) {
    double d = static_cast<double>(units) * 360.0 / 65536.0;
    d = std::fmod(d, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}

double WrapDegrees(double d) {
    d = std::fmod(d, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}

// The tracking-space yaw (BodyYaw convention) as a world direction through the view's frame.
std::optional<ik::Vec3> WorldForward(engine::TrackingFrame const& frame, XrVector3f const& at, double yaw) {
    XrVector3f const ahead{at.x - static_cast<float>(std::sin(yaw)), at.y, at.z - static_cast<float>(std::cos(yaw))};
    engine::UnrealVector const a = engine::ToWorld(frame, at);
    engine::UnrealVector const b = engine::ToWorld(frame, ahead);
    ik::Vec3 const forward{b.x - a.x, b.y - a.y, 0.0};
    if (!(ik::Length(forward) > 1e-6)) {
        return std::nullopt;
    }
    return ik::Normalize(forward);
}

// The yaw the body turns toward (tracking): the head's, pulled halfway toward the hands when both are
// tracked, out in front and not behind.
double FacingYaw(hand_targets::Targets const& targets, double headYaw) {
    vr::controllers::State const controllers = vr::controllers::Latest();
    if (!controllers.active || !controllers.left.gripValid || !controllers.right.gripValid) {
        return headYaw;
    }
    XrVector3f const& l = controllers.left.grip.position;
    XrVector3f const& r = controllers.right.grip.position;
    XrVector3f const& h = targets.head.position;
    double const dx = 0.5 * (l.x + r.x) - h.x;
    double const dz = 0.5 * (l.z + r.z) - h.z;
    double const handsYaw = std::atan2(-dx, -dz);  // BodyYaw's convention: 0 = -Z, + = left
    double const apart = std::abs(ik::BlendYaw(0.0, handsYaw - headYaw, 1.0)) * kDegrees;
    if (std::hypot(dx, dz) >= kHandsMinMeters && apart <= kHandsMaxDegrees) {
        return ik::BlendYaw(headYaw, handsYaw, kHandsYawWeight);
    }
    return headYaw;
}

ik::Vec3 ToComponent(ik::Affine const& worldToComponent, engine::UnrealVector const& world) {
    return ik::TransformPoint(worldToComponent, {world.x, world.y, world.z});
}

double ComponentScale(ik::Affine const& worldToComponent) {
    double const s = ik::Length(ik::TransformDirection(worldToComponent, {1.0, 0.0, 0.0}));
    return std::isfinite(s) && s > 1e-6 ? s : 1.0;
}

// The last write's result in the player's terms: how far the shoulders' facing stayed from the wanted
// one, how far Shepard's head stayed from its goal, how far the body moved.
struct Off {
    double yawGap = 0.0;        // degrees the facing stayed short
    double referenceGap = 0.0;  // meters Shepard's reference stayed from its goal
    double shift = 0.0;         // meters the body slid
    double wantedShift = 0.0;   // meters the view was from where the game put him
    bool shiftCapped = false;
    double bodyTurn = 0.0;
    double wantedTurn = 0.0;
    double twist = 0.0;
    double bend = 0.0;
};

std::optional<Off> LastOff(double unitsPerMeter) {
    auto const result = engine::bone_pose::LatestTorso();
    if (!result || !result->ok || !(unitsPerMeter > 0.0)) {
        return std::nullopt;
    }
    ik::TorsoResult const& r = *result;
    Off off;
    off.wantedTurn = r.wantedTurnDegrees;
    off.bodyTurn = r.bodyTurnDegrees;
    off.twist = r.twistDegrees;
    off.bend = r.bendDegrees;
    off.yawGap = WrapDegrees(r.wantedTurnDegrees - r.bodyTurnDegrees - r.twistDegrees);
    off.referenceGap = ik::Length(r.newReference - r.goalReference) / unitsPerMeter;
    off.shift = ik::Length(r.bodyShift) / unitsPerMeter;
    off.wantedShift = ik::Length(r.wantedBodyShift) / unitsPerMeter;
    off.shiftCapped = r.shiftLimited;
    return off;
}

void LogAnchor(ik::FollowState const& state) {
    ++gAnchors;
    std::uint64_t const now = GetTickCount64();
    if (now < gNextAnchorLogMs) {
        return;
    }
    gNextAnchorLogMs = now + kAnchorLogEveryMs;
    log::Info("torso: re-anchored ({}): feet under the head's pivot ({:.2f}, {:.2f}) m, body yaw {:.0f} deg (tracking space)",
        ik::Name(state.anchored), state.pivot.x, state.pivot.z, state.yaw * kDegrees);
}

void LogIfOff() {
    auto const off = LastOff(gLast.unitsPerMeter);
    if (!off) {
        return;
    }
    bool const yawOff = std::abs(off->yawGap) > kYawGapDegrees;
    bool const headOff = off->referenceGap > kReferenceGapMeters;
    bool const big = std::abs(off->bodyTurn) > kBigTurnDegrees || off->wantedShift > kBigShiftMeters;
    if (!yawOff && !headOff && !off->shiftCapped && !big) {
        return;
    }
    ++gOffLines;
    std::uint64_t const now = GetTickCount64();
    if (now < gNextLogMs) {
        return;
    }
    gNextLogMs = now + kLogEveryMs;
    ik::FollowState const& s = gLast.state;
    log::Info("torso: {}{}{}{} ({}): head yaw {:.0f} (world {:.0f}), body yaw {:.0f} (world {:.0f}), pawn {:.0f}, "
              "ControlRotation {:.0f}, camera {:.0f} deg; turn wanted {:.0f} = body {:.0f} + spine {:.0f} (short {:.0f}), bend {:.0f} deg; "
              "the view {:.2f} m from where the game put Shepard, body slid {:.2f} m{}; Shepard's head {:.2f} m from the head's pivot; "
              "lean {:.2f} m (head pivot ({:.2f}, {:.2f}), feet ({:.2f}, {:.2f}) m)",
        yawOff ? "shoulders turned off the player's" : "", headOff ? (yawOff ? ", head off the view" : "head off the view") : "",
        off->shiftCapped ? (yawOff || headOff ? ", body slide capped" : "body slide capped") : "",
        !yawOff && !headOff && !off->shiftCapped ? "large correction" : "", gLast.mode, gLast.headYaw, gLast.worldHead,
        gLast.state.yaw * kDegrees, gLast.worldBody, gLast.pawnYaw, gLast.controlYaw, gLast.cameraYaw, off->wantedTurn, off->bodyTurn,
        off->twist, off->yawGap, off->bend, off->wantedShift, off->shift,
        off->shiftCapped ? " (capped: [Hands] bodyMoveMaxM; recenter with R to bring the view back over him)" : "",
        off->referenceGap, std::hypot(s.lean.x, s.lean.z), s.pivot.x, s.pivot.z, s.feet.x, s.feet.z);
}

} // namespace

void NoteFloor(std::optional<vr::floor::State> const& floor) {
    gFloor = floor;
}

bool Fill(hand_targets::Targets const& targets, ik::Affine const& worldToComponent, ik::BodyFrame const& frame,
    APawn const* pawn, Bones const& bones, double deltaSeconds, ik::TorsoGoal& goal) {
    hand_calibration::BodyMove const move = hand_calibration::GetBodyMove();
    XrPosef const& head = targets.head;
    ik::Quat const orientation{head.orientation.x, head.orientation.y, head.orientation.z, head.orientation.w};
    double const headYaw = ik::YawOf(head.orientation.x, head.orientation.y, head.orientation.z, head.orientation.w);
    double const facingYaw = FacingYaw(targets, headYaw);
    ik::Vec3 const pivot = ik::HeadPivot({head.position.x, head.position.y, head.position.z}, orientation);

    ik::FollowSettings settings;
    settings.leanRadius = move.leanMeters;
    gFollow.SetSettings(settings);
    ik::FollowInput in;
    in.pivot = pivot;
    in.headYaw = headYaw;
    in.facingYaw = facingYaw;
    in.haveRecenters = gFloor.has_value();
    in.recenters = gFloor ? gFloor->recenters : 0;
    in.deltaSeconds = deltaSeconds;
    ik::FollowState const state = gFollow.Update(in);
    if (state.anchored != ik::FollowAnchor::None) {
        LogAnchor(state);
        gLast.lastAnchor = state.anchored;
    }
    gDriven = true;

    auto const forwardWorld = WorldForward(targets.body, head.position, state.yaw);
    bool const firstPerson = targets.anchor == hand_targets::Anchor::View;
    std::array<int, 2> reference{bones.head, -1};
    bool const wholeBody = move.on && bones.body >= 0;
    if (firstPerson && !wholeBody && bones.eyes[0] >= 0 && bones.eyes[1] >= 0) {
        reference = bones.eyes;
    }
    if (!forwardWorld || reference[0] < 0) {
        gLast.valid = false;
        return false;
    }
    double const scale = ComponentScale(worldToComponent);
    goal.reference = reference;
    if (firstPerson && wholeBody) {
        // The whole body under the view: the feet (tracking space, steps after the head) under the view's
        // map of them, Shepard's head bone at the head's pivot, the hips turning what the spine can't.
        XrVector3f const p{static_cast<float>(pivot.x), static_cast<float>(pivot.y), static_cast<float>(pivot.z)};
        XrVector3f const f{static_cast<float>(state.feet.x), static_cast<float>(state.feet.y), static_cast<float>(state.feet.z)};
        goal.head = ToComponent(worldToComponent, engine::ToWorld(targets.body, p));
        goal.relative = false;
        goal.body = bones.body;
        goal.bodyTurns = true;
        goal.bodyMoves = true;
        goal.bodyAt = ToComponent(worldToComponent, engine::ToWorld(targets.body, f));
        goal.maxBodyShift = move.maxMeters * targets.body.unitsPerMeter * scale;
        goal.maxTwistDegrees = kSpineTwistWithBodyDegrees;
        gLast.mode = "first person: the whole body under the view";
    } else if (firstPerson) {
        // The spine alone (bodyMove off): Shepard's eyes move as far as the headset moved from where it was
        // recentered (ik::TorsoGoal::relative), the old behaviour.
        engine::UnrealVector const origin = engine::OriginWorld(targets.body);
        goal.head = ik::TransformDirection(worldToComponent,
            {targets.headWorld.x - origin.x, targets.headWorld.y - origin.y, targets.headWorld.z - origin.z});
        goal.relative = true;
        gLast.mode = "first person: the spine alone";
    } else {
        // Third person: the hands hang off Shepard's animated head bone; his head goes to the headset there.
        // With bodyMove the hips still take the turn the spine can't.
        goal.head = ToComponent(worldToComponent, targets.headWorld);
        goal.relative = false;
        if (wholeBody) {
            goal.body = bones.body;
            goal.bodyTurns = true;
            goal.bodyMoves = false;
            goal.maxTwistDegrees = kSpineTwistWithBodyDegrees;
        }
        gLast.mode = wholeBody ? "third person: the hips turn" : "third person: the spine alone";
    }
    ik::Vec3 forward = ik::TransformDirection(worldToComponent, *forwardWorld);
    forward = forward - frame.up * ik::Dot(forward, frame.up);
    goal.forward = ik::Normalize(forward, frame.forward);

    // The inputs, for the log and the snapshot.
    gLast.valid = true;
    gLast.state = state;
    gLast.headYaw = headYaw * kDegrees;
    gLast.facingYaw = facingYaw * kDegrees;
    gLast.unitsPerMeter = targets.body.unitsPerMeter * scale;
    gLast.haveRecenters = in.haveRecenters;
    gLast.recenters = in.recenters;
    gLast.worldBody = std::atan2(forwardWorld->y, forwardWorld->x) * kDegrees;
    if (auto const view = WorldForward(targets.body, head.position, headYaw)) {
        gLast.worldHead = std::atan2(view->y, view->x) * kDegrees;
    }
    gLast.pawnYaw = pawn != nullptr ? UnitsToDegrees(pawn->Rotation.Yaw) : 0.0;
    APlayerController* const controller = engine::LocalPlayerController();
    gLast.controlYaw = controller != nullptr ? UnitsToDegrees(controller->Rotation.Yaw) : 0.0;
    gLast.cameraYaw = controller != nullptr && controller->PlayerCamera != nullptr
        ? UnitsToDegrees(controller->PlayerCamera->CameraCache.POV.Rotation.Yaw)
        : 0.0;
    LogIfOff();
    return true;
}

void Reset() {
    gFollow.Reset();
    gDriven = false;
    gLast.valid = false;
}

std::optional<double> Yaw() {
    if (!gDriven || !gFollow.Active()) {
        return std::nullopt;
    }
    return gFollow.Last().yaw;
}

std::string StatusJson() {
    namespace json = vr::test_request;
    if (!gLast.valid) {
        return std::format("{{\"driven\": false, \"anchors\": {}, \"offLines\": {}}}", gAnchors, gOffLines);
    }
    ik::FollowState const& s = gLast.state;
    std::string result = "null";
    if (auto const off = LastOff(gLast.unitsPerMeter)) {
        result = std::format("{{\"wantedTurnDeg\": {}, \"bodyTurnDeg\": {}, \"spineTwistDeg\": {}, \"yawGapDeg\": {}, \"bendDeg\": {}, "
                             "\"viewFromBodyM\": {}, \"bodySlideM\": {}, \"slideCapped\": {}, \"headFromPivotM\": {}}}",
            json::JsonNumber(off->wantedTurn), json::JsonNumber(off->bodyTurn), json::JsonNumber(off->twist), json::JsonNumber(off->yawGap),
            json::JsonNumber(off->bend), json::JsonNumber(off->wantedShift), json::JsonNumber(off->shift),
            off->shiftCapped ? "true" : "false", json::JsonNumber(off->referenceGap));
    }
    return std::format("{{\"driven\": true, \"mode\": {}, \"lastAnchor\": {}, \"anchors\": {}, \"offLines\": {}, \"recenters\": {}, "
                       "\"headYawDeg\": {}, \"facingYawDeg\": {}, \"bodyYawDeg\": {}, \"worldYawDeg\": {{\"head\": {}, \"body\": {}, "
                       "\"pawn\": {}, \"controlRotation\": {}, \"camera\": {}}}, \"pivot\": [{}, {}, {}], \"feet\": [{}, {}], "
                       "\"leanM\": {}, \"unitsPerMeter\": {}, \"result\": {}}}",
        json::JsonString(gLast.mode), json::JsonString(ik::Name(gLast.lastAnchor)), gAnchors, gOffLines,
        gLast.haveRecenters ? std::to_string(gLast.recenters) : std::string("null"), json::JsonNumber(gLast.headYaw),
        json::JsonNumber(gLast.facingYaw), json::JsonNumber(s.yaw * kDegrees), json::JsonNumber(gLast.worldHead),
        json::JsonNumber(gLast.worldBody), json::JsonNumber(gLast.pawnYaw), json::JsonNumber(gLast.controlYaw),
        json::JsonNumber(gLast.cameraYaw), json::JsonNumber(s.pivot.x), json::JsonNumber(s.pivot.y), json::JsonNumber(s.pivot.z),
        json::JsonNumber(s.feet.x), json::JsonNumber(s.feet.z), json::JsonNumber(std::hypot(s.lean.x, s.lean.z)),
        json::JsonNumber(gLast.unitsPerMeter), result);
}

void LogStats() {
    if (!gLast.valid) {
        return;
    }
    auto const off = LastOff(gLast.unitsPerMeter);
    log::Info("    torso: {}; re-anchored {} times (last: {}), {} off-the-player ticks; body yaw {:.0f} vs head {:.0f} deg (tracking), "
              "pawn {:.0f} vs body {:.0f} deg (world){}",
        gLast.mode, gAnchors, ik::Name(gLast.lastAnchor), gOffLines, gLast.state.yaw * kDegrees, gLast.headYaw, gLast.pawnYaw,
        gLast.worldBody,
        off ? std::format("; last write: hips {:.0f} + spine {:.0f} of {:.0f} deg, slid {:.2f} of {:.2f} m, head {:.2f} m off", off->bodyTurn,
                  off->twist, off->wantedTurn, off->shift, off->wantedShift, off->referenceGap)
            : std::string());
    gOffLines = 0;
}

void Shutdown() {
    gFollow = ik::BodyFollow{};
    gDriven = false;
    gFloor.reset();
    gLast = {};
    gAnchors = 0;
    gOffLines = 0;
    gNextLogMs = gNextAnchorLogMs = 0;
}

} // namespace mevr::game::body_follow
