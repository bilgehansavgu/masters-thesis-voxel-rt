// Hardware tracer: the GPU builds and walks its own search structure (a BVH) in the ray
// tracing cores; our code only answers "where does this ray hit inside this primitive's box?".
//
// Uses ray queries from a compute shader (VK_KHR_ray_query): same shader stage and dispatch as
// the software tracer, so the comparison isolates the search structure. A ray tracing
// pipeline variant (intersection shaders, as in OptiX) can be added later as another plugin.

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
#include <string>
#include <utility>
#include <vector>

namespace thesis::tracers {
namespace {

constexpr std::uint32_t kGroupSize = 64; // matches [numthreads] in trace_hardware.slang
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

        // Bottom level: one box per primitive, read straight from the scene's bounds buffer.
        VkAccelerationStructureGeometryKHR boxes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        boxes.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
        boxes.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        boxes.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
        boxes.geometry.aabbs.data.deviceAddress = scene.primitiveBounds.address();
        boxes.geometry.aabbs.stride = sizeof(core::Aabb);
        gpu::AccelerationStructureBuild bottom = gpu::buildAccelerationStructure(
            context, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, boxes, scene.primitiveCount,
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

        // Top level: a single instance of the bottom level with an identity transform.
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

        bottomLevel_ = std::move(bottom.structure);
        topLevel_ = std::move(top.structure);

        countingKernel_.reset();
        createKernel(kernel_, false);

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
        dispatch(cmd, *kernel_, batch, nullptr);
    }

    bool countsWork() const override { return true; }

    void traceCounting(VkCommandBuffer cmd, const tracing::RayBatch& batch, const gpu::Buffer& work) override
    {
        if (scene_ == nullptr) {
            throw std::logic_error("HardwareTracer: prepare() must be called before traceCounting()");
        }
        if (!countingKernel_) {
            createKernel(countingKernel_, true);
        }
        dispatch(cmd, *countingKernel_, batch, &work);
    }

    std::uint64_t memoryBytes() const override { return bottomLevel_.size() + topLevel_.size(); }

private:
    void createKernel(std::optional<gpu::ComputeKernel>& kernel, bool counting) const
    {
        std::vector<gpu::KernelBinding> bindings{{tracing::kRayBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                 {tracing::kHitBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                 {kSceneBinding, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR}};
        if (counting) {
            bindings.push_back({tracing::kRayWorkBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
        }
        for (const gpu::KernelBinding& binding : scene_->primitiveBindings()) {
            bindings.push_back(binding);
        }
        const std::string file = "trace_hardware." + scene_->primitiveKind + (counting ? ".counting.spv" : ".spv");
        kernel.emplace(environment_.gpu, gpu::KernelDescription{environment_.shaderDirectory / file,
                                                                std::move(bindings), sizeof(TraceParams)});
    }

    void dispatch(VkCommandBuffer cmd, const gpu::ComputeKernel& kernel, const tracing::RayBatch& batch,
                  const gpu::Buffer* work) const
    {
        gpu::KernelResources resources;
        resources.buffer(tracing::kRayBinding, batch.rays)
            .buffer(tracing::kHitBinding, batch.hits)
            .accelerationStructure(kSceneBinding, topLevel_.handle());
        if (work != nullptr) {
            resources.buffer(tracing::kRayWorkBinding, *work);
        }
        scene_->bindPrimitiveData(resources);
        const TraceParams params{batch.count(), 0, 0, 0};
        kernel.dispatch(cmd, resources, gpu::groupCount(batch.count(), kGroupSize), 1, 1, params);
    }

    tracing::TracerEnvironment environment_;
    const tracing::GpuScene* scene_ = nullptr;
    gpu::AccelerationStructure bottomLevel_;
    gpu::AccelerationStructure topLevel_;
    std::optional<gpu::ComputeKernel> kernel_;
    std::optional<gpu::ComputeKernel> countingKernel_; // created when it is first needed
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
