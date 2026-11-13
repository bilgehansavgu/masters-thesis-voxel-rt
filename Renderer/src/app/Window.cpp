#include "Window.h"

#include "thesis/gpu/Check.h"

#define GLFW_INCLUDE_VULKAN
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

VkExtent2D Window::framebufferExtent() const
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
}

bool Window::minimised() const
{
    const VkExtent2D extent = framebufferExtent();
    return extent.width == 0 || extent.height == 0;
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

std::vector<std::string> Window::requiredInstanceExtensions()
{
    if (!glfwInit()) {
        throw std::runtime_error("GLFW could not be initialised");
    }
    std::uint32_t count = 0;
    const char** names = glfwGetRequiredInstanceExtensions(&count);
    if (names == nullptr) {
        throw std::runtime_error("GLFW reports no Vulkan support on this system");
    }
    return {names, names + count};
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    THESIS_VK_CHECK(glfwCreateWindowSurface(instance, window_, nullptr, &surface));
    return surface;
}

} // namespace thesis::app
