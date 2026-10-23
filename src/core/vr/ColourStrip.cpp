// SPDX-License-Identifier: MIT
#include "core/vr/ColourStrip.hpp"

#include <algorithm>
#include <array>

namespace mevr::vr::colour {
namespace {

std::uint32_t Pixel(int r, int g, int b) {
    auto byte = [](int v) { return static_cast<std::uint32_t>(std::clamp(v, 0, 255)); };
    return byte(r) | (byte(g) << 8) | (byte(b) << 16) | 0xFF000000u;
}

std::uint32_t Grey(int v) {
    return Pixel(v, v, v);
}

struct Patch {
    int r, g, b;
};

// The ColorChecker Classic's first row, sRGB (BabelColor's averaged values, rounded).
constexpr std::array<Patch, 6> kChecker{{
    {115, 82, 68},    // dark skin
    {194, 150, 130},  // light skin
    {98, 122, 157},   // blue sky
    {87, 108, 67},    // foliage
    {133, 128, 177},  // blue flower
    {103, 189, 170},  // bluish green
}};

constexpr std::array<Patch, 12> kPrimaries{{
    {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {0, 255, 255}, {255, 0, 255}, {255, 255, 0},
    {128, 0, 0}, {0, 128, 0}, {0, 0, 128}, {0, 128, 128}, {128, 0, 128}, {128, 128, 0},
}};

} // namespace

Strip TestStrip(int width, int height) {
    Strip s;
    s.width = std::max(width, 96);
    s.height = std::max(height, 48);
    s.pixels.assign(static_cast<std::size_t>(s.width) * s.height, Grey(0));
    for (int y = 0; y < s.height; ++y) {
        int const band = std::min(y * kStripBands / s.height, kStripBands - 1);
        for (int x = 0; x < s.width; ++x) {
            std::uint32_t p = Grey(0);
            switch (band) {
            case 0: {
                int const step = std::min(x * 17 / s.width, 16);
                p = Grey(std::min(step * 16, 255));
                break;
            }
            case 1:
                p = Grey((x * 255 + (s.width - 1) / 2) / (s.width - 1));
                break;
            case 2:
                p = Grey(std::min(x * 16 / s.width, 15) * 2);
                break;
            case 3:
                p = Grey(225 + std::min(x * 16 / s.width, 15) * 2);
                break;
            case 4: {
                Patch const& c = kChecker[std::min<std::size_t>(static_cast<std::size_t>(x) * kChecker.size() / s.width, 5)];
                p = Pixel(c.r, c.g, c.b);
                break;
            }
            default: {
                Patch const& c = kPrimaries[std::min<std::size_t>(static_cast<std::size_t>(x) * kPrimaries.size() / s.width, 11)];
                p = Pixel(c.r, c.g, c.b);
                break;
            }
            }
            s.pixels[static_cast<std::size_t>(y) * s.width + x] = p;
        }
    }
    return s;
}

} // namespace mevr::vr::colour
