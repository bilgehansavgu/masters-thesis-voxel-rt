#pragma once

// Side: GPU

// What every kind of GPU program in this library does the same way: read SPIR-V, check the
// shader's reflection report against the C++ side, build the push-descriptor set layout, and
// read the driver's statistics. ComputeKernel and RayTracingPipeline both stand on it, so a
// tracer built on either is checked by the same rules.

#include "thesis/gpu/ComputeKernel.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace thesis::gpu {

[[nodiscard]] std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path);

// Compares the shader's own report of its interface (<spirv>.json, written by the build) with
// what the C++ side declares. Throws if the shader reads a binding the C++ side does not
// provide, with a different descriptor type, or if the push-constant sizes differ.
void checkAgainstReflection(const std::filesystem::path& spirvPath, const std::vector<KernelBinding>& bindings,
                            std::uint32_t pushConstantSize, const std::string& name);

// The layout of descriptor set 0, bound with push descriptors, visible to `stages`.
[[nodiscard]] VkDescriptorSetLayout
createPushDescriptorSetLayout(VkDevice device, const std::vector<KernelBinding>& bindings, VkShaderStageFlags stages);

// What the driver made of each compiled shader of `pipeline`. A pipeline with more than one
// executable (a ray tracing pipeline) prefixes every name with the executable's name.
[[nodiscard]] std::vector<ShaderStatistic> readPipelineStatistics(const Context& context, VkPipeline pipeline);

} // namespace thesis::gpu
