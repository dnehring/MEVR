// SPDX-License-Identifier: MIT
#include "game/MenuPause.hpp"

#include "core/Log.hpp"
#include "core/vr/PauseOwner.hpp"
#include "core/WriteJournal.hpp"
#include "engine/GameAdapter.hpp"
#include "game/HeadTracking.hpp"
#include "mod/ModApi.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"
#if defined(MEVR_GAME_LE3)
#include "ue3/UnambiguousMethod.hpp"
#endif

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
#elif defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
// ME2 has no WorldInfo.PauseGame. ME1's is one line, `bPlayersOnly = bPause`, and ME2's own weapon wheel does the same
// (SFXGameModeWeaponWheel.Activated sets Outer.WorldInfo.bPlayersOnly = TRUE, Deactivated FALSE; ShowBrowserWheel keeps
// the value it found and sets it): the field is written directly, as ME2's script does (research/le2-menus.md).
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gPlayersOnly{"Engine.WorldInfo", "bPlayersOnly", ue3::kEveryGame};
bool gToldDown = false;
vr::PauseOwner gOwner;
std::string gOwnerName; // helps reject a recycled object address
#if defined(MEVR_GAME_LE3)
ue3::FunctionRef gNativePause{"SFXGame.BioWorldInfo.PauseGame", ue3::kEveryGame};

bool NativePause(ue3::Object* world, bool value) {
    // LE3's override honors SFXGRI.bPauseForCommand. Calling the Engine base function or
    // writing its bit directly would bypass that admission (research/le3-menu-pause.md).
    auto* fn = gNativePause.Get();
    auto const before = gPlayersOnly.ReadBool(world);
    if (!before || fn == nullptr || ue3::UnambiguousMethod(ue3::runtime::Get(), world, "PauseGame") != fn) return false;
    if (*before == value) return true;
    ue3::Params args(ue3::runtime::Get(), fn);
    if (!args.SetBool("bPause", value) || !ue3::runtime::Call(world, fn, args)) return false;
    // Dispatch is not acceptance: the native script may deliberately leave the bit alone.
    auto const observed = gPlayersOnly.ReadBool(world);
    return observed && *observed == value;
}
#endif

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
#if defined(MEVR_GAME_LE3)
        return NativePause(world, value);
#else
        auto const was = gPlayersOnly.ReadBool(world);
        if (!was) return false;
        if (*was != value) {
            if (!gPlayersOnly.WriteBool(world, value)) return false;
            if (void* const field = gPlayersOnly.Address(world)) write_journal::Wrote("MenuPause.bPlayersOnly", field, 4);
        }
        return true;
#endif
    };
    if (pause) {
        auto* world = CurrentWorld();
        if (!ue3::runtime::IsAlive(world)) return false;
        auto const previous = gPlayersOnly.ReadBool(world);
        if (!previous) return false;
#if defined(MEVR_GAME_LE3)
        auto const name = ue3::runtime::FullName(world);
        if (!gOwner.Acquire(reinterpret_cast<std::uintptr_t>(world), *previous, [&](std::uintptr_t) {
            return write(world, true);
        })) {
            gLast = "pause refused or unavailable: native BioWorldInfo.PauseGame did not accept";
            return false;
        }
        gOwnerName = name;
#else
        if (!write(world, true)) return false;
        gOwner.Capture(reinterpret_cast<std::uintptr_t>(world), *previous);
        gOwnerName = ue3::runtime::FullName(world);
#endif
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
    log::Info("menu pause: original world pause {} ({}; " MEVR_GAME_NAME_A " native pause)", pause ? "captured" : "restored or released", why);
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
    bool worldChanged = false;
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
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
        // On foot or in a vehicle (ME1's 0 and 1), ME3's Atlas too, through each game's table (ME2's 1 is its power wheel,
        // its Hammerhead 4: engine/GameAdapter.hpp).
        bool const controllable = mode && (engine::adapter::GameMode(*mode) == engine::adapter::Mode::OnFoot ||
            engine::adapter::GameMode(*mode) == engine::adapter::Mode::Vehicle ||
            engine::adapter::GameMode(*mode) == engine::adapter::Mode::Atlas);
        if (controllable) {
            gPaused = Pause(true, "the main menu is open");
            gWasOpen = true;  // tried: a failed call isn't repeated every tick
        } else {
            gLast = std::format("not paused yet: game mode {}", mode ? static_cast<int>(*mode) : -1);
        }
        return;
    }
    gWasOpen = wanted;  // "wanted last tick": a new opening or the toggle turned on tries again
}

bool Paused() {
    return gPaused;
}

void Shutdown(bool exiting) {
    if (gPaused && !exiting && !Pause(false, "the mod unloads")) {
        log::Warn("menu pause: unload could not restore the original world's pause state; restoration remains pending");
        return;
    }
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    gOwner.Forget(); gOwnerName.clear();
#endif
    gPaused = false;
    gWasOpen = false;
}

std::string StatusJson() {
    return std::format("{{\"paused\": {}, \"last\": \"{}\"}}", gPaused ? "true" : "false", gLast);
}

} // namespace mevr::game::menu_pause
