// BVH tracer: a compute shader that walks our own BVH over the boxes of the primitives.
// No help from the ray tracing hardware.
//
// The hardware tracer also walks a BVH over the same boxes, but the GPU builds that one and its
// ray tracing cores walk it. This tracer is the same kind of structure in plain shader code,
// so the two differ in who walks the tree, not in the idea. The BVH (Bvh.h) is built on the
// CPU when a scene is prepared.

#include "BvhDiagram.h"
#include "thesis/tracers/software_bvh/Bvh.h"

#include "thesis/gpu/Buffer.h"
#include "thesis/tracers/software_bvh/BvhShared.h"
#include "thesis/tracing/Bindings.h"
#include "thesis/tracing/TraceKernels.h"
#include "thesis/tracing/Tracer.h"

#include <chrono>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace thesis::tracers {
namespace {

constexpr std::uint32_t kNodesBinding = THESIS_BINDING_BVH_NODES;
static_assert(kNodesBinding >= THESIS_BINDING_TRACER_FIRST && kNodesBinding <= THESIS_BINDING_TRACER_LAST);

// Mirrors TraceParams in trace_software_bvh.slang.
struct TraceParams {
    std::uint32_t rayCount;
    std::uint32_t pad0;
    std::uint32_t pad1;
    std::uint32_t pad2;
};

class BvhTracer final : public tracing::ITracer {
public:
    explicit BvhTracer(const tracing::TracerEnvironment& environment)
        : context_(environment.gpu),
          kernels_(environment, "trace_software_bvh", {{kNodesBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
                   sizeof(TraceParams))
    {}

    std::string_view name() const override { return "software_bvh"; }
    tracing::TracerText text() const override
    {
        return {"Software BVH",
                "A tree of boxes (a BVH). Each box encloses a group of bricks and follows the shape of the model. The "
                "ray skips every box it misses. The GPU uses the same kind of tree, walked by its own units.",
                "tree boxes"};
    }
    std::unique_ptr<tracing::IStructureDiagram> diagram(const tracing::DiagramInput& input) const override
    {
        return makeBvhDiagram(input);
    }
    std::string describeStructureStep(const core::RayLogEntry& step) const override
    {
        return std::format("BVH node {}", step.a);
    }

    tracing::PrepareReport prepare(const tracing::GpuScene& scene) override
    {
        const std::vector<core::Aabb> boxes = scene.downloadPrimitiveBounds(context_);

        const auto start = std::chrono::steady_clock::now();
        const Bvh bvh = buildBvh(boxes);
        const double buildMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (bvh.depth > THESIS_BVH_STACK_SIZE) {
            throw tracing::UnsupportedScene("The BVH of this scene is " + std::to_string(bvh.depth) +
                                            " levels deep, more than the stack of the shader holds (" +
                                            std::to_string(THESIS_BVH_STACK_SIZE) + ")");
        }

        nodes_ = gpu::uploadBuffer(context_, std::as_bytes(std::span(bvh.nodes)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::vector<gpu::ShaderStatistic> shaderStatistics = kernels_.prepare(scene);

        tracing::PrepareReport report;
        report.cpuMilliseconds = buildMilliseconds;
        report.memoryBytes = memoryBytes();
        report.shaderStatistics = std::move(shaderStatistics);
        return report;
    }

    void trace(VkCommandBuffer cmd, const tracing::RayBatch& batch) override { dispatch(cmd, batch, nullptr); }

    bool countsWork() const override { return true; }

    void traceCounting(VkCommandBuffer cmd, const tracing::RayBatch& batch, const tracing::WorkOutput& work) override
    {
        dispatch(cmd, batch, &work);
    }

    std::uint64_t memoryBytes() const override { return nodes_.size(); }

private:
    void dispatch(VkCommandBuffer cmd, const tracing::RayBatch& batch, const tracing::WorkOutput* work)
    {
        gpu::KernelResources own;
        own.buffer(kNodesBinding, nodes_);
        kernels_.dispatch(cmd, batch, std::move(own), TraceParams{batch.count(), 0, 0, 0}, work);
    }

    const gpu::Context& context_;
    tracing::TraceKernels kernels_;
    gpu::Buffer nodes_; // Bvh::nodes
};

} // namespace
} // namespace thesis::tracers

namespace thesis::plugins {

void registerSoftwareBvhTracer(tracing::TracerRegistry& registry)
{
    registry.add("software_bvh", "Software: our own BVH over the boxes, built on the CPU and walked in the shader.",
                 [](const tracing::TracerEnvironment& environment) {
                     return std::make_unique<tracers::BvhTracer>(environment);
                 });
}

} // namespace thesis::plugins
