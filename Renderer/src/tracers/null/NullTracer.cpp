// Null tracer: a measurement baseline, not a real tracer.
//
// It does the part every tracer must do (read each ray, write a hit) and nothing else; every
// ray reports a miss. Its time is the floor that moving the ray and hit lists through memory
// costs, so a tracer's real search cost is roughly (tracer time - null time).

#include "thesis/gpu/ComputeKernel.h"
#include "thesis/tracing/Bindings.h"
#include "thesis/tracing/Tracer.h"

#include <memory>
#include <optional>
#include <stdexcept>

namespace thesis::tracers {
namespace {

constexpr std::uint32_t kGroupSize = 64; // matches [numthreads] in trace_null.slang

struct TraceParams {
    std::uint32_t rayCount;
    std::uint32_t pad0;
    std::uint32_t pad1;
    std::uint32_t pad2;
};

class NullTracer final : public tracing::ITracer {
public:
    explicit NullTracer(const tracing::TracerEnvironment& environment) : environment_(environment) {}

    std::string_view name() const override { return "null"; }

    tracing::PrepareReport prepare(const tracing::GpuScene& /*scene*/) override
    {
        kernel_.emplace(environment_.gpu,
                        gpu::KernelDescription{environment_.shaderDirectory / "trace_null.spv",
                                               {{tracing::kRayBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                {tracing::kHitBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
                                               sizeof(TraceParams)});
        return {};
    }

    void trace(VkCommandBuffer cmd, const tracing::RayBatch& batch) override
    {
        if (!kernel_) {
            throw std::logic_error("NullTracer: prepare() must be called before trace()");
        }
        gpu::KernelResources resources;
        resources.buffer(tracing::kRayBinding, batch.rays).buffer(tracing::kHitBinding, batch.hits);
        const TraceParams params{batch.count(), 0, 0, 0};
        kernel_->dispatch(cmd, resources, gpu::groupCount(batch.count(), kGroupSize), 1, 1, params);
    }

    std::uint64_t memoryBytes() const override { return 0; }
    bool findsHits() const override { return false; }

private:
    tracing::TracerEnvironment environment_;
    std::optional<gpu::ComputeKernel> kernel_;
};

} // namespace
} // namespace thesis::tracers

namespace thesis::plugins {

void registerNullTracer(tracing::TracerRegistry& registry)
{
    registry.add("null", "Baseline: reads every ray, writes a miss. Its time is the memory-traffic floor.",
                 [](const tracing::TracerEnvironment& environment) {
                     return std::make_unique<tracers::NullTracer>(environment);
                 });
}

} // namespace thesis::plugins
