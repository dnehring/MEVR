// SPDX-License-Identifier: MIT
#include "game/SquadOrders.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/SquadGesture.hpp"
#include "core/vr/SquadParser.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "game/SquadContext.hpp"
#include "game/SquadWeapons.hpp"
#include "game/Throwables.hpp"
#include "game/Vent.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace mevr::game::squad_orders {

namespace sc = vr::squad_context;
namespace sg = vr::squad_gesture;
namespace json = vr::test_request;

namespace {

constexpr float kGripOn = 0.6f;
constexpr float kGripOff = 0.4f;
constexpr double kTraceRangeM = 100.0;
constexpr double kChestUnits = 50.0;
constexpr std::size_t kHistory = 3;

ue3::FieldRef gRotation{"Engine.Actor", "Rotation"};
ue3::FunctionRef gMoveToFn{"SFXGame.BioPlayerController.QuickCommandMoveTo"};
ue3::FunctionRef gAttackFn{"SFXGame.BioPlayerController.QuickCommandAttackTarget"};
ue3::FunctionRef gFollowFn{"SFXGame.BioPlayerController.QuickCommandFollowPlayer"};
ue3::FunctionRef gTraceFn{"Engine.Actor.Trace"};

sg::Machine gMachine;
std::vector<sc::Command> gHistory;
bool gOwnGrip = false;
std::uint32_t gLastCount = 0;
bool gHaveCount = false;
bool gToldOldGrip = false;
bool gToldNoFunctions = false;
double gClock = 0.0;
std::string gLast;  // the last order's outcome
unsigned gStarted = 0, gGiven = 0, gCancelled = 0;

// This tick's pointer: the left aim ray and what it ends on.
struct Pointer {
    bool valid = false;
    engine::UnrealVector origin{};
    engine::UnrealVector direction{};
    sg::Hit hit = sg::Hit::None;
    int group = 0;
    float distanceM = 0.0f;
    engine::UnrealVector point{};  // Ground: where
    int member = -1;               // the squadmate the ray points at (the party's order)
    bool traced = false;
};
Pointer gPointer;

engine::UnrealVector Add(engine::UnrealVector const& a, engine::UnrealVector const& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
engine::UnrealVector Sub(engine::UnrealVector const& a, engine::UnrealVector const& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
engine::UnrealVector Times(engine::UnrealVector const& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double Dot(engine::UnrealVector const& a, engine::UnrealVector const& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double Len(engine::UnrealVector const& a) { return std::sqrt(Dot(a, a)); }
sc::Vec3 Metres(engine::UnrealVector const& v, double upm) {
    return {static_cast<float>(v.x / upm), static_cast<float>(v.y / upm), static_cast<float>(v.z / upm)};
}

// Actor.Trace on the player's pawn, through ProcessEvent: the actor hit and, if the out parameter came back, where.
ue3::Object* TraceOnce(ue3::Object* pawn, ue3::Function* fn, engine::UnrealVector const& start, engine::UnrealVector const& end,
    engine::UnrealVector* hit) {
    ue3::Params parms(ue3::runtime::Get(), fn);
    parms.Set("TraceEnd", ue3::Vector{static_cast<float>(end.x), static_cast<float>(end.y), static_cast<float>(end.z)});
    parms.Set("TraceStart", ue3::Vector{static_cast<float>(start.x), static_cast<float>(start.y), static_cast<float>(start.z)});
    parms.SetBool("bTraceActors", true);
    ue3::runtime::Call(pawn, fn, parms);
    if (hit != nullptr) {
        ue3::Vector const at = parms.Get<ue3::Vector>("HitLocation").value_or(ue3::Vector{});
        *hit = {at.x, at.y, at.z};
    }
    return parms.Return<ue3::Object*>().value_or(nullptr);
}

// Where the ray meets the world within kTraceRangeM (bisecting when the hit point doesn't come back, as game/sights does).
std::optional<engine::UnrealVector> TraceRay(ue3::Object* pawn, engine::UnrealVector const& origin, engine::UnrealVector const& direction,
    double upm) {
    ue3::Function* const fn = gTraceFn.Get();
    if (pawn == nullptr || fn == nullptr) return std::nullopt;
    double const range = kTraceRangeM * upm;
    engine::UnrealVector at{};
    ue3::Object* actor = TraceOnce(pawn, fn, origin, Add(origin, Times(direction, range)), &at);
    if (actor == nullptr) return std::nullopt;
    double const along = Dot(Sub(at, origin), direction);
    bool const plausible = (at.x != 0.0 || at.y != 0.0 || at.z != 0.0) && along >= 0.0 && along <= range + 1.0 &&
        Len(Sub(at, Add(origin, Times(direction, along)))) < 10.0;
    if (plausible) return at;
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 10; ++i) {
        double const mid = 0.5 * (lo + hi);
        if (TraceOnce(pawn, fn, origin, Add(origin, Times(direction, range * mid)), nullptr) != nullptr) hi = mid; else lo = mid;
    }
    return Add(origin, Times(direction, range * hi));
}

void ReadPointer(engine::TrackingFrame const& frame, vr::controllers::State const& c, hand_calibration::SquadOrders const& s, bool trace,
    ue3::Object* pawn) {
    gPointer = {};
    if (!c.active || !c.left.aimValid) return;
    gPointer.valid = true;
    gPointer.origin = engine::ToWorld(frame, c.left.aim.position);
    gPointer.direction = engine::OrientationToWorld(frame, c.left.aim.orientation).forward;
    double const upm = frame.unitsPerMeter > 0.0 ? frame.unitsPerMeter : engine::kUnitsPerMeter;
    sc::Ray const ray{Metres(gPointer.origin, upm),
        {static_cast<float>(gPointer.direction.x), static_cast<float>(gPointer.direction.y), static_cast<float>(gPointer.direction.z)}};
    // A squadmate: the nearest to the ray within memberConeDeg.
    float best = static_cast<float>(s.memberConeDeg);
    int index = 0;
    for (squad_weapons::MemberView const& m : squad_weapons::Members()) {
        if (auto const at = player::Location(m.pawn)) {
            float const angle = sc::AngleTo(ray, Metres({at->x, at->y, at->z + kChestUnits}, upm));
            if (angle < best) {
                best = angle;
                gPointer.member = index;
            }
        }
        ++index;
    }
    // An enemy: the nearest to the ray within enemyConeDeg, its group.
    sc::Scene const& scene = squad_context::Scene();
    best = static_cast<float>(s.enemyConeDeg);
    for (sc::Group const& g : squad_context::Groups()) {
        for (sc::PawnId id : g.members) {
            for (sc::Hostile const& h : scene.hostiles) {
                if (h.id != id) continue;
                float const angle = sc::AngleTo(ray, h.position);
                if (angle < best) {
                    best = angle;
                    gPointer.group = g.id;
                }
            }
        }
    }
    if (gPointer.group != 0) {
        gPointer.hit = sg::Hit::Enemy;
        return;
    }
    if (gPointer.member >= 0) {
        gPointer.hit = sg::Hit::Squadmate;
        return;
    }
    if (!trace) return;
    gPointer.traced = true;
    if (auto const point = TraceRay(pawn, gPointer.origin, gPointer.direction, upm)) {
        gPointer.hit = sg::Hit::Ground;
        gPointer.point = *point;
        engine::UnrealVector const me = player::Location(pawn) ? engine::UnrealVector{player::Location(pawn)->x, player::Location(pawn)->y,
                                                                      player::Location(pawn)->z}
                                                                : gPointer.origin;
        gPointer.distanceM = static_cast<float>(Len(Sub(*point, me)) / upm);
    }
}

// The squad indices an order goes to: everyone present, or the one with the letter.
std::vector<std::pair<int, std::string>> Recipients(sc::Resolved const& r) {
    std::vector<std::pair<int, std::string>> out;
    for (squad_weapons::MemberView const& m : squad_weapons::Members()) {
        if (r.all || squad_context::LetterOf(m.classId) == r.who) out.emplace_back(m.squadIndex, m.name);
    }
    return out;
}

ue3::Object* NearestOfGroup(int groupId) {
    sc::Group const* const g = sc::Find(squad_context::Groups(), groupId);
    if (g == nullptr) return nullptr;
    sc::Scene const& scene = squad_context::Scene();
    ue3::Object* best = nullptr;
    float bestDistance = 1e9f;
    for (sc::PawnId id : g->members) {
        for (sc::Hostile const& h : scene.hostiles) {
            if (h.id != id) continue;
            float const d = sc::Distance(scene.player, h.position);
            auto* const object = reinterpret_cast<ue3::Object*>(static_cast<std::uintptr_t>(id));
            if (d < bestDistance && ue3::runtime::IsAlive(object)) {
                bestDistance = d;
                best = object;
            }
        }
    }
    return best;
}

std::string Dispatch(sc::Resolved const& r, sc::Action const& action, std::optional<engine::UnrealVector> point) {
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr || !player::IsBioPlayerController(controller)) return "no player controller";
    ue3::Function* const moveTo = gMoveToFn.Get();
    ue3::Function* const attack = gAttackFn.Get();
    ue3::Function* const follow = gFollowFn.Get();
    if (moveTo == nullptr || attack == nullptr || follow == nullptr) {
        if (!gToldNoFunctions) {
            gToldNoFunctions = true;
            log::Warn("squad orders: the QuickCommand natives weren't found on BioPlayerController: no orders");
        }
        return "the game's order functions weren't found";
    }
    std::vector<std::pair<int, std::string>> const to = Recipients(r);
    if (to.empty()) return "nobody to order";
    std::string names;
    for (auto const& [index, name] : to) names += std::format("{}{} (squad index {})", names.empty() ? "" : ", ", name, index);
    std::string returns;
    auto const call = [&](ue3::Function* fn, int index, ue3::Object* target) {
        ue3::Params parms(ue3::runtime::Get(), fn);
        parms.Set("iSquadMember", static_cast<std::int32_t>(index));
        if (target != nullptr) parms.Set("pTarget", target);
        bool const called = ue3::runtime::Call(controller, fn, parms);
        returns += std::format("{}{}", returns.empty() ? "" : "/", called ? (parms.ReturnBool().value_or(false) ? "true" : "false") : "not called");
    };
    std::string what;
    switch (r.order) {
    case sc::Order::Follow:
    case sc::Order::Regroup:
    case sc::Order::Hold:
        for (auto const& [index, name] : to) call(follow, index, nullptr);
        what = std::format("{}: QuickCommandFollowPlayer for {} -> {}", r.order == sc::Order::Hold ? "hold (as follow, until the hold order is found)" : "follow", names, returns);
        break;
    case sc::Order::Attack: {
        ue3::Object* const target = NearestOfGroup(r.group);
        if (target == nullptr) return std::format("attack: no living member of E{} to target", r.group);
        for (auto const& [index, name] : to) call(attack, index, target);
        what = std::format("attack E{} ({}): QuickCommandAttackTarget for {} -> {}", r.group, ue3::runtime::FullName(target), names, returns);
        break;
    }
    case sc::Order::Move: {
        if (!point && r.group != 0) {
            sc::Group const* const g = sc::Find(squad_context::Groups(), r.group);
            double const upm = engine::kUnitsPerMeter;
            if (g != nullptr) point = engine::UnrealVector{g->centre.x * upm, g->centre.y * upm, g->centre.z * upm};
        }
        if (!point) return "move: no point to move to";
        ue3::Rotator* const rotation = gRotation.In<ue3::Rotator>(controller);
        if (rotation == nullptr) return "move: the controller's Rotation can't be written";
        std::optional<ue3::Vector> const from = player::Location(controller);
        engine::UnrealVector const origin = from ? engine::UnrealVector{from->x, from->y, from->z} : gPointer.origin;
        engine::UnrealRotation const toward = engine::ToRotation(Sub(*point, origin));
        ue3::Rotator const saved = *rotation;
        *rotation = {toward.pitch, toward.yaw, saved.roll};
        write_journal::Wrote("SquadOrders.Rotation (toward the point)", rotation);
        for (auto const& [index, name] : to) call(moveTo, index, nullptr);
        *rotation = saved;
        write_journal::Wrote("SquadOrders.Rotation (restored)", rotation);
        what = std::format("move to ({:.0f}, {:.0f}, {:.0f}) ({:.1f} m, facing yaw {} pitch {} for the call): QuickCommandMoveTo for {} -> {}", point->x,
            point->y, point->z, Len(Sub(*point, origin)) / engine::kUnitsPerMeter, toward.yaw, toward.pitch, names, returns);
        break;
    }
    case sc::Order::Use:
        return std::format("use {}: squad power orders aren't dispatched yet (SquadOrderUsePower is unproven)", r.power);
    case sc::Order::Cancel:
    case sc::Order::Repeat:
    case sc::Order::None:
        return "nothing to dispatch";
    }
    sc::Command record;
    record.who = r.all ? 0 : r.who;
    record.action = action;
    record.group = r.group;
    record.time = gClock;
    gHistory.push_back(record);
    if (gHistory.size() > kHistory) gHistory.erase(gHistory.begin());
    return what;
}

std::string GiveResolved(sc::Action const& action, std::optional<engine::UnrealVector> point) {
    sc::Resolved const r = sc::Validate(action, squad_context::Scene(), squad_context::Groups(), {}, gHistory);
    std::string outcome;
    if (!r.ok) {
        outcome = std::format("refused: {}", r.problem);
    } else if (r.notReady) {
        outcome = std::format("not ready: {} in {:.0f} s", r.power, r.readyInS);
    } else {
        outcome = Dispatch(r, action, point);
    }
    gLast = std::format("{} -> {}", sc::Line(action), outcome);
    log::Info("squad orders: {}", gLast);
    return outcome;
}

}  // namespace

void Tick(double deltaSeconds, Link const& link) {
    gClock += deltaSeconds;
    hand_calibration::SquadOrders const s = hand_calibration::GetSquadOrders();
    vr::controllers::State const controllers = vr::controllers::Latest();
    bool const was = gOwnGrip;
    gOwnGrip = controllers.active && (gOwnGrip ? controllers.left.squeeze > kGripOff : controllers.left.squeeze >= kGripOn);
    bool fresh = false;
    bool squeezed = gOwnGrip;
    if (link.leftGrip) {
        std::uint32_t const count = *link.leftGrip >> 1;
        bool const current = (*link.leftGrip & 1u) != 0;
        fresh = gHaveCount && count != gLastCount && current;
        squeezed = current;
        gLastCount = count;
        gHaveCount = true;
    } else {
        fresh = gOwnGrip && !was;
        if (!gToldOldGrip && s.enabled && s.gesture) {
            gToldOldGrip = true;
            log::Warn("squad orders: the host doesn't report the left grip (services older than v11): the gesture follows our own "
                      "reading of the grip; close the game and redeploy the host");
        }
    }
    bool const unclaimed = fresh && !throwables::ClaimedLeft() && !vent::ClaimedLeft();
    ue3::Object* const controller = ue3::runtime::Ready() ? player::Controller() : nullptr;
    std::optional<std::uint8_t> const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
    bool const onFoot = mode && *mode == 0 && pawn != nullptr;
    bool const usable = s.enabled && s.gesture && onFoot && frame.has_value() && controllers.active && controllers.left.aimValid &&
        !squad_weapons::Members().empty();
    if (frame && onFoot) {
        ReadPointer(*frame, controllers, s, gMachine.holding, pawn);
    } else {
        gPointer = {};
    }
    sg::Settings gs;
    gs.enabled = s.enabled && s.gesture;
    gs.followWithinM = static_cast<float>(s.followWithinM);
    gs.minHoldS = static_cast<float>(s.minHoldS);
    gs.maxHoldS = static_cast<float>(s.maxHoldS);
    gs.enemyConeDeg = static_cast<float>(s.enemyConeDeg);
    gs.memberConeDeg = static_cast<float>(s.memberConeDeg);
    sg::Input in;
    in.dt = deltaSeconds;
    in.usable = usable;
    in.newSqueeze = unclaimed;
    in.squeezed = squeezed;
    in.pointedMember = gPointer.member;
    in.hit = gPointer.hit;
    in.hitGroup = gPointer.group;
    in.hitDistanceM = gPointer.distanceM;
    sg::Step const step = gMachine.Advance(in, gs);
    if (step.started) {
        ++gStarted;
        if (link.haptic != nullptr) link.haptic(0, 0.05f, 0.5f);
        log::Info("squad orders: pointing an order{}", gMachine.member >= 0 ? std::format(" for squadmate {}", gMachine.member) : std::string(" for everyone"));
    }
    if (step.hitChanged && link.haptic != nullptr) link.haptic(0, 0.02f, 0.3f);
    if (step.cancelled) {
        ++gCancelled;
        log::Info("squad orders: called off: {}", step.why);
    }
    if (step.order.given) {
        ++gGiven;
        if (link.haptic != nullptr) link.haptic(0, 0.08f, 0.8f);
        sc::Action action;
        action.who.kind = sc::Who::All;
        if (step.order.member >= 0) {
            std::vector<squad_weapons::MemberView> const members = squad_weapons::Members();
            if (step.order.member < static_cast<int>(members.size())) {
                action.who = {sc::Who::Member, squad_context::LetterOf(members[static_cast<std::size_t>(step.order.member)].classId)};
            }
        }
        action.order = step.order.order;
        if (step.order.order == sc::Order::Attack) action.target = {sc::Target::Group, step.order.group};
        std::optional<engine::UnrealVector> point;
        if (step.order.toPoint && gPointer.hit == sg::Hit::Ground) point = gPointer.point;
        log::Info("squad orders: let go on {}{}", sg::Name(gPointer.hit),
            gPointer.hit == sg::Hit::Ground ? std::format(" {:.1f} m away", gPointer.distanceM)
            : gPointer.hit == sg::Hit::Enemy ? std::format(" (E{})", gPointer.group) : std::string());
        GiveResolved(action, point);
    }
}

std::string Give(sc::Action const& action) {
    return GiveResolved(action, std::nullopt);
}

std::string Say(std::string_view transcript, double secondsAgo) {
    namespace sp = vr::squad_parser;
    hand_calibration::SquadOrders const s = hand_calibration::GetSquadOrders();
    sc::Scene const& scene = squad_context::Scene();
    std::vector<sc::Group> const& groups = squad_context::Groups();
    sp::Settings ps;
    sp::Parsed const parsed = sp::Parse(transcript, scene, groups, squad_context::Roster(), ps);
    std::string lines;
    for (sp::Clause const& c : parsed.clauses) {
        lines += std::format("{}{} ({:.2f}{}{})", lines.empty() ? "" : "; ", sc::Line(c.action), c.confidence,
            c.unresolved.empty() ? "" : ", unresolved: ", c.unresolved);
    }
    log::Info("squad orders: say \"{}\" -> {}{}{}{}", transcript, parsed.empty ? "nothing that looks like an order" : lines,
        parsed.correction ? "; a correction" : "", parsed.cancel ? "; a cancel" : "", parsed.notes.empty() ? "" : "; " + parsed.notes);
    if (parsed.empty) {
        return "nothing that looks like an order";
    }
    if (parsed.confidence < s.minConfidence) {
        std::string why;
        for (sp::Clause const& c : parsed.clauses) {
            if (!c.unresolved.empty()) why += (why.empty() ? "" : "; ") + c.unresolved;
        }
        return std::format("not sure ({:.2f} < {:.2f}): {}; parsed {}", parsed.confidence, s.minConfidence, why.empty() ? "-" : why, lines);
    }
    // The deictics, from where the head and the hands pointed `secondsAgo`; a place for a move from the left hand's ray now.
    std::vector<sc::Deictic> deictics;
    std::optional<engine::UnrealVector> place;
    for (sp::DeicticWord const& d : parsed.deictics) {
        deictics.push_back({d.index, d.word, squad_context::ResolveAgo(secondsAgo)});
        if (d.place && !place && gPointer.valid) {
            double const upm = engine::kUnitsPerMeter;
            if (auto const frame = view_anchor::Current()) {
                place = TraceRay(player::BioPawn(player::Controller()), gPointer.origin, gPointer.direction,
                    frame->unitsPerMeter > 0.0 ? frame->unitsPerMeter : upm);
            }
        }
    }
    std::string outcomes;
    for (sp::Clause const& c : parsed.clauses) {
        std::string outcome;
        if (c.action.order == sc::Order::Cancel) {
            outcome = "cancelled: nothing undone yet (the game has no cancel; a follow order would call them back)";
        } else {
            sc::Resolved const r = sc::Validate(c.action, scene, groups, deictics, gHistory);
            std::optional<engine::UnrealVector> point;
            if (r.ok && r.order == sc::Order::Move && c.action.target.kind == sc::Target::Pointed) {
                point = place;
            }
            outcome = !r.ok ? std::format("refused: {}", r.problem)
                : r.notReady ? std::format("not ready: {} in {:.0f} s", r.power, r.readyInS)
                             : Dispatch(r, c.action, point);
            gLast = std::format("{} -> {}", sc::Line(c.action), outcome);
            log::Info("squad orders: {}", gLast);
        }
        outcomes += std::format("{}{}: {}", outcomes.empty() ? "" : "\n", sc::Line(c.action), outcome);
    }
    return outcomes;
}

std::string StatusJson() {
    return std::format("{{\"holding\": {}, \"heldS\": {}, \"member\": {}, \"pointer\": {{\"valid\": {}, \"hit\": {}, \"group\": {}, \"distanceM\": {}, "
                       "\"member\": {}, \"traced\": {}}}, \"last\": {}, \"started\": {}, \"given\": {}, \"cancelled\": {}, \"history\": {}}}",
        gMachine.holding ? "true" : "false", json::JsonNumber(gMachine.heldS), gMachine.member, gPointer.valid ? "true" : "false",
        json::JsonString(sg::Name(gPointer.hit)), gPointer.group, json::JsonNumber(gPointer.distanceM), gPointer.member,
        gPointer.traced ? "true" : "false", json::JsonString(gLast), gStarted, gGiven, gCancelled, gHistory.size());
}

void LogStats() {
    log::Info("squad orders: {} pointed, {} given, {} called off; {}{}", gStarted, gGiven, gCancelled,
        gMachine.holding ? std::format("holding ({:.1f} s, on {})", gMachine.heldS, sg::Name(gPointer.hit)) : std::string("idle"),
        gLast.empty() ? std::string() : std::format("; last: {}", gLast));
}

void Shutdown() {
    gMachine.Reset();
    gPointer = {};
    gHistory.clear();
}

}  // namespace mevr::game::squad_orders
