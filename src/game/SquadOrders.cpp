// SPDX-License-Identifier: MIT
#include "game/SquadOrders.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/SquadGesture.hpp"
#include "core/vr/SquadParser.hpp"
#include "core/vr/SquadStanding.hpp"
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
namespace sp = vr::squad_parser;
namespace ss = vr::squad_standing;
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
// The game's own hold (research/54 "A real hold"): the member's hold position and its formation on the player's squad.
ue3::FieldRef gBehavior{"SFXGame.BioPawn", "m_oBehavior"};
ue3::FieldRef gSquadOf{"SFXGame.BioEpicPawnBehavior", "Squad"};
ue3::FunctionRef gSetFormationFn{"SFXGame.BioBaseSquad.SetSquadFollowFormation"};
ue3::FunctionRef gSetHoldPositionFn{"SFXGame.BioBaseSquad.SetMemberHoldPosition"};
ue3::FunctionRef gGetFormationFn{"SFXGame.BioBaseSquad.GetMemberFormation"};
// The level's cover for a retreat (research/54 "Retreat to cover"): CoverLink slots, each at the link's place plus its offset
// in the link's axes.
ue3::ClassRef gCoverLink{"Engine.CoverLink"};
ue3::FieldRef gCoverSlots{"Engine.CoverLink", "Slots"};
ue3::FieldRef gCoverSlotOffset{gCoverSlots, "LocationOffset"};
ue3::FieldRef gCoverDisabled{"Engine.CoverLink", "bDisabled"};
constexpr std::uint8_t kFormationFollow = 1;  // BioBaseSquad.EFollowFormation
constexpr std::uint8_t kFormationHold = 2;

sg::Machine gMachine;
std::vector<sc::Command> gHistory;
std::vector<ss::Standing> gStandings;  // at most one per squadmate
bool gOwnGrip = false;
std::uint32_t gLastCount = 0;
bool gHaveCount = false;
bool gToldOldGrip = false;
bool gToldNoFunctions = false;
double gClock = 0.0;
double gUpm = engine::kUnitsPerMeter;
std::string gLast;  // the last order's outcome
unsigned gStarted = 0, gGiven = 0, gCancelled = 0, gReasserted = 0, gEnded = 0;

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
engine::UnrealVector Units(sc::Vec3 const& v, double upm) { return {v.x * upm, v.y * upm, v.z * upm}; }

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
        std::optional<ue3::Vector> const me = player::Location(pawn);
        engine::UnrealVector const from = me ? engine::UnrealVector{me->x, me->y, me->z} : gPointer.origin;
        gPointer.distanceM = static_cast<float>(Len(Sub(*point, from)) / upm);
    }
}

// ---- The game's calls -------------------------------------------------------------------------------------------------

struct Recipient {
    int index = -1;  // the squad's own index
    char letter = 0;
    std::string name;
    ue3::Object* pawn = nullptr;
};

