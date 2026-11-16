#pragma once

// ============================================================================
// READING ORDER: FILE 20A
// ============================================================================

#include "thesis/gpu/Buffer.h"

#include <cstdint>

namespace thesis::gpu {
class Context; // Forward declaration: avoids including all of Context.h when we only need a reference
}

namespace thesis::tracing {

// The two lists that pass through the tracer socket: rays in, hits out (same array index).
//
// `rays` and `hits` live in fast GPU memory (`DeviceLocal`).
// Stage 1 (`CameraRays`) writes `rays`, Stage 2 (`ITracer::trace`) reads `rays` and writes `hits`,
// and Stage 3 (`HitShading`) reads `hits` to color the image.
//
// Rays are laid out as an image (row-major, width x height) so hits can be shown on screen,
// but tracers treat both as flat 1D lists (`count() = width * height`) and never assume where
// the rays came from (so shadow rays or ambient occlusion rays can use the exact same interface).
struct RayBatch {
    gpu::Buffer rays; // GPU buffer holding core::Ray[count()] (32 bytes per ray)
    gpu::Buffer hits; // GPU buffer holding core::Hit[count()] (16 bytes per hit)
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] static RayBatch create(const gpu::Context& context, std::uint32_t width, std::uint32_t height);
    [[nodiscard]] std::uint32_t count() const { return width * height; }
    [[nodiscard]] std::uint64_t memoryBytes() const { return rays.size() + hits.size(); }
};

} // namespace thesis::tracing
