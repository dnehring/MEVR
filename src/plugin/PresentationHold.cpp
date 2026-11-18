// SPDX-License-Identifier: MIT
// The holds on Halcyon's flat/VR presentation (seams in src/halcyon/le1/xr_session.cpp, declared in mevr_bridge.h; rules in
// core/vr/PresentationHold, research/56): the front end's unreadable mode byte and its cinematics before the first load are
// the menu, and plain VR after another view waits 100 ms. Present thread only.

#include "halcyon/le1/mevr_bridge.h"

#include "core/Log.hpp"
#include "core/vr/PresentationHold.hpp"
#include "plugin/CineScreen.hpp"

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
            !FrontEndOnScreen() ? "the flat panel" : gFrontEnd.BeforeTitle() ? "the boot before the title: flat, head look parked"
                                                    : "the 3D screen; a movie on the flat panel",
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

bool FrontEndBeforeTitle() noexcept {
    return gFrontEnd.BeforeTitle();
}

bool FrontEndOnScreen() noexcept {
    // A load from the menus stays on the flat panel (its unreadable byte and movie: no scene to show, and the screen would
    // come and go for the second before Halcyon sees the movie decoding).
    return gFrontEnd.Starting() && mevr::host::cine_screen::FrontEndWanted();
}

bool HoldPlainVr(bool plainVr, int gameMode, bool worldUi) noexcept {
    if (plainVr && worldUi) gVrEntry.AcceptWorldUi();
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

#if !defined(MEVR_WITH_HALCYON)
// The sequels (T18, research/56 "The sequels"): the same holds and the same log through the common seams
// (src/halcyon/MevrSeams.h), one implementation above for the three games. A renderer passes its game's own mode number;
// the rules are written in ME1's, so the game's adapter (engine/GameAdapter.hpp) maps it: the gameplay family to ME1's 0..4
// (ME3's Atlas and injured Shepard are on foot), the conversation, cinematic, GUI, movie and galaxy modes to 5..9, the
// orbit and minigame to 10, anything else to -1 (unreadable). LE1's renderer calls the functions above by their LE1 names.
#include "engine/GameAdapter.hpp"

namespace {

int Me1Mode(int gameMode) noexcept {
    namespace ga = mevr::engine::adapter;
    switch (ga::GameMode(gameMode)) {
    case ga::Mode::OnFoot: case ga::Mode::Atlas: case ga::Mode::Injured: return 0;
    case ga::Mode::Vehicle: return 1;
    case ga::Mode::PowerWheel: return 2;
    case ga::Mode::WeaponWheel: return 3;
    case ga::Mode::Command: return 4;
    case ga::Mode::Conversation: return 5;
    case ga::Mode::Cinematic: return 6;
    case ga::Mode::Gui: return 7;
    case ga::Mode::Movie: return 8;
    case ga::Mode::Galaxy: return 9;
    case ga::Mode::Orbital: case ga::Mode::Minigame: return 10;
    case ga::Mode::Unknown: break;
    }
    return -1;
}

mevr::vr::presentation::LoadTail gLoadTail;
bool gLoadTailHeld = false;
std::uint64_t gLoadTailSinceMs = 0;
int gLoadTailMode = -1;
constexpr unsigned kMaxTailLogs = 20;
unsigned gTailLogs = 0;

} // namespace

namespace MevrSeams {

bool FrontEndHold(int gameMode) noexcept {
    return MELEVR::MevrBridge::FrontEndHold(Me1Mode(gameMode));
}

bool FrontEndOnScreen() noexcept {
    return MELEVR::MevrBridge::FrontEndOnScreen();
}

bool FrontEndBeforeTitle() noexcept {
    return MELEVR::MevrBridge::FrontEndBeforeTitle();
}

bool HoldPlainVr(bool plainVr, int gameMode, bool worldUi) noexcept {
    return MELEVR::MevrBridge::HoldPlainVr(plainVr, Me1Mode(gameMode), worldUi);
}

bool HoldLoadTail(bool movie, int gameMode) noexcept {
    int const me1 = Me1Mode(gameMode);
    unsigned const coveredBefore = gLoadTail.Covered();
    std::uint64_t const now = GetTickCount64();
    bool const held = gLoadTail.Hold(movie, me1 == 5 || me1 == 6, now);
    if (held && !gLoadTailHeld) {
        gLoadTailSinceMs = now;
        gLoadTailMode = gameMode;
    } else if (!held && gLoadTailHeld && gTailLogs < kMaxTailLogs) {
        ++gTailLogs;
        mevr::log::Info("presentation: game mode {} ({}) right after a movie was kept as the movie for {:.2f} s, then mode {}: {}",
            gLoadTailMode, gLoadTail.Covered() != coveredBefore ? "a load's tail" : "a cinematic",
            static_cast<double>(now - gLoadTailSinceMs) / 1000.0, gameMode,
            gLoadTail.Covered() != coveredBefore ? "never shown as the 3D screen" : "it lasted: the game's own, shown as it is");
    }
    gLoadTailHeld = held;
    return held;
}

} // namespace MevrSeams
#endif
