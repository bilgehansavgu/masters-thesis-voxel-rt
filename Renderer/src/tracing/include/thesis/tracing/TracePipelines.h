#pragma once

// Side: GPU

#include "thesis/gpu/ComputeKernel.h"
#include "thesis/gpu/RayTracingPipeline.h"
#include "thesis/tracing/GpuScene.h"
#include "thesis/tracing/RayBatch.h"
#include "thesis/tracing/Tracer.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace thesis::tracing {

// The entry points of a tracer's ray tracing pipeline, by name (see gpu::RayTracingPipelineDescription).
struct TraceEntryPoints {
    std::string rayGeneration;
    std::string miss;
    std::string intersection;
    std::string closestHit; // empty: none
};

// The ray tracing pipeline of one tracer, for the primitive kind of one scene: what TraceKernels
// is for a tracer that runs as a compute shader. Same shader files (<shader>.<kind>.spv and
// <shader>.<kind>.counting.spv), same bindings, same resources, so both kinds of tracer read the
// same rays and write the same hits and work.
//
// The ray generation shader runs once for each ray: launch index x is the index in the ray list.
class TracePipelines {
public:
    TracePipelines(const TracerEnvironment& environment, std::string shaderName, TraceEntryPoints entryPoints,
                   std::vector<gpu::KernelBinding> ownBindings, std::uint32_t pushConstantSize);

    // Creates the timed and the counting pipeline for the primitive kind of `scene`. `scene` must
    // outlive every trace. Call it from ITracer::prepare, never while a command buffer records:
    // it submits work. Returns what the driver made of the timed pipeline's shaders.
    std::vector<gpu::ShaderStatistic> prepare(const GpuScene& scene);

    // Records one launch over all rays of `batch`. With `work`, the counting variant runs.
    template <typename PushConstants>
    void trace(VkCommandBuffer cmd, const RayBatch& batch, gpu::KernelResources own, const PushConstants& params,
               const WorkOutput* work = nullptr)
    {
        trace(cmd, batch, std::move(own), &params, static_cast<std::uint32_t>(sizeof(PushConstants)), work);
    }

private:
    void trace(VkCommandBuffer cmd, const RayBatch& batch, gpu::KernelResources resources, const void* params,
               std::uint32_t paramsSize, const WorkOutput* work);
    void create(std::optional<gpu::RayTracingPipeline>& pipeline, bool counting) const;

    TracerEnvironment environment_;
    std::string shaderName_;
    TraceEntryPoints entryPoints_;
    std::vector<gpu::KernelBinding> ownBindings_;
    std::uint32_t pushConstantSize_;
    const GpuScene* scene_ = nullptr;
    std::optional<gpu::RayTracingPipeline> timed_;
    std::optional<gpu::RayTracingPipeline> counting_;
};

} // namespace thesis::tracing
