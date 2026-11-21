#include "thesis/tracing/TracePipelines.h"

#include "TraceShaders.h"

#include <stdexcept>

namespace thesis::tracing {

TracePipelines::TracePipelines(const TracerEnvironment& environment, std::string shaderName,
                               TraceEntryPoints entryPoints, std::vector<gpu::KernelBinding> ownBindings,
                               std::uint32_t pushConstantSize)
    : environment_(environment),
      shaderName_(std::move(shaderName)),
      entryPoints_(std::move(entryPoints)),
      ownBindings_(std::move(ownBindings)),
      pushConstantSize_(pushConstantSize)
{}

std::vector<gpu::ShaderStatistic> TracePipelines::prepare(const GpuScene& scene)
{
    scene_ = &scene;
    create(timed_, false);
    // WHY both now, not the counting one on first use as TraceKernels does: creating a pipeline
    // uploads its shader binding table, which submits work and waits. trace() runs while the
    // caller records a command buffer, where that is not allowed.
    create(counting_, true);
    return timed_->statistics();
}

void TracePipelines::create(std::optional<gpu::RayTracingPipeline>& pipeline, bool counting) const
{
    gpu::RayTracingPipelineDescription description;
    description.spirvPath = traceShaderPath(environment_, shaderName_, *scene_, counting);
    description.bindings = traceBindings(ownBindings_, *scene_, counting);
    description.pushConstantSize = pushConstantSize_;
    description.rayGeneration = entryPoints_.rayGeneration;
    description.miss = entryPoints_.miss;
    description.intersection = entryPoints_.intersection;
    description.closestHit = entryPoints_.closestHit;
    pipeline.emplace(environment_.gpu, description);
}

void TracePipelines::trace(VkCommandBuffer cmd, const RayBatch& batch, gpu::KernelResources resources,
                           const void* params, std::uint32_t paramsSize, const WorkOutput* work)
{
    if (scene_ == nullptr || !timed_ || !counting_) {
        throw std::logic_error("TracePipelines '" + shaderName_ + "': prepare() must be called before a trace");
    }
    bindTraceResources(resources, batch, *scene_, work);
    const gpu::RayTracingPipeline& pipeline = work != nullptr ? *counting_ : *timed_;
    pipeline.trace(cmd, resources, batch.count(), 1, 1, params, paramsSize);
}

} // namespace thesis::tracing
