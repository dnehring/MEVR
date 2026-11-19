// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>

// The squad's weapons at the left trigger (docs/squad-weapons-design.md): on every game the left trigger's pull is the game's
// own weapon wheel (the owner, 2026-11-18: "remove the custom menu and just always default to the classic menu"; our drawn
// menu, [Squad] style = custom, plugin/SquadPanel, is gone). Who the pull is (RouteTrigger, below): on ME1 the squad opens
// the wheel itself at once (SFXGameModeDefault.EnterWeaponWheel; pointing the left hand at a squadmate preselects them;
// letting go gives the highlighted entry: wheel::Machine); elsewhere, and on ME1 with both hands on the gun, the pull reaches
// the game as its own LB, whose hold opens the same wheel. Engine-agnostic and pure (unit-tested in tests/Main.cpp), except
// PublishPanel/LatestPanel, which carry the wheel panel's settings from the mod (game thread: game/SquadWeapons) to the
// host's renderer (Present thread: plugin/WheelPanel).
//
// Conventions: tracking space (OpenXR: meters, +X right, +Y up, -Z forward). Weapons are ME1's
// quick-slot types (BioBaseSquad.ESwitchToWeapon): 0 pistol, 1 shotgun, 2 assault rifle, 3 sniper
// rifle (4 = none).
namespace mevr::vr::squad_menu {

inline constexpr int kWeapons = 4;
inline constexpr int kMaxMembers = 2;  // ME1 takes two squadmates along

char const* WeaponName(int weapon);  // "pistol", "shotgun", "assault rifle", "sniper rifle", "none"

// ---- Pointing at a squadmate ------------------------------------------------------------------------

struct Target {
    bool valid = false;
    XrVector3f position{};  // tracking space
};

// The angle between a ray and the direction to `point` (radians, 0..pi); pi when the point is at the
// origin or the ray has no direction.
float AngleToRay(XrVector3f const& origin, XrVector3f const& direction, XrVector3f const& point);
// The target the ray points at: the smallest angle within `cone` (radians) and `maxDistance`
// (meters); a tie within a degree goes to the nearer one. -1 if none.
int PickPointed(XrVector3f const& origin, XrVector3f const& direction, Target const* targets, int count, float cone,
    float maxDistance);
// The target nearest the ray by angle, whatever the cone (-1 if none valid).
int NearestToRay(XrVector3f const& origin, XrVector3f const& direction, Target const* targets, int count);

// ---- The panel at the hand ---------------------------------------------------------------------------

// Where the game's wheel's quad goes when it opens (plugin/WheelPanel): `distance` meters from the hand, mostly up and a
// little further out along the line from the head through the hand, so it sits above the hand and clear of it; level, its
// front (+Z) facing the eyes (yaw and pitch toward the head, no roll).
XrPosef PlaceAtHand(XrVector3f const& hand, XrVector3f const& head, float distance);

// ---- The game's own weapon wheel (docs/squad-weapons-design.md) --------------------------------------
//
// ME1's weapon wheel (SFXGameModeWeaponWheel, game mode 3) is one ring in the console HUD movie
// (GUI_SF_HUD.ME_HUD): its angle picks a squad member and one of their four weapons together. The
// native BioSFHandler_HUD::UpdateHUDModes turns the right stick (vInput) into `nAngle` = the stick's
// yaw in degrees (atan2(up, right), 0..359; kept while the stick is centred), and the movie's
// updateArrowAngleInfo maps 361 - nAngle through weaponWheelAngleBoundaries to an entry. A pressed on
// an entry queues it (FSCommand SelectWeaponHUD -> BioSFHandler_HUD.HandleEvent 11 ->
// SquadOrderEquipWeapon); closing the wheel applies the queue (research/19 section 9).
// Read from the movie's ActionScript and SFXGame.pcc's bytecode (research/19 section 10).
namespace wheel {

// The wheel's members: 0 Shepard, 1 the HUD's first henchman (BioSFHandler_HUD.m_nHench1Index), 2 its
// second (m_nHench2Index). Its slots: 1 sniper rifle, 2 pistol, 3 assault rifle, 4 shotgun.
inline constexpr int kMembers = 3;
inline constexpr int kSlots = 4;

struct Entry {
    int member = -1;  // -1: none
    int slot = 0;     // 1..4; 0: none
    bool Valid() const { return member >= 0 && slot >= 1; }
    bool operator==(Entry const& other) const { return member == other.member && slot == other.slot; }
};

// The entry the HUD highlights for its nAngle (degrees; negative: no wheel, no entry).
Entry EntryAtStickAngle(int nAngle);
// The nAngle at the centre of an entry's arc (0..359), or -1 for an invalid entry.
int StickAngleOf(Entry entry);
// Slot <-> weapon type (BioBaseSquad.ESwitchToWeapon: 0 pistol, 1 shotgun, 2 assault rifle, 3 sniper
// rifle), as HandleEvent 11 converts them. -1 / 0 for anything else.
int WeaponOfSlot(int slot);
int SlotOfWeapon(int weapon);

// The open/steer/close machine for the game's wheel (TriggerRoute::OpensWheel): a new pull of the left trigger opens it at
// once; letting go of the trigger gives the highlighted entry and closes it.
struct Input {
    float dt = 0.0f;
    bool usable = false;          // it may open or stay open (a world view, tracked controllers, LE1 on foot or in the wheel)
    bool triggerDown = false;     // the left trigger (latched)
    bool newPull = false;         // a new left-trigger pull began this step
    bool pointed = false;         // the left hand points at a squadmate (the wheel opens on their weapon)
    bool onFoot = false;          // the game is in its default mode (0): the wheel can be entered
    bool wheelActive = false;     // the game's mode is the weapon wheel (3)
    bool confirmOnRelease = true; // letting go gives the highlighted entry (an A press) before closing
    bool highlightValid = false;  // the HUD highlights an entry
    bool highlightCarried = false;// ...and that member carries its weapon
    bool choiceRecorded = false;  // ...and it's already queued (the member's switch weapon is it)
    bool b = false;               // B (held): closes without giving anything
};

enum class Phase : std::uint8_t {
    Idle,
    Opening,     // EnterWeaponWheel called, waiting for the game's mode
    Open,        // the wheel is up and the trigger holds it
    Confirming,  // the trigger let go: A held, then waiting for the queue to take it
    Closing,     // ExitWeaponWheel called, waiting for the mode to leave
};
char const* PhaseName(Phase phase);

struct Step {
    bool enter = false;          // call SFXGameModeDefault.EnterWeaponWheel now
    bool openedPointing = false; // (with enter) the hand pointed at someone: preselect their weapon
    bool opened = false;         // the game's mode became the wheel
    bool openFailed = false;     // ...or didn't within kOpenTimeout
    // (with opened) a wheel we weren't holding was taken over: one that opened after kOpenTimeout (within
    // kLateOpenSeconds of the failed open), or any weapon wheel that's up when the trigger is pulled anew. Letting
    // go of the trigger then closes it as usual (with the trigger already up: closed at once, nothing given), so the
    // game is never left paused in a wheel nobody holds.
    bool adopted = false;
    bool holdA = false;          // the pad shows A this tick
    bool confirmed = false;      // an A was sent for the highlighted entry
    bool exit = false;           // call SFXGameModeWeaponWheel.ExitWeaponWheel now
    bool closed = false;         // the mode left the wheel after our exit
    bool closedByGame = false;   // ...or by itself (LB released, the pause menu, a cutscene)
    bool cancelled = false;      // B, or let go with nothing to give
    bool exitStuck = false;      // the mode didn't leave within kCloseTimeout twice: given up
};

inline constexpr float kOpenTimeout = 0.6f;    // seconds for the game to enter the wheel
inline constexpr float kPressASeconds = 0.08f; // how long A is held for a confirm
inline constexpr float kQueueTimeout = 0.4f;   // after A: how long to wait for the queue before closing anyway
inline constexpr float kCloseTimeout = 0.5f;   // after an exit: how long before trying once more
inline constexpr float kLateOpenSeconds = 10.0f; // after a failed open: a wheel that comes up this late is still ours

class Machine {
public:
    Step Update(Input const& input);
    void Reset() { *this = Machine{}; }
    Phase CurrentPhase() const { return phase_; }
    // The wheel is ours: from the enter until the mode has left (the pad's kFlagNativeWheel).
    bool Holding() const { return phase_ == Phase::Opening || phase_ == Phase::Open || phase_ == Phase::Confirming || phase_ == Phase::Closing; }

private:
    void BeginClose(Step& step, bool confirm, Input const& input);

