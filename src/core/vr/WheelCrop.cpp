#include "core/vr/WheelCrop.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::wheel_crop {
namespace {

constexpr float kStageWidth = 1280.0f;
constexpr float kStageHeight = 720.0f;
// The union of ME_HUD's squadMC (298, 129)-(982, 603) and PC_ME_HUD's (357, 77)-(914, 633), stage
// pixels, plus 16 around it.
constexpr float kLeft = 282.0f;
constexpr float kTop = 61.0f;
constexpr float kRight = 998.0f;
constexpr float kBottom = 649.0f;

Rect Clip(Rect r) {
    float const x0 = std::clamp(r.x, 0.0f, 1.0f);
    float const y0 = std::clamp(r.y, 0.0f, 1.0f);
    float const x1 = std::clamp(r.x + r.width, 0.0f, 1.0f);
    float const y1 = std::clamp(r.y + r.height, 0.0f, 1.0f);
    return {x0, y0, std::max(x1 - x0, 0.0f), std::max(y1 - y0, 0.0f)};
}

} // namespace

Rect DefaultCrop(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return {};
    }
    float const w = static_cast<float>(width);
    float const h = static_cast<float>(height);
    float const scale = std::min(w / kStageWidth, h / kStageHeight);
    return Clip({kLeft * scale / w, kTop * scale / h, (kRight - kLeft) * scale / w, (kBottom - kTop) * scale / h});
}

std::optional<Measured> MeasureCrop(Grid const& before, Grid const& wheel, std::uint8_t threshold, std::uint32_t minCells) {
    if (!before.Valid() || !wheel.Valid() || before.columns != wheel.columns || before.rows != wheel.rows) {
        return std::nullopt;
    }
    std::uint32_t const columns = wheel.columns;
    std::uint32_t const rows = wheel.rows;
    size_t const count = wheel.cells.size();
    std::vector<std::uint8_t> changed(count, 0);
    std::uint32_t changedCount = 0;
    for (size_t i = 0; i < count; ++i) {
        if (static_cast<int>(wheel.cells[i]) - static_cast<int>(before.cells[i]) >= static_cast<int>(threshold)) {
            changed[i] = 1;
            ++changedCount;
        }
    }
    // The largest 4-connected group of changed cells.
    std::vector<std::int32_t> label(count, -1);
    std::vector<std::uint32_t> stack;
    std::uint32_t bestSize = 0;
    std::uint32_t bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    std::int32_t next = 0;
    for (size_t start = 0; start < count; ++start) {
        if (changed[start] == 0 || label[start] >= 0) {
            continue;
        }
        std::uint32_t size = 0;
        std::uint32_t x0 = columns, y0 = rows, x1 = 0, y1 = 0;
        stack.assign(1, static_cast<std::uint32_t>(start));
        label[start] = next;
        while (!stack.empty()) {
            std::uint32_t const i = stack.back();
            stack.pop_back();
            ++size;
            std::uint32_t const x = i % columns;
            std::uint32_t const y = i / columns;
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
            auto const visit = [&](std::uint32_t nx, std::uint32_t ny) {
                std::uint32_t const j = ny * columns + nx;
                if (changed[j] != 0 && label[j] < 0) {
                    label[j] = next;
                    stack.push_back(j);
                }
            };
            if (x > 0) visit(x - 1, y);
            if (x + 1 < columns) visit(x + 1, y);
            if (y > 0) visit(x, y - 1);
            if (y + 1 < rows) visit(x, y + 1);
        }
        if (size > bestSize) {
            bestSize = size;
            bx0 = x0;
            by0 = y0;
            bx1 = x1;
            by1 = y1;
        }
        ++next;
    }
    if (bestSize < minCells) {
        return std::nullopt;
    }
    // One cell of padding around the group, as fractions of the frame.
    float const cw = 1.0f / static_cast<float>(columns);
    float const ch = 1.0f / static_cast<float>(rows);
    float const left = (static_cast<float>(bx0) - 1.0f) * cw;
    float const top = (static_cast<float>(by0) - 1.0f) * ch;
    float const right = (static_cast<float>(bx1) + 2.0f) * cw;
    float const bottom = (static_cast<float>(by1) + 2.0f) * ch;
    Measured measured;
    measured.rect = Clip({left, top, right - left, bottom - top});
    measured.cells = bestSize;
    measured.changed = changedCount;
    return measured;
}

Pixels ToPixels(Rect const& rect, std::uint32_t width, std::uint32_t height) {
    if (width < 16 || height < 16) {
        return {0, 0, width, height};
    }
    Rect const r = Clip(rect);
    auto const px = [](float f, std::uint32_t size) {
        return static_cast<std::uint32_t>(std::clamp(std::lround(f * static_cast<float>(size)), 0L, static_cast<long>(size)));
    };
    std::uint32_t x0 = px(r.x, width);
    std::uint32_t y0 = px(r.y, height);
    std::uint32_t x1 = px(r.x + r.width, width);
    std::uint32_t y1 = px(r.y + r.height, height);
    if (x1 < x0 + 16) {
        x1 = std::min(x0 + 16, width);
        x0 = x1 - 16;
    }
    if (y1 < y0 + 16) {
        y1 = std::min(y0 + 16, height);
        y0 = y1 - 16;
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

} // namespace mevr::vr::wheel_crop
