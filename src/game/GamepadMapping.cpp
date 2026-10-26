// SPDX-License-Identifier: GPL-3.0-only
#include "game/GamepadMapping.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace mevr::game::gamepad {
namespace {

constexpr float kGripOn = 0.6f;
constexpr float kGripOff = 0.4f;

SHORT ToAxis(float value) {
    float const clamped = std::clamp(value, -1.0f, 1.0f);
    return static_cast<SHORT>(std::lround(clamped * 32767.0f));
}

BYTE ToTrigger(float value) {
    return static_cast<BYTE>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

bool Latch(bool& latched, float squeeze) {
    latched = latched ? squeeze > kGripOff : squeeze >= kGripOn;
    return latched;
}

// The stick pair pushed further from center.
void Stronger(SHORT ax, SHORT ay, SHORT bx, SHORT by, SHORT& outX, SHORT& outY) {
    long long const a = static_cast<long long>(ax) * ax + static_cast<long long>(ay) * ay;
    long long const b = static_cast<long long>(bx) * bx + static_cast<long long>(by) * by;
    outX = a >= b ? ax : bx;
    outY = a >= b ? ay : by;
}

} // namespace

// Adapted from Overlord (Aeka0, GPL-3.0): see the note at its declaration in GamepadMapping.hpp and NOTICE.md.
LeftGrip UpdateSupportGrip(SupportGrip& grip, bool gripDown, SupportInput const& input) {
    bool const available = std::isfinite(input.distance) && input.distance >= 0.0f && std::isfinite(input.acquire) &&
        std::isfinite(input.release) && input.acquire > 0.0f && input.release >= input.acquire;
    if (!gripDown) {
        grip.engaged = false;
        grip.shoulder = false;
        grip.armed = true;
        return LeftGrip::Open;
    }
    if (grip.armed) {  // a new squeeze: on the foregrip, or the shoulder button
        grip.armed = false;
        grip.engaged = available && input.distance <= input.acquire;
        grip.shoulder = !grip.engaged;
        return grip.engaged ? LeftGrip::Support : LeftGrip::Shoulder;
    }
    if (grip.engaged) {
        if (!available || input.distance > input.release) {
            grip.engaged = false;  // let go; nothing more until the grip opens
            return LeftGrip::Held;
        }
        return LeftGrip::Support;
    }
    return grip.shoulder ? LeftGrip::Shoulder : LeftGrip::Held;
}

char const* Name(RbSource source) {
    switch (source) {
    case RbSource::RightStickClick: return "the right stick click";
    case RbSource::Y: return "Y";
    case RbSource::X: return "X";
    case RbSource::B: return "B";
    case RbSource::None: break;
    }
    return "nothing (no RB)";
}

bool RbFromStickClick(RbClick& s, RbClickInput const& in) {
    using Phase = RbClick::Phase;
    bool const pressed = in.click && !s.down;
    s.down = in.click;
    bool const deflected = std::abs(in.stickX) > kRbStickCentered || std::abs(in.stickY) > kRbStickCentered;
    bool const clock = in.nowMs != 0;
    if (pressed) {
        // Halcyon counts every R3 press it sees (the host hides the chords' clicks from it): two
        // within kRbDoubleClickMs are its recenter, and the pair is used up.
        bool const counted = !in.reserved && !in.otherClick;
        bool const second = clock && counted && s.lastPressMs != 0 && in.nowMs - s.lastPressMs <= kRbDoubleClickMs;
        if (counted && clock) {
            s.lastPressMs = second ? 0 : in.nowMs;
        }
        if (!in.enabled || second || in.reserved || in.otherClick || deflected) {
            s.phase = Phase::Ignored;  // also drops a tap still waiting for the window to pass
        } else {
            s.phase = clock ? Phase::Pending : Phase::Held;
            s.pressMs = in.nowMs;
        }
    }
    switch (s.phase) {
    case Phase::Pending:
        if (in.otherClick || in.reserved) {
            s.phase = Phase::Ignored;
        } else if (!in.click) {
            s.phase = Phase::TapWait;
        } else if (in.nowMs - s.pressMs >= kRbCommitMs) {
            s.phase = Phase::Held;
        }
        break;
    case Phase::Held:
        if (!in.click) {
            s.phase = Phase::Idle;
        } else if (in.otherClick || in.reserved) {
            s.phase = Phase::Ignored;  // a chord after all: let go of RB
        }
        break;
    case Phase::TapWait:
        if (in.nowMs - s.pressMs > kRbDoubleClickMs) {
            s.phase = Phase::Tap;
            s.tapUntilMs = in.nowMs + kRbTapMs;
        }
        break;
    case Phase::Tap:
        if (in.nowMs >= s.tapUntilMs) {
            s.phase = Phase::Idle;
        }
        break;
    case Phase::Ignored:
        if (!in.click) {
            s.phase = Phase::Idle;
        }
        break;
    case Phase::Idle:
        break;
    }
    return s.phase == Phase::Held || s.phase == Phase::Tap;
}

StickChordStep UpdateStickChord(StickChord& c, bool leftClick, bool rightClick, std::uint64_t nowMs) {
    StickChordStep step;
    bool const anyBefore = c.left || c.right;
    bool const anyNow = leftClick || rightClick;
    if (!anyBefore && anyNow) {  // a new sequence
        c.firstPressMs = nowMs;
        c.bothSinceMs = 0;
        c.both = c.joined = c.held = false;
    }
    if (leftClick && rightClick) {
        if (c.bothSinceMs == 0) {
            c.bothSinceMs = nowMs;
            if (!c.both) {
                c.both = true;
                c.joined = nowMs - c.firstPressMs <= kStickJoinMs;
            }
        }
        if (!c.held && nowMs - c.bothSinceMs >= kStickHoldMs) {
            c.held = true;
            step.hold = true;
        }
    } else {
        c.bothSinceMs = 0;
    }
    if (anyBefore && !anyNow) {
        step.tap = c.both && c.joined && !c.held && nowMs - c.firstPressMs <= kStickTapMs;
        c.both = c.joined = c.held = false;
    }
    c.left = leftClick;
    c.right = rightClick;
    return step;
}

void HoldCloseLatch(CloseLatch& latch) {
    latch.buttons = 0xFFFF;
    latch.leftTrigger = true;
    latch.rightTrigger = true;
}

bool ApplyCloseLatch(CloseLatch& latch, XINPUT_GAMEPAD& pad) {
    latch.buttons = static_cast<WORD>(latch.buttons & pad.wButtons);
    latch.leftTrigger = latch.leftTrigger && pad.bLeftTrigger >= kCloseLatchTriggerOff;
    latch.rightTrigger = latch.rightTrigger && pad.bRightTrigger >= kCloseLatchTriggerOff;
    pad.wButtons = static_cast<WORD>(pad.wButtons & ~latch.buttons);
    if (latch.leftTrigger) {
        pad.bLeftTrigger = 0;
    }
    if (latch.rightTrigger) {
        pad.bRightTrigger = 0;
    }
    return latch.buttons != 0 || latch.leftTrigger || latch.rightTrigger;
}

void StartMenuPress(MenuPress& p, WORD button, std::uint64_t nowMs) {
    p.phase = button != 0 ? MenuPress::Phase::Waiting : MenuPress::Phase::Idle;
    p.button = button;
    p.sinceMs = nowMs;
    p.polls = 0;
}

MenuPressStep StepMenuPress(MenuPress& p, bool menuOpen, std::uint64_t nowMs) {
    using Phase = MenuPress::Phase;
    MenuPressStep step;
    if (p.phase == Phase::Idle) {
        return step;
    }
    if (menuOpen) {
        // Opened again before the press went through (or while it was held): called off; a held button lets go.
        step.released = p.phase == Phase::Holding;
        step.dropped = true;
        p.phase = Phase::Idle;
        return step;
    }
    ++p.polls;
    if (p.phase == Phase::Waiting) {
        if (p.polls >= kMenuPressWaitPolls && nowMs - p.sinceMs >= kMenuPressWaitMs) {
            p.phase = Phase::Holding;
            p.sinceMs = nowMs;
            p.polls = 1;
            step.buttons = p.button;
            step.pressed = true;
        } else if (nowMs - p.sinceMs >= kMenuPressGiveUpMs) {
            step.dropped = true;
            p.phase = Phase::Idle;
        }
        return step;
    }
    // Holding.
    if (p.polls > kMenuPressHoldPolls && nowMs - p.sinceMs >= kMenuPressHoldMs) {
        step.released = true;
        p.phase = Phase::Idle;
        return step;
    }
    step.buttons = p.button;
    return step;
}

bool GateClick(ClickGate& g, bool click, bool otherClick, std::uint64_t nowMs) {
    using Phase = ClickGate::Phase;
    bool const pressed = click && !g.down;
    g.down = click;
    if (nowMs == 0) {
        g.phase = click ? Phase::Pass : Phase::Idle;
        return click;
    }
    if (pressed) {
        g.phase = otherClick ? Phase::Blocked : Phase::Pending;
        g.pressMs = nowMs;
    }
    switch (g.phase) {
    case Phase::Pending:
        if (otherClick) {
            g.phase = Phase::Blocked;  // the both-sticks chord: never L3
        } else if (!click) {
            g.phase = Phase::Pulse;    // a quick click alone: L3 now, briefly
            g.pulseUntilMs = nowMs + kRbTapMs;
        } else if (nowMs - g.pressMs >= kStickJoinMs) {
            g.phase = Phase::Pass;
        }
        break;
    case Phase::Pass:
        if (!click) {
            g.phase = Phase::Idle;
        } else if (otherClick) {
            g.phase = Phase::Blocked;  // the right click joined late: L3 lets go and doesn't come back
        }
        break;
    case Phase::Pulse:
        if (nowMs >= g.pulseUntilMs) {
            g.phase = Phase::Idle;
        }
        break;
    case Phase::Blocked:
        if (!click) {
            g.phase = Phase::Idle;
        }
        break;
    case Phase::Idle:
        break;
    }
    return g.phase == Phase::Pass || g.phase == Phase::Pulse;
}

char const* Name(WheelStick stick) {
    switch (stick) {
    case WheelStick::Right: return "the right stick";
    case WheelStick::Left: return "the left stick";
    case WheelStick::Either: return "either stick";
    }
    return "?";
}

std::uint32_t ToFlags(Options const& options) {
    return (options.leftTrigger ? kFlagLeftTrigger : 0u) | (options.supportAims ? kFlagSupportAims : 0u) |
           (options.gripShoulder ? kFlagGripShoulder : 0u) | (options.leftTriggerShoulder ? kFlagLeftTriggerShoulder : 0u) |
           (options.rightGripFree ? kFlagRightGripFree : 0u) | (options.rightGripHolds ? kFlagRightGripHolds : 0u) |
           (options.squadMenu ? kFlagSquadMenu : 0u) | (options.nativeWheel ? kFlagNativeWheel : 0u) |
           (options.wheelPressA ? kFlagWheelPressA : 0u) |
           (options.meleeButton ? 0u : kFlagNoMeleeButton) | (options.grenadeButton ? 0u : kFlagNoGrenadeButton) |
           (options.modTurns ? kFlagModTurns : 0u) | (options.handWalk ? kFlagHandWalk : 0u) |
           ((static_cast<std::uint32_t>(options.wheelStick) << kWheelStickShift) & kWheelStickMask) |
           ((static_cast<std::uint32_t>(options.rbSource) << kRbSourceShift) & kRbSourceMask);
}

Options FromFlags(float distance, float acquire, float release, std::uint32_t flags) {
    Options options;
    options.leftTrigger = (flags & kFlagLeftTrigger) != 0;
    options.supportAims = (flags & kFlagSupportAims) != 0;
    options.gripShoulder = (flags & kFlagGripShoulder) != 0;
    options.leftTriggerShoulder = (flags & kFlagLeftTriggerShoulder) != 0;
    options.rightGripFree = (flags & kFlagRightGripFree) != 0;
    options.rightGripHolds = (flags & kFlagRightGripHolds) != 0;
    options.squadMenu = (flags & kFlagSquadMenu) != 0;
    options.nativeWheel = (flags & kFlagNativeWheel) != 0;
    options.wheelPressA = (flags & kFlagWheelPressA) != 0;
    options.meleeButton = (flags & kFlagNoMeleeButton) == 0;
    options.grenadeButton = (flags & kFlagNoGrenadeButton) == 0;
    options.modTurns = (flags & kFlagModTurns) != 0;
    options.handWalk = (flags & kFlagHandWalk) != 0;
    std::uint32_t const stick = (flags & kWheelStickMask) >> kWheelStickShift;
    options.wheelStick = stick <= static_cast<std::uint32_t>(WheelStick::Either) ? static_cast<WheelStick>(stick) : WheelStick::Right;
    std::uint32_t const source = (flags & kRbSourceMask) >> kRbSourceShift;
    options.rbSource = source <= static_cast<std::uint32_t>(RbSource::B) ? static_cast<RbSource>(source) : RbSource::None;
    options.support = {distance, acquire, release};
    return options;
}

XINPUT_GAMEPAD FromControllers(vr::controllers::State const& state, Latches& latches) {
    return FromControllers(state, latches, Options{});
}

XINPUT_GAMEPAD FromControllers(vr::controllers::State const& input, Latches& latches, Options const& options) {
    // The squad menu's controls: everything in use while it's open stays the menu's until released or
    // centred, so the flick that chose a weapon doesn't turn the view and the A that confirmed doesn't
    // "use" whatever is in front once it closes.
    auto const pushed = [](float x, float y) { return x * x + y * y > kSquadStickCentred * kSquadStickCentred; };
    std::uint8_t const active = static_cast<std::uint8_t>((pushed(input.left.stickX, input.left.stickY) ? kSquadLeftStick : 0) |
        (pushed(input.right.stickX, input.right.stickY) ? kSquadRightStick : 0) | (input.right.primary ? kSquadA : 0) |
        (input.right.secondary ? kSquadB : 0) | (input.left.stickClick ? kSquadL3 : 0) | (input.right.stickClick ? kSquadR3 : 0));
    bool const owning = options.squadMenu || options.nativeWheel;
    latches.squadHeld = owning ? active : static_cast<std::uint8_t>(latches.squadHeld & active);
    // The game's own weapon wheel: one stick steers it (as the game's right stick), once it has been
    // centred since the wheel opened; nothing else of the sticks, B or the clicks reaches the game.
    float steerX = 0.0f;
    float steerY = 0.0f;
    if (options.nativeWheel) {
        bool const left = options.wheelStick == WheelStick::Left ||
            (options.wheelStick == WheelStick::Either &&
                input.left.stickX * input.left.stickX + input.left.stickY * input.left.stickY >
                    input.right.stickX * input.right.stickX + input.right.stickY * input.right.stickY);
        steerX = left ? input.left.stickX : input.right.stickX;
        steerY = left ? input.left.stickY : input.right.stickY;
        if (!latches.wheelWas) {
            latches.wheelArmed = false;
        }
        if (!pushed(steerX, steerY)) {
            latches.wheelArmed = true;
        }
    }
    latches.wheelWas = options.nativeWheel;
    std::uint8_t const block = options.squadMenu ? kSquadAll
        : options.nativeWheel ? static_cast<std::uint8_t>(kSquadLeftStick | kSquadRightStick | kSquadB | kSquadL3 | kSquadR3)
                              : latches.squadHeld;
    vr::controllers::State state = input;
    if ((block & kSquadLeftStick) != 0) {
        state.left.stickX = state.left.stickY = 0.0f;
    }
    if ((block & kSquadRightStick) != 0) {
        state.right.stickX = state.right.stickY = 0.0f;
    }
    if ((block & kSquadA) != 0) {
        state.right.primary = false;
    }
    if ((block & kSquadB) != 0) {
        state.right.secondary = false;
    }
    if ((block & kSquadL3) != 0) {
        state.left.stickClick = false;
    }
    if ((block & kSquadR3) != 0) {
        state.right.stickClick = false;
    }
    if (options.nativeWheel && latches.wheelArmed) {
        state.right.stickX = steerX;
        state.right.stickY = steerY;
    }
    auto const& l = state.left;
    auto const& r = state.right;
    XINPUT_GAMEPAD pad{};
    pad.sThumbLX = ToAxis(l.stickX);
    pad.sThumbLY = ToAxis(l.stickY);
    pad.sThumbRX = ToAxis(r.stickX);
    pad.sThumbRY = ToAxis(r.stickY);
    if (options.modTurns && !owning) {
        pad.sThumbRX = 0;  // the mod turns the player (game/comfort): the game's own turn would add to it
    }
    pad.bLeftTrigger = options.leftTrigger && !options.leftTriggerShoulder ? ToTrigger(l.trigger) : BYTE{0};
    pad.bRightTrigger = ToTrigger(r.trigger);

    WORD buttons = 0;
    bool const leftGrip = Latch(latches.leftGrip, l.squeeze);
    LeftGrip const leftBefore = latches.left;
    latches.left = UpdateSupportGrip(latches.support, leftGrip, options.support);
    // A new left squeeze away from the gun (not the support grip): the squad menu's (counted for the mod).
    if (leftBefore == LeftGrip::Open && latches.left != LeftGrip::Open) {
        latches.leftAway = latches.left == LeftGrip::Shoulder;
        if (latches.leftAway && options.countKept) {
            ++latches.leftAwayCount;
        }
    } else if (latches.left == LeftGrip::Open) {
        latches.leftAway = false;
    }
    if (latches.left == LeftGrip::Shoulder && options.gripShoulder) buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    bool const leftTriggerDown = Latch(latches.leftTrigger, l.trigger);
    if (options.leftTriggerShoulder && leftTriggerDown) buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (latches.left == LeftGrip::Support && options.supportAims) pad.bLeftTrigger = 255;
    // The right grip is RB, except a squeeze that began at a body holster (Options::rightGripFree) or
    // while the grip holds the gun (Options::rightGripHolds): that one stays off RB until the grip
    // opens, wherever the hand goes and whatever the options say meanwhile.
    bool const rightWasDown = latches.rightGrip;
    bool const rightGrip = Latch(latches.rightGrip, r.squeeze);
    if (rightGrip && !rightWasDown) {
        latches.rightHold = options.rightGripHolds;
        latches.rightFree = options.rightGripFree || options.rightGripHolds;
        if (latches.rightFree && options.countKept) {
            ++latches.rightFreeCount;
        }
    } else if (!rightGrip) {
        latches.rightFree = false;
        latches.rightHold = false;
    }
    bool rb = rightGrip && !latches.rightFree;
    // RB from Options::rbSource while the grip holds the gun. Each press is RB or its own button,
    // decided when it goes down.
    RbSource const source = options.rightGripHolds ? options.rbSource : RbSource::None;
    struct Face {
        bool down;
        RbSource as;
        WORD bit;
        std::uint8_t mask;
        bool allowed;  // reaches the game as its own button (B / X can be the physical melee's and throw's only)
    };
    Face const faces[] = {
        {r.secondary, RbSource::B, XINPUT_GAMEPAD_B, 1, options.meleeButton},
        {l.primary, RbSource::X, XINPUT_GAMEPAD_X, 2, options.grenadeButton},
        {l.secondary, RbSource::Y, XINPUT_GAMEPAD_Y, 4, true},
    };
    for (Face const& face : faces) {
        bool const was = (latches.faceDown & face.mask) != 0;
        if (face.down && !was && source == face.as) {
            latches.faceRb = static_cast<std::uint8_t>(latches.faceRb | face.mask);
        } else if (!face.down || !was) {
            latches.faceRb = static_cast<std::uint8_t>(latches.faceRb & ~face.mask);
        }
        latches.faceDown = static_cast<std::uint8_t>(face.down ? (latches.faceDown | face.mask) : (latches.faceDown & ~face.mask));
        if (face.down) {
            if ((latches.faceRb & face.mask) != 0) {
                rb = true;
            } else if (face.allowed) {
                buttons |= face.bit;
            }
        }
    }
    // The right stick click stays R3 either way: Halcyon's double-click recenter reads it, and the
    // game's R3 (ChangeMagnification, a scoped weapon's zoom step) does nothing in Halcyon's view.
    RbClickInput click;
    click.click = r.stickClick;
    click.stickX = r.stickX;
    click.stickY = r.stickY;
    click.otherClick = l.stickClick;
    click.reserved = options.stickClicksReserved;
    click.enabled = source == RbSource::RightStickClick;
    click.nowMs = state.timeMs;
    if (RbFromStickClick(latches.rbClick, click)) {
        rb = true;
    }
    if (rb) buttons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (r.primary || (options.nativeWheel && options.wheelPressA)) buttons |= XINPUT_GAMEPAD_A;
    if (GateClick(latches.leftClick, l.stickClick, r.stickClick, state.timeMs)) buttons |= XINPUT_GAMEPAD_LEFT_THUMB;
    if (r.stickClick) buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;
    if (l.menu) buttons |= leftGrip ? XINPUT_GAMEPAD_BACK : XINPUT_GAMEPAD_START;
    pad.wButtons = buttons;
    return pad;
}

XINPUT_GAMEPAD Merge(XINPUT_GAMEPAD const& a, XINPUT_GAMEPAD const& b) {
    XINPUT_GAMEPAD out{};
    out.wButtons = a.wButtons | b.wButtons;
    out.bLeftTrigger = std::max(a.bLeftTrigger, b.bLeftTrigger);
    out.bRightTrigger = std::max(a.bRightTrigger, b.bRightTrigger);
    Stronger(a.sThumbLX, a.sThumbLY, b.sThumbLX, b.sThumbLY, out.sThumbLX, out.sThumbLY);
    Stronger(a.sThumbRX, a.sThumbRY, b.sThumbRX, b.sThumbRY, out.sThumbRX, out.sThumbRY);
    return out;
}

void RotateStick(SHORT& x, SHORT& y, int yawUnits) {
    if (yawUnits == 0 || (x == 0 && y == 0)) {
        return;
    }
    double const yaw = yawUnits * std::numbers::pi / 32768.0;
    double const c = std::cos(yaw);
    double const s = std::sin(yaw);
    double const rx = x * c + y * s;
    double const ry = -x * s + y * c;
    x = static_cast<SHORT>(std::clamp(std::lround(rx), -32768L, 32767L));
    y = static_cast<SHORT>(std::clamp(std::lround(ry), -32768L, 32767L));
}

} // namespace mevr::game::gamepad
