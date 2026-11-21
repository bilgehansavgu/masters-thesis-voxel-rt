#pragma once

// Side: View

#include "thesis/gpu/StageTiming.h"

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

struct GLFWwindow;

namespace thesis::app {

// The operating-system window (GLFW). Owns GLFW's lifetime as well.
class Window {
public:
    Window(int width, int height, const char* title);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] GLFWwindow* handle() const { return window_; }
    [[nodiscard]] VkExtent2D framebufferExtent() const;
    [[nodiscard]] bool minimised() const;
    // True while the window has the keyboard focus (it is the window in front).
    [[nodiscard]] bool focused() const;
    [[nodiscard]] bool shouldClose() const;
    void requestClose();

    // Processes pending events; blocks while the window is minimised.
    void pollEvents();

    // True once after the window's size changed.
    [[nodiscard]] bool consumeResize();

    // What Vulkan needs to draw into this window.
    [[nodiscard]] static std::vector<std::string> requiredInstanceExtensions();
    [[nodiscard]] VkSurfaceKHR createSurface(VkInstance instance) const;

private:
    GLFWwindow* window_ = nullptr;
    bool resized_ = false;
};

} // namespace thesis::app
