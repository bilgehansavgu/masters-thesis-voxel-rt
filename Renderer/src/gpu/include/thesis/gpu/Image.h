#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <vector>

namespace thesis::gpu {

class Context;

// A 2D image that compute shaders write and that can be copied or blitted from.
// It remembers its layout and last access, so transition() only needs the next use.
class StorageImage {
public:
    StorageImage(const Context& context, VkExtent2D extent, VkFormat format);
    ~StorageImage();
    StorageImage(const StorageImage&) = delete;
    StorageImage& operator=(const StorageImage&) = delete;

    [[nodiscard]] VkImage image() const { return image_; }
    [[nodiscard]] VkImageView view() const { return view_; }
    [[nodiscard]] VkExtent2D extent() const { return extent_; }
    [[nodiscard]] VkFormat format() const { return format_; }
    [[nodiscard]] VkImageLayout layout() const { return layout_; }

    // Records a barrier from the previous use to the next one and updates the tracked state.
    void transition(VkCommandBuffer cmd, VkImageLayout newLayout, VkPipelineStageFlags2 nextStage,
                    VkAccessFlags2 nextAccess);

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 lastStage_ = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 lastAccess_ = VK_ACCESS_2_NONE;
};

// Copies the pixels of an image to the CPU, row by row from the top (blocks). Waits for all
// previously submitted GPU work. The image is left in TRANSFER_SRC layout.
[[nodiscard]] std::vector<std::byte> downloadImage(const Context& context, StorageImage& image);

} // namespace thesis::gpu
