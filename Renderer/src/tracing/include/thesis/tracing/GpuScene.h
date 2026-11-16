#pragma once

// ============================================================================
// READING ORDER: FILE 20B
// ============================================================================

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
// WHY SEPARATE `core::Scene` (CPU) AND `tracing::GpuScene` (GPU)?
// `core::Scene` holds standard C++ `std::vector` arrays in system RAM so CPU code and unit tests
// can build or inspect scenes without needing a GPU.
// `GpuScene::upload(...)` copies those arrays into fast GPU VRAM (`gpu::Buffer`) once at startup.
//
// Tracers never name the primitive kind's buffers directly: they ask the `GpuScene` for its
// bindings when they build a shader kernel (`primitiveBindings()`) and to attach its buffers
// when they dispatch (`bindPrimitiveData()`). When Step 1 adds compressed voxel brick buffers
// at bindings 9..15, tracers pass them through automatically without changing a single line of
// tracer code.
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
    // GPU buffer holding core::Aabb[primitiveCount]. Every primitive kind gets it at
    // kPrimitiveBoundsBinding (slot 8); it is also the direct input for GPU-built BVHs.
    gpu::Buffer primitiveBounds;
    // The primitive kind's own data buffers, at slots kPrimitiveDataBindingFirst..Last (9..15).
    std::vector<DataBuffer> primitiveData;

    [[nodiscard]] static GpuScene upload(const gpu::Context& context, const core::Scene& scene);

    // Descriptor bindings the primitive kind's shader code reads (bounds at slot 8 plus any 9..15).
    [[nodiscard]] std::vector<gpu::KernelBinding> primitiveBindings() const;
    // Attaches those GPU buffers to a shader dispatch.
    void bindPrimitiveData(gpu::KernelResources& resources) const;

    [[nodiscard]] std::uint64_t memoryBytes() const;
};

} // namespace thesis::tracing
