#pragma once

#include <vulkan/vulkan.h>

namespace thesis::gpu {

// Compute shader writes become visible to the next compute shader (e.g. rays → tracer → shading).
void computeToComputeBarrier(VkCommandBuffer cmd);

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess);

} // namespace thesis::gpu
