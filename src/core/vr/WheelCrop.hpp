#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// Where the game's weapon and power wheels are in the UI frame (the HUD panel texture), so the host can
// show just them on a panel at the hand (plugin/HalcyonOverlays, docs/squad-weapons-design.md). Pure;
// unit-tested in tests/Main.cpp.
//
// The HUD movie (GUI_SF_HUD.ME_HUD / PC_ME_HUD) is a 1280x720 stage; its wheel is the clip `squadMC`
// centred on the stage, about 684x474 (console movie) or 557x556 (PC movie) stage pixels. How the
// stage maps onto the backbuffer depends on the movie's scale mode, which isn't known for certain: the
// movie sets Stage.align = "TL" and snaps its edge groups to Stage.visibleRect, which fits "show all".
// So the host starts from DefaultCrop and measures the real place the first time a wheel opens
// (MeasureCrop: what appears in the UI when the wheel opens, compared with the HUD just before).
namespace mevr::vr::wheel_crop {

// A rectangle as fractions of the frame: x, y (top left), width, height.
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool Valid() const { return width > 0.0f && height > 0.0f; }
};

// The wheel's part of a frame of `width` x `height` pixels, if the stage is shown whole ("show all"),
// aligned top left: the union of both movies' wheel clips plus a margin, clipped to the frame.
Rect DefaultCrop(std::uint32_t width, std::uint32_t height);

// A grid of the UI's coverage (each cell the most opaque pixel in it, 0..255), `columns` x `rows`
// cells over the whole frame, row by row.
struct Grid {
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::vector<std::uint8_t> cells;
    bool Valid() const { return columns > 0 && rows > 0 && cells.size() == static_cast<size_t>(columns) * rows; }
};

struct Measured {
    Rect rect;            // padded by one cell, clipped to the frame
    std::uint32_t cells = 0;  // the changed cells in the part taken
    std::uint32_t changed = 0;  // all changed cells
};

// What appeared when the wheel opened: the cells at least `threshold` more opaque in `wheel` than in
// `before`, the largest 4-connected group of them (stray HUD changes are smaller), its bounding box.
// Nullopt if the grids don't match, or the group is smaller than `minCells`.
std::optional<Measured> MeasureCrop(Grid const& before, Grid const& wheel, std::uint8_t threshold = 40,
    std::uint32_t minCells = 12);

// The crop as whole pixels of a width x height frame (at least 16x16, inside the frame).
struct Pixels {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};
Pixels ToPixels(Rect const& rect, std::uint32_t width, std::uint32_t height);

} // namespace mevr::vr::wheel_crop
