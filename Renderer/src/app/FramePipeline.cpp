#include "FramePipeline.h"

// ============================================================================
// READING ORDER: FILE 23B (after FramePipeline.h)
// ============================================================================

#include "thesis/gpu/Barriers.h"
#include "thesis/gpu/Context.h"

namespace thesis::app {

FramePipeline::FramePipeline(const gpu::Context& context, const std::filesystem::path& shaderDirectory,
                             VkExtent2D resolution)
    : context_(context),
      batch_(tracing::RayBatch::create(context, resolution.width, resolution.height)),
      output_(context, resolution, VK_FORMAT_R8G8B8A8_UNORM),
      cameraRays_(context, shaderDirectory),
      shading_(context, shaderDirectory),
      timer_(context)
{}

void FramePipeline::begin(VkCommandBuffer cmd)
{
    TracyVkCollect(context_.tracy(), cmd);
    timer_.reset(cmd);
}

// GPU BASICS: Why are there `computeToComputeBarrier(cmd)` calls between stages?
// In Vulkan, recording two shader dispatches into a command buffer (`vkCmdDispatch`) does NOT
// automatically wait for the first shader to finish before starting the second! By default, the
// GPU overlaps dispatches to keep its cores busy.
// Because `tracer.trace()` reads `batch_.rays` written by `cameraRays_.generate()`, we MUST insert
// a `computeToComputeBarrier` between them so the GPU finishes all writes to `batch_.rays` and
// flushes its caches before the tracer starts reading `batch_.rays`.
//
// C++ REFRESHER: Why are there extra curly braces `{ ... }` around each stage?
// `THESIS_GPU_SCOPE` creates a local RAII object whose constructor writes a GPU "start" timestamp
// and whose destructor writes a GPU "end" timestamp when the closing `}` is reached.
void FramePipeline::trace(VkCommandBuffer cmd, tracing::ITracer& tracer, const core::CameraFrame& camera)
{
    {
        THESIS_GPU_SCOPE(timer_, context_.tracy(), cmd, kStageRayGeneration);
        cameraRays_.generate(cmd, camera, batch_);
    }
    gpu::computeToComputeBarrier(cmd);
    {
        THESIS_GPU_SCOPE(timer_, context_.tracy(), cmd, kStageTrace);
        tracer.trace(cmd, batch_);
    }
    gpu::computeToComputeBarrier(cmd);
}

void FramePipeline::shade(VkCommandBuffer cmd, const resolve::ShadingParams& params)
{
    // Put `output_` into `VK_IMAGE_LAYOUT_GENERAL` so the compute shader can write pixels into it.
    output_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                       VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    THESIS_GPU_SCOPE(timer_, context_.tracy(), cmd, kStageShading);
    shading_.shade(cmd, batch_, output_.view(), params);
}

std::vector<gpu::StageTiming> FramePipeline::collectTimings()
{
    return timer_.resolve();
}

float FramePipeline::aspectRatio() const
{
    return static_cast<float>(batch_.width) / static_cast<float>(batch_.height);
}

} // namespace thesis::app
