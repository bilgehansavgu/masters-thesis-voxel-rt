#pragma once

// Side: GPU

#include "thesis/core/Registry.h"
#include "thesis/gpu/ComputeKernel.h"
#include "thesis/tracing/GpuScene.h"
#include "thesis/tracing/RayBatch.h"
#include "thesis/tracing/StructureDiagram.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace thesis::gpu {
class Context;
}

namespace thesis::tracing {

// What every tracer gets when it is created.
struct TracerEnvironment {
    const gpu::Context& gpu;
    std::filesystem::path shaderDirectory;
};

// What building a tracer's search structure cost.
struct PrepareReport {
    double gpuMilliseconds = 0.0;   // GPU time of the build
    double cpuMilliseconds = 0.0;   // CPU time of the build, for the part that runs on the CPU
    std::uint64_t memoryBytes = 0;  // memory the tracer keeps for this scene
    std::uint64_t scratchBytes = 0; // temporary memory the build needed
    // What the driver made of the timed shader of the tracer, for example its register count.
    // Empty if the driver does not report it.
    std::vector<gpu::ShaderStatistic> shaderStatistics;
};

// Thrown by ITracer::prepare() when a tracer cannot handle a scene (for example too many
// primitives for a tracer without a search structure). The message says why.
class UnsupportedScene : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// What people read about a tracer in the window. The tracer owns it, so no code outside the
// tracer's folder needs to know its name.
struct TracerText {
    std::string label;  // a few words, for example "Sparse octree"
    std::string method; // how the search works, one to three sentences; empty for a baseline
    // What core::RayWork::structureSteps counts, plural (for example "octree cubes"). Empty if the
    // tracer does not count them (see ITracer::countsStructureSteps).
    std::string structureStep;
};

// Where the counting variant of a tracer writes (ITracer::traceCounting).
//   perRay  core::RayWork for each ray of the batch
//   log     core::RayLogEntry[1 + THESIS_RAY_LOG_CAPACITY]: the steps of the one ray that its
//           header names (thesis/shared/RayLog.h). The caller writes the header.
struct WorkOutput {
    const gpu::Buffer& perRay;
    const gpu::Buffer& log;
};

// THE TRACER SOCKET. One rule: a list of rays goes in, a list of hits comes out.
//
// A tracer owns how rays find primitives (its search structure); it does not own the rays,
// the shading, or the measuring. Everything around it is shared, so tracers are compared on
// identical input. New tracers are plugins: see src/tracers/ and cmake/ThesisPlugins.cmake.
class ITracer {
public:
    virtual ~ITracer() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual TracerText text() const = 0;

    // Builds whatever the tracer needs for `scene` and blocks until it is ready. `scene` must
    // outlive the tracer or the next prepare() call. Throws UnsupportedScene if the tracer
    // cannot handle the scene.
    virtual PrepareReport prepare(const GpuScene& scene) = 0;

    // Records commands that read batch.rays and write batch.hits for every ray. The caller
    // places the barriers before and after.
    virtual void trace(VkCommandBuffer cmd, const RayBatch& batch) = 0;

    // GPU memory the tracer currently owns for its scene.
    [[nodiscard]] virtual std::uint64_t memoryBytes() const = 0;

    // False for measurement baselines that pass rays through without searching (the null
    // tracer). They are benchmarked like any tracer but skipped by correctness checks.
    [[nodiscard]] virtual bool findsHits() const { return true; }

    // DIAGNOSIS, never timed. traceCounting() does the same search as trace() and writes the same
    // hits. In addition it writes the work of every ray to work.perRay, and the steps of one ray
    // to work.log.
    // It runs the counting variant of the tracer's shader, which is slower. Call it only when
    // countsWork() returns true.
    [[nodiscard]] virtual bool countsWork() const { return false; }
    virtual void traceCounting(VkCommandBuffer /*cmd*/, const RayBatch& /*batch*/, const WorkOutput& /*work*/)
    {
        throw std::logic_error("This tracer does not count work");
    }
    // True if the counting variant also counts the steps through the search structure
    // (core::RayWork::structureSteps). The hardware tracer cannot: the GPU does not report the
    // nodes of its BVH.
    [[nodiscard]] virtual bool countsStructureSteps() const { return countsWork(); }
    // True if the counting variant measures the GPU clock cycles of each ray
    // (core::RayWork::cycles). False where the clock reads at the start and at the end of a ray
    // are not comparable, e.g. when the GPU moves the ray to another thread in between.
    [[nodiscard]] virtual bool countsCycles() const { return countsWork(); }
    // True if ray i of the list runs on thread i, so that a warp traces neighbouring rays of the
    // list (one warp = rays [k * warp size, (k + 1) * warp size)). The window groups the work of
    // the rays by warp only then. A ray tracing pipeline may place rays on threads in its own way.
    [[nodiscard]] virtual bool warpsFollowRayList() const { return true; }
    // DIAGNOSIS. A step through the search structure from the ray log (kind
    // THESIS_LOG_STRUCTURE_STEP) in a few words. a to d mean what the tracer's shader wrote there.
    [[nodiscard]] virtual std::string describeStructureStep(const core::RayLogEntry& step) const
    {
        return std::format("structure step ({}, {}, {}, {})", step.a, step.b, step.c, step.d);
    }

    // DIAGNOSIS. What the window shows of the search structure, built on the CPU for `input`.
    // Null if the tracer has no structure to show (a baseline).
    [[nodiscard]] virtual std::unique_ptr<IStructureDiagram> diagram(const DiagramInput& /*input*/) const
    {
        return nullptr;
    }
};

using TracerRegistry = core::Registry<ITracer, const TracerEnvironment&>;

} // namespace thesis::tracing
