// SPDX-License-Identifier: MIT
#include "game/BodyFollow.hpp"

#include "game/ConversationWorld.hpp"

#include "core/Log.hpp"
#include "core/ik/BodyFollow.hpp"
#include "core/ik/Ik.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "game/Weapon.hpp"
#include "engine/BonePose.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
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
// Standing still, the whole body slides at most this far to hold the hips over the feet (the idle sway is about 6 cm).
constexpr double kMaxAnchorShiftMeters = 0.15;
// The `torso:` line: at most this often, when the result is this far off the player.
constexpr std::uint64_t kLogEveryMs = 2000;
constexpr std::uint64_t kAnchorLogEveryMs = 1000;
constexpr double kYawGapDegrees = 15.0;
constexpr double kReferenceGapMeters = 0.10;
constexpr double kBigTurnDegrees = 60.0;
constexpr double kBigShiftMeters = 0.35;
// The legs' `torso: legs keep the run` line while the pawn moves: at most this often.
constexpr std::uint64_t kLegsLogEveryMs = 5000;

#if defined(MEVR_GAME_LE1)
// The pawn's speed (the legs keep the run while it moves). engine/ReflectCheck has its row.
ue3::FieldRef gVelocity{"Engine.Actor", "Velocity"};
#endif

ik::BodyFollow gFollow;
ik::LegsFollow gLegs;
bool gDriven = false;
std::optional<vr::floor::State> gFloor;
std::uint64_t gNextLogMs = 0;
std::uint64_t gNextAnchorLogMs = 0;
unsigned gAnchors = 0;
unsigned gOffLines = 0;
unsigned gLegsStarts = 0;       // moves begun (standing -> moving) since the last heartbeat
unsigned gLegsMovingTicks = 0;  // ticks the legs kept the run since the last heartbeat
std::uint64_t gNextLegsLogMs = 0;
bool gToldNoSpeed = false;

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
    // The legs (research/29 "The legs keep the run"): whether the rule ran this tick, its state, the pawn's speed.
    bool legsUsed = false;
    ik::LegsState legs;
    double speed = 0.0;         // units a second, horizontal; NaN: unreadable
    double legsTwistDeg = 60.0;
    double anchorWeight = 0.0;  // the hips held over the feet (1 standing still; research/29 "The idle sway")
    bool weaponDrawn = false;
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

// The pawn's horizontal speed (units a second), NaN when it can't be read (then the legs count as standing: the old rule).
double PawnSpeed(ue3::Object const* pawn) {
#if defined(MEVR_GAME_LE1)
    if (pawn != nullptr) {
        if (auto const v = gVelocity.Read<ue3::Vector>(pawn)) {
            double const speed = std::hypot(static_cast<double>(v->x), static_cast<double>(v->y));
            if (std::isfinite(speed)) {
                return speed;
            }
        }
    }
#else
    (void)pawn;
#endif
    return std::numeric_limits<double>::quiet_NaN();
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
    double standingTurn = 0.0;       // the turn the standing rule alone would have made (degrees)
    double shouldersFromLegs = 0.0;  // how far the animation had turned the shoulders from the legs (degrees)
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
    off.standingTurn = r.standingTurnDegrees;
    off.shouldersFromLegs = r.shouldersFromLegsDegrees;
    return off;
}

// The legs' part of a line: "legs keep the run (...)" or "standing".
std::string LegsText() {
    if (!gLast.legsUsed) {
        return "the legs turn with the body (legsFollowMotion off, or no whole body)";
    }
    ik::LegsState const& l = gLast.legs;
    std::string const speed = std::isfinite(gLast.speed) ? std::format("{:.0f} u/s", gLast.speed) : std::string("speed unreadable");
    if (!l.moving && l.weight <= 0.0) {
        return std::format("legs: standing ({}): the hips turn what the spine's 30 deg can't", speed);
    }
    return std::format("legs: {} ({}, {}, weight {:.2f}): shoulders {:.0f} deg from the legs, the spine {:.0f} (limit {:.0f}) + the whole body {:.0f}",
        l.moving ? "keep the run" : "settling", speed, gLast.weaponDrawn ? "weapon drawn" : "holstered", l.weight, l.facing * kDegrees,
        l.spineFromLegs * kDegrees, gLast.legsTwistDeg, l.legsTurn * kDegrees);
}

