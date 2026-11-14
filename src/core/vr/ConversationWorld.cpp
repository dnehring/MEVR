// SPDX-License-Identifier: MIT
#include "core/vr/ConversationWorld.hpp"

#include <Windows.h>

#include <algorithm>
#include <span>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>

namespace mevr::vr::conversation_world {
namespace {

std::mutex gMutex;
State gLatest{};
bool gHave = false;
std::uint64_t gSequence = 0;

constexpr float kStageWidth = 1280.0f;
constexpr float kStageHeight = 720.0f;

// A band of the stage (stage pixels) as fractions of the frame. The conversation movie shows its whole 1280x720 stage across the
// frame's width, centred top to bottom (measured 2026-11-06 on a 2344x2392 HUD frame: the wheel's centre at frame y 1715, stage y
// 643 at 1.83 frame pixels per stage pixel, 537 px of frame above and below the stage).
interact::Rect StageBand(std::uint32_t width, std::uint32_t height, float left, float right, float top, float bottom) {
    if (width == 0 || height == 0) {
        return {};
    }
    float const w = static_cast<float>(width);
    float const h = static_cast<float>(height);
    float const scale = w / kStageWidth;                     // frame pixels per stage pixel
    float const stageTop = (h - kStageHeight * scale) * 0.5f;  // centred (negative for a frame wider than 16:9)
    auto const y = [&](float stageY) { return stageTop + stageY * scale; };
    float const y0 = std::clamp(y(top), 0.0f, h);
    float const y1 = std::clamp(y(bottom), 0.0f, h);
    interact::Rect rect;
    rect.x = std::clamp(left / kStageWidth, 0.0f, 1.0f);
    rect.width = std::clamp((right - left) / kStageWidth, 0.0f, 1.0f - rect.x);
    rect.y = y0 / h;
    rect.height = std::max(0.0f, (y1 - y0) / h);
    return rect;
}

} // namespace

void Publish(State const& state) {
    std::lock_guard lock(gMutex);
    gLatest = state;
    gLatest.size = sizeof(State);
    gLatest.sequence = ++gSequence;
    gLatest.timeMs = GetTickCount64();
    gHave = true;
}

std::optional<State> Latest(std::uint64_t maxAgeMs) {
    std::lock_guard lock(gMutex);
    if (!gHave || gLatest.active == 0 || GetTickCount64() - gLatest.timeMs > maxAgeMs) {
        return std::nullopt;
    }
    return gLatest;
}

bool Armed(std::uint64_t maxAgeMs) {
    std::lock_guard lock(gMutex);
    return gHave && gLatest.active == 0 && gLatest.holdPad[0] != 0 && GetTickCount64() - gLatest.timeMs <= maxAgeMs;
}

bool HandingOver(std::uint64_t maxAgeMs) {
    std::lock_guard lock(gMutex);
    return gHave && gLatest.active == 0 && gLatest.holdPad[1] != 0 && GetTickCount64() - gLatest.timeMs <= maxAgeMs;
}

void Reset() {
    std::lock_guard lock(gMutex);
    gLatest = {};
    gHave = false;
}

Mode ParseMode(char const* text, Mode fallback) {
    if (text == nullptr) {
        return fallback;
    }
    std::string word;
    for (char const* p = text; *p != '\0'; ++p) {
        if (!std::isspace(static_cast<unsigned char>(*p))) {
            word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
        }
    }
    if (word == "world" || word == "2") {
        return Mode::World;
    }
    if (word == "screen" || word == "1") {
        return Mode::Screen;
    }
    if (word == "vr" || word == "0") {
        return Mode::Vr;
    }
    return fallback;
}

namespace {
double Wrap180(double deg) {
    while (deg > 180.0) deg -= 360.0;
    while (deg < -180.0) deg += 360.0;
    return deg;
}
double BearingDeg(Vec3 const& from, Vec3 const& to) {
    return std::atan2(to.y - from.y, to.x - from.x) * 180.0 / 3.14159265358979323846;
}
} // namespace

bool InView(Vec3 const& player, double facingYawDeg, Vec3 const& person, double halfAngleDeg, double minUnits, double maxUnits) {
    double const dx = person.x - player.x, dy = person.y - player.y;
    double const flat = std::hypot(dx, dy);
    if (flat < minUnits || flat > maxUnits) {
        return false;
    }
    return std::abs(Wrap180(BearingDeg(player, person) - facingYawDeg)) <= halfAngleDeg;
}

std::vector<ArcSeat> ArcSeats(Vec3 const& player, Vec3 const& owner, int count, double radiusUnits, double spacingDeg, double halfAngleDeg) {
    std::vector<ArcSeat> seats;
    if (count <= 0 || radiusUnits <= 0.0) {
        return seats;
    }
    double const centre = BearingDeg(player, owner);
    for (int i = 0; i < count; ++i) {
        // Right by one step, left by one, right by two... (the owner's own bearing is the centre).
        int const step = i / 2 + 1;
        double const side = (i % 2 == 0) ? -1.0 : 1.0;
        double const offset = std::min(step * spacingDeg, halfAngleDeg);
        double const bearing = (centre + side * offset) * 3.14159265358979323846 / 180.0;
        ArcSeat seat;
        seat.at = {player.x + std::cos(bearing) * radiusUnits, player.y + std::sin(bearing) * radiusUnits, player.z};
        seat.yawDeg = Wrap180(BearingDeg(seat.at, player));
        seats.push_back(seat);
    }
    return seats;
}

Vec3 GroupMiddle(Vec3 const& player, Vec3 const& owner, std::vector<ArcSeat> const& seats) {
    double x = player.x + owner.x;
    double y = player.y + owner.y;
    for (ArcSeat const& s : seats) {
        x += s.at.x;
        y += s.at.y;
    }
    double const n = 2.0 + static_cast<double>(seats.size());
    return {x / n, y / n, player.z};
}

void FaceToward(std::vector<ArcSeat>& seats, Vec3 const& middle) {
    for (ArcSeat& s : seats) {
        if (std::hypot(middle.x - s.at.x, middle.y - s.at.y) > 1.0) {
            s.yawDeg = Wrap180(BearingDeg(s.at, middle));
        }
    }
}

std::vector<ArcSeat> SquadSeats(Vec3 const& player, double facingYawDeg, Vec3 const& lookAt, int count, double lateralUnits,
    double backUnits) {
    std::vector<ArcSeat> seats;
    if (count <= 0) {
        return seats;
    }
    double const kDeg = 3.14159265358979323846 / 180.0;
    double const f = facingYawDeg * kDeg;
    for (int i = 0; i < count; ++i) {
        double const side = (i % 2 == 0) ? -1.0 : 1.0;
        double const ring = static_cast<double>(i / 2 + 1);
        double const s = f + side * 90.0 * kDeg;
        ArcSeat seat;
        seat.at = {player.x + std::cos(s) * lateralUnits * ring - std::cos(f) * backUnits,
                   player.y + std::sin(s) * lateralUnits * ring - std::sin(f) * backUnits, player.z};
        seat.yawDeg = Wrap180(BearingDeg(seat.at, lookAt));
        seats.push_back(seat);
    }
    return seats;
}

bool IsRideStep(double stepUnits, double previousStepUnits, double jumpUnits, double maxRideUnits) {
    return stepUnits > jumpUnits && stepUnits <= maxRideUnits && previousStepUnits > 0.01 && previousStepUnits <= maxRideUnits;
}

Provisional StepProvisional(bool trackMoved, bool cameraOfItsOwn, double sinceSwitchS, double graceS, double maxS) {
    if (trackMoved) {
        return Provisional::Moment;
    }
    if (sinceSwitchS <= graceS) {
        return Provisional::Wait;
    }
    return cameraOfItsOwn || sinceSwitchS > maxS ? Provisional::NotOne : Provisional::Wait;
}

std::vector<TalkPair> CountPairs(std::vector<LineEnds> const& lines) {
    std::vector<TalkPair> pairs;
    for (LineEnds const& line : lines) {
        if (line.speaker == line.listener || line.speaker < -2 || line.listener < -2) {
            continue;  // to oneself, or an end the conversation leaves open (a reply's -3: whoever spoke before)
        }
        int const a = std::min(line.speaker, line.listener);
        int const b = std::max(line.speaker, line.listener);
        auto it = std::find_if(pairs.begin(), pairs.end(), [&](TalkPair const& p) { return p.a == a && p.b == b; });
        if (it == pairs.end()) {
            pairs.push_back({a, b, 1});
        } else {
            ++it->lines;
        }
    }
    std::stable_sort(pairs.begin(), pairs.end(), [](TalkPair const& l, TalkPair const& r) { return l.lines > r.lines; });
    return pairs;
}

namespace {
// Where member m stands and faces under `seatOf` (a seated member at its seat; one without a seat where it is).
GroupMember MemberAt(std::vector<GroupMember> const& members, std::vector<int> const& seated, std::vector<ArcSeat> const& seats,
    std::vector<int> const& seatOf, int m) {
    GroupMember g = members[static_cast<std::size_t>(m)];
    for (std::size_t k = 0; k < seated.size(); ++k) {
        if (seated[k] == m && k < seatOf.size() && seatOf[k] >= 0 && static_cast<std::size_t>(seatOf[k]) < seats.size()) {
            g.at = seats[static_cast<std::size_t>(seatOf[k])].at;
            g.yawDeg = seats[static_cast<std::size_t>(seatOf[k])].yawDeg;
        }
    }
    return g;
}
// One person's turn toward another: its angle off the facing, four times over past the reach.
double LookCost(GroupMember const& from, Vec3 const& to, double reachDeg) {
    if (!from.looks || std::hypot(to.x - from.at.x, to.y - from.at.y) < 1.0) {
        return 0.0;
    }
    double const off = std::abs(Wrap180(BearingDeg(from.at, to) - from.yawDeg));
    return off + 3.0 * std::max(0.0, off - reachDeg);
}
} // namespace

double PairLookCost(std::vector<GroupMember> const& members, std::vector<int> const& seated, std::vector<ArcSeat> const& seats,
    std::vector<int> const& seatOf, std::vector<TalkPair> const& pairs, double reachDeg) {
    double cost = 0.0;
    int const n = static_cast<int>(members.size());
    for (TalkPair const& p : pairs) {
        if (p.a < 0 || p.b < 0 || p.a >= n || p.b >= n || p.a == p.b || p.lines <= 0) {
            continue;
        }
        GroupMember const a = MemberAt(members, seated, seats, seatOf, p.a);
        GroupMember const b = MemberAt(members, seated, seats, seatOf, p.b);
        cost += p.lines * (LookCost(a, b.at, reachDeg) + LookCost(b, a.at, reachDeg));
    }
    return cost;
}

std::vector<int> SeatsForPairs(std::vector<GroupMember> const& members, std::vector<int> const& seated,
    std::vector<ArcSeat> const& seats, std::vector<TalkPair> const& pairs, double reachDeg) {
    std::vector<int> seatOf(seated.size(), -1);
    for (std::size_t k = 0; k < seated.size() && k < seats.size(); ++k) {
        seatOf[k] = static_cast<int>(k);
    }
    if (seated.size() < 2 || seats.size() < seated.size() || seats.size() > 7) {
        return seatOf;  // nothing to choose, or too many to try every order
    }
    // Every assignment of the first seats.size() seats, in lexicographic order from the seat order itself: a strictly lower
    // cost replaces the best, so a tie keeps the earlier (the seat order first).
    std::vector<int> order(seats.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = static_cast<int>(i);
    }
    std::vector<int> best = seatOf;
    double bestCost = PairLookCost(members, seated, seats, best, pairs, reachDeg);
    std::vector<int> trial(seated.size());
    do {
        std::copy_n(order.begin(), seated.size(), trial.begin());
        double const cost = PairLookCost(members, seated, seats, trial, pairs, reachDeg);
        if (cost < bestCost - 1e-9) {
            bestCost = cost;
            best = trial;
        }
    } while (std::next_permutation(order.begin(), order.end()));
    return best;
}

namespace {

bool SameText(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

bool HasText(std::string_view text, std::string_view part) {
    if (part.empty() || part.size() > text.size()) {
        return part.empty();
    }
    for (std::size_t i = 0; i + part.size() <= text.size(); ++i) {
        if (SameText(text.substr(i, part.size()), part)) {
            return true;
        }
    }
    return false;
}

}  // namespace

ShownEarly const* ShownEarlyFor(std::string_view trackPath) {
    // The walk with Anderson to the Council (save 80): its matinee is the captain level's SarenCouncil.SeqAct_Interp_0; the hearing
    // stands in BIOA_STA70_02council_DSG, shown by the chamber's streaming volume (streamvol_council) under the walk's black screen,
    // its people unhidden there by its own SarenCouncil sequence. Measured 2026-11-13 (research/69 "The council during the walk").
    static constexpr ShownEarly kTable[] = {
        {"BIOA_STA70_02captain_DSG", "SarenCouncil.SeqAct_Interp_0", "BIOA_STA70_02council_DSG",
         {"sta90_ambassador", "sta90_asari_councilor", "sta90_salarian_councilor", "sta90_turian_councilor"}},
    };
    for (ShownEarly const& e : kTable) {
        std::string const level = std::string(e.matineeLevel) + ".TheWorld.PersistentLevel.";
        std::string const matinee = "." + std::string(e.matinee);
        std::size_t const at = [&]() -> std::size_t {
            for (std::size_t i = 0; i + matinee.size() <= trackPath.size(); ++i) {
                if (SameText(trackPath.substr(i, matinee.size()), matinee)) {
                    return i + matinee.size();
                }
            }
            return std::string_view::npos;
        }();
        // The matinee's name ends there (SeqAct_Interp_0, not SeqAct_Interp_01).
        bool const whole = at != std::string_view::npos && (at == trackPath.size() || trackPath[at] == '.');
        if (whole && HasText(trackPath, level)) {
            return &e;
        }
    }
    return nullptr;
}

bool ShownEarlyTag(ShownEarly const& entry, std::string_view tag) {
    return !tag.empty() && std::any_of(entry.tags.begin(), entry.tags.end(), [&](std::string_view t) { return !t.empty() && SameText(t, tag); });
}

bool IsStagedScene(std::string_view name, std::string_view extra) {
    // The owner, 2026-11-11: "Staged scenes are like the council or the virmire speech." Kirrahe's camp on Virmire (his arrival
    // relocates everyone onto the stage by the tents, his speech stands his men before him: research/69 items 20, 27); the
    // Council's hearing, the Spectre induction and the grounding of the Normandy; the ending's Council and choice (on the 3D
    // screen as set pieces unless worldSetPieces = world). Candidates for more: research/69 "The staged scenes".
    static constexpr std::string_view kStaged[] = {
        "jug20_sal_captainkirrahe", "sta90_council_saren", "sta90_council_spectre_induction", "sta90_grounded",
        "end90_council_alive", "end90_council_dead", "end90_choice",
    };
    std::string const key = ConversationKey(name);
    if (std::find(std::begin(kStaged), std::end(kStaged), key) != std::end(kStaged)) {
        return true;
    }
    while (!extra.empty()) {
        std::size_t const comma = extra.find(',');
        std::string_view item = extra.substr(0, comma);
        extra = comma == std::string_view::npos ? std::string_view{} : extra.substr(comma + 1);
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) {
            item.remove_prefix(1);
        }
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) {
            item.remove_suffix(1);
        }
        if (!item.empty() && ConversationKey(item) == key) {
            return true;
        }
    }
    return false;
}

