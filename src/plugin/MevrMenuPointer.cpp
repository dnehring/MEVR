// SPDX-License-Identifier: MIT
// Laser pointer for Halcyon's menu (src/halcyon/le1/vr_menu.cpp): a motion controller's aim ray
// on the menu quad (head-locked, or fixed in the app space with world-locked panels) drives ImGui's mouse. The trigger is the left button, the right
// stick scrolls, and a dot marks where the ray lands. Either hand points: the right one by default,
// the left one once it points at the menu while the right doesn't (or pulls its trigger on it).
// Gamepad navigation (Touch through Halcyon's pad read, or a real pad) keeps working alongside.

#include "halcyon/le1/mevr_bridge.h"
#include "plugin/MevrMenu.hpp"
#include "plugin/TestLoop.hpp"

#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/MenuPointer.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace MELEVR::MevrBridge {
namespace {

namespace pointer = mevr::vr::menu_pointer;
namespace controllers = mevr::vr::controllers;

// Present thread only (RunFrame submits the quad, then the next RenderFrame samples the pointer).
pointer::Quad gQuad;                  // in the app space, the controllers' space
XrPosef gHead{};                      // the head (VIEW) when it was submitted
std::uint64_t gQuadMs = 0;            // when the quad was last submitted; 0 = never
constexpr std::uint64_t kQuadFreshMs = 250;

constexpr float kTriggerOn = 0.6f;
constexpr float kTriggerOff = 0.4f;
constexpr float kStickDeadZone = 0.2f;
constexpr float kWheelPerSecond = 12.0f;  // ImGui wheel steps per second at full deflection
constexpr float kSmoothing = 0.5f;        // per frame, of the way to the new point (hand tremor)

int gHand = 1;               // 0 left, 1 right: the hand that points
bool gTriggerLatched[2] = {false, false};
bool gPressing = false;      // the left button is down (the press began on the menu)
bool gHit = false;           // the last sample landed on the menu (or a press is being dragged)
bool gWasHit = false;
float gSmoothX = 0.0f;
float gSmoothY = 0.0f;
float gX = 0.0f;             // reported to ImGui
float gY = 0.0f;
constexpr float kDeadBandPx = 1.5f;
bool gLoggedFirstHit = false;

// The OS cursor and the pointer share ImGui's one mouse: whichever moved last owns it.
bool gPointerOwnsMouse = false;
long gOsX = 0;
long gOsY = 0;
bool gHaveOs = false;

bool Latch(bool& latched, float value) {
    latched = latched ? value > kTriggerOff : value >= kTriggerOn;
    return latched;
}

bool HitOf(controllers::Hand const& hand, pointer::Hit& hit) {
    return hand.aimValid &&
        pointer::Intersect(gQuad, hand.aim.position, pointer::AimDirection(hand.aim), hit);
}

} // namespace

