// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// Draws OpenXR quad layers into an eye image the way the compositor shows them, so a snapshot
// (plugin/TestLoop) shows what the eye sees: the world plus the HUD panel, the reticle and the
// menu. Nearest sampling, blending in linear light. OpenXR conventions: meters, +Y up, -Z forward;
// a quad faces along its +Z, its texture's top row at +Y. Pure; tested in tests/Main.cpp.
namespace mevr::vr::composite {

// Straight-alpha sRGB RGBA8 pixels, 0xAABBGGRR (vr::image's output).
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;
    std::uint32_t At(int x, int y) const { return pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + x]; }
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// The eye an image was rendered for: its pose (in the space the quads are given in) and its FOV.
struct Eye {
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrFovf fov{};
};

struct Quad {
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};  // center, in the eye's space
    float width = 0.0f;   // meters
    float height = 0.0f;
    Image const* image = nullptr;
    Rect sub;            // the part of `image` the layer shows (empty: all of it)
    bool blend = true;   // blended by alpha; false: opaque
};

// Eye-space direction (not unit length, z = -1) through the point (u, v) of the image: u from the
// left edge (0) to the right (1), v from the top (0) to the bottom (1).
XrVector3f EyeRay(XrFovf const& fov, float u, float v);

// Where the ray meets the quad's front: texture coordinates u (0 left, 1 right) and v (0 top,
// 1 bottom). False if it misses, runs parallel, points away, or hits it from behind.
bool HitQuad(Quad const& quad, XrVector3f const& origin, XrVector3f const& direction, float& u, float& v);

// Draws `quad` into `target`, an image rendered for `eye` covering its whole FOV. Returns the number
// of pixels it covered.
std::size_t Draw(Image& target, Eye const& eye, Quad const& quad);

} // namespace mevr::vr::composite
