// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// Our main menu's Game menu and Holster buttons (research/28 "The game's menus", HostServices v19): the host's menu
// asks, the mod does it on the game thread by calling the game's own functions (the active game mode's ShowMenuGui:
// the pause menu; SFXGameModeDefault.TryHolster). The game-independent parts, unit-tested in tests/Main.cpp: the
// hand-off between the threads (Slot) and when the mod runs a request it took (Schedule).
namespace mevr::vr::game_menu {

// What the menu asked for (HostServices::takeGameMenuRequest's value).
enum class Action : std::uint32_t {
    None = 0,
    PauseMenu = 1,  // the game's pause menu (Start: Console_Menu = ShowMenuGui)
    Holster = 2,    // put the weapon away (Back: Console_Holster = TryHolster), only while one is drawn
    // The omni-tool's rail (research/57, 2026-11-04): one of the game's own screens, as its key opens it (the game mode's
    // Show<Screen>Gui), or every game screen closed (the screens' own B, as the player backs out).
    Equipment = 3,  // ShowEquipmentGui: the Inventory panel
    Squad = 4,      // ShowSquadGui: the character record (talents), over the main wheel
    Journal = 5,    // ShowJournalGui
    Codex = 6,      // ShowCodexGui
    Map = 7,        // ShowMapGui
    CloseScreens = 8,
    // The level's own way out (ME2 and ME3, the owner's answer to question 16, 2026-11-17): SFXGameModeBase.LeaveWorld on the
    // active mode, what their B tap called on foot (Console_Melee) before B was kept from the game in VR. Outside combat it fires
    // the level's SFXSeqEvt_LeaveWorld events (a finished assignment's "return to the Normandy?", leaving a scene or an
    // activity). The menu offers it only where it would act (Offers, LeaveOffer below).
    Leave = 9,
};
char const* Name(Action action);
// One of the screens (Equipment..Map): what the rail opens.
bool IsScreen(Action action);
// The game's function for a screen ("ShowEquipmentGui"...), nullptr for anything else.
char const* ShowFunction(Action action);
// Values from another module (an older or newer host): anything unknown is None.
Action FromValue(std::uint32_t value);
enum class Origin : std::uint32_t { Menu = 0, OmniRail = 1 };
std::uint32_t Encode(Action action, Origin origin);
Action DecodeAction(std::uint32_t value);
Origin DecodeOrigin(std::uint32_t value);

// ---- What the menu's game entries can do now (HostServices v44 setGameMenuOffers; research/28 "Leave") ----
// The mod says each game tick which entries would act (bits); the host shows such an entry only while that word is fresh, so a
// mod that is gone, reloading or older than v44 leaves the entry hidden.
inline constexpr std::uint32_t kOfferLeave = 1u << 0;
inline constexpr std::uint64_t kOfferFreshMs = 1000;
class Offers {
public:
    void Publish(std::uint32_t bits, std::uint64_t nowMs);
    // The bits last published, or 0 once older than kOfferFreshMs (or never published).
    std::uint32_t Current(std::uint64_t nowMs) const;
    void Forget();

private:
    std::atomic<std::uint32_t> bits_{0};
    std::atomic<std::uint64_t> atMs_{0};
};

// One of the level's SFXSeqEvt_LeaveWorld events as the game would take it (SequenceEvent.CheckActivate's own conditions)
// and where its output leads: through pass-through nodes (logs) to the first node that does something; a closed gate there
// does nothing (the cabins' post-credits scenes and the shooting gallery open theirs only while their scene or game runs).
struct LeaveEvent {
    bool enabled = false;          // SequenceEvent.bEnabled
    std::int32_t triggers = 0;     // TriggerCount
    std::int32_t maxTriggers = 0;  // MaxTriggerCount (0: no limit)
    int openPaths = 0;             // output links reaching a node that acts (anything but a closed gate)
};
bool Armed(LeaveEvent const& event);

// When the menu offers Leave: on foot, out of combat (the game's own InCombat, which LeaveWorld asks), and at least one armed
// leave event loaded in the game's sequence (WorldInfo.GetGameSequence, recursively, as LeaveWorld's FindSeqObjectsByClass).
struct LeaveFacts {
    bool known = false;      // the game's modes, the sequence and the combat answer were read
    bool onFoot = false;
    bool inCombat = false;
    int events = 0;          // SFXSeqEvt_LeaveWorld events loaded
    int armed = 0;           // ...of which Armed
};
enum class LeaveWhy { Offered, Unknown, NotOnFoot, InCombat, NoEvent, NoneArmed };
LeaveWhy LeaveOffer(LeaveFacts const& facts);
char const* Text(LeaveWhy why);

// The host's side: the menu's button stores a request (any thread), the mod takes it once (HostServices v19). A newer
// request replaces one not yet taken. Whether anything has ever asked (Taken() called) tells the host a v19 mod runs.
class Slot {
public:
    // Returns the request it replaced (None if none was waiting).
    Action Put(Action action, Origin origin = Origin::Menu);
    // The waiting request, cleared (None if none).
    Action Take();
    std::uint32_t TakeValue();
    // A mod has polled at least once.
    bool Polled() const { return polled_.load(std::memory_order_relaxed); }

private:
    std::atomic<std::uint32_t> value_{0};
    std::atomic<bool> polled_{false};
};

// The mod's side, one step per game tick: a request taken from the host waits until our menu is closed and the
// menu's pause is lifted (game/MenuPause unpauses on the tick it sees the menu closed) for kSettleTicks ticks in a
// row, then runs once. Our menu opening again calls it off; so does waiting longer than kGiveUpTicks.
inline constexpr int kSettleTicks = 2;
inline constexpr int kGiveUpTicks = 600;  // about 5-10 s of game ticks
struct Schedule {
    Action action = Action::None;
    int settled = 0;  // ticks in a row with the menu closed and the game not paused by it
    int age = 0;      // ticks since it was taken
    Origin origin = Origin::Menu;
};
struct Step {
    Action run = Action::None;      // do this now
    Action dropped = Action::None;  // this request was called off
    bool reopened = false;          // ...because our menu opened again (else it waited too long)
    Origin origin = Origin::Menu;
};
// A request just taken (None: nothing new, the schedule is left alone). A new one replaces one still waiting (the menu
// can't ask twice without opening in between, which calls the first off anyway).
void Take(Schedule& schedule, Action action, Origin origin = Origin::Menu);
// One game tick. `menuOpen`: our main menu is open; `pausedByMenu`: the game is still paused by it.
Step Advance(Schedule& schedule, bool menuOpen, bool pausedByMenu);

// ---- Backing out of the game's screens by waiting for each to go (ME3; lane OMX, research/le3-native-gui.md) ----
// The rail's Close and a page switch back out of the game's screens with the player's B. ME1 and ME2 send B a few ticks
// apart while the game stays in its GUI mode. ME3's area map closes by an animation its B release starts
// (SFXSFHandler_AreaMap.HandleInputEvent: B_RELEASE -> OnBeginClose -> the movie's ActionScript OnBeginClose, whose end calls
// CloseMap: back to the Mission Computer, or out of the GUI mode when opened on its own), and a B before that end starts it
// again: a B every 12 ticks held the map up through all 8 back-outs (lane CTL, 2026-11-17). So here a screen gets its B once;
// the next B goes to the screen that takes its place, `settleMs` after it has the focus, and the same screen gets another only
// if it is still in focus `resendMs` after its B (it took no notice: still opening). A screen is its object and its tag: ME3
// makes a new movie for each page and for B's return to the Mission Computer.
struct BackOutRules {
    std::uint64_t settleMs = 250;    // a new screen in focus: this long before its B (the first screen gets its B at once)
    std::uint64_t resendMs = 1500;   // the same screen still in focus this long after its B: B again
    int maxPerScreen = 3;            // Bs to one screen before giving up
    int maxSends = 8;                // Bs in all
    std::uint64_t noFocusMs = 3000;  // the GUI mode with no screen in focus this long: give up
};
struct BackOutScreen {
    std::uintptr_t object = 0;  // the focused screen's object; 0: none in focus
    std::string tag;
    bool operator==(BackOutScreen const& other) const { return object == other.object && tag == other.tag; }
};
struct BackOut {
    bool started = false;
    BackOutScreen seen;             // the screen in focus at the last look
    std::uint64_t seenSinceMs = 0;  // ...in focus since
    BackOutScreen sentTo;           // the screen the last B went to
    std::uint64_t sentAtMs = 0;
    int sentToSame = 0;             // Bs to that screen
    int sends = 0;                  // Bs in all
};
enum class BackOutStep { Wait, Send, GiveUp };
// One look at the focused screen while the game is in its GUI mode. Send: send B now (counted as sent).
BackOutStep StepBackOut(BackOut& state, BackOutScreen const& focused, std::uint64_t nowMs, BackOutRules const& rules = {});

} // namespace mevr::vr::game_menu
