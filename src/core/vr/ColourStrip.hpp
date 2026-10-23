#pragma once

#include <cstdint>
#include <vector>

// A test picture for tuning the colour correction (core/vr/Colour, research/27) in the headset: the coming
// menu can show it on a quad, corrected like the game's picture. Six bands, top to bottom, sRGB-encoded
// 8-bit values (pixels 0xAABBGGRR, opaque, row after row):
//   1. 17 grey steps 0, 16, 32 ... 240, 255
//   2. a smooth grey ramp 0 .. 255 (banding)
//   3. 16 near-black steps 0, 2 ... 30 (the LCD's shadow floor: Meta says below 13 can't be told apart)
//   4. 16 near-white steps 225, 227 ... 255
//   5. the ColorChecker's first row: dark skin, light skin, blue sky, foliage, blue flower, bluish green
//   6. red, green, blue, cyan, magenta, yellow at full and at half (128) value
namespace mevr::vr::colour {

struct Strip {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;
};

constexpr int kStripBands = 6;

// At least 96 x 48 (smaller sizes are raised to that).
Strip TestStrip(int width = 1024, int height = 576);

} // namespace mevr::vr::colour
