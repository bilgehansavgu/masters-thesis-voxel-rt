#include "thesis/gpu/Swapchain.h"

#include "thesis/gpu/Context.h"

#include <stdexcept>

namespace thesis::gpu {

Swapchain::Swapchain(const Context& context, VkExtent2D desiredExtent) : context_(context)
{
    build(desiredExtent);
}

Swapchain::~Swapchain()
{
    swapchain_.destroy_image_views(views_);
    vkb::destroy_swapchain(swapchain_);
}

void Swapchain::recreate(VkExtent2D desiredExtent)
{
    build(desiredExtent);
}

void Swapchain::build(VkExtent2D desiredExtent)
{
    // UNORM, not SRGB: the shading pass already writes display-ready (gamma-encoded) colours,
    // and ImGui expects a UNORM target.
    vkb::SwapchainBuilder builder(context_.bootstrapDevice());
    builder.set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .add_fallback_format({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(desiredExtent.width, desiredExtent.height)
        .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    if (swapchain_.swapchain != VK_NULL_HANDLE) {
        builder.set_old_swapchain(swapchain_);
    }
    auto result = builder.build();
    if (!result) {
        throw std::runtime_error("Creating the swapchain: " + result.error().message());
    }

    swapchain_.destroy_image_views(views_);
    vkb::destroy_swapchain(swapchain_);
    swapchain_ = result.value();

    images_ = swapchain_.get_images().value();
    views_ = swapchain_.get_image_views().value();
}

} // namespace thesis::gpu
