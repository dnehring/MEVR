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
// The omni-tool's rail (research/54): the screens' own functions (declared on SFXGameModeBase only: no mode overrides them, so
// in game mode 7 the screens open only from the game's Flash wheel), and the GUI manager's focused panel, whose handler takes
// B as the player's back-out.
ue3::FunctionRef gShowEquipmentGui{"SFXGame.SFXGameModeBase.ShowEquipmentGui"};
ue3::FunctionRef gShowSquadGui{"SFXGame.SFXGameModeBase.ShowSquadGui"};
ue3::FunctionRef gShowJournalGui{"SFXGame.SFXGameModeBase.ShowJournalGui"};
ue3::FunctionRef gShowCodexGui{"SFXGame.SFXGameModeBase.ShowCodexGui"};
ue3::FunctionRef gShowMapGui{"SFXGame.SFXGameModeBase.ShowMapGui"};
ue3::FunctionRef gHandleInputEvent{"Engine.BioSFHandler.HandleInputEvent"};
ue3::FieldRef gScaleFormManager{"Engine.PlayerController", "m_oCachedScaleFormManager"};
ue3::FieldRef gFocusPanel{"Engine.BioSFManager", "m_FocusPanel"};
ue3::FieldRef gPanelTag{"Engine.BioSFPanel", "nmTag"};
ue3::FieldRef gPanelHandler{"Engine.BioSFPanel", "m_DefaultHandler"};
constexpr std::uint8_t kGuiEventB = 13;  // BIOGUI_EVENT_BUTTON_B (Engine.EBioGuiEvent)
constexpr int kBackOutWaitTicks = 4;     // after a B, the ticks to let the screen go before looking again
constexpr int kMaxBackOuts = 6;          // a screen over the main wheel takes two

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

// ---- The omni-tool's rail (research/54) ----
// A screen is opened the way its key opens it, on the mode that has the input (on foot or the Mako); with a screen already up
// (game mode 7) the rail backs out of it first, B on the focused panel as the player would, a few ticks apart, until the game
// leaves mode 7, then opens the new one. CloseScreens is the backing out alone.
constexpr std::uint8_t kModeGui = 7;

struct Switch {
    gm::Action action = gm::Action::None;  // what to do once no screen is up (CloseScreens: nothing more)
    int backOuts = 0;
    int wait = 0;
};
Switch gSwitch;

ue3::FunctionRef* ShowRef(gm::Action action) {
    switch (action) {
    case gm::Action::Equipment: return &gShowEquipmentGui;
    case gm::Action::Squad: return &gShowSquadGui;
    case gm::Action::Journal: return &gShowJournalGui;
    case gm::Action::Codex: return &gShowCodexGui;
    case gm::Action::Map: return &gShowMapGui;
    default: return nullptr;
    }
}

// The panel tag a screen opens (research/53's audit): the rail asking for the one already in focus does nothing.
char const* PanelTag(gm::Action action) {
    switch (action) {
    case gm::Action::Equipment: return "Inventory";
    case gm::Action::Squad: return "CharacterRecord";
    case gm::Action::Journal: return "Journal";
    case gm::Action::Codex: return "Codex";
    case gm::Action::Map: return "Map";
    default: return "";
    }
}

struct Focus {
    ue3::Object* panel = nullptr;
    ue3::Object* handler = nullptr;
    std::string tag;
};

Focus FocusedPanel() {
    Focus focus;
    ue3::Object* const pc = player::Controller();
    ue3::Object* const manager = pc != nullptr ? gScaleFormManager.Read<ue3::Object*>(pc).value_or(nullptr) : nullptr;
    focus.panel = manager != nullptr ? gFocusPanel.Read<ue3::Object*>(manager).value_or(nullptr) : nullptr;
    if (focus.panel != nullptr && ue3::runtime::Get().IsAlive(focus.panel)) {
        focus.handler = gPanelHandler.Read<ue3::Object*>(focus.panel).value_or(nullptr);
        if (auto const tag = gPanelTag.Read<ue3::Name>(focus.panel)) {
            focus.tag = ue3::runtime::Get().ToString(*tag);
        }
    } else {
        focus.panel = nullptr;
    }
    return focus;
}

