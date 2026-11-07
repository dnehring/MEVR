// SPDX-License-Identifier: MIT
#include "core/vr/SquadContext.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <numbers>

namespace mevr::vr::squad_context {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

float Length(Vec3 const& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 Sub(Vec3 const& a, Vec3 const& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Dot(Vec3 const& a, Vec3 const& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float WrapDeg(float deg) {
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}
std::string_view Trim(std::string_view text) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    return text;
}
std::string Upper(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string Lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
// A score as the block shows it: .81 (two decimals, no leading zero), 1.0 for one.
std::string ScoreText(float score) {
    if (score >= 0.995f) return "1.0";
    std::string s = std::format("{:.2f}", score);
    return s.substr(1);
}
std::string Seconds(float s) { return std::format("{}s", static_cast<int>(std::lround(s))); }

}  // namespace

float Distance(Vec3 const& a, Vec3 const& b) { return Length(Sub(a, b)); }

float AngleTo(Ray const& ray, Vec3 const& point) {
    Vec3 const to = Sub(point, ray.origin);
    float const lt = Length(to);
    float const ld = Length(ray.direction);
    if (lt < 1e-3f || ld < 1e-6f) return 180.0f;
    float const c = std::clamp(Dot(to, ray.direction) / (lt * ld), -1.0f, 1.0f);
    return std::acos(c) * 180.0f / kPi;
}

float BearingDeg(Vec3 const& from, float yawDeg, Vec3 const& to) {
    float const dx = to.x - from.x;
    float const dy = to.y - from.y;
    if (std::abs(dx) < 1e-4f && std::abs(dy) < 1e-4f) return 0.0f;
    return WrapDeg(std::atan2(dy, dx) * 180.0f / kPi - yawDeg);
}

char const* WeaponCode(int weapon) {
    switch (weapon) {
    case 0: return "PI";
    case 1: return "SG";
    case 2: return "AR";
    case 3: return "SR";
    default: return "-";
    }
}

std::optional<int> WeaponFromCode(std::string_view code) {
    for (int w = 0; w < kWeapons; ++w) {
        if (code == WeaponCode(w)) return w;
    }
    return std::nullopt;
}

char const* Name(Sector sector) {
    switch (sector) {
    case Sector::Ahead: return "ahead";
    case Sector::Left: return "left";
    case Sector::Right: return "right";
    case Sector::Behind: return "behind";
    }
    return "ahead";
}
char const* Name(Band band) {
    switch (band) {
    case Band::Near: return "near";
    case Band::Mid: return "mid";
    case Band::Far: return "far";
    }
    return "near";
}
char const* Name(Elevation elevation) {
    switch (elevation) {
    case Elevation::Level: return "level";
    case Elevation::Above: return "above";
    case Elevation::Below: return "below";
    }
    return "level";
}

int Count(Group const& group) { return static_cast<int>(group.members.size()); }

// ---- Groups -----------------------------------------------------------------------------------------------------------

std::vector<Group> const& Grouper::Update(Scene const& scene, GroupSettings const& s) {
    // 1. Clusters of the living, by single linkage: close enough and on the same level.
    std::vector<Hostile const*> alive;
    for (Hostile const& h : scene.hostiles) {
        if (h.id != 0 && h.health > 0.0f) alive.push_back(&h);
    }
    std::vector<int> cluster(alive.size(), -1);
    int clusters = 0;
    for (std::size_t i = 0; i < alive.size(); ++i) {
        if (cluster[i] >= 0) continue;
        cluster[i] = clusters++;
        std::vector<std::size_t> stack{i};
        while (!stack.empty()) {
            std::size_t const a = stack.back();
            stack.pop_back();
            for (std::size_t b = 0; b < alive.size(); ++b) {
                if (cluster[b] >= 0) continue;
                if (Distance(alive[a]->position, alive[b]->position) <= s.joinM &&
                    std::abs(alive[a]->position.z - alive[b]->position.z) <= s.splitAboveM) {
                    cluster[b] = cluster[a];
                    stack.push_back(b);
                }
            }
        }
    }
    // 2. One fresh group per cluster.
    std::vector<Group> fresh(static_cast<std::size_t>(clusters));
    for (std::size_t i = 0; i < alive.size(); ++i) {
        Hostile const& h = *alive[i];
        Group& g = fresh[static_cast<std::size_t>(cluster[i])];
        g.members.push_back(h.id);
        g.centre.x += h.position.x;
        g.centre.y += h.position.y;
        g.centre.z += h.position.z;
        if (g.surface.empty()) g.surface = h.surface;
        if (g.members.size() == 1) g.visible = h.visible; else g.visible = g.visible || h.visible;
    }
    for (Group& g : fresh) {
        float const n = static_cast<float>(g.members.size());
        g.centre = {g.centre.x / n, g.centre.y / n, g.centre.z / n};
        // The most common type word among the members.
        std::vector<std::pair<std::string, int>> types;
        for (PawnId id : g.members) {
            for (Hostile const* h : alive) {
                if (h->id != id || h->type.empty()) continue;
                auto it = std::find_if(types.begin(), types.end(), [&](auto const& t) { return t.first == h->type; });
                if (it == types.end()) types.emplace_back(h->type, 1); else ++it->second;
            }
        }
        for (auto const& t : types) {
            if (g.type.empty() || t.second > std::find_if(types.begin(), types.end(), [&](auto const& u) { return u.first == g.type; })->second) {
                g.type = t.first;
            }
        }
        g.distanceM = Distance(scene.player, g.centre);
        g.bearingDeg = BearingDeg(scene.player, scene.headYawDeg, g.centre);
        g.heightM = g.centre.z - scene.player.z;
    }
    // 3. The nearest maxGroups.
    std::sort(fresh.begin(), fresh.end(), [](Group const& a, Group const& b) { return a.distanceM < b.distanceM; });
    if (s.maxGroups > 0 && fresh.size() > static_cast<std::size_t>(s.maxGroups)) {
        fresh.resize(static_cast<std::size_t>(s.maxGroups));
    }
    // 4. Left to right, then the ids: an old group's id where most members are shared, else the next unused.
    std::sort(fresh.begin(), fresh.end(), [](Group const& a, Group const& b) { return a.bearingDeg < b.bearingDeg; });
    struct Overlap {
        std::size_t fresh, old, shared;
    };
    std::vector<Overlap> overlaps;
    for (std::size_t f = 0; f < fresh.size(); ++f) {
        for (std::size_t o = 0; o < groups_.size(); ++o) {
            std::size_t shared = 0;
            for (PawnId id : fresh[f].members) {
                if (std::find(groups_[o].members.begin(), groups_[o].members.end(), id) != groups_[o].members.end()) ++shared;
            }
            if (shared > 0) overlaps.push_back({f, o, shared});
        }
    }
    std::stable_sort(overlaps.begin(), overlaps.end(), [&](Overlap const& a, Overlap const& b) {
        if (a.shared != b.shared) return a.shared > b.shared;
        return groups_[a.old].id < groups_[b.old].id;
    });
    std::vector<bool> oldUsed(groups_.size(), false);
    for (Overlap const& ov : overlaps) {
        if (fresh[ov.fresh].id != 0 || oldUsed[ov.old]) continue;
        fresh[ov.fresh].id = groups_[ov.old].id;
        oldUsed[ov.old] = true;
    }
    for (Group& g : fresh) {
        if (g.id == 0) g.id = nextId_++;
    }
    // 5. The words.
    for (Group& g : fresh) {
        float const ab = std::abs(g.bearingDeg);
        g.sector = ab <= s.aheadDeg ? Sector::Ahead : ab >= s.behindDeg ? Sector::Behind : g.bearingDeg < 0.0f ? Sector::Left : Sector::Right;
        g.band = g.distanceM < s.nearM ? Band::Near : g.distanceM > s.farM ? Band::Far : Band::Mid;
        g.elevation = g.heightM > s.levelM ? Elevation::Above : g.heightM < -s.levelM ? Elevation::Below : Elevation::Level;
    }
    if (fresh.size() >= 2) {
        auto const by = [&](auto key, bool Group::*flag, bool least) {
            std::size_t best = 0;
            for (std::size_t i = 1; i < fresh.size(); ++i) {
                if (least ? key(fresh[i]) < key(fresh[best]) : key(fresh[i]) > key(fresh[best])) best = i;
            }
            fresh[best].*flag = true;
        };
        by([](Group const& g) { return g.distanceM; }, &Group::nearest, true);
        by([](Group const& g) { return g.distanceM; }, &Group::farthest, false);
        by([](Group const& g) { return g.bearingDeg; }, &Group::leftmost, true);
        by([](Group const& g) { return g.bearingDeg; }, &Group::rightmost, false);
        by([](Group const& g) { return g.heightM; }, &Group::highest, false);
        // Biggest only when one group is strictly the largest.
        std::size_t big = 0;
        bool tie = false;
        for (std::size_t i = 1; i < fresh.size(); ++i) {
            if (fresh[i].members.size() > fresh[big].members.size()) { big = i; tie = false; }
            else if (fresh[i].members.size() == fresh[big].members.size()) tie = true;
        }
        if (!tie) fresh[big].biggest = true;
    }
    groups_ = std::move(fresh);
    return groups_;
}

void Grouper::Reset() {
    groups_.clear();
    nextId_ = 1;
}

Group const* Find(std::vector<Group> const& groups, int id) {
    for (Group const& g : groups) {
        if (g.id == id) return &g;
    }
    return nullptr;
}

int GroupOf(std::vector<Group> const& groups, PawnId pawn) {
    if (pawn == 0) return 0;
    for (Group const& g : groups) {
        if (std::find(g.members.begin(), g.members.end(), pawn) != g.members.end()) return g.id;
    }
    return 0;
}

// ---- Landmarks ----------------------------------------------------------------------------------------------------------

std::vector<Landmark> const& Landmarker::Update(Scene const& scene, LandmarkSettings const& s) {
    std::vector<Landmark> fresh;
    for (Cover const& c : scene.cover) {
        if (c.id == 0) continue;
        Landmark l;
        l.cover = c.id;
        l.yawDeg = c.yawDeg;
        l.slots = c.slots;
        l.type = c.type.empty() ? "cover" : c.type;
        if (c.slots.empty()) {
            l.centre = c.position;
        } else {
            for (Vec3 const& p : c.slots) {
                l.centre.x += p.x;
                l.centre.y += p.y;
                l.centre.z += p.z;
            }
            float const n = static_cast<float>(c.slots.size());
            l.centre = {l.centre.x / n, l.centre.y / n, l.centre.z / n};
        }
        l.distanceM = Distance(scene.player, l.centre);
        if (l.distanceM > s.rangeM) continue;
        l.bearingDeg = BearingDeg(scene.player, scene.headYawDeg, l.centre);
        l.heightM = l.centre.z - scene.player.z;
        fresh.push_back(std::move(l));
    }
    std::sort(fresh.begin(), fresh.end(), [](Landmark const& a, Landmark const& b) { return a.distanceM < b.distanceM; });
    if (s.maxLandmarks > 0 && fresh.size() > static_cast<std::size_t>(s.maxLandmarks)) fresh.resize(static_cast<std::size_t>(s.maxLandmarks));
    std::sort(fresh.begin(), fresh.end(), [](Landmark const& a, Landmark const& b) { return a.bearingDeg < b.bearingDeg; });
    for (Landmark& l : fresh) {
        auto const known = std::find_if(ids_.begin(), ids_.end(), [&](auto const& p) { return p.first == l.cover; });
        if (known != ids_.end()) {
            l.id = known->second;
        } else {
            l.id = nextId_++;
            ids_.emplace_back(l.cover, l.id);
        }
        float const ab = std::abs(l.bearingDeg);
        l.sector = ab <= s.aheadDeg ? Sector::Ahead : ab >= s.behindDeg ? Sector::Behind : l.bearingDeg < 0.0f ? Sector::Left : Sector::Right;
        l.band = l.distanceM < s.nearM ? Band::Near : l.distanceM > s.farM ? Band::Far : Band::Mid;
        l.elevation = l.heightM > s.levelM ? Elevation::Above : l.heightM < -s.levelM ? Elevation::Below : Elevation::Level;
    }
    if (fresh.size() >= 2) {
        auto const by = [&](auto key, bool Landmark::*flag, bool least) {
            std::size_t best = 0;
            for (std::size_t i = 1; i < fresh.size(); ++i) {
                if (least ? key(fresh[i]) < key(fresh[best]) : key(fresh[i]) > key(fresh[best])) best = i;
            }
            fresh[best].*flag = true;
        };
        by([](Landmark const& l) { return l.distanceM; }, &Landmark::nearest, true);
        by([](Landmark const& l) { return l.bearingDeg; }, &Landmark::leftmost, true);
        by([](Landmark const& l) { return l.bearingDeg; }, &Landmark::rightmost, false);
    }
    landmarks_ = std::move(fresh);
    return landmarks_;
}

void Landmarker::Reset() {
    landmarks_.clear();
    ids_.clear();
    nextId_ = 1;
}

Landmark const* Find(std::vector<Landmark> const& landmarks, int id) {
    for (Landmark const& l : landmarks) {
        if (l.id == id) return &l;
    }
    return nullptr;
}

Vec3 LandmarkPoint(Landmark const& landmark, std::vector<Group> const& groups, Scene const& scene) {
    // The threat: the nearest group's centre; none: the player stands in for it (the far side from Shepard is at least
    // the side the enemy would come from when the player faces them).
    Vec3 threat = scene.player;
    Group const* nearest = nullptr;
    for (Group const& g : groups) {
        if (nearest == nullptr || g.distanceM < nearest->distanceM) nearest = &g;
    }
    if (nearest != nullptr) threat = nearest->centre;
    if (landmark.slots.empty()) {
        // 1 m behind the link's front: the front faces the enemy's side.
        float const r = landmark.yawDeg * kPi / 180.0f;
        Vec3 const behind{landmark.centre.x - std::cos(r), landmark.centre.y - std::sin(r), landmark.centre.z};
        Vec3 const toward{landmark.centre.x + std::cos(r), landmark.centre.y + std::sin(r), landmark.centre.z};
        return Distance(behind, threat) >= Distance(toward, threat) ? behind : toward;
    }
    Vec3 best = landmark.slots[0];
    for (Vec3 const& p : landmark.slots) {
        if (Distance(p, threat) > Distance(best, threat)) best = p;
    }
    return best;
}

// ---- Deixis -------------------------------------------------------------------------------------------------------------

void PoseHistory::Push(PoseSample const& sample) {
    samples_.push_back(sample);
    while (!samples_.empty() && samples_.front().time < sample.time - keepS_) samples_.pop_front();
}

void PoseHistory::Between(double from, double to, std::vector<PoseSample>& out) const {
    out.clear();
    for (PoseSample const& s : samples_) {
        if (s.time >= from && s.time <= to) out.push_back(s);
    }
}

std::vector<Candidate> Resolve(PoseHistory const& history, double wordTime, std::vector<Group> const& groups,
    Scene const& scene, DeixisSettings const& st) {
    return Resolve(history, wordTime, groups, {}, scene, st);
}

std::vector<Candidate> Resolve(PoseHistory const& history, double wordTime, std::vector<Group> const& groups,
    std::vector<Landmark> const& landmarks, Scene const& scene, DeixisSettings const& st) {
    if (groups.empty() && landmarks.empty()) return {};
    // The things a ray can mean: the groups (their members' places), then the landmarks (their slots and centre).
    std::size_t const things = groups.size() + landmarks.size();
    std::vector<std::vector<Vec3>> positions(things);
    for (std::size_t g = 0; g < groups.size(); ++g) {
        for (PawnId id : groups[g].members) {
            for (Hostile const& h : scene.hostiles) {
                if (h.id == id) positions[g].push_back(h.position);
            }
        }
    }
    for (std::size_t l = 0; l < landmarks.size(); ++l) {
        positions[groups.size() + l] = landmarks[l].slots;
        positions[groups.size() + l].push_back(landmarks[l].centre);
    }
    struct Source {
        int kind;  // 0 head, 1 left hand, 2 right hand
        float peak, before, after, sigma, upBias, weight;
    };
    Source const sources[3] = {
        {0, st.headPeakS, st.headBeforeS, st.headAfterS, st.headSigmaDeg, st.headUpBiasDeg, st.headWeight},
        {1, st.handPeakS, st.handBeforeS, st.handAfterS, st.handSigmaDeg, 0.0f, st.handWeight},
        {2, st.handPeakS, st.handBeforeS, st.handAfterS, st.handSigmaDeg, 0.0f, st.handWeight},
    };
    float const maxWeight = std::max(st.headWeight, st.handWeight);
    std::vector<float> best(things, 0.0f);
    std::vector<PoseSample> samples;
    for (Source const& src : sources) {
        history.Between(wordTime - src.before, wordTime + src.after, samples);
        std::vector<float> sum(things, 0.0f);
        float weightSum = 0.0f;
        for (PoseSample const& sample : samples) {
            Ray ray = src.kind == 0 ? sample.head : src.kind == 1 ? sample.leftAim : sample.rightAim;
            bool const valid = src.kind == 0 ? Length(ray.direction) > 0.0f : src.kind == 1 ? sample.leftValid : sample.rightValid;
            if (!valid || Length(ray.direction) <= 0.0f) continue;
            float const dt = static_cast<float>(sample.time - wordTime);
            float wt = 0.0f;
            if (dt <= src.peak) {
                float const rise = src.peak + src.before;
                wt = rise > 0.0f ? (dt + src.before) / rise : 1.0f;
            } else {
                float const fall = src.after - src.peak;
                wt = fall > 0.0f ? (src.after - dt) / fall : 1.0f;
            }
            wt = std::clamp(wt, 0.0f, 1.0f);
            if (wt <= 0.0f) continue;
            if (src.upBias != 0.0f) {
                float const flat = std::sqrt(ray.direction.x * ray.direction.x + ray.direction.y * ray.direction.y);
                ray.direction.z += flat * std::tan(src.upBias * kPi / 180.0f);
            }
            weightSum += wt;
            for (std::size_t g = 0; g < things; ++g) {
                float angle = 180.0f;
                for (Vec3 const& p : positions[g]) angle = std::min(angle, AngleTo(ray, p));
                float const a = angle / src.sigma;
                sum[g] += wt * std::exp(-0.5f * a * a);
            }
        }
        if (weightSum <= 0.0f) continue;
        for (std::size_t g = 0; g < things; ++g) {
            best[g] = std::max(best[g], src.weight * sum[g] / weightSum / maxWeight);
        }
    }
    std::vector<Candidate> out;
    for (std::size_t g = 0; g < things; ++g) {
        if (best[g] < st.minScore) continue;
        Candidate c;
        c.score = best[g];
        if (g < groups.size()) c.group = groups[g].id; else c.landmark = landmarks[g - groups.size()].id;
        out.push_back(c);
    }
    std::stable_sort(out.begin(), out.end(), [](Candidate const& a, Candidate const& b) { return a.score > b.score; });
    if (st.top > 0 && out.size() > static_cast<std::size_t>(st.top)) out.resize(static_cast<std::size_t>(st.top));
    return out;
}

// ---- Actions ---------------------------------------------------------------------------------------------------------------

char const* Name(Order order) {
    switch (order) {
    case Order::None: return "-";
    case Order::Attack: return "attack";
    case Order::Move: return "move";
    case Order::Follow: return "follow";
    case Order::Hold: return "hold";
    case Order::Regroup: return "regroup";
    case Order::Use: return "use";
    case Order::Cancel: return "cancel";
    case Order::Repeat: return "repeat";
    }
    return "-";
}

std::optional<Order> ParseOrder(std::string_view text) {
    std::string const t = Lower(Trim(text));
    for (Order o : {Order::Attack, Order::Move, Order::Follow, Order::Hold, Order::Regroup, Order::Use, Order::Cancel, Order::Repeat}) {
        if (t == Name(o)) return o;
    }
    if (t == "-" || t == "none") return Order::None;
    return std::nullopt;
}

std::string Line(Action const& a) {
    std::string who = "-";
    switch (a.who.kind) {
    case Who::Member: who = std::string(1, a.who.letter); break;
    case Who::All: who = "ALL"; break;
    case Who::Last: who = "LAST"; break;
    case Who::None: break;
    }
    std::string target = "-";
    switch (a.target.kind) {
    case Target::Group: target = std::format("E{}", a.target.index); break;
    case Target::Pointed: target = std::format("PT{}", a.target.index); break;
    case Target::Aim: target = "AIM"; break;
    case Target::Gaze: target = "GAZE"; break;
    case Target::Last: target = "LAST"; break;
    case Target::Other: target = "OTHER"; break;
    case Target::Self: target = "SELF"; break;
    case Target::Direction: target = Upper(Name(static_cast<Sector>(a.target.index))); break;
    case Target::Landmark: target = std::format("L{}", a.target.index); break;
    case Target::None: break;
    }
    return std::format("{}|{}|{}|{}|{}", who, WeaponCode(a.weapon), a.power.empty() ? "-" : a.power, Name(a.order), target);
}

Vec3 DirectionPoint(Scene const& scene, Sector sector, float distanceM) {
    float yaw = scene.headYawDeg;
    switch (sector) {
    case Sector::Left: yaw -= 90.0f; break;
    case Sector::Right: yaw += 90.0f; break;
    case Sector::Behind: yaw += 180.0f; break;
    case Sector::Ahead: break;
    }
    float const r = yaw * 3.14159265f / 180.0f;
    return {scene.player.x + std::cos(r) * distanceM, scene.player.y + std::sin(r) * distanceM, scene.player.z};
}

std::optional<Action> ParseLine(std::string_view line) {
    std::string_view fields[5];
    int n = 0;
    std::string_view rest = Trim(line);
    while (n < 5) {
        std::size_t const bar = rest.find('|');
        fields[n++] = Trim(bar == std::string_view::npos ? rest : rest.substr(0, bar));
        if (bar == std::string_view::npos) break;
        rest = rest.substr(bar + 1);
    }
    if (n < 5) return std::nullopt;
    Action a;
    std::string const who = Upper(fields[0]);
    if (who == "-") a.who.kind = Who::None;
    else if (who == "ALL") a.who.kind = Who::All;
    else if (who == "LAST") a.who.kind = Who::Last;
    else if (who.size() == 1 && who[0] >= 'A' && who[0] <= 'Z') a.who = {Who::Member, who[0]};
    else return std::nullopt;
    std::string const weapon = Upper(fields[1]);
    if (weapon != "-") {
        auto const w = WeaponFromCode(weapon);
        if (!w) return std::nullopt;
        a.weapon = *w;
    }
    std::string const power = Upper(fields[2]);
    if (power != "-") {
        if (power.empty() || power.size() > 8) return std::nullopt;
        for (char c : power) {
            if (c < 'A' || c > 'Z') return std::nullopt;
        }
        a.power = power;
    }
    auto const order = ParseOrder(fields[3]);
    if (!order) return std::nullopt;
    a.order = *order;
    std::string const target = Upper(fields[4]);
    auto const number = [](std::string_view digits) -> std::optional<int> {
        if (digits.empty() || digits.size() > 4) return std::nullopt;
        int v = 0;
        for (char c : digits) {
            if (c < '0' || c > '9') return std::nullopt;
            v = v * 10 + (c - '0');
        }
        return v;
    };
    if (target == "-") a.target.kind = Target::None;
    else if (target == "AIM") a.target.kind = Target::Aim;
    else if (target == "GAZE") a.target.kind = Target::Gaze;
    else if (target == "LAST") a.target.kind = Target::Last;
    else if (target == "OTHER") a.target.kind = Target::Other;
    else if (target == "SELF") a.target.kind = Target::Self;
    else if (target == "AHEAD") a.target = {Target::Direction, static_cast<int>(Sector::Ahead)};
    else if (target == "LEFT") a.target = {Target::Direction, static_cast<int>(Sector::Left)};
    else if (target == "RIGHT") a.target = {Target::Direction, static_cast<int>(Sector::Right)};
    else if (target == "BEHIND") a.target = {Target::Direction, static_cast<int>(Sector::Behind)};
    else if (target.size() >= 2 && target[0] == 'E') {
        auto const v = number(std::string_view(target).substr(1));
        if (!v || *v <= 0) return std::nullopt;
        a.target = {Target::Group, *v};
    } else if (target.size() >= 3 && target[0] == 'P' && target[1] == 'T') {
        auto const v = number(std::string_view(target).substr(2));
        if (!v || *v <= 0) return std::nullopt;
        a.target = {Target::Pointed, *v};
    } else if (target.size() >= 2 && target[0] == 'L') {
        auto const v = number(std::string_view(target).substr(1));
        if (!v || *v <= 0) return std::nullopt;
        a.target = {Target::Landmark, *v};
    } else return std::nullopt;
    return a;
}

Resolved Validate(Action const& action, Scene const& scene, std::vector<Group> const& groups,
    std::vector<Deictic> const& deictics, std::vector<Command> const& history) {
    return Validate(action, scene, groups, {}, deictics, history);
}

Resolved Validate(Action const& action, Scene const& scene, std::vector<Group> const& groups, std::vector<Landmark> const& landmarks,
    std::vector<Deictic> const& deictics, std::vector<Command> const& history) {
    Resolved r;
    Action a = action;
    Command const* last = history.empty() ? nullptr : &history.back();
    // "Do it again": the last command over, for the same member unless another is named.
    if (a.order == Order::Repeat) {
        if (last == nullptr) {
            r.problem = "nothing to repeat";
            return r;
        }
        Who const who = a.who;
        a = last->action;
        if (who.kind != Who::None && who.kind != Who::Last) a.who = who;
        if (a.target.kind == Target::Pointed) a.target = {Target::Group, last->group};
    }
    // Who.
    switch (a.who.kind) {
    case Who::None:
    case Who::All: {
        bool any = false;
        for (Member const& m : scene.party) any = any || (m.present && m.alive);
        if (!any) {
            r.problem = "no squadmate is with you";
            return r;
        }
        r.all = true;
        break;
    }
    case Who::Last:
        if (last == nullptr || last->who == 0) {
            if (last == nullptr) {
                r.problem = "no earlier order to take the squadmate from";
                return r;
            }
            r.all = true;
            break;
        }
        a.who = {Who::Member, last->who};
        [[fallthrough]];
    case Who::Member: {
        Member const* m = nullptr;
        for (Member const& c : scene.party) {
            if (c.letter == a.who.letter) m = &c;
        }
        if (m == nullptr || !m->present) {
            r.problem = std::format("{} isn't with you", a.who.letter);
            return r;
        }
        if (!m->alive) {
            r.problem = std::format("{} is down", a.who.letter);
            return r;
        }
        r.who = a.who.letter;
        // The weapon and the power are theirs to have.
        if (a.weapon >= 0 && !(m->carried & (1u << a.weapon))) {
            r.problem = std::format("{} carries no {}", a.who.letter, WeaponCode(a.weapon));
            return r;
        }
        if (!a.power.empty()) {
            Power const* p = nullptr;
            for (Power const& c : m->powers) {
                if (c.code == a.power) p = &c;
            }
            if (p == nullptr) {
                r.problem = std::format("{} has no {}", a.who.letter, a.power);
                return r;
            }
            if (p->readyInS > 0.0f) {
                r.notReady = true;
                r.readyInS = p->readyInS;
            }
        }
        break;
    }
    }
    if (r.all && (a.weapon >= 0 || !a.power.empty())) {
        r.problem = "a weapon or a power needs one squadmate";
        return r;
    }
    r.weapon = a.weapon;
    r.power = a.power;
    r.order = a.order;
    if (a.order == Order::None && a.power.empty() && a.weapon < 0) {
        r.problem = "no order";
        return r;
    }
    if (a.order == Order::None) r.order = a.power.empty() ? Order::None : Order::Use;
    // The target.
    bool const wantsTarget = r.order == Order::Attack || r.order == Order::Use || r.order == Order::Move;
    auto const nearestOther = [&](int except, std::string const& type) {
        Group const* best = nullptr;
        for (Group const& g : groups) {
            if (g.id == except) continue;
            if (!type.empty() && g.type != type) continue;
            if (best == nullptr || g.distanceM < best->distanceM) best = &g;
        }
        return best == nullptr ? 0 : best->id;
    };
    switch (a.target.kind) {
    case Target::None:
        if (wantsTarget && r.order != Order::Move) {
            r.group = GroupOf(groups, scene.aimTarget);
            if (r.group == 0) r.group = GroupOf(groups, scene.gazeTarget);
        }
        break;
    case Target::Group:
        if (Find(groups, a.target.index) == nullptr) {
            r.problem = std::format("no group E{}", a.target.index);
            return r;
        }
        r.group = a.target.index;
        break;
    case Target::Pointed: {
        Deictic const* d = nullptr;
        for (Deictic const& c : deictics) {
            if (c.index == a.target.index) d = &c;
        }
        bool const place = r.order == Order::Move || r.order == Order::Hold;
        if (d == nullptr || d->candidates.empty()) {
            // A move or hold at a pointed spot needs no thing there: the dispatcher has the floor the ray hit.
            if (place) break;
            r.problem = std::format("PT{} points at nothing", a.target.index);
            return r;
        }
        // A move or hold takes the best thing pointed at, a group or a landmark; an attack the best group.
        Candidate const* chosen = nullptr;
        for (Candidate const& c : d->candidates) {
            if (c.group != 0 || (place && c.landmark != 0)) {
                chosen = &c;
                break;
            }
        }
        if (chosen == nullptr) {
            r.problem = std::format("PT{} points at cover, not an enemy", a.target.index);
            return r;
        }
        r.group = chosen->group;
        r.landmark = chosen->landmark;
        for (Candidate const& c : d->candidates) {
            if (&c != chosen && c.group != 0 && r.runnerUp == 0) r.runnerUp = c.group;
        }
        break;
    }
    case Target::Landmark:
        if (Find(landmarks, a.target.index) == nullptr) {
            r.problem = std::format("no cover L{}", a.target.index);
            return r;
        }
        if (r.order != Order::Move && r.order != Order::Hold) {
            r.problem = std::format("L{} is cover: a place to go, not a target", a.target.index);
            return r;
        }
        r.landmark = a.target.index;
        break;
    case Target::Aim:
        r.group = GroupOf(groups, scene.aimTarget);
        break;
    case Target::Gaze:
        r.group = GroupOf(groups, scene.gazeTarget);
        break;
    case Target::Last:
        if (last == nullptr || last->group == 0) {
            r.problem = "no earlier target";
            return r;
        }
        r.group = last->group;
        break;
    case Target::Other: {
        int const except = last == nullptr ? 0 : last->group;
        Group const* lastGroup = Find(groups, except);
        r.group = nearestOther(except, lastGroup == nullptr ? std::string() : lastGroup->type);
        if (r.group == 0) r.group = nearestOther(except, std::string());
        if (r.group == 0) {
            r.problem = "no other group";
            return r;
        }
        break;
    }
    case Target::Self:
        break;
    case Target::Direction: {
        // A move or hold that way needs no group (the dispatcher makes the point); an attack that way is the nearest group there.
        if (a.target.index < 0 || a.target.index > static_cast<int>(Sector::Behind)) {
            r.problem = "no such direction";
            return r;
        }
        if (r.order == Order::Attack || r.order == Order::Use) {
            Group const* best = nullptr;
            for (Group const& g : groups) {
                if (g.sector == static_cast<Sector>(a.target.index) && (best == nullptr || g.distanceM < best->distanceM)) best = &g;
            }
            if (best == nullptr) {
                r.problem = std::format("nothing {}", Name(static_cast<Sector>(a.target.index)));
                return r;
            }
            r.group = best->id;
        }
        break;
    }
    }
    if (r.group != 0 && Find(groups, r.group) == nullptr) {
        r.problem = std::format("group E{} is gone", r.group);
        r.group = 0;
        return r;
    }
    if (r.landmark != 0 && Find(landmarks, r.landmark) == nullptr) {
        r.problem = std::format("cover L{} is out of range", r.landmark);
        r.landmark = 0;
        return r;
    }
    if (wantsTarget && r.group == 0 && r.order != Order::Move) {
        r.problem = "no target";
        return r;
    }
    if (r.runnerUp == 0 && r.group != 0) {
        Group const* g = Find(groups, r.group);
        r.runnerUp = nearestOther(r.group, g == nullptr ? std::string() : g->type);
    }
    r.ok = true;
    return r;
}

// ---- Text --------------------------------------------------------------------------------------------------------------

std::string Block(Scene const& scene, std::vector<Group> const& groups, std::vector<Deictic> const& deictics,
    std::vector<Command> const& history, std::string_view said) {
    return Block(scene, groups, {}, deictics, history, said);
}

std::string Block(Scene const& scene, std::vector<Group> const& groups, std::vector<Landmark> const& landmarks,
    std::vector<Deictic> const& deictics, std::vector<Command> const& history, std::string_view said) {
    std::string out = scene.combat ? "NOW combat\n" : "NOW explore\n";
    for (Member const& m : scene.party) {
        if (!m.present) continue;
        out += std::format("PARTY {}:", m.letter);
        bool first = true;
        for (int w = 0; w < kWeapons; ++w) {
            if (!(m.carried & (1u << w))) continue;
            out += std::format("{}{}{}", first ? "" : ",", WeaponCode(w), w == m.inHand ? "*" : "");
            first = false;
        }
        if (first) out += "-";
        out += " | ";
        first = true;
        for (Power const& p : m.powers) {
            out += std::format("{}{} {}", first ? "" : ", ", p.code, p.readyInS > 0.0f ? Seconds(p.readyInS) : "ok");
            first = false;
        }
        if (first) out += "-";
        if (!m.alive) out += " | down";
        out += "\n";
    }
    out += std::format("SHEP {}\n", scene.shepardWeapon >= 0 ? std::string(WeaponCode(scene.shepardWeapon)) + "*" : "holstered");
    for (Group const& g : groups) {
        out += std::format("E{} {} x{} | {} {} {}", g.id, g.type.empty() ? "enemy" : g.type, Count(g), Name(g.sector), Name(g.band),
            Name(g.elevation));
        if (!g.surface.empty()) out += " | " + g.surface;
        std::string flags;
        for (auto const& [set, word] : {std::pair{g.nearest, "nearest"}, std::pair{g.farthest, "farthest"}, std::pair{g.leftmost, "leftmost"},
                 std::pair{g.rightmost, "rightmost"}, std::pair{g.highest, "highest"}, std::pair{g.biggest, "biggest"}}) {
            if (set) flags += (flags.empty() ? "" : " ") + std::string(word);
        }
        if (!flags.empty()) out += " | " + flags;
        if (!g.visible) out += " | hidden";
        out += "\n";
    }
    for (Landmark const& l : landmarks) {
        out += std::format("L{} {} | {} {} {}", l.id, l.type, Name(l.sector), Name(l.band), Name(l.elevation));
        std::string flags;
        for (auto const& [set, word] : {std::pair{l.nearest, "nearest"}, std::pair{l.leftmost, "leftmost"}, std::pair{l.rightmost, "rightmost"}}) {
            if (set) flags += (flags.empty() ? "" : " ") + std::string(word);
        }
        if (!flags.empty()) out += " | " + flags;
        out += "\n";
    }
    int const aim = GroupOf(groups, scene.aimTarget);
    int const gaze = GroupOf(groups, scene.gazeTarget);
    if (aim != 0 || gaze != 0) {
        out += std::format("AIM {} | GAZE {}\n", aim != 0 ? std::format("E{}", aim) : "-", gaze != 0 ? std::format("E{}", gaze) : "-");
    }
    for (Deictic const& d : deictics) {
        out += std::format("PT{} \"{}\" =", d.index, d.word);
        if (d.candidates.empty()) out += " nothing";
        bool first = true;
        for (Candidate const& c : d.candidates) {
            out += std::format("{} {}{} {}", first ? "" : ",", c.landmark != 0 ? "L" : "E", c.landmark != 0 ? c.landmark : c.group, ScoreText(c.score));
            first = false;
        }
        out += "\n";
    }
    if (!history.empty()) {
        out += "LAST";
        std::size_t const from = history.size() > 2 ? history.size() - 2 : 0;
        bool first = true;
        for (std::size_t i = history.size(); i-- > from;) {
            Command const& c = history[i];
            Action const& a = c.action;
            out += std::format("{} {} {}", first ? "" : ";", c.who == 0 ? "ALL" : std::string(1, c.who), Name(a.order));
            if (c.group != 0) out += std::format(" E{}", c.group);
            if (!a.power.empty()) out += " " + a.power;
            if (a.weapon >= 0) out += std::string(" ") + WeaponCode(a.weapon);
            out += " " + Seconds(static_cast<float>(scene.time - c.time));
            first = false;
        }
        out += "\n";
    }
    if (!said.empty()) out += std::format("SAY {}\n", said);
    return out;
}

std::vector<std::string> KnownAliases(std::string_view name) {
    std::string key;
    for (char c : name) key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    while (!key.empty() && key.back() == ' ') key.pop_back();
    // The first alias is the name's spoken form (the dataset lists it first too: tools/voice-model/make_dataset.py's ROSTER).
    if (key == "garrus") return {"Garrus", "Vakarian"};
    if (key == "wrex") return {"Wrex", "Urdnot", "Rex"};
    if (key == "liara") return {"Liara", "T'Soni", "Tsoni"};
    if (key == "tali") return {"Tali", "Zorah", "Tali'Zorah"};
    if (key == "ashley") return {"Ashley", "Williams", "Ash", "Chief"};
    if (key == "kaidan") return {"Kaidan", "Alenko", "Lieutenant"};
    return {};
}

std::string Prefix(std::vector<RosterEntry> const& roster) {
    std::string out =
        "You turn a spoken squad order into one line: WHO|WEAPON|POWER|ORDER|TARGET.\n"
        "WHO: a party letter, ALL, or LAST. WEAPON: a code or -. POWER: a code or -.\n"
        "ORDER: attack, move, follow, hold, regroup, use, cancel, repeat.\n"
        "TARGET: an E id, an L id (cover: only for move and hold), PT1..PT3 (what the player pointed at while saying a word), AIM, GAZE, LAST, OTHER, SELF, or -.\n"
        "Only name squadmates listed in PARTY. Use only powers marked ok unless told to wait.\n"
        "ROSTER\n";
    for (RosterEntry const& e : roster) {
        out += std::format("{} {}", e.letter, e.name);
        if (!e.aliases.empty()) {
            out += ":";
            for (std::string const& alias : e.aliases) out += " " + alias + ",";
            out.pop_back();
        }
        out += ". Weapons";
        bool first = true;
        for (int w = 0; w < kWeapons; ++w) {
            if (!(e.weapons & (1u << w))) continue;
            out += std::format("{} {}", first ? "" : ",", WeaponCode(w));
            first = false;
        }
        if (first) out += " -";
        out += ". Powers";
        first = true;
        for (auto const& [code, spoken] : e.powers) {
            out += std::format("{} {} {}", first ? "" : ",", code, spoken);
            first = false;
        }
        if (first) out += " -";
        out += ".\n";
    }
    out += "WEAPONS PI pistol, SG shotgun, AR assault rifle, SR sniper rifle.\n"
           "WORDS troops, soldiers, guys, them, those = enemies. bridge = catwalk = walkway.\n";
    return out;
}

}  // namespace mevr::vr::squad_context
