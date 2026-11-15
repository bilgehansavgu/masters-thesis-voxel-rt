// ============================================================================
// READING ORDER: FILE 15C (C++ side of the Hardware Ray Tracer — BLAS & TLAS)
// ============================================================================
// Hardware tracer: the GPU builds and walks its own search structure (a BVH) in the ray
// tracing cores; our code only answers "where does this ray hit inside this primitive's box?".
//
// Uses ray queries from a compute shader (VK_KHR_ray_query): same shader stage and dispatch as
// the software tracer, so the comparison isolates the search structure. A ray tracing
// pipeline variant (intersection shaders, as in OptiX) can be added later as another plugin.
//
// RAY TRACING BASICS: What are BLAS and TLAS?
// Vulkan (and DirectX / OptiX) splits its hardware Bounding Volume Hierarchy into two levels:
//   1. Bottom-Level Acceleration Structure (BLAS):
//      Built directly over the geometry of an object — in our case, the array of AABBs
//      (`scene.primitiveBounds`) for all boxes/bricks in the scene.
//   2. Top-Level Acceleration Structure (TLAS):
//      Built over "instances" of one or more BLASes, each with a 3x4 world transform matrix.
//      Even when our scene has only a single non-moved object, Vulkan hardware ray tracing
//      requires a TLAS on top of the BLAS; shaders always trace rays against the TLAS (`topLevel_`).

#include "thesis/core/GpuTypes.h"
#include "thesis/gpu/AccelerationStructure.h"
#include "thesis/gpu/ComputeKernel.h"
#include "thesis/tracing/Bindings.h"
#include "thesis/tracing/Tracer.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace thesis::tracers {
namespace {

constexpr std::uint32_t kGroupSize = 64; // matches [numthreads(64, 1, 1)] in trace_hardware.slang
constexpr std::uint32_t kSceneBinding = THESIS_BINDING_HARDWARE_SCENE;

// The GPU's search-structure builder reads the scene's bounds buffer directly, as an array of
// VkAabbPositionsKHR (six packed floats) with stride sizeof(core::Aabb). These checks fail the
// build if core::Aabb ever stops matching that format (the step-0 box-layout bug).
static_assert(offsetof(core::Aabb, lo) == offsetof(VkAabbPositionsKHR, minX));
static_assert(offsetof(core::Aabb, lo) + 4 == offsetof(VkAabbPositionsKHR, minY));
static_assert(offsetof(core::Aabb, lo) + 8 == offsetof(VkAabbPositionsKHR, minZ));
static_assert(offsetof(core::Aabb, hi) == offsetof(VkAabbPositionsKHR, maxX));
static_assert(offsetof(core::Aabb, hi) + 4 == offsetof(VkAabbPositionsKHR, maxY));
static_assert(offsetof(core::Aabb, hi) + 8 == offsetof(VkAabbPositionsKHR, maxZ));
static_assert(sizeof(core::Aabb) % 8 == 0, "Vulkan requires the AABB stride to be a multiple of 8");

struct TraceParams {
    std::uint32_t rayCount;
    std::uint32_t pad0;
    std::uint32_t pad1;
    std::uint32_t pad2;
};

class HardwareTracer final : public tracing::ITracer {
public:
    explicit HardwareTracer(const tracing::TracerEnvironment& environment) : environment_(environment) {}

    std::string_view name() const override { return "hardware"; }

