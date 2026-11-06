// SPDX-License-Identifier: MIT
// Squad placement by an arc (game/SquadPlace.hpp).

#include "game/SquadPlace.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/SquadContext.hpp"
#include "core/vr/SquadMenu.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/GamepadMapping.hpp"
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
#include <cctype>
#include <charconv>
#include <cstdio>
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
    float flick = 0.0f;   // `select left|right`: the stick pushed sideways for one tick
    float flickY = 0.0f;  // `select down|up`
    std::vector<bool> script;  // `double`: B down, up, down, up, one tick each
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
double gClock = 0.0;
double gSecondPulseAt = -1.0;  // the second squadmate's tile: a second pulse this long after the first

unsigned gStarted = 0, gGiven = 0, gCancelled = 0, gTraces = 0, gTicksTraced = 0, gSelections = 0, gRegroups = 0, gEquips = 0;
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

// "ASHLEY" -> "Ashley": the selector's tiles in the game's own capitalisation of names.
std::string TitleCase(std::string const& name) {
    std::string out = name;
    bool first = true;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z' && !first) c = static_cast<char>(c - 'A' + 'a');
        first = c == ' ' || c == '-';
    }
    return out;
}

void PublishView(Link const& link, engine::TrackingFrame const* frame, sp::ArcSettings const& settings, bool follow,
    std::vector<squad_weapons::MemberView> const& members, XrPosef const& hand) {
    if (link.publish == nullptr) return;
    sp::View view;
    if (frame != nullptr && gMachine.Holding() && gArc.valid) {
        view.active = 1;
        view.landing = static_cast<std::uint8_t>(gArc.landing);
        view.follow = follow ? 1 : 0;
        view.member = gMachine.Member();
        view.heldS = gMachine.HeldS();
        int const tiles = sp::TileCount(gMachine.Members());
        view.tiles = static_cast<std::uint8_t>(tiles);
        view.tile = static_cast<std::uint8_t>(std::clamp(gMachine.Tile(), 0, std::max(tiles - 1, 0)));
        for (int t = 0; t < tiles && t < 3; ++t) {
            int const m = sp::MemberOfTile(t, gMachine.Members());
            std::string const name = m >= 0 && m < static_cast<int>(members.size()) ? TitleCase(members[static_cast<std::size_t>(m)].name)
                                                                                    : std::string("Squad");
            std::snprintf(view.names[t], sizeof(view.names[t]), "%s", name.c_str());
        }
        view.hand = hand;
        bool const weaponRow = gMachine.CurrentRow() == sp::Row::Weapon;
        std::vector<sp::Vec3> const points = sp::ArcSamples(gArc.originM, gArc.direction, gArc.untilS, kDrawPoints, settings);
        int const count = weaponRow ? 0 : std::min<int>(static_cast<int>(points.size()), sp::kMaxPoints);
        for (int i = 0; i < count; ++i) view.points[i] = engine::ToTracking(*frame, Units(points[static_cast<std::size_t>(i)], gUpm));
        view.count = static_cast<std::uint8_t>(count);
        if (weaponRow) {
            view.row = 1;
            std::vector<int> const& weapons = gMachine.Weapons();
            view.weaponTiles = static_cast<std::uint8_t>(std::min<std::size_t>(weapons.size(), 4));
            view.weaponTile = static_cast<std::uint8_t>(std::max(gMachine.WeaponTile(), 0));
            for (int w = 0; w < view.weaponTiles; ++w) {
                std::string name = vr::squad_menu::WeaponName(weapons[static_cast<std::size_t>(w)]);
                if (!name.empty()) name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
                std::snprintf(view.weaponNames[w], sizeof(view.weaponNames[w]), "%s", name.c_str());
            }
            int const member = gMachine.Member();
            view.weaponInHand = static_cast<std::int8_t>(member >= 0 && member < static_cast<int>(members.size()) ? members[static_cast<std::size_t>(member)].current : -1);
        } else if (count > 0 && gArc.landing == sp::Landing::None) {
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
    gClock += deltaSeconds;
    bool const tracked = controllers.active && controllers.right.aimValid;
    bool const scripted = !gSim.script.empty();
    bool const simulated = gSim.held || gSim.releasing || scripted;
    gSim.releasing = false;
    bool button = gSim.held || (tracked && controllers.right.secondary);
    if (scripted) {
        button = gSim.script.front();
        gSim.script.erase(gSim.script.begin());
    }
    float const stickX = gSim.flick != 0.0f ? gSim.flick : simulated ? 0.0f : tracked ? controllers.right.stickX : 0.0f;
    float const stickY = gSim.flickY != 0.0f ? gSim.flickY : simulated ? 0.0f : tracked ? controllers.right.stickY : 0.0f;
    gSim.flick = 0.0f;
    gSim.flickY = 0.0f;
    bool const usable = s.enabled && s.place && !melee && onFoot && frame.has_value() && (tracked || simulated) &&
        !squad_weapons::Members().empty() && !squad_weapons::IsOpen() && !power_wheel::IsOpen();

    gArc = {};
    XrPosef hand{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    if (usable && (button || gMachine.Holding())) {
        sp::Vec3 originM{};
        sp::Vec3 direction{};
        if (simulated) {
            // From the head, its yaw plus the console's, at the console's pitch.
            engine::UnrealVector const head = frame->headWorld;
            engine::Basis const look = engine::OrientationToWorld(*frame, frame->head.orientation);
            double const yaw = std::atan2(look.forward.y, look.forward.x) + gSim.yawDeg * std::numbers::pi / 180.0;
            double const pitch = gSim.pitchDeg * std::numbers::pi / 180.0;
            // 0.35 m ahead (flat) and 0.3 m below the eyes: where a hand holding the arc out would be.
            originM = Metres(head, gUpm);
            originM.x += static_cast<float>(std::cos(yaw) * 0.35);
            originM.y += static_cast<float>(std::sin(yaw) * 0.35);
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
        hand = simulated ? XrPosef{frame->head.orientation, engine::ToTracking(*frame, Units(originM, gUpm))} : controllers.right.aim;
    }
    std::vector<squad_weapons::MemberView> const members = squad_weapons::Members();

    sp::Settings settings;
    settings.enabled = s.enabled && s.place;
    settings.minHoldS = static_cast<float>(s.placeMinHoldS);
    settings.followWithinM = static_cast<float>(s.followWithinM);
    settings.weapons = s.placeWeapons;
    sp::Input in;
    in.dt = static_cast<float>(deltaSeconds);
    in.usable = usable;
    in.button = button;
    in.stickX = stickX;
    in.stickY = stickY;
    in.members = static_cast<int>(members.size());
    // The lit tile's weapons: a squadmate's own, or any the squad carries (each one equips it if they have it).
    {
        int const lit = gMachine.Holding() ? gMachine.Member() : sp::kSquadTile;
        for (int i = 0; i < static_cast<int>(members.size()); ++i) {
            if (lit != sp::kSquadTile && lit != i) continue;
            in.weaponMask = static_cast<std::uint8_t>(in.weaponMask | members[static_cast<std::size_t>(i)].carried);
            if (lit == i) in.currentWeapon = members[static_cast<std::size_t>(i)].current;
        }
    }
    in.landing = gArc.landing;
    in.landingFromPlayerM = gArc.fromPlayerM;
    sp::Step const step = gMachine.Advance(in, settings);
    auto const whoName = [&](int member) {
        return member >= 0 && member < static_cast<int>(members.size()) ? members[static_cast<std::size_t>(member)].name
                                                                          : std::string("the squad");
    };
    if (step.started) {
        ++gStarted;
        if (link.haptic != nullptr) link.haptic(kRight, 0.04f, 0.4f);
        log::Info("squad place: aiming a place for {}", whoName(step.member));
    }
    // Who's selected, felt without looking: one pulse for the left squadmate, two for the right, a longer one for the squad.
    if (step.selectionChanged) {
        ++gSelections;
        int const member = gMachine.Member();
        if (link.haptic != nullptr) {
            if (member == sp::kSquadTile) {
                link.haptic(kRight, 0.09f, 0.45f);
            } else {
                link.haptic(kRight, 0.03f, 0.6f);
                if (member == 1) gSecondPulseAt = gClock + 0.09;
            }
        }
        log::Info("squad place: the selector is on {}", whoName(member));
    }
    if (step.rowChanged) {
        if (link.haptic != nullptr) link.haptic(kRight, 0.05f, 0.5f);
        log::Info("squad place: {}", gMachine.CurrentRow() == sp::Row::Weapon ? std::format("the weapon row for {}", whoName(gMachine.Member()))
                                                                              : std::string("back to the names"));
    }
    if (step.weaponChanged && link.haptic != nullptr) link.haptic(kRight, 0.02f, 0.35f);
    if (step.equip) {
        ++gEquips;
        std::string outcome;
        for (int i = 0; i < static_cast<int>(members.size()); ++i) {
            squad_weapons::MemberView const& m = members[static_cast<std::size_t>(i)];
            if (step.member != sp::kSquadTile && step.member != i) continue;
            std::string const one = (m.carried & (1u << step.weapon)) != 0 ? squad_weapons::GiveWeapon(m.pawn, step.weapon)
                                                                            : std::string("doesn't carry one");
            outcome += std::format("{}{}: {}", outcome.empty() ? "" : "; ", m.name, one);
        }
        gLast = std::format("{} for {}: {}", vr::squad_menu::WeaponName(step.weapon), whoName(step.member), outcome);
        log::Info("squad place: {}", gLast);
        if (link.haptic != nullptr) link.haptic(kRight, 0.08f, 0.8f);
    }
    if (gSecondPulseAt >= 0.0 && gClock >= gSecondPulseAt) {
        gSecondPulseAt = -1.0;
        if (link.haptic != nullptr) link.haptic(kRight, 0.03f, 0.6f);
    }
    if (step.regroup) {
        ++gRegroups;
        sc::Action action;
        action.who.kind = sc::Who::All;
        action.order = sc::Order::Follow;
        std::string const outcome = squad_orders::Give(action);
        gLast = std::format("B double-tapped: the squad back to Shepard: {}", outcome);
        log::Info("squad place: {}", gLast);
        if (link.haptic != nullptr) link.haptic(kRight, 0.12f, 0.8f);
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
    PublishView(link, frame ? &*frame : nullptr, arcSettings, follow, members, hand);
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
    if (verb == "select") {
        std::string side;
        words >> side;
        if (side == "down" || side == "up") {
            gSim.flickY = side == "down" ? -1.0f : 1.0f;
            return std::format("the stick flicked {} (the {} row was lit)", side, gMachine.CurrentRow() == sp::Row::Weapon ? "weapon" : "names");
        }
        if (side != "left" && side != "right") return std::string("usage: mevr place select left|right|down|up (one flick of the stick)");
        gSim.flick = side == "left" ? -1.0f : 1.0f;
        return std::format("the stick flicked {} (the selector was on tile {})", side, gMachine.Tile());
    }
    if (verb == "double") {
        gSim.held = false;
        gSim.script = {true, false, true, false};
        return std::string("B double-tapped (four ticks): the squad comes back to Shepard (see the log's `squad place:` line)");
    }
    if (verb == "release") {
        gSim.releasing = gSim.held;
        gSim.held = false;
        return std::string("B let go: the order follows on the next tick (see the log's `squad place:` line)");
    }
    if (verb.empty() || verb == "status") return StatusJson();
    return std::string("usage: mevr place [aim <pitchDeg> [yawDeg] | select left|right|down|up | release | double | status]");
}

bool Holding() {
    return gMachine.Holding();
}

void AdjustPad(gamepad::Options& options) {
    if (gMachine.Holding()) {
        options.squadPlace = true;  // the right stick is the selector's while B is held: no turn, no camera
    }
}

std::string StatusJson() {
    return std::format(
        "{{\"holding\": {}, \"member\": {}, \"tile\": {}, \"members\": {}, \"heldS\": {:.2f}, \"landing\": {}, \"fromPlayerM\": {:.2f}, "
        "\"point\": [{:.0f}, {:.0f}, {:.0f}], \"traces\": {}, \"simulated\": {}, \"started\": {}, \"given\": {}, \"cancelled\": {}, "
        "\"selections\": {}, \"regroups\": {}, \"row\": {}, \"weapon\": {}, \"equips\": {}, \"last\": {}}}",
        gMachine.Holding() ? "true" : "false", gMachine.Member(), gMachine.Tile(), gMachine.Members(), gMachine.HeldS(),
        json::JsonString(sp::Name(gArc.landing)), gArc.fromPlayerM, gArc.point.x, gArc.point.y, gArc.point.z, gArc.traces,
        gSim.held ? "true" : "false", gStarted, gGiven, gCancelled, gSelections, gRegroups,
        gMachine.CurrentRow() == sp::Row::Weapon ? "\"weapon\"" : "\"who\"", gMachine.Weapon(), gEquips, json::JsonString(gLast));
}

void LogStats() {
    if (gStarted == 0 && gRegroups == 0) return;
    log::Info("squad place: {} aimed, {} given, {} called off, {} selector steps, {} double-tap regroups; {:.1f} traces a tick while "
              "aiming", gStarted, gGiven, gCancelled, gSelections, gRegroups, gTicksTraced > 0 ? static_cast<double>(gTraces) / gTicksTraced : 0.0);
}

void Shutdown() {
    gMachine.Reset();
    gSim = {};
    gArc = {};
    gPublished = false;
}

}  // namespace mevr::game::squad_place