// Everyone present, or the one with the letter.
std::vector<Recipient> Recipients(bool all, char who) {
    std::vector<Recipient> out;
    for (squad_weapons::MemberView const& m : squad_weapons::Members()) {
        char const letter = squad_context::LetterOf(m.classId);
        if (all || letter == who) out.push_back({m.squadIndex, letter, m.name, m.pawn});
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

bool Functions(ue3::Object*& controller) {
    controller = player::Controller();
    if (controller == nullptr || !player::IsBioPlayerController(controller)) return false;
    if (gMoveToFn.Get() == nullptr || gAttackFn.Get() == nullptr || gFollowFn.Get() == nullptr) {
        if (!gToldNoFunctions) {
            gToldNoFunctions = true;
            log::Warn("squad orders: the QuickCommand natives weren't found on BioPlayerController: no orders");
        }
        return false;
    }
    return true;
}

std::string CallResult(ue3::Params const& parms, bool called) {
    return called ? (parms.ReturnBool().value_or(false) ? "true" : "false") : "not called";
}

std::string CallFollow(ue3::Object* controller, int index) {
    ue3::Params parms(ue3::runtime::Get(), gFollowFn.Get());
    parms.Set("iSquadMember", static_cast<std::int32_t>(index));
    return CallResult(parms, ue3::runtime::Call(controller, gFollowFn.Get(), parms));
}

std::string CallAttack(ue3::Object* controller, int index, ue3::Object* target) {
    ue3::Params parms(ue3::runtime::Get(), gAttackFn.Get());
    parms.Set("iSquadMember", static_cast<std::int32_t>(index));
    parms.Set("pTarget", target);
    return CallResult(parms, ue3::runtime::Call(controller, gAttackFn.Get(), parms));
}

// The move: the controller's Rotation turned toward the point for the call (the native drops the point along the controller's
// facing: research/54's trace), and put back.
std::string CallMove(ue3::Object* controller, int index, engine::UnrealVector const& point) {
    ue3::Rotator* const rotation = gRotation.In<ue3::Rotator>(controller);
    if (rotation == nullptr) return "no Rotation to turn";
    std::optional<ue3::Vector> const from = player::Location(controller);
    engine::UnrealVector const origin = from ? engine::UnrealVector{from->x, from->y, from->z} : gPointer.origin;
    engine::UnrealRotation const toward = engine::ToRotation(Sub(point, origin));
    ue3::Rotator const saved = *rotation;
    *rotation = {toward.pitch, toward.yaw, saved.roll};
    write_journal::Wrote("SquadOrders.Rotation (toward the point)", rotation);
    ue3::Params parms(ue3::runtime::Get(), gMoveToFn.Get());
    parms.Set("iSquadMember", static_cast<std::int32_t>(index));
    bool const called = ue3::runtime::Call(controller, gMoveToFn.Get(), parms);
    *rotation = saved;
    write_journal::Wrote("SquadOrders.Rotation (restored)", rotation);
    return CallResult(parms, called);
}

// The player's BioBaseSquad (through Shepard's behaviour), or null.
ue3::Object* Squad() {
    ue3::Object* const pawn = player::BioPawn(player::Controller());
    if (pawn == nullptr) return nullptr;
    auto const behavior = gBehavior.Read<ue3::Object*>(pawn);
    if (!behavior || *behavior == nullptr) return nullptr;
    auto const squad = gSquadOf.Read<ue3::Object*>(*behavior);
    return squad ? *squad : nullptr;
}

// The game's own hold: the member's hold position, then the HOLD formation for that member (the route the Follow native
// takes for FOLLOW, which fires the squad's acknowledgement). The formation read back says whether the game took it.
std::string CallHold(int index, engine::UnrealVector const& point) {
    ue3::Object* const squad = Squad();
    if (squad == nullptr) return "no squad";
    ue3::Function* const setPosition = gSetHoldPositionFn.Get();
    ue3::Function* const setFormation = gSetFormationFn.Get();
    ue3::Function* const getFormation = gGetFormationFn.Get();
    if (setPosition == nullptr || setFormation == nullptr) return "the squad's hold functions weren't found";
    ue3::Params position(ue3::runtime::Get(), setPosition);
    position.Set("nIndex", static_cast<std::int32_t>(index));
    position.Set("vPosition", ue3::Vector{static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)});
    bool const placed = ue3::runtime::Call(squad, setPosition, position);
    ue3::Params formation(ue3::runtime::Get(), setFormation);
    formation.Set("eFormation", kFormationHold);
    formation.SetBool("bForceFormation", true);
    formation.Set("iSquadMember", static_cast<std::int32_t>(index));
    bool const formed = ue3::runtime::Call(squad, setFormation, formation);
    std::string now = "?";
    if (getFormation != nullptr) {
        ue3::Params read(ue3::runtime::Get(), getFormation);
        read.Set("nIndex", static_cast<std::int32_t>(index));
        if (ue3::runtime::Call(squad, getFormation, read)) {
            std::uint8_t const f = read.Return<std::uint8_t>().value_or(0);
            now = f == kFormationHold ? "HOLD" : f == kFormationFollow ? "FOLLOW" : std::format("{}", f);
        }
    }
    return std::format("hold position {}, formation {} (now {})", placed ? "set" : "not set", formed ? "set" : "not set", now);
}

// The cover slots within radiusM of a point (metres): a walk over the live objects, so only when a retreat is possible.
std::vector<sc::Vec3> CoverNear(sc::Vec3 const& at, float radiusM) {
    std::vector<sc::Vec3> out;
    ue3::Reflection& r = ue3::runtime::Get();
    std::int32_t const n = r.ObjectCount();
    for (std::int32_t i = 0; i < n && out.size() < 64; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !ue3::IsA(o, gCoverLink) || gCoverDisabled.ReadBool(o).value_or(false)) continue;
        std::optional<ue3::Vector> const loc = player::Location(o);
        std::optional<ue3::Rotator> const rot = player::Rotation(o);
        if (!loc || !rot) continue;
        sc::Vec3 const link = Metres({loc->x, loc->y, loc->z}, gUpm);
        if (sc::Distance(link, at) > radiusM + 10.0f) continue;
        engine::Basis const axes = engine::ToBasis(engine::UnrealRotation{rot->pitch, rot->yaw, rot->roll});
        auto const slots = gCoverSlots.ElementsOf(o);
        if (!slots) continue;
        for (std::int32_t k = 0; k < slots->count && k < 32; ++k) {
            ue3::Vector const offset = gCoverSlotOffset.Read<ue3::Vector>(slots->At(k)).value_or(ue3::Vector{});
            engine::UnrealVector const world = Add({loc->x, loc->y, loc->z}, engine::Apply(axes, {offset.x, offset.y, offset.z}));
            sc::Vec3 const point = Metres(world, gUpm);
            if (sc::Distance(point, at) <= radiusM) out.push_back(point);
        }
    }
    return out;
}

