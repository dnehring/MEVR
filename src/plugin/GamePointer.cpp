// SPDX-License-Identifier: MIT
// The game pointer (research/42): the flat screen's quad as Halcyon submits it (MevrBridge::OnFlatQuad, the one seam),
// the controllers' aim rays against it (core/vr/GamePointer), and the mouse sent to the game's window.

#include "plugin/GamePointer.hpp"

#include "core/vr/GalaxyMap.hpp"

#include "halcyon/le1/mevr_bridge.h"
#include "plugin/Calibration.hpp"
#include "plugin/MevrMenu.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/GamePointer.hpp"
#include "core/vr/OverlayPlacement.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>

namespace mevr::host::game_pointer {
namespace {

namespace pointer = mevr::vr::game_pointer;
namespace menu_pointer = mevr::vr::menu_pointer;
namespace controllers = mevr::vr::controllers;

constexpr std::uint64_t kQuadFreshMs = 250;      // the quad counts as shown this long after its last submit
constexpr std::uint64_t kSettingsEveryMs = 5000;
constexpr float kDotLiftM = 0.005f;              // the dot sits this far in front of the quad
constexpr float kDegToRad = 3.14159265f / 180.0f;

// `[Pointer]` in ASI\LE1MEVR.ini.
struct Settings {
    bool gameMenus = true;     // the feature: the game's menus and the galaxy map
    bool flatCine = false;     // also flat conversations, cutscenes and movies (the dialogue wheel's flat form)
    bool moveMessages = true;  // WM_MOUSEMOVE / WM_LBUTTON* / WM_MOUSEWHEEL posted to the game window
    bool setCursor = true;     // SetCursorPos to the screen point (for a GetCursorPos poll)
    bool sendInput = false;    // clicks and wheel by SendInput (the foreground window's; sets the async key state)
    bool wheel = true;         // the right stick scrolls
    float wheelPerSecond = 6.0f;
    float dotDeg = 0.6f;       // the dot's angular size as seen from the head
    bool operator==(Settings const&) const = default;
};

// Present thread.
Settings gSettings;
std::uint64_t gSettingsReadMs = 0;
bool gSettingsLogged = false;

// The flat quad as submitted this frame (OnFlatQuad), in the app space (the controllers' space).
pointer::Quad gQuad;
std::uint64_t gQuadMs = 0;
MELEVR::MevrBridge::PanelKind gKind = MELEVR::MevrBridge::PanelKind::GameMenu;
bool gQuadHeadLocked = false;
std::uint64_t gShownSinceMs = 0;   // the current appearance's start; 0 = not shown

pointer::Pointer gPointer;
bool gLoggedHit = false;        // per appearance
bool gLoggedPress = false;      // per session
bool gLoggedWheel = false;
bool gLoggedNoWindow = false;
bool gLoggedForeground = false;
int gLoggedHand = -1;
std::uint64_t gLastStepMs = 0;
int gLastX = 0;                 // the last point sent (client pixels)
int gLastY = 0;
bool gHaveLast = false;
unsigned gMoves = 0;            // this appearance
unsigned gClicks = 0;
int gNotches = 0;

// Any thread: the status for snapshots.
std::mutex gStatusMutex;
std::string gStatus = "null";

char const* KindName(MELEVR::MevrBridge::PanelKind kind) {
    using MELEVR::MevrBridge::PanelKind;
    switch (kind) {
    case PanelKind::GameMenu: return "the game's menus";
    case PanelKind::GalaxyMap: return "the galaxy map";
    case PanelKind::Conversation: return "a flat conversation";
    case PanelKind::Cutscene: return "a flat cutscene or movie";
    case PanelKind::InsertMenu: break;
    }
    return "Halcyon's menu";
}

bool KindWanted(MELEVR::MevrBridge::PanelKind kind, Settings const& s) {
    using MELEVR::MevrBridge::PanelKind;
    switch (kind) {
    case PanelKind::GameMenu:
    case PanelKind::GalaxyMap: return s.gameMenus;
    case PanelKind::Conversation:
    case PanelKind::Cutscene: return s.gameMenus && s.flatCine;
    case PanelKind::InsertMenu: break;
    }
    return false;
}

float ReadFloat(wchar_t const* key, float fallback, std::wstring const& path) {
    wchar_t text[64] = {};
    GetPrivateProfileStringW(L"Pointer", key, L"", text, static_cast<DWORD>(std::size(text)), path.c_str());
    std::string narrow;
    for (wchar_t const* p = text; *p != L'\0' && *p < 128; ++p) {
        narrow.push_back(static_cast<char>(*p));
    }
    std::optional<double> const value = mevr::ini::Number(narrow);  // never strtod (the C locale)
    return value && std::isfinite(*value) ? static_cast<float>(*value) : fallback;
}

void RefreshSettings(std::uint64_t now) {
    if (gSettingsReadMs != 0 && now - gSettingsReadMs < kSettingsEveryMs) {
        return;
    }
    gSettingsReadMs = now;
    Settings s;
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    if (!path.empty()) {
        auto readBool = [&](wchar_t const* key, bool fallback) {
            return GetPrivateProfileIntW(L"Pointer", key, fallback ? 1 : 0, path.c_str()) != 0;
        };
        s.gameMenus = readBool(L"gameMenus", s.gameMenus);
        s.flatCine = readBool(L"flatCine", s.flatCine);
        s.moveMessages = readBool(L"moveMessages", s.moveMessages);
        s.setCursor = readBool(L"setCursor", s.setCursor);
        s.sendInput = readBool(L"sendInput", s.sendInput);
        s.wheel = readBool(L"wheel", s.wheel);
        s.wheelPerSecond = std::clamp(ReadFloat(L"wheelPerSecond", s.wheelPerSecond, path), 0.0f, 60.0f);
        s.dotDeg = std::clamp(ReadFloat(L"dotDeg", s.dotDeg, path), 0.1f, 5.0f);
    }
    if (!gSettingsLogged || !(s == gSettings)) {
        gSettingsLogged = true;
        mevr::log::Info("game pointer: {} ([Pointer] gameMenus = {}, flatCine = {}; the mouse by {}{}{}{}; wheel {} at {:.1f} notches/s; dot {:.2f} deg)",
            s.gameMenus ? "on: a controller's aim ray on the game's flat menus is its mouse" : "off",
            s.gameMenus ? 1 : 0, s.flatCine ? 1 : 0,
            s.moveMessages ? "window messages" : "", s.moveMessages && s.setCursor ? " and " : "",
            s.setCursor ? "the OS cursor (SetCursorPos)" : (s.moveMessages ? "" : "nothing: moveMessages and setCursor are both 0"),
            s.sendInput ? ", clicks by SendInput" : "", s.wheel ? "on" : "off", s.wheelPerSecond, s.dotDeg);
        if (s.sendInput) {
            mevr::log::Info("game pointer: sendInput = 1: clicks and the wheel go through SendInput, which reaches the foreground window only");
        }
    }
    gSettings = s;
    pointer::Settings ps = gPointer.GetSettings();
    ps.wheelPerSecond = s.wheel ? s.wheelPerSecond : 0.0f;
    if (!(ps.wheelPerSecond == gPointer.GetSettings().wheelPerSecond)) {
        gPointer = pointer::Pointer(ps);  // a fresh latch: the settings changed, nothing is held
    }
}

bool ToScreen(HWND window, int x, int y, POINT& screen) {
    screen = {x, y};
    return ClientToScreen(window, &screen) != FALSE;
}

void SendMove(HWND window, int x, int y, bool pressed, Settings const& s) {
    if (s.setCursor) {
        POINT screen;
        if (ToScreen(window, x, y, screen)) {
            SetCursorPos(screen.x, screen.y);  // Halcyon's hook passes it through while its menu is closed
        }
    }
    if (s.moveMessages) {
        PostMessageW(window, WM_MOUSEMOVE, pressed ? MK_LBUTTON : 0, MAKELPARAM(x, y));
    }
    ++gMoves;
}

void SendButton(HWND window, int x, int y, bool down, Settings const& s) {
    bool sent = false;
    if (s.sendInput) {
        if (GetForegroundWindow() == window) {
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            sent = SendInput(1, &input, sizeof input) == 1;
        } else if (!gLoggedForeground) {
            gLoggedForeground = true;
            mevr::log::Warn("game pointer: the game's window isn't in the foreground: SendInput would reach another program; posting the click instead");
        }
    }
    if (!sent) {
        PostMessageW(window, down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? MK_LBUTTON : 0, MAKELPARAM(x, y));
    }
    if (down) {
        ++gClicks;
    }
}

void SendWheel(HWND window, int x, int y, int notches, bool pressed, Settings const& s) {
    if (notches == 0) {
        return;
    }
    int const delta = notches * WHEEL_DELTA;
    bool sent = false;
    if (s.sendInput && GetForegroundWindow() == window) {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = static_cast<DWORD>(delta);
        sent = SendInput(1, &input, sizeof input) == 1;
    }
    if (!sent) {
        POINT screen;
        if (!ToScreen(window, x, y, screen)) {
            screen = {x, y};
        }
        // WM_MOUSEWHEEL's point is in SCREEN coordinates (unlike the other mouse messages).
        PostMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(pressed ? MK_LBUTTON : 0, static_cast<WORD>(static_cast<short>(delta))),
            MAKELPARAM(screen.x, screen.y));
    }
    gNotches += notches;
    if (!gLoggedWheel) {
        gLoggedWheel = true;
        mevr::log::Info("game pointer: the right stick scrolled the game's menu ({} notch{})", notches, notches == 1 || notches == -1 ? "" : "es");
    }
}

// Everything held is let go: a press that was open gets its button up at the last point sent.
void ReleaseAll(HWND window, char const* why) {
    pointer::Output const out = gPointer.Release();
    if (out.pressEdge && window != nullptr && gHaveLast) {
        SendButton(window, gLastX, gLastY, false, gSettings);
        mevr::log::Info("game pointer: the press was let go ({})", why);
    }
}

void EndAppearance(HWND window, std::uint64_t now, char const* why) {
    if (gShownSinceMs == 0) {
        return;
    }
    ReleaseAll(window, why);
    mevr::log::Info("game pointer: the flat screen ({}) is gone after {:.1f} s ({}: {} mouse moves, {} clicks, {} wheel notches sent)",
        KindName(gKind), static_cast<double>(now - gShownSinceMs) / 1000.0, why, gMoves, gClicks, gNotches);
    gShownSinceMs = 0;
    gLoggedHit = false;
    gMoves = gClicks = 0;
    gNotches = 0;
    gHaveLast = false;
}

void PublishStatus(bool shown, pointer::Output const* out, int width, int height) {
    std::string status;
    if (!shown) {
        status = std::format("{{\"on\": {}, \"flat\": false}}", gSettings.gameMenus ? "true" : "false");
    } else {
        status = std::format(
            "{{\"on\": {}, \"flat\": true, \"kind\": \"{}\", \"worldLocked\": {}, \"quadM\": [{:.3f}, {:.3f}], \"client\": [{}, {}], "
            "\"hit\": {}, \"hand\": \"{}\", \"px\": [{}, {}], \"uv\": [{:.4f}, {:.4f}], \"pressed\": {}, \"moves\": {}, \"clicks\": {}, \"notches\": {}}}",
            gSettings.gameMenus ? "true" : "false", KindName(gKind), gQuadHeadLocked ? "false" : "true", gQuad.width, gQuad.height,
            width, height, out != nullptr && out->hit ? "true" : "false", gPointer.Hand() == 1 ? "right" : "left",
            out != nullptr ? out->x : 0, out != nullptr ? out->y : 0, out != nullptr ? out->u : 0.0f, out != nullptr ? out->v : 0.0f,
            gPointer.Pressed() ? "true" : "false", gMoves, gClicks, gNotches);
    }
    std::lock_guard<std::mutex> const lock(gStatusMutex);
    gStatus = std::move(status);
}

} // namespace

