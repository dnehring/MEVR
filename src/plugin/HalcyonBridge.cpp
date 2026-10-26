// SPDX-License-Identifier: MIT
// Our side of the seams in Halcyon's renderer (src/halcyon/le1/mevr_bridge.h): the OpenXR loader,
// controller actions on Halcyon's session, motion controllers as Xbox pad 0, the view published
// for mapping the controllers into the world, and controller aim replacing Halcyon's head aim.

#include "halcyon/le1/mevr_bridge.h"
#include "halcyon/le1/vr_config.h"
#include "halcyon/le1/vr_menu.h"

#include "core/Log.hpp"
#include "core/Window.hpp"
#include "core/vr/CombatStance.hpp"
#include "core/vr/Comfort.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/Floor.hpp"
#include "core/vr/FrameProbe.hpp"
#include "core/vr/Haptics.hpp"
#include "core/vr/HostView.hpp"
#include "core/xr/Floor.hpp"
#include "core/xr/Input.hpp"
#include "game/GamepadMapping.hpp"
#include "halcyon/le1/le1_game.h"
#include "halcyon/le1/render_hook.h"
#include "plugin/Calibration.hpp"
#include "plugin/Colour.hpp"
#include "plugin/CrashBreadcrumbs.hpp"
#include "plugin/GameThreadCalls.hpp"
#include "plugin/Halcyon.hpp"
#if defined(MEVR_WITH_MAIN_MENU)
#include "plugin/MainMenu.hpp"
#endif
#include "plugin/MevrMenu.hpp"
#include "plugin/DrawReplay.hpp"
#include "plugin/PerfProbe.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/WorldPanels.hpp"

#include <dxgi1_6.h>   // [MEVR] research/27 7c: the HDR-display answer
#include <openxr/openxr.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iterator>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <string>

