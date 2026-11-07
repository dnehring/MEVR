// SPDX-License-Identifier: GPL-3.0-only
#include "core/vr/Sights.hpp"

#include "core/vr/OverlayPlacement.hpp"

#include <charconv>
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
    float value = 0.0f;  // std::from_chars: no C locale
    auto const [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end == text.data() || end != text.data() + text.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

struct Typical {
    float share;   // of the muzzle-to-grip length, behind the muzzle
    float height;  // above the bore (gun units)
    float length;  // the muzzle-to-grip length when unknown (gun units)
    float housing; // a scope on a bone that isn't its eyepiece: the eyepiece this far behind the bone (gun units)
};
// Pistol and assault rifle lengths from the 2026-10-21 log (Flash_1 27.2 and 39.2 units ahead of the
// mesh's root); the shotgun's and the sniper rifle's are guesses, as are all the heights [verify].
// The housing offsets are guesses too [verify]: the assault rifle's `Scope` bone (39.6 behind the muzzle, 9.1 up,
// 2026-10-26) is taken as the middle of a short sight housing, its rear 1.5 units further back.
constexpr Typical kTypical[kWeaponTypes] = {
    {0.75f, 4.0f, 27.0f, 1.0f},  // pistol: on the slide, above the hand
    {0.85f, 5.0f, 35.0f, 1.5f},  // shotgun
    {0.90f, 6.0f, 39.0f, 1.5f},  // assault rifle: just ahead of the grip
    {1.05f, 7.0f, 55.0f, 2.0f},  // sniper rifle: the eyepiece a little behind the grip
};

bool HasSightWord(std::string const& n) {
    return n.find("scope") != std::string::npos || n.find("sight") != std::string::npos || n.find("optic") != std::string::npos ||
        n.find("reticle") != std::string::npos || n.find("lens") != std::string::npos || n.find("eyepiece") != std::string::npos;
}

bool HasRearWord(std::string const& n) {
    return n.find("eye") != std::string::npos || n.find("ocular") != std::string::npos || n.find("rear") != std::string::npos;
}

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
    case ScopeReticle::Game: return "the game's";
    default: return "duplex";
    }
}

namespace {
// research/21 section 16: from the meshes in BIOC_Materials.pcc (each bone's own vertices' box, the reference pose; the
// sessions' live bones agree with it: SNPa's Scope_Eye 59.2 back, 7.0 up; ASLa's Scope 39.6 back, 9.1 up).
//   SNPa: Scope_Eye at x -5.49, z 12.39; its tube x -6.1..8.0, 4.2 wide, z 8.5..12.8: the glass 13.5 behind the bone, its
//         axis 1.7 under it (behind the grip, `handle` at x 1.9). Scope_Lens (the objective) keeps being hidden.
//   SNPb: Scope_Rear at x 5.76, z 10.16; its disc x 6.6..7.2, 3.4 across, centre z 9.85: the glass itself.
//   ASLa: Scope at x -0.34, z 10.65; its housing x -5.8..7.1, 4.3 wide, z 9.5..13.9: the rear 7.4 behind the bone.
//   ASLb: Scope at x 7.06, z 10.21; its housing x 3.0..9.0, 3.3 wide, z 8.4..12.0: the rear 1.9 behind the bone.
constexpr Optic kOptics[] = {
    {"WPN_SNPa_MDL", "Scope_Eye", 13.49f, -1.74f, 2.10f, "Scope_Lens"},
    {"WPN_SNPb_MDL_1a", "Scope_Rear", 1.44f, -0.31f, 1.70f, ""},
    {"WPN_ASLa_MDL", "Scope", 7.44f, 1.05f, 2.15f, ""},
    {"WPN_ASLb_MDL", "Scope", 1.94f, 0.0f, 1.70f, ""},
};
} // namespace

std::optional<Optic> OpticFor(std::string_view meshName) {
    std::string const name = Lower(meshName);
    for (Optic const& optic : kOptics) {
        if (name == Lower(optic.mesh)) {
            return optic;
        }
    }
    return std::nullopt;
}

