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

#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2)
// Our reflection layer (research/34); engine/ReflectCheck has a row for each. The game modes, the pause menu and the
// holster on every game (ME2 since M4: research/le2-menus.md); the omni-tool's screens ME1's only so far.
ue3::FieldRef gGameModeManager{"SFXGame.BioPlayerController", engine::adapter::kGameModeManager, ue3::kEveryGame};
ue3::FieldRef gGameModes{"SFXGame.SFXGameModeManager", "GameModes", ue3::kEveryGame};
ue3::FieldRef gCurrentMode{"SFXGame.SFXGameModeManager", "CurrentMode", ue3::kEveryGame};
ue3::FieldRef gIsActive{"SFXGame.SFXGameModeBase", "bIsActive", ue3::kEveryGame};
ue3::FieldRef gPriority{"SFXGame.SFXGameModeBase", "Priority", ue3::kEveryGame};
ue3::ClassRef gModeBase{"SFXGame.SFXGameModeBase", ue3::kEveryGame};
ue3::ClassRef gModeDefault{"SFXGame.SFXGameModeDefault", ue3::kEveryGame};
ue3::ClassRef gModeVehicle{"SFXGame.SFXGameModeVehicle", ue3::kEveryGame};
ue3::ClassRef gModePowerWheel{"SFXGame.SFXGameModePowerWheel", ue3::kEveryGame};
ue3::ClassRef gModeWeaponWheel{"SFXGame.SFXGameModeWeaponWheel", ue3::kEveryGame};
ue3::ClassRef gModeCommand{"SFXGame.SFXGameModeCommand", ue3::kEveryGame};
// Start (Console_Menu) calls it on the mode that has the input; looked up on the mode object's own class for the
// overrides (SFXGameModeVehicle, -PowerWheel, -WeaponWheel, -Command, -Photo). This one is the declaration, resolved
// first so a missing function says so once. ME2's Start is SFXGameModeBase.ShowMenu (its pause menu, the browser wheel:
// MassEffectGuiManager.ShowBrowserWheel), which returns nothing.
#if defined(MEVR_GAME_LE1)
ue3::FunctionRef gShowMenuGuiBase{"SFXGame.SFXGameModeBase.ShowMenuGui"};
constexpr char const* kFunctionName = "ShowMenuGui";
#else
ue3::FunctionRef gShowMenuGuiBase{"SFXGame.SFXGameModeBase.ShowMenu", ue3::kEveryGame};
constexpr char const* kFunctionName = "ShowMenu";
#endif
ue3::FunctionRef gTryHolster{"SFXGame.SFXGameModeDefault.TryHolster", ue3::kEveryGame};
ue3::FunctionRef gIsInCombatActionState{"SFXGame.BioPawn.IsInCombatActionState"};  // ME1's (ME2's TryHolster never draws)
// The omni-tool's rail (research/57): the screens' own functions (declared on SFXGameModeBase only: no mode overrides them, so
// in game mode 7 the screens open only from the game's Flash wheel), and the player's B as the game hears it: GuiKey on the
// active mode, which hands it to the GUI manager (BioSFManager.HandleInputEvent), which offers it to every handler on the
// stack and closes the screen through MassEffectGuiManager.HidePauseMenu / HideBrowserWheel (traced 2026-11-04 23:20 with a
// replayed B). The focused panel's handler alone (BioSFHandler.HandleInputEvent(13)) never closed the equipment screen.
ue3::FunctionRef gShowEquipmentGui{"SFXGame.SFXGameModeBase.ShowEquipmentGui"};
ue3::FunctionRef gShowSquadGui{"SFXGame.SFXGameModeBase.ShowSquadGui"};
ue3::FunctionRef gShowJournalGui{"SFXGame.SFXGameModeBase.ShowJournalGui"};
ue3::FunctionRef gShowCodexGui{"SFXGame.SFXGameModeBase.ShowCodexGui"};
ue3::FunctionRef gShowMapGui{"SFXGame.SFXGameModeBase.ShowMapGui"};
ue3::FunctionRef gHandleInputEvent{"Engine.BioSFHandler.HandleInputEvent"};
ue3::FunctionRef gGuiKey{"SFXGame.SFXGameModeBase.GuiKey"};
ue3::FieldRef gScaleFormManager{"Engine.PlayerController", "m_oCachedScaleFormManager"};
ue3::FieldRef gFocusPanel{"Engine.BioSFManager", "m_FocusPanel"};
ue3::FieldRef gPanelTag{"Engine.BioSFPanel", "nmTag"};
ue3::FieldRef gPanelHandler{"Engine.BioSFPanel", "m_DefaultHandler"};
constexpr std::uint8_t kGuiEventB = 13;  // BIOGUI_EVENT_BUTTON_B (Engine.EBioGuiEvent)
constexpr int kBackOutWaitTicks = 4;     // after a B, the ticks to let the screen go before looking again
constexpr int kMaxBackOuts = 6;          // a screen over the main wheel takes two

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
    if (!ue3::runtime::Up()) {
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

void OpenPauseMenu(Link const& link, gm::Origin origin) {
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
        gLast = std::format("pause menu: SFXGameModeBase.{} not found", kFunctionName);
        log::Warn("main menu: the game's pause menu not opened: SFXGame.SFXGameModeBase.{} not found", kFunctionName);
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Function* const show = r.FindFunction(r.ClassOf(mode), kFunctionName);
    if (show == nullptr) {
        gLast = std::format("pause menu: {} has no {}", ClassName(mode), kFunctionName);
        log::Warn("main menu: the game's pause menu not opened: {} has no {}", ClassName(mode), kFunctionName);
        return;
    }
    ue3::Params parms(r, show);
    std::uint32_t const request = world_ui::BeginOpening(link.worldUi, gm::Action::PauseMenu, origin);
    bool const called = ue3::runtime::Call(mode, show, parms);  // game thread: permission precedes the mode change inside the call
#if defined(MEVR_GAME_LE1)
    bool const opened = called && parms.ReturnBool().value_or(false);
#else
    bool const opened = called;  // ME2's ShowMenu returns nothing (it shows nothing where bAllowPauseMenu is off)
#endif
    if (!opened && request != 0) world_ui::CancelOpening(link.worldUi, request);
    std::string const declared = r.NameText(r.OuterOf(show));
    std::string const where = std::format("{}.{}{}", ClassName(mode), kFunctionName,
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
#if defined(MEVR_GAME_LE1)
        } else if (gTryHolster.Get() == nullptr || gIsInCombatActionState.Get() == nullptr) {
            why = "SFXGameModeDefault.TryHolster or BioPawn.IsInCombatActionState not found";
#else
        } else if (gTryHolster.Get() == nullptr) {
            why = "SFXGameModeDefault.TryHolster not found";
#endif
        } else if (mode == nullptr) {
            why = "no SFXGameModeDefault in the player controller's game modes";
        }
    }
    if (!why.empty()) {
        gLast = std::format("holster: not done ({})", why);
        log::Info("main menu: holster not done: {}", why);
        return;
    }
#if defined(MEVR_GAME_LE1)
    ue3::Params drawn(ue3::runtime::Get(), gIsInCombatActionState.Get());
    ue3::runtime::Call(pawn, gIsInCombatActionState.Get(), drawn);
    if (!drawn.ReturnBool().value_or(false)) {
        // TryHolster is the holster key's toggle: with the weapon away it would draw it.
        gLast = "holster: nothing drawn";
        log::Info("main menu: holster: no weapon drawn, nothing to put away (TryHolster not called: it would draw)");
        return;
    }
#endif
    // ME2's TryHolster only ever puts the weapon away (SetActionState(0)), and refuses in combat, mid-reload, while a power
    // or an animated transition runs: no toggle to guard against.
    ue3::Params parms(ue3::runtime::Get(), gTryHolster.Get());
    ue3::runtime::Call(mode, gTryHolster.Get(), parms);  // directly: never a generated wrapper
    gLast = "holster: TryHolster called";
    log::Info("main menu: holstered the weapon (SFXGameModeDefault.TryHolster)");
}