void OpenScreen(gm::Action action, Modes const& modes) {
    ue3::FunctionRef* const ref = ShowRef(action);
    std::string how;
    ue3::Object* const mode = ActiveMode(modes, how);
    if (ref == nullptr || ref->Get() == nullptr || mode == nullptr) {
        gLast = std::format("{}: not opened ({})", gm::Name(action), mode == nullptr ? "no active game mode" : "its function wasn't found");
        log::Warn("omni-tool: {} not opened: {}", gm::Name(action), mode == nullptr ? "no active game mode" : "SFXGameModeBase's function not found");
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Function* const show = r.FindFunction(r.ClassOf(mode), gm::ShowFunction(action));
    if (show == nullptr) {
        gLast = std::format("{}: {} has no {}", gm::Name(action), ClassName(mode), gm::ShowFunction(action));
        log::Warn("omni-tool: {} not opened: {} has no {}", gm::Name(action), ClassName(mode), gm::ShowFunction(action));
        return;
    }
    ue3::Params parms(r, show);
    parms.SetBool("bShow", true);
    ue3::runtime::Call(mode, show, parms);  // as its key: the script function through ProcessEvent (game thread)
    gLast = std::format("{}: {}.{}(true) called", gm::Name(action), ClassName(mode), gm::ShowFunction(action));
    log::Info("omni-tool: opened {} ({}.{}(true); {})", gm::Name(action), ClassName(mode), gm::ShowFunction(action), how);
}

void StepSwitch() {
    if (gSwitch.action == gm::Action::None) {
        return;
    }
    std::string why;
    std::optional<Modes> const modes = ReadModes(why);
    if (!modes || !modes->current) {
        gLast = std::format("{}: not done ({})", gm::Name(gSwitch.action), modes ? "no game mode" : why);
        log::Warn("omni-tool: {} not done: {}", gm::Name(gSwitch.action), modes ? "the game mode isn't known" : why);
        gSwitch = {};
        return;
    }
    std::uint8_t const current = *modes->current;
    if (current == kModeGui) {
        Focus const focus = FocusedPanel();
        if (gm::IsScreen(gSwitch.action) && focus.tag == PanelTag(gSwitch.action)) {
            gLast = std::format("{}: already up", gm::Name(gSwitch.action));
            log::Info("omni-tool: {} is already up ('{}' has the focus)", gm::Name(gSwitch.action), focus.tag);
            gSwitch = {};
            return;
        }
        if (gSwitch.wait > 0) {
            --gSwitch.wait;
            return;
        }
        if (gSwitch.backOuts >= kMaxBackOuts || focus.handler == nullptr || gHandleInputEvent.Get() == nullptr) {
            gLast = std::format("{}: the game's screen didn't close", gm::Name(gSwitch.action));
            log::Warn("omni-tool: {} not done: the game stayed in its GUI mode after {} backs out (the focused panel '{}'{})",
                gm::Name(gSwitch.action), gSwitch.backOuts, focus.tag, focus.handler == nullptr ? ", without a handler" : "");
            gSwitch = {};
            return;
        }
        ue3::Params parms(ue3::runtime::Get(), gHandleInputEvent.Get());
        parms.Set("Event", kGuiEventB);
        parms.Set("fValue", 1.0f);
        ue3::runtime::Call(focus.handler, gHandleInputEvent.Get(), parms);  // the player's B on the screen in focus
        ++gSwitch.backOuts;
        gSwitch.wait = kBackOutWaitTicks;
        log::Info("omni-tool: backing out of '{}' (B, {} of at most {}) for {}", focus.tag, gSwitch.backOuts, kMaxBackOuts,
            gm::Name(gSwitch.action));
        return;
    }
    gm::Action const action = gSwitch.action;
    gSwitch = {};
    if (action == gm::Action::CloseScreens) {
        gLast = "close: done";
        log::Info("omni-tool: the game's screens are closed (game mode {})", current);
        return;
    }
    if (current != 0 && current != 1) {
        gLast = std::format("{}: not here (game mode {})", gm::Name(action), current);
        log::Info("omni-tool: {} not opened: the game's screens open on foot or in the Mako (game mode {})", gm::Name(action), current);
        return;
    }
    OpenScreen(action, *modes);
}

void Run(gm::Action action) {
    switch (action) {
    case gm::Action::PauseMenu: OpenPauseMenu(); return;
    case gm::Action::Holster: Holster(); return;
    case gm::Action::Equipment:
    case gm::Action::Squad:
    case gm::Action::Journal:
    case gm::Action::Codex:
    case gm::Action::Map:
    case gm::Action::CloseScreens:
        gSwitch = {action, 0, 0};  // StepSwitch does it, a tick at a time
        return;
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
#if defined(MEVR_GAME_LE1)
    StepSwitch();
#endif
}

void Shutdown() {
    gSchedule = {};
    gToldOldHost = false;
#if defined(MEVR_GAME_LE1)
    gSwitch = {};
#endif
}

std::string StatusJson() {
    return std::format("{{\"waiting\": \"{}\", \"last\": \"{}\"}}", gm::Name(gSchedule.action), gLast);
}

} // namespace mevr::game::game_menu_request
