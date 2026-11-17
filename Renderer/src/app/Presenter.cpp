#include "Presenter.h"

#include "Window.h"

#include "thesis/gpu/Barriers.h"
#include "thesis/gpu/Check.h"
#include "thesis/gpu/Context.h"
#include "thesis/gpu/Image.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace thesis::app {

std::array<VkOffset3D, 2> letterbox(VkExtent2D image, VkExtent2D window)
{
    const float scale = std::min(static_cast<float>(window.width) / static_cast<float>(image.width),
                                 static_cast<float>(window.height) / static_cast<float>(image.height));
    const auto width = static_cast<std::int32_t>(static_cast<float>(image.width) * scale);
    const auto height = static_cast<std::int32_t>(static_cast<float>(image.height) * scale);
    const auto x = (static_cast<std::int32_t>(window.width) - width) / 2;
    const auto y = (static_cast<std::int32_t>(window.height) - height) / 2;
    return {VkOffset3D{x, y, 0}, VkOffset3D{x + width, y + height, 1}};
}

Presenter::Presenter(const gpu::Context& context, Window& window)
    : context_(context), window_(window), swapchain_(context, window.framebufferExtent())
{
    const VkDevice device = context_.device();

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context_.queueFamily();
    THESIS_VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &pool_));

    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = pool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    THESIS_VK_CHECK(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer_));

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    THESIS_VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &frameDone_));

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    THESIS_VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailable_));
    createRenderFinishedSemaphores();
}

Presenter::~Presenter()
{
    const VkDevice device = context_.device();
    vkDeviceWaitIdle(device);
    destroyRenderFinishedSemaphores();
    vkDestroySemaphore(device, imageAvailable_, nullptr);
    vkDestroyFence(device, frameDone_, nullptr);
    vkDestroyCommandPool(device, pool_, nullptr);
}

bool Presenter::waitForPreviousFrame()
{
    THESIS_VK_CHECK(vkWaitForFences(context_.device(), 1, &frameDone_, VK_TRUE, UINT64_MAX));
    return std::exchange(frameInFlight_, false);
}

std::optional<VkCommandBuffer> Presenter::beginFrame()
{
    const VkResult acquired = vkAcquireNextImageKHR(context_.device(), swapchain_.handle(), UINT64_MAX, imageAvailable_,
                                                    VK_NULL_HANDLE, &imageIndex_);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        rebuild();
        return std::nullopt;
    }
    THESIS_VK_CHECK(acquired);
    THESIS_VK_CHECK(vkResetFences(context_.device(), 1, &frameDone_));

    THESIS_VK_CHECK(vkResetCommandBuffer(commandBuffer_, 0));
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    THESIS_VK_CHECK(vkBeginCommandBuffer(commandBuffer_, &beginInfo));
    return commandBuffer_;
}

bool Presenter::present(gpu::StorageImage& image, const Overlay& overlay)
{
    const VkCommandBuffer cmd = commandBuffer_;
    const VkImage target = swapchain_.image(imageIndex_);

    // Clear the window image (for the bars around a letterboxed picture), then blit into it.
    image.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    gpu::imageBarrier(cmd, target, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT);
    const VkClearColorValue black{{0.0f, 0.0f, 0.0f, 1.0f}};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
    gpu::imageBarrier(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkImageBlit2 blit{VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {static_cast<std::int32_t>(image.extent().width),
                          static_cast<std::int32_t>(image.extent().height), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    const auto destination = letterbox(image.extent(), swapchain_.extent());
    blit.dstOffsets[0] = destination[0];
    blit.dstOffsets[1] = destination[1];
    VkBlitImageInfo2 blitInfo{VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
    blitInfo.srcImage = image.image();
    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blitInfo.dstImage = target;
    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitInfo.regionCount = 1;
    blitInfo.pRegions = &blit;
    blitInfo.filter = VK_FILTER_LINEAR;
    vkCmdBlitImage2(cmd, &blitInfo);

    // Overlay (the control panel), then hand the image to the window system.
    gpu::imageBarrier(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    if (overlay) {
        overlay(cmd, swapchain_.view(imageIndex_), swapchain_.extent());
    }
    gpu::imageBarrier(cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    THESIS_VK_CHECK(vkEndCommandBuffer(cmd));

    // The blit waits for the window image to be free; the presentation waits for the blit.
    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = imageAvailable_;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = renderFinished_[imageIndex_];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandInfo.commandBuffer = cmd;
    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;
    THESIS_VK_CHECK(vkQueueSubmit2(context_.queue(), 1, &submitInfo, frameDone_));
    frameInFlight_ = true;

    const VkSwapchainKHR swapchain = swapchain_.handle();
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished_[imageIndex_];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex_;
    const VkResult presented = vkQueuePresentKHR(context_.queue(), &presentInfo);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR || window_.consumeResize()) {
        rebuild();
        return true;
    }
    THESIS_VK_CHECK(presented);
    return false;
}

void Presenter::rebuild()
{
    vkDeviceWaitIdle(context_.device());
    swapchain_.recreate(window_.framebufferExtent());
    destroyRenderFinishedSemaphores();
    createRenderFinishedSemaphores();
}

void Presenter::createRenderFinishedSemaphores()
{
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    renderFinished_.resize(swapchain_.imageCount());
    for (VkSemaphore& semaphore : renderFinished_) {
        THESIS_VK_CHECK(vkCreateSemaphore(context_.device(), &semaphoreInfo, nullptr, &semaphore));
    }
}

void Presenter::destroyRenderFinishedSemaphores()
{
    for (VkSemaphore semaphore : renderFinished_) {
        vkDestroySemaphore(context_.device(), semaphore, nullptr);
    }
    renderFinished_.clear();
}

} // namespace thesis::app
