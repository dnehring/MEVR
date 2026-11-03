// SPDX-License-Identifier: MIT
#pragma once

#include "core/Ini.hpp"
#include "core/vr/InteractLabel.hpp"

#include <openxr/openxr.h>

#include <cstdint>
#include <string>

// HUD pieces on the body (research/41): two parts of the game's HUD cut out of the body-locked HUD panel and shown
// where a VR player looks for them. The player's status (the health and shield arc at the bottom centre of the HUD,
// with the squad's portraits and bars and the medi-gel count) rides on the back of the LEFT wrist, a small quad a
// fixed offset from the left grip pose, facing the eyes; the weapon panel (the gun's silhouette, its heat strip and
// the grenade count, at the bottom left of the HUD) rides ON THE GUN, above the receiver behind the sights, where
// the mod says the held gun is (HostServices v22, setHudPieces). Both are masked out of the HUD panel meanwhile, so
// they show once. Shared by the host (plugin/HalcyonOverlays: settings, crops, placement, the quads) and the mod
// (game/HudPieces: the gun anchor); the pure parts are unit-tested in tests/Main.cpp.
//
// The HUD frame is the game's near-square backbuffer (e.g. 2344x2392); the Scaleform HUD's 1280-wide stage is shown
// across its width, aligned to the top, so the frame is about 1306 stage pixels tall and the HUD's bottom pieces are
// anchored to the frame's BOTTOM edge at a constant offset in stage pixels (measured on 591 HUD snapshots of four
// frame sizes, 2026-10-31: the same offsets within 3 px on 1059x1080, 1172x1196, 1412x1440 and 1536x1594 frames).
namespace mevr::vr::hud_pieces {

using interact::Pixels;
using interact::Quad;
using interact::Rect;
using interact::ToPixels;

// What the mod publishes each tick (mod -> host, HostServices::setHudPieces).
struct Pieces {
    std::uint32_t size = sizeof(Pieces);  // the publisher's struct size: host and mod must match
    bool gunVisible = false;     // a weapon is drawn on foot and aimed along its barrel: the weapon panel goes on it
    XrVector3f gunAnchor{};      // tracking space (meters): where the weapon panel's centre goes
    XrVector3f gunUp{};          // unit, the gun's top at the anchor (the quad leans toward it; zero: world up)
    std::uint32_t serial = 0;    // changes with the held weapon
    // The cockpit rides in the Mako (research/47, docs/mako-cockpit-goal.md item 8): the vehicle's status (the weapon panel's
    // band, where the Mako's silhouette is) and the radar go on the cockpit's displays, the rest of the HUD panel to
    // `panelAnchor`, none of them over the screens. Tracking space (meters), each quad facing the eyes, upright to `cockpitUp`.
    bool cockpit = false;
    XrVector3f cockpitUp{};
    XrVector3f statusAnchor{};
    float statusWidthM = 0.0f;
    XrVector3f radarAnchor{};
    float radarWidthM = 0.0f;
    XrVector3f panelAnchor{};
    float panelWidthM = 0.0f;
    std::uint64_t sequence = 0;  // set by Publish
    std::uint64_t timeMs = 0;    // GetTickCount64 when published
};

void Publish(Pieces const& pieces);
// The latest; `gunVisible` false (and size 0) if none was published within `maxAgeMs` (the mod stopped).
Pieces Latest(std::uint64_t maxAgeMs = 300);
void Reset();

// [Hud] in ASI\LE1MEVR.ini (the host reads it for the quads; the mod for the gun anchor and the file's block).
struct Settings {
    bool wristStatus = true;   // the player's status on the left wrist
    bool gunStatus = true;     // the weapon panel on the gun
    float wristWidthM = 0.08f;  // the wrist quad's width (meters)
    float gunWidthM = 0.06f;    // the gun quad's width (meters)
    // Where the wrist quad's centre sits, in the LEFT grip pose's axes (meters): +X out of the palm (the back of the
    // left hand is -X), +Y toward the elbow for a handle held upright, +Z down the handle away from the thumb. The
    // default is 6 cm up the forearm, 5 cm down the handle and 3 cm out over the back of the wrist.
    XrVector3f wristOffset{-0.03f, 0.06f, 0.05f};
    float gunBackM = 0.15f;  // the gun anchor: this far behind the muzzle along the barrel...
    float gunUpM = 0.04f;    // ...and this far above the bore toward the gun's top
    Rect wristCrop;  // the status band by hand (fractions of the HUD frame); width 0: the measured default
    Rect gunCrop;    // the weapon panel's band by hand; width 0: the measured default
};

// The [Hud] section of a settings file: missing keys keep their defaults; a bad value is described in `problems`
// (one line each, empty if none) and ignored.
Settings ReadSettings(ini::File const& file, std::string* problems = nullptr);
// A short line for the log.
std::string Describe(Settings const& settings);

// The measured bands of a `width` x `height` HUD frame, anchored to its bottom edge (stage pixels: 1280 across the
// width; from the bottom). The player's status: x 440..840, the bottom 165..25 (the arc spans 459..817 and the bottom
// 152..34 at alpha > 40; the portraits' bars reach 444..832). The weapon panel: x 56..224, the bottom 145..10 (the
// gun, heat strip and grenade count span 69..200 and the bottom 133..40; the panel's bracket reaches 64..220 and
// the bottom 12). Invalid (width 0) for an empty frame.
Rect DefaultWristCrop(std::uint32_t width, std::uint32_t height);
Rect DefaultGunCrop(std::uint32_t width, std::uint32_t height);
// The Mako's radar at the bottom right: x 1080..1222, the bottom 172..28 (stage pixels).
Rect DefaultRadarCrop(std::uint32_t width, std::uint32_t height);
// `crop` if it's valid, else the default for the frame.
Rect WristCrop(Settings const& settings, std::uint32_t width, std::uint32_t height);
Rect GunCrop(Settings const& settings, std::uint32_t width, std::uint32_t height);

// A quad `widthM` wide (its height from the crop's `aspect`, width / height) centred at `position`, its front
// facing `head`, upright (no roll; `up` leans it: zero or along the facing, the world's). False if degenerate.
bool Place(XrVector3f const& position, XrVector3f const& head, float widthM, float aspect, Quad& out,
    XrVector3f const& up = {0.0f, 0.0f, 0.0f});
// Where the wrist quad's centre goes: `offset` (grip axes, meters) from the grip pose.
XrVector3f WristPoint(XrPosef const& grip, XrVector3f const& offset);
// Where the gun quad's centre goes: `backM` behind the muzzle along `direction` (unit), `upM` toward `top` (unit,
// perpendicular to the direction; the gun's top).
XrVector3f GunPoint(XrVector3f const& muzzle, XrVector3f const& direction, XrVector3f const& top, float backM, float upM);

// "a b c ..." or "a, b, c": exactly `count` numbers into `out`; false otherwise.
bool ParseNumbers(std::string_view text, int count, float* out);

} // namespace mevr::vr::hud_pieces
