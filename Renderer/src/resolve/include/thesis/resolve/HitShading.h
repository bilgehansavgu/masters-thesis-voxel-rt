#pragma once

// Side: GPU

#include "thesis/gpu/Buffer.h"
#include "thesis/gpu/ComputeKernel.h"
#include "thesis/resolve/ShadingParams.h"
#include "thesis/tracing/RayBatch.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>

namespace thesis::resolve {

// Turns the hit list into an image. Shared by all tracers, so they are judged on identical output.
class HitShading {
public:
    HitShading(const gpu::Context& context, const std::filesystem::path& shaderDirectory);

    // `output` must be an RGBA8 storage image of batch.width × batch.height in GENERAL layout.
    // `work` (core::RayWork for each ray) is needed for the work views. Without it they fall
    // back to the shaded view.
    void shade(VkCommandBuffer cmd, const tracing::RayBatch& batch, VkImageView output, const ShadingParams& params,
               const gpu::Buffer* work = nullptr) const;

private:
    gpu::ComputeKernel kernel_;
    gpu::ComputeKernel workKernel_;
};

} // namespace thesis::resolve
