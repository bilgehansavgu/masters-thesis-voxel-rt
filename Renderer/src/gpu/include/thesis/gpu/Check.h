#pragma once

// Side: GPU

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>

namespace thesis::gpu {

class VulkanError : public std::runtime_error {
public:
    VulkanError(VkResult result, const std::string& message) : std::runtime_error(message), result_(result) {}
    [[nodiscard]] VkResult result() const { return result_; }

private:
    VkResult result_;
};

// Throws VulkanError for any negative VkResult (positive codes such as VK_SUBOPTIMAL_KHR pass).
void check(VkResult result, const char* expression, const char* file, int line);

} // namespace thesis::gpu

#define THESIS_VK_CHECK(expression) ::thesis::gpu::check((expression), #expression, __FILE__, __LINE__)
