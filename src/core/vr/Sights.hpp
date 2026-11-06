// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

// Game (research/21 section 16): the game's own sniper scope view (its reticle, lens mask and tube rings, vr::scope_art),
// taken from the running game; Duplex when those textures aren't found.
enum class ScopeReticle : std::int32_t { Duplex = 0, MilDot = 1, Dot = 2, Game = 3 };
char const* Name(ScopeReticle reticle);

struct Gun {
    Kind kind = Kind::None;
    float zoom = 1.0f;       // Scope: magnification; 0 = the game's (the held weapon's own, GameZoom; research/21 section 16)
    bool autoPlace = true;   // where the sight sits: from the gun's length (AutoPlace)
    float back = 0.0f;       // gun units behind the muzzle, along the barrel
    float height = 0.0f;     // gun units above the bore line, toward the gun's top
    float side = 0.0f;       // gun units to the gun's right (a bone off the gun's middle; 0 when set by hand)
    // Scopes (research/21 section 14): the model's eyepiece radius (gun units; 0 = auto: the model's own (Optic), else
    // Settings::lensUnits), and the model's bones hidden while the scope's picture is up (`<type>Hide`: names, comma
    // separated; "auto": the model's own (Optic), else Scope_Lens for a sniper rifle).
    float lens = 0.0f;
    std::string hide;
};
// A scope with its magnification (0: the game's), eyepiece radius (gun units; 0: auto) and the bones hidden while its
// picture is up ("auto": the model's).
inline Gun ScopeGun(float zoom, float lens, char const* hide) {
    Gun gun{Kind::Scope, zoom};
    gun.lens = lens;
    gun.hide = hide;
    return gun;
}
// A red dot on a gun whose model has a scope's eyepiece of this radius (used when the player picks `scope` for it).
inline Gun DotGun(float lens) {
    Gun gun{Kind::Dot};
    gun.lens = lens;
    return gun;
}

// The alignment assist (research/21 section 8; Overlord's ADS eye-distance assistance adapted to the
// sideways case): with the gun raised to the face, the whole gun slides across the sight line toward the aiming eye by
// the eye's offset times a continuous weight (since 2026-10-27 Overlord's ADS approach weight, vr::ads::Approach times
// vr::ads::DistanceWeight, research/21 section 15; before it a 10-degree cone around the line). Mod-side: the right
// hand's goal moves (game/arm_ik), so the muzzle, the shots and the sight follow the gun as drawn.
struct Assist {
    bool enabled = true;
    float strength = 1.0f;       // 0..1: how much of the way to the line
    float maxShiftM = 0.10f;     // the gun moves sideways at most this far (tracking meters): scopes
    // ...and red dots and holos: none since 2026-11-05 (research/21 section 17). At 0.03 the rifle slid 3 cm toward the
    // eye and back each time the hold crossed the corridor's edge (10 times in 18 s of the user's session), while the eye
    // already sat on the unassisted line (0.1 cm off on average): a dot's eye box forgives, the gun stays in the hand.
    float dotMaxShiftM = 0.0f;
};

// The pull to the eye (research/21 sections 14-15; Overlord's ADS eye-distance assistance): the assist also slides the
// gun back along the sight line so the sight comes to `reliefM` in front of the aiming eye, by at most `maxM` (per sight:
// Overlord's 5 cm for a holo, 10 cm low power, 15 cm scopes; a scope under ADS further), never away from the eye.
// Weighted like the sideways move and eased by the same spring. Zero: no pull.
struct Pull {
    float reliefM = 0.0f;  // tracking meters: where the eyepiece should end up in front of the eye
    float maxM = 0.0f;     // tracking meters: the gun moves at most this far toward the eye
};

