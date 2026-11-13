#pragma once

#include "thesis/gpu/Buffer.h"

#include <cstdint>

namespace thesis::gpu {
class Context;
}

namespace thesis::tracing {

// The two lists that pass through the tracer socket: rays in, hits out (same index).
// Rays are laid out as an image (row-major, width × height) so hits can be shown on screen,
// but tracers treat both as flat lists and never assume where the rays came from.
struct RayBatch {
    gpu::Buffer rays; // core::Ray[count()]
    gpu::Buffer hits; // core::Hit[count()]
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] static RayBatch create(const gpu::Context& context, std::uint32_t width, std::uint32_t height);
    [[nodiscard]] std::uint32_t count() const { return width * height; }
    [[nodiscard]] std::uint64_t memoryBytes() const { return rays.size() + hits.size(); }
};

} // namespace thesis::tracing
