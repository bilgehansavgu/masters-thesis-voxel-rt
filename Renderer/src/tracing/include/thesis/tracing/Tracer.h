// ============================================================================
// READING ORDER: FILE 20C (after RayBatch.h and GpuScene.h)
// ============================================================================
// PURPOSE:
// Defines the central C++ interface (`ITracer`) that every ray tracer plugin
// (`null`, `software`, `hardware`) must implement.
//
// C++ SYNTAX NOTE:
// - `class UnsupportedScene : public std::runtime_error`:
//   `using std::runtime_error::runtime_error;` is an "inheriting constructor"
//   declaration (C++11). It tells the compiler to automatically expose all of
//   `std::runtime_error`'s constructors on `UnsupportedScene` without writing
//   boilerplate forwarding constructors.
// ============================================================================

#pragma once

#include "thesis/core/Registry.h"
#include "thesis/tracing/GpuScene.h"
#include "thesis/tracing/RayBatch.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string_view>

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
    std::uint64_t memoryBytes = 0;  // memory the tracer keeps for this scene
    std::uint64_t scratchBytes = 0; // temporary memory the build needed
};

// Thrown by ITracer::prepare() when a tracer cannot handle a scene (for example too many
// primitives for a tracer without a search structure, such as `SoftwareTracer` when
// `primitiveCount() > 30000`). The message says why.
class UnsupportedScene : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
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
};

using TracerRegistry = core::Registry<ITracer, const TracerEnvironment&>;

} // namespace thesis::tracing
