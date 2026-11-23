#include "WindowSurface.h"

#include "thesis/gpu/Check.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdint>
#include <stdexcept>

namespace thesis::app {

std::vector<std::string> windowInstanceExtensions()
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

VkSurfaceKHR createWindowSurface(GLFWwindow* window, VkInstance instance)
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    THESIS_VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));
    return surface;
}

} // namespace thesis::app
