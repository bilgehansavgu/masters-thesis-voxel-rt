#include "thesis/gpu/AccelerationStructure.h"

#include "thesis/gpu/Check.h"
#include "thesis/gpu/Context.h"
#include "thesis/gpu/GpuTimer.h"

#include <utility>

namespace thesis::gpu {

AccelerationStructure::AccelerationStructure(const Context& context, VkAccelerationStructureTypeKHR type,
                                             VkDeviceSize size)
    : context_(&context),
      storage_(context, size,
               VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
               MemoryUsage::DeviceLocal)
{
    VkAccelerationStructureCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
    createInfo.buffer = storage_.handle();
    createInfo.size = size;
    createInfo.type = type;
    THESIS_VK_CHECK(context.dispatch().createAccelerationStructureKHR(&createInfo, nullptr, &handle_));

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
    addressInfo.accelerationStructure = handle_;
    address_ = context.dispatch().getAccelerationStructureDeviceAddressKHR(&addressInfo);
}

AccelerationStructure::~AccelerationStructure()
{
    release();
}

AccelerationStructure::AccelerationStructure(AccelerationStructure&& other) noexcept
    : context_(std::exchange(other.context_, nullptr)),
      storage_(std::move(other.storage_)),
      handle_(std::exchange(other.handle_, VK_NULL_HANDLE)),
      address_(std::exchange(other.address_, 0))
{}

AccelerationStructure& AccelerationStructure::operator=(AccelerationStructure&& other) noexcept
{
    if (this != &other) {
        release();
        context_ = std::exchange(other.context_, nullptr);
        storage_ = std::move(other.storage_);
        handle_ = std::exchange(other.handle_, VK_NULL_HANDLE);
        address_ = std::exchange(other.address_, 0);
    }
    return *this;
}

void AccelerationStructure::release() noexcept
{
    if (handle_ != VK_NULL_HANDLE) {
        context_->dispatch().destroyAccelerationStructureKHR(handle_, nullptr);
        handle_ = VK_NULL_HANDLE;
    }
}

AccelerationStructureBuild buildAccelerationStructure(const Context& context, VkAccelerationStructureTypeKHR type,
                                                      const VkAccelerationStructureGeometryKHR& geometry,
                                                      std::uint32_t primitiveCount,
                                                      VkBuildAccelerationStructureFlagsKHR flags)
{
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    buildInfo.type = type;
    buildInfo.flags = flags;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    context.dispatch().getAccelerationStructureBuildSizesKHR(VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                             &buildInfo, &primitiveCount, &sizes);

    AccelerationStructureBuild result;
    result.structure = AccelerationStructure(context, type, sizes.accelerationStructureSize);
    result.scratchBytes = sizes.buildScratchSize;

    Buffer scratch(context, sizes.buildScratchSize,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   MemoryUsage::DeviceLocal, context.info().minScratchOffsetAlignment);
    buildInfo.dstAccelerationStructure = result.structure.handle();
    buildInfo.scratchData.deviceAddress = scratch.address();

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = primitiveCount;
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;

    GpuTimer timer(context, 1);
    context.submitAndWait([&](VkCommandBuffer cmd) {
        timer.reset(cmd);
        const std::uint32_t scope = timer.begin(cmd, "acceleration structure build");
        context.dispatch().cmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &ranges);
        timer.end(cmd, scope);
    });
    result.gpuMilliseconds = timer.resolve().at(0).milliseconds;
    return result;
}

} // namespace thesis::gpu
