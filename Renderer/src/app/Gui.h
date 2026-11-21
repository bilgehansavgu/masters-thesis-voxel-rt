#pragma once

// Side: GPU

#include <vulkan/vulkan.h>

#include <cstdint>

namespace thesis::gpu {
class Context;
}

namespace thesis::app {

class Window;

// The face of the panel text, and of the colour bar that --screenshot writes. Where the file is
// missing, both use the face built into Dear ImGui.
inline constexpr const char* kInterfaceFontFile = "C:/Windows/Fonts/segoeui.ttf";

// Dear ImGui with GLFW input and Vulkan dynamic rendering. Draws whatever ImGui calls were made
// between newFrame() and render(); it does not decide what the panel shows (see ControlPanel).
class Gui {
public:
    Gui(const gpu::Context& context, const Window& window, VkFormat targetFormat, std::uint32_t imageCount);
    ~Gui();
    Gui(const Gui&) = delete;
    Gui& operator=(const Gui&) = delete;

    void newFrame();
    // Records the UI on top of `view`, which must be in COLOR_ATTACHMENT_OPTIMAL layout.
    void render(VkCommandBuffer cmd, VkImageView view, VkExtent2D extent);
    // Call after the window's swapchain was rebuilt.
    void swapchainRebuilt();

private:
    VkDevice device_;
    VkFormat targetFormat_;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
};

} // namespace thesis::app
