#pragma once

// ============================================================================
// READING ORDER: FILE 27A
// ============================================================================

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <span>
#include <vector>

namespace thesis::gpu {

class Context;

enum class MemoryUsage {
    DeviceLocal, // fast GPU VRAM; filled by GPU copies or compute shaders
    Upload,      // CPU-visible memory, persistently mapped, written by the CPU to copy to the GPU
    Readback,    // CPU-visible memory, persistently mapped, read by the CPU after a GPU copy
};

// A VkBuffer with its memory allocation (managed via VMA, the Vulkan Memory Allocator library).
//
// C++ REFRESHER: Move-only RAII (Resource Acquisition Is Initialization)
// A `Buffer` owns a chunk of GPU memory (`buffer_` and `allocation_`), and its destructor
// (`~Buffer()`) automatically frees that GPU memory when the `Buffer` variable goes out of scope.
// What would happen if you accidentally copied a `Buffer` (`Buffer b2 = b1;`)? Both `b1` and `b2`
// would hold the same `VkBuffer` handle, and when both went out of scope, the GPU buffer would be
// freed TWICE (a crash!).
// To prevent that:
//   - Copying is disabled (`Buffer(const Buffer&) = delete;`).
//   - Moving is enabled (`Buffer(Buffer&& other) noexcept;`), which transfers the handle from
//     `other` into `this` and sets `other.buffer_ = VK_NULL_HANDLE` so `other`'s destructor does
//     nothing.
//
// TRANSFER_SRC and TRANSFER_DST are always added to the usage flags so any buffer can be uploaded
// to or read back.
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
    // 64-bit GPU virtual memory pointer (`VkDeviceAddress`), used by the hardware BVH builder.
    // Only valid when created with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT.
    [[nodiscard]] VkDeviceAddress address() const { return address_; }
    // CPU pointer to the buffer's bytes. Only valid for Upload and Readback buffers.
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

// Creates a device-local buffer holding `data` (copied through a temporary Upload staging buffer;
// blocks until the GPU copy completes).
// C++ REFRESHER: `std::span<const std::byte>` is a lightweight view (pointer + length) over any
// contiguous array or `std::vector` in CPU memory, without copying the vector.
[[nodiscard]] Buffer uploadBuffer(const Context& context, std::span<const std::byte> data, VkBufferUsageFlags usage,
                                  VkDeviceSize alignment = 0);

// Copies a GPU buffer's contents back to a CPU `std::vector<std::byte>` (blocks until complete).
[[nodiscard]] std::vector<std::byte> downloadBuffer(const Context& context, const Buffer& buffer);

} // namespace thesis::gpu
