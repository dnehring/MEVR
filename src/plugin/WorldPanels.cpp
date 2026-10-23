// SPDX-License-Identifier: MIT
// World-locked flat panels (seam in src/halcyon/le1/mevr_bridge.h, MevrBridge::PlacePanel): Halcyon's
// flat screen (menus, galaxy map, flat conversations, cutscenes, movies) and its Insert menu stay
// put in the app space where they appeared, instead of riding the head (VIEW space). The placement
// rules are core/vr/OverlayPlacement's WorldPanel (unit-tested); this file feeds it Halcyon's
// frames and settings (MELEVR.ini worldLockedPanels, panelFollowDeg).

#include "plugin/WorldPanels.hpp"

#include "halcyon/le1/mevr_bridge.h"
#include "halcyon/le1/vr_config.h"

#include "core/Log.hpp"
#include "core/vr/OverlayPlacement.hpp"

#include <openxr/openxr.h>

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace MELEVR::MevrBridge {
namespace {

namespace overlay = mevr::vr::overlay;

constexpr float kDegToRad = 3.14159265358979f / 180.0f;
constexpr unsigned kMaxPlacementLogs = 200;  // a kind that flickers mustn't flood the log

// Present thread only.
struct Slot {
    overlay::WorldPanel panel;
    bool on = false;            // world-locked (the setting was on the last time it was shown)
    void* appSpace = nullptr;   // the app space it was placed in: a new one (recenter, new session) re-places
    std::uint64_t lastMs = 0;   // for dt
};
Slot gSlots[2];  // 0: the flat screen, 1: the Insert menu
std::uint64_t gFrame = 1;
unsigned gPlacementLogs = 0;

char const* KindName(PanelKind kind) {
    switch (kind) {
    case PanelKind::InsertMenu: return "Insert menu";
    case PanelKind::GameMenu: return "game menu";
    case PanelKind::GalaxyMap: return "galaxy map";
    case PanelKind::Conversation: return "conversation";
    case PanelKind::Cutscene: return "cutscene/movie";
    }
    return "?";
}

} // namespace

void NoteWorldPanelFrame() noexcept {
    ++gFrame;
}

void RecenterWorldPanels() noexcept {
    for (Slot& slot : gSlots) {
        slot.panel.Recenter();
    }
}

PanelPose PlacePanel(PanelKind kind, float const viewPosition[3], void* viewSpace, void* appSpace, bool headValid,
    float const headOrientation[4], float const headPosition[3]) noexcept {
    PanelPose out;
    out.space = viewSpace;
    out.position[0] = viewPosition[0];
    out.position[1] = viewPosition[1];
    out.position[2] = viewPosition[2];
    out.headLocked = true;

    auto const& cfg = MELEVR::Config::Get();
    Slot& slot = gSlots[kind == PanelKind::InsertMenu ? 1 : 0];
    bool const on = cfg.worldLockedPanels && appSpace != nullptr;
    if (on != slot.on) {
        mevr::log::Info("world panels: the {} slot is {}", kind == PanelKind::InsertMenu ? "Insert menu" : "flat screen",
            on ? "world-locked (stays where it appears)" : "head-locked (Halcyon's VIEW space)");
        slot = Slot{};
        slot.on = on;
    }
    if (!on) {
        return out;
    }
    if (appSpace != slot.appSpace) {
        if (slot.appSpace != nullptr) {
            slot.panel.Recenter();  // the old pose is in a space that's gone
        }
        slot.appSpace = appSpace;
    }

    overlay::WorldPanelSettings settings;
    settings.followAfter = cfg.panelFollowDeg > 0.0f ? std::clamp(cfg.panelFollowDeg, 10.0f, 170.0f) * kDegToRad : 0.0f;
    XrPosef head{};
    head.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    if (headValid) {
        head.orientation = {headOrientation[0], headOrientation[1], headOrientation[2], headOrientation[3]};
        head.position = {headPosition[0], headPosition[1], headPosition[2]};
        bool const finite = std::isfinite(head.orientation.x) && std::isfinite(head.orientation.y) &&
            std::isfinite(head.orientation.z) && std::isfinite(head.orientation.w) && std::isfinite(head.position.x) &&
            std::isfinite(head.position.y) && std::isfinite(head.position.z);
        headValid = finite;
    }
    std::uint64_t const now = GetTickCount64();
    float const dt = slot.lastMs == 0 ? 0.0f : static_cast<float>(now - slot.lastMs) / 1000.0f;
    slot.lastMs = now;

    overlay::Placement const why = slot.panel.Update(static_cast<int>(kind), gFrame, head, headValid, settings, dt);
    XrPosef const pose = slot.panel.PoseOf({viewPosition[0], viewPosition[1], viewPosition[2]});
    if (why != overlay::Placement::Kept && gPlacementLogs < kMaxPlacementLogs) {
        ++gPlacementLogs;
        XrVector3f const anchor = slot.panel.Anchor();
        mevr::log::Info("world panels: {} {}: heading {:.0f} deg, eyes at ({:.2f}, {:.2f}, {:.2f}), quad at ({:.2f}, "
                        "{:.2f}, {:.2f}) m{}",
            KindName(kind), overlay::PlacementName(why), slot.panel.Yaw() / kDegToRad, anchor.x, anchor.y, anchor.z,
            pose.position.x, pose.position.y, pose.position.z, headValid ? "" : " (no head yet: at the origin)");
    }
    out.space = appSpace;
    out.orientation[0] = pose.orientation.x;
    out.orientation[1] = pose.orientation.y;
    out.orientation[2] = pose.orientation.z;
    out.orientation[3] = pose.orientation.w;
    out.position[0] = pose.position.x;
    out.position[1] = pose.position.y;
    out.position[2] = pose.position.z;
    out.headLocked = false;
    return out;
}

} // namespace MELEVR::MevrBridge
