#pragma once

// Side: GPU

#include <VkBootstrap.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace thesis::gpu {

class Context;

// The window's chain of presentable images. Images are written by blits and ImGui only.
class Swapchain {
public:
    Swapchain(const Context& context, VkExtent2D desiredExtent);
    ~Swapchain();
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    // Call after the device is idle, e.g. when the window was resized.
    void recreate(VkExtent2D desiredExtent);

    [[nodiscard]] VkSwapchainKHR handle() const { return swapchain_.swapchain; }
    [[nodiscard]] VkFormat format() const { return swapchain_.image_format; }
    [[nodiscard]] VkExtent2D extent() const { return swapchain_.extent; }
    [[nodiscard]] std::uint32_t imageCount() const { return static_cast<std::uint32_t>(images_.size()); }
    [[nodiscard]] VkImage image(std::uint32_t index) const { return images_.at(index); }
    [[nodiscard]] VkImageView view(std::uint32_t index) const { return views_.at(index); }

private:
    void build(VkExtent2D desiredExtent);

    const Context& context_;
    vkb::Swapchain swapchain_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
};

} // namespace thesis::gpu
