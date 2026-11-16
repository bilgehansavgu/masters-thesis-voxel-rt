#pragma once

#include "thesis/core/Camera.h"
#include "thesis/core/GpuTypes.h"
#include "thesis/gpu/Context.h"
#include "thesis/tracing/GpuScene.h"
#include "thesis/tracing/RayBatch.h"
#include "thesis/tracing/Tracer.h"
#include "thesis/workloads/CameraRays.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace thesis::tests {

// A GPU context for a test: the validation layer is on.
[[nodiscard]] gpu::ContextOptions testContextOptions(const char* name);

// Every registered tracer that finds hits (baselines such as the null tracer are skipped),
// prepared for `scene`.
struct HitFindingTracers {
    std::vector<std::string> names;
    std::vector<std::unique_ptr<tracing::ITracer>> tracers;
};
[[nodiscard]] HitFindingTracers makeHitFindingTracers(const gpu::Context& context, const tracing::GpuScene& scene);

// A ray batch that holds `rays` (width = number of rays, height 1).
[[nodiscard]] tracing::RayBatch uploadRays(const gpu::Context& context, std::span<const core::Ray> rays);

// Fills batch.rays with the rays of `camera` and returns them.
[[nodiscard]] std::vector<core::Ray> generateRays(const gpu::Context& context, const workloads::CameraRays& cameraRays,
                                                  const core::CameraFrame& camera, const tracing::RayBatch& batch);

// Traces the rays that are in batch.rays and returns the hits.
[[nodiscard]] std::vector<core::Hit> traceRays(const gpu::Context& context, tracing::ITracer& tracer,
                                               const tracing::RayBatch& batch);

// The same with the counting variant of the tracer. `work` gets the work of each ray.
[[nodiscard]] std::vector<core::Hit> traceRaysCounting(const gpu::Context& context, tracing::ITracer& tracer,
                                                       const tracing::RayBatch& batch,
                                                       std::vector<core::RayWork>& work);

// Camera positions around a scene, as changes to the camera that shows the whole scene.
struct Viewpoint {
    float yaw;
    float pitch;
    float distanceScale;
};
[[nodiscard]] core::CameraFrame cameraFrame(const core::Bounds& bounds, const Viewpoint& viewpoint, float aspectRatio);

} // namespace thesis::tests
