// SPDX-License-Identifier: MIT
#include "game/GameMenuRequest.hpp"

#include "core/Log.hpp"
#include "core/vr/GameMenuRequest.hpp"
#include "engine/GameAdapter.hpp"
#include "game/MenuPause.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <format>

namespace mevr::game::game_menu_request {
namespace {

namespace gm = vr::game_menu;

gm::Schedule gSchedule;
bool gToldOldHost = false;
std::string gLast = "none";

#if defined(MEVR_GAME_LE1)
// Our reflection layer (research/34); engine/ReflectCheck has a row for each.
ue3::FieldRef gGameModeManager{"SFXGame.BioPlayerController", engine::adapter::kGameModeManager};
ue3::FieldRef gGameModes{"SFXGame.SFXGameModeManager", "GameModes"};
ue3::FieldRef gCurrentMode{"SFXGame.SFXGameModeManager", "CurrentMode"};
ue3::FieldRef gIsActive{"SFXGame.SFXGameModeBase", "bIsActive"};
ue3::FieldRef gPriority{"SFXGame.SFXGameModeBase", "Priority"};
ue3::ClassRef gModeBase{"SFXGame.SFXGameModeBase"};
ue3::ClassRef gModeDefault{"SFXGame.SFXGameModeDefault"};
ue3::ClassRef gModeVehicle{"SFXGame.SFXGameModeVehicle"};
ue3::ClassRef gModePowerWheel{"SFXGame.SFXGameModePowerWheel"};
ue3::ClassRef gModeWeaponWheel{"SFXGame.SFXGameModeWeaponWheel"};
ue3::ClassRef gModeCommand{"SFXGame.SFXGameModeCommand"};
// Start (Console_Menu) calls it on the mode that has the input; looked up on the mode object's own class for the
// overrides (SFXGameModeVehicle, -PowerWheel, -WeaponWheel, -Command, -Photo). This one is the declaration, resolved
// first so a missing function says so once.
ue3::FunctionRef gShowMenuGuiBase{"SFXGame.SFXGameModeBase.ShowMenuGui"};
ue3::FunctionRef gTryHolster{"SFXGame.SFXGameModeDefault.TryHolster"};
ue3::FunctionRef gIsInCombatActionState{"SFXGame.BioPawn.IsInCombatActionState"};

constexpr char const* kFunctionName = "ShowMenuGui";

// EGameModes (research/13): the modes whose object is known by class. Others (conversation, GUI...) go by bIsActive.
ue3::ClassRef* ClassForMode(std::uint8_t mode) {
    switch (mode) {
    case 0: return &gModeDefault;
    case 1: return &gModeVehicle;
    case 2: return &gModePowerWheel;
    case 3: return &gModeWeaponWheel;
    case 4: return &gModeCommand;
    default: return nullptr;
    }
}

struct Modes {
    ue3::Object* manager = nullptr;
    ue3::TArray<ue3::Object*> const* list = nullptr;
    std::optional<std::uint8_t> current;
};

std::optional<Modes> ReadModes(std::string& why) {
    if (!ue3::runtime::Ready()) {
        why = "our reflection layer is off (see the 'reflect:' log lines)";
        return std::nullopt;
    }
    ue3::Object* const pc = player::Controller();
    if (pc == nullptr || !player::IsBioPlayerController(pc)) {
        why = "no player controller";
        return std::nullopt;
    }
    Modes modes;
    modes.manager = gGameModeManager.Read<ue3::Object*>(pc).value_or(nullptr);
    modes.list = modes.manager != nullptr ? gGameModes.In<ue3::TArray<ue3::Object*>>(modes.manager) : nullptr;
    if (modes.list == nullptr) {
        why = "the player controller has no game modes";
        return std::nullopt;
    }
    modes.current = gCurrentMode.Read<std::uint8_t>(modes.manager);
    return modes;
}

ue3::Object* FindOfClass(Modes const& modes, ue3::ClassRef& cls) {
    for (int i = 0; i < modes.list->Count(); ++i) {
        ue3::Object* const mode = modes.list->data[i];
        if (mode != nullptr && ue3::IsA(mode, cls)) {
            return mode;
        }
    }
    return nullptr;
}

// The mode that has the input now: the one of CurrentMode's class, else the active one of the highest priority.
ue3::Object* ActiveMode(Modes const& modes, std::string& how) {
    if (modes.current) {
        if (ue3::ClassRef* const cls = ClassForMode(*modes.current)) {
            if (ue3::Object* const mode = FindOfClass(modes, *cls)) {
                how = std::format("game mode {}", *modes.current);
                return mode;
            }
        }
    }
    ue3::Object* best = nullptr;
    int bestPriority = -1;
    for (int i = 0; i < modes.list->Count(); ++i) {
        ue3::Object* const mode = modes.list->data[i];
        if (mode == nullptr || !ue3::IsA(mode, gModeBase) || !gIsActive.ReadBool(mode).value_or(false)) {
            continue;
        }
        int const priority = gPriority.Read<std::uint8_t>(mode).value_or(0);
        if (priority >= bestPriority) {
            best = mode;
            bestPriority = priority;
        }
    }
    if (best != nullptr) {
        how = std::format("game mode {}: the active mode of the highest priority, {}",
            modes.current ? std::to_string(*modes.current) : std::string("?"), bestPriority);
    }
    return best;
}

std::string ClassName(ue3::Object const* object) {
    ue3::Reflection& r = ue3::runtime::Get();
    return r.NameText(r.ClassOf(object));
}

void OpenPauseMenu() {
    std::string why;
    std::optional<Modes> const modes = ReadModes(why);
    if (!modes) {
        gLast = std::format("pause menu: not opened ({})", why);
        log::Warn("main menu: the game's pause menu not opened: {}", why);
        return;
    }
    std::string how;
    ue3::Object* const mode = ActiveMode(*modes, how);
    if (mode == nullptr) {
        gLast = "pause menu: no active game mode";
        log::Warn("main menu: the game's pause menu not opened: no active game mode (game mode {})",
            modes->current ? static_cast<int>(*modes->current) : -1);
        return;
    }
    if (gShowMenuGuiBase.Get() == nullptr) {
        gLast = "pause menu: SFXGameModeBase.ShowMenuGui not found";
        log::Warn("main menu: the game's pause menu not opened: SFXGame.SFXGameModeBase.ShowMenuGui not found");
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Function* const show = r.FindFunction(r.ClassOf(mode), kFunctionName);
    if (show == nullptr) {
        gLast = std::format("pause menu: {} has no ShowMenuGui", ClassName(mode));
        log::Warn("main menu: the game's pause menu not opened: {} has no ShowMenuGui", ClassName(mode));
        return;
    }
    ue3::Params parms(r, show);
    ue3::runtime::Call(mode, show, parms);  // the script function through ProcessEvent (game thread)
    bool const opened = parms.ReturnBool().value_or(false);
    std::string const declared = r.NameText(r.OuterOf(show));
    std::string const where = std::format("{}.ShowMenuGui{}", ClassName(mode),
        declared != ClassName(mode) ? std::format(" (declared on {})", declared) : std::string());
    gLast = std::format("pause menu: {} returned {}", where, opened ? 1 : 0);
    if (opened) {
        log::Info("main menu: opened the game's pause menu ({} returned 1; {})", where, how);
    } else {
        log::Warn("main menu: the game's pause menu didn't open ({} returned 0; {}): the game allows none here", where, how);
    }
}

void Holster() {
    std::string why;
    std::optional<Modes> const modes = ReadModes(why);
    ue3::Object* const pawn = modes ? player::BioPawn(player::Controller()) : nullptr;
    ue3::Object* const mode = modes ? FindOfClass(*modes, gModeDefault) : nullptr;
    if (modes) {
        if (!modes->current || *modes->current != 0) {
            why = std::format("not on foot (game mode {})", modes->current ? static_cast<int>(*modes->current) : -1);
        } else if (pawn == nullptr) {
            why = "no player pawn";
        } else if (gTryHolster.Get() == nullptr || gIsInCombatActionState.Get() == nullptr) {
            why = "SFXGameModeDefault.TryHolster or BioPawn.IsInCombatActionState not found";
        } else if (mode == nullptr) {
            why = "no SFXGameModeDefault in the player controller's game modes";
        }
    }
    if (!why.empty()) {
        gLast = std::format("holster: not done ({})", why);
        log::Info("main menu: holster not done: {}", why);
        return;
    }
    ue3::Params drawn(ue3::runtime::Get(), gIsInCombatActionState.Get());
    ue3::runtime::Call(pawn, gIsInCombatActionState.Get(), drawn);
    if (!drawn.ReturnBool().value_or(false)) {
        // TryHolster is the holster key's toggle: with the weapon away it would draw it.
        gLast = "holster: nothing drawn";
        log::Info("main menu: holster: no weapon drawn, nothing to put away (TryHolster not called: it would draw)");
        return;
    }
    ue3::Params parms(ue3::runtime::Get(), gTryHolster.Get());
    ue3::runtime::Call(mode, gTryHolster.Get(), parms);  // directly: never a generated wrapper
    gLast = "holster: TryHolster called";
    log::Info("main menu: holstered the weapon (SFXGameModeDefault.TryHolster)");
}

void Run(gm::Action action) {
    switch (action) {
    case gm::Action::PauseMenu: OpenPauseMenu(); return;
    case gm::Action::Holster: Holster(); return;
    case gm::Action::None: return;
    }
}
#else
void Run(gm::Action) {
    gLast = "not LE1";
}
#endif

} // namespace

void Tick(Link const& link) {
    if (link.take == nullptr) {
        if (!gToldOldHost) {
            gToldOldHost = true;
            log::Info("main menu request: the host (services older than v19) has no Game menu or Holster requests to give");
        }
        return;
    }
    gm::Action const taken = gm::FromValue(link.take());
    if (taken != gm::Action::None) {
        log::Info("main menu request: {} taken; it runs once the main menu is closed and the game runs ({} ticks)",
            gm::Name(taken), gm::kSettleTicks);
        gm::Take(gSchedule, taken);
    }
    gm::Step const step = gm::Advance(gSchedule, link.menuOpen, menu_pause::Paused());
    if (step.dropped != gm::Action::None) {
        gLast = std::format("{}: dropped", gm::Name(step.dropped));
        log::Warn("main menu request: {} dropped: {}", gm::Name(step.dropped),
            step.reopened ? "the main menu opened again first" : "the game stayed paused by the main menu");
    }
    if (step.run != gm::Action::None) {
        Run(step.run);
    }
}

void Shutdown() {
    gSchedule = {};
    gToldOldHost = false;
}

std::string StatusJson() {
    return std::format("{{\"waiting\": \"{}\", \"last\": \"{}\"}}", gm::Name(gSchedule.action), gLast);
}

} // namespace mevr::game::game_menu_request