struct Settings {
    bool enabled = true;
    // Since 2026-10-23 the shotgun defaults to a red dot (a holo's ring is busy on a gun held at the cheek; the user
    // asked for "something like a red dot"). The assault rifle was a 1.5x scope on its model's scope for a day
    // (research/21 section 14) and is a red dot again (section 15: the player wants a dot there). Since 2026-11-03
    // (section 16) the sniper rifle's magnification is the game's own for the held weapon, and the eyepieces and the
    // hidden parts are the model's own, measured from the game's meshes (Optic).
    Gun guns[kWeaponTypes] = {{Kind::Dot}, {Kind::Dot}, DotGun(0.0f), ScopeGun(0.0f, 0.0f, "auto")};
    // Dot and holo sights.
    std::uint32_t dotColor = 0xFF3020;  // 0xRRGGBB
    float dotSizeDeg = 0.25f;           // the dot's diameter as seen
    float ringSizeDeg = 1.6f;           // the holo ring's diameter as seen
    float windowUnits = 3.0f;           // the window's radius (gun units), as drawn (the ring)
    float eyeBoxUnits = 6.0f;           // the eye counts as looking through when its line along the sight passes
                                        // within this of the window's centre (gun units; at least windowUnits): a
                                        // gun raised to the face shows its dot without hunting for a 3 cm window
    bool frame = true;                  // a faint ring at the window, so the sight can be found
    bool dotWindow = true;              // see through a solid sight housing (kWindow; research/21 section 17)
    float zeroM = 0.0f;                 // where the sight line meets the shot (game meters); 0: parallel
    bool converge = true;               // shots leave the muzzle toward the point the sight line hits (a world
                                        // trace along it), so they land on the dot at any distance
    // Scopes.
    float lensUnits = 2.0f;             // the eyepiece's radius (gun units) on a model we don't know
    ScopeReticle scopeReticle = ScopeReticle::Duplex;  // Game can't work yet (research/21 section 16): the owner, 2026-11-04
    // The game's second magnification (research/21 section 16): aiming down a scope, a flick of the right thumbstick
    // forward steps to the next of the held weapon's magnifications, back to the one before.
    bool zoomStick = true;
    std::uint32_t scopeColor = 0xFF2020;  // the reticle's lit centre
    bool otherEye = true;               // scopeSource = other: magnify the other eye's image (the gun's own
                                        // scope model doesn't block it); same: the aiming eye's
    float focusM = 40.0f;               // game meters: where the two eyes' images agree (parallax)
    float eyeReliefM = 0.12f;           // real meters from the eye to the eyepiece for a full picture (fieldDeg 0 only)
    int resolution = 768;               // the scope picture's pixels across
    // The virtual eyepiece (research/21 section 13, 2026-10-25): a headset and a controller can't bring the eye to a
    // real scope's 8 cm eye relief, so with the aiming eye behind the eyepiece (up to `scopeMaxM`) the picture is a disc
    // that looks `scopeFieldDeg` across from where the eye is (its apparent field: a real 4x hunting scope's is about
    // 20-25 degrees), the magnification true across it. The eye `scopeEyeBoxM` off the sight line shades half of it.
    // scopeFieldDeg = 0: the physical lens (lensUnits, eyeReliefM) as before.
    float scopeFieldDeg = 22.0f;
    float scopeEyeBoxM = 0.05f;
    float scopeMaxM = 0.70f;
    // The pull to the eye (research/21 section 14, 2026-10-27): raised to the eye, a scoped gun slides back along the
    // sight line until the model's eyepiece is `scopeReliefM` in front of the aiming eye (never nearer than the camera's
    // near plane plus 2 cm), at most `scopePullMaxM`; the picture then sits on the model's eyepiece (it looks what the
    // eyepiece subtends from there, wherever the eye is). scopePullMaxM = 0: no pull, the virtual eyepiece as before.
    float scopeReliefM = 0.12f;
    float scopePullMaxM = 0.15f;
    // Aiming down the sight (research/21 section 15, 2026-10-27; vr::ads, Overlord's corridor, dwell and hysteresis, plus
    // `adsReachM`: the centre eye within this of the sight to come on, 0.10 m further to go off). A scope's picture shows,
    // and its lens part hides, only under ADS; the pull may then reach `scopeAdsPullMaxM` with the faster spring. ads = 0:
    // the picture whenever an eye is behind the eyepiece within scopeMaxM (section 13), the pull at scopePullMaxM.
    bool ads = true;
    float adsReachM = 0.40f;
    float scopeAdsPullMaxM = 0.35f;
    // Both.
    bool replaceRing = true;            // hide the floating ring reticle while an eye looks through a sight
    bool aimedFire = true;              // the sniper rifle's shots get the game's aimed-fire accuracy while
                                        // its scope picture is up (bIsZoomed for the shot only)
    Assist assist;
};