void OnMenuQuad(float const headOrientation[4], float const headPosition[3], PanelPose const& placed, float width,
    float height) noexcept {
    gHead.orientation = {headOrientation[0], headOrientation[1], headOrientation[2], headOrientation[3]};
    gHead.position = {headPosition[0], headPosition[1], headPosition[2]};
    XrPosef local{};
    local.orientation = {placed.orientation[0], placed.orientation[1], placed.orientation[2], placed.orientation[3]};
    local.position = {placed.position[0], placed.position[1], placed.position[2]};
    // Head-locked: VIEW sits at the head. World-locked: the quad is in the app space already.
    XrPosef const identity{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    gQuad = pointer::InTracking(placed.headLocked ? gHead : identity, local, width, height);
    gQuadMs = GetTickCount64();
    mevr::host::testloop::NoteMenuQuad(placed.position, placed.headLocked, width, height);  // snapshots name its layer "menu"
}

bool SampleMenuPointer(int width, int height, float dt, MenuPointer* out) noexcept {
    if (out == nullptr) {
        return false;
    }
    *out = MenuPointer{};
    bool const wasHit = gWasHit;
    out->left = wasHit;  // until a hit below says otherwise
    gWasHit = false;
    gHit = false;
    std::uint64_t const now = GetTickCount64();
    controllers::State const state = controllers::Latest();
    bool const fresh = gQuadMs != 0 && now - gQuadMs < kQuadFreshMs && width > 0 && height > 0;
    if (!fresh || !state.active) {
        gPressing = false;
        gTriggerLatched[0] = gTriggerLatched[1] = false;
        return false;
    }
    controllers::Hand const* hands[2] = {&state.left, &state.right};
    bool triggers[2];
    for (int i = 0; i < 2; ++i) {
        triggers[i] = Latch(gTriggerLatched[i], hands[i]->trigger);
    }
    pointer::Hit hits[2];
    bool on[2] = {HitOf(state.left, hits[0]), HitOf(state.right, hits[1])};

    // Which hand points: keep the pointing hand while it's on the menu (and through a drag); else
    // the other one if it is on the menu.
    int const other = 1 - gHand;
    if (!gPressing && on[other] && (!on[gHand] || (triggers[other] && !triggers[gHand]))) {
        gHand = other;
        mevr::log::Info("menu pointer: the {} controller points", gHand == 1 ? "right" : "left");
    }
    bool const pointing = on[gHand];
    if (pointing) {
        float const x = std::clamp(hits[gHand].u, 0.0f, 1.0f) * static_cast<float>(width);
        float const y = std::clamp(hits[gHand].v, 0.0f, 1.0f) * static_cast<float>(height);
        bool const arrived = !wasHit;  // no smoothing from a stale point
        gSmoothX = arrived ? x : gSmoothX + (x - gSmoothX) * kSmoothing;
        gSmoothY = arrived ? y : gSmoothY + (y - gSmoothY) * kSmoothing;
        // A small dead band, so a steady hand doesn't keep moving ImGui's mouse (which hides the
        // gamepad navigation highlight).
        if (arrived || std::fabs(gSmoothX - gX) > kDeadBandPx || std::fabs(gSmoothY - gY) > kDeadBandPx) {
            gX = gSmoothX;
            gY = gSmoothY;
        }
        if (!gLoggedFirstHit) {
            gLoggedFirstHit = true;
            mevr::log::Info("menu pointer: the {} controller's ray is on the menu ({:.0f}, {:.0f}) px, {:.2f} m away",
                gHand == 1 ? "right" : "left", gX, gY, hits[gHand].distance);
        }
    }
    // A press starts only on the menu; once started, it lasts until the trigger lets go, even if
    // the ray slides off (dragging a slider past its end).
    if (gPressing) {
        gPressing = triggers[gHand];
    } else {
        gPressing = pointing && triggers[gHand];
    }
    gHit = pointing || gPressing;
    gWasHit = gHit;
    if (!gHit) {
        return false;
    }
    out->hit = true;
    out->left = false;
    out->x = gX;
    out->y = gY;
    out->click = gPressing;
    float const stick = state.right.stickY;
    if (std::fabs(stick) > kStickDeadZone && dt > 0.0f) {
        out->wheel = stick * kWheelPerSecond * std::min(dt, 0.1f);
    }
    return true;
}

bool LatestMenuHead(XrPosef& head, unsigned maxAgeMs) noexcept {
    if (gQuadMs == 0 || GetTickCount64() - gQuadMs >= maxAgeMs) {
        return false;
    }
    head = gHead;
    return true;
}

bool MenuMouseFromOs(long x, long y, bool pointerHit) noexcept {
    bool const moved = !gHaveOs || x != gOsX || y != gOsY;
    gHaveOs = true;
    gOsX = x;
    gOsY = y;
    if (pointerHit) {
        gPointerOwnsMouse = true;
    } else if (moved) {
        gPointerOwnsMouse = false;
    }
    return !gPointerOwnsMouse;
}

void DrawMenuPointer() noexcept {
    if (!gHit) {
        return;
    }
    ImDrawList* const draw = ImGui::GetForegroundDrawList();
    ImVec2 const center(gX, gY);
    ImU32 const fill = gPressing ? IM_COL32(255, 170, 40, 255) : IM_COL32(80, 200, 255, 255);
    draw->AddCircleFilled(center, 9.0f, IM_COL32(0, 0, 0, 200));
    draw->AddCircleFilled(center, 6.0f, fill);
}

} // namespace MELEVR::MevrBridge
