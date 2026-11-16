#include "thesis/gpu/Image.h"

#include "thesis/gpu/Barriers.h"
#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/Check.h"
#include "thesis/gpu/Context.h"

#include <stdexcept>

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

std::vector<std::byte> downloadImage(const Context& context, StorageImage& image)
{
    if (image.format() != VK_FORMAT_R8G8B8A8_UNORM) {
        throw std::invalid_argument("downloadImage: only RGBA8 images are supported");
    }
    const VkExtent2D extent = image.extent();
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
    Buffer staging(context, bytes, 0, MemoryUsage::Readback);
    context.submitAndWait([&](VkCommandBuffer cmd) {
        image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                         VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(cmd, image.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.handle(), 1, &region);

        VkMemoryBarrier2 after{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        after.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        after.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        after.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        after.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &after;
        vkCmdPipelineBarrier2(cmd, &dependency);
    });
    staging.invalidate();
    const auto* first = static_cast<const std::byte*>(staging.mapped());
    return std::vector<std::byte>(first, first + bytes);
}

} // namespace thesis::gpu