// ---- The omni-tool's rail (research/57) ----
// A screen is opened the way its key opens it, on the mode that has the input (on foot or the Mako); with a screen already up
// (game mode 7) the rail backs out of it first, B on the focused panel as the player would, a few ticks apart, until the game
// leaves mode 7, then opens the new one. CloseScreens is the backing out alone.
constexpr std::uint8_t kModeGui = 7;

struct Switch {
    gm::Action action = gm::Action::None;  // what to do once no screen is up (CloseScreens: nothing more)
    int backOuts = 0;
    int wait = 0;
    gm::Origin origin = gm::Origin::Menu;
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

void OpenScreen(gm::Action action, gm::Origin origin, Modes const& modes, Link const& link) {
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
    if (!parms.SetBool("bShow", true)) {
        gLast = std::format("{}: bShow parameter missing", gm::Name(action));
        log::Warn("omni-tool: {} not opened: bShow parameter missing", gm::Name(action));
        return;
    }
    std::uint32_t const request = world_ui::BeginOpening(link.worldUi, action, origin);
    if (!ue3::runtime::Call(mode, show, parms)) {
        if (request != 0) world_ui::CancelOpening(link.worldUi, request);
        gLast = std::format("{}: opening call failed", gm::Name(action));
        log::Warn("omni-tool: {} opening call failed", gm::Name(action));
        return;
    }
    gLast = std::format("{}: {}.{}(true) called", gm::Name(action), ClassName(mode), gm::ShowFunction(action));
    log::Info("omni-tool: opened {} ({}.{}(true); {})", gm::Name(action), ClassName(mode), gm::ShowFunction(action), how);
}

void StepSwitch(Link const& link) {
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
        // The player's B as the game hears it: GuiKey on the active mode (the GUI mode's), looked up on its class.
        std::string how;
        ue3::Object* const mode = ActiveMode(*modes, how);
        ue3::Reflection& r = ue3::runtime::Get();
        ue3::Function* const guiKey = mode != nullptr && gGuiKey.Get() != nullptr ? r.FindFunction(r.ClassOf(mode), "GuiKey") : nullptr;
        if (gSwitch.backOuts >= kMaxBackOuts || (guiKey == nullptr && (focus.handler == nullptr || gHandleInputEvent.Get() == nullptr))) {
            gLast = std::format("{}: the game's screen didn't close", gm::Name(gSwitch.action));
            log::Warn("omni-tool: {} not done: the game stayed in its GUI mode after {} backs out (the focused panel '{}'{})",
                gm::Name(gSwitch.action), gSwitch.backOuts, focus.tag,
                guiKey == nullptr ? ", no GuiKey on the active mode and no handler" : "");
            gSwitch = {};
            return;
        }
        if (guiKey != nullptr) {
            ue3::Params parms(r, guiKey);
            parms.Set("Event", kGuiEventB);
            ue3::runtime::Call(mode, guiKey, parms);  // the player's B: the GUI manager offers it to the whole stack
        } else {
            ue3::Params parms(r, gHandleInputEvent.Get());
            parms.Set("Event", kGuiEventB);
            parms.Set("fValue", 1.0f);
            ue3::runtime::Call(focus.handler, gHandleInputEvent.Get(), parms);  // older route: the focused screen's handler only
        }
        ++gSwitch.backOuts;
        gSwitch.wait = kBackOutWaitTicks;
        log::Info("omni-tool: backing out of '{}' (B through {}, {} of at most {}) for {}", focus.tag,
            guiKey != nullptr ? std::format("{}.GuiKey", ClassName(mode)) : std::string("the focused handler"), gSwitch.backOuts,
            kMaxBackOuts, gm::Name(gSwitch.action));
        return;
    }
    gm::Action const action = gSwitch.action;
    gm::Origin const origin = gSwitch.origin;
    gSwitch = {};
    if (action == gm::Action::CloseScreens) {
        gLast = "close: done";
        log::Info("omni-tool: the game's screens are closed (game mode {})", current);
        return;
    }
    if (action == gm::Action::PauseMenu) {
        OpenPauseMenu(link, origin);  // on the active mode, whichever it is (the Mako's and the wheels' overrides run)
        return;
    }
    if (current != 0 && current != 1) {
        gLast = std::format("{}: not here (game mode {})", gm::Name(action), current);
        log::Info("omni-tool: {} not opened: the game's screens open on foot or in the Mako (game mode {})", gm::Name(action), current);
        return;
    }
    OpenScreen(action, origin, *modes, link);
}