// While the pawn moves: a `torso: legs keep the run` line at most every kLegsLogEveryMs, with what the standing rule would
// have turned (the last write's), so a strafe (shoulders 90 from the legs: the body 30 instead of 60) reads off the log.
void NoteLegs(ik::LegsState const& legs) {
    if (legs.started) {
        ++gLegsStarts;
    }
    if (!legs.moving) {
        return;
    }
    ++gLegsMovingTicks;
    std::uint64_t const now = GetTickCount64();
    if (now < gNextLegsLogMs) {
        return;
    }
    gNextLegsLogMs = now + kLegsLogEveryMs;
    auto const off = LastOff(gLast.unitsPerMeter);
    log::Info("torso: {}{}", LegsText(),
        off ? std::format("; last write: the whole body {:.0f} deg (the standing rule: {:.0f}), the spine {:.0f}; the animation had the shoulders "
                          "{:.0f} deg from the legs",
                  off->bodyTurn, off->standingTurn, off->twist, off->shouldersFromLegs)
            : std::string());
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
              "lean {:.2f} m (head pivot ({:.2f}, {:.2f}), feet ({:.2f}, {:.2f}) m); {}",
        yawOff ? "shoulders turned off the player's" : "", headOff ? (yawOff ? ", head off the view" : "head off the view") : "",
        off->shiftCapped ? (yawOff || headOff ? ", body slide capped" : "body slide capped") : "",
        !yawOff && !headOff && !off->shiftCapped ? "large correction" : "", gLast.mode, gLast.headYaw, gLast.worldHead,
        gLast.state.yaw * kDegrees, gLast.worldBody, gLast.pawnYaw, gLast.controlYaw, gLast.cameraYaw, off->wantedTurn, off->bodyTurn,
        off->twist, off->yawGap, off->bend, off->wantedShift, off->shift,
        off->shiftCapped ? " (capped: [Hands] bodyMoveMaxM; recenter with R to bring the view back over him)" : "",
        off->referenceGap, std::hypot(s.lean.x, s.lean.z), s.pivot.x, s.pivot.z, s.feet.x, s.feet.z, LegsText());
}

} // namespace

void NoteFloor(std::optional<vr::floor::State> const& floor) {
    gFloor = floor;
}

