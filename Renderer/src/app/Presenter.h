#pragma once

#include "thesis/gpu/Swapchain.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace thesis::gpu {
class Context;
class StorageImage;
} // namespace thesis::gpu

namespace thesis::app {

class Window;

// Largest rectangle with the image's aspect ratio that fits in the window, centred: where the
// picture of the frame is in the window. Corners [0] (inclusive) and [1] (exclusive), in pixels.
[[nodiscard]] std::array<VkOffset3D, 2> letterbox(VkExtent2D image, VkExtent2D window);

// Gets frames onto the window: the swapchain, the per-frame command buffer and the
// synchronisation objects, with one frame in flight. Per frame:
//
//   waitForPreviousFrame()  then the caller may read the previous frame's GPU timings
//   beginFrame()            acquires a window image, returns a command buffer to record into
//   present(image, overlay) copies `image` into the window, records the overlay, submits, presents
class Presenter {
public:
    using Overlay = std::function<void(VkCommandBuffer, VkImageView, VkExtent2D)>;

    Presenter(const gpu::Context& context, Window& window);
    ~Presenter();
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    // Blocks until the GPU has finished the previous frame. Returns true if there was one.
    bool waitForPreviousFrame();

    // Acquires a window image and starts recording. Returns nothing if the window had to be
    // rebuilt first (resize); the caller then skips this frame.
    [[nodiscard]] std::optional<VkCommandBuffer> beginFrame();

    // Copies `image` into the window (keeping its aspect ratio), lets `overlay` draw on top,
    // then submits and presents. Returns true if the window was rebuilt afterwards.
    bool present(gpu::StorageImage& image, const Overlay& overlay);

    [[nodiscard]] VkFormat format() const { return swapchain_.format(); }
    [[nodiscard]] std::uint32_t imageCount() const { return swapchain_.imageCount(); }

private:
    void rebuild();
    void createRenderFinishedSemaphores();
    void destroyRenderFinishedSemaphores();

    const gpu::Context& context_;
    Window& window_;
    gpu::Swapchain swapchain_;

    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence frameDone_ = VK_NULL_HANDLE;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    // One per swapchain image: a semaphore may only be reused once the presentation that
    // waited on it is done, which is only known per image.
    std::vector<VkSemaphore> renderFinished_;

    std::uint32_t imageIndex_ = 0;
    bool frameInFlight_ = false;
};

} // namespace thesis::app