#if defined(MEVR_GAME_LE2)
// ---- ME2's screens (research/le2-menus.md "The omni-tool's entries on ME2") ----
// ME2 has no Show<Screen>Gui: its squad, journal, codex and map screens are pages of its pause menu, the browser wheel
// ("Mission Computer": SFXGameModeBase.ShowMenu -> MassEffectGuiManager.ShowBrowserWheel). The page is opened the way the
// wheel's own buttons open it: BioSFHandler_BrowserWheel.ShowSelectedPanel(index into its lstPages). So: the pause menu
// first (on foot or in a vehicle), then, once the game is in its GUI mode (7), the page whose Type is the screen's
// (MEBrowserWheelSubPages: MBW_SP_Map 0, MBW_SP_CharacterRecord 2 (the squad), MBW_SP_Journal 4; the codex is found by its
// tag). ME2 has no equipment screen outside its lockers: the entry stands down.
ue3::FieldRef gGuiManager{"Engine.PlayerController", "m_oCachedScaleFormManager", ue3::kEveryGame};
ue3::FieldRef gBrowserWheel{"SFXGame.MassEffectGuiManager", "m_oBWHandler", ue3::kEveryGame};
ue3::FieldRef gPages{"SFXGame.BioSFHandler_BrowserWheel", "lstPages", ue3::kEveryGame};
ue3::FieldRef gPageTag{gPages, "Tag"};
ue3::FieldRef gPageType{gPages, "Type"};
ue3::FunctionRef gShowSelectedPanel{"SFXGame.BioSFHandler_BrowserWheel.ShowSelectedPanel", ue3::kEveryGame};
ue3::FunctionRef gGetBrowserHandler{"SFXGame.MassEffectGuiManager.GetBrowserHandler", ue3::kEveryGame};
constexpr int kPageWaitTicks = 4;
constexpr int kPageMaxTries = 30;  // ticks with the pause menu asked for and not up yet