bool Fill(hand_targets::Targets const& targets, ik::Affine const& worldToComponent, ik::BodyFrame const& frame,
    ue3::Object const* pawn, Bones const& bones, double deltaSeconds, ik::TorsoGoal& goal) {
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
#if defined(MEVR_GAME_LE1)
        // The view is away from Shepard's pawn (a scripted walk, a walk-in by the view: research/69 "The body stays with
        // the player"): his body is drawn where the player stands, however far the script has taken the actor, and on the
        // player's floor. The same slide as on foot, without its limit; the bones move, the component stays the actor's.
        if (conversation_world::BodyAwayState const away = conversation_world::BodyAway(); away.on) {
            goal.maxBodyShift = 1e12;
            goal.bodyRise = away.riseUnits * scale;
            gLast.mode = "first person: the whole body where the player stands, away from the pawn";
        }
#endif
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

    // The legs keep the run (research/29, 2026-11-04): while the pawn moves, its legs keep its facing (where the game's
    // run, strafe and backpedal animations point) and only the spine twists toward the view, up to legsTwistDeg from
    // them; the whole body turns beyond that. Standing (or the speed unreadable): the hips take what the spine can't.
    gLast.legsUsed = false;
    if (goal.body >= 0 && goal.bodyTurns && move.legsFollowMotion) {
        ik::LegsSettings legsSettings;
        legsSettings.twistLimitRadians = move.legsTwistDegrees / kDegrees;
        gLegs.SetSettings(legsSettings);
        if (state.anchored != ik::FollowAnchor::None) {
            gLegs.Reset();
        }
#if defined(MEVR_GAME_LE1)
        // (Away from the pawn its speed is the script's walk, not the player's: the legs turn with the body.)
        double const speed = conversation_world::BodyAway().on ? 0.0 : PawnSpeed(pawn);
#else
        double const speed = PawnSpeed(pawn);
#endif
        if (!std::isfinite(speed) && !gToldNoSpeed) {
            gToldNoSpeed = true;
            log::Warn("torso: the pawn's velocity (Engine.Actor.Velocity) can't be read: the legs turn with the body while moving too");
        }
        ik::LegsInput legsIn;
        legsIn.facing = ik::YawBetween(frame.forward, goal.forward, frame.up);
        legsIn.speed = speed;
        legsIn.deltaSeconds = deltaSeconds;
        ik::LegsState const legs = gLegs.Update(legsIn);
        goal.legsWeight = legs.weight;
        goal.legsTurnDegrees = legs.legsTurn * kDegrees;
        goal.legsTwistDegrees = move.legsTwistDegrees;
        gLast.legsUsed = true;
        gLast.legs = legs;
        gLast.speed = speed;
        gLast.legsTwistDeg = move.legsTwistDegrees;
        gLast.weaponDrawn = weapon::Key() != 0;
        NoteLegs(legs);
    } else {
        gLegs.Reset();
    }

    // Standing still, the hips over the feet (research/29 "The idle sway", 2026-11-14): the idle animation's weight shift
    // rocks Shepard's pelvis about 6 cm side to side against the whole body's bone every 5-7 s (measured at the
    // Presidium, `mevr camera trace`), so his hips, spine and shoulders swayed under a player standing still. The torso
    // slides the whole body to hold the pelvis over the feet, by the part of standing still (the legs' blend toward the
    // run: 0 standing, 1 moving), and the writer plants the feet where the animation had them (bone_pose's Leg::planted).
    // Only with the whole body under the view, the legs' motion rule known and the body at the pawn (a scripted walk's
    // legs stand by their own IK).
    goal.anchorWeight = 0.0;
    gLast.anchorWeight = 0.0;
    bool away = false;
#if defined(MEVR_GAME_LE1)
    away = conversation_world::BodyAway().on;
#endif
    if (move.steadyIdle && firstPerson && wholeBody && gLast.legsUsed && !away && bones.anchor >= 0 && bones.feet[0] >= 0 &&
        bones.feet[1] >= 0) {
        goal.anchor = bones.anchor;
        goal.feet = bones.feet;
        goal.anchorWeight = std::clamp(1.0 - gLast.legs.weight, 0.0, 1.0);
        goal.maxAnchorShift = kMaxAnchorShiftMeters * targets.body.unitsPerMeter * scale;
        gLast.anchorWeight = goal.anchorWeight;
    }

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
    // Through our reflection layer (game/player); 0 when a read isn't there, as before for a missing object.
    auto const pawnRotation = player::Rotation(pawn);
    gLast.pawnYaw = pawnRotation ? UnitsToDegrees(pawnRotation->yaw) : 0.0;
    ue3::Object* const controller = player::Controller();
    auto const controlRotation = player::Rotation(controller);
    gLast.controlYaw = controlRotation ? UnitsToDegrees(controlRotation->yaw) : 0.0;
    auto const pov = player::CameraPov(player::Camera(controller));
    gLast.cameraYaw = pov ? UnitsToDegrees(pov->rotation->yaw) : 0.0;
    LogIfOff();
    return true;
}

