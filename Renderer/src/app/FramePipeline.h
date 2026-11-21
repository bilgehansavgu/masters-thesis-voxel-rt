#pragma once

// Side: GPU

#include "thesis/core/Camera.h"
#include "thesis/gpu/GpuTimer.h"
#include "thesis/gpu/Image.h"
#include "thesis/resolve/HitShading.h"
#include "thesis/tracing/RayBatch.h"
#include "thesis/tracing/RayLogBuffer.h"
#include "thesis/tracing/Tracer.h"
#include "thesis/workloads/CameraRays.h"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace thesis::app {

// Stage names: shown in Tracy, used as keys in results files.
inline constexpr const char* kStageRayGeneration = "ray_generation";
inline constexpr const char* kStageTrace = "trace";
inline constexpr const char* kStageShading = "shading";

// The per-frame work shared by the window and the benchmark:
//   camera rays → [tracer socket] → hits → shading → image
// Each stage is timed on the GPU. One frame in flight at a time.
class FramePipeline {
public:
    FramePipeline(const gpu::Context& context, const std::filesystem::path& shaderDirectory, VkExtent2D resolution,
                  tracing::RayOrder order);

    // Start of a frame's command buffer: resets the timers, flushes Tracy GPU zones.
    void begin(VkCommandBuffer cmd);
    // Ray generation and tracing, with barriers around the tracer.
    // With `countWork` the tracer runs its counting variant, and work() holds the work of each
    // ray afterwards. That is for diagnosis: the trace time of such a frame is not a result.
    // A tracer that cannot count work is traced as usual. Such a frame also writes the ray log
    // of the ray set with setLoggedRay().
    void trace(VkCommandBuffer cmd, tracing::ITracer& tracer, const core::CameraFrame& camera, bool countWork = false);
    // Fixed-view benchmark: generate the workload once, or load the first condition's exact rays.
    void freezeRays(const core::CameraFrame& camera, std::span<const std::byte> reference);
    // Shading into output() (left in GENERAL layout, ready to be copied by the caller).
    void shade(VkCommandBuffer cmd, const resolve::ShadingParams& params);

    // Call once the frame's commands have finished on the GPU.
    [[nodiscard]] std::vector<gpu::StageTiming> collectTimings();

    // The ray inspector: the ray whose steps the frames that count work write to the ray log.
    void setLoggedRay(std::optional<std::uint32_t> ray) { loggedRay_ = ray; }
    // The steps of the logged ray in the last frame, or nothing if that frame wrote no log. Call
    // once that frame has finished on the GPU.
    [[nodiscard]] std::optional<tracing::RayLog> rayLog() const;

    [[nodiscard]] const tracing::RayBatch& batch() const { return batch_; }
    // core::RayWork for each ray of the last frame, or null if that frame did not count work.
    [[nodiscard]] const gpu::Buffer* work() const { return workCounted_ ? &work_ : nullptr; }
    [[nodiscard]] gpu::StorageImage& output() { return output_; }
    [[nodiscard]] float aspectRatio() const;

private:
    const gpu::Context& context_;
    tracing::RayBatch batch_;
    gpu::Buffer work_; // created when work is counted for the first time
    bool workCounted_ = false;
    std::optional<tracing::RayLogBuffer> log_; // created with work_
    std::optional<std::uint32_t> loggedRay_;
    bool logWritten_ = false;
    bool frozenRays_ = false;
    gpu::StorageImage output_;
    workloads::CameraRays cameraRays_;
    resolve::HitShading shading_;
    gpu::GpuTimer timer_;
};

} // namespace thesis::app
