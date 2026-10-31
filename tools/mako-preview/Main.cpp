// SPDX-License-Identifier: MIT
// mevr-mako-preview <folder>: the Mako's lever and stick (research/46) as PNGs with the host's own pixels, over a mid-grey and
// a dark ground, each state side by side: idle, a hand in reach, held; the lever at neutral, half forward, full reverse; the
// stick centred, half right, full lock left.
#include "core/PngFile.hpp"
#include "core/vr/MakoControls.hpp"

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace mc = mevr::vr::mako_controls;

constexpr int kGap = 12;

struct Sheet {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;

    Sheet(int w, int h) : width(w), height(h), pixels(static_cast<std::size_t>(w) * h, 0xFF000000u) {
        for (int y = 0; y < h; ++y) {
            std::uint32_t const grey = y < h / 2 ? 0x70u : 0x18u;  // both grounds: it must read on either
            for (int x = 0; x < w; ++x) {
                pixels[static_cast<std::size_t>(y) * w + x] = 0xFF000000u | (grey << 16) | (grey << 8) | grey;
            }
        }
    }
    // The picture is premultiplied in encoded values: composited as the runtime would for a preview's purposes.
    void Put(int x0, int y0, int w, int h, std::vector<std::uint32_t> const& image) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                std::uint32_t const p = image[static_cast<std::size_t>(y) * w + x];
                std::uint32_t& d = pixels[static_cast<std::size_t>(y0 + y) * width + x0 + x];
                std::uint32_t const a = p >> 24;
                auto const over = [a](std::uint32_t top, std::uint32_t under) {
                    return std::min<std::uint32_t>(255u, top + under * (255u - a) / 255u);
                };
                d = 0xFF000000u | over(p & 255u, d & 255u) | (over((p >> 8) & 255u, (d >> 8) & 255u) << 8) |
                    (over((p >> 16) & 255u, (d >> 16) & 255u) << 16);
            }
        }
    }
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: mevr-mako-preview <folder>\n");
        return 2;
    }
    std::filesystem::path const folder = argv[1];
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    struct State {
        bool inReach;
        bool held;
    };
    State const states[] = {{false, false}, {true, false}, {true, true}};
    float const levers[] = {0.0f, 0.5f, -1.0f};
    float const steers[] = {0.0f, 0.5f, -1.0f};
    int const leverW = static_cast<int>(mc::kLeverPixelsWide);
    int const leverH = static_cast<int>(mc::kLeverPixelsLong);
    int const stickS = static_cast<int>(mc::kStickPixels);
    int const columns = 3;
    int const width = kGap + (leverW + kGap) * columns * 3 + (stickS + kGap) * columns * 3;
    int const height = 2 * (leverH + 2 * kGap);
    Sheet sheet(width, height);
    int x = kGap;
    for (State const& state : states) {
        for (float const lever : levers) {
            std::vector<std::uint32_t> const image = mc::LeverPixels(lever, 0.15f, 0.08f, state.inReach, state.held);
            sheet.Put(x, kGap, leverW, leverH, image);
            sheet.Put(x, height / 2 + kGap, leverW, leverH, image);
            x += leverW + kGap;
        }
    }
    for (State const& state : states) {
        for (float const steer : steers) {
            std::vector<std::uint32_t> const image = mc::StickPixels(steer, state.inReach, state.held);
            sheet.Put(x, kGap, stickS, stickS, image);
            sheet.Put(x, height / 2 + kGap, stickS, stickS, image);
            x += stickS + kGap;
        }
    }
    std::string error;
    std::filesystem::path const path = folder / "mako-controls.png";
    if (!mevr::png::Write(path, width, height, sheet.pixels.data(), &error)) {
        std::fprintf(stderr, "couldn't write %s: %s\n", path.string().c_str(), error.c_str());
        return 1;
    }
    std::printf("wrote %s\n", path.string().c_str());
    return 0;
}
