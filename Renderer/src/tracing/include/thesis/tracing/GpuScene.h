#pragma once

#include "thesis/core/Scene.h"
#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/ComputeKernel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace thesis::gpu {
class Context;
}

namespace thesis::tracing {

// A scene uploaded to the GPU once, then shared read-only by every tracer.
//
// Tracers never name the primitive kind's buffers: they ask the scene for its bindings when
// they build a kernel (primitiveBindings) and to attach its buffers when they dispatch
// (bindPrimitiveData). A primitive kind with more data (the brick encoding) therefore needs
// no tracer changes.
struct GpuScene {
    struct DataBuffer {
        std::uint32_t binding;
        std::string name;
        gpu::Buffer buffer;
    };

    std::string name;
    std::string primitiveKind;
    std::uint32_t primitiveCount = 0;
    core::Bounds bounds;
    // core::Aabb[primitiveCount]. Every kind gets it at kPrimitiveBoundsBinding; it is also the
    // input for GPU-built search structures.
    gpu::Buffer primitiveBounds;
    // The kind's own data, at bindings kPrimitiveDataBindingFirst..kPrimitiveDataBindingLast.
    std::vector<DataBuffer> primitiveData;

    [[nodiscard]] static GpuScene upload(const gpu::Context& context, const core::Scene& scene);

    // Descriptor bindings the primitive kind's shader code reads (bounds plus its own data).
    [[nodiscard]] std::vector<gpu::KernelBinding> primitiveBindings() const;
    // Attaches those buffers to a dispatch.
    void bindPrimitiveData(gpu::KernelResources& resources) const;

    [[nodiscard]] std::uint64_t memoryBytes() const;
};

} // namespace thesis::tracing
