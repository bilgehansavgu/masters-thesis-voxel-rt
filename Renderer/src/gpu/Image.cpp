#include "thesis/gpu/Image.h"

#include "thesis/gpu/Barriers.h"
#include "thesis/gpu/Check.h"
#include "thesis/gpu/Context.h"

namespace thesis::gpu {

StorageImage::StorageImage(const Context& context, VkExtent2D extent, VkFormat format)
    : device_(context.device()), allocator_(context.allocator()), extent_(extent), format_(format)
{
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    THESIS_VK_CHECK(vmaCreateImage(allocator_, &imageInfo, &allocationInfo, &image_, &allocation_, nullptr));

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    THESIS_VK_CHECK(vkCreateImageView(device_, &viewInfo, nullptr, &view_));
}

StorageImage::~StorageImage()
{
    if (view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, view_, nullptr);
    }
    if (image_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, image_, allocation_);
    }
}

void StorageImage::transition(VkCommandBuffer cmd, VkImageLayout newLayout, VkPipelineStageFlags2 nextStage,
                              VkAccessFlags2 nextAccess)
{
    imageBarrier(cmd, image_, layout_, newLayout, lastStage_, lastAccess_, nextStage, nextAccess);
    layout_ = newLayout;
    lastStage_ = nextStage;
    lastAccess_ = nextAccess;
}

} // namespace thesis::gpu
