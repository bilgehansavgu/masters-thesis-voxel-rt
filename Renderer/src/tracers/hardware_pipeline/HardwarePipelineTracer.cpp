// Hardware pipeline tracers: the GPU builds and walks the same BVH as the hardware tracer
// (tracing::BoxBvh), but the rays run in a ray tracing pipeline (vkCmdTraceRays) instead of a ray
// query inside a compute shader. This is the model of OptiX: the GPU calls an intersection
// shader for every box its walk reaches.
//
// Three tracers, one per question:
//   hardware_pipeline               pipeline instead of ray query, nothing else changed
//   hardware_pipeline_reorder_hit   + shader execution reordering after the hit (a control: the
//                                   brick tests run before the regrouping)
//   hardware_pipeline_reorder_box   the walk only finds boxes; rays are regrouped by box before
//                                   the brick test, which then runs in the ray generation shader
// The shaders say how each works: shaders/trace_pipeline*.slang.

#include "thesis/gpu/Context.h"
#include "thesis/tracers/hardware_pipeline/BoxOverlap.h"
#include "thesis/tracers/hardware_pipeline/HardwarePipelineShared.h"
#include "thesis/tracing/Bindings.h"
#include "thesis/tracing/BoxBvh.h"
#include "thesis/tracing/GridFrame.h"
#include "thesis/tracing/HiddenStructureDiagram.h"
#include "thesis/tracing/TracePipelines.h"
#include "thesis/tracing/Tracer.h"

#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace thesis::tracers {
namespace {

constexpr std::uint32_t kSceneBinding = THESIS_BINDING_HARDWARE_PIPELINE_SCENE;
static_assert(kSceneBinding >= THESIS_BINDING_TRACER_FIRST && kSceneBinding <= THESIS_BINDING_TRACER_LAST);

// Mirrors TraceParams in shaders/pipeline_stages.slang.
struct TraceParams {
    std::uint32_t rayCount;
    std::uint32_t pad0;
    std::uint32_t pad1;
    std::uint32_t pad2;
};

// What makes one of the three tracers: its name, its words, and the entry points of its pipeline.
struct Variant {
    std::string name;
    std::string description; // one line, for the registry
    tracing::TracerText text;
    std::string brickTest; // what the diagram calls a test of one primitive
    std::string shader;
    tracing::TraceEntryPoints entryPoints;
    bool reorders = false;           // needs shader execution reordering
    bool needsDisjointBoxes = false; // exact only when no two boxes overlap
};

Variant plainVariant()
{
    return {"hardware_pipeline",
            "Hardware pipeline: the RT cores walk the GPU's BVH; brick test in an intersection shader.",
            {"Hardware RT pipeline",
             "The ray tracing units search the BVH that the driver builds from the brick boxes, as in the hardware "
             "tracer. The rays run in a ray tracing pipeline: the GPU calls an intersection shader with the brick "
             "test for every box it reaches, instead of handing the box back to one compute shader."},
            "brick test in the intersection shader",
            "trace_pipeline",
            {"rayGeneration", "miss", "intersectBrick", "closestHit"}};
}

Variant reorderAfterHitVariant()
{
    return {"hardware_pipeline_reorder_hit",
            "Hardware pipeline with shader execution reordering by hit, after the brick tests.",
            {"Hardware RT pipeline, reorder after hit",
             "As the hardware RT pipeline. After the search, the GPU regroups the rays by their hit (shader "
             "execution reordering) before the closest-hit shader reads the hit. The brick tests run during the "
             "search, before the regrouping."},
            "brick test in the intersection shader",
            "trace_pipeline_reorder",
            {"reorderAfterHit", "miss", "intersectBrick", "closestHit"},
            true};
}

Variant reorderBeforeBrickTestVariant()
{
    return {"hardware_pipeline_reorder_box",
            "Hardware pipeline: the BVH walk finds the first box, rays are regrouped by box, then the brick test.",
            {"Hardware RT pipeline, reorder by brick",
             "The ray tracing units only find the brick box that the ray enters first. The GPU regroups the rays "
             "by that box (shader execution reordering), then each ray runs the brick test, so rays in the same "
             "brick test it together. On a miss the ray is traced again from where it leaves the box."},
            "brick test after regrouping by box",
            "trace_pipeline_reorder",
            {"reorderBeforeBrickTest", "miss", "intersectBox", ""},
            true,
            true};
}

class HardwarePipelineTracer final : public tracing::ITracer {
public:
    HardwarePipelineTracer(const tracing::TracerEnvironment& environment, Variant variant)
        : context_(environment.gpu),
          variant_(std::move(variant)),
          pipelines_(environment, variant_.shader, variant_.entryPoints,
                     {{kSceneBinding, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR}}, sizeof(TraceParams))
    {}