namespace MELEVR::MevrBridge {
namespace {

mevr::xr::Input gInput;
mevr::xr::Input::Target gTarget;  // set on Halcyon's XR thread before frames start, then Present thread
bool gInputFailed = false;
std::atomic<HWND> gGameWindow{nullptr};
bool gWasFocused = false;  // Present thread
// After the headset gains focus, keep trying (once a second, for a while) until the game window is
// the foreground window: the first focused frame can come before the window is known, and Windows
// may refuse a single attempt.
constexpr std::uint64_t kForegroundTryMs = 15000;
std::uint64_t gForegroundUntilMs = 0;
std::uint64_t gForegroundNextMs = 0;
// Both thumbsticks clicked and held (gamepad::kStickHoldMs) opens or closes Halcyon's menu, like its
// menu key (\ in our deploy; Insert upstream, which a profile slot can bring back). The menu blocks all
// game input while open. Both tapped together (down and up within gamepad::kStickTapMs) is the test
// loop's snapshot. Present thread, on the player's own clicks.
mevr::game::gamepad::StickChord gStickChord;
std::atomic<unsigned> gFrames{0};

std::mutex gPadMutex;  // the latches and packet numbering below (XInput is polled from any thread)
mevr::game::gamepad::Latches gLatches;
XINPUT_GAMEPAD gLastPad{};
DWORD gPacket = 0;

// The mod's two-handed grip input (HostServices::setSupportGrip), and the mapping's answer.
constexpr std::uint64_t kSupportInputMaxAgeMs = 250;
std::atomic<float> gSupportDistance{-1.0f};
std::atomic<float> gSupportAcquire{0.12f};
std::atomic<float> gSupportRelease{0.22f};
std::atomic<unsigned> gSupportFlags{mevr::game::gamepad::kFlagLeftTrigger | mevr::game::gamepad::kFlagSupportAims};
std::atomic<std::uint64_t> gSupportInputMs{0};
std::atomic<bool> gSupportHeld{false};
// The body holsters: the mapping's right-grip report (gamepad::RightGripReport), and each hand's
// vibration (pulses and the continuous contact buzz, core/vr/Haptics), stepped after the action
// sync on the frame thread.
std::atomic<std::uint32_t> gRightGripReport{0};
std::atomic<bool> gLoggedRightGripFree{false};
std::atomic<bool> gLoggedRightGripHolds{false};
// The squad weapon menu (v11): the mapping's left-grip report (gamepad::LeftGripReport), and whether
// the mod's flag says the menu is open (logged when it changes).
std::atomic<std::uint32_t> gLeftGripReport{0};
bool gSquadMenuLogged = false;  // under gPadMutex
bool gNativeWheelLogged = false;  // under gPadMutex: the mod holds the game's weapon wheel (v12)
bool gWheelPressALogged = false;  // under gPadMutex
// Comfort (v17, game/comfort in the mod, research/31): whether the mod turns the player (logged when it changes), and
// walking toward the left hand: whether it's on, and the left controller's last usable flat yaw (+ left, radians).
bool gModTurnsLogged = false;  // under gPadMutex
std::uint64_t gModTurnsLogMs = 0;  // under gPadMutex: the next change may be logged from then
bool gHandWalkLogged = false;  // under gPadMutex
std::uint64_t gHandWalkLogMs = 0;  // under gPadMutex
std::optional<double> gHandYaw;  // under gPadMutex

// The motion controllers' left stick turned so its forward is the left controller's yaw, laid flat (the mod's
// kFlagHandWalk: on foot, outside menus and wheels). Halcyon's moveFollowsHead then turns the merged stick by its head
// look yaw (RotateMoveStickByHeadLook, after this): HandWalkUnits takes that off first, so the two together turn it by
// the hand's yaw. The pointing preselect and the support grip read the controllers' poses, never this stick.
void WalkTowardHand(XINPUT_GAMEPAD& pad, mevr::vr::controllers::State const& controllers, bool on) {
    namespace cf = mevr::vr::comfort;
    if (on != gHandWalkLogged && GetTickCount64() >= gHandWalkLogMs) {  // at most every 2 s
        gHandWalkLogged = on;
        gHandWalkLogMs = GetTickCount64() + 2000;
        mevr::log::Info("halcyon bridge: the left stick walks toward {}", on
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
    int const headLook = MELEVR::Config::Get().moveFollowsHead ? MELEVR::RenderHook::HeadLookYawUU() : 0;
    mevr::game::gamepad::RotateStick(pad.sThumbLX, pad.sThumbLY, cf::HandWalkUnits(*gHandYaw, headLook));
}
std::mutex gHapticMutex;  // gHaptics
std::array<mevr::vr::haptics::Channel, 2> gHaptics;
bool gLoggedHaptic = false;            // frame thread
unsigned gHapticFailures = 0;          // frame thread: in a row
constexpr unsigned kHapticFailuresToGiveUp = 5;
bool gHapticGaveUp = false;            // frame thread: for this session

// The head above the floor (core/xr/Floor): measured each frame, read by the mod (HostServices::floor).
mevr::xr::FloorSpace gFloor;  // XR thread at creation, then frame thread
std::mutex gFloorMutex;       // gFloorState
mevr::vr::floor::State gFloorState;
std::atomic<std::uint32_t> gRecenters{0};
std::atomic<std::uint32_t> gSessions{0};
bool gLoggedFloor = false;  // frame thread
// The bound interaction profile, asked for about once a second (frame thread): the guided calibration is kept
// per controller type (plugin/Calibration).
unsigned gProfileCheckFrames = 0;
constexpr unsigned kProfileCheckEvery = 90;
// Diagnostics (frame thread, research/28 "The game's menus"): the profile the runtime picked for each hand, logged when
// it changes, and the first time each controller's menu action reads true in a session. Logged only: nothing depends on
// the menu button (a Quest over Link, or SteamVR's emulation, may never send it); our main menu's buttons are the way in.
std::string gHandProfile[2];
bool gLoggedMenuButton[2] = {false, false};
// A pad button our main menu presses for the game (Game menu: Start; Holster: Back), under gPadMutex.
mevr::game::gamepad::MenuPress gMenuPress;
// What was down when our main menu or the calibration gave the pad back: kept from the game until let go (gPadMutex).
mevr::game::gamepad::CloseLatch gCloseLatch;
bool gCloseLatchKept = false;  // for the log: something is being kept

void ApplyPendingHaptics() {
    std::uint64_t const now = GetTickCount64();
    for (int hand : {0, 1}) {
        mevr::vr::haptics::Command command;
        {
            std::lock_guard lock(gHapticMutex);
            command = gHaptics[hand].Step(now);
        }
        using Kind = mevr::vr::haptics::Command::Kind;
        if (command.kind == Kind::None || gHapticGaveUp) {
            continue;
        }
        bool const ok = command.kind == Kind::Play
            ? gInput.Vibrate(gTarget, hand, command.seconds, command.amplitude, command.frequency)
            : gInput.StopVibration(gTarget, hand);
        if (command.kind == Kind::Play && !gLoggedHaptic) {
            gLoggedHaptic = true;
            mevr::log::Info("halcyon bridge: first controller vibration ({} hand, {:.0f} ms at {:.2f}, {}): {}",
                hand == 0 ? "left" : "right", command.seconds * 1000.0f, command.amplitude,
                command.frequency > 0.0f ? std::format("{:.0f} Hz", command.frequency) : std::string("the runtime's frequency"),
                ok ? "applied" : "refused (no vibration action, or the runtime said no)");
        }
        gHapticFailures = ok ? 0 : gHapticFailures + 1;
        if (gHapticFailures >= kHapticFailuresToGiveUp) {
            gHapticGaveUp = true;
            mevr::log::Warn("halcyon bridge: {} controller vibrations in a row failed: no more vibration this session",
                gHapticFailures);
        }
    }
}

// The floor height for this frame (render thread, after the sync).
void MeasureFloor(XrTime time) {
    std::optional<float> const height = gFloor.EyeHeight(time);
    {
        std::lock_guard lock(gFloorMutex);
        gFloorState.valid = height.has_value();
        if (height) {
            gFloorState.eyeHeight = *height;
        }
        gFloorState.source = gFloor.Source();
        gFloorState.recenters = gRecenters.load(std::memory_order_relaxed);
        gFloorState.sessions = gSessions.load(std::memory_order_relaxed);
        ++gFloorState.sequence;
        gFloorState.timeMs = GetTickCount64();
    }
    if (height && !gLoggedFloor) {
        gLoggedFloor = true;
        mevr::log::Info("floor: first sample: the head is {:.2f} m above the floor ({})", *height,
            mevr::vr::floor::Name(gFloor.Source()));
    }
}

// The pad options from the mod's latest input; defaults (no grip, LT from the trigger) when stale.
mevr::game::gamepad::Options PadOptions() {
    std::uint64_t const at = gSupportInputMs.load(std::memory_order_relaxed);
    if (at == 0 || GetTickCount64() - at > kSupportInputMaxAgeMs) {
        return {};
    }
    return mevr::game::gamepad::FromFlags(gSupportDistance.load(std::memory_order_relaxed),
        gSupportAcquire.load(std::memory_order_relaxed), gSupportRelease.load(std::memory_order_relaxed),
        gSupportFlags.load(std::memory_order_relaxed));
}

// Controller aim: the mod aims with the right controller (HostServices::setHandAim) and that
// controller's aim pose was tracked within the hold time, so a moment of lost tracking doesn't
// hand aiming back and forth between the head and the hand.
constexpr std::uint64_t kControllerAimHoldMs = 1000;
std::atomic<bool> gModHandAim{false};
std::atomic<std::uint64_t> gRightAimTrackedMs{0};
bool gControllerAim = false;  // Present thread: the last answer, to log the switches
bool gLoggedView = false;     // Present thread

} // namespace

void* StaticXrGetInstanceProcAddr() noexcept {
    return reinterpret_cast<void*>(&xrGetInstanceProcAddr);
}

void OnSessionCreated(void* instance, void* session) noexcept {
    if (gInput.Matches(gTarget)) {
        gInput.Release(gTarget);  // a new session (upstream creates one per process, but be safe)
    }
    gTarget.instance = static_cast<XrInstance>(instance);
    gTarget.session = static_cast<XrSession>(session);
    ++gTarget.instanceGeneration;
    ++gTarget.sessionGeneration;
    mevr::host::testloop::OnSessionCreated(gTarget.session);
    gInputFailed = !gInput.Create(gTarget);
    if (gInputFailed) {
        mevr::log::Error("halcyon bridge: controller actions failed; motion controllers unavailable");
    }
    // The headset's name, for the guided calibration's per-controller-type sections (plugin/Calibration).
    {
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrSystemId system = XR_NULL_SYSTEM_ID;
        XrSystemProperties properties{XR_TYPE_SYSTEM_PROPERTIES};
        if (XR_SUCCEEDED(xrGetSystem(gTarget.instance, &systemInfo, &system)) &&
            XR_SUCCEEDED(xrGetSystemProperties(gTarget.instance, system, &properties))) {
            mevr::host::calibration::SetSystem(properties.systemName);
            // The colour correction's per-headset settings and automatic profile (plugin/Colour, research/27).
            XrInstanceProperties runtime{XR_TYPE_INSTANCE_PROPERTIES};
            xrGetInstanceProperties(gTarget.instance, &runtime);
            mevr::host::colour::SetHeadset(runtime.runtimeName, properties.systemName, properties.vendorId);
        }
    }
    gProfileCheckFrames = 0;
    gHandProfile[0].clear();
    gHandProfile[1].clear();
    gLoggedMenuButton[0] = gLoggedMenuButton[1] = false;
    // A new session: its own floor space, vibration from scratch.
    gFloor.Create(gTarget.session);
    gSessions.fetch_add(1, std::memory_order_relaxed);
    gLoggedFloor = false;
    gHapticGaveUp = false;
    gHapticFailures = 0;
    std::lock_guard lock(gHapticMutex);
    for (auto& channel : gHaptics) {
        channel = {};
    }
}

void OnFrameBegun(void* session, void* space, std::int64_t predictedDisplayTime) noexcept {
    NoteWorldPanelFrame();  // every frame, whatever else happens below
    if (session == nullptr || static_cast<XrSession>(session) != gTarget.session) {
        return;
    }
    gTarget.base = static_cast<XrSpace>(space);  // recentering replaces it
    namespace probe = mevr::vr::frame_probe;
    struct Done {  // the frame-breakdown probe: the end of this function, whichever return
        ~Done() { mevr::host::perf::Mark(probe::kFrameBegunEnd); }
    } const done;
    mevr::host::perf::Mark(probe::kInputBegin);
    mevr::vr::controllers::State state;
    if (!gInputFailed) {
        gInput.Sync(gTarget, static_cast<XrTime>(predictedDisplayTime), state);
        ApplyPendingHaptics();  // the mod's holster pulses and contact buzz (HostServices v9/v10)
    }
    MeasureFloor(static_cast<XrTime>(predictedDisplayTime));
    mevr::host::perf::Mark(probe::kInputEnd);
    bool const inputFocused = !gInputFailed && gInput.Focused();
    mevr::vr::controllers::State const live = state;
    // The test loop records the live sample, or replaces it with a recording's (plugin/TestLoop).
    mevr::host::testloop::OnFrameBegun(static_cast<XrSession>(session), static_cast<XrSpace>(space),
        static_cast<XrTime>(predictedDisplayTime), inputFocused, state);
    if (gInputFailed && !state.active) {
        return;  // no controllers, and no replay standing in for them
    }
    if (!gInputFailed && gProfileCheckFrames++ % kProfileCheckEvery == 0) {
        std::string const hands[2] = {gInput.InteractionProfile(gTarget, 0), gInput.InteractionProfile(gTarget, 1)};
        if (hands[0] != gHandProfile[0] || hands[1] != gHandProfile[1]) {
            gHandProfile[0] = hands[0];
            gHandProfile[1] = hands[1];
            mevr::log::Info("halcyon bridge: the runtime's interaction profile: left {}, right {} (xrGetCurrentInteractionProfile)",
                hands[0].empty() ? "(none)" : hands[0], hands[1].empty() ? "(none)" : hands[1]);
        }
        std::string const& profile = !hands[1].empty() ? hands[1] : hands[0];
        if (!profile.empty()) {
            mevr::host::calibration::SetProfile(profile);
        }
    }
    // The player's own menu buttons (not a replay's): the first press per hand and session, for the log only.
    for (int hand : {0, 1}) {
        mevr::vr::controllers::Hand const& h = hand == 0 ? live.left : live.right;
        if (live.active && h.menu && !gLoggedMenuButton[hand]) {
            gLoggedMenuButton[hand] = true;
            mevr::log::Info("halcyon bridge: the {} controller's menu button reached us (/input/menu/click, profile {}){}",
                hand == 0 ? "left" : "right", gHandProfile[hand].empty() ? "(not known yet)" : gHandProfile[hand],
                hand == 0 ? ": the game's Start (Back with the left grip)" : ": not mapped to the game");
        }
    }
    // The guided calibration takes the controllers while it runs, and our main menu while it's open: everything else
    // (the mod, our pad mapping) sees none, so holsters, grenades, melee, the squad trigger and the support grip stand
    // down.
    mevr::host::calibration::NoteControllers(state);
    bool const menuTakes = MainMenuOpen();
#if defined(MEVR_WITH_MAIN_MENU)
    mevr::host::main_menu::NoteControllers(state);
#endif
    mevr::vr::controllers::Publish(mevr::host::calibration::Active() || menuTakes ? mevr::host::calibration::Hidden(state) : state);
    TickCameraAlign();
    if (state.active && state.right.aimValid) {
        gRightAimTrackedMs.store(GetTickCount64(), std::memory_order_relaxed);
    }
    gFrames.fetch_add(1, std::memory_order_relaxed);
    std::uint64_t const now = GetTickCount64();
    // A replay that presses buttons needs the game window in front too (UE3 ignores pad input without it).
    bool const focused = inputFocused || mevr::host::testloop::ReplayDrivesInputs();
    mevr::host::calibration::NoteFocused(focused);  // the first-run prompt's clocks run only while it can be seen
    if (focused && !gWasFocused) {
        gForegroundUntilMs = now + kForegroundTryMs;
        gForegroundNextMs = now;
    }
    gWasFocused = focused;
    if (focused && now < gForegroundUntilMs && now >= gForegroundNextMs) {
        if (HWND const window = gGameWindow.load()) {
            if (GetForegroundWindow() == window) {
                gForegroundUntilMs = 0;  // done
            } else {
                mevr::window::BringToForeground(window);
                gForegroundNextMs = now + 1000;
            }
        }
    }

    // The player's clicks, not a replay's (a sample without input focus has none).
    mevr::game::gamepad::StickChordStep const chord =
        mevr::game::gamepad::UpdateStickChord(gStickChord, live.active && live.left.stickClick,
            live.active && live.right.stickClick, now);
    if (chord.hold && mevr::host::calibration::Running()) {
        mevr::host::calibration::Cancel("both sticks held");  // the calibration panel's hint
    } else if (chord.hold && MELEVR::Menu::IsOpen()) {
        MELEVR::Menu::SetOpen(false);
        mevr::log::Info("halcyon bridge: both sticks held: Halcyon's menu closed");
    } else if (chord.hold) {
#if defined(MEVR_WITH_MAIN_MENU)
        mevr::host::main_menu::Toggle("both sticks held");
#else
        MELEVR::Menu::SetOpen(true);
        mevr::log::Info("halcyon bridge: both sticks held: Halcyon's menu opened");
#endif
    }
    if (chord.tap) {
        mevr::log::Info("test loop: both sticks tapped: snapshot");
        mevr::host::testloop::SnapshotRequest request;
        request.trigger = "stick tap";
        mevr::host::testloop::RequestSnapshot(request);
    }
}

bool MainMenuOpen() noexcept {
#if defined(MEVR_WITH_MAIN_MENU)
    return mevr::host::main_menu::IsOpen();
#else
    return false;
#endif
}

void OnMenuKey() noexcept {
    // \ (Halcyon's menuKey in our deploy) is our menu's; with Ctrl or Shift held, Halcyon's own (a developer fallback).
    // While Halcyon's menu is open, the key closes it as before.
    bool const modifier = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0 || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
#if defined(MEVR_WITH_MAIN_MENU)
    if (!modifier && !MELEVR::Menu::IsOpen()) {
        mevr::host::main_menu::Toggle("the menu key");
        return;
    }
    if (mevr::host::main_menu::IsOpen()) {
        mevr::host::main_menu::SetOpen(false, "Ctrl/Shift + the menu key: Halcyon's menu instead");
    }
#endif
    (void)modifier;
    MELEVR::Menu::Toggle();
}

bool FirstPersonAtBoot(bool saved) noexcept {
    mevr::log::Info("halcyon bridge: first person at launch {} (the active profile's saved state; upstream always boots it off)",
        saved ? "on" : "off");
    return saved;
}

void SetGameWindow(HWND window) noexcept {
    gGameWindow.store(window);
}

void OnViewBuilt(bool worldView, bool headLook, bool headAim, float const headOrientation[4],
    float const headPosition[3], float offsetRightUU, float offsetUpUU, float offsetForwardUU,
    float unitsPerMeter) noexcept {
    mevr::vr::host_view::State view;
    view.valid = worldView;
    view.headLook = headLook;
    view.headAim = headAim;
    view.head.orientation = {headOrientation[0], headOrientation[1], headOrientation[2], headOrientation[3]};
    view.head.position = {headPosition[0], headPosition[1], headPosition[2]};
    // Halcyon's head look yaw is + when the view turns left; ours (UE rotators) + right. Pitch is
    // + up in both. Zero while head look is off.
    view.lookYaw = -MELEVR::RenderHook::HeadLookYawUU();
    view.lookPitch = MELEVR::RenderHook::HeadLookPitchUU();
    view.offsetForward = offsetForwardUU;
    view.offsetRight = offsetRightUU;
    view.offsetUp = offsetUpUU;
    view.unitsPerMeter = unitsPerMeter;
    view.baseRightValid =
        MELEVR::RenderHook::GetLastViewRight(&view.baseRight[0], &view.baseRight[1], &view.baseRight[2]);
    mevr::vr::host_view::Publish(view);
    if (worldView && !gLoggedView) {
        gLoggedView = true;
        mevr::log::Info("halcyon bridge: first world view: head look yaw {} pitch {} (head aim {}), view offset "
                        "fwd {:.1f} right {:.1f} up {:.1f}, {:.1f} units per meter, head at ({:.2f}, {:.2f}, {:.2f}) m",
            view.lookYaw, view.lookPitch, headAim ? "on" : "off", offsetForwardUU, offsetRightUU, offsetUpUU,
            unitsPerMeter, headPosition[0], headPosition[1], headPosition[2]);
    }
}

bool ControllerAimActive() noexcept {
    std::uint64_t const tracked = gRightAimTrackedMs.load(std::memory_order_relaxed);
    bool const on = gModHandAim.load(std::memory_order_relaxed) && tracked != 0 &&
        GetTickCount64() - tracked < kControllerAimHoldMs;
    if (on != gControllerAim) {
        gControllerAim = on;
        mevr::log::Info("halcyon bridge: {}", on ? "the right controller aims weapons; Halcyon's on-foot head aim is "
                                                   "off (the head only looks)"
                                                 : "controller aim stopped; Halcyon's head aim is back");
    }
    return on;
}

namespace {
// The close latch's log (gPadMutex held): once when it starts keeping something, once when all is let go.
void NoteCloseLatch(bool kept) noexcept {
    if (kept == gCloseLatchKept) {
        return;
    }
    gCloseLatchKept = kept;
    if (kept) {
        mevr::log::Info("halcyon bridge: buttons {:#06x}{}{} held as our menu or the calibration let go of the pad: kept from "
                        "the game until released", gCloseLatch.buttons, gCloseLatch.leftTrigger ? ", the left trigger" : "",
            gCloseLatch.rightTrigger ? ", the right trigger" : "");
    } else {
        mevr::log::Info("halcyon bridge: what was held at the close is let go: the pad is the game's again");
    }
}
} // namespace

DWORD MergeMotionControllers(DWORD index, XINPUT_STATE* state, DWORD result) noexcept {
    if (index != 0 || state == nullptr) {
        return result;
    }
    // A button our main menu presses for the game (Game menu: Start; Holster: Back), once the menu has closed and the
    // game reads the pad again (gamepad::MenuPress): OR'd into whatever the game sees below.
    WORD gameButtons = 0;
    {
        std::uint64_t const now = GetTickCount64();
        std::lock_guard lock(gPadMutex);
#if defined(MEVR_WITH_MAIN_MENU)
        bool const menuOpen = mevr::host::main_menu::IsOpen();
        if (WORD const asked = mevr::host::main_menu::TakeGamePress(); asked != 0) {
            mevr::game::gamepad::StartMenuPress(gMenuPress, asked, now);
        }
#else
        bool const menuOpen = false;
#endif
        mevr::game::gamepad::MenuPressStep const step = mevr::game::gamepad::StepMenuPress(gMenuPress, menuOpen, now);
        gameButtons = step.buttons;
        char const* const name = gMenuPress.button == XINPUT_GAMEPAD_START ? "Start (its pause menu)" : "Back (holster)";
        if (step.pressed) {
            mevr::log::Info("main menu: sent the game's {}: held {} ms", name, mevr::game::gamepad::kMenuPressHoldMs);
        } else if (step.released) {
            mevr::log::Info("main menu: the game's {} let go{}", name, step.dropped ? " (our menu opened again)" : "");
        } else if (step.dropped) {
            mevr::log::Warn("main menu: the game's {} not sent: {}", name,
                menuOpen ? "our menu opened again first" : "the game didn't read the pad within 5 s");
        }
    }
#if defined(MEVR_WITH_MAIN_MENU)
    if (mevr::host::main_menu::IsOpen()) {
        // Our main menu: a real pad navigates it; the game sees a neutral pad (the motion controllers' and a real pad's).
        if (result == ERROR_SUCCESS) {
            mevr::host::main_menu::NotePad(state->Gamepad);
        }
        std::lock_guard lock(gPadMutex);
        mevr::game::gamepad::HoldCloseLatch(gCloseLatch);  // what's down when it closes stays the menu's until let go
        XINPUT_GAMEPAD const neutral{};
        if (std::memcmp(&neutral, &gLastPad, sizeof neutral) != 0) {
            gLastPad = neutral;
            ++gPacket;
        }
        state->dwPacketNumber = gPacket;
        state->Gamepad = neutral;
        return ERROR_SUCCESS;
    }
#endif
    if (mevr::host::calibration::Active()) {
        // The guided calibration: the game sees a neutral pad (the motion controllers' and a real pad's alike).
        std::lock_guard lock(gPadMutex);
        mevr::game::gamepad::HoldCloseLatch(gCloseLatch);
        XINPUT_GAMEPAD const neutral{};
        if (std::memcmp(&neutral, &gLastPad, sizeof neutral) != 0) {
            gLastPad = neutral;
            ++gPacket;
        }
        state->dwPacketNumber = gPacket;
        state->Gamepad = neutral;
        return ERROR_SUCCESS;
    }
    mevr::vr::controllers::State const controllers = mevr::vr::controllers::Latest();
    if (!controllers.active) {
        gSupportHeld.store(false, std::memory_order_relaxed);
        std::lock_guard lock(gPadMutex);
        bool const latched = gCloseLatch.buttons != 0 || gCloseLatch.leftTrigger || gCloseLatch.rightTrigger;
        if (gameButtons == 0 && (!latched || result != ERROR_SUCCESS)) {
            if (latched) {
                gCloseLatch = {};  // no real pad: nothing of it is held (and the motion controllers reach no one)
                NoteCloseLatch(false);
            }
            return result;
        }
        // No motion controllers: the real pad (if any), less what closed our menu, plus the menu's button.
        XINPUT_GAMEPAD pad = result == ERROR_SUCCESS ? state->Gamepad : XINPUT_GAMEPAD{};
        NoteCloseLatch(mevr::game::gamepad::ApplyCloseLatch(gCloseLatch, pad));
        pad.wButtons |= gameButtons;
        if (std::memcmp(&pad, &gLastPad, sizeof pad) != 0) {
            gLastPad = pad;
            ++gPacket;
        }
        state->dwPacketNumber = gPacket;
        state->Gamepad = pad;
        return ERROR_SUCCESS;
    }
    bool const realPad = result == ERROR_SUCCESS;
    std::lock_guard lock(gPadMutex);
    mevr::game::gamepad::Options options = PadOptions();
    if (MELEVR::Menu::IsOpen()) {
        // A squeeze in the menu never draws a weapon (the menu zeroes the game's pad). One that holds
        // the gun still isn't RB, so it doesn't reach the game as RB once the menu closes.
        options.rightGripFree = false;
        options.countKept = false;
    }
    XINPUT_GAMEPAD touch = mevr::game::gamepad::FromControllers(controllers, gLatches, options);
    bool const owned = options.squadMenu || options.nativeWheel;  // the squad menu or the game's wheel has the sticks
    WalkTowardHand(touch, controllers, options.handWalk && !owned && !MELEVR::Menu::IsOpen());
    if (options.modTurns != gModTurnsLogged && GetTickCount64() >= gModTurnsLogMs) {  // at most every 2 s (camera blends)
        gModTurnsLogged = options.modTurns;
        gModTurnsLogMs = GetTickCount64() + 2000;
        mevr::log::Info("halcyon bridge: {}", options.modTurns
            ? "the mod turns the player (comfort: snap or its own smooth turn): the game sees no right-stick X from the motion controllers"
            : "the game turns the player with the right stick again");
    }
    XINPUT_GAMEPAD pad = mevr::game::gamepad::Merge(realPad ? state->Gamepad : XINPUT_GAMEPAD{}, touch);
    gLeftGripReport.store(mevr::game::gamepad::LeftGripReport(gLatches), std::memory_order_relaxed);
    if (options.squadMenu != gSquadMenuLogged) {
        gSquadMenuLogged = options.squadMenu;
        mevr::log::Info("halcyon bridge: squad menu {}", options.squadMenu
            ? "open: the sticks, A, B and the stick clicks are its own (kept from the game until released)"
            : "closed");
    }
    if (options.nativeWheel != gNativeWheelLogged) {
        gNativeWheelLogged = options.nativeWheel;
        mevr::log::Info("halcyon bridge: the game's weapon wheel {}", options.nativeWheel
            ? std::format("held by the left grip: {} steers it (once centred), A queues; the left stick, B and the stick "
                          "clicks are kept from the game", mevr::game::gamepad::Name(options.wheelStick))
            : std::string("let go (what was in use stays kept from the game until released or centred)"));
    }
    if (options.nativeWheel && options.wheelPressA != gWheelPressALogged) {
        gWheelPressALogged = options.wheelPressA;
        if (options.wheelPressA) {
            mevr::log::Info("halcyon bridge: A pressed for the mod (confirming the highlighted entry on release)");
        }
    }
    std::uint32_t const report = mevr::game::gamepad::RightGripReport(gLatches);
    if (gRightGripReport.exchange(report, std::memory_order_relaxed) != report && (report & 1u) != 0) {
        if (gLatches.rightHold) {
            if (!gLoggedRightGripHolds.exchange(true, std::memory_order_relaxed)) {
                mevr::log::Info("halcyon bridge: first right-grip squeeze that holds the gun: kept from the game (no RB); RB "
                                "comes from {}", mevr::game::gamepad::Name(options.rbSource));
            }
        } else if (!gLoggedRightGripFree.exchange(true, std::memory_order_relaxed)) {
            mevr::log::Info("halcyon bridge: first right-grip squeeze at a holster: kept from the game (no RB)");
        }
    }
    bool const held = gLatches.left == mevr::game::gamepad::LeftGrip::Support;
    if (gSupportHeld.exchange(held, std::memory_order_relaxed) != held) {
        mevr::log::Info("halcyon bridge: support grip {} (left hand {:.2f} m from the foregrip){}", held ? "taken" : "let go",
            gSupportDistance.load(std::memory_order_relaxed),
            held ? ((PadOptions().supportAims) ? ": LT to the game instead of LB" : ": no LT (sniper rifle)") : "");
    }
    if (controllers.left.stickClick && controllers.right.stickClick) {
        // The both-sticks chord (Halcyon's menu held, a snapshot tapped), not L3 + R3. (L3 alone is held
        // back briefly by the mapping, gamepad::GateClick, so a chord pressed left first isn't a crouch.)
        pad.wButtons &= ~(XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB);
    }
    if (CameraAlignOwnsLeftTrigger()) {
        pad.bLeftTrigger = 0;
    }
    if (mevr::host::calibration::OwnsTriggers()) {
        pad.bLeftTrigger = 0;  // the calibration's first-run prompt is being pointed at: its click isn't a shot
        pad.bRightTrigger = 0;
    }
    NoteCloseLatch(mevr::game::gamepad::ApplyCloseLatch(gCloseLatch, pad));  // the A, B or trigger that closed our menu
    pad.wButtons |= gameButtons;
    if (std::memcmp(&pad, &gLastPad, sizeof pad) != 0) {
        gLastPad = pad;
        ++gPacket;  // the game only re-reads a pad whose packet number changed
    }
    state->dwPacketNumber = gPacket;
    state->Gamepad = pad;
    return ERROR_SUCCESS;
}

bool OnGameThread() noexcept {
    return mevr::host::game_thread::OnGameThread();
}

bool QueueProcessEvent(void* object, void* function, void const* params) noexcept {
    return mevr::host::game_thread::Queue(object, function, params);
}

// One FViewportClient::Draw a frame (research/37): the host's switch, and pass 0's record of pass 1's view.
std::atomic<bool> g_oneDrawStereo{false};
namespace {
std::mutex gPass1Mutex;
Pass1View gPass1Ring[8];
unsigned gPass1Next = 0;
} // namespace
void RecordPass1View(Pass1View const& v) {
    std::lock_guard<std::mutex> const lock(gPass1Mutex);
    gPass1Ring[gPass1Next % 8] = v;
    ++gPass1Next;
}
bool FindPass1View(float const viewOrigin[3], float const forward[3], Pass1View* out) {
    std::lock_guard<std::mutex> const lock(gPass1Mutex);
    unsigned const n = gPass1Next < 8 ? gPass1Next : 8;
    float best = 0.0f;
    bool found = false;
    for (unsigned i = 0; i < n; ++i) {
        Pass1View const& v = gPass1Ring[(gPass1Next + 7 - i) % 8];  // newest first
        float const dx = v.viewOrigin[0] - viewOrigin[0], dy = v.viewOrigin[1] - viewOrigin[1], dz = v.viewOrigin[2] - viewOrigin[2];
        float const dist2 = dx * dx + dy * dy + dz * dz;
        if (dist2 > 50.0f * 50.0f) {
            continue;
        }
        // The game thread runs a frame ahead: the frame's own record is the one whose forward matches too (a turn in
        // place, a recenter, a cut: research/37, the sheared right eye after a replay stopped). A radian is 100 uu.
        float const fx = v.forward[0] - forward[0], fy = v.forward[1] - forward[1], fz = v.forward[2] - forward[2];
        float const score = dist2 + 100.0f * 100.0f * (fx * fx + fy * fy + fz * fz);
        if (!found || score < best) {
            best = score;
            *out = v;
            found = true;
        }
    }
    return found;
}
std::uint64_t Pass1Records() {
    std::lock_guard<std::mutex> const lock(gPass1Mutex);
    return gPass1Next;
}
bool NewestPass1View(Pass1View* out) {
    std::lock_guard<std::mutex> const lock(gPass1Mutex);
    if (gPass1Next == 0) return false;
    *out = gPass1Ring[(gPass1Next + 7) % 8];
    return true;
}

// The draw recorder's ear in Halcyon's draw hooks (research/37, experiment 6).
void RecordDrawIndexed(ID3D11DeviceContext* context, UINT indexCount, UINT startIndex, INT baseVertex) {
    mevr::host::draw_replay::NoteDrawIndexed(context, indexCount, startIndex, baseVertex);
}
void RecordDraw(ID3D11DeviceContext* context, UINT vertexCount, UINT startVertex) {
    mevr::host::draw_replay::NoteDraw(context, vertexCount, startVertex);
}
void RecordDrawIndexedInstanced(ID3D11DeviceContext* context, UINT indexCountPerInstance, UINT instanceCount,
    UINT startIndex, INT baseVertex, UINT startInstance) {
    mevr::host::draw_replay::NoteDrawIndexedInstanced(context, indexCountPerInstance, instanceCount, startIndex, baseVertex, startInstance);
}
void RecordDrawInstanced(ID3D11DeviceContext* context, UINT vertexCountPerInstance, UINT instanceCount, UINT startVertex,
    UINT startInstance) {
    mevr::host::draw_replay::NoteDrawInstanced(context, vertexCountPerInstance, instanceCount, startVertex, startInstance);
}
void RecordDrawAuto(ID3D11DeviceContext* context) {
    mevr::host::draw_replay::NoteDrawAuto(context);
}
void RecordUpdateSubresource(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource, D3D11_BOX const* box,
    void const* data, UINT rowPitch, UINT depthPitch) {
    mevr::host::draw_replay::NoteUpdateSubresource(context, resource, subresource, box, data, rowPitch, depthPitch);
}
void RecordClearDepthStencilView(ID3D11DeviceContext* context, ID3D11DepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil) {
    mevr::host::draw_replay::NoteClearDepthStencilView(context, view, flags, depth, stencil);
}

} // namespace MELEVR::MevrBridge

// ---- The constant eye (research/38 B) ----------------------------------------------------------------------------
// The camera is the hook bone + HookOffset - Offset (view pitch 0; research/18). Halcyon writes each first-person
// mode's Offset by class, so the eye drops 26 units and moves 10 back on a draw, 9 more aiming, 20 right sprinting
// with the weapon out. With [Hands] constantEye = 1 every standing on-foot mode gets HookOffset - E instead, E the
// explore eye (Explore's HookOffset - fpExplore): the game's blends between modes then blend equal points. Crouch
// modes keep Halcyon's values (crouching lowers the view as before). Present thread only (FpOwnAndWrite).
namespace MELEVR::MevrBridge {
namespace {

namespace cs = mevr::vr::combat_stance;

// SFXCameraMode.HookOffset (an FVector; engine/ReflectCheck checks the field against LExSDKv2: 0xC0, next to Offset 0xB4).
constexpr std::uintptr_t kModeHookOffset = 0xC0;

struct ModeRead {
    char cls[64] = {};
    char obj[64] = {};
    float hook[3] = {};
};

void CopyName(char* out, std::size_t cap, char const* text) {
    std::size_t i = 0;
    for (; text != nullptr && i + 1 < cap && text[i] != '\0'; ++i) {
        out[i] = text[i];
    }
    out[i] = '\0';
}

// No C++ objects with destructors: it can catch an access violation.
bool ReadModeSEH(void* mode, ModeRead* out) noexcept {
    __try {
        CopyName(out->cls, sizeof(out->cls), MELEVR::LE1::ClassName(mode));
        CopyName(out->obj, sizeof(out->obj), MELEVR::LE1::ObjectName(mode));
        for (int i = 0; i < 3; ++i) {
            out->hook[i] = MELEVR::LE1::ReadF32(mode, kModeHookOffset + 4u * static_cast<unsigned>(i));
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// [Hands] constantEye and blendFix: read from the ini on the game thread (RefreshCameraSettings, once a second), used
// on the Present thread: no file I/O on the render thread.
std::atomic<bool> gConstantEyeOn{true};
std::atomic<bool> gBlendFixOn{true};

bool ConstantEyeSetting() {
    return gConstantEyeOn.load(std::memory_order_relaxed);
}

void RefreshConstantEyeSetting() {
    static bool on = true;
    static bool logged = false;
    {
        std::wstring const& path = SettingsPath();
        bool const read = path.empty() || GetPrivateProfileIntW(L"Hands", L"constantEye", 1, path.c_str()) != 0;
        if (!logged || read != on) {
            logged = true;
            mevr::log::Info("constant eye: {} ([Hands] constantEye = {}, research/38 B)",
                read ? "on: every standing on-foot camera mode puts the eye where exploring does" : "off: Halcyon's per-mode offsets",
                read ? 1 : 0);
        }
        on = read;
        gConstantEyeOn.store(read, std::memory_order_relaxed);
    }
}

cs::Vec gExploreHook{0.0, 0.0, 120.0};  // the package default (research/38 B), until an Explore mode is seen
void const* gLoggedMode = nullptr;
float gLogged[3] = {1e9f, 1e9f, 1e9f};

std::string V(cs::Vec v) {
    return std::format("({:.1f}, {:.1f}, {:.1f})", v.x, v.y, v.z);
}

} // namespace

bool ConstantFpEye(void* mode, float* fwd, float* right, float* up) noexcept {
    if (mode == nullptr || fwd == nullptr || right == nullptr || up == nullptr || !ConstantEyeSetting()) {
        return false;
    }
    ModeRead read;
    if (!ReadModeSEH(mode, &read)) {
        return false;
    }
    cs::Vec const hook{read.hook[0], read.hook[1], read.hook[2]};
    if (!cs::PlausibleHook(hook)) {
        return false;
    }
    if (cs::IsExploreMode(read.cls, read.obj)) {
        gExploreHook = hook;
    }
    cs::Vec const halcyon{*fwd, *right, *up};
    bool const applies = cs::ConstantEyeApplies(read.cls, read.obj);
    MELEVR::Config::VrConfig::FpStateCfg const& explore = MELEVR::Config::Get().fpExplore;
    cs::Vec const eye = cs::ExploreEye(gExploreHook, {explore.fwd, explore.right, explore.up});
    cs::Vec const offset = applies ? cs::ConstantEyeOffset(hook, eye) : halcyon;
    float const out[3] = {static_cast<float>(offset.x), static_cast<float>(offset.y), static_cast<float>(offset.z)};
    // Logged once per mode change (and when the values move): the `camera: mode` lines should then read the same eye.
    if (mode != gLoggedMode || std::fabs(out[0] - gLogged[0]) > 0.05f || std::fabs(out[1] - gLogged[1]) > 0.05f ||
        std::fabs(out[2] - gLogged[2]) > 0.05f) {
        gLoggedMode = mode;
        std::copy(std::begin(out), std::end(out), std::begin(gLogged));
        if (applies) {
            mevr::log::Info("constant eye: {} ({}): HookOffset {} -> Offset {}: the eye {} from the hook bone, as exploring (Halcyon's {})",
                read.obj, read.cls, V(hook), V(offset), V(eye), V(halcyon));
        } else {
            mevr::log::Info("constant eye: {} ({}): a crouch mode, Halcyon's Offset {} kept (HookOffset {}: the eye {} from the hook bone)",
                read.obj, read.cls, V(halcyon), V(hook), V({hook.x - halcyon.x, hook.y - halcyon.y, hook.z - halcyon.z}));
        }
    }
    if (!applies) {
        return false;
    }
    *fwd = out[0];
    *right = out[1];
    *up = out[2];
    return true;
}

// [MEVR] research/27 7c: the game's HDR-capability answer. The game (0x1409a0050) takes DXGI_OUTPUT_DESC1::ColorSpace
// == HDR10 as "HDR capable" and, windowed, drops an HDR range to SDR otherwise; so an SDR desktop never gets the float
// backbuffer. With [Colour] hdrDisplay = 1 the SDR answer becomes HDR10 (10 bits, 1000 nits if the display gives no
// luminance): the game then creates its R16G16B16A16_FLOAT scRGB swapchain, which DXGI accepts on an SDR desktop
// (the game window on the monitor looks washed; the eyes get our tonemap).
void HdrDisplayDesc(DXGI_OUTPUT_DESC1* desc) {
    static std::atomic<int> logs{0};
    std::wstring const& path = SettingsPath();
    bool const force = !path.empty() && GetPrivateProfileIntW(L"Colour", L"hdrDisplay", 0, path.c_str()) != 0;
    bool const hdr = desc->ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    if (force && !hdr) {
        int const was = static_cast<int>(desc->ColorSpace);
        desc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        desc->BitsPerColor = 10;
        if (desc->MaxLuminance <= 0.0f) {
            desc->MinLuminance = 0.005f;
            desc->MaxLuminance = 1000.0f;
            desc->MaxFullFrameLuminance = 1000.0f;
        }
        if (logs.fetch_add(1, std::memory_order_relaxed) < 3) {
            mevr::log::Info("hdr display: the display reports colour space {} (SDR); answered HDR10 to the game ([Colour] hdrDisplay = 1): "
                            "its HDR range makes the float backbuffer without Windows HDR (research/27 7c)", was);
        }
    } else if (logs.fetch_add(1, std::memory_order_relaxed) < 3) {
        mevr::log::Info("hdr display: the display reports colour space {} ({}), {} bits per colour, {:.0f} nits; [Colour] hdrDisplay = {}",
            static_cast<int>(desc->ColorSpace), hdr ? "HDR10" : "SDR", desc->BitsPerColor, desc->MaxLuminance, force ? 1 : 0);
    }
}

} // namespace MELEVR::MevrBridge

// ---- The blend's stored offset (research/38 B) ------------------------------------------------------------------
// SFXCameraMode_Interpolate.InitializeTransition (script, on the game thread, inside the camera update that picks the
// new mode) stores LocationOffset = To.m_pov.Location - From.m_pov.Location once, and each Tick puts the view at
// To.m_pov.Location - LocationOffset * (1 - T). Halcyon writes a destination's first-person Offset on the Present thread,
// a frame after that: for a mode it hadn't written yet the stored offset held the game's over-the-shoulder position, so
// the view jumped by the write (1.8 m forward for the sniper rifle's first draw) and slid back over the blend. Here the
// stored offset moves with the destination. Present thread only.
namespace MELEVR::MevrBridge {
namespace {

// SFXCameraMode_Interpolate (engine/ReflectCheck checks each against LExSDKv2) and SFXCameraMode.m_pov.Rotation.
constexpr std::uintptr_t kInterpRotationOffset = 0x148;  // FRotator
constexpr std::uintptr_t kInterpLocationOffset = 0x154;  // FVector
constexpr std::uintptr_t kInterpFrom = 0x160;            // USFXCameraMode*
constexpr std::uintptr_t kInterpTotalTime = 0x170;       // float
constexpr std::uintptr_t kInterpCurrentTime = 0x174;     // float
constexpr std::uintptr_t kInterpFlags = 0x184;           // bComplete 0x1
constexpr std::uintptr_t kModePovRotation = 0x98 + 0x0C; // m_pov (FTPOV: Location, Rotation, FOV) .Rotation

bool BlendFixSetting() {
    return gBlendFixOn.load(std::memory_order_relaxed);
}

void RefreshBlendFixSetting() {
    static bool on = true;
    static bool logged = false;
    {
        std::wstring const& path = SettingsPath();
        bool const read = path.empty() || GetPrivateProfileIntW(L"Hands", L"blendFix", 1, path.c_str()) != 0;
        if (!logged || read != on) {
            logged = true;
            mevr::log::Info("camera blend: {} ([Hands] blendFix = {}, research/38 B)",
                read ? "on: a camera blend's stored offset follows Halcyon's write of its destination"
                     : "off: Halcyon's mid-blend writes move the view (upstream)",
                read ? 1 : 0);
        }
        on = read;
        gBlendFixOn.store(read, std::memory_order_relaxed);
    }
}

struct BlendRead {
    void* to = nullptr;
    void* from = nullptr;
    bool complete = false;
    float currentTime = 0.0f;
    float totalTime = 0.0f;
    std::int32_t fromRotation[3] = {};
    std::int32_t rotationOffset[3] = {};
    float locationOffset[3] = {};
    float offset[3] = {};  // the destination's Offset now (after Halcyon's write)
};

// No C++ objects with destructors: it can catch an access violation.
bool ReadBlendSEH(void* interp, void* toMode, BlendRead* out) noexcept {
    __try {
        out->to = MELEVR::LE1::ReadPtr(interp, MELEVR::LE1::kInterpolateToMode);
        out->from = MELEVR::LE1::ReadPtr(interp, kInterpFrom);
        if (out->to != toMode || out->from == nullptr) {
            return false;
        }
        out->complete = (MELEVR::LE1::ReadU32(interp, kInterpFlags) & 1u) != 0;
        out->currentTime = MELEVR::LE1::ReadF32(interp, kInterpCurrentTime);
        out->totalTime = MELEVR::LE1::ReadF32(interp, kInterpTotalTime);
        for (unsigned i = 0; i < 3; ++i) {
            out->fromRotation[i] = MELEVR::LE1::ReadI32(out->from, kModePovRotation + 4u * i);
            out->rotationOffset[i] = MELEVR::LE1::ReadI32(interp, kInterpRotationOffset + 4u * i);
            out->locationOffset[i] = MELEVR::LE1::ReadF32(interp, kInterpLocationOffset + 4u * i);
            out->offset[i] = MELEVR::LE1::ReadF32(toMode, MELEVR::LE1::kSFXCameraModeOffset + 4u * i);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Present thread, while the game thread may start a new blend in the same object: writes only if the blend is still the
// one read (its destination and stored offset unchanged), so a new blend's offset is never overwritten.
bool WriteLocationOffsetSEH(void* interp, void* toMode, float const expected[3], float const value[3]) noexcept {
    __try {
        auto* target = reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(interp) + kInterpLocationOffset);
        if (MELEVR::LE1::ReadPtr(interp, MELEVR::LE1::kInterpolateToMode) != toMode || target[0] != expected[0] ||
            target[1] != expected[1] || target[2] != expected[2]) {
            return false;
        }
        target[0] = value[0];
        target[1] = value[1];
        target[2] = value[2];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* ReadOuterSEH(void* object) noexcept {
    __try {
        return MELEVR::LE1::ReadPtr(object, MELEVR::LE1::kUObjectOuter);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool Finite3(float const v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

void const* gBlendLoggedTo = nullptr;
std::uint64_t gBlendCorrections = 0;

} // namespace

void InterpolateDestinationWritten(void* interpolateMode, void* toMode, float const before[3]) noexcept {
    if (interpolateMode == nullptr || toMode == nullptr || before == nullptr || !BlendFixSetting()) {
        return;
    }
    BlendRead blend;
    if (!ReadBlendSEH(interpolateMode, toMode, &blend) || blend.complete || !Finite3(before) || !Finite3(blend.offset) ||
        !Finite3(blend.locationOffset)) {
        return;
    }
    cs::Vec const oldOffset{before[0], before[1], before[2]};
    cs::Vec const newOffset{blend.offset[0], blend.offset[1], blend.offset[2]};
    if (!cs::OffsetMoved(oldOffset, newOffset)) {
        return;  // nothing written this frame (Halcyon writes only when the Offset isn't at its target)
    }
    // The destination's rotation when the blend started: From's m_pov isn't ticked during the blend, and RotationOffset
    // is To's rotation minus it then (rotators subtract per component).
    std::int32_t const pitch = blend.fromRotation[0] + blend.rotationOffset[0];
    std::int32_t const yaw = blend.fromRotation[1] + blend.rotationOffset[1];
    cs::Vec const stored{blend.locationOffset[0], blend.locationOffset[1], blend.locationOffset[2]};
    cs::Vec const fixed = cs::BlendLocationOffset(stored, pitch, yaw, oldOffset, newOffset);
    float const value[3] = {static_cast<float>(fixed.x), static_cast<float>(fixed.y), static_cast<float>(fixed.z)};
    if (!Finite3(value) || !WriteLocationOffsetSEH(interpolateMode, toMode, blend.locationOffset, value)) {
        return;
    }
    void const* const written = reinterpret_cast<std::uint8_t const*>(interpolateMode) + kInterpLocationOffset;
    mevr::host::crash::RecordWrite("HalcyonBridge.blendLocationOffset", written, static_cast<std::uint32_t>(sizeof(value)),
        mevr::breadcrumbs::FirstBytes(written, sizeof(value)));
    ++gBlendCorrections;
    if (toMode != gBlendLoggedTo) {
        gBlendLoggedTo = toMode;
        ModeRead to;
        ModeRead interp;
        ModeRead setup;  // the destination's Outer: its SFXCameraSetup (the sniper rifle has its own)
        ReadModeSEH(toMode, &to);
        ReadModeSEH(interpolateMode, &interp);
        if (void* const outer = ReadOuterSEH(toMode)) {
            ReadModeSEH(outer, &setup);
        }
        double const jump = std::hypot(fixed.x - stored.x, fixed.y - stored.y, fixed.z - stored.z);
        mevr::log::Info("camera blend: {} toward {}.{} ({}) at {:.2f} of {:.2f} s: Halcyon wrote its Offset {} -> {} after the "
                        "blend stored the destination's position; the stored LocationOffset {} -> {}: the view stays where it was "
                        "(it would have jumped {:.0f} units and slid back), correction #{}",
            interp.obj, setup.obj, to.obj, to.cls, blend.currentTime, blend.totalTime, V(oldOffset), V(newOffset), V(stored),
            V(fixed), jump, gBlendCorrections);
    }
}

} // namespace MELEVR::MevrBridge

namespace mevr::host::halcyon {

void RefreshCameraSettings() {
    static std::uint64_t nextReadMs = 0;
    std::uint64_t const now = GetTickCount64();
    if (now < nextReadMs) {
        return;
    }
    nextReadMs = now + 1000;
    MELEVR::MevrBridge::RefreshConstantEyeSetting();
    MELEVR::MevrBridge::RefreshBlendFixSetting();
}

void SetModHandAim(bool on) {
    if (MELEVR::MevrBridge::gModHandAim.exchange(on, std::memory_order_relaxed) != on) {
        log::Info("halcyon bridge: the mod {} weapons with the right controller", on ? "aims" : "no longer aims");
    }
}

void SetSupportGrip(float distanceMeters, float acquireMeters, float releaseMeters, unsigned flags) {
    using namespace MELEVR::MevrBridge;
    gSupportDistance.store(distanceMeters, std::memory_order_relaxed);
    gSupportAcquire.store(acquireMeters, std::memory_order_relaxed);
    gSupportRelease.store(releaseMeters, std::memory_order_relaxed);
    gSupportFlags.store(flags, std::memory_order_relaxed);
    gSupportInputMs.store(GetTickCount64(), std::memory_order_relaxed);
}

void ForgetSupportGrip() {
    MELEVR::MevrBridge::gSupportInputMs.store(0, std::memory_order_relaxed);
}

bool SupportGripHeld() {
    return MELEVR::MevrBridge::gSupportHeld.load(std::memory_order_relaxed);
}

void RequestHaptic(int hand, float seconds, float amplitude, float frequencyHz) {
    if ((hand != 0 && hand != 1) || !(seconds > 0.0f) || !(amplitude > 0.0f)) {
        return;
    }
    std::lock_guard lock(MELEVR::MevrBridge::gHapticMutex);
    MELEVR::MevrBridge::gHaptics[hand].Pulse(seconds, amplitude, frequencyHz);
}

void RequestContinuousHaptic(int hand, float amplitude, float frequencyHz, float seconds) {
    if (hand != 0 && hand != 1) {
        return;
    }
    std::lock_guard lock(MELEVR::MevrBridge::gHapticMutex);
    MELEVR::MevrBridge::gHaptics[hand].Continuous(amplitude, frequencyHz, seconds, GetTickCount64());
}

void ForgetHaptics() {
    std::lock_guard lock(MELEVR::MevrBridge::gHapticMutex);
    for (auto& channel : MELEVR::MevrBridge::gHaptics) {
        channel.Clear();
    }
}

std::uint32_t RightGripFree() {
    return MELEVR::MevrBridge::gRightGripReport.load(std::memory_order_relaxed);
}

std::uint32_t LeftGripAway() {
    return MELEVR::MevrBridge::gLeftGripReport.load(std::memory_order_relaxed);
}

void Floor(vr::floor::State* out) {
    if (out == nullptr) {
        return;
    }
    std::lock_guard lock(MELEVR::MevrBridge::gFloorMutex);
    *out = MELEVR::MevrBridge::gFloorState;
}

void NoteRecentered() {
    MELEVR::MevrBridge::gRecenters.fetch_add(1, std::memory_order_relaxed);
}

} // namespace mevr::host::halcyon
