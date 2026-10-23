#include "game/MenuPause.hpp"

#include "core/Log.hpp"
#include "engine/Objects.hpp"
#include "game/HeadTracking.hpp"
#include "mod/ModApi.hpp"

#include <LESDK/Headers.hpp>

#include <format>

namespace mevr::game::menu_pause {
namespace {

#if defined(SDK_TARGET_LE1)
UFunction* gPauseGame = nullptr;
bool gLookedUp = false;
#endif
bool gPaused = false;     // we paused the game
bool gWasOpen = false;
std::string gLast = "never";

#if defined(SDK_TARGET_LE1)
bool Pause(bool pause, char const* why) {
    if (!gLookedUp) {
        gLookedUp = true;
        gPauseGame = engine::FindFunction(L"PauseGame", "Function Engine.WorldInfo.PauseGame");
        if (gPauseGame == nullptr) {
            log::Warn("menu pause: Engine.WorldInfo.PauseGame not found: the main menu won't pause the game");
        }
    }
    APlayerController* const pc = engine::LocalPlayerController();
    if (pc == nullptr || pc->WorldInfo == nullptr || gPauseGame == nullptr) {
        return false;
    }
    AWorldInfo_execPauseGame_Parms parms;
    parms.bPause = pause ? 1 : 0;
    pc->WorldInfo->ProcessEvent(gPauseGame, &parms, nullptr);  // the script function directly, never the SDK's wrapper
    gLast = std::format("{} ({})", pause ? "paused" : "resumed", why);
    log::Info("menu pause: the game {} ({}; WorldInfo.PauseGame, as ME1's weapon wheel)", pause ? "paused" : "runs again", why);
    return true;
}
#else
bool Pause(bool, char const*) {
    return false;
}
#endif

} // namespace

void Tick(Link const& link) {
    std::uint32_t const state = link.state != nullptr ? link.state() : 0u;
    bool const open = (state & kMainMenuOpen) != 0;
    bool const wanted = open && (state & kMainMenuPause) != 0;
    if (gPaused && !wanted) {
        Pause(false, open ? "pause turned off" : "the main menu closed");
        gPaused = false;
    } else if (wanted && !gPaused && !gWasOpen) {
        // Once each time it's wanted (the menu opens, or the toggle goes on), and only where the player is in control.
        std::optional<unsigned char> const mode = head_tracking::GameMode();
        if (mode && (*mode == 0 || *mode == 1)) {
            gPaused = Pause(true, "the main menu is open");
        } else {
            gLast = std::format("not paused: game mode {}", mode ? static_cast<int>(*mode) : -1);
        }
    }
    gWasOpen = wanted;  // "wanted last tick": a new opening or the toggle turned on tries again
}

void Shutdown(bool exiting) {
    if (gPaused && !exiting) {
        Pause(false, "the mod unloads");
    }
    gPaused = false;
    gWasOpen = false;
}

std::string StatusJson() {
    return std::format("{{\"paused\": {}, \"last\": \"{}\"}}", gPaused ? "true" : "false", gLast);
}

} // namespace mevr::game::menu_pause
