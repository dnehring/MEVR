// SPDX-License-Identifier: MIT
// Squad placement by an arc (game/SquadPlace.hpp).

#include "game/SquadPlace.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/SquadContext.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "game/PowerWheel.hpp"
#include "game/SquadContext.hpp"
#include "game/SquadOrders.hpp"
#include "game/SquadWeapons.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>
#include <sstream>
#include <vector>

namespace mevr::game::squad_place {

namespace sc = vr::squad_context;
namespace sp = vr::squad_place;
namespace json = vr::test_request;

namespace {

constexpr int kRight = 1;            // the haptic's hand
constexpr double kChestUnits = 50.0; // a squadmate's chest above their location (as game/SquadOrders)
constexpr int kDrawPoints = 24;
constexpr double kPlausibleUnits = 10.0;

ue3::FunctionRef gTraceFn{"Engine.Actor.Trace"};
// The squadmate's own AI walked to the point (2026-11-05): the HOLD formation's hold position keeps them where they stand and
// QuickCommandMoveTo picks its point from the camera's view, so neither brings them to the ring.
ue3::FieldRef gPawnController{"Engine.Pawn", "Controller"};
ue3::FunctionRef gPushMoveFn{"SFXGame.BioAiController.PushMoveToPoint"};
constexpr float kPointToleranceUnits = 60.0f;

sp::Machine gMachine;
double gUpm = engine::kUnitsPerMeter;
bool gToldMelee = false;
bool gPublished = false;  // the last view sent was active (one inactive view follows the end)

// The console's stand-in for B (no tracked controllers): held with the arc from the head at a pitch.
struct Simulated {
    bool held = false;
    bool releasing = false;  // let go: the next tick still aims from the head, so the release is judged where it was held
    float pitchDeg = -10.0f;
    float yawDeg = 0.0f;  // relative to where the head looks
};
Simulated gSim;

// This tick's arc.
struct Arc {
    bool valid = false;
    sp::Vec3 originM{};
    sp::Vec3 direction{};
    sp::Landing landing = sp::Landing::None;
    float untilS = 0.0f;          // where the flight ends (the landing, or the last segment)
    engine::UnrealVector point{}; // the landing (game units)
    sp::Vec3 normal{0.0f, 0.0f, 1.0f};
    float fromPlayerM = 0.0f;
    int traces = 0;
};
Arc gArc;
int gPointed = -1;

unsigned gStarted = 0, gGiven = 0, gCancelled = 0, gTraces = 0, gTicksTraced = 0;
std::string gLast;

sp::ArcSettings ArcSettingsFrom(hand_calibration::SquadOrders const& s) {
    sp::ArcSettings a;
    a.speedMS = static_cast<float>(s.placeSpeedMS);
    a.rangeM = static_cast<float>(s.placeRangeM);
    return a;
}

engine::UnrealVector Units(sp::Vec3 const& v, double upm) { return {v.x * upm, v.y * upm, v.z * upm}; }
sp::Vec3 Metres(engine::UnrealVector const& v, double upm) {
    return {static_cast<float>(v.x / upm), static_cast<float>(v.y / upm), static_cast<float>(v.z / upm)};
}

// Actor.Trace on the pawn, level geometry only (bTraceActors false: pawns and movable things don't stop the arc).
ue3::Object* TraceOnce(ue3::Object* pawn, ue3::Function* fn, engine::UnrealVector const& start, engine::UnrealVector const& end,
    engine::UnrealVector& hit, sp::Vec3& normal) {
    ue3::Params parms(ue3::runtime::Get(), fn);
    parms.Set("TraceEnd", ue3::Vector{static_cast<float>(end.x), static_cast<float>(end.y), static_cast<float>(end.z)});
    parms.Set("TraceStart", ue3::Vector{static_cast<float>(start.x), static_cast<float>(start.y), static_cast<float>(start.z)});
    parms.SetBool("bTraceActors", false);
    ue3::runtime::Call(pawn, fn, parms);
    ue3::Vector const at = parms.Get<ue3::Vector>("HitLocation").value_or(ue3::Vector{});
    ue3::Vector const n = parms.Get<ue3::Vector>("HitNormal").value_or(ue3::Vector{});
    hit = {at.x, at.y, at.z};
    normal = {n.x, n.y, n.z};
    return parms.Return<ue3::Object*>().value_or(nullptr);
}

// The flight traced segment by segment until the first hit.
Arc TraceArc(ue3::Object* pawn, sp::Vec3 const& originM, sp::Vec3 const& direction, sp::ArcSettings const& settings, double upm) {
    Arc arc;
    arc.valid = true;
    arc.originM = originM;
    arc.direction = direction;
    std::vector<sp::Vec3> const segments = sp::ArcSegments(originM, direction, settings);
    arc.untilS = settings.stepS * static_cast<float>(segments.size() - 1);
    ue3::Function* const fn = gTraceFn.Get();
    if (fn == nullptr || pawn == nullptr) return arc;
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        engine::UnrealVector const a = Units(segments[i], upm);
        engine::UnrealVector const b = Units(segments[i + 1], upm);
        engine::UnrealVector hit{};
        sp::Vec3 normal{};
        ++arc.traces;
        if (TraceOnce(pawn, fn, a, b, hit, normal) == nullptr) continue;
        double const sx = b.x - a.x, sy = b.y - a.y, sz = b.z - a.z;
        double const length = std::sqrt(sx * sx + sy * sy + sz * sz);
        double const along = length > 0.0 ? ((hit.x - a.x) * sx + (hit.y - a.y) * sy + (hit.z - a.z) * sz) / length : 0.0;
        double const fraction = length > 0.0 ? std::clamp(along / length, 0.0, 1.0) : 0.0;
        engine::UnrealVector const onLine{a.x + sx * fraction, a.y + sy * fraction, a.z + sz * fraction};
        double const off = std::sqrt((hit.x - onLine.x) * (hit.x - onLine.x) + (hit.y - onLine.y) * (hit.y - onLine.y) +
                                     (hit.z - onLine.z) * (hit.z - onLine.z));
        bool const zero = hit.x == 0.0 && hit.y == 0.0 && hit.z == 0.0;
        arc.point = (!zero && off < kPlausibleUnits) ? hit : b;  // the hit point didn't come back: the segment's end
        arc.untilS = settings.stepS * (static_cast<float>(i) + static_cast<float>(fraction));
        arc.normal = normal;
        arc.landing = sp::IsFloor(normal, settings) ? sp::Landing::Floor : sp::Landing::Wall;
        break;
    }
    if (arc.landing == sp::Landing::Floor) {
        if (std::optional<ue3::Vector> const me = player::Location(pawn)) {
            double const dx = arc.point.x - me->x, dy = arc.point.y - me->y;
            arc.fromPlayerM = static_cast<float>(std::sqrt(dx * dx + dy * dy) / upm);
        }
    }
    return arc;
}

// The squadmate the straight ray from `originM` along `direction` passes nearest within the cone (-1: none).
int PointedMember(sp::Vec3 const& originM, sp::Vec3 const& direction, double coneDeg, double upm) {
    int best = -1;
    double bestDeg = coneDeg;
    int index = 0;
    for (squad_weapons::MemberView const& m : squad_weapons::Members()) {
        if (std::optional<ue3::Vector> const at = player::Location(m.pawn)) {
            double const tx = at->x / upm - originM.x, ty = at->y / upm - originM.y, tz = (at->z + kChestUnits) / upm - originM.z;
            double const length = std::sqrt(tx * tx + ty * ty + tz * tz);
            if (length > 0.1) {
                double const cosine = (tx * direction.x + ty * direction.y + tz * direction.z) / length;
                double const deg = std::acos(std::clamp(cosine, -1.0, 1.0)) * 180.0 / std::numbers::pi;
                if (deg < bestDeg) {
                    bestDeg = deg;
                    best = index;
                }
            }
        }
        ++index;
    }
    return best;
}

// Each recipient's AI pushed toward the point; the outcome in words.
std::string WalkThere(int member, engine::UnrealVector const& point) {
    ue3::Function* const fn = gPushMoveFn.Get();
    if (fn == nullptr) return "no PushMoveToPoint";
    std::string out;
    int index = 0;
    for (squad_weapons::MemberView const& m : squad_weapons::Members()) {
        if (member < 0 || member == index) {
            ue3::Object* const* const controller = m.pawn != nullptr ? gPawnController.In<ue3::Object*>(m.pawn) : nullptr;
            ue3::Object* const ai = controller != nullptr ? *controller : nullptr;
            bool called = false;
            if (ai != nullptr && ue3::runtime::Get().FindFunction(ue3::runtime::Get().ClassOf(ai), "PushMoveToPoint") == fn) {
                ue3::Params parms(ue3::runtime::Get(), fn);
                parms.Set("vLoc", ue3::Vector{static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)});
                parms.Set("PointTolerance", kPointToleranceUnits);
                parms.SetBool("bWalking", false);
                parms.SetBool("bRequiresPrecisionMovement", false);
                called = ue3::runtime::Call(ai, fn, parms);
            }
            out += std::format("{}{}: {}", out.empty() ? "" : ", ", m.name, ai == nullptr ? "no AI" : called ? "walking" : "not a BioAiController");
        }
        ++index;
    }
    return out;
}

