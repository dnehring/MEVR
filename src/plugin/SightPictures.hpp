// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/ScopeArt.hpp"
#include "core/vr/Sights.hpp"

#include <openxr/openxr.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// What the gun's sight looks like (plugin/SightLayers draws it; tools/sight-preview renders it offline):
// the red dot / holo reticle and the window ring as premultiplied sRGB RGBA8 pixels (0xAABBGGRR), and the
// scope picture's pixel shader with its constants (the maths of core/vr/Sights LensDirection, FocusPoint
// and Project, per pixel). No D3D or OpenXR calls here.
namespace mevr::host::sight_pictures {

inline constexpr unsigned kPictureSize = 256;  // the reticle's and the window ring's pixels across
inline constexpr float kFrameScale = 1.15f;    // the window ring's quad, relative to the window

// An sRGB byte (the low 8 bits) to linear.
float Decode(std::uint32_t byte);
// Linear premultiplied colour and alpha to an sRGB-encoded premultiplied pixel.
std::uint32_t Pack(float r, float g, float b, float a);

// The reticle quad's angular size, and where the dot and the holo's ring sit in it ([-1, 1] units).
struct DotLayout {
    float quadDeg = 0.0f;
    float dotRadius = 0.0f;
    float ringRadius = 0.0f;  // 0: none
};
DotLayout LayoutFor(vr::sights::Sight const& sight);
// An emissive dot (and the holo's ring) with a soft glow, in `rgb` (0xRRGGBB); kPictureSize square.
std::vector<std::uint32_t> DotPixels(DotLayout const& layout, std::uint32_t rgb);
// The window's ring: a thin dark band with a lighter inner edge, half transparent; kPictureSize square.
std::vector<std::uint32_t> FramePixels();

// HLSL: VSMain (a full-screen triangle) and PSMain (the picture), constants in b0 (ScopeParams), the
// source eye image in t0 with a linear clamp sampler in s0.
extern char const kScopeShader[];
extern std::size_t const kScopeShaderSize;

struct ScopeParams {
    float eyeAlong[4];
    float lensCenter[4];
    float lensRight[4];
    float lensUp[4];
    float lensAxis[4];
    float source[4];
    float row0[4];
    float row1[4];
    float row2[4];
    float tanFov[4];
    float rect[4];
    float texel[4];
    float lit[4];
    float decode[4];  // x: each sample raised to this (1: already linear); research/39's 8-bit capture without an sRGB view
    float art[4];     // x: 1 = the game's scope view (research/21 section 16): its reticle layer in t1, its tube in t2; y: a
                      // picture radius in a layer's uv; z: the reticle's dark halo (0..1); w: its coverage gain
};
// The game's reticle in our picture: its lines were a pixel wide on the game's 720-line HUD, and the scope's field is fewer
// headset pixels across, so they get more coverage and a soft dark halo against bright targets.
inline constexpr float kArtHalo = 0.45f;
inline constexpr float kArtBoost = 1.8f;
static_assert(sizeof(ScopeParams) == 15 * 16);

// The constants for one picture: the aiming eye at `eye` looking through `lens` (`view`), the source eye
// image rendered from `source` with `fov`, its part `rect` of a `textureWidth` x `textureHeight` texture,
// into a `size`-pixel square. The source may also be the scope's scene capture (research/39): its own pose, its square
// frustum and the whole texture; `decodeGamma` 1 unless its samples are gamma-encoded. `gameArt`: the game's scope view is
// composed and bound (t1, t2); without it a sight asking for it gets the duplex.
ScopeParams MakeScopeParams(vr::sights::Sight const& sight, vr::sights::Lens const& lens, vr::sights::ScopeView const& view,
    XrVector3f const& eye, XrPosef const& source, XrFovf const& fov, XrRect2Di const& rect, unsigned textureWidth,
    unsigned textureHeight, unsigned size, float decodeGamma = 1.0f, bool gameArt = false);

// A dot's window (vr::sights::kWindow, research/21 section 17): the same picture at 1x with no reticle (the dot is its own
// quad), no tube rim or shadow, and an edge that fades into the housing behind it over the outer kWindowFade of its radius.
inline constexpr float kWindowStyle = 4.0f;  // the shader's style (row1.w) for a window
inline constexpr float kWindowFade = 0.15f;
ScopeParams MakeWindowParams(vr::sights::Sight const& sight, vr::sights::Lens const& lens, vr::sights::ScopeView const& view,
    XrVector3f const& eye, XrPosef const& source, XrFovf const& fov, XrRect2Di const& rect, unsigned textureWidth,
    unsigned textureHeight, unsigned size);

// ---- The game's scope view, composed (research/21 section 16, vr::scope_art) ---------------------------------------
// Once per set of textures the host draws each layer (the reticle, the tube) into a square float texture of its own,
// kArtLayerSize across with mips, from the game's three textures (t0 the atlas, t1 the mask, t2 the rings: vr::scope_art's
// order), 4x4 samples a pixel (the game's art has no mips and is minified 2.5x): premultiplied linear colour. VSMain (the
// full-screen triangle) and PSCompose, constants in b0 (ArtParams).
extern char const kArtShader[];
extern std::size_t const kArtShaderSize;
inline constexpr unsigned kArtLayerSize = 1024;
struct ArtParams {
    float info[4];    // x: the pieces, y: the output's pixels across
    float decode[4];  // x, y, z: each texture's samples raised to this (2.2: gamma-encoded UI art; 1: an sRGB view decodes)
    float pieces[vr::scope_art::kMaxPiecesPerLayer * 3][4];  // per piece: u row + texture; v row + alpha; clip (texture uv)
};
static_assert(sizeof(ArtParams) == (2 + vr::scope_art::kMaxPiecesPerLayer * 3) * 16);
// The constants for composing `layer`, each texture decoded with `decode`; info.x 0 if a piece is degenerate.
ArtParams MakeArtParams(vr::scope_art::Layer layer, float const decode[vr::scope_art::kTextures]);

} // namespace mevr::host::sight_pictures