    std::string_view name() const override { return variant_.name; }
    tracing::TracerText text() const override { return variant_.text; }
    std::unique_ptr<tracing::IStructureDiagram> diagram(const tracing::DiagramInput& input) const override
    {
        return std::make_unique<tracing::HiddenStructureDiagram>(
            input, tracing::WalkText{"", variant_.brickTest, variant_.text.method});
    }

    tracing::PrepareReport prepare(const tracing::GpuScene& scene) override
    {
        if (!context_.rayTracingPipelines()) {
            throw tracing::UnsupportedScene("This GPU or driver has no ray tracing pipelines "
                                            "(VK_KHR_ray_tracing_pipeline).");
        }
        if (variant_.reorders && !context_.invocationReorder()) {
            throw tracing::UnsupportedScene(
                "This GPU or driver does not reorder rays (VK_NV_ray_tracing_invocation_reorder is missing, or "
                "the driver reports that it ignores the reorder calls).");
        }
        tracing::PrepareReport report;
        if (variant_.needsDisjointBoxes) {
            const auto start = std::chrono::steady_clock::now();
            const std::vector<core::Aabb> boxes = scene.downloadPrimitiveBounds(context_);
            const tracing::GridFrame frame = tracing::chooseGridFrame(boxes.size(), scene.bounds, scene.primitiveGrid);
            const std::optional<BoxOverlap> overlap = findBoxOverlap(boxes, frame);
            report.cpuMilliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (overlap) {
                throw tracing::UnsupportedScene(
                    std::format("Boxes {} and {} overlap. This tracer walks the boxes front to back one after the "
                                "other and is exact only when no two boxes overlap.",
                                overlap->first, overlap->second));
            }
        }
        bvh_ = tracing::BoxBvh::build(context_, scene);
        report.gpuMilliseconds = bvh_.gpuMilliseconds;
        report.memoryBytes = memoryBytes();
        report.shaderStatistics = pipelines_.prepare(scene);
        report.scratchBytes = bvh_.scratchBytes;
        return report;
    }

    void trace(VkCommandBuffer cmd, const tracing::RayBatch& batch) override { launch(cmd, batch, nullptr); }

    bool countsWork() const override { return true; }
    // The GPU does not report the nodes of its BVH.
    bool countsStructureSteps() const override { return false; }

    void traceCounting(VkCommandBuffer cmd, const tracing::RayBatch& batch, const tracing::WorkOutput& work) override
    {
        launch(cmd, batch, &work);
    }

    std::uint64_t memoryBytes() const override { return bvh_.memoryBytes(); }

private:
    void launch(VkCommandBuffer cmd, const tracing::RayBatch& batch, const tracing::WorkOutput* work)
    {
        gpu::KernelResources own;
        own.accelerationStructure(kSceneBinding, bvh_.topLevel.handle());
        pipelines_.trace(cmd, batch, std::move(own), TraceParams{batch.count(), 0, 0, 0}, work);
    }

    const gpu::Context& context_;
    Variant variant_;
    tracing::TracePipelines pipelines_;
    tracing::BoxBvh bvh_;
};

void add(tracing::TracerRegistry& registry, Variant variant)
{
    std::string name = variant.name;
    std::string description = variant.description;
    registry.add(std::move(name), std::move(description),
                 [variant = std::move(variant)](const tracing::TracerEnvironment& environment) {
                     return std::make_unique<HardwarePipelineTracer>(environment, variant);
                 });
}

} // namespace
} // namespace thesis::tracers

namespace thesis::plugins {

void registerHardwarePipelineTracers(tracing::TracerRegistry& registry)
{
    tracers::add(registry, tracers::plainVariant());
    tracers::add(registry, tracers::reorderAfterHitVariant());
    tracers::add(registry, tracers::reorderBeforeBrickTestVariant());
}

} // namespace thesis::plugins
