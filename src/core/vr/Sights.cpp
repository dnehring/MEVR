#include "core/vr/Sights.hpp"

#include "core/vr/OverlayPlacement.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace mevr::vr::sights {
namespace {

XrVector3f Add(XrVector3f a, XrVector3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
XrVector3f Sub(XrVector3f a, XrVector3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XrVector3f Scale(XrVector3f a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(XrVector3f a, XrVector3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
XrVector3f Cross(XrVector3f a, XrVector3f b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Length(XrVector3f a) { return std::sqrt(Dot(a, a)); }
bool Finite(XrVector3f a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

std::optional<XrVector3f> Unit(XrVector3f v) {
    float const len = Length(v);
    if (!(len > 1e-6f) || !std::isfinite(len)) {
        return std::nullopt;
    }
    return Scale(v, 1.0f / len);
}

std::string Lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Words and numbers separated by spaces, commas or semicolons.
std::vector<std::string> Tokens(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == ',' || c == ';') {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

std::optional<float> Number(std::string const& text) {
    char* end = nullptr;
    float const value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

struct Typical {
    float share;   // of the muzzle-to-grip length, behind the muzzle
    float height;  // above the bore (gun units)
    float length;  // the muzzle-to-grip length when unknown (gun units)
};
// Pistol and assault rifle lengths from the 2026-10-21 log (Flash_1 27.2 and 39.2 units ahead of the
// mesh's root); the shotgun's and the sniper rifle's are guesses, as are all the heights [verify].
constexpr Typical kTypical[kWeaponTypes] = {
    {0.75f, 4.0f, 27.0f},  // pistol: on the slide, above the hand
    {0.85f, 5.0f, 35.0f},  // shotgun
    {0.90f, 6.0f, 39.0f},  // assault rifle: just ahead of the grip
    {1.05f, 7.0f, 55.0f},  // sniper rifle: the eyepiece a little behind the grip
};

std::mutex gMutex;
Sight gLatest{0};
std::uint64_t gSequence = 0;
std::atomic<std::uint32_t> gView{0};
std::atomic<std::uint64_t> gViewMs{0};

} // namespace

char const* Name(Kind kind) {
    switch (kind) {
    case Kind::Dot: return "red dot";
    case Kind::Holo: return "holographic";
    case Kind::Scope: return "scope";
    default: return "none";
    }
}

char const* WeaponName(int weapon) {
    switch (weapon) {
    case 0: return "pistol";
    case 1: return "shotgun";
    case 2: return "assault rifle";
    case 3: return "sniper rifle";
    default: return "unknown";
    }
}

char const* WeaponKey(int weapon) {
    switch (weapon) {
    case 0: return "pistol";
    case 1: return "shotgun";
    case 2: return "assaultRifle";
    case 3: return "sniperRifle";
    default: return "";
    }
}

char const* Name(ScopeReticle reticle) {
    switch (reticle) {
    case ScopeReticle::MilDot: return "mil-dot";
    case ScopeReticle::Dot: return "dot";
    default: return "duplex";
    }
}

Gun AutoPlace(int weapon, Gun const& gun, float gunLength) {
    if (!gun.autoPlace || weapon < 0 || weapon >= kWeaponTypes) {
        return gun;
    }
    Typical const& t = kTypical[weapon];
    float const length = gunLength > 5.0f && gunLength < 300.0f && std::isfinite(gunLength) ? gunLength : t.length;
    Gun out = gun;
    out.back = t.share * length;
    out.height = t.height;
    return out;
}

std::optional<Gun> ParseGun(std::string_view text, Gun const& defaults) {
    std::vector<std::string> const tokens = Tokens(text);
    if (tokens.empty() || tokens.size() > 2) {
        return std::nullopt;
    }
    std::string const kind = Lower(tokens[0]);
    Gun gun = defaults;
    if (kind == "none" || kind == "off" || kind == "0") {
        gun.kind = Kind::None;
    } else if (kind == "dot" || kind == "reddot" || kind == "red-dot") {
        gun.kind = Kind::Dot;
    } else if (kind == "holo" || kind == "holographic") {
        gun.kind = Kind::Holo;
    } else if (kind == "scope") {
        gun.kind = Kind::Scope;
    } else {
        return std::nullopt;
    }
    if (tokens.size() == 2) {
        auto const zoom = Number(tokens[1]);
        if (!zoom || gun.kind != Kind::Scope) {
            return std::nullopt;
        }
        gun.zoom = std::clamp(*zoom, 1.0f, 12.0f);
    } else if (gun.kind == Kind::Scope && !(defaults.kind == Kind::Scope && defaults.zoom >= 1.0f)) {
        gun.zoom = 4.0f;
    }
    if (gun.kind != Kind::Scope) {
        gun.zoom = 1.0f;
    }
    return gun;
}

bool ParsePlace(std::string_view text, Gun& gun) {
    std::vector<std::string> const tokens = Tokens(text);
    if (tokens.size() == 1 && Lower(tokens[0]) == "auto") {
        gun.autoPlace = true;
        return true;
    }
    if (tokens.size() != 2) {
        return false;
    }
    auto const back = Number(tokens[0]);
    auto const height = Number(tokens[1]);
    if (!back || !height) {
        return false;
    }
    gun.autoPlace = false;
    gun.back = std::clamp(*back, -50.0f, 300.0f);
    gun.height = std::clamp(*height, -30.0f, 50.0f);
    return true;
}

std::optional<std::uint32_t> ParseColor(std::string_view text) {
    std::vector<std::string> const tokens = Tokens(text);
    if (tokens.size() != 1) {
        return std::nullopt;
    }
    std::string value = Lower(tokens[0]);
    if (value == "red") return 0xFF3020u;
    if (value == "green") return 0x30FF40u;
    if (value == "amber" || value == "orange") return 0xFFA020u;
    if (value == "cyan" || value == "blue") return 0x40E0FFu;
    if (value == "white") return 0xFFFFFFu;
    if (value.starts_with("#")) {
        value.erase(0, 1);
    } else if (value.starts_with("0x")) {
        value.erase(0, 2);
    }
    if (value.size() != 6 || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 16));
}

std::optional<ScopeReticle> ParseScopeReticle(std::string_view text) {
    std::vector<std::string> const tokens = Tokens(text);
    if (tokens.size() != 1) {
        return std::nullopt;
    }
    std::string const value = Lower(tokens[0]);
    if (value == "duplex" || value == "crosshair") return ScopeReticle::Duplex;
    if (value == "mildot" || value == "mil-dot") return ScopeReticle::MilDot;
    if (value == "dot") return ScopeReticle::Dot;
    return std::nullopt;
}

void Publish(Sight const& sight) {
    std::lock_guard lock(gMutex);
    gLatest = sight;
    gLatest.size = sizeof(Sight);
    gLatest.sequence = ++gSequence;
    gLatest.timeMs = GetTickCount64();
}

Sight Latest(std::uint64_t maxAgeMs) {
    Sight copy{0};
    {
        std::lock_guard lock(gMutex);
        copy = gLatest;
    }
    if (copy.timeMs == 0 || GetTickCount64() - copy.timeMs > maxAgeMs || copy.size != sizeof(Sight)) {
        copy.kind = static_cast<std::int32_t>(Kind::None);
    }
    return copy;
}

void Reset() {
    std::lock_guard lock(gMutex);
    gLatest = Sight{0};
}

void PublishView(std::uint32_t view) {
    gView.store(view, std::memory_order_relaxed);
    gViewMs.store(GetTickCount64(), std::memory_order_relaxed);
}

std::uint32_t LatestView(std::uint64_t maxAgeMs) {
    std::uint64_t const at = gViewMs.load(std::memory_order_relaxed);
    if (at == 0 || GetTickCount64() - at > maxAgeMs) {
        return 0;
    }
    return gView.load(std::memory_order_relaxed);
}

XrVector3f Place(XrVector3f const& muzzle, XrVector3f const& barrel, XrVector3f const& up, float backM, float heightM) {
    return Add(Sub(muzzle, Scale(barrel, backM)), Scale(up, heightM));
}

std::optional<XrVector3f> TopOf(XrVector3f const& barrel, XrVector3f const& up) {
    auto const b = Unit(barrel);
    if (!b) {
        return std::nullopt;
    }
    XrVector3f const across = Sub(up, Scale(*b, Dot(up, *b)));
    if (Length(across) < 0.05f * std::max(Length(up), 1e-6f)) {  // within 3 degrees of the barrel
        return std::nullopt;
    }
    return Unit(across);
}

XrVector3f Zeroed(XrVector3f const& center, XrVector3f const& muzzle, XrVector3f const& barrel, float zero) {
    if (zero > 0.0f && std::isfinite(zero)) {
        if (auto const to = Unit(Sub(Add(muzzle, Scale(barrel, zero)), center))) {
            return *to;
        }
    }
    return Unit(barrel).value_or(XrVector3f{0.0f, 0.0f, -1.0f});
}

Line EyeLine(XrVector3f const& eye, XrVector3f const& center, XrVector3f const& axis, XrVector3f const& up) {
    Line line;
    XrVector3f const v = Sub(eye, center);
    line.along = -Dot(v, axis);
    XrVector3f const off = Sub(v, Scale(axis, Dot(v, axis)));  // the eye's line meets the sight's plane here
    line.lateral = Length(off);
    XrVector3f const right = Cross(axis, up);
    line.x = Dot(off, right);
    line.y = Dot(off, up);
    return line;
}

bool Looking::Update(Line const& line, float radius, float minAlong, float maxAlong) {
    if (!(radius > 0.0f) || !std::isfinite(line.along) || !std::isfinite(line.lateral)) {
        on_ = false;
        return on_;
    }
    if (on_) {
        on_ = line.lateral <= 1.25f * radius && line.along >= 0.5f * minAlong && line.along <= 1.2f * maxAlong;
    } else {
        on_ = line.lateral <= radius && line.along >= minAlong && line.along <= maxAlong;
    }
    return on_;
}

int AimingEye(Line const lines[2], float box, float minAlong, float maxAlong, int current) {
    auto inside = [&](int e, float scale, float alongScale) {
        Line const& l = lines[e];
        return std::isfinite(l.lateral) && std::isfinite(l.along) && l.lateral <= box * scale && l.along >= minAlong * (2.0f - alongScale) &&
            l.along <= maxAlong * alongScale;
    };
    int best = -1;
    for (int e = 0; e < 2; ++e) {
        if (inside(e, 1.0f, 1.0f) && (best < 0 || lines[e].lateral < lines[best].lateral)) {
            best = e;
        }
    }
    if ((current == 0 || current == 1) && inside(current, 1.25f, 1.2f)) {
        int const other = 1 - current;
        bool const otherClearlyBetter = best == other && lines[other].lateral < lines[current].lateral - 0.25f * box;
        return otherClearlyBetter ? other : current;
    }
    return best;
}

float Smooth(float t) {
    if (!(t > 0.0f)) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

AssistGoal AssistTarget(Assist const& s, XrVector3f const& center, XrVector3f const& axisIn, XrVector3f const& upIn,
    XrVector3f const eyes[2], XrVector3f const& headForward, int currentEye) {
    constexpr float kDeg = 3.14159265358979f / 180.0f;
    constexpr float kMinAlong = 0.02f;     // the eye at the sight itself: no line to speak of
    constexpr float kRangeFade = 0.15f;    // the distance weight fades over this, ending at rangeM
    constexpr float kHeadFade = 20.0f;     // the head weight fades over this many degrees, ending at headDeg
    constexpr float kEyeSwitchDeg = 1.5f;  // the other eye takes over only when this much nearer the line
    AssistGoal goal;
    auto const axis = Unit(axisIn);
    if (!s.enabled || !axis || !Finite(center) || !Finite(eyes[0]) || !Finite(eyes[1]) || !(s.coneDeg > 0.0f)) {
        return goal;
    }
    XrVector3f up = Sub(upIn, Scale(*axis, Dot(upIn, *axis)));
    up = Unit(up).value_or(XrVector3f{0.0f, 1.0f, 0.0f});
    // Each eye against the line: how far behind the sight, and its angle off the line seen from the sight.
    Line lines[2];
    float angles[2] = {180.0f, 180.0f};
    bool usable[2] = {false, false};
    for (int e = 0; e < 2; ++e) {
        lines[e] = EyeLine(eyes[e], center, *axis, up);
        usable[e] = std::isfinite(lines[e].along) && lines[e].along >= kMinAlong && lines[e].along <= s.rangeM;
        if (usable[e]) {
            angles[e] = std::atan2(lines[e].lateral, lines[e].along) / kDeg;
        }
    }
    int eye = -1;
    for (int e = 0; e < 2; ++e) {
        if (usable[e] && (eye < 0 || angles[e] < angles[eye])) {
            eye = e;
        }
    }
    if ((currentEye == 0 || currentEye == 1) && usable[currentEye] && eye >= 0 && eye != currentEye &&
        angles[eye] > angles[currentEye] - kEyeSwitchDeg) {
        eye = currentEye;  // hysteresis: the other eye must be clearly nearer the line
    }
    if (eye < 0 || angles[eye] >= s.coneDeg) {
        goal.eye = eye;
        if (eye >= 0) {
            goal.angleDeg = angles[eye];
            goal.along = lines[eye].along;
            goal.x = lines[eye].x;
            goal.y = lines[eye].y;
        }
        return goal;
    }
    Line const& line = lines[eye];
    goal.eye = eye;
    goal.angleDeg = angles[eye];
    goal.along = line.along;
    goal.x = line.x;
    goal.y = line.y;
    // Full inside half the cone, none at its edge; full up to 0.15 m short of the range; full with the
    // head within 20 degrees of looking along the sight, none past headDeg.
    float const cone = Smooth((s.coneDeg - angles[eye]) / (0.5f * s.coneDeg));
    float const range = Smooth((s.rangeM - line.along) / kRangeFade);
    float head = 1.0f;
    if (auto const forward = Unit(headForward)) {
        float const off = std::acos(std::clamp(Dot(*forward, *axis), -1.0f, 1.0f)) / kDeg;
        head = Smooth((s.headDeg - off) / kHeadFade);
    }
    goal.weight = std::clamp(s.strength, 0.0f, 1.0f) * cone * range * head;
    // The eye's offset from the line, in the sight's right and up: moving the gun by it puts the line
    // through the eye.
    XrVector3f const right = Cross(*axis, up);
    XrVector3f shift = Add(Scale(right, line.x * goal.weight), Scale(up, line.y * goal.weight));
    float const length = Length(shift);
    if (length > s.maxShiftM && length > 0.0f) {
        shift = Scale(shift, std::max(s.maxShiftM, 0.0f) / length);
    }
    goal.shift = shift;
    return goal;
}

XrVector3f AssistSpring::Update(XrVector3f const& goal, float dtIn, float frequency, float maxSpeed) {
    if (!Finite(goal) || !std::isfinite(dtIn) || !Finite(value_) || !Finite(velocity_)) {
        Reset();
        return value_;
    }
    float const dt = std::clamp(dtIn, 0.0f, 0.15f);
    if (dt <= 0.0f) {
        return value_;
    }
    // Critically damped, speed-limited: adapted from Overlord (https://github.com/Aeka0/Overlord, GPL-3.0,
    // Aeka0), src/client/component/vr/gameplay/ads_comfort.hpp `transition::update` (there one distance
    // along the barrel; here a vector across the sight line). x'' = -w^2 e - 2 w x'.
    XrVector3f const error = Sub(value_, goal);
    XrVector3f const tangent = Add(velocity_, Scale(error, frequency));
    float const decay = std::exp(-frequency * dt);
    XrVector3f const next = Add(goal, Scale(Add(error, Scale(tangent, dt)), decay));
    velocity_ = Scale(Sub(velocity_, Scale(tangent, frequency * dt)), decay);
    float const speed = Length(velocity_);
    if (speed > maxSpeed && speed > 0.0f) {
        velocity_ = Scale(velocity_, maxSpeed / speed);
    }
    XrVector3f step = Sub(next, value_);
    float const stepLength = Length(step);
    float const maxStep = maxSpeed * dt;
    if (stepLength > maxStep && stepLength > 0.0f) {
        step = Scale(step, maxStep / stepLength);
    }
    value_ = Add(value_, step);
    // Finish the tail (under 0.05 mm and 1 mm/s).
    if (Length(Sub(goal, value_)) < 0.00005f && Length(velocity_) < 0.001f) {
        value_ = goal;
        velocity_ = {};
    }
    return value_;
}

Quad Reticle(XrVector3f const& center, XrVector3f const& axis, XrVector3f const& up, float distance, float angularSize) {
    Quad quad;
    quad.pose.position = Add(center, Scale(axis, distance));
    quad.pose.orientation = overlay::Facing(Scale(axis, -1.0f), up);
    quad.size = 2.0f * distance * std::tan(0.5f * angularSize);
    return quad;
}

Quad Disc(XrVector3f const& center, XrVector3f const& axis, XrVector3f const& up, float radius) {
    Quad quad;
    quad.pose.position = center;
    quad.pose.orientation = overlay::Facing(Scale(axis, -1.0f), up);
    quad.size = 2.0f * radius;
    return quad;
}

std::optional<Lens> MakeLens(XrVector3f const& center, XrVector3f const& axis, XrVector3f const& up, float radius) {
    auto const a = Unit(axis);
    if (!a || !Finite(center) || !(radius > 0.0f) || !std::isfinite(radius)) {
        return std::nullopt;
    }
    auto const u = TopOf(*a, up);
    if (!u) {
        return std::nullopt;
    }
    return Lens{center, Cross(*a, *u), *u, *a, radius};
}

std::optional<ScopeView> View(Lens const& lens, XrVector3f const& eye, float eyeRelief) {
    if (!Finite(eye) || !(eyeRelief > 0.0f)) {
        return std::nullopt;
    }
    Line const line = EyeLine(eye, lens.center, lens.axis, lens.up);
    if (!(line.along >= 0.01f) || line.along > 6.0f * eyeRelief) {
        return std::nullopt;
    }
    ScopeView view;
    view.along = line.along;
    view.qx = line.x / lens.radius;
    view.qy = line.y / lens.radius;
    // Further back than the eye relief (and a half) the picture shrinks inside a black ring.
    float const past = line.along - 1.5f * eyeRelief;
    view.pupil = past <= 0.0f ? 1.0f : std::max(0.2f, 1.0f - past / (3.0f * eyeRelief));
    return view;
}

XrVector3f LensDirection(Lens const& lens, ScopeView const& view, float x, float y, float zoom) {
    float const k = lens.radius / std::max(zoom, 1e-3f);
    return Add(Scale(lens.axis, view.along), Add(Scale(lens.right, (x - view.qx) * k), Scale(lens.up, (y - view.qy) * k)));
}

XrVector3f FocusPoint(XrVector3f const& eye, XrVector3f const& direction, XrVector3f const& axis, float focus) {
    float const along = Dot(direction, axis);
    if (!(along > 1e-6f)) {
        return Add(eye, Scale(axis, focus));
    }
    return Add(eye, Scale(direction, focus / along));
}

bool Project(XrPosef const& pose, XrFovf const& fov, XrVector3f const& point, float& u, float& v) {
    XrQuaternionf const inverse{-pose.orientation.x, -pose.orientation.y, -pose.orientation.z, pose.orientation.w};
    XrVector3f const e = overlay::Rotate(inverse, Sub(point, pose.position));
    if (!(e.z < -1e-6f)) {
        return false;
    }
    float const tx = e.x / -e.z;
    float const ty = e.y / -e.z;
    float const l = std::tan(fov.angleLeft);
    float const r = std::tan(fov.angleRight);
    float const t = std::tan(fov.angleUp);
    float const b = std::tan(fov.angleDown);
    if (!(r - l > 1e-6f) || !(t - b > 1e-6f)) {
        return false;
    }
    u = (tx - l) / (r - l);
    v = (t - ty) / (t - b);
    return std::isfinite(u) && std::isfinite(v);
}

} // namespace mevr::vr::sights
