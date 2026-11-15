#pragma once

// ============================================================================
// READING ORDER: FILE 16A (The 3-stage per-frame pipeline)
// ============================================================================

#include "thesis/core/Camera.h"
#include "thesis/gpu/GpuTimer.h"
#include "thesis/gpu/Image.h"
#include "thesis/resolve/HitShading.h"
#include "thesis/tracing/RayBatch.h"
#include "thesis/tracing/Tracer.h"
#include "thesis/workloads/CameraRays.h"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <vector>

namespace thesis::app {

// Stage names: shown in Tracy, used as keys in results files.
inline constexpr const char* kStageRayGeneration = "ray_generation";
inline constexpr const char* kStageTrace = "trace";
inline constexpr const char* kStageShading = "shading";

// The per-frame work shared by the interactive window and the headless benchmark:
//   Stage 1 (`kStageRayGeneration`): camera rays -> `batch_.rays`
//   Stage 2 (`kStageTrace`):         `batch_.rays` -> [ITracer socket] -> `batch_.hits`
//   Stage 3 (`kStageShading`):       `batch_.hits` -> `output_` image ( benchmark skips Stage 3 )
// Each stage is timed on the GPU with hardware timestamp queries. One frame in flight at a time.
class FramePipeline {
public:
    FramePipeline(const gpu::Context& context, const std::filesystem::path& shaderDirectory, VkExtent2D resolution);

    // Start of a frame's command buffer: resets the GPU timers, flushes Tracy GPU zones.
    void begin(VkCommandBuffer cmd);
    // Records Stage 1 (ray generation) and Stage 2 (tracing), with memory barriers around the tracer.
    void trace(VkCommandBuffer cmd, tracing::ITracer& tracer, const core::CameraFrame& camera);
    // Records Stage 3 (shading into `output()`, left in GENERAL layout, ready to be copied to the window).
    void shade(VkCommandBuffer cmd, const resolve::ShadingParams& params);

    // Call once the frame's commands have finished executing on the GPU to read the stage times in ms.
    [[nodiscard]] std::vector<gpu::StageTiming> collectTimings();

    [[nodiscard]] const tracing::RayBatch& batch() const { return batch_; }
    [[nodiscard]] gpu::StorageImage& output() { return output_; }
    [[nodiscard]] float aspectRatio() const;

private:
    const gpu::Context& context_;
    tracing::RayBatch batch_;
    gpu::StorageImage output_;
    workloads::CameraRays cameraRays_;
    resolve::HitShading shading_;
    gpu::GpuTimer timer_;
};

} // namespace thesis::app
