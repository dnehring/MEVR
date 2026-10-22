#pragma once

#include <openxr/openxr.h>

#include <cstdint>

// The squad weapon menu (docs/squad-weapons-design.md, option A): point the left hand at a squadmate,
// squeeze the left grip, flick the right stick toward a weapon, let go. Engine-agnostic and pure
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
    bool gripDown = false;        // the left grip (latched)
    bool newAwaySqueeze = false;  // a new left-grip squeeze began away from the gun's foregrip this step
    bool nearGun = false;         // the left hand is near the foregrip (a squeeze there is a missed grab)
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
    bool confirmOnRelease = true; // letting go of the grip confirms (or cancels with nothing chosen)
    float openHoldSeconds = 0.35f;// pointing at nobody: the grip must stay squeezed this long to open
    float idleCloseSeconds = 15.0f;  // with confirmOnRelease off: closes by itself after this long untouched
    RadialSettings radial;
    float cardPush = 0.6f;        // the left stick past this left or right picks that card
};

enum class Phase : std::uint8_t {
    Idle,
    Arming,  // squeezed while pointing at nobody: opens once held openHoldSeconds
    Open,
};

enum class Close : std::uint8_t {
    None,
    Confirmed,      // released with a tile chosen, or A
    Released,       // released with nothing chosen
    CancelButton,   // B
    Resqueezed,     // confirmOnRelease off: the grip squeezed again
    Unusable,       // the conditions stopped holding (mode, view, tracking, squad changed)
    Idle,           // confirmOnRelease off: untouched for idleCloseSeconds
};
char const* CloseName(Close close);

struct Step {
    bool opened = false;
    bool openedPointing = false;  // opened at once because the hand pointed at someone
    bool armed = false;           // a squeeze at nobody began arming
    bool missedGrab = false;      // a squeeze at nobody near the foregrip: not the menu
    bool refused = false;         // a squeeze that would open it, with nobody in the squad
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
    float ArmedFor() const { return armedFor_; }

private:
    void OpenMenu(Input const& input, int member, Step& step);
    void CloseMenu(Close why, Step& step);

    Phase phase_ = Phase::Idle;
    int selected_ = 0;
    int highlighted_ = -1;
    Direction rightHeld_ = Direction::None;  // the radial's last answer
    int leftSide_ = 0;                       // the left stick's last card side (-1, 0, 1)
    bool flickRefused_ = false;              // this flick already refused (one feedback per flick)
    float armedFor_ = 0.0f;
    float idleFor_ = 0.0f;
    bool gripWasDown_ = false;
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

} // namespace mevr::vr::squad_menu
