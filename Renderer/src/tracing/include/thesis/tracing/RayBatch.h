#pragma once

// Side: GPU

#include "thesis/gpu/Buffer.h"
#include "thesis/shared/RayOrder.h"

#include <cstdint>

namespace thesis::gpu {
class Context;
}

namespace thesis::tracing {

// How the pixels of the image are laid out in the ray list (thesis/shared/RayOrder.h).
enum class RayOrder : std::uint32_t {
    Rows = THESIS_RAY_ORDER_ROWS,   // row by row
    Tiles = THESIS_RAY_ORDER_TILES, // tiles of 8 x 4 pixels, one tile per warp
};

// The two lists that pass through the tracer socket: rays in, hits out (same index).
// Rays belong to the pixels of a width × height image, in `order`, so hits can be shown on
// screen. Tracers treat both as flat lists and never assume where the rays came from.
struct RayBatch {
    gpu::Buffer rays; // core::Ray[count()]
    gpu::Buffer hits; // core::Hit[count()]
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    RayOrder order = RayOrder::Rows;

    [[nodiscard]] static RayBatch create(const gpu::Context& context, std::uint32_t width, std::uint32_t height,
                                         RayOrder order = RayOrder::Rows);
    [[nodiscard]] std::uint32_t count() const { return width * height; }
    // The index of the ray (and hit) of pixel (x, y).
    [[nodiscard]] std::uint32_t rayOf(std::uint32_t x, std::uint32_t y) const
    {
        return THESIS_RAY_INDEX(static_cast<std::uint32_t>(order), x, y, width, height);
    }
    [[nodiscard]] std::uint64_t memoryBytes() const { return rays.size() + hits.size(); }
};

} // namespace thesis::tracing