// The sight's place on a gun of this type when it isn't set: `back` a share of the muzzle-to-grip length
// (`gunLength`, gun units; <= 0 unknown: a typical length), `height` a typical height above the bore.
Gun AutoPlace(int weapon, Gun const& gun, float gunLength);
// The sight's height on the gun's own model (research/21 section 11), from `modelTop`: the model's highest point
// above the bore line (gun units, from its mesh bounds, game/weapon). A scope's eyepiece centre sits a lens radius
// (`radius`) below it, so the picture covers the top of the model's scope tube; a dot's or holo's window centre one
// unit above it, just over the gun. Nullopt for an implausible top (not above 0.5 or above 30 units): keep the
// typical height.
std::optional<float> HeightOnModel(Kind kind, float modelTop, float radius);

// "none", "dot", "holo", "scope", "scope auto" (the game's magnification: zoom 0) or "scope 4" (with a magnification, 1-12,
// decimals allowed: "scope 1.5"; `defaults` gives the zoom when none is written, else auto). Nullopt: unreadable.
std::optional<Gun> ParseGun(std::string_view text, Gun const& defaults);

// ---- The game's own optics (research/21 section 16) ---------------------------------------------------------------
// The game's player guns are 8 meshes, two per type (a and b; the manufacturers only change their colours). The rifles'
// carry a sight: measured from the meshes (the vertices each sight bone carries, the reference pose: the unfolded gun),
// the bone the sight follows, where the eyepiece's glass (a scope) or the housing's rear opening (a sight's window) is from
// that bone's origin in the gun's frame (`back` toward the butt, `up` toward the top; gun units), its radius, and the
// bones to hide while a scope's picture is up ("" none). Pistols and shotguns carry no sight: nullopt, as for any mesh we
// don't know (a mod's), which keeps the generic placement.
struct Optic {
    char const* mesh;  // the SkeletalMesh's object name, "WPN_SNPa_MDL"
    char const* bone;
    float back;
    float up;
    float lens;
    char const* hide;
};
std::optional<Optic> OpticFor(std::string_view meshName);
// The bones `<type>Hide` names: its own list, or for "auto" the model's (Optic), else Scope_Lens on a sniper rifle.
std::vector<std::string> HideList(std::string_view setting, std::optional<Optic> const& optic, int weapon);

// The game's magnifications: a weapon's SFXWeaponStatsBase.AimModes (per manufacturer), each a zoomed camera FOV and
// whether it's a scope. Against the combat camera's FOV (SFXCameraMode's 70 degrees; SFXCameraMode_Combat keeps it) a
// ZoomFOV of 22.6 degrees is 3.5x, 11.4 is 7.0x.
inline constexpr float kGameCombatFovDeg = 70.0f;
// tan(base / 2) / tan(zoomFov / 2), clamped to 1..12; 0 for an implausible FOV (not between 1 and the base).
float ZoomFromFov(float zoomFovDeg, float baseFovDeg = kGameCombatFovDeg);
struct GameZoom {
    int count = 0;           // the scoped modes, in the weapon's order
    float zoom[4]{};
    float fovDeg[4]{};
    int unscoped = 0;        // AimModes that aren't scopes (the unzoomed TightAim of pistols, shotguns, assault rifles)
};
// Adds one of the weapon's AimModes; implausible FOVs and modes past four are skipped.
void AddAimMode(GameZoom& zoom, float zoomFovDeg, bool scoped);
// The magnification for `level` (clamped into the scoped modes); 0 without any.
float ZoomAt(GameZoom const& zoom, int level);