std::vector<std::string> HideList(std::string_view setting, std::optional<Optic> const& optic, int weapon) {
    std::vector<std::string> const tokens = Tokens(setting);
    if (tokens.size() == 1 && Lower(tokens[0]) == "auto") {
        if (optic) {
            return ParseBoneList(optic->hide);
        }
        return weapon == 3 ? std::vector<std::string>{"Scope_Lens"} : std::vector<std::string>{};
    }
    return ParseBoneList(setting);
}

float ZoomFromFov(float zoomFovDeg, float baseFovDeg) {
    if (!std::isfinite(zoomFovDeg) || !std::isfinite(baseFovDeg) || !(zoomFovDeg >= 1.0f) || !(zoomFovDeg <= baseFovDeg) ||
        !(baseFovDeg < 179.0f)) {
        return 0.0f;
    }
    constexpr float kHalfRadians = 3.14159265f / 360.0f;
    return std::clamp(std::tan(baseFovDeg * kHalfRadians) / std::tan(zoomFovDeg * kHalfRadians), 1.0f, 12.0f);
}

void AddAimMode(GameZoom& zoom, float zoomFovDeg, bool scoped) {
    if (!scoped) {
        ++zoom.unscoped;
        return;
    }
    float const z = ZoomFromFov(zoomFovDeg);
    if (!(z > 0.0f) || zoom.count >= 4) {
        return;
    }
    zoom.zoom[zoom.count] = z;
    zoom.fovDeg[zoom.count] = zoomFovDeg;
    ++zoom.count;
}

float ZoomAt(GameZoom const& zoom, int level) {
    if (zoom.count <= 0) {
        return 0.0f;
    }
    return zoom.zoom[std::clamp(level, 0, zoom.count - 1)];
}

int ZoomFlick::Update(float stickY, float on, float off) {
    if (!std::isfinite(stickY)) {
        return 0;
    }
    if (!armed_) {
        if (std::abs(stickY) < off) {
            armed_ = true;
        }
        return 0;
    }
    if (stickY > on || stickY < -on) {
        armed_ = false;
        return stickY > 0.0f ? 1 : -1;
    }
    return 0;
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
        if (gun.kind != Kind::Scope) {
            return std::nullopt;
        }
        std::string const second = Lower(tokens[1]);
        if (second == "auto" || second == "game") {
            gun.zoom = 0.0f;  // the held weapon's own magnification (research/21 section 16)
        } else {
            auto const zoom = Number(tokens[1]);
            if (!zoom) {
                return std::nullopt;
            }
            gun.zoom = std::clamp(*zoom, 1.0f, 12.0f);
        }
    } else if (gun.kind == Kind::Scope && !(defaults.kind == Kind::Scope && (defaults.zoom >= 1.0f || defaults.zoom == 0.0f))) {
        gun.zoom = 0.0f;
    }
    if (gun.kind != Kind::Scope) {
        gun.zoom = 1.0f;
    }
    return gun;
}

std::optional<std::string> MigrateAssaultRifle(std::string_view value) {
    std::vector<std::string> const tokens = Tokens(value);
    if (tokens.size() != 2 || Lower(tokens[0]) != "scope") {
        return std::nullopt;
    }
    auto const zoom = Number(tokens[1]);
    if (!zoom || std::abs(*zoom - 1.5f) > 1e-4f) {
        return std::nullopt;
    }
    return std::string("dot");
}

bool ParsePlace(std::string_view text, Gun& gun) {
    std::vector<std::string> const tokens = Tokens(text);
    if (tokens.size() == 1 && Lower(tokens[0]) == "auto") {
        gun.autoPlace = true;
        return true;
    }
    if (tokens.size() != 2 && tokens.size() != 3) {
        return false;
    }
    auto const back = Number(tokens[0]);
    auto const height = Number(tokens[1]);
    auto const side = tokens.size() == 3 ? Number(tokens[2]) : std::optional<float>(0.0f);
    if (!back || !height || !side) {
        return false;
    }
    gun.autoPlace = false;
    gun.back = std::clamp(*back, -50.0f, 300.0f);
    gun.height = std::clamp(*height, -30.0f, 50.0f);
    gun.side = std::clamp(*side, -30.0f, 30.0f);
    return true;
}