bool Step(Frame const& frame, Dot& dot) {
    std::uint64_t const now = GetTickCount64();
    RefreshSettings(now);
    float const dt = gLastStepMs == 0 ? 0.0f : static_cast<float>(now - gLastStepMs) / 1000.0f;
    gLastStepMs = now;
    HWND const window = MELEVR::MevrBridge::GameWindow();

    bool const shown = gQuadMs != 0 && now - gQuadMs < kQuadFreshMs && gQuad.width > 0.0f;
    if (!shown) {
        EndAppearance(window, now, "the flat screen isn't submitted any more");
        PublishStatus(false, nullptr, 0, 0);
        return false;
    }
    if (gShownSinceMs == 0) {
        gShownSinceMs = now;
        if (KindWanted(gKind, gSettings)) {
            mevr::log::Info("game pointer: the flat screen is up ({}, {:.2f} x {:.2f} m, {}): a controller's aim ray on it is the game's mouse",
                KindName(gKind), gQuad.width, gQuad.height, gQuadHeadLocked ? "head-locked" : "fixed in the room");
        } else {
            mevr::log::Info("game pointer: the flat screen is up ({}): not pointed at ([Pointer] {})", KindName(gKind),
                gSettings.gameMenus ? "flatCine = 0" : "gameMenus = 0");
        }
    }

    // Who owns the pointer now: ours only with our menus closed and no calibration running.
    bool const othersOwn = frame.halcyonMenuOpen || MELEVR::MevrBridge::MainMenuOpen() || mevr::host::calibration::Active() ||
        mevr::host::calibration::Running();
    if (!KindWanted(gKind, gSettings) || othersOwn) {
        ReleaseAll(window, othersOwn ? "a menu of ours or the calibration took the controllers" : "the setting is off");
        PublishStatus(true, nullptr, 0, 0);
        return false;
    }
    if (window == nullptr) {
        if (!gLoggedNoWindow) {
            gLoggedNoWindow = true;
            mevr::log::Warn("game pointer: the game's window isn't known yet (Halcyon hasn't seen the swapchain's output window): no mouse");
        }
        PublishStatus(true, nullptr, 0, 0);
        return false;
    }
    RECT client{};
    if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0) {
        PublishStatus(true, nullptr, 0, 0);
        return false;
    }

    controllers::State const state = controllers::Latest();
    pointer::Input input;
    input.active = state.active;
    for (int i = 0; i < 2; ++i) {
        controllers::Hand const& hand = i == 0 ? state.left : state.right;
        input.hands[i].aimValid = hand.aimValid;
        input.hands[i].aim = hand.aim;
        input.hands[i].trigger = hand.trigger;
    }
    input.rightStickY = state.right.stickY;
    input.width = static_cast<int>(client.right);
    input.height = static_cast<int>(client.bottom);
    input.dt = dt;
    pointer::Output const out = gPointer.Step(gQuad, input);

    if (out.hand != gLoggedHand && out.hit) {
        gLoggedHand = out.hand;
        mevr::log::Info("game pointer: the {} controller points", out.hand == 1 ? "right" : "left");
    }
    // Off the quad (or released): a press that ended off it still gets its button up at the last point.
    if (!out.hit) {
        if (out.pressEdge && gHaveLast) {
            SendButton(window, gLastX, gLastY, false, gSettings);
        }
        PublishStatus(true, &out, input.width, input.height);
        return false;
    }
    // On the quad: a move when the pixel changed, then the button edge and the wheel at that point.
    if (out.moved || !gHaveLast) {
        gLastX = out.x;
        gLastY = out.y;
        gHaveLast = true;
        SendMove(window, out.x, out.y, out.pressed && !out.pressEdge, gSettings);
        if (!gLoggedHit) {
            gLoggedHit = true;
            mevr::log::Info("game pointer: the {} controller's ray is on {} at ({}, {}) px of the {}x{} window, {:.2f} m away; sending {}{}{}",
                out.hand == 1 ? "right" : "left", KindName(gKind), out.x, out.y, input.width, input.height, out.distance,
                gSettings.moveMessages ? "WM_MOUSEMOVE" : "", gSettings.moveMessages && gSettings.setCursor ? " and " : "",
                gSettings.setCursor ? "SetCursorPos" : (gSettings.moveMessages ? "" : "nothing (both paths off)"));
        }
    }
    if (out.pressEdge) {
        SendButton(window, gLastX, gLastY, out.pressed, gSettings);
        if (out.pressed && !gLoggedPress) {
            gLoggedPress = true;
            mevr::log::Info("game pointer: the trigger clicked the game's menu at ({}, {}) px ({})", gLastX, gLastY,
                gSettings.sendInput ? "SendInput" : "WM_LBUTTONDOWN posted");
        }
    }
    SendWheel(window, gLastX, gLastY, out.wheelNotches, out.pressed, gSettings);
    PublishStatus(true, &out, input.width, input.height);

    // The dot: at the hit, a hair in front of the quad, facing like it.
    XrVector3f const normal = mevr::vr::overlay::Rotate(gQuad.pose.orientation, {0.0f, 0.0f, 1.0f});
    dot.pose.orientation = gQuad.pose.orientation;
    dot.pose.position = {out.point.x + normal.x * kDotLiftM, out.point.y + normal.y * kDotLiftM, out.point.z + normal.z * kDotLiftM};
    dot.size = frame.headValid ? pointer::DotSize(frame.head, dot.pose.position, gSettings.dotDeg * kDegToRad) : 0.015f;
    dot.size = std::clamp(dot.size, 0.004f, 0.1f);
    dot.pressed = out.pressed;
    return true;
}