namespace {
constexpr double kPi = 3.14159265358979323846;
Vec3 OnCircle(ConversationCircle const& c, double deg, double z) {
    double const r = deg * kPi / 180.0;
    return {c.centre.x + c.radius * std::cos(r), c.centre.y + c.radius * std::sin(r), z};
}
} // namespace

double CircleRadius(int people, CircleSettings const& s) {
    double const n = static_cast<double>(std::max(2, people));
    return std::clamp(n * s.gapUnits / (2.0 * kPi), s.minRadiusUnits, std::max(s.minRadiusUnits, s.maxRadiusUnits));
}

ConversationCircle MakeCircle(Vec3 const& player, Vec3 const& anchor, int people, CircleSettings const& s) {
    ConversationCircle c;
    c.made = true;
    c.player = player;
    c.people = std::max(2, people);
    c.radius = CircleRadius(people, s);
    double dx = anchor.x - player.x;
    double dy = anchor.y - player.y;
    double const len = std::hypot(dx, dy);
    if (len < 1e-6) {
        dx = 1.0;
        dy = 0.0;
    } else {
        dx /= len;
        dy /= len;
    }
    c.centre = {player.x + dx * c.radius, player.y + dy * c.radius, player.z};
    c.playerDeg = BearingDeg(c.centre, player);
    return c;
}