void PublishView(Link const& link, engine::TrackingFrame const* frame, sp::ArcSettings const& settings, bool follow) {
    if (link.publish == nullptr) return;
    sp::View view;
    if (frame != nullptr && gMachine.Holding() && gArc.valid) {
        view.active = 1;
        view.landing = static_cast<std::uint8_t>(gArc.landing);
        view.follow = follow ? 1 : 0;
        view.member = gMachine.Member();
        view.heldS = gMachine.HeldS();
        std::vector<sp::Vec3> const points = sp::ArcSamples(gArc.originM, gArc.direction, gArc.untilS, kDrawPoints, settings);
        int const count = std::min<int>(static_cast<int>(points.size()), sp::kMaxPoints);
        for (int i = 0; i < count; ++i) view.points[i] = engine::ToTracking(*frame, Units(points[static_cast<std::size_t>(i)], gUpm));
        view.count = static_cast<std::uint8_t>(count);
        if (gArc.landing == sp::Landing::None) {
            view.landingPoint = view.points[count - 1];
        } else {
            view.landingPoint = engine::ToTracking(*frame, gArc.point);
            XrVector3f const n = engine::DirectionToTracking(*frame, {gArc.normal.x, gArc.normal.y, gArc.normal.z});
            if (n.x != 0.0f || n.y != 0.0f || n.z != 0.0f) view.landingNormal = n;
        }
        link.publish(&view);
        gPublished = true;
    } else if (gPublished) {
        link.publish(&view);  // active 0: gone at once
        gPublished = false;
    }
}

}  // namespace