std::string StatusJson() {
    std::lock_guard<std::mutex> const lock(gStatusMutex);
    return gStatus;
}

// The seam's record (MevrBridge::OnFlatQuad below): the quad brought into the app space.
void NoteFlatQuad(MELEVR::MevrBridge::PanelKind kind, XrPosef const& space, XrPosef const& local, float width, float height,
    bool headLocked) {
    gQuad = menu_pointer::InTracking(space, local, width, height);
    gQuadHeadLocked = headLocked;
    gKind = kind;
    gQuadMs = GetTickCount64();
}

} // namespace mevr::host::game_pointer

namespace MELEVR::MevrBridge {

void OnFlatQuad(PanelKind kind, float const headOrientation[4], float const headPosition[3], PanelPose const& placed,
    float width, float height, bool headValid) noexcept {
    if (placed.headLocked && !headValid) {
        return;  // a head-locked quad has no place in the app space without the head
    }
    XrPosef space{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    if (placed.headLocked) {
        space.orientation = {headOrientation[0], headOrientation[1], headOrientation[2], headOrientation[3]};
        space.position = {headPosition[0], headPosition[1], headPosition[2]};
    }
    XrPosef local{};
    local.orientation = {placed.orientation[0], placed.orientation[1], placed.orientation[2], placed.orientation[3]};
    local.position = {placed.position[0], placed.position[1], placed.position[2]};
    mevr::host::game_pointer::NoteFlatQuad(kind, space, local, width, height, placed.headLocked);
}

bool GalaxyHologram(float* unitsPerMeter, float* belowM) noexcept {
    constexpr std::uint64_t kFreshMs = 500;  // the mod publishes every tick while the map is up
    auto const view = mevr::vr::galaxy_map::Latest(kFreshMs);
    if (!view || !(view->unitsPerMeter > 0.0f)) {
        return false;
    }
    if (unitsPerMeter != nullptr) {
        *unitsPerMeter = view->unitsPerMeter;
    }
    if (belowM != nullptr) {
        *belowM = view->belowM;
    }
    return true;
}

void OnGalaxyHologram(float cpuHalfH, float cpuHalfV) noexcept {
    auto const view = mevr::vr::galaxy_map::Latest(500);
    if (!view || !(cpuHalfH > 0.0f) || !(cpuHalfV > 0.0f)) {
        return;
    }
    auto const screen = mevr::vr::galaxy_map::VirtualScreen(view->distanceM, view->belowM, std::tan(cpuHalfH), std::tan(cpuHalfV));
    if (!screen) {
        return;
    }
    XrPosef const space{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    mevr::host::game_pointer::NoteFlatQuad(PanelKind::GalaxyMap, space, screen->pose, screen->width, screen->height, false);
}

} // namespace MELEVR::MevrBridge