CircleSpot JoinCircle(ConversationCircle& circle, int id, Vec3 const& from, bool first, CircleSettings const& s) {
    CircleSpot spot;
    for (ConversationCircle::Member const& m : circle.members) {
        if (m.id == id) {
            spot.already = true;
            spot.angleDeg = m.angleDeg;
            spot.at = from;
            spot.faceYawDeg = BearingDeg(from, circle.centre);
            return spot;
        }
    }
    double const bearing = BearingDeg(circle.centre, from);
    double const fromPlayer = std::hypot(from.x - circle.player.x, from.y - circle.player.y);
    if ((first && (s.ownerAlwaysStays || fromPlayer <= s.ownerNearUnits)) || (!first && fromPlayer <= s.inRangeUnits)) {
        // The one the player talks to (the player walked up to it), or someone already within talking range: it is where the
        // conversation is; it stays.
        spot.at = from;
        spot.angleDeg = bearing;
        spot.faceYawDeg = BearingDeg(from, circle.player);
        circle.members.push_back({id, bearing});
        return spot;
    }
    // The gap between two people on the rim, as an angle.
    double const half = std::clamp(s.gapUnits / (2.0 * std::max(circle.radius, 1.0)), 0.0, 1.0);
    double const gapDeg = 2.0 * std::asin(half) * 180.0 / kPi;
    auto const isFree = [&](double deg) {
        if (std::abs(Wrap180(deg - circle.playerDeg)) < gapDeg) {
            return false;
        }
        for (ConversationCircle::Member const& m : circle.members) {
            if (std::abs(Wrap180(deg - m.angleDeg)) < gapDeg) {
                return false;
            }
        }
        return true;
    };
    // The circle's own places: the player's and `people - 1` more, evenly round it. A newcomer takes the free one nearest its
    // own bearing (so nobody crosses the circle); one already standing there (it joined where it stood) holds the nearest.
    int const people = std::max(2, circle.people);
    double const spacing = 360.0 / people;
    double chosen = 0.0;
    bool found = false;
    double best = 1e9;
    for (int k = 1; k < people; ++k) {
        double const deg = circle.playerDeg + k * spacing;
        bool taken = false;
        for (ConversationCircle::Member const& m : circle.members) {
            if (std::abs(Wrap180(deg - m.angleDeg)) < spacing * 0.5) {
                taken = true;
                break;
            }
        }
        double const off = std::abs(Wrap180(deg - bearing));
        if (!taken && off < best) {
            best = off;
            chosen = deg;
            found = true;
        }
    }
    if (!found) {
        // More people than the circle was made for: the free place nearest its bearing, a gap from everyone.
        chosen = bearing;
        for (int step = 0; step <= 36; ++step) {
            double const off = 5.0 * step;
            if (isFree(bearing + off)) {
                chosen = bearing + off;
                break;
            }
            if (step > 0 && isFree(bearing - off)) {
                chosen = bearing - off;
                break;
            }
        }
    }
    chosen = Wrap180(chosen);
    Vec3 const at = OnCircle(circle, chosen, from.z);
    double const walk = std::hypot(at.x - from.x, at.y - from.y);
    if (walk > s.maxWalkUnits) {
        spot.tooFar = true;
        spot.at = from;
        spot.walkUnits = walk;
        return spot;
    }
    if (walk < s.arriveUnits) {
        spot.at = from;
        spot.angleDeg = bearing;
        spot.faceYawDeg = BearingDeg(from, circle.centre);
        circle.members.push_back({id, bearing});
        return spot;
    }
    spot.at = at;
    spot.angleDeg = chosen;
    spot.faceYawDeg = BearingDeg(at, circle.centre);
    spot.walkUnits = walk;
    spot.walk = true;
    circle.members.push_back({id, chosen});
    return spot;
}

bool ShouldRegather(RegatherState& state, ConversationCircle const& circle, Vec3 const& player, double dt, CircleSettings const& s) {
    if (!s.regather) {
        return false;
    }
    if (!state.have || std::hypot(player.x - state.last.x, player.y - state.last.y) > 10.0) {
        state.last = player;
        state.have = true;
        state.stillFor = 0.0;
        return false;
    }
    state.stillFor += std::max(0.0, dt);
    return circle.made && state.stillFor >= s.stillSeconds &&
           std::hypot(player.x - circle.player.x, player.y - circle.player.y) > s.regatherUnits;
}

double StaggerSeconds(int order, CircleSettings const& s) {
    return std::max(0, order) * s.staggerSeconds;
}

namespace {
// Breadth-first over the graph from entry `entry`: `visit(isReply, node, lines)` for every node within `depth` lines.
template <class Visit>
void WalkGraph(DialogGraph const& graph, int entry, int depth, Visit&& visit) {
    if (entry < 0 || entry >= static_cast<int>(graph.entries.size()) || depth <= 0) {
        return;
    }
    std::vector<int> seenEntry(graph.entries.size(), -1);
    std::vector<int> seenReply(graph.replyEntries.size(), -1);
    struct Node {
        bool reply;
        int index;
        int lines;
    };
    std::vector<Node> queue{{false, entry, 0}};
    seenEntry[static_cast<std::size_t>(entry)] = 0;
    for (std::size_t q = 0; q < queue.size(); ++q) {
        Node const node = queue[q];
        if (node.lines > 0) {
            visit(node.reply, node.index, node.lines);
        }
        if (node.lines >= depth) {
            continue;
        }
        if (!node.reply) {
            for (int r : graph.entries[static_cast<std::size_t>(node.index)].replies) {
                if (r >= 0 && r < static_cast<int>(seenReply.size()) && seenReply[static_cast<std::size_t>(r)] < 0) {
                    seenReply[static_cast<std::size_t>(r)] = node.lines + 1;
                    queue.push_back({true, r, node.lines + 1});
                }
            }
        } else {
            for (int e : graph.replyEntries[static_cast<std::size_t>(node.index)]) {
                if (e >= 0 && e < static_cast<int>(seenEntry.size()) && seenEntry[static_cast<std::size_t>(e)] < 0) {
                    seenEntry[static_cast<std::size_t>(e)] = node.lines + 1;
                    queue.push_back({false, e, node.lines + 1});
                }
            }
        }
    }
}
} // namespace

std::vector<Upcoming> UpcomingPeople(DialogGraph const& graph, int entry, int depth) {
    std::vector<Upcoming> out;
    auto const note = [&](int index, int lines) {
        if (index == -2 || index < -1) {
            return;  // the player
        }
        for (Upcoming& u : out) {
            if (u.index == index) {
                u.linesAway = std::min(u.linesAway, lines);
                return;
            }
        }
        out.push_back({index, lines});
    };
    WalkGraph(graph, entry, depth, [&](bool reply, int node, int lines) {
        if (!reply) {
            DialogGraph::Entry const& e = graph.entries[static_cast<std::size_t>(node)];
            note(e.speaker, lines);
            note(e.listener, lines);
        }
    });
    std::sort(out.begin(), out.end(), [](Upcoming const& a, Upcoming const& b) {
        return a.linesAway != b.linesAway ? a.linesAway < b.linesAway : a.index < b.index;
    });
    return out;
}