// The flick that steps the magnification: +1 once the stick is pushed forward past `on`, -1 pulled back past it, then
// nothing until it has come back inside `off`.
class ZoomFlick {
public:
    int Update(float stickY, float on = 0.7f, float off = 0.3f);
    void Reset() { armed_ = true; }

private:
    bool armed_ = true;
};
// A list of bone names ("Scope_Lens, Scope_Glow"; "none" or empty: no bones), for `<type>Hide`.
std::vector<std::string> ParseBoneList(std::string_view text);
// The eyepiece's radius on this gun (gun units): its own `lens`, else `lensUnits`.
float LensUnits(Settings const& settings, int weapon);
// How far behind a scope's bone its eyepiece sits when the bone isn't named for the eyepiece (a housing's middle, as
// the assault rifle's `Scope`; gun units, per weapon type) [verify with a snapshot].
float HousingToEyepiece(int weapon);
// Whether a bone's name says it's a scope's eyepiece ("eye", "ocular" or "rear" beside a sight word: `Scope_Eye`).
bool IsEyepieceName(std::string_view name);
// The one-time move back of the assault rifle's sight (research/21 section 15): `assaultRifle = scope 1.5`, exactly what
// section 14's migration wrote (case, spaces and "1.50" aside), becomes "dot"; anything else is the player's: nullopt.
std::optional<std::string> MigrateAssaultRifle(std::string_view value);
// "auto" or "back, height" or "back, height, side" (gun units).
bool ParsePlace(std::string_view text, Gun& gun);
// "ff3020", "#FF3020", or a name: red, green, amber, cyan, white.
std::optional<std::uint32_t> ParseColor(std::string_view text);
std::optional<ScopeReticle> ParseScopeReticle(std::string_view text);

// ---- What the mod publishes each tick (mod -> host, HostServices::setSight) --------------------------