struct Page {
    gm::Action action = gm::Action::None;
    gm::Origin origin = gm::Origin::Menu;
    bool asked = false;  // the pause menu was asked for
    int wait = 0;
    int tries = 0;
};
Page gPage;
bool gLoggedPages = false;

// The page for a screen: by its Type, or (the codex, which the sub-page enum doesn't name) by its tag.
int FindPage(gm::Action action, ue3::Elements const& pages) {
    int const type = action == gm::Action::Squad ? 2 : action == gm::Action::Journal ? 4 : action == gm::Action::Map ? 0 : -1;
    for (int i = 0; i < pages.count; ++i) {
        void* const page = pages.At(i);
        if (type >= 0 && gPageType.Read<std::uint8_t>(page).value_or(0xFF) == type) {
            return i;
        }
        if (action == gm::Action::Codex) {
            if (auto const tag = gPageTag.Read<ue3::Name>(page)) {
                std::string const text = ue3::runtime::Get().ToString(*tag);
                if (text.find("Codex") != std::string::npos || text.find("codex") != std::string::npos) {
                    return i;
                }
            }
        }
    }
    return -1;
}

void StepPage(Link const& link) {
    if (gPage.action == gm::Action::None) {
        return;
    }
    if (gPage.wait > 0) {
        --gPage.wait;
        return;
    }
    gm::Action const action = gPage.action;
    std::string why;
    std::optional<Modes> const modes = ReadModes(why);
    if (!modes || !modes->current) {
        gLast = std::format("{}: not done ({})", gm::Name(action), modes ? "no game mode" : why);
        log::Warn("omni-tool: {} not done: {}", gm::Name(action), modes ? "the game mode isn't known" : why);
        gPage = {};
        return;
    }
    std::uint8_t const current = *modes->current;
    if (current != kModeGui) {
        if (gPage.asked) {
            if (++gPage.tries > kPageMaxTries) {
                gLast = std::format("{}: the pause menu didn't come up", gm::Name(action));
                log::Warn("omni-tool: {} not opened: ME2's pause menu didn't come up (game mode {})", gm::Name(action), current);
                gPage = {};
            }
            return;
        }
        if (current != 0 && current != 1) {
            gLast = std::format("{}: not here (game mode {})", gm::Name(action), current);
            log::Info("omni-tool: {} not opened: the game's screens open on foot or in a vehicle (game mode {})", gm::Name(action), current);
            gPage = {};
            return;
        }
        OpenPauseMenu(link, gPage.origin);  // ME2's Mission Computer: its pages are the screens
        gPage.asked = true;
        gPage.wait = kPageWaitTicks;
        return;
    }
    ue3::Object* const pc = player::Controller();
    ue3::Object* const manager = pc != nullptr ? gGuiManager.Read<ue3::Object*>(pc).value_or(nullptr) : nullptr;
    ue3::Object* wheel = manager != nullptr ? gBrowserWheel.Read<ue3::Object*>(manager).value_or(nullptr) : nullptr;
    if (wheel == nullptr && manager != nullptr && gGetBrowserHandler.Get() != nullptr) {
        // The PC's pause menu leaves m_oBWHandler empty (seen 2026-11-14): the manager's own native finds the handler of
        // its 'MenuBrowser' panel.
        ue3::Params parms(ue3::runtime::Get(), gGetBrowserHandler.Get());
        if (ue3::runtime::Call(manager, gGetBrowserHandler.Get(), parms)) {
            wheel = parms.Return<ue3::Object*>().value_or(nullptr);
        }
    }
    std::optional<ue3::Elements> const pages = wheel != nullptr && ue3::runtime::Get().IsAlive(wheel) ? gPages.ElementsOf(wheel)
                                                                                                    : std::nullopt;
    if (!pages || gShowSelectedPanel.Get() == nullptr) {
        if (++gPage.tries > kPageMaxTries) {
            gLast = std::format("{}: no browser wheel", gm::Name(action));
            log::Warn("omni-tool: {} not opened: ME2's pause menu has no browser wheel handler with pages (GUI manager {}, "
                      "handler {}, pages {}{})", gm::Name(action), manager != nullptr ? "found" : "none",
                wheel != nullptr ? ClassName(wheel) : std::string("none"), pages ? std::to_string(pages->count) : std::string("none"),
                gShowSelectedPanel.Get() == nullptr ? "; BioSFHandler_BrowserWheel.ShowSelectedPanel not found" : "");
            gPage = {};
        }
        return;
    }
    if (!gLoggedPages) {
        gLoggedPages = true;
        std::string list;
        for (int i = 0; i < pages->count; ++i) {
            auto const tag = gPageTag.Read<ue3::Name>(pages->At(i));
            list += std::format("{}{}: {} (type {})", i == 0 ? "" : ", ", i, tag ? ue3::runtime::Get().ToString(*tag) : std::string("?"),
                gPageType.Read<std::uint8_t>(pages->At(i)).value_or(0xFF));
        }
        log::Info("omni-tool: ME2's pause menu pages: {}", list);
    }
    int const index = FindPage(action, *pages);
    if (index < 0) {
        gLast = std::format("{}: no page for it", gm::Name(action));
        log::Warn("omni-tool: {} not opened: ME2's pause menu has no page for it", gm::Name(action));
        gPage = {};
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Params parms(r, gShowSelectedPanel.Get());
    parms.Set("nPanel", static_cast<std::int32_t>(index));
    parms.Set("oManager", manager);
    bool const called = ue3::runtime::Call(wheel, gShowSelectedPanel.Get(), parms);
    gLast = std::format("{}: page {} {}", gm::Name(action), index, called ? "shown" : "call failed");
    log::Info("omni-tool: opened {} (ME2's pause menu page {} through BioSFHandler_BrowserWheel.ShowSelectedPanel{})",
        gm::Name(action), index, called ? "" : ": the call failed");
    gPage = {};
}
#endif

void Run(gm::Action action, gm::Origin origin) {
    switch (action) {
    case gm::Action::Holster: Holster(); return;
#if defined(MEVR_GAME_LE2)
    case gm::Action::Equipment:
        gLast = "equipment: not on ME2";
        log::Info("omni-tool: Equipment isn't a screen on Mass Effect 2 (its weapons and armour change at its lockers)");
        return;
    case gm::Action::Squad:
    case gm::Action::Journal:
    case gm::Action::Codex:
    case gm::Action::Map:
        gPage = {action, origin, false, 0, 0};  // StepPage does it, a tick at a time
        return;
    case gm::Action::PauseMenu:
    case gm::Action::CloseScreens:
        gSwitch = {action, 0, 0, origin};
        return;
#else
    case gm::Action::PauseMenu:  // the game opens none over its own screens (ShowMenuGui returns 0 in mode 7): back out first
    case gm::Action::Equipment:
    case gm::Action::Squad:
    case gm::Action::Journal:
    case gm::Action::Codex:
    case gm::Action::Map:
    case gm::Action::CloseScreens:
        gSwitch = {action, 0, 0, origin};  // StepSwitch does it, a tick at a time
        return;
#endif
    case gm::Action::None: return;
    }
}
#else
void Run(gm::Action, gm::Origin) {
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
    std::uint32_t const value = link.take();
    gm::Action const taken = gm::DecodeAction(value);
    if (taken != gm::Action::None) {
        log::Info("main menu request: {} taken; it runs once the main menu is closed and the game runs ({} ticks)",
            gm::Name(taken), gm::kSettleTicks);
        world_ui::CancelOpening(link.worldUi);
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2)
        gSwitch = {};  // a replacement must not finish the older backout/opening path
#endif
#if defined(MEVR_GAME_LE2)
        gPage = {};
#endif
        gm::Take(gSchedule, taken, gm::DecodeOrigin(value));
    }
    gm::Step const step = gm::Advance(gSchedule, link.menuOpen, menu_pause::Paused());
    if (step.dropped != gm::Action::None) {
        gLast = std::format("{}: dropped", gm::Name(step.dropped));
        log::Warn("main menu request: {} dropped: {}", gm::Name(step.dropped),
            step.reopened ? "the main menu opened again first" : "the game stayed paused by the main menu");
    }
    if (step.run != gm::Action::None) {
        Run(step.run, step.origin);
    }
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2)
    StepSwitch(link);
