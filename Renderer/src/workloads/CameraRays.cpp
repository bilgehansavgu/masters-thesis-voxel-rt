#include "thesis/workloads/CameraRays.h"

#include "thesis/tracing/Bindings.h"

#include <glm/vec4.hpp>

namespace thesis::workloads {
namespace {

constexpr std::uint32_t kGroupSize = 8; // matches [numthreads(8, 8, 1)] in camera_rays.slang

struct CameraParams {
    glm::vec4 origin;
    glm::vec4 forward;
    glm::vec4 right;
    glm::vec4 up;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t pad0;
    std::uint32_t pad1;
};
static_assert(sizeof(CameraParams) == 80);

} // namespace

CameraRays::CameraRays(const gpu::Context& context, const std::filesystem::path& shaderDirectory)
    : kernel_(context, gpu::KernelDescription{shaderDirectory / "camera_rays.spv",
                                              {{tracing::kRayBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}},
                                              sizeof(CameraParams)})
{}

void CameraRays::generate(VkCommandBuffer cmd, const core::CameraFrame& camera, const tracing::RayBatch& batch) const
{
    const CameraParams params{glm::vec4(camera.origin, 1.0f),
                              glm::vec4(camera.forward, 0.0f),
                              glm::vec4(camera.right, 0.0f),
                              glm::vec4(camera.up, 0.0f),
                              batch.width,
                              batch.height,
                              0,
                              0};
    gpu::KernelResources resources;
    resources.buffer(tracing::kRayBinding, batch.rays);
    kernel_.dispatch(cmd, resources, gpu::groupCount(batch.width, kGroupSize),
                     gpu::groupCount(batch.height, kGroupSize), 1, params);
}

} // namespace thesis::workloads
