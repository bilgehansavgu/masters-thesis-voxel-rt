#pragma once

// Side: View

#include <cstdint>

struct GLFWwindow;

namespace thesis::app {

// The size of the window's drawing area in pixels. Zero while the window is minimised.
struct FramebufferSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// The operating-system window (GLFW). Owns GLFW's lifetime as well. What Vulkan needs to draw
// into it is in WindowSurface.h.
class Window {
public:
    Window(int width, int height, const char* title);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] GLFWwindow* handle() const { return window_; }
    [[nodiscard]] FramebufferSize framebufferSize() const;
    [[nodiscard]] bool minimised() const;
    // True while the window has the keyboard focus (it is the window in front).
    [[nodiscard]] bool focused() const;
    [[nodiscard]] bool shouldClose() const;
    void requestClose();

    // Processes pending events; blocks while the window is minimised.
    void pollEvents();

    // True once after the window's size changed.
    [[nodiscard]] bool consumeResize();

private:
    GLFWwindow* window_ = nullptr;
    bool resized_ = false;
};

} // namespace thesis::app
