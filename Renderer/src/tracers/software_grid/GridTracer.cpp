// Grid tracer: a compute shader that walks a uniform grid over the boxes of the primitives.
// No help from the ray tracing hardware.
//
// The grid (UniformGrid.h) is built on the CPU when a scene is prepared. It is the plainest
// search structure: every cell is stored, also the empty ones, and a ray takes one step for
// each cell on its way. The octree and the BVH tracers are the structures with a hierarchy.

#include "thesis/tracers/software_grid/UniformGrid.h"

#include "thesis/gpu/Buffer.h"
#include "thesis/tracing/Bindings.h"
#include "thesis/tracing/TraceKernels.h"
#include "thesis/tracing/Tracer.h"

#include <glm/vec3.hpp>

#include <chrono>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace thesis::tracers {
namespace {

constexpr std::uint32_t kCellsBinding = THESIS_BINDING_GRID_CELLS;
constexpr std::uint32_t kItemsBinding = THESIS_BINDING_GRID_ITEMS;

// Mirrors TraceParams in trace_software_grid.slang.
struct TraceParams {
    glm::vec3 gridOrigin;
    std::uint32_t rayCount;
    glm::vec3 cellSize;
    std::uint32_t pad0;
    glm::uvec3 gridSize;
    std::uint32_t pad1;
};
static_assert(sizeof(TraceParams) == 48);

class GridTracer final : public tracing::ITracer {
public:
    explicit GridTracer(const tracing::TracerEnvironment& environment)
        : context_(environment.gpu),
          kernels_(
              environment, "trace_software_grid",
              {{kCellsBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, {kItemsBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
              sizeof(TraceParams))
    {}

    std::string_view name() const override { return "software_grid"; }

    tracing::PrepareReport prepare(const tracing::GpuScene& scene) override
    {
        const std::vector<core::Aabb> boxes = scene.downloadPrimitiveBounds(context_);

        const auto start = std::chrono::steady_clock::now();
        UniformGrid grid;
        try {
            grid = buildUniformGrid(boxes, chooseGridFrame(boxes.size(), scene.bounds, scene.primitiveGrid));
        } catch (const std::length_error& reason) {
            throw tracing::UnsupportedScene(reason.what());
        }
        const double buildMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        params_ = {grid.frame.origin, 0, grid.frame.cellSize, 0, grid.frame.size, 0};
        cells_ =
            gpu::uploadBuffer(context_, std::as_bytes(std::span(grid.cellStart)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        items_ = gpu::uploadBuffer(context_, std::as_bytes(std::span(grid.items)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
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

    std::uint64_t memoryBytes() const override { return cells_.size() + items_.size(); }

private:
    void dispatch(VkCommandBuffer cmd, const tracing::RayBatch& batch, const tracing::WorkOutput* work)
    {
        gpu::KernelResources own;
        own.buffer(kCellsBinding, cells_).buffer(kItemsBinding, items_);
        TraceParams params = params_;
        params.rayCount = batch.count();
        kernels_.dispatch(cmd, batch, std::move(own), params, work);
    }

    const gpu::Context& context_;
    tracing::TraceKernels kernels_;
    TraceParams params_{};
    gpu::Buffer cells_; // UniformGrid::cellStart
    gpu::Buffer items_; // UniformGrid::items
};

} // namespace
} // namespace thesis::tracers

namespace thesis::plugins {

void registerSoftwareGridTracer(tracing::TracerRegistry& registry)
{
    registry.add("software_grid",
                 "Software: a uniform grid over the boxes. One step for each cell, also the empty ones.",
                 [](const tracing::TracerEnvironment& environment) {
                     return std::make_unique<tracers::GridTracer>(environment);
                 });
}

} // namespace thesis::plugins
