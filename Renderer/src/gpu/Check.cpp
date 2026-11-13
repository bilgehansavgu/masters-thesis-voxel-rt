#include "thesis/gpu/Check.h"

namespace thesis::gpu {

void check(VkResult result, const char* expression, const char* file, int line)
{
    if (result >= 0) {
        return;
    }
    throw VulkanError(result, std::string(expression) + " failed with VkResult " + std::to_string(result) + " (" +
                                  file + ":" + std::to_string(line) + ")");
}

} // namespace thesis::gpu
