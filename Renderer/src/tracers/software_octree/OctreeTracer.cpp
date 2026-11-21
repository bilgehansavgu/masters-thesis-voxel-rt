// Octree tracer: a compute shader that walks a sparse octree over the boxes of the primitives.
// No help from the ray tracing hardware.
//
// This is the kind of structure that Reichl et al. 2012 use for their own ray caster. The
// octree (Octree.h) is built on the CPU when a scene is prepared. It stands on the same cells
// as the uniform grid of the grid tracer, but it stores only the parts of space that hold a
// primitive.

#include "OctreeDiagram.h"
#include "thesis/tracers/software_octree/Octree.h"
#include "thesis/tracers/software_octree/OctreeShared.h"

#include "thesis/gpu/Buffer.h"
#include "thesis/tracing/Bindings.h"
#include "thesis/tracing/TraceKernels.h"
#include "thesis/tracing/Tracer.h"

#include <glm/vec3.hpp>

#include <chrono>
#include <format>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace thesis::tracers {
namespace {

constexpr std::uint32_t kNodesBinding = THESIS_BINDING_OCTREE_NODES;
constexpr std::uint32_t kItemsBinding = THESIS_BINDING_OCTREE_ITEMS;
static_assert(kNodesBinding >= THESIS_BINDING_TRACER_FIRST && kItemsBinding <= THESIS_BINDING_TRACER_LAST);

// Mirrors TraceParams in trace_software_octree.slang.
struct TraceParams {
    glm::vec3 origin;
    std::uint32_t rayCount;
    glm::vec3 cellSize;
    std::uint32_t depth;
};
static_assert(sizeof(TraceParams) == 32);

class OctreeTracer final : public tracing::ITracer {
public:
    explicit OctreeTracer(const tracing::TracerEnvironment& environment)
        : context_(environment.gpu),
          kernels_(
              environment, "trace_software_octree",
              {{kNodesBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, {kItemsBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
              sizeof(TraceParams))
    {}

    std::string_view name() const override { return "software_octree"; }
    tracing::TracerText text() const override
    {
        return {"Sparse octree",
                "Space is cut into 8 cubes, and each cube again into 8, down to the size of one brick. Empty cubes are "
                "not cut further, so the ray crosses a large empty cube in one step.",
                "octree cubes"};
    }
    std::unique_ptr<tracing::IStructureDiagram> diagram(const tracing::DiagramInput& input) const override
    {
        return makeOctreeDiagram(input);
    }
    std::string describeStructureStep(const core::RayLogEntry& step) const override
    {
        return step.d == 0 ? std::format("octree cell ({}, {}, {})", step.a, step.b, step.c)
                           : std::format("octree node of {0}x{0}x{0} cells around cell ({1}, {2}, {3})", 1u << step.d,
                                         step.a, step.b, step.c);
    }

    tracing::PrepareReport prepare(const tracing::GpuScene& scene) override
    {
        const std::vector<core::Aabb> boxes = scene.downloadPrimitiveBounds(context_);

        const auto start = std::chrono::steady_clock::now();
        Octree octree;
        try {
            octree = buildOctree(boxes, tracing::chooseGridFrame(boxes.size(), scene.bounds, scene.primitiveGrid));
        } catch (const std::length_error& reason) {
            throw tracing::UnsupportedScene(reason.what());
        }
        const double buildMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        params_ = {octree.origin, 0, octree.cellSize, octree.depth};
        nodes_ =
            gpu::uploadBuffer(context_, std::as_bytes(std::span(octree.nodes)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        items_ =
            gpu::uploadBuffer(context_, std::as_bytes(std::span(octree.items)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
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

    std::uint64_t memoryBytes() const override { return nodes_.size() + items_.size(); }

private:
    void dispatch(VkCommandBuffer cmd, const tracing::RayBatch& batch, const tracing::WorkOutput* work)
    {
        gpu::KernelResources own;
        own.buffer(kNodesBinding, nodes_).buffer(kItemsBinding, items_);
        TraceParams params = params_;
        params.rayCount = batch.count();
        kernels_.dispatch(cmd, batch, std::move(own), params, work);
    }

    const gpu::Context& context_;
    tracing::TraceKernels kernels_;
    TraceParams params_{};
    gpu::Buffer nodes_; // Octree::nodes
    gpu::Buffer items_; // Octree::items
};

} // namespace
} // namespace thesis::tracers

namespace thesis::plugins {

void registerSoftwareOctreeTracer(tracing::TracerRegistry& registry)
{
    registry.add("software_octree", "Software: a sparse octree over the boxes. Empty space takes a few large steps.",
                 [](const tracing::TracerEnvironment& environment) {
                     return std::make_unique<tracers::OctreeTracer>(environment);
                 });
}

} // namespace thesis::plugins
