// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <span>

// The game's own sniper scope view on our scope picture (research/21 section 16). LE1's HUD reticle movie
// (GUI_SF_HUD.Reticule, Startup_INT.pcc: always loaded) draws the zoomed sniper rifle's view in its `SniperMode` clip from
// three textures: a dirty lens mask, the cyan reticle cut from one atlas (some pieces mirrored), and the tube's rings on
// top. tools/scope-art.py reads the clip's settled display list from the movie and prints the pieces below (our numbers:
// where each piece goes, in the movie's 1280x720 stage pixels; no game data here). The mod finds the three textures in the
// running game and publishes them with the sight (vr::sights::Sight::art); the host composes two layers from them once
// (plugin/SightLayers, the shader in plugin/SightPictures): the reticle, centred on the sight line through the aiming eye
// like any reticle of ours, and the tube (mask and rings), centred on the eyepiece. The scope picture samples both.
//
// Engine-agnostic, pure, unit-tested in tests/Main.cpp.
namespace mevr::vr::scope_art {

// The three textures, all PF_DXT5, no mips, SRGB off (gamma-encoded UI art), NeverStream (fully resident).
enum class Texture : std::int32_t {
    I4E = 0,  // Reticule_I4E, the reticle's atlas (1024 x 1024)
    I4C = 1,  // Reticule_I4C, the lens mask: black outside the field, specks and scratches in it (2048 x 2048)
    I41 = 2,  // Reticule_I41, the tube's rings around a clear disc (2048 x 2048)
};
inline constexpr int kTextures = 3;
struct TextureInfo {
    char const* name;        // the Texture2D's object name, in package `kPackage`
    std::uint32_t width;     // the movie's logical size of the image (its DefineExternalImage2 target size) = the texture's
    std::uint32_t height;
};
inline constexpr char const* kPackage = "GUI_SF_HUD";
TextureInfo const& Info(Texture texture);
TextureInfo const& Info(int index);  // 0..kTextures-1

enum class Layer : std::int32_t { Reticle = 0, Tube = 1 };
inline constexpr int kLayers = 2;

// One piece of the settled view: its texels (the image's logical size) to the movie's pixels, x' = a u + c v + tx,
// y' = b u + d v + ty (SWF's matrix order: sx, r0, r1, sy, tx, ty), the texels it covers (u0, v0, u1, v1), and its
// alpha. Drawn in this order, each over the last.
struct Piece {
    Texture texture;
    Layer layer;
    float map[6];
    float clip[4];
    float alpha;
};
std::span<Piece const> Pieces();
inline constexpr int kMaxPiecesPerLayer = 12;

// The settled view in the movie's pixels (tools/scope-art.py's measurement): the field (the mask's clear disc) and the
// reticle's centre (the post's column, the stems' row).
inline constexpr float kFieldX = 6.85f;
inline constexpr float kFieldY = 6.62f;
inline constexpr float kFieldRadius = 223.1f;
inline constexpr float kReticleX = 0.86f;
inline constexpr float kReticleY = 1.31f;
// Our picture's edge in the movie's pixels: the field fills this share of the picture's radius, the tube's rings the
// rest (our own picture's dark rim began at 0.84).
inline constexpr float kFieldShare = 0.89f;
inline constexpr float kPictureRadius = kFieldRadius / kFieldShare;

// Each layer is composed into a square texture this many movie pixels across, centred on its origin: the picture's
// diameter (a picture point r = 1 from the origin is on the texture's edge).
inline constexpr float kLayerSpan = 2.0f * kPictureRadius;
struct Point {
    float x = 0.0f;
    float y = 0.0f;
};
// Where a layer's texture is centred (movie pixels): the reticle's centre, the field's centre.
Point Origin(Layer layer);

// A picture point (picture radii from the layer's origin: x right, y up) in the layer texture's uv (0..1, v down).
Point PictureToLayer(float x, float y);

// For composing a layer: a piece's map from the layer texture's uv (U, V: 0..1 across kLayerSpan, v down) to its own
// texture's uv (0..1 over its logical size), u' = u[0] U + u[1] V + u[2], v' = v[0] U + v[1] V + v[2], and the part of
// its texture it covers, in that uv. False for a degenerate piece.
struct Map {
    float u[3];
    float v[3];
    float clip[4];
};
bool LayerToTexture(Piece const& piece, Map& out);

} // namespace mevr::vr::scope_art