// ---- Standing orders --------------------------------------------------------------------------------------------------

ss::Settings StandingSettings(hand_calibration::SquadOrders const& s) {
    ss::Settings out;
    out.reassertS = static_cast<float>(s.reassertS);
    out.holdRadiusM = static_cast<float>(s.holdRadiusM);
    out.calmS = static_cast<float>(s.calmS);
    out.retreatHealth = static_cast<float>(s.retreatHealth);
    out.overwhelmCount = s.overwhelmCount;
    out.retreatToCover = s.retreatToCover;
    out.retreatCooldownS = static_cast<float>(s.retreatCooldownS);
    out.retreat.retreatM = static_cast<float>(s.retreatM);
    out.retreat.coverSearchM = static_cast<float>(s.coverSearchM);
    return out;
}

void DropStanding(char who, char const* why) {
    for (auto it = gStandings.begin(); it != gStandings.end();) {
        if (it->intent.who == who) {
            log::Info("squad orders: {}'s standing {} order ends: {}", who, ss::Name(it->intent.kind), why);
            it = gStandings.erase(it);
        } else {
            ++it;
        }
    }
}

void KeepStanding(ss::Intent const& intent) {
    DropStanding(intent.who, "replaced");
    ss::Standing standing;
    standing.intent = intent;
    standing.sinceIssueS = 0.0;  // the order was just given
    gStandings.push_back(standing);
    log::Info("squad orders: {} keeps a standing {} order {}{}{}", intent.who, ss::Name(intent.kind), ss::Name(intent.until),
        intent.returnAfter ? ", then comes back" : ", and stays", intent.mayRetreat ? "" : ", no matter what");
}

std::string IssueText(ss::Issue const& issue) {
    switch (issue.kind) {
    case ss::Issue::MoveTo: return "move";
    case ss::Issue::Follow: return "follow";
    case ss::Issue::Attack: return std::format("attack E{}", issue.group);
    case ss::Issue::Retreat: return std::format("retreat to {} ({:.1f}, {:.1f})", issue.cover ? "cover" : "open ground", issue.point.x, issue.point.y);
    case ss::Issue::None: break;
    }
    return "nothing";
}

