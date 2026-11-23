#pragma once

// Side: GPU

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

struct GLFWwindow;

namespace thesis::app {

// What Vulkan needs to draw into a GLFW window (Window.h). Kept apart from Window, which is
// window code and knows no Vulkan.

// The instance extensions that a surface for a GLFW window needs.
[[nodiscard]] std::vector<std::string> windowInstanceExtensions();

// The surface of `window`. The caller destroys it.
[[nodiscard]] VkSurfaceKHR createWindowSurface(GLFWwindow* window, VkInstance instance);

} // namespace thesis::app
