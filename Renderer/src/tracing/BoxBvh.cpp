#include "thesis/tracing/BoxBvh.h"

#include "thesis/core/GpuTypes.h"
#include "thesis/gpu/Buffer.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>

namespace thesis::tracing {

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

BoxBvh BoxBvh::build(const gpu::Context& context, const GpuScene& scene)
{
    VkAccelerationStructureGeometryKHR boxes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    boxes.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
    boxes.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    boxes.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
    boxes.geometry.aabbs.data.deviceAddress = scene.primitiveBounds.address();
    boxes.geometry.aabbs.stride = sizeof(core::Aabb);
    gpu::AccelerationStructureBuild bottom = gpu::buildAccelerationStructure(
        context, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, boxes, scene.primitiveCount,
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

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
    instanceGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instanceGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
    instanceGeometry.geometry.instances.data.deviceAddress = instances.address();
    gpu::AccelerationStructureBuild top =
        gpu::buildAccelerationStructure(context, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, instanceGeometry, 1,
                                        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);

    BoxBvh bvh;
    bvh.gpuMilliseconds = bottom.gpuMilliseconds + top.gpuMilliseconds;
    bvh.scratchBytes = std::max(bottom.scratchBytes, top.scratchBytes);
    bvh.bottomLevel = std::move(bottom.structure);
    bvh.topLevel = std::move(top.structure);
    return bvh;
}

} // namespace thesis::tracing
