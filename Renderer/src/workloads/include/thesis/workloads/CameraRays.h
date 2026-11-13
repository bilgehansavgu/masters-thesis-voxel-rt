#pragma once

#include "thesis/core/Camera.h"
#include "thesis/gpu/ComputeKernel.h"
#include "thesis/tracing/RayBatch.h"

#include <vulkan/vulkan.h>

#include <filesystem>

namespace thesis::workloads {

// Ray workload: one primary ray per pixel from a pinhole camera.
// Workloads only fill the ray list; any tracer can consume what they produce.
class CameraRays {
public:
    CameraRays(const gpu::Context& context, const std::filesystem::path& shaderDirectory);

    // Records a compute dispatch that writes batch.rays. The caller places the barrier after.
    void generate(VkCommandBuffer cmd, const core::CameraFrame& camera, const tracing::RayBatch& batch) const;

private:
    gpu::ComputeKernel kernel_;
};

} // namespace thesis::workloads