void Tick(double deltaSeconds, Link const& link) {
    hand_calibration::SquadOrders const s = hand_calibration::GetSquadOrders();
    sp::ArcSettings const arcSettings = ArcSettingsFrom(s);
    vr::controllers::State const controllers = vr::controllers::Latest();
    ue3::Object* const controller = ue3::runtime::Ready() ? player::Controller() : nullptr;
    std::optional<std::uint8_t> const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
    if (frame && frame->unitsPerMeter > 0.0) gUpm = frame->unitsPerMeter;
    bool const onFoot = mode && *mode == 0 && pawn != nullptr;
    bool const melee = hand_calibration::GetThrowables().meleeButton;
    if (melee && s.enabled && s.place && !gToldMelee) {
        gToldMelee = true;
        log::Info("squad place: off while B is the game's melee button ([Throwables] meleeButton = 1)");
    }
    bool const tracked = controllers.active && controllers.right.aimValid;
    bool const simulated = gSim.held || gSim.releasing;
    gSim.releasing = false;
    bool const button = gSim.held || (tracked && controllers.right.secondary);
    bool const usable = s.enabled && s.place && !melee && onFoot && frame.has_value() && (tracked || simulated) &&
        !squad_weapons::Members().empty() && !squad_weapons::IsOpen() && !power_wheel::IsOpen();

    gArc = {};
    gPointed = -1;
    if (usable && (button || gMachine.Holding())) {
        sp::Vec3 originM{};
        sp::Vec3 direction{};
        if (simulated) {
            // From the head, its yaw plus the console's, at the console's pitch; 0.3 m below the eyes (the hand's height).
            engine::UnrealVector const head = frame->headWorld;
            engine::Basis const look = engine::OrientationToWorld(*frame, frame->head.orientation);
            double const yaw = std::atan2(look.forward.y, look.forward.x) + gSim.yawDeg * std::numbers::pi / 180.0;
            double const pitch = gSim.pitchDeg * std::numbers::pi / 180.0;
            originM = Metres(head, gUpm);
            originM.z -= 0.3f;
            direction = {static_cast<float>(std::cos(pitch) * std::cos(yaw)), static_cast<float>(std::cos(pitch) * std::sin(yaw)),
                static_cast<float>(std::sin(pitch))};
        } else {
            originM = Metres(engine::ToWorld(*frame, controllers.right.aim.position), gUpm);
            engine::Basis const aim = engine::OrientationToWorld(*frame, controllers.right.aim.orientation);
            direction = {static_cast<float>(aim.forward.x), static_cast<float>(aim.forward.y), static_cast<float>(aim.forward.z)};
        }
        gArc = TraceArc(pawn, originM, direction, arcSettings, gUpm);
        gTraces += static_cast<unsigned>(gArc.traces);
        ++gTicksTraced;
        if (!gMachine.Holding()) gPointed = PointedMember(originM, direction, s.memberConeDeg, gUpm);
    }

    sp::Settings settings;
    settings.enabled = s.enabled && s.place;
    settings.minHoldS = static_cast<float>(s.placeMinHoldS);
    settings.followWithinM = static_cast<float>(s.followWithinM);
    sp::Input in;
    in.dt = static_cast<float>(deltaSeconds);
    in.usable = usable;
    in.button = button;
    in.pointedMember = gPointed;
    in.landing = gArc.landing;
    in.landingFromPlayerM = gArc.fromPlayerM;
    sp::Step const step = gMachine.Advance(in, settings);
    std::vector<squad_weapons::MemberView> const members = squad_weapons::Members();
    auto const whoName = [&](int member) {
        return member >= 0 && member < static_cast<int>(members.size()) ? members[static_cast<std::size_t>(member)].name
                                                                          : std::string("the squad");
    };
    if (step.started) {
        ++gStarted;
        if (link.haptic != nullptr) link.haptic(kRight, 0.04f, 0.4f);
        log::Info("squad place: aiming a place for {}", whoName(step.member));
    }
    if (step.landingChanged && link.haptic != nullptr) {
        link.haptic(kRight, 0.02f, gArc.landing == sp::Landing::Floor ? 0.3f : 0.15f);
    }
    if (step.cancelled) {
        ++gCancelled;
        gLast = std::format("called off for {}: {}", whoName(step.member), step.why);
        log::Info("squad place: {}", gLast);
        if (link.haptic != nullptr && std::string_view(step.why) != "a blip") link.haptic(kRight, 0.05f, 0.6f);
    }
    if (step.given) {
        ++gGiven;
        sc::Action action;
        action.who.kind = sc::Who::All;
        if (step.member >= 0 && step.member < static_cast<int>(members.size())) {
            action.who = {sc::Who::Member, squad_context::LetterOf(members[static_cast<std::size_t>(step.member)].classId)};
        }
        std::string outcome;
        if (step.order == sp::Order::Follow) {
            action.order = sc::Order::Follow;
            outcome = squad_orders::Give(action);
        } else {
            // A hold at the point, kept until they're called back: the game's own HOLD formation puts each one exactly there
            // (SetMemberHoldPosition, read by the henchman AI's GetFollowTargets), where QuickCommandMoveTo picks its own point
            // along the camera's view (2026-11-05: a point 5 m off sent Ashley 30 m ahead, whatever Rotation said).
            action.order = sc::Order::Hold;
            outcome = squad_orders::GiveAt(action, gArc.point, true);
            outcome += "; " + WalkThere(step.member, gArc.point);
        }
        gLast = std::format("{} for {}, {:.1f} m from Shepard at ({:.0f}, {:.0f}, {:.0f}): {}", sp::Name(step.order),
            whoName(step.member), gArc.fromPlayerM, gArc.point.x, gArc.point.y, gArc.point.z, outcome);
        log::Info("squad place: {}", gLast);
        if (link.haptic != nullptr) link.haptic(kRight, 0.08f, 0.8f);
    }
    bool const follow = gArc.landing == sp::Landing::Floor && gArc.fromPlayerM <= settings.followWithinM;
    PublishView(link, frame ? &*frame : nullptr, arcSettings, follow);
}

