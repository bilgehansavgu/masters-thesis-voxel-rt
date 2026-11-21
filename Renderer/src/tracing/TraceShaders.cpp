#include "TraceShaders.h"

#include "thesis/tracing/Bindings.h"

namespace thesis::tracing {

std::vector<gpu::KernelBinding> traceBindings(const std::vector<gpu::KernelBinding>& own, const GpuScene& scene,
                                              bool counting)
{
    std::vector<gpu::KernelBinding> bindings{{kRayBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                             {kHitBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}};
    bindings.insert(bindings.end(), own.begin(), own.end());
    if (counting) {
        bindings.push_back({kRayWorkBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
        bindings.push_back({kRayLogBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    }
    for (const gpu::KernelBinding& binding : scene.primitiveBindings()) {
        bindings.push_back(binding);
    }
    return bindings;
}

std::filesystem::path traceShaderPath(const TracerEnvironment& environment, const std::string& shaderName,
                                      const GpuScene& scene, bool counting)
{
    return environment.shaderDirectory /
           (shaderName + "." + scene.primitiveKind + (counting ? ".counting.spv" : ".spv"));
}

void bindTraceResources(gpu::KernelResources& resources, const RayBatch& batch, const GpuScene& scene,
                        const WorkOutput* work)
{
    resources.buffer(kRayBinding, batch.rays).buffer(kHitBinding, batch.hits);
    if (work != nullptr) {
        resources.buffer(kRayWorkBinding, work->perRay).buffer(kRayLogBinding, work->log);
    }
    scene.bindPrimitiveData(resources);
}

} // namespace thesis::tracing