enum Flags : std::uint32_t {
    kFrame = 1u,        // draw the window's ring
    kReplaceRing = 2u,  // hide the floating ring reticle while an eye looks through the sight
    kOtherEye = 4u,     // a scope magnifies the other eye's image
    // A dot or holo on a model whose sight housing is solid (the assault rifles' boxes: Optic, research/21 section 17):
    // the eye looking through sees the world through the window (the other eye's image at 1x on a disc there), not the
    // housing's back face. A host from before ignores it.
    kWindow = 8u,
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
    float eyeBox = 0.06f;      // tracking meters: an eye within this of the line looks through (dot, holo)
    float scopeFieldDeg = 0.0f;  // the virtual eyepiece (Settings): 0 = the physical lens
    float scopeEyeBox = 0.05f;   // tracking meters
    float scopeMaxM = 0.70f;     // tracking meters
    // The game's scope art (research/21 section 16; vr::scope_art's three textures, in its order): each Texture2D's
    // FTextureResource* (UTexture.Resource: engine memory) and size, found by the mod while a scope is on the held gun
    // (also while its picture waits for ADS and `kind` is None, so the host composes it before the first look). The host
    // composes the layers once per `artGeneration` (0: none: our own reticles).
    struct ArtTexture {
        std::uint64_t resource = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };
    ArtTexture art[3]{};
    std::uint64_t artGeneration = 0;
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

// The sight's centre: `backM` behind the muzzle along the barrel, `heightM` up toward the gun's top, `sideM` to its
// right (barrel x up).
XrVector3f Place(XrVector3f const& muzzle, XrVector3f const& barrel, XrVector3f const& up, float backM, float heightM,
    float sideM = 0.0f);

// ---- The sight on the gun's model (research/21 sections 11 and 13) --------------------------------------------------
// Which of the model's bones the sight goes on (an index into `names`; nullopt: none named for a sight). A scope's
// picture belongs at its eyepiece: a name with "eye" or "ocular" or "rear" beside a scope word ("Scope_Eye") wins over
// the front lens ("Scope_Lens"); a dot or holo takes the first sight-named bone (scope, sight, optic, reticle, lens).
std::optional<std::size_t> SightBone(Kind kind, std::span<std::string const> names);
// A point of the model in the gun's frame (gun units): behind the muzzle, above the bore, to the right.
struct ModelPoint {
    float back = 0.0f;
    float up = 0.0f;
    float side = 0.0f;
};
// The model's top above the bore from its bones (the mesh's bounds are a loose culling box: 15-22 units on guns
// whose bones reach 3-7): the highest bone between the muzzle and the gun's back end (`gunLength` x 1.6 behind it) and
// within 8 units of its middle. Nullopt when none is above 0.5 units.
std::optional<float> ModelTop(std::span<ModelPoint const> bones, float gunLength);
// Which of the model's bones to hide while the scope's picture is up (research/21 section 14): each name of `wanted`
// found among `names` (case-insensitive; the bones only, sockets skipped), except the root, the sight's own bone and
// its ancestors (hiding a bone collapses its children onto it [verify]: the sight would move). `parents` as the
// reference skeleton gives them (-1: none). What was skipped and why goes into `skipped` (one entry per name).
struct HideChoice {
    std::vector<std::size_t> bones;    // indices into `names`
    std::vector<std::string> skipped;  // "Foo (not on this model)", "Root (the sight hangs on it)"
};
HideChoice PartsToHide(std::span<std::string const> names, std::span<int const> parents, std::optional<std::size_t> sightBone,
    std::span<std::string const> wanted);
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

// ---- The alignment assist ----------------------------------------------------------------------------

// 0 at t <= 0, 1 at t >= 1, the quintic smoothstep between (zero slope and curvature at both ends).
float Smooth(float t);

// What the assist wants this tick. `center` and `axis` are the sight as it would be WITHOUT the assist's
// own shift (so the shift never feeds back into its goal); `eyes` the two eyes (tracking space). `weight` (0..1) is how
// raised the gun is (vr::ads::Approach x vr::ads::DistanceWeight), times `settings.strength` here. `currentEye` keeps the
// aiming eye unless the other is clearly better.
struct AssistGoal {
    XrVector3f shift{};    // move the gun by this (tracking meters): the sight line then runs `weight` of the
                           // way toward the aiming eye
    int eye = -1;          // the aiming eye (0 left, 1 right); -1: none in reach
    float angleDeg = 0.0f; // that eye off the sight line, seen from the sight
    float along = 0.0f;    // the sight ahead of that eye (meters)
    float x = 0.0f;        // the eye's offset from the line along the sight's right (meters)...
    float y = 0.0f;        // ...and its up (the gun's top)
    float weight = 0.0f;   // 0..1: the given weight times the strength
    float pull = 0.0f;     // of `shift`: how far the gun moves back along the line toward the eye (meters)
};
// `pull`: the gun also slides back along the sight line toward the aiming eye, `pull.maxM` at most, until the sight is
// `pull.reliefM` ahead of it, times the same weight (never forward, never past the relief). The sideways move is capped
// at `settings.maxShiftM` on its own.
AssistGoal AssistTarget(Assist const& settings, XrVector3f const& center, XrVector3f const& axis, XrVector3f const& up,
    XrVector3f const eyes[2], float weight, int currentEye, Pull const& pull = {});
// The eye relief the pull aims for: `reliefM`, but at least the camera's near plane (`nearUnits` game units at
// `unitsPerMeter`) plus `marginM`, so the eyepiece and the tube around it aren't cut by the near plane for either eye.
float ReliefFor(float reliefM, float nearUnits, float unitsPerMeter, float marginM = 0.02f);
// With the pull, the picture's apparent size (degrees across) is what the model's eyepiece (`lensM` radius) subtends from
// `reliefM`: at the relief the picture is the eyepiece itself (the tube frames it), further back it keeps that angle
// (the virtual eyepiece's growth, PictureRadius), so it's never smaller. 0 for a bad input.
float PulledFieldDeg(float lensM, float reliefM);

// The gun's move toward the assist's goal: a critically damped spring (no overshoot, starts from rest),
// at most `maxSpeed` meters per second (Overlord's ADS comfort transition: 10/s, 0.25 m/s).
class AssistSpring {
public:
    XrVector3f Update(XrVector3f const& goal, float dt, float frequency = 10.0f, float maxSpeed = 0.25f);
    XrVector3f Value() const { return value_; }
    void Reset() { value_ = {}; velocity_ = {}; }

private:
    XrVector3f value_{};
    XrVector3f velocity_{};
};

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
    float qy = 0.0f;     //   centre (+x right, +y up): the centre of the reticle
    float pupil = 1.0f;  // the exit pupil's radius (lens radii): 1 at the eye relief, smaller further back
    float px = 0.0f;     // the exit pupil's centre (lens radii); outside it the scope's shadow. The physical lens: q
    float py = 0.0f;
    float fade = 1.0f;   // the picture's opacity (the virtual eyepiece fades out past its range)
};
// Nullopt when the eye isn't behind the lens (closer than 1 cm) or more than 6 eye reliefs back.
std::optional<ScopeView> View(Lens const& lens, XrVector3f const& eye, float eyeRelief);