#endif
#if defined(MEVR_GAME_LE2)
    StepPage(link);
#endif
}

std::string Inject(std::string const& which) {
    struct Name {
        char const* word;
        gm::Action action;
    };
    static constexpr Name kNames[] = {{"equipment", gm::Action::Equipment}, {"squad", gm::Action::Squad}, {"map", gm::Action::Map},
        {"journal", gm::Action::Journal}, {"codex", gm::Action::Codex}, {"computer", gm::Action::PauseMenu},
        {"menu", gm::Action::PauseMenu}, {"close", gm::Action::CloseScreens}, {"holster", gm::Action::Holster}};
    for (Name const& name : kNames) {
        if (which == name.word) {
            log::Info("main menu request: {} asked from the console (mevr omni {})", gm::Name(name.action), which);
            gm::Take(gSchedule, name.action, gm::Origin::OmniRail);
            return std::format("omni: {} taken; it runs in {} ticks once nothing of ours pauses the game (log `main menu request:`, "
                               "`omni-tool:`)", gm::Name(name.action), gm::kSettleTicks);
        }
    }
    return "usage: mevr omni <equipment|squad|map|journal|codex|computer|close|holster>";
}

bool GuiKey(std::uint8_t event, std::string& why) {
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2)
    std::optional<Modes> const modes = ReadModes(why);
    if (!modes) {
        return false;
    }
    std::string how;
    ue3::Object* const mode = ActiveMode(*modes, how);
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Function* const guiKey = mode != nullptr && gGuiKey.Get() != nullptr ? r.FindFunction(r.ClassOf(mode), "GuiKey") : nullptr;
    if (guiKey == nullptr) {
        why = mode == nullptr ? "no active game mode" : std::format("no GuiKey on {}", ClassName(mode));
        return false;
    }
    ue3::Params parms(r, guiKey);
    parms.Set("Event", event);
    if (!ue3::runtime::Call(mode, guiKey, parms)) {
        why = "the call failed";
        return false;
    }
    why = std::format("{}.GuiKey({})", ClassName(mode), event);
    return true;
#else
    (void)event;
    why = "LE1 only";
    return false;
#endif
}

void Shutdown() {
    gSchedule = {};
    gToldOldHost = false;
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2)
    gSwitch = {};
#endif
#if defined(MEVR_GAME_LE2)
    gPage = {};
    gLoggedPages = false;
#endif
}

std::string StatusJson() {
    return std::format("{{\"waiting\": \"{}\", \"last\": \"{}\"}}", gm::Name(gSchedule.action), gLast);
}

} // namespace mevr::game::game_menu_request
