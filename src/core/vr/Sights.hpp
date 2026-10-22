#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>
#include <string_view>

// Gun sights (research/21): a collimated red-dot or holographic sight for every gun, and a magnified
// scope by picture-in-picture for the sniper rifle (optionally the assault rifle). The mod (game/sights)
// places the sight on the held gun each tick from the measured muzzle and barrel (game/weapon) and the
// [Sights] settings, and publishes it here; the host (plugin/SightLayers, on Halcyon's Present thread)
// decides per eye whether that eye looks through it and draws the quads:
//   - dot/holo: the reticle as a quad far out along the sight line (collimated: it sits on the target
//     whatever the eye's place behind the sight), shown only to an eye whose line along the sight passes
//     through the sight's window; a faint ring marks the window itself.
//   - scope: a disc at the eyepiece showing a magnified crop of an eye image already rendered (no extra
//     scene render), only to the eye looking through it: the lens point (x, y) shows the direction
//     axis * along + (x right + y up) * radius / zoom from the eye, so the image is centred on the sight
//     line (the reticle too) and magnified `zoom` times about it; an eye off the line sees the exit
//     pupil's shadow creep in.
//
// Engine-agnostic. Tracking space: OpenXR conventions (meters, +X right, +Y up, -Z forward), Halcyon's
// recentered app space (the eyes' and controllers' space). Everything but Publish/Latest is pure and
// unit-tested in tests/Main.cpp.
namespace mevr::vr::sights {

// ---- Settings ([Sights] in <GAME>MEVR.ini, parsed by the mod) ---------------------------------------

enum class Kind : std::int32_t { None = 0, Dot = 1, Holo = 2, Scope = 3 };
char const* Name(Kind kind);

// Weapon types, as BioWeaponRanged.GetRangedWeaponType (EBioItemWeaponRangedType) numbers them.
inline constexpr int kWeaponTypes = 4;
char const* WeaponName(int weapon);  // "pistol", "shotgun", "assault rifle", "sniper rifle", "unknown"
char const* WeaponKey(int weapon);   // the [Sights] key: "pistol", "shotgun", "assaultRifle", "sniperRifle"

enum class ScopeReticle : std::int32_t { Duplex = 0, MilDot = 1, Dot = 2 };
char const* Name(ScopeReticle reticle);

struct Gun {
    Kind kind = Kind::None;
    float zoom = 1.0f;       // Scope: magnification
    bool autoPlace = true;   // where the sight sits: from the gun's length (AutoPlace)
    float back = 0.0f;       // gun units behind the muzzle, along the barrel
    float height = 0.0f;     // gun units above the bore line, toward the gun's top
};

struct Settings {
    bool enabled = true;
    Gun guns[kWeaponTypes] = {{Kind::Dot}, {Kind::Holo}, {Kind::Holo}, {Kind::Scope, 4.0f}};
    // Dot and holo sights.
    std::uint32_t dotColor = 0xFF3020;  // 0xRRGGBB
    float dotSizeDeg = 0.25f;           // the dot's diameter as seen
    float ringSizeDeg = 1.6f;           // the holo ring's diameter as seen
    float windowUnits = 3.0f;           // the window's radius (gun units): the eye must look through it
    bool frame = true;                  // a faint ring at the window, so the sight can be found
    float zeroM = 0.0f;                 // where the sight line meets the shot (game meters); 0: parallel
    // Scopes.
    float lensUnits = 2.0f;             // the eyepiece's radius (gun units)
    ScopeReticle scopeReticle = ScopeReticle::Duplex;
    std::uint32_t scopeColor = 0xFF2020;  // the reticle's lit centre
    bool otherEye = true;               // scopeSource = other: magnify the other eye's image (the gun's own
                                        // scope model doesn't block it); same: the aiming eye's
    float focusM = 40.0f;               // game meters: where the two eyes' images agree (parallax)
    float eyeReliefM = 0.12f;           // real meters from the eye to the eyepiece for a full picture
    int resolution = 768;               // the scope picture's pixels across
    // Both.
    bool replaceRing = true;            // hide the floating ring reticle while an eye looks through a sight
    bool aimedFire = true;              // the sniper rifle's shots get the game's aimed-fire accuracy while
                                        // its scope picture is up (bIsZoomed for the shot only)
};

// The sight's place on a gun of this type when it isn't set: `back` a share of the muzzle-to-grip length
// (`gunLength`, gun units; <= 0 unknown: a typical length), `height` a typical height above the bore.
Gun AutoPlace(int weapon, Gun const& gun, float gunLength);

// "none", "dot", "holo", "scope" or "scope 4" (with a magnification, 1-12; `defaults` gives the zoom
// when none is written). Nullopt: unreadable.
std::optional<Gun> ParseGun(std::string_view text, Gun const& defaults);
// "auto" or "back, height" (gun units).
bool ParsePlace(std::string_view text, Gun& gun);
// "ff3020", "#FF3020", or a name: red, green, amber, cyan, white.
std::optional<std::uint32_t> ParseColor(std::string_view text);
std::optional<ScopeReticle> ParseScopeReticle(std::string_view text);

// ---- What the mod publishes each tick (mod -> host, HostServices::setSight) --------------------------

enum Flags : std::uint32_t {
    kFrame = 1u,        // draw the window's ring
    kReplaceRing = 2u,  // hide the floating ring reticle while an eye looks through the sight
    kOtherEye = 4u,     // a scope magnifies the other eye's image
};

struct Sight {
    std::uint32_t size = sizeof(Sight);  // the mod's struct size: the host ignores a mismatch
    std::int32_t kind = 0;               // Kind; None: no sight (no gun, not aiming along its barrel)
    std::int32_t weapon = -1;
    std::uint32_t flags = 0;
    XrVector3f center{};  // tracking space: the window's centre (dot, holo) or the eyepiece's (scope)
    XrVector3f axis{};    // unit: the sight line, toward the target
    XrVector3f up{};      // unit, perpendicular to the axis: the gun's top
    float radius = 0.0f;  // tracking meters: the window's or the eyepiece's radius
    float zoom = 1.0f;
    float dotSizeDeg = 0.25f;
    float ringSizeDeg = 1.6f;
    std::uint32_t dotColor = 0;
    std::uint32_t scopeColor = 0;
    std::int32_t scopeReticle = 0;
    float focusM = 40.0f;      // tracking meters
    float eyeReliefM = 0.12f;  // tracking meters
    std::int32_t resolution = 768;
    std::uint64_t sequence = 0;  // set by Publish
    std::uint64_t timeMs = 0;    // GetTickCount64 at Publish
};

// Any thread.
void Publish(Sight const& sight);
// The latest; `kind` forced to None when older than `maxAgeMs` or published by a mod with another size.
Sight Latest(std::uint64_t maxAgeMs = 250);
void Reset();

// What the host shows (host -> mod, HostServices::sightView), bits:
enum View : std::uint32_t {
    kViewLeft = 1u,    // the left eye looks through the sight
    kViewRight = 2u,   // the right eye
    kViewScope = 4u,   // a scope's picture is up (for the eye above)
};
void PublishView(std::uint32_t view);
std::uint32_t LatestView(std::uint64_t maxAgeMs = 250);

// ---- Geometry ---------------------------------------------------------------------------------------

// The sight's centre: `backM` behind the muzzle along the barrel, `heightM` up toward the gun's top.
XrVector3f Place(XrVector3f const& muzzle, XrVector3f const& barrel, XrVector3f const& up, float backM, float heightM);
// The gun's top: `up` made perpendicular to the barrel (unit). Nullopt if `up` runs along the barrel.
std::optional<XrVector3f> TopOf(XrVector3f const& barrel, XrVector3f const& up);
// The sight line: along the barrel, or (zero > 0) from the sight to the shot's line `zero` ahead of the
// muzzle (all in one unit; unit length).
XrVector3f Zeroed(XrVector3f const& center, XrVector3f const& muzzle, XrVector3f const& barrel, float zero);

// An eye against the sight line through `center` along `axis`.
struct Line {
    float along = 0.0f;    // from the eye forward to the sight's plane (negative: the sight is behind)
    float lateral = 0.0f;  // how far the eye is from the sight line
    float x = 0.0f;        // that offset along the sight's right (lens radii are this / radius)
    float y = 0.0f;        // ...and along its up
};
Line EyeLine(XrVector3f const& eye, XrVector3f const& center, XrVector3f const& axis, XrVector3f const& up);

// Whether an eye looks through a window of `radius`, with hysteresis: it comes on with the eye's line
// inside the window and between `minAlong` and `maxAlong` behind it, and goes off past 1.25 times the
// radius, or outside 0.5x / 1.2x those distances.
class Looking {
public:
    bool Update(Line const& line, float radius, float minAlong, float maxAlong);
    bool On() const { return on_; }
    void Reset() { on_ = false; }

private:
    bool on_ = false;
};

// The eye aiming through a scope (-1 none): of the eyes behind the eyepiece within `maxAlong`, the one
// nearest the line, within `box` (meters off the line); `current` keeps the eye it had while it stays
// within 1.25 box and isn't clearly worse (another eye nearer by more than a quarter box).
int AimingEye(Line const lines[2], float box, float minAlong, float maxAlong, int current);

// A quad's pose and size (square, meters).
struct Quad {
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    float size = 0.0f;
};
// The collimated reticle: `distance` out along the sight line from its centre, facing back, upright to
// the gun's top, `angularSize` (radians) across.
Quad Reticle(XrVector3f const& center, XrVector3f const& axis, XrVector3f const& up, float distance, float angularSize);
// A disc at the sight's centre, facing back along the line (the eyepiece; the window's ring).
Quad Disc(XrVector3f const& center, XrVector3f const& axis, XrVector3f const& up, float radius);

// ---- The scope's picture ------------------------------------------------------------------------------

struct Lens {
    XrVector3f center{};
    XrVector3f right{};  // axis x up
    XrVector3f up{};
    XrVector3f axis{};
    float radius = 0.0f;
};
std::optional<Lens> MakeLens(XrVector3f const& center, XrVector3f const& axis, XrVector3f const& up, float radius);

// The aiming eye against the lens.
struct ScopeView {
    float along = 0.0f;  // the eye to the lens plane, along the axis
    float qx = 0.0f;     // where the sight line through the eye meets the lens, in lens radii from its
    float qy = 0.0f;     //   centre (+x right, +y up): the centre of the exit pupil and of the reticle
    float pupil = 1.0f;  // the exit pupil's radius (lens radii): 1 at the eye relief, smaller further back
};
// Nullopt when the eye isn't behind the lens (closer than 1 cm) or more than 6 eye reliefs back.
std::optional<ScopeView> View(Lens const& lens, XrVector3f const& eye, float eyeRelief);
// The direction the lens point (x, y) (lens radii) shows, magnified `zoom` times about the sight line
// (not unit length: its component along the axis is `view.along`).
XrVector3f LensDirection(Lens const& lens, ScopeView const& view, float x, float y, float zoom);
// The point that direction from `eye` reaches `focus` ahead along the axis.
XrVector3f FocusPoint(XrVector3f const& eye, XrVector3f const& direction, XrVector3f const& axis, float focus);
// Where `point` lands in an eye image rendered from `pose` with `fov`: u from the left edge (0) to the
// right (1), v from the top (0) to the bottom (1). False if it's behind the eye.
bool Project(XrPosef const& pose, XrFovf const& fov, XrVector3f const& point, float& u, float& v);

} // namespace mevr::vr::sights