std::vector<std::string> ParseBoneList(std::string_view text) {
    std::vector<std::string> out;
    for (std::string const& token : Tokens(text)) {
        std::string const lower = Lower(token);
        if (lower == "none" || lower == "off" || lower == "0") {
            continue;
        }
        if (std::none_of(out.begin(), out.end(), [&](std::string const& o) { return Lower(o) == lower; })) {
            out.push_back(token);
        }
    }
    return out;
}

float LensUnits(Settings const& settings, int weapon) {
    if (weapon >= 0 && weapon < kWeaponTypes && settings.guns[weapon].lens > 0.0f && std::isfinite(settings.guns[weapon].lens)) {
        return settings.guns[weapon].lens;
    }
    return settings.lensUnits;
}

float HousingToEyepiece(int weapon) {
    return weapon >= 0 && weapon < kWeaponTypes ? kTypical[weapon].housing : 0.0f;
}

bool IsEyepieceName(std::string_view name) {
    std::string const n = Lower(name);
    return n.rfind("socket ", 0) != 0 && HasSightWord(n) && HasRearWord(n);
}

HideChoice PartsToHide(std::span<std::string const> names, std::span<int const> parents, std::optional<std::size_t> sightBone,
    std::span<std::string const> wanted) {
    HideChoice out;
    // The sight's bone and every bone above it: hiding one would collapse the sight onto it.
    std::vector<bool> protectedBone(names.size(), false);
    if (sightBone && *sightBone < names.size()) {
        std::size_t at = *sightBone;
        for (std::size_t guard = 0; guard <= names.size(); ++guard) {
            protectedBone[at] = true;
            int const parent = at < parents.size() ? parents[at] : -1;
            if (parent < 0 || static_cast<std::size_t>(parent) >= names.size() || static_cast<std::size_t>(parent) == at) {
                break;
            }
            at = static_cast<std::size_t>(parent);
        }
    }
    for (std::string const& want : wanted) {
        std::string const w = Lower(want);
        std::optional<std::size_t> found;
        for (std::size_t i = 0; i < names.size(); ++i) {
            std::string const n = Lower(names[i]);
            if (n.rfind("socket ", 0) != 0 && n == w) {
                found = i;
                break;
            }
        }
        if (!found) {
            out.skipped.push_back(want + " (not on this model)");
        } else if (*found == 0 || (*found < parents.size() && parents[*found] < 0)) {
            out.skipped.push_back(names[*found] + " (the model's root)");
        } else if (protectedBone[*found]) {
            out.skipped.push_back(names[*found] + (sightBone && *found == *sightBone ? " (the sight's own bone)" : " (the sight hangs on it)"));
        } else if (std::find(out.bones.begin(), out.bones.end(), *found) == out.bones.end()) {
            out.bones.push_back(*found);
        }
    }
    return out;
}

float ReliefFor(float reliefM, float nearUnits, float unitsPerMeter, float marginM) {
    float relief = std::isfinite(reliefM) && reliefM > 0.0f ? reliefM : 0.0f;
    if (std::isfinite(nearUnits) && nearUnits > 0.0f && std::isfinite(unitsPerMeter) && unitsPerMeter > 0.0f) {
        relief = std::max(relief, nearUnits / unitsPerMeter + std::max(marginM, 0.0f));
    }
    return relief;
}

