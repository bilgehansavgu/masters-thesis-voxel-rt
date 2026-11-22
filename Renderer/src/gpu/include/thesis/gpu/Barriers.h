#pragma once

// Side: GPU

#include "thesis/gpu/Context.h"

#include <vulkan/vulkan.h>

namespace thesis::gpu {

// Storage buffer writes of one shader become visible to the next shader (e.g. rays -> tracer ->
// shading). Both sides are every stage of Context::storageShaderStages(), so the barrier holds
// whether a step runs as a compute shader or as a ray tracing pipeline.
void shaderToShaderBarrier(const Context& context, VkCommandBuffer cmd);

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                  VkAccessFlags2 dstAccess);

} // namespace thesis::gpu
