// SPDX-License-Identifier: MIT
// The game pointer (research/42): the flat screen's quad as Halcyon submits it (MevrBridge::OnFlatQuad, the one seam),
// the controllers' aim rays against it (core/vr/GamePointer), and the mouse sent to the game's window.

#include "plugin/GamePointer.hpp"

#include "core/vr/GalaxyHologram.hpp"
#include "core/vr/GalaxyMap.hpp"
#include "core/vr/WorldUi.hpp"

#include "halcyon/le1/mevr_bridge.h"
#include "plugin/Calibration.hpp"
#include "plugin/GalaxyHologram.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/MevrMenu.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/GamePointer.hpp"
#include "core/vr/OverlayPlacement.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <thread>
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
    // The cursor and the messages from a worker thread: posted from the Present callback (the render thread) the same click
    // was ignored by the game's Flash message box, from any other thread it was taken (2026-11-03, research/51 section 8).
    bool postThread = true;
    bool logMessages = false;  // every posted message logged
    // Touch (research/53 section 6.2, Half-Life: Alyx's way): the index fingertip, touchTipM ahead of the controller's aim
    // pose, pressing a quad within reach is the mouse; the rays still work from afar.
    bool touch = true;
    float touchTipM = 0.04f;
    float touchHoverM = 0.06f;
    float touchPressM = 0.005f;
    float touchReleaseM = 0.015f;
    bool operator==(Settings const&) const = default;
};

// Present thread.
Settings gSettings;
std::uint64_t gSettingsReadMs = 0;
bool gSettingsLogged = false;

// The flat quad as submitted this frame (OnFlatQuad), in the app space (the controllers' space).
pointer::Quad gQuad;
float gTouchArea[4] = {0.0f, 0.0f, 1.0f, 1.0f};  // the touchable part (quad UV): the world UI's visible crop
int gProjectingHand = -1;                        // the hand a wrist screen is projected from: its fingertip doesn't touch it
// The pointer's last point on one of the game's menus (LatestMenuPoint): window UV as bits, and when.
std::atomic<std::uint32_t> gMenuU{0};
std::atomic<std::uint32_t> gMenuV{0};
std::atomic<std::uint64_t> gMenuMs{0};
bool gWasTouching = false;                       // the last step's point was a fingertip's (haptic tick on arrival)
std::uint64_t gQuadMs = 0;
MELEVR::MevrBridge::PanelKind gKind = MELEVR::MevrBridge::PanelKind::GameMenu;
bool gQuadHeadLocked = false;
std::uint64_t gShownSinceMs = 0;   // the current appearance's start; 0 = not shown
bool gWaitRelease = false;         // a trigger was held as the quad appeared (the pull that brought it up): ignored until let go

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
std::atomic<bool> gOwnsTriggers{false};  // OwnsTriggers(): written by Step (Present thread), read by the XInput hook
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
        s.postThread = readBool(L"postThread", s.postThread);
        s.logMessages = readBool(L"logMessages", s.logMessages);
        s.touch = readBool(L"touch", s.touch);
        s.touchTipM = std::clamp(ReadFloat(L"touchTipM", s.touchTipM, path), -0.1f, 0.2f);
        s.touchHoverM = std::clamp(ReadFloat(L"touchHoverM", s.touchHoverM, path), 0.01f, 0.3f);
        s.touchPressM = std::clamp(ReadFloat(L"touchPressM", s.touchPressM, path), -0.05f, 0.05f);
        s.touchReleaseM = std::clamp(ReadFloat(L"touchReleaseM", s.touchReleaseM, path), s.touchPressM + 0.002f, 0.1f);
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
        mevr::log::Info("game pointer: touch {} (a fingertip {:.3f} m ahead of the aim pose: within {:.3f} m of a screen it points, at "
                        "{:.3f} m it presses, back past {:.3f} m it lets go)", s.touch ? "on" : "off ([Pointer] touch = 0)", s.touchTipM,
            s.touchHoverM, s.touchPressM, s.touchReleaseM);
    }
    gSettings = s;
    pointer::Settings ps = gPointer.GetSettings();
    pointer::Settings const was = ps;
    ps.wheelPerSecond = s.wheel ? s.wheelPerSecond : 0.0f;
    ps.touch = s.touch;
    ps.touchHoverM = s.touchHoverM;
    ps.touchPressM = s.touchPressM;
    ps.touchReleaseM = s.touchReleaseM;
    if (!(ps.wheelPerSecond == was.wheelPerSecond) || ps.touch != was.touch || !(ps.touchHoverM == was.touchHoverM) ||
        !(ps.touchPressM == was.touchPressM) || !(ps.touchReleaseM == was.touchReleaseM)) {
        gPointer = pointer::Pointer(ps);  // a fresh latch: the settings changed, nothing is held
    }
}

