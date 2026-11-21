// Hardware tracer: the GPU builds and walks its own search structure (a BVH) in the ray
// tracing cores; our code only answers "where does this ray hit inside this primitive's box?".
//
// Uses ray queries from a compute shader (VK_KHR_ray_query): same shader stage and dispatch as
// the software tracers, so the comparison isolates the search structure. The ray tracing
// pipeline variant (intersection shaders, as in OptiX) is another plugin.

#include "thesis/tracers/hardware/HardwareShared.h"
#include "thesis/tracing/Bindings.h"
#include "thesis/tracing/BoxBvh.h"
#include "thesis/tracing/HiddenStructureDiagram.h"
#include "thesis/tracing/TraceKernels.h"
#include "thesis/tracing/Tracer.h"

#include <memory>
#include <utility>

namespace thesis::tracers {
namespace {

constexpr std::uint32_t kSceneBinding = THESIS_BINDING_HARDWARE_SCENE;
static_assert(kSceneBinding >= THESIS_BINDING_TRACER_FIRST && kSceneBinding <= THESIS_BINDING_TRACER_LAST);

// Mirrors TraceParams in trace_hardware.slang.
struct TraceParams {
    std::uint32_t rayCount;
    std::uint32_t pad0;
    std::uint32_t pad1;
    std::uint32_t pad2;
};

class HardwareTracer final : public tracing::ITracer {
public:
    explicit HardwareTracer(const tracing::TracerEnvironment& environment)
        : context_(environment.gpu),
          kernels_(environment, "trace_hardware", {{kSceneBinding, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR}},
                   sizeof(TraceParams))
    {}

    std::string_view name() const override { return "hardware"; }
    tracing::TracerText text() const override
    {
        return {"Hardware ray tracing", "The GPU's ray tracing units search a tree (a BVH) that the GPU driver builds "
                                        "from the brick boxes. The program cannot see the steps of that search."};
    }
    std::unique_ptr<tracing::IStructureDiagram> diagram(const tracing::DiagramInput& input) const override
    {
        return std::make_unique<tracing::HiddenStructureDiagram>(
            input, tracing::WalkText{
                       "", "candidate brick from RT traversal",
                       "Hardware: the driver builds its own BVH from the brick AABBs, and the RT cores walk it. The "
                       "shader only sees the candidate bricks that the traversal reports, and runs the same brick "
                       "test as the software tracers."});
    }

    tracing::PrepareReport prepare(const tracing::GpuScene& scene) override
    {
        bvh_ = tracing::BoxBvh::build(context_, scene);
        tracing::PrepareReport report;
        report.gpuMilliseconds = bvh_.gpuMilliseconds;
        report.memoryBytes = memoryBytes();
        report.shaderStatistics = kernels_.prepare(scene);
        report.scratchBytes = bvh_.scratchBytes;
        return report;
    }

    void trace(VkCommandBuffer cmd, const tracing::RayBatch& batch) override { dispatch(cmd, batch, nullptr); }

    bool countsWork() const override { return true; }
    bool countsStructureSteps() const override { return false; }

    void traceCounting(VkCommandBuffer cmd, const tracing::RayBatch& batch, const tracing::WorkOutput& work) override
    {
        dispatch(cmd, batch, &work);
    }

    std::uint64_t memoryBytes() const override { return bvh_.memoryBytes(); }

private:
    void dispatch(VkCommandBuffer cmd, const tracing::RayBatch& batch, const tracing::WorkOutput* work)
    {
        gpu::KernelResources own;
        own.accelerationStructure(kSceneBinding, bvh_.topLevel.handle());
        kernels_.dispatch(cmd, batch, std::move(own), TraceParams{batch.count(), 0, 0, 0}, work);
    }

    const gpu::Context& context_;
    tracing::TraceKernels kernels_;
    tracing::BoxBvh bvh_;
};

} // namespace
} // namespace thesis::tracers

namespace thesis::plugins {

void registerHardwareTracer(tracing::TracerRegistry& registry)
{
    registry.add(
        "hardware", "Hardware: the ray tracing cores walk a BVH that the GPU builds (ray queries).",
        [](const tracing::TracerEnvironment& environment) {
            return std::make_unique<tracers::HardwareTracer>(environment);
        },
        core::Pick::Default);
}

} // namespace thesis::plugins