    Phase phase_ = Phase::Idle;
    float timer_ = 0.0f;
    bool releasedEarly_ = false;  // let go while it was still opening
    bool triggerWasDown_ = false;
    bool bWasDown_ = true;        // B edges only from a press that began after it opened
    int exitTries_ = 0;
    float lateLeft_ = 0.0f;       // seconds left in which a late-opening wheel is adopted (after openFailed)
};

// What the host shows (HostServices::setWheelPanel, v12): the game's wheels on a panel of their own
// while one is up, cut out of the HUD panel texture. Published by the mod every tick.
struct Panel {
    std::uint32_t size = sizeof(Panel);  // the mod's sizeof(Panel): the host uses only a matching one
    std::uint32_t serial = 0;            // + 1 each time the mod opens the weapon wheel: placed afresh
    bool enabled = true;                 // a wheel panel at all (0: the wheels stay on the HUD panel)
    bool modOpen = false;                // the mod holds the weapon wheel open (its gesture)
    std::int8_t weaponPlace = 0;         // where the weapon wheel goes: 0 left hand, 1 right hand, 2 in front
    std::int8_t powerPlace = 1;          // where the power wheel goes (R3 hold): 0 left hand, 1 right hand, 2 in front
    float distance = 0.27f;              // panel centre from the hand, meters
    float width = 0.40f;                 // panel width, meters
    // The wheel's part of the UI, as fractions of the backbuffer (x, y, width, height); width <= 0: the
    // host measures it (WheelCrop) and starts from DefaultCrop.
    float crop[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    std::uint64_t sequence = 0;          // set by PublishPanel
    std::uint64_t timeMs = 0;
};
void PublishPanel(Panel const& panel);
// Any thread: the mod's latest, or nullopt-like (size 0) when it never published or is older than maxAgeMs.
Panel LatestPanel(std::uint64_t maxAgeMs = 1000);
void ResetPanel();

} // namespace wheel

// ---- Who the left trigger's pull is (pure; unit-tested) ------------------------------------------------
//
// Whatever the left hand does (open, or on the gun's foregrip), a pull opens only the game's own weapon wheel, one of two
// ways (the owner, 2026-11-18):
//   OpensWheel  the squad's: the mod opens the wheel itself at once (wheel::Machine: ME1's EnterWeaponWheel), pointing
//               preselects, letting go gives the highlighted entry; the game sees neither LB nor LT from the trigger, so
//               there is no 0.3 s hold and no tap's TrySwapWeapon (ME1's LB binding: OnHold 0.3 EnterWeaponWheel / OnTap
//               0.3 TrySwapWeapon).
//   GameButton  the game's: the pull reaches it through the pad mapping ([Controls] leftTrigger: lb, whose hold opens the
//               same wheel by the game's own binding, which the host shows at the hand all the same: plugin/WheelPanel).
//               ME2 and ME3 always (their wheels are other UIs: the mod can't open, steer or preselect them, and Halcyon's
//               ME3 renderer knows its wheel by the pad's LB held); ME1 with both hands on the gun (since 2026-11-15), in
//               the Mako (LB: boost), in menus, while the hand's power wheel has the left hand, or without [Squad] enabled.
enum class TriggerRoute : std::uint8_t { GameButton, OpensWheel };
char const* RouteName(TriggerRoute route);

struct TriggerFacts {
    bool enabled = true;         // [Squad] enabled
    bool leftTriggerLB = true;   // [Controls] leftTrigger = lb (the weapon wheel's button)
    bool opensWheel = false;     // this game's wheel can be opened by the mod (ME1: a v12 host, the script functions and the
                                 // game's EnterWeaponWheel / ExitWeaponWheel found); never on ME2 or ME3
    bool modeTakesIt = false;    // on foot or in the game's wheels (ME1's modes 0, 2, 3)
    bool menuOpen = false;       // our main menu or the omni-tool's rail has the inputs
    bool powerWheel = false;     // the hand's power wheel has the left hand (its pinch equips a power)
    bool supportGrip = false;    // the left hand holds the gun's foregrip (both hands on the gun)
};
TriggerRoute RouteTrigger(TriggerFacts const& facts);

// The route is decided when the trigger goes down and kept until it's let go, so a mode change mid-pull (or the left hand
// leaving the foregrip) never turns it into an LB press or release the game would take as a tap (TrySwapWeapon) or its
// own wheel.
struct TriggerOwner {
    bool down = false;
    bool owned = false;
};
// One step: `pulled` the latched trigger now, `wanted` whether the squad may have it now (RouteTrigger says OpensWheel).
// Returns whether it's the squad's this step.
bool OwnTrigger(TriggerOwner& owner, bool pulled, bool wanted);

} // namespace mevr::vr::squad_menu