// A dot's window (kWindow, research/21 section 17): the world through it at 1x, from wherever the eye is behind it (the
// picture shows each window point's true direction from the eye), with no exit pupil or shadow (a red dot has none).
// Nullopt when the eye isn't between `minAlong` and `maxAlong` behind the window.
std::optional<ScopeView> WindowView(Lens const& lens, XrVector3f const& eye, float minAlong, float maxAlong);

// The eye a dot shows to, kept through a brief dropout (research/21 section 17): a tick where the drawn gun jumps, or the
// eye a moment past the box's edge, mustn't make the dot (and its window) blink. `chosen` is AimingEye's answer (-1:
// none); a -1 within `holdMs` of the last eye keeps that eye.
class EyeHold {
public:
    static constexpr std::uint64_t kHoldMs = 120;
    int Update(int chosen, std::uint64_t nowMs, std::uint64_t holdMs = kHoldMs);
    void Reset() { eye_ = -1; }
    unsigned Bridged() const { return bridged_; }  // dropouts that ended inside the hold (for the log)

private:
    int eye_ = -1;
    std::uint64_t seenMs_ = 0;
    bool holding_ = false;
    unsigned bridged_ = 0;
};

// The virtual eyepiece (Settings::scopeFieldDeg; with the pull, PulledFieldDeg). The picture's radius on the lens plane
// for an eye `along` behind it: the radius that looks `fieldDeg` across from there, never smaller than the lens's own.
float PictureRadius(float lensRadius, float along, float fieldDeg);
struct Eyepiece {
    float fieldDeg = 22.0f;  // the picture's apparent diameter
    float eyeBox = 0.05f;    // the eye this far off the sight line: the pupil's centre a picture radius off (half shaded)
    float maxAlong = 0.70f;  // full picture up to this far behind the eyepiece; gone at 1.25 times it
};
// The view through it: `lens` is the physical eyepiece; `picture` gets the same frame with the picture's radius, and
// the view's q, p are in its radii. Nullopt when the eye is closer than 3 cm, past 1.25 maxAlong, or 2 eye boxes off.
std::optional<ScopeView> VirtualView(Lens const& lens, XrVector3f const& eye, Eyepiece const& eyepiece, Lens& picture);
// The direction the lens point (x, y) (lens radii) shows, magnified `zoom` times about the sight line
// (not unit length: its component along the axis is `view.along`).
XrVector3f LensDirection(Lens const& lens, ScopeView const& view, float x, float y, float zoom);
// The point that direction from `eye` reaches `focus` ahead along the axis.
XrVector3f FocusPoint(XrVector3f const& eye, XrVector3f const& direction, XrVector3f const& axis, float focus);
// Where `point` lands in an eye image rendered from `pose` with `fov`: u from the left edge (0) to the
// right (1), v from the top (0) to the bottom (1). False if it's behind the eye.
bool Project(XrPosef const& pose, XrFovf const& fov, XrVector3f const& point, float& u, float& v);

// ---- Shots that land on the sight (research/21 section 10) -----------------------------------------------------
// The shot from the muzzle toward the point the sight line reaches `distance` ahead of the sight: the sight's centre
// at `sightFromMuzzle` (relative to the muzzle) and its line along `axis`, all in one unit (game units). Unit length;
// `axis` itself when the distance isn't positive and finite, or when that point is less than `minAhead` in front of
// the muzzle along the line (a wall at the sight: the shot would bend sharply, or backward).
XrVector3f ConvergedShot(XrVector3f const& sightFromMuzzle, XrVector3f const& axis, float distance, float minAhead = 0.0f);
// The angle between two directions (degrees; 180 if either is zero).
float AngleDeg(XrVector3f const& a, XrVector3f const& b);

} // namespace mevr::vr::sights