bool ToScreen(HWND window, int x, int y, POINT& screen) {
    screen = {x, y};
    return ClientToScreen(window, &screen) != FALSE;
}

// The cursor and the posted messages, in order: here, or from a worker thread ([Pointer] postThread).
struct Post {
    HWND window = nullptr;
    bool cursor = false;  // SetCursorPos(screen) first
    POINT screen{};
    UINT message = 0;     // 0: none
    WPARAM wparam = 0;
    LPARAM lparam = 0;
    bool log = false;     // [Pointer] logMessages when it was queued
};
std::mutex gPostMutex;
std::condition_variable gPostReady;
std::deque<Post> gPosts;
bool gPostThreadStarted = false;

char const* MessageName(UINT m) {
    switch (m) {
    case WM_MOUSEMOVE: return "MOVE";
    case WM_LBUTTONDOWN: return "DOWN";
    case WM_LBUTTONUP: return "UP";
    case WM_MOUSEWHEEL: return "WHEEL";
    default: return "-";
    }
}

void Deliver(Post const& p, bool log) {
    if (p.cursor) {
        SetCursorPos(p.screen.x, p.screen.y);
    }
    if (p.message != 0) {
        PostMessageW(p.window, p.message, p.wparam, p.lparam);
    }
    if (log) {
        mevr::log::Info("game pointer: posted {}{} ({}, {}) wparam {} on thread {}", p.cursor ? "cursor + " : "", MessageName(p.message),
            static_cast<short>(LOWORD(p.lparam)), static_cast<short>(HIWORD(p.lparam)), static_cast<unsigned>(p.wparam), GetCurrentThreadId());
    }
}

void Send(Post const& p, Settings const& s) {
    if (!s.postThread) {
        Deliver(p, s.logMessages);
        return;
    }
    std::lock_guard lock(gPostMutex);
    Post queued = p;
    queued.log = s.logMessages;
    if (!gPostThreadStarted) {
        gPostThreadStarted = true;
        std::thread([] {
            for (;;) {
                Post next;
                {
                    std::unique_lock lock(gPostMutex);
                    gPostReady.wait(lock, [] { return !gPosts.empty(); });
                    next = gPosts.front();
                    gPosts.pop_front();
                }
                Deliver(next, next.log);
            }
        }).detach();
    }
    gPosts.push_back(queued);
    gPostReady.notify_one();
}

void SendMove(HWND window, int x, int y, bool pressed, Settings const& s) {
    Post p;
    p.window = window;
    // SetCursorPos: Halcyon's hook passes it through while its menu is closed.
    p.cursor = s.setCursor && ToScreen(window, x, y, p.screen);
    if (s.moveMessages) {
        p.message = WM_MOUSEMOVE;
        p.wparam = pressed ? MK_LBUTTON : 0;
        p.lparam = MAKELPARAM(x, y);
    }
    Send(p, s);
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
        Post p;
        p.window = window;
        p.message = down ? WM_LBUTTONDOWN : WM_LBUTTONUP;
        p.wparam = down ? MK_LBUTTON : 0;
        p.lparam = MAKELPARAM(x, y);
        Send(p, s);
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
            "\"hit\": {}, \"hand\": \"{}\", \"px\": [{}, {}], \"uv\": [{:.4f}, {:.4f}], \"pressed\": {}, \"moves\": {}, \"clicks\": {}, \"notches\": {}, "
            "\"touch\": {}, \"touchM\": {:.4f}}}",
            gSettings.gameMenus ? "true" : "false", KindName(gKind), gQuadHeadLocked ? "false" : "true", gQuad.width, gQuad.height,
            width, height, out != nullptr && out->hit ? "true" : "false", gPointer.Hand() == 1 ? "right" : "left",
            out != nullptr ? out->x : 0, out != nullptr ? out->y : 0, out != nullptr ? out->u : 0.0f, out != nullptr ? out->v : 0.0f,
            gPointer.Pressed() ? "true" : "false", gMoves, gClicks, gNotches, out != nullptr && out->touch ? "true" : "false",
            out != nullptr ? out->touchDistance : 0.0f);
    }
    std::lock_guard<std::mutex> const lock(gStatusMutex);
    gStatus = std::move(status);
}

} // namespace

