// SPDX-License-Identifier: MIT
// The holds on Halcyon's flat/VR presentation (seams in src/halcyon/le1/xr_session.cpp, declared in mevr_bridge.h; rules in
// core/vr/PresentationHold, research/56): the front end's unreadable mode byte and its cinematics before the first load are
// the menu, and plain VR after another view waits 100 ms. Present thread only.

#include "halcyon/le1/mevr_bridge.h"

#include "core/Log.hpp"
#include "core/vr/PresentationHold.hpp"

#include <Windows.h>

#include <cstdint>

namespace MELEVR::MevrBridge {
namespace {

namespace presentation = mevr::vr::presentation;

constexpr unsigned kMaxCoveredLogs = 50;  // a mode that flickers mustn't flood the log

presentation::FrontEnd gFrontEnd;
presentation::VrEntry gVrEntry;
bool gFrontEndHeld = false;
std::uint64_t gFrontEndSinceMs = 0;
int gLoggedMode = -2;    // the mode the front end's hold last logged (-2: none)
int gHeldFromMode = -1;  // the mode of the last other view shown, before the plain VR being held back
int gBlipMode = -1;      // the mode while plain VR was held back

} // namespace

static char const* ModeName(int gameMode) {
    switch (gameMode) {
    case -1: return "unreadable";
    case 0: return "default";
    case 1: return "vehicle";
    case 2: return "power wheel";
    case 3: return "weapon wheel";
    case 4: return "command";
    case 5: return "conversation";
    case 6: return "cinematic";
    case 7: return "GUI";
    case 8: return "movie";
    case 9: return "galaxy";
    case 10: return "minigame";
    }
    return "?";
}

bool FrontEndHold(int gameMode) noexcept {
    bool const wasStarting = gFrontEnd.Starting();
    bool const held = gFrontEnd.Update(gameMode);
    std::uint64_t const now = GetTickCount64();
    if (held && (!gFrontEndHeld || gameMode != gLoggedMode)) {
        if (!gFrontEndHeld) {
            gFrontEndSinceMs = now;
        }
        mevr::log::Info("presentation: mode {} ({}) {}: the front end ({}){}", gameMode, ModeName(gameMode),
            gFrontEnd.Starting() ? "at start-up, before the first load or gameplay" : "after the menus (a load)",
            MELEVR::MevrBridge::FrontEndOnScreen() ? "the 3D screen; a movie on the flat panel" : "the flat panel",
            gameMode == -1 && gFrontEnd.LastReadable() < 0 ? ", not the title's backdrop in VR" : "");
        gLoggedMode = gameMode;
    } else if (!held && gFrontEndHeld) {
        mevr::log::Info("presentation: mode {} ({}) after {:.1f} s of the front end: Halcyon decides again",
            gameMode, ModeName(gameMode), static_cast<double>(now - gFrontEndSinceMs) / 1000.0);
        gLoggedMode = -2;
    }
    if (wasStarting && !gFrontEnd.Starting()) {
        mevr::log::Info("presentation: the start-up's front end is over (mode {}, {}): cinematics are the game's from here",
            gameMode, gameMode < 0 ? "a load from the menus" : "gameplay");
    }
    gFrontEndHeld = held;
    return held;
}

bool HoldPlainVr(bool plainVr, int gameMode) noexcept {
    unsigned const coveredBefore = gVrEntry.Covered();
    bool const hold = gVrEntry.Hold(plainVr, GetTickCount64());
    if (hold) {
        gBlipMode = gameMode;
    }
    if (gVrEntry.Covered() != coveredBefore && gVrEntry.Covered() <= kMaxCoveredLogs) {
        mevr::log::Info("presentation: plain VR held back: mode {} between mode {} and mode {} was never shown as the world "
                        "({} so far)", gBlipMode, gHeldFromMode, gameMode, gVrEntry.Covered());
    }
    if (!plainVr) {
        gHeldFromMode = gameMode;
    }
    return hold;
}

} // namespace MELEVR::MevrBridge
