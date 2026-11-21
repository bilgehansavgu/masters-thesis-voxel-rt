#pragma once

// Side: GPU

#include "thesis/gpu/ComputeKernel.h"
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

// Threads in one work group of every tracer shader: [numthreads(64, 1, 1)].
inline constexpr std::uint32_t kTraceGroupSize = 64;

// The compute shader of one tracer, for the primitive kind of one scene.
//
// Every tracer shader exists in two variants: the timed one, and the counting one that also
// writes the work of each ray (shaders/common/work.slang). This class loads
// <shader>.<kind>.spv when the scene is known, and <shader>.<kind>.counting.spv when counting
// is asked for the first time. It adds the bindings that every tracer has (rays, hits, the data
// of the primitive kind, the work list), so a tracer names only its own buffers.
class TraceKernels {
public:
    // `shaderName` is the file name without kind and extension, for example "trace_hardware".
    // `ownBindings` are the buffers of the tracer itself (bindings kTracerBindingFirst to
    // kTracerBindingLast). `pushConstantSize` is the size of the tracer's parameter struct.
    TraceKernels(const TracerEnvironment& environment, std::string shaderName,
                 std::vector<gpu::KernelBinding> ownBindings, std::uint32_t pushConstantSize);

    // Loads the timed shader for the primitive kind of `scene`. `scene` must outlive every
    // dispatch. Call it from ITracer::prepare. Returns what the driver made of the timed shader
    // (for PrepareReport::shaderStatistics).
    std::vector<gpu::ShaderStatistic> prepare(const GpuScene& scene);

    // Records one dispatch over all rays of `batch`. `own` holds the buffers of the tracer
    // itself. With `work`, the counting variant runs and writes the work of each ray and the ray
    // log to it.
    template <typename PushConstants>
    void dispatch(VkCommandBuffer cmd, const RayBatch& batch, gpu::KernelResources own, const PushConstants& params,
                  const WorkOutput* work = nullptr)
    {
        dispatch(cmd, batch, std::move(own), &params, static_cast<std::uint32_t>(sizeof(PushConstants)), work);
    }

private:
    void dispatch(VkCommandBuffer cmd, const RayBatch& batch, gpu::KernelResources resources, const void* params,
                  std::uint32_t paramsSize, const WorkOutput* work);
    void create(std::optional<gpu::ComputeKernel>& kernel, bool counting) const;

    TracerEnvironment environment_;
    std::string shaderName_;
    std::vector<gpu::KernelBinding> ownBindings_;
    std::uint32_t pushConstantSize_;
    const GpuScene* scene_ = nullptr;
    std::optional<gpu::ComputeKernel> timed_;
    std::optional<gpu::ComputeKernel> counting_; // created when it is first needed
};

} // namespace thesis::tracing