static bool StepInner(Frame const& frame, Dot& dot);

bool Step(Frame const& frame, Dot& dot) {
    bool const pointing = StepInner(frame, dot);  // true: the ray is on the quad (or a press is held), a dot is drawn
    gOwnsTriggers.store(pointing, std::memory_order_release);
    return pointing;
}

bool OwnsTriggers() {
    return gOwnsTriggers.load(std::memory_order_acquire);
}

static bool StepInner(Frame const& frame, Dot& dot) {
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
        controllers::State const held = controllers::Latest();
        gWaitRelease = held.active && (held.left.trigger > 0.4f || held.right.trigger > 0.4f);
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
        // The hand a wrist screen is projected from neither points at it nor touches it: it sits under the screen, its ray
        // through it, and moved the game's cursor all the time (research/56 section 6).
        input.hands[i].aimValid = hand.aimValid && i != gProjectingHand;
        input.hands[i].aim = hand.aim;
        input.hands[i].trigger = hand.trigger;
        if (hand.aimValid && i != gProjectingHand) {
            // The index fingertip: a little ahead of the aim pose along it (the pointing finger over a Touch controller's ring).
            XrVector3f d = menu_pointer::AimDirection(hand.aim);
            float const n = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (n > 1e-6f) {
                d = {d.x / n, d.y / n, d.z / n};
                input.hands[i].tipValid = true;
                input.hands[i].tip = {hand.aim.position.x + d.x * gSettings.touchTipM, hand.aim.position.y + d.y * gSettings.touchTipM,
                    hand.aim.position.z + d.z * gSettings.touchTipM};
            }
        }
    }
    input.touchU0 = gTouchArea[0];
    input.touchV0 = gTouchArea[1];
    input.touchU1 = gTouchArea[2];
    input.touchV1 = gTouchArea[3];
    if (gWaitRelease) {
        // The pull that brought the quad up (the galaxy map's travel question appears under the trigger that picked a system)
        // isn't a click on it.
        gWaitRelease = state.active && (state.left.trigger > 0.4f || state.right.trigger > 0.4f);
        input.hands[0].trigger = 0.0f;
        input.hands[1].trigger = 0.0f;
    }
    input.rightStickY = state.right.stickY;
    input.width = static_cast<int>(client.right);
    input.height = static_cast<int>(client.bottom);
    input.dt = dt;
    if (mevr::host::galaxy_hologram::Hovering()) {
        // A controller points at the galaxy hologram's table: the game's screen behind it gets no ray (no click through it).
        input.hands[0].aimValid = false;
        input.hands[1].aimValid = false;
    }
    pointer::Output const out = gPointer.Step(gQuad, input);
    // Touch's feel: a light tick as the fingertip reaches a screen, a click on the press and a lighter one on the release.
    if (out.touch && !gWasTouching) {
        mevr::host::halcyon::RequestHaptic(out.hand, 0.01f, 0.12f);
    }
    if (out.touch && out.pressEdge) {
        mevr::host::halcyon::RequestHaptic(out.hand, out.pressed ? 0.02f : 0.012f, out.pressed ? 0.5f : 0.25f);
    }
    gWasTouching = out.touch;

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
        // The cursor put at the point again first: the game moves it in between (a Scaleform button took a click only with a
        // move just before it: the galaxy map's travel question, research/51 section 8).
        SendMove(window, gLastX, gLastY, false, gSettings);
        SendButton(window, gLastX, gLastY, out.pressed, gSettings);
        if (out.pressed && !gLoggedPress) {
            gLoggedPress = true;
            mevr::log::Info("game pointer: the {} clicked the game's menu at ({}, {}) px ({})", out.touch ? "fingertip" : "trigger", gLastX,
                gLastY, gSettings.sendInput ? "SendInput" : "WM_LBUTTONDOWN posted");
        }
        if (out.touch) {
            mevr::log::Info("game pointer: the {} fingertip {} at ({}, {}) px, {:.3f} m from the surface", out.hand == 1 ? "right" : "left",
                out.pressed ? "pressed" : "let go", gLastX, gLastY, out.touchDistance);
        }
    }
    SendWheel(window, gLastX, gLastY, out.wheelNotches, out.pressed, gSettings);
    PublishStatus(true, &out, input.width, input.height);
    if (gKind == MELEVR::MevrBridge::PanelKind::GameMenu) {
        gMenuU.store(std::bit_cast<std::uint32_t>(std::clamp(out.u, 0.0f, 1.0f)), std::memory_order_relaxed);
        gMenuV.store(std::bit_cast<std::uint32_t>(std::clamp(out.v, 0.0f, 1.0f)), std::memory_order_relaxed);
        gMenuMs.store(GetTickCount64(), std::memory_order_release);
    }

    // The dot: at the hit, a hair in front of the quad, facing like it.
    XrVector3f const normal = mevr::vr::overlay::Rotate(gQuad.pose.orientation, {0.0f, 0.0f, 1.0f});
    dot.pose.orientation = gQuad.pose.orientation;
    dot.pose.position = {out.point.x + normal.x * kDotLiftM, out.point.y + normal.y * kDotLiftM, out.point.z + normal.z * kDotLiftM};
    dot.size = frame.headValid ? pointer::DotSize(frame.head, dot.pose.position, gSettings.dotDeg * kDegToRad) : 0.015f;
    dot.size = std::clamp(dot.size, 0.004f, 0.1f);
    if (out.touch) {
        // The fingertip's shadow: larger while it's farther, the dot's own size at the surface.
        float const t = std::clamp(out.touchDistance / std::max(gSettings.touchHoverM, 0.01f), 0.0f, 1.0f);
        dot.size *= 1.0f + 2.0f * t;
    }
    dot.pressed = out.pressed;
    return true;
}

