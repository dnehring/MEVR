// SPDX-License-Identifier: MIT
// The motion controllers as the game's pad with the mod's flags (plugin/PadMerge.hpp), for both Halcyon hosts: LE1's
// plugin/HalcyonBridge.cpp MergeMotionControllers moved here unchanged, its log lines word for word.

#include "plugin/PadMerge.hpp"

#include "core/Log.hpp"
#include "core/vr/Comfort.hpp"

#include <Windows.h>

#include <atomic>
#include <format>
#include <mutex>
#include <optional>
#include <string>

namespace mevr::host::pad_merge {
namespace {

namespace gp = game::gamepad;

// The mod's two-handed grip input (HostServices::setSupportGrip), and the mapping's answer.
std::atomic<float> gSupportDistance{-1.0f};
std::atomic<float> gSupportAcquire{0.12f};
std::atomic<float> gSupportRelease{0.22f};
std::atomic<std::uint32_t> gSupportFlags{gp::kFlagLeftTrigger | gp::kFlagSupportAims};
std::atomic<std::uint64_t> gSupportInputMs{0};
std::atomic<bool> gSupportHeld{false};
// The body holsters: the mapping's right-grip report (gamepad::RightGripReport).
std::atomic<std::uint32_t> gRightGripReport{0};
std::atomic<bool> gLoggedRightGripFree{false};
std::atomic<bool> gLoggedRightGripHolds{false};
// The squad weapon menu (v11): the mapping's left-grip report (gamepad::LeftGripReport).
std::atomic<std::uint32_t> gLeftGripReport{0};

std::mutex gMutex;  // the latches and the log state below (XInput is polled from any thread)
gp::Latches gLatches;
bool gSquadMenuLogged = false;    // whether the mod's flag says the squad menu is open (logged when it changes)
bool gNativeWheelLogged = false;  // the mod holds the game's weapon wheel (v12)
bool gWheelPressALogged = false;
// Comfort (v17, game/comfort in the mod, research/31): whether the mod turns the player (logged when it changes), and
// walking toward the left hand: whether it's on, and the left controller's last usable flat yaw (+ left, radians).
bool gModTurnsLogged = false;
std::uint64_t gModTurnsLogMs = 0;  // the next change may be logged from then
bool gHandWalkLogged = false;
std::uint64_t gHandWalkLogMs = 0;
std::optional<double> gHandYaw;

// The motion controllers' left stick turned so its forward is the left controller's yaw, laid flat (the mod's
// kFlagHandWalk: on foot, outside menus and wheels). The renderer's moveFollowsHead then turns the merged stick by its head
// look yaw (after this): HandWalkUnits takes that off first, so the two together turn it by the hand's yaw. The pointing
// preselect and the support grip read the controllers' poses, never this stick. gMutex held.
void WalkTowardHand(XINPUT_GAMEPAD& pad, vr::controllers::State const& controllers, bool on, Renderer const& renderer,
    std::uint64_t nowMs) {
    namespace cf = vr::comfort;
    if (on != gHandWalkLogged && nowMs >= gHandWalkLogMs) {  // at most every 2 s
        gHandWalkLogged = on;
        gHandWalkLogMs = nowMs + 2000;
        log::Info("halcyon bridge: the left stick walks toward {}", on
            ? "the left controller's pointing, laid flat (comfort: walk toward the left hand)"
            : "where the player looks again (Halcyon's moveFollowsHead)");
    }
    if (!on) {
        gHandYaw.reset();
        return;
    }
    if (controllers.left.aimValid) {
        XrQuaternionf const& q = controllers.left.aim.orientation;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        cf::Forward(q.x, q.y, q.z, q.w, x, y, z);
        if (std::optional<double> const yaw = cf::FlatYawLeft(x, y, z)) {
            gHandYaw = yaw;  // pointing (nearly) straight up or down keeps the last one
        }
    }
    if (!gHandYaw || (pad.sThumbLX == 0 && pad.sThumbLY == 0)) {
        return;
    }
    int const headLook = renderer.headLookYawUU != nullptr ? renderer.headLookYawUU() : 0;
    gp::RotateStick(pad.sThumbLX, pad.sThumbLY, cf::HandWalkUnits(*gHandYaw, headLook));
}

} // namespace

void SetSupportGrip(float distanceMeters, float acquireMeters, float releaseMeters, std::uint32_t flags) {
    gSupportDistance.store(distanceMeters, std::memory_order_relaxed);
    gSupportAcquire.store(acquireMeters, std::memory_order_relaxed);
    gSupportRelease.store(releaseMeters, std::memory_order_relaxed);
    gSupportFlags.store(flags, std::memory_order_relaxed);
    gSupportInputMs.store(GetTickCount64(), std::memory_order_relaxed);
}

void ForgetSupportGrip() {
    gSupportInputMs.store(0, std::memory_order_relaxed);
}

gp::Options Options(std::uint64_t nowMs) {
    std::uint64_t const at = gSupportInputMs.load(std::memory_order_relaxed);
    if (at == 0 || nowMs - at > kInputMaxAgeMs) {
        return {};
    }
    return gp::FromFlags(gSupportDistance.load(std::memory_order_relaxed), gSupportAcquire.load(std::memory_order_relaxed),
        gSupportRelease.load(std::memory_order_relaxed), gSupportFlags.load(std::memory_order_relaxed));
}

std::uint32_t FreshFlags(std::uint64_t nowMs) {
    bool const fresh = nowMs - gSupportInputMs.load(std::memory_order_relaxed) <= kInputMaxAgeMs;
    return fresh ? gSupportFlags.load(std::memory_order_relaxed) : 0u;
}

bool SupportGripHeld() {
    return gSupportHeld.load(std::memory_order_relaxed);
}

std::uint32_t RightGripFree() {
    return gRightGripReport.load(std::memory_order_relaxed);
}

std::uint32_t LeftGripAway() {
    return gLeftGripReport.load(std::memory_order_relaxed);
}

void NoControllers() {
    gSupportHeld.store(false, std::memory_order_relaxed);
}

XINPUT_GAMEPAD Merge(XINPUT_GAMEPAD const& real, vr::controllers::State const& controllers, Renderer const& renderer,
    std::uint64_t nowMs) {
    std::lock_guard lock(gMutex);
    gp::Options options = Options(nowMs);
    options.gateRightClick = renderer.gateRightClick;  // host-only, never across the boundary (lane D2's gate; LE1 false)
    if (renderer.ownMenuOpen) {
        // A squeeze in the menu never draws a weapon (the menu zeroes the game's pad). One that holds
        // the gun still isn't RB, so it doesn't reach the game as RB once the menu closes.
        options.rightGripFree = false;
        options.countKept = false;
    }
    XINPUT_GAMEPAD touch = gp::FromControllers(controllers, gLatches, options);
    bool const owned = options.squadMenu || options.nativeWheel;  // the squad menu or the game's wheel has the sticks
    WalkTowardHand(touch, controllers, options.handWalk && !owned && !renderer.ownMenuOpen, renderer, nowMs);
    if (options.modTurns != gModTurnsLogged && nowMs >= gModTurnsLogMs) {  // at most every 2 s (camera blends)
        gModTurnsLogged = options.modTurns;
        gModTurnsLogMs = nowMs + 2000;
        log::Info("halcyon bridge: {}", options.modTurns
            ? "the mod turns the player (comfort: snap or its own smooth turn): the game sees no right-stick X from the motion controllers"
            : "the game turns the player with the right stick again");
    }
    XINPUT_GAMEPAD pad = gp::Merge(real, touch);
    gLeftGripReport.store(gp::LeftGripReport(gLatches), std::memory_order_relaxed);
    if (options.squadMenu != gSquadMenuLogged) {
        gSquadMenuLogged = options.squadMenu;
        log::Info("halcyon bridge: squad menu {}", options.squadMenu
            ? "open: the sticks, A, B and the stick clicks are its own (kept from the game until released)"
            : "closed");
    }
    if (options.nativeWheel != gNativeWheelLogged) {
        gNativeWheelLogged = options.nativeWheel;
        log::Info("halcyon bridge: the game's weapon wheel {}", options.nativeWheel
            ? std::format("held by the left grip: {} steers it (once centred), A queues; the left stick, B and the stick "
                          "clicks are kept from the game", gp::Name(options.wheelStick))
            : std::string("let go (what was in use stays kept from the game until released or centred)"));
    }
    if (options.nativeWheel && options.wheelPressA != gWheelPressALogged) {
        gWheelPressALogged = options.wheelPressA;
        if (options.wheelPressA) {
            log::Info("halcyon bridge: A pressed for the mod (confirming the highlighted entry on release)");
        }
    }
    std::uint32_t const report = gp::RightGripReport(gLatches);
    if (gRightGripReport.exchange(report, std::memory_order_relaxed) != report && (report & 1u) != 0) {
        if (gLatches.rightHold) {
            if (!gLoggedRightGripHolds.exchange(true, std::memory_order_relaxed)) {
                log::Info("halcyon bridge: first right-grip squeeze that holds the gun: kept from the game (no RB); RB "
                          "comes from {}", gp::Name(options.rbSource));
            }
        } else if (!gLoggedRightGripFree.exchange(true, std::memory_order_relaxed)) {
            log::Info("halcyon bridge: first right-grip squeeze at a holster: kept from the game (no RB)");
        }
    }
    bool const held = gLatches.left == gp::LeftGrip::Support;
    if (gSupportHeld.exchange(held, std::memory_order_relaxed) != held) {
        log::Info("halcyon bridge: support grip {} (left hand {:.2f} m from the foregrip){}", held ? "taken" : "let go",
            gSupportDistance.load(std::memory_order_relaxed),
            held ? (Options(nowMs).supportAims ? ": LT to the game instead of LB" : ": no LT (sniper rifle)") : "");
    }
    if (controllers.left.stickClick && controllers.right.stickClick) {
        // The both-sticks chord (our menu held, a snapshot tapped), not L3 + R3. (L3 alone is held back briefly by the
        // mapping, gamepad::GateClick, so a chord pressed left first isn't a crouch.)
        pad.wButtons &= ~(XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB);
    }
    if (renderer.ownsLeftTrigger != nullptr && renderer.ownsLeftTrigger()) {
        pad.bLeftTrigger = 0;
    }
    if (renderer.ownsTriggers != nullptr && renderer.ownsTriggers()) {
        pad.bLeftTrigger = 0;  // a quad of ours is pointed at (the calibration's prompt, the omni-tool's rail, the game's flat
        pad.bRightTrigger = 0; // screen ...): the trigger is its click there, not a shot
    }
    return pad;
}

void Reset() {
    std::lock_guard lock(gMutex);
    gSupportDistance.store(-1.0f);
    gSupportAcquire.store(0.12f);
    gSupportRelease.store(0.22f);
    gSupportFlags.store(gp::kFlagLeftTrigger | gp::kFlagSupportAims);
    gSupportInputMs.store(0);
    gSupportHeld.store(false);
    gRightGripReport.store(0);
    gLoggedRightGripFree.store(false);
    gLoggedRightGripHolds.store(false);
    gLeftGripReport.store(0);
    gLatches = {};
    gSquadMenuLogged = gNativeWheelLogged = gWheelPressALogged = false;
    gModTurnsLogged = false;
    gModTurnsLogMs = 0;
    gHandWalkLogged = false;
    gHandWalkLogMs = 0;
    gHandYaw.reset();
}

} // namespace mevr::host::pad_merge
