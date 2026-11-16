// Software tracer: a compute shader with no help from the ray tracing hardware.
//
// Its search structure is a uniform grid over the primitive boxes (UniformGrid.h), built on the
// CPU when a scene is prepared. The shader walks the grid cell by cell (trace_software.slang).
// Step 3 compares this plain grid with better structures.

#include "thesis/tracers/software/UniformGrid.h"

#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/ComputeKernel.h"
#include "thesis/tracing/Bindings.h"
#include "thesis/tracing/Tracer.h"

#include <glm/vec3.hpp>

#include <chrono>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace thesis::tracers {
namespace {

constexpr std::uint32_t kGroupSize = 64; // matches [numthreads] in trace_software.slang
constexpr std::uint32_t kGridCellsBinding = THESIS_BINDING_SOFTWARE_GRID_CELLS;
constexpr std::uint32_t kGridItemsBinding = THESIS_BINDING_SOFTWARE_GRID_ITEMS;

// Mirrors TraceParams in trace_software.slang.
struct TraceParams {
    glm::vec3 gridOrigin;
    std::uint32_t rayCount;
    glm::vec3 cellSize;
    std::uint32_t pad0;
    glm::uvec3 gridSize;
    std::uint32_t pad1;
};
static_assert(sizeof(TraceParams) == 48);

class SoftwareTracer final : public tracing::ITracer {
public:
    explicit SoftwareTracer(const tracing::TracerEnvironment& environment) : environment_(environment) {}

    std::string_view name() const override { return "software"; }

    tracing::PrepareReport prepare(const tracing::GpuScene& scene) override
    {
        const gpu::Context& context = environment_.gpu;

        // The grid is built from the boxes alone, the same input that the GPU gets for its BVH.
        const std::vector<std::byte> bytes = gpu::downloadBuffer(context, scene.primitiveBounds);
        std::vector<core::Aabb> boxes(scene.primitiveCount);
        if (bytes.size() < boxes.size() * sizeof(core::Aabb)) {
            throw std::logic_error("SoftwareTracer: the bounds buffer is smaller than the primitive count");
        }
        std::memcpy(boxes.data(), bytes.data(), boxes.size() * sizeof(core::Aabb));

        const auto start = std::chrono::steady_clock::now();
        UniformGrid grid;
        try {
            grid = buildUniformGrid(boxes, scene.bounds, scene.primitiveGrid);
        } catch (const std::length_error& reason) {
            throw tracing::UnsupportedScene(reason.what());
        }
        const double buildMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        scene_ = &scene;
        params_ = {grid.origin, 0, grid.cellSize, 0, grid.size, 0};
        cells_ =
            gpu::uploadBuffer(context, std::as_bytes(std::span(grid.cellStart)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        items_ = gpu::uploadBuffer(context, std::as_bytes(std::span(grid.items)), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        countingKernel_.reset();
        createKernel(kernel_, false);

        tracing::PrepareReport report;
        report.cpuMilliseconds = buildMilliseconds;
        report.memoryBytes = memoryBytes();
        return report;
    }

    void trace(VkCommandBuffer cmd, const tracing::RayBatch& batch) override
    {
        if (!kernel_ || scene_ == nullptr) {
            throw std::logic_error("SoftwareTracer: prepare() must be called before trace()");
        }
        dispatch(cmd, *kernel_, batch, nullptr);
    }

    bool countsWork() const override { return true; }

    void traceCounting(VkCommandBuffer cmd, const tracing::RayBatch& batch, const gpu::Buffer& work) override
    {
        if (scene_ == nullptr) {
            throw std::logic_error("SoftwareTracer: prepare() must be called before traceCounting()");
        }
        if (!countingKernel_) {
            createKernel(countingKernel_, true);
        }
        dispatch(cmd, *countingKernel_, batch, &work);
    }

    std::uint64_t memoryBytes() const override { return cells_.size() + items_.size(); }

private:
    void createKernel(std::optional<gpu::ComputeKernel>& kernel, bool counting) const
    {
        std::vector<gpu::KernelBinding> bindings{{tracing::kRayBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                 {tracing::kHitBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                 {kGridCellsBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                                 {kGridItemsBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}};
        if (counting) {
            bindings.push_back({tracing::kRayWorkBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
        }
        for (const gpu::KernelBinding& binding : scene_->primitiveBindings()) {
            bindings.push_back(binding);
        }
        const std::string file = "trace_software." + scene_->primitiveKind + (counting ? ".counting.spv" : ".spv");
        kernel.emplace(environment_.gpu, gpu::KernelDescription{environment_.shaderDirectory / file,
                                                                std::move(bindings), sizeof(TraceParams)});
    }

    void dispatch(VkCommandBuffer cmd, const gpu::ComputeKernel& kernel, const tracing::RayBatch& batch,
                  const gpu::Buffer* work) const
    {
        gpu::KernelResources resources;
        resources.buffer(tracing::kRayBinding, batch.rays)
            .buffer(tracing::kHitBinding, batch.hits)
            .buffer(kGridCellsBinding, cells_)
            .buffer(kGridItemsBinding, items_);
        if (work != nullptr) {
            resources.buffer(tracing::kRayWorkBinding, *work);
        }
        scene_->bindPrimitiveData(resources);
        TraceParams params = params_;
        params.rayCount = batch.count();
        kernel.dispatch(cmd, resources, gpu::groupCount(batch.count(), kGroupSize), 1, 1, params);
    }

    tracing::TracerEnvironment environment_;
    const tracing::GpuScene* scene_ = nullptr;
    TraceParams params_{};
    gpu::Buffer cells_; // UniformGrid::cellStart
    gpu::Buffer items_; // UniformGrid::items
    std::optional<gpu::ComputeKernel> kernel_;
    std::optional<gpu::ComputeKernel> countingKernel_; // created when it is first needed
};

} // namespace
} // namespace thesis::tracers

namespace thesis::plugins {

void registerSoftwareTracer(tracing::TracerRegistry& registry)
{
    registry.add("software",
                 "Compute shader, no ray tracing hardware. Walks its own uniform grid over the primitive boxes.",
                 [](const tracing::TracerEnvironment& environment) {
                     return std::make_unique<tracers::SoftwareTracer>(environment);
                 });
}

} // namespace thesis::plugins
