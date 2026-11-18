// SPDX-License-Identifier: MIT
#include "game/GameMenuRequest.hpp"

#include "core/Log.hpp"
#include "core/vr/GameMenuRequest.hpp"
#include "engine/GameAdapter.hpp"
#include "game/MenuPause.hpp"
#include "game/Player.hpp"
#if defined(MEVR_GAME_LE3)
#include "game/Le3Gui.hpp"
#endif
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <chrono>
#include <format>
#include <unordered_set>
#include <vector>

namespace mevr::game::game_menu_request {
namespace {

namespace gm = vr::game_menu;

gm::Schedule gSchedule;
bool gToldOldHost = false;
std::string gLast = "none";

#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
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
// The back-out (CloseScreens, a screen switch) is the same on ME2 (its GuiKey hands the event to the GUI manager the same way,
// SFXGameModeBase.GuiKey -> GetScaleFormManager().HandleInputEvent; its BioGuiEvents enum is ME1's, B = 13): every game.
ue3::FunctionRef gHandleInputEvent{"Engine.BioSFHandler.HandleInputEvent", ue3::kEveryGame};
ue3::FunctionRef gGuiKey{"SFXGame.SFXGameModeBase.GuiKey", ue3::kEveryGame};
ue3::FieldRef gScaleFormManager{"Engine.PlayerController", "m_oCachedScaleFormManager", ue3::kEveryGame};
ue3::FieldRef gFocusPanel{"Engine.BioSFManager", "m_FocusPanel", ue3::kEveryGame};
ue3::FieldRef gPanelTag{"Engine.BioSFPanel", "nmTag", ue3::kEveryGame};
ue3::FieldRef gPanelHandler{"Engine.BioSFPanel", "m_DefaultHandler", ue3::kEveryGame};
#if defined(MEVR_GAME_LE3)
constexpr std::uint8_t kGuiEventB = 16;
// ME3's screens act on B's release (BIOGUI_EVENT_BUTTON_B_RELEASE): B alone left the squad record up, B then its release
// went back to the Mission Computer, and again closed it (measured 2026-11-16, Save 31). Every back-out sends both.
constexpr std::uint8_t kGuiEventBRelease = 37;
#else
constexpr std::uint8_t kGuiEventB = 13;  // BIOGUI_EVENT_BUTTON_B (Engine.EBioGuiEvent; ME2's BioSFSharedBase.BioGuiEvents too)
#endif
#if defined(MEVR_GAME_LE3)
// ME3 backs out by waiting for each screen to go (vr::game_menu::StepBackOut) instead of a B every few ticks: its area map
// closes by an animation its B release starts and another B starts again, so a B every 12 ticks held it up through 8
// back-outs (lane CTL, 2026-11-17; "six back-outs 4 ticks apart left it up" on 2026-11-16 was the same). The times are the
// measured ones (research/le3-native-gui.md, lane OMX).
constexpr vr::game_menu::BackOutRules kBackOutRules{};
// The game out of its GUI mode this long after a B before the screens count as closed (a screen's way back to the
// Mission Computer must not pass for the end).
constexpr std::uint64_t kClosedSettleMs = 200;
#else
constexpr int kBackOutWaitTicks = 4;     // after a B, the ticks to let the screen go before looking again
constexpr int kMaxBackOuts = 6;          // a screen over the main wheel takes two
#endif

// EGameModes (research/13): the modes whose object is known by class. Others (conversation, GUI...) go by bIsActive.
ue3::ClassRef* ClassForMode(std::uint8_t mode) {
    using engine::adapter::Mode;
    switch (engine::adapter::GameMode(mode)) {
    case Mode::OnFoot: return &gModeDefault;
    case Mode::Vehicle: return &gModeVehicle;
    case Mode::PowerWheel: return &gModePowerWheel;
    case Mode::WeaponWheel: return &gModeWeaponWheel;
    case Mode::Command: return &gModeCommand;
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

#if !defined(MEVR_GAME_LE3)
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
#endif

// ---- The omni-tool's rail (research/57) ----
// A screen is opened the way its key opens it, on the mode that has the input (on foot or the Mako); with a screen already up
// (game mode 7) the rail backs out of it first, B on the focused panel as the player would, a few ticks apart, until the game
// leaves mode 7, then opens the new one. CloseScreens is the backing out alone.
#if defined(MEVR_GAME_LE3)
constexpr std::uint8_t kModeGui = 9;
#else
constexpr std::uint8_t kModeGui = 7;
#endif

struct Switch {
    gm::Action action = gm::Action::None;  // what to do once no screen is up (CloseScreens: nothing more)
    int backOuts = 0;
    int wait = 0;
    gm::Origin origin = gm::Origin::Menu;
#if defined(MEVR_GAME_LE3)
    gm::BackOut backOut;          // each screen's B once, the next when another has the focus
    std::uint64_t outOfGuiMs = 0;  // since when the game is out of its GUI mode after a B (0: in it)
#endif
};
Switch gSwitch;

#if defined(MEVR_GAME_LE3)
std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// The player's B as ME3's screens take it: B, then its release (they act on the release, and the game passes a release on only
// after its press: `mevr guikey 37` alone left the area map up, 2026-11-17).
bool GuiKeyB(std::string& why) {
    if (!GuiKey(kGuiEventB, why)) {
        return false;
    }
    std::string const press = why;
    if (!GuiKey(kGuiEventBRelease, why)) {
        return false;
    }
    why = std::format("{} then {}", press, why);
    return true;
}

// One look at the focused screen while backing out on ME3: sends B (and its release) when vr::game_menu::StepBackOut says so,
// logs a screen giving way (the time from its B: the measure of its close), and says what happened.
gm::BackOutStep BackOutLook(gm::BackOut& state, ue3::Object* panel, std::string const& tag, char const* forWhat) {
    std::uint64_t const now = NowMs();
    gm::BackOutScreen const screen{reinterpret_cast<std::uintptr_t>(panel), tag};
    std::uint64_t const sinceLast = state.sends > 0 ? now - state.sentAtMs : 0;
    if (state.sends > 0 && state.started && !(screen == state.seen) && state.seen == state.sentTo) {
        log::Info("omni-tool: '{}' gave way to '{}' {} ms after its B", state.seen.tag, screen.object != 0 ? tag : std::string("no screen"),
            sinceLast);
    }
    gm::BackOutStep const step = gm::StepBackOut(state, screen, now, kBackOutRules);
    if (step == gm::BackOutStep::GiveUp) {
        log::Warn("omni-tool: {} not done: the game stayed in its GUI mode ('{}' in focus {} ms, {} B{} to it, {} in all)", forWhat,
            screen.object != 0 ? tag : std::string("no screen"), now - state.seenSinceMs, screen == state.sentTo ? state.sentToSame : 0,
            screen == state.sentTo && state.sentToSame == 1 ? "" : "s", state.sends);
        return step;
    }
    if (step == gm::BackOutStep::Send) {
        std::string why;
        if (!GuiKeyB(why)) {
            log::Warn("omni-tool: {} not done: B couldn't be sent ({})", forWhat, why);
            return gm::BackOutStep::GiveUp;
        }
        log::Info("omni-tool: backing out of '{}' (B and its release through {}; {}, {} of at most {}) for {}", tag, why,
            state.sentToSame == 1 ? std::string("its first") : std::format("again: still up {} ms after the last", sinceLast),
            state.sends, kBackOutRules.maxSends, forWhat);
    }
    return step;
}
#endif

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
    #if defined(MEVR_GAME_LE3)
    case gm::Action::Squad: return "SquadRecord";
#else
    case gm::Action::Squad: return "CharacterRecord";
#endif
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
#if defined(MEVR_GAME_LE3)
    focus.panel = le3_gui::FocusMovie();
    focus.handler = focus.panel;
    focus.tag = le3_gui::Tag(focus.panel);
#else
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
#endif
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
#if defined(MEVR_GAME_LE3)
    if (current == kModeGui) {
        gSwitch.outOfGuiMs = 0;
        Focus const focus = FocusedPanel();
        // ME3 has no Show<Screen>Gui here (its screens are the Mission Computer's pages: StepPage); only the pause menu and
        // Close come this way. Each screen gets its B once; the next B waits for the screen that takes its place.
        if (gSwitch.action == gm::Action::PauseMenu && focus.tag == "MenuBrowser") {
            gLast = "pause menu: already up";
            log::Info("omni-tool: the Mission Computer is up ('MenuBrowser' has the focus)");
            gSwitch = {};
            return;
        }
        switch (BackOutLook(gSwitch.backOut, focus.panel, focus.tag, gm::Name(gSwitch.action))) {
        case gm::BackOutStep::Wait: return;
        case gm::BackOutStep::Send: gLast = std::format("{}: backing out of '{}'", gm::Name(gSwitch.action), focus.tag); return;
        case gm::BackOutStep::GiveUp:
            gLast = std::format("{}: the game's screen didn't close", gm::Name(gSwitch.action));
            gSwitch = {};
            return;
        }
        return;
    }
    if (gSwitch.backOut.sends > 0) {
        // Out of the GUI mode: settled only after kClosedSettleMs (a screen's way back to the Mission Computer must not count).
        std::uint64_t const now = NowMs();
        if (gSwitch.outOfGuiMs == 0) {
            gSwitch.outOfGuiMs = now;
            log::Info("omni-tool: the game left its GUI mode {} ms after the last B (game mode {})", now - gSwitch.backOut.sentAtMs, current);
        }
        if (now - gSwitch.outOfGuiMs < kClosedSettleMs) {
            return;
        }
    }
#else
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
#endif
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
    if (engine::adapter::GameMode(current) != engine::adapter::Mode::OnFoot &&
            engine::adapter::GameMode(current) != engine::adapter::Mode::Vehicle) {
        gLast = std::format("{}: not here (game mode {})", gm::Name(action), current);
        log::Info("omni-tool: {} not opened: the game's screens open on foot or in the Mako (game mode {})", gm::Name(action), current);
        return;
    }
    OpenScreen(action, origin, *modes, link);
}

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
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
    int backOuts = 0;
#if defined(MEVR_GAME_LE3)
    gm::BackOut backOut;             // back to the Mission Computer from another page, each screen's B once
    std::uint64_t browserSinceMs = 0;  // the Mission Computer in focus since, after a B
#endif
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
                log::Warn("omni-tool: {} not opened: the native pause menu didn't come up (game mode {})", gm::Name(action), current);
                gPage = {};
            }
            return;
        }
        if (engine::adapter::GameMode(current) != engine::adapter::Mode::OnFoot &&
            engine::adapter::GameMode(current) != engine::adapter::Mode::Vehicle) {
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
    Focus const focus = FocusedPanel();
    // ME2's map page is 'AreaMap', ME3's 'Areamap'.
    if (focus.tag == PanelTag(action) || (action == gm::Action::Map && (focus.tag == "AreaMap" || focus.tag == "Areamap"))) {
        gLast = std::format("{}: already up", gm::Name(action));
        gPage = {};
        return;
    }
#if defined(MEVR_GAME_LE3)
    if (focus.tag != "MenuBrowser") {
        // Opening from a different child page stacks two movies and leaves the old one accepting clicks: back out to the
        // Mission Computer first, each screen's B once (the map's B starts its close, a second starts it again).
        if (gPage.asked || focus.tag.empty()) {
            // The pause menu asked for, or no screen in focus yet: wait for the browser.
            if (++gPage.tries > kPageMaxTries) {
                gLast = std::format("{}: the browser did not take focus", gm::Name(action));
                log::Warn("omni-tool: {} not opened: the focused page is '{}'", gm::Name(action), focus.tag);
                gPage = {};
            }
            return;
        }
        std::string const forWhat = std::format("returning to the Mission Computer before opening {}", gm::Name(action));
        if (BackOutLook(gPage.backOut, focus.panel, focus.tag, forWhat.c_str()) == gm::BackOutStep::GiveUp) {
            gLast = std::format("{}: the browser did not take focus", gm::Name(action));
            gPage = {};
        }
        return;
    }
    if (gPage.backOut.sends > 0) {
        // Back at the Mission Computer after a B: let its new movie come up before asking it for a page.
        std::uint64_t const now = NowMs();
        if (gPage.browserSinceMs == 0) {
            gPage.browserSinceMs = now;
            log::Info("omni-tool: back at the Mission Computer {} ms after the last B", now - gPage.backOut.sentAtMs);
        }
        if (now - gPage.browserSinceMs < kBackOutRules.settleMs) {
            return;
        }
    }
#else
    if (focus.tag != "MenuBrowser") {
        // Opening from a different child page stacks two movies and leaves the old one accepting clicks.
        // Let the game return to its browser before asking the browser to open the next page.
        if (++gPage.tries > kPageMaxTries || gPage.backOuts >= kMaxBackOuts) {
            gLast = std::format("{}: the browser did not take focus", gm::Name(action));
            log::Warn("omni-tool: {} not opened: the focused page is '{}'", gm::Name(action), focus.tag);
            gPage = {};
        } else if (!gPage.asked && !focus.tag.empty() && GuiKey(kGuiEventB, why)) {
            ++gPage.backOuts;
            gPage.wait = kBackOutWaitTicks;
            log::Info("omni-tool: returning from '{}' to the browser before opening {}", focus.tag, gm::Name(action));
        }
        return;
    }
#endif
#if defined(MEVR_GAME_LE3)
    ue3::Object* const manager = le3_gui::Manager();
    ue3::Object* const wheel = le3_gui::Browser(manager);
#else
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
#endif
    std::optional<ue3::Elements> const pages = wheel != nullptr && ue3::runtime::Get().IsAlive(wheel) ? gPages.ElementsOf(wheel)
                                                                                                    : std::nullopt;
    if (!pages || gShowSelectedPanel.Get() == nullptr) {
        if (++gPage.tries > kPageMaxTries) {
            gLast = std::format("{}: no browser wheel", gm::Name(action));
            log::Warn("omni-tool: {} not opened: the native pause menu has no browser wheel handler with pages (GUI manager {}, "
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
        log::Info("omni-tool: the native pause menu pages: {}", list);
    }
    int const index = FindPage(action, *pages);
    if (index < 0) {
        gLast = std::format("{}: no page for it", gm::Name(action));
        log::Warn("omni-tool: {} not opened: the native pause menu has no page for it", gm::Name(action));
        gPage = {};
        return;
    }
#if defined(MEVR_GAME_LE3)
    auto const type = gPageType.Read<std::uint8_t>(pages->At(index)).value_or(255);
    if (!le3_gui::PageAllowed(wheel, type)) {
        gLast = std::format("{}: page disabled by the game or its state unavailable", gm::Name(action));
        log::Info("omni-tool: {}", gLast);
        gPage = {};
        return;
    }
#endif
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Params parms(r, gShowSelectedPanel.Get());
    bool const called = parms.Set("nPanel", static_cast<std::int32_t>(index)) && parms.Set("oManager", manager) &&
        ue3::runtime::Call(wheel, gShowSelectedPanel.Get(), parms);
    gLast = std::format("{}: page {} {}", gm::Name(action), index, called ? "shown" : "call failed");
    log::Info("omni-tool: opened {} (the native pause menu page {} through BioSFHandler_BrowserWheel.ShowSelectedPanel{})",
        gm::Name(action), index, called ? "" : ": the call failed");
    gPage = {};
}
#endif

// ---- Leave (ME2 and ME3; the owner's answer to question 16, 2026-11-17; research/28 "Leave") ----
// SFXGameModeBase.LeaveWorld (ME2's and ME3's B tap on foot, Console_Melee) asks the game whether it is in combat and, if not,
// activates every SFXSeqEvt_LeaveWorld event of the game's sequence (WorldInfo.GetGameSequence, FindSeqObjectsByClass
// recursively). The menu offers Leave only where that would act (vr::game_menu::LeaveOffer): the same walk over the script,
// each event as CheckActivate would take it (enabled, not used up) and with something behind it that acts (not a closed gate).
// ME1 has no on-foot leave (its LeaveWorld is the Mako's, X and the cockpit's extraction lever): nothing here runs there.
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
ue3::ClassRef gLeaveEventClass{"SFXGame.SFXSeqEvt_LeaveWorld", ue3::kEveryGame};
ue3::ClassRef gSequenceClass{"Engine.Sequence", ue3::kEveryGame};
ue3::ClassRef gSequenceReferenceClass{"Engine.SequenceReference", ue3::kEveryGame};  // a Sequence in these games
ue3::ClassRef gLogClass{"Engine.SeqAct_Log", ue3::kEveryGame};
ue3::ClassRef gGateClass{"Engine.SeqAct_Gate", ue3::kEveryGame};
ue3::FieldRef gSequenceObjects{"Engine.Sequence", "SequenceObjects", ue3::kEveryGame};
ue3::FieldRef gNestedSequences{"Engine.Sequence", "NestedSequences", ue3::kEveryGame};  // the streamed levels' scripts
ue3::FieldRef gReferencedSequence{"Engine.SequenceReference", "oSequenceReference", ue3::kEveryGame};  // a prefab's script
ue3::FieldRef gOutputLinks{"Engine.SequenceOp", "OutputLinks", ue3::kEveryGame};
ue3::FieldRef gOutputLinkLinks{gOutputLinks, "Links"};
ue3::FieldRef gLinkedOp{gOutputLinkLinks, "LinkedOp"};
ue3::FieldRef gLinkInput{gOutputLinkLinks, "InputLinkIdx"};
ue3::FieldRef gGateOpen{"Engine.SeqAct_Gate", "bOpen", ue3::kEveryGame};
ue3::FieldRef gEventEnabled{"Engine.SequenceEvent", "bEnabled", ue3::kEveryGame};
ue3::FieldRef gTriggerCount{"Engine.SequenceEvent", "TriggerCount", ue3::kEveryGame};
ue3::FieldRef gMaxTriggerCount{"Engine.SequenceEvent", "MaxTriggerCount", ue3::kEveryGame};
ue3::FieldRef gActorWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FunctionRef gGetGameSequence{"Engine.WorldInfo.GetGameSequence", ue3::kEveryGame};
ue3::FunctionRef gLeaveWorld{"SFXGame.SFXGameModeBase.LeaveWorld", ue3::kEveryGame};
#if defined(MEVR_GAME_LE2)
// ME2's LeaveWorld asks SFXGame(WorldInfo.Game).InCombat() (bInCombat or bForceCombat).
ue3::FieldRef gCombatHolder{"Engine.WorldInfo", "Game", ue3::kEveryGame};
ue3::FunctionRef gInCombat{"SFXGame.SFXGame.InCombat", ue3::kEveryGame};
constexpr char const* kCombatWhere = "SFXGame.InCombat";
#else
// ME3's asks SFXGRI(WorldInfo.GRI).InCombat() (bInCombat, bForceCombat or bAlwaysInCombat).
ue3::FieldRef gCombatHolder{"Engine.WorldInfo", "GRI", ue3::kEveryGame};
ue3::FunctionRef gInCombat{"SFXGame.SFXGRI.InCombat", ue3::kEveryGame};
constexpr char const* kCombatWhere = "SFXGRI.InCombat";
#endif
constexpr int kOfferEveryTicks = 8;          // the facts read every 8 game ticks (the word itself goes every tick)
constexpr std::uint64_t kRescanMs = 5000;    // the script walked again this often whatever its signature says
constexpr int kMaxVisited = 200000;          // sequence objects in one walk, at most
constexpr int kMaxDepth = 64;
constexpr int kMaxHops = 8;                  // through logs behind an event, at most

struct LeaveScan {
    ue3::Object* root = nullptr;
    std::uint64_t signature = 0;  // the root and its nested sequences (a streamed level's script joins them)
    std::uint64_t atMs = 0;
    std::vector<ue3::Object*> events;
    int visited = 0;
    int sequences = 0;
    int nested = 0;   // sequences reached through NestedSequences (the streamed levels' scripts)
    bool truncated = false;
    double ms = 0.0;
};
LeaveScan gLeaveScan;
int gOfferTick = 0;
std::uint32_t gOffers = 0;
gm::LeaveWhy gLastWhy = gm::LeaveWhy::Unknown;
bool gLoggedWhy = false;
std::size_t gLoggedEvents = static_cast<std::size_t>(-1);
std::string gLeaveFacts = "not read yet";

std::uint64_t LeaveNowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

ue3::Object* GameSequence(ue3::Object* worldInfo) {
    if (worldInfo == nullptr || gGetGameSequence.Get() == nullptr) {
        return nullptr;
    }
    ue3::Params parms(ue3::runtime::Get(), gGetGameSequence.Get());
    if (!ue3::runtime::Call(worldInfo, gGetGameSequence.Get(), parms)) {
        return nullptr;
    }
    return parms.Return<ue3::Object*>().value_or(nullptr);
}

std::uint64_t SequenceSignature(ue3::Object* root) {
    std::uint64_t h = reinterpret_cast<std::uintptr_t>(root) * 0x9E3779B97F4A7C15ull;
    if (auto const* nested = root != nullptr ? gNestedSequences.In<ue3::TArray<ue3::Object*>>(root) : nullptr) {
        h ^= static_cast<std::uint64_t>(nested->Count()) << 1;
        for (int i = 0; i < nested->Count() && i < 4096; ++i) {
            h = (h ^ reinterpret_cast<std::uintptr_t>(nested->data[i])) * 0x100000001B3ull;
        }
    }
    return h;
}

// What FindSeqObjectsByClass(SFXSeqEvt_LeaveWorld, true) finds: the sequence's objects, every sequence among them (a prefab's
// SequenceReference through the sequence it references), and the nested sequences (the streamed levels' scripts).
void WalkSequence(ue3::Object* seq, int depth, std::unordered_set<ue3::Object*>& seen, LeaveScan& out) {
    if (seq == nullptr || depth > kMaxDepth || out.visited > kMaxVisited) {
        out.truncated = out.truncated || (seq != nullptr);
        return;
    }
    if (!seen.insert(seq).second) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    if (!r.IsAlive(seq)) {
        return;
    }
    if (ue3::IsA(seq, gSequenceReferenceClass)) {
        WalkSequence(gReferencedSequence.Read<ue3::Object*>(seq).value_or(nullptr), depth + 1, seen, out);
    }
    if (auto const* list = gSequenceObjects.In<ue3::TArray<ue3::Object*>>(seq); list != nullptr && list->Count() <= 65536) {
        for (int i = 0; i < list->Count(); ++i) {
            ue3::Object* const o = list->data[i];
            if (o == nullptr) {
                continue;
            }
            ++out.visited;
            if (ue3::IsA(o, gLeaveEventClass)) {
                out.events.push_back(o);
            } else if (ue3::IsA(o, gSequenceClass)) {
                WalkSequence(o, depth + 1, seen, out);
            }
        }
    }
    if (auto const* nested = gNestedSequences.In<ue3::TArray<ue3::Object*>>(seq); nested != nullptr && nested->Count() <= 4096) {
        out.nested += nested->Count();
        for (int i = 0; i < nested->Count(); ++i) {
            WalkSequence(nested->data[i], depth + 1, seen, out);
        }
    }
}

// How many of the event's output links reach a node that acts: through logs (they pass on at once), up to a gate's In (index
// 0) that is closed, which does nothing. Any other node (a plot check, a message box, a switch...) counts as acting: it is the
// script's to decide.
int OpenPaths(ue3::Object* op, int hops) {
    std::optional<ue3::Elements> const outputs = gOutputLinks.ElementsOf(op, 64);
    if (!outputs) {
        return 0;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    int open = 0;
    for (int o = 0; o < outputs->count; ++o) {
        std::optional<ue3::Elements> const links = gOutputLinkLinks.ElementsOf(outputs->At(o), 64);
        if (!links) {
            continue;
        }
        for (int l = 0; l < links->count; ++l) {
            ue3::Object* const next = gLinkedOp.Read<ue3::Object*>(links->At(l)).value_or(nullptr);
            if (next == nullptr || !r.IsAlive(next)) {
                continue;
            }
            int const input = gLinkInput.Read<std::int32_t>(links->At(l)).value_or(0);
            if (ue3::IsA(next, gGateClass) && input == 0 && !gGateOpen.ReadBool(next).value_or(true)) {
                continue;  // a closed gate's In: stops here
            }
            if (ue3::IsA(next, gLogClass) && hops < kMaxHops) {
                open += OpenPaths(next, hops + 1) > 0 ? 1 : 0;
                continue;
            }
            ++open;
        }
    }
    return open;
}

// The facts for vr::game_menu::LeaveOffer, read now. `rescan`: walk the script again whatever its signature says. `events`:
// also each leave event's state, one line each (mevr omni offers).
gm::LeaveFacts ReadLeaveFacts(bool rescan, std::string& detail, std::string* events = nullptr) {
    gm::LeaveFacts facts;
    if (gLeaveEventClass.Get() == nullptr || gSequenceObjects.Get() == nullptr || gGetGameSequence.Get() == nullptr) {
        detail = "SFXSeqEvt_LeaveWorld, Sequence.SequenceObjects or WorldInfo.GetGameSequence not found";
        return facts;
    }
    std::string why;
    std::optional<Modes> const modes = ReadModes(why);
    if (!modes || !modes->current) {
        detail = modes ? "the game mode isn't known" : why;
        return facts;
    }
    ue3::Object* const pc = player::Controller();
    ue3::Object* const worldInfo = gActorWorldInfo.Read<ue3::Object*>(pc).value_or(nullptr);
    ue3::Object* const root = GameSequence(worldInfo);
    if (root == nullptr) {
        detail = "no game sequence";
        return facts;
    }
    std::uint64_t const now = LeaveNowMs();
    std::uint64_t const signature = SequenceSignature(root);
    if (rescan || root != gLeaveScan.root || signature != gLeaveScan.signature || now - gLeaveScan.atMs >= kRescanMs) {
        auto const start = std::chrono::steady_clock::now();
        LeaveScan scan;
        scan.root = root;
        scan.signature = signature;
        scan.atMs = now;
        std::unordered_set<ue3::Object*> seen;
        WalkSequence(root, 0, seen, scan);
        scan.sequences = static_cast<int>(seen.size());
        scan.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        gLeaveScan = std::move(scan);
        if (gLeaveScan.events.size() != gLoggedEvents) {
            gLoggedEvents = gLeaveScan.events.size();
            ue3::Reflection& r = ue3::runtime::Get();
            std::string names;
            for (std::size_t i = 0; i < gLeaveScan.events.size() && i < 4; ++i) {
                names += std::format("{}{}", i == 0 ? ": " : ", ", r.PathName(gLeaveScan.events[i]));
            }
            log::Info("main menu: leave: {} leave event{} in the level's script{}{} ({} sequence objects in {} sequences, {:.2f} ms{})",
                gLeaveScan.events.size(), gLeaveScan.events.size() == 1 ? "" : "s", names,
                gLeaveScan.events.size() > 4 ? ", ..." : "", gLeaveScan.visited, seen.size(), gLeaveScan.ms,
                gLeaveScan.truncated ? "; the walk stopped at its limit" : "");
        }
    }
    facts.known = true;
    facts.onFoot = engine::adapter::GameMode(*modes->current) == engine::adapter::Mode::OnFoot;
    ue3::Reflection& r = ue3::runtime::Get();
    for (ue3::Object* const event : gLeaveScan.events) {
        if (!r.IsAlive(event)) {
            continue;
        }
        ++facts.events;
        gm::LeaveEvent e;
        e.enabled = gEventEnabled.ReadBool(event).value_or(false);
        e.triggers = gTriggerCount.Read<std::int32_t>(event).value_or(0);
        e.maxTriggers = gMaxTriggerCount.Read<std::int32_t>(event).value_or(0);
        e.openPaths = OpenPaths(event, 0);
        facts.armed += gm::Armed(e) ? 1 : 0;
        if (events != nullptr) {
            *events += std::format("\n  {}: enabled {}, triggered {} of {}, {} open path{}: {}", r.PathName(event), e.enabled ? 1 : 0,
                e.triggers, e.maxTriggers <= 0 ? std::string("any") : std::to_string(e.maxTriggers), e.openPaths,
                e.openPaths == 1 ? "" : "s", gm::Armed(e) ? "armed" : "not armed");
        }
    }
    // The game's own answer, as LeaveWorld asks it.
    ue3::Object* const holder = worldInfo != nullptr ? gCombatHolder.Read<ue3::Object*>(worldInfo).value_or(nullptr) : nullptr;
    ue3::Function* const inCombat = holder != nullptr && gInCombat.Get() != nullptr ? r.FindFunction(r.ClassOf(holder), "InCombat")
                                                                                     : nullptr;
    if (inCombat == nullptr) {
        facts.known = false;
        detail = std::format("no {} to ask", kCombatWhere);
        return facts;
    }
    ue3::Params parms(r, inCombat);
    if (!ue3::runtime::Call(holder, inCombat, parms)) {
        facts.known = false;
        detail = std::format("{} couldn't be asked", kCombatWhere);
        return facts;
    }
    facts.inCombat = parms.ReturnBool().value_or(true);
    detail = std::format("game mode {}, {} {}, {} leave event{} ({} armed) in {} sequence objects of {} sequences, {} of them "
                         "nested (the streamed levels')", *modes->current, kCombatWhere, facts.inCombat ? "true" : "false",
        facts.events, facts.events == 1 ? "" : "s", facts.armed, gLeaveScan.visited, gLeaveScan.sequences, gLeaveScan.nested);
    return facts;
}

// Each tick: the word for the host (every tick, so it stays fresh while our menu is open and the game paused), the facts
// behind it every kOfferEveryTicks ticks. A change of the answer is logged once.
void StepOffers(Link const& link) {
    if (link.offer == nullptr) {
        return;
    }
    if (gOfferTick-- <= 0) {
        gOfferTick = kOfferEveryTicks;
        std::string detail;
        gm::LeaveFacts const facts = ReadLeaveFacts(false, detail);
        gm::LeaveWhy const why = gm::LeaveOffer(facts);
        gOffers = why == gm::LeaveWhy::Offered ? gm::kOfferLeave : 0;
        gLeaveFacts = std::format("{} ({})", gm::Text(why), detail);
        if (!gLoggedWhy || why != gLastWhy) {
            gLoggedWhy = true;
            gLastWhy = why;
            log::Info("main menu: leave: {} ({})", why == gm::LeaveWhy::Offered ? "offered" : std::format("hidden: {}", gm::Text(why)), detail);
        }
    }
    link.offer(gOffers);
}

#if defined(MEVR_GAME_LE2)
// `mevr omni party` (a test fixture's way past ME2's squad selection, lane LV: a map opened by the console starts with it): the
// screen's own A on its confirmation, BioSFHandler_PartySelection.FinalizeParty(true, 0), on the focused PartySelect panel's
// handler, with the party it has selected (none: no squad).
ue3::ClassRef gPartySelection{"SFXGame.BioSFHandler_PartySelection", ue3::kEveryGame};
ue3::FunctionRef gFinalizeParty{"SFXGame.BioSFHandler_PartySelection.FinalizeParty", ue3::kEveryGame};

std::string FinalizeParty() {
    Focus const focus = FocusedPanel();
    if (focus.tag != "PartySelect" || focus.handler == nullptr || !ue3::IsA(focus.handler, gPartySelection)) {
        return std::format("omni party: no squad selection in focus (the focused panel is '{}')", focus.tag);
    }
    if (gFinalizeParty.Get() == nullptr) {
        return "omni party: BioSFHandler_PartySelection.FinalizeParty not found";
    }
    ue3::Params parms(ue3::runtime::Get(), gFinalizeParty.Get());
    bool const set = parms.SetBool("bAPressed", true) && parms.Set("nContext", std::int32_t{0});
    bool const called = set && ue3::runtime::Call(focus.handler, gFinalizeParty.Get(), parms);
    log::Info("omni-tool: the squad selection finalized by the console ({}.FinalizeParty(true, 0) {})", ClassName(focus.handler),
        called ? "called" : "not called");
    return std::format("omni party: {}.FinalizeParty(true, 0) {}", ClassName(focus.handler), called ? "called" : "not called");
}
#endif

void Leave() {
    std::string detail;
    gm::LeaveFacts const facts = ReadLeaveFacts(true, detail);
    gm::LeaveWhy const why = gm::LeaveOffer(facts);
    if (why != gm::LeaveWhy::Offered) {
        gLast = std::format("leave: not done ({})", gm::Text(why));
        log::Info("main menu: leave not done: {} ({})", gm::Text(why), detail);
        return;
    }
    std::string mwhy;
    std::optional<Modes> const modes = ReadModes(mwhy);
    std::string how;
    ue3::Object* const mode = modes ? ActiveMode(*modes, how) : nullptr;
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Function* const leave = mode != nullptr && gLeaveWorld.Get() != nullptr ? r.FindFunction(r.ClassOf(mode), "LeaveWorld")
                                                                                  : nullptr;
    if (leave == nullptr) {
        gLast = "leave: no LeaveWorld on the active mode";
        log::Warn("main menu: leave not done: {}", mode == nullptr ? "no active game mode" : std::format("{} has no LeaveWorld", ClassName(mode)));
        return;
    }
    ue3::Params parms(r, leave);
    bool const called = ue3::runtime::Call(mode, leave, parms);  // directly: never a generated wrapper
    std::string const declared = r.NameText(r.OuterOf(leave));
    gLast = std::format("leave: {}.LeaveWorld {}", ClassName(mode), called ? "called" : "call failed");
    log::Info("main menu: leave: {}.LeaveWorld{} {} ({}; {})", ClassName(mode),
        declared != ClassName(mode) ? std::format(" (declared on {})", declared) : std::string(), called ? "called" : "call failed",
        detail, how);
}
#endif

void Run(gm::Action action, gm::Origin origin) {
    switch (action) {
    case gm::Action::Leave:
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
        Leave();
#else
        gLast = "leave: ME1 has none on foot";
        log::Info("main menu: leave not done: ME1 has no leave on foot (its LeaveWorld is the Mako's)");
#endif
        return;
    case gm::Action::Holster:
#if defined(MEVR_GAME_LE3)
        gLast = "holster: ME3 native contract not enabled";
#else
        Holster();
#endif
        return;
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    case gm::Action::Equipment:
        gLast = "equipment: use the game's weapon/armour stations";
        log::Info("omni-tool: {}", gLast);
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
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
        gSwitch = {};  // a replacement must not finish the older backout/opening path
#endif
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
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
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    StepOffers(link);  // whether the menu's Leave would act here, every tick (also while our menu is open)
#endif
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    if (link.menuOpen) {
        gSwitch = {};
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
        gPage = {};
#endif
        return;
    }
    StepSwitch(link);
#endif
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
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
        {"menu", gm::Action::PauseMenu}, {"close", gm::Action::CloseScreens}, {"holster", gm::Action::Holster},
        {"leave", gm::Action::Leave}};
    if (which == "offers") {
        return LeaveStatus();
    }
#if defined(MEVR_GAME_LE2)
    if (which == "party") {
        return FinalizeParty();
    }
#endif
    for (Name const& name : kNames) {
        if (which == name.word) {
            log::Info("main menu request: {} asked from the console (mevr omni {})", gm::Name(name.action), which);
            gm::Take(gSchedule, name.action, gm::Origin::OmniRail);
            return std::format("omni: {} taken; it runs in {} ticks once nothing of ours pauses the game (log `main menu request:`, "
                               "`omni-tool:`)", gm::Name(name.action), gm::kSettleTicks);
        }
    }
    return "usage: mevr omni <equipment|squad|map|journal|codex|computer|close|holster|leave|offers>";
}

std::string LeaveStatus() {
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    std::string detail;
    std::string events;
    gm::LeaveFacts const facts = ReadLeaveFacts(true, detail, &events);
    gm::LeaveWhy const why = gm::LeaveOffer(facts);
    return std::format("leave: {} ({}); the last tick's word to the host: {}{}", why == gm::LeaveWhy::Offered ? "offered" :
        std::format("hidden: {}", gm::Text(why)), detail, gOffers != 0 ? "offered" : "hidden", events);
#else
    return "leave: ME1 has no leave on foot (its LeaveWorld is the Mako's)";
#endif
}

bool GuiInputReady() {
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    std::string why;
    auto const modes = ReadModes(why);
    if (!modes || !modes->current) return false;
    auto const context = engine::adapter::GameMode(*modes->current);
#if defined(MEVR_GAME_LE3)
    // ME3's galaxy map over the CIC's projector: its Flash movie takes the pointer as a screen's does (the host notes its UI
    // panel as a game menu only while the map is held there: game/Le3GalaxyCapture, plugin/SequelOverlays).
    bool const mapScreen = engine::adapter::IsMapMode(*modes->current);
#else
    bool const mapScreen = false;
#endif
    if (context != engine::adapter::Mode::Gui && context != engine::adapter::Mode::Minigame && !mapScreen) return false;
    auto* mode = ActiveMode(*modes, why);
    auto* fn = mode && gGuiKey.Get() ? ue3::runtime::Get().FindFunction(ue3::runtime::Get().ClassOf(mode), "GuiKey") : nullptr;
    if (!fn) return false;
#if defined(MEVR_GAME_LE3)
    if (!le3_gui::FocusMovie()) return false;
#endif
    ue3::Params p(ue3::runtime::Get(), fn);
    return p.Set("Event", std::uint8_t{6});
#else
    return false;
#endif
}

bool GuiKey(std::uint8_t event, std::string& why) {
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
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
    if (!parms.Set("Event", event) || !ue3::runtime::Call(mode, guiKey, parms)) {
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
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    gLeaveScan = {};
    gOfferTick = 0;
    gOffers = 0;
    gLoggedWhy = false;
    gLoggedEvents = static_cast<std::size_t>(-1);
#endif
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    gSwitch = {};
#endif
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    gPage = {};
    gLoggedPages = false;
#endif
}

std::string StatusJson() {
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    return std::format("{{\"waiting\": \"{}\", \"last\": \"{}\", \"leave\": \"{}\", \"leaveOffered\": {}}}", gm::Name(gSchedule.action), gLast,
        gLeaveFacts, gOffers != 0 ? "true" : "false");
#else
    return std::format("{{\"waiting\": \"{}\", \"last\": \"{}\"}}", gm::Name(gSchedule.action), gLast);
#endif
}

} // namespace mevr::game::game_menu_request
