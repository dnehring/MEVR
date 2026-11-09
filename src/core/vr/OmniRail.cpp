// SPDX-License-Identifier: MIT
#include "core/vr/OmniRail.hpp"

#include "core/vr/OverlayPlacement.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace mevr::vr::omni_rail {

namespace {

namespace gm = game_menu;

constexpr std::array<EntryInfo, static_cast<std::size_t>(Entry::Count)> kEntries = {{
    {"squad", "Squad", "squad", gm::Action::Squad},
    {"equipment", "Equipment", "equipment", gm::Action::Equipment},
    {"map", "Map", "map", gm::Action::Map},
    {"journal", "Journal", "journal", gm::Action::Journal},
    {"codex", "Codex", "codex", gm::Action::Codex},
    {"computer", "Computer", "gamemenu", gm::Action::PauseMenu},
    {"close", "Close", "close", gm::Action::CloseScreens},
}};

float Distance(XrVector3f const& a, XrVector3f const& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

} // namespace

EntryInfo const& Info(Entry entry) {
    auto const i = std::min(static_cast<std::size_t>(entry), kEntries.size() - 1);
    return kEntries[i];
}

std::optional<Entry> FromId(std::string_view id) {
    for (std::size_t i = 0; i < kEntries.size(); ++i) {
        if (id == kEntries[i].id) {
            return static_cast<Entry>(i);
        }
    }
    return std::nullopt;
}

std::optional<Entry> ForScreen(world_ui::Screen screen) {
    using S = world_ui::Screen;
    switch (screen) {
    case S::Inventory:
    case S::XMods: return Entry::Equipment;
    case S::CharacterRecord:
    case S::Specialization: return Entry::Squad;
    case S::Journal: return Entry::Journal;
    case S::Codex: return Entry::Codex;
    case S::Map: return Entry::Map;
    case S::MenuBrowser:
    case S::Options:
    case S::Save:
    case S::Load: return Entry::Computer;
    default: return std::nullopt;
    }
}

Rail Beside(world_ui::Panel const& screen, float side, float gapM, float aspect) {
    Rail rail;
    rail.heightM = screen.heightM;
    rail.widthM = screen.heightM * aspect;
    XrVector3f const right = overlay::Rotate(screen.pose.orientation, {1.0f, 0.0f, 0.0f});
    float const shift = (side < 0.0f ? -1.0f : 1.0f) * (0.5f * screen.widthM + gapM + 0.5f * rail.widthM);
    rail.pose.orientation = screen.pose.orientation;
    rail.pose.position = {screen.pose.position.x + right.x * shift, screen.pose.position.y + right.y * shift,
        screen.pose.position.z + right.z * shift};
    return rail;
}

float SlideEase(float seconds) {
    float const t = std::isfinite(seconds) ? std::clamp(seconds / kSlideS, 0.0f, 1.0f) : (seconds > 0.0f ? 1.0f : 0.0f);
    return t*t*(3.0f-2.0f*t); // cubic smoothstep: zero endpoint velocity, peak slope 1.5
}


namespace {
XrQuaternionf Product(XrQuaternionf const& a, XrQuaternionf const& b) {
    return {a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y, a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w, a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z};
}
XrQuaternionf Inverse(XrQuaternionf const& q) { return {-q.x, -q.y, -q.z, q.w}; }
}

Rail Interpolate(Rail const& from, Rail const& to, float seconds) {
    float const t = SlideEase(seconds);
    auto mix = [t](float a, float b) { return a + (b-a)*t; };
    XrQuaternionf a = from.pose.orientation, b = to.pose.orientation;
    if (a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w < 0.0f) b = {-b.x, -b.y, -b.z, -b.w};
    XrQuaternionf q{mix(a.x,b.x), mix(a.y,b.y), mix(a.z,b.z), mix(a.w,b.w)};
    float const length = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    q = length > 1e-6f ? XrQuaternionf{q.x/length,q.y/length,q.z/length,q.w/length} : XrQuaternionf{0,0,0,1};
    return {{q, {mix(from.pose.position.x,to.pose.position.x), mix(from.pose.position.y,to.pose.position.y),
        mix(from.pose.position.z,to.pose.position.z)}}, mix(from.widthM,to.widthM), mix(from.heightM,to.heightM)};
}

Rail Tucked(world_ui::Panel const& screen, float side) {
    Rail rail = Beside(screen, side);
    XrVector3f const offset = overlay::Rotate(screen.pose.orientation,
        {(side < 0.0f ? 1.0f : -1.0f) * (rail.widthM + kGapM), 0.0f, -0.005f});
    rail.pose.position.x += offset.x;
    rail.pose.position.y += offset.y;
    rail.pose.position.z += offset.z;
    return rail;
}

std::optional<Face> Exposed(Rail const& rail, world_ui::Panel const& screen, float side) {
    if (!(rail.widthM > 0.0f)) return std::nullopt;
    XrVector3f const local = overlay::Rotate(Inverse(screen.pose.orientation),
        {rail.pose.position.x-screen.pose.position.x,rail.pose.position.y-screen.pose.position.y,rail.pose.position.z-screen.pose.position.z});
    float const boundary = (side < 0.0f ? -0.5f : 0.5f) * screen.widthM - local.x;
    float const lo = side < 0.0f ? -rail.widthM/2 : std::max(-rail.widthM/2,boundary);
    float const hi = side < 0.0f ? std::min(rail.widthM/2,boundary) : rail.widthM/2;
    if (!(hi-lo > 1e-5f)) return std::nullopt;
    Face face{rail,(lo+rail.widthM/2)/rail.widthM,(hi+rail.widthM/2)/rail.widthM};
    XrVector3f const offset = overlay::Rotate(rail.pose.orientation,{(lo+hi)/2,0,0});
    face.rail.pose.position.x += offset.x;
    face.rail.pose.position.y += offset.y;
    face.rail.pose.position.z += offset.z;
    face.rail.widthM = hi-lo;
    return face;
}

void Slide::Reset() { *this = Slide{}; }

float Slide::Age() const { return std::max(0.0f, ageS_ - world_ui::kUnfoldS); }
float Slide::Opacity() const { return FadingAlone() ? 1.0f-ageS_/kAloneFadeS : 1.0f; }

Rail Slide::Step(Rail const& target, bool beside, float side, float dt, std::optional<Rail> const& tucked, float screenAgeS) {
    started_ = beside && (!beside_ || side != side_);
    if (started_) {
        fadeAlone_ = have_ && !beside_;
        alone_ = drawn_;
        from_ = tucked.value_or(target); // never traverse the screen from the standalone rail
        XrQuaternionf const inverse = Inverse(target.pose.orientation);
        from_.pose.position = overlay::Rotate(inverse, {from_.pose.position.x-target.pose.position.x,
            from_.pose.position.y-target.pose.position.y, from_.pose.position.z-target.pose.position.z});
        from_.pose.orientation = Product(inverse, from_.pose.orientation);
        ageS_ = std::isfinite(screenAgeS) ? std::clamp(screenAgeS, 0.0f, world_ui::kUnfoldS) : 0.0f;
    } else if (beside && std::isfinite(dt)) {
        ageS_ = std::clamp(ageS_ + std::max(dt, 0.0f), 0.0f, world_ui::kUnfoldS+kSlideS);
    }
    drawn_ = target;
    if (beside && Age() < kSlideS) {
        Rail from = from_;
        XrVector3f const offset = overlay::Rotate(target.pose.orientation, from.pose.position);
        from.pose.position = {target.pose.position.x+offset.x, target.pose.position.y+offset.y, target.pose.position.z+offset.z};
        from.pose.orientation = Product(target.pose.orientation, from.pose.orientation);
        drawn_ = Interpolate(from, target, Age());
    }
    if (beside && fadeAlone_ && ageS_ < kAloneFadeS) drawn_ = alone_;
    have_ = true;
    beside_ = beside;
    side_ = side;
    return drawn_;
}

std::optional<Rail> Alone(XrVector3f const& eyes, XrVector3f const& wrist, float heightM, float aboveM, float towardM, float aspect) {
    if (!(heightM > 0.02f) || !(aspect > 0.01f)) {
        return std::nullopt;
    }
    float const width = heightM * aspect;
    auto const panel = world_ui::WristPlace(eyes, wrist, width, aboveM, towardM, 1.0f / aspect);
    if (!panel) {
        return std::nullopt;
    }
    return Rail{panel->pose, panel->widthM, panel->heightM};
}

bool Tap::Step(bool tracked, XrVector3f const& tip, XrVector3f const& wrist, bool allowed, float dt) {
    cooldown_ = std::max(0.0f, cooldown_ - std::max(dt, 0.0f));
    if (!tracked) {
        armed_ = false;  // a hand found again inside the zone isn't a tap
        return false;
    }
    float const d = Distance(tip, wrist);
    if (!std::isfinite(d)) {
        return false;
    }
    if (d > settings_.releaseM) {
        armed_ = true;
        return false;
    }
    if (armed_ && d < settings_.pressM && allowed && cooldown_ <= 0.0f) {
        armed_ = false;
        cooldown_ = settings_.cooldownS;
        return true;
    }
    if (d < settings_.pressM && !allowed) {
        armed_ = false;  // in the zone while taps are off (going for the screen): it must leave and come back to tap
    }
    return false;
}

XrVector3f TargetPoint(XrVector3f const& wrist) {
    return {wrist.x, wrist.y - kTargetBelowM, wrist.z};
}

namespace {
bool Finite(XrVector3f const& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
} // namespace

std::optional<XrVector3f> BodyTarget(Body const& body, XrVector3f const& eyes) {
    if (!Finite(body.leftWrist) || !Finite(body.leftElbow) || !Finite(eyes) || Distance(body.leftWrist, body.leftElbow) < 0.05f) {
        return std::nullopt;
    }
    XrVector3f const on{body.leftWrist.x + (body.leftElbow.x - body.leftWrist.x) * kForearmAlong,
        body.leftWrist.y + (body.leftElbow.y - body.leftWrist.y) * kForearmAlong,
        body.leftWrist.z + (body.leftElbow.z - body.leftWrist.z) * kForearmAlong};
    float const toEyes = Distance(on, eyes);
    if (!(toEyes > kForearmLiftM * 2.0f)) {
        return std::nullopt;
    }
    float const k = kForearmLiftM / toEyes;
    return XrVector3f{on.x + (eyes.x - on.x) * k, on.y + (eyes.y - on.y) * k, on.z + (eyes.z - on.z) * k};
}

std::optional<XrVector3f> BodyFingertip(Body const& body) {
    if (!Finite(body.rightIndex1) || !Finite(body.rightIndex2) || Distance(body.rightIndex1, body.rightIndex2) < 0.005f) {
        return std::nullopt;
    }
    return XrVector3f{body.rightIndex2.x + (body.rightIndex2.x - body.rightIndex1.x) * kFingertipRatio,
        body.rightIndex2.y + (body.rightIndex2.y - body.rightIndex1.y) * kFingertipRatio,
        body.rightIndex2.z + (body.rightIndex2.z - body.rightIndex1.z) * kFingertipRatio};
}

TargetLook TargetLookFor(bool allowed, XrVector3f const& eyes, XrVector3f const& target, bool tipTracked, XrVector3f const& tip,
    bool armed, float releaseM) {
    if (!allowed) {
        return TargetLook::Hidden;
    }
    if (tipTracked) {
        float const d = Distance(tip, target);
        if (std::isfinite(d)) {
            if (armed && d <= releaseM) {
                return TargetLook::Armed;
            }
            if (d <= kTargetNearM) {
                return TargetLook::Near;
            }
        }
    }
    float const below = eyes.y - target.y;
    float const across = std::hypot(target.x - eyes.x, target.z - eyes.z);
    if (std::isfinite(below) && std::isfinite(across) && below <= kRaisedBelowEyesM && across <= kRaisedAcrossM) {
        return TargetLook::Faint;
    }
    return TargetLook::Hidden;
}

bool GoingForPanel(XrVector3f const& tip, world_ui::Panel const& panel, float depthM, float marginM) {
    if (!(panel.widthM > 0.0f) || !(panel.heightM > 0.0f)) {
        return false;
    }
    XrQuaternionf const& q = panel.pose.orientation;
    XrQuaternionf const inverse{-q.x, -q.y, -q.z, q.w};
    XrVector3f const rel{tip.x - panel.pose.position.x, tip.y - panel.pose.position.y, tip.z - panel.pose.position.z};
    XrVector3f const local = overlay::Rotate(inverse, rel);  // +Z: in front of its face
    return local.z <= depthM && local.z >= -kTapPanelBehindM && std::fabs(local.x) <= 0.5f * panel.widthM + marginM &&
           std::fabs(local.y) <= 0.5f * panel.heightM + marginM;
}

} // namespace mevr::vr::omni_rail
