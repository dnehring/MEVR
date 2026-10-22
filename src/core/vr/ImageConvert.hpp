#pragma once

#include <cstdint>

// Converting what a swapchain image holds into 8-bit sRGB RGBA for PNG snapshots
// (plugin/TestLoop). Pure; tested in tests/Main.cpp. Formats are DXGI_FORMAT numbers, so this
// needs no D3D headers.
//
// Output pixels are straight (not premultiplied) alpha, sRGB-encoded, R in the low byte
// (0xAABBGGRR), which is what PNG wants.
namespace mevr::vr::image {

namespace format {
inline constexpr int kR16G16B16A16Typeless = 9;
inline constexpr int kR16G16B16A16Float = 10;
inline constexpr int kR10G10B10A2Typeless = 23;
inline constexpr int kR10G10B10A2Unorm = 24;
inline constexpr int kR8G8B8A8Typeless = 27;
inline constexpr int kR8G8B8A8Unorm = 28;
inline constexpr int kR8G8B8A8UnormSrgb = 29;
inline constexpr int kB8G8R8A8Unorm = 87;
inline constexpr int kB8G8R8A8Typeless = 90;
inline constexpr int kB8G8R8A8UnormSrgb = 91;
} // namespace format

// The formats ConvertRow reads: 8-bit RGBA/BGRA (UNORM, sRGB, typeless), 10:10:10:2 and FP16.
bool Supported(int dxgiFormat);
// Bytes per pixel of a supported format, 0 otherwise.
int BytesPerPixel(int dxgiFormat);
// FP16 (scRGB) formats: linear values, 1.0 = SDR white times `whiteScale`'s inverse.
bool IsFloat(int dxgiFormat);

float HalfToFloat(std::uint16_t half);
float SrgbToLinear(float encoded);
float LinearToSrgb(float linear);
// Rounded and clamped to 0..255.
std::uint8_t LinearToSrgb8(float linear);

struct Options {
    // The colour is premultiplied by alpha (an OpenXR layer blended with source alpha, without
    // XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT): divided out, in linear light.
    bool premultiplied = false;
    // Alpha ignored (a layer without blending): written as 255.
    bool opaque = false;
    // FP16 only: multiplies the linear values (maps the source's SDR white to 1.0).
    float whiteScale = 1.0f;
};

// Converts one row of `width` pixels. 8-bit and 10-bit values are taken as already display-encoded
// (sRGB), whatever the view format says: that is what Halcyon writes into every 8-bit image (UNORM
// backbuffer bytes copied into sRGB swapchains). FP16 is linear and gets encoded. False for an
// unsupported format (nothing written).
bool ConvertRow(int dxgiFormat, void const* source, int width, Options const& options, std::uint32_t* out);

// Straight-alpha sRGB pixel helpers (0xAABBGGRR).
inline std::uint32_t Pack(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(b) << 16) |
        (static_cast<std::uint32_t>(a) << 24);
}

// `source` over `destination`, blended in linear light (what a compositor does with sRGB
// layers); both straight alpha.
std::uint32_t Over(std::uint32_t destination, std::uint32_t source);

} // namespace mevr::vr::image