void Reset() {
    gFollow.Reset();
    gLegs.Reset();
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
        result = std::format("{{\"wantedTurnDeg\": {}, \"bodyTurnDeg\": {}, \"standingTurnDeg\": {}, \"spineTwistDeg\": {}, "
                             "\"shouldersFromLegsDeg\": {}, \"yawGapDeg\": {}, \"bendDeg\": {}, "
                             "\"viewFromBodyM\": {}, \"bodySlideM\": {}, \"slideCapped\": {}, \"headFromPivotM\": {}}}",
            json::JsonNumber(off->wantedTurn), json::JsonNumber(off->bodyTurn), json::JsonNumber(off->standingTurn), json::JsonNumber(off->twist),
            json::JsonNumber(off->shouldersFromLegs), json::JsonNumber(off->yawGap),
            json::JsonNumber(off->bend), json::JsonNumber(off->wantedShift), json::JsonNumber(off->shift),
            off->shiftCapped ? "true" : "false", json::JsonNumber(off->referenceGap));
    }
    std::string legs = "null";
    if (gLast.legsUsed) {
        ik::LegsState const& l = gLast.legs;
        legs = std::format("{{\"moving\": {}, \"weight\": {}, \"speedUps\": {}, \"weaponDrawn\": {}, \"shouldersFromLegsDeg\": {}, "
                           "\"legsTurnDeg\": {}, \"spineFromLegsDeg\": {}, \"twistLimitDeg\": {}, \"starts\": {}}}",
            l.moving ? "true" : "false", json::JsonNumber(l.weight), std::isfinite(gLast.speed) ? json::JsonNumber(gLast.speed) : std::string("null"),
            gLast.weaponDrawn ? "true" : "false", json::JsonNumber(l.facing * kDegrees), json::JsonNumber(l.legsTurn * kDegrees),
            json::JsonNumber(l.spineFromLegs * kDegrees), json::JsonNumber(gLast.legsTwistDeg), gLegsStarts);
    }
    return std::format("{{\"driven\": true, \"mode\": {}, \"lastAnchor\": {}, \"anchors\": {}, \"offLines\": {}, \"recenters\": {}, "
                       "\"headYawDeg\": {}, \"facingYawDeg\": {}, \"bodyYawDeg\": {}, \"worldYawDeg\": {{\"head\": {}, \"body\": {}, "
                       "\"pawn\": {}, \"controlRotation\": {}, \"camera\": {}}}, \"pivot\": [{}, {}, {}], \"feet\": [{}, {}], "
                       "\"leanM\": {}, \"unitsPerMeter\": {}, \"legs\": {}, \"result\": {}}}",
        json::JsonString(gLast.mode), json::JsonString(ik::Name(gLast.lastAnchor)), gAnchors, gOffLines,
        gLast.haveRecenters ? std::to_string(gLast.recenters) : std::string("null"), json::JsonNumber(gLast.headYaw),
        json::JsonNumber(gLast.facingYaw), json::JsonNumber(s.yaw * kDegrees), json::JsonNumber(gLast.worldHead),
        json::JsonNumber(gLast.worldBody), json::JsonNumber(gLast.pawnYaw), json::JsonNumber(gLast.controlYaw),
        json::JsonNumber(gLast.cameraYaw), json::JsonNumber(s.pivot.x), json::JsonNumber(s.pivot.y), json::JsonNumber(s.pivot.z),
        json::JsonNumber(s.feet.x), json::JsonNumber(s.feet.z), json::JsonNumber(std::hypot(s.lean.x, s.lean.z)),
        json::JsonNumber(gLast.unitsPerMeter), legs, result);
}

void LogStats() {
    if (!gLast.valid) {
        return;
    }
    auto const off = LastOff(gLast.unitsPerMeter);
    log::Info("    torso: {}; re-anchored {} times (last: {}), {} off-the-player ticks; body yaw {:.0f} vs head {:.0f} deg (tracking), "
              "pawn {:.0f} vs body {:.0f} deg (world); {} (moves begun: {}, ticks keeping the run: {}){}",
        gLast.mode, gAnchors, ik::Name(gLast.lastAnchor), gOffLines, gLast.state.yaw * kDegrees, gLast.headYaw, gLast.pawnYaw,
        gLast.worldBody, LegsText(), gLegsStarts, gLegsMovingTicks,
        off ? std::format("; last write: hips {:.0f} (standing rule {:.0f}) + spine {:.0f} of {:.0f} deg, slid {:.2f} of {:.2f} m, head {:.2f} m off",
                  off->bodyTurn, off->standingTurn, off->twist, off->wantedTurn, off->shift, off->wantedShift, off->referenceGap)
            : std::string());
    gOffLines = 0;
    gLegsStarts = 0;
    gLegsMovingTicks = 0;
}

void Shutdown() {
    gFollow = ik::BodyFollow{};
    gLegs = ik::LegsFollow{};
    gDriven = false;
    gFloor.reset();
    gLast = {};
    gAnchors = 0;
    gOffLines = 0;
    gLegsStarts = 0;
    gLegsMovingTicks = 0;
    gNextLogMs = gNextAnchorLogMs = gNextLegsLogMs = 0;
    gToldNoSpeed = false;
}

} // namespace mevr::game::body_follow
