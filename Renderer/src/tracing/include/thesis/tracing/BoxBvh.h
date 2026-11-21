#pragma once

// Side: GPU

#include "thesis/gpu/AccelerationStructure.h"
#include "thesis/tracing/GpuScene.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace thesis::gpu {
class Context;
}

namespace thesis::tracing {

// The GPU's own BVH over the primitive boxes of a scene: the search structure of every tracer
// that lets the GPU search, whether through ray queries or a ray tracing pipeline. One builder,
// so those tracers differ only in how they walk it, never in what they walk.
//
// Bottom level: one box per primitive, read straight from GpuScene::primitiveBounds, opaque.
// Top level: one instance of it with an identity transform. Built for fast tracing.
struct BoxBvh {
    gpu::AccelerationStructure bottomLevel;
    gpu::AccelerationStructure topLevel;
    double gpuMilliseconds = 0.0;   // both builds
    std::uint64_t scratchBytes = 0; // the larger of the two builds' temporary memory

    [[nodiscard]] static BoxBvh build(const gpu::Context& context, const GpuScene& scene);

    [[nodiscard]] std::uint64_t memoryBytes() const { return bottomLevel.size() + topLevel.size(); }
};

} // namespace thesis::tracing
