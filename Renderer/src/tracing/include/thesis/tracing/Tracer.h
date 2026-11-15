#pragma once

// ============================================================================
// READING ORDER: FILE 14 (The central `ITracer` interface — the "Tracer Socket")
// ============================================================================

#include "thesis/core/Registry.h"
#include "thesis/tracing/GpuScene.h"
#include "thesis/tracing/RayBatch.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace thesis::gpu {
class Context;
}

namespace thesis::tracing {

// What every tracer receives when it is constructed: a reference to the Vulkan GPU context
// and the folder where compiled `.spv` shaders live.
struct TracerEnvironment {
    const gpu::Context& gpu;
    std::filesystem::path shaderDirectory;
};

// What building a tracer's search structure (in `prepare()`) cost in GPU time and memory.
struct PrepareReport {
    double gpuMilliseconds = 0.0;   // GPU time spent building the search structure (e.g., BVH build)
    std::uint64_t memoryBytes = 0;  // persistent GPU memory the tracer keeps for this scene
    std::uint64_t scratchBytes = 0; // temporary GPU memory needed only during the build
};

// THE TRACER SOCKET. One rule: a list of rays goes in, a list of hits comes out.
//
// C++ REFRESHER: Abstract Base Class (Interface)
// In C++, a class with `= 0` on its `virtual` functions is an interface (cannot be instantiated
// directly). Any concrete tracer (`HardwareTracer`, `SoftwareTracer`, `NullTracer`) inherits from
// `ITracer` and implements these functions using the `override` keyword.
// Callers hold a `std::unique_ptr<ITracer>` or `ITracer&` and call `tracer.trace(cmd, batch)`
// without knowing or caring which tracer is plugged in!
//
// A tracer owns how rays find primitives (its search structure); it does not own the rays,
// the shading, or the measuring. Everything around it is shared, so tracers are compared on
// identical input. New tracers are plugins: see src/tracers/ and cmake/ThesisPlugins.cmake.
class ITracer {
public:
    // Virtual destructor: required in C++ whenever a class has virtual functions so deleting a
    // derived tracer through an `ITracer*` calls the derived class's destructor properly.
    virtual ~ITracer() = default;

    // Short identifier such as "hardware", "software", or "null".
    [[nodiscard]] virtual std::string_view name() const = 0;

    // Called once when a scene is loaded: builds whatever search structure (e.g., BVH or octree)
    // and shader pipeline the tracer needs for `scene`, and waits until it is ready on the GPU.
    // `scene` must outlive the tracer or the next `prepare()` call.
    virtual PrepareReport prepare(const GpuScene& scene) = 0;

    // Called every frame: records GPU commands into `cmd` that read `batch.rays` and write
    // `batch.hits` for every ray. The caller (`FramePipeline::trace`) places memory barriers
    // before and after this call.
    virtual void trace(VkCommandBuffer cmd, const RayBatch& batch) = 0;

    // Persistent GPU memory (in bytes) the tracer currently owns for its search structure.
    [[nodiscard]] virtual std::uint64_t memoryBytes() const = 0;

    // Returns `false` for measurement baselines that pass rays through without searching (like
    // `NullTracer`). They are benchmarked like any tracer, but skipped by correctness tests.
    [[nodiscard]] virtual bool findsHits() const { return true; }
};

// Maps a tracer name ("hardware", "software", "null") to a factory function that creates it.
using TracerRegistry = core::Registry<ITracer, const TracerEnvironment&>;

} // namespace thesis::tracing
