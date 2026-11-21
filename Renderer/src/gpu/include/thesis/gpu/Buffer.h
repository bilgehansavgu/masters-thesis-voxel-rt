#pragma once

// Side: GPU

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <span>
#include <vector>

namespace thesis::gpu {

class Context;

enum class MemoryUsage {
    DeviceLocal, // GPU memory; filled by copies or shaders
    Upload,      // host-visible, persistently mapped, written by the CPU
    Readback,    // host-visible, persistently mapped, read by the CPU
};

// A VkBuffer with its memory (VMA). Move-only. TRANSFER_SRC and TRANSFER_DST are always added
// to the usage, so any buffer can be uploaded to or read back.
class Buffer {
public:
    Buffer() = default;
    Buffer(const Context& context, VkDeviceSize size, VkBufferUsageFlags usage, MemoryUsage memory,
           VkDeviceSize alignment = 0);
    ~Buffer();
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    [[nodiscard]] VkBuffer handle() const { return buffer_; }
    [[nodiscard]] VkDeviceSize size() const { return size_; }
    // Only valid when created with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT.
    [[nodiscard]] VkDeviceAddress address() const { return address_; }
    // Only valid for Upload and Readback buffers.
    [[nodiscard]] void* mapped() const { return mapped_; }

    void flush() const;      // after CPU writes to an Upload buffer
    void invalidate() const; // before CPU reads from a Readback buffer

private:
    void release() noexcept;

    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    VkDeviceAddress address_ = 0;
    void* mapped_ = nullptr;
};

// Creates a device-local buffer holding `data` (copied through a staging buffer; blocks).
[[nodiscard]] Buffer uploadBuffer(const Context& context, std::span<const std::byte> data, VkBufferUsageFlags usage,
                                  VkDeviceSize alignment = 0);

// Copies a buffer's contents to the CPU (blocks). Waits for all previously submitted GPU work.
[[nodiscard]] std::vector<std::byte> downloadBuffer(const Context& context, const Buffer& buffer);

} // namespace thesis::gpu
