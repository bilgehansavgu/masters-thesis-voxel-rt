#include "thesis/tracing/TraceKernels.h"

#include "thesis/tracing/Bindings.h"

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
    std::vector<gpu::KernelBinding> bindings{{kRayBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                             {kHitBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}};
    bindings.insert(bindings.end(), ownBindings_.begin(), ownBindings_.end());
    if (counting) {
        bindings.push_back({kRayWorkBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
        bindings.push_back({kRayLogBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    }
    for (const gpu::KernelBinding& binding : scene_->primitiveBindings()) {
        bindings.push_back(binding);
    }
    const std::string file = shaderName_ + "." + scene_->primitiveKind + (counting ? ".counting.spv" : ".spv");
    kernel.emplace(environment_.gpu,
                   gpu::KernelDescription{environment_.shaderDirectory / file, std::move(bindings), pushConstantSize_});
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
    resources.buffer(kRayBinding, batch.rays).buffer(kHitBinding, batch.hits);
    if (work != nullptr) {
        resources.buffer(kRayWorkBinding, work->perRay).buffer(kRayLogBinding, work->log);
    }
    scene_->bindPrimitiveData(resources);
    const gpu::ComputeKernel& kernel = work != nullptr ? *counting_ : *timed_;
    kernel.dispatch(cmd, resources, gpu::groupCount(batch.count(), kTraceGroupSize), 1, 1, params, paramsSize);
}

} // namespace thesis::tracing
