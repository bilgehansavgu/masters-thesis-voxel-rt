#include "thesis/gpu/Buffer.h"

#include "thesis/gpu/Check.h"
#include "thesis/gpu/Context.h"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace thesis::gpu {

Buffer::Buffer(const Context& context, VkDeviceSize size, VkBufferUsageFlags usage, MemoryUsage memory,
               VkDeviceSize alignment)
    : allocator_(context.allocator()), size_(size)
{
    if (size == 0) {
        throw std::invalid_argument("Buffer: size must be greater than zero");
    }

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    switch (memory) {
    case MemoryUsage::DeviceLocal:
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case MemoryUsage::Upload:
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationInfo.flags =
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case MemoryUsage::Readback:
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }

    VmaAllocationInfo resultInfo{};
    if (alignment > 0) {
        THESIS_VK_CHECK(vmaCreateBufferWithAlignment(allocator_, &bufferInfo, &allocationInfo, alignment, &buffer_,
                                                     &allocation_, &resultInfo));
    } else {
        THESIS_VK_CHECK(vmaCreateBuffer(allocator_, &bufferInfo, &allocationInfo, &buffer_, &allocation_, &resultInfo));
    }
    mapped_ = resultInfo.pMappedData;

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        addressInfo.buffer = buffer_;
        address_ = vkGetBufferDeviceAddress(context.device(), &addressInfo);
    }
}

Buffer::~Buffer()
{
    release();
}

Buffer::Buffer(Buffer&& other) noexcept
    : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)),
      buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
      size_(std::exchange(other.size_, 0)),
      address_(std::exchange(other.address_, 0)),
      mapped_(std::exchange(other.mapped_, nullptr))
{}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
    if (this != &other) {
        release();
        allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
        size_ = std::exchange(other.size_, 0);
        address_ = std::exchange(other.address_, 0);
        mapped_ = std::exchange(other.mapped_, nullptr);
    }
    return *this;
}

void Buffer::release() noexcept
{
    if (buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }
}

void Buffer::flush() const
{
    THESIS_VK_CHECK(vmaFlushAllocation(allocator_, allocation_, 0, VK_WHOLE_SIZE));
}

void Buffer::invalidate() const
{
    THESIS_VK_CHECK(vmaInvalidateAllocation(allocator_, allocation_, 0, VK_WHOLE_SIZE));
}

Buffer uploadBuffer(const Context& context, std::span<const std::byte> data, VkBufferUsageFlags usage,
                    VkDeviceSize alignment)
{
    Buffer staging(context, data.size(), 0, MemoryUsage::Upload);
    std::memcpy(staging.mapped(), data.data(), data.size());
    staging.flush();

    Buffer result(context, data.size(), usage, MemoryUsage::DeviceLocal, alignment);
    context.submitAndWait([&](VkCommandBuffer cmd) {
        const VkBufferCopy region{0, 0, data.size()};
        vkCmdCopyBuffer(cmd, staging.handle(), result.handle(), 1, &region);
    });
    return result;
}

std::vector<std::byte> downloadBuffer(const Context& context, const Buffer& buffer)
{
    Buffer staging(context, buffer.size(), 0, MemoryUsage::Readback);
    context.submitAndWait([&](VkCommandBuffer cmd) {
        // Make every earlier write (any stage, any earlier submission) visible to the copy.
        VkMemoryBarrier2 before{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        before.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        before.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        before.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        before.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &before;
        vkCmdPipelineBarrier2(cmd, &dependency);

        const VkBufferCopy region{0, 0, buffer.size()};
        vkCmdCopyBuffer(cmd, buffer.handle(), staging.handle(), 1, &region);

        VkMemoryBarrier2 after{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        after.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        after.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        after.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        after.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        dependency.pMemoryBarriers = &after;
        vkCmdPipelineBarrier2(cmd, &dependency);
    });
    staging.invalidate();

    std::vector<std::byte> result(buffer.size());
    std::memcpy(result.data(), staging.mapped(), result.size());
    return result;
}

} // namespace thesis::gpu
