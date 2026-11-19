// SPDX-License-Identifier: MIT
#include "core/vr/RenderTarget.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace mevr::vr::render_target {

namespace ms = menu_settings;

int RoundToMultiple(float value, int multiple) {
    int const m = std::max(multiple, 1);
    if (!std::isfinite(value) || value < static_cast<float>(m)) {
        return m;
    }
    return m * static_cast<int>(std::lround(value / static_cast<float>(m)));
}

Size HeadsetSize(Shape shape, std::string_view key, int recommendedWidth, int recommendedHeight) {
    if (recommendedWidth <= 0 || recommendedHeight <= 0) {
        return {};
    }
    for (ms::HeadsetChoice const& choice : ms::HeadsetChoices()) {
        if (key != choice.key) {
            continue;
        }
        float const w = static_cast<float>(recommendedWidth) * choice.scale;
        float const h = static_cast<float>(recommendedHeight) * choice.scale;
        int const height = RoundToMultiple(h, shape.multiple);
        if (shape.minAspect > 0.0f && w < h * shape.minAspect) {
            // Narrower than the engine draws its frame: widened to that shape at the chip's height (no axis below the chip).
            int const width = RoundToMultiple(static_cast<float>(height) * shape.minAspect, shape.multiple);
            return {std::max(width, RoundToMultiple(w, shape.multiple)), height};
        }
        return {RoundToMultiple(w, shape.multiple), height};
    }
    return {};
}

std::string ResolutionChoice(Shape shape, int width, int height, int recommendedWidth, int recommendedHeight) {
    for (ms::HeadsetChoice const& choice : ms::HeadsetChoices()) {
        Size const size = HeadsetSize(shape, choice.key, recommendedWidth, recommendedHeight);
        if (size.width > 0 && size == Size{width, height}) {
            return choice.key;
        }
    }
    return "";
}

std::string HeadsetSizesText(Shape shape, int recommendedWidth, int recommendedHeight) {
    if (recommendedWidth <= 0 || recommendedHeight <= 0) {
        return "Your headset's own eye size, known once VR starts";
    }
    Size const full = HeadsetSize(shape, "headset", recommendedWidth, recommendedHeight);
    Size const s85 = HeadsetSize(shape, "headset85", recommendedWidth, recommendedHeight);
    Size const s75 = HeadsetSize(shape, "headset75", recommendedWidth, recommendedHeight);
    return std::format("Headset {} \xC2\xB7 \xC3\x97 0.85: {} \xC2\xB7 \xC3\x97 0.75: {}", ms::SizeText(full.width, full.height),
        ms::SizeText(s85.width, s85.height), ms::SizeText(s75.width, s75.height));
}

Size FirstStartSize(Shape shape, bool alreadySet, Size current, int recommendedWidth, int recommendedHeight) {
    if (alreadySet) {
        return {};
    }
    Size const size = HeadsetSize(shape, "headset85", recommendedWidth, recommendedHeight);  // ME1's 0.85, FSR on
    if (size.width <= 0 || size.height <= 0 || size == current) {
        return {};
    }
    return size;
}

} // namespace mevr::vr::render_target
