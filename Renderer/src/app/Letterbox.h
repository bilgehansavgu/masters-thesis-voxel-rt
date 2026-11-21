#pragma once

// Side: CPU

#include <algorithm>
#include <cstdint>

namespace thesis::app {

// A rectangle of window pixels: corner (x0, y0) inclusive, (x1, y1) exclusive.
struct PixelRect {
    std::int32_t x0 = 0;
    std::int32_t y0 = 0;
    std::int32_t x1 = 0;
    std::int32_t y1 = 0;

    [[nodiscard]] std::int32_t width() const { return x1 - x0; }
    [[nodiscard]] std::int32_t height() const { return y1 - y0; }
    [[nodiscard]] bool empty() const { return width() <= 0 || height() <= 0; }
};

// The largest rectangle with the aspect ratio of a `width` x `height` image that fits in `area`,
// centred: where the picture of the frame is. The presenter blits there, and the panel maps the
// mouse and the overlay with the same rectangle.
[[nodiscard]] inline PixelRect letterbox(std::uint32_t width, std::uint32_t height, PixelRect area)
{
    const float scale = std::min(static_cast<float>(area.width()) / static_cast<float>(width),
                                 static_cast<float>(area.height()) / static_cast<float>(height));
    const auto w = static_cast<std::int32_t>(static_cast<float>(width) * scale);
    const auto h = static_cast<std::int32_t>(static_cast<float>(height) * scale);
    const std::int32_t x = area.x0 + (area.width() - w) / 2;
    const std::int32_t y = area.y0 + (area.height() - h) / 2;
    return {x, y, x + w, y + h};
}

} // namespace thesis::app
