#include "thesis/tracing/TraceKernels.h"

#include "TraceShaders.h"

#include <stdexcept>

namespace thesis::tracing {

TraceKernels::TraceKernels(const TracerEnvironment& environment, std::string shaderName,
                           std::vector<gpu::KernelBinding> ownBindings, std::uint32_t pushConstantSize)
    : environment_(environment),
      shaderName_(std::move(shaderName)),
      ownBindings_(std::move(ownBindings)),
      pushConstantSize_(pushConstantSize)
{}

std::vector<gpu::ShaderStatistic> TraceKernels::prepare(const GpuScene& scene)
{
    scene_ = &scene;
    counting_.reset();
    create(timed_, false);
    return timed_->statistics();
}

void TraceKernels::create(std::optional<gpu::ComputeKernel>& kernel, bool counting) const
{
    kernel.emplace(environment_.gpu,
                   gpu::KernelDescription{traceShaderPath(environment_, shaderName_, *scene_, counting),
                                          traceBindings(ownBindings_, *scene_, counting), pushConstantSize_});
}

void TraceKernels::dispatch(VkCommandBuffer cmd, const RayBatch& batch, gpu::KernelResources resources,
                            const void* params, std::uint32_t paramsSize, const WorkOutput* work)
{
    if (scene_ == nullptr || !timed_) {
        throw std::logic_error("TraceKernels '" + shaderName_ + "': prepare() must be called before a dispatch");
    }
    if (work != nullptr && !counting_) {
        create(counting_, true);
    }
    bindTraceResources(resources, batch, *scene_, work);
    const gpu::ComputeKernel& kernel = work != nullptr ? *counting_ : *timed_;
    kernel.dispatch(cmd, resources, gpu::groupCount(batch.count(), kTraceGroupSize), 1, 1, params, paramsSize);
}

} // namespace thesis::tracing
