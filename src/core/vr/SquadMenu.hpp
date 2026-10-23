// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>

// The squad weapon menu (docs/squad-weapons-design.md, option A): pull the left trigger (pointing the
// left hand at a squadmate preselects them), flick the right stick toward a weapon, let go. Since
// 2026-10-22 the left trigger opens it, at once; the left grip is only the support grip (and grenades),
// and never opens it. Engine-agnostic and pure
// (unit-tested in tests/Main.cpp), except Publish/Latest, which carry the menu from the mod (game
// thread: game/SquadWeapons) to the host's renderer (Present thread: plugin/SquadPanel).
//
// Conventions: tracking space (OpenXR: meters, +X right, +Y up, -Z forward). Weapons are ME1's
// quick-slot types (BioBaseSquad.ESwitchToWeapon): 0 pistol, 1 shotgun, 2 assault rifle, 3 sniper
// rifle (4 = none).
namespace mevr::vr::squad_menu {

inline constexpr int kWeapons = 4;
inline constexpr int kMaxMembers = 2;  // ME1 takes two squadmates along

// ---- The radial: four tiles, one per weapon type, at fixed places ----------------------------------

enum class Direction : std::int8_t { None = -1, Up = 0, Left = 1, Right = 2, Down = 3 };

// The fixed layout (muscle memory, like the body holsters): up the assault rifle, left the shotgun,
// right the sniper rifle, down the pistol.
int WeaponAt(Direction direction);
Direction DirectionOf(int weapon);
char const* WeaponName(int weapon);  // "pistol", "shotgun", "assault rifle", "sniper rifle", "none"
char const* DirectionName(Direction direction);

struct RadialSettings {
    float deadZone = 0.55f;      // a tile is chosen once the stick is pushed past this ...
    float release = 0.35f;       // ... and stays chosen until it comes back inside this
    float hysteresisDeg = 12.0f; // a chosen tile keeps the stick this far past its 45-degree border
};

// Where a stick (x right, y up/forward, -1..1) points among the four tiles. `held`: the last answer
// (None when the stick was centered): it is kept across a sector border by the hysteresis, and past
// the dead zone only `release` applies.
Direction Radial(float x, float y, Direction held, RadialSettings const& settings);

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

// Where the menu's quad goes when it opens: `distance` meters from the hand, mostly up and a little
// further out along the line from the head through the hand, so it sits above the hand and clear of
// it; level, its front (+Z) facing the eyes (yaw and pitch toward the head, no roll).
XrPosef PlaceAtHand(XrVector3f const& hand, XrVector3f const& head, float distance);

// ---- The open/choose/close machine ------------------------------------------------------------------

struct Member {
    std::uint8_t carried = 0;  // bit w: carries a weapon of type w
    int current = -1;          // the type in hand (-1: none or unknown)
};

struct Input {
    float dt = 0.0f;              // seconds since the last step
    bool usable = false;          // the menu may be open now (on foot, a world view, tracked, a squad)
    bool triggerDown = false;     // the left trigger (latched)
    bool newPull = false;         // a new left-trigger pull began this step (while the menu may open)
    int pointed = -1;             // the member the left hand points at (within the cone), -1: none
    int preferred = -1;           // who to preselect when not pointing (the last one used, or nearest the ray)
    int memberCount = 0;          // 0..kMaxMembers, in card order (0 = the left card)
    Member members[kMaxMembers];
    float leftX = 0.0f;           // left stick: chooses the card
    float leftY = 0.0f;
    float rightX = 0.0f;          // right stick: chooses the tile
    float rightY = 0.0f;
    bool a = false;               // A (confirm), B (cancel): held state; the machine takes edges
    bool b = false;
    bool confirmOnRelease = true; // letting go of the trigger confirms (or cancels with nothing chosen)
    float idleCloseSeconds = 15.0f;  // with confirmOnRelease off: closes by itself after this long untouched
    RadialSettings radial;
    float cardPush = 0.6f;        // the left stick past this left or right picks that card
};

enum class Phase : std::uint8_t {
    Idle,
    Open,
};

enum class Close : std::uint8_t {
    None,
    Confirmed,      // released with a tile chosen, or A
    Released,       // released with nothing chosen
    CancelButton,   // B
    Repulled,       // confirmOnRelease off: the trigger pulled again
    Unusable,       // the conditions stopped holding (mode, view, tracking, squad changed)
    Idle,           // confirmOnRelease off: untouched for idleCloseSeconds
};
char const* CloseName(Close close);

struct Step {
    bool opened = false;
    bool openedPointing = false;  // opened with the squadmate the hand pointed at (else `preferred`)
    bool refused = false;         // a pull that would open it, with nobody in the squad
    Close closed = Close::None;
    bool cardChanged = false;
    bool tileChanged = false;     // the highlight moved (to a tile or to none)
    bool tileRefused = false;     // a flick toward a tile the member doesn't carry
    int member = -1;              // with Confirmed: the card index
    int weapon = -1;              // with Confirmed: the weapon type
};

class Machine {
public:
    Step Update(Input const& input);
    void Reset();