float PulledFieldDeg(float lensM, float reliefM) {
    if (!(lensM > 0.0f) || !(reliefM > 0.0f) || !std::isfinite(lensM) || !std::isfinite(reliefM)) {
        return 0.0f;
    }
    return 2.0f * std::atan(lensM / reliefM) * 180.0f / 3.14159265f;
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
    if (value == "game" || value == "original") return ScopeReticle::Game;
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

XrVector3f Place(XrVector3f const& muzzle, XrVector3f const& barrel, XrVector3f const& up, float backM, float heightM, float sideM) {
    XrVector3f const at = Add(Sub(muzzle, Scale(barrel, backM)), Scale(up, heightM));
    return sideM == 0.0f ? at : Add(at, Scale(Cross(barrel, up), sideM));
}

std::optional<std::size_t> SightBone(Kind kind, std::span<std::string const> names) {
    std::optional<std::size_t> first;
    std::optional<std::size_t> eyepiece;
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::string const n = Lower(names[i]);
        if (n.rfind("socket ", 0) == 0) {
            continue;  // sockets hang off bones; the bone itself is what moves when the gun unfolds
        }
        if (!HasSightWord(n)) {
            continue;
        }
        if (!first) {
            first = i;
        }
        if (HasRearWord(n) && !eyepiece) {
            eyepiece = i;
        }
    }
    return kind == Kind::Scope && eyepiece ? eyepiece : first;
}

