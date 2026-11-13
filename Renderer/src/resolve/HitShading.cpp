#include "thesis/resolve/HitShading.h"

#include "thesis/shared/Bindings.h"

namespace thesis::resolve {
namespace {

constexpr std::uint32_t kGroupSize = 8; // matches [numthreads(8, 8, 1)] in shade.slang
constexpr std::uint32_t kHitBinding = THESIS_BINDING_SHADE_HITS;
constexpr std::uint32_t kOutputBinding = THESIS_BINDING_SHADE_OUTPUT;

struct ShadeParams {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t mode;
    std::uint32_t pad0;
    float distanceNear;
    float distanceFar;
    float pad1;
    float pad2;
};

} // namespace

HitShading::HitShading(const gpu::Context& context, const std::filesystem::path& shaderDirectory)
    : kernel_(context, gpu::KernelDescription{shaderDirectory / "shade.spv",
                                              {{kHitBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                                               {kOutputBinding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}},
                                              sizeof(ShadeParams)})
{}

void HitShading::shade(VkCommandBuffer cmd, const tracing::RayBatch& batch, VkImageView output,
                       const ShadingParams& params) const
{
    const ShadeParams push{
        batch.width, batch.height, static_cast<std::uint32_t>(params.mode), 0, params.distanceNear, params.distanceFar,
        0.0f,        0.0f};
    gpu::KernelResources resources;
    resources.buffer(kHitBinding, batch.hits).storageImage(kOutputBinding, output);
    kernel_.dispatch(cmd, resources, gpu::groupCount(batch.width, kGroupSize),
                     gpu::groupCount(batch.height, kGroupSize), 1, push);
}

} // namespace thesis::resolve