    tracing::PrepareReport prepare(const tracing::GpuScene& scene) override
    {
        const gpu::Context& context = environment_.gpu;
        scene_ = &scene;

        // Step 1: Build the Bottom-Level Acceleration Structure (BLAS).
        // Tell Vulkan that our geometry is an array of procedural AABBs (`VK_GEOMETRY_TYPE_AABBS_KHR`)
        // living at GPU memory address `scene.primitiveBounds.address()`, with 32 bytes (`sizeof(core::Aabb)`)
        // between the start of one box and the next.
        VkAccelerationStructureGeometryKHR boxes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        boxes.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        boxes.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        boxes.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        boxes.geometry.aabbs.data.deviceAddress = scene.primitiveBounds.address();
        boxes.geometry.aabbs.stride = sizeof(core::Aabb);
        gpu::AccelerationStructureBuild bottom = gpu::buildAccelerationStructure(
            context, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, boxes, scene.primitiveCount,
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

        // Step 2: Build the Top-Level Acceleration Structure (TLAS).
        // Create a single `VkAccelerationStructureInstanceKHR` pointing to `bottom.structure.address()`
        // with an identity 3x4 transform matrix (1s on the diagonal = no rotation/translation/scaling).
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform.matrix[0][0] = 1.0f;
        instance.transform.matrix[1][1] = 1.0f;
        instance.transform.matrix[2][2] = 1.0f;
        instance.instanceCustomIndex = 0;
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = bottom.structure.address();
        const gpu::Buffer instances =
            gpu::uploadBuffer(context, std::as_bytes(std::span(&instance, 1)),
                              VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                              16);

        VkAccelerationStructureGeometryKHR instanceGeometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        instanceGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        instanceGeometry.geometry.instances.sType =
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        instanceGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
        instanceGeometry.geometry.instances.data.deviceAddress = instances.address();
        gpu::AccelerationStructureBuild top =
            gpu::buildAccelerationStructure(context, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, instanceGeometry, 1,
                                            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

        // C++ REFRESHER: `std::move` transfers ownership of the GPU buffers inside `bottom` and `top`
        // into our member variables `bottomLevel_` and `topLevel_` without copying them.
        bottomLevel_ = std::move(bottom.structure);
        topLevel_ = std::move(top.structure);

        // Step 3: Load the `trace_hardware.<primitiveKind>.spv` shader and declare its bindings:
        //   - Slot 0: input ray list
        //   - Slot 1: output hit list
        //   - Slot 2: the hardware TLAS (`VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR`)
        //   - Slots 8..15: primitive bounds and any extra primitive data buffers
        std::vector<gpu::KernelBinding> bindings{{tracing::kRayBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                 {tracing::kHitBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                 {kSceneBinding, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR}};
        for (const gpu::KernelBinding& binding : scene.primitiveBindings()) {
            bindings.push_back(binding);
        }
        kernel_.emplace(context, gpu::KernelDescription{environment_.shaderDirectory /
                                                            ("trace_hardware." + scene.primitiveKind + ".spv"),
                                                        std::move(bindings), sizeof(TraceParams)});

        tracing::PrepareReport report;
        report.gpuMilliseconds = bottom.gpuMilliseconds + top.gpuMilliseconds;
        report.memoryBytes = memoryBytes();
        report.scratchBytes = std::max(bottom.scratchBytes, top.scratchBytes);
        return report;
    }

    void trace(VkCommandBuffer cmd, const tracing::RayBatch& batch) override
    {
        if (!kernel_ || scene_ == nullptr) {
            throw std::logic_error("HardwareTracer: prepare() must be called before trace()");
        }
        gpu::KernelResources resources;
        resources.buffer(tracing::kRayBinding, batch.rays)
            .buffer(tracing::kHitBinding, batch.hits)
            .accelerationStructure(kSceneBinding, topLevel_.handle());
        scene_->bindPrimitiveData(resources);
        const TraceParams params{batch.count(), 0, 0, 0};
        kernel_->dispatch(cmd, resources, gpu::groupCount(batch.count(), kGroupSize), 1, 1, params);
    }

    std::uint64_t memoryBytes() const override { return bottomLevel_.size() + topLevel_.size(); }

private:
    tracing::TracerEnvironment environment_;
    const tracing::GpuScene* scene_ = nullptr;
    gpu::AccelerationStructure bottomLevel_; // must stay alive while topLevel_ references it
    gpu::AccelerationStructure topLevel_;    // bound to slot 2 in trace()
    std::optional<gpu::ComputeKernel> kernel_;
};

} // namespace
} // namespace thesis::tracers

namespace thesis::plugins {

void registerHardwareTracer(tracing::TracerRegistry& registry)
{
    registry.add("hardware", "Ray tracing hardware walks a GPU-built BVH over the primitive boxes (ray queries).",
                 [](const tracing::TracerEnvironment& environment) {
                     return std::make_unique<tracers::HardwareTracer>(environment);
                 });
}

} // namespace thesis::plugins