std::optional<float> ModelTop(std::span<ModelPoint const> bones, float gunLength) {
    float const length = std::isfinite(gunLength) && gunLength > 5.0f ? gunLength : 40.0f;
    std::optional<float> top;
    for (ModelPoint const& p : bones) {
        if (!std::isfinite(p.back) || !std::isfinite(p.up) || !std::isfinite(p.side) || p.back < -2.0f || p.back > 1.6f * length ||
            std::abs(p.side) > 8.0f || p.up > 30.0f) {
            continue;
        }
        top = top ? std::max(*top, p.up) : p.up;
    }
    if (!top || !(*top > 0.5f)) {
        return std::nullopt;
    }
    return top;
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
        // The hysteresis widens the range on both ends, also for a minimum below zero (an eye a little past the sight).
        return std::isfinite(l.lateral) && std::isfinite(l.along) && l.lateral <= box * scale &&
            l.along >= minAlong - std::abs(minAlong) * (alongScale - 1.0f) && l.along <= maxAlong * alongScale;
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

int NearestEye(Line const lines[2], float minAlong, float maxAlong, int current, float switchMargin) {
    auto usable = [&](int e) {
        return std::isfinite(lines[e].lateral) && std::isfinite(lines[e].along) && lines[e].along >= minAlong && lines[e].along <= maxAlong;
    };
    int best = -1;
    for (int e = 0; e < 2; ++e) {
        if (usable(e) && (best < 0 || lines[e].lateral < lines[best].lateral)) {
            best = e;
        }
    }
    if ((current == 0 || current == 1) && usable(current) && best == 1 - current &&
        !(lines[best].lateral < lines[current].lateral - std::max(switchMargin, 0.0f))) {
        return current;
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
    XrVector3f const eyes[2], float weightIn, int currentEye, Pull const& pull) {
    constexpr float kDeg = 3.14159265358979f / 180.0f;
    // The eye at the sight itself: no line to speak of. With a push (a scope pulled into the shoulder) an eye up to the push
    // past the eyepiece still counts: it's what the push moves away from.
    bool const pushes = pull.pushM > 0.0f && std::isfinite(pull.pushM) && pull.reliefM > 0.0f;
    float const kMinAlong = pushes ? -std::min(pull.pushM, 0.5f) : 0.02f;
    constexpr float kMaxAlong = 1.5f;      // an eye further back than this isn't aiming through it
    constexpr float kEyeSwitchDeg = 1.5f;  // the other eye takes over only when this much nearer the line
    AssistGoal goal;
    auto const axis = Unit(axisIn);
    if (!s.enabled || !axis || !Finite(center) || !Finite(eyes[0]) || !Finite(eyes[1])) {
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
        usable[e] = std::isfinite(lines[e].along) && lines[e].along >= kMinAlong && lines[e].along <= kMaxAlong;
        if (usable[e]) {
            angles[e] = std::atan2(lines[e].lateral, std::max(lines[e].along, 0.02f)) / kDeg;
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
    if (eye < 0) {
        return goal;
    }
    Line const& line = lines[eye];
    goal.eye = eye;
    goal.angleDeg = angles[eye];
    goal.along = line.along;
    goal.x = line.x;
    goal.y = line.y;
    float const weight = std::isfinite(weightIn) ? std::clamp(weightIn, 0.0f, 1.0f) : 0.0f;
    goal.weight = std::clamp(s.strength, 0.0f, 1.0f) * weight;
    if (!(goal.weight > 0.0f)) {
        return goal;
    }
    // The eye's offset from the line, in the sight's right and up: moving the gun by it puts the line
    // through the eye.
    XrVector3f const right = Cross(*axis, up);
    XrVector3f shift = Add(Scale(right, line.x * goal.weight), Scale(up, line.y * goal.weight));
    float const length = Length(shift);
    if (length > s.maxShiftM && length > 0.0f) {
        shift = Scale(shift, std::max(s.maxShiftM, 0.0f) / length);
    }
    // The pull: back along the line toward the eye, until the sight is the relief ahead of it; away from the eye (a push)
    // only up to pushM, when the eye is nearer than the relief.
    if (pull.reliefM > 0.0f && (pull.maxM > 0.0f || pushes) && std::isfinite(pull.reliefM) && std::isfinite(pull.maxM)) {
        float const push = pushes ? std::min(pull.pushM, 0.5f) : 0.0f;
        float const want = std::clamp(line.along - pull.reliefM, -push, std::max(pull.maxM, 0.0f));
        goal.pull = want * goal.weight;
        shift = Sub(shift, Scale(*axis, goal.pull));
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

Quad ReticleInWindow(XrVector3f const& center, XrVector3f const& axisIn, XrVector3f const& up, float radius, XrVector3f const& eye,
    float distance, float angularSize, float fraction, bool* clamped) {
    if (clamped != nullptr) {
        *clamped = false;
    }
    Quad const truth = Reticle(center, axisIn, up, distance, angularSize);
    auto const axis = Unit(axisIn);
    if (!axis || !Finite(center) || !Finite(eye) || !(radius > 0.0f) || !(distance > 0.0f)) {
        return truth;
    }
    float const ahead = Dot(Sub(center, eye), *axis);  // the window this far ahead of the eye along the line
    if (!(ahead > 0.01f)) {
        return truth;
    }
    // The ray from the eye toward the true dot, where it crosses the window's plane.
    auto const toDot = Unit(Sub(truth.pose.position, eye));
    if (!toDot) {
        return truth;
    }
    float const along = Dot(*toDot, *axis);
    if (!(along > 0.05f)) {
        return truth;
    }
    XrVector3f const crossing = Add(eye, Scale(*toDot, ahead / along));
    XrVector3f const offset = Sub(crossing, center);
    float const off = Length(offset);
    float const limit = std::clamp(fraction, 0.0f, 1.0f) * radius;
    if (off <= limit) {
        return truth;
    }
    XrVector3f const inside = Add(center, Scale(offset, limit / off));
    auto const ray = Unit(Sub(inside, eye));
    if (!ray) {
        return truth;
    }
    Quad quad;
    quad.pose.position = Add(eye, Scale(*ray, distance));
    quad.pose.orientation = overlay::Facing(Scale(*ray, -1.0f), up);
    quad.size = 2.0f * distance * std::tan(0.5f * angularSize);
    if (clamped != nullptr) {
        *clamped = true;
    }
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
    view.px = view.qx;
    view.py = view.qy;
    return view;
}

std::optional<ScopeView> WindowView(Lens const& lens, XrVector3f const& eye, float minAlong, float maxAlong) {
    if (!Finite(eye) || !(lens.radius > 0.0f)) {
        return std::nullopt;
    }
    Line const line = EyeLine(eye, lens.center, lens.axis, lens.up);
    if (!(line.along >= minAlong) || !(line.along <= maxAlong)) {
        return std::nullopt;
    }
    ScopeView view;
    view.along = line.along;
    view.qx = line.x / lens.radius;
    view.qy = line.y / lens.radius;
    // The pupil far wider than the window and centred: no shadow anywhere on it.
    view.pupil = 4.0f;
    view.px = 0.0f;
    view.py = 0.0f;
    view.fade = 1.0f;
    return view;
}

int EyeHold::Update(int chosen, std::uint64_t nowMs, std::uint64_t holdMs) {
    if (chosen == 0 || chosen == 1) {
        if (holding_ && chosen == eye_) {
            ++bridged_;
        }
        holding_ = false;
        eye_ = chosen;
        seenMs_ = nowMs;
        return chosen;
    }
    if (eye_ >= 0 && nowMs >= seenMs_ && nowMs - seenMs_ < holdMs) {
        holding_ = true;
        return eye_;
    }
    holding_ = false;
    eye_ = -1;
    return -1;
}

float PictureRadius(float lensRadius, float along, float fieldDeg) {
    if (!(fieldDeg > 0.0f) || !std::isfinite(fieldDeg) || !(along > 0.0f)) {
        return lensRadius;
    }
    float const half = std::min(fieldDeg, 90.0f) * 0.5f * 3.14159265f / 180.0f;
    return std::max(lensRadius, along * std::tan(half));
}

std::optional<ScopeView> VirtualView(Lens const& lens, XrVector3f const& eye, Eyepiece const& eyepiece, Lens& picture) {
    if (!Finite(eye) || !(eyepiece.maxAlong > 0.0f) || !(eyepiece.eyeBox > 0.0f) || !(lens.radius > 0.0f)) {
        return std::nullopt;
    }
    Line const line = EyeLine(eye, lens.center, lens.axis, lens.up);
    if (!(line.along >= 0.03f) || line.along > 1.25f * eyepiece.maxAlong || line.lateral > 2.0f * eyepiece.eyeBox) {
        return std::nullopt;
    }
    picture = lens;
    picture.radius = PictureRadius(lens.radius, line.along, eyepiece.fieldDeg);
    ScopeView view;
    view.along = line.along;
    view.qx = line.x / picture.radius;
    view.qy = line.y / picture.radius;
    view.pupil = 1.0f;
    // The shadow: the pupil's centre goes a picture radius off for each eye box the eye is off the line (the same side
    // as the eye, as the physical lens's), so a centred eye sees all of it and an eye an eye box off half.
    view.px = line.x / eyepiece.eyeBox;
    view.py = line.y / eyepiece.eyeBox;
    view.fade = 1.0f - Smooth((line.along - eyepiece.maxAlong) / (0.25f * eyepiece.maxAlong));
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

XrVector3f ConvergedShot(XrVector3f const& sightFromMuzzle, XrVector3f const& axis, float distance, float minAhead) {
    XrVector3f const unitAxis = Unit(axis).value_or(XrVector3f{0.0f, 0.0f, -1.0f});
    if (!(distance > 0.0f) || !std::isfinite(distance) || !Finite(sightFromMuzzle)) {
        return unitAxis;
    }
    XrVector3f const target = Add(sightFromMuzzle, Scale(unitAxis, distance));
    if (!(Dot(target, unitAxis) >= std::max(minAhead, 1e-3f))) {
        return unitAxis;  // what the sight sees is level with the muzzle or behind it: no line to bend toward
    }
    return Unit(target).value_or(unitAxis);
}

float AngleDeg(XrVector3f const& a, XrVector3f const& b) {
    float const lengths = Length(a) * Length(b);
    if (!(lengths > 0.0f) || !std::isfinite(lengths)) {
        return 180.0f;
    }
    // atan2 of |a x b| and a.b: accurate for the small angles the logs care about.
    return std::atan2(Length(Cross(a, b)), Dot(a, b)) * 180.0f / 3.14159265358979f;
}

std::optional<float> HeightOnModel(Kind kind, float modelTop, float radius) {
    if (!std::isfinite(modelTop) || !(modelTop > 0.5f) || modelTop > 30.0f || kind == Kind::None) {
        return std::nullopt;
    }
    float const r = std::isfinite(radius) && radius > 0.0f ? radius : 0.0f;
    float const height = kind == Kind::Scope ? modelTop - r : modelTop + 1.0f;
    return std::max(height, 1.0f);
}

} // namespace mevr::vr::sights