bool LatestMenuPoint(float& u, float& v, std::uint64_t maxAgeMs) {
    std::uint64_t const at = gMenuMs.load(std::memory_order_acquire);
    if (at == 0 || GetTickCount64() - at > maxAgeMs) {
        return false;
    }
    u = std::bit_cast<float>(gMenuU.load(std::memory_order_relaxed));
    v = std::bit_cast<float>(gMenuV.load(std::memory_order_relaxed));
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
    gTouchArea[0] = gTouchArea[1] = 0.0f;  // the whole quad unless the caller says (NoteGameUiQuad)
    gTouchArea[2] = gTouchArea[3] = 1.0f;
    gProjectingHand = -1;
    gQuadHeadLocked = headLocked;
    gKind = kind;
    gQuadMs = GetTickCount64();
}

void NoteGameUiQuad(XrPosef const& pose, float width, float height, MELEVR::MevrBridge::PanelKind kind, float const* touchArea,
    int projectingHand) {
    XrPosef const space{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    NoteFlatQuad(kind, space, pose, width, height, false);
    if (touchArea != nullptr) {
        std::copy(touchArea, touchArea + 4, gTouchArea);
    }
    gProjectingHand = projectingHand;
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
    // research/51 section 8: the map open in the CIC ([Galaxy] inWorld): the mod holds the view at Shepard's and hides the game's
    // own map, so Halcyon renders the world in stereo as on foot (no flat panel, both Draws, no galaxy zoom). No scale of its own
    // (0: Halcyon's galaxy lean branch stays off). The camera-based hologram that used a scale (section 7) is gone.
    constexpr std::uint64_t kFreshMs = 500;  // the mod sends every tick while the map is up
    auto const scene = mevr::vr::galaxy_holo::Latest(kFreshMs);
    if (!scene || scene->inWorld == 0) {
        // Fresh on foot too: Halcyon decides its galaxy presentation (and holds it) as the map's mode begins, and the mod is
        // still sending the on-foot scene then (a map opened with A at the console as much as by a pick). Only read in mode 9.
        return false;
    }
    if (unitsPerMeter != nullptr) {
        *unitsPerMeter = 0.0f;
    }
    if (belowM != nullptr) {
        *belowM = 0.0f;
    }
    return true;
}

namespace {
// The held view swapped into the player camera's POV for the main view's CalcSceneView (galaxy map in the CIC).
struct GalaxyViewSwap {
    bool active = false;
    float* location = nullptr;
    std::int32_t* rotation = nullptr;
    float* fov = nullptr;
    float savedLocation[3] = {};
    std::int32_t savedRotation[3] = {};
    float savedFov = 0.0f;
};
GalaxyViewSwap gGalaxySwap;  // game thread (CalcSceneView)
std::uint64_t gGalaxySwaps = 0;

bool SwapIn(GalaxyViewSwap& s, float const location[3], std::int32_t const rotation[3], float fov) noexcept {
    __try {
        for (int i = 0; i < 3; ++i) {
            s.savedLocation[i] = s.location[i];
            s.savedRotation[i] = s.rotation[i];
        }
        s.savedFov = *s.fov;
        for (int i = 0; i < 3; ++i) {
            s.location[i] = location[i];
            s.rotation[i] = rotation[i];
        }
        *s.fov = fov;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void SwapOut(GalaxyViewSwap& s) noexcept {
    __try {
        for (int i = 0; i < 3; ++i) {
            s.location[i] = s.savedLocation[i];
            s.rotation[i] = s.savedRotation[i];
        }
        *s.fov = s.savedFov;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
} // namespace

bool GalaxyViewSwapBegin() noexcept {
    // The galaxy map in the CIC, or one of the game's screens standing in the room (research/53: the shop's camera looks at a
    // backdrop stage of its own): the view held at Shepard's for the main view's build only.
    gGalaxySwap.active = false;
    auto const scene = mevr::vr::galaxy_holo::Latest(500);
    if (scene && scene->holdRender != 0 && scene->povLocationAddress != 0 && scene->povRotationAddress != 0 && scene->povFovAddress != 0) {
        gGalaxySwap.location = reinterpret_cast<float*>(scene->povLocationAddress);
        gGalaxySwap.rotation = reinterpret_cast<std::int32_t*>(scene->povRotationAddress);
        gGalaxySwap.fov = reinterpret_cast<float*>(scene->povFovAddress);
        if (!SwapIn(gGalaxySwap, scene->holdLocation, scene->holdRotation, scene->holdFov)) {
            return false;
        }
        gGalaxySwap.active = true;
        if (gGalaxySwaps++ == 0) {
            mevr::log::Info("galaxy hologram: the view held at Shepard's for the main view's build only (the game keeps its map camera)");
        }
        return true;
    }
    auto const ui = mevr::vr::world_ui::Latest(500);
    if (!ui || ui->holdRender == 0 || ui->povLocationAddress == 0 || ui->povRotationAddress == 0 || ui->povFovAddress == 0) {
        return false;
    }
    gGalaxySwap.location = reinterpret_cast<float*>(ui->povLocationAddress);
    gGalaxySwap.rotation = reinterpret_cast<std::int32_t*>(ui->povRotationAddress);
    gGalaxySwap.fov = reinterpret_cast<float*>(ui->povFovAddress);
    if (!SwapIn(gGalaxySwap, ui->holdLocation, ui->holdRotation, ui->holdFov)) {
        return false;
    }
    gGalaxySwap.active = true;
    static std::uint64_t worldUiSwaps = 0;
    if (worldUiSwaps++ == 0) {
        mevr::log::Info("world ui: the view held at Shepard's for the main view's build only (the game's screen keeps its own camera)");
    }
    return true;
}

void GalaxyViewSwapEnd() noexcept {
    if (gGalaxySwap.active) {
        SwapOut(gGalaxySwap);
        gGalaxySwap.active = false;
    }
}

bool GalaxyHeld() noexcept {
    static std::atomic<std::uint64_t> lastHeldMs{0};
    auto const scene = mevr::vr::galaxy_holo::Latest(500);
    std::uint64_t const now = GetTickCount64();
    if (scene && scene->inWorld != 0 && scene->onFoot == 0) {
        lastHeldMs.store(now, std::memory_order_relaxed);
        return true;
    }
    // A short grace: the game's mode changes (to the map's closing cutscene) a frame before the mod's tick sees it and holds.
    return scene && scene->inWorld != 0 && now - lastHeldMs.load(std::memory_order_relaxed) < 400;
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
