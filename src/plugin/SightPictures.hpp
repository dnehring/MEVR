#pragma once

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
};
static_assert(sizeof(ScopeParams) == 13 * 16);

// The constants for one picture: the aiming eye at `eye` looking through `lens` (`view`), the source eye
// image rendered from `source` with `fov`, its part `rect` of a `textureWidth` x `textureHeight` texture,
// into a `size`-pixel square.
ScopeParams MakeScopeParams(vr::sights::Sight const& sight, vr::sights::Lens const& lens, vr::sights::ScopeView const& view,
    XrVector3f const& eye, XrPosef const& source, XrFovf const& fov, XrRect2Di const& rect, unsigned textureWidth,
    unsigned textureHeight, unsigned size);

} // namespace mevr::host::sight_pictures