bool PlayerSoon(DialogGraph const& graph, int entry, int depth) {
    bool soon = false;
    WalkGraph(graph, entry, depth, [&](bool reply, int node, int) {
        if (reply) {
            soon = true;
        } else {
            DialogGraph::Entry const& e = graph.entries[static_cast<std::size_t>(node)];
            soon = soon || e.listener == -2 || e.speaker == -2;
        }
    });
    if (entry >= 0 && entry < static_cast<int>(graph.entries.size())) {
        DialogGraph::Entry const& now = graph.entries[static_cast<std::size_t>(entry)];
        soon = soon || now.listener == -2 || now.speaker == -2;
    }
    return soon;
}

std::vector<FormationSpot> SquadFormation(Vec3 const& player, Vec3 const& lookAt, int count, double sideUnits, double backUnits,
    std::uint32_t seed) {
    std::vector<FormationSpot> out;
    double dx = lookAt.x - player.x;
    double dy = lookAt.y - player.y;
    double const len = std::hypot(dx, dy);
    if (len < 1e-6) {
        dx = 1.0;
        dy = 0.0;
    } else {
        dx /= len;
        dy /= len;
    }
    // Unreal's right of a forward (x, y) on the floor: (-y, x).
    double const rx = -dy;
    double const ry = dx;
    // A small deterministic variation per place: no two squads stand or set off alike.
    std::uint32_t state = seed * 2654435761u + 0x9E3779B9u;
    auto const vary = [&](double spread) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (static_cast<double>(state % 10001u) / 10000.0 * 2.0 - 1.0) * spread;
    };
    for (int i = 0; i < count; ++i) {
        double side = i % 2 == 0 ? 1.0 : -1.0;              // right, left, right...
        double const row = static_cast<double>(i / 2);      // a third (and fourth) further back
        double const across = sideUnits + vary(0.15 * sideUnits) - row * 0.25 * sideUnits;
        double const back = backUnits * (1.0 + row) + vary(0.3 * backUnits) + row * 40.0;
        Vec3 at{player.x + rx * side * across - dx * back, player.y + ry * side * across - dy * back, player.z};
        FormationSpot spot;
        spot.at = at;
        spot.faceYawDeg = BearingDeg(at, lookAt) + vary(8.0);
        spot.delaySeconds = 0.5 + 0.9 * i + std::abs(vary(0.4));
        out.push_back(spot);
    }
    return out;
}

std::vector<int> AssignSpots(std::vector<Vec3> const& from, std::vector<FormationSpot> const& spots) {
    std::size_t const n = std::min(from.size(), spots.size());
    std::vector<int> order(n);
    for (std::size_t i = 0; i < n; ++i) {
        order[i] = static_cast<int>(i);
    }
    if (n < 2 || n > 6) {
        return order;
    }
    auto const cost = [&](std::vector<int> const& o) {
        double sum = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            Vec3 const& a = from[i];
            Vec3 const& b = spots[static_cast<std::size_t>(o[i])].at;
            sum += std::hypot(a.x - b.x, a.y - b.y);
        }
        return sum;
    };
    std::vector<int> best = order;
    double bestCost = cost(order);
    while (std::next_permutation(order.begin(), order.end())) {
        double const c = cost(order);
        if (c < bestCost - 1e-6) {
            bestCost = c;
            best = order;
        }
    }
    return best;
}

WalkAlongStep StepWalkAlong(WalkAlongState& state, double gapUnits, int companions, bool linePlaying, double dt,
    WalkAlongSettings const& settings) {
    if (state.paused) {
        state.pausedS += dt;
        state.pausedTotalS += dt;
        if (gapUnits <= settings.resumeUnits || companions == 0) {
            state.paused = false;
            return WalkAlongStep::Resume;
        }
        if (settings.maxPauseS > 0.0 && state.pausedS >= settings.maxPauseS) {
            state.paused = false;
            state.gaveUp = true;
            return WalkAlongStep::Resume;
        }
        return WalkAlongStep::Nothing;
    }
    if (state.gaveUp || companions == 0 || gapUnits <= settings.pauseUnits) {
        state.waitingLine = false;
        return WalkAlongStep::Nothing;
    }
    if (linePlaying) {
        // A line is not cut off: the walk pauses at its end.
        bool const first = !state.waitingLine;
        state.waitingLine = true;
        return first ? WalkAlongStep::WaitLine : WalkAlongStep::Nothing;
    }
    state.waitingLine = false;
    state.paused = true;
    state.pausedS = 0.0;
    ++state.pauses;
    return WalkAlongStep::Pause;
}

bool IsSetPiece(std::string_view name) {
    // tools/conversation-scan.py --list framed, over the game's packages (2026-11-06).
    // tools/conversation-scan.py --set-pieces (research/59 "Marking the rest", 2026-11-09): framed, or Shepard walked by the game.
    // Since 2026-11-10 (research/69 "Set pieces in the world") without the six framed conversations that were played in first
    // person and had a place for the player (the scan's SEEN_IN_WORLD: the council hearing, the comm room with Anderson and
    // Nihlus, the approach to Ilos, the archive door, Wrex at Fist's): their cameras were all that was authored.
    // Since 2026-11-11 without Kirrahe's camp (the scan's STAGED_IN_WORLD: the owner wants it in first person, a staged scene).
    static constexpr std::string_view kSetPieces[] = {
        "end90_council_alive", "end90_council_dead", "end90_saren", "jug70_sovereign", "nor20_empty_locker",
        "nor20_ilos_open", "nor20_leaving_oculon", "nor20_start_kaidan", "nor20_start_liara", "prc1_cutscene_discovered",
        "prc1_human_console", "sta60_scenicview", "sta60_stripperseat", "war30_trig_01_entry",
    };
    std::string const key = ConversationKey(name);
    return std::find(std::begin(kSetPieces), std::end(kSetPieces), key) != std::end(kSetPieces);
}