void TickStandings(double dt, hand_calibration::SquadOrders const& s) {
    if (gStandings.empty()) return;
    if (!s.standing) {
        gStandings.clear();
        return;
    }
    ue3::Object* controller = nullptr;
    bool const callable = Functions(controller);
    sc::Scene const& scene = squad_context::Scene();
    std::vector<sc::Group> const& groups = squad_context::Groups();
    ss::Settings const settings = StandingSettings(s);
    std::vector<Recipient> const members = Recipients(true, 0);
    for (ss::Standing& st : gStandings) {
        Recipient const* who = nullptr;
        for (Recipient const& r : members) {
            if (r.letter == st.intent.who) who = &r;
        }
        ss::Observation seen;
        seen.dt = dt;
        if (who == nullptr) {
            seen.alive = false;
        } else {
            std::optional<ue3::Vector> const at = player::Location(who->pawn);
            seen.position = at ? Metres({at->x, at->y, at->z}, gUpm) : sc::Vec3{};
            for (squad_weapons::MemberView const& m : squad_weapons::Members()) {
                if (m.pawn == who->pawn) {
                    seen.alive = m.health != 0.0f;
                    seen.health = m.health < 0.0f ? 1.0f : std::clamp(m.health > 1.0f ? m.health / 100.0f : m.health, 0.0f, 1.0f);
                }
            }
        }
        for (sc::Hostile const& h : scene.hostiles) {
            if (h.health <= 0.0f) continue;
            if (sc::Distance(h.position, st.intent.point) <= settings.calmRadiusM) ++seen.hostilesNearPoint;
            if (who != nullptr && sc::Distance(h.position, seen.position) <= settings.overwhelmRadiusM) ++seen.hostilesNearMember;
        }
        seen.groupAlive = sc::Find(groups, st.intent.group) != nullptr;
        seen.combat = scene.combat;
        seen.shepard = scene.player;
        if (who != nullptr) {
            for (sc::Hostile const& h : scene.hostiles) {
                if (h.health > 0.0f && sc::Distance(h.position, seen.position) <= settings.calmRadiusM) seen.threats.push_back(h.position);
            }
            bool const mayRetreatNow = st.intent.mayRetreat && settings.retreatToCover && st.sinceRetreatS >= settings.retreatCooldownS &&
                (seen.health < settings.retreatHealth || seen.hostilesNearMember >= settings.overwhelmCount);
            if (mayRetreatNow) seen.cover = CoverNear(seen.position, settings.retreat.retreatM + settings.retreat.coverSearchM + 2.0f);
        }
        ss::Issue const issue = st.Advance(seen, settings);
        if (issue.kind == ss::Issue::None && !st.Done()) continue;
        std::string result = "no squadmate";
        if (who != nullptr && callable && issue.kind != ss::Issue::None) {
            switch (issue.kind) {
            case ss::Issue::MoveTo: result = CallMove(controller, who->index, Units(issue.point, gUpm)); break;
            case ss::Issue::Follow: result = CallFollow(controller, who->index); break;
            case ss::Issue::Retreat: {
                engine::UnrealVector const where = Units(issue.point, gUpm);
                result = CallMove(controller, who->index, where);
                if (s.holdFormation) result += "; " + CallHold(who->index, where);
                break;
            }
            case ss::Issue::Attack: {
                ue3::Object* const target = NearestOfGroup(issue.group);
                result = target != nullptr ? CallAttack(controller, who->index, target) : "no living member of the group";
                break;
            }
            case ss::Issue::None: break;
            }
        }
        if (st.Done()) {
            ++gEnded;
            log::Info("squad orders: {}'s standing {} order ends ({}){}", st.intent.who, ss::Name(st.intent.kind), issue.why,
                issue.kind != ss::Issue::None ? std::format(": {} -> {}", IssueText(issue), result) : std::string());
        } else {
            ++gReasserted;
            log::Info("squad orders: {}'s standing {} order: {} ({}) -> {}", st.intent.who, ss::Name(st.intent.kind), IssueText(issue), issue.why, result);
        }
    }
    gStandings.erase(std::remove_if(gStandings.begin(), gStandings.end(), [](ss::Standing const& st) { return st.Done(); }), gStandings.end());
}

// ---- Dispatch -------------------------------------------------------------------------------------------------------------

