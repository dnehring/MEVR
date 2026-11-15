// SPDX-License-Identifier: MIT
#include "game/MenuPause.hpp"

#include "core/Log.hpp"
#include "core/vr/PauseOwner.hpp"
#include "core/WriteJournal.hpp"
#include "game/HeadTracking.hpp"
#include "mod/ModApi.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <format>
#include <optional>

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
std::uint32_t (*gStateReader)() = nullptr;

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
    if (!ue3::runtime::Call(world, gPauseGame, parms)) return false;  // keep ownership when the restore failed
    gLast = std::format("{} ({})", pause ? "paused" : "resumed", why);
    log::Info("menu pause: the game {} ({}; WorldInfo.PauseGame, as ME1's weapon wheel)", pause ? "paused" : "runs again", why);
    return true;
}
#elif defined(MEVR_GAME_LE2)
// ME2 has no WorldInfo.PauseGame. ME1's is one line, `bPlayersOnly = bPause`, and ME2's own weapon wheel does the same
// (SFXGameModeWeaponWheel.Activated sets Outer.WorldInfo.bPlayersOnly = TRUE, Deactivated FALSE; ShowBrowserWheel keeps
// the value it found and sets it): the field is written directly, as ME2's script does (research/le2-menus.md).
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gPlayersOnly{"Engine.WorldInfo", "bPlayersOnly", ue3::kEveryGame};
bool gToldDown = false;
vr::PauseOwner gOwner;
std::string gOwnerName; // helps reject a recycled object address

ue3::Object* CurrentWorld() {
    if (!ue3::runtime::Up()) return nullptr;
    auto* pc = ue3::runtime::LocalPlayerController();
    return pc != nullptr ? gWorldInfo.Read<ue3::Object*>(pc).value_or(nullptr) : nullptr;
}

bool Pause(bool pause, char const* why) {
    if (!ue3::runtime::Up()) {
        if (!gToldDown) {
            gToldDown = true;
            log::Warn("menu pause: our reflection layer is off ({}): the main menu won't pause the game", ue3::runtime::Status());
        }
        return false;
    }
    auto write = [&](ue3::Object* world, bool value) {
        auto const was = gPlayersOnly.ReadBool(world);
        if (!was) return false;
        if (*was != value) {
            if (!gPlayersOnly.WriteBool(world, value)) return false;
            if (void* const field = gPlayersOnly.Address(world)) write_journal::Wrote("MenuPause.bPlayersOnly", field, 4);
        }
        return true;
    };
    if (pause) {
        auto* world = CurrentWorld();
        if (!ue3::runtime::IsAlive(world)) return false;
        auto const previous = gPlayersOnly.ReadBool(world);
        if (!previous || !write(world, true)) return false;
        gOwner.Capture(reinterpret_cast<std::uintptr_t>(world), *previous);
        gOwnerName = ue3::runtime::FullName(world);
    } else if (!gOwner.Release([&](std::uintptr_t owner, bool previous) {
        auto* world = reinterpret_cast<ue3::Object*>(owner);
        if (!ue3::runtime::IsAlive(world) || ue3::runtime::FullName(world) != gOwnerName) {
            log::Info("menu pause: original world is gone; discarded its saved pause state without changing the new world");
            return true;
        }
        return write(world, previous); // always the captured owner, never CurrentWorld()
    })) {
        gLast = "restore pending: original world's pause bit could not be restored";
        return false;
    }

    gLast = std::format("{} ({})", pause ? "paused" : "resumed", why);
    log::Info("menu pause: original world pause {} ({}; ME2 WorldInfo.bPlayersOnly)", pause ? "captured" : "restored or released", why);
    return true;
}
#else
bool Pause(bool, char const*) {
    return false;
}
#endif

} // namespace

void Tick(Link const& link) {
    gStateReader = link.state;
    std::uint32_t const state = link.state != nullptr ? link.state() : 0u;
    bool const open = (state & kMainMenuOpen) != 0;
    bool const wanted = (open && (state & kMainMenuPause) != 0) || link.squadPause;
    bool worldChanged = false;
#if defined(MEVR_GAME_LE2)
    worldChanged = gPaused && ue3::runtime::Up() && gOwner.Owner() != reinterpret_cast<std::uintptr_t>(CurrentWorld());
#endif
    if (gPaused && (!wanted || worldChanged)) {
        if (Pause(false, worldChanged ? "world changed" : open ? "pause turned off" : "all requesting menus closed")) {
            gPaused = false;
            if (worldChanged) { gWasOpen = false; return; } // a still-open menu may acquire the new world next tick
        }
    } else if (wanted && !gPaused && !gWasOpen) {
        // Once each time it's wanted (the menu opens, or the toggle goes on), and only where the player is in control.
        // In any other mode it waits for one: the menu opened over a weapon or power wheel closes the wheel (the
        // controllers go to the menu) and the game comes back to mode 0, which tried nothing until 2026-11-06 and left
        // the game running behind the menu (Codex review, gameM#3).
        std::optional<unsigned char> const mode = head_tracking::GameMode();
        if (mode && (*mode == 0 || *mode == 1)) {
            gPaused = Pause(true, "the main menu is open");
            gWasOpen = true;  // tried: a failed call isn't repeated every tick
        } else {
            gLast = std::format("not paused yet: game mode {}", mode ? static_cast<int>(*mode) : -1);
        }
        return;
    }
    gWasOpen = wanted;  // "wanted last tick": a new opening or the toggle turned on tries again
}

void SetSquadPause(bool pause) {
    Link link; link.state = gStateReader; link.squadPause = pause;
    Tick(link);
}

bool Paused() {
    return gPaused;
}

void Shutdown(bool exiting) {
    gStateReader = nullptr;
    if (gPaused && !exiting && !Pause(false, "the mod unloads")) {
        log::Warn("menu pause: unload could not restore the original world's pause state; restoration remains pending");
        return;
    }
#if defined(MEVR_GAME_LE2)
    gOwner.Forget(); gOwnerName.clear();
#endif
    gPaused = false;
    gWasOpen = false;
}

std::string StatusJson() {
    return std::format("{{\"paused\": {}, \"last\": \"{}\"}}", gPaused ? "true" : "false", gLast);
}

} // namespace mevr::game::menu_pause