std::string ConversationKey(std::string_view name) {
    std::string key;
    std::size_t const dot = name.rfind('.');
    std::size_t const space = name.rfind(' ');
    std::size_t start = 0;
    if (dot != std::string_view::npos) {
        start = dot + 1;
    } else if (space != std::string_view::npos) {
        start = space + 1;
    }
    for (char c : name.substr(start)) {
        key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    for (std::string_view suffix : {std::string_view("_dlg"), std::string_view("_d")}) {
        if (key.size() > suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
            key.resize(key.size() - suffix.size());
            break;
        }
    }
    return key;
}

namespace {
struct FlowRow {
    std::string_view name;
    int offset;  // into kFlowEntries
    int count;
};
// tools/conversation-scan.py --flow-list over the game's packages (2026-11-09; research/59 "Flowing conversations"): per
// conversation a level's script starts, the entries of its chain starts (plays that can never reach a wheel) that no
// conversation start reaches and that the game left ordinary; chain starts behind an unconditional start are left out (the game
// takes the first start whose conditional holds, so they never play), as are set pieces, transmissions and the narration under
// the game's own screens (the character creation, the tutorial popups, the news terminals), and chains that apply plot effects
// after their first line (a state transition or a Kismet node event on a later entry or reply: research/69 item 1; an ambient
// conversation can be cut short, and the lines it never reaches never apply theirs). Generated, never typed.
constexpr int kFlowEntries[] = {
    67,  // cha00_asari_liara
    1, 2, 4, 5,  // combat_banter_00
    2, 3,  // ice20_giannaparasini
    1, 2, 39, 40, 41, 42,  // ice20_inamorda
    1,  // ice50_amb_announcements
    10, 15,  // ice50_trig13_salarian_suicide
    0,  // ice50_trig22_juleth_ambush
    2, 3,  // ice60_ambient_barricade_2
    2, 3,  // ice60_ambient_guard_1
    2, 3,  // ice60_ambient_guard_2
    5, 6,  // ice60_ambient_scientist_1
    5, 6,  // ice60_ambient_scientist_2
    6, 9, 10,  // ice60_ambient_scientist_3
    5, 6,  // ice60_ambient_scientist_4
    0, 1,  // ice60_trig12_olar_execution
    8,  // jug20_amb_ashley
    7,  // jug20_amb_garrus
    1,  // jug20_amb_kaidan
    1,  // jug20_amb_liara
    8,  // jug20_amb_tali
    3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 15, 16, 17, 19, 20, 21, 22, 23,  // jug20_trigger
    26,  // jug70_sal_prisoner
    31, 32, 35, 38, 39, 42, 52, 55,  // jug70_trigger
    0, 1, 2, 3, 4, 5, 6, 7, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46,  // jug80_saren
    10, 15, 19, 20,  // jug80_trigger
    21, 22,  // lav70_asariscientist
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,  // los30_security_system_done
    5, 6, 7, 8, 9,  // los30_trig_a
    6,  // los40_trig_c
    1, 2, 3,  // nor10_ambient_01
    0, 1, 2,  // nor10_engineer
    0,  // nor10_joker
    9, 10,  // nor10_landing_bay
    1, 3, 4,  // prc1_human_radio
    72, 73,  // prc1_surveyor_wrapup
    0,  // prc1_trig_batarians
    1, 2, 3,  // prc1_trig_main_facility_radio
    0,  // pro10_m_scientist02
    0, 1,  // pro20_powell
    0, 1,  // sp108_diplomat
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,  // sp114_triggers
    16,  // sp119_major_kyle
    0,  // sta00_cab
    1, 4,  // sta20_asari_employee03
    0,  // sta20_chorban_triggers
    35, 36, 37, 89, 90, 91, 92, 93, 94,  // sta20_human_ambassador_enter
    3, 7,  // sta30_banter_01
    0, 1,  // sta30_chellik
    1, 4,  // sta60_schells
    0, 1,  // sta70_garrus
    1,  // unc_banter_00
    1, 9,  // war20_ambientbunker01
    1, 2, 5, 10, 11, 19, 24,  // war20_ambientmain02
    1, 2, 4, 5, 6,  // war20_arceliasilvamartinez
    1, 2, 3, 4, 6, 7, 9, 23, 24, 25, 26, 27,  // war20_calanthablake
    2, 3, 4, 5, 7, 8, 9, 10, 52, 53, 54, 55,  // war20_davinandersson
    0, 1, 2, 3, 4,  // war20_faizhou
    28, 29, 30, 31,  // war20_inoste_merchant
    6, 23, 24, 25,  // war20_machadoyle
    10,  // war40_ambrefugee_female01
    10,  // war40_ambrefugee_male01
    10,  // war40_ambrefugee_male03
    12,  // war50_claw01
    15, 17, 18, 20, 22, 72, 73, 74, 75, 76, 77, 78, 79, 80,  // war50_lizbeth
};
constexpr FlowRow kFlowRows[] = {
    {"cha00_asari_liara", 0, 1},
    {"combat_banter_00", 1, 4},
    {"ice20_giannaparasini", 5, 2},
    {"ice20_inamorda", 7, 6},
    {"ice50_amb_announcements", 13, 1},
    {"ice50_trig13_salarian_suicide", 14, 2},
    {"ice50_trig22_juleth_ambush", 16, 1},
    {"ice60_ambient_barricade_2", 17, 2},
    {"ice60_ambient_guard_1", 19, 2},
    {"ice60_ambient_guard_2", 21, 2},
    {"ice60_ambient_scientist_1", 23, 2},
    {"ice60_ambient_scientist_2", 25, 2},
    {"ice60_ambient_scientist_3", 27, 3},
    {"ice60_ambient_scientist_4", 30, 2},
    {"ice60_trig12_olar_execution", 32, 2},
    {"jug20_amb_ashley", 34, 1},
    {"jug20_amb_garrus", 35, 1},
    {"jug20_amb_kaidan", 36, 1},
    {"jug20_amb_liara", 37, 1},
    {"jug20_amb_tali", 38, 1},
    {"jug20_trigger", 39, 18},
    {"jug70_sal_prisoner", 57, 1},
    {"jug70_trigger", 58, 8},
    {"jug80_saren", 66, 21},
    {"jug80_trigger", 87, 4},
    {"lav70_asariscientist", 91, 2},
    {"los30_security_system_done", 93, 16},
    {"los30_trig_a", 109, 5},
    {"los40_trig_c", 114, 1},
    {"nor10_ambient_01", 115, 3},
    {"nor10_engineer", 118, 3},
    {"nor10_joker", 121, 1},
    {"nor10_landing_bay", 122, 2},
    {"prc1_human_radio", 124, 3},
    {"prc1_surveyor_wrapup", 127, 2},
    {"prc1_trig_batarians", 129, 1},
    {"prc1_trig_main_facility_radio", 130, 3},
    {"pro10_m_scientist02", 133, 1},
    {"pro20_powell", 134, 2},
    {"sp108_diplomat", 136, 2},
    {"sp114_triggers", 138, 19},
    {"sp119_major_kyle", 157, 1},
    {"sta00_cab", 158, 1},
    {"sta20_asari_employee03", 159, 2},
    {"sta20_chorban_triggers", 161, 1},
    {"sta20_human_ambassador_enter", 162, 9},
    {"sta30_banter_01", 171, 2},
    {"sta30_chellik", 173, 2},
    {"sta60_schells", 175, 2},
    {"sta70_garrus", 177, 2},
    {"unc_banter_00", 179, 1},
    {"war20_ambientbunker01", 180, 2},
    {"war20_ambientmain02", 182, 7},
    {"war20_arceliasilvamartinez", 189, 5},
    {"war20_calanthablake", 194, 12},
    {"war20_davinandersson", 206, 12},
    {"war20_faizhou", 218, 5},
    {"war20_inoste_merchant", 223, 4},
    {"war20_machadoyle", 227, 4},
    {"war40_ambrefugee_female01", 231, 1},
    {"war40_ambrefugee_male01", 232, 1},
    {"war40_ambrefugee_male03", 233, 1},
    {"war50_claw01", 234, 1},
    {"war50_lizbeth", 235, 14},
};
} // namespace

std::span<int const> FlowEntries(std::string_view name) {
    std::string const key = ConversationKey(name);
    for (FlowRow const& row : kFlowRows) {
        if (row.name == key) {
            return {kFlowEntries + row.offset, static_cast<std::size_t>(row.count)};
        }
    }
    return {};
}

PlayerStep StepPlayer(Vec3 const& place, Vec3 const& anchor, double leashUnits, double stickX, double stickY, double headYawDeg,
    double speedUnitsPerS, double dtS) {
    PlayerStep out;
    out.place = place;
    constexpr double kDeadZone = 0.15;
    double const magnitude = std::hypot(stickX, stickY);
    if (magnitude < kDeadZone || dtS <= 0.0 || speedUnitsPerS <= 0.0) {
        return out;
    }
    // The stick's deflection past the dead zone, up to 1: a gentle push walks slowly.
    double const scale = std::min(1.0, (magnitude - kDeadZone) / (1.0 - kDeadZone)) / magnitude;
    double const yaw = headYawDeg * 3.14159265358979323846 / 180.0;
    // Unreal: forward (cos, sin), right (-sin, cos) in the X-Y plane.
    double const dx = (std::cos(yaw) * stickY - std::sin(yaw) * stickX) * scale;
    double const dy = (std::sin(yaw) * stickY + std::cos(yaw) * stickX) * scale;
    double const step = speedUnitsPerS * dtS;
    Vec3 next{place.x + dx * step, place.y + dy * step, place.z};
    double const before = std::hypot(place.x - anchor.x, place.y - anchor.y);
    double const after = std::hypot(next.x - anchor.x, next.y - anchor.y);
    if (leashUnits > 0.0 && after > leashUnits) {
        if (before > leashUnits) {
            if (after >= before) {
                return out;  // outside already (the anchor moved): only a step nearer is taken, as it is
            }
        } else {
            // Slide along the circle: the step's part along the tangent stays, the part outward is dropped.
            double const ox = next.x - anchor.x, oy = next.y - anchor.y;
            next.x = anchor.x + ox / after * leashUnits;
            next.y = anchor.y + oy / after * leashUnits;
            out.atLeash = true;
        }
    }
    if (std::hypot(next.x - place.x, next.y - place.y) < 1e-6) {
        out.atLeash = leashUnits > 0.0;
        return out;
    }
    out.place = next;
    out.moved = true;
    return out;
}

NewcomerRule ParseNewcomerRule(std::string_view text, NewcomerRule fallback) {
    std::string lower;
    for (char const c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (lower == "hold" || lower == "2") return NewcomerRule::Hold;
    if (lower == "refuse" || lower == "1") return NewcomerRule::Refuse;
    if (lower == "through" || lower == "off" || lower == "0") return NewcomerRule::Through;
    return fallback;
}

char const* Name(NewcomerRule rule) {
    return rule == NewcomerRule::Hold ? "hold" : rule == NewcomerRule::Refuse ? "refuse" : "through";
}

NewcomerRule DecideNewcomer(NewcomerFacts const& facts, NewcomerRule scenes) {
    if (!facts.sceneOurs || facts.same || scenes == NewcomerRule::Through) {
        return NewcomerRule::Through;
    }
    if (facts.gameBark) {
        return NewcomerRule::Refuse;  // as the game refuses it during the conversation the scene was authored as
    }
    if (scenes == NewcomerRule::Hold) {
        return facts.hasStartAction ? NewcomerRule::Hold : NewcomerRule::Through;  // nothing to fire again: not lost by us
    }
    return NewcomerRule::Refuse;
}

OpeningRule ParseOpeningRule(std::string_view text, OpeningRule fallback) {
    std::string lower;
    for (char const c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    if (lower == "auto") return OpeningRule::Auto;
    if (lower == "keep") return OpeningRule::Keep;
    if (lower == "accept") return OpeningRule::Accept;
    return fallback;
}

char const* Name(OpeningRule rule) {
    return rule == OpeningRule::Auto ? "auto" : rule == OpeningRule::Keep ? "keep" : "accept";
}

char const* Name(Opening opening) {
    return opening == Opening::Stay ? "stay" : opening == Opening::WalkIn ? "walk in" : "cut";
}

Opening DecideOpening(OpeningFacts const& facts, OpeningRule rule, OpeningLimits const& limits) {
    if (rule == OpeningRule::Accept) {
        return Opening::Cut;
    }
    if (rule == OpeningRule::Keep) {
        return Opening::Stay;
    }
    // Without a place to stay at, without a body, or with the people in view rearranged, it is another composition.
    if (!facts.known || facts.shepardHidden || facts.peopleMovedUnits > limits.peopleMaxUnits) {
        return Opening::Cut;
    }
    if (facts.markUnits <= limits.stayUnits && std::abs(facts.riseUnits) <= limits.stayUnits) {
        return Opening::Stay;
    }
    // The game's own fade to black moved him: the player goes with him under it (the owner, 2026-11-13).
    if (facts.coveredByBlack) {
        return Opening::Cut;
    }
    if (facts.markUnits <= limits.walkMaxUnits && std::abs(facts.riseUnits) <= limits.riseMaxUnits && facts.sightToMark && facts.floorAtMark) {
        return Opening::WalkIn;
    }
    return Opening::Cut;
}

AttentionStep StepAttention(AttentionState& state, double bodyYawDeg, std::optional<double> bearingDeg, double dtS, AttentionLimits const& limits) {
    AttentionStep step;
    if (!bearingDeg) {
        state = {};
        return step;
    }
    double const off = Wrap180(*bearingDeg - bodyYawDeg);
    double const comfort = std::max(0.0, limits.comfortDeg);
    double const settle = std::clamp(limits.settleDeg, 0.0, comfort);
    step.offDeg = off;
    if (state.turning) {
        if (std::abs(off) <= settle + 0.5) {
            state = {};
            return step;
        }
    } else if (std::abs(off) > comfort) {
        state.outsideS += std::max(0.0, dtS);
        if (state.outsideS >= limits.dwellS) {
            state.turning = true;
            step.began = true;
        }
    } else {
        state.outsideS = 0.0;
    }
    if (state.turning) {
        step.turning = true;
        step.goalYawDeg = Wrap180(*bearingDeg - (off > 0.0 ? settle : -settle));
    }
    return step;
}

double TurnStepDeg(double diffDeg, double dtS, double rateDegPerS) {
    double const rate = std::clamp(std::abs(diffDeg) * 2.0, rateDegPerS * 0.2, rateDegPerS);
    double const step = rate * std::max(0.0, dtS);
    return std::clamp(diffDeg, -step, step);
}

TurnRule ParseTurnRule(std::string_view text, TurnRule fallback) {
    std::string lower;
    for (char const c : text) {
        if (c != ' ' && c != '\t') {
            lower.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
        }
    }
    if (lower == "roles" || lower == "role" || lower == "parts") return TurnRule::Roles;
    if (lower == "look" || lower == "lookat" || lower == "attention") return TurnRule::Look;
    if (lower == "always" || lower == "pivot") return TurnRule::Always;
    if (lower == "off" || lower == "0" || lower == "game") return TurnRule::Off;
    return fallback;
}

char const* Name(Role role) {
    return role == Role::Speaker ? "speaker" : role == Role::Addressed ? "spoken to" : "bystander";
}

Role RoleOf(std::uint64_t who, std::uint64_t speaker, std::uint64_t listener) {
    if (who != 0 && who == speaker) {
        return Role::Speaker;
    }
    if (who != 0 && who == listener) {
        return Role::Addressed;
    }
    return Role::Bystander;
}

std::uint64_t PartnerOf(std::uint64_t who, std::uint64_t speaker, std::uint64_t listener) {
    if (who == 0 || speaker == 0) {
        return 0;
    }
    std::uint64_t const partner = who == speaker ? listener : speaker;
    return partner == who ? 0 : partner;
}

RoleAttention AttendAs(RoleState& state, Role role, std::uint64_t who, std::optional<double> partnerOffDeg, bool bodyCanTurn,
    RoleLimits const& limits) {
    RoleAttention out;
    if (!partnerOffDeg) {
        state.glancing = false;
        return out;
    }
    double const off = std::abs(Wrap180(*partnerOffDeg));
    if (role != Role::Bystander) {
        state.glancing = false;
        out.bodyMayTurn = bodyCanTurn;
        out.look = bodyCanTurn || off <= limits.pairReachDeg;
        out.delayS = std::max(0.0, limits.pairDelayS);
        return out;
    }
    double const enter = std::max(0.0, limits.glanceDeg);
    double const keep = std::max(enter, limits.glanceKeepDeg);
    state.glancing = off <= (state.glancing ? keep : enter);
    out.look = state.glancing;
    // Not in unison: each bystander by who they are (a few bits of the id, spread over bystanderSpreadS).
    std::uint64_t const mixed = (who ^ (who >> 17)) * 0x9E3779B97F4A7C15ull;
    double const share = static_cast<double>((mixed >> 40) & 0xFF) / 255.0;
    out.delayS = std::max(0.0, limits.bystanderDelayS) + std::max(0.0, limits.bystanderSpreadS) * share;
    return out;
}

double HeadShortDeg(double offDeg, std::span<LookLink const> links) {
    double left = std::abs(Wrap180(offDeg));
    for (LookLink const& link : links) {
        left -= std::clamp(link.strength, 0.0, 1.0) * std::min(left, std::max(0.0, link.limitDeg));
    }
    return left;
}

double StrongerLink(double strength, double gain) {
    double const s = std::clamp(strength, 0.0, 1.0);
    return s + (1.0 - s) * std::clamp(gain, 0.0, 1.0);
}

VoiceAt PlaceVoice(VoiceState& state, Vec3 const& listener, Vec3 const& head, VoiceLimits const& limits) {
    VoiceAt out;
    Vec3 const to{head.x - listener.x, head.y - listener.y, head.z - listener.z};
    double const d = std::hypot(to.x, to.y, to.z);
    out.distanceUnits = d;
    if (!std::isfinite(d)) {
        state.placed = false;
        return out;
    }
    double const keep = std::max(limits.withinUnits, limits.keepUnits);
    state.placed = d <= (state.placed ? keep : limits.withinUnits);
    out.placed = state.placed;
    if (!out.placed) {
        return out;
    }
    out.at = head;
    double const full = std::max(1.0, limits.nearUnits);
    if (d > full && limits.falloffDb > 0.0) {
        double const quieter = std::min(limits.falloffDb * std::log2(d / full), std::max(0.0, limits.floorDb));
        out.gain = std::pow(10.0, -quieter / 20.0);
    }
    return out;
}

bool StickWalks(StickLatch& latch, bool wheelUp, double magnitude, double releasedBelow) {
    if (wheelUp) {
        latch.held = true;
        return false;
    }
    if (latch.held) {
        if (magnitude < releasedBelow) {
            latch.held = false;
        }
        return false;
    }
    return true;
}

Covered CoveredChanges(std::span<Seen const> before, std::span<Seen const> after, double movedUnits) {
    Covered covered;
    for (Seen const& was : before) {
        auto const now = std::find_if(after.begin(), after.end(), [&](Seen const& s) { return s.id == was.id; });
        if (now == after.end()) {
            ++covered.gone;
        } else if (std::hypot(now->at.x - was.at.x, now->at.y - was.at.y, now->at.z - was.at.z) > movedUnits) {
            ++covered.moved;
        }
    }
    for (Seen const& now : after) {
        if (std::none_of(before.begin(), before.end(), [&](Seen const& s) { return s.id == now.id; })) {
            ++covered.appeared;
        }
    }
    return covered;
}

double NearestChange(std::span<Seen const> before, std::span<Seen const> after, Vec3 const& eye, double movedUnits) {
    double nearest = std::numeric_limits<double>::infinity();
    auto const from = [&](Vec3 const& at) { return std::hypot(at.x - eye.x, at.y - eye.y, at.z - eye.z); };
    for (Seen const& was : before) {
        auto const now = std::find_if(after.begin(), after.end(), [&](Seen const& s) { return s.id == was.id; });
        if (now == after.end() || std::hypot(now->at.x - was.at.x, now->at.y - was.at.y, now->at.z - was.at.z) > movedUnits) {
            nearest = (std::min)(nearest, from(was.at));
            if (now != after.end()) {
                nearest = (std::min)(nearest, from(now->at));  // moved: where they stand now counts too
            }
        }
    }
    for (Seen const& now : after) {
        if (std::none_of(before.begin(), before.end(), [&](Seen const& s) { return s.id == now.id; })) {
            nearest = (std::min)(nearest, from(now.at));
        }
    }
    return nearest;
}

std::optional<Vec3> TrailTarget(std::span<Vec3 const> trail, Vec3 const& at, double aheadUnits) {
    if (trail.empty()) {
        return std::nullopt;
    }
    std::size_t nearest = 0;
    double best = -1.0;
    for (std::size_t i = 0; i < trail.size(); ++i) {
        double const d = std::hypot(trail[i].x - at.x, trail[i].y - at.y);
        if (best < 0.0 || d <= best) {  // (<=: of two equally near, the later one: never back along the trail)
            best = d;
            nearest = i;
        }
    }
    double along = 0.0;
    for (std::size_t i = nearest; i + 1 < trail.size(); ++i) {
        along += std::hypot(trail[i + 1].x - trail[i].x, trail[i + 1].y - trail[i].y);
        if (along >= aheadUnits) {
            return trail[i + 1];
        }
    }
    return std::nullopt;
}

WalkInWay DecideWalkIn(WalkInFacts const& facts, WalkInRule rule) {
    if (rule == WalkInRule::Body) {
        return WalkInWay::Body;
    }
    if (rule == WalkInRule::View) {
        return WalkInWay::View;
    }
    return facts.midMoment || facts.markBehindTrigger || facts.putRefused ? WalkInWay::View : WalkInWay::Body;
}

WalkInRule ParseWalkInRule(std::string_view text, WalkInRule fallback) {
    std::string lower;
    for (char const c : text) {
        if (c != ' ' && c != '\t') {
            lower.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
        }
    }
    if (lower == "auto") return WalkInRule::Auto;
    if (lower == "body" || lower == "pawn") return WalkInRule::Body;
    if (lower == "view") return WalkInRule::View;
    return fallback;
}

char const* Name(WalkInWay way) {
    return way == WalkInWay::View ? "view" : "body";
}

Guide StepGuide(GuideState& state, bool wanted, bool stickUsed, bool progressed, double dtS, double patienceS, double blockedMaxS) {
    if (!wanted) {
        state = {};
        return Guide::Wait;
    }
    if (stickUsed) {
        state = {};  // his own walk: the wait starts again, and a guide that gave up may try once more after it
        return Guide::Wait;
    }
    if (patienceS < 0.0) {
        return Guide::Wait;
    }
    if (state.gaveUp) {
        return Guide::GaveUp;
    }
    double const dt = dtS > 0.0 ? dtS : 0.0;
    if (!state.guiding) {
        state.idleS += dt;
        if (state.idleS < patienceS) {
            return Guide::Wait;
        }
        state.guiding = true;
        state.blockedS = 0.0;
        return Guide::Walk;
    }
    state.blockedS = progressed ? 0.0 : state.blockedS + dt;
    if (state.blockedS > blockedMaxS) {
        state.guiding = false;
        state.gaveUp = true;
        return Guide::GaveUp;
    }
    return Guide::Walk;
}

bool Insists(Insistence& state, Vec3 const& wanted, double movedUnits, InsistLimits const& limits) {
    if (movedUnits > limits.maxUnits) {
        state.count = 0;
        return false;
    }
    double const dx = wanted.x - state.at.x;
    double const dy = wanted.y - state.at.y;
    double const dz = wanted.z - state.at.z;
    bool const same = state.count > 0 && std::sqrt(dx * dx + dy * dy + dz * dz) <= limits.sameUnits;
    state.count = same ? state.count + 1 : 1;
    state.at = wanted;
    return state.count >= limits.ticks;
}

bool HoldStart(std::vector<HeldStart>& held, std::uint64_t id, std::uint64_t nowMs) {
    for (HeldStart& h : held) {
        if (h.id == id) {
            ++h.times;
            return false;
        }
    }
    if (id == 0 || held.size() >= 8) {
        return false;
    }
    held.push_back({id, nowMs, 1});
    return true;
}

std::optional<std::uint64_t> ReleaseStart(std::vector<HeldStart>& held, bool free, std::uint64_t nowMs, std::uint64_t maxAgeMs,
    std::vector<std::uint64_t>* expired) {
    for (auto it = held.begin(); it != held.end();) {
        if (maxAgeMs != 0 && nowMs - it->heldMs > maxAgeMs) {
            if (expired != nullptr) {
                expired->push_back(it->id);
            }
            it = held.erase(it);
        } else {
            ++it;
        }
    }
    if (!free || held.empty()) {
        return std::nullopt;
    }
    std::uint64_t const id = held.front().id;
    held.erase(held.begin());
    return id;
}

ClearStep KeepClear(Vec3 const& place, Vec3 const& next, std::span<Vec3 const> people, double clearUnits) {
    ClearStep out;
    out.place = next;
    if (clearUnits <= 0.0) {
        out.moved = std::hypot(next.x - place.x, next.y - place.y) >= 1e-6;
        return out;
    }
    // Each person's room: the clearance, or for someone already nearer (they walked up, or the squad's formation stood them
    // there) the distance they stand at now: a step away is taken as it is, a step nearer slides round them at that distance
    // (it used to be refused whole: Zabaleta's wander with Tali at the clearance's edge moved 0.24 m in 3 s, 2026-11-12).
    auto const room = [&](Vec3 const& p) { return std::min(clearUnits, std::hypot(place.x - p.x, place.y - p.y)); };
    for (int pass = 0; pass < 4; ++pass) {
        Vec3 const* nearest = nullptr;
        double nearestDist = 0.0;
        double deepest = 1e-6;
        for (Vec3 const& p : people) {
            double const d = std::hypot(out.place.x - p.x, out.place.y - p.y);
            if (room(p) - d > deepest) {
                nearest = &p;
                nearestDist = d;
                deepest = room(p) - d;
            }
        }
        if (nearest == nullptr) {
            break;
        }
        out.held = true;
        // Push the step's end out to the person's room along the line from them; the step's part around them stays.
        double ox = out.place.x - nearest->x, oy = out.place.y - nearest->y;
        if (nearestDist < 1e-6) {
            ox = place.x - nearest->x;  // the step landed on the person: back along the way it came
            oy = place.y - nearest->y;
        }
        double const len = std::hypot(ox, oy);
        if (len < 1e-6) {
            out.place = place;
            break;
        }
        double const r = room(*nearest);
        out.place.x = nearest->x + ox / len * r;
        out.place.y = nearest->y + oy / len * r;
    }
    // Still within someone's room after the passes (two people closer together than two clearances: no way between them): the
    // step is held where it started.
    for (Vec3 const& p : people) {
        double const d = std::hypot(out.place.x - p.x, out.place.y - p.y);
        if (d < room(p) - 1e-6) {
            out.place = place;
            out.held = true;
            break;
        }
        out.held = out.held || d < clearUnits - 1e-6;
    }
    out.moved = std::hypot(out.place.x - place.x, out.place.y - place.y) >= 1e-6;
    return out;
}

char const* Name(Mode mode) {
    switch (mode) {
    case Mode::Vr:
        return "vr";
    case Mode::Screen:
        return "screen";
    case Mode::World:
        return "world";
    }
    return "?";
}

interact::Rect DefaultSubtitleCrop(std::uint32_t width, std::uint32_t height) {
    // The line across the stage, its bottom row at stage y 564..575 (measured), up to three rows above it (a long line wraps
    // upward from SubtitleConversation's anchor at 605.6); the wheel's top row starts at 590.
    return StageBand(width, height, 80.0f, 1200.0f, 480.0f, 586.0f);
}

interact::Rect DefaultWheelCrop(std::uint32_t width, std::uint32_t height) {
    // The ring centred on (640, 643), its rows at stage y 590..680 (measured). The replies' text runs from the ring's arms outward,
    // about 9.6 stage px a character: "The Council won't help us." began at 738 and ended past 970 (the owner's headset run,
    // 2026-11-11: the band then ended at 950 and the rest, "us.", stayed on the HUD panel). 540 px to each side holds a reply of
    // about 45 characters.
    return StageBand(width, height, 100.0f, 1180.0f, 586.0f, 712.0f);
}

float WheelQuadWidth(float wheelWidthM, float cropWidth) {
    // wheelWidthM is the width of the ring's 620 stage px (330..950, the band until 2026-11-11): the text keeps its size however
    // wide the band is.
    constexpr float kRingBand = 620.0f / kStageWidth;
    if (!(cropWidth > 0.0f) || !(wheelWidthM > 0.0f)) {
        return 0.0f;
    }
    return wheelWidthM * cropWidth / kRingBand;
}

bool SubtitlePoint(XrVector3f const& face, XrVector3f const& eyes, float belowM, float towardM, XrVector3f& out) {
    XrVector3f const to{eyes.x - face.x, 0.0f, eyes.z - face.z};  // level: the line stays upright under the face
    float const d = std::sqrt(to.x * to.x + to.z * to.z);
    if (!(d > 0.05f)) {
        return false;
    }
    float const toward = std::clamp(towardM, 0.0f, d * 0.5f);
    out = {face.x + to.x / d * toward, face.y - std::max(0.0f, belowM), face.z + to.z / d * toward};
    return true;
}

Follow FollowHead(Vec3 const& place, Vec3 const& anchor, Vec3 const& headNow, double threshold) {
    double const dx = headNow.x - anchor.x;
    double const dy = headNow.y - anchor.y;
    double const dz = headNow.z - anchor.z;
    if (!(std::sqrt(dx * dx + dy * dy + dz * dz) > threshold)) {
        return {place, anchor, false};
    }
    return {{place.x + dx, place.y + dy, place.z + dz}, headNow, true};
}

bool FollowHeadSettled(FollowState& state, Vec3 const& headNow, double threshold, int settle) {
    Follow const f = FollowHead(state.place, state.anchor, headNow, threshold);
    if (!f.jumped) {
        state.away = 0;
        return false;
    }
    if (++state.away < std::max(1, settle)) {
        return false;
    }
    state.place = f.place;
    state.anchor = f.anchor;
    state.away = 0;
    return true;
}

HeadFollow HeadFollowFor(bool settling, bool viewWalksToHim, bool openedWithoutCut, double moveUnits, double stayMaxUnits) {
    if (settling || viewWalksToHim) {
        return HeadFollow::Anchor;
    }
    if (openedWithoutCut && moveUnits < stayMaxUnits) {
        return HeadFollow::Anchor;
    }
    return HeadFollow::Follow;
}

namespace {
double MarkDistance(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
} // namespace

void MarkWalk::Jump(Vec3 wanted, Vec3 standing, double nowS, double minUnits) {
    wanted_ = wanted;
    wantedAtS_ = nowS;
    phase_ = MarkDistance(wanted, standing) > std::max(0.0, minUnits) ? WalkPhase::Waiting : WalkPhase::Idle;
}

bool MarkWalk::StartIfSettled(Vec3 standing, double nowS, double settleS, double minUnits) {
    if (phase_ != WalkPhase::Waiting) {
        return false;
    }
    if (!(MarkDistance(wanted_, standing) > std::max(0.0, minUnits))) {
        End();
        return false;
    }
    if (nowS - wantedAtS_ < std::max(0.0, settleS)) {
        return false;
    }
    phase_ = WalkPhase::Walking;
    return true;
}

double BeyondWalk(Vec3 standing, Vec3 actual, Vec3 mark, double speedUnitsPerS, double elapsedS) {
    Vec3 const end = StepTowardMark(standing, mark, speedUnitsPerS, elapsedS).place;
    Vec3 const delta{end.x - standing.x, end.y - standing.y, end.z - standing.z};
    double const lengthSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
    double const along = lengthSq > 1e-12 ? std::clamp(((actual.x - standing.x) * delta.x +
        (actual.y - standing.y) * delta.y + (actual.z - standing.z) * delta.z) / lengthSq, 0.0, 1.0) : 0.0;
    return MarkDistance(actual, {standing.x + along * delta.x, standing.y + along * delta.y, standing.z + along * delta.z});
}

bool MayReface(double playerFromMarkUnits, double headAboveLocationUnits, double atMarkUnits, double seatedHeadUnits) {
    return playerFromMarkUnits > atMarkUnits && headAboveLocationUnits >= seatedHeadUnits;
}

bool ShowBarkSubtitle(BarkSubtitleState& state, bool enabled, int subtitlesOption, std::uint64_t textHash, double nowS, double everyS) {
    if (!enabled || subtitlesOption != 0 || textHash == 0) {
        state = {};
        return false;
    }
    if (state.text != textHash || state.shownS < 0.0 || nowS - state.shownS >= everyS || nowS < state.shownS) {
        state.text = textHash;
        state.shownS = nowS;
        return true;
    }
    return false;
}

int AiEnableDeficit(int counter) {
    return counter > 0 ? 0 : 1 - counter;
}

bool WalkWorthIt(double distanceUnits, double offFacingDeg, double longUnits, double stepOffDeg) {
    return distanceUnits >= longUnits || std::abs(Wrap180(offFacingDeg)) <= stepOffDeg;
}

WalkStep StepTowardMark(Vec3 standing, Vec3 mark, double speedUnitsPerS, double dtS) {
    double const distance = MarkDistance(standing, mark);
    if (distance <= 1e-6) {
        return {mark, {}, true};
    }
    if (!(dtS > 0.0) || !(speedUnitsPerS > 0.0)) {
        return {standing, {}, false};
    }
    double const fraction = std::min(1.0, speedUnitsPerS * dtS / distance);
    Vec3 const delta{(mark.x - standing.x) * fraction, (mark.y - standing.y) * fraction, (mark.z - standing.z) * fraction};
    return {{standing.x + delta.x, standing.y + delta.y, standing.z + delta.z},
            {delta.x / dtS, delta.y / dtS, delta.z / dtS}, fraction == 1.0};
}

} // namespace mevr::vr::conversation_world