std::string Dispatch(sc::Resolved const& r, sc::Action const& action, std::optional<engine::UnrealVector> point, sp::StandingHint const& hint) {
    ue3::Object* controller = nullptr;
    if (!Functions(controller)) return controller == nullptr ? "no player controller" : "the game's order functions weren't found";
    hand_calibration::SquadOrders const s = hand_calibration::GetSquadOrders();
    std::vector<Recipient> const to = Recipients(r.all, r.who);
    if (to.empty()) return "nobody to order";
    std::string names;
    for (Recipient const& rc : to) names += std::format("{}{} (squad index {})", names.empty() ? "" : ", ", rc.name, rc.index);
    std::string returns;
    auto const note = [&](std::string const& result) { returns += std::format("{}{}", returns.empty() ? "" : "/", result); };
    auto const until = [&](ss::Until byDefault) {
        switch (hint.until) {
        case sp::StandingHint::Until::Calm: return ss::Until::Calm;
        case sp::StandingHint::Until::GroupGone: return ss::Until::GroupGone;
        case sp::StandingHint::Until::Relieved: return ss::Until::Relieved;
        case sp::StandingHint::Until::Default: break;
        }
        return byDefault;
    };
    // "Go left", "move up", "cover the right": a point that way from the player's feet (an attack that way was resolved to a group).
    if (!point && action.target.kind == sc::Target::Direction && (r.order == sc::Order::Move || r.order == sc::Order::Hold)) {
        constexpr float kDirectionM = 8.0f;
        point = Units(sc::DirectionPoint(squad_context::Scene(), static_cast<sc::Sector>(action.target.index), kDirectionM), gUpm);
    }
    std::string what;
    switch (r.order) {
    case sc::Order::Follow:
    case sc::Order::Regroup:
        for (Recipient const& rc : to) {
            DropStanding(rc.letter, "called back");
            note(CallFollow(controller, rc.index));
        }
        what = std::format("follow: QuickCommandFollowPlayer for {} -> {}", names, returns);
        break;
    case sc::Order::Hold: {
        // Hold a place: move there and keep it; hold here: keep where they stand (the standing brings them back when they drift).
        if (!point && r.group != 0) {
            if (sc::Group const* const g = sc::Find(squad_context::Groups(), r.group)) point = Units(g->centre, gUpm);
        }
        for (Recipient const& rc : to) {
            engine::UnrealVector where = point.value_or(engine::UnrealVector{});
            std::optional<ue3::Vector> const at = player::Location(rc.pawn);
            if (!point) {
                if (!at) {
                    note("no position");
                    continue;
                }
                where = {at->x, at->y, at->z};
            }
            bool const distant = point && at && Len(Sub(where, {at->x, at->y, at->z})) / gUpm > s.holdRadiusM;  // not `far`: a Windows.h macro
            if (distant) note(CallMove(controller, rc.index, where));
            if (s.holdFormation) {
                note(CallHold(rc.index, where));
            } else if (!distant) {
                note("holds here");
            }
            if (s.standing) {
                ss::Intent intent;
                intent.who = rc.letter;
                intent.kind = ss::Kind::HoldAt;
                intent.point = Metres(where, gUpm);
                intent.until = until(ss::Until::Calm);
                intent.returnAfter = !hint.stay;
                intent.mayRetreat = !hint.noRetreat;
                KeepStanding(intent);
            }
        }
        what = std::format("hold {}: {} for {} -> {}", point ? std::format("at ({:.0f}, {:.0f}, {:.0f})", point->x, point->y, point->z) : std::string("here"),
            s.holdFormation ? "the game's HOLD formation" : "a move, re-asserted", names, returns);
        break;
    }
    case sc::Order::Attack: {
        ue3::Object* const target = NearestOfGroup(r.group);
        if (target == nullptr) return std::format("attack: no living member of E{} to target", r.group);
        for (Recipient const& rc : to) {
            note(CallAttack(controller, rc.index, target));
            if (s.standing) {
                ss::Intent intent;
                intent.who = rc.letter;
                intent.kind = ss::Kind::AttackGroup;
                intent.group = r.group;
                intent.until = until(ss::Until::GroupGone);
                intent.returnAfter = hint.comeBack;
                intent.mayRetreat = !hint.noRetreat;
                KeepStanding(intent);
            }
        }
        what = std::format("attack E{} ({}): QuickCommandAttackTarget for {} -> {}", r.group, ue3::runtime::FullName(target), names, returns);
        break;
    }
    case sc::Order::Move: {
        if (!point && r.group != 0) {
            if (sc::Group const* const g = sc::Find(squad_context::Groups(), r.group)) point = Units(g->centre, gUpm);
        }
        if (!point) return "move: no point to move to";
        for (Recipient const& rc : to) {
            note(CallMove(controller, rc.index, *point));
            // "Move there and stay" / "until ...": a move becomes a hold there.
            bool const keep = s.standing && (hint.stay || hint.until != sp::StandingHint::Until::Default || hint.noRetreat);
            if (keep) {
                ss::Intent intent;
                intent.who = rc.letter;
                intent.kind = ss::Kind::HoldAt;
                intent.point = Metres(*point, gUpm);
                intent.until = until(ss::Until::Calm);
                intent.returnAfter = !hint.stay;
                intent.mayRetreat = !hint.noRetreat;
                KeepStanding(intent);
            } else {
                DropStanding(rc.letter, "moved elsewhere");
            }
        }
        what = std::format("move to ({:.0f}, {:.0f}, {:.0f}): QuickCommandMoveTo for {} -> {}", point->x, point->y, point->z, names, returns);
        break;
    }
    case sc::Order::Use:
        return std::format("use {}: squad power orders aren't dispatched yet (SquadOrderUsePower is unproven)", r.power);
    case sc::Order::None:
        if (r.weapon < 0) return "nothing to dispatch";
        for (Recipient const& rc : to) note(squad_weapons::GiveWeapon(rc.pawn, r.weapon));
        what = std::format("equip {}: the squad menu's switch for {} -> {}", sc::WeaponCode(r.weapon), names, returns);
        break;
    case sc::Order::Cancel:
    case sc::Order::Repeat:
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

std::string GiveResolved(sc::Action const& action, std::optional<engine::UnrealVector> point, sp::StandingHint const& hint) {
    sc::Resolved const r = sc::Validate(action, squad_context::Scene(), squad_context::Groups(), {}, gHistory);
    std::string outcome;
    if (!r.ok) {
        outcome = std::format("refused: {}", r.problem);
    } else if (r.notReady) {
        outcome = std::format("not ready: {} in {:.0f} s", r.power, r.readyInS);
    } else {
        outcome = Dispatch(r, action, point, hint);
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
    if (frame && frame->unitsPerMeter > 0.0) gUpm = frame->unitsPerMeter;
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
        GiveResolved(action, point, {});
    }
    if (s.enabled && onFoot) {
        TickStandings(deltaSeconds, s);
    }
}

std::string Give(sc::Action const& action) {
    return GiveResolved(action, std::nullopt, {});
}

std::string Say(std::string_view transcript, double secondsAgo) {
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
    // The deictics, from where the head and the hands pointed `secondsAgo`; a place for a move or a hold from the left hand's ray now.
    std::vector<sc::Deictic> deictics;
    std::optional<engine::UnrealVector> place;
    for (sp::DeicticWord const& d : parsed.deictics) {
        deictics.push_back({d.index, d.word, squad_context::ResolveAgo(secondsAgo)});
        if (d.place && !place && gPointer.valid) {
            place = TraceRay(player::BioPawn(player::Controller()), gPointer.origin, gPointer.direction, gUpm);
        }
    }
    std::string outcomes;
    for (sp::Clause const& c : parsed.clauses) {
        std::string outcome;
        if (c.action.order == sc::Order::Cancel) {
            for (Recipient const& rc : Recipients(true, 0)) DropStanding(rc.letter, "belayed");
            outcome = "belayed: the standing orders are dropped (the game's own order can't be undone; a follow calls them back)";
        } else {
            sc::Action action = c.action;
            std::optional<engine::UnrealVector> point;
            std::string supporting;
            if (c.supportOf != 0) {
                // Support X: what X is attacking (her standing, else her last order), else where X is.
                for (ss::Standing const& st : gStandings) {
                    if (st.intent.who != c.supportOf) continue;
                    if (st.intent.kind == ss::Kind::AttackGroup && sc::Find(groups, st.intent.group) != nullptr) {
                        action.target = {sc::Target::Group, st.intent.group};
                        supporting = std::format("{}'s target E{}", c.supportOf, st.intent.group);
                    } else {
                        action.order = sc::Order::Move;
                        action.target = {};
                        point = Units(st.intent.point, gUpm);
                        supporting = std::format("{}'s held point", c.supportOf);
                    }
                }
                if (supporting.empty()) {
                    for (auto it = gHistory.rbegin(); it != gHistory.rend() && supporting.empty(); ++it) {
                        if (it->who == c.supportOf && it->group != 0 && sc::Find(groups, it->group) != nullptr) {
                            action.target = {sc::Target::Group, it->group};
                            supporting = std::format("{}'s last target E{}", c.supportOf, it->group);
                        }
                    }
                }
                if (supporting.empty()) {
                    for (Recipient const& rc : Recipients(true, 0)) {
                        if (rc.letter != c.supportOf) continue;
                        if (std::optional<ue3::Vector> const at = player::Location(rc.pawn)) {
                            action.order = sc::Order::Move;
                            action.target = {};
                            point = engine::UnrealVector{at->x, at->y, at->z};
                            supporting = std::format("{}'s side", c.supportOf);
                        }
                    }
                }
                if (supporting.empty()) supporting = std::format("{} isn't here: the gun's target", c.supportOf);
            }
            sc::Resolved const r = sc::Validate(action, scene, groups, deictics, gHistory);
            if (r.ok && (r.order == sc::Order::Move || r.order == sc::Order::Hold) && action.target.kind == sc::Target::Pointed) {
                point = place;
            }
            outcome = !r.ok ? std::format("refused: {}", r.problem)
                : r.notReady ? std::format("not ready: {} in {:.0f} s", r.power, r.readyInS)
                             : Dispatch(r, action, point, c.standing);
            if (!supporting.empty()) outcome = std::format("supporting {}: {}", supporting, outcome);
            gLast = std::format("{} -> {}", sc::Line(action), outcome);
            log::Info("squad orders: {}", gLast);
        }
        outcomes += std::format("{}{}: {}", outcomes.empty() ? "" : "\n", sc::Line(c.action), outcome);
    }
    return outcomes;
}

std::string StatusJson() {
    std::string standings;
    for (ss::Standing const& st : gStandings) {
        standings += std::format("{}{{\"who\": \"{}\", \"kind\": \"{}\", \"until\": \"{}\", \"phase\": \"{}\", \"ageS\": {}, \"group\": {}, \"returnAfter\": {}, "
                                 "\"mayRetreat\": {}}}",
            standings.empty() ? "" : ", ", st.intent.who, ss::Name(st.intent.kind), ss::Name(st.intent.until), ss::Name(st.phase),
            json::JsonNumber(st.ageS), st.intent.group, st.intent.returnAfter ? "true" : "false", st.intent.mayRetreat ? "true" : "false");
    }
    return std::format("{{\"holding\": {}, \"heldS\": {}, \"member\": {}, \"pointer\": {{\"valid\": {}, \"hit\": {}, \"group\": {}, \"distanceM\": {}, "
                       "\"member\": {}, \"traced\": {}}}, \"last\": {}, \"started\": {}, \"given\": {}, \"cancelled\": {}, \"history\": {}, "
                       "\"standings\": [{}], \"reasserted\": {}, \"ended\": {}}}",
        gMachine.holding ? "true" : "false", json::JsonNumber(gMachine.heldS), gMachine.member, gPointer.valid ? "true" : "false",
        json::JsonString(sg::Name(gPointer.hit)), gPointer.group, json::JsonNumber(gPointer.distanceM), gPointer.member,
        gPointer.traced ? "true" : "false", json::JsonString(gLast), gStarted, gGiven, gCancelled, gHistory.size(), standings, gReasserted, gEnded);
}

void LogStats() {
    std::string standings;
    for (ss::Standing const& st : gStandings) {
        standings += std::format("{}{} {} {} ({:.0f} s)", standings.empty() ? "" : ", ", st.intent.who, ss::Name(st.intent.kind), ss::Name(st.intent.until), st.ageS);
    }
    log::Info("squad orders: {} pointed, {} given, {} called off; {} standing re-orders, {} ended; {}{}{}", gStarted, gGiven, gCancelled, gReasserted, gEnded,
        gMachine.holding ? std::format("holding ({:.1f} s, on {})", gMachine.heldS, sg::Name(gPointer.hit)) : std::string("idle"),
        standings.empty() ? std::string() : std::format("; standing: {}", standings), gLast.empty() ? std::string() : std::format("; last: {}", gLast));
}

void Shutdown() {
    gMachine.Reset();
    gPointer = {};
    gHistory.clear();
    gStandings.clear();
}

}  // namespace mevr::game::squad_orders