std::string Console(std::string_view args) {
    std::istringstream words{std::string(args)};
    std::string verb;
    words >> verb;
    if (verb == "aim") {
        std::string pitch, yaw;
        words >> pitch >> yaw;
        float value = 0.0f;
        if (!pitch.empty() && std::from_chars(pitch.data(), pitch.data() + pitch.size(), value).ec == std::errc{}) gSim.pitchDeg = value;
        if (!yaw.empty() && std::from_chars(yaw.data(), yaw.data() + yaw.size(), value).ec == std::errc{}) gSim.yawDeg = value;
        gSim.held = true;
        return std::format("B held from the head at pitch {:.0f} deg, yaw {:.0f} deg from the look; `mevr place release` lets go "
                           "(last arc: {}, {:.1f} m from Shepard)", gSim.pitchDeg, gSim.yawDeg, sp::Name(gArc.landing), gArc.fromPlayerM);
    }
    if (verb == "release") {
        gSim.releasing = gSim.held;
        gSim.held = false;
        return std::string("B let go: the order follows on the next tick (see the log's `squad place:` line)");
    }
    if (verb.empty() || verb == "status") return StatusJson();
    return std::string("usage: mevr place [aim <pitchDeg> [yawDeg] | release | status]");
}

std::string StatusJson() {
    return std::format(
        "{{\"holding\": {}, \"member\": {}, \"heldS\": {:.2f}, \"landing\": {}, \"fromPlayerM\": {:.2f}, \"point\": [{:.0f}, {:.0f}, "
        "{:.0f}], \"traces\": {}, \"simulated\": {}, \"started\": {}, \"given\": {}, \"cancelled\": {}, \"last\": {}}}",
        gMachine.Holding() ? "true" : "false", gMachine.Member(), gMachine.HeldS(), json::JsonString(sp::Name(gArc.landing)),
        gArc.fromPlayerM, gArc.point.x, gArc.point.y, gArc.point.z, gArc.traces, gSim.held ? "true" : "false", gStarted, gGiven,
        gCancelled, json::JsonString(gLast));
}

void LogStats() {
    if (gStarted == 0) return;
    log::Info("squad place: {} aimed, {} given, {} called off; {:.1f} traces a tick while aiming", gStarted, gGiven, gCancelled,
        gTicksTraced > 0 ? static_cast<double>(gTraces) / gTicksTraced : 0.0);
}

void Shutdown() {
    gMachine.Reset();
    gSim = {};
    gArc = {};
    gPublished = false;
}

}  // namespace mevr::game::squad_place