    Phase CurrentPhase() const { return phase_; }
    bool IsOpen() const { return phase_ == Phase::Open; }
    int Selected() const { return selected_; }
    int Highlighted() const { return highlighted_; }  // weapon type, -1: none

private:
    void OpenMenu(Input const& input, int member, Step& step);
    void CloseMenu(Close why, Step& step);

    Phase phase_ = Phase::Idle;
    int selected_ = 0;
    int highlighted_ = -1;
    Direction rightHeld_ = Direction::None;  // the radial's last answer
    int leftSide_ = 0;                       // the left stick's last card side (-1, 0, 1)
    bool flickRefused_ = false;              // this flick already refused (one feedback per flick)
    float idleFor_ = 0.0f;
    bool triggerWasDown_ = false;
    bool aWasDown_ = true;  // edges only from a press that began while open
    bool bWasDown_ = true;
};

// ---- What the host draws -------------------------------------------------------------------------

struct MemberView {
    char name[24] = {};      // UTF-8, as shown ("GARRUS")
    char role[24] = {};      // the class, as shown ("TURIAN AGENT")
    char initials[4] = {};   // for the class badge
    std::int8_t discipline = 0;  // badge colour: 0 combat, 1 tech, 2 biotic
    std::int8_t current = -1;    // weapon type in hand
    std::uint8_t carried = 0;    // bit per weapon type
    std::uint8_t pointed = 0;    // the hand pointed at them when the menu opened
    float health = -1.0f;        // 0..1; negative: unknown
};

struct View {
    std::uint32_t size = sizeof(View);  // the mod's sizeof(View): the host draws only a matching one
    std::uint32_t serial = 0;           // + 1 at each opening: the host places the panel anew
    bool open = false;
    bool confirmOnRelease = true;
    std::int8_t count = 0;
    std::int8_t selected = 0;           // card index
    std::int8_t highlighted = -1;       // weapon type, -1: none
    float distance = 0.27f;             // panel center from the hand, meters
    float width = 0.40f;                // panel width, meters
    MemberView members[kMaxMembers];
    std::uint64_t sequence = 0;         // set by Publish
    std::uint64_t timeMs = 0;           // set by Publish (GetTickCount64)
};

// Any thread. Latest: `open` forced false if older than `maxAgeMs` (the mod stalled or unloaded).
void Publish(View const& view);
View Latest(std::uint64_t maxAgeMs = 250);
void Reset();

// ---- The game's own weapon wheel ([Squad] style = native; docs/squad-weapons-design.md) -------------
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

// The open/steer/close machine for the game's wheel, opened like the custom menu: a new pull of the
// left trigger opens it at once; letting go of the trigger gives the highlighted entry and closes it.
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

// Who gets the left trigger (pure; unit-tested; both styles): the squad's wheel or menu (the mod reads
// it and the game sees neither LB nor LT from it) or the game's own mapping ([Controls] leftTrigger).
// Decided when the trigger goes down and kept until it's let go, so a mode change mid-pull never turns
// it into an LB press or release the game would take as a tap (TrySwapWeapon) or its own wheel.
struct TriggerOwner {
    bool down = false;
    bool owned = false;
};
// One step: `pulled` the latched trigger now, `wanted` whether the squad may have it now (on foot or in
// a wheel, the feature on). Returns whether it's the squad's this step.
bool OwnTrigger(TriggerOwner& owner, bool pulled, bool wanted);

} // namespace mevr::vr::squad_menu
