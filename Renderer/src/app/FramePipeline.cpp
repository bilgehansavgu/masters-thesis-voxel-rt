#include "FramePipeline.h"

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
