#include "Window.h"

#include <GLFW/glfw3.h>

#include <stdexcept>
#include <utility>

namespace thesis::app {

Window::Window(int width, int height, const char* title)
{
    if (!glfwInit()) {
        throw std::runtime_error("GLFW could not be initialised");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (window_ == nullptr) {
        glfwTerminate();
        throw std::runtime_error("The window could not be created");
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* window, int, int) {
        static_cast<Window*>(glfwGetWindowUserPointer(window))->resized_ = true;
    });
}

Window::~Window()
{
    glfwDestroyWindow(window_);
    glfwTerminate();
}

FramebufferSize Window::framebufferSize() const
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
}

bool Window::minimised() const
{
    const FramebufferSize size = framebufferSize();
    return size.width == 0 || size.height == 0;
}

bool Window::focused() const
{
    return glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0;
}

bool Window::shouldClose() const
{
    return glfwWindowShouldClose(window_) != 0;
}

void Window::requestClose()
{
    glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

void Window::pollEvents()
{
    glfwPollEvents();
    while (minimised() && !shouldClose()) {
        glfwWaitEvents();
    }
}

bool Window::consumeResize()
{
    return std::exchange(resized_, false);
}

} // namespace thesis::app
