#pragma once

// Side: GPU

#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/ComputeKernel.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace thesis::gpu {

class Context;

// A ray tracing pipeline made of entry points of one SPIR-V file: one ray generation shader,
// one miss shader, and one hit group for boxes (procedural primitives) with an intersection
// shader and, if named, a closest-hit shader. One file may hold more entry points than one
// pipeline uses; the names pick them.
struct RayTracingPipelineDescription {
    std::filesystem::path spirvPath;
    std::vector<KernelBinding> bindings; // all in descriptor set 0, visible to every stage
    std::uint32_t pushConstantSize = 0;
    std::string rayGeneration;
    std::string miss;
    std::string intersection;
    std::string closestHit; // empty: the hit group has none
};

// One ray tracing pipeline (VK_KHR_ray_tracing_pipeline) and its shader binding table: the table
// that tells the GPU which shader to run for the ray generation, a miss and a hit. The ray
// generation shader may trace one ray at a time (recursion depth 1).
//
// On creation it checks the shader's reflection report against the C++ side, as ComputeKernel
// does, and uploads the shader binding table to device memory: creating one submits work and
// waits, so never create one while a command buffer records. Throws std::runtime_error if the
// device has no ray tracing pipelines.
class RayTracingPipeline {
public:
    RayTracingPipeline(const Context& context, const RayTracingPipelineDescription& description);
    ~RayTracingPipeline();
    RayTracingPipeline(const RayTracingPipeline&) = delete;
    RayTracingPipeline& operator=(const RayTracingPipeline&) = delete;

    // Runs the ray generation shader once for each of width x height x depth launch indices.
    void trace(VkCommandBuffer cmd, const KernelResources& resources, std::uint32_t width, std::uint32_t height,
               std::uint32_t depth, const void* pushConstants = nullptr, std::uint32_t pushConstantSize = 0) const;

    [[nodiscard]] const std::string& name() const { return name_; }
    // What the driver made of each shader of the pipeline. Empty if the driver does not report it.
    [[nodiscard]] const std::vector<ShaderStatistic>& statistics() const { return statistics_; }

private:
    const Context& context_;
    std::string name_;
    std::uint32_t pushConstantSize_ = 0;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    Buffer bindingTable_;
    VkStridedDeviceAddressRegionKHR rayGenerationRegion_{};
    VkStridedDeviceAddressRegionKHR missRegion_{};
    VkStridedDeviceAddressRegionKHR hitRegion_{};
    std::vector<ShaderStatistic> statistics_;
};

} // namespace thesis::gpu
