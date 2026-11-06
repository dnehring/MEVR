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

Rail Beside(world_ui::Panel const& screen, float gapM, float aspect) {
    Rail rail;
    rail.heightM = screen.heightM;
    rail.widthM = screen.heightM * aspect;
    XrVector3f const right = overlay::Rotate(screen.pose.orientation, {1.0f, 0.0f, 0.0f});
    float const shift = 0.5f * screen.widthM + gapM + 0.5f * rail.widthM;
    rail.pose.orientation = screen.pose.orientation;
    rail.pose.position = {screen.pose.position.x - right.x * shift, screen.pose.position.y - right.y * shift,
        screen.pose.position.z - right.z * shift};
    return rail;
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
