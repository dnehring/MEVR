#include "game/MenuPause.hpp"

#include "core/Log.hpp"
#include "game/HeadTracking.hpp"
#include "mod/ModApi.hpp"
#include "ue3/Handles.hpp"

#include <format>

namespace mevr::game::menu_pause {
namespace {

#if defined(MEVR_GAME_LE1)
// Our reflection layer (ue3/, research/34).
ue3::FunctionRef gPauseGameRef{"Engine.WorldInfo.PauseGame"};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::Function* gPauseGame = nullptr;
bool gLookedUp = false;
#endif
bool gPaused = false;     // we paused the game
bool gWasOpen = false;
std::string gLast = "never";

#if defined(MEVR_GAME_LE1)
bool Pause(bool pause, char const* why) {
    if (!ue3::runtime::Ready()) {
        if (!gLookedUp) {
            gLookedUp = true;
            log::Warn("menu pause: our reflection layer is off ({}): the main menu won't pause the game", ue3::runtime::Status());
        }
        return false;
    }
    if (!gLookedUp) {
        gLookedUp = true;
        gPauseGame = gPauseGameRef.Get();
        if (gPauseGame == nullptr) {
            log::Warn("menu pause: Engine.WorldInfo.PauseGame not found: the main menu won't pause the game");
        }
    }
    ue3::Object* const pc = ue3::runtime::LocalPlayerController();
    ue3::Object* const world = pc != nullptr ? gWorldInfo.Read<ue3::Object*>(pc).value_or(nullptr) : nullptr;
    if (world == nullptr || gPauseGame == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), gPauseGame);
    if (!parms.SetBool("bPause", pause)) {
        log::Warn("menu pause: WorldInfo.PauseGame has no bool parameter bPause: not called");
        return false;
    }
    ue3::runtime::Call(world, gPauseGame, parms);  // the script function through ProcessEvent
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
